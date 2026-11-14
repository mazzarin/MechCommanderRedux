#pragma once

#include "platform/MCAddressKey.h"
#include "platform/MCSharpLines.h"
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
/// <see cref="Frame::Blended"/>) has premultiplied colours instead, and its index picture's blue is 255. A legs
/// frame's turn middles (HDTurns: halfway to a neighbouring facing) lie beside it as <c>&lt;frame&gt;_turn</c> and
/// <c>&lt;frame&gt;_turnm</c> (sprite/MCInBetween.h's TurnStep, TurnMirroredStep), and its walk's in-betweens (the
/// way to the next frame, LegsInBetweenSteps) as <c>&lt;frame&gt;_&lt;step&gt;</c>, over a turn middle
/// <c>&lt;frame&gt;_turn_&lt;step&gt;</c> (TurnBetweenStep), each over both frames' bounds.</para>
/// <para>Pictures (the menus' and logistics' art) are <c>menus/&lt;hash&gt;.hdf</c> (or a PNG pair) likewise, named by
/// their pixels' hash (<see cref="PictureHash"/>). Port-only: the briefing maps and their tactical maps are
/// <c>maps/&lt;hash&gt;.hdf</c>, named the same way, so that the menus' archive holds only what the screens show; a
/// picture is looked for in <c>menus</c>, then <c>maps</c> (a pack made before keeps its maps in <c>menus</c>).</para>
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
/// <para>A terrain tile's relief map is <c>relief/&lt;key&gt;.hdf</c> (named as its tile), BC5 at its own scale and
/// without indices: each pixel's slope, its height's rise a pixel right (R) and a pixel down the picture (G), 128 flat
/// and each step above or below it a 127th of a pixel's rise or fall (<see cref="ReliefFlat"/>).</para>
/// <para>A movie's HD frames are <c>movies/&lt;NAME&gt;.ivf</c> (the Smacker file's name, upper-case): AV1, frame for
/// frame the original's, which keeps playing for its sound, timing and palette (<see cref="FindMovie"/>).</para>
/// <para>The pack's <c>palette.rgb</c> (<see cref="MadeWith"/>) holds the colours its pictures were made from. The GPU
/// shows an HD pixel scaled by its original index's colour now over that colour, so another mission's palette, the
/// water's colour cycle and the brightness setting show through.</para>
/// <para>A folder's frames may instead be entries of an archive standing for it, <c>X.hda</c> beside folder
/// <c>X</c> (a FastFile, lib/MCFastFile.h: each entry named by the rest of its path with <c>\</c> between folders,
/// DEFLATE-packed or stored), the nearest of the folder's own and its parents'. An update writes a changed archive anew
/// (its patch applied in full, lib/MCFilePatch.h), so each is one file. At most 128 archives' files are kept open; the
/// others keep their directories and open again when read.</para>
/// <para>Frames are read when first drawn and kept while there is room (<see cref="CacheBudget"/>). Port-only: once a
/// numbered folder's frame in an archive is drawn the first time (an animation's), a thread of its own reads the frames
/// drawn after it (the rest of its in-betweens or turn middle's, then the next frame's), so that the game's thread
/// seldom reads one; a frame drawn before it is read ahead is read as drawn.</para>
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
        /// <summary>
        /// Port-only (HD movies): a movie's frame as dav1d decodes it, 8-bit 4:2:0 (IYUV): its Y plane, <c>Width</c>
        /// bytes a row, then its U and V planes, <c>(Width + 1) / 2</c> bytes a row and <c>(Height + 1) / 2</c> rows
        /// each, in the frame's <see cref="Frame::Colorspace"/>. The GPU makes its colours (opaque).
        /// </summary>
        Yuv420,
    };

    /// <summary>An HD frame as the GPU takes it.</summary>
    struct Frame
    {
        /// <summary>
        /// A number no other frame read in this run has, kept when the same frame is read again (the GPU keeps its copy
        /// by it).
        /// </summary>
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
        /// <summary>
        /// Port-only (sun shadows): whether its index picture marks a baked shadow (<see cref="BakedShadow"/>) the pack's
        /// generator found in art that casts no shadow of its own (a forest's), not one MarkBakedShadows marked.
        /// </summary>
        bool PaintedShadow = false;
        /// <summary>
        /// Port-only (HD movies): a <see cref="ColorFormat::Yuv420"/> frame's colour space (its matrix and range), and
        /// the movie it is a frame of: a number of the movie's own, which every frame of it has (the GPU keeps one set
        /// of textures for a movie, each frame put in place of the one before); 0 for any other frame.
        /// </summary>
        SDL_Colorspace Colorspace = SDL_COLORSPACE_UNKNOWN;
        uint64_t Movie = 0;

        /// <summary>The original's width and height.</summary>
        int32_t OriginalWidth() const { return Width / std::max(Scale, 1); }
        int32_t OriginalHeight() const { return Height / std::max(Scale, 1); }

        /// <summary>The bytes it keeps.</summary>
        size_t Bytes() const { return Colors.size() + Indices.size(); }

        /// <summary>
        /// Port-only: a copy without its colours, all a draw reads of a frame the GPU holds (MCVulkanRenderer's
        /// TextureState::Hd): every field but <see cref="Colors"/> (a field added here is copied there too).
        /// </summary>
        Frame WithoutColors() const;
    };

    /// <summary>The bytes an index picture's pixel takes (R8G8), and the green of a pixel with an index.</summary>
    inline constexpr size_t IndexBytes = 2;
    inline constexpr uint8_t HasIndex = 255;
    /// <summary>
    /// Port-only: an index picture's green for a pixel with an index that is part of an object's baked shadow
    /// (<see cref="MarkBakedShadows"/>, or marked by the pack's generator in art the game doesn't mark, a forest's);
    /// otherwise it is as HasIndex's.
    /// </summary>
    inline constexpr uint8_t BakedShadow = 254;
    /// <summary>The palette index the original's object art draws its baked shadows in (black).</summary>
    inline constexpr uint8_t BakedShadowIndex = 0x10;
    /// <summary>An index picture's blue in a frame that holds its blended pixels (<see cref="Frame::Blended"/>).</summary>
    inline constexpr uint8_t HoldsBlended = 255;

    /// <summary>
    /// A frame from a PNG pair as the generator writes them: <paramref name="colorPath"/> (straight RGBA, made
    /// premultiplied; already premultiplied in a frame that holds its blended pixels) and <paramref name="indexPath"/>
    /// (the same size; each original pixel's index read from its block's top-left pixel, its green HasIndex, BakedShadow
    /// or none, and whether the frame holds its blended pixels from the top-left pixel's blue) at
    /// <paramref name="scale"/> HD pixels per original pixel.
    /// </summary>
    std::expected<Frame, std::string> FrameFromPngs(const std::filesystem::path& colorPath,
                                                    const std::filesystem::path& indexPath, int32_t scale);

    /// <summary>
    /// A frame as a <c>.hdf</c> file: <c>MCHD</c>, version (u16), colour format (u8, 1 = BC7, 2 = BC7 holding a
    /// translucent shape's blended pixels too, which older builds leave to the original, 3 = BC5, a relief map), scale
    /// (u8), the original's width and height (u16 each), the colours' bytes (u32), the colours (BC7, encoded here when
    /// the frame holds RGBA8, as <paramref name="options"/> say), then the indices (R8G8 at the original's size; none
    /// in a relief map). Little-endian.
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

    /// <summary>
    /// The bytes of frames kept in memory before the least recently used go: MCMemoryRoom::MostHdBytes, unless
    /// <see cref="SetCacheBudget"/> set another. Port-only: below it the frames go only while memory is short
    /// (<see cref="NextFrame"/>).
    /// </summary>
    size_t CacheBudget();

    /// <summary>Sets the budget (0: MostHdBytes again), dropping frames until the kept ones fit (for the tests).</summary>
    void SetCacheBudget(size_t bytes);

    /// <summary>
    /// Port-only: a frame begins (the renderer's, as it starts recording one). While <paramref name="memoryShort"/>
    /// (MCMemoryRoom::MCFrameRoom), the least recently used frames go until MCMemoryRoom::TrimmedBytesPerFrame have
    /// gone, but never one asked for in the frame before or since: those are being drawn.
    /// </summary>
    void NextFrame(bool memoryShort);

    /// <summary>
    /// Drops the kept frames of the ground and its relief maps, which nothing reads once the GPU has the ground up
    /// (MCTerrainUploadGround); one asked for again (the pack or the relief maps changed) is read again, with its Id.
    /// </summary>
    void DropGround();

    /// <summary>The pack's folder's name, in the user folder or a game folder.</summary>
    inline constexpr std::string_view DefaultFolder = "HD";

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
    /// Opens now the pack's archive holding <paramref name="key"/>'s frames, if it has one, so that the first draw of a
    /// shape loaded from it doesn't read the archive's directory. Does nothing while HD art is off.
    /// </summary>
    void Prepare(const MechShapeKey& key);

    /// <summary>
    /// Notes that <paramref name="shape"/>'s frames are in its <c>HdFolder</c> (none: nothing), as <see cref="Attach"/>
    /// with its key does, and its neighbours in a turn in its <c>HdNeighbours</c> (<see cref="SetNeighbours"/>). Does
    /// nothing while HD art is off.
    /// </summary>
    void AttachFolder(const std::shared_ptr<MCShape>& shape);

    /// <summary>
    /// Port-only: notes that the shapes next to <paramref name="shape"/> in a turn (a mech part's gesture at the facings
    /// beside its own, nearest first) have their frames in the pack's <paramref name="folders"/> (FolderOf their keys),
    /// kept with the shape (<c>HdNeighbours</c>) as its folder is. Once one of its frames is drawn the first time, the
    /// reading thread reads the same frame of each (with its steps) where the shape's archive holds it, as it reads the
    /// frames drawn after it, so that the facing a turn comes to is seldom read by the game's thread. Their frames are
    /// noted only while HD art is on (AttachFolder notes them when it goes on).
    /// </summary>
    void SetNeighbours(const std::shared_ptr<MCShape>& shape, std::vector<std::filesystem::path> folders);

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
    std::shared_ptr<const Frame> Find(std::span<const uint8_t> table, int32_t frame, int32_t width, int32_t height,
                                      int32_t step = 0);

    /// <summary>
    /// Whether the frames of the shape table starting at <paramref name="table"/> are drawn with their own alpha's edges
    /// (the mech bar's damage diagrams: the interface's pixel art), not the original's pixels' smoothed (the renderer's
    /// default for a frame that doesn't hold its blended pixels).
    /// </summary>
    bool KeepsFrameEdges(std::span<const uint8_t> table);

    /// <summary>
    /// Whether the shape table starting at <paramref name="table"/> is tied to the pack (and its owner lives): its frames
    /// are looked for there.
    /// </summary>
    bool Attached(std::span<const uint8_t> table);

    /// <summary>
    /// For the tools: the pack folder the frames of the shape table starting at <paramref name="table"/> are read from;
    /// none when it isn't attached (or its owner is gone).
    /// </summary>
    std::optional<std::filesystem::path> AttachedFolder(std::span<const uint8_t> table);

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
    /// The HD copy of the picture with <paramref name="pixels"/> (<c>menus/&lt;hash&gt;</c> in the pack, or a map's
    /// <c>maps/&lt;hash&gt;</c>, kept as a frame's), read the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindPicture(std::span<const uint8_t> pixels, int32_t width, int32_t height);

    /// <summary>Port-only: a picture whose HD copy to read ahead (<see cref="ReadPicturesAhead"/>): FindPicture's.</summary>
    struct PictureAhead
    {
        std::span<const uint8_t> Pixels;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// Port-only: has the reading thread read the HD copies of <paramref name="pictures"/> ahead of their first draw, those
    /// the pack's archives have (the menus', the maps') that aren't kept, looked at or asked for already, in order, while
    /// they and the frames kept take at most half the cache's budget; <see cref="KeepReadAhead"/> keeps them as drawn ones
    /// are kept.
    /// </summary>
    void ReadPicturesAhead(std::span<const PictureAhead> pictures);

    /// <summary>
    /// Port-only: keeps the frames and pictures the reading thread has read ahead (<see cref="ReadPicturesAhead"/>) as a
    /// first draw keeps a frame (Find and FindPicture then find them without a read), against the cache's budget, its
    /// least recently used going first, so that one read well before its draw (a neighbouring facing's,
    /// <see cref="SetNeighbours"/>) is kept until then. At its first draw a frame is checked against the size the draw
    /// asks for, as one read then is, and has the frames drawn after it read ahead. The game's thread, once a frame.
    /// </summary>
    void KeepReadAhead();

    /// <summary>
    /// Port-only: keeps <paramref name="frame"/>, numbered anew, as the HD copy of the picture with
    /// <paramref name="pixels"/>: one the game makes of the pack's pictures (the session screen's MATCH OPTIONS tab,
    /// lettered from the briefing's tabs), which no pack has. FindPicture finds it until the pack changes.
    /// </summary>
    void KeepPicture(std::span<const uint8_t> pixels, int32_t width, int32_t height, Frame&& frame);

    /// <summary>
    /// Port-only: a frame of <paramref name="like"/>'s scale and colour format for a picture of
    /// <paramref name="width"/> x <paramref name="height"/> original pixels, all blank (no colour, no index).
    /// </summary>
    Frame BlankFrame(const Frame& like, int32_t width, int32_t height);

    /// <summary>
    /// Port-only: copies the colours and indices of the <paramref name="width"/> x <paramref name="height"/> original
    /// pixels at (<paramref name="x"/>, <paramref name="y"/>) of <paramref name="source"/> to (<paramref name="toX"/>,
    /// <paramref name="toY"/>) of <paramref name="target"/>. False, copying nothing, when the frames' scales or colour
    /// formats differ, the pixels aren't inside both, or the format's blocks don't fall on original pixels (BC7 at a
    /// scale that isn't a whole number of its blocks; BC5).
    /// </summary>
    bool CopyArea(const Frame& source, int32_t x, int32_t y, int32_t width, int32_t height, Frame& target, int32_t toX,
                  int32_t toY);
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

    /// <summary>Port-only: a ground tile whose HD copy to read ahead (<see cref="ReadGroundAhead"/>): FindTerrainTile's.</summary>
    struct GroundAhead
    {
        std::span<const uint8_t> Tile;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// Port-only (SPEC-first-use.md): has the readers read the HD copies of <paramref name="tiles"/>, and their relief
    /// maps with <paramref name="relief"/>, as a load's reads (first of all their reads, several at once, in the
    /// archive's order), those the pack's archive has that aren't kept, looked at or asked for already, while they and
    /// the frames kept take at most the cache's budget. <see cref="WaitForLoadReads"/> waits for them and
    /// <see cref="KeepReadAhead"/> keeps them; FindTerrainTile and FindTerrainRelief then find them without a read.
    /// </summary>
    void ReadGroundAhead(std::span<const GroundAhead> tiles, bool relief);

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
    /// in-between, as <see cref="Find"/>'s does. Given the table's <paramref name="frames"/>, the frames drawn after it
    /// are read ahead, as Find's are.
    /// </summary>
    std::shared_ptr<const Frame> FindShape(uint64_t hash, int32_t frame, int32_t width, int32_t height,
                                           int32_t step = 0, int32_t frames = 0);

    /// <summary>Port-only: a shape's frame whose HD copy to read ahead (<see cref="ReadShapesAhead"/>): FindShape's.</summary>
    struct ShapeAhead
    {
        std::span<const uint8_t> Table;
        int32_t Frame = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// Port-only (SPEC-first-use.md): has the readers read the HD copies of <paramref name="shapes"/>' frames (each
    /// table's own folder, found by its bytes' hash: <see cref="ShapeTableHash"/>) as a load's reads, as
    /// <see cref="ReadGroundAhead"/> reads the ground: several at once, each archive's in its order, waited for by
    /// <see cref="WaitForLoadReads"/> and kept by <see cref="KeepReadAhead"/>, so FindShape then finds them without a
    /// read.
    /// </summary>
    void ReadShapesAhead(std::span<const ShapeAhead> shapes);

    /// <summary>
    /// Sets the shape tables (the mechs' shadows: flat translucent shapes) whose edges are drawn smooth at the output's
    /// resolution while HD art is on, from the 1x shape's coverage; none needs a pack.
    /// </summary>
    void SetSmoothEdgeTables(std::span<const MCAddressKey> tables);

    /// <summary>Whether HD art is on and <paramref name="table"/> is one whose edges are drawn smooth.</summary>
    bool SmoothEdges(std::span<const uint8_t> table);

    /// <summary>A frame of a shape table (where the table starts), and its parts drawn by hand.</summary>
    struct LineArtFrame
    {
        MCAddressKey Table;
        int32_t Frame = 0;
        std::vector<MCSharpLines::MCDrawnLoop> Drawn;
    };

    /// <summary>
    /// Sets the shape frames that are line art (one-pixel outlines: the mechs' and vehicles' diagrams in the store and
    /// the mech bay, and the store's sold-out outlines), redrawn as smooth strokes (platform/MCSharpLines) in place of
    /// any pack's copy on a screen drawn at the size it's shown while HD art is on; none needs a pack.
    /// </summary>
    void SetLineArtFrames(std::span<const LineArtFrame> frames);

    /// <summary>Whether HD art is on and frame <paramref name="frame"/> of <paramref name="table"/> is line art.</summary>
    bool LineArt(std::span<const uint8_t> table, int32_t frame);

    /// <summary>A line art frame's parts drawn by hand (none for most).</summary>
    std::span<const MCSharpLines::MCDrawnLoop> LineArtDrawn(std::span<const uint8_t> table, int32_t frame);

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

    /// <summary>Port-only: how many of the frames read the reading thread read ahead.</summary>
    size_t FramesReadAhead();

    /// <summary>Port-only: how many frames the reading thread has read as a neighbour's in a turn (<see cref="SetNeighbours"/>).</summary>
    size_t NeighboursReadAhead();

    /// <summary>
    /// Waits until the readers have read ahead all they were handed: the tests, and a mission's load for what its first
    /// frame draws.
    /// </summary>
    void WaitForReadAhead();

    /// <summary>
    /// Port-only: waits until the readers have read what a load waits for (<see cref="ReadGroundAhead"/>), not the rest
    /// they were handed.
    /// </summary>
    void WaitForLoadReads();

    /// <summary>
    /// Port-only (SPEC-first-use.md): how the readers read ahead: how many there are (each a read in flight, and its
    /// frames unpacked on it), at most how many reads of the sets read ahead (a screen's pictures) may be in flight at
    /// once (0: as many as there are readers), and whether they run a step below the game's thread or level with it
    /// (platform/MCThreadPriority.h). The game's command line sets them (<c>-hdreaders</c>, <c>-hdsetreads</c>,
    /// <c>-hdreaderpriority level|below</c>); the defaults are to be measured on the M4 Pro and the A10.
    /// </summary>
    struct ReaderSettings
    {
        size_t Readers = 4;
        size_t SetReads = 0;
        bool Below = true;

        bool operator==(const ReaderSettings&) const = default;
    };

    /// <summary>Port-only: the most readers there may be (<see cref="SetReaderSettings"/>).</summary>
    constexpr size_t MostReaders = 32;

    /// <summary>
    /// Port-only: sets how the readers read (1 to <see cref="MostReaders"/> of them). Those running end once the read in
    /// their hands is done and new ones start for what's left to read.
    /// </summary>
    void SetReaderSettings(const ReaderSettings& settings);

    /// <summary>Port-only: how the readers read now.</summary>
    ReaderSettings GetReaderSettings();

    /// <summary>
    /// Port-only (SPEC-first-use.md): the most bytes one of the readers' reads of an archive's neighbouring entries takes,
    /// and the widest gap between two entries one read takes both across (read and thrown away: a read saved costs a slow
    /// disk more than this many bytes do).
    /// </summary>
    constexpr uint64_t ReaderRunBytes = 1024u * 1024u;
    constexpr uint64_t ReaderRunGap = 64u * 1024u;

    /// <summary>
    /// Port-only: how many reads of the archives the readers have made: one for a run of neighbouring entries read
    /// together (at most <see cref="ReaderRunBytes"/>, across gaps of at most <see cref="ReaderRunGap"/>).
    /// </summary>
    size_t ReaderReads();

    /// <summary>How many of the pack's archives are open (each opened once, the first time it is needed).</summary>
    size_t ArchivesOpen();

    /// <summary>
    /// How many of the open archives' files are open now, the readers' own among them (at most 128: the rest open
    /// again when read).
    /// </summary>
    size_t ArchiveFilesOpen();

    /// <summary>
    /// How many times the readers have opened handles of their own on the archives' files (each kept for the reads of its
    /// archive after, while the readers' kept handles hold it).
    /// </summary>
    size_t ReaderFilesOpened();
}

/// <summary>Port-only: draw the HD art pack's frames where it has them (PREFS "HDArt"; off by default).</summary>
extern int GHdArt;
