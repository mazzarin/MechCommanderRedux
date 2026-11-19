#pragma once

#include "platform/MCSharpLines.h"
#include "platform/MCSharpText.h"

/// <summary>
/// Port-only: the pictures a sharp screen (<see cref="MCFrame::SharpScreen"/>) draws the HD menus' text and line art
/// with: a font's glyphs redrawn by <see cref="MCSharpText"/> and line art by <see cref="MCSharpLines"/>. Here: what
/// each is cut from and the key the GPU renderer keeps it by (its draws and the pictures made ahead share them), and
/// the pictures made ahead on threads of their own (<see cref="MakeFont"/>, <see cref="MakeLineArt"/>), which the
/// renderer takes (<see cref="Take"/>) rather than redraw them as a screen first shows them: a redraw takes 0.1 to 1 ms
/// of the game's thread, and a screen's first frame had hundreds (the purchase screen's first visit, 40-60 ms).
/// </summary>
/// <remarks>
/// Threads: everything here from the game's thread. The pictures are made on a thread of its own with helpers
/// (lib/MCWorkers), below the game's threads, from copies of what they're cut from.
/// </remarks>
namespace MCSharpPictures
{
    /// <summary>
    /// The redraws' version, part of every picture's key: one more whenever what MCSharpText or MCSharpLines draws
    /// changes, so the pictures of the HD art pack's sharp.hda, made by the version before, are left alone (made here
    /// again) until it's made again (tools/upscale/make_sharp_pack.py). Tests pin the redraws of this version.
    /// </summary>
    inline constexpr uint64_t RedrawVersion = 1;

    /// <summary>A VFX font's glyph: where it lies in the font, its width (its rows' pitch), the font's height, its bytes.</summary>
    struct MCFontGlyph
    {
        size_t At = 0;
        int32_t Pitch = 0;
        int32_t Height = 0;
        std::span<const uint8_t> Pixels;
    };

    /// <summary>The glyphs in the VFX font <paramref name="font"/>'s table (0 when it has none).</summary>
    int32_t FontGlyphCount(std::span<const uint8_t> font);

    /// <summary>
    /// Glyph <paramref name="character"/> of the VFX font <paramref name="font"/> whole, as it's redrawn: its width by
    /// the font's height. None when it isn't in the font's table or its bytes run past the font's.
    /// </summary>
    std::optional<MCFontGlyph> FontGlyph(std::span<const uint8_t> font, int32_t character);

    /// <summary>
    /// A glyph's pictures: the key they're kept by and, for a glyph whose ink pixels of different bytes touch
    /// (<see cref="MCSharpText::Layered"/>), its layers, a picture each, back to front.
    /// </summary>
    struct MCGlyphPictures
    {
        uint64_t Key = 0;
        std::optional<MCSharpText::MCGlyphLayers> Layers;

        /// <summary>How many pictures: one a layer, else one.</summary>
        size_t Count() const { return Layers.has_value() ? Layers->Bytes.size() : 1; }
        /// <summary>The key picture <paramref name="picture"/> is kept by.</summary>
        uint64_t KeyOf(size_t picture) const;
    };

    /// <summary>
    /// The pictures of the glyph <paramref name="columns"/> x <paramref name="rows"/> at <paramref name="glyph"/> (rows
    /// its width apart) of a font layered as <paramref name="font"/> says: kept by its bytes and size, so the same glyph
    /// from any font or place is redrawn once, and a layered one by its layers too.
    /// </summary>
    MCGlyphPictures GlyphPictures(std::span<const uint8_t> glyph, int32_t columns, int32_t rows,
                                  const MCSharpText::MCFontLayers& font);

    /// <summary>The glyph's pictures redrawn (<see cref="MCGlyphPictures::Count"/>, in its order).</summary>
    std::vector<std::vector<uint8_t>> RedrawGlyph(std::span<const uint8_t> glyph, int32_t columns, int32_t rows,
                                                  const MCGlyphPictures& pictures);

    /// <summary>
    /// The line art in a shape's rows <paramref name="shapeRows"/> (<see cref="MCShapeRows"/>'s), <paramref name="columns"/>
    /// x <paramref name="rows"/> bytes: its drawn pixels' values, <see cref="MCSharpText::NoInk"/> where none is drawn.
    /// </summary>
    std::vector<uint8_t> LineArt(std::span<const uint8_t> shapeRows, int32_t columns, int32_t rows);

    /// <summary>The key line art is kept by: its bytes and size, and its parts drawn by hand.</summary>
    uint64_t LineArtKey(std::span<const uint8_t> art, int32_t columns, int32_t rows,
                        std::span<const MCSharpLines::MCDrawnLoop> drawn);

    /// <summary>Line art cut from a shape frame: its pixels (<see cref="LineArt"/>'s), its size and its key.</summary>
    struct MCLineArtCut
    {
        std::vector<uint8_t> Art;
        int32_t Columns = 0;
        int32_t Rows = 0;
        uint64_t Key = 0;
    };

    /// <summary>
    /// Frame <paramref name="frame"/> of the shape table <paramref name="table"/> cut as line art whole (the rows its
    /// header covers, as the game draws it), <paramref name="drawn"/> its parts drawn by hand: none when the table has no
    /// such frame or it draws nothing.
    /// </summary>
    std::optional<MCLineArtCut> CutLineArt(std::span<const uint8_t> table, int32_t frame,
                                           std::span<const MCSharpLines::MCDrawnLoop> drawn);

    /// <summary>
    /// A picture made ahead, as the renderer uploads it: RGBA8 (<see cref="MCSharpText::Redraw"/>'s form, red the byte,
    /// green the coverage), kept as its red and green alone when its blue and alpha are 0 throughout, as the redraws
    /// leave them.
    /// </summary>
    class MCMadePicture
    {
    public:
        explicit MCMadePicture(std::vector<uint8_t> rgba);

        /// <summary>A picture kept as <paramref name="redGreen"/>, its pixels' red and green bytes (blue and alpha 0).</summary>
        static MCMadePicture OfRedGreen(std::vector<uint8_t> redGreen);

        /// <summary>Its bytes as RGBA8.</summary>
        size_t RgbaBytes() const { return _RgbaBytes; }
        /// <summary>The bytes it's kept in.</summary>
        size_t KeptBytes() const { return _Bytes.size(); }
        /// <summary>Its pixels' red and green bytes, when it's kept so (none: kept whole).</summary>
        std::optional<std::span<const uint8_t>> RedGreen() const;
        /// <summary>Writes it as RGBA8 into <paramref name="out"/> (<see cref="RgbaBytes"/> of them).</summary>
        void CopyRgbaTo(std::span<uint8_t> out) const;

    private:
        MCMadePicture() = default;

        std::vector<uint8_t> _Bytes;
        size_t _RgbaBytes = 0;
        bool _RedGreen = false;
    };

    /// <summary>
    /// Has the glyphs of the printable characters (space to tilde, the game's text) of the VFX font
    /// <paramref name="font"/> made ahead, those not made or drawn already (its bytes are copied): taken from the HD art
    /// pack's sharp.hda (MCHdArt::SharpArchive, read whole by the making thread the first time) where it has them.
    /// </summary>
    void MakeFont(std::span<const uint8_t> font);

    /// <summary>
    /// Has frame <paramref name="frame"/> of the shape table <paramref name="table"/> made ahead as line art whole (the
    /// rows its header covers, as the game draws it), <paramref name="drawn"/> its parts drawn by hand, unless made or
    /// drawn already, or the line art made ahead and not taken would take more than <see cref="MostLineArtBytes"/>:
    /// taken from the pack's sharp.hda where it has it, as a font's glyphs are.
    /// </summary>
    void MakeLineArt(std::span<const uint8_t> table, int32_t frame, std::span<const MCSharpLines::MCDrawnLoop> drawn);

    /// <summary>
    /// The most bytes line art made ahead and not taken is kept in (at 8 pixels a pixel, a mech's diagrams take about
    /// 2.5 MB); past it, a list's drawings are made as they're drawn.
    /// </summary>
    inline constexpr size_t MostLineArtBytes = size_t{48} * 1024 * 1024;

    /// <summary>
    /// The picture made ahead under <paramref name="key"/>, handed over (kept here no more), waiting while it's being
    /// made; else the pack's sharp.hda's, when read already and it has one redrawn from <paramref name="columns"/> x
    /// <paramref name="rows"/> pixels. None when neither: then it's never made here (the caller makes it).
    /// </summary>
    std::optional<MCMadePicture> Take(uint64_t key, int32_t columns, int32_t rows);

    /// <summary>
    /// Drops the line art asked for and made, not taken (a logistics' lists go with it; what's being made is dropped
    /// when done). The glyphs stay: they're the fonts', for the run.
    /// </summary>
    void ForgetLineArt();

    /// <summary>For the tests: waits until all that was asked for is made.</summary>
    void WaitForMade();

    /// <summary>What was made ahead and taken since the start, and what's kept now.</summary>
    struct MCMadeTally
    {
        /// <summary>Glyphs made ahead, and their pictures (a layer's each).</summary>
        size_t Glyphs = 0;
        size_t GlyphPictures = 0;
        /// <summary>Line art drawings made ahead, and those left to be made as drawn (past MostLineArtBytes).</summary>
        size_t Drawings = 0;
        size_t DrawingsLeft = 0;
        /// <summary>The pictures in the pack's sharp.hda as last read, and those of the above taken from it.</summary>
        size_t PackPictures = 0;
        size_t FromPack = 0;
        /// <summary>The bytes all those were kept in, and would take as RGBA8 (on the GPU).</summary>
        size_t KeptBytes = 0;
        size_t RgbaBytes = 0;
        /// <summary>The pictures the renderer took, and their RGBA8 bytes; and of those, the ones not made ahead, read from the pack then.</summary>
        size_t Taken = 0;
        size_t TakenRgbaBytes = 0;
        size_t TakenFromPack = 0;
        /// <summary>The pictures made and not taken, and the bytes they're kept in.</summary>
        size_t Held = 0;
        size_t HeldBytes = 0;
    };

    MCMadeTally Tally();
}
