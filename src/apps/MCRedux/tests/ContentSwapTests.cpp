#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCTest.h"
#include "gui/MCGuiStartup.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
#include "update/MCContentRemoval.h"
#include "update/MCContentSwap.h"
#include "update/MCContentUpdater.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

#include <random>
#include <thread>

namespace
{
    using MCContentSwap::MCKeep;
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

        /// <summary>What's at <paramref name="path"/> can't be looked at (a drive failing; empty: none).</summary>
        void FailStat(std::string path) { _Unseen = std::move(path); }

        /// <summary>The folder at <paramref name="path"/> can't be listed (empty: none).</summary>
        void FailFiles(std::string path) { _Unlisted = std::move(path); }

        /// <summary>The folder at <paramref name="path"/> is a link (a program put one in its place; empty: none).</summary>
        void MakeLink(std::string path) { _Link = std::move(path); }

        /// <summary>How long each move of a file takes, by where from and to (a slow drive); none: at once.</summary>
        using MCMoveTime = std::function<std::chrono::milliseconds(const std::string& from, const std::string& to)>;
        void SlowMoves(MCMoveTime time) { _MoveTime = std::move(time); }

        /// <summary>The files hashed since the disk was made.</summary>
        int Hashes() const { return _Hashes; }

        /// <summary>
        /// The player puts <paramref name="bytes"/> at <paramref name="path"/> just as the next file is moved there (once).
        /// </summary>
        void AppearAt(std::string path, std::string bytes) { _Appearing = {std::move(path), std::move(bytes)}; }

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

            if (key == _Unseen)
            {
                return {.Error = key + " can't be looked at"};
            }

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

        std::expected<bool, std::string> IsLink(const std::filesystem::path& path) override
        {
            return !_Link.empty() && Key(path) == _Link;
        }

        std::expected<MCSha256::MCDigest, std::string> Hash(const std::filesystem::path& path) override
        {
            ++_Hashes;
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

        std::expected<std::vector<std::string>, std::string> Files(const std::filesystem::path& folder) override
        {
            const std::string key = Key(folder);
            std::vector<std::string> files;

            if (key == _Unlisted)
            {
                return std::unexpected(key + " can't be listed");
            }

            for (const auto& [path, file] : _Seen)
            {
                if (Inside(path, key))
                {
                    files.push_back(path.substr(key.size() + 1));
                }
            }

            return files;
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

        bool Move(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            if (_Appearing && _Appearing->first == Key(to))
            {
                const auto [path, bytes] = *std::exchange(_Appearing, std::nullopt);
                Record({MCKind::Write, path, {}, bytes, {Parent(to)}, true});
            }

            if (_MoveTime)
            {
                std::this_thread::sleep_for(_MoveTime(Key(from), Key(to)));
            }

            // Never over a file, nor a folder.
            if (Stat(to).Exists)
            {
                Step();
                return false;
            }

            return Rename(from, to);
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

        // The swap only moves files staging wrote: it never asks.
        std::expected<uint64_t, std::string> FreeBytes(const std::filesystem::path&) override
        {
            return std::numeric_limits<uint64_t>::max();
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
        std::string _Unseen;
        std::string _Unlisted;
        std::string _Link;
        std::optional<std::pair<std::string, std::string>> _Appearing;
        MCMoveTime _MoveTime;
        int _Hashes = 0;
    };

    /// <summary>The scratch folders' group, for the tests on a real disk.</summary>
    constexpr std::string_view Group = "mc_content_swap_tests";

    /// <summary>
    /// The game folder. Its files are on the disk in memory, but removal takes the game's lock on the real disk, so it's
    /// a folder there (a scratch folder of its own: <see cref="GameName"/>).
    /// </summary>
    constexpr std::string_view GameName = "crash-game";
    const std::filesystem::path Game = std::filesystem::temp_directory_path() / Group / GameName;

    /// <summary>A program new enough for an update that needs one, and the program before it.</summary>
    constexpr uint64_t NewProgram = 600;
    constexpr uint64_t OldProgram = NewProgram - 1;

    std::string GamePath(std::string_view relative)
    {
        return (Game / MCFileSystem::FromUtf8(relative)).generic_string();
    }

    /// <summary>
    /// An update of seven files: one replaced, one added in a new folder, one removed, one replaced in HD, two an
    /// update didn't place (one replaced, one removed: both kept in Redux backup), and one of a set taken out (deleted);
    /// with each old file, the staged new ones and ready.json on the disk, all safe.
    /// </summary>
    struct MCUpdate
    {
        MCUpdate()
        {
            Add("ART.FST", "art v1", "art v2");
            Add("data/missions/new.fit", std::nullopt, "a new mission");
            Add("data/old.fit", "removed in v2", std::nullopt);
            Add("HD/terrain.hda", "terrain v1, bigger", "terrain v2");
            Add("data/mod.fit", "the folder's own mod", "mod v2", MCKeep::Always);
            Add("data/dropped/own.fit", "the folder's own, dropped", std::nullopt, MCKeep::Always);
            Add("HD/taken/out.hda", "a set taken out", std::nullopt, MCKeep::Never);
            Before.Release = 1;
            After.Release = 2;
            After.List = MCSha256::Of("list 2");
            // What MarkReady gives a journal: the record says the swap is done once it's this one.
            After.Swap = "a swap of the tests";
            Before.Sets = After.Sets = {"game", "hd"};
            Files[GamePath(MCInstalled::RecordPath)] = {MCInstalled::Text(Before), 1};
            Files[GamePath(MCContentSwap::ReadyFile)] = {MCContentSwap::JournalText(Journal()), 1};
        }

        void Add(std::string path, std::optional<std::string> old, std::optional<std::string> now,
                 MCKeep keep = MCKeep::IfChanged)
        {
            MCContentSwap::MCMove move{.Path = path, .Keep = keep};
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
                                        .Placed = keep != MCKeep::Always};
            }

            if (now)
            {
                move.New = MCSha256::Of(*now);
                move.NewSize = now->size();
                // As MarkReady gives it: the staged copy's time.
                move.NewWritten = ++Clock;
                Files[GamePath(std::string(MCContentSwap::StagedFolder) + "/" + path)] = {*now, move.NewWritten};
                After.Files[folded] = {.Path = path, .Set = "game", .Sha256 = *move.New, .Size = now->size()};
            }

            Moves.push_back(std::move(move));
            Old[path] = old;
            New[path] = now;
        }

        MCContentSwap::MCJournal Journal() const { return {Moves, After, LowestProgram}; }

        /// <summary>The update's content needs build <paramref name="build"/> of the program, as ready.json says.</summary>
        void NeedsProgram(uint64_t build)
        {
            LowestProgram = build;
            Files[GamePath(MCContentSwap::ReadyFile)] = {MCContentSwap::JournalText(Journal()), 1};
        }

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
                if (move.Keep == MCKeep::Always && Old.at(move.Path))
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
        bool IsOldAndReady(const MCCrashDisk::MCFiles& files) const { return IsOld(files, true); }

        /// <summary>The old set in place, the update disarmed: its files staged, nothing ready.</summary>
        bool IsOldAndDisarmed(const MCCrashDisk::MCFiles& files) const { return IsOld(files, false); }

        /// <summary>The old set in place and the update staged, ready for the next start or not.</summary>
        bool IsOld(const MCCrashDisk::MCFiles& files, bool ready) const
        {
            if (!WhichSet(files, Old) || !InBackup(files).empty() ||
                files.contains(GamePath(MCContentSwap::ReadyFile)) != ready ||
                files.contains(GamePath(MCContentSwap::JournalFile)))
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

        /// <summary>
        /// The swap stopped where neither way works: the old files set aside and ART.FST and the new mission moved in,
        /// when HD/terrain.hda's staged copy was found lost; then the set taken out's file, held open, couldn't go back.
        /// Returns the disk, the hold let go (the player closed the program).
        /// </summary>
        MCCrashDisk Stuck() const
        {
            MCCrashDisk disk(Files);
            disk.HoldOpen(GamePath("Redux update/replaced/HD/taken/out.hda"));
            disk.Remove(Game / "Redux update/staged/HD/terrain.hda");
            const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game);
            REQUIRE(result.Outcome == MCOutcome::Stuck);
            CHECK(result.Error.find("HD/terrain.hda's staged copy") != std::string::npos);
            CHECK(result.Error.find("HD/taken/out.hda can't be put back") != std::string::npos);
            disk.HoldOpen({});
            disk.Cut([](size_t) { return true; });
            return disk;
        }

        /// <summary>What giving <see cref="Stuck"/> up leaves, but the record: each file by its bytes.</summary>
        static std::map<std::string, std::string> Bytes(const MCCrashDisk::MCFiles& files)
        {
            std::map<std::string, std::string> bytes;

            for (const auto& [path, file] : files)
            {
                bytes[path] = file.Bytes;
            }

            return bytes;
        }

        std::vector<MCContentSwap::MCMove> Moves;
        MCInstalled::MCRecord Before;
        MCInstalled::MCRecord After;
        MCCrashDisk::MCFiles Files;
        std::map<std::string, std::optional<std::string>> Old;
        std::map<std::string, std::optional<std::string>> New;
        int64_t Clock = 100;
        uint64_t LowestProgram = 0;
    };
}

TEST_CASE("content swap: a ready update swaps in; with nothing ready nothing happens")
{
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    CHECK(MCContentSwap::Waiting(disk, Game) == true);
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game);
    CHECK(result.Outcome == MCOutcome::Swapped);
    CHECK_EQ(result.Error, std::string());
    CHECK(update.IsNew(disk.Files()));
    CHECK(MCContentSwap::Waiting(disk, Game) == false);
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

TEST_CASE("content swap: a set taken out is deleted, a file of it changed since staging too")
{
    MCUpdate update;
    update.Files[GamePath("HD/taken/out.hda")] = {"changed since staging", 9};
    MCCrashDisk disk(update.Files);
    REQUIRE(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK(!disk.Files().contains(GamePath("HD/taken/out.hda")));
    CHECK(update.BackupKept(disk.Files()));
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

    // An update's files move only where nothing is.
    REQUIRE(WriteDurably(*disk, folder / "d.json", "third"));
    CHECK(!disk->Move(folder / "c.json", folder / "d.json"));
    CHECK_EQ(*disk->ReadText(folder / "d.json"), std::string("third"));
    CHECK(disk->Move(folder / "c.json", folder / "a" / "e.json"));
    CHECK_EQ(*disk->ReadText(folder / "a" / "e.json"), std::string("second"));

    // The same on a volume that can't move a file only where nothing is (SMB, NFS, FAT): it looks first.
    const std::unique_ptr<MCUpdateDisk> lookingFirst = MakeDisk(MCMoves::LookingFirst);
    REQUIRE(WriteDurably(*lookingFirst, folder / "f.json", "fourth"));
    CHECK(!lookingFirst->Move(folder / "f.json", folder / "d.json"));
    CHECK(!lookingFirst->Move(folder / "f.json", folder / "a"));
    CHECK_EQ(*lookingFirst->ReadText(folder / "d.json"), std::string("third"));
#ifndef _WIN32
    // Nor where a link is, even one to nothing.
    std::filesystem::create_symlink(folder / "nothing.json", folder / "dangling.json");
    CHECK(!lookingFirst->Move(folder / "f.json", folder / "dangling.json"));
    CHECK(!disk->Move(folder / "f.json", folder / "dangling.json"));
    CHECK(!std::filesystem::exists(folder / "nothing.json"));
    REQUIRE(disk->Remove(folder / "dangling.json"));
#endif
    CHECK(lookingFirst->Move(folder / "f.json", folder / "a" / "g.json"));
    CHECK_EQ(*lookingFirst->ReadText(folder / "a" / "g.json"), std::string("fourth"));
    CHECK(!lookingFirst->Stat(folder / "f.json").Exists);

    // A file made anew replaces what's there, never through a link; a folder there refuses it.
    {
        auto made = MCNewFile::Create(folder / "d.json");
        REQUIRE(made.has_value());
        CHECK(made->WriteAt(2, std::string_view("ew")));
        CHECK(made->WriteAt(0, std::string_view("n")));
        CHECK(made->WriteAt(1, std::string_view("n")));
        std::array<char, 4> back{};
        CHECK(made->ReadAt(0, back));
        CHECK_EQ(std::string_view(back.data(), back.size()), std::string_view("nnew"));
        CHECK(!made->ReadAt(1, back));
        CHECK(made->Close());
        CHECK_EQ(*disk->ReadText(folder / "d.json"), std::string("nnew"));
        CHECK(!MCNewFile::Create(folder / "a").has_value());
        CHECK(disk->Stat(folder / "a").Folder);
    }

#ifndef _WIN32
    std::filesystem::create_symlink(folder / "d.json", folder / "link.json");
    REQUIRE(MCNewFile::Create(folder / "link.json").has_value());
    CHECK(!std::filesystem::is_symlink(folder / "link.json"));
    CHECK_EQ(disk->Stat(folder / "link.json").Size, uint64_t{0});
    CHECK_EQ(*disk->ReadText(folder / "d.json"), std::string("nnew"));
    REQUIRE(disk->Remove(folder / "link.json"));
#endif

    // A folder lists its files; one that isn't there, none; a file isn't a folder.
    const auto listed = disk->Files(folder);
    REQUIRE(listed.has_value());
    CHECK((std::set<std::string>(listed->begin(), listed->end()) ==
           std::set<std::string>{"a/e.json", "a/g.json", "d.json"}));
    CHECK(disk->Files(folder / "none").value_or(std::vector<std::string>{"?"}).empty());
    CHECK(!disk->Files(folder / "d.json").has_value());

    // A file's time is the one std::filesystem gives (as the records written before kept it).
    CHECK_EQ(disk->Stat(folder / "d.json").Written,
             static_cast<int64_t>(std::filesystem::last_write_time(folder / "d.json").time_since_epoch().count()));
    CHECK_EQ(MCInstalled::WrittenAt(folder / "d.json").value_or(0), disk->Stat(folder / "d.json").Written);

    // Missing is only what isn't there: a file in a folder that can't be read can't be looked at.
    const MCUpdateDisk::MCStat missing = disk->Stat(folder / "none.json");
    CHECK(!missing.Exists);
    CHECK(missing.Error.empty());
#ifndef _WIN32
    std::filesystem::permissions(folder / "a", std::filesystem::perms::none, error);
    const MCUpdateDisk::MCStat unseen = disk->Stat(folder / "a" / "e.json");
    // Nor is a folder in one that can't be read an empty one.
    const auto unlisted = disk->Files(folder / "a" / "b");
    std::filesystem::permissions(folder / "a", std::filesystem::perms::owner_all, error);
    CHECK(!unseen.Exists);
    CHECK(!unseen.Error.empty());
    CHECK(unlisted.error_or("").find("can't be looked at") != std::string::npos);
    CHECK(IsThere(*disk, folder / "a" / "e.json").value_or(false));
#endif
    CHECK(disk->Remove(folder));
    CHECK(!disk->Stat(folder).Exists);
}

TEST_CASE("content swap: REPAIR gives up a swap that can neither finish nor go back; until a swap is committed the "
          "game doesn't play the files")
{
    const MCUpdate update;
    MCCrashDisk disk = update.Stuck();

    // replaced/ that can't be listed: nothing set aside is taken for none, so nothing goes; it's tried again.
    disk.FailFiles(GamePath(MCContentSwap::ReplacedFolder));
    const std::map<std::string, std::string> stuck = MCUpdate::Bytes(disk.Files());
    CHECK(MCContentSwap::Abandon(disk, Game).error_or("").find("can't be listed") != std::string::npos);
    CHECK(MCContentSwap::Repairing(disk, Game) == true);
    std::map<std::string, std::string> unlisted = MCUpdate::Bytes(disk.Files());
    unlisted.erase(GamePath(MCContentSwap::RepairFile));
    std::map<std::string, std::string> before = stuck;
    before.erase(GamePath(MCContentSwap::JournalFile));
    CHECK(unlisted == before);
    disk.FailFiles({});

    REQUIRE(MCContentSwap::Abandon(disk, Game).has_value());
    const MCCrashDisk::MCFiles& files = disk.Files();

    // The set aside go: kept in Redux backup when an update didn't place them, else deleted with replaced/.
    CHECK(MCContentSwap::Repairing(disk, Game) == true);
    CHECK(MCContentSwap::Waiting(disk, Game) == false);
    CHECK(update.BackupKept(files));
    CHECK(!std::ranges::any_of(files, [](const auto& entry)
                               { return entry.first.find("Redux update/replaced") != std::string::npos; }));
    CHECK(!files.contains(GamePath("HD/terrain.hda")));
    CHECK(!files.contains(GamePath("HD/taken/out.hda")));
    CHECK(!files.contains(GamePath("data/old.fit")));

    // The new files already in place stay, recorded as an update's; staged/ stays for the repair's staging.
    CHECK_EQ(files.at(GamePath("ART.FST")).Bytes, std::string("art v2"));
    CHECK_EQ(files.at(GamePath("data/missions/new.fit")).Bytes, std::string("a new mission"));
    CHECK_EQ(files.at(GamePath("Redux update/staged/data/mod.fit")).Bytes, std::string("mod v2"));
    const auto record = MCInstalled::FromText(files.at(GamePath(MCInstalled::RecordPath)).Bytes);
    REQUIRE(record.has_value());
    CHECK_EQ(record->Release, uint64_t{1});
    CHECK(record->Files.at("ART.FST").Sha256 == MCSha256::Of("art v2"));
    CHECK(record->Files.at("ART.FST").Placed);
    CHECK_EQ(record->Files.at("ART.FST").Written, files.at(GamePath("ART.FST")).Written);
    CHECK(record->Files.at("DATA/MISSIONS/NEW.FIT").Placed);
    CHECK(record->Files.at("DATA/MOD.FIT").Sha256 == MCSha256::Of("the folder's own mod"));

    // Nothing to play until the repair's update goes in.
    const MCContentSwap::MCResult waiting = MCContentSwap::Run(disk, Game);
    CHECK(waiting.Outcome == MCOutcome::Stuck);
    CHECK(waiting.Error.find("repair") != std::string::npos);

    // The repair's update put back (its staged copy lost) is still no set to play; given up again, it goes too.
    MCContentSwap::MCJournal repair{.Moves = {{.Path = "HD/terrain.hda",
                                               .New = MCSha256::Of("terrain v2"),
                                               .NewSize = std::string_view("terrain v2").size()}},
                                    .Record = update.After};
    REQUIRE(MCContentSwap::MarkReady(disk, Game, repair));
    const MCContentSwap::MCResult putBack = MCContentSwap::Run(disk, Game);
    CHECK(putBack.Outcome == MCOutcome::Stuck);
    CHECK(putBack.Error.find("staged copy") != std::string::npos);
    REQUIRE(MCContentSwap::Abandon(disk, Game).has_value());
    CHECK(MCContentSwap::Waiting(disk, Game) == false);
    CHECK(MCContentSwap::Repairing(disk, Game) == true);

    // Committed, the repair is done.
    REQUIRE(disk.WriteFlushed(Game / "Redux update/staged/HD/terrain.hda", "terrain v2"));
    REQUIRE(MCContentSwap::MarkReady(disk, Game, repair));
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK(MCContentSwap::Repairing(disk, Game) == false);
    CHECK_EQ(disk.Files().at(GamePath("HD/terrain.hda")).Bytes, std::string("terrain v2"));
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Nothing);
}

TEST_CASE(
    "content swap: REPAIR's giving up stopped at any step, any unflushed changes lost, ends as it does whole when "
    "done again")
{
    const MCUpdate update;
    MCCrashDisk whole = update.Stuck();
    REQUIRE(MCContentSwap::Abandon(whole, Game).has_value());
    const int steps = whole.Changes();
    CHECK(steps > 5);
    const std::map<std::string, std::string> given = MCUpdate::Bytes(whole.Files());
    constexpr int SubsetsPerStep = 12;
    std::mt19937 random(20261105);

    for (int step = 0; step < steps; ++step)
    {
        for (int subset = 0; subset < SubsetsPerStep; ++subset)
        {
            MCCrashDisk disk = update.Stuck();
            disk.CutAt(step);

            try
            {
                // Cut short by the power cut: what it returns is never reached.
                static_cast<void>(MCContentSwap::Abandon(disk, Game));
            }
            catch (const MCPowerCut&)
            {
            }

            // Every unflushed change lost, every one kept, then random halves.
            const uint32_t seed = random();
            disk.Cut([&](size_t i)
                     { return subset == 0   ? false
                              : subset == 1 ? true
                                            : ((seed >> (i % 32)) & 1) != 0; });

            // Begun, it's a repair: the game doesn't play the files.
            if (MCContentSwap::Repairing(disk, Game) == true)
            {
                MCCrashDisk start = disk;

                if (MCContentSwap::Run(start, Game).Outcome != MCOutcome::Stuck)
                {
                    FAIL_CHECK(
                        std::format("step {} subset {}: a start played the files a repair gave up", step, subset));
                }
            }

            const auto again = MCContentSwap::Abandon(disk, Game);

            if (!again || MCUpdate::Bytes(disk.Files()) != given)
            {
                FAIL_CHECK(std::format("step {} subset {}: done again, it didn't end as whole ({})", step, subset,
                                       again ? std::string("its files differ") : again.error()));
            }
        }
    }
}

TEST_CASE("content swap: REPAIR keeps every file set aside when the journal can't be read")
{
    const MCUpdate update;
    MCCrashDisk disk = update.Stuck();
    REQUIRE(disk.WriteFlushed(Game / MCFileSystem::FromUtf8(MCContentSwap::JournalFile), "{ damaged"));
    REQUIRE(MCContentSwap::Abandon(disk, Game).has_value());
    const auto kept = MCUpdate::InBackup(disk.Files());
    CHECK_EQ(kept.at(GamePath("Redux backup/ART.FST")), std::string("art v1"));
    CHECK_EQ(kept.at(GamePath("Redux backup/HD/taken/out.hda")), std::string("a set taken out"));
    CHECK_EQ(kept.at(GamePath("Redux backup/data/mod.fit")), std::string("the folder's own mod"));
    CHECK(MCContentSwap::Repairing(disk, Game) == true);
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Stuck);
}

TEST_CASE("content swap: content needing a newer program than the one starting is disarmed, its files kept staged, and "
          "the previous set plays")
{
    MCUpdate update;
    update.NeedsProgram(NewProgram);
    MCCrashDisk disk(update.Files);
    const MCContentSwap::MCResult old = MCContentSwap::Run(disk, Game, std::nullopt, OldProgram);
    CHECK(old.Outcome == MCOutcome::Disarmed);
    CHECK(old.Error.find(std::format("needs build {}", NewProgram)) != std::string::npos);
    CHECK(update.IsOldAndDisarmed(disk.Files()));
    CHECK(MCContentSwap::Waiting(disk, Game) == false);
    CHECK(MCContentSwap::Run(disk, Game, std::nullopt, NewProgram).Outcome == MCOutcome::Nothing);

    // Armed again (the next INSTALL, its files already staged), the program it needs swaps it in.
    REQUIRE(MCContentSwap::MarkReady(disk, Game, update.Journal()));
    CHECK(MCContentSwap::Run(disk, Game, std::nullopt, NewProgram).Outcome == MCOutcome::Swapped);
    CHECK(update.IsNew(disk.Files()));
}

TEST_CASE("content swap: a swap a newer program began, stopped at any step, goes back under an older one, disarmed; "
          "committed, it only finishes")
{
    MCUpdate update;
    update.NeedsProgram(NewProgram);
    MCCrashDisk measure(update.Files);
    REQUIRE(MCContentSwap::Run(measure, Game, std::nullopt, NewProgram).Outcome == MCOutcome::Swapped);
    const int steps = measure.Changes();
    int disarmed = 0;
    int finished = 0;

    for (int step = 0; step < steps; ++step)
    {
        for (const bool keepAll : {false, true})
        {
            MCCrashDisk disk(update.Files);
            disk.CutAt(step);

            try
            {
                MCContentSwap::Run(disk, Game, std::nullopt, NewProgram);
            }
            catch (const MCPowerCut&)
            {
            }

            disk.Cut([&](size_t) { return keepAll; });
            const MCContentSwap::MCResult old = MCContentSwap::Run(disk, Game, std::nullopt, OldProgram);

            if (old.Outcome == MCOutcome::Disarmed && update.IsOldAndDisarmed(disk.Files()))
            {
                ++disarmed;
            }
            else if ((old.Outcome == MCOutcome::Swapped || old.Outcome == MCOutcome::Nothing) &&
                     update.IsNew(disk.Files()))
            {
                ++finished;
            }
            else
            {
                FAIL_CHECK(std::format("step {} keep {}: under the older program {} ({})", step, keepAll,
                                       static_cast<int>(old.Outcome), old.Error));
            }
        }
    }

    CHECK(disarmed > 0);
    CHECK(finished > 0);
}

TEST_CASE("content swap at a start: content whose list needs a newer program is disarmed, the previous files play; "
          "armed again, the program it needs swaps it in")
{
    MCContentFixture::MCRelease release(Group, "too-new");
    const std::filesystem::path& game = release.Game.Path();
    release.List.LowestProgram = NewProgram;
    const MCContentPlan::MCPlan plan = release.Plan();
    CHECK_EQ(plan.LowestProgram, NewProgram);
    MCContentFixture::MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(MCContentStage::Stage(*disk, source, game, plan, release.List, MCContentFixture::GoOn).has_value());
    REQUIRE(MCContentStage::Arm(*disk, game, plan).has_value());
    const std::unique_ptr<MCUpdateLock> lock = MCUpdateLock::Open(game, MCUpdateLock::GameLock);
    REQUIRE(lock != nullptr);

    // Armed, the journal-to-be names the program the content needs, and each staged file's time.
    const auto ready = disk->ReadText(game / MCFileSystem::FromUtf8(MCContentSwap::ReadyFile));
    REQUIRE(ready.has_value());
    const auto journal = MCContentSwap::ReadJournal(*ready);
    REQUIRE(journal.has_value());
    CHECK_EQ(journal->LowestProgram, NewProgram);

    for (const MCContentSwap::MCMove& move : journal->Moves)
    {
        if (move.New)
        {
            const std::filesystem::path staged =
                game / MCFileSystem::FromUtf8(MCContentSwap::StagedFolder) / MCFileSystem::FromUtf8(move.Path);
            CHECK(move.NewWritten != 0);
            CHECK_EQ(move.NewWritten, disk->Stat(staged).Written);
        }
    }

    const MCContentSwap::MCResult old = MCContentSwap::RunAtStart(*disk, game, lock.get(), OldProgram);
    CHECK(old.Outcome == MCOutcome::Disarmed);
    CHECK(MCContentSwap::Waiting(*disk, game) == false);
    CHECK_EQ(MCContentFixture::Read(game / "data" / "changed.fit"), std::string("changed v1"));
    CHECK_EQ(MCContentFixture::Read(game / "Redux update" / "staged" / "data" / "changed.fit"),
             std::string("changed v2"));
    lock->Release();

    REQUIRE(MCContentStage::Arm(*disk, game, plan).has_value());
    CHECK(MCContentSwap::RunAtStart(*disk, game, lock.get(), NewProgram).Outcome == MCOutcome::Swapped);
    release.CheckIsRelease2();
}

TEST_CASE("content removal: a committed swap that can't finish its clearing keeps Redux update whole, the folder's own "
          "files still set aside in it")
{
    const MCScratchFolder locks(Group, GameName);
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    REQUIRE(disk.Rename(Game / MCFileSystem::FromUtf8(MCContentSwap::ReadyFile),
                        Game / MCFileSystem::FromUtf8(MCContentSwap::JournalFile)));
    const std::string setAside = GamePath(std::string(MCContentSwap::ReplacedFolder) + "/data/mod.fit");
    disk.HoldOpen(setAside);
    const auto held = MCContentRemoval::Remove(disk, Game, std::chrono::milliseconds(0));
    REQUIRE(!held.has_value());
    CHECK(held.error().find("data/mod.fit") != std::string::npos);
    CHECK_EQ(disk.Files().at(setAside).Bytes, std::string("the folder's own mod"));
    CHECK(disk.Files().contains(GamePath(MCContentSwap::JournalFile)));
    CHECK(disk.Files().contains(GamePath(MCInstalled::RecordPath)));
    CHECK(disk.Files().contains(GamePath("ART.FST")));

    // Let go, it's finished and removed: the folder's own files are in Redux backup, what the update placed gone.
    disk.HoldOpen({});
    const auto removed = MCContentRemoval::Remove(disk, Game, std::chrono::milliseconds(0));
    REQUIRE(removed.has_value());
    CHECK(update.BackupKept(disk.Files()));
    CHECK(!disk.Files().contains(GamePath("ART.FST")));
    CHECK(!std::ranges::any_of(disk.Files(), [](const auto& entry)
                               { return entry.first.find("Redux update") != std::string::npos; }));
}

TEST_CASE("content removal: a repair's giving up stopped at any step is finished first: the folder's own files end in "
          "Redux backup")
{
    const MCScratchFolder locks(Group, GameName);
    const MCUpdate update;
    MCCrashDisk whole = update.Stuck();
    REQUIRE(MCContentSwap::Abandon(whole, Game).has_value());
    const int steps = whole.Changes();
    int tried = 0;

    for (int step = 0; step < steps; ++step)
    {
        for (const bool keepAll : {false, true})
        {
            MCCrashDisk disk = update.Stuck();
            disk.CutAt(step);

            try
            {
                // Cut short by the power cut: what it returns is never reached.
                static_cast<void>(MCContentSwap::Abandon(disk, Game));
            }
            catch (const MCPowerCut&)
            {
            }

            disk.Cut([&](size_t) { return keepAll; });

            // Begun (the journal is repair.json): removal finishes it.
            if (disk.Files().contains(GamePath(MCContentSwap::JournalFile)))
            {
                continue;
            }

            ++tried;
            const auto removed = MCContentRemoval::Remove(disk, Game, std::chrono::milliseconds(0));

            if (!removed || !update.BackupKept(disk.Files()))
            {
                FAIL_CHECK(
                    std::format("step {} keep {}: {}", step, keepAll,
                                removed ? std::string("Redux backup lacks the folder's own files") : removed.error()));
            }
        }
    }

    CHECK(tried > 0);
}

TEST_CASE("content removal: a repair's giving up of a swap that only added files, stopped before it recorded them, is "
          "finished first: the new files go with the rest")
{
    const MCScratchFolder locks(Group, GameName);
    constexpr int64_t ArtWritten = 101;
    constexpr int64_t MissionWritten = 102;
    const std::string art = "art v1";
    const std::string mission = "a new mission";
    MCInstalled::MCRecord before{.Release = 1, .Sets = {"game"}};
    before.Files["ART.FST"] = {.Path = "ART.FST",
                               .Set = "game",
                               .Sha256 = MCSha256::Of(art),
                               .Size = art.size(),
                               .Written = ArtWritten,
                               .Placed = true};
    MCInstalled::MCRecord after = before;
    after.Release = 2;
    after.Swap = "an additions-only swap";
    after.Files["DATA/MISSIONS/NEW.FIT"] = {
        .Path = "data/missions/new.fit", .Set = "game", .Sha256 = MCSha256::Of(mission), .Size = mission.size()};
    const MCContentSwap::MCMove added{.Path = "data/missions/new.fit",
                                      .New = MCSha256::Of(mission),
                                      .NewSize = mission.size(),
                                      .NewWritten = MissionWritten};

    // Given up (the journal is repair.json), the new mission in place, the record not yet naming it; no replaced/.
    MCCrashDisk disk(
        {{GamePath(MCInstalled::RecordPath), {MCInstalled::Text(before), 1}},
         {GamePath("ART.FST"), {art, ArtWritten}},
         {GamePath("data/missions/new.fit"), {mission, MissionWritten}},
         {GamePath(MCContentSwap::RepairFile), {MCContentSwap::JournalText({.Moves = {added}, .Record = after}), 1}}});
    const auto removed = MCContentRemoval::Remove(disk, Game, std::chrono::milliseconds(0));
    REQUIRE(removed.has_value());
    CHECK_EQ(removed->Removed, size_t{2});
    CHECK(disk.Files().empty());
}

TEST_CASE("content removal: a recorded file that can't be looked at keeps the record and fails the removal; looked at "
          "again, it goes")
{
    const MCScratchFolder locks(Group, GameName);
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    REQUIRE(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    disk.FailStat(GamePath("ART.FST"));
    const auto unseen = MCContentRemoval::Remove(disk, Game, std::chrono::milliseconds(0));
    CHECK(unseen.error_or("").find("ART.FST") != std::string::npos);
    CHECK(disk.Files().contains(GamePath(MCInstalled::RecordPath)));
    CHECK(disk.Files().contains(GamePath("ART.FST")));

    disk.FailStat({});
    REQUIRE(MCContentRemoval::Remove(disk, Game, std::chrono::milliseconds(0)).has_value());
    CHECK(!disk.Files().contains(GamePath("ART.FST")));
    CHECK(!disk.Files().contains(GamePath(MCInstalled::RecordPath)));
}

TEST_CASE("update lock: held exclusive by one copy, another can't hold it shared; a wait for it ends at its deadline")
{
    const MCScratchFolder game(Group, "lock-waits");
    const std::unique_ptr<MCUpdateLock> swapping = MCUpdateLock::Open(game.Path(), MCUpdateLock::GameLock);
    const std::unique_ptr<MCUpdateLock> starting = MCUpdateLock::Open(game.Path(), MCUpdateLock::GameLock);
    REQUIRE(swapping != nullptr);
    REQUIRE(starting != nullptr);
    REQUIRE(swapping->TryExclusive());
    CHECK(!starting->TryShared());

    constexpr std::chrono::milliseconds wait{250};
    const auto before = std::chrono::steady_clock::now();
    CHECK(!starting->SharedBy(before + wait));
    CHECK(!starting->ExclusiveBy(before + wait));
    const auto waited = std::chrono::steady_clock::now() - before;
    CHECK(waited >= wait);
    CHECK(waited < 2 * wait + MCUpdateLock::RetryEvery);

    // Let go, the waits end as soon as they can; exclusive turns shared in place.
    swapping->Release();
    CHECK(starting->ExclusiveBy(std::chrono::steady_clock::now() + wait));
    CHECK(starting->TryShared());
    CHECK(swapping->TryShared());
    CHECK(!swapping->TryExclusive());
}

TEST_CASE("content swap at a start: another copy holding the lock exclusive makes the start Stuck by its deadline, "
          "holding nothing; it never waits longer")
{
    const MCScratchFolder game(Group, "start-stuck");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const std::unique_ptr<MCUpdateLock> other = MCUpdateLock::Open(game.Path(), MCUpdateLock::GameLock);
    const std::unique_ptr<MCUpdateLock> starting = MCUpdateLock::Open(game.Path(), MCUpdateLock::GameLock);
    REQUIRE(other != nullptr);
    REQUIRE(starting != nullptr);
    REQUIRE(other->TryExclusive());
    constexpr std::chrono::milliseconds wait{300};

    // Nothing under way here, but another copy holds it exclusive (removing the content, say), then a swap stopped
    // midway (a repair runs for as long as its download takes).
    for (const bool midway : {false, true})
    {
        if (midway)
        {
            REQUIRE(WriteDurably(*disk, game.Path() / MCFileSystem::FromUtf8(MCContentSwap::JournalFile), "{}"));
        }

        const auto before = std::chrono::steady_clock::now();
        const MCContentSwap::MCResult result =
            MCContentSwap::RunAtStart(*disk, game.Path(), starting.get(), NewProgram, before + wait);
        const auto waited = std::chrono::steady_clock::now() - before;
        CHECK(result.Outcome == MCOutcome::Stuck);
        CHECK(result.Error.find("another copy") != std::string::npos);
        CHECK(waited < 2 * wait + MCUpdateLock::RetryEvery);

        // It holds nothing: the other copy, letting go, takes it back exclusive at once.
        other->Release();
        CHECK(other->TryExclusive());
    }
}

TEST_CASE("content swap at a start: a journal that can't be looked at makes the start Stuck, holding nothing; once it "
          "can be, the swap stopped midway finishes")
{
    const MCUpdate update;
    // A swap stopped as it began: its journal where ready.json was.
    MCCrashDisk::MCFiles files = update.Files;
    files[GamePath(MCContentSwap::JournalFile)] = files.at(GamePath(MCContentSwap::ReadyFile));
    files.erase(GamePath(MCContentSwap::ReadyFile));
    MCCrashDisk disk(files);
    disk.FailStat(GamePath(MCContentSwap::JournalFile));
    CHECK(!MCContentSwap::Waiting(disk, Game).has_value());
    CHECK(!MCContentSwap::UnderWay(disk, Game).has_value());

    std::filesystem::create_directories(Game);
    const std::unique_ptr<MCUpdateLock> starting = MCUpdateLock::Open(Game, MCUpdateLock::GameLock);
    const std::unique_ptr<MCUpdateLock> other = MCUpdateLock::Open(Game, MCUpdateLock::GameLock);
    REQUIRE(starting != nullptr);
    REQUIRE(other != nullptr);
    const MCContentSwap::MCResult unseen = MCContentSwap::RunAtStart(disk, Game, starting.get(), NewProgram);
    CHECK(unseen.Outcome == MCOutcome::Stuck);
    CHECK(unseen.Error.find("can't be looked at") != std::string::npos);
    CHECK(other->TryExclusive());
    other->Release();
    CHECK(!update.IsNew(disk.Files()));

    // Seen again: the swap finishes.
    disk.FailStat({});
    CHECK(MCContentSwap::RunAtStart(disk, Game, starting.get(), NewProgram).Outcome == MCOutcome::Swapped);
    CHECK(update.IsNew(disk.Files()));
}

TEST_CASE("content swap: a folder put in as a link since staging stops the swap, naming it; putting the previous set "
          "back moves nothing through it either")
{
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    disk.MakeLink(GamePath("data/missions"));
    const MCContentSwap::MCResult stopped = MCContentSwap::Run(disk, Game);
    CHECK(stopped.Outcome == MCOutcome::PutBack);
    // Named as the system writes the path: data\missions on Windows.
    const std::string link = MCFileSystem::ToUtf8(std::filesystem::path("data") / "missions") + " is a link";
    CHECK(stopped.Error.find(link) != std::string::npos);
    CHECK(update.IsOldAndReady(disk.Files()));

    // One on an old file's way aside: the files set aside before it come back, and nothing moved through it.
    MCCrashDisk aside(update.Files);
    aside.MakeLink(GamePath("HD"));
    const MCContentSwap::MCResult held = MCContentSwap::Run(aside, Game);
    CHECK(held.Outcome == MCOutcome::PutBack);
    CHECK(held.Error.find("HD is a link") != std::string::npos);
    CHECK(update.IsOldAndReady(aside.Files()));
}

TEST_CASE("content swap: a file the player puts in a new file's place as it moves in is never replaced: the old set "
          "goes back")
{
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    disk.AppearAt(GamePath("data/missions/new.fit"), "the player's own mission");
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game);
    CHECK(result.Outcome == MCOutcome::PutBack);
    CHECK(result.Error.find("data/missions/new.fit") != std::string::npos);
    CHECK_EQ(disk.Files().at(GamePath("data/missions/new.fit")).Bytes, std::string("the player's own mission"));
    CHECK_EQ(disk.Files().at(GamePath("ART.FST")).Bytes, std::string("art v1"));
    CHECK(disk.Files().contains(GamePath(MCContentSwap::ReadyFile)));

    // The next start sets the player's file aside, kept in Redux backup, and swaps the update in.
    CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
    CHECK(update.WhichSet(disk.Files(), update.New));
    CHECK_EQ(disk.Files().at(GamePath("Redux backup/data/missions/new.fit")).Bytes,
             std::string("the player's own mission"));
}

TEST_CASE("content swap: a file set aside that can't be looked at once the swap is committed stays set aside, never "
          "taken for gone")
{
    const MCUpdate update;
    const std::string setAside = GamePath(std::string(MCContentSwap::ReplacedFolder) + "/data/mod.fit");
    MCCrashDisk measure(update.Files);
    REQUIRE(MCContentSwap::Run(measure, Game).Outcome == MCOutcome::Swapped);
    bool tried = false;

    // Stopped once committed, with the folder's own mod still set aside.
    for (int step = 0; step < measure.Changes() && !tried; ++step)
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

        if (!record || record->Swap != update.After.Swap || !disk.Files().contains(setAside))
        {
            continue;
        }

        tried = true;
        disk.FailStat(setAside);
        const MCContentSwap::MCResult unseen = MCContentSwap::Run(disk, Game);
        CHECK(unseen.Outcome == MCOutcome::Swapped);
        CHECK(unseen.Error.find("data/mod.fit") != std::string::npos);
        CHECK_EQ(disk.Files().at(setAside).Bytes, std::string("the folder's own mod"));

        disk.FailStat({});
        CHECK(MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Swapped);
        CHECK(update.IsNew(disk.Files()));
    }

    CHECK(tried);
}

TEST_CASE("content swap: a swap the machine stopped at any step is finished telling each file by its size and time: "
          "none is hashed")
{
    const MCUpdate update;
    MCCrashDisk measure(update.Files);
    REQUIRE(MCContentSwap::Run(measure, Game).Outcome == MCOutcome::Swapped);
    CHECK_EQ(measure.Hashes(), 0);
    const int steps = measure.Changes();

    for (int step = 0; step < steps; ++step)
    {
        for (const bool keepAll : {false, true})
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

            disk.Cut([&](size_t) { return keepAll; });
            const int before = disk.Hashes();
            const MCContentSwap::MCResult again = MCContentSwap::Run(disk, Game);

            if (disk.Hashes() != before || !update.IsNew(disk.Files()))
            {
                FAIL_CHECK(std::format("step {} keep {}: {} hashed, {} ({})", step, keepAll, disk.Hashes() - before,
                                       static_cast<int>(again.Outcome), again.Error));
            }
        }
    }
}

namespace
{
    /// <summary>Whether a move puts the previous set back: a new file to staged/, or an old one out of replaced/.</summary>
    bool MovesBack(const std::string& from, const std::string& to)
    {
        return to.starts_with(GamePath(MCContentSwap::StagedFolder)) ||
               (from.starts_with(GamePath(MCContentSwap::ReplacedFolder)) &&
                !to.starts_with(GamePath(MCContentList::BackupFolder)));
    }
}

TEST_CASE("content swap: a lobby's start moves files forward for half its deadline, keeping the rest for putting the "
          "previous set back")
{
    constexpr std::chrono::milliseconds slow{100};
    constexpr std::chrono::milliseconds deadline{600};
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    disk.SlowMoves([&](const std::string& from, const std::string& to)
                   { return MovesBack(from, to) ? std::chrono::milliseconds(0) : slow; });
    const auto start = std::chrono::steady_clock::now();
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game, start + deadline);
    const auto took = std::chrono::steady_clock::now() - start;
    CHECK(result.Outcome == MCOutcome::PutBack);
    CHECK(result.Error.find("lobby") != std::string::npos);
    CHECK(update.IsOldAndReady(disk.Files()));
    CHECK(took < deadline);
}

TEST_CASE("content swap: putting the previous set back after a fast swap failed isn't cut short by a lobby's deadline: "
          "it ends late, and the previous set plays")
{
    constexpr std::chrono::milliseconds slow{100};
    constexpr std::chrono::milliseconds deadline{300};
    const MCUpdate update;
    MCCrashDisk disk(update.Files);
    REQUIRE(disk.Remove(Game / MCFileSystem::FromUtf8(MCContentSwap::StagedFolder) / "HD" / "terrain.hda"));
    int movedBack = 0;
    disk.SlowMoves(
        [&](const std::string& from, const std::string& to)
        {
            if (!MovesBack(from, to))
            {
                return std::chrono::milliseconds(0);
            }

            ++movedBack;
            return slow;
        });
    const auto start = std::chrono::steady_clock::now();
    const MCContentSwap::MCResult result = MCContentSwap::Run(disk, Game, start + deadline);
    const auto took = std::chrono::steady_clock::now() - start;
    CHECK(result.Outcome == MCOutcome::PutBack);
    CHECK(result.Error.find("HD/terrain.hda's staged copy") != std::string::npos);
    CHECK(MCUpdate::WhichSet(disk.Files(), update.Old));
    CHECK(disk.Files().contains(GamePath(MCContentSwap::ReadyFile)));
    CHECK(movedBack * slow > deadline);
    CHECK(took >= movedBack * slow);
}

TEST_CASE("content at a start: a first start, a release older than the program plays, and the release a program was "
          "installed alone for, when this program takes it")
{
    using MCInstalled::MCStartNeed;
    const MCScratchFolder game(Group, "at-start");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    MCInstalled::MCRecord record;
    constexpr uint64_t noLowest = 0;

    // Nothing installed and no installer's answer: nothing (a copy pointed at by hand updates when asked).
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Nothing);

    // The installer's answer and nothing installed: the first start's install.
    REQUIRE(MCContentUpdater::Choose(*disk, game.Path(), {std::string(MCContentList::HdSet)}).has_value());
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Needed);

    // A release installed, as old as the program plays or newer: nothing; older: needed.
    record.Release = 3;
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, 3) == MCStartNeed::Nothing);
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, 4) == MCStartNeed::Needed);

    // A program armed alone for release 5: due only to a program that takes it, and only until it's in.
    const MCInstalled::MCDue due{.Release = 5, .LowestProgram = NewProgram};
    REQUIRE(MCInstalled::NoteDue(*disk, game.Path(), due).has_value());
    CHECK(MCInstalled::ReadDue(*disk, game.Path()) == due);
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, OldProgram, noLowest) == MCStartNeed::Nothing);
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Due);
    record.Release = 5;
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Nothing);

    // A later epoch's release is due over an earlier epoch's higher number: the key list started the order again.
    record.Release = 100;
    const MCInstalled::MCDue later{.Epoch = 1, .Release = 2, .LowestProgram = NewProgram};
    REQUIRE(MCInstalled::NoteDue(*disk, game.Path(), later).has_value());
    CHECK(MCInstalled::ReadDue(*disk, game.Path()) == later);
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Due);
    record.Epoch = 1;
    record.Release = 2;
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Nothing);
    record.Epoch = 0;

    // A note that can't be read waits for nothing.
    record.Release = 3;
    REQUIRE(WriteDurably(*disk, game.Path() / MCFileSystem::FromUtf8(MCInstalled::DueFile), "{ damaged"));
    CHECK(!MCInstalled::ReadDue(*disk, game.Path()).has_value());
    CHECK(MCInstalled::AtStart(*disk, game.Path(), record, NewProgram, noLowest) == MCStartNeed::Nothing);
}

TEST_CASE("content at a start: the program armed alone notes its release's content due; a committed swap of it, or "
          "a later one, ends it")
{
    const MCScratchFolder game(Group, "due");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const MCContentUpdater::MCSetup setup{.GameFolder = game.Path(), .RunningBuild = OldProgram};
    const MCContentUpdater::MCOffer alone{.Release = {.Number = 2}, .ProgramBuild = NewProgram};
    REQUIRE(MCContentUpdater::Arm(setup, *disk, alone).has_value());
    const MCInstalled::MCDue due{.Release = 2, .LowestProgram = NewProgram};
    CHECK(MCInstalled::ReadDue(*disk, game.Path()) == due);

    // A swap of an earlier release leaves it; of that release, it goes.
    for (const uint64_t release : {uint64_t{1}, uint64_t{2}})
    {
        MCUpdate update;
        update.After.Release = release;
        update.Files[GamePath(MCContentSwap::ReadyFile)] = {MCContentSwap::JournalText(update.Journal()), 1};
        update.Files[GamePath(MCInstalled::DueFile)] = {
            *disk->ReadText(game.Path() / MCFileSystem::FromUtf8(MCInstalled::DueFile)), 1};
        MCCrashDisk crash(update.Files);
        REQUIRE(MCContentSwap::Run(crash, Game).Outcome == MCOutcome::Swapped);
        CHECK_EQ(crash.Files().contains(GamePath(MCInstalled::DueFile)), release < 2);
    }

    // A swap of a later epoch ends an earlier epoch's note, whatever its number.
    const MCContentUpdater::MCOffer earlier{.Release = {.Number = 100}, .ProgramBuild = NewProgram};
    REQUIRE(MCContentUpdater::Arm(setup, *disk, earlier).has_value());
    MCUpdate update;
    update.After.Epoch = 1;
    update.After.Release = 1;
    update.Files[GamePath(MCContentSwap::ReadyFile)] = {MCContentSwap::JournalText(update.Journal()), 1};
    update.Files[GamePath(MCInstalled::DueFile)] = {
        *disk->ReadText(game.Path() / MCFileSystem::FromUtf8(MCInstalled::DueFile)), 1};
    MCCrashDisk crash(update.Files);
    REQUIRE(MCContentSwap::Run(crash, Game).Outcome == MCOutcome::Swapped);
    CHECK(!crash.Files().contains(GamePath(MCInstalled::DueFile)));
}

TEST_CASE(
    "content at a start: the copy has HD as its record says once a release is installed, else as its folder holds "
    "it")
{
    const MCScratchFolder game(Group, "has-hd");
    MCInstalled::MCRecord record;
    CHECK(!MCInstalled::Has(record, game.Path(), MCContentList::HdSet));
    std::filesystem::create_directories(game.Path() / "HD");
    CHECK(MCInstalled::Has(record, game.Path(), MCContentList::HdSet));

    // Installed without HD (or HD removed), its folder left behind: no HD.
    record.Release = 2;
    record.Sets = {std::string(MCContentList::GameSet)};
    CHECK(!MCInstalled::Has(record, game.Path(), MCContentList::HdSet));
    record.Sets.emplace(MCContentList::HdSet);
    CHECK(MCInstalled::Has(record, game.Path(), MCContentList::HdSet));
}

TEST_CASE("content swap: a new file already in place before the swap stays there when the previous set goes back, the "
          "machine stopping at any step or not")
{
    MCUpdate update;
    update.Files[GamePath("data/missions/new.fit")] = {"a new mission", 7};
    MCCrashDisk measure(update.Files);
    REQUIRE(MCContentSwap::Run(measure, Game).Outcome == MCOutcome::Swapped);
    const int steps = measure.Changes();

    for (int step = 0; step <= steps; ++step)
    {
        for (const bool keepAll : {false, true})
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

            disk.Cut([&](size_t) { return keepAll; });
            disk.HoldOpen(GamePath("HD/terrain.hda"));
            const MCContentSwap::MCResult held = MCContentSwap::Run(disk, Game);

            if (held.Outcome == MCOutcome::Stuck ||
                disk.Files().at(GamePath("data/missions/new.fit")).Bytes != "a new mission")
            {
                FAIL_CHECK(
                    std::format("step {} keep {}: {} ({})", step, keepAll, static_cast<int>(held.Outcome), held.Error));
            }

            disk.HoldOpen({});

            if (MCContentSwap::Run(disk, Game).Outcome == MCOutcome::Stuck ||
                !update.WhichSet(disk.Files(), update.New))
            {
                FAIL_CHECK(std::format("step {} keep {}: let go, the start after didn't finish", step, keepAll));
            }
        }
    }
}
