#pragma once

// The port's renderer interface. The game's VFX/AG routines (vfx/*.cpp) are its front end: they keep their
// signatures, clip each draw to its pane within the window, and hand the renderer commands that are already
// resolved. The renderer walks shape, tile, glyph and polygon data and writes palette
// indices. MCSoftwareRenderer is the original pixel code; a hardware renderer implements the same commands.
//
// Targets: a WINDOW is a picture in memory (window->buffer), as in the original, or a view (window->View, see
// vfx/MCVfxView.h): a UI element's place on another window, with an origin and a scissor, drawn into only while the
// element draws in the frame pass. The world view has a surface of its own. Nothing is locked, read back or re-uploaded
// each frame.
//
// Coordinates in commands are window coordinates, rectangles inclusive. A command lives for its one call: it borrows
// what it reads (spans, references). Tables are spans into registered data blocks (MCDataKind::Tables): a renderer
// keeps what it took from a table by its address until the block's owner says the bytes changed.

#include "lib/MCOptionalRef.h"
#include "platform/MCAddressKey.h"
#include "vfx/MCVfx.h"

/// <summary>How a texture's pixels change (<see cref="MCRenderer::CreateTexture"/>).</summary>
enum class MCTextureUse : uint8_t
{
    /// <summary>Loaded once (art); changed only through <see cref="MCRenderer::LockTexture"/>.</summary>
    Static,
    /// <summary>Drawn into on the CPU as well (a port the game paints); every draw into it marks it changed.</summary>
    Dynamic,
    /// <summary>
    /// A new picture most frames (a movie), written through <see cref="MCRenderer::LockTexture"/>. While the GPU draws,
    /// a lock hands out the hardware renderer's upload memory, so the pixels go straight to the GPU and
    /// <c>Pixels</c> no longer holds them (<c>CpuStale</c>).
    /// </summary>
    Stream
};

/// <summary>What a registered data block holds (<see cref="MCRenderer::RegisterData"/>).</summary>
enum class MCDataKind : uint8_t
{
    /// <summary>Encoded images: shape tables, fast shapes, fonts, terrain tiles.</summary>
    Shapes,
    /// <summary>256-byte colour tables (fades, haze, remaps, translate tables), at any alignment.</summary>
    Tables
};

/// <summary>
/// A block of memory registered as holding data draws read (<see cref="MCRenderer::RegisterData"/>). A renderer that
/// keeps something made from the data (a GPU atlas image) keys it by the address read; the block's owner says when the
/// bytes change or go, so nothing ever compares or hashes them.
/// </summary>
struct MCDataBlock
{
    /// <summary>The block's first byte and one past its last.</summary>
    MCAddressKey Begin;
    MCAddressKey End;
    MCDataKind Kind = MCDataKind::Shapes;

    /// <summary>How many of the block's bytes lie from <paramref name="at"/> on (0 when it lies outside).</summary>
    size_t BytesFrom(MCAddressKey at) const { return at >= Begin && at < End ? static_cast<size_t>(End - at) : 0; }
};

/// <summary>
/// The lowest address a 256-byte table reaching <paramref name="at"/> may start at (255 bytes before it, or the bottom
/// of the address space): what a change at <paramref name="at"/> may reach of the tables kept by address.
/// </summary>
inline MCAddressKey MCFirstTableReaching(MCAddressKey at)
{
    return at.Before(255);
}

/// <summary>What a hardware renderer keeps for a texture: its own type, derived from this.</summary>
struct MCTextureHardware
{
    virtual ~MCTextureHardware() = default;
};

/// <summary>
/// A texture: a picture's pixels as the renderers hold them. The code that owns the pixels makes it
/// (<see cref="MCRenderer::CreateTexture"/>), hands it to the window over them (<c>_window::Texture</c>) and
/// destroys it before the pixels go. The software renderer reads <c>Pixels</c>; the hardware renderer keeps its own
/// copy (<c>Hardware</c>) and uploads <c>Pixels</c> again only when they changed (<c>Dirty</c>). Nothing ever compares
/// or hashes the pixels to find out.
/// </summary>
struct MCTexture
{
    /// <summary>The pixels (owned by the caller), <c>Width</c> bytes a row.</summary>
    std::span<uint8_t> Pixels;
    int32_t Width = 0;
    int32_t Height = 0;
    MCTextureUse Use = MCTextureUse::Static;
    /// <summary>Whether <c>Pixels</c> changed since the hardware renderer last took them.</summary>
    bool Dirty = true;
    /// <summary>
    /// Whether a stream lock wrote into the hardware renderer's memory instead of <c>Pixels</c>, which then lack those
    /// pixels: the software renderer must not read or draw into them (<see cref="MCRenderer::StaleCpuReads"/>).
    /// </summary>
    bool CpuStale = false;
    /// <summary>The rectangle locked (<see cref="MCRenderer::LockTexture"/>), and whether the lock went to the
    /// hardware renderer.</summary>
    MCRect Locked{0, 0, -1, -1};
    bool LockedOnHardware = false;
    /// <summary>What the hardware renderer keeps for it, or null.</summary>
    std::unique_ptr<MCTextureHardware> Hardware;
    /// <summary>Its place in the renderer's list of textures.</summary>
    size_t Slot = 0;
};

/// <summary>What a run-length shape draw does with each pixel it writes.</summary>
enum class MCShapeOp
{
    /// <summary>Runs and literals are written as stored.</summary>
    Draw,
    /// <summary>Each pixel becomes <c>AlphaTable[shape &lt;&lt; 8 | screen]</c>.</summary>
    Alpha,
    /// <summary>Each pixel is mapped through the table.</summary>
    Xlat,
    /// <summary>Blended as Alpha, then mapped through the table.</summary>
    XlatAlpha,
    /// <summary>As Draw, and skipped pixels are written as colour 0.</summary>
    Fill,
    /// <summary>As Xlat, and skipped pixels are written as colour 0.</summary>
    XlatFill
};

/// <summary>
/// A run-length shape (VFX's format: 0x18-byte header, then rows of tokens) with its clipping resolved: rows
/// <c>SkipRows</c>.. of the shape are drawn from window row <c>Top</c>, each starting at window column <c>Left</c>,
/// writing only columns <c>Lo</c>..<c>Hi</c>.
/// </summary>
struct MCShapeCommand
{
    /// <summary>The shape table (from its first byte to the end of its registered block) and the shape's number in it.</summary>
    std::span<const uint8_t> ShapeTable;
    int32_t ShapeNum;
    /// <summary>The encoded rows stepped over before the first drawn, and how many are drawn.</summary>
    int32_t SkipRows;
    int32_t Rows;
    /// <summary>The window row of the first drawn row, and the window column every row starts at.</summary>
    int32_t Top;
    int32_t Left;
    /// <summary>The columns written, inclusive.</summary>
    int32_t Lo;
    int32_t Hi;
    MCShapeOp Op;
    /// <summary>The table of the Xlat ops (none otherwise).</summary>
    std::optional<MCColorTable> Table;
    /// <summary>
    /// Port-only: the rows the pane lets it write, inclusive (none when the bottom is above the top), so a smooth edge
    /// drawn at the output's resolution may reach a little past the shape's rows without leaving the pane.
    /// </summary>
    int32_t ClipTop = 0;
    int32_t ClipBottom = -1;
};

/// <summary>
/// A "fast shape" (fastShapeDraw's DNAH format, the terrain overlay tiles) with its clipping resolved: shape rows
/// <c>FirstRow</c>..<c>EndRow</c> - 1 from window row <c>Top</c>, each starting at column <c>StartX</c>, or, when
/// <c>LeftSkip</c> is set, with that many pixels stepped over and the rest drawn from <c>ClipX0</c>; <c>Limit</c>
/// pixels of each row are covered.
/// </summary>
struct MCFastShapeCommand
{
    /// <summary>The shape (its header, at the offset the table gives), to the end of its registered block.</summary>
    std::span<const uint8_t> Shape;
    int32_t Top;
    int32_t FirstRow;
    int32_t EndRow;
    int32_t StartX;
    int32_t ClipX0;
    int32_t LeftSkip;
    int32_t Limit;
    /// <summary>Whether the shape is translucent (its first row starts with the word 1).</summary>
    bool Alpha;
    /// <summary>The colour table, or none.</summary>
    std::optional<MCColorTable> Table;
    /// <summary>
    /// Port-only: the pane's last column and its rows, inclusive (none when the last is before the first), so a smooth
    /// edge drawn at the output's resolution may reach a little past the shape without leaving the pane.
    /// </summary>
    int32_t ClipX1 = -1;
    int32_t ClipY0 = 0;
    int32_t ClipY1 = -1;
};

/// <summary>A terrain tile (VFX_nTile_draw) with its clipping resolved.</summary>
struct MCTileCommand
{
    /// <summary>The tile's data (format in vfx/vfxtile.cpp), to the end of its registered block.</summary>
    std::span<const uint8_t> Tile;
    /// <summary>The window column of the tile's bounding box, and the window row of the first row drawn.</summary>
    int32_t Left;
    int32_t Top;
    /// <summary>The first tile row drawn and one past the last.</summary>
    int32_t FirstRow;
    int32_t EndRow;
    /// <summary>The columns written, inclusive; ignored when <c>Unclipped</c>.</summary>
    int32_t Lo;
    int32_t Hi;
    /// <summary>Whether the tile lies wholly inside horizontally (its spans are written without clipping).</summary>
    bool Unclipped;
    /// <summary>Whether every pixel is written as colour 0x10 (VfxTileFill, the haze factor's "all black").</summary>
    bool Fill = false;
    /// <summary>Unless <c>Fill</c>: none, the pixels are copied; else they are mapped through the table.</summary>
    std::optional<MCColorTable> Table;
};

/// <summary>Which vfx3d filler a <see cref="MCPolygonCommand"/> reproduces.</summary>
enum class MCPolygonKind
{
    Flat,
    Gouraud,
    DitheredGouraud,
    Translate,
    Illuminate,
    Map
};

/// <summary>
/// A convex polygon (vfx/MCVfxPolygon.cpp). Its vertices are relative to the clipped pane's corner (<c>OriginX</c>,
/// <c>OriginY</c>), and it is clipped to 0..<c>XMax</c>, 0..<c>YMax</c> from there.
/// </summary>
struct MCPolygonCommand
{
    MCPolygonKind Kind;
    int32_t OriginX;
    int32_t OriginY;
    int32_t XMax;
    int32_t YMax;
    std::span<const MCScreenVertex> Vertices;
    /// <summary>The dithered kinds' amount (16.16).</summary>
    int32_t DitherAmount;
    /// <summary>Translate: the destination table. Map: the lookaside table VfxMapXlat maps texels through.</summary>
    std::optional<MCColorTable> Table;
    /// <summary>Map: the texture and the MP_* flags.</summary>
    MCOptionalRef<const MCWindow> Texture;
    uint32_t MapFlags;
};

/// <summary>One corner of a <see cref="MCMapQuadCommand"/>: window position and texel.</summary>
struct MCMapQuadVertex
{
    int32_t X;
    int32_t Y;
    int32_t U;
    int32_t V;
};

/// <summary>
/// VFX_shape_transform's textured quadrilateral: the corners clockwise from the shape's top left, clipped to
/// <c>Clip</c>; texels of colour 255 are transparent.
/// </summary>
struct MCMapQuadCommand
{
    MCMapQuadVertex Corners[4];
    MCRect Clip;
    std::reference_wrapper<const MCWindow> Texture;
};

/// <summary>
/// A line with VFX_line_draw's clipping resolved into a walk: <c>Count</c> pixels from (X, Y). After each pixel the
/// 0.32 fraction gains <c>Slope</c>; on a carry the position also moves by (MinorX, MinorY); then it moves by
/// (MajorX, MajorY).
/// </summary>
struct MCLineCommand
{
    int32_t X;
    int32_t Y;
    int32_t Count;
    int32_t MajorX;
    int32_t MajorY;
    int32_t MinorX;
    int32_t MinorY;
    uint32_t Slope;
    uint32_t Fraction;
    /// <summary>None: every pixel becomes <c>Color</c>; else each is mapped through the table (VfxLineTranslate).</summary>
    std::optional<MCColorTable> Table;
    uint8_t Color;
};

/// <summary>
/// An ellipse (VFX's midpoint walk, which the game's routines share) centred at window (<c>CenterX</c>,
/// <c>CenterY</c>) with radii <c>Width</c> and <c>Height</c> (both non-zero), each point or span clipped to
/// <c>Clip</c>. Points on the axes are plotted twice and filled rows filled again at every step that stays on them,
/// which matters when the colour blends.
/// </summary>
struct MCEllipseCommand
{
    int32_t CenterX;
    int32_t CenterY;
    int32_t Width;
    int32_t Height;
    MCRect Clip;
    /// <summary>Whether the ellipse is filled (two spans per step) or outlined (four points per step).</summary>
    bool Fill;
    uint8_t Color;
    /// <summary>Whether the colour blends through its AlphaTable row (the game's routines, for special colours).</summary>
    bool Alpha;
};

/// <summary>
/// A copy of <c>SourceRect</c> of <c>Source</c> to (X, Y) of the target. Rows are copied downwards or upwards and
/// columns rightwards or leftwards as the routine chose, so an overlapping copy within one window comes out as it did.
/// </summary>
struct MCCopyCommand
{
    std::reference_wrapper<const MCWindow> Source;
    MCRect SourceRect;
    int32_t X;
    int32_t Y;
    /// <summary>Whether source pixels of colour <c>Key</c> are left out.</summary>
    bool ColorKey;
    uint8_t Key;
    bool Downwards;
    bool Rightwards;
};

/// <summary>AG_StatusBar with its clamping done.</summary>
struct MCStatusBarCommand
{
    /// <summary>The clamped box.</summary>
    MCRect Box;
    /// <summary>The unclamped box's top and bottom rows: the frame rows (which may have been clipped away).</summary>
    int32_t FrameTop;
    int32_t FrameBottom;
    /// <summary>The bar's length inside each row (0: none), and its AlphaTable row.</summary>
    int32_t BarLength;
    int32_t AlphaColor;
    /// <summary>
    /// Port-only: the bar's length as the game measured it, <c>BarLength</c> to <c>BarLength</c> + 1 (the bar then ends
    /// that far into its last pixel), which a renderer drawing at the output's resolution shows.
    /// </summary>
    float FillLength = 0.0f;
};

/// <summary>
/// A map's ground as a hardware renderer keeps it: the tile of every cell (the quad whose top-left corner is that map
/// vertex) and the elevations of its corners, over the map and a ring of the off-map cells around it. Made once per map
/// (see terrain.cpp); a renderer uploads it again only when <c>Version</c> changes.
/// </summary>
struct MCTerrainMesh
{
    /// <summary>Changes whenever the contents do (a renderer compares it with the one it holds).</summary>
    uint64_t Version = 0;
    /// <summary>The map vertex (row, column) of the first cell, and how many cells a row and how many rows.</summary>
    int32_t FirstRow = 0;
    int32_t FirstCol = 0;
    int32_t Cols = 0;
    int32_t Rows = 0;
    /// <summary>Per cell, row by row: its tile (an index into <c>Tiles</c>), or <c>NoTile</c>.</summary>
    std::vector<uint32_t> CellTiles;
    /// <summary>Per cell: its corners' elevation levels, a byte each (top left, top right, bottom right, bottom left).</summary>
    std::vector<uint32_t> CellElevations;
    /// <summary>The tiles' data (vfx/vfxtile.cpp's format), copied.</summary>
    std::vector<std::vector<uint8_t>> Tiles;

    static constexpr uint32_t NoTile = 0xffffffffu;
};

/// <summary>
/// One frame's terrain pass (TerrainWindow::render's tiles), drawn from a <see cref="MCTerrainMesh"/>: the numbers that
/// place it, the cells of the terrain window's grid, the clipping and the haze. Points are in pane pixels, as the
/// terrain's vertices are projected.
/// </summary>
struct MCTerrainFrame
{
    MCOptionalRef<const MCTerrainMesh> Mesh;
    /// <summary>The screen point of map vertex (0, 0): vertex (row, col) at elevation e lies at OriginX + (col - row) *
    /// StepX, OriginY + (row + col) * StepY - e * ElevStep.</summary>
    int32_t OriginX = 0;
    int32_t OriginY = 0;
    int32_t StepX = 0;
    int32_t StepY = 0;
    int32_t ElevStep = 0;
    /// <summary>The cells drawn: their top-left vertices' rows and columns, inclusive.</summary>
    int32_t FirstRow = 0;
    int32_t FirstCol = 0;
    int32_t LastRow = -1;
    int32_t LastCol = -1;
    /// <summary>A cell is left out when all its corners lie outside these (Vertex::clipped).</summary>
    int32_t MinX = 0;
    int32_t MaxX = 0;
    int32_t MinY = 0;
    int32_t MaxY = 0;
    /// <summary>The pane's corner in the window, and the window pixels a tile may write (VFX_nTile_draw's clip).</summary>
    int32_t PaneX = 0;
    int32_t PaneY = 0;
    MCRect Clip{0, 0, -1, -1};
    /// <summary>The fog of war's flags (a ByteFlag's window, a byte per map vertex), as the home side sees.</summary>
    MCOptionalRef<const MCWindow> Fog;
    /// <summary>Every tile filled with colour 0x10 (the haze factor's "all black").</summary>
    bool AllFilled = false;
    /// <summary>The haze tables for one, two and three corners seen.</summary>
    std::array<std::optional<MCColorTable>, 3> Haze{};
};

/// <summary>
/// A font character (VFX_character_draw) with its clipping resolved: <c>Columns</c> x <c>Rows</c> of the glyph from
/// (SourceX, SourceY), drawn at window (X, Y).
/// </summary>
struct MCGlyphCommand
{
    /// <summary>The font, from its header to the end of its registered block.</summary>
    std::span<const uint8_t> Font;
    int32_t Character;
    int32_t X;
    int32_t Y;
    int32_t SourceX;
    int32_t SourceY;
    int32_t Columns;
    int32_t Rows;
    /// <summary>None: every byte copied; else mapped through the table, 255 transparent.</summary>
    std::optional<MCColorTable> Table;
};

/// <summary>
/// CopySprite with its clipping resolved: a bitmap blended onto the target through AlphaTable, at full size or
/// halved (every other pixel of every other row), optionally mirrored.
/// </summary>
struct MCAlphaBlitCommand
{
    /// <summary>The bitmap's texture (its pixels are <c>Sprite</c>, <c>Pitch</c> wide), or none when it has none.</summary>
    MCOptionalRef<MCTexture> Texture;
    /// <summary>
    /// The bitmap's pixels, and where in them the first pixel read lies (it may be before the start when mirrored).
    /// </summary>
    std::span<const uint8_t> Sprite;
    intptr_t Offset;
    int32_t Pitch;
    /// <summary>The window position of the first pixel written.</summary>
    int32_t Left;
    int32_t Top;
    /// <summary>The source columns and rows covered (before halving).</summary>
    int32_t Columns;
    int32_t Rows;
    bool Mirror;
    bool FullSize;
};

/// <summary>
/// A shape transform (AG_shape_transform, AG_shape_translate_transform): shape <c>ShapeNum</c> filled into a
/// picture of its bounds (<c>Width</c> x <c>Height</c>: skipped pixels and the rest colour 0, drawn pixels mapped
/// through <c>Table</c> when there is one), then blended onto the target as <c>Blit</c> says (its <c>Sprite</c> is
/// that picture, which the renderer makes).
/// </summary>
struct MCShapeBlitCommand
{
    /// <summary>The shape table, from its first byte to the end of its registered block.</summary>
    std::span<const uint8_t> ShapeTable;
    int32_t ShapeNum;
    /// <summary>The lookaside table of the translating transform, else none.</summary>
    std::optional<MCColorTable> Table;
    int32_t Width;
    int32_t Height;
    MCAlphaBlitCommand Blit;
};

/// <summary>
/// A picture shown under a window's key colour (<see cref="MCRenderer::UnderlayKey"/>): <c>Source</c> scaled into
/// <c>Rect</c> of the window over <c>TargetPixels</c>, each target pixel showing the source pixel under its centre
/// (nearest). The world view is the screen's underlay: the camera draws the world into a surface of its own at 1x, the
/// display's composite shader scales it into the view, and the UI drawn on the screen lets it show through where it
/// left the key.
/// </summary>
/// <remarks>
/// A draw that maps the pixels under it (a translucent shape, a status bar, a translate polygon) can't map the world
/// from the screen. Where it meets the key, it records its table in the target's op plane instead: the pixel stays the
/// key, and its op names a table (<see cref="MCRenderer::OpTables"/>) the composite maps the world pixel through. A
/// second such draw on the pixel records the two tables composed. The world under it comes out as the original's
/// palette arithmetic made it.
/// </remarks>
struct MCUnderlay
{
    /// <summary>Who set it (a view window), to replace or remove it by.</summary>
    MCAddressKey Owner;
    /// <summary>The pixels of the window it lies under (the screen's).</summary>
    MCAddressKey TargetPixels;
    /// <summary>Where it is shown, in target coordinates (may reach past the target).</summary>
    MCRect Rect;
    /// <summary>The picture, shown whole (a frame surface its owner keeps; none once it is gone).</summary>
    std::weak_ptr<const MCWindow> Source;
};

/// <summary>Who draws the frame surfaces (<see cref="MCRenderer::AddFrameSurface"/>).</summary>
enum class MCGpuDrawing
{
    /// <summary>The software renderer, into the windows' memory (the presenter uploads them each frame).</summary>
    Off,
    /// <summary>The hardware renderer, on the GPU.</summary>
    On,
    /// <summary>Both, the hardware renderer's surfaces compared with the software's each frame (development).</summary>
    Mirror
};

/// <summary>The drawing a name stands for ("off", "on", "mirror", any case); empty for another name.</summary>
std::optional<MCGpuDrawing> MCGpuDrawingFromName(std::string_view name);

/// <summary>
/// The renderer. Every call is made from the game's thread. The vfx front end asks <see cref="For"/> which renderer
/// draws into a window, and issues its commands there.
/// </summary>
class MCRenderer
{
public:
    virtual ~MCRenderer() = default;

    /// <summary>
    /// The renderer that draws into <paramref name="window"/>: for a frame surface, the hardware renderer when there is
    /// one and it draws (or both, mirrored); for any other picture, the software renderer; for a view, one that moves
    /// each command by the view's origin and hands it to its target's renderer (valid until the next call).
    /// </summary>
    static MCRenderer& For(const MCWindow& window);

    // Textures ----------------------------------------------------------------------------------------------------

    /// <summary>
    /// Makes a texture over <paramref name="pixels"/> (<paramref name="width"/> bytes a row). The renderer's list holds
    /// it until <see cref="DestroyTexture"/>; what is returned is for the caller to keep a <c>weak_ptr</c> of.
    /// </summary>
    static std::shared_ptr<MCTexture> CreateTexture(std::span<uint8_t> pixels, int32_t width, int32_t height,
                                                    MCTextureUse use);

    /// <summary>Makes a texture over <paramref name="window"/>'s pixels and hands it to the window.</summary>
    static std::shared_ptr<MCTexture> CreateTexture(MCWindow& window, MCTextureUse use);

    /// <summary>Points <paramref name="texture"/> at new pixels or a new size (they count as changed).</summary>
    static void ResizeTexture(MCTexture& texture, std::span<uint8_t> pixels, int32_t width, int32_t height);

    /// <summary>Makes <paramref name="window"/>'s texture follow its pixels and size again.</summary>
    static void ResizeTexture(MCWindow& window);

    /// <summary>Destroys <paramref name="texture"/>: the list lets go of it (a holder's <c>weak_ptr</c> expires).</summary>
    static void DestroyTexture(MCTexture& texture);

    /// <summary>Destroys the texture <paramref name="texture"/> refers to, if any, and empties the reference.</summary>
    static void DestroyTexture(std::weak_ptr<MCTexture>& texture);

    /// <summary>Destroys <paramref name="window"/>'s texture, if it has one.</summary>
    static void DestroyTexture(MCWindow& window);

    /// <summary>Where to write a texture's new pixels (its <c>Pixels</c>); <see cref="UnlockTexture"/> when done.</summary>
    static std::span<uint8_t> LockTexture(MCTexture& texture);

    /// <summary>
    /// Where to write new pixels for <paramref name="rect"/> of <paramref name="texture"/>: from its top left pixel,
    /// rows <c>Width</c> bytes apart (whole rows are writable). Every pixel of the rectangle must be written before
    /// <see cref="UnlockTexture"/>: for a <see cref="MCTextureUse::Stream"/> texture while the GPU draws, the memory is
    /// the hardware renderer's upload memory, whose old contents are undefined.
    /// </summary>
    static std::span<uint8_t> LockTexture(MCTexture& texture, const MCRect& rect);

    /// <summary>The pixels written since <see cref="LockTexture"/> are complete: renderers take them at the next use.</summary>
    static void UnlockTexture(MCTexture& texture);

    /// <summary>Every texture made and not yet destroyed.</summary>
    static std::span<const std::shared_ptr<MCTexture>> Textures();

    // Data blocks -------------------------------------------------------------------------------------------------

    /// <summary>
    /// Registers <paramref name="bytes"/> as holding <paramref name="kind"/> data (where its owner loads it). A block
    /// they overlap is taken to be gone (the memory was reused). An <c>MCBlockStore</c> block, once freed, is
    /// unregistered by the store; other memory must be unregistered by its owner before it goes.
    /// </summary>
    static void RegisterData(std::span<const uint8_t> bytes, MCDataKind kind);

    /// <summary><paramref name="bytes"/> of a registered block changed: renderers drop what they made of them.</summary>
    static void DataChanged(std::span<const uint8_t> bytes);

    /// <summary>The <paramref name="size"/> bytes from <paramref name="begin"/> changed (or went).</summary>
    static void DataChanged(MCAddressKey begin, size_t size);

    /// <summary>
    /// Unregisters every block that starts in <paramref name="bytes"/> (renderers drop what they made of them); a span
    /// of one byte unregisters the block starting there.
    /// </summary>
    static void UnregisterData(std::span<const uint8_t> bytes);

    /// <summary>The registered block holding <paramref name="at"/>, or none.</summary>
    static std::optional<MCDataBlock> DataBlockOf(MCAddressKey at);

    /// <summary>How many blocks are registered.</summary>
    static size_t DataBlockCount();

    /// <summary>
    /// Notes a draw that read a resource nobody registered (a window without a texture, shape data outside every
    /// registered block): a bug in the caller, asserted in Debug, logged once per <paramref name="what"/> and counted.
    /// </summary>
    static void NoteUnregistered(std::string_view what);

    /// <summary>How many draws <see cref="NoteUnregistered"/> counted.</summary>
    static int64_t UnregisteredDraws();

    /// <summary>
    /// While one is alive, <see cref="NoteUnregistered"/> logs and counts but doesn't assert: for tests that draw
    /// unregistered data on purpose to check that it is refused. Scopes nest.
    /// </summary>
    class ExpectUnregistered
    {
    public:
        /// <summary>Starts expecting unregistered draws.</summary>
        ExpectUnregistered();

        /// <summary>Stops expecting them (once the outermost scope ends).</summary>
        ~ExpectUnregistered();

        ExpectUnregistered(const ExpectUnregistered&) = delete;
        ExpectUnregistered& operator=(const ExpectUnregistered&) = delete;
    };

    // Frame surfaces ----------------------------------------------------------------------------------------------

    /// <summary>
    /// Makes <paramref name="window"/> a frame surface: a picture drawn every frame (the screen, the world view's
    /// surface), which a hardware renderer keeps on the GPU. Windows over the same pixels count as the same surface.
    /// A <paramref name="kept"/> surface is one the game also reads (the fog of war's flags): while the GPU draws, the
    /// software renderer draws its memory too, and the GPU's copy starts from the pixels it has when added. The
    /// renderers refer to the window until <see cref="RemoveFrameSurface"/> (or until its owner lets it go).
    /// </summary>
    static void AddFrameSurface(std::shared_ptr<MCWindow> window, bool kept = false);

    /// <summary>Whether <paramref name="window"/> is (or lies over) a kept frame surface.</summary>
    static bool KeptSurface(const MCWindow& window);

    /// <summary>Makes <paramref name="window"/> an ordinary picture again (before its pixels go).</summary>
    static void RemoveFrameSurface(const MCWindow& window);

    /// <summary>The frame surface <paramref name="window"/> is (or lies over the pixels of), or null.</summary>
    static std::shared_ptr<MCWindow> FrameSurfaceOf(const MCWindow& window);

    /// <summary>The frame surface whose pixels lie at <paramref name="pixels"/>, or null.</summary>
    static std::shared_ptr<MCWindow> FrameSurfaceOver(MCAddressKey pixels);

    /// <summary>
    /// Sets the hardware renderer (the Vulkan presenter's, or null) and who draws the frame surfaces; the drawing is
    /// <see cref="MCGpuDrawing::Off"/> without one. The renderers refer to it until it is set again.
    /// </summary>
    static void SetHardware(std::shared_ptr<MCRenderer> hardware, MCGpuDrawing drawing);

    /// <summary>The hardware renderer, or null.</summary>
    static std::shared_ptr<MCRenderer> Hardware();

    /// <summary>Who draws the frame surfaces now.</summary>
    static MCGpuDrawing GpuDrawing();

    /// <summary>
    /// Who should draw the frame surfaces when a hardware renderer starts: what <see cref="RequestGpuDrawing"/> set
    /// (<c>-gpudraw off|on|mirror</c>, tests); on by default.
    /// </summary>
    static MCGpuDrawing RequestedGpuDrawing();

    /// <summary>Sets what <see cref="RequestedGpuDrawing"/> returns (before the display is made).</summary>
    static void RequestGpuDrawing(MCGpuDrawing drawing);

    /// <summary>Where mirror mode saves the first frame that differs (<c>-gpudump</c>); empty saves nothing.</summary>
    static const std::filesystem::path& MirrorDumpFolder();

    /// <summary>Sets <see cref="MirrorDumpFolder"/>.</summary>
    static void SetMirrorDumpFolder(const std::filesystem::path& folder);

    /// <summary>
    /// Called where the software renderer reads <paramref name="source"/>'s pixels (for <paramref name="command"/>).
    /// While the GPU draws the frame surfaces alone, a frame surface's memory is never drawn, so such a read sees stale
    /// pixels: it is logged (once per command) and counted.
    /// </summary>
    static void NoteCpuRead(const MCWindow& source, std::string_view command);

    /// <summary>
    /// How many stale CPU reads <see cref="NoteCpuRead"/> found, plus software draws into a stream texture whose pixels
    /// went to the GPU alone (<c>MCTexture::CpuStale</c>).
    /// </summary>
    static int64_t StaleCpuReads();

    // Commands --------------------------------------------------------------------------------------------------

    /// <summary>Fills <paramref name="rect"/> with <paramref name="color"/>.</summary>
    virtual void Clear(MCWindow& target, const MCRect& rect, uint8_t color) = 0;

    /// <summary>
    /// VFX_rectangle_hash's pattern over <paramref name="rect"/>: every other pixel, rows an even distance above its
    /// bottom row starting at its left column, the others one pixel in.
    /// </summary>
    virtual void Hash(MCWindow& target, const MCRect& rect, uint8_t color) = 0;

    /// <summary>Copies a rectangle of one window into another (or the same).</summary>
    virtual void Copy(MCWindow& target, const MCCopyCommand& command) = 0;

    /// <summary>Blends a bitmap onto the target (CopySprite).</summary>
    virtual void AlphaBlit(MCWindow& target, const MCAlphaBlitCommand& command) = 0;

    /// <summary>Fills a shape into a picture and blends that onto the target (a shape transform).</summary>
    virtual void ShapeBlit(MCWindow& target, const MCShapeBlitCommand& command) = 0;

    /// <summary>Writes <paramref name="pixels"/> as they are from (x, y) (the image decoders' rows).</summary>
    virtual void Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels) = 0;

    /// <summary>One pixel.</summary>
    virtual void Pixel(MCWindow& target, int32_t x, int32_t y, uint8_t color) = 0;

    /// <summary>A run-length shape.</summary>
    virtual void Shape(MCWindow& target, const MCShapeCommand& command) = 0;

    /// <summary>A fast shape.</summary>
    virtual void FastShape(MCWindow& target, const MCFastShapeCommand& command) = 0;

    /// <summary>A terrain tile.</summary>
    virtual void Tile(MCWindow& target, const MCTileCommand& command) = 0;

    /// <summary>A convex polygon.</summary>
    virtual void Polygon(MCWindow& target, const MCPolygonCommand& command) = 0;

    /// <summary>A textured quadrilateral (a rotated or scaled shape).</summary>
    virtual void MapQuad(MCWindow& target, const MCMapQuadCommand& command) = 0;

    /// <summary>A line.</summary>
    virtual void Line(MCWindow& target, const MCLineCommand& command) = 0;

    /// <summary>An ellipse outline or fill.</summary>
    virtual void Ellipse(MCWindow& target, const MCEllipseCommand& command) = 0;

    /// <summary>A status bar.</summary>
    virtual void StatusBar(MCWindow& target, const MCStatusBarCommand& command) = 0;

    /// <summary>A font character.</summary>
    virtual void Glyph(MCWindow& target, const MCGlyphCommand& command) = 0;

    /// <summary>
    /// Draws a frame's terrain tiles from the map's mesh. A hardware renderer always does (the terrain pass asks it
    /// whenever it draws the world, and a failure is an error, never a quiet return to tile commands); the software
    /// renderer draws none. Until <see cref="EndTerrainLayer"/>, the tiles of the pass (VFX_nTile_draw) still come as
    /// commands, for the software renderer (mirror mode, or alone); one that drew the layer leaves them.
    /// </summary>
    virtual std::expected<void, std::string> TerrainLayer(MCWindow& target, const MCTerrainFrame& frame);

    /// <summary>Ends the terrain pass <see cref="TerrainLayer"/> began.</summary>
    virtual void EndTerrainLayer(MCWindow& target);

    // Shared state ----------------------------------------------------------------------------------------------

    /// <summary>Tells every renderer the alpha table changed (InitAlphaLookup).</summary>
    static void AlphaTableChanged();

    // Underlays -------------------------------------------------------------------------------------------------

    /// <summary>The colour that shows a window's underlays.</summary>
    static constexpr uint8_t UnderlayKey = 0xff;

    /// <summary>Sets the underlay of <c>underlay.Owner</c> (replacing the one it set before).</summary>
    static void SetUnderlay(const MCUnderlay& underlay);

    /// <summary>Drops the underlay <paramref name="owner"/> set, if any.</summary>
    static void RemoveUnderlay(MCAddressKey owner);

    /// <summary>The underlays, in the order they were first set (later ones are shown over earlier ones).</summary>
    static std::span<const MCUnderlay> Underlays();

    /// <summary>
    /// Gives <paramref name="target"/> an op plane: one byte per pixel, laid out as its pixels, which its owner keeps
    /// (null removes it). Clearing the target clears its ops too. Set it again whenever the target's pixels move.
    /// </summary>
    static void SetOpPlane(const MCWindow& target, std::shared_ptr<std::vector<uint8_t>> ops);

    /// <summary>The op plane of <paramref name="target"/>, or an empty span.</summary>
    static std::span<uint8_t> OpPlane(const MCWindow& target);

    /// <summary>The op tables: 256 rows of 256 bytes, row 0 the identity; <see cref="OpTableCount"/> rows in use.</summary>
    static std::span<const uint8_t> OpTables();

    /// <summary>How many rows of <see cref="OpTables"/> are in use (at least 1).</summary>
    static int32_t OpTableCount();

    /// <summary>
    /// The op of a registered 256-byte table: the frame's row for that address, or a new one holding its bytes; 0 when
    /// every row is taken (the caller then maps the key itself).
    /// </summary>
    static uint8_t OpFor(MCColorTable table);

    /// <summary>The op that maps through <paramref name="first"/>'s table, then <paramref name="second"/>'s (0 when
    /// both are 0, or every row is taken).</summary>
    static uint8_t ComposeOps(uint8_t first, uint8_t second);

    /// <summary>Starts a frame's op tables (once a frame, before anything is drawn, after the last present).</summary>
    static void ResetOpTables();

    /// <summary>
    /// The picture as shown, in palette indices: in <paramref name="pixels"/>, a copy of <paramref name="target"/>'s
    /// pixels with its layout, each key pixel of <paramref name="rect"/> over an underlay becomes the underlay's pixel,
    /// mapped through its op (the composite shader's rule, for screenshots, tests and a display without shaders).
    /// </summary>
    static void ComposeUnderlays(const MCWindow& target, std::span<uint8_t> pixels, const MCRect& rect);

private:
    /// <summary>Logs <paramref name="what"/> (once) and counts it in <see cref="StaleCpuReads"/>.</summary>
    static void NoteStale(std::string_view what);

protected:
    /// <summary>The alpha table changed: drop anything built from it.</summary>
    virtual void OnAlphaTableChanged() = 0;

    /// <summary>Registered data from <paramref name="begin"/> on changed or is going: drop anything made from it.</summary>
    virtual void OnDataChanged(MCAddressKey /*begin*/, size_t /*size*/) {}

    /// <summary>A frame surface became an ordinary picture (its pixels are going): drop what was kept for it.</summary>
    virtual void OnFrameSurfaceRemoved(const MCWindow&) {}

    /// <summary>A texture is going (or its size changed): drop what is kept in its <c>Hardware</c>.</summary>
    virtual void OnTextureReleased(MCTexture&) {}

    /// <summary>
    /// A stream texture's lock while the GPU draws alone: memory to write <c>Locked</c> into (rows <c>Width</c> bytes
    /// apart, uploaded at <see cref="OnUnlockStream"/>), or an empty span to have the pixels written into <c>Pixels</c>.
    /// </summary>
    virtual std::span<uint8_t> OnLockStream(MCTexture&) { return {}; }

    /// <summary>
    /// A stream texture's new pixels for <c>Locked</c> are complete: written into the memory
    /// <see cref="OnLockStream"/> gave (<paramref name="pixels"/> empty), or in <paramref name="pixels"/> (from its top
    /// left pixel in <c>Pixels</c>; mirror mode), to be copied up the same way.
    /// </summary>
    virtual void OnUnlockStream(MCTexture&, std::span<const uint8_t> /*pixels*/) {}
};
