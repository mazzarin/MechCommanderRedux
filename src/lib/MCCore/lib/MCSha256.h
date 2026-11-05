#pragma once

// Standard headers only: update/MCUpdateDisk.cpp includes this without the precompiled header on Windows.
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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

    /// <summary>What a hash reading a file ends with when <c>goOn</c> says to stop.</summary>
    constexpr std::string_view StoppedError = "stopped";

    /// <summary>A hash taken in pieces, as the bytes come (a download as it arrives, an entry as it's unpacked).</summary>
    class MCHasher
    {
    public:
        MCHasher();
        ~MCHasher();
        MCHasher(const MCHasher&) = delete;
        MCHasher& operator=(const MCHasher&) = delete;

        void Add(std::span<const char> bytes);

        /// <summary>
        /// Adds the first <paramref name="count"/> bytes of the file at <paramref name="path"/> (all of it when it's
        /// shorter), read a megabyte at a time; <paramref name="goOn"/>, when given, is asked before each and returns
        /// false to stop.
        /// </summary>
        /// <returns>The bytes added, or why the file couldn't be read (<see cref="StoppedError"/>: stopped).</returns>
        std::expected<uint64_t, std::string> AddFile(const std::filesystem::path& path, uint64_t count,
                                                     const std::function<bool()>& goOn = {});

        /// <summary>The hash of the bytes added; nothing more can be added after.</summary>
        MCDigest Finish();

    private:
        struct MCState;
        std::unique_ptr<MCState> _State;
    };

    /// <summary>
    /// The hash of the file at <paramref name="path"/>, read a megabyte at a time; <paramref name="goOn"/>, when given,
    /// is asked before each and returns false to stop.
    /// </summary>
    /// <returns>The hash, or why the file couldn't be read (<see cref="StoppedError"/>: stopped).</returns>
    std::expected<MCDigest, std::string> OfFile(const std::filesystem::path& path,
                                                const std::function<bool()>& goOn = {});

    /// <summary><paramref name="digest"/> as 64 lower-case hex digits, as updates' files write it.</summary>
    std::string Hex(const MCDigest& digest);

    /// <summary>The hash 64 hex digits spell (either case), or none when they don't.</summary>
    std::optional<MCDigest> FromHex(std::string_view hex);
}
