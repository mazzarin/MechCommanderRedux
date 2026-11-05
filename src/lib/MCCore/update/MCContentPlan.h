#pragma once

#include "update/MCContentList.h"
#include "update/MCInstalled.h"

class MCContentFolder;

/// <summary>
/// Port-only: what a copy must do to hold a release's content (SPEC-install-and-updates.md's The player's copy and
/// Applying an update's Plan). For each listed file of a set the copy has:
///
/// - one whose hash already matches stays, wherever it came from;
/// - one that's an earlier official version (a hash the list knows, or the one this copy installed) is replaced:
///   patched when the list has a patch from it, else fetched whole;
/// - one that's missing is fetched;
/// - any other is the player's (a mod), left as it is and recorded as theirs.
///
/// A file the copy installed that the release no longer lists, or whose set the copy no longer has, is removed when
/// it's still as installed. Only the zips holding a fetched file are downloaded. A copy several releases behind plans
/// the same way, against the current list alone.
/// </summary>
namespace MCContentPlan
{
    enum class MCAction : uint8_t
    {
        /// <summary>Already the release's.</summary>
        Keep,
        /// <summary>Unpacked from its zip.</summary>
        Fetch,
        /// <summary>The copy on disk patched.</summary>
        Patch,
        /// <summary>Installed by an earlier release, no longer listed (or its set dropped).</summary>
        Remove,
        /// <summary>The player's: left as it is.</summary>
        LeaveTheirs,
    };

    struct MCStep
    {
        MCAction Action = MCAction::Keep;
        /// <summary>The path as listed (or, removing, as installed).</summary>
        std::string Path;
        /// <summary>Where the file is on disk, or goes (Fetch of a missing file: placed in the folders on disk).</summary>
        std::string OnDisk;
        /// <summary>The listed file (none removing), and the patch applied.</summary>
        const MCContentList::MCFile* File = nullptr;
        const MCContentList::MCPatch* Patch = nullptr;
        /// <summary>The hash on disk (none: missing), and the file's size and time written as found.</summary>
        std::optional<MCSha256::MCDigest> Found;
        uint64_t FoundSize = 0;
        int64_t FoundWritten = 0;
    };

    struct MCPlan
    {
        std::vector<MCStep> Steps;
        /// <summary>The zips to download, and the bytes downloaded (zips and patches).</summary>
        std::vector<const MCContentList::MCZip*> Zips;
        uint64_t DownloadBytes = 0;
        /// <summary>The bytes staged: each fetched or patched file written anew before the swap.</summary>
        uint64_t StagedBytes = 0;
        /// <summary>
        /// The record once the plan is done (the files as they'll be; a fetched or patched file's time is set when it
        /// lands).
        /// </summary>
        MCInstalled::MCRecord Record;

        /// <summary>Whether anything changes.</summary>
        bool Changes() const;
    };

    /// <summary>
    /// The plan to bring <paramref name="folder"/> from <paramref name="installed"/> to <paramref name="list"/>'s
    /// files of <paramref name="sets"/>; <paramref name="listHash"/> is the list's own hash, for the record.
    /// </summary>
    /// <returns>The plan, or why there's none (a file can't be read, a link or folder in a file's place).</returns>
    std::expected<MCPlan, std::string> Make(MCContentFolder& folder, const MCInstalled::MCRecord& installed,
                                            const MCContentList::MCList& list, const MCSha256::MCDigest& listHash,
                                            const std::set<std::string, std::less<>>& sets);
}
