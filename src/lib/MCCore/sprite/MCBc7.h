#pragma once

/// <summary>
/// Port-only: BC7 texture compression for the HD art pack (sprite/MCHdArt.h), which keeps its colours as BC7 blocks the
/// GPU samples directly: a quarter of RGBA8's size. The encoder writes mode 6 only (one subset, RGBA endpoints with a
/// p-bit each, 4-bit indices): principal-axis endpoints refined by least squares, each p-bit pair tried. The decoder
/// reads mode 6 blocks (what the encoder writes), for a GPU without BC7 and for tests.
/// </summary>
namespace MCBc7
{
    /// <summary>A block's bytes, and the pixels a block covers a side.</summary>
    inline constexpr size_t BlockBytes = 16;
    inline constexpr int32_t BlockSide = 4;

    /// <summary>Compresses 4x4 RGBA8 pixels (row by row, 64 bytes) into a block.</summary>
    void EncodeBlock(std::span<const uint8_t, 64> pixels, std::span<uint8_t, BlockBytes> block);

    /// <summary>The 4x4 RGBA8 pixels of a mode 6 block; another mode decodes as opaque magenta.</summary>
    void DecodeBlock(std::span<const uint8_t, BlockBytes> block, std::span<uint8_t, 64> pixels);

    /// <summary>A side rounded up to whole blocks.</summary>
    constexpr int32_t Padded(int32_t side)
    {
        return (side + BlockSide - 1) / BlockSide * BlockSide;
    }

    /// <summary>
    /// The blocks of a <paramref name="width"/> x <paramref name="height"/> RGBA8 picture (rows <c>4 * width</c>
    /// bytes), block row by block row; the pixels past its right and bottom edges are transparent black.
    /// </summary>
    std::vector<uint8_t> Encode(std::span<const uint8_t> rgba, int32_t width, int32_t height);

    /// <summary>The RGBA8 picture (rows <c>4 * width</c> bytes) of <see cref="Encode"/>'s blocks.</summary>
    std::vector<uint8_t> Decode(std::span<const uint8_t> blocks, int32_t width, int32_t height);
}
