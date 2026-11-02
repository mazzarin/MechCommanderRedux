#pragma once

#include "vfx/MCVfx.h"

// Every VFX routine MechCommander uses: Miles Design's VFX (hand-written assembly in MCX.EXE, vfxa.asm and
// vfx3d.asm; C names without the cdecl underscore of the asm symbols, _VFX_shape_draw -> VFX_shape_draw) and the
// game's own additions in mcx\vfx\*.cpp (AG_*, fastShapeDraw, the alpha tables, the terrain tile drawer).
// The types are in vfx/MCVfx.h.
//
// Conventions shared by the routines that draw through a pane:
// - coordinates are relative to the pane's (x0, y0); drawing is clipped to the pane intersected with the window;
// - they return VfxErrBadWindow (-1) when the window has no pixels, VfxErrEmptyPane (-2) when the clipped pane
//   is empty, VfxErrClipped (-3) when nothing of the object is inside it, and 0 (or a result) otherwise.
//
// The sections below follow the implementation files; each is owned by its file.

// ---------------------------------------------------------------------------------------------------------------
// Common return codes and modes
// ---------------------------------------------------------------------------------------------------------------

/// <summary>The pane's window has no pixels (x_max or y_max below 0).</summary>
inline constexpr int32_t VfxErrBadWindow = -1;
/// <summary>The pane, clipped to its window, is empty.</summary>
inline constexpr int32_t VfxErrEmptyPane = -2;
/// <summary>The object lies wholly outside the clipped pane.</summary>
inline constexpr int32_t VfxErrClipped = -3;
/// <summary>A shape's bounds are empty (x_max &lt; x_min or y_max &lt; y_min).</summary>
inline constexpr int32_t VfxErrBadShape = -4;

/// <summary>The "no fill" colour of <c>VFX_pane_copy</c> and <c>VFX_pane_scroll</c>.</summary>
inline constexpr int32_t VfxNoColor = -1;

// ---------------------------------------------------------------------------------------------------------------
// vfxa.asm: pixels, lines, rectangles, panes, ellipses (vfx/MCVfxPrimitives.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>Sets one pixel.</summary>
/// <returns>The pixel's previous colour, or VFX_ERR_* (-3 when the point is outside the pane).</returns>
int32_t VfxPixelWrite(const MCPane& pane, int32_t x, int32_t y, uint8_t color);

/// <summary>
/// Draws the line from (x0, y0) to (x1, y1), both ends included, clipped to the pane, in <paramref name="color"/>
/// (its low byte): <c>VFX_line_draw</c>'s LD_DRAW mode.
/// </summary>
/// <returns>0 when the line was drawn whole, 1 when it was clipped, 2 when nothing of it was inside the pane.</returns>
int32_t VfxLineDraw(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color);

/// <summary>
/// Port-only: the line of <see cref="VfxLineDraw"/>, <paramref name="width"/> window pixels wide where lines are drawn
/// smooth (the GPU's HD screens). Its pixels, and the line elsewhere, are the original's.
/// </summary>
int32_t VfxLineDrawWide(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color, float width);

/// <summary>
/// The line of <see cref="VfxLineDraw"/>, each pixel replaced by its entry in the 256-byte <paramref name="table"/>
/// (LD_TRANSLATE).
/// </summary>
int32_t VfxLineTranslate(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, MCColorTable table);

/// <summary>Fills the pane with <paramref name="color"/>.</summary>
int32_t VfxPaneWipe(const MCPane& pane, int32_t color);

/// <summary>
/// Copies the part of <paramref name="source"/> that, placed with its point (sx, sy) on (tx, ty) of
/// <paramref name="target"/>, overlaps the target (both clipped to their windows). <paramref name="fill"/> selects
/// the operation: negative (VfxNoColor) copies; 0..255 fills that area of the target with the colour instead; above
/// 255 copies all but the pixels of colour <c>fill &amp; 0xff</c> (a colour key).
/// </summary>
/// <returns>0, VFX_ERR_* for either pane, or VfxErrClipped when they don't overlap.</returns>
/// <remarks>Rows and columns are walked in the order that survives overlap.</remarks>
int32_t VfxPaneCopy(const MCPane& source, int32_t sx, int32_t sy, const MCPane& target, int32_t tx, int32_t ty,
                    int32_t fill);

/// <summary>
/// Scrolls the pane's contents by (dx, dy). <paramref name="mode"/> 0: a scroll by the pane's size or more wipes it
/// with <c>parm &amp; 0xff</c>; otherwise the image moves and the uncovered strips are filled with
/// <paramref name="parm"/> (VFX_pane_copy's fill: a colour 0..255). <paramref name="mode"/> 1 (wrap): the pixels
/// scrolled out come back on the other side.
/// </summary>
/// <returns>0, VfxErrEmptyPane, or in mode 1 with parm 0 the scratch size the asm needed (width * height).</returns>
/// <remarks>
/// In mode 1 the asm took a scratch buffer of width * height bytes as parm; the port allocates
/// its own and only tests parm against 0.
/// </remarks>
int32_t VfxPaneScroll(const MCPane& pane, int32_t dx, int32_t dy, int32_t mode, int32_t parm);

/// <summary>
/// Fills the ellipse centred at (xc, yc) with radii <paramref name="width"/> and <paramref name="height"/>, one
/// horizontal span per step of the same midpoint walk as <c>VFX_ellipse_draw</c>; a zero radius draws a line.
/// </summary>
int32_t VfxEllipseFill(const MCPane& pane, int32_t xc, int32_t yc, int32_t width, int32_t height, int32_t color);

// ---------------------------------------------------------------------------------------------------------------
// vfxa.asm: shapes (vfx/MCVfxShape.cpp, vfx/MCVfxShapeTransform.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// Draws shape <paramref name="shapeNum"/> of <paramref name="shapeTable"/> with its origin (hot spot) at
/// (hotX, hotY). Colour runs and literal pixels are written as stored; skipped pixels are left alone.
/// </summary>
/// <returns>0, or VFX_ERR_* (-3 wholly clipped, -4 empty bounds).</returns>
int32_t VfxShapeDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                     int32_t hotY);

/// <summary>Sets the 256-byte colour table <c>VFX_shape_translate_draw</c> maps every shape pixel through.</summary>
void VfxShapeLookaside(MCColorTable table);

/// <summary>Draws a shape like <c>VFX_shape_draw</c>, mapping each pixel through the lookaside table.</summary>
int32_t VfxShapeTranslateDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                              int32_t hotY);

/// <summary>
/// Draws a shape rotated by <paramref name="rot"/> (tenths of a degree) and scaled by x_scale/y_scale (16.16) about
/// its hot spot (clockwise on screen for positive angles). The shape is drawn upright into the work
/// <paramref name="buffer"/> (width * height bytes, cleared to 255 first), which is then texture-mapped onto the
/// transformed corners with 255 transparent. <paramref name="flags"/>: VfxShapeTransformXlat maps pixels through the lookaside
/// table as they go into the buffer; VfxShapeTransformReuse skips filling the buffer (it still holds this shape from a previous
/// call). With rot 0 and both scales 1.0 it is a plain VFX_shape_draw / VFX_shape_translate_draw.
/// </summary>
/// <returns>
/// The plain draw's result in the untransformed case, VfxErrBadWindow / VfxErrEmptyPane, else 0 (the asm
/// returned whatever EAX held).
/// </returns>
int32_t VfxShapeTransform(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                          int32_t hotY, std::shared_ptr<uint8_t[]> buffer, int32_t rot, int32_t xScale, int32_t yScale,
                          uint32_t flags);

/// <summary><c>VFX_shape_transform</c> flag: map pixels through the shape lookaside table.</summary>
inline constexpr uint32_t VfxShapeTransformXlat = 0x01;
/// <summary><c>VFX_shape_transform</c> flag: the work buffer already holds the shape; don't redraw it.</summary>
inline constexpr uint32_t VfxShapeTransformReuse = 0x02;

/// <summary>Maps every pixel of a shape through the lookaside table, in place.</summary>
int32_t VfxShapeRemapColors(std::span<uint8_t> shapeTable, int32_t shapeNum);

/// <summary>
/// VFX's shape encoder: encodes the pane's pixels as one shape (its 0x18-byte header and rows, no table header) into
/// <paramref name="buffer"/>, pixels of <paramref name="transparentColor"/> becoming skips, with its origin at
/// (hotX, hotY). With an empty buffer only the size is computed; bytes past the buffer's end aren't written.
/// </summary>
/// <returns>The number of bytes written.</returns>
/// <remarks>
/// Named Asm in the port: the
/// game's own C++ VFX_shape_scan (encode_vfx.cpp) has the same name and parameters.
/// </remarks>
int32_t VfxShapeScanAsm(const MCPane& pane, uint8_t transparentColor, int32_t hotX, int32_t hotY,
                        std::span<uint8_t> buffer);

/// <summary>A shape's bounds word: its width in the high 16 bits and height in the low ones.</summary>
int32_t VfxShapeBounds(std::span<const uint8_t> shapeTable, int32_t shapeNum);

/// <summary>A shape's size: <c>(x_max - x_min + 1) &lt;&lt; 16 | (y_max - y_min + 1)</c> (low word only).</summary>
int32_t VfxShapeResolution(std::span<const uint8_t> shapeTable, int32_t shapeNum);

/// <summary>A shape's top-left offset from its hot spot: <c>x_min &lt;&lt; 16 | (uint16_t)y_min</c>.</summary>
int32_t VfxShapeMinxy(std::span<const uint8_t> shapeTable, int32_t shapeNum);

/// <summary>The number of shapes in a table.</summary>
int32_t VfxShapeCount(std::span<const uint8_t> shapeTable);

/// <summary>
/// Port-only: calls <paramref name="pixel"/> with the offset from the hot spot and the colour of every pixel shape
/// <paramref name="shapeNum"/> of <paramref name="shapeTable"/> draws (VfxShapeDraw's pixels, unclipped; skipped ones
/// left out). Nothing for a table that isn't a shape table or a shape it hasn't; a row running past the table's end
/// stops the walk.
/// </summary>
void VfxShapeVisit(std::span<const uint8_t> shapeTable, int32_t shapeNum,
                   const std::function<void(int32_t x, int32_t y, uint8_t color)>& pixel);

/// <summary>Whether <paramref name="shapeTable"/> starts with the shape table version "1.10".</summary>
inline bool VfxIsShapeTable(std::span<const uint8_t> shapeTable)
{
    return shapeTable.size() >= 4 && std::memcmp(shapeTable.data(), "1.10", 4) == 0;
}

// ---------------------------------------------------------------------------------------------------------------
// vfxa.asm: fixed-point maths (vfx/MCVfxMath.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>The cosine and sine of <paramref name="angle"/> (tenths of a degree), as 16.16 fixed point.</summary>
void VfxCosSin(int32_t angle, MCFixed16& cosine, MCFixed16& sine);

/// <summary>The 16.16 product of <paramref name="m1"/> and <paramref name="m2"/>, also stored to <paramref name="result"/>.</summary>
MCFixed16 VfxFixedMul(MCFixed16 m1, MCFixed16 m2, MCFixed16& result);

/// <summary>
/// Rotates <paramref name="in"/> by <paramref name="rot"/> (tenths of a degree) about <paramref name="origin"/> and
/// scales it by x_scale/y_scale (16.16), into <paramref name="out"/>.
/// </summary>
void VfxPointTransform(const MCVfxPoint& in, MCVfxPoint& out, const MCVfxPoint& origin, int32_t rot, int32_t xScale,
                       int32_t yScale);

// ---------------------------------------------------------------------------------------------------------------
// vfxa.asm: fonts (vfx/MCVfxFont.cpp)
// ---------------------------------------------------------------------------------------------------------------

// A VFX font (the game's data\fonts\*.fnt) is a VFX_FONT header, then at +0x10 one dword per character: the offset
// of its glyph from the start of the font. A glyph is a dword width followed by width * char_height pixel bytes, row
// by row, top row first.

/// <summary>The height of a font's characters in pixels (the header's char_height).</summary>
int32_t VfxFontHeight(std::span<const uint8_t> font);

/// <summary>
/// Whether <paramref name="font"/>'s glyph table (its count, header word +4) lies inside the data, and each listed
/// glyph's width and pixels do: what the glyph routines rely on (characters past the table have none).
/// </summary>
/// <remarks>Issue #25: the fonts are data files; the original drew a negative width's glyph as a huge copy.</remarks>
bool VfxFontFits(std::span<const uint8_t> font);

/// <summary>The width of <paramref name="character"/> in pixels (its glyph's first dword).</summary>
int32_t VfxCharacterWidth(std::span<const uint8_t> font, int32_t character);

/// <summary>
/// Draws <paramref name="character"/> with its top-left corner at (x, y), clipped to the pane. Without a
/// <paramref name="colorTranslate"/> table every glyph byte is copied (the background too); with one, each byte is
/// mapped through the table and the pixels it maps to 255 are left alone (transparent).
/// </summary>
/// <returns>The character's width, whether or not any of it was drawn; VFX_ERR_* for a bad or empty pane.</returns>
int32_t VfxCharacterDraw(const MCPane& pane, int32_t x, int32_t y, std::span<const uint8_t> font, int32_t character,
                         std::optional<MCColorTable> colorTranslate);

/// <summary>
/// Draws a string from (x, y), each character advancing x by <c>VFX_character_draw</c>'s result (OB-124: an empty
/// string draws nothing).
/// </summary>
void VfxStringDraw(const MCPane& pane, int32_t x, int32_t y, std::span<const uint8_t> font, std::string_view string,
                   std::optional<MCColorTable> colorTranslate);

// ---------------------------------------------------------------------------------------------------------------
// vfxa.asm: image files, fades, colour scans (vfx/MCVfxImage.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// Writes the pixels of <paramref name="line"/> to pane row <paramref name="y"/> from the pane's left edge, clipped to
/// the pane (rows outside it are dropped).
/// </summary>
void VfxLineToPane(const MCPane& pane, int32_t y, std::span<const uint8_t> line);

/// <summary>
/// Draws an IFF picture into the pane from its top-left corner: FORM ILBM (8 bitplanes) or, for any other form
/// type, PBM (chunky); BODY uncompressed or ByteRun1. Pictures with a mask plane (masking 1) aren't drawn.
/// </summary>
/// <returns>The low byte of BMHD's transparent colour (0 for a masked picture, which the asm left undefined).</returns>
int32_t VfxIlbmDraw(const MCPane& pane, std::span<const uint8_t> ilbm);

/// <summary>An ILBM's size from BMHD: width in the high 16 bits, height in the low ones.</summary>
int32_t VfxIlbmResolution(std::span<const uint8_t> ilbm);

/// <summary>Draws an 8-bit, single-plane, RLE PCX picture into the pane from its top-left corner.</summary>
/// <returns>0.</returns>
int32_t VfxPcxDraw(const MCPane& pane, std::span<const uint8_t> pcx);

/// <summary>
/// Copies a PCX file's 256-colour palette (the last 768 bytes of <paramref name="pcx"/>, the whole file) into
/// <paramref name="palette"/>, scaled to 6 bits.
/// </summary>
void VfxPcxPalette(std::span<const uint8_t> pcx, std::span<MCVfxRgb, 256> palette);

/// <summary>A PCX picture's size: width in the high 16 bits, height in the low ones.</summary>
int32_t VfxPcxResolution(std::span<const uint8_t> pcx);

/// <summary>The size of <c>VFX_GIF_draw</c>'s LZW state (the game allocated a work buffer of 0x502e bytes).</summary>
inline constexpr int32_t VfxGifBufferSize = 0x502e;

/// <summary>
/// Draws the first image of a GIF into the pane from its top-left corner (the LZW state, which the game passed as a
/// work buffer, is the routine's own). Interlaced images are drawn in their pass order.
/// Extension blocks before the image aren't skipped (the game's GIFs have none).
/// </summary>
/// <returns>
/// The GIF's background colour index; or why the file can't be drawn (too short for its header, colour tables or
/// image descriptor, no pixels, or an LZW code size past 11). Image data that ends early draws what it holds.
/// </returns>
std::expected<int32_t, std::string> VfxGifDraw(const MCPane& pane, std::span<const uint8_t> gif);

/// <summary>
/// Copies a GIF's global colour table and then its first image's local colour table (each if present) into
/// <paramref name="palette"/>, scaled down to 6 bits.
/// </summary>
/// <returns>Nothing, or why the file isn't a GIF the game can draw (see <see cref="VfxGifDraw"/>).</returns>
std::expected<void, std::string> VfxGifPalette(std::span<const uint8_t> gif, std::span<MCVfxRgb, 256> palette);

/// <summary>
/// A GIF's first image's size: width in the high 16 bits, height in the low ones; or why the file isn't a GIF the
/// game can draw (see <see cref="VfxGifDraw"/>).
/// </summary>
std::expected<int32_t, std::string> VfxGifResolution(std::span<const uint8_t> gif);

/// <summary>
/// The display palette ("DAC") VFX_window_fade reads and writes, 6-bit entries. Port: the asm went through the
/// registered driver's DAC read/write entries (0x007a80ec/0x007a80f0); the port keeps the palette here and tells
/// the platform layer through <see cref="VfxDacWriteHook"/>.
/// </summary>
extern std::array<MCVfxRgb, 256> VfxDacPalette;
/// <summary>Called after each <see cref="VfxDacPalette"/> entry changes (empty: nothing is told).</summary>
extern std::function<void(int32_t index, const MCVfxRgb& rgb)> VfxDacWriteHook;
/// <summary>Called once per vertical retrace wait of a fade (the driver's entry at 0x007a80e0; empty: no wait).</summary>
extern std::function<void()> VfxWaitRetraceHook;

/// <summary>
/// Fades the display palette entries of the colours the window uses towards <paramref name="palette"/>, one DAC
/// step per channel at a time, over about <paramref name="intervals"/> vertical retraces.
/// </summary>
void VfxWindowFade(const MCWindow& window, std::span<const MCVfxRgb, 256> palette, int32_t intervals);

/// <summary>
/// Lists in <paramref name="colors"/> (when not empty) every distinct colour index in the pane, in the order first met
/// scanning rows top to bottom and each row right to left. The pane isn't clipped to its window.
/// </summary>
/// <returns>The number of distinct colours.</returns>
int32_t VfxColorScan(const MCPane& pane, std::span<uint32_t> colors);

// ---------------------------------------------------------------------------------------------------------------
// vfx3d.asm: polygons (vfx/MCVfxPolygon.cpp)
// ---------------------------------------------------------------------------------------------------------------

// The polygon routines take vertices relative to the pane's corner clipped to the window (max(0, x0), max(0, y0)),
// which is the pane's own corner for any pane inside its window. Polygons must be convex; rows are filled from the
// top vertex to the bottom one inclusive, each span from its left to its right edge inclusive (edges at x + 0.5
// rounded down).

/// <summary>Fills a convex polygon with the colour of its first vertex (<c>c</c> rounded, 16.16).</summary>
void VfxFlatPolygon(const MCPane& pane, std::span<const MCScreenVertex> vertices);

/// <summary>Fills a convex polygon interpolating the vertices' colours (<c>c</c>, 16.16).</summary>
void VfxGouraudPolygon(const MCPane& pane, std::span<const MCScreenVertex> vertices);

/// <summary>
/// A Gouraud polygon with <paramref name="ditherAmount"/> (16.16) added to the colour of every other pixel, in a
/// checkerboard, before it is rounded down.
/// </summary>
void VfxDitheredGouraudPolygon(const MCPane& pane, MCFixed16 ditherAmount, std::span<const MCScreenVertex> vertices);

/// <summary>Maps every pixel under a convex polygon through the 256-byte table <paramref name="lookaside"/>.</summary>
void VfxTranslatePolygon(const MCPane& pane, std::span<const MCScreenVertex> vertices, MCColorTable lookaside);

/// <summary>
/// Adds the interpolated, dithered vertex colours (as <c>VFX_dithered_Gouraud_polygon</c> computes them) to the pixels
/// under the polygon, as byte offsets into a shading palette.
/// </summary>
void VfxIlluminatePolygon(const MCPane& pane, MCFixed16 ditherAmount, std::span<const MCScreenVertex> vertices);

/// <summary>Sets the 256-byte table <c>VFX_map_polygon</c> maps texels through with VfxMapXlat.</summary>
void VfxMapLookaside(MCColorTable table);

/// <summary><c>VFX_map_polygon</c> flag: map texels through the map lookaside table.</summary>
inline constexpr uint32_t VfxMapXlat = 0x01;
/// <summary>
/// <c>VFX_map_polygon</c> flag: texels of colour 255 (after the lookaside, with VfxMapXlat) are transparent.
/// </summary>
inline constexpr uint32_t VfxMapTransparent = 0x02;

/// <summary>
/// Texture-maps <paramref name="texture"/> onto a convex polygon, affinely per the vertices' u, v (16.16 texel
/// coordinates). <paramref name="flags"/> is a combination of VfxMapXlat and VfxMapTransparent (the asm indexes a jump table with
/// it; only 0..3 exist).
/// </summary>
void VfxMapPolygon(const MCPane& pane, std::span<const MCScreenVertex> vertices, const MCWindow& texture,
                   uint32_t flags);

// ---------------------------------------------------------------------------------------------------------------
// vfx\alphapalette.cpp (vfx/MCAlphaPalette.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>The number of alpha "colours": the 256 palette indices plus 24 extra blend-only ones (256..279).</summary>
inline constexpr int32_t AlphaColorCount = 0x118;

/// <summary>
/// The blend tables built by <see cref="InitAlphaLookup"/>: row <c>c</c> (256 bytes, at <c>c * 256</c>) gives, for
/// each background index, the index to draw when colour <c>c</c> is blended over it. Rows of colours that aren't
/// special are plain (every entry <c>c</c>); rows 0 and 255 leave the background unchanged. The game's translucent
/// draws index it as <c>AlphaTable[(c &lt;&lt; 8) | background]</c>.
/// </summary>
/// <remarks>
/// 0x007f09d0 in MCX.EXE, 0x11800 bytes (mangled as <c>char*</c>, as MSVC mangles arrays). Row 0x108 is the darkening
/// table <c>AG_StatusBar</c> draws bar frames with (0x008011d0).
/// </remarks>
extern std::array<uint8_t, AlphaColorCount * 256> AlphaTable;
/// <summary>1 for every alpha colour AlphaPal.ini defines (a translucent colour), else 0. 0x008021d0 in MCX.EXE.</summary>
extern std::array<uint8_t, AlphaColorCount> SpecialColor;

/// <summary>An AlphaPal.ini entry: the colour's (8-bit) components, its weight A and the background's weight B2.</summary>
struct MCAlphaColor
{
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float Alpha = 0.0f;
    float BackgroundWeight = 0.0f;
};

/// <summary>
/// The entries <see cref="InitAlphaLookup"/> read (port: the original kept them on its stack), for a renderer that
/// blends the colours itself instead of looking the result up in <see cref="AlphaTable"/>. Zero where
/// <see cref="SpecialColor"/> is 0.
/// </summary>
extern std::array<MCAlphaColor, AlphaColorCount> MCAlphaColors;

/// <summary>Port-only: what an alpha colour draws over a pixel, premultiplied: <c>RGB + pixel * (1 - Coverage)</c>.</summary>
struct MCAlphaBlend
{
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float Coverage = 0.0f;
};

/// <summary>
/// Port-only: <paramref name="entry"/> (a special colour's) as a blend of full colour: <c>RGB * A</c> with
/// <c>1 - B2</c> coverage, or, for a colour dodge (A and B2 both 0), added light of its colour, with none.
/// </summary>
MCAlphaBlend AlphaColorBlend(const MCAlphaColor& entry);

/// <summary>
/// Builds <see cref="AlphaTable"/> and <see cref="SpecialColor"/> for <paramref name="palette"/> from
/// <c>AlphaPal.ini</c>: lines of <c>index R G B A B2</c>; a special colour blended over background (r, g, b) (8-bit)
/// gives <c>background * B2 + RGB * A</c>, or, with A and B2 both 0, <c>background * 255 / (255 - RGB)</c> (a
/// colour dodge). Backgrounds outside 10..245 (the reserved system colours) blend to 255.
/// </summary>
void InitAlphaLookup(std::span<const MCVfxRgb, 256> palette);

/// <summary>
/// The palette index 10..245 nearest to (r, g, b) (6-bit components), by distance weighted 39:51:10 (R:G:B); the first
/// exact match wins.
/// </summary>
uint8_t FindClosest(std::span<const MCVfxRgb, 256> palette, int r, int g, int b);

// ---------------------------------------------------------------------------------------------------------------
// vfx\encode_vfx.cpp (vfx/MCVfxShapeScan.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// The game's copy of VFX's shape encoder (the asm transcribed into inline assembly with its state in globals):
/// encodes the pane's pixels as one shape (header and rows, no table header) into <paramref name="buffer"/>,
/// <paramref name="transparentColor"/> becoming skips, with its origin at (hotX, hotY). Its output is byte for byte
/// that of <see cref="VfxShapeScanAsm"/>.
/// </summary>
/// <returns>The number of bytes written (or VfxErrBadWindow / VfxErrEmptyPane).</returns>
int VfxShapeScan(const MCPane& pane, uint8_t transparentColor, int hotX, int hotY, std::span<uint8_t> buffer);

// ---------------------------------------------------------------------------------------------------------------
// vfx\fastshp.cpp (vfx/MCFastShape.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>The tag at the start of a fast-shape table ("DNAH" read as a little-endian int).</summary>
inline constexpr int32_t FastShapeTag = 'D' | ('N' << 8) | ('A' << 16) | ('H' << 24);

/// <summary>
/// Draws shape <paramref name="shapeNum"/> of a "fast shape" table (the terrain overlay tiles, tables tagged "DNAH";
/// not the VFX format), its hot spot at (hotX, hotY), each colour mapped through
/// <paramref name="xlat"/> when there is one. Run colour 255 is transparent; a shape whose first row begins with the
/// word 1 is drawn translucent through <see cref="AlphaTable"/>.
/// </summary>
/// <returns>0.</returns>
/// <remarks>The original took a last argument it never read (the game passed 0).</remarks>
int32_t FastShapeDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                      int32_t hotY, std::optional<MCColorTable> xlat);

/// <summary>
/// Port-only: the bytes a fast-shape row's packets take to cover <paramref name="width"/> pixels, or none when they run
/// past <paramref name="row"/>.
/// </summary>
std::optional<size_t> FastShapeRowBytes(std::span<const uint8_t> row, int32_t width);

/// <summary>
/// Port-only: the bytes fast shape <paramref name="shape"/> (its header) takes: its header and row offsets, then its
/// rows, to the end of the row that starts last (a terrain overlay's data, as the HD art pack names it); none when a
/// row runs past <paramref name="shape"/>.
/// </summary>
std::optional<size_t> FastShapeSize(std::span<const uint8_t> shape);

/// <summary>
/// Port-only: the pixel shape <paramref name="shapeNum"/> of fast-shape table <paramref name="shapeTable"/> covers
/// (<paramref name="x"/>, <paramref name="y"/>) right and down of its hot spot with (where <see cref="FastShapeDraw"/>
/// puts it drawn at that hot spot); none where a run of colour 255 leaves it see-through, outside its box, for a
/// translucent shape (its pixels blend with what lies under them), for a table not tagged <see cref="FastShapeTag"/>,
/// or where its table or that row lie outside the data (only that row is read).
/// </summary>
std::optional<uint8_t> FastShapePixel(std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t x, int32_t y);

// ---------------------------------------------------------------------------------------------------------------
// vfx\vfx_ellipse.cpp (vfx/MCAgEllipse.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// The game's ellipse outline (midpoint algorithm) centred at (xc, yc) with radii <paramref name="width"/> and
/// <paramref name="height"/>, clipped to the pane. A special (translucent) <paramref name="color"/> blends through
/// <see cref="AlphaTable"/>. A zero radius draws the line from (xc - width, yc - height) to (xc + width, yc + height).
/// </summary>
/// <remarks>
/// Unlike VFX, the centre is offset by the pane's
/// origin clipped to the window.
/// </remarks>
void AGEllipseDraw(const MCPane& pane, int32_t xc, int32_t yc, int32_t width, int32_t height, int32_t color);

/// <summary>The game's filled ellipse, as <see cref="AGEllipseDraw"/> with horizontal spans.</summary>
void AGEllipseFill(const MCPane& pane, int32_t xc, int32_t yc, int32_t width, int32_t height, int32_t color);

// ---------------------------------------------------------------------------------------------------------------
// vfx\vfx_map_polygon.cpp (vfx/MCAgStatusBar.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// Draws a translucent status bar in the box (x0, y0)-(x1, y1), in window coordinates (the pane only clips): the
/// top and bottom rows and the left and right columns are darkened through AlphaTable row 0x108, and the first
/// <paramref name="barLength"/> + 1 pixels inside each other row are blended with alpha colour
/// <paramref name="alphaColor"/>. Port-only: <paramref name="measured"/> is the length the game measured before it
/// rounded it down to <paramref name="barLength"/>, which a renderer drawing at the output's resolution shows.
/// </summary>
void AGStatusBar(const MCPane& pane, int x0, int y0, int x1, int y1, int alphaColor, int barLength,
                 std::optional<float> measured = std::nullopt);

/// <summary>
/// Writes a pixel (the low byte of <paramref name="color"/>) at pane coordinates (x, y), only strictly inside the
/// pane's rectangle (its border rows and columns are never written; the window isn't checked).
/// </summary>
void AGPixelWrite(const MCPane& pane, int32_t x, int32_t y, uint32_t color);

/// <summary>
/// Copies the top-left <paramref name="width"/> x <paramref name="height"/> pixels of <paramref name="texture"/> to
/// (x, y) in the pane, skipping texels of colour 255, clipped to the pane.
/// </summary>
/// <returns>0 when anything was drawn, 1 when it was wholly clipped.</returns>
int32_t DrawTransparent(const MCPane& pane, const MCWindow& texture, int x, int y, int width, int height);

// ---------------------------------------------------------------------------------------------------------------
// vfx\vfx_transform.cpp (vfx/MCAgShapeTransform.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// Draws a shape blended onto the pane through <see cref="AlphaTable"/>, at full size or half size (every other
/// pixel of every other row), mirrored left to right when <paramref name="mirror"/> is nonzero: the shape is first
/// rendered opaque into a scratch picture by <see cref="AGShapeFill"/> (skips as colour 0), then blended by
/// <see cref="CopySprite"/>. (hotX, hotY) are relative to the pane's corner clipped to the window.
/// </summary>
/// <remarks>
/// The original rendered into a buffer its callers passed (0x1fa40 bytes: shapes of 360 x 360 pixels or more weren't
/// drawn); the port's renderers keep their own.
/// </remarks>
void AGShapeTransform(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                      int32_t hotY, int32_t mirror, int32_t fullSize);

/// <summary>
/// As <see cref="AGShapeTransform"/>, rendering the shape through <paramref name="lookaside"/> first (the table the
/// original's AG_shape_lookaside set beforehand).
/// </summary>
void AGShapeTranslateTransform(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                               int32_t hotY, int32_t mirror, int32_t fullSize, MCColorTable lookaside);

/// <summary>
/// Blends a <paramref name="width"/> x <paramref name="height"/> 8-bit <paramref name="sprite"/> onto the pane at
/// (x, y) relative to the pane's corner clipped to the window: each pixel becomes
/// <c>AlphaTable[sprite &lt;&lt; 8 | screen]</c>. <paramref name="fullSize"/> 0 halves it (every other pixel of every
/// other row); <paramref name="mirror"/> nonzero mirrors it left to right. Clipped to the pane.
/// </summary>
void CopySprite(const MCPane& pane, std::span<const uint8_t> sprite, int x, int y, int width, int height, int mirror,
                int fullSize);

/// <summary>
/// Port: <see cref="CopySprite"/> of a window's pixels, which the
/// renderers read through its texture (the sprite is the whole window, <paramref name="width"/> its width).
/// </summary>
void CopySprite(const MCPane& pane, const MCWindow& sprite, int x, int y, int width, int height, int mirror,
                int fullSize);

// ---------------------------------------------------------------------------------------------------------------
// vfx\vfx_translatedraw.cpp (vfx/MCAgShapeDraw.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>
/// The game's shape draw: <c>VFX_shape_draw</c>'s format and pixels, but with the hot spot at window coordinates
/// (hotX, hotY) (the pane only clips) and translucent shapes: a shape whose data begins with the token pair 03 00 is
/// blended, each pixel becoming <c>AlphaTable[shape &lt;&lt; 8 | screen]</c>, one column to the right.
/// </summary>
void AGShapeDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX, int32_t hotY);

/// <summary>
/// As <see cref="AGShapeDraw"/>, each pixel mapped through <paramref name="lookaside"/> (translucent shapes blended
/// first, then mapped). The original read the table AG_shape_lookaside set beforehand.
/// </summary>
void AGShapeTranslateDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                          int32_t hotY, MCColorTable lookaside);

// ---------------------------------------------------------------------------------------------------------------
// vfx\vfxtile.cpp (vfx/MCVfxTile.cpp)
// ---------------------------------------------------------------------------------------------------------------

/// <summary>How <see cref="VfxNTileDraw"/> writes a tile's pixels (the original's <c>xlat</c>: null, -1 or a table).</summary>
struct MCTileXlat
{
    /// <summary>The table each pixel is mapped through (a haze palette); none copies the pixels.</summary>
    std::optional<MCColorTable> Table;
    /// <summary>Fills the tile with colour 0x10 instead (the original's -1).</summary>
    bool Fill = false;
};

/// <summary>
/// Draws a terrain tile (format in vfx/MCVfxTile.cpp) with its hot spot at (x, y) relative to the pane's corner
/// clipped to the window, clipped to the pane, its pixels written as <paramref name="xlat"/> says.
/// </summary>
/// <returns>
/// 0, 0xcdcf0001 when the tile is wholly outside the pane, or (the port's) 0xcdcf0002 when its data fails
/// <see cref="VfxTileFits"/> and nothing is drawn.
/// </returns>
int32_t VfxNTileDraw(const MCPane& pane, std::span<const uint8_t> tile, int32_t x, int32_t y, const MCTileXlat& xlat);

/// <summary>
/// Whether <paramref name="tile"/>'s row table lies in its data, its rows run forward through the data, and each
/// row's span stays inside the tile's width: what every tile reader relies on to stay inside the tile and its rows.
/// </summary>
/// <remarks>Issue #25: tiles come from the tile set's files; the original drew whatever the table said.</remarks>
bool VfxTileFits(std::span<const uint8_t> tile);

/// <summary>
/// Port: the columns a tile's spans reach (0 for a tile without pixels), one more than its stated width for the retail
/// tiles that reach past their box (see <see cref="VfxTileFits"/>, which the tile must pass).
/// </summary>
int32_t VfxTileWidth(std::span<const uint8_t> tile);

/// <summary>
/// Port: the pixel of <paramref name="tile"/> at (<paramref name="column"/>, <paramref name="row"/>) of its box (the
/// pixel VfxNTileDraw writes that far right and down from its top-left corner, its hot spot taken off); none where its
/// span leaves a hole, or where that row's table entries or span lie outside the data (only that row is read).
/// </summary>
std::optional<uint8_t> VfxTilePixel(std::span<const uint8_t> tile, int32_t column, int32_t row);
