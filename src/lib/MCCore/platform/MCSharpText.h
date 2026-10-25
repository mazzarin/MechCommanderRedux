#pragma once

/// <summary>
/// Port-only: the game's bitmap font glyphs redrawn sharp, for a screen drawn at the size it is shown
/// (<see cref="MCFrame::SharpScreen"/>). A glyph's ink pixels are joined into strokes, one pixel wide with round ends,
/// between the centres of the pixels that touch (a diagonal only where no straight pair already joins them): curves
/// come out round and diagonals straight, in the same letterforms, at any size. Nothing is made up: the strokes are the
/// glyph's own pixels, and each redrawn pixel keeps the font byte of the ink pixel nearest it, so the glyph's colours
/// (its table, its shading) are the original's.
/// </summary>
namespace MCSharpText
{
    /// <summary>The redrawn pixels per glyph pixel across and down.</summary>
    inline constexpr int32_t Scale = 8;

    /// <summary>The font byte that is no ink (the glyph's background).</summary>
    inline constexpr uint8_t NoInk = 255;

    /// <summary>
    /// The glyph <paramref name="columns"/> x <paramref name="rows"/> bytes at <paramref name="glyph"/>, its rows
    /// <paramref name="pitch"/> apart, redrawn at <see cref="Scale"/>: RGBA8, <c>columns * Scale</c> pixels a row and
    /// <c>rows * Scale</c> rows. Red is the font byte of the nearest ink pixel, green the strokes' coverage (255 inside).
    /// </summary>
    std::vector<uint8_t> Redraw(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns, int32_t rows);
}
