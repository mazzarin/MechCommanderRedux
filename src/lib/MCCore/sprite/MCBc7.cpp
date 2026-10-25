#include "stdafx.h"
#include "sprite/MCBc7.h"

// bc7enc_rdo (src/lib/bc7enc_rdo): the encoder, the decoder and the entropy reduction.
#include "bc7decomp.h"
#include "bc7enc.h"
#include "ert.h"

namespace
{
    constexpr int32_t Channels = 4;
    constexpr int32_t BlockPixels = MCBc7::BlockSide * MCBc7::BlockSide;
    constexpr size_t BlockPixelBytes = static_cast<size_t>(BlockPixels) * Channels;

    /// <summary>bc7enc's tables, built once before the first block (it asks for this, or blocks come out wrong).</summary>
    void InitEncoder()
    {
        static std::once_flag once;
        std::call_once(once, [] { bc7enc_compress_block_init(); });
    }

    /// <summary>bc7enc's settings for <paramref name="effort"/>: every mode, linear RGBA weights (the colours are
    /// premultiplied, so alpha counts as much as the rest), as bc7enc_rdo's own tool sets them.</summary>
    bc7enc_compress_block_params EncoderParams(int32_t effort)
    {
        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        bc7enc_compress_block_params_init_linear_weights(&params);
        params.m_uber_level = static_cast<uint32_t>(std::clamp(effort, 0, MCBc7::MaxEffort));
        return params;
    }

    /// <summary>
    /// The 4x4 pixels of block (<paramref name="bx"/>, <paramref name="by"/>) of an RGBA8 picture; those past its edges
    /// (or past the bytes given) are transparent black.
    /// </summary>
    std::array<uint8_t, BlockPixelBytes> BlockPixelsOf(std::span<const uint8_t> rgba, int32_t width, int32_t height,
                                                       int32_t bx, int32_t by)
    {
        std::array<uint8_t, BlockPixelBytes> out{};

        for (int32_t y = 0; y < MCBc7::BlockSide; y++)
        {
            const int32_t row = by * MCBc7::BlockSide + y;

            for (int32_t x = 0; x < MCBc7::BlockSide; x++)
            {
                const int32_t column = bx * MCBc7::BlockSide + x;
                const size_t from = (static_cast<size_t>(row) * width + column) * Channels;

                if (row < height && column < width && from + Channels <= rgba.size())
                {
                    std::copy_n(rgba.begin() + static_cast<ptrdiff_t>(from), Channels,
                                out.begin() + (y * MCBc7::BlockSide + x) * Channels);
                }
            }
        }

        return out;
    }

    /// <summary>The entropy reduction's way to see what a block it tries looks like.</summary>
    bool UnpackForRdo(const void* block, ert::color_rgba* pixels, uint32_t /*blockIndex*/, void* /*user*/)
    {
        static_assert(sizeof(ert::color_rgba) == sizeof(bc7decomp::color_rgba));
        return bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(pixels));
    }
}

void MCBc7::EncodeBlock(std::span<const uint8_t, 64> pixels, std::span<uint8_t, BlockBytes> block)
{
    InitEncoder();
    const bc7enc_compress_block_params params = EncoderParams(0);
    bc7enc_compress_block(block.data(), pixels.data(), &params);
}

void MCBc7::DecodeBlock(std::span<const uint8_t, BlockBytes> block, std::span<uint8_t, 64> pixels)
{
    static_assert(sizeof(bc7decomp::color_rgba) == Channels);

    if (!bc7decomp::unpack_bc7(block.data(), reinterpret_cast<bc7decomp::color_rgba*>(pixels.data())))
    {
        std::ranges::fill(pixels, uint8_t{0});
    }
}

std::vector<uint8_t> MCBc7::Encode(std::span<const uint8_t> rgba, int32_t width, int32_t height,
                                   const MCBc7Options& options)
{
    InitEncoder();
    const int32_t columns = Padded(width) / BlockSide;
    const int32_t rows = Padded(height) / BlockSide;
    const size_t count = static_cast<size_t>(std::max(columns, 0)) * static_cast<size_t>(std::max(rows, 0));
    std::vector<uint8_t> blocks(count * BlockBytes);
    std::vector<uint8_t> pixels(count * BlockPixelBytes);
    const bc7enc_compress_block_params params = EncoderParams(options.Effort);

    for (int32_t by = 0; by < rows; by++)
    {
        for (int32_t bx = 0; bx < columns; bx++)
        {
            const size_t index = static_cast<size_t>(by) * columns + bx;
            const std::array<uint8_t, BlockPixelBytes> block = BlockPixelsOf(rgba, width, height, bx, by);
            std::ranges::copy(block, pixels.begin() + static_cast<ptrdiff_t>(index * BlockPixelBytes));
            bc7enc_compress_block(blocks.data() + index * BlockBytes, block.data(), &params);
        }
    }

    if (options.RdoLambda > 0.0f && count > 0)
    {
        ert::reduce_entropy_params rdo;
        rdo.m_lambda = options.RdoLambda;
        uint32_t modified = 0;
        ert::reduce_entropy(blocks.data(), static_cast<uint32_t>(count), static_cast<uint32_t>(BlockBytes),
                            static_cast<uint32_t>(BlockBytes), BlockSide, BlockSide, Channels,
                            reinterpret_cast<const ert::color_rgba*>(pixels.data()), rdo, modified, UnpackForRdo,
                            nullptr);
    }

    return blocks;
}

std::vector<uint8_t> MCBc7::Decode(std::span<const uint8_t> blocks, int32_t width, int32_t height)
{
    const int32_t columns = Padded(width) / BlockSide;
    const int32_t rows = Padded(height) / BlockSide;
    std::vector<uint8_t> rgba(static_cast<size_t>(std::max(width, 0)) * static_cast<size_t>(std::max(height, 0)) *
                              Channels);

    if (columns <= 0 || rows <= 0 || blocks.size() < static_cast<size_t>(columns) * rows * BlockBytes)
    {
        return rgba;
    }

    for (int32_t by = 0; by < rows; by++)
    {
        for (int32_t bx = 0; bx < columns; bx++)
        {
            std::array<uint8_t, BlockPixelBytes> pixels{};
            DecodeBlock(std::span<const uint8_t, BlockBytes>(
                            blocks.data() + (static_cast<size_t>(by) * columns + bx) * BlockBytes, BlockBytes),
                        pixels);

            for (int32_t y = 0; y < BlockSide; y++)
            {
                const int32_t row = by * BlockSide + y;

                for (int32_t x = 0; x < BlockSide; x++)
                {
                    const int32_t column = bx * BlockSide + x;

                    if (row < height && column < width)
                    {
                        std::copy_n(pixels.begin() + (y * BlockSide + x) * Channels, Channels,
                                    rgba.begin() +
                                        static_cast<ptrdiff_t>((static_cast<size_t>(row) * width + column) * Channels));
                    }
                }
            }
        }
    }

    return rgba;
}
