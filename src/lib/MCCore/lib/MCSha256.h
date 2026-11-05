#pragma once

/// <summary>
/// Port-only: SHA-256, which names an update's files and checks every one the game downloads, stages or keeps
/// (update/). libsodium computes it (src/lib/libsodium-1.0.22).
/// </summary>
namespace MCSha256
{
    /// <summary>A SHA-256 hash.</summary>
    using MCDigest = std::array<uint8_t, 32>;

    /// <summary>The hash of <paramref name="bytes"/>.</summary>
    MCDigest Of(std::span<const uint8_t> bytes);

    /// <summary>The hash of <paramref name="text"/>'s bytes.</summary>
    MCDigest Of(std::string_view text);

    /// <summary>The hash of the file at <paramref name="path"/>, read a megabyte at a time.</summary>
    /// <returns>The hash, or why the file couldn't be read.</returns>
    std::expected<MCDigest, std::string> OfFile(const std::filesystem::path& path);

    /// <summary><paramref name="digest"/> as 64 lower-case hex digits, as updates' files write it.</summary>
    std::string Hex(const MCDigest& digest);

    /// <summary>The hash 64 hex digits spell (either case), or none when they don't.</summary>
    std::optional<MCDigest> FromHex(std::string_view hex);
}
