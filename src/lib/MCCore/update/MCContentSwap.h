#pragma once

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
/// 4. The installed record is written durably; then the journal goes, then <c>replaced/</c> and <c>staged/</c>.
///
/// A crash at any step leaves the journal, and the next start finishes the swap, judging each file by what's at its
/// path (its size, else its hash): old, new or missing. Nothing new moves in before every old file is safely aside,
/// so no old file is ever lost. If the swap can't finish (a file held open, a full disk), the previous set is put back
/// from <c>replaced/</c> and the update stays staged for the next start; only a set that can't be put back stops the
/// game. A file the player changed since staging is left as it is and recorded as theirs.
/// </summary>
namespace MCContentSwap
{
    /// <summary>The update's folders and files, in the game folder.</summary>
    constexpr std::string_view UpdateFolder = "Redux update";
    constexpr std::string_view StagedFolder = "Redux update/staged";
    constexpr std::string_view ReplacedFolder = "Redux update/replaced";
    constexpr std::string_view ReadyFile = "Redux update/ready.json";
    constexpr std::string_view JournalFile = "Redux update/journal.json";
    /// <summary>
    /// How long a multiplayer lobby's start lets the swap take before putting the previous set back: a room is never
    /// held up (a normal swap takes well under a second).
    /// </summary>
    constexpr std::chrono::seconds LobbyStartTime{15};

    /// <summary>How long a start waits for another copy to finish a swap it's midway through.</summary>
    constexpr std::chrono::seconds MidwayWait{15};

    /// <summary>
    /// How long a start with a ready update waits for another copy to quit: RESTART starts the new copy as the old one
    /// closes, and the old one holds the lock until its process ends.
    /// </summary>
    constexpr std::chrono::seconds QuittingWait{5};

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
    };

    struct MCJournal
    {
        std::vector<MCMove> Moves;
        /// <summary>The installed record once swapped.</summary>
        MCInstalled::MCRecord Record;
    };

    enum class MCOutcome : uint8_t
    {
        /// <summary>No update waiting.</summary>
        Nothing,
        /// <summary>The update is in place.</summary>
        Swapped,
        /// <summary>It couldn't finish: the previous set is back, the update still staged (<see cref="MCResult::Error"/>).</summary>
        PutBack,
        /// <summary>Neither: the game can't play this set (<see cref="MCResult::Error"/>).</summary>
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
    /// swap id of its own.
    /// </summary>
    bool MarkReady(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, const MCJournal& journal);

    /// <summary>Whether an update is staged and ready, or a swap was stopped midway.</summary>
    bool Waiting(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>
    /// Swaps a ready update in, or finishes one a crash stopped; puts the previous set back if it can't, or if moving
    /// the files is still going at <paramref name="deadline"/> (a committed swap only finishes its clearing).
    /// </summary>
    MCResult Run(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                 std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);

    /// <summary>
    /// The start's swap (<see cref="Run"/>) under the game's lock (update/MCUpdateLock.h), which then stays held shared
    /// for the game's run. The swap takes the lock exclusive: a ready update waits <see cref="QuittingWait"/> for
    /// another copy to quit, then for a later start. A swap stopped midway, whose files can't be played, is waited for
    /// (another copy finishing it) until <paramref name="deadline"/> or, with none, <see cref="MidwayWait"/>. Neither
    /// wait passes <paramref name="deadline"/>. Without a lock (none could be opened) the swap runs as it is.
    /// </summary>
    MCResult RunAtStart(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, MCUpdateLock* lock,
                        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
}
