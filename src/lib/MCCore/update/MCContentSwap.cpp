#include "stdafx.h"
#include "update/MCContentSwap.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"

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
    constexpr std::string_view TooLateError = "the swap took longer than a lobby's start allows";

    /// <summary>What's at a move's path.</summary>
    enum class MCAt : uint8_t
    {
        Missing,
        Old,
        New,
        /// <summary>Neither: the player's (changed since staging, or added in a new file's place).</summary>
        Other,
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
                    return "the installed record can't be written";
                }
            }

            _Committed = true;

            // Flushed before the journal goes, or a crash could keep its deletion and lose theirs.
            if (!_Disk.Remove(At(ReplacedFolder)) || !_Disk.Remove(At(StagedFolder)) ||
                !_Disk.FlushFolder(At(UpdateFolder)))
            {
                return "the update's folders can't be cleared";
            }

            if (!_Disk.Remove(At(JournalFile)) || !_Disk.FlushFolder(At(UpdateFolder)))
            {
                return "the journal can't be deleted";
            }

            return {};
        }

        /// <summary>Whether the installed record says the new set: the swap can then only go forward.</summary>
        bool Committed() const { return _Committed; }

    private:
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

                if (*at == MCAt::Old && !Move(Destination(move), Replaced(move)))
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
                if (!move.New || _Theirs.contains(move.Path))
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

    public:
        /// <summary>
        /// Puts the previous set back: each new file back in staged/, each old one in place; the journal becomes
        /// ready.json again, for the next start. Empty, or what failed.
        /// </summary>
        std::string Back()
        {
            for (const MCMove& move : std::views::reverse(_Journal.Moves))
            {
                if (move.New && !_Theirs.contains(move.Path) && _Disk.Stat(Destination(move)).Exists)
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

        /// <summary>
        /// What's at the move's path: old without hashing when its size and time are as planned, else by its hash. A
        /// file that's neither is the player's: kept in <see cref="_Theirs"/>, and left alone from then.
        /// </summary>
        std::expected<MCAt, std::string> Classify(const MCMove& move)
        {
            const MCUpdateDisk::MCStat stat = _Disk.Stat(Destination(move));

            if (!stat.Exists)
            {
                return MCAt::Missing;
            }

            if (!stat.Folder && move.Old && stat.Size == move.OldSize && move.OldWritten != 0 &&
                stat.Written == move.OldWritten)
            {
                return MCAt::Old;
            }

            std::optional<MCSha256::MCDigest> hash;

            if (!stat.Folder)
            {
                const auto read = _Disk.Hash(Destination(move));

                if (!read)
                {
                    return std::unexpected(read.error());
                }

                hash = *read;
            }

            if (hash && hash == move.New)
            {
                return MCAt::New;
            }

            if (hash && hash == move.Old)
            {
                return MCAt::Old;
            }

            _Theirs.insert_or_assign(
                move.Path, MCTheirs{hash.value_or(MCSha256::MCDigest{}), stat.Size, stat.Written, stat.Folder});
            return MCAt::Other;
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

        /// <summary>The record the journal holds, with the new files' times and the player's files as theirs.</summary>
        MCInstalled::MCRecord Record()
        {
            MCInstalled::MCRecord record = _Journal.Record;

            for (const MCMove& move : _Journal.Moves)
            {
                const std::string folded = MCContentList::FoldedPath(move.Path);
                const auto entry = record.Files.find(folded);

                if (const auto theirs = _Theirs.find(move.Path); theirs != _Theirs.end())
                {
                    if (theirs->second.Folder || entry == record.Files.end())
                    {
                        continue;
                    }

                    entry->second.Sha256 = theirs->second.Sha256;
                    entry->second.Size = theirs->second.Size;
                    entry->second.Written = theirs->second.Written;
                    entry->second.Ours = false;
                }
                else if (move.New && entry != record.Files.end())
                {
                    entry->second.Written = _Disk.Stat(Destination(move)).Written;
                }
            }

            return record;
        }

        struct MCTheirs
        {
            MCSha256::MCDigest Sha256{};
            uint64_t Size = 0;
            int64_t Written = 0;
            bool Folder = false;
        };

        MCUpdateDisk& _Disk;
        std::filesystem::path _Game;
        MCJournal _Journal;
        /// <summary>The folders whose entries changed since the last flush.</summary>
        std::set<std::filesystem::path> _Touched;
        /// <summary>The moves left alone: the player's file at the path.</summary>
        std::map<std::string, MCTheirs, std::less<>> _Theirs;
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

    return MCJson{{MovesKey, std::move(moves)}, {RecordKey, MCJson::parse(MCInstalled::Text(journal.Record))}}.dump(1);
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

MCContentSwap::MCResult MCContentSwap::Run(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                           std::optional<std::chrono::steady_clock::time_point> deadline)
{
    const std::filesystem::path journalPath = gameFolder / MCFileSystem::FromUtf8(JournalFile);

    if (!disk.Stat(journalPath).Exists)
    {
        const std::filesystem::path readyPath = gameFolder / MCFileSystem::FromUtf8(ReadyFile);

        if (!disk.Stat(readyPath).Exists)
        {
            return {};
        }

        if (!disk.Rename(readyPath, journalPath) ||
            !disk.FlushFolder(gameFolder / MCFileSystem::FromUtf8(UpdateFolder)))
        {
            return {MCOutcome::PutBack, "the swap couldn't begin"};
        }
    }

    const auto text = disk.ReadText(journalPath);

    if (!text)
    {
        return {MCOutcome::Stuck, text.error()};
    }

    auto journal = ReadJournal(*text);

    if (!journal)
    {
        return {MCOutcome::Stuck, journal.error()};
    }

    MCSwapper swapper(disk, gameFolder, std::move(*journal), deadline);
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
