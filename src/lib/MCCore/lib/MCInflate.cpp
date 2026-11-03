#include "stdafx.h"
#include "lib/MCInflate.h"

// libdeflate (src/lib/libdeflate-1.26, MIT): its decompressor alone.
#include "libdeflate.h"

namespace
{
    struct FreeDecompressor
    {
        void operator()(libdeflate_decompressor* decompressor) const { libdeflate_free_decompressor(decompressor); }
    };

    /// <summary>This thread's decompressor, made the first time it is needed (null: no memory for one).</summary>
    libdeflate_decompressor* Decompressor()
    {
        thread_local const std::unique_ptr<libdeflate_decompressor, FreeDecompressor> decompressor(
            libdeflate_alloc_decompressor());
        return decompressor.get();
    }
}

auto MCInflate::Inflate(std::span<const uint8_t> deflated, size_t size)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    libdeflate_decompressor* decompressor = Decompressor();

    if (decompressor == nullptr)
    {
        return std::unexpected(std::format("no memory to unpack a DEFLATE stream of {} bytes", deflated.size()));
    }

    std::vector<uint8_t> out(size);

    // A raw stream (no zlib or gzip wrapper). libdeflate stops at the buffer's end, so a stream that would unpack to
    // more fails instead of writing past it, as one that is damaged or ends early does.
    size_t written = 0;
    const libdeflate_result result =
        libdeflate_deflate_decompress(decompressor, deflated.data(), deflated.size(), out.data(), out.size(), &written);

    if (result != LIBDEFLATE_SUCCESS)
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
