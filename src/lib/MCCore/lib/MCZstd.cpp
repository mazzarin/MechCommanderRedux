#include "stdafx.h"
#include "lib/MCZstd.h"

// Zstandard (src/lib/zstd-1.5.7, BSD): its decompressor alone, linked statically, so its frame header reader too.
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

namespace
{
    struct FreeContext
    {
        void operator()(ZSTD_DCtx* context) const { ZSTD_freeDCtx(context); }
    };

    /// <summary>This thread's decompression context, made the first time it is needed (null: no memory for one).</summary>
    ZSTD_DCtx* Context()
    {
        thread_local const std::unique_ptr<ZSTD_DCtx, FreeContext> context(ZSTD_createDCtx());
        return context.get();
    }
}

auto MCZstd::Unpack(std::span<const uint8_t> packed, size_t size) -> std::expected<std::vector<uint8_t>, std::string>
{
    // One whole frame and nothing past it: a stream cut short, or with bytes after its frame, is refused here.
    const size_t frameBytes = ZSTD_findFrameCompressedSize(packed.data(), packed.size());

    if (ZSTD_isError(frameBytes) != 0 || frameBytes != packed.size())
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes isn't one whole frame", packed.size()));
    }

    ZSTD_FrameHeader header{};

    if (ZSTD_getFrameHeader(&header, packed.data(), packed.size()) != 0 || header.frameType != ZSTD_frame ||
        header.checksumFlag == 0)
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes carries no checksum", packed.size()));
    }

    ZSTD_DCtx* context = Context();

    if (context == nullptr)
    {
        return std::unexpected(std::format("no memory to unpack a Zstandard stream of {} bytes", packed.size()));
    }

    std::vector<uint8_t> out(size);

    // The frame's checksum is checked as it unpacks; a frame that would unpack to more than the buffer fails instead of
    // writing past it.
    const size_t written = ZSTD_decompressDCtx(context, out.data(), out.size(), packed.data(), packed.size());

    if (ZSTD_isError(written) != 0)
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes is damaged or unpacks past {} bytes ({})",
                                           packed.size(), size, ZSTD_getErrorName(written)));
    }

    if (written != size)
    {
        return std::unexpected(std::format("a Zstandard stream unpacks to {} bytes, not {}", written, size));
    }

    return out;
}
