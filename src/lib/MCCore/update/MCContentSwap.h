#pragma once

#include "update/MCContentList.h"
#include "update/MCInstalled.h"

#include <chrono>
#include <vector>

class MCUpdateDisk;
class MCUpdateLock;

/// <summary>
/// Port-only: putting a staged update in place (SPEC-install-and-updates.md's Applying an update: Swap and If it can't
/// finish). Staging leaves each new file, flushed and checked, in <c>Redux update/staged/</c> at its path, then
/// writes the journal-to-be (<c>ready.json</c>: each file moved, with its old and new hashes, and the record once
/// done). At the next start, before any archive opens:
///
/// 1. <c>ready.json</c> becomes <c>journal.json</c> (a rename, its folder flushed): the swap has begun.
/// 2. Each file replaced or removed is moved aside to <c>Redux update/replaced/</c>; the folders flushed.
/// 3. Each new file is moved from <c>staged/</c> into place; the folders flushed.
/// 4. The installed record is written durably: the swap is committed. The old files an update didn't place, as placed
///    (the folder's own, or the player's: planned so, or changed since staging), move from <c>replaced/</c> into
///    <c>Redux backup/</c> at their paths, the folders flushed, but those of a set taken out (<see cref="MCKeep"/>);
///    then <c>replaced/</c> and <c>staged/</c> go, then the journal.
///
/// An update whose content needs a newer program than the one starting (its program's half never went in: a setup that
/// failed, a crash before Sparkle installed) isn't swapped in: it's disarmed, <c>ready.json</c> going and
/// <c>staged/</c> staying for the next INSTALL, and the previous set plays.
///
/// A crash at any step leaves the journal, and the next start finishes the swap, judging each file by what's at its
/// path (its size and time, else its hash): old, new, missing, or another (the player's, set aside and kept as an old
/// one is). Nothing new moves in before every old file is safely aside, and no move ever replaces a file, so no old
/// file is ever lost. If the swap can't finish (a file held open, a full disk), the previous set is put back from
/// <c>replaced/</c> and the update stays staged for the next start; only a set that can't be put back stops the game,
/// on the recovery screen. In <c>Redux backup/</c> the first copy of a path stays: it's the folder's own, which the
/// uninstall puts back; a later one is kept beside it, numbered.
///
/// The recovery screen's REPAIR gives that swap up (<see cref="Abandon"/>) and installs the release afresh over the
/// files as they are; until a swap is committed, the game doesn't play them (<see cref="Repairing"/>).
/// </summary>
namespace MCContentSwap
{
    /// <summary>The update's folders and files, in the game folder.</summary>
    constexpr std::string_view UpdateFolder = MCContentList::UpdateFolder;
    constexpr std::string_view StagedFolder = MCContentList::InUpdateFolder<"staged">;
    constexpr std::string_view ReplacedFolder = MCContentList::InUpdateFolder<"replaced">;
    constexpr std::string_view ReadyFile = MCContentList::InUpdateFolder<"ready.json">;
    constexpr std::string_view JournalFile = MCContentList::InUpdateFolder<"journal.json">;
    /// <summary>A repair under way: the journal of the swap it gave up, kept until a swap is committed.</summary>
    constexpr std::string_view RepairFile = MCContentList::InUpdateFolder<"repair.json">;
    /// <summary>
    /// How long a multiplayer lobby's start means the swap to take, at most, so a room isn't held up (a normal swap
    /// takes well under a second): a target, not a promise. The files move forward for half of it; past that the
    /// previous set goes back, which isn't cut short (it must be whole to play).
    /// </summary>
    constexpr std::chrono::seconds LobbyStartTime{15};

    /// <summary>How long a start waits for another copy to finish a swap it's midway through.</summary>
    constexpr std::chrono::seconds MidwayWait{15};

    /// <summary>
    /// How long a start with a ready update waits for another copy to quit: RESTART starts the new copy as the old one
    /// closes, and the old one holds the lock until its process ends.
    /// </summary>
    constexpr std::chrono::seconds QuittingWait{5};

    /// <summary>Whether a file the swap sets aside is kept in <c>Redux backup/</c> once the swap is committed.</summary>
    enum class MCKeep : uint8_t
    {
        /// <summary>Only when it isn't the old file planned (the player's, changed since staging): an update placed it.</summary>
        IfChanged,
        /// <summary>Always: an update didn't place it as it is (the folder's own, a mod, a change, damage).</summary>
        Always,
        /// <summary>Never: its set was taken out (HD OPTIONS' REMOVE), which deletes its files whatever they hold.</summary>
        Never,
    };

    /// <summary>A file the swap moves: added (no old), replaced, or removed (no new).</summary>
    struct MCMove
    {
        /// <summary>Its path on disk, relative to the game folder (its staged copy is at the same path in staged/).</summary>
        std::string Path;
        std::optional<MCSha256::MCDigest> Old;
        uint64_t OldSize = 0;
        /// <summary>The old file's time written, when planned: unchanged, it's taken as old without hashing.</summary>
        int64_t OldWritten = 0;
        std::optional<MCSha256::MCDigest> New;
        uint64_t NewSize = 0;
        /// <summary>
        /// The staged file's time written, when armed (<see cref="MarkReady"/>): moved into place, unchanged, it's taken
        /// as new without hashing.
        /// </summary>
        int64_t NewWritten = 0;
        /// <summary>Whether the old file, set aside, is kept in <c>Redux backup/</c>.</summary>
        MCKeep Keep = MCKeep::IfChanged;
    };

    struct MCJournal
    {
        std::vector<MCMove> Moves;
        /// <summary>The installed record once swapped.</summary>
        MCInstalled::MCRecord Record;
        /// <summary>The lowest program build the content needs (its list's; 0 in a journal from before).</summary>
        uint64_t LowestProgram = 0;
    };

    enum class MCOutcome : uint8_t
    {
        /// <summary>No update waiting.</summary>
        Nothing,
        /// <summary>The update is in place.</summary>
        Swapped,
        /// <summary>It couldn't finish: the previous set is back, the update still staged (<see cref="MCResult::Error"/>).</summary>
        PutBack,
        /// <summary>
        /// It needs a newer program than the one running: the previous set is in place, the update disarmed, its files
        /// still staged (<see cref="MCResult::Error"/> says why).
        /// </summary>
        Disarmed,
        /// <summary>
        /// Neither, or a repair isn't done: the game can't play the files as they are (<see cref="MCResult::Error"/>).
        /// </summary>
        Stuck,
    };

    struct MCResult
    {
        MCOutcome Outcome = MCOutcome::Nothing;
        std::string Error;
    };

    std::string JournalText(const MCJournal& journal);
    std::expected<MCJournal, std::string> ReadJournal(std::string_view text);

    /// <summary>
    /// Staging is done (each staged file and its folder flushed): writes <c>ready.json</c> durably, the record given a
    /// swap id of its own and each new file its staged copy's time. False when it can't be written, or a staged file
    /// can't be looked at.
    /// </summary>
    bool MarkReady(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, const MCJournal& journal);

    /// <summary>Whether an update is staged and ready, or a swap was stopped midway.</summary>
    bool Waiting(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>Whether a repair is under way: the game can't play the files until a swap is committed.</summary>
    bool Repairing(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>
    /// Whether an update is under way: ready, stopped midway or being repaired (<see cref="Waiting"/>,
    /// <see cref="Repairing"/>). Its game files may be set aside, SYSTEM.CFG among them, until a start finishes it.
    /// </summary>
    bool UnderWay(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>
    /// REPAIR's first step, under the game's lock held exclusive: gives up a swap that can neither finish nor go back.
    /// Its journal becomes <see cref="RepairFile"/>; each file set aside is kept in <c>Redux backup/</c> or deleted as
    /// the commit would (one the journal doesn't name, kept), and <c>replaced/</c> goes; the record names each new file
    /// already in place as an update's. <c>staged/</c> stays, so the repair's staging takes what it holds as fetched.
    /// An update put back while repairing (<c>ready.json</c>) goes too. Stopped at any step, it's done again.
    /// </summary>
    /// <returns>Nothing, or what failed.</returns>
    std::expected<void, std::string> Abandon(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>
    /// Swaps a ready update in, or finishes one a crash stopped; puts the previous set back if it can't, or if moving
    /// the files is still going at <paramref name="deadline"/> (a committed swap only finishes its clearing). An update
    /// needing a newer program than <paramref name="runningBuild"/> (none: any program will do) is disarmed instead,
    /// one begun put back first. While a repair is under way there's no previous set to play: anything but a committed
    /// swap is Stuck.
    /// </summary>
    MCResult Run(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                 std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt,
                 std::optional<uint64_t> runningBuild = std::nullopt);

    /// <summary>
    /// The start's swap (<see cref="Run"/>) under the game's lock (update/MCUpdateLock.h), which then stays held shared
    /// for the game's run. The swap takes the lock exclusive: a ready update waits <see cref="QuittingWait"/> for
    /// another copy to quit, then for a later start. A swap stopped midway or a repair, whose files can't be played, is
    /// waited for (another copy finishing it) until <paramref name="deadline"/> or, with none, <see cref="MidwayWait"/>;
    /// so is another copy holding the lock exclusive when it's to be held shared. Past that the start is Stuck, never
    /// waiting longer, and holds nothing. Without a lock (none could be opened) the swap runs as it is. The program
    /// starting is build <paramref name="runningBuild"/>: content needing a newer one is disarmed.
    /// </summary>
    MCResult RunAtStart(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, MCUpdateLock* lock,
                        uint64_t runningBuild,
                        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
}
