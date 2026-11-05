#pragma once

/// <summary>
/// Port-only: a signed update file (the key list, <c>release.json</c>), as a DSSE envelope (the Dead Simple Signing
/// Envelope of in-toto and Sigstore): a JSON object holding the payload in base64, its type, and Ed25519 signatures of
/// "DSSEv1 &lt;type's length&gt; &lt;type&gt; &lt;payload's length&gt; &lt;payload&gt;". The signature covers the
/// payload's exact bytes and its type, so nothing is put back into a canonical form before checking, and a key list
/// can't be passed off as a release file. Sparkle's <c>sign_update</c> makes the signatures (the release script).
/// libsodium checks them.
/// </summary>
namespace MCSignedFile
{
    /// <summary>An Ed25519 public key.</summary>
    using MCPublicKey = std::array<uint8_t, 32>;

    /// <summary>The key list's payload type: the release keys, signed by the root key.</summary>
    constexpr std::string_view KeyListType = "application/vnd.mechcommander-redux.keys+json";
    /// <summary><c>release.json</c>'s payload type, signed by a release key.</summary>
    constexpr std::string_view ReleaseType = "application/vnd.mechcommander-redux.release+json";

    /// <summary>
    /// The payload of <paramref name="envelope"/> when its type is <paramref name="type"/> and one of its signatures
    /// is by one of <paramref name="keys"/>.
    /// </summary>
    /// <returns>The payload's bytes, or why the file isn't trusted.</returns>
    std::expected<std::string, std::string> Open(std::string_view envelope, std::string_view type,
                                                 std::span<const MCPublicKey> keys);

    /// <summary>What's signed: DSSE's pre-authentication encoding of <paramref name="payload"/> as <paramref name="type"/>.</summary>
    std::string SignedBytes(std::string_view type, std::string_view payload);

    /// <summary>An envelope holding <paramref name="payload"/> with <paramref name="signatures"/> (each 64 bytes).</summary>
    std::string Envelope(std::string_view type, std::string_view payload,
                         std::span<const std::vector<uint8_t>> signatures);

    /// <summary>The key the base64 text <paramref name="text"/> spells, or none.</summary>
    std::optional<MCPublicKey> KeyFromBase64(std::string_view text);
}
