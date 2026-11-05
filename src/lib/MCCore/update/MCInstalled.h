#pragma once

#include "lib/MCSha256.h"

/// <summary>
/// Port-only: what a copy has installed (<c>Redux update/installed.json</c> in the game folder): the content release
/// and its list, the sets the copy has (the installer's HD answer, then HD OPTIONS'), and each managed file as it was
/// last seen, with its hash, size and time written. A file whose size and time are still as recorded keeps its
/// recorded hash, so a plan hashes only what changed since. Files updates installed are the copy's ("ours"); a listed
/// file the player changed is recorded as theirs, and updates leave it alone. A file already in the folder as the
/// release has it is ours too, and recorded as found there, so removing the content leaves it.
/// </summary>
namespace MCInstalled
{
    /// <summary>Where the record is, in the game folder.</summary>
    constexpr std::string_view RecordPath = "Redux update/installed.json";

    struct MCFile
    {
        /// <summary>Its path as spelled on disk.</summary>
        std::string Path;
        /// <summary>The set it belongs to.</summary>
        std::string Set;
        MCSha256::MCDigest Sha256{};
        uint64_t Size = 0;
        /// <summary>The time it was written, in the file system's own ticks (only ever compared on this machine).</summary>
        int64_t Written = 0;
        /// <summary>Whether it's the release's (installed or found as the release has it), not the player's.</summary>
        bool Ours = true;
        /// <summary>
        /// Whether it was in the folder, as the release has it, before any update put it there (a copy the player's
        /// installer took over): removing the content (MCContentRemoval) leaves it, so that copy stays as it was.
        /// </summary>
        bool Found = false;
    };

    struct MCRecord
    {
        /// <summary>The content release installed (0: none yet) and its list's hash.</summary>
        uint64_t Release = 0;
        MCSha256::MCDigest List{};
        /// <summary>The sets the copy has.</summary>
        std::set<std::string, std::less<>> Sets;
        /// <summary>
        /// The bytes of each set the release lists, had or not (what HD OPTIONS says getting HD takes; none recorded by
        /// an earlier version).
        /// </summary>
        std::map<std::string, uint64_t, std::less<>> SetBytes;
        /// <summary>
        /// The swap that wrote it (a random id each update's journal carries; empty none): the swap is committed once
        /// the record on disk is its own, whatever the release (a repair keeps the same one).
        /// </summary>
        std::string Swap;
        /// <summary>The files, by their listed paths folded (MCContentList::FoldedPath).</summary>
        std::map<std::string, MCFile, std::less<>> Files;
    };

    /// <summary>The record in <paramref name="gameFolder"/>; an empty one when there's none.</summary>
    /// <returns>The record, or why the one there can't be read.</returns>
    std::expected<MCRecord, std::string> Read(const std::filesystem::path& gameFolder);

    /// <summary>The record as written.</summary>
    std::string Text(const MCRecord& record);

    /// <summary>The record in <paramref name="text"/>.</summary>
    std::expected<MCRecord, std::string> FromText(std::string_view text);

    /// <summary>The time <paramref name="path"/> was written, as recorded, or none when it can't be read.</summary>
    std::optional<int64_t> WrittenAt(const std::filesystem::path& path);
}
