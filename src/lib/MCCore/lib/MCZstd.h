#pragma once

/// <summary>
/// Port-only: Zstandard decoding for the version 2 HD art pack's streams (the workspace's SPEC-pack-v2.md, which packs
/// them with zstd at level 19): Zstandard's decompressor (src/lib/zstd-1.5.7), on one frame that carries its content
/// checksum.
/// </summary>
namespace MCZstd
{
    /// <summary>
    /// The <paramref name="size"/> bytes <paramref name="packed"/> unpacks to, or what is wrong with it: it isn't one
    /// whole frame, carries no checksum, is damaged (its checksum fails), or unpacks to another size. Never writes past
    /// <paramref name="size"/>.
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> Unpack(std::span<const uint8_t> packed, size_t size);
}
