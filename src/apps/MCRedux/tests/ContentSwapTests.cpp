#include "stdafx.h"
#include "MCTest.h"
#include "gui/MCGuiStartup.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"

#include <random>

namespace
{
    using MCContentSwap::MCOutcome;

    /// <summary>The machine stopping: thrown by the disk at the step a test picks.</summary>
    struct MCPowerCut
    {
    };

    /// <summary>
    /// A disk in memory that keeps, like a real one, what was flushed and what's only in its cache: a file's bytes
    /// are safe once it's flushed, a move, a new file or a deletion once every folder it changed is flushed after it.
    /// <see cref="Cut"/> stops the machine: the safe changes stay, and of the others any subset the test picks (as a
    /// system may write its cache in any order), each move whole or not at all.
    /// </summary>
    class MCCrashDisk final : public MCUpdateDisk
    {
    public:
        struct MCFile
        {
            std::string Bytes;
            int64_t Written = 0;
        };

        using MCFiles = std::map<std::string, MCFile>;

        explicit MCCrashDisk(MCFiles files) : _Safe(files), _Seen(std::move(files)) {}

        /// <summary>What a program sees now.</summary>
        const MCFiles& Files() const { return _Seen; }

        /// <summary>Stops the machine at the <paramref name="step"/>th change from now (none: never).</summary>
        void CutAt(std::optional<int> step) { _CutAt = step; }

        /// <summary>The changes since the disk was made or last cut.</summary>
        int Changes() const { return _Changes; }

        /// <summary>The file at <paramref name="path"/> is held open: moving or replacing it fails (empty: none).</summary>
        void HoldOpen(std::string path) { _Held = std::move(path); }

        /// <summary>The folder flush right after a file moves into <paramref name="path"/> fails, once.</summary>
        void FailFlushAfterMoveTo(std::string path) { _FailFlushAfter = std::move(path); }

        /// <summary>
        /// The machine stops and starts again: the safe changes stay, and each unsafe one stays when
        /// <paramref name="keep"/> says so (by its order), applied in order, skipping one whose file is gone.
        /// </summary>
        void Cut(const std::function<bool(size_t)>& keep)
        {
            MCFiles files = _Safe;

            for (size_t i = 0; i < _Pending.size(); ++i)
            {
                MCChange& change = _Pending[i];

                if (change.Safe() || keep(i))
                {
                    Apply(files, change);
                }
            }

            _Safe = files;
            _Seen = std::move(files);
            _Pending.clear();
            _Changes = 0;
            _CutAt.reset();
        }

        MCStat Stat(const std::filesystem::path& path) override
        {
            const std::string key = Key(path);

            if (const auto found = _Seen.find(key); found != _Seen.end())
            {
                return {.Exists = true,
                        .Folder = false,
                        .Size = found->second.Bytes.size(),
                        .Written = found->second.Written};
            }

            const bool folder = std::ranges::any_of(_Seen, [&](const auto& entry) { return Inside(entry.first, key); });
            return {.Exists = folder, .Folder = folder};
        }

        std::expected<MCSha256::MCDigest, std::string> Hash(const std::filesystem::path& path) override
        {
            const auto found = _Seen.find(Key(path));

            if (found == _Seen.end())
            {
                return std::unexpected(Key(path) + " is missing");
            }

            return MCSha256::Of(found->second.Bytes);
        }

        std::expected<std::string, std::string> ReadText(const std::filesystem::path& path) override
        {
            const auto found = _Seen.find(Key(path));

            if (found == _Seen.end())
            {
                return std::unexpected(Key(path) + " is missing");
            }

            return found->second.Bytes;
        }

        bool MakeFolders(const std::filesystem::path&) override { return true; }

        bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            Step();

            // A held file can't be moved or replaced; with nothing at its path, nothing holds it.
            if (!_Held.empty() && (Key(from) == _Held || (Key(to) == _Held && _Seen.contains(_Held))))
            {
                return false;
            }

            _LastMovedTo = Key(to);
            return Record({MCKind::Rename, Key(from), Key(to), {}, {Parent(from), Parent(to)}, true});
        }

        bool Remove(const std::filesystem::path& path) override
        {
            Step();
            return Record({MCKind::Remove, Key(path), {}, {}, {Parent(path)}, true});
        }

        // Its folders are only its files' paths: one holding nothing is gone already.
        bool RemoveIfEmpty(const std::filesystem::path&) override { return true; }

        bool WriteFlushed(const std::filesystem::path& path, std::string_view bytes) override
        {
            Step();
            return Record({MCKind::Write, Key(path), {}, std::string(bytes), {Parent(path)}, true});
        }

        bool FlushFile(const std::filesystem::path&) override
        {
            Step();
            return true;
        }

        bool FlushFolder(const std::filesystem::path& path) override
        {
            Step();

            if (!_FailFlushAfter.empty() && _FailFlushAfter == _LastMovedTo)
            {
                _FailFlushAfter.clear();
                return false;
            }

            const std::string folder = Key(path);

            for (MCChange& change : _Pending)
            {
                change.Folders.erase(folder);
            }

            return true;
        }

    private:
        enum class MCKind : uint8_t
        {
            Write,
            Rename,
            Remove,
        };

        struct MCChange
        {
            MCKind Kind;
            std::string Path;
            std::string To;
            std::string Bytes;
            /// <summary>The folders still to be flushed before it's safe.</summary>
            std::set<std::string> Folders;
            bool BytesFlushed = false;

            bool Safe() const { return Folders.empty() && BytesFlushed; }
        };

        static std::string Key(const std::filesystem::path& path) { return path.lexically_normal().generic_string(); }
        static std::string Parent(const std::filesystem::path& path) { return Key(path.parent_path()); }
        static bool Inside(const std::string& path, const std::string& folder)
        {
            return path.size() > folder.size() && path.starts_with(folder) && path[folder.size()] == '/';
        }

        /// <summary>Counts a change, and stops the machine at the step picked.</summary>
        void Step()
        {
            if (_CutAt && _Changes >= *_CutAt)
            {
                throw MCPowerCut{};
            }

            ++_Changes;
        }

        /// <summary>Applies a change to <paramref name="files"/>; false when it can't be (a move's file is gone).</summary>
        bool Apply(MCFiles& files, const MCChange& change)
        {
            switch (change.Kind)
            {
                case MCKind::Write:
                {
                    files[change.Path] = {change.Bytes, ++_Clock};
                    return true;
                }
                case MCKind::Rename:
                {
                    const auto found = files.find(change.Path);

                    if (found == files.end())
                    {
                        return false;
                    }

                    MCFile file = std::move(found->second);
                    files.erase(found);
                    files[change.To] = std::move(file);
                    return true;
                }
                case MCKind::Remove:
                {
                    std::erase_if(files, [&](const auto& entry)
                                  { return entry.first == change.Path || Inside(entry.first, change.Path); });
                    return true;
                }
            }

            return false;
        }

        bool Record(MCChange change)
        {
            if (!Apply(_Seen, change))
            {
                return false;
            }

            _Pending.push_back(std::move(change));
            return true;
        }

        MCFiles _Safe;
        MCFiles _Seen;
        std::vector<MCChange> _Pending;
        std::optional<int> _CutAt;
        int _Changes = 0;
        int64_t _Clock = 1000;
        std::string _Held;
        std::string _FailFlushAfter;
        std::string _LastMovedTo;
    };

    const std::filesystem::path Game = "/game";

    std::string GamePath(std::string_view relative)
    {
        return (Game / MCFileSystem::FromUtf8(relative)).generic_string();
    }

    /// <summary>
    /// An update of six files: one replaced, one added in a new folder, one removed, one replaced in HD, and two an
    /// update didn't place (one replaced, one removed: both kept in Redux backup); with each old file, the staged new
    /// ones and ready.json on the disk, all safe.
    /// </summary>
    struct MCUpdate
    {
        MCUpdate()
        {
            Add("ART.FST", "art v1", "art v2");
            Add("data/missions/new.fit", std::nullopt, "a new mission");
            Add("data/old.fit", "removed in v2", std::nullopt);
            Add("HD/terrain.hda", "terrain v1, bigger", "terrain v2");
            Add("data/mod.fit", "the folder's own mod", "mod v2", true);
            Add("data/dropped/own.fit", "the folder's own, dropped", std::nullopt, true);
            Before.Release = 1;
            After.Release = 2;
            After.List = MCSha256::Of("list 2");
            // What MarkReady gives a journal: the record says the swap is done once it's this one.
            After.Swap = "a swap of the tests";
            Before.Sets = After.Sets = {"game", "hd"};
            Files[GamePath(MCInstalled::RecordPath)] = {MCInstalled::Text(Before), 1};
            Files[GamePath(MCContentSwap::ReadyFile)] = {MCContentSwap::JournalText(Journal()), 1};
        }

        void Add(std::string path, std::optional<std::string> old, std::optional<std::string> now, bool backup = false)
        {
            MCContentSwap::MCMove move{.Path = path, .Backup = backup};
            const std::string folded = MCContentList::FoldedPath(path);

            if (old)
            {
                move.Old = MCSha256::Of(*old);
                move.OldSize = old->size();
                move.OldWritten = ++Clock;
                Files[GamePath(path)] = {*old, move.OldWritten};
                Before.Files[folded] = {.Path = path,
                                        .Set = "game",
                                        .Sha256 = *move.Old,
                                        .Size = old->size(),
                                        .Written = move.OldWritten,
                                        .Placed = !backup};
            }

            if (now)
            {
                move.New = MCSha256::Of(*now);
                move.NewSize = now->size();
                Files[GamePath(std::string(MCContentSwap::StagedFolder) + "/" + path)] = {*now, ++Clock};
                After.Files[folded] = {.Path = path, .Set = "game", .Sha256 = *move.New, .Size = now->size()};
            }

            Moves.push_back(std::move(move));
            Old[path] = old;
            New[path] = now;
        }

        MCContentSwap::MCJournal Journal() const { return {Moves, After}; }

        /// <summary>Whether the files a program sees are wholly <paramref name="set"/>.</summary>
        static bool WhichSet(const MCCrashDisk::MCFiles& files,
                             const std::map<std::string, std::optional<std::string>>& set)
        {
            for (const auto& [path, bytes] : set)
            {
                const auto found = files.find(GamePath(path));

                if (bytes.has_value() != (found != files.end()) || (bytes && found->second.Bytes != *bytes))
                {
                    return false;
                }
            }

            return true;
        }

        /// <summary>Redux backup's files and their bytes.</summary>
        static std::map<std::string, std::string> InBackup(const MCCrashDisk::MCFiles& files)
        {
            const std::string backup = GamePath(MCContentList::BackupFolder) + "/";
            std::map<std::string, std::string> kept;

            for (const auto& [path, file] : files)
            {
                if (path.starts_with(backup))
                {
                    kept[path] = file.Bytes;
                }
            }

            return kept;
        }

        /// <summary>Whether Redux backup holds the old files an update didn't place, each once, and nothing else.</summary>
        bool BackupKept(const MCCrashDisk::MCFiles& files) const
        {
            std::map<std::string, std::string> wanted;

            for (const MCContentSwap::MCMove& move : Moves)
            {
                if (move.Backup && Old.at(move.Path))
                {
                    wanted[GamePath(std::string(MCContentList::BackupFolder) + "/" + move.Path)] = *Old.at(move.Path);
                }
            }

            return InBackup(files) == wanted;
        }

        bool IsNew(const MCCrashDisk::MCFiles& files) const
        {
            if (!WhichSet(files, New) || !BackupKept(files) || files.contains(GamePath(MCContentSwap::JournalFile)) ||
                files.contains(GamePath(MCContentSwap::ReadyFile)))
            {
                return false;
            }

            const auto record = MCInstalled::FromText(files.at(GamePath(MCInstalled::RecordPath)).Bytes);
            return record && record->Release == 2 && record->Files.size() == After.Files.size() &&
                   !std::ranges::any_of(files,
                                        [](const auto& entry)
                                        {
                                            return entry.first.find("Redux update/staged") != std::string::npos ||
                                                   entry.first.find("Redux update/replaced") != std::string::npos;
                                        });
        }

        /// <summary>The old set in place, the update staged again and ready for the next start.</summary>
        bool IsOldAndReady(const MCCrashDisk::MCFiles& files) const
        {
            if (!WhichSet(files, Old) || !InBackup(files).empty() ||
                !files.contains(GamePath(MCContentSwap::ReadyFile)))
            {
                return false;
            }

            for (const auto& [path, bytes] : New)
            {
                const auto staged = files.find(GamePath(std::string(MCContentSwap::StagedFolder) + "/" + path));

                if (bytes && (staged == files.end() || staged->second.Bytes != *bytes))
                {
                    return false;
                }
            }

            const auto record = MCInstalled::FromText(files.at(GamePath(MCInstalled::RecordPath)).Bytes);
            return record && record->Release == 1;
        }

        std::vector<MCContentSwap::MCMove> Moves;
        MCInstalled::MCRecord Before;
        MCInstalled::MCRecord After;
        MCCrashDisk::MCFiles Files;
        std::map<std::string, std::optional<std::string>> Old;
        std::map<std::string, std::optional<std::string>> New;
        int64_t Clock = 100;
    };
}

TEST_CASE("content swap: a ready update swaps in; with nothing ready nothing happens")
{
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    CHECK(MCContentSwap::Waiting(disk, Game));
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game);
    CHECK(result.Outcome == MCOutcome::Swapped);
    CHECK_EQ(result.Error, std::string());
    CHECK(update.IsNew(disk.Files()));
    CHECK(!MCContentSwap::Waiting(disk, Game));
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Nothing);

    // The new files' times are recorded, so the next plan needn't hash them.
    const auto record = MCInstalled::FromText(disk.Files().at(GamePath(MCInstalled::RecordPath)).Bytes);
    REQUIRE(record.has_value());
    CHECK_EQ(record->Files.at("ART.FST").Written, disk.Files().at(GamePath("ART.FST")).Written);
}

TEST_CASE("content swap: the machine stopping at any step, any unflushed changes lost, still ends wholly new")
{
    const MCUpdate update;
    MCCrashDisk measure(update.Files);
    REQUIRE(MCContentSwap::Run(measure, Game).Outcome == MCOutcome::Swapped);
    const int steps = measure.Changes();
    CHECK(steps > 10);
    constexpr int SubsetsPerStep = 12;
    std::mt19937 random(20261104);
    int cuts = 0;

    for (int step = 0; step <= steps; ++step)
    {
        for (int subset = 0; subset < SubsetsPerStep; ++subset)
        {
            MCCrashDisk disk(update.Files);
            disk.CutAt(step);
            bool cut = false;

            try
            {
                MCContentSwap::Run(disk, Game);
            }
            catch (const MCPowerCut&)
            {
                cut = true;
            }

            CHECK_EQ(cut, step < steps);
            // Every unflushed change lost, every one kept, then random halves.
            const uint32_t seed = random();
            disk.Cut([&](size_t i)
                     { return subset == 0   ? false
                              : subset == 1 ? true
                                            : ((seed >> (i % 32)) & 1) != 0; });
            ++cuts;

            // Whatever was left, a next start that can't finish (a file held open) leaves the old set whole, or the
            // new one; and with the file let go the start after finishes it. No old file was lost along the way.
            MCCrashDisk held = disk;
            held.HoldOpen(GamePath("HD/terrain.hda"));
            const MCContentSwap::MCResult blocked = MCContentSwap::Run(held, Game);
            const bool whole = (blocked.Outcome == MCOutcome::PutBack && update.IsOldAndReady(held.Files())) ||
                               (blocked.Outcome == MCOutcome::Swapped && update.IsNew(held.Files())) ||
                               (blocked.Outcome == MCOutcome::Nothing && update.IsNew(held.Files()));

            if (!whole)
            {
                FAIL_CHECK(std::format("step {} subset {}: with a file held open the next start {} ({}) left a mix",
                                       step, subset, static_cast<int>(blocked.Outcome), blocked.Error));
            }

            held.HoldOpen({});

            if (MCContentSwap::Run(held, Game).Outcome == MCOutcome::Stuck || !update.IsNew(held.Files()))
            {
                FAIL_CHECK(std::format("step {} subset {}: let go, the start after didn't finish", step, subset));
            }

            // And a next start that can finish does (or finds it finished).
            const bool done = update.IsNew(disk.Files());
            const MCContentSwap::MCResult again = MCContentSwap::Run(disk, Game);

            if (!(again.Outcome == MCOutcome::Swapped || (done && again.Outcome == MCOutcome::Nothing)))
            {
                FAIL_CHECK(std::format("step {} subset {}: the next start {} ({})", step, subset,
                                       static_cast<int>(again.Outcome), again.Error));
                continue;
            }

            if (!update.IsNew(disk.Files()))
            {
                FAIL_CHECK(std::format("step {} subset {}: not wholly the new set after the next start", step, subset));
            }
        }
    }

    CHECK_EQ(cuts, (steps + 1) * SubsetsPerStep);
}

TEST_CASE("content swap: a file held open puts the old set back and keeps the update for the next start")
{
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    disk.HoldOpen(GamePath("HD/terrain.hda"));
    const MCContentSwap::MCResult held = MCContentSwap::Run(disk, Game);
    CHECK(held.Outcome == MCOutcome::PutBack);
    CHECK(held.Error.find("HD/terrain.hda") != std::string::npos);
    CHECK(update.IsOldAndReady(disk.Files()));

    // Let go, the next start swaps it in.
    disk.HoldOpen({});
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK(update.IsNew(disk.Files()));
}

TEST_CASE("content swap: the machine stopping while the old set is put back still ends whole")
{
    const MCUpdate update;
    MCCrashDisk measure(update.Files);
    measure.HoldOpen(GamePath("HD/terrain.hda"));
    REQUIRE(MCContentSwap::Run(measure, Game).Outcome == MCOutcome::PutBack);
    const int steps = measure.Changes();

    for (int step = 0; step < steps; ++step)
    {
        for (const bool keepAll : {false, true})
        {
            MCCrashDisk disk(update.Files);
            disk.HoldOpen(GamePath("HD/terrain.hda"));
            disk.CutAt(step);

            try
            {
                MCContentSwap::Run(disk, Game);
            }
            catch (const MCPowerCut&)
            {
            }

            disk.Cut([&](size_t) { return keepAll; });
            disk.HoldOpen({});
            const MCContentSwap::MCResult again = MCContentSwap::Run(disk, Game);

            if (again.Outcome != MCOutcome::Swapped || !update.IsNew(disk.Files()))
            {
                FAIL_CHECK(std::format("step {} keep {}: {} ({})", step, keepAll, static_cast<int>(again.Outcome),
                                       again.Error));
            }
        }
    }
}

TEST_CASE("content swap: past a lobby's deadline the old set goes back, but a committed swap still finishes")
{
    const MCUpdate update;
    const auto now = std::chrono::steady_clock::now();
    const auto passed = now - std::chrono::seconds(1);

    // In time, it swaps as without a deadline.
    MCCrashDisk inTime(update.Files);
    CHECK(MCContentSwap::Run(inTime, Game, now + MCContentSwap::LobbyStartTime).Outcome == MCOutcome::Swapped);
    CHECK(update.IsNew(inTime.Files()));

    // Too late, nothing moves in: the old set stays, the update ready for the next start, which swaps it.
    MCCrashDisk late(update.Files);
    const MCContentSwap::MCResult tooLate = MCContentSwap::Run(late, Game, passed);
    CHECK(tooLate.Outcome == MCOutcome::PutBack);
    CHECK(tooLate.Error.find("lobby") != std::string::npos);
    CHECK(update.IsOldAndReady(late.Files()));
    CHECK(MCContentSwap::Run(late, Game).Outcome == MCOutcome::Swapped);
    CHECK(update.IsNew(late.Files()));

    // Stopped after the record was written: the swap is committed, so a start past its deadline only finishes it.
    MCCrashDisk measure(update.Files);
    REQUIRE(MCContentSwap::Run(measure, Game).Outcome == MCOutcome::Swapped);
    bool committedTried = false;

    for (int step = 0; step < measure.Changes() && !committedTried; ++step)
    {
        MCCrashDisk disk(update.Files);
        disk.CutAt(step);

        try
        {
            MCContentSwap::Run(disk, Game);
        }
        catch (const MCPowerCut&)
        {
        }

        disk.Cut([](size_t) { return true; });
        const auto record = MCInstalled::FromText(disk.Files().at(GamePath(MCInstalled::RecordPath)).Bytes);

        if (!record || record->Swap != update.After.Swap)
        {
            continue;
        }

        committedTried = true;
        CHECK(disk.Files().contains(GamePath(MCContentSwap::JournalFile)));
        CHECK(MCContentSwap::Run(disk, Game, passed).Outcome == MCOutcome::Swapped);
        CHECK(update.IsNew(disk.Files()));
    }

    CHECK(committedTried);
}

TEST_CASE("content swap: a lobby's start, which gets the deadline, is one with -host or -join")
{
    CHECK(StartsInLobby("-join 192.168.1.5 -player LAPTOP"));
    CHECK(StartsInLobby("-player MAC --HOST"));
    CHECK(StartsInLobby("-host"));
    CHECK(!StartsInLobby(""));
    CHECK(!StartsInLobby("-mission 3"));
    CHECK(!StartsInLobby("-player \"-host and friends\""));
}

TEST_CASE("content swap: a file the player changed since staging is replaced too, and kept in Redux backup")
{
    MCUpdate update;
    update.Files[GamePath("ART.FST")] = {"the player's art", 5};
    update.Files[GamePath("data/missions/new.fit")] = {"the player's own mission", 6};
    MCCrashDisk disk(update.Files);
    REQUIRE(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK(update.WhichSet(disk.Files(), update.New));
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/ART.FST")).Bytes, std::string("the player's art"));
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/data/missions/new.fit")).Bytes,
             std::string("the player's own mission"));
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/data/mod.fit")).Bytes, std::string("the folder's own mod"));
    CHECK(!disk.Files().contains(GamePath("Redux backup/HD/terrain.hda")));
    const auto record = MCInstalled::FromText(disk.Files().at(GamePath(MCInstalled::RecordPath)).Bytes);
    REQUIRE(record.has_value());
    CHECK(record->Files.at("ART.FST").Placed);
    CHECK(record->Files.at("ART.FST").Sha256 == MCSha256::Of("art v2"));
    CHECK(record->Files.at("DATA/MISSIONS/NEW.FIT").Placed);
}

TEST_CASE("content swap: a later copy of a path already in Redux backup is kept beside it, numbered")
{
    MCUpdate update;
    update.Files[GamePath("Redux backup/data/mod.fit")] = {"kept by an earlier update", 7};
    update.Files[GamePath("Redux backup/data/mod (2).fit")] = {"kept by the one after", 8};
    MCCrashDisk disk(update.Files);
    REQUIRE(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/data/mod.fit")).Bytes, std::string("kept by an earlier update"));
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/data/mod (2).fit")).Bytes, std::string("kept by the one after"));
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/data/mod (3).fit")).Bytes, std::string("the folder's own mod"));
}

TEST_CASE("content swap: a folder put in a file's place since staging puts the old set back")
{
    MCUpdate update;
    update.Files.erase(GamePath("data/mod.fit"));
    update.Files[GamePath("data/mod.fit/inside.txt")] = {"a folder's file", 9};
    MCCrashDisk disk(update.Files);
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game);
    CHECK(result.Outcome == MCOutcome::PutBack);
    CHECK(result.Error.find("data/mod.fit: a folder is in its place") != std::string::npos);
    CHECK(disk.Files().contains(GamePath(MCContentSwap::ReadyFile)));
    CHECK_EQ(disk.Files().at(GamePath("ART.FST")).Bytes, std::string("art v1"));
}

TEST_CASE("content swap: the record moved into place but its folder's flush failing is committed: the old files aren't "
          "put back under it")
{
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    disk.FailFlushAfterMoveTo(GamePath(MCInstalled::RecordPath));
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game);
    CHECK(result.Outcome == MCOutcome::Swapped);
    CHECK(result.Error.find("record") != std::string::npos);
    CHECK(update.WhichSet(disk.Files(), update.New));

    // The next start finishes the clearing.
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK(update.IsNew(disk.Files()));
}

TEST_CASE("content swap: the disk's writes are durable and replace a file whole")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "mc_content_swap_disk";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(WriteDurably(*disk, folder / "a" / "b.json", "first"));
    REQUIRE(WriteDurably(*disk, folder / "a" / "b.json", "second"));
    CHECK_EQ(*disk->ReadText(folder / "a" / "b.json"), std::string("second"));
    CHECK(!disk->Stat(folder / "a" / "b.json.writing").Exists);
    CHECK(disk->Stat(folder / "a").Folder);
    CHECK_EQ(disk->Stat(folder / "a" / "b.json").Size, uint64_t{6});
    CHECK(*disk->Hash(folder / "a" / "b.json") == MCSha256::Of("second"));
    REQUIRE(disk->Rename(folder / "a" / "b.json", folder / "c.json"));
    CHECK(disk->FlushFolder(folder / "a"));
    CHECK(disk->FlushFile(folder / "c.json"));
    CHECK(!disk->Stat(folder / "a" / "b.json").Exists);
    CHECK(disk->Remove(folder));
    CHECK(!disk->Stat(folder).Exists);
}
