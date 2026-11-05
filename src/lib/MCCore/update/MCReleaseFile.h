#pragma once

#include "lib/MCSha256.h"
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
/// takes no file older, so an old file served again can't hold copies back. Formats only grow: a newer format adds
/// fields, which this one ignores.
/// </summary>
namespace MCReleaseFile
{
    /// <summary>The format this game reads; a file of a higher one adds what it doesn't know.</summary>
    constexpr uint64_t KnownFormat = 1;

    /// <summary>The key list.</summary>
    struct MCKeyList
    {
        uint64_t Format = 0;
        uint64_t Issue = 0;
        /// <summary>The release keys in force: <c>release.json</c> must be signed by one.</summary>
        std::vector<MCSignedFile::MCPublicKey> ReleaseKeys;
        /// <summary>The lowest release number taken, whatever <c>release.json</c> says.</summary>
        uint64_t LowestRelease = 0;
    };

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
    };

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
    };

    /// <summary>What the game keeps of the files it has read, and of the copy (the user folder's <c>update.json</c>).</summary>
    struct MCKept
    {
        /// <summary>The highest issues read; none is taken lower.</summary>
        uint64_t KeyListIssue = 0;
        uint64_t ReleaseIssue = 0;
        /// <summary>The copy's rollout number (<see cref="For"/>), made once at random and never sent; none yet.</summary>
        std::optional<uint32_t> RolloutNumber;
        /// <summary>The release the player skipped (0: none): it isn't offered again, and a higher one is.</summary>
        uint64_t Skipped = 0;

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
    /// <c>release.json</c> (<paramref name="releases"/>) against the release keys it names; refuses either older than
    /// <paramref name="kept"/>, and releases below the list's lowest.
    /// </summary>
    /// <returns>Both, or why they aren't taken.</returns>
    std::expected<MCTrusted, std::string> Check(std::string_view keyList, std::string_view releases,
                                                std::span<const MCSignedFile::MCPublicKey> rootKeys,
                                                const MCKept& kept);

    /// <summary>What <paramref name="kept"/> becomes once <paramref name="trusted"/> is taken.</summary>
    MCKept Keep(const MCKept& kept, const MCTrusted& trusted);

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
