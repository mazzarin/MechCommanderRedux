#include "stdafx.h"
#include "lib/MCInflate.h"

// miniz (src/lib/miniz-3.0.2, MIT), as the editor uses it for the Gold CD's cabinets; its zlib names stay out.
#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#include "miniz.h"

auto MCInflate::Inflate(std::span<const uint8_t> deflated, size_t size)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    std::vector<uint8_t> out(size);

    // A raw stream (no zlib header flag). tinfl stops at the buffer's end, so a stream that would unpack to more fails
    // instead of writing past it, as one that is damaged or ends early does.
    const size_t written = tinfl_decompress_mem_to_mem(out.data(), out.size(), deflated.data(), deflated.size(), 0);

    if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
    {
        return std::unexpected(std::format(
            "a DEFLATE stream of {} bytes is damaged, ends early or unpacks past {} bytes", deflated.size(), size));
    }

    if (written != size)
    {
        return std::unexpected(std::format("a DEFLATE stream unpacks to {} bytes, not {}", written, size));
    }

    return out;
}
