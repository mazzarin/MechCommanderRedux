#pragma once

#include "lib/MCDiskFile.h"
#include "lib/MCFilesAhead.h"
#include "platform/MCAddressKey.h"
#include "platform/MCSharpLines.h"
#include "shaders/channels.hlsli"
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
        /// <summary>
        /// The premultiplied colours, as <see cref="Format"/> says. Port-only (SPEC-memory.md's frame record): none in a
        /// frame the cache holds (Find and the rest hand out its record): the cache keeps them apart until the GPU has the
        /// frame's picture (<see cref="ColorsOf"/>, <see cref="ColorsUploaded"/>).
        /// </summary>
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
        /// Port-only (a version 2 pack's frame, SPEC-pack-v2.md's "A frame"): its index picture's green says what each
        /// pixel is (drawn, spread, in a baked shadow, blended: <see cref="HasIndexMark"/>), so it isn't marked again.
        /// </summary>
        bool Marked = false;
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
        /// <summary>Port-only (the ledger): the bytes its memory takes, as the allocator rounds its blocks up.</summary>
        size_t AllocatedBytes() const;
    };

    /// <summary>The bytes an index picture's pixel takes (R8G8), and the green of a pixel with an index.</summary>
    inline constexpr size_t IndexBytes = 2;
    inline constexpr auto HasIndex = static_cast<uint8_t>(MCGpuChannels::HasIndex);
    /// <summary>
    /// Port-only: an index picture's green for a pixel with an index that is part of an object's baked shadow
    /// (<see cref="MarkBakedShadows"/>, or marked by the pack's generator in art the game doesn't mark, a forest's);
    /// otherwise it is as HasIndex's.
    /// </summary>
    inline constexpr auto BakedShadow = static_cast<uint8_t>(MCGpuChannels::BakedShadow);
    /// <summary>
    /// Port-only (a version 2 frame's green, <see cref="Frame::Marked"/>): a pixel the original doesn't draw, its index
    /// spread from the nearest drawn one (the HD edge reaches past the original's), outside a baked shadow or in one; a
    /// pixel the original draws and blends (a translucent shape's, through AlphaPal.ini), outside a baked shadow or in
    /// one. A version 1 frame has only HasIndex, BakedShadow and 0, until the GPU marks its copy
    /// (shaders/channels.hlsli).
    /// </summary>
    inline constexpr auto Spread = static_cast<uint8_t>(MCGpuChannels::Spread);
    inline constexpr auto SpreadShadow = static_cast<uint8_t>(MCGpuChannels::SpreadShadow);
    inline constexpr auto DrawnBlended = static_cast<uint8_t>(MCGpuChannels::DrawnBlended);
    inline constexpr auto DrawnBlendedShadow = static_cast<uint8_t>(MCGpuChannels::DrawnBlendedShadow);

    /// <summary>
    /// Whether an index picture's <paramref name="green"/> gives its pixel an index (draw.pshader's HasIndexIn).
    /// </summary>
    constexpr bool HasIndexMark(uint8_t green)
    {
        return MCGpuChannels::HasIndexGreen(green);
    }

    /// <summary>
    /// Whether an index picture's <paramref name="green"/> puts its pixel in a baked shadow (draw.pshader's
    /// InShadowIn).
    /// </summary>
    constexpr bool InShadowMark(uint8_t green)
    {
        return MCGpuChannels::InShadowGreen(green);
    }

    /// <summary>
    /// Whether a version 2 frame's (<see cref="Frame::Marked"/>) index picture's <paramref name="green"/> is a pixel the
    /// original draws, its red the original's index: drawn, drawn and blended, in a baked shadow or not. A version 1
    /// frame's green doesn't say.
    /// </summary>
    constexpr bool OriginalDrawsMark(uint8_t green)
    {
        return MCGpuChannels::OriginalDrawsGreen(green);
    }

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
    /// at least as many empty pixels as drawn ones. A frame that holds its blended pixels (an effect's) has none; a
    /// version 2 frame (<see cref="Frame::Marked"/>) is marked already.
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

    /// <summary>
    /// The folder in a pack that holds mech <paramref name="mechNum"/>'s frames of part PAK <paramref name="partFile"/>
    /// ("legs90"), its archive's (<c>mechs/legs90/12</c>).
    /// </summary>
    std::filesystem::path MechArchiveFolder(std::string_view partFile, uint32_t mechNum);

    /// <summary>
    /// Port-only (a version 2 pack, SPEC-pack-v2.md's "The facts"): the data of the facts record of type
    /// <paramref name="type"/> in the own folder of the archive standing for <paramref name="folder"/> (below the
    /// pack); none when HD art isn't drawn, or the pack has no such archive, an archive of version 1 or no such record.
    /// </summary>
    std::optional<std::vector<uint8_t>> ArchiveFacts(const std::filesystem::path& folder, uint16_t type);

    /// <summary>
    /// Port-only (a version 2 pack, SPEC-pack-v2.md's "A frame"): whether the archive standing for
    /// <paramref name="folder"/> (below the pack) is of version 2, so its frames' green says which pixels the original
    /// draws (<see cref="OriginalDrawsMark"/>) apart from those its indices spread to; false when HD art isn't drawn or
    /// the pack has no such archive.
    /// </summary>
    bool MarksOriginalPixels(const std::filesystem::path& folder);

    /// <summary>
    /// Port-only (a version 2 pack, SPEC-pack-v2.md's record 1): whether shapes may be loaded from the pack's stripped
    /// shape tables: HD art drawn from numbered folders, by the GPU alone (the software renderer and mirror mode draw the
    /// original's rows).
    /// </summary>
    bool DrawsStrippedTables();

    /// <summary>Port-only: a shape table from a version 2 pack's record 1 (StrippedShapeTable).</summary>
    struct PackShapeTable
    {
        std::vector<uint8_t> Bytes;
        /// <summary>
        /// Its frames' rows stripped (each frame it holds with a box drawn from the pack); else the original's packet
        /// whole, rows and all, which the builder stores where the pack lacks some of its frames at their size.
        /// </summary>
        bool Stripped = true;
    };

    /// <summary>
    /// Port-only (record 1): the shape table standing for the packet whose frames are in <paramref name="folder"/>
    /// (FolderOf its key), when <see cref="DrawsStrippedTables"/> and the folder's archive, of version 2, holds the
    /// record: stripped, when the archive holds a frame for each frame the table holds with a box; or the original's
    /// packet whole (drawn from its rows as without a pack). Otherwise why not, in a few words the same each time (the
    /// loader then reads the original's packet, which the guard notes; --sprite-pack-check counts them).
    /// </summary>
    std::expected<PackShapeTable, std::string_view> StrippedShapeTable(const std::filesystem::path& folder);

    /// <summary>
    /// Port-only: the pack's folder of the original's shape file <paramref name="gamePath"/> (as the game opens it), or
    /// of its packet <paramref name="packet"/> when it's a PAK: <c>shapes/&lt;its path, in lower case&gt;[/&lt;packet&gt;]</c>
    /// in <c>shapes.hda</c>, whose shape table record holds the file's (or packet's) bytes whole.
    /// </summary>
    std::filesystem::path ShapeFileFolder(std::string_view gamePath, std::optional<int32_t> packet = std::nullopt);

    /// <summary>
    /// Port-only: the original's shape file <paramref name="gamePath"/> (its packet <paramref name="packet"/> of a
    /// PAK) as the pack holds it (ShapeFileFolder), so its loader reads nothing of the original's with HD art's numbered
    /// folders drawn; none when they aren't, the pack lacks it, or the game's file isn't <paramref name="size"/> bytes
    /// as the pack's is (another file than the pack was made from: a mod's).
    /// </summary>
    std::optional<std::vector<uint8_t>> PackShapeFile(std::string_view gamePath, std::optional<int32_t> packet,
                                                      uint32_t size);

    /// <summary>
    /// Port-only: tells HD art its shape loader (MCShapeFile) read <paramref name="bytes"/>, from the pack or from the
    /// game's files (the guard noted that read there), so finding their HD frames by their hash notes no read.
    /// </summary>
    void NoteShapeLoaded(std::span<const uint8_t> bytes);

    /// <summary>
    /// Port-only (SPEC-first-use.md: a mission's effects at its load): has the readers read every frame of each shape
    /// table read since the last call (<see cref="NoteShapeLoaded"/>: a laser's, smoke's, an explosion's, drawn by their
    /// bytes' hash), as a load's reads, so an effect's first frame at a new facing is taken, not unpacked on the game's
    /// thread. <see cref="WaitForReadAhead"/> waits for them and <see cref="KeepReadAhead"/> keeps them.
    /// </summary>
    void ReadLoadedShapesAhead();

    /// <summary>Port-only: whether <paramref name="table"/> is an attached stripped shape's (MCShape::Stripped).</summary>
    bool Stripped(std::span<const uint8_t> table);

    /// <summary>
    /// Port-only: VfxShapeVisit's pixels of frame <paramref name="frame"/> of <paramref name="table"/>, a stripped
    /// table's from its HD frame's index picture where the original draws (<see cref="OriginalDrawsMark"/>), the frame
    /// read if need be; none when the pack has no such frame.
    /// </summary>
    void VisitShape(std::span<const uint8_t> table, int32_t frame,
                    const std::function<void(int32_t x, int32_t y, uint8_t color)>& pixel);

    /// <summary>
    /// Port-only: the HD frame that stands for frame <paramref name="frame"/> of a stripped table
    /// <paramref name="table"/> (<see cref="Stripped"/>), read if need be; none for a table that isn't stripped, a frame
    /// it doesn't hold, or one the pack lacks. Its Id names the frame where its bytes can't: a stripped frame's bytes
    /// are its header alone, alike in frames of the same bounds.
    /// </summary>
    std::shared_ptr<const Frame> StrippedFrame(std::span<const uint8_t> table, int32_t frame);

    /// <summary>Port-only: forgets <paramref name="table"/>'s attachment (its shape takes another packet).</summary>
    void Detach(std::span<const uint8_t> table);
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
    /// The bytes of frames kept in memory before the least recently used go: port-only, the frames' share of the HD
    /// art's room as it stands (SPEC-memory.md's plan: what the rest, the hold and the pictures leave, never below the
    /// frames' records and a floor of colours), unless <see cref="SetCacheBudget"/> set another.
    /// </summary>
    size_t CacheBudget();

    /// <summary>Sets the budget (0: the plan's again), dropping frames until the kept ones fit (for the tests).</summary>
    void SetCacheBudget(size_t bytes);

    /// <summary>
    /// Port-only: a frame begins (the renderer's, as it starts recording one). The HD art's room is shared out
    /// (SPEC-memory.md's plan: MCMemoryRoom::ArtRoom): past their shares the least recently used frames go, then the
    /// hold's runs asked for least recently (it reads no more this mission), MCMemoryRoom::TrimmedBytesPerFrame a frame
    /// in all; while <paramref name="memoryShort"/> (MCMemoryRoom::MCFrameRoom) the frames go whatever their share,
    /// then the hold. Never a frame asked for in the frame before or since: those are being drawn.
    /// </summary>
    /// <returns>
    /// On shared memory, the room the GPU's HD pictures have this frame (the renderer keeps them to the smaller of it
    /// and the card's); none on a card of its own.
    /// </returns>
    std::optional<uint64_t> NextFrame(bool memoryShort);

    /// <summary>
    /// Drops the kept frames of the ground and its relief maps, which nothing reads once the GPU has the ground up
    /// (MCTerrainUploadGround); one asked for again (the pack or the relief maps changed) is read again, with its Id.
    /// </summary>
    void DropGround();

    /// <summary>The pack's folder's name, in the user folder or a game folder.</summary>
    inline constexpr std::string_view DefaultFolder = "HD";

    /// <summary>
    /// The pack's folders of the ground tiles' and the overlays' frames (terrain.hda, overlays.hda; a version 2 pack's
    /// records 2 and 3 in their own folders).
    /// </summary>
    inline constexpr std::string_view TerrainFolder = "terrain";
    inline constexpr std::string_view OverlayFolder = "overlays";

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
    /// Port-only: the pack's archive of the pictures a sharp screen draws its text and line art with
    /// (<c>sharp.hda</c>, platform/MCSharpPictures's, each named by its key), when the pack folder exists, its
    /// <c>pack.cfg</c> is of a format this code reads and it has one (looked for once a pack); none otherwise. HD art
    /// needn't be on: the pictures are the same whatever the pack's art.
    /// </summary>
    std::optional<std::filesystem::path> SharpArchive();

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
    /// Has the readers open the pack's archive holding <paramref name="key"/>'s frames, if it has one
    /// (<see cref="OpenArchivesAhead"/>), so that the first draw of a shape loaded from it doesn't read the archive's
    /// directory. Does nothing while HD art is off.
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
    /// Port-only (SPEC-first-use.md): what reads ahead are for: a load's, which it waits for
    /// (<see cref="WaitForLoadReads"/>); what the camera's surroundings draw soon (a unit's packets now and next), read
    /// after the frames drawn next; or a set's, read when nothing else is. Those but a load's within the cache's room for
    /// the sets.
    /// </summary>
    enum class ReadAs : uint8_t
    {
        Load,
        Soon,
        Set,
    };

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): a packet to read ahead whole: the pack's numbered folder
    /// (FolderOf a key), its first <see cref="Frames"/> frames (none given: every frame its archive holds), or those from
    /// frame <see cref="First"/> on, each with its in-betweens, and with <see cref="TurnMiddles"/> its frames' turn
    /// middles too (a legs packet's).
    /// </summary>
    struct PacketAhead
    {
        std::filesystem::path Folder;
        std::optional<int32_t> Frames;
        bool TurnMiddles = false;
        int32_t First = 0;
    };

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): has the readers read <paramref name="packets"/> whole,
    /// ahead of their first draws, as <paramref name="as"/> (Soon or Set; Soon is the load's under
    /// <see cref="MCOpeningViewAsks"/>): a unit's or object's in or near the view. A packet's archive entries neighbour
    /// each other, so it is one read or a few. Nothing when HD art doesn't draw numbered folders, and none of a frame
    /// kept, read, looked at or asked for already.
    /// </summary>
    void ReadPacketsAhead(std::span<const PacketAhead> packets, ReadAs as);

    /// <summary>
    /// Port-only (SPEC-first-use.md: a load): has the readers open the archives standing for the pack's numbered
    /// <paramref name="folders"/> (FolderOf keys) and read their directories, ahead of everything else they read, so the
    /// game's thread takes them open when it first looks in one (waiting while a reader opens it) rather than read a
    /// directory itself (up to a few MB a mech part's): a mission's mechs and objects at its load. Nothing for those found
    /// or asked for already, or when HD art doesn't draw numbered folders.
    /// </summary>
    void OpenArchivesAhead(std::span<const std::filesystem::path> folders);

    /// <summary>Port-only: how many archives the game's thread has taken open from the readers (OpenArchivesAhead).</summary>
    size_t ArchivesOpenedAhead();

    /// <summary>
    /// Port-only (SPEC-first-use.md: proving nothing is late): starts the HD trace into <paramref name="path"/>
    /// (replaced; <c>-hdtrace &lt;file&gt;</c>), a line a time, tab-separated, times in microseconds from its start:
    /// each frame begun (<c>F</c>), each read asked of the readers (<c>A</c>: its kind, archive, offset, stored bytes and
    /// entry), each of their reads (<c>R</c>: begun, its bytes in, its frames unpacked, its kind, archive, offset, bytes,
    /// entries; <c>K</c>: a run served from memory, its streams kept unpacked or held as stored, no read: begun, its
    /// frames unpacked, its kind, archive, offset, entries), each of the hold's reads (<c>H</c>: begun, its bytes in,
    /// archive, offset, bytes, streams held; <see cref="StartHold"/>), each archive named once (<c>P</c>), each frame's
    /// first use (<c>U</c>: kept as read ahead, taken from the
    /// readers, read by the game's thread or lacked, its folder and name, and the game's thread's microseconds on it),
    /// and notes (<c>N</c>: a benchmark's sections), for replaying the reads against another disk
    /// (tools: the read-ahead replay). False when it can't be written.
    /// </summary>
    bool OpenTrace(const std::filesystem::path& path);

    /// <summary>Port-only: ends the HD trace, if one is open (OpenTrace).</summary>
    void CloseTrace();

    /// <summary>Port-only: adds a note to the HD trace, if one is open (OpenTrace): <c>N time text</c>.</summary>
    void TraceNote(std::string_view text);

    /// <summary>Port-only: the examples the guard keeps of each kind of read it notes (GuardOriginalReads).</summary>
    inline constexpr size_t GuardExamples = 4;

    /// <summary>
    /// Port-only, for test runs alone (the tests; <c>-hdguard</c>, which no player passes): while on, each read of the
    /// original's art that a version 2 pack is drawn in place of (a shape table, a ground tile or overlay) is noted by its
    /// kind, so the run can fail on it (SPEC-pack-v2.md's done: no original art read). Setting it clears what it noted.
    /// The game reads the art as without the guard: a mod's art, which the pack lacks, still comes from its files.
    /// Each kind keeps its first <paramref name="examples"/> examples (a survey of every screen's pictures keeps all).
    /// </summary>
    void GuardOriginalReads(bool on, size_t examples = GuardExamples);

    /// <summary>Port-only: whether the guard is on (GuardOriginalReads) with a version 2 pack drawn.</summary>
    bool GuardingOriginalReads();

    /// <summary>
    /// Port-only: notes a read of the original's art, of <paramref name="kind"/> (which reads are told apart by) and
    /// <paramref name="what"/> (an example), while the guard is on with a version 2 pack drawn (GuardingOriginalReads).
    /// </summary>
    void NoteOriginalRead(std::string_view kind, std::string_view what);

    /// <summary>Port-only: the guard's kind of read for a shape table found by its bytes' hash (FindShape's).</summary>
    inline constexpr std::string_view ShapeHashRead =
        "a shape table read from the original's files, found by its bytes' hash";

    /// <summary>Port-only: one kind of read the guard noted: how many, and the first few.</summary>
    struct GuardedRead
    {
        std::string Kind;
        size_t Count = 0;
        std::vector<std::string> Examples;
    };

    /// <summary>
    /// Port-only: the reads the guard noted since it was set on (GuardOriginalReads), by kind; none while it's off.
    /// </summary>
    std::optional<std::vector<GuardedRead>> GuardedReads();

    /// <summary>
    /// Port-only: <see cref="ReadPacketsAhead"/> of one packet, without its turn middles, as a set's (the load's under
    /// <see cref="MCOpeningViewAsks"/>): folder <paramref name="folder"/>'s first <paramref name="frames"/> frames (none
    /// given: all its archive holds).
    /// </summary>
    void ReadPacketAhead(const std::filesystem::path& folder, std::optional<int32_t> frames = std::nullopt);

    /// <summary>
    /// Port-only: counts the times the readers forgot what they read and were asked (the pack set), and the times some of
    /// what was asked for ahead was dropped or left out for want of room (at most every few frames: NextFrame): an ask
    /// made before is asked again. The game's thread's, without the readers' lock.
    /// </summary>
    uint64_t AheadGeneration();

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): the packets an asker has had read ahead, each asked for
    /// once while the readers remember what they read (AheadGeneration), so an appearance can ask for what it draws every
    /// frame and only a packet it hasn't asked for costs a look in the archive.
    /// </summary>
    class MCAskedPackets
    {
    public:
        /// <summary>
        /// Whether <paramref name="packet"/> (a number naming it to the asker) is yet to be asked for since the readers
        /// last forgot; it is noted as asked.
        /// </summary>
        bool Ask(uint64_t packet);

    private:
        /// <summary>Each packet asked for: the generation it was asked in, plus one.</summary>
        std::unordered_map<uint64_t, uint64_t> _Asked;
    };

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
    /// Port-only (SPEC-memory.md's one copy of a frame's colours): <paramref name="frame"/>'s colours: its own when it
    /// holds them (a movie's, a tool's), else the cache's, read again when the cache let go of them (as a first draw reads
    /// a frame, on this thread). Empty when the pack no longer gives them. Valid until the cache next changes.
    /// </summary>
    std::span<const uint8_t> ColorsOf(const Frame& frame);

    /// <summary>
    /// Port-only: the GPU holds <paramref name="frame"/>'s picture now. The cache lets go of its colours and keeps its
    /// record, never letting go of it while the picture lives (<see cref="PictureDropped"/>), so a draw of it reads
    /// nothing.
    /// </summary>
    void ColorsUploaded(const Frame& frame);

    /// <summary>Port-only: the GPU let go of the picture of the frame numbered <paramref name="id"/>.</summary>
    void PictureDropped(uint64_t id);

    /// <summary>Port-only (the tests): how many times the cache read a frame again for its colours (ColorsOf).</summary>
    size_t ColorsReadAgain();

    /// <summary>Port-only (SPEC-memory.md's indices): who asks for a frame's indices (<see cref="IndicesOf"/>).</summary>
    enum class MCIndicesUse : uint8_t
    {
        /// <summary>Code on the CPU reading them: the frame's folder keeps its frames' indices from then on.</summary>
        Read,
        /// <summary>The GPU's upload of the frame's picture, which holds them after.</summary>
        Upload
    };

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): <paramref name="frame"/>'s indices: its own when it holds them, else its
    /// cached record's, read again when the record let go of them (once the GPU had its picture, a couple of frames
    /// after, unless code on the CPU had read its folder's: as a first draw reads a frame, on this thread). Valid until
    /// the cache next changes.
    /// </summary>
    std::span<const uint8_t> IndicesOf(const Frame& frame, MCIndicesUse use = MCIndicesUse::Read);

    /// <summary>Port-only (the tests, the frame log): how many times the cache read a frame again for its indices.</summary>
    size_t IndicesReadAgain();

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): while <paramref name="marked"/>, version 2 frames (Frame::Marked: a pack
    /// whose shape tables are stripped, so code on the CPU reads their indices for the original's pixels) keep their
    /// indices once the GPU has their pictures, whatever their folder: the weather, shown, measures every unit's frames
    /// as they're drawn (MCWeather's columns), the loading bar's among them, before it has read any.
    /// </summary>
    void KeepIndices(bool marked);

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
    /// the pack's archives have (the menus', the maps') that aren't kept, looked at or asked for already, in order, as
    /// <paramref name="as"/>: as sets while they and the frames kept take at most half the cache's budget, or as a load's
    /// (a mission's interface) within the whole budget; <see cref="KeepReadAhead"/> keeps them as drawn ones are kept.
    /// </summary>
    void ReadPicturesAhead(std::span<const PictureAhead> pictures, ReadAs as = ReadAs::Set);

    /// <summary>
    /// Port-only (SPEC-first-use.md): has the readers read the screens' whole set ahead, every picture in the pack's
    /// menus archive (a set's reads, the lowest kind, in the archive's order), while they and the frames kept take at
    /// most half the cache's budget, as <see cref="ReadPicturesAhead"/>'s do: a screen's, a tab's or a dialog's first
    /// draw then finds its pictures without a read, whichever it is. Only where the briefing maps lie apart, in an
    /// archive of their own (<c>maps.hda</c>): in a pack made before, they fill the menus' (370 of its 554 MB
    /// unpacked), and the screens' pictures are read ahead as logistics loads them. A picture read this way is checked
    /// against the size its first draw asks for, as any frame kept as read ahead is. The bytes asked for.
    /// </summary>
    size_t ReadMenuSetAhead();

    /// <summary>
    /// Port-only: keeps the frames and pictures the reading thread has read ahead (<see cref="ReadPicturesAhead"/>) as a
    /// first draw keeps a frame (Find and FindPicture then find them without a read), against the cache's budget, its
    /// least recently used going first, so that one read well before its draw (a neighbouring facing's,
    /// <see cref="SetNeighbours"/>) is kept until then. At its first draw a frame is checked against the size the draw
    /// asks for, as one read then is, and has the frames drawn after it read ahead. The game's thread, once a frame.
    /// </summary>
    void KeepReadAhead();

    /// <summary>
    /// Port-only (SPEC-memory.md's "Under the loading bar": the opening view): while one lives (the end of a mission's
    /// load), what play's first frames draw is asked for as the load's own reads, which the load waits for
    /// (<see cref="WaitForLoadReads"/>): a unit's packets drawn now (<see cref="ReadAs::Soon"/>) and an object's
    /// (<see cref="ReadPacketAhead"/>). A unit's sets to come stay sets. The game's thread's; the folders asked are noted
    /// for <see cref="KeepOpeningView"/>.
    /// </summary>
    class MCOpeningViewAsks
    {
    public:
        MCOpeningViewAsks();
        ~MCOpeningViewAsks();
        MCOpeningViewAsks(const MCOpeningViewAsks&) = delete;
        MCOpeningViewAsks& operator=(const MCOpeningViewAsks&) = delete;
    };

    /// <summary>
    /// Port-only (the opening view): keeps what the readers read (<see cref="KeepReadAhead"/>) and gives the frames of
    /// the folders asked for under <see cref="MCOpeningViewAsks"/> that are kept and not yet drawn, for the GPU to take
    /// before play (MCRenderer::PrepareHdPictures); the folders noted are forgotten.
    /// </summary>
    std::vector<std::shared_ptr<const Frame>> KeepOpeningView();

    /// <summary>
    /// Port-only: keeps <paramref name="frame"/>, numbered anew, as the HD copy of the picture with
    /// <paramref name="pixels"/>: one the game makes of the pack's pictures (the session screen's MATCH OPTIONS tab,
    /// lettered from the briefing's tabs), which no pack has. FindPicture finds it until the pack changes.
    /// </summary>
    void KeepPicture(std::span<const uint8_t> pixels, int32_t width, int32_t height, Frame&& frame);

    /// <summary>
    /// Port-only (SPEC-memory.md's cache budget): keeps the HD copies of <paramref name="pictures"/> in the cache, its
    /// budget whatever, until the hold lets go (<see cref="LetGoHold"/>, at the mission's results' end or instead of
    /// them) or the pack changes: a screen's pictures first drawn at the mission's end, the frame it's decided, with no
    /// lead for a read (the results screen's: MCMissionResultsScreen::KeepPicturesForEnd). Called once the load's reads
    /// are kept (<see cref="KeepReadAhead"/>); one they left out is read now, under the loading bar. They count in the
    /// cache's bytes. The bytes kept so.
    /// </summary>
    size_t KeepForMission(std::span<const PictureAhead> pictures);

    /// <summary>
    /// Port-only (SPEC-first-use.md: HD craters and footprints): keeps every frame of the attached shape tables
    /// <paramref name="tables"/> in the cache as <see cref="KeepForMission"/> keeps pictures, until the hold lets go or
    /// the pack changes: shapes the game makes in play at no unit's lead (a blast's crater, a step's footprint), read
    /// at the load (MCCraterManager::AttachHdArt). Called once the load's reads are kept; a frame they left out is read
    /// now, under the loading bar. The bytes kept so (the pictures' with them).
    /// </summary>
    size_t KeepShapesForMission(std::span<const std::vector<uint8_t>> tables);

    /// <summary>Port-only: the bytes <see cref="KeepForMission"/> keeps now.</summary>
    size_t KeptForMissionBytes();

    /// <summary>
    /// Port-only: lets go of what <see cref="KeepForMission"/> kept, trimmed from the next frame as any other frames are
    /// (<see cref="LetGoHold"/> calls it).
    /// </summary>
    void LetGoKeptForMission();

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
    /// The key the pack keeps terrain tile or overlay <paramref name="tile"/> under (the hash <see cref="TerrainTileName"/>
    /// writes in hex).
    /// </summary>
    uint64_t TerrainTileKey(std::span<const uint8_t> tile);

    /// <summary>
    /// The HD copy of terrain tile <paramref name="tile"/> (its bounds <paramref name="width"/> x
    /// <paramref name="height"/>), read from the pack the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindTerrainTile(std::span<const uint8_t> tile, int32_t width, int32_t height);

    /// <summary>
    /// Port-only (terrain/MCGroundNames.h): as FindTerrainTile, for the tile whose key (<see cref="TerrainTileKey"/>)
    /// is <paramref name="key"/>, known without its bytes.
    /// </summary>
    std::shared_ptr<const Frame> FindTerrainTileByKey(uint64_t key, int32_t width, int32_t height);

    /// <summary>A relief map's byte for flat ground; a byte stands for a rise of (byte - this) / 127 of a pixel.</summary>
    inline constexpr auto ReliefFlat = static_cast<int32_t>(MCGpuChannels::ReliefFlat);

    /// <summary>
    /// Terrain tile <paramref name="tile"/>'s relief map (its bounds <paramref name="width"/> x
    /// <paramref name="height"/>), read from the pack the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindTerrainRelief(std::span<const uint8_t> tile, int32_t width, int32_t height);

    /// <summary>Port-only (terrain/MCGroundNames.h): as FindTerrainRelief, by the tile's key, as FindTerrainTileByKey.</summary>
    std::shared_ptr<const Frame> FindTerrainReliefByKey(uint64_t key, int32_t width, int32_t height);

    /// <summary>Port-only: a ground tile whose HD copy to read ahead (<see cref="ReadGroundAhead"/>): FindTerrainTile's.</summary>
    struct GroundAhead
    {
        std::span<const uint8_t> Tile;
        int32_t Width = 0;
        int32_t Height = 0;
        /// <summary>
        /// Port-only (terrain/MCGroundNames.h): its key (<see cref="TerrainTileKey"/>) when it is known without the
        /// tile's bytes, which are then empty; otherwise the key is worked out from them.
        /// </summary>
        std::optional<uint64_t> Key;
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
    /// Port-only (SPEC-first-use.md: a load): has the readers read the HD frames of the map's <paramref name="overlays"/>
    /// (each a fast shape's bytes up to its last row's end and its size, as the renderer finds them: FindOverlay) as a
    /// load's reads, as ReadGroundAhead does the ground's, so the first frame of play finds them without a read.
    /// </summary>
    void ReadOverlaysAhead(std::span<const GroundAhead> overlays);

    /// <summary>
    /// The HD copy of terrain overlay <paramref name="shape"/> (a fast shape's bytes, <see cref="FastShapeSize"/>; its
    /// bounds <paramref name="width"/> x <paramref name="height"/>), read from the pack the first time; null when HD art
    /// is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindOverlay(std::span<const uint8_t> shape, int32_t width, int32_t height);

    /// <summary>
    /// Port-only (terrain/MCGroundNames.h): as FindOverlay, for the overlay whose key (<see cref="TerrainTileKey"/>) is
    /// <paramref name="key"/>, known without its bytes.
    /// </summary>
    std::shared_ptr<const Frame> FindOverlayByKey(uint64_t key, int32_t width, int32_t height);

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
    /// table's own folder, found by its bytes' hash: <see cref="ShapeTableHash"/>) as <paramref name="as"/>' reads:
    /// several at once, each archive's in its order, kept by <see cref="KeepReadAhead"/>, so FindShape then finds them
    /// without a read.
    /// </summary>
    void ReadShapesAhead(std::span<const ShapeAhead> shapes, ReadAs as);

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

    /// <summary>Port-only: who set a line art frame (<see cref="SetLineArtFrames"/>): each one's are set apart.</summary>
    enum class LineArtSet : uint8_t
    {
        /// <summary>Logistics' lists: the store's and the mech bay's diagrams and sold-out outlines.</summary>
        Logistics,
        /// <summary>The tactical map's DATA page: the part diagram of the unit it shows (MCTacticalMap::SetID).</summary>
        PartDiagram,
    };

    /// <summary>
    /// Sets the shape frames that are line art (one-pixel outlines: the mechs' and vehicles' diagrams in the store, the
    /// mech bay and the tactical map's DATA page, and the store's sold-out outlines), redrawn as smooth strokes
    /// (platform/MCSharpLines) in place of any pack's copy on a screen drawn at the size it's shown while HD art is on;
    /// none needs a pack. Replaces <paramref name="set"/>'s frames only; a table's frames must go before its memory does.
    /// </summary>
    void SetLineArtFrames(std::span<const LineArtFrame> frames, LineArtSet set);

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
    /// <c>-hdreaderpriority level|below</c>); the defaults are to be measured on the M4 Pro and the A10. Port-only: for
    /// the tests and benchmarks, whether a mission's art is held (<see cref="StartHold"/>) whatever the machine's
    /// memory (<c>-hdhold on|off</c>; none: the memory decides); and whether the frames a mech's gestures to come start
    /// on are read as soon reads, at its facings either side too (MCMechActor::ReadAhead), off only to measure it. No
    /// player sets them.
    /// </summary>
    struct ReaderSettings
    {
        size_t Readers = 4;
        size_t SetReads = 0;
        bool Below = true;
        std::optional<bool> Hold;
        bool FirstFramesSoon = true;

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
    /// Port-only, for the tests: with <paramref name="hold"/>, the readers take none of the reads waiting (they still
    /// open archives and make reads of what is asked for ahead), so a test lays out what waits before one is taken; let
    /// go, once what was asked for is made reads, they take them again.
    /// </summary>
    void HoldReads(bool hold);

    /// <summary>
    /// Port-only, for the tests: with <paramref name="hold"/>, the readers open none of the archives asked for ahead
    /// (<see cref="OpenArchivesAhead"/>), so a test changes the hold while they wait; let go, they open them.
    /// </summary>
    void HoldOpens(bool hold);

    /// <summary>
    /// Port-only, for the tests: with <paramref name="hold"/>, each reader that takes a run of reads holds it before
    /// reading it, its span counted as being read (so the reads asked for meanwhile whose entries lie in it wait on it);
    /// let go, they read them.
    /// </summary>
    void HoldTakenRuns(bool hold);

    /// <summary>
    /// Port-only, for the tests: waits until a reader holds a run it took (<see cref="HoldTakenRuns"/>) and the others have
    /// nothing in hand (what was asked for is made reads: waiting, holding a run, or served by one held).
    /// </summary>
    void WaitForHeldRun();

    /// <summary>
    /// Port-only (SPEC-first-use.md): the most bytes one of the readers' reads of an archive's neighbouring entries takes
    /// (a version 2 archive's stream is read whole, however large, once for all its entries waiting), and the widest gap
    /// between two entries one read takes both across (read and thrown away: a read saved costs a slow disk more than this
    /// many bytes do).
    /// </summary>
    constexpr uint64_t ReaderRunBytes = MCDiskFile::RunMostBytes;
    constexpr uint64_t ReaderRunGap = MCDiskFile::RunGapBytes;

    /// <summary>
    /// Port-only: how many reads of the archives the readers have made: one for a run of an archive's entries read
    /// together, the first read waiting and those waiting of any kind, asked for in any order, whose entries lie in its
    /// span (a version 2 stream's) or within <see cref="ReaderRunGap"/> of it (at most <see cref="ReaderRunBytes"/> in
    /// all), with those asked for while it reads whose entries lie in it; none for a run whose version 2 streams are kept
    /// unpacked from an earlier read.
    /// </summary>
    size_t ReaderReads();

    /// <summary>
    /// Port-only: the most bytes one of the hold's reads takes (a run of an archive's consecutive streams or entries; one
    /// larger than it alone), so a read asked for while the hold reads waits for at most this much more of the disk.
    /// </summary>
    constexpr uint64_t HoldRunBytes = uint64_t{4} << 20;

    /// <summary>
    /// Port-only: whether a mission's art is held (<see cref="StartHold"/>): <see cref="ReaderSettings::Hold"/> when set
    /// (the tests' and benchmarks' <c>-hdhold</c>; on, every byte, whatever the room), else always, as much as the plan's
    /// room takes (<see cref="HoldRoom"/>).
    /// </summary>
    bool HoldWanted();

    /// <summary>
    /// Port-only (SPEC-memory.md's fill: the mission packed in memory, or as much as fits): the most bytes the hold may
    /// keep now: the HD art's room (MCMemoryRoom::ArtRoom) less the working set's (MCMemoryRoom::WorkingSetBytes), on
    /// shared memory and on a card alike, as the hold's own bytes are the machine's; none (no limit) with
    /// <c>-hdhold on</c>.
    /// </summary>
    std::optional<uint64_t> HoldRoom();

    /// <summary>
    /// Port-only (SPEC-pack-v2.md's Memory: the hold): when HD art is drawn and the hold is wanted
    /// (<see cref="HoldWanted"/>), the readers read every archive open, and each opened later in the mission, below
    /// everything else they read (when nothing else waits, one run of at most <see cref="HoldRunBytes"/> at a time), and
    /// keep what of it a mission draws as stored (MCFastFile::HoldPieces: a version 2 archive's packets' and heads'
    /// streams, a version 1 archive's numbered frames), not unpacked, so no read in play goes to the disk: a frame nobody
    /// read ahead is unpacked from them. As much as <see cref="HoldRoom"/> takes, in the order read. Let go of
    /// (<see cref="LetGoHold"/>) at the mission's end, all at once when memory goes short (<see cref="NextFrame"/>,
    /// holding no more that mission), and when the pack is set.
    /// </summary>
    void StartHold();

    /// <summary>
    /// Port-only (SPEC-memory.md's "Under the loading bar"): a mission's load begins: when the hold is wanted, it holds
    /// from now each archive the load opens or asks for (<see cref="OpenArchivesAhead"/>), one an earlier mission left
    /// open too, but none the load doesn't ask for, its reads going several at once beside the load's, as readers are
    /// free of the load's own, until <see cref="FinishLoadFill"/>.
    /// </summary>
    void StartLoadFill();

    /// <summary>
    /// Port-only (the loading bar's last stretch): waits until the hold has read every archive the load handed it (or
    /// was let go of, or is full), calling <paramref name="shown"/> every <see cref="LoadFillShowEvery"/> meanwhile so the
    /// bar moves and the screen stays live, and once more after, the bar whole; from then on the hold reads below
    /// everything else, a run at a time.
    /// </summary>
    void FinishLoadFill(const std::function<void()>& shown);

    /// <summary>Port-only: how often <see cref="FinishLoadFill"/> shows the bar while it waits.</summary>
    constexpr std::chrono::milliseconds LoadFillShowEvery{33};

    /// <summary>
    /// Port-only: the share of the load's fill read (<see cref="StartLoadFill"/>): the bytes the hold has read over those
    /// it was handed; 0 while a load has handed it none yet, 1 once it's full or let go of, or outside a load when it
    /// was handed none.
    /// </summary>
    double LoadFillShare();

    /// <summary>
    /// Port-only (the loading bar): the share of the bar the load's fill takes while it fills, the last stretch: the
    /// load's own steps (0 to 100) take the rest before it.
    /// </summary>
    constexpr int32_t LoadFillBarPercent = 25;

    /// <summary>
    /// Port-only (the loading bar): the bar's percent for the load's own <paramref name="progress"/>: while a load fills
    /// (<see cref="StartLoadFill"/> to <see cref="FinishLoadFill"/>) its steps take the bar up to
    /// <see cref="LoadFillBarPercent"/> short of the end and the fill's share (<see cref="LoadFillShare"/>) the rest, and
    /// the bar never goes back (the share falls as the load hands the hold more); <paramref name="progress"/> itself
    /// otherwise.
    /// </summary>
    int32_t LoadBarPercent(int32_t progress);

    /// <summary>Port-only: lets go of the hold (<see cref="StartHold"/>) and holds no more: a mission's end.</summary>
    void LetGoHold();

    /// <summary>Port-only: the stored bytes the hold keeps now (<see cref="StartHold"/>).</summary>
    size_t HeldBytes();

    /// <summary>
    /// Port-only (SPEC-memory.md's "From the briefing"): the next mission is known, <paramref name="mission"/> (its
    /// <c>.fit</c>'s name): when the hold is wanted, it holds from now that mission's art, the archives of
    /// <paramref name="folders"/> (the pack's, from its list) and of those handed it next (<see cref="OpenArchivesAhead"/>:
    /// each mech of the player's lance as it's placed), reading behind the screens, a run of
    /// <see cref="BriefingRunBytes"/> at a time, until the load (<see cref="StartLoadFill"/>). The ground's frames the
    /// list names (<c>terrain/&lt;hash&gt;</c>, <c>overlays/&lt;hash&gt;</c>, as the load asked for them:
    /// <see cref="ReadGroundAhead"/>, <see cref="ReadOverlaysAhead"/>) are held alone, not their archives (every map's),
    /// and with <paramref name="relief"/> each tile's relief map beside it. Another mission's hold is let go of first.
    /// </summary>
    void StartBriefingHold(std::string_view mission, std::span<const std::filesystem::path> folders, bool relief);

    /// <summary>
    /// Port-only, for test runs (<c>-hdbriefinghold on|off</c>; on unless set): whether the hold may start from the
    /// briefing (<see cref="StartBriefingHold"/>). Off, it starts at the load as before the briefing's hold, the
    /// timing runs' comparison.
    /// </summary>
    void SetBriefingHold(bool on);

    /// <summary>Port-only: whether the hold is wanted, and from the briefing (<see cref="SetBriefingHold"/>).</summary>
    bool BriefingHoldWanted();

    /// <summary>
    /// Port-only: whether the hold holds for <paramref name="mission"/> since its briefing (<see cref="StartBriefingHold"/>,
    /// not let go of since), which its load keeps rather than letting go of.
    /// </summary>
    bool HoldsFor(std::string_view mission);

    /// <summary>
    /// Port-only: the most bytes one of the hold's reads takes behind a briefing's screens (<see cref="StartBriefingHold"/>):
    /// what a hard disk reads in about 10 ms, so a screen's read waits behind one for at most that.
    /// </summary>
    constexpr uint64_t BriefingRunBytes = uint64_t{1} << 20;

    /// <summary>
    /// Port-only (SPEC-memory.md's "From the briefing": a mission's list): the pack's folders the last load handed the hold
    /// (<see cref="StartLoadFill"/> to <see cref="FinishLoadFill"/>): those of the types it made
    /// (<see cref="OpenArchivesAhead"/>) and of the frames it drew or read ahead whose archives the hold keeps any of; each
    /// once, in the order handed, relative to the pack.
    /// </summary>
    std::vector<std::filesystem::path> LoadFolders();

    /// <summary>
    /// Port-only (SPEC-memory.md's "From the briefing" and "The mission's manifest"): the pack's folders
    /// <paramref name="mission"/>'s load hands the hold, but those of the player's lance: the list its last load learned
    /// (<see cref="KeepHoldList"/>, in the user folder), else the program's (sprite/MCHoldLists.h, worked out from
    /// <see cref="LoadFolders"/>); none for a mission with neither (a mod's first play), which fills at its load.
    /// </summary>
    std::vector<std::filesystem::path> BriefingFolders(std::string_view mission);

    /// <summary>
    /// Port-only (SPEC-memory.md's "The mission's manifest"): one of the pack's archives a mission's manifest lists, each
    /// once: the folder it stands for (relative to the pack), what the hold keeps of it (stored) and its pictures as the
    /// GPU takes them (unpacked), and the frames it holds that the mission needs (a ground archive's: those its list
    /// names; any other's: all). Need: when recorded plays of the mission first drew it, in seconds of play; none until
    /// a play has been recorded.
    /// </summary>
    struct ManifestEntry
    {
        std::filesystem::path Folder;
        std::optional<float> Need;
        uint64_t Bytes = 0;
        uint64_t GpuBytes = 0;
        uint32_t Frames = 0;
    };

    /// <summary>
    /// Port-only: a mission's manifest as the hold reads it now: the mission (its briefing's, <see cref="StartBriefingHold"/>;
    /// empty when none), its part (Entries) and the players' (each mech as it's picked: <see cref="HoldMechAhead"/>; an
    /// archive another unit of the mission's shares is the mission's), each archive in the order the hold took it; a
    /// count of its changes (a list held, a mech picked, an archive opened, the load begun, the hold let go); and whether
    /// it's complete: the load has begun, so every player's mechs are in, and every archive it lists is open.
    /// </summary>
    struct MissionManifest
    {
        std::string Mission;
        std::vector<ManifestEntry> Entries;
        std::vector<ManifestEntry> Players;
        uint64_t Generation = 0;
        bool Complete = false;
    };

    /// <summary>Port-only: the mission's manifest now (<see cref="MissionManifest"/>). The game's thread's.</summary>
    MissionManifest Manifest();

    /// <summary>
    /// Port-only (SPEC-memory.md's "The mission's manifest"): keeps <paramref name="folders"/> (MCScenario::HoldList) as
    /// the hold list <paramref name="mission"/>'s load learned, in the user folder, for its next briefing
    /// (<see cref="BriefingFolders"/>). Each load writes its own, so a list follows the mission's files and settings.
    /// </summary>
    void KeepHoldList(std::string_view mission, std::span<const std::string> folders);

    /// <summary>
    /// Port-only (SPEC-memory.md's "From the briefing"): the pieces of the game folder's files <paramref name="mission"/>'s
    /// load reads from the disk (MCFilesAhead::NotedPieces), its briefing's to read ahead: those its last load learned
    /// (<see cref="KeepFilePieces"/>), else the program's (sprite/MCHoldLists.h); none for a mission with neither. HD
    /// art or none.
    /// </summary>
    std::vector<MCFilesAhead::MCFilePiece> MissionFilePieces(std::string_view mission);

    /// <summary>
    /// Port-only: keeps <paramref name="pieces"/> as those <paramref name="mission"/>'s load read, in the user folder
    /// beside its hold list, for <see cref="MissionFilePieces"/>; none kept leaves the last list.
    /// </summary>
    void KeepFilePieces(std::string_view mission, const std::vector<MCFilesAhead::MCFilePiece>& pieces);

    /// <summary>
    /// Port-only (SPEC-memory.md's "From the briefing"): a mech is in a player's lance: while a briefing's mission is
    /// held (<see cref="StartBriefingHold"/>), the hold reads its tree's archives too, <paramref name="folders"/> (the
    /// mech's, MechTreeFolders), and holds them until the mission ends.
    /// </summary>
    void HoldMechAhead(std::span<const std::filesystem::path> folders);

    /// <summary>Port-only: how many reads the hold has made (<see cref="StartHold"/>), for the tests.</summary>
    size_t HeldReads();

    /// <summary>
    /// Port-only, for the tests: waits until the hold (<see cref="StartHold"/>) has read every archive it was handed, or
    /// was let go of.
    /// </summary>
    void WaitForHold();

    /// <summary>
    /// How many of the pack's archives are open (each opened once, the first time it is needed, or by the readers when
    /// asked ahead: OpenArchivesAhead).
    /// </summary>
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
