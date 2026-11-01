#pragma once

#include "sprite/MCBc7.h"

class MCShape;

/// <summary>
/// Port-only: the HD art pack, higher-resolution copies of sprite frames that a player generates on their own machine
/// from their own install (the pack is never shipped). When it is on (PREFS "HDArt", <see cref="GHdArt"/>) and the
/// pack has a frame, the GPU renderer draws the frame from the pack at the output's resolution; the palette indices
/// (what tests, the fog test and mirror mode read) stay the original's.
/// </summary>
/// <remarks>
/// <para>A pack is a folder (by default <c>HD</c> in the user folder). A mech part's frames are
/// <c>mechs/&lt;part PAK&gt;/&lt;mech&gt;/&lt;packet&gt;/&lt;frame&gt;.hdf</c> (<see cref="EncodeHdf"/>: BC7 colours and
/// the original's indices), or, as the generator first writes them, <c>&lt;frame&gt;.png</c>: the frame's bounds at an
/// integer scale, RGBA with straight alpha, with <c>&lt;frame&gt;_idx.png</c> beside it giving each pixel the
/// original's palette index (red) and whether it has one (green). The indices let a paint scheme's fade table recolour
/// the HD pixels it would have recoloured. A frame that holds a translucent shape's blended pixels too (an effect's,
/// <see cref="Frame::Blended"/>) has premultiplied colours instead, and its index picture's blue is 255.</para>
/// <para>Pictures (the menus' and logistics' art) are <c>menus/&lt;hash&gt;.hdf</c> (or a PNG pair) likewise, named by
/// their pixels' hash (<see cref="PictureHash"/>).</para>
/// <para>Every other appearance's shapes (vehicles, Elementals, buildings, trees, terrain objects...) come from its own
/// PAK, a packet of the shape PAK (<c>shapes90.pak</c>, or <c>shapes.pak</c> for an appearance that one leaves
/// empty); their frames are <c>objects/&lt;shape PAK&gt;/&lt;appearance&gt;/&lt;packet&gt;/&lt;frame&gt;</c>, the
/// appearance being its packet in the shape PAK (its type id's low 24 bits). Only the appearances the pack has folders
/// for are drawn from it. A translucent shape's frame (one drawn through AlphaPal.ini) leaves out the pixels the shape
/// blends, which the GPU still blends from the original, except an effect's (explosions, fire, smoke...), which holds
/// them as the colour and coverage they draw.</para>
/// <para>A mech bar icon's damage shapes (a frame a body part) are <c>icons/&lt;shape file&gt;/&lt;frame&gt;</c>.</para>
/// <para>A crater's or footprint's frames (MCCraterManager's shapes, a frame a facing) are
/// <c>craters/&lt;crater PAK&gt;/&lt;packet&gt;/&lt;frame&gt;</c>, holding their blended pixels as an effect's do.</para>
/// <para>A terrain tile's colours are <c>terrain/&lt;key&gt;.hdf</c> (<see cref="TerrainTileName"/>), drawn inside the
/// original tile's pixels; their alpha is unused. A terrain overlay's (a fast shape over the ground: roads, rivers'
/// banks, rubble, mines) are <c>overlays/&lt;key&gt;.hdf</c> (named as tiles are), with their alpha; a translucent
/// one's hold its blended pixels.</para>
/// <para>A terrain tile's relief map is <c>relief/&lt;key&gt;.hdf</c> (named as its tile), BC5 at its own scale: each
/// pixel's slope, its height's rise a pixel right (R) and a pixel down the picture (G), 128 flat and 255 a rise of 127
/// / 128 of a pixel (<see cref="ReliefFlat"/>).</para>
/// <para>A movie's HD frames are <c>movies/&lt;NAME&gt;.ivf</c> (the Smacker file's name, upper-case): AV1, frame for
/// frame the original's, which keeps playing for its sound, timing and palette (<see cref="FindMovie"/>).</para>
/// <para>The pack's <c>palette.rgb</c> (<see cref="MadeWith"/>) holds the colours its pictures were made from. The GPU
/// shows an HD pixel scaled by its original index's colour now over that colour, so another mission's palette, the
/// water's colour cycle and the brightness setting show through.</para>
/// <para>Frames are read when first drawn and kept while there is room (<see cref="CacheBudget"/>).</para>
/// </remarks>
namespace MCHdArt
{
    /// <summary>How an HD frame keeps its colours.</summary>
    enum class ColorFormat : uint8_t
    {
        /// <summary>RGBA8 rows, <c>4 * Width</c> bytes each.</summary>
        Rgba8,
        /// <summary>BC7 blocks (sprite/MCBc7.h) for the size padded to whole blocks, block row by block row.</summary>
        Bc7,
        /// <summary>
        /// BC5 blocks (two channels, a BC4 block each), laid out as Bc7's: a ground tile's relief map
        /// (<see cref="FindTerrainRelief"/>).
        /// </summary>
        Bc5,
    };

    /// <summary>An HD frame as the GPU takes it.</summary>
    struct Frame
    {
        /// <summary>A number no other frame read in this run has (the GPU keeps its copy by it).</summary>
        uint64_t Id = 0;
        /// <summary>The colours' width and height (HD pixels).</summary>
        int32_t Width = 0;
        int32_t Height = 0;
        /// <summary>HD pixels per original pixel.</summary>
        int32_t Scale = 1;
        ColorFormat Format = ColorFormat::Rgba8;
        /// <summary>The premultiplied colours, as <see cref="Format"/> says.</summary>
        std::vector<uint8_t> Colors;
        /// <summary>
        /// Per original pixel (<c>Width / Scale</c> x <c>Height / Scale</c>), R8G8: its palette index, and whether it
        /// has one (255).
        /// </summary>
        std::vector<uint8_t> Indices;
        /// <summary>
        /// Whether the colours include the pixels a translucent shape blends (an effect's), as AlphaPal.ini's colours draw
        /// them: a pixel may add light (colour without coverage), and the original's draw blends none of them.
        /// </summary>
        bool Blended = false;

        /// <summary>The original's width and height.</summary>
        int32_t OriginalWidth() const { return Width / std::max(Scale, 1); }
        int32_t OriginalHeight() const { return Height / std::max(Scale, 1); }

        /// <summary>The bytes it keeps.</summary>
        size_t Bytes() const { return Colors.size() + Indices.size(); }
    };

    /// <summary>The bytes an index picture's pixel takes (R8G8), and the green of a pixel with an index.</summary>
    inline constexpr size_t IndexBytes = 2;
    inline constexpr uint8_t HasIndex = 255;
    /// <summary>
    /// Port-only: an index picture's green for a pixel with an index that is part of an object's baked shadow
    /// (<see cref="MarkBakedShadows"/>); otherwise it is as HasIndex's.
    /// </summary>
    inline constexpr uint8_t BakedShadow = 254;
    /// <summary>The palette index the original's object art draws its baked shadows in (black).</summary>
    inline constexpr uint8_t BakedShadowIndex = 0x10;
    /// <summary>An index picture's blue in a frame that holds its blended pixels (<see cref="Frame::Blended"/>).</summary>
    inline constexpr uint8_t HoldsBlended = 255;

    /// <summary>
    /// A frame from a PNG pair as the generator writes them: <paramref name="colorPath"/> (straight RGBA, made
    /// premultiplied; already premultiplied in a frame that holds its blended pixels) and <paramref name="indexPath"/>
    /// (the same size; each original pixel's index read from its block's top-left pixel, and whether the frame holds its
    /// blended pixels from the top-left pixel's blue) at <paramref name="scale"/> HD pixels per original pixel.
    /// </summary>
    std::expected<Frame, std::string> FrameFromPngs(const std::filesystem::path& colorPath,
                                                    const std::filesystem::path& indexPath, int32_t scale);

    /// <summary>
    /// A frame as a <c>.hdf</c> file: <c>MCHD</c>, version (u16), colour format (u8, 1 = BC7, 2 = BC7 holding a
    /// translucent shape's blended pixels too, which older builds leave to the original), scale (u8), the
    /// original's width and height (u16 each), the colours' bytes (u32), the colours (BC7, encoded here when the frame
    /// holds RGBA8, as <paramref name="options"/> say), then the indices (R8G8 at the original's size). Little-endian.
    /// </summary>
    std::vector<uint8_t> EncodeHdf(const Frame& frame, const MCBc7::MCBc7Options& options = {});

    /// <summary>
    /// The frame a <c>.hdf</c> file's <paramref name="bytes"/> hold, checked against the original's
    /// <paramref name="width"/> x <paramref name="height"/> (0 x 0: any), or what is wrong with it.
    /// </summary>
    std::expected<Frame, std::string> DecodeHdf(std::span<const uint8_t> bytes, int32_t width, int32_t height);

    /// <summary>
    /// Port-only: marks <paramref name="frame"/>'s baked shadow in its index picture (<see cref="BakedShadow"/>). The
    /// original's object art (trees, buildings, rocks, turrets) casts its shadow down and right in solid
    /// <see cref="BakedShadowIndex"/>; the same black also draws detail inside an object (a tank's hull). Its shadow is
    /// that black outside the rest of the art's outline (closed over one-pixel gaps, holes filled), in groups that touch
    /// at least as many empty pixels as drawn ones. A frame that holds its blended pixels (an effect's) has none.
    /// </summary>
    void MarkBakedShadows(Frame& frame);

    /// <summary>Where a mech part's shape comes from.</summary>
    struct MechShapeKey
    {
        /// <summary>The part PAK's name ("legs90"...).</summary>
        std::string PartFile;
        /// <summary>The mech's packet in it, and the shape's packet in the mech's.</summary>
        uint32_t MechNum = 0;
        uint32_t Packet = 0;
    };

    /// <summary>Where a shape of any other appearance comes from (MCSpriteManager::GetShapeData).</summary>
    struct ObjectShapeKey
    {
        /// <summary>The shape PAK's name, lower case ("shapes90"...).</summary>
        std::string ShapeFile;
        /// <summary>The appearance's packet in it, and the shape's packet in the appearance's.</summary>
        uint32_t Appearance = 0;
        uint32_t Packet = 0;
    };

    /// <summary>Where a crater's or footprint's shape comes from (MCCraterManager).</summary>
    struct CraterShapeKey
    {
        /// <summary>The crater PAK's name, lower case ("feet", "crater"), and the shape's packet in it.</summary>
        std::string CraterFile;
        uint32_t Packet = 0;
    };

    /// <summary>The folder in a pack that holds the frames of the shape loaded from <paramref name="key"/>.</summary>
    std::filesystem::path FolderOf(const MechShapeKey& key);
    std::filesystem::path FolderOf(const ObjectShapeKey& key);
    std::filesystem::path FolderOf(const CraterShapeKey& key);

    /// <summary>Where a mech bar icon's damage shapes come from (data\\art's <c>mi##.shp</c> or <c>vi#.shp</c>).</summary>
    struct IconShapeKey
    {
        /// <summary>The shape file's name, lower case, without its extension ("mi01").</summary>
        std::string File;
    };

    std::filesystem::path FolderOf(const IconShapeKey& key);

    /// <summary>The bytes of frames kept in memory before the least recently used go.</summary>
    inline constexpr size_t CacheBudget = 512u * 1024u * 1024u;

    /// <summary>
    /// The pack's folder: the one set, else the user folder's <c>HD</c> when there is one, else the game folder's (a
    /// pack unzipped into the game's folder, as mods are).
    /// </summary>
    std::filesystem::path PackFolder();

    /// <summary>Sets the pack's folder (empty: the default); forgets the shapes and frames found so far.</summary>
    void SetPackFolder(const std::filesystem::path& folder);

    /// <summary>
    /// Whether HD art is drawn: <see cref="GHdArt"/>, and a pack folder that exists whose <c>pack.cfg</c> (if it has one)
    /// is of a format this code reads.
    /// </summary>
    bool Active();

    /// <summary>
    /// Whether the pack's mech and object folders, named by the game's sprite files' numbers, are drawn: <see
    /// cref="Active"/>, and the pack was made from this game's sprite files (<c>pack.cfg</c>'s Sprites, when it says).
    /// A pack made from other files would put another mech's frames in a mech's place; its ground and menu pictures,
    /// named by their pixels, are still drawn.
    /// </summary>
    bool NumberedFolders();

    /// <summary>
    /// Sets this game's sprite files (their names and sizes; the sprite manager, when it opens them), which
    /// <see cref="SpriteFiles"/> gives as a pack records them: <c>NAME=size;...</c>, names upper-case.
    /// </summary>
    void SetSpriteFiles(std::span<const std::pair<std::string, uint32_t>> files);
    const std::string& SpriteFiles();

    /// <summary>Writes <c>pack.cfg</c> into a pack's folder: this code's pack format and this game's sprite files.</summary>
    std::expected<void, std::string> WritePackFile(const std::filesystem::path& folder);

    /// <summary>
    /// Notes that <paramref name="shape"/> was loaded from <paramref name="key"/>: while it lives, its frames are drawn
    /// from the pack's when it has them. Does nothing while HD art is off.
    /// </summary>
    void Attach(const std::shared_ptr<MCShape>& shape, const MechShapeKey& key);
    void Attach(const std::shared_ptr<MCShape>& shape, const ObjectShapeKey& key);

    /// <summary>
    /// Notes that <paramref name="shape"/>'s frames are in its <c>HdFolder</c> (none: nothing), as <see cref="Attach"/>
    /// with its key does. Does nothing while HD art is off.
    /// </summary>
    void AttachFolder(const std::shared_ptr<MCShape>& shape);

    /// <summary>
    /// Notes that shape table <paramref name="table"/> was loaded from <paramref name="key"/>: while
    /// <paramref name="owner"/> lives, its frames are drawn from the pack's when it has them. Does nothing while HD art
    /// is off.
    /// </summary>
    void Attach(std::span<const uint8_t> table, const std::shared_ptr<const void>& owner, const CraterShapeKey& key);
    void Attach(std::span<const uint8_t> table, const std::shared_ptr<const void>& owner, const IconShapeKey& key);

    /// <summary>
    /// The HD copy of frame <paramref name="frame"/> of the shape table starting at <paramref name="table"/> (whose
    /// bounds are <paramref name="width"/> x <paramref name="height"/>), read from the pack the first time; null when
    /// HD art is off or the pack has no such frame, or its frame isn't a whole multiple of those bounds. A
    /// <paramref name="step"/> asks for the frame's in-between of that step toward the next instead (MCInBetween,
    /// <c>&lt;frame&gt;_&lt;step&gt;</c>), whose bounds are the two frames' together.
    /// </summary>
    std::shared_ptr<const Frame> Find(const uint8_t* table, int32_t frame, int32_t width, int32_t height,
                                      int32_t step = 0);

    /// <summary>
    /// Whether the frames of the shape table starting at <paramref name="table"/> are drawn with their own alpha's edges
    /// (the mech bar's damage diagrams: the interface's pixel art), not the original's pixels' smoothed (the renderer's
    /// default for a frame that doesn't hold its blended pixels).
    /// </summary>
    bool KeepsFrameEdges(const uint8_t* table);

    /// <summary>
    /// Whether the shape table starting at <paramref name="table"/> is tied to the pack (and its owner lives): its frames
    /// are looked for there.
    /// </summary>
    bool Attached(const uint8_t* table);

    /// <summary>
    /// For the tools: the pack folder the frames of the shape table starting at <paramref name="table"/> are read from;
    /// none when it isn't attached (or its owner is gone).
    /// </summary>
    std::optional<std::filesystem::path> AttachedFolder(const uint8_t* table);

    /// <summary>
    /// For the tools: frame <paramref name="frame"/> (its original bounds <paramref name="width"/> x
    /// <paramref name="height"/>) of the pack's folder <paramref name="folder"/> (relative to the pack, its frames named
    /// by number, as an attached shape's), read the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindInFolder(const std::filesystem::path& folder, int32_t frame, int32_t width,
                                              int32_t height);

    /// <summary>
    /// The name an HD art pack gives a picture (the menus' and logistics' art): a hash of its
    /// <paramref name="width"/> x <paramref name="height"/> palette indices, the same whichever file or packet they came
    /// from. A picture the game has drawn into has another, so it is shown as it is.
    /// </summary>
    uint64_t PictureHash(std::span<const uint8_t> pixels, int32_t width, int32_t height);

    /// <summary>The file name (without its extension) of the picture <paramref name="hash"/> names: 16 hex digits.</summary>
    std::string PictureName(uint64_t hash);

    /// <summary>
    /// The HD copy of the picture with <paramref name="pixels"/> (<c>menus/&lt;hash&gt;</c> in the pack, kept as a
    /// frame's), read the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindPicture(std::span<const uint8_t> pixels, int32_t width, int32_t height);
    /// The name the pack keeps terrain tile <paramref name="tile"/> under (its data as the ground mesh keeps it: up to
    /// its last row's end): the data's 64-bit FNV-1a hash in hex, so the same tile in any tile set has one name.
    /// </summary>
    std::string TerrainTileName(std::span<const uint8_t> tile);

    /// <summary>
    /// The HD copy of terrain tile <paramref name="tile"/> (its bounds <paramref name="width"/> x
    /// <paramref name="height"/>), read from the pack the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindTerrainTile(std::span<const uint8_t> tile, int32_t width, int32_t height);

    /// <summary>A relief map's byte for flat ground; a byte stands for a rise of (byte - this) / 127 of a pixel.</summary>
    inline constexpr int32_t ReliefFlat = 128;

    /// <summary>
    /// Terrain tile <paramref name="tile"/>'s relief map (its bounds <paramref name="width"/> x
    /// <paramref name="height"/>), read from the pack the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindTerrainRelief(std::span<const uint8_t> tile, int32_t width, int32_t height);

    /// <summary>
    /// The HD copy of terrain overlay <paramref name="shape"/> (a fast shape's bytes, <see cref="FastShapeSize"/>; its
    /// bounds <paramref name="width"/> x <paramref name="height"/>), read from the pack the first time; null when HD art
    /// is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindOverlay(std::span<const uint8_t> shape, int32_t width, int32_t height);

    /// <summary>
    /// The name the pack keeps a shape table loaded outside the sprite, crater and icon files under (a tree's shadow, a
    /// forest's burnt edges, a sensor blip, smoke...): the hash of its bytes (a whole <c>.shp</c> file, or a packet of a
    /// PAK, as its loader reads it), 64-bit FNV-1a.
    /// </summary>
    uint64_t ShapeTableHash(std::span<const uint8_t> table);

    /// <summary>The pack folder the frames of the shape table whose bytes hash to <paramref name="hash"/> are in.</summary>
    std::filesystem::path ShapeTableFolder(uint64_t hash);

    /// <summary>
    /// The HD copy of frame <paramref name="frame"/> of the shape table whose bytes hash to <paramref name="hash"/>
    /// (<see cref="ShapeTableHash"/>; the frame's bounds <paramref name="width"/> x <paramref name="height"/>), read from
    /// the pack the first time; null when HD art is off or the pack has none. A <paramref name="step"/> asks for an
    /// in-between, as <see cref="Find"/>'s does.
    /// </summary>
    std::shared_ptr<const Frame> FindShape(uint64_t hash, int32_t frame, int32_t width, int32_t height,
                                           int32_t step = 0);

    /// <summary>
    /// Sets the shape tables (the mechs' shadows: flat translucent shapes) whose edges are drawn smooth at the output's
    /// resolution while HD art is on, from the 1x shape's coverage; none needs a pack.
    /// </summary>
    void SetSmoothEdgeTables(std::span<const uint8_t* const> tables);

    /// <summary>Whether HD art is on and <paramref name="table"/> is one whose edges are drawn smooth.</summary>
    bool SmoothEdges(const uint8_t* table);

    /// <summary>A frame of a shape table (the table's first byte).</summary>
    struct LineArtFrame
    {
        const uint8_t* Table = nullptr;
        int32_t Frame = 0;
    };

    /// <summary>
    /// Sets the shape frames that are line art (one-pixel outlines: the purchase screen's sold-out mechs and vehicles),
    /// redrawn as smooth strokes (platform/MCSharpLines) on a screen drawn at the size it's shown while HD art is on;
    /// none needs a pack.
    /// </summary>
    void SetLineArtFrames(std::span<const LineArtFrame> frames);

    /// <summary>Whether HD art is on and frame <paramref name="frame"/> of <paramref name="table"/> is line art.</summary>
    bool LineArt(const uint8_t* table, int32_t frame);

    /// <summary>The bytes of a palette file: 256 colours, red, green and blue.</summary>
    inline constexpr size_t PaletteBytes = 256 * 3;

    /// <summary>The colours the pack's pictures were made from (its palette.rgb), or none.</summary>
    const std::optional<std::array<uint8_t, PaletteBytes>>& MadeWith();

    /// <summary>A number that changes whenever the pack in use (or whether one is) may have: see SetPackFolder.</summary>
    uint64_t PackVersion();

    /// <summary>
    /// A number for <see cref="Frame::Id"/> that no other frame read in this run has (a movie's frames take one too).
    /// </summary>
    uint64_t NewFrameId();

    /// <summary>
    /// The pack's HD frames for the Smacker movie <paramref name="movie"/> (its game path or name; folders, extension and
    /// case don't matter), when HD art is on and the pack has them.
    /// </summary>
    std::optional<std::filesystem::path> FindMovie(std::string_view movie);

    /// <summary>
    /// How many frames have been read from the pack so far, the time reading them took, and how many bytes the kept
    /// ones take.
    /// </summary>
    size_t FramesRead();
    std::chrono::nanoseconds ReadTime();
    size_t CachedBytes();
}

/// <summary>Port-only: draw the HD art pack's frames where it has them (PREFS "HDArt"; off by default).</summary>
extern int GHdArt;
