#pragma once

/// <summary>
/// Port-only: BC7 texture compression for the HD art pack (sprite/MCHdArt.h), which keeps its colours as BC7 blocks the
/// GPU samples directly: a quarter of RGBA8's size. A thin wrapper over bc7enc_rdo (src/lib/bc7enc_rdo, by Richard
/// Geldreich, Jr.; MIT or public domain): its encoder (modes 1, 5, 6 and 7), its decoder (every mode), and its entropy
/// reduction, which nudges blocks towards their neighbours' bytes so the pack's DEFLATE packs them smaller.
/// </summary>
namespace MCBc7
{
    /// <summary>A block's bytes, and the pixels a block covers a side.</summary>
    inline constexpr size_t BlockBytes = 16;
    inline constexpr int32_t BlockSide = 4;

    /// <summary>The encoder's most effort (bc7enc's uber level: slower, closer).</summary>
    inline constexpr int32_t MaxEffort = 4;

    /// <summary>How <see cref="Encode"/> works.</summary>
    struct MCBc7Options
    {
        /// <summary>0 (fast: frames the renderer encodes as it draws) to <see cref="MaxEffort"/> (a pack made once).</summary>
        int32_t Effort = 0;
        /// <summary>
        /// The entropy reduction's trade of error for size (bc7enc_rdo's lambda: about 0.5 to 8, larger packs smaller and
        /// differs more); 0 leaves the blocks as encoded.
        /// </summary>
        float RdoLambda = 0.0f;
    };

    /// <summary>Compresses 4x4 RGBA8 pixels (row by row, 64 bytes) into a block, at the fast effort.</summary>
    void EncodeBlock(std::span<const uint8_t, 64> pixels, std::span<uint8_t, BlockBytes> block);

    /// <summary>The 4x4 RGBA8 pixels of a block, of any mode; an invalid block decodes as transparent black.</summary>
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
    std::vector<uint8_t> Encode(std::span<const uint8_t> rgba, int32_t width, int32_t height,
                                const MCBc7Options& options = {});

    /// <summary>The RGBA8 picture (rows <c>4 * width</c> bytes) of <see cref="Encode"/>'s blocks.</summary>
    std::vector<uint8_t> Decode(std::span<const uint8_t> blocks, int32_t width, int32_t height);
}
