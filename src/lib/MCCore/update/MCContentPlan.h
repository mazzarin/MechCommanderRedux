#pragma once

#include "update/MCContentList.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"

class MCContentFolder;

/// <summary>
/// Port-only: what a copy must do to hold a release's content (SPEC-install-and-updates.md's The player's copy and
/// Applying an update's Plan): every listed path is the release's. For each listed file of a set the copy has:
///
/// - one whose hash already matches stays, wherever it came from;
/// - any other is replaced: patched when the list has a patch from its bytes, else fetched whole;
/// - one that's missing is fetched.
///
/// A recorded file the release no longer lists, or whose set the copy no longer has, is removed. A file replaced or
/// removed that isn't one an update placed, as placed (the folder's own, or one changed since), is kept in
/// <c>Redux backup</c> (update/MCContentSwap.h), but a set taken out (HD OPTIONS' REMOVE) is deleted whatever its
/// files hold; unlisted files are never touched. Only the zips holding a fetched file
/// are downloaded. A copy several releases behind plans the same way, against the current list alone.
///
/// A file found is hashed only when neither the record nor the hashes kept from the last plan (<see cref="MCHashes"/>)
/// have it at its size and time: a copy with no record yet (one the program reached first) hashes its files once.
/// </summary>
namespace MCContentPlan
{
    /// <summary>The hashes kept from the last plan, in the game folder.</summary>
    constexpr std::string_view HashesFile = "Redux update/hashes.json";

    /// <summary>A file found and hashed, as found.</summary>
    struct MCHash
    {
        uint64_t Size = 0;
        int64_t Written = 0;
        MCSha256::MCDigest Sha256{};

        bool operator==(const MCHash&) const = default;
    };

    /// <summary>
    /// The files the last plan found that the record doesn't vouch for (none recorded, or changed since), by their paths
    /// as spelled on disk: their hashes are taken while their size and time stay as found.
    /// </summary>
    using MCHashes = std::map<std::string, MCHash, std::less<>>;

    enum class MCAction : uint8_t
    {
        /// <summary>Already the release's.</summary>
        Keep,
        /// <summary>Unpacked from its zip.</summary>
        Fetch,
        /// <summary>The copy on disk patched.</summary>
        Patch,
        /// <summary>Recorded, no longer listed (or its set dropped).</summary>
        Remove,
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
        /// <summary>Replaced or removed, whether the file found is kept in <c>Redux backup</c>.</summary>
        MCContentSwap::MCKeep Keep = MCContentSwap::MCKeep::IfChanged;
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
        /// lands), with every listed set's bytes.
        /// </summary>
        MCInstalled::MCRecord Record;
        /// <summary>
        /// The hashes to keep for the next plan: those of the files found that the record doesn't vouch for. A file no
        /// longer found has none.
        /// </summary>
        MCHashes Hashes;
        /// <summary>How many files were hashed for this plan (read whole).</summary>
        size_t HashedFiles = 0;

        /// <summary>Whether anything changes.</summary>
        bool Changes() const;
    };

    /// <summary>
    /// The plan to bring <paramref name="folder"/> from <paramref name="installed"/> to <paramref name="list"/>'s
    /// files of <paramref name="sets"/>; <paramref name="listHash"/> is the list's own hash, for the record;
    /// <paramref name="hashes"/> those the last plan kept.
    /// </summary>
    /// <returns>
    /// The plan, or why there's none (a file can't be read, a folder can't be listed, a link or folder in a file's
    /// place).
    /// </returns>
    std::expected<MCPlan, std::string> Make(MCContentFolder& folder, const MCInstalled::MCRecord& installed,
                                            const MCContentList::MCList& list, const MCSha256::MCDigest& listHash,
                                            const std::set<std::string, std::less<>>& sets,
                                            const MCHashes& hashes = {});

    /// <summary>The hashes kept as text (JSON).</summary>
    std::string HashesText(const MCHashes& hashes);

    /// <summary>The hashes <paramref name="text"/> keeps, every field checked.</summary>
    std::expected<MCHashes, std::string> ReadHashes(std::string_view text);
}
