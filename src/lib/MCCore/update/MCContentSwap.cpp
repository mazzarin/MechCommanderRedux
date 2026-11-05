#include "stdafx.h"
#include "update/MCContentSwap.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"
#include "update/MCUpdateLock.h"

#include <random>
#include <thread>

namespace
{
    using namespace MCContentSwap;
    using namespace MCUpdateJson;

    /// <summary>How often a start waiting for another copy's swap tries the lock again.</summary>
    constexpr std::chrono::milliseconds LockRetry{100};

    constexpr std::string_view MovesKey = "moves";
    constexpr std::string_view RecordKey = "record";
    constexpr std::string_view PathKey = "path";
    constexpr std::string_view OldKey = "old";
    constexpr std::string_view OldSizeKey = "oldSize";
    constexpr std::string_view OldWrittenKey = "oldWritten";
    constexpr std::string_view NewKey = "new";
    constexpr std::string_view NewSizeKey = "newSize";
    constexpr std::string_view LowestProgramKey = "lowestProgram";
    /// <summary>A move's MCKeep, when not IfChanged: "always" or "never".</summary>
    constexpr std::string_view KeepKey = "keep";
    constexpr std::string_view KeepAlways = "always";
    constexpr std::string_view KeepNever = "never";
    /// <summary>A later copy kept in Redux backup beside a path's first: "name (2).ext", then "(3)", up to the last.</summary>
    constexpr int FirstNumberedCopy = 2;
    constexpr int LastNumberedCopy = 999;
    constexpr std::string_view TooLateError = "the swap took longer than a lobby's start allows";
    constexpr std::string_view RepairUnfinishedError = "a repair stopped before it was done";

    /// <summary>What's at a move's path.</summary>
    enum class MCAt : uint8_t
    {
        Missing,
        Old,
        New,
        /// <summary>Another file: the player's (changed since staging, or added in a new file's place).</summary>
        Other,
        /// <summary>A folder in the file's place.</summary>
        Folder,
    };

    /// <summary>One run of the swap: forward, and back if forward fails.</summary>
    class MCSwapper
    {
    public:
        MCSwapper(MCUpdateDisk& disk, std::filesystem::path game, MCJournal journal,
                  std::optional<std::chrono::steady_clock::time_point> deadline)
            : _Disk(disk), _Game(std::move(game)), _Journal(std::move(journal)), _Deadline(deadline)
        {
        }

        /// <summary>
        /// Moves the old files aside, the new ones in, writes the record, then clears the update's folders and the
        /// journal; empty, or what failed. Once the record is written the swap is committed (<see cref="Committed"/>):
        /// a later failure, or a crash, only leaves the clearing to finish.
        /// </summary>
        std::string Forward()
        {
            if (!RecordWritten())
            {
                if (std::string error = MoveFiles(); !error.empty())
                {
                    return error;
                }

                if (!WriteDurably(_Disk, At(MCInstalled::RecordPath), MCInstalled::Text(Record())))
                {
                    // Moved into place with only its folder's flush failing, the record names the new set: the swap is
                    // committed, and the old files mustn't go back under it (the next start finishes the clearing).
                    _Committed = RecordWritten();
                    return "the installed record can't be written";
                }
            }

            _Committed = true;

            if (std::string error = KeepSetAside(); !error.empty())
            {
                return error;
            }

            // Flushed before the journal goes, or a crash could keep its deletion and lose the record of what to clear.
            // A repair under way is done once a swap is committed.
            if (!_Disk.Remove(At(ReplacedFolder)) || !_Disk.Remove(At(StagedFolder)) || !_Disk.Remove(At(RepairFile)) ||
                !_Disk.FlushFolder(At(UpdateFolder)))
            {
                return "the update's folders can't be cleared";
            }

            if (!_Disk.Remove(At(JournalFile)) || !_Disk.FlushFolder(At(UpdateFolder)))
            {
                return "the journal can't be deleted";
            }

            RemoveEmptiedFolders();
            return {};
        }

        /// <summary>Whether the installed record says the new set: the swap can then only go forward.</summary>
        bool Committed() const { return _Committed; }

        /// <summary>Whether the installed record on disk is the one this journal writes (its swap's id).</summary>
        bool RecordWritten()
        {
            if (_Journal.Record.Swap.empty())
            {
                return false;
            }

            const auto text = _Disk.ReadText(At(MCInstalled::RecordPath));
            const auto record = text ? MCInstalled::FromText(*text) : std::unexpected(text.error());
            return record && record->Swap == _Journal.Record.Swap;
        }

        /// <summary>
        /// Gives the swap up, for a repair (<see cref="MCContentSwap::Abandon"/>): each file in replaced/
        /// (<paramref name="setAside"/>, relative to it) kept or let go as the commit would, one the journal doesn't name
        /// kept; then the record names each new file in place as an update's, and replaced/ goes. Empty, or what failed.
        /// </summary>
        std::string GiveUp(const std::vector<std::string>& setAside)
        {
            std::map<std::string_view, const MCMove*> moves;

            for (const MCMove& move : _Journal.Moves)
            {
                moves.emplace(move.Path, &move);
            }

            for (const std::string& path : setAside)
            {
                const auto found = moves.find(path);
                const MCMove unknown{.Path = path, .Keep = MCKeep::Always};

                if (std::string error = KeepOne(found != moves.end() ? *found->second : unknown); !error.empty())
                {
                    return error;
                }
            }

            if (!FlushTouched())
            {
                return std::format("{}'s folders can't be flushed", MCContentList::BackupFolder);
            }

            if (std::string error = RecordPlacedNewFiles(); !error.empty())
            {
                return error;
            }

            if (!_Disk.Remove(At(ReplacedFolder)) || !_Disk.FlushFolder(At(UpdateFolder)))
            {
                return "the files set aside can't be cleared";
            }

            return {};
        }

    private:
        /// <summary>
        /// Deletes the folders the removed files leave empty (a set dropped: HD OPTIONS removing HD), deepest first, up
        /// to the game folder. Tidying only: a folder left behind holds nothing.
        /// </summary>
        void RemoveEmptiedFolders()
        {
            std::set<std::filesystem::path> folders;

            for (const MCMove& move : _Journal.Moves)
            {
                if (move.New)
                {
                    continue;
                }

                for (std::filesystem::path folder = MCFileSystem::FromUtf8(move.Path).parent_path(); !folder.empty();
                     folder = folder.parent_path())
                {
                    folders.insert(folder);
                }
            }

            // Longer paths first: a folder's own folders come after it.
            std::vector<std::filesystem::path> deepestFirst(folders.begin(), folders.end());
            std::ranges::sort(deepestFirst, [](const std::filesystem::path& a, const std::filesystem::path& b)
                              { return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end()); });

            for (const std::filesystem::path& folder : deepestFirst)
            {
                _Disk.RemoveIfEmpty(_Game / folder);
            }
        }

        /// <summary>Moves the old files aside, flushes, then the new ones in, flushes; empty, or what failed.</summary>
        std::string MoveFiles()
        {
            for (const MCMove& move : _Journal.Moves)
            {
                if (TooLate())
                {
                    return std::string(TooLateError);
                }

                const auto at = Classify(move);

                if (!at)
                {
                    return at.error();
                }

                if (*at == MCAt::Folder)
                {
                    return std::format("{}: a folder is in its place", move.Path);
                }

                if (*at != MCAt::Old && *at != MCAt::Other)
                {
                    continue;
                }

                // Only one file is ever set aside at a path: the next start's put back finds it there.
                if (_Disk.Stat(Replaced(move)).Exists)
                {
                    return std::format("{} can't be moved aside: another copy is already set aside", move.Path);
                }

                if (!Move(Destination(move), Replaced(move)))
                {
                    return std::format("{} can't be moved aside", move.Path);
                }
            }

            if (!FlushTouched())
            {
                return "the old files' folders can't be flushed";
            }

            for (const MCMove& move : _Journal.Moves)
            {
                if (!move.New)
                {
                    continue;
                }

                if (TooLate())
                {
                    return std::string(TooLateError);
                }

                if (_Disk.Stat(Destination(move)).Exists)
                {
                    const auto at = Classify(move);

                    if (!at)
                    {
                        return at.error();
                    }

                    if (*at == MCAt::Old)
                    {
                        return std::format("{} is still in place", move.Path);
                    }

                    if (*at != MCAt::New)
                    {
                        return std::format("{} changed while the update went in", move.Path);
                    }

                    continue;
                }

                if (const MCUpdateDisk::MCStat staged = _Disk.Stat(Staged(move));
                    !staged.Exists || staged.Folder || staged.Size != move.NewSize)
                {
                    return std::format("{}'s staged copy is missing or the wrong size", move.Path);
                }

                if (!Move(Staged(move), Destination(move)))
                {
                    return std::format("{} can't be moved into place", move.Path);
                }
            }

            if (!FlushTouched())
            {
                return "the new files' folders can't be flushed";
            }

            return {};
        }

    public:
        /// <summary>
        /// Puts the previous set back: each new file back in staged/, each old one in place; the journal becomes
        /// ready.json again, for the next start. Empty, or what failed.
        /// </summary>
        std::string Back()
        {
            for (const MCMove& move : std::views::reverse(_Journal.Moves))
            {
                if (move.New && _Disk.Stat(Destination(move)).Exists)
                {
                    const auto at = Classify(move);

                    if (!at)
                    {
                        return at.error();
                    }

                    if (*at == MCAt::New && !Move(Destination(move), Staged(move)))
                    {
                        return std::format("{} can't be moved back to staged", move.Path);
                    }
                }

                if (_Disk.Stat(Replaced(move)).Exists)
                {
                    if (_Disk.Stat(Destination(move)).Exists)
                    {
                        return std::format("{} can't be put back: something else is in its place", move.Path);
                    }

                    if (!Move(Replaced(move), Destination(move)))
                    {
                        return std::format("{} can't be put back", move.Path);
                    }
                }
            }

            if (!FlushTouched())
            {
                return "the folders can't be flushed";
            }

            if (!_Disk.Rename(At(JournalFile), At(ReadyFile)) || !_Disk.FlushFolder(At(UpdateFolder)))
            {
                return "the journal can't be set aside";
            }

            return {};
        }

    private:
        std::filesystem::path At(std::string_view relative) const { return _Game / MCFileSystem::FromUtf8(relative); }

        std::filesystem::path Destination(const MCMove& move) const { return At(move.Path); }
        std::filesystem::path Staged(const MCMove& move) const
        {
            return At(StagedFolder) / MCFileSystem::FromUtf8(move.Path);
        }

        std::filesystem::path Replaced(const MCMove& move) const
        {
            return At(ReplacedFolder) / MCFileSystem::FromUtf8(move.Path);
        }

        /// <summary>What's at the move's path (<see cref="What"/>).</summary>
        std::expected<MCAt, std::string> Classify(const MCMove& move) { return What(Destination(move), move); }

        /// <summary>
        /// What the file at <paramref name="path"/> is to <paramref name="move"/>: old without hashing when its size
        /// and time are as planned (a move keeps them), else by its hash.
        /// </summary>
        std::expected<MCAt, std::string> What(const std::filesystem::path& path, const MCMove& move)
        {
            const MCUpdateDisk::MCStat stat = _Disk.Stat(path);

            if (!stat.Exists)
            {
                return MCAt::Missing;
            }

            if (stat.Folder)
            {
                return MCAt::Folder;
            }

            if (move.Old && stat.Size == move.OldSize && move.OldWritten != 0 && stat.Written == move.OldWritten)
            {
                return MCAt::Old;
            }

            const auto hash = _Disk.Hash(path);

            if (!hash)
            {
                return std::unexpected(hash.error());
            }

            if (*hash == move.New)
            {
                return MCAt::New;
            }

            if (*hash == move.Old)
            {
                return MCAt::Old;
            }

            return MCAt::Other;
        }

        /// <summary>
        /// Once committed: each file set aside that an update didn't place, as placed (planned so, or another found at
        /// the swap: the player's), moves from replaced/ into Redux backup at its path; the rest, and every file of a
        /// set taken out, goes with replaced/. Empty, or what failed (the next start tries again).
        /// </summary>
        std::string KeepSetAside()
        {
            for (const MCMove& move : _Journal.Moves)
            {
                if (!_Disk.Stat(Replaced(move)).Exists)
                {
                    continue;
                }

                if (std::string error = KeepOne(move); !error.empty())
                {
                    return error;
                }
            }

            return FlushTouched() ? std::string()
                                  : std::format("{}'s folders can't be flushed", MCContentList::BackupFolder);
        }

        /// <summary>
        /// The file <paramref name="move"/> set aside, in replaced/: moved into Redux backup when an update didn't place
        /// it as it is, else left to go with replaced/. Empty, or what failed.
        /// </summary>
        std::string KeepOne(const MCMove& move)
        {
            if (move.Keep == MCKeep::Never)
            {
                return {};
            }

            if (move.Keep == MCKeep::IfChanged)
            {
                const auto at = What(Replaced(move), move);

                if (!at)
                {
                    return at.error();
                }

                if (*at == MCAt::Old)
                {
                    return {};
                }
            }

            const std::optional<std::filesystem::path> kept = KeptAt(move);

            if (!kept)
            {
                return std::format("{} has no free name in {}", move.Path, MCContentList::BackupFolder);
            }

            if (!Move(Replaced(move), *kept))
            {
                return std::format("{} can't be kept in {}", move.Path, MCContentList::BackupFolder);
            }

            return {};
        }

        /// <summary>
        /// The installed record with each new file the swap moved into place named as the journal's record has it,
        /// placed, so a plan neither hashes it again nor keeps it in Redux backup when it goes. A record that can't be
        /// read starts again empty (a plan then hashes every file). Empty, or what failed.
        /// </summary>
        std::string RecordPlacedNewFiles()
        {
            const auto text = _Disk.ReadText(At(MCInstalled::RecordPath));
            auto read = text ? MCInstalled::FromText(*text) : std::unexpected(text.error());
            MCInstalled::MCRecord record;

            if (read)
            {
                record = std::move(*read);
            }
            else if (_Disk.Stat(At(MCInstalled::RecordPath)).Exists)
            {
                SDL_Log("Updates: the installed record can't be read (%s): the repair starts it again",
                        read.error().c_str());
            }

            for (const MCMove& move : _Journal.Moves)
            {
                const auto entry = _Journal.Record.Files.find(MCContentList::FoldedPath(move.Path));

                if (!move.New || entry == _Journal.Record.Files.end())
                {
                    continue;
                }

                const auto at = Classify(move);

                if (!at)
                {
                    return at.error();
                }

                if (*at != MCAt::New)
                {
                    continue;
                }

                MCInstalled::MCFile file = entry->second;
                file.Written = _Disk.Stat(Destination(move)).Written;
                file.Placed = true;
                record.Files.insert_or_assign(entry->first, std::move(file));
            }

            return WriteDurably(_Disk, At(MCInstalled::RecordPath), MCInstalled::Text(record))
                       ? std::string()
                       : std::string("the installed record can't be written");
        }

        /// <summary>
        /// Where a file set aside is kept: its path in Redux backup, the folder's own copy there staying (the uninstall
        /// puts it back), so a later one goes beside it as "name (2).ext", "name (3).ext" and on; none when all are taken.
        /// </summary>
        std::optional<std::filesystem::path> KeptAt(const MCMove& move) const
        {
            const std::filesystem::path first = At(MCContentList::BackupFolder) / MCFileSystem::FromUtf8(move.Path);

            if (!_Disk.Stat(first).Exists)
            {
                return first;
            }

            const std::string stem = MCFileSystem::ToUtf8(first.stem());
            const std::string extension = MCFileSystem::ToUtf8(first.extension());

            for (int copy = FirstNumberedCopy; copy <= LastNumberedCopy; ++copy)
            {
                const std::filesystem::path numbered =
                    first.parent_path() / MCFileSystem::FromUtf8(std::format("{} ({}){}", stem, copy, extension));

                if (!_Disk.Stat(numbered).Exists)
                {
                    return numbered;
                }
            }

            return std::nullopt;
        }

        /// <summary>Moves a file, noting both folders for the next flush.</summary>
        bool Move(const std::filesystem::path& from, const std::filesystem::path& to)
        {
            if (!_Disk.MakeFolders(to.parent_path()) || !_Disk.Rename(from, to))
            {
                return false;
            }

            _Touched.insert(from.parent_path());
            _Touched.insert(to.parent_path());
            return true;
        }

        /// <summary>Whether the deadline has passed (none: never).</summary>
        bool TooLate() const { return _Deadline && std::chrono::steady_clock::now() > *_Deadline; }

        bool FlushTouched()
        {
            const bool flushed = std::ranges::all_of(_Touched, [&](const std::filesystem::path& folder)
                                                     { return _Disk.FlushFolder(folder); });
            _Touched.clear();
            return flushed;
        }

        /// <summary>The record the journal holds, with the new files' times.</summary>
        MCInstalled::MCRecord Record()
        {
            MCInstalled::MCRecord record = _Journal.Record;

            for (const MCMove& move : _Journal.Moves)
            {
                if (!move.New)
                {
                    continue;
                }

                if (const auto entry = record.Files.find(MCContentList::FoldedPath(move.Path));
                    entry != record.Files.end())
                {
                    entry->second.Written = _Disk.Stat(Destination(move)).Written;
                }
            }

            return record;
        }

        MCUpdateDisk& _Disk;
        std::filesystem::path _Game;
        MCJournal _Journal;
        /// <summary>The folders whose entries changed since the last flush.</summary>
        std::set<std::filesystem::path> _Touched;
        std::optional<std::chrono::steady_clock::time_point> _Deadline;
        bool _Committed = false;
    };

    MCJson MoveJson(const MCMove& move)
    {
        MCJson json{{PathKey, move.Path}};

        if (move.Old)
        {
            json[std::string(OldKey)] = MCSha256::Hex(*move.Old);
            json[std::string(OldSizeKey)] = move.OldSize;
            json[std::string(OldWrittenKey)] = move.OldWritten;
        }

        if (move.New)
        {
            json[std::string(NewKey)] = MCSha256::Hex(*move.New);
            json[std::string(NewSizeKey)] = move.NewSize;
        }

        if (move.Keep != MCKeep::IfChanged)
        {
            json[std::string(KeepKey)] = move.Keep == MCKeep::Always ? KeepAlways : KeepNever;
        }

        return json;
    }

    MCMove ReadMove(const MCJson& json)
    {
        MCMove move;
        move.Path = Text(json, PathKey);

        if (const std::string problem = MCContentList::PathProblem(move.Path); !problem.empty())
        {
            Fail("\"{}\" {}", move.Path, problem);
        }

        if (json.contains(OldKey))
        {
            move.Old = Digest(json, OldKey);
            move.OldSize = Count(json, OldSizeKey);
            const MCJson& written = Member(json, OldWrittenKey);

            if (!written.is_number_integer())
            {
                Fail("\"{}\" isn't a whole number", OldWrittenKey);
            }

            move.OldWritten = written.get<int64_t>();
        }

        if (json.contains(NewKey))
        {
            move.New = Digest(json, NewKey);
            move.NewSize = Count(json, NewSizeKey);
        }

        if (json.contains(KeepKey))
        {
            const std::string keep = Text(json, KeepKey);

            if (keep != KeepAlways && keep != KeepNever)
            {
                Fail("\"{}\" is \"{}\", not \"{}\" or \"{}\"", KeepKey, keep, KeepAlways, KeepNever);
            }

            move.Keep = keep == KeepAlways ? MCKeep::Always : MCKeep::Never;
        }

        if (!move.Old && !move.New)
        {
            Fail("\"{}\" is neither added, replaced nor removed", move.Path);
        }

        return move;
    }
}

std::string MCContentSwap::JournalText(const MCJournal& journal)
{
    MCJson moves = MCJson::array();

    for (const MCMove& move : journal.Moves)
    {
        moves.push_back(MoveJson(move));
    }

    return MCJson{{MovesKey, std::move(moves)},
                  {RecordKey, MCJson::parse(MCInstalled::Text(journal.Record))},
                  {LowestProgramKey, journal.LowestProgram}}
        .dump(1);
}

std::expected<MCJournal, std::string> MCContentSwap::ReadJournal(std::string_view text)
{
    return MCUpdateJson::Read("the update's journal",
                              [&]()
                              {
                                  const MCJson json = Object(text);
                                  MCJournal journal;

                                  for (const MCJson& move : Array(json, MovesKey))
                                  {
                                      journal.Moves.push_back(ReadMove(move));
                                  }

                                  auto record = MCInstalled::FromText(Child(json, RecordKey).dump());

                                  if (!record)
                                  {
                                      Fail("{}", record.error());
                                  }

                                  journal.Record = std::move(*record);

                                  // Written since the swap checks the program: a journal from before has none.
                                  if (json.contains(LowestProgramKey))
                                  {
                                      journal.LowestProgram = Count(json, LowestProgramKey);
                                  }

                                  return journal;
                              });
}

bool MCContentSwap::MarkReady(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, const MCJournal& journal)
{
    // A random id per swap: the record it writes says it's done, even when an earlier swap wrote the same release.
    std::random_device random;
    MCJournal ready = journal;
    ready.Record.Swap = std::format("{:08x}{:08x}{:08x}{:08x}", random(), random(), random(), random());
    return WriteDurably(disk, gameFolder / MCFileSystem::FromUtf8(ReadyFile), JournalText(ready));
}

bool MCContentSwap::Waiting(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    return disk.Stat(gameFolder / MCFileSystem::FromUtf8(JournalFile)).Exists ||
           disk.Stat(gameFolder / MCFileSystem::FromUtf8(ReadyFile)).Exists;
}

namespace
{
    /// <summary>
    /// The reason <paramref name="journal"/>'s content can't go in under build <paramref name="runningBuild"/> of the
    /// program (none: any will do), or empty.
    /// </summary>
    std::string TooNew(const MCJournal& journal, std::optional<uint64_t> runningBuild)
    {
        if (!runningBuild || journal.LowestProgram <= *runningBuild)
        {
            return {};
        }

        return std::format("release {}'s content needs build {} of the program, newer than {}", journal.Record.Release,
                           journal.LowestProgram, *runningBuild);
    }

    /// <summary>
    /// Disarms an update whose program never went in (<paramref name="reason"/>): <c>ready.json</c> goes, its files
    /// stay staged for the next INSTALL, and the previous set plays.
    /// </summary>
    MCContentSwap::MCResult Disarm(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, std::string reason)
    {
        SDL_Log("Updates: the staged update isn't put in place: %s; it's disarmed, its files kept staged",
                reason.c_str());

        // Left armed, the next start disarms it again: it's never swapped in under this program.
        if (!disk.Remove(gameFolder / MCFileSystem::FromUtf8(ReadyFile)) ||
            !disk.FlushFolder(gameFolder / MCFileSystem::FromUtf8(UpdateFolder)))
        {
            SDL_Log("Updates: the update can't be disarmed: the next start tries again");
        }

        return {MCOutcome::Disarmed, std::move(reason)};
    }

    /// <summary>The swap, a repair under way aside (<see cref="MCContentSwap::Run"/>).</summary>
    MCContentSwap::MCResult SwapOnce(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                     std::optional<std::chrono::steady_clock::time_point> deadline,
                                     std::optional<uint64_t> runningBuild)
    {
        const std::filesystem::path journalPath = gameFolder / MCFileSystem::FromUtf8(JournalFile);
        const std::filesystem::path readyPath = gameFolder / MCFileSystem::FromUtf8(ReadyFile);
        const bool begun = disk.Stat(journalPath).Exists;

        if (!begun && !disk.Stat(readyPath).Exists)
        {
            return {};
        }

        const auto text = disk.ReadText(begun ? journalPath : readyPath);
        auto journal = text ? ReadJournal(*text) : std::unexpected(text.error());

        if (!begun)
        {
            if (journal)
            {
                if (std::string tooNew = TooNew(*journal, runningBuild); !tooNew.empty())
                {
                    return Disarm(disk, gameFolder, std::move(tooNew));
                }
            }

            if (!disk.Rename(readyPath, journalPath) ||
                !disk.FlushFolder(gameFolder / MCFileSystem::FromUtf8(UpdateFolder)))
            {
                return {MCOutcome::PutBack, "the swap couldn't begin"};
            }
        }

        if (!journal)
        {
            return {MCOutcome::Stuck, journal.error()};
        }

        std::string tooNew = TooNew(*journal, runningBuild);
        MCSwapper swapper(disk, gameFolder, std::move(*journal), deadline);

        // Begun under a newer program (one beside this, sharing the folder), and not yet committed: it goes back.
        if (!tooNew.empty() && !swapper.RecordWritten())
        {
            if (const std::string back = swapper.Back(); !back.empty())
            {
                return {MCOutcome::Stuck, std::format("{}; then {}", tooNew, back)};
            }

            return Disarm(disk, gameFolder, std::move(tooNew));
        }

        const std::string forward = swapper.Forward();

        if (forward.empty())
        {
            return {MCOutcome::Swapped, {}};
        }

        if (swapper.Committed())
        {
            SDL_Log("Updates: the update is in place, but %s: the next start finishes clearing it", forward.c_str());
            return {MCOutcome::Swapped, forward};
        }

        SDL_Log("Updates: the swap stopped (%s): putting the previous files back", forward.c_str());

        if (const std::string back = swapper.Back(); !back.empty())
        {
            return {MCOutcome::Stuck, std::format("{}; then {}", forward, back)};
        }

        return {MCOutcome::PutBack, forward};
    }
}

bool MCContentSwap::Repairing(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    return disk.Stat(gameFolder / MCFileSystem::FromUtf8(RepairFile)).Exists;
}

std::expected<void, std::string> MCContentSwap::Abandon(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    const std::filesystem::path journalPath = gameFolder / MCFileSystem::FromUtf8(JournalFile);
    const std::filesystem::path repairPath = gameFolder / MCFileSystem::FromUtf8(RepairFile);
    const std::filesystem::path readyPath = gameFolder / MCFileSystem::FromUtf8(ReadyFile);
    const std::filesystem::path updateFolder = gameFolder / MCFileSystem::FromUtf8(UpdateFolder);

    // The journal becomes the repair's first, so from here the game doesn't play the files until a swap is committed. One
    // already there was given up before (a journal now is the repair's own swap, stopped midway).
    if (disk.Stat(journalPath).Exists && (!disk.Rename(journalPath, repairPath) || !disk.FlushFolder(updateFolder)))
    {
        return std::unexpected("the update's journal can't be set aside for the repair");
    }

    if (!disk.Stat(repairPath).Exists)
    {
        return {};
    }

    // The repair's own update, put back whole: its files wait in staged/, where the next staging finds them.
    if (disk.Stat(readyPath).Exists && (!disk.Remove(readyPath) || !disk.FlushFolder(updateFolder)))
    {
        return std::unexpected("the update put back can't be cleared for the repair");
    }

    const auto text = disk.ReadText(repairPath);
    auto journal = text ? ReadJournal(*text) : std::unexpected(text.error());

    if (!journal)
    {
        // Every file set aside is then kept: nothing says which an update placed.
        SDL_Log("Updates: the journal of the update given up can't be read (%s): every file it set aside is kept",
                journal.error().c_str());
        journal = MCJournal{};
    }

    const auto setAside = disk.Files(gameFolder / MCFileSystem::FromUtf8(ReplacedFolder));

    if (!setAside)
    {
        return std::unexpected(setAside.error());
    }

    if (std::string error = MCSwapper(disk, gameFolder, std::move(*journal), std::nullopt).GiveUp(*setAside);
        !error.empty())
    {
        return std::unexpected(error);
    }

    return {};
}

MCContentSwap::MCResult MCContentSwap::Run(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                           std::optional<std::chrono::steady_clock::time_point> deadline,
                                           std::optional<uint64_t> runningBuild)
{
    MCResult result = SwapOnce(disk, gameFolder, deadline, runningBuild);

    // The files a repair gave up on aren't a set to play: only a committed swap ends it.
    if (result.Outcome != MCOutcome::Swapped && result.Outcome != MCOutcome::Stuck && Repairing(disk, gameFolder))
    {
        return {MCOutcome::Stuck,
                result.Outcome == MCOutcome::Nothing ? std::string(RepairUnfinishedError) : std::move(result.Error)};
    }

    return result;
}

MCContentSwap::MCResult MCContentSwap::RunAtStart(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                  MCUpdateLock* lock, uint64_t runningBuild,
                                                  std::optional<std::chrono::steady_clock::time_point> deadline)
{
    if (lock == nullptr)
    {
        return Run(disk, gameFolder, deadline, runningBuild);
    }

    MCResult result;

    if (Waiting(disk, gameFolder) || Repairing(disk, gameFolder))
    {
        const auto now = std::chrono::steady_clock::now();
        const auto midwayUntil = deadline.value_or(now + MidwayWait);
        const auto quittingUntil = std::min(midwayUntil, now + QuittingWait);
        const std::filesystem::path journalPath = gameFolder / MCFileSystem::FromUtf8(JournalFile);

        for (;;)
        {
            if (lock->TryExclusive())
            {
                result = Run(disk, gameFolder, deadline, runningBuild);
                break;
            }

            const bool midway = disk.Stat(journalPath).Exists || Repairing(disk, gameFolder);
            const auto waited = std::chrono::steady_clock::now();

            if (!midway && waited >= quittingUntil)
            {
                result = {MCOutcome::PutBack, "another copy of the game is running"};
                break;
            }

            if (midway && waited >= midwayUntil)
            {
                result = {MCOutcome::Stuck, "another copy of the game is putting an update in place"};
                break;
            }

            std::this_thread::sleep_for(LockRetry);
        }
    }

    if (!lock->Shared())
    {
        SDL_Log("Updates: the game's lock can't be held: another copy may swap an update in under this one");
    }

    return result;
}
