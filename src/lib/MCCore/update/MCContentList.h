#pragma once

#include "lib/MCSha256.h"

/// <summary>
/// Port-only: a release's content list (SPEC-install-and-updates.md's The content list), named on the host by its
/// SHA-256, which the signed <c>release.json</c> gives (update/MCReleaseFile.h). It covers only the managed folder,
/// the game data (which holds HD as <c>HD/</c>), never the program: every file of the release, each with its path,
/// set, size, SHA-256 and the zip holding its current bytes; the hashes of its earlier official versions (so a copy
/// knows them from the player's own); and, for a file a release changed in part, its patches, each from one earlier
/// release's copy.
///
/// Paths are relative to the game folder, '/' apart, as the release has them. The rules the release script follows
/// are checked again here (<see cref="PathProblem"/>), so a list can't name anything outside the folder, anything
/// Windows can't hold, or two files whose names differ only in case.
/// </summary>
namespace MCContentList
{
    /// <summary>The format this game reads; a list of a higher one needs a newer program.</summary>
    constexpr uint64_t KnownFormat = 1;

    /// <summary>The sets, as the list names them: the original's data, and the HD pack.</summary>
    constexpr std::string_view GameSet = "game";
    constexpr std::string_view HdSet = "hd";

    /// <summary>The folders in the game folder updates keep for themselves, never content.</summary>
    constexpr std::string_view UpdateFolder = "Redux update";
    constexpr std::string_view BackupFolder = "Redux backup";

    /// <summary>A zip the content comes in: stored (not compressed), never changed once uploaded.</summary>
    struct MCZip
    {
        /// <summary>Its path on the host, under the release's folder.</summary>
        std::string Name;
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
    };

    /// <summary>A patch that turns one earlier release's copy of a file into this one (lib/MCFilePatch.h).</summary>
    struct MCPatch
    {
        /// <summary>The copy it applies to.</summary>
        MCSha256::MCDigest From{};
        /// <summary>Its path on the host, size and hash.</summary>
        std::string Name;
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
    };

    /// <summary>A file of the release.</summary>
    struct MCFile
    {
        /// <summary>Its path in the game folder ("data/missions/m01.fit", "HD/terrain.hda").</summary>
        std::string Path;
        /// <summary>Its set (<see cref="GameSet"/>, <see cref="HdSet"/>).</summary>
        std::string Set;
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
        /// <summary>The zip holding it (<see cref="MCList::Zips"/>' name).</summary>
        std::string Zip;
        /// <summary>The hashes of its earlier official versions: a copy holding one replaces it.</summary>
        std::vector<MCSha256::MCDigest> Earlier;
        std::vector<MCPatch> Patches;
    };

    /// <summary>The list.</summary>
    struct MCList
    {
        uint64_t Format = 0;
        /// <summary>The content release it describes.</summary>
        uint64_t Release = 0;
        /// <summary>The lowest program build the content needs.</summary>
        uint64_t LowestProgram = 0;
        std::vector<MCZip> Zips;
        std::vector<MCFile> Files;
    };

    /// <summary>The list <paramref name="payload"/> holds, every rule checked.</summary>
    std::expected<MCList, std::string> Read(std::string_view payload);

    /// <summary>The list as the release script writes it (the tests make lists with it).</summary>
    std::string Write(const MCList& list);

    /// <summary>
    /// What's wrong with <paramref name="path"/> as a content path, or empty: absolute, '\', a part empty, "." or
    /// "..", one Windows can't hold (a reserved name such as CON or LPT1, a character such as ':' or '?', a control
    /// character, a trailing dot or space), or the folders updates keep or anything in them (<see cref="UpdateFolder"/>,
    /// <see cref="BackupFolder"/>).
    /// </summary>
    std::string PathProblem(std::string_view path);

    /// <summary><paramref name="path"/> with its ASCII letters upper-cased: two paths alike in it are the same file.</summary>
    std::string FoldedPath(std::string_view path);
}
