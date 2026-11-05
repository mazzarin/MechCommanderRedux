#pragma once

#include "update/MCContentPlan.h"

/// <summary>
/// Port-only: whether the game folder can take a planned update, checked before anything downloads
/// (SPEC-install-and-updates.md's Platforms). On Windows, every path the update installs, stages, downloads and sets
/// aside must fit Windows' limit (the game isn't long-path aware), and the folder mustn't be one a sync client
/// (OneDrive) keeps in the cloud, found by where the folder resolves: it would hold the files while it syncs, and
/// upload gigabytes. Elsewhere there's nothing to check.
/// </summary>
namespace MCUpdatePlace
{
    /// <summary>
    /// Windows' limits without long paths, in UTF-16 units: a file's whole path (MAX_PATH, less its end), and a
    /// folder's (CreateDirectory leaves room for an 8.3 name).
    /// </summary>
    constexpr size_t WindowsFileLimit = 259;
    constexpr size_t WindowsFolderLimit = 247;

    /// <summary>
    /// Whether every path <paramref name="plan"/> uses in <paramref name="gameFolder"/> fits <paramref name="fileLimit"/>
    /// (and its folder <paramref name="folderLimit"/>).
    /// </summary>
    /// <returns>Nothing, or the longest that doesn't fit, said for the player.</returns>
    std::expected<void, std::string> FitPaths(const std::filesystem::path& gameFolder,
                                              const MCContentPlan::MCPlan& plan, size_t fileLimit, size_t folderLimit);

    /// <summary>Both checks on Windows (the limits above, and the folder's sync); nothing elsewhere.</summary>
    std::expected<void, std::string> Check(const std::filesystem::path& gameFolder, const MCContentPlan::MCPlan& plan);
}
