#pragma once

#include "update/MCContentList.h"
#include "update/MCContentPlan.h"
#include "update/MCContentStage.h"
#include "update/MCProgramSwap.h"
#include "update/MCReleaseFile.h"

class MCUpdateDisk;

/// <summary>
/// Port-only: the game's own updater, an update's content half (SPEC-install-and-updates.md's Releases and Applying an
/// update), worked off the main thread by main/MCUpdater. The check reads the key list and <c>release.json</c> from the
/// release's host and takes them only when signed as they must be and no older than those read before (the user
/// folder's <c>update.json</c>, which also keeps the copy's rollout number, the release the player skipped and
/// release.json's mirrors); then it picks this copy's release and plans its content. The lists and staging come from the
/// host and its mirrors (update/MCHttpSource.h). Staging fetches the content; arming it, once the program's half is in
/// too, has the next start swap it in.
/// </summary>
namespace MCContentUpdater
{
    /// <summary>The hosted files' names, as the release script lays them out.</summary>
    constexpr std::string_view KeyListName = "keys.json";
    constexpr std::string_view ReleasesName = "release.json";
    constexpr std::string_view ListsFolder = "lists";

    /// <summary>
    /// The content lists fetched, kept in the game folder by their names on the host (each checked against its name
    /// when read): the installed release's, so a change of sets plans offline, and the newest fetched, so a check while
    /// an update waits doesn't fetch it again.
    /// </summary>
    constexpr std::string_view KeptListsFolder = MCContentList::InUpdateFolder<"lists">;

    /// <summary>The most a signed file (the key list, <c>release.json</c>) and a content list may hold.</summary>
    constexpr size_t MostSignedBytes = 1 << 20;
    constexpr size_t MostListBytes = 64 << 20;

    /// <summary>
    /// The sets the player chose, in the game folder: the installer's answer (whether HD was ticked, and the full HD art
    /// or the 2x art), then HD OPTIONS'. JSON: <c>{"format": 1, "sets": ["game", "hd", "hd2x"], "picked": "machine"}</c>.
    /// The copy plans for these; without the file, for the sets its record has. <c>picked</c> says who chose the HD
    /// art's size (<see cref="MCPicked"/>); a file without it (every one before release 9) is the machine's.
    /// </summary>
    constexpr std::string_view ChosenFile = MCContentList::InUpdateFolder<"sets.json">;

    using MCSets = std::set<std::string, std::less<>>;

    /// <summary>
    /// The machine, for the HD art it takes by default (SPEC-memory.md's The 2x set): the 2x art with memory under 12
    /// GB, a card of its own under 4 GB, or a GPU that can't sample BC7 (<see cref="Takes2x"/>).
    /// </summary>
    struct MCMachine
    {
        /// <summary>Its memory (0: not known, which counts as enough).</summary>
        uint64_t MemoryBytes = 0;
        /// <summary>The memory of its card, one of its own (none: the GPU shares the machine's memory, or not known).</summary>
        std::optional<uint64_t> CardBytes;
        /// <summary>Whether its GPU samples BC7 (taken as so until the GPU says otherwise).</summary>
        bool SamplesBc7 = true;

        bool operator==(const MCMachine&) const = default;
    };

    /// <summary>
    /// The least memory, and the least memory of a card of its own, for the full HD art. 11.5 GiB: a 12 GB machine that
    /// reports a little less (what the firmware or a GPU keeps) counts as 12.
    /// </summary>
    constexpr uint64_t FullArtMemory = uint64_t{11776} << 20;
    /// <summary>3.5 GiB: a 4 GB card that reports a little less counts as 4.</summary>
    constexpr uint64_t FullArtCard = uint64_t{3584} << 20;

    /// <summary>Whether <paramref name="machine"/>'s default HD art is the 2x set.</summary>
    bool Takes2x(const MCMachine& machine);

    /// <summary>Who chose the HD art's size, full or 2x (<see cref="ChosenFile"/>'s <c>picked</c>).</summary>
    enum class MCPicked : uint8_t
    {
        /// <summary>
        /// The machine's default was taken: every download of HD takes the machine's (<see cref="MCSetup::Machine"/>):
        /// the first install, an update and HD OPTIONS' GET; otherwise the copy keeps the size it has.
        /// </summary>
        Machine,
        /// <summary>The player chose it: it stays as chosen.</summary>
        Player,
    };

    /// <summary>The sets chosen, and who chose the HD art's size.</summary>
    struct MCChoice
    {
        MCSets Sets;
        MCPicked Picked = MCPicked::Machine;

        bool operator==(const MCChoice&) const = default;
    };

    /// <summary>What the sets wanted are for (<see cref="Wanted"/>).</summary>
    enum class MCFor : uint8_t
    {
        /// <summary>The sets the copy keeps: a size the machine picked stays as installed.</summary>
        Keeping,
        /// <summary>A download of HD (an update, a first install): a size the machine picked is the machine's now.</summary>
        Downloading,
    };

    /// <summary>
    /// Asked as a check plans (hashing the copy's files, which on a first plan may read gigabytes): false to stop, which
    /// ends the check with MCSha256::StoppedError. None: never stops.
    /// </summary>
    using MCGoOn = std::function<bool()>;

    /// <summary>The copy and the build running.</summary>
    struct MCSetup
    {
        /// <summary>The game folder the content goes in, and the user folder (<c>update.json</c>).</summary>
        std::filesystem::path GameFolder;
        std::filesystem::path UserFolder;
        /// <summary>The root keys the build carries: the key list must be signed by one.</summary>
        std::vector<MCSignedFile::MCPublicKey> RootKeys;
        /// <summary>The lowest key list issue the build takes, even with no issue kept (a fresh install).</summary>
        uint64_t LowestKeyListIssue = 0;
        /// <summary>This platform's name in <c>release.json</c> ("mac", "windows").</summary>
        std::string Platform;
        /// <summary>The build running, and its version as shown ("0.2.1 (525)").</summary>
        uint64_t RunningBuild = 0;
        std::string RunningVersion;
        /// <summary>
        /// The machine, whose default HD art (<see cref="Takes2x"/>) is taken wherever the player hasn't picked the
        /// size (<see cref="MCPicked"/>).
        /// </summary>
        MCMachine Machine;
        /// <summary>
        /// Windows: whether the setup installed the copy, as the program's backend asks it (MCSetupBackend::MCPlatform's
        /// SetupOwns), so the download offered is the one it fetches. None: the setup's.
        /// </summary>
        MCProgramSwap::MCSetupOwns SetupOwns;
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
        /// <summary>
        /// The mirrors its content may come from besides the release's host (release.json's, kept in
        /// <c>update.json</c> for a change of sets), handed to the source that stages it (MCSource::UseMirrors).
        /// </summary>
        std::vector<std::string> Mirrors;
        /// <summary>
        /// The program running is below release.json's lowest release a copy may stay on: the program comes by itself
        /// and alone (no content, and armed without noting the content due), and the player isn't asked.
        /// </summary>
        bool Forced = false;
        /// <summary>
        /// Its content brings the art a program installed alone came for (MCInstalled::DueFor), its release or a later
        /// one: it downloads by itself, skipped or not, planned as any download (the machine's HD art where the player
        /// didn't pick its size), and the player isn't asked (SPEC-install-and-updates.md's Forced art).
        /// </summary>
        bool Due = false;

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
    /// it read in <c>update.json</c>. Its content is planned for the sets the copy is to have (<see cref="Wanted"/>),
    /// <paramref name="goOn"/> asked as it hashes.
    /// </summary>
    /// <returns>
    /// The update; none when nothing's newer, the player skipped it, or an update already waits for the next start; or
    /// why there's none (the files refused, the host out of reach, a game folder this account can't write). Below
    /// release.json's lowest release a copy may stay on, the program alone, forced (<see cref="MCOffer::Forced"/>),
    /// skipped or not; the art a program installed alone came for, forced too (<see cref="MCOffer::Due"/>), skipped
    /// or not.
    /// </returns>
    std::expected<std::optional<MCOffer>, std::string> Check(const MCSetup& setup, MCUpdateDisk& disk,
                                                             MCContentStage::MCSource& source, const MCGoOn& goOn = {});

    /// <summary>
    /// The change of sets the player chose (HD OPTIONS getting or removing HD), planned against the release the copy
    /// has installed, so nothing else changes. Sets taken out alone are planned from the record (MCContentPlan::Removal),
    /// offline; a set added, from the release's list, read by the hash the record keeps (kept, else fetched).
    /// <paramref name="goOn"/> is asked as it hashes.
    /// </summary>
    /// <returns>
    /// The change (no program); none when the copy has the sets chosen, or an update already waits for the next start;
    /// or why there's none (no release installed yet; adding, the list neither kept nor hosted, the host out of reach).
    /// </returns>
    std::expected<std::optional<MCOffer>, std::string> CheckSets(const MCSetup& setup, MCUpdateDisk& disk,
                                                                 MCContentStage::MCSource& source,
                                                                 const MCGoOn& goOn = {});

    /// <summary>
    /// The first start's install (<see cref="AwaitsInstall"/>): the release the copy takes, planned for the sets chosen,
    /// for the program running (the content must not need a newer one); <paramref name="goOn"/> asked as it hashes.
    /// </summary>
    /// <returns>The install, its content always planned; or why there's none.</returns>
    std::expected<MCOffer, std::string> CheckInstall(const MCSetup& setup, MCUpdateDisk& disk,
                                                     MCContentStage::MCSource& source, const MCGoOn& goOn = {});

    /// <summary>
    /// Stages <paramref name="offer"/>'s content from <paramref name="source"/> (MCContentStage::Stage), not yet armed.
    /// </summary>
    std::expected<void, MCUpdateWork::MCFailure> Stage(const MCSetup& setup, MCUpdateDisk& disk,
                                                       MCContentStage::MCSource& source, const MCOffer& offer,
                                                       const MCUpdateWork::MCProgressCallback& progress);

    /// <summary>
    /// The first start's install whole (<see cref="AwaitsInstall"/>), under the staging lock: checked
    /// (<see cref="CheckInstall"/>), staged and armed, so the swap that follows puts the content in place.
    /// <paramref name="progress"/> gets the bytes downloaded so far and expected, and returns false to stop; it's asked
    /// too while the files are hashed and before arming, told the bytes as they were.
    /// </summary>
    /// <returns>
    /// Nothing, or what failed, of its kind: the host out of reach (Unreachable), a full disk (DiskFull), content that
    /// needs a newer program (NewerProgram), another copy downloading it, stopped (Other). Stopped, nothing is armed.
    /// </returns>
    std::expected<void, MCUpdateWork::MCFailure> Install(const MCSetup& setup, MCUpdateDisk& disk,
                                                         MCContentStage::MCSource& source,
                                                         const MCUpdateWork::MCProgressCallback& progress);

    /// <summary>
    /// The recovery screen's REPAIR, under the game's lock held exclusive: the release is installed as at the first
    /// start (<see cref="Install"/>), planned from the files as they are, so the swap that follows makes every listed
    /// file the release's. The update stopped partway is given up (MCContentSwap::Abandon) only once the release and its
    /// list are fetched and checked: offline, it stays as it was.
    /// </summary>
    /// <returns>Nothing, or what failed, of its kind (as <see cref="Install"/>'s).</returns>
    std::expected<void, MCUpdateWork::MCFailure> Repair(const MCSetup& setup, MCUpdateDisk& disk,
                                                        MCContentStage::MCSource& source,
                                                        const MCUpdateWork::MCProgressCallback& progress);

    /// <summary>Arms <paramref name="offer"/>'s staged content: the next start swaps it in.</summary>
    std::expected<void, std::string> Arm(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer);

    /// <summary>The player skipped <paramref name="offer"/>: its release isn't offered again (a higher one is).</summary>
    std::expected<void, std::string> Skip(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer);

    /// <summary>The sets a copy with no record has: the game's data, and HD when its folder is in the game folder.</summary>
    MCSets SetsFound(const std::filesystem::path& gameFolder);

    /// <summary>
    /// The sets the player chose (<see cref="ChosenFile"/>), the game's data always among them, and who chose the HD
    /// art's size; none without the file, or when it can't be read (logged).
    /// </summary>
    std::optional<MCChoice> Choice(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>The sets the player chose (<see cref="Choice"/>'s).</summary>
    std::optional<MCSets> Chosen(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>
    /// Keeps <paramref name="sets"/> (the game's data added) as the sets the player chose, the HD art's size chosen by
    /// <paramref name="picked"/>.
    /// </summary>
    std::expected<void, std::string> Choose(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                            const MCSets& sets, MCPicked picked = MCPicked::Machine);

    /// <summary>
    /// The sets the copy <paramref name="setup"/> describes is to have, for <paramref name="purpose"/>: those chosen,
    /// else those <paramref name="installed"/> records, else those found; the game's data always, the 2x set only with
    /// HD (and HD with it). Where the machine picked the HD art's size, a download takes the machine's
    /// (<see cref="MCSetup::Machine"/>), and a copy keeping its sets the size it has (the machine's when it has no HD).
    /// </summary>
    MCSets Wanted(const MCSetup& setup, MCUpdateDisk& disk, const MCInstalled::MCRecord& installed,
                  MCFor purpose = MCFor::Keeping);

    /// <summary>
    /// Whether the copy awaits its first install: an installer left its answer (<see cref="ChosenFile"/>), and no
    /// content is recorded installed yet. The game then gets the content before anything opens.
    /// </summary>
    bool AwaitsInstall(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>Whether this account can write the game folder and its update folder (another account's can't).</summary>
    std::expected<void, std::string> Writable(const std::filesystem::path& gameFolder);
}
