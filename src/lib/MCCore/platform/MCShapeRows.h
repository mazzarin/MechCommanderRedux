#pragma once

/// <summary>
/// Port-only: a run-length shape's rows (VFX's format, see MCSoftwareRendererShapes.cpp) walked and decoded, for the
/// GPU renderer's atlas and the sharp line art (platform/MCSharpPictures) alike. Each row is tokens: a token's high
/// seven bits count pixels (bit 0: a literal of that many bytes, else a run of the next byte), 0 with bit 0 set skips
/// the next byte's count of pixels, 0 ends the row.
/// </summary>
namespace MCShapeRows
{
    /// <summary>A decoded pixel's bytes (<see cref="Decode"/>): its value, then its state.</summary>
    inline constexpr size_t DecodedBytes = 2;
    /// <summary>A decoded pixel's states: nothing there, a pixel drawn, a pixel skipped.</summary>
    inline constexpr uint8_t Empty = 0;
    inline constexpr uint8_t Drawn = 1;
    inline constexpr uint8_t Skipped = 2;
    /// <summary>Where a shape's rows start after its header.</summary>
    inline constexpr size_t RowsAt = 0x18;

    /// <summary>Walks <paramref name="rows"/> rows of <paramref name="bytes"/>: the widest row and the bytes they take.</summary>
    size_t Extent(std::span<const uint8_t> bytes, int32_t rows, int32_t& width);

    /// <summary>
    /// Decodes <paramref name="rows"/> rows of <paramref name="bytes"/> into <paramref name="out"/>, <paramref name="width"/>
    /// pixels a row of <see cref="DecodedBytes"/> each: the value, then the state (<see cref="Drawn"/>,
    /// <see cref="Skipped"/> or <see cref="Empty"/>).
    /// </summary>
    void Decode(std::span<const uint8_t> bytes, int32_t rows, int32_t width, std::span<uint8_t> out);

    /// <summary>The rows a shape's header (<paramref name="header"/>, its 0x18 bytes) says it covers.</summary>
    int32_t HeaderRows(std::span<const uint8_t> header);
}
