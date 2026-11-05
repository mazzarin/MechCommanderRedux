#pragma once

#include "update/MCContentList.h"
#include "update/MCContentPlan.h"
#include "update/MCContentStage.h"
#include "update/MCReleaseFile.h"

class MCUpdateDisk;

/// <summary>
/// Port-only: the game's own updater, an update's content half (SPEC-install-and-updates.md's Releases and Applying an
/// update), worked off the main thread by main/MCUpdater. The check reads the key list and <c>release.json</c> from the
/// release's host and takes them only when signed as they must be and no older than those read before (the user
/// folder's <c>update.json</c>, which also keeps the copy's rollout number and the release the player skipped); then it
/// picks this copy's release and plans its content. Staging fetches the content; arming it, once the program's half is
/// in too, has the next start swap it in.
/// </summary>
namespace MCContentUpdater
{
    /// <summary>The hosted files' names, as the release script lays them out.</summary>
    constexpr std::string_view KeyListName = "keys.json";
    constexpr std::string_view ReleasesName = "release.json";
    constexpr std::string_view ListsFolder = "lists";

    /// <summary>The most a signed file (the key list, <c>release.json</c>) and a content list may hold.</summary>
    constexpr size_t MostSignedBytes = 1 << 20;
    constexpr size_t MostListBytes = 64 << 20;

    /// <summary>The copy and the build running.</summary>
    struct MCSetup
    {
        /// <summary>The game folder the content goes in, and the user folder (<c>update.json</c>).</summary>
        std::filesystem::path GameFolder;
        std::filesystem::path UserFolder;
        /// <summary>The root keys the build carries: the key list must be signed by one.</summary>
        std::vector<MCSignedFile::MCPublicKey> RootKeys;
        /// <summary>This platform's name in <c>release.json</c> ("mac", "windows").</summary>
        std::string Platform;
        /// <summary>The build running, and its version as shown ("0.2.1 (525)").</summary>
        uint64_t RunningBuild = 0;
        std::string RunningVersion;
    };

    /// <summary>A release's content planned for the copy: the list, and the plan, which points into it.</summary>
    struct MCContent
    {
        MCContentList::MCList List;
        MCContentPlan::MCPlan Plan;
    };

    /// <summary>An update for the copy.</summary>
    struct MCOffer
    {
        MCReleaseFile::MCRelease Release;
        /// <summary>The program's build to install, when the release's is newer than the one running, and its size.</summary>
        std::optional<uint64_t> ProgramBuild;
        uint64_t ProgramBytes = 0;
        /// <summary>The content (none: a list in a format this game can't read, so the program comes alone).</summary>
        std::shared_ptr<const MCContent> Content;

        /// <summary>What the content downloads.</summary>
        uint64_t ContentBytes() const;

        /// <summary>
        /// Whether it can go in unasked: no program, and the content changes no file (the record catches up with the
        /// release, as for a copy whose files are already the release's).
        /// </summary>
        bool Quiet() const;

        /// <summary>The version as shown: the release's, and the program's build ("0.2.2 (480)").</summary>
        std::string ShownVersion(uint64_t runningBuild) const;
    };

    /// <summary>
    /// Looks for an update for the copy <paramref name="setup"/> describes on <paramref name="source"/>, keeping what
    /// it read in <c>update.json</c>.
    /// </summary>
    /// <returns>
    /// The update; none when nothing's newer, the player skipped it, or an update already waits for the next start; or
    /// why there's none (the files refused, the host out of reach, a game folder this account can't write).
    /// </returns>
    std::expected<std::optional<MCOffer>, std::string> Check(const MCSetup& setup, MCUpdateDisk& disk,
                                                             MCContentStage::MCSource& source);

    /// <summary>
    /// Stages <paramref name="offer"/>'s content from <paramref name="source"/> (MCContentStage::Stage), not yet armed.
    /// </summary>
    std::expected<void, std::string> Stage(const MCSetup& setup, MCUpdateDisk& disk, MCContentStage::MCSource& source,
                                           const MCOffer& offer,
                                           const std::function<bool(uint64_t, uint64_t)>& progress);

    /// <summary>Arms <paramref name="offer"/>'s staged content: the next start swaps it in.</summary>
    std::expected<void, std::string> Arm(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer);

    /// <summary>The player skipped <paramref name="offer"/>: its release isn't offered again (a higher one is).</summary>
    std::expected<void, std::string> Skip(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer);

    /// <summary>The sets a copy with no record has: the game's data, and HD when its folder is in the game folder.</summary>
    std::set<std::string, std::less<>> SetsFound(const std::filesystem::path& gameFolder);

    /// <summary>Whether this account can write the game folder and its update folder (another account's can't).</summary>
    std::expected<void, std::string> Writable(const std::filesystem::path& gameFolder);
}
