#pragma once

/// <summary>
/// Port-only: the game's bitmap font glyphs redrawn sharp, for a screen drawn at the size it is shown
/// (<see cref="MCFrame::SharpScreen"/>). A glyph's ink pixels are joined into strokes, one pixel wide with round ends,
/// between the centres of the pixels that touch (a diagonal only where no straight pair already joins them): curves
/// come out round and diagonals straight, in the same letterforms, at any size. Nothing is made up: the strokes are the
/// glyph's own pixels, and each redrawn pixel keeps the font byte of the ink pixel nearest it, so the glyph's colours
/// (its table, its shading) are the original's.
/// </summary>
/// <remarks>
/// A glyph whose pixels of different bytes touch (a two-tone font's: a body and its drop shadow, or letters on a box;
/// <see cref="Layered"/>) is redrawn as one layer per byte instead (<see cref="RedrawLayers"/>), so each colour keeps
/// its own edges: joined as one, its bytes would blur into one blob, as blocky as its pixels.
/// </remarks>
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

    /// <summary>
    /// A drop shadow: the body moved by each offset from (<c>FromX</c>, <c>FromY</c>) to (<c>ToX</c>, <c>ToY</c>), one
    /// pixel apart at most, where the body isn't. Redrawn, it is the body's strokes swept between the two.
    /// </summary>
    struct MCDropShadow
    {
        uint8_t Body = NoInk;
        uint8_t Shadow = NoInk;
        int32_t FromX = 0;
        int32_t FromY = 0;
        int32_t ToX = 0;
        int32_t ToY = 0;
    };

    /// <summary>How a font's layered glyphs are drawn (<see cref="LayersOf"/>).</summary>
    struct MCFontLayers
    {
        /// <summary>
        /// Each byte's place, back to front (0 the back): the bytes with more of the font's pixels lie further back (the
        /// shadow or box behind the letters).
        /// </summary>
        std::array<uint8_t, 256> Depth{};
        /// <summary>The font's drop shadow, when each of its layered glyphs is a body and its shadow.</summary>
        std::optional<MCDropShadow> Shadow;
    };

    /// <summary>How a layered glyph is drawn: its bytes back to front, and its drop shadow (when it is one).</summary>
    struct MCGlyphLayers
    {
        std::vector<uint8_t> Bytes;
        std::optional<MCDropShadow> Shadow;

        /// <summary>
        /// Whether layer <paramref name="layer"/> runs on past the glyph's border (<see cref="RedrawLayers"/>: a byte
        /// behind the front one, not a shadow), so it is shown to the glyph's edge, not faded there as strokes are.
        /// </summary>
        bool RunsOn(size_t layer) const { return !Shadow.has_value() && layer + 1 < Bytes.size(); }
    };

    /// <summary>
    /// The layers of the VFX font <paramref name="font"/> (from its header), read from the art's own glyphs (not those
    /// MCFontGlyphs makes for its blanks): none placed, no shadow, when its glyphs don't fit its data.
    /// </summary>
    MCFontLayers LayersOf(std::span<const uint8_t> font);

    /// <summary>Whether ink pixels of different bytes touch in the glyph (across, down or diagonally).</summary>
    bool Layered(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns, int32_t rows);

    /// <summary>How the glyph is drawn in its font's <paramref name="font"/> layers.</summary>
    MCGlyphLayers LayersOf(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns, int32_t rows,
                           const MCFontLayers& font);

    /// <summary>
    /// A layered glyph (<see cref="Layered"/>) redrawn as one picture per byte, back to front, each as
    /// <see cref="Redraw"/>'s with its byte throughout. Brought back to the glyph's size, the topmost layer covering a
    /// pixel's centre is its byte's, and none covers a background pixel's. The front byte's pixels are strokes as a
    /// one-colour glyph's. A drop shadow is the body's strokes swept along its offsets; another byte behind is the
    /// strokes of its pixels and those in front of it that touch them, its edges at the glyph's border carried on past
    /// it (a box runs on into the next letter's).
    /// </summary>
    std::vector<std::vector<uint8_t>> RedrawLayers(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns,
                                                   int32_t rows, const MCGlyphLayers& layers);
}
