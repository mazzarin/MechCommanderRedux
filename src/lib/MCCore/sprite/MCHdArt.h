#pragma once

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
/// the HD pixels it would have recoloured.</para>
/// <para>A terrain tile's colours are <c>terrain/&lt;key&gt;.hdf</c> (<see cref="TerrainTileName"/>), drawn inside the
/// original tile's pixels; their alpha is unused.</para>
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
    /// A frame from a PNG pair as the generator writes them: <paramref name="colorPath"/> (straight RGBA, made
    /// premultiplied) and <paramref name="indexPath"/> (the same size; each original pixel's index read from its block's
    /// top-left pixel) at <paramref name="scale"/> HD pixels per original pixel.
    /// </summary>
    std::expected<Frame, std::string> FrameFromPngs(const std::filesystem::path& colorPath,
                                                    const std::filesystem::path& indexPath, int32_t scale);

    /// <summary>
    /// A frame as a <c>.hdf</c> file: <c>MCHD</c>, version (u16), colour format (u8, 1 = BC7), scale (u8), the
    /// original's width and height (u16 each), the colours' bytes (u32), the colours (BC7, encoded here when the frame
    /// holds RGBA8), then the indices (R8G8 at the original's size). Little-endian.
    /// </summary>
    std::vector<uint8_t> EncodeHdf(const Frame& frame);

    /// <summary>
    /// The frame a <c>.hdf</c> file's <paramref name="bytes"/> hold, checked against the original's
    /// <paramref name="width"/> x <paramref name="height"/> (0 x 0: any), or what is wrong with it.
    /// </summary>
    std::expected<Frame, std::string> DecodeHdf(std::span<const uint8_t> bytes, int32_t width, int32_t height);

    /// <summary>Where a mech part's shape comes from.</summary>
    struct MechShapeKey
    {
        /// <summary>The part PAK's name ("legs90"...).</summary>
        std::string PartFile;
        /// <summary>The mech's packet in it, and the shape's packet in the mech's.</summary>
        uint32_t MechNum = 0;
        uint32_t Packet = 0;
    };

    /// <summary>The bytes of frames kept in memory before the least recently used go.</summary>
    inline constexpr size_t CacheBudget = 512u * 1024u * 1024u;

    /// <summary>The pack's folder (the user folder's <c>HD</c> unless set).</summary>
    std::filesystem::path PackFolder();

    /// <summary>Sets the pack's folder (empty: the default); forgets the shapes and frames found so far.</summary>
    void SetPackFolder(const std::filesystem::path& folder);

    /// <summary>Whether HD art is drawn: <see cref="GHdArt"/>, and a pack folder that exists.</summary>
    bool Active();

    /// <summary>
    /// Notes that <paramref name="shape"/> was loaded from <paramref name="key"/>: while it lives, its frames are drawn
    /// from the pack's when it has them. Does nothing while HD art is off.
    /// </summary>
    void Attach(const std::shared_ptr<MCShape>& shape, const MechShapeKey& key);

    /// <summary>
    /// The HD copy of frame <paramref name="frame"/> of the shape table starting at <paramref name="table"/> (whose
    /// bounds are <paramref name="width"/> x <paramref name="height"/>), read from the pack the first time; null when
    /// HD art is off or the pack has no such frame, or its frame isn't a whole multiple of those bounds.
    /// </summary>
    std::shared_ptr<const Frame> Find(const uint8_t* table, int32_t frame, int32_t width, int32_t height);

    /// <summary>
    /// The name the pack keeps terrain tile <paramref name="tile"/> under (its data as the ground mesh keeps it: up to
    /// its last row's end): the data's 64-bit FNV-1a hash in hex, so the same tile in any tile set has one name.
    /// </summary>
    std::string TerrainTileName(std::span<const uint8_t> tile);

    /// <summary>
    /// The HD copy of terrain tile <paramref name="tile"/> (its bounds <paramref name="width"/> x
    /// <paramref name="height"/>), read from the pack the first time; null when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const Frame> FindTerrainTile(std::span<const uint8_t> tile, int32_t width, int32_t height);

    /// <summary>The bytes of a palette file: 256 colours, red, green and blue.</summary>
    inline constexpr size_t PaletteBytes = 256 * 3;

    /// <summary>The colours the pack's pictures were made from (its palette.rgb), or none.</summary>
    const std::optional<std::array<uint8_t, PaletteBytes>>& MadeWith();

    /// <summary>A number that changes whenever the pack in use (or whether one is) may have: see SetPackFolder.</summary>
    uint64_t PackVersion();

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
