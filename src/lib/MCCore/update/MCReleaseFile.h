#pragma once

// Standard headers and lib/MCSha256.h only: the Mac's Sparkle backend (apps/MCRedux/MCSparkleUpdater.mm) includes this
// without the precompiled header.
#include "lib/MCSha256.h"

#include <compare>
#include <map>
#include <vector>

#include "update/MCSignedFile.h"

/// <summary>
/// Port-only: what the game acts on to update, and why it trusts it (SPEC-install-and-updates.md's Releases, Staged
/// rollouts and Signing keys). Two signed files, fetched together:
///
/// - The key list, signed by the root key, whose public half every build carries: the release keys in force and the
///   lowest release number still taken. Replacing a release key is a new key list.
/// - <c>release.json</c>, signed by a release key the list names: the release every copy takes and, maybe, a newer one
///   rolling out to a share of copies. Each release has its number, its content list (by SHA-256) and each platform's
///   program.
///
/// Each file carries an issue number that only rises: the game keeps the highest it has read (in the user folder) and
/// takes no file older, so an old file served again can't hold copies back; a build also carries the lowest key list
/// issue it takes. The key list's epoch, raised by the root only to recover from a release key that signed numbers too
/// high to outbid, starts <c>release.json</c>'s issues and the releases' order afresh. Formats only grow: a newer
/// format adds fields, which this one ignores.
/// </summary>
namespace MCReleaseFile
{
    /// <summary>The format this game reads; a file of a higher one adds what it doesn't know.</summary>
    constexpr uint64_t KnownFormat = 1;

    /// <summary>The platforms' names in <c>release.json</c>.</summary>
    constexpr std::string_view MacPlatform = "mac";
    constexpr std::string_view WindowsPlatform = "windows";

    /// <summary>The key list.</summary>
    struct MCKeyList
    {
        uint64_t Format = 0;
        uint64_t Issue = 0;
        /// <summary>The release keys in force: <c>release.json</c> must be signed by one.</summary>
        std::vector<MCSignedFile::MCPublicKey> ReleaseKeys;
        /// <summary>The lowest release number taken, whatever <c>release.json</c> says.</summary>
        uint64_t LowestRelease = 0;
        /// <summary>
        /// Its epoch (0 without one), which only rises: a list of a later epoch than the copy read before starts
        /// <c>release.json</c>'s issues again, and its releases come after any of an earlier epoch, whatever their
        /// numbers (<see cref="MCOrder"/>).
        /// </summary>
        uint64_t Epoch = 0;
    };

    /// <summary>The bytes of an Ed25519 signature.</summary>
    constexpr size_t EdSignatureBytes = 64;
    using MCEdSignature = std::array<uint8_t, EdSignatureBytes>;

    /// <summary>One of the Windows program's files, by its name at the top of the game folder.</summary>
    struct MCProgramFile
    {
        std::string Name;
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
    };

    /// <summary>A file hosted for the program: its address, size and SHA-256.</summary>
    struct MCHostedFile
    {
        std::string Url;
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
    };

    /// <summary>The most files a program may name (Windows' has five).</summary>
    constexpr size_t MaxProgramFiles = 32;

    /// <summary>A platform's program in a release.</summary>
    struct MCProgram
    {
        /// <summary>Its build number (the commit count): the one Sparkle may install on the Mac.</summary>
        uint64_t Build = 0;
        /// <summary>What it downloads, in bytes.</summary>
        uint64_t Size = 0;
        /// <summary>Its installer's hash and address, and how to run it silently (none on the Mac: Sparkle's).</summary>
        std::optional<MCSha256::MCDigest> Sha256;
        std::string Url;
        std::string Silent;
        /// <summary>
        /// The Mac's: its archive's EdDSA signatures as Sparkle's feed gives them (<c>sparkle:edSignature</c>), one for
        /// each release key the feed signs it with (two while a key is replaced). Ed25519 signs deterministically, so
        /// each pins the archive's bytes: Sparkle installs only that archive (<see cref="FeedItemProblem"/>). Without
        /// one the Mac takes no program from the release.
        /// </summary>
        std::vector<MCEdSignature> EdSignatures;
        /// <summary>
        /// Windows': the program's files (<see cref="MaxProgramFiles"/> at most, each a plain name), one stored zip
        /// holding them, and the setup's level, which rises whenever the setup's own work changes. A copy whose setup
        /// was at that level puts the files in itself (update/MCProgramSwap.h); any other runs the setup. None given (a
        /// release from before), the setup always runs.
        /// </summary>
        std::vector<MCProgramFile> Files;
        std::optional<MCHostedFile> Zip;
        uint64_t SetupLevel = 0;
    };

    /// <summary>A release.</summary>
    struct MCRelease
    {
        /// <summary>Its number: only rises, and the game takes none lower than it has.</summary>
        uint64_t Number = 0;
        /// <summary>The version shown ("0.2.2").</summary>
        std::string Version;
        /// <summary>What it brings, as plain text.</summary>
        std::string Notes;
        /// <summary>Its content list's SHA-256 (its name on the host) and size.</summary>
        MCSha256::MCDigest List{};
        uint64_t ListSize = 0;
        /// <summary>Each platform's program ("mac", "windows").</summary>
        std::map<std::string, MCProgram, std::less<>> Programs;
        /// <summary>The epoch of the key list it was taken under (<see cref="Check"/> sets it: release.json has none).</summary>
        uint64_t Epoch = 0;
    };

    /// <summary>Where a release comes in the order releases are taken: by its key list's epoch, then its number.</summary>
    struct MCOrder
    {
        uint64_t Epoch = 0;
        uint64_t Release = 0;

        auto operator<=>(const MCOrder&) const = default;
    };

    /// <summary>The most mirrors <c>release.json</c> may name.</summary>
    constexpr size_t MaxMirrors = 16;

    /// <summary><c>release.json</c>.</summary>
    struct MCReleases
    {
        uint64_t Format = 0;
        uint64_t Issue = 0;
        /// <summary>The release every copy takes.</summary>
        MCRelease Current;
        /// <summary>A newer release going to a share of copies, and the share (0 to 100).</summary>
        std::optional<MCRelease> Rollout;
        double RolloutShare = 0.0;
        /// <summary>
        /// The mirrors: other hosts serving the content (<c>lists/</c>, <c>zips/</c>, <c>patches/</c>) at the paths
        /// ours does, each an https address (none: ours alone; SPEC-install-and-updates.md's Mirrors). The key list and
        /// <c>release.json</c> come from ours alone, so a mirror can't hold a copy on an old release, and every file is
        /// checked against the signed list, so a mirror can only fail to serve one. The file may also name the earlier
        /// releases still supported (<c>supported</c>), which the mirrors' sync reads and the game doesn't.
        /// </summary>
        std::vector<std::string> Mirrors;
    };

    /// <summary>What the game keeps of the files it has read, and of the copy (the user folder's <c>update.json</c>).</summary>
    struct MCKept
    {
        /// <summary>The highest issues read; none is taken lower.</summary>
        uint64_t KeyListIssue = 0;
        uint64_t ReleaseIssue = 0;
        /// <summary>The epoch of the key list last taken: <see cref="ReleaseIssue"/> and <see cref="Skipped"/> are its.</summary>
        uint64_t Epoch = 0;
        /// <summary>The copy's rollout number (<see cref="For"/>), made once at random and never sent; none yet.</summary>
        std::optional<uint32_t> RolloutNumber;
        /// <summary>The release the player skipped (0: none): it isn't offered again, and a higher one is.</summary>
        uint64_t Skipped = 0;
        /// <summary>
        /// The mirrors of the <c>release.json</c> last taken (<see cref="MCReleases::Mirrors"/>), for a change of sets
        /// planned against the release installed, which reads no <c>release.json</c>.
        /// </summary>
        std::vector<std::string> Mirrors;

        bool operator==(const MCKept&) const = default;
    };

    /// <summary>Where the game keeps it, in the user folder.</summary>
    constexpr std::string_view KeptFile = "update.json";

    /// <summary>Both files, checked.</summary>
    struct MCTrusted
    {
        MCKeyList Keys;
        MCReleases Releases;
    };

    /// <summary>The key list's payload.</summary>
    std::expected<MCKeyList, std::string> ReadKeyList(std::string_view payload);

    /// <summary><c>release.json</c>'s payload.</summary>
    std::expected<MCReleases, std::string> ReadReleases(std::string_view payload);

    /// <summary>
    /// Checks the key list (<paramref name="keyList"/>, an envelope) against <paramref name="rootKeys"/>, then
    /// <c>release.json</c> (<paramref name="releases"/>) against the release keys it names. Refuses a key list older
    /// than <paramref name="kept"/>'s or than <paramref name="lowestKeyListIssue"/> (the build's own floor), or of an
    /// earlier epoch; <c>release.json</c> older than <paramref name="kept"/>'s within the list's epoch (a later epoch
    /// starts its issues again); and releases below the list's lowest. The releases carry the list's epoch.
    /// </summary>
    /// <returns>Both, or why they aren't taken.</returns>
    std::expected<MCTrusted, std::string> Check(std::string_view keyList, std::string_view releases,
                                                std::span<const MCSignedFile::MCPublicKey> rootKeys, const MCKept& kept,
                                                uint64_t lowestKeyListIssue = 0);

    /// <summary>
    /// What <paramref name="kept"/> becomes once <paramref name="trusted"/> is taken: the higher issues and release.json's
    /// mirrors; under a key list of a later epoch, that epoch, its release.json's issue and no skip.
    /// </summary>
    MCKept Keep(const MCKept& kept, const MCTrusted& trusted);

    /// <summary>Where <paramref name="release"/> comes in the order releases are taken.</summary>
    MCOrder OrderOf(const MCRelease& release);

    /// <summary>An update the Mac's Sparkle found in its feed, as the game checks it (apps/MCRedux/MCSparkleUpdater.mm).</summary>
    struct MCFeedItem
    {
        /// <summary>The build it installs (its <c>sparkle:version</c>, the app's CFBundleVersion).</summary>
        std::string Build;
        /// <summary>Its archive's EdDSA signature in base64, as the feed spells it (<c>sparkle:edSignature</c>), and its length.</summary>
        std::string EdSignature;
        uint64_t Size = 0;
        /// <summary>
        /// Its delta updates, and whether it's one itself: Sparkle takes a delta over the archive chosen, and checks a
        /// delta only against the app's own key.
        /// </summary>
        size_t Deltas = 0;
        bool Delta = false;
    };

    /// <summary>
    /// Why the Mac's Sparkle may not install <paramref name="item"/> over build <paramref name="runningBuild"/> (the
    /// running app's CFBundleVersion), or empty: only the program <paramref name="held"/>, the one release.json gives
    /// this copy, goes in: its build, newer than the one running, with no delta updates, its archive's size and one of
    /// its signatures as held (Sparkle then checks the signature against the app's key, so the archive is the one
    /// release.json names).
    /// </summary>
    std::string FeedItemProblem(const MCProgram& held, std::string_view runningBuild, const MCFeedItem& item);

    /// <summary>
    /// The release for a copy whose rollout number is <paramref name="rolloutNumber"/> (made once, at random): the
    /// rolling-out one when the number falls below its share, else the one every copy takes.
    /// </summary>
    const MCRelease& For(const MCReleases& releases, uint32_t rolloutNumber);

    /// <summary>What <c>update.json</c>'s <paramref name="text"/> keeps.</summary>
    std::expected<MCKept, std::string> ReadKept(std::string_view text);

    /// <summary><c>update.json</c> as written.</summary>
    std::string KeptText(const MCKept& kept);

    /// <summary>The payloads as the release script writes them (the tests make files with them).</summary>
    std::string KeyListPayload(const MCKeyList& keys);
    std::string ReleasesPayload(const MCReleases& releases);
}
