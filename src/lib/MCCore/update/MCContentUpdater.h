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

    /// <summary>
    /// The sets the player chose, in the game folder: the installer's answer (whether HD was ticked), then HD OPTIONS'.
    /// JSON: <c>{"format": 1, "sets": ["game", "hd"]}</c>. The copy plans for these; without the file, for the sets its
    /// record has.
    /// </summary>
    constexpr std::string_view ChosenFile = "Redux update/sets.json";

    using MCSets = std::set<std::string, std::less<>>;

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
    /// it read in <c>update.json</c>. Its content is planned for the sets the copy is to have (<see cref="Wanted"/>).
    /// </summary>
    /// <returns>
    /// The update; none when nothing's newer, the player skipped it, or an update already waits for the next start; or
    /// why there's none (the files refused, the host out of reach, a game folder this account can't write).
    /// </returns>
    std::expected<std::optional<MCOffer>, std::string> Check(const MCSetup& setup, MCUpdateDisk& disk,
                                                             MCContentStage::MCSource& source);

    /// <summary>
    /// The change of sets the player chose (HD OPTIONS getting or removing HD), planned against the release the copy
    /// has installed: its list fetched by the hash its record keeps, so nothing else changes.
    /// </summary>
    /// <returns>
    /// The change (no program); none when the copy has the sets chosen, or an update already waits for the next start;
    /// or why there's none (no release installed yet, the list no longer hosted, the host out of reach).
    /// </returns>
    std::expected<std::optional<MCOffer>, std::string> CheckSets(const MCSetup& setup, MCUpdateDisk& disk,
                                                                 MCContentStage::MCSource& source);

    /// <summary>
    /// The first start's install (<see cref="AwaitsInstall"/>): the release the copy takes, planned for the sets chosen,
    /// for the program running (the content must not need a newer one).
    /// </summary>
    /// <returns>The install, its content always planned; or why there's none.</returns>
    std::expected<MCOffer, std::string> CheckInstall(const MCSetup& setup, MCUpdateDisk& disk,
                                                     MCContentStage::MCSource& source);

    /// <summary>
    /// Stages <paramref name="offer"/>'s content from <paramref name="source"/> (MCContentStage::Stage), not yet armed.
    /// </summary>
    std::expected<void, std::string> Stage(const MCSetup& setup, MCUpdateDisk& disk, MCContentStage::MCSource& source,
                                           const MCOffer& offer,
                                           const std::function<bool(uint64_t, uint64_t)>& progress);

    /// <summary>
    /// The first start's install whole (<see cref="AwaitsInstall"/>), under the staging lock: checked
    /// (<see cref="CheckInstall"/>), staged and armed, so the swap that follows puts the content in place.
    /// <paramref name="progress"/> gets the bytes downloaded so far and expected, and returns false to stop.
    /// </summary>
    /// <returns>Nothing, or what failed (another copy downloading it, the host out of reach, a full disk).</returns>
    std::expected<void, std::string> Install(const MCSetup& setup, MCUpdateDisk& disk, MCContentStage::MCSource& source,
                                             const std::function<bool(uint64_t, uint64_t)>& progress);

    /// <summary>
    /// The recovery screen's REPAIR, under the game's lock held exclusive: the update stopped partway is given up
    /// (MCContentSwap::Abandon), then the release is installed as at the first start (<see cref="Install"/>), planned
    /// from the files as they are, so the swap that follows makes every listed file the release's.
    /// </summary>
    /// <returns>Nothing, or what failed (the host out of reach, a full disk).</returns>
    std::expected<void, std::string> Repair(const MCSetup& setup, MCUpdateDisk& disk, MCContentStage::MCSource& source,
                                            const std::function<bool(uint64_t, uint64_t)>& progress);

    /// <summary>Arms <paramref name="offer"/>'s staged content: the next start swaps it in.</summary>
    std::expected<void, std::string> Arm(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer);

    /// <summary>The player skipped <paramref name="offer"/>: its release isn't offered again (a higher one is).</summary>
    std::expected<void, std::string> Skip(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer);

    /// <summary>The sets a copy with no record has: the game's data, and HD when its folder is in the game folder.</summary>
    MCSets SetsFound(const std::filesystem::path& gameFolder);

    /// <summary>
    /// The sets the player chose (<see cref="ChosenFile"/>), the game's data always among them; none without the file,
    /// or when it can't be read (logged).
    /// </summary>
    std::optional<MCSets> Chosen(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>Keeps <paramref name="sets"/> (the game's data added) as the sets the player chose.</summary>
    std::expected<void, std::string> Choose(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                            const MCSets& sets);

    /// <summary>
    /// The sets the copy is to have: those chosen, else those <paramref name="installed"/> records, else those found;
    /// the game's data always.
    /// </summary>
    MCSets Wanted(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, const MCInstalled::MCRecord& installed);

    /// <summary>
    /// Whether the copy awaits its first install: an installer left its answer (<see cref="ChosenFile"/>), and no
    /// content is recorded installed yet. The game then gets the content before anything opens.
    /// </summary>
    bool AwaitsInstall(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>Whether this account can write the game folder and its update folder (another account's can't).</summary>
    std::expected<void, std::string> Writable(const std::filesystem::path& gameFolder);
}
