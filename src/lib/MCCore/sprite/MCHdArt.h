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
/// <c>mechs/&lt;part PAK&gt;/&lt;mech&gt;/&lt;packet&gt;/&lt;frame&gt;.png</c>: the frame's bounds at an integer scale,
/// RGBA with straight alpha. Beside each, <c>&lt;frame&gt;_idx.png</c> gives each pixel the original's palette index
/// (red) and whether it has one (green), so a paint scheme's fade table recolours the HD pixels it would have
/// recoloured.</para>
/// <para>Frames are read when first drawn and kept while there is room (<see cref="CacheBudget"/>).</para>
/// </remarks>
namespace MCHdArt
{
    /// <summary>An HD frame as the GPU takes it.</summary>
    struct Frame
    {
        /// <summary>A number no other frame read in this run has (the GPU keeps its copy by it).</summary>
        uint64_t Id = 0;
        /// <summary>The colours' width and height; the picture is twice as wide (see <see cref="Pixels"/>).</summary>
        int32_t Width = 0;
        int32_t Height = 0;
        /// <summary>HD pixels per original pixel.</summary>
        int32_t Scale = 1;
        /// <summary>
        /// RGBA8 rows <c>2 * Width</c> pixels wide: the premultiplied colours, then the palette indices (red) and
        /// whether a pixel has one (green, 255).
        /// </summary>
        std::vector<uint8_t> Pixels;
    };

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
    /// How many frames have been read from the pack so far, the time reading them took, and how many bytes the kept
    /// ones take.
    /// </summary>
    size_t FramesRead();
    std::chrono::nanoseconds ReadTime();
    size_t CachedBytes();
}

/// <summary>Port-only: draw the HD art pack's frames where it has them (PREFS "HDArt"; off by default).</summary>
extern int GHdArt;
