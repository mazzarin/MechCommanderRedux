#include "stdafx.h"
#include "update/MCContentSwap.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
#include "update/MCInstalledJson.h"
#include "update/MCReleaseFile.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"
#include "update/MCUpdateLock.h"

#include <random>

namespace
{
    using namespace MCContentSwap;
    using namespace MCUpdateJson;

    constexpr std::string_view MovesKey = "moves";
    constexpr std::string_view RecordKey = "record";
    constexpr std::string_view PathKey = "path";
    constexpr std::string_view OldKey = "old";
    constexpr std::string_view OldSizeKey = "oldSize";
    constexpr std::string_view OldWrittenKey = "oldWritten";
    constexpr std::string_view NewKey = "new";
    constexpr std::string_view NewSizeKey = "newSize";
    constexpr std::string_view NewWrittenKey = "newWritten";
    constexpr std::string_view LowestProgramKey = "lowestProgram";
    /// <summary>A move's MCKeep, when not IfChanged: "always" or "never".</summary>
    constexpr std::string_view KeepKey = "keep";
    constexpr std::string_view KeepAlways = "always";
    constexpr std::string_view KeepNever = "never";
    /// <summary>
    /// A lobby's start moves the files forward for at most this part of the time left (a half), keeping the rest for
    /// putting the previous set back.
    /// </summary>
    constexpr int ForwardShare = 2;
    /// <summary>A later copy kept in Redux backup beside a path's first: "name (2).ext", then "(3)", up to the last.</summary>
    constexpr int FirstNumberedCopy = 2;
    constexpr int LastNumberedCopy = 999;
    constexpr std::string_view TooLateError = "the swap took longer than a lobby's start allows";
    constexpr std::string_view RepairUnfinishedError = "a repair stopped before it was done";
    constexpr std::string_view AnotherSwappingError = "another copy of the game is putting an update in place";
    constexpr std::string_view PutBackFailed = "something else is in its place, or it can't move";

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
        /// <summary>
        /// The swap of <paramref name="journal"/>, <paramref name="fresh"/> when it begins this run (from
        /// <c>ready.json</c>: nothing was set aside before). Moving the files stops at half the time left to
        /// <paramref name="deadline"/>, keeping the rest for putting the previous set back (the deadline is a target,
        /// not a promise: a put back that ends late still plays).
        /// </summary>
        MCSwapper(MCUpdateDisk& disk, std::filesystem::path game, MCJournal journal,
                  std::optional<std::chrono::steady_clock::time_point> deadline, bool fresh)
            : _Disk(disk), _Game(std::move(game)), _NoLinks(disk, _Game), _Journal(std::move(journal)), _Fresh(fresh)
        {
            if (deadline)
            {
                const auto now = std::chrono::steady_clock::now();
                _ForwardUntil = now + (*deadline - now) / ForwardShare;
            }
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

                if (!WriteDurably(_Disk, _Game, At(MCInstalled::RecordPath), MCInstalled::Text(Record())))
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
            // A repair under way is done once a swap is committed, and so is the content due, once it's this or older.
            if (!_Disk.Remove(At(ReplacedFolder)) || !_Disk.Remove(At(StagedFolder)) || !_Disk.Remove(At(RepairFile)) ||
                !ClearDue() || !_Disk.FlushFolder(At(UpdateFolder)))
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

        /// <summary>
        /// Deletes the content noted due (MCInstalled::DueFile) when this swap's release is it or later: false when it
        /// can't be. One that can't be read goes too (it waits for nothing).
        /// </summary>
        bool ClearDue()
        {
            const std::optional<MCInstalled::MCDue> due = MCInstalled::ReadDue(_Disk, _Game);

            if (due && MCReleaseFile::MCOrder{due->Epoch, due->Release} >
                           MCReleaseFile::MCOrder{_Journal.Record.Epoch, _Journal.Record.Release})
            {
                return true;
            }

            return _Disk.Remove(At(MCInstalled::DueFile));
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
            std::map<std::string_view, std::reference_wrapper<const MCMove>> moves;

            for (const MCMove& move : _Journal.Moves)
            {
                moves.emplace(move.Path, move);
            }

            for (const std::string& path : setAside)
            {
                const auto found = moves.find(path);
                const MCMove unknown{.Path = path, .Keep = MCKeep::Always};

                if (std::string error = KeepOne(found != moves.end() ? found->second.get() : unknown); !error.empty())
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
            for (size_t index = 0; index < _Journal.Moves.size(); ++index)
            {
                const MCMove& move = _Journal.Moves[index];

                if (TooLate())
                {
                    return std::string(TooLateError);
                }

                const auto at = Classify(index);

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
                const auto setAside = IsThere(_Disk, Replaced(move));

                if (!setAside)
                {
                    return setAside.error();
                }

                if (*setAside)
                {
                    return std::format("{} can't be moved aside: another copy is already set aside", move.Path);
                }

                if (const auto moved = Move(Destination(move), Replaced(move)); !moved)
                {
                    return Unmoved(std::format("{} can't be moved aside", move.Path), moved);
                }

                _MovedAside.push_back(index);
                _At.insert_or_assign(index, MCAt::Missing);
            }

            if (!FlushTouched())
            {
                return "the old files' folders can't be flushed";
            }

            for (size_t index = 0; index < _Journal.Moves.size(); ++index)
            {
                const MCMove& move = _Journal.Moves[index];

                if (!move.New)
                {
                    continue;
                }

                if (TooLate())
                {
                    return std::string(TooLateError);
                }

                const auto inPlace = IsThere(_Disk, Destination(move));

                if (!inPlace)
                {
                    return inPlace.error();
                }

                if (*inPlace)
                {
                    const auto at = Classify(index);

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

                const MCUpdateDisk::MCStat staged = _Disk.Stat(Staged(move));

                if (!staged.Error.empty())
                {
                    return staged.Error;
                }

                if (!staged.Exists || staged.Folder || staged.Size != move.NewSize)
                {
                    return std::format("{}'s staged copy is missing or the wrong size", move.Path);
                }

                if (const auto moved = Move(Staged(move), Destination(move)); !moved)
                {
                    return Unmoved(std::format("{} can't be moved into place", move.Path), moved);
                }

                _MovedIn.push_back(index);
                _At.insert_or_assign(index, MCAt::New);
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
        /// ready.json again, for the next start. A swap begun this run moves back just what it moved, looking at
        /// nothing again; one a crash stopped looks at each move's files. It has no deadline: the previous set must be
        /// whole before anything plays. Empty, or what failed.
        /// </summary>
        std::string Back()
        {
            if (std::string error = _Fresh ? BackWhatMoved() : BackEveryMove(); !error.empty())
            {
                return error;
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
        /// <summary>
        /// Back, a swap begun this run: what it moved, last first, as <see cref="BackEveryMove"/> would (a move's new file
        /// back to staged/ before its old one comes back), none looked at again.
        /// </summary>
        std::string BackWhatMoved()
        {
            // Both lists hold positions in the journal, in its order, so the later of their last entries is the last
            // move made.
            auto in = _MovedIn.rbegin();
            auto aside = _MovedAside.rbegin();

            while (in != _MovedIn.rend() || aside != _MovedAside.rend())
            {
                if (in != _MovedIn.rend() && (aside == _MovedAside.rend() || *in >= *aside))
                {
                    const MCMove& move = _Journal.Moves[*in];

                    if (const auto moved = Move(Destination(move), Staged(move)); !moved)
                    {
                        return Unmoved(std::format("{} can't be moved back to staged", move.Path), moved);
                    }

                    _At.insert_or_assign(*in, MCAt::Missing);
                    ++in;
                    continue;
                }

                const MCMove& move = _Journal.Moves[*aside];

                if (const auto moved = Move(Replaced(move), Destination(move)); !moved)
                {
                    return Unmoved(std::format("{} can't be put back", move.Path), moved, PutBackFailed);
                }

                _At.erase(*aside);
                ++aside;
            }

            return {};
        }

        /// <summary>Back, a swap a crash stopped: each move's files looked at, last first.</summary>
        std::string BackEveryMove()
        {
            for (size_t index = _Journal.Moves.size(); index-- > 0;)
            {
                const MCMove& move = _Journal.Moves[index];

                if (move.New)
                {
                    // A new file in place goes back to staged/; anything else there is left as it is.
                    const auto at = Classify(index);

                    if (!at)
                    {
                        return at.error();
                    }

                    // Its staged copy still there, the swap never moved it in (it was in place already): it stays.
                    std::expected<bool, std::string> staged = true;

                    if (*at == MCAt::New)
                    {
                        staged = IsThere(_Disk, Staged(move));
                    }

                    if (!staged)
                    {
                        return staged.error();
                    }

                    if (*at == MCAt::New && !*staged)
                    {
                        if (const auto moved = Move(Destination(move), Staged(move)); !moved)
                        {
                            return Unmoved(std::format("{} can't be moved back to staged", move.Path), moved);
                        }

                        _At.insert_or_assign(index, MCAt::Missing);
                    }
                }

                const auto setAside = IsThere(_Disk, Replaced(move));

                if (!setAside)
                {
                    return setAside.error();
                }

                if (*setAside)
                {
                    if (const auto moved = Move(Replaced(move), Destination(move)); !moved)
                    {
                        return Unmoved(std::format("{} can't be put back", move.Path), moved, PutBackFailed);
                    }

                    _At.erase(index);
                }
            }

            return {};
        }

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

        /// <summary>
        /// What's at the move's path (<see cref="What"/>), told once a run: the swap's own moves keep it up to date.
        /// </summary>
        std::expected<MCAt, std::string> Classify(size_t index)
        {
            if (const auto known = _At.find(index); known != _At.end())
            {
                return known->second;
            }

            const MCMove& move = _Journal.Moves.at(index);
            const auto at = What(Destination(move), move);

            if (at)
            {
                _At.emplace(index, *at);
            }

            return at;
        }

        /// <summary>
        /// What the file at <paramref name="path"/> is to <paramref name="move"/>: old or new without hashing when its
        /// size and time are the old file's as planned, or the staged one's as armed (a move keeps them); else by its
        /// hash.
        /// </summary>
        std::expected<MCAt, std::string> What(const std::filesystem::path& path, const MCMove& move)
        {
            const MCUpdateDisk::MCStat stat = _Disk.Stat(path);

            if (!stat.Error.empty())
            {
                return std::unexpected(stat.Error);
            }

            if (!stat.Exists)
            {
                return MCAt::Missing;
            }

            if (stat.Folder)
            {
                return MCAt::Folder;
            }

            const bool oldByTime =
                move.Old && move.OldWritten != 0 && stat.Size == move.OldSize && stat.Written == move.OldWritten;
            const bool newByTime =
                move.New && move.NewWritten != 0 && stat.Size == move.NewSize && stat.Written == move.NewWritten;

            if (oldByTime != newByTime)
            {
                return oldByTime ? MCAt::Old : MCAt::New;
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
                const auto setAside = IsThere(_Disk, Replaced(move));

                if (!setAside)
                {
                    return setAside.error();
                }

                if (!*setAside)
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

            const auto kept = KeptAt(move);

            if (!kept)
            {
                return kept.error();
            }

            if (const auto moved = Move(Replaced(move), *kept); !moved)
            {
                return Unmoved(std::format("{} can't be kept in {}", move.Path, MCContentList::BackupFolder), moved);
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

            for (size_t index = 0; index < _Journal.Moves.size(); ++index)
            {
                const MCMove& move = _Journal.Moves[index];
                const auto entry = _Journal.Record.Files.find(MCContentList::FoldedPath(move.Path));

                if (!move.New || entry == _Journal.Record.Files.end())
                {
                    continue;
                }

                const auto at = Classify(index);

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

            return WriteDurably(_Disk, _Game, At(MCInstalled::RecordPath), MCInstalled::Text(record))
                       ? std::string()
                       : std::string("the installed record can't be written");
        }

        /// <summary>
        /// Where a file set aside is kept: its path in Redux backup, the folder's own copy there staying (the uninstall
        /// puts it back), so a later one goes beside it as "name (2).ext", "name (3).ext" and on; or why none (all are
        /// taken, or a name can't be looked at).
        /// </summary>
        std::expected<std::filesystem::path, std::string> KeptAt(const MCMove& move) const
        {
            const std::filesystem::path first = At(MCContentList::BackupFolder) / MCFileSystem::FromUtf8(move.Path);
            const std::string stem = MCFileSystem::ToUtf8(first.stem());
            const std::string extension = MCFileSystem::ToUtf8(first.extension());

            for (int copy = FirstNumberedCopy - 1; copy <= LastNumberedCopy; ++copy)
            {
                const std::filesystem::path name =
                    copy < FirstNumberedCopy
                        ? first
                        : first.parent_path() / MCFileSystem::FromUtf8(std::format("{} ({}){}", stem, copy, extension));
                const auto taken = IsThere(_Disk, name);

                if (!taken)
                {
                    return std::unexpected(taken.error());
                }

                if (!*taken)
                {
                    return name;
                }
            }

            return std::unexpected(std::format("{} has no free name in {}", move.Path, MCContentList::BackupFolder));
        }

        /// <summary>
        /// Moves a file where nothing is (never replacing one), noting both folders for the next flush. Just before, no
        /// part of either path below the game folder may be a link (MCNoLinks).
        /// </summary>
        /// <returns>Nothing, or why not: a link on its way (named), else empty (the move failed).</returns>
        std::expected<void, std::string> Move(const std::filesystem::path& from, const std::filesystem::path& to)
        {
            for (const std::filesystem::path& path : {from, to})
            {
                if (auto checked = _NoLinks.Check(path); !checked)
                {
                    SDL_Log("Updates: %s isn't moved to %s: %s", MCFileSystem::ToUtf8(from).c_str(),
                            MCFileSystem::ToUtf8(to).c_str(), checked.error().c_str());
                    return checked;
                }
            }

            if (!_Disk.MakeFolders(to.parent_path()) || !_Disk.Move(from, to))
            {
                return std::unexpected(std::string());
            }

            _Touched.insert(from.parent_path());
            _Touched.insert(to.parent_path());
            return {};
        }

        /// <summary>
        /// A move that failed, as told: <paramref name="what"/>, and why (a link on its way, else
        /// <paramref name="usual"/> when given).
        /// </summary>
        static std::string Unmoved(std::string what, const std::expected<void, std::string>& moved,
                                   std::string_view usual = {})
        {
            const std::string_view why = moved.error().empty() ? usual : std::string_view(moved.error());
            return why.empty() ? what : std::format("{}: {}", what, why);
        }

        /// <summary>Whether moving the files forward is past its share of the deadline (none: never).</summary>
        bool TooLate() const { return _ForwardUntil && std::chrono::steady_clock::now() > *_ForwardUntil; }

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
        /// <summary>Each move's paths looked at just before it (<see cref="Move"/>).</summary>
        MCNoLinks _NoLinks;
        MCJournal _Journal;
        /// <summary>The folders whose entries changed since the last flush.</summary>
        std::set<std::filesystem::path> _Touched;
        /// <summary>When moving the files forward stops (<see cref="TooLate"/>).</summary>
        std::optional<std::chrono::steady_clock::time_point> _ForwardUntil;
        /// <summary>
        /// Whether the swap began this run, and what it moved aside and in, in order: each move's place in the journal.
        /// </summary>
        bool _Fresh = false;
        std::vector<size_t> _MovedAside;
        std::vector<size_t> _MovedIn;
        /// <summary>What's at each move's path, once told this run, by the move's place in the journal.</summary>
        std::map<size_t, MCAt> _At;
        bool _Committed = false;
    };

    /// <summary>The member <paramref name="key"/>, a whole number (a time written may be below 0).</summary>
    int64_t WholeNumber(const MCJson& json, std::string_view key)
    {
        const MCJson& number = Member(json, key);

        if (!number.is_number_integer())
        {
            Fail("\"{}\" isn't a whole number", key);
        }

        return number.get<int64_t>();
    }

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
            json[std::string(NewWrittenKey)] = move.NewWritten;
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
            move.OldWritten = WholeNumber(json, OldWrittenKey);
        }

        if (json.contains(NewKey))
        {
            move.New = Digest(json, NewKey);
            move.NewSize = Count(json, NewSizeKey);

            // Written since the swap tells a new file by its time: a journal from before has none (it's hashed).
            if (json.contains(NewWrittenKey))
            {
                move.NewWritten = WholeNumber(json, NewWrittenKey);
            }
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
                  {RecordKey, MCInstalled::ToJson(journal.Record)},
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

                                  auto record = MCInstalled::FromJson(Child(json, RecordKey));

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

    // Each staged file's time, which its move into place keeps: the swap then tells it without hashing it.
    for (MCMove& move : ready.Moves)
    {
        if (!move.New)
        {
            continue;
        }

        const std::filesystem::path staged =
            gameFolder / MCFileSystem::FromUtf8(StagedFolder) / MCFileSystem::FromUtf8(move.Path);
        const MCUpdateDisk::MCStat stat = disk.Stat(staged);

        if (!stat.Error.empty())
        {
            SDL_Log("Updates: the update isn't marked ready: %s", stat.Error.c_str());
            return false;
        }

        // One not there as planned is told by its hash, and the swap finds it lost.
        if (!stat.Exists || stat.Folder || stat.Size != move.NewSize)
        {
            SDL_Log("Updates: %s isn't staged as planned: the swap will say so", move.Path.c_str());
            continue;
        }

        move.NewWritten = stat.Written;
    }

    return WriteDurably(disk, gameFolder, gameFolder / MCFileSystem::FromUtf8(ReadyFile), JournalText(ready));
}

namespace
{
    /// <summary>Whether any of <paramref name="files"/> (in the update folder) is there, or why one can't be looked at.</summary>
    std::expected<bool, std::string> AnyThere(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                              std::initializer_list<std::string_view> files)
    {
        for (const std::string_view file : files)
        {
            const auto there = IsThere(disk, gameFolder / MCFileSystem::FromUtf8(file));

            if (!there || *there)
            {
                return there;
            }
        }

        return false;
    }
}

std::expected<bool, std::string> MCContentSwap::Waiting(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    return AnyThere(disk, gameFolder, {JournalFile, ReadyFile});
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
        const auto begun = IsThere(disk, journalPath);

        if (!begun)
        {
            return {MCOutcome::Stuck, begun.error()};
        }

        if (!*begun)
        {
            const auto ready = IsThere(disk, readyPath);

            if (!ready)
            {
                return {MCOutcome::PutBack, ready.error()};
            }

            if (!*ready)
            {
                return {};
            }
        }

        const auto text = disk.ReadText(*begun ? journalPath : readyPath);
        auto journal = text ? ReadJournal(*text) : std::unexpected(text.error());

        if (!*begun)
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
        MCSwapper swapper(disk, gameFolder, std::move(*journal), deadline, !*begun);

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

std::expected<bool, std::string> MCContentSwap::Repairing(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    return AnyThere(disk, gameFolder, {RepairFile});
}

std::expected<bool, std::string> MCContentSwap::UnderWay(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    return AnyThere(disk, gameFolder, {JournalFile, ReadyFile, RepairFile});
}

namespace
{
    /// <summary>
    /// Whether a swap was stopped midway, or a repair is under way: the files can't be played as they are; or why it
    /// can't be told.
    /// </summary>
    std::expected<bool, std::string> Midway(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
    {
        return AnyThere(disk, gameFolder, {JournalFile, RepairFile});
    }
}

std::expected<void, std::string> MCContentSwap::Abandon(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    const std::filesystem::path journalPath = gameFolder / MCFileSystem::FromUtf8(JournalFile);
    const std::filesystem::path repairPath = gameFolder / MCFileSystem::FromUtf8(RepairFile);
    const std::filesystem::path readyPath = gameFolder / MCFileSystem::FromUtf8(ReadyFile);
    const std::filesystem::path updateFolder = gameFolder / MCFileSystem::FromUtf8(UpdateFolder);

    // The journal becomes the repair's first, so from here the game doesn't play the files until a swap is committed. One
    // already there was given up before (a journal now is the repair's own swap, stopped midway).
    const auto begun = IsThere(disk, journalPath);

    if (!begun)
    {
        return std::unexpected(begun.error());
    }

    if (*begun && (!disk.Rename(journalPath, repairPath) || !disk.FlushFolder(updateFolder)))
    {
        return std::unexpected("the update's journal can't be set aside for the repair");
    }

    const auto repairing = IsThere(disk, repairPath);

    if (!repairing)
    {
        return std::unexpected(repairing.error());
    }

    if (!*repairing)
    {
        return {};
    }

    // The repair's own update, put back whole: its files wait in staged/, where the next staging finds them.
    const auto ready = IsThere(disk, readyPath);

    if (!ready)
    {
        return std::unexpected(ready.error());
    }

    if (*ready && (!disk.Remove(readyPath) || !disk.FlushFolder(updateFolder)))
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

    if (std::string error = MCSwapper(disk, gameFolder, std::move(*journal), std::nullopt, false).GiveUp(*setAside);
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

    if (result.Outcome == MCOutcome::Swapped || result.Outcome == MCOutcome::Stuck)
    {
        return result;
    }

    // The files a repair gave up on aren't a set to play: only a committed swap ends it. Nor are files whose repair
    // can't be looked for.
    const auto repairing = Repairing(disk, gameFolder);

    if (!repairing)
    {
        return {MCOutcome::Stuck, repairing.error()};
    }

    if (*repairing)
    {
        return {MCOutcome::Stuck,
                result.Outcome == MCOutcome::Nothing ? std::string(RepairUnfinishedError) : std::move(result.Error)};
    }

    return result;
}

MCContentSwap::MCResult MCContentSwap::RunAtStart(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                  MCOptionalRef<MCUpdateLock> lock, uint64_t runningBuild,
                                                  std::optional<std::chrono::steady_clock::time_point> deadline)
{
    if (!lock)
    {
        return Run(disk, gameFolder, deadline, runningBuild);
    }

    MCUpdateLock& gameLock = *lock;
    const auto now = std::chrono::steady_clock::now();
    const auto until = deadline.value_or(now + MidwayWait);
    MCResult result;
    // A folder whose update can't be looked for may be half swapped: it isn't played.
    const auto underWay = UnderWay(disk, gameFolder);

    if (!underWay)
    {
        return {MCOutcome::Stuck, underWay.error()};
    }

    if (*underWay)
    {
        // A copy quitting (RESTART starts this one as it closes) is waited for briefly; one midway through a swap or a
        // repair, whose files can't be played, up to the deadline.
        if (gameLock.ExclusiveBy(std::min(until, now + QuittingWait)))
        {
            result = Run(disk, gameFolder, deadline, runningBuild);
        }
        else if (const auto midway = Midway(disk, gameFolder); !midway)
        {
            return {MCOutcome::Stuck, midway.error()};
        }
        else if (!*midway)
        {
            result = {MCOutcome::PutBack, "another copy of the game is running"};
        }
        else if (gameLock.ExclusiveBy(until))
        {
            result = Run(disk, gameFolder, deadline, runningBuild);
        }
        else
        {
            return {MCOutcome::Stuck, std::string(AnotherSwappingError)};
        }
    }

    // Held shared for the game's run. Another copy holding it exclusive is swapping (or removing the content): waited
    // for no longer than the deadline, then this start doesn't play the files.
    if (!gameLock.SharedBy(until))
    {
        SDL_Log("Updates: the game's lock can't be held shared: another copy holds it");
        return {MCOutcome::Stuck, std::string(AnotherSwappingError)};
    }

    return result;
}
