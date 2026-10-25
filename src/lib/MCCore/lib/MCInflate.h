#pragma once

/// <summary>
/// Port-only: DEFLATE decoding for the HD art pack's archives, whose entries tools/upscale packs with zlib: miniz's
/// inflate (src/lib/miniz-3.0.2), on a raw stream (no zlib or gzip wrapper).
/// </summary>
namespace MCInflate
{
    /// <summary>
    /// The <paramref name="size"/> bytes <paramref name="deflated"/> unpacks to, or what is wrong with it (it is damaged,
    /// ends early, or unpacks to another size); never writes past <paramref name="size"/>.
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> Inflate(std::span<const uint8_t> deflated, size_t size);
}
