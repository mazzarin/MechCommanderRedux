#include "stdafx.h"
#include "platform/MCVulkanRenderer.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCSharpLines.h"
#include "platform/MCSharpText.h"
#include "platform/MCSoftwareRenderer.h"
#include "platform/MCVulkanShaders.h"
#include "lib/MCFatal.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSceneLights.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Upload memory (the bytes a queued upload gives) seen as values of <typeparamref name="T"/>.</summary>
    template <typename T> std::span<T> UploadAs(std::span<uint8_t> bytes)
    {
        return std::span<T>(reinterpret_cast<T*>(bytes.data()), bytes.size() / sizeof(T));
    }

    std::string SdlError(std::string_view what)
    {
        return std::format("{}: {}", what, SDL_GetError());
    }

    /// <summary>A VFX shape's header: where its bounds (from its hot spot) lie, and its size (its rows follow).</summary>
    constexpr size_t ShapeXMinAt = 0x08;
    constexpr size_t ShapeYMinAt = 0x0c;
    constexpr size_t ShapeXMaxAt = 0x10;
    constexpr size_t ShapeYMaxAt = 0x14;
    constexpr size_t ShapeHeaderBytes = 0x18;

    // shaders/draw.pshader's kinds and flags.
    constexpr uint32_t KindFill = 0;
    constexpr uint32_t KindHash = 1;
    constexpr uint32_t KindTexture = 2;
    constexpr uint32_t KindGouraud = 3;
    constexpr uint32_t KindTexelWalk = 4;
    constexpr uint32_t KindHd = 5;
    /// <summary>Port-only: a glyph redrawn sharp (MCSharpText), on a sharp screen.</summary>
    constexpr uint32_t KindSharpGlyph = 6;
    /// <summary>Port-only: a line's and an ellipse's colour pass at the output's resolution (shaders/draw.pshader).</summary>
    constexpr uint32_t KindSmoothLine = 7;
    constexpr uint32_t KindSmoothEllipse = 8;
    /// <summary>Port-only: a textured polygon's picture shown from the HD art pack, turned and scaled (draw.pshader).</summary>
    constexpr uint32_t KindHdMap = 9;
    /// <summary>Port-only: a game-drawn picture mapped with its key transparent (the fog of war), its edges smooth.</summary>
    constexpr uint32_t KindSmoothMap = 10;
    /// <summary>Port-only: a flat or Gouraud polygon's colour pass at the output's resolution (a laser's beam).</summary>
    constexpr uint32_t KindSmoothPolygon = 11;
    /// <summary>Port-only: a status bar's part at the output's resolution, its fill ending where the game measured it.</summary>
    constexpr uint32_t KindSmoothBar = 12;
    /// <summary>
    /// Port-only (HD lighting): a unit's shadow (MCShadowCommand), drawn into the colours' alpha alone (the shadow
    /// pipeline): the composite lights the world that much less there.
    /// </summary>
    constexpr uint32_t KindShadow = 13;
    /// <summary>
    /// Port-only: an HD art frame drawn as KindHd is, its edges from the original's pixels it draws, smoothed
    /// (draw.pshader).
    /// </summary>
    constexpr uint32_t KindHdSmooth = 14;
    /// <summary>Port-only: a terrain tile's seam pass (shaders/terrain.vshader; drawn only there).</summary>
    [[maybe_unused]] constexpr uint32_t KindTerrainSeam = 15;
    /// <summary>The kind's bits of a draw's op.</summary>
    constexpr uint32_t KindMask = 15;
    constexpr uint32_t OpaqueSource = 1u << 4;
    constexpr uint32_t FillSkipped = 1u << 5;
    constexpr uint32_t SkipRunKey = 1u << 6;
    constexpr uint32_t TableBefore = 1u << 7;
    constexpr uint32_t SkipKeyAfterTable = 1u << 8;
    constexpr uint32_t ColorKey = 1u << 9;
    constexpr uint32_t UseColor = 1u << 10;
    /// <summary>
    /// Port-only (HD lighting): a light's glow (MCRenderer::GlowScope), blended as strong as the frame's look.x
    /// (shaders/draw.pshader).
    /// </summary>
    constexpr uint32_t Glow = 1u << 11;
    constexpr uint32_t AlphaBlend = 1u << 12;
    constexpr uint32_t TableAfter = 1u << 13;
    /// <summary>The alpha colour is the draw's op.w, not the pixel's value.</summary>
    constexpr uint32_t GivenAlpha = 1u << 14;
    /// <summary>The draw writes the indices alone (the index-only pipeline); the shader doesn't read it.</summary>
    constexpr uint32_t IndexOnly = 1u << 15;
    /// <summary>
    /// Port-only: a translucent shape's coverage taken between its pixels (smooth edges), its place in the source in
    /// the draw's walk (shaders/draw.pshader). Bit 16 is the terrain shader's HdColors.
    /// </summary>
    constexpr uint32_t SmoothEdges = 1u << 17;
    /// <summary>
    /// Port-only, for a translucent shape drawn from the HD art pack: only its opaque pixels, or only its blended ones
    /// (shaders/draw.pshader).
    /// </summary>
    constexpr uint32_t OpaqueOnly = 1u << 18;
    constexpr uint32_t BlendedOnly = 1u << 19;
    /// <summary>Port-only: an HD frame's colours shown as made, not corrected by the palette (MCVulkanRenderer::HdColours).</summary>
    constexpr uint32_t HdAsMade = 1u << 20;
    /// <summary>Port-only: an HD art frame holding its blended pixels, some of which may add light (MCHdArt::Frame).</summary>
    constexpr uint32_t HdLight = 1u << 21;
    /// <summary>Port-only (sun shadows): an HD art frame drawn as a unit's shadow (AddSunShadow).</summary>
    constexpr uint32_t SunShadowDraw = 1u << 22;
    /// <summary>
    /// Port-only (HD particles): a KindSmoothEllipse draw that is a particle (MCParticleCommand), not an ellipse's colour
    /// pass (shaders/draw.pshader's ParticlePixel).
    /// </summary>
    constexpr uint32_t ParticleDraw = 1u << 23;
    /// <summary>
    /// Port-only (HDSway): a draw of a forest's canopy. Its index pass, drawn first, marks its pixels in the index
    /// target's alpha for the composite to sway (shaders/draw.pshader's SwayMark); its HD frame then takes the mark off
    /// its painted shadow, which shows the ground under it (_BlendMarks: the indices' alpha alone).
    /// </summary>
    constexpr uint32_t Sways = 1u << 24;

    /// <summary>
    /// Sways for an HD frame's draws when forests sway (GHdSway) and it is a forest's (a frame whose painted shadow the
    /// pack's generator marked: tools/upscale/baked_shadow.py marks only a forest's canopy and edges), else nothing.
    /// </summary>
    uint32_t SwaysOf(const MCHdArt::Frame& frame)
    {
        return GHdSway != 0 && frame.PaintedShadow ? Sways : 0u;
    }

    /// <summary>An atlas page's width and height.</summary>
    constexpr int32_t PageSize = 2048;
    /// <summary>More pages than this and the atlas starts again at the next frame.</summary>
    constexpr size_t MaxPages = 6;
    /// <summary>A picture not used for this many frames is dropped.</summary>
    constexpr uint64_t PictureFrames = 300;
    /// <summary>
    /// Pictures past PictureFrames are looked for once in this many frames: going over them all every frame costs more
    /// than holding them a little longer.
    /// </summary>
    constexpr uint64_t PictureSweepFrames = 32;
    /// <summary>The bytes the pictures may take before the oldest go.</summary>
    constexpr size_t PictureBudget = 128 * 1024 * 1024;
    /// <summary>The same for the HD art frames on the GPU.</summary>
    constexpr size_t HdPictureBudget = 512 * 1024 * 1024;
    /// <summary>
    /// The bytes a pixel of an HD art frame's RGBA8 colours, and of its BC7 colours (a 16-byte block per 4x4; BC5's, a
    /// relief map's, as many).
    /// </summary>
    constexpr uint32_t HdRgbaBytes = 4;
    constexpr uint32_t HdBc7Bytes = 1;
    /// <summary>
    /// draw.pshader's textures: the source, the tables, the alpha colours, the fits, the palette, the HD colours (an
    /// HD art frame's, or the terrain's two atlases) and the terrain's relief maps (two atlases); the palette where a
    /// draw has none.
    /// </summary>
    constexpr uint32_t DrawSamplers = 9;
    /// <summary>The surfaces' index targets (Surface::Index): RGBA8, the palette index in R.</summary>
    constexpr SDL_GPUTextureFormat IndexFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    constexpr uint32_t IndexPixelBytes = 4;
    /// <summary>Port-only (HD weather): the surfaces' glint targets (Surface::Glints), premultiplied RGBA8.</summary>
    constexpr SDL_GPUTextureFormat GlintFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    /// <summary>A surface's colour targets: its colours, its indices and (port-only) its glints.</summary>
    constexpr uint32_t SurfaceTargets = 3;
    /// <summary>
    /// The palette texture's rows: the colours shown, then the ones the HD art pack was made from (the shown ones when
    /// there is none), which draw.pshader corrects HD colours by.
    /// </summary>
    constexpr uint32_t PaletteRows = 2;
    /// <summary>The HD terrain atlas's widest and tallest (what the GPUs the port runs on sample).</summary>
    constexpr uint32_t MaxHdAtlasSide = 16384;
    /// <summary>The HD terrain's atlases at most (a big map's tiles fill more than one; shaders/draw.pshader's hdTex2).</summary>
    constexpr uint32_t HdTerrainAtlases = 2;
    /// <summary>A terrain image's ints in the mesh's image buffer (shaders/terrain.vshader's Image).</summary>
    constexpr size_t TerrainImageInts = 16;
    /// <summary>A terrain cell's ints in the mesh's cell buffer (shaders/terrain.vshader's cells: a uint4).</summary>
    constexpr size_t TerrainCellInts = 4;
    /// <summary>The bytes the glyphs redrawn sharp may take on the GPU (each a few kilobytes; RGBA8).</summary>
    constexpr size_t SharpGlyphBudget = 32 * 1024 * 1024;
    /// <summary>
    /// Port-only: a sharp glyph's colour field, above its scale (the low byte): the layer runs on past the glyph's
    /// border (MCSharpText::MCGlyphLayers::RunsOn), so its texels outside are its border's (shaders/draw.pshader).
    /// </summary>
    constexpr uint32_t SharpGlyphRunsOn = 1u << 8;
    /// <summary>A VFX font glyph's pixels follow its width (vfx/MCVfxFunctions.h's fonts).</summary>
    constexpr size_t GlyphPixelsAt = 4;
    /// <summary>FNV-1a's 64-bit offset basis and prime (a redrawn glyph is kept by its bytes' hash).</summary>
    constexpr uint64_t HashBasis = 0xcbf29ce484222325ull;
    constexpr uint64_t HashPrime = 0x100000001b3ull;

    /// <summary>Mixes <paramref name="value"/> into the FNV-1a hash <paramref name="key"/>.</summary>
    constexpr void MixHash(uint64_t& key, uint64_t value)
    {
        key ^= value;
        key *= HashPrime;
    }

    /// <summary>The most colour tables held at once (the table texture's height).</summary>
    constexpr uint32_t MaxTableRows = 8192;
    /// <summary>The table texture's row that leaves every colour as it is.</summary>
    constexpr uint32_t IdentityRow = 0;
    /// <summary>A decoded shape's bytes a pixel (DecodeShape): its value, then its state (1: drawn).</summary>
    constexpr size_t DecodedBytes = 2;
    constexpr uint8_t DecodedDrawn = 1;
    /// <summary>The width of the strip of written pixels.</summary>
    constexpr int32_t StripWidth = 4096;
    /// <summary>Upload offsets are kept on this boundary (the strictest any backend asks of a texture copy).</summary>
    constexpr size_t UploadAlignment = 512;

    /// <summary>The draw shader's vertex uniforms (shaders/instance.vshader, cbuffer Batch).</summary>
    struct BatchUniforms
    {
        float TargetSize[4];
        uint32_t Batch[4];
    };

    /// <summary>The draw shader's fragment uniforms (shaders/draw.pshader, cbuffer Frame).</summary>
    struct FrameUniforms
    {
        int32_t Scale[4];
        int32_t SourceScale[4];
        int32_t Cycle[4];
        uint32_t CycleSources[8];
        /// <summary>
        /// Port-only (HD lighting): x how strong a light's glow (Glow) shows, 1 as the original drew it; y with sun
        /// shadows on, how much of the light under it an object's baked shadow takes away (0: off, solid black).
        /// </summary>
        float Look[4];
    };

    /// <summary>shaders/draw.pshader's alpha colour kinds.</summary>
    constexpr float AlphaNothing = 0.0f;
    constexpr float AlphaOpaque = 1.0f;
    constexpr float AlphaBlended = 2.0f;

    /// <summary>
    /// Whether a translucent draw shows <paramref name="color"/> opaque (AlphaOpaque): not 0 or 0xff, which it leaves
    /// out, nor a special colour (AlphaPal.ini's), which it blends.
    /// </summary>
    bool OpaqueAlphaColor(int32_t color)
    {
        return color != 0 && color != 0xff && SpecialColor[static_cast<size_t>(color)] == 0;
    }

    /// <summary>Whether any of <paramref name="colors"/> is one a translucent draw shows opaque.</summary>
    bool AnyOpaqueAlphaColor(const std::bitset<256>& colors)
    {
        for (size_t color = 0; color < colors.size(); ++color)
        {
            if (colors.test(color) && OpaqueAlphaColor(static_cast<int32_t>(color)))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>The colours of the drawn pixels of an image decoded into the atlas (DecodedBytes a pixel).</summary>
    std::bitset<256> DrawnColors(std::span<const uint8_t> decoded)
    {
        std::bitset<256> colors;

        for (size_t at = 0; at + DecodedBytes <= decoded.size(); at += DecodedBytes)
        {
            if (decoded[at + 1] == DecodedDrawn)
            {
                colors.set(decoded[at]);
            }
        }

        return colors;
    }

    /// <summary>The free colours (AlphaTable and FindClosest leave out the system's 0..9 and 246..255).</summary>
    constexpr int32_t FirstFreeColor = 10;
    constexpr int32_t LastFreeColor = 245;

    /// <summary>Key spaces, so one block's bytes read as different formats don't share an atlas place.</summary>
    enum class ImageKind : uint64_t
    {
        Shape = 1,
        FastShape,
        Tile,
        Glyph,
        ShapeFill
    };

    uint64_t Seed(ImageKind kind, int32_t width, int32_t height)
    {
        return static_cast<uint64_t>(kind) << 56 ^ static_cast<uint64_t>(static_cast<uint32_t>(width)) << 28 ^
               static_cast<uint32_t>(height);
    }

    bool Empty(const MCRect& rect)
    {
        return rect.X1 < rect.X0 || rect.Y1 < rect.Y0;
    }

    bool Overlaps(const MCRect& a, const MCRect& b)
    {
        return !Empty(a) && !Empty(b) && a.X0 <= b.X1 && b.X0 <= a.X1 && a.Y0 <= b.Y1 && b.Y0 <= a.Y1;
    }

    bool Contains(const MCRect& outer, const MCRect& inner)
    {
        return !Empty(outer) && inner.X0 >= outer.X0 && inner.Y0 >= outer.Y0 && inner.X1 <= outer.X1 &&
               inner.Y1 <= outer.Y1;
    }

    MCRect Intersection(const MCRect& a, const MCRect& b)
    {
        return MCRect{std::max(a.X0, b.X0), std::max(a.Y0, b.Y0), std::min(a.X1, b.X1), std::min(a.Y1, b.Y1)};
    }

    int64_t Area(const MCRect& rect)
    {
        return Empty(rect) ? 0 : static_cast<int64_t>(rect.X1 - rect.X0 + 1) * (rect.Y1 - rect.Y0 + 1);
    }

    MCRect Union(const MCRect& a, const MCRect& b)
    {
        if (Empty(a))
        {
            return b;
        }

        if (Empty(b))
        {
            return a;
        }

        return MCRect{std::min(a.X0, b.X0), std::min(a.Y0, b.Y0), std::max(a.X1, b.X1), std::max(a.Y1, b.Y1)};
    }

    // Run-length shapes (VFX's format, see MCSoftwareRendererShapes.cpp): each row is tokens; a token's high seven
    // bits count pixels (bit 0: a literal of that many bytes, else a run of the next byte), 0 with bit 0 set skips the
    // next byte's count of pixels, 0 ends the row.

    /// <summary>Walks <paramref name="rows"/> rows of <paramref name="bytes"/>: the widest row and the bytes they take.</summary>
    size_t ShapeExtent(std::span<const uint8_t> bytes, int32_t rows, int32_t& width)
    {
        size_t data = 0;
        width = 0;

        for (int32_t row = 0; row < rows; ++row)
        {
            int32_t x = 0;

            for (;;)
            {
                const uint8_t token = bytes[data++];
                const int32_t count = token >> 1;

                if (count == 0)
                {
                    if ((token & 1) == 0)
                    {
                        break;
                    }

                    x += bytes[data++];
                    continue;
                }

                data += (token & 1) ? count : 1;
                x += count;
            }

            width = std::max(width, x);
        }

        return data;
    }

    /// <summary>Decodes shape rows into value + state pairs (1 a pixel, 2 a skipped pixel, 0 nothing).</summary>
    void DecodeShape(std::span<const uint8_t> bytes, int32_t rows, int32_t width, std::span<uint8_t> out)
    {
        size_t data = 0;
        std::memset(out.data(), 0, static_cast<size_t>(width) * rows * 2);

        for (int32_t row = 0; row < rows; ++row)
        {
            const std::span<uint8_t> line = out.subspan(static_cast<size_t>(row) * width * 2);
            int32_t x = 0;

            for (;;)
            {
                const uint8_t token = bytes[data++];
                const int32_t count = token >> 1;

                if (count == 0)
                {
                    if ((token & 1) == 0)
                    {
                        break;
                    }

                    const int32_t skip = bytes[data++];

                    for (int32_t i = 0; i < skip; ++i)
                    {
                        line[(x + i) * 2 + 1] = 2;
                    }

                    x += skip;
                    continue;
                }

                for (int32_t i = 0; i < count; ++i)
                {
                    line[(x + i) * 2] = (token & 1) ? bytes[data + i] : bytes[data];
                    line[(x + i) * 2 + 1] = 1;
                }

                data += (token & 1) ? count : 1;
                x += count;
            }
        }
    }

    /// <summary>
    /// The picture AG_shape_fill makes of a shape in a buffer of its bounds: <paramref name="rows"/> rows of
    /// <paramref name="width"/> bytes, drawn pixels as stored (through <paramref name="table"/> when there is one),
    /// everything else colour 0, each row cut at the buffer's width.
    /// </summary>
    void FillShape(std::span<const uint8_t> bytes, int32_t rows, int32_t width, const std::optional<MCColorTable>& xlat,
                   std::span<uint8_t> out)
    {
        size_t data = 0;
        std::memset(out.data(), 0, static_cast<size_t>(width) * rows);

        for (int32_t row = 0; row < rows; ++row)
        {
            const std::span<uint8_t> line = out.subspan(static_cast<size_t>(row) * width);
            int32_t x = 0;

            for (;;)
            {
                const uint8_t token = bytes[data++];
                const int32_t count = token >> 1;

                if (count == 0)
                {
                    if ((token & 1) == 0)
                    {
                        break;
                    }

                    x += bytes[data++];
                    continue;
                }

                for (int32_t i = 0; i < count && x + i < width; ++i)
                {
                    const uint8_t pixel = (token & 1) ? bytes[data + i] : bytes[data];
                    line[x + i] = xlat.has_value() ? (*xlat)[pixel] : pixel;
                }

                data += (token & 1) ? count : 1;
                x += count;
            }
        }
    }

    // Fast shapes (fastshp.cpp's format): rows of packets covering width + 1 pixels; c < 0x80 is a run of c pixels of
    // the next byte, c >= 0x80 is c - 0x80 literal bytes.

    /// <summary>Decodes a fast-shape row into value + state pairs (1 a literal pixel, 3 a run's).</summary>
    void DecodeFastRow(std::span<const uint8_t> bytes, int32_t width, std::span<uint8_t> out)
    {
        size_t data = 0;
        const std::span<uint8_t> line = out;

        for (int32_t x = 0; x < width;)
        {
            const uint32_t c = bytes[data++];

            if (c >= 0x80)
            {
                const int32_t n = static_cast<int32_t>(c) - 0x80;

                for (int32_t i = 0; i < n && x < width; ++i, ++x)
                {
                    line[x * 2] = bytes[data + i];
                    line[x * 2 + 1] = 1;
                }

                data += n;
            }
            else
            {
                const uint8_t color = bytes[data++];

                for (uint32_t i = 0; i < c && x < width; ++i, ++x)
                {
                    line[x * 2] = color;
                    line[x * 2 + 1] = 3;
                }
            }
        }
    }

    /// <summary>Where tile row <paramref name="row"/> starts, from the tile's start.</summary>
    uint32_t TileOffset(std::span<const uint8_t> tile, int32_t row)
    {
        return static_cast<uint32_t>(MCRead32(tile, 4 + static_cast<size_t>(row) * 4));
    }

    /// <summary>Decodes a tile into <paramref name="width"/> x height value + state pairs (1 a pixel, 0 none).</summary>
    void DecodeTile(std::span<const uint8_t> tile, int32_t width, std::span<uint8_t> image)
    {
        const int32_t height = tile[2];
        std::memset(image.data(), 0, static_cast<size_t>(width) * height * 2);

        for (int32_t row = 0; row < height; ++row)
        {
            const uint32_t offset = TileOffset(tile, row);
            const int32_t length = static_cast<int32_t>(TileOffset(tile, row + 1) - offset);
            const std::span<uint8_t> line = image.subspan(static_cast<size_t>(row) * width * 2);

            for (int32_t i = 0; i < length - 1; ++i)
            {
                line[(tile[offset] + i) * 2] = tile[offset + 1 + i];
                line[(tile[offset] + i) * 2 + 1] = 1;
            }
        }
    }

    /// <summary>Port-only (the kept ground): the palette indices a tile's pixels hold (those DecodeTile draws).</summary>
    std::bitset<256> TileColors(std::span<const uint8_t> tile)
    {
        std::bitset<256> colors;

        for (int32_t row = 0; row < tile[2]; ++row)
        {
            const uint32_t offset = TileOffset(tile, row);
            const int32_t length = static_cast<int32_t>(TileOffset(tile, row + 1) - offset);

            for (int32_t i = 0; i < length - 1; ++i)
            {
                colors.set(tile[offset + 1 + i]);
            }
        }

        return colors;
    }

    /// <summary>
    /// Port-only (HD ground's seam pass, draw.pshader's PatchMask): with HD ground, the terrain atlas's columns past each
    /// tile's place, which hold its rows' runs (a tile's row is one run, DecodeTile's): red the first column drawn, green
    /// the last (first past last: none). A column is a byte, so a tile with HD ground is at most TerrainRunWidest wide.
    /// </summary>
    constexpr int32_t TerrainRunColumns = 1;
    constexpr int32_t TerrainRunWidest = UINT8_MAX + 1;

    /// <summary>
    /// Writes the rows' runs of a tile <paramref name="width"/> pixels wide into the last column of
    /// <paramref name="texels"/>, where DecodeTile decoded it as <paramref name="height"/> rows of width +
    /// TerrainRunColumns value + state pairs.
    /// </summary>
    void WriteTileRuns(int32_t width, int32_t height, std::span<uint8_t> texels)
    {
        const int32_t stride = width + TerrainRunColumns;

        for (int32_t y = 0; y < height; ++y)
        {
            const std::span<uint8_t> line = texels.subspan(static_cast<size_t>(y) * stride * 2);
            int32_t first = UINT8_MAX;
            int32_t last = 0;

            for (int32_t x = 0; x < width; ++x)
            {
                if (line[x * 2 + 1] != 0)
                {
                    first = std::min(first, x);
                    last = std::max(last, x);
                }
            }

            line[width * 2] = static_cast<uint8_t>(first);
            line[width * 2 + 1] = static_cast<uint8_t>(last);
        }
    }

    /// <summary>Port-only (the kept ground): terrain.vshader's SeamReach, how far the seam pass reaches past a tile.</summary>
    constexpr int32_t TerrainSeamReach = 2;
    /// <summary>
    /// Port-only (the kept ground): the most parts a frame draws again (each launches the terrain's grid again; more go
    /// as their bounds), the share of the clip past which it is drawn again whole, and the drawn pixels two parts' bounds
    /// may add to them to be drawn as one.
    /// </summary>
    constexpr size_t MostGroundParts = 8;
    constexpr int64_t WholeGroundPercent = 60;
    constexpr int64_t GroundMergeSlack = 64 * 64;
    /// <summary>Port-only (the kept ground): a cell's draw in the terrain's first pass and its seam pass, and its haze.</summary>
    struct GroundCellDraw
    {
        bool First = false;
        bool Seam = false;
        /// <summary>The corners seen (0-4), or AllHazed for the fill (everything filled).</summary>
        int32_t Seen = 0;

        bool operator==(const GroundCellDraw&) const = default;
    };

    constexpr int32_t AllHazed = -1;

    /// <summary>The quad shader's uniforms (shaders/quad.vshader, cbuffer Quad).</summary>
    struct QuadUniforms
    {
        float Rect[4];
        float TargetSize[4];
    };

    /// <summary>Port-only: the kept ground's copy's uniforms (shaders/ground.pshader, cbuffer Ground).</summary>
    struct GroundUniforms
    {
        int32_t Shift[4];
        int32_t Clip[4];
        int32_t Scale[4];
    };

    /// <summary>The terrain shader's vertex uniforms (shaders/terrain.vshader, cbuffer Terrain).</summary>
    struct TerrainUniforms
    {
        float TargetSize[4];
        int32_t Origin[4];
        int32_t Steps[4];
        int32_t Mesh[4];
        int32_t Grid[4];
        int32_t Corners[4];
        int32_t Clip[4];
        uint32_t Haze[4];
        int32_t Pane[4];
    };

    /// <summary>The address of the frame surface <paramref name="window"/> is (or lies over), or none.</summary>
    MCAddressKey SurfaceKeyOf(const MCWindow& window)
    {
        const std::shared_ptr<MCWindow> surface = MCRenderer::FrameSurfaceOf(window);
        return surface != nullptr ? MCAddressKey::Of(*surface) : MCAddressKey();
    }

    /// <summary>The address of the frame surface an underlay shows, or none.</summary>
    MCAddressKey SourceSurfaceKey(const MCUnderlay& underlay)
    {
        const std::shared_ptr<const MCWindow> source = underlay.Source.lock();
        return source != nullptr ? SurfaceKeyOf(*source) : MCAddressKey();
    }

    /// <summary>The address of the frame surface an underlay lies under, or none.</summary>
    MCAddressKey TargetSurfaceKey(const MCUnderlay& underlay)
    {
        const std::shared_ptr<MCWindow> target = MCRenderer::FrameSurfaceOver(underlay.TargetPixels);
        return target != nullptr ? MCAddressKey::Of(*target) : MCAddressKey();
    }

    /// <summary>The terrain atlas's width (tiles are packed in shelves; its height is what they take).</summary>
    constexpr int32_t TerrainAtlasWidth = 4096;

    /// <summary>
    /// The index shown at (<paramref name="x"/>, <paramref name="y"/>) of the GPU's screen (index
    /// <paramref name="pixel"/>) over an underlay shown in <paramref name="shown"/>: the index; at the key, the world
    /// pixel under the pixel's centre (<paramref name="world"/>'s indices, <paramref name="worldWidth"/> x
    /// <paramref name="worldHeight"/>). The composite shader's rule.
    /// </summary>
    uint8_t ShownIndex(uint8_t pixel, int32_t x, int32_t y, const MCRect& shown, std::span<const uint8_t> world,
                       int64_t worldWidth, int64_t worldHeight)
    {
        if (pixel != MCRenderer::UnderlayKey)
        {
            return pixel;
        }

        const int64_t shownWidth = shown.X1 - shown.X0 + 1;
        const int64_t shownHeight = shown.Y1 - shown.Y0 + 1;
        const auto wx = static_cast<size_t>((2 * (x - shown.X0) + 1) * worldWidth / (2 * shownWidth));
        const auto wy = static_cast<size_t>((2 * (y - shown.Y0) + 1) * worldHeight / (2 * shownHeight));
        return world[wy * static_cast<size_t>(worldWidth) + wx];
    }

    /// <summary>An HD frame's place in one of the terrain's HD atlases (or relief atlases), on a block's corner.</summary>
    struct HdPlace
    {
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Atlas = 0;
    };

    /// <summary>
    /// Places <paramref name="frames"/> (null: none) in shelves across up to HdTerrainAtlases atlases MaxHdAtlasSide
    /// wide (the GPUs' largest texture; a big map's tiles need more than one), each on whole blocks, into
    /// <paramref name="places"/>; gives each atlas's height, or none when they don't fit.
    /// </summary>
    std::optional<std::array<uint32_t, HdTerrainAtlases>> ShelveHdFrames(
        std::span<const std::shared_ptr<const MCHdArt::Frame>> frames, std::vector<HdPlace>& places)
    {
        places.assign(frames.size(), HdPlace{});
        std::array<uint32_t, HdTerrainAtlases> heights{};
        uint32_t atlas = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t rowHeight = 0;

        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (frames[i] == nullptr)
            {
                continue;
            }

            const auto width = static_cast<uint32_t>(MCBc7::Padded(frames[i]->Width));
            const auto height = static_cast<uint32_t>(MCBc7::Padded(frames[i]->Height));

            if (x + width > MaxHdAtlasSide)
            {
                y += rowHeight;
                x = 0;
                rowHeight = 0;
            }

            if (y + height > MaxHdAtlasSide)
            {
                ++atlas;
                x = 0;
                y = 0;
                rowHeight = 0;
            }

            if (atlas >= HdTerrainAtlases || width > MaxHdAtlasSide)
            {
                return std::nullopt;
            }

            places[i] = HdPlace{x, y, atlas};
            x += width;
            rowHeight = std::max(rowHeight, height);
            heights[atlas] = std::max(heights[atlas], y + rowHeight);
        }

        return heights;
    }
}

std::expected<std::unique_ptr<MCVulkanRenderer>, std::string> MCVulkanRenderer::Create(
    std::shared_ptr<SDL_GPUDevice> device)
{
    auto renderer = std::make_unique<MCVulkanRenderer>(Key{});
    renderer->_Device = device;

    auto vertex = MCVulkanShaders::Load(device, MCVulkanShaders::DrawVertex);

    if (!vertex)
    {
        return std::unexpected(vertex.error());
    }

    renderer->_VertexShader = std::move(*vertex);
    auto fragment = MCVulkanShaders::Load(device, MCVulkanShaders::DrawFragment);

    if (!fragment)
    {
        return std::unexpected(fragment.error());
    }

    renderer->_FragmentShader = std::move(*fragment);

    // Port-only: the same for the draws of the original's own kinds alone and of HD art frames alone, and for the
    // terrain layer's.
    for (auto [info, shader] : {std::pair{&MCVulkanShaders::DrawBasicFragment, &renderer->_BasicFragmentShader},
                                std::pair{&MCVulkanShaders::DrawHdFragment, &renderer->_HdFragmentShader},
                                std::pair{&MCVulkanShaders::DrawTerrainFragment, &renderer->_TerrainFragmentShader}})
    {
        auto loaded = MCVulkanShaders::Load(device, *info);

        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }

        *shader = std::move(*loaded);
    }

    // The terrain layer: its own vertex shader, and its own fragment shader (the draws' for its kinds alone).
    auto terrain = MCVulkanShaders::Load(device, MCVulkanShaders::TerrainVertex);

    if (!terrain)
    {
        return std::unexpected(terrain.error());
    }

    renderer->_TerrainShader = std::move(*terrain);

    // Port-only: the kept ground's copy (KeepGround): a rectangle of the target, with its own fragment shader.
    for (auto [info, shader] : {std::pair{&MCVulkanShaders::Quad, &renderer->_QuadShader},
                                std::pair{&MCVulkanShaders::GroundFragment, &renderer->_GroundShader}})
    {
        auto loaded = MCVulkanShaders::Load(device, *info);

        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }

        *shader = std::move(*loaded);
    }

    // A surface's targets: its colours, its indices and (port-only, HD weather) its glints. Opaque draws write all
    // three; blended draws blend their premultiplied colour over the colours and the glints (ONE, ONE_MINUS_SRC_ALPHA)
    // and leave the indices. A shadow (HD lighting) takes the colours' alpha down by its own (ZERO,
    // ONE_MINUS_SRC_ALPHA) and leaves the rest, as an index-only draw leaves all but the indices.
    const auto makePipeline = [&](SDL_GPUShader& vertexShader, bool blend, bool indexOnly = false, bool shadow = false,
                                  bool marks = false, SDL_GPUShader* fragmentShader = nullptr)
    {
        SDL_GPUColorTargetDescription targets[SurfaceTargets]{};
        targets[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        targets[1].format = IndexFormat;
        targets[2].format = GlintFormat;

        if (indexOnly)
        {
            targets[0].blend_state.enable_color_write_mask = true;
            targets[0].blend_state.color_write_mask = 0;
            targets[2].blend_state.enable_color_write_mask = true;
            targets[2].blend_state.color_write_mask = 0;
        }

        if (blend)
        {
            for (SDL_GPUColorTargetBlendState* const colors : {&targets[0].blend_state, &targets[2].blend_state})
            {
                colors->enable_blend = true;
                colors->src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                colors->dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                colors->color_blend_op = SDL_GPU_BLENDOP_ADD;
                colors->src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                colors->dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                colors->alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }

            targets[1].blend_state.enable_color_write_mask = true;
            targets[1].blend_state.color_write_mask = 0;
        }

        if (shadow)
        {
            SDL_GPUColorTargetBlendState& light = targets[0].blend_state;
            light.enable_blend = true;
            light.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            light.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            light.color_blend_op = SDL_GPU_BLENDOP_ADD;
            light.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            light.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            light.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            light.enable_color_write_mask = true;
            light.color_write_mask = SDL_GPU_COLORCOMPONENT_A;
            targets[1].blend_state.enable_color_write_mask = true;
            targets[1].blend_state.color_write_mask = 0;
            targets[2].blend_state.enable_color_write_mask = true;
            targets[2].blend_state.color_write_mask = 0;
        }

        // Port-only: a forest's HD frame (HDSway) writes the sway mark (the indices' alpha) alone, as an HD frame's
        // mark pass (indexOnly too) writes what stands on the ground.
        if (marks)
        {
            targets[1].blend_state.enable_color_write_mask = true;
            targets[1].blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_A;
        }

        SDL_GPUGraphicsPipelineCreateInfo pipeline{};
        pipeline.vertex_shader = &vertexShader;
        pipeline.fragment_shader = fragmentShader != nullptr ? fragmentShader : renderer->_FragmentShader.get();
        pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline.target_info.num_color_targets = SurfaceTargets;
        pipeline.target_info.color_target_descriptions = targets;
        return MCGpuHandle<SDL_GPUGraphicsPipeline>(SDL_CreateGPUGraphicsPipeline(device.get(), &pipeline),
                                                    MCGpuRelease{device});
    };

    const auto makeDrawPipelines = [&](DrawPipelines& pipelines, SDL_GPUShader& fragmentShader)
    {
        SDL_GPUShader& vertexShader = *renderer->_VertexShader;
        pipelines.Opaque = makePipeline(vertexShader, false, false, false, false, &fragmentShader);
        pipelines.Blend = makePipeline(vertexShader, true, false, false, false, &fragmentShader);
        pipelines.IndexOnly = makePipeline(vertexShader, false, true, false, false, &fragmentShader);
        pipelines.BlendMarks = makePipeline(vertexShader, true, false, false, true, &fragmentShader);
        pipelines.IndexMarks = makePipeline(vertexShader, false, true, false, true, &fragmentShader);
    };

    makeDrawPipelines(renderer->_Pipelines[static_cast<size_t>(DrawVariant::Every)], *renderer->_FragmentShader);
    makeDrawPipelines(renderer->_Pipelines[static_cast<size_t>(DrawVariant::Basic)], *renderer->_BasicFragmentShader);
    makeDrawPipelines(renderer->_Pipelines[static_cast<size_t>(DrawVariant::Hd)], *renderer->_HdFragmentShader);
    renderer->_ShadowPipeline = makePipeline(*renderer->_VertexShader, false, false, true);
    renderer->_TerrainPipeline =
        makePipeline(*renderer->_TerrainShader, false, false, false, false, renderer->_TerrainFragmentShader.get());
    renderer->_TerrainSeamPipeline =
        makePipeline(*renderer->_TerrainShader, true, false, false, false, renderer->_TerrainFragmentShader.get());
    renderer->_GroundPipeline =
        makePipeline(*renderer->_QuadShader, false, false, false, false, renderer->_GroundShader.get());
    renderer->_GroundOverPipeline =
        makePipeline(*renderer->_QuadShader, true, false, false, false, renderer->_GroundShader.get());
    renderer->_GroundIndexPipeline =
        makePipeline(*renderer->_QuadShader, false, true, false, false, renderer->_GroundShader.get());

    if (!std::ranges::all_of(renderer->_Pipelines, &DrawPipelines::Made) || renderer->_ShadowPipeline == nullptr ||
        renderer->_TerrainPipeline == nullptr || renderer->_TerrainSeamPipeline == nullptr ||
        renderer->_GroundPipeline == nullptr || renderer->_GroundOverPipeline == nullptr ||
        renderer->_GroundIndexPipeline == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUGraphicsPipeline(draw)"));
    }

    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    renderer->_Nearest =
        MCGpuHandle<SDL_GPUSampler>(SDL_CreateGPUSampler(device.get(), &sampler), MCGpuRelease{device});

    if (renderer->_Nearest == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUSampler"));
    }

    for (auto made : {renderer->EnsureTexture(renderer->_Blank, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                              SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1),
                      renderer->EnsureTexture(renderer->_AlphaTexture, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
                                              SDL_GPU_TEXTUREUSAGE_SAMPLER, AlphaColorCount, 2),
                      renderer->EnsureTexture(renderer->_PaletteTexture, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                              SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, PaletteRows)})
    {
        if (!made)
        {
            return std::unexpected(made.error());
        }
    }

    std::ranges::fill(renderer->QueueUpload(renderer->_Blank.Handle, 0, 0, 1, 1, 4), uint8_t{0});
    return renderer;
}

MCVulkanRenderer::~MCVulkanRenderer()
{
    if (_Device == nullptr)
    {
        return;
    }

    SDL_WaitForGPUIdle(_Device.get());

    for (Surface& surface : _Surfaces)
    {
        Release(surface.Target);
        Release(surface.Index);
        Release(surface.Copy);
        Release(surface.Glints);
    }

    for (Page& page : _Pages)
    {
        Release(page.Texture);
    }

    for (auto& [key, picture] : _Pictures)
    {
        Release(picture.Texture);
    }

    for (auto& [key, picture] : _HdPictures)
    {
        Release(picture.Texture);
        Release(picture.HdColors);
    }

    for (auto& [key, picture] : _SharpGlyphs)
    {
        Release(picture.Texture);
    }

    for (const std::shared_ptr<MCTexture>& texture : MCRenderer::Textures())
    {
        OnTextureReleased(*texture);
    }

    for (Texture& texture : _Retired)
    {
        Release(texture);
    }

    _RetiredTransfers.clear();
    _Uploads.clear();
    _BufferUploads.clear();
    _FramePictures.clear();
    _FrameHdColors.clear();
    Release(_Blank);
    Release(_TableTexture);
    Release(_AlphaTexture);
    Release(_FitTexture);
    Release(_PaletteTexture);
    Release(_Strip);

    _Transfer.reset();
    _DrawBuffer.reset();
    _Nearest.reset();
    ReleaseTerrainMesh();
    _Pipelines = {};
    _ShadowPipeline.reset();
    _TerrainPipeline.reset();
    _TerrainSeamPipeline.reset();
    _VertexShader.reset();
    _FragmentShader.reset();
    _BasicFragmentShader.reset();
    _HdFragmentShader.reset();
    _TerrainFragmentShader.reset();
    _TerrainShader.reset();
}

void MCVulkanRenderer::Release(Texture& texture)
{
    // Released to the device once nothing holds it (an upload or a frame picture of the frame being recorded).
    texture = Texture{};
}

std::expected<void, std::string> MCVulkanRenderer::EnsureTexture(Texture& texture, SDL_GPUTextureFormat format,
                                                                 SDL_GPUTextureUsageFlags usage, uint32_t width,
                                                                 uint32_t height)
{
    if (texture.Handle != nullptr && texture.Width == width && texture.Height == height)
    {
        return {};
    }

    Release(texture);
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    texture.Handle = MCGpuHandle<SDL_GPUTexture>(SDL_CreateGPUTexture(_Device.get(), &info), MCGpuRelease{_Device});

    if (texture.Handle == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUTexture"));
    }

    texture.Width = width;
    texture.Height = height;

    if (MCFrameLog::Enabled())
    {
        MCFrameLog::Note(std::format("GPU texture made, {}x{}", width, height));
    }

    return {};
}

std::span<uint8_t> MCVulkanRenderer::QueueUpload(std::shared_ptr<SDL_GPUTexture> texture, uint32_t x, uint32_t y,
                                                 uint32_t width, uint32_t height, uint32_t bytesPerPixel)
{
    const size_t offset = (_Staging.size() + UploadAlignment - 1) & ~(UploadAlignment - 1);
    const size_t size = static_cast<size_t>(width) * height * bytesPerPixel;
    _Staging.resize(offset + size);
    _Uploads.push_back(Upload{std::move(texture), x, y, width, height, bytesPerPixel, offset});
    return std::span(_Staging).subspan(offset, size);
}

void MCVulkanRenderer::NotSupported(std::string_view command)
{
    if (_Unsupported[std::string(command)]++ == 0)
    {
        SDL_Log("MCVulkanRenderer: %.*s isn't drawn on the GPU yet", static_cast<int>(command.size()), command.data());
    }
}

void MCVulkanRenderer::BeginRecording()
{
    if (_Recording)
    {
        return;
    }

    _Recording = true;

    if (RecordingStarts)
    {
        RecordingStarts();
    }

    // The atlas starts again when it has grown too big (nothing of the last frame is drawn from it any more).
    if (_Pages.size() > MaxPages)
    {
        for (Page& page : _Pages)
        {
            Release(page.Texture);
        }

        _Pages.clear();
        _DataImages.clear();
    }

    // Pictures go when unused for a while, or, oldest first, when they take more than their budget (pictures drawn
    // each frame, such as the shape transforms' work buffer, make a new one every frame).
    TrimPictures(_Pictures, _PictureBytes, PictureBudget, 1);
    TrimPictures(_HdPictures, _HdPictureBytes, HdPictureBudget, HdRgbaBytes);
    TrimPictures(_SharpGlyphs, _SharpGlyphBytes, SharpGlyphBudget, HdRgbaBytes);

    // Row 0 is the identity; rows dropped before the last frame ran are free now.
    if (_Tables.empty())
    {
        _Tables.resize(256);

        for (size_t i = 0; i < 256; ++i)
        {
            _Tables[i] = static_cast<uint8_t>(i);
        }

        _NewRows.push_back(0);
    }

    _FreeRows.insert(_FreeRows.end(), _ReleasedRows.begin(), _ReleasedRows.end());
    _ReleasedRows.clear();
}

auto MCVulkanRenderer::DropPicture(std::unordered_map<uint64_t, Picture>& pictures,
                                   std::unordered_map<uint64_t, Picture>::iterator it, size_t& bytes,
                                   size_t bytesPerPixel) -> std::unordered_map<uint64_t, Picture>::iterator
{
    bytes -= it->second.Bytes != 0
                 ? it->second.Bytes
                 : static_cast<size_t>(it->second.Texture.Width) * it->second.Texture.Height * bytesPerPixel;
    Release(it->second.Texture);
    Release(it->second.HdColors);
    return pictures.erase(it);
}

void MCVulkanRenderer::ForgetHdPicture(uint64_t id)
{
    _HdRefused.erase(id);

    if (const auto found = _HdPictures.find(id); found != _HdPictures.end())
    {
        DropPicture(_HdPictures, found, _HdPictureBytes, HdRgbaBytes);
    }
}

void MCVulkanRenderer::TrimPictures(std::unordered_map<uint64_t, Picture>& pictures, size_t& bytes, size_t budget,
                                    size_t bytesPerPixel)
{
    const auto drop = [&](auto it) { return DropPicture(pictures, it, bytes, bytesPerPixel); };

    if (_Frame % PictureSweepFrames == 0)
    {
        for (auto it = pictures.begin(); it != pictures.end();)
        {
            it = it->second.LastFrame + PictureFrames < _Frame ? drop(it) : std::next(it);
        }
    }

    if (bytes > budget)
    {
        std::vector<std::pair<uint64_t, uint64_t>> byAge;

        for (const auto& [key, picture] : pictures)
        {
            byAge.emplace_back(picture.LastFrame, key);
        }

        std::ranges::sort(byAge);

        for (const auto& [lastFrame, key] : byAge)
        {
            if (bytes <= budget / 2)
            {
                break;
            }

            drop(pictures.find(key));
        }
    }
}

uint16_t MCVulkanRenderer::SurfaceFor(const MCWindow& window)
{
    BeginRecording();
    const std::shared_ptr<MCWindow> surfaceWindow = MCRenderer::FrameSurfaceOf(window);
    const MCWindow& canonical = surfaceWindow != nullptr ? *surfaceWindow : window;
    const MCAddressKey key = MCAddressKey::Of(canonical);
    size_t index = 0;

    while (index < _Surfaces.size() && _Surfaces[index].Key != key)
    {
        ++index;
    }

    if (index == _Surfaces.size())
    {
        _Surfaces.push_back(Surface{});
        _Surfaces.back().Key = key;
        _Surfaces.back().Window = surfaceWindow;
    }

    Surface& surface = _Surfaces[index];
    const auto width = static_cast<uint32_t>(canonical.XMax + 1);
    const auto height = static_cast<uint32_t>(canonical.YMax + 1);

    if (surface.RecordedWidth != width || surface.RecordedHeight != height)
    {
        surface.RecordedWidth = width;
        surface.RecordedHeight = height;
        Record resize;
        resize.Resize = true;
        resize.SurfaceIndex = static_cast<uint16_t>(index);
        _Records.push_back(resize);
    }

    return static_cast<uint16_t>(index);
}

auto MCVulkanRenderer::VariantOf(uint32_t op) -> DrawVariant
{
    const uint32_t kind = op & KindMask;

    if (kind <= KindTexelWalk && (op & SmoothEdges) == 0)
    {
        return DrawVariant::Basic;
    }

    return kind == KindHd || kind == KindHdSmooth ? DrawVariant::Hd : DrawVariant::Every;
}

bool MCVulkanRenderer::FillsSurface(const Record& record, const Surface& surface) const
{
    if (record.Resize || record.TerrainDraw >= 0 || record.Count == 0 || record.Source != SourceKind::None ||
        record.Blend || record.IndexOnly || record.Shadow || record.Marks)
    {
        return false;
    }

    // An opaque fill (no flags: no key, table or blend) over every window pixel the surface's drawn size shows.
    const std::array<int32_t, 20>& draw = _Draws[record.First];
    return static_cast<uint32_t>(draw[8]) == KindFill && draw[0] <= 0 && draw[1] <= 0 &&
           draw[2] >= static_cast<int32_t>(surface.MappedWidth) - 1 &&
           draw[3] >= static_cast<int32_t>(surface.MappedHeight) - 1;
}

bool MCVulkanRenderer::DrawPipelines::Made() const
{
    return Opaque != nullptr && Blend != nullptr && IndexOnly != nullptr && BlendMarks != nullptr &&
           IndexMarks != nullptr;
}

SDL_GPUGraphicsPipeline* MCVulkanRenderer::DrawPipelines::For(const Record& record) const
{
    return record.IndexOnly ? (record.Marks ? IndexMarks.get() : IndexOnly.get())
           : record.Blend   ? (record.Marks ? BlendMarks.get() : Blend.get())
                            : Opaque.get();
}

MCRect MCVulkanRenderer::Clipped(const Surface& surface, const Quad& rect)
{
    return MCRect{std::max(rect[0], 0), std::max(rect[1], 0),
                  std::min(rect[2], static_cast<int32_t>(surface.RecordedWidth) - 1),
                  std::min(rect[3], static_cast<int32_t>(surface.RecordedHeight) - 1)};
}

void MCVulkanRenderer::NoteBlended(uint16_t surfaceIndex, const Quad& rect)
{
    Surface& surface = _Surfaces[surfaceIndex];

    if (const MCRect clipped = Clipped(surface, rect); !Empty(clipped))
    {
        surface.Blended.push_back(clipped);
    }
}

bool MCVulkanRenderer::Add(uint16_t surfaceIndex, SourceKind source, uint32_t sourceIndex, const Quad& rect,
                           const std::optional<Quad>& sourceAt, uint32_t op, uint32_t color, uint32_t before,
                           uint32_t after, bool join)
{
    Surface& surface = _Surfaces[surfaceIndex];
    const MCRect clipped = Clipped(surface, rect);

    if (Empty(clipped))
    {
        return false;
    }

    std::array<int32_t, 20> draw{};
    draw[0] = clipped.X0;
    draw[1] = clipped.Y0;
    draw[2] = clipped.X1;
    draw[3] = clipped.Y1;

    if (sourceAt.has_value())
    {
        // The source pixel of the clipped corner.
        const Quad& from = *sourceAt;
        draw[4] = from[0] + (draw[0] - rect[0]) * from[2];
        draw[5] = from[1] + (draw[1] - rect[1]) * from[3];
        draw[6] = from[2];
        draw[7] = from[3];
    }

    if (_Audited.has_value())
    {
        const uint32_t kind = op & KindMask;
        _Audited->Scaled = DrawnLarger(surfaceIndex);
        _Audited->Pixels += static_cast<int64_t>(draw[2] - draw[0] + 1) * (draw[3] - draw[1] + 1);
        // A table applied to what lies under a fill on a surface drawn larger (a fade: the logistics rows' darkening)
        // recolours the drawn colours at the output's resolution, drawing nothing of the original's pixels.
        const bool recolours = kind == KindFill && (op & TableAfter) != 0 && _Audited->Scaled;
        _Audited->Sharp |= kind == KindHd || kind == KindHdSmooth || kind == KindHdMap || kind == KindSmoothMap ||
                           kind == KindSmoothPolygon || kind == KindSharpGlyph || kind == KindSmoothLine ||
                           kind == KindSmoothEllipse || kind == KindSmoothBar || (op & SmoothEdges) != 0 || recolours;
    }

    const MCRect written{draw[0], draw[1], draw[2], draw[3]};
    const bool blend = (op & (AlphaBlend | TableAfter | SmoothEdges)) != 0 || (op & KindMask) == KindSmoothLine ||
                       (op & KindMask) == KindSmoothEllipse || (op & KindMask) == KindSmoothPolygon;

    // Only a blended draw can show fainter: an opaque one replaces what lies under it.
    if (blend && MCRenderer::DrawingGlow())
    {
        op |= Glow;
    }

    draw[8] = static_cast<int32_t>(op);
    draw[9] = static_cast<int32_t>(color);
    draw[10] = static_cast<int32_t>(before);
    draw[11] = static_cast<int32_t>(after);
    const bool indexOnly = (op & IndexOnly) != 0;
    const bool shadow = (op & KindMask) == KindShadow;
    // A forest's HD frame marks its sway; an HD frame's index-only draw is its mark pass (HdMarks::Standing).
    const bool hdFrame = (op & KindMask) == KindHd || (op & KindMask) == KindHdSmooth;
    const bool marks = indexOnly ? hdFrame : (op & Sways) != 0 && blend;
    const DrawVariant variant = VariantOf(op);
    // A run of draws with the same target, bindings and pipeline: a copy within the surface only when the draws are
    // one command's and apart (else each must see the ones before).
    const auto continues = [&](const Record& last)
    {
        return !last.Resize && last.TerrainDraw < 0 && last.SurfaceIndex == surfaceIndex && last.Source == source &&
               last.SourceIndex == sourceIndex && last.Blend == blend && last.IndexOnly == indexOnly &&
               last.Shadow == shadow && last.Marks == marks && last.Variant == variant &&
               (join || (!last.ReadsCopy && source != SourceKind::TargetCopy)) &&
               last.First + last.Count == _Draws.size();
    };

    const bool extends = !_Records.empty() && continues(_Records.back());

    if (!extends)
    {
        Record fresh;
        fresh.SurfaceIndex = surfaceIndex;
        fresh.Source = source;
        fresh.SourceIndex = sourceIndex;
        fresh.Blend = blend;
        fresh.IndexOnly = indexOnly;
        fresh.Shadow = shadow;
        fresh.Marks = marks;
        fresh.Variant = variant;
        fresh.First = static_cast<uint32_t>(_Draws.size());
        _Records.push_back(fresh);
    }

    Record& record = _Records.back();

    // Mirror mode leaves the blended pixels out of its comparison (an HD art frame's leave the indices under them
    // the original's).
    // An HD frame's or a smooth shape's colour pass leaves the indices an index-only pass wrote: they still compare.
    const uint32_t kind = op & KindMask;
    const bool keepsIndices = kind == KindHd || kind == KindHdSmooth || kind == KindHdMap || kind == KindSmoothMap ||
                              kind == KindSmoothPolygon || kind == KindSmoothLine || kind == KindSmoothEllipse;

    if (blend && !keepsIndices)
    {
        surface.Blended.push_back(written);
    }

    if (source == SourceKind::TargetCopy)
    {
        // The source rectangle of the target, read from the copy.
        const int32_t width = draw[2] - draw[0];
        const int32_t height = draw[3] - draw[1];
        const MCRect read{std::min(draw[4], draw[4] + width * draw[6]), std::min(draw[5], draw[5] + height * draw[7]),
                          std::max(draw[4], draw[4] + width * draw[6]), std::max(draw[5], draw[5] + height * draw[7])};
        record.ReadsCopy = true;
        record.Reads = Union(record.Reads, read);
    }

    record.Writes = Union(record.Writes, written);
    ++record.Count;
    _Draws.push_back(draw);
    return true;
}

bool MCVulkanRenderer::AddSpan(uint16_t surface, const SourceRef& source, const Quad& at, const Quad& from, uint32_t op,
                               uint32_t color, uint32_t before, uint32_t after, const MCSpan& span, bool join)
{
    if (!Add(surface, source.Kind, source.Index, at, std::nullopt, op, color, before, after, join))
    {
        return false;
    }

    // The span's first column (the values are counted from it, wherever the draw was clipped) and the texture's
    // size, then its values.
    std::array<int32_t, 20>& draw = _Draws.back();
    draw[4] = span.X0;
    draw[6] = from[2];
    draw[7] = from[3];

    if ((op & 15u) == KindGouraud)
    {
        draw[12] = static_cast<int32_t>(span.Value);
        draw[13] = span.Slope;
    }
    else if ((op & 15u) == KindTexelWalk)
    {
        draw[12] = static_cast<int32_t>(span.Texel);
        draw[13] = span.Step0;
        draw[14] = span.StepU;
        draw[15] = span.StepV;
        draw[16] = static_cast<int32_t>(span.UFraction);
        draw[17] = static_cast<int32_t>(span.UStep);
        draw[18] = static_cast<int32_t>(span.VFraction);
        draw[19] = static_cast<int32_t>(span.VStep);
    }

    if (source.Kind == SourceKind::TargetCopy)
    {
        // A walk through the target itself may read any of it.
        Record& record = _Records.back();
        record.ReadsCopy = true;
        record.Reads = MCRect{0, 0, from[2] - 1, from[3] - 1};
    }

    return true;
}

auto MCVulkanRenderer::TableSlotOf(MCColorTable table) -> std::optional<TableSlot>
{
    BeginRecording();
    const MCAddressKey key = MCAddressKey::OfData(table);

    if (const auto found = _TableSlots.find(key); found != _TableSlots.end())
    {
        const TableSlot& slot = found->second;

        // Mirror mode checks that nobody changed the bytes without saying so.
        if (MCRenderer::GpuDrawing() == MCGpuDrawing::Mirror &&
            std::memcmp(&_Tables[static_cast<size_t>(slot.Row) * 256], table.data(), 256) != 0)
        {
            Fatal(0, "MCVulkanRenderer: a colour table changed without MCRenderer::DataChanged");
        }

        return slot;
    }

    const std::optional<MCDataBlock> block = MCRenderer::DataBlockOf(key);

    if (!block.has_value() || block->Kind != MCDataKind::Tables || block->BytesFrom(key) < 256)
    {
        MCRenderer::NoteUnregistered("a colour table");
        return std::nullopt;
    }

    uint32_t row;

    if (!_FreeRows.empty())
    {
        row = _FreeRows.back();
        _FreeRows.pop_back();
    }
    else
    {
        row = static_cast<uint32_t>(_Tables.size() / 256);
        _Tables.resize(_Tables.size() + 256);
    }

    std::ranges::copy(table, _Tables.begin() + static_cast<ptrdiff_t>(row) * 256);
    _NewRows.push_back(row);
    return _TableSlots.emplace(key, TableSlot{row, ++_LastTableId}).first->second;
}

uint32_t MCVulkanRenderer::TableRow(MCColorTable table)
{
    const std::optional<TableSlot> slot = TableSlotOf(table);
    return slot.has_value() ? slot->Row : 0;
}

void MCVulkanRenderer::SetColors(std::span<const SDL_Color> palette, const MCColorCycle& cycle)
{
    if (!palette.empty() &&
        !std::equal(palette.begin(), palette.begin() + 256, _Colors.begin(),
                    [](const SDL_Color& a, const SDL_Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }))
    {
        std::copy(palette.begin(), palette.begin() + 256, _Colors.begin());
        _ColorsChanged = true;
    }

    _Cycle = cycle;
}

void MCVulkanRenderer::UploadAlphaColors()
{
    // Per colour: the kind (row 0), and the premultiplied colour and coverage drawn over the pixel (row 1).
    const std::span<float> out = UploadAs<float>(QueueUpload(_AlphaTexture.Handle, 0, 0, AlphaColorCount, 2, 16));
    std::ranges::fill(out.first(static_cast<size_t>(AlphaColorCount) * 2 * 4), 0.0f);

    for (int32_t color = 0; color < AlphaColorCount; ++color)
    {
        const std::span<float> kind = out.subspan(static_cast<size_t>(color) * 4, 4);
        const std::span<float> blend = out.subspan(static_cast<size_t>(AlphaColorCount + color) * 4, 4);
        const MCAlphaColor& entry = MCAlphaColors[color];

        if (color == 0 || color == 0xff)
        {
            kind[0] = AlphaNothing;
        }
        else if (OpaqueAlphaColor(color))
        {
            kind[0] = AlphaOpaque;
        }
        else
        {
            const MCAlphaBlend drawn = AlphaColorBlend(entry);
            kind[0] = AlphaBlended;
            blend[0] = drawn.R;
            blend[1] = drawn.G;
            blend[2] = drawn.B;
            blend[3] = drawn.Coverage;
        }
    }

    _AlphaColorsChanged = false;
}

std::expected<void, std::string> MCVulkanRenderer::UploadTables()
{
    const auto rows = static_cast<uint32_t>(_Tables.size() / 256);
    bool all = _ColorsChanged;

    // Grown textures start empty: every row goes up again.
    if (_TableTexture.Handle == nullptr || _TableTexture.Height < rows)
    {
        if (rows > MaxTableRows)
        {
            return std::unexpected(
                std::format("MCVulkanRenderer: {} colour tables in use (at most {})", rows, MaxTableRows));
        }

        const uint32_t height = std::bit_ceil(std::max(rows, 64u));

        for (auto made :
             {EnsureTexture(_TableTexture, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, height),
              EnsureTexture(_FitTexture, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1,
                            height)})
        {
            if (!made)
            {
                return made;
            }
        }

        all = true;
    }

    std::vector<uint32_t> changed;

    if (all)
    {
        changed.resize(rows);
        std::iota(changed.begin(), changed.end(), 0u);
    }
    else
    {
        changed = std::move(_NewRows);
        std::ranges::sort(changed);
        changed.erase(std::ranges::unique(changed).begin(), changed.end());
    }

    _NewRows.clear();

    // Each run of consecutive rows in one upload a texture. A colour change refits the rows: their tables stay.
    for (size_t first = 0; first < changed.size();)
    {
        size_t end = first + 1;

        while (end < changed.size() && changed[end] == changed[end - 1] + 1)
        {
            ++end;
        }

        const uint32_t row = changed[first];
        const auto count = static_cast<uint32_t>(end - first);
        std::memcpy(QueueUpload(_TableTexture.Handle, 0, row, 256, count, 1).data(),
                    &_Tables[static_cast<size_t>(row) * 256], static_cast<size_t>(count) * 256);
        const std::span<float> fits = UploadAs<float>(QueueUpload(_FitTexture.Handle, 0, row, 1, count, 16));

        for (uint32_t i = 0; i < count; ++i)
        {
            FitTable(row + i, fits.subspan(static_cast<size_t>(i) * 4).first<4>());
        }

        first = end;
    }

    return {};
}

void MCVulkanRenderer::FitTable(uint32_t row, std::span<float, 4> fit) const
{
    // What table T does to the free colours, fitted as T(c) = c * k + C over them all (one k, a C a channel); drawn
    // as C over the pixel with coverage 1 - k.
    constexpr int32_t count = LastFreeColor - FirstFreeColor + 1;
    const std::span<const uint8_t> table =
        std::span<const uint8_t>(_Tables).subspan(static_cast<size_t>(row) * 256, 256);
    double meanIn[3] = {};
    double meanOut[3] = {};

    for (int32_t i = FirstFreeColor; i <= LastFreeColor; ++i)
    {
        const SDL_Color& in = _Colors[i];
        const SDL_Color& mapped = _Colors[table[i]];
        meanIn[0] += in.r;
        meanIn[1] += in.g;
        meanIn[2] += in.b;
        meanOut[0] += mapped.r;
        meanOut[1] += mapped.g;
        meanOut[2] += mapped.b;
    }

    for (size_t c = 0; c < 3; ++c)
    {
        meanIn[c] /= count * 255.0;
        meanOut[c] /= count * 255.0;
    }

    double covariance = 0.0;
    double variance = 0.0;

    for (int32_t i = FirstFreeColor; i <= LastFreeColor; ++i)
    {
        const SDL_Color& in = _Colors[i];
        const SDL_Color& mapped = _Colors[table[i]];
        const double x[3] = {in.r / 255.0, in.g / 255.0, in.b / 255.0};
        const double y[3] = {mapped.r / 255.0, mapped.g / 255.0, mapped.b / 255.0};

        for (size_t c = 0; c < 3; ++c)
        {
            covariance += (x[c] - meanIn[c]) * (y[c] - meanOut[c]);
            variance += (x[c] - meanIn[c]) * (x[c] - meanIn[c]);
        }
    }

    const double k = std::clamp(variance > 0.0 ? covariance / variance : 1.0, 0.0, 1.0);

    for (size_t c = 0; c < 3; ++c)
    {
        fit[c] = static_cast<float>(std::clamp(meanOut[c] - k * meanIn[c], 0.0, 1.0));
    }

    fit[3] = static_cast<float>(1.0 - k);
}

auto MCVulkanRenderer::AtlasFor(int32_t width, int32_t height, const std::function<void(std::span<uint8_t>)>& decode)
    -> std::optional<AtlasPlace>
{
    if (width <= 0 || height <= 0 || width > PageSize || height > PageSize)
    {
        return std::nullopt;
    }

    if (!_Pages.empty() && _Pages.back().ShelfX + width > PageSize)
    {
        // A new shelf.
        Page& last = _Pages.back();
        last.ShelfY += last.ShelfHeight;
        last.ShelfX = 0;
        last.ShelfHeight = 0;
    }

    if (_Pages.empty() || _Pages.back().ShelfY + height > PageSize)
    {
        _Pages.push_back(Page{});

        if (!EnsureTexture(_Pages.back().Texture, SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           PageSize, PageSize))
        {
            _Pages.pop_back();
            return std::nullopt;
        }
    }

    Page& page = _Pages.back();
    const AtlasPlace place{static_cast<uint32_t>(_Pages.size() - 1), page.ShelfX, page.ShelfY};
    page.ShelfX += width;
    page.ShelfHeight = std::max(page.ShelfHeight, height);
    decode(QueueUpload(page.Texture.Handle, static_cast<uint32_t>(place.X), static_cast<uint32_t>(place.Y),
                       static_cast<uint32_t>(width), static_cast<uint32_t>(height), 2));
    ++_FrameUploads.AtlasImages;
    _FrameUploads.AtlasBytes += static_cast<int64_t>(width) * height * 2;
    return place;
}

auto MCVulkanRenderer::ImageOf(std::span<const uint8_t> data, uint64_t seed, std::string_view what,
                               const std::function<Extent()>& measure) -> MCOptionalRef<DataImage>
{
    BeginRecording();
    const bool mirror = MCRenderer::GpuDrawing() == MCGpuDrawing::Mirror;
    const MCAddressKey key = MCAddressKey::OfData(data);

    if (const auto found = _DataImages.find({key, seed}); found != _DataImages.end())
    {
        DataImage& image = found->second;

        // Mirror mode checks that nobody changed the bytes without saying so.
        if (mirror && std::memcmp(image.Bytes.data(), data.data(), image.Bytes.size()) != 0)
        {
            Fatal(0, std::format("MCVulkanRenderer: {} changed without MCRenderer::DataChanged", what).c_str());
        }

        return image;
    }

    const std::optional<MCDataBlock> block = MCRenderer::DataBlockOf(key);

    if (!block.has_value())
    {
        MCRenderer::NoteUnregistered(what);
        return std::nullopt;
    }

    const Extent extent = measure();

    if (extent.Size > block->BytesFrom(key))
    {
        MCRenderer::NoteUnregistered(std::format("{} reaching past its registered block", what));
        return std::nullopt;
    }

    DataImage image;
    image.Measured = extent;

    if (mirror)
    {
        image.Bytes.assign(data.begin(), data.begin() + static_cast<ptrdiff_t>(extent.Size));
    }

    return _DataImages.emplace(std::pair{key, seed}, std::move(image)).first->second;
}

auto MCVulkanRenderer::AtlasForData(std::span<const uint8_t> data, uint64_t seed, std::string_view what,
                                    const std::function<Extent()>& measure,
                                    const std::function<void(std::span<uint8_t>, const Extent&)>& decode)
    -> MCOptionalRef<DataImage>
{
    const MCOptionalRef<DataImage> found = ImageOf(data, seed, what, measure);

    if (!found.has_value())
    {
        return found;
    }

    DataImage& image = found->get();

    if (image.Place || image.Measured.Width <= 0 || image.Measured.Height <= 0)
    {
        return found;
    }

    const Extent& extent = image.Measured;
    image.Place = AtlasFor(extent.Width, extent.Height,
                           [&](std::span<uint8_t> out)
                           {
                               decode(out, extent);
                               image.Colors = DrawnColors(out);
                           });
    return found;
}

void MCVulkanRenderer::OnDataChanged(MCAddressKey first, size_t size)
{
    _DataImages.erase(_DataImages.lower_bound({first, 0}), _DataImages.lower_bound({first + size, 0}));
    _ShapeHashes.erase(_ShapeHashes.lower_bound(first), _ShapeHashes.lower_bound(first + size));
    _FontLayers.erase(_FontLayers.lower_bound(first), _FontLayers.lower_bound(first + size));

    // Tables reaching into the bytes: their rows stay as they are for the draws recorded, and are reused from the
    // next frame on.
    const auto last = _TableSlots.lower_bound(first + size);

    for (auto it = _TableSlots.lower_bound(MCFirstTableReaching(first)); it != last;)
    {
        _ReleasedRows.push_back(it->second.Row);
        it = _TableSlots.erase(it);
    }
}

std::optional<uint32_t> MCVulkanRenderer::PictureFor(const MCWindow& window)
{
    BeginRecording();

    if (window.Pixels == nullptr)
    {
        return std::nullopt;
    }

    const std::shared_ptr<MCTexture> texture = window.Texture.lock();

    if (texture == nullptr)
    {
        MCRenderer::NoteUnregistered(std::format("a {}x{} window drawn from", window.XMax + 1, window.YMax + 1));
        return std::nullopt;
    }

    // A window over other pixels than its texture's (a window re-pointed without telling it) is its owner's bug.
    if (texture->Pixels.data() != window.Pixels.get() || texture->Width != window.XMax + 1 ||
        texture->Height != window.YMax + 1)
    {
        MCRenderer::NoteUnregistered("a window whose texture is over other pixels");
        return std::nullopt;
    }

    return PictureOf(*texture);
}

std::optional<uint32_t> MCVulkanRenderer::PictureOf(MCTexture& texture)
{
    BeginRecording();

    if (texture.Pixels.data() == nullptr || texture.Width <= 0 || texture.Height <= 0)
    {
        return std::nullopt;
    }

    TextureState& state = StateOf(texture);
    const auto width = static_cast<uint32_t>(texture.Width);
    const auto height = static_cast<uint32_t>(texture.Height);
    const size_t size = static_cast<size_t>(width) * height;
    const bool mirror = MCRenderer::GpuDrawing() == MCGpuDrawing::Mirror;

    if (texture.Dirty && state.Frame == _Frame && state.Copy.Handle != nullptr)
    {
        // A draw of this frame read the copy before the pixels changed, and the frame's uploads all run before its
        // draws: the new pixels go into a picture of their own for the rest of the frame, the copy next frame.
        Texture picture;

        if (!EnsureTexture(picture, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height))
        {
            return std::nullopt;
        }

        std::memcpy(QueueUpload(picture.Handle, 0, 0, width, height, 1).data(), texture.Pixels.data(), size);
        _Retired.push_back(picture);
        ++_FrameUploads.Pictures;
        _FrameUploads.PictureBytes += static_cast<int64_t>(size);
        texture.Dirty = false;
        state.Behind = true;
        state.HdLooked = false;
        state.Index = static_cast<uint32_t>(_FramePictures.size());
        _FramePictures.push_back(picture.Handle);
        return state.Index;
    }

    if (texture.Dirty || state.Behind || state.Copy.Handle == nullptr)
    {
        if (auto uploaded = UploadWhole(texture, state); !uploaded)
        {
            NotSupported(uploaded.error().c_str());
            return std::nullopt;
        }
    }
    else if (mirror && state.Frame != _Frame &&
             (state.Uploaded.size() != size || std::memcmp(state.Uploaded.data(), texture.Pixels.data(), size) != 0))
    {
        // Mirror mode checks that nobody changed a texture's pixels without saying so.
        Fatal(0, std::format("MCVulkanRenderer: a {}x{} texture's pixels changed without MCRenderer::UnlockTexture",
                             width, height)
                     .c_str());
    }

    if (state.Frame != _Frame)
    {
        state.Frame = _Frame;
        state.Index = static_cast<uint32_t>(_FramePictures.size());
        _FramePictures.push_back(state.Copy.Handle);
    }

    return state.Index;
}

MCVulkanRenderer::TextureState& MCVulkanRenderer::StateOf(MCTexture& texture)
{
    if (texture.Hardware == nullptr)
    {
        texture.Hardware = std::make_unique<TextureState>();
    }

    return static_cast<TextureState&>(*texture.Hardware);
}

std::expected<void, std::string> MCVulkanRenderer::UploadWhole(MCTexture& texture, TextureState& state)
{
    if (!texture.Dirty && !state.Behind && state.Copy.Handle != nullptr)
    {
        return {};
    }

    const auto width = static_cast<uint32_t>(texture.Width);
    const auto height = static_cast<uint32_t>(texture.Height);
    const size_t size = static_cast<size_t>(width) * height;

    if (auto made =
            EnsureTexture(state.Copy, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height);
        !made)
    {
        return made;
    }

    std::memcpy(QueueUpload(state.Copy.Handle, 0, 0, width, height, 1).data(), texture.Pixels.data(), size);
    ++_FrameUploads.Pictures;
    _FrameUploads.PictureBytes += static_cast<int64_t>(size);
    texture.Dirty = false;
    state.Behind = false;
    state.HdLooked = false;

    if (MCRenderer::GpuDrawing() == MCGpuDrawing::Mirror)
    {
        state.Uploaded.assign(texture.Pixels.data(), texture.Pixels.data() + size);
    }

    return {};
}

std::expected<void, std::string> MCVulkanRenderer::KeepFramePicture(MCTexture& texture, TextureState& state)
{
    if (state.Frame != _Frame)
    {
        return {};
    }

    Texture kept;
    const auto width = static_cast<uint32_t>(texture.Width);
    const auto height = static_cast<uint32_t>(texture.Height);

    if (auto made = EnsureTexture(kept, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height);
        !made)
    {
        return made;
    }

    _Uploads.push_back(Upload{kept.Handle, 0, 0, width, height, 1, 0, nullptr, 0, _FramePictures[state.Index]});
    _FramePictures[state.Index] = kept.Handle;
    _Retired.push_back(kept);
    state.Frame = ~0ull;
    return {};
}

std::expected<std::span<uint8_t>, std::string> MCVulkanRenderer::MapStream(MCTexture& texture, TextureState& state)
{
    const MCRect& rect = texture.Locked;
    const auto size = static_cast<uint32_t>((rect.Y1 - rect.Y0 + 1) * texture.Width);

    if (state.Stream == nullptr || state.StreamSize < size)
    {
        if (state.Stream != nullptr)
        {
            _RetiredTransfers.push_back(std::move(state.Stream));
            state.Stream = nullptr;
            state.StreamSize = 0;
        }

        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<uint32_t>(texture.Width * texture.Height);
        state.Stream = MCGpuHandle<SDL_GPUTransferBuffer>(SDL_CreateGPUTransferBuffer(_Device.get(), &info),
                                                          MCGpuRelease{_Device});

        if (state.Stream == nullptr)
        {
            return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer(stream)"));
        }

        state.StreamSize = info.size;
    }

    // A second frame before the first went up replaces it: the memory isn't in use yet, so it is written over.
    if (state.StreamFrame == _Frame)
    {
        _Uploads[state.StreamUpload].Texture = nullptr;
        state.StreamFrame = ~0ull;
    }

    void* const mapped = SDL_MapGPUTransferBuffer(_Device.get(), state.Stream.get(), true);

    if (mapped == nullptr)
    {
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer(stream)"));
    }

    return std::span(static_cast<uint8_t*>(mapped), state.StreamSize);
}

std::span<uint8_t> MCVulkanRenderer::OnLockStream(MCTexture& texture)
{
    BeginRecording();
    auto mapped = MapStream(texture, StateOf(texture));

    if (!mapped)
    {
        Fatal(0, std::format("MCVulkanRenderer: a movie frame can't be locked: {}", mapped.error()).c_str());
    }

    return *mapped;
}

void MCVulkanRenderer::OnUnlockStream(MCTexture& texture, std::span<const uint8_t> written)
{
    BeginRecording();
    TextureState& state = StateOf(texture);
    const MCRect& rect = texture.Locked;
    const auto width = static_cast<uint32_t>(rect.X1 - rect.X0 + 1);
    const auto height = static_cast<uint32_t>(rect.Y1 - rect.Y0 + 1);
    const auto pitch = static_cast<size_t>(texture.Width);

    // Mirror mode: the pixels were written into the texture's memory; they go up through the same upload memory.
    if (!written.empty())
    {
        auto mapped = MapStream(texture, state);

        if (!mapped)
        {
            Fatal(0, std::format("MCVulkanRenderer: a movie frame can't be locked: {}", mapped.error()).c_str());
        }

        for (uint32_t row = 0; row < height; ++row)
        {
            std::ranges::copy(written.subspan(row * pitch, width),
                              mapped->begin() + static_cast<ptrdiff_t>(row * pitch));
        }
    }

    SDL_UnmapGPUTransferBuffer(_Device.get(), state.Stream.get());

    // Earlier draws of the frame keep the old frame; the rest of the texture (all but the rectangle) is what its
    // pixels hold.
    auto kept = KeepFramePicture(texture, state);

    if (kept)
    {
        kept = UploadWhole(texture, state);
    }

    if (!kept)
    {
        Fatal(0, std::format("MCVulkanRenderer: a movie frame can't be uploaded: {}", kept.error()).c_str());
    }

    state.StreamFrame = _Frame;
    state.StreamUpload = _Uploads.size();
    _Uploads.push_back(Upload{state.Copy.Handle, static_cast<uint32_t>(rect.X0), static_cast<uint32_t>(rect.Y0), width,
                              height, 1, 0, state.Stream, static_cast<uint32_t>(pitch)});
    ++_FrameUploads.MovieFrames;
    _FrameUploads.MovieBytes += static_cast<int64_t>(width) * height;

    if (!written.empty() && state.Uploaded.size() == pitch * static_cast<size_t>(texture.Height))
    {
        for (uint32_t row = 0; row < height; ++row)
        {
            const size_t at = (rect.Y0 + row) * pitch + rect.X0;
            std::ranges::copy(written.subspan(row * pitch, width), state.Uploaded.begin() + static_cast<ptrdiff_t>(at));
        }
    }
}

uint32_t MCVulkanRenderer::UsePicture(Picture& picture)
{
    picture.LastFrame = _Frame;
    const auto index = static_cast<uint32_t>(_FramePictures.size());
    _FramePictures.push_back(picture.Texture.Handle);

    if (picture.HdColors.Handle != nullptr)
    {
        _FrameHdColors[index] = picture.HdColors.Handle;
    }

    return index;
}

std::optional<uint32_t> MCVulkanRenderer::PictureForKey(uint64_t key, uint32_t width, uint32_t height,
                                                        const std::function<void(std::span<uint8_t>)>& fill)
{
    BeginRecording();
    const size_t size = static_cast<size_t>(width) * height;
    auto found = _Pictures.find(key);

    if (found == _Pictures.end())
    {
        Picture picture;

        if (!EnsureTexture(picture.Texture, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, width,
                           height))
        {
            return std::nullopt;
        }

        fill(QueueUpload(picture.Texture.Handle, 0, 0, width, height, 1));
        found = _Pictures.emplace(key, picture).first;
        _PictureBytes += size;
        ++_FrameUploads.Pictures;
        _FrameUploads.PictureBytes += static_cast<int64_t>(size);
    }

    return UsePicture(found->second);
}

auto MCVulkanRenderer::SourceFor(uint16_t target, const MCWindow& window) -> std::optional<SourceRef>
{
    if (const std::shared_ptr<MCWindow> surface = MCRenderer::FrameSurfaceOf(window); surface != nullptr)
    {
        // A surface: read from the target's copy when it is the target (the pixels as they were before the draw).
        const uint16_t index = SurfaceFor(*surface);
        return SourceRef{index == target ? SourceKind::TargetCopy : SourceKind::Surface, index};
    }

    const std::optional<uint32_t> picture = PictureFor(window);

    if (!picture)
    {
        return std::nullopt;
    }

    return SourceRef{SourceKind::Picture, *picture};
}

// Commands ---------------------------------------------------------------------------------------------------------

MCVulkanRenderer::AuditScope::AuditScope(MCVulkanRenderer& renderer, std::string_view command, const MCWindow& target,
                                         std::span<const uint8_t> data, int32_t frame, const MCWindow* from)
    : _Renderer(renderer)
{
    if (renderer.Audit && !renderer._Audited.has_value())
    {
        _Outermost = true;
        renderer._Audited = AuditedDraw{command, &target, false, data, frame, from, 0, false};
    }
}

MCVulkanRenderer::AuditScope::~AuditScope()
{
    if (_Outermost && _Renderer._Audited.has_value())
    {
        const AuditedDraw audited = *_Renderer._Audited;
        _Renderer._Audited.reset();
        _Renderer.Audit(audited);
    }
}

void MCVulkanRenderer::Clear(MCWindow& target, const MCRect& rect, uint8_t color)
{
    const AuditScope audit(*this, "Clear", target);
    const uint16_t surface = SurfaceFor(target);
    const Quad at = {rect.X0, rect.Y0, rect.X1, rect.Y1};
    Add(surface, SourceKind::None, 0, at, std::nullopt, KindFill, color, 0, 0);
}

void MCVulkanRenderer::Hash(MCWindow& target, const MCRect& rect, uint8_t color)
{
    const AuditScope audit(*this, "Hash", target);
    const uint16_t surface = SurfaceFor(target);
    const Quad at = {rect.X0, rect.Y0, rect.X1, rect.Y1};

    // The pattern counts from the rectangle's left column and bottom row, kept apart from the clipped rectangle.
    if (Add(surface, SourceKind::None, 0, at, std::nullopt, KindHash, color, 0, 0))
    {
        _Draws.back()[12] = rect.X0;
        _Draws.back()[13] = rect.Y1;
    }
}

void MCVulkanRenderer::Copy(MCWindow& target, const MCCopyCommand& command)
{
    const AuditScope audit(*this, "Copy", target, {}, -1, &command.Source.get());
    const uint16_t surface = SurfaceFor(target);
    const MCRect& rect = command.SourceRect;
    const Quad at = {command.X, command.Y, command.X + rect.X1 - rect.X0, command.Y + rect.Y1 - rect.Y0};
    const Quad from = {rect.X0, rect.Y0, 1, 1};
    const uint32_t op = KindTexture | OpaqueSource | (command.ColorKey ? ColorKey : 0);
    const uint32_t color = static_cast<uint32_t>(command.Key) << 8;

    // A copy within a surface reads the target's copy: the pixels as they were, as the software renderer's ordered
    // copy reads them.
    const std::optional<SourceRef> source = SourceFor(surface, command.Source.get());

    if (!source)
    {
        NotSupported("Copy from a window without pixels");
        return;
    }

    // Port-only: on a sharp screen, a frame shown over the source's pixels (a movie's HD frame) is drawn over the part
    // of the copy it covers, blended after the pixels themselves, which keep their indices.
    if (const std::optional<ShownHdPart> shown = ShownHdPartOf(surface, command))
    {
        Add(surface, source->Kind, source->Index, at, from, op, color, 0, 0);
        AddHd(surface, *shown->Frame, shown->At, shown->From, std::nullopt,
              command.ColorKey ? std::optional(command.Key) : std::nullopt, HdColours::AsMade);
        return;
    }

    // Port-only: on a sharp screen, a picture the HD art pack has is shown from the pack; the copy keeps the indices.
    if (const std::shared_ptr<const MCHdArt::Frame> hd = HdPictureFor(surface, command.Source.get());
        hd != nullptr && AddHd(surface, *hd, at, from, std::nullopt,
                               command.ColorKey ? std::optional(command.Key) : std::nullopt, HdColours::AsMade))
    {
        Add(surface, source->Kind, source->Index, at, from, op | IndexOnly, color, 0, 0);
        return;
    }

    Add(surface, source->Kind, source->Index, at, from, op, color, 0, 0);
}

std::shared_ptr<const MCHdArt::Frame> MCVulkanRenderer::HdPictureFor(uint16_t target, const MCWindow& window)
{
    return SharpScreenSurface(target) ? HdPicture(window) : nullptr;
}

auto MCVulkanRenderer::ShownHdPartOf(uint16_t surface, const MCCopyCommand& command) -> std::optional<ShownHdPart>
{
    const std::shared_ptr<MCTexture> texture = command.Source.get().Texture.lock();

    if (!SharpScreenSurface(surface) || texture == nullptr || texture->Hardware == nullptr)
    {
        return std::nullopt;
    }

    const TextureState& state = StateOf(*texture);

    if (state.ShownHd == nullptr)
    {
        return std::nullopt;
    }

    // The copied rectangle within the frame's, in the source's pixels (rectangles inclusive).
    const MCRect& rect = command.SourceRect;
    const int32_t left = std::max(rect.X0, state.ShownHdX);
    const int32_t top = std::max(rect.Y0, state.ShownHdY);
    const int32_t right = std::min(rect.X1, state.ShownHdX + state.ShownHd->OriginalWidth() - 1);
    const int32_t bottom = std::min(rect.Y1, state.ShownHdY + state.ShownHd->OriginalHeight() - 1);

    if (left > right || top > bottom)
    {
        return std::nullopt;
    }

    return ShownHdPart{state.ShownHd,
                       {command.X + left - rect.X0, command.Y + top - rect.Y0, command.X + right - rect.X0,
                        command.Y + bottom - rect.Y0},
                       {left - state.ShownHdX, top - state.ShownHdY, 1, 1}};
}

std::shared_ptr<const MCHdArt::Frame> MCVulkanRenderer::HdPicture(const MCWindow& window)
{
    if (!MCHdArt::Active() || MCRenderer::FrameSurfaceOf(window) != nullptr)
    {
        return nullptr;
    }

    const std::shared_ptr<MCTexture> texture = window.Texture.lock();

    if (texture == nullptr || texture->Hardware == nullptr || texture->CpuStale)
    {
        return nullptr;
    }

    TextureState& state = StateOf(*texture);

    if (!state.HdLooked || state.HdPack != MCHdArt::PackVersion())
    {
        state.HdLooked = true;
        state.HdPack = MCHdArt::PackVersion();
        state.Hd = MCHdArt::FindPicture(texture->Pixels, texture->Width, texture->Height);
    }

    return state.Hd;
}

std::shared_ptr<const MCHdArt::Frame> MCVulkanRenderer::HdShape(std::span<const uint8_t> table, int32_t frame,
                                                                int32_t width, int32_t height, int32_t step)
{
    if (!MCHdArt::Active())
    {
        return nullptr;
    }

    if (MCHdArt::AttachedFolder(table.data()).has_value())
    {
        return MCHdArt::Find(table.data(), frame, width, height, step);
    }

    // The table's bytes to the end of its block: the whole file or packet its loader read.
    const MCAddressKey key = MCAddressKey::OfData(table);
    const auto [found, fresh] = _ShapeHashes.try_emplace(key);

    if (fresh)
    {
        if (const std::optional<MCDataBlock> block = MCRenderer::DataBlockOf(key);
            block.has_value() && block->Kind == MCDataKind::Shapes)
        {
            found->second = MCHdArt::ShapeTableHash(std::span(table.data(), block->BytesFrom(key)));
        }
    }

    return found->second.has_value() ? MCHdArt::FindShape(*found->second, frame, width, height, step) : nullptr;
}

auto MCVulkanRenderer::HdInBetween(std::span<const uint8_t> table, int32_t shapeNum, const MCHdArt::Frame& hd)
    -> std::optional<HdInBetweenFrame>
{
    const std::optional<MCInBetween>& inBetween = MCRenderer::DrawingInBetween();
    const int32_t count = VfxShapeCount(table);

    if (!inBetween.has_value() || inBetween->Frame != shapeNum || shapeNum < 0 || shapeNum >= count ||
        inBetween->Next < 0 || inBetween->Next >= count)
    {
        return std::nullopt;
    }

    const auto shown = [this](HdInBetweenFrame found) -> std::optional<HdInBetweenFrame>
    {
        if (found.Frame == nullptr || !HdPictureOf(*found.Frame).has_value())
        {
            return std::nullopt;
        }

        return found;
    };

    // A mech's legs' turn middle: where it lies, the draw says.
    if (const std::optional<MCInBetweenBounds>& bounds = inBetween->Bounds; bounds.has_value())
    {
        return shown(HdInBetweenFrame{HdShape(table, shapeNum, bounds->Width, bounds->Height, inBetween->Step),
                                      bounds->FrameX, bounds->FrameY, bounds->Width, bounds->Height});
    }

    // An effect's in-between: its frame holds its blended pixels.
    if (!hd.Blended)
    {
        return std::nullopt;
    }

    const size_t frameAt = MCShapeOffset(table, shapeNum);
    const size_t nextAt = MCShapeOffset(table, inBetween->Next);

    if (table.size() < frameAt + ShapeHeaderBytes || table.size() < nextAt + ShapeHeaderBytes)
    {
        return std::nullopt;
    }

    // The bounds of the frame and of the next, together.
    const std::span<const uint8_t> frame = table.subspan(frameAt);
    const std::span<const uint8_t> next = table.subspan(nextAt);
    const int32_t xMin = std::min(MCRead32(frame, ShapeXMinAt), MCRead32(next, ShapeXMinAt));
    const int32_t yMin = std::min(MCRead32(frame, ShapeYMinAt), MCRead32(next, ShapeYMinAt));
    const int32_t xMax = std::max(MCRead32(frame, ShapeXMaxAt), MCRead32(next, ShapeXMaxAt));
    const int32_t yMax = std::max(MCRead32(frame, ShapeYMaxAt), MCRead32(next, ShapeYMaxAt));

    if (xMax < xMin || yMax < yMin)
    {
        return std::nullopt;
    }

    return shown(HdInBetweenFrame{HdShape(table, shapeNum, xMax - xMin + 1, yMax - yMin + 1, inBetween->Step),
                                  MCRead32(frame, ShapeXMinAt) - xMin, MCRead32(frame, ShapeYMinAt) - yMin,
                                  xMax - xMin + 1, yMax - yMin + 1});
}

auto MCVulkanRenderer::BlitPlace(const MCAlphaBlitCommand& blit, int32_t width, int32_t height,
                                 std::string_view command) -> std::optional<std::pair<Quad, Quad>>
{
    // Where the blit's first pixel lies in the picture, and its steps: the bytes CopySprite reads, as coordinates
    // (each run stays within a row of the picture).
    const int32_t rows = blit.FullSize ? blit.Rows : static_cast<int32_t>(static_cast<uint32_t>(blit.Rows) >> 1);
    const int32_t columns =
        blit.FullSize ? blit.Columns : static_cast<int32_t>(static_cast<uint32_t>(blit.Columns) >> 1);
    const int32_t stepX = (blit.FullSize ? 1 : 2) * (blit.Mirror ? -1 : 1);
    const int32_t stepY = blit.FullSize ? 1 : 2;
    const intptr_t first = blit.Offset + (blit.Mirror ? static_cast<intptr_t>(stepY) * width - 1 : 0);
    const auto x0 = static_cast<int32_t>(first >= 0 ? first % width : width - 1 - (-first - 1) % width);
    const auto y0 = static_cast<int32_t>((first - x0) / width);
    const int32_t lastX = x0 + (columns - 1) * stepX;
    const int32_t lastY = y0 + (rows - 1) * stepY;

    if (rows <= 0 || columns <= 0)
    {
        return std::nullopt;
    }

    // CopySprite's clipping keeps every pixel read inside the picture (the mirrored half size reads the odd rows).
    if (std::min(x0, lastX) < 0 || std::max(x0, lastX) >= width || y0 < 0 || lastY >= height)
    {
        NotSupported(command);
        return std::nullopt;
    }

    const Quad at = {blit.Left, blit.Top, blit.Left + columns - 1, blit.Top + rows - 1};
    const Quad from = {x0, y0, stepX, stepY};
    return std::pair{at, from};
}

void MCVulkanRenderer::BlitPicture(MCWindow& target, const MCAlphaBlitCommand& blit, int32_t width, int32_t height,
                                   std::string_view command, const std::function<std::optional<uint32_t>()>& picture,
                                   bool smoothEdges, BlitPixels pixels)
{
    const std::optional<std::pair<Quad, Quad>> place = BlitPlace(blit, width, height, command);

    if (!place)
    {
        return;
    }

    const std::optional<uint32_t> index = picture();

    if (!index)
    {
        return;
    }

    const uint16_t surface = SurfaceFor(target);
    const uint32_t op = KindTexture | OpaqueSource | AlphaBlend | (smoothEdges ? SmoothEdges : 0u) |
                        (pixels == BlitPixels::Blended ? BlendedOnly : 0u) |
                        (pixels == BlitPixels::OpaqueIndices ? IndexOnly | OpaqueOnly : 0u);

    // A smooth-edged picture's place in the source is the whole picture.
    if (Add(surface, SourceKind::Picture, *index, place->first, place->second, op, 0, 0, 0) && smoothEdges)
    {
        std::array<int32_t, 20>& draw = _Draws.back();
        draw[16] = 0;
        draw[17] = 0;
        draw[18] = width - 1;
        draw[19] = height - 1;
    }
}

void MCVulkanRenderer::AlphaBlit(MCWindow& target, const MCAlphaBlitCommand& command)
{
    const AuditScope audit(*this, "AlphaBlit", target, command.Sprite);

    if (!command.Texture.has_value())
    {
        MCRenderer::NoteUnregistered("a CopySprite bitmap");
        return;
    }

    MCTexture& texture = command.Texture.value().get();

    // The bitmap is the texture's pixels, the texture's width a row.
    if (command.Sprite.data() != texture.Pixels.data() || command.Pitch != texture.Width)
    {
        NotSupported("CopySprite from other pixels than its texture's");
        return;
    }

    BlitPicture(target, command, texture.Width, texture.Height, "CopySprite reading outside its bitmap",
                [&] { return PictureOf(texture); });
}

void MCVulkanRenderer::ShapeBlit(MCWindow& target, const MCShapeBlitCommand& command)
{
    const AuditScope audit(*this, "ShapeBlit", target, command.ShapeTable, command.ShapeNum);

    // Port-only: a unit's sun shadow, from the blit's place (a whole frame at full size: mirrored, its first column
    // drawn is its last), clipped as the blit was.
    if (MCRenderer::DrawingSunShadow())
    {
        const MCAlphaBlitCommand& blit = command.Blit;
        const std::optional<std::pair<Quad, Quad>> place =
            BlitPlace(blit, command.Width, command.Height, "a sun shadow's blit reading outside its shape");

        if (place.has_value() && blit.FullSize)
        {
            const auto& [at, from] = *place;
            const int32_t left = blit.Mirror ? at[0] + from[0] - command.Width + 1 : at[0] - from[0];
            const bool clipKnown = blit.Clip.X1 >= blit.Clip.X0 && blit.Clip.Y1 >= blit.Clip.Y0;
            AddSunShadow(target, command.ShapeTable, command.ShapeNum, left, at[1] - from[1], blit.Mirror,
                         clipKnown ? std::optional(Quad{blit.Clip.X0, blit.Clip.Y0, blit.Clip.X1, blit.Clip.Y1})
                                   : std::nullopt);
        }

        return;
    }

    const std::span<const uint8_t> data =
        command.ShapeTable.subspan(MCShapeOffset(command.ShapeTable, command.ShapeNum) + 0x18);
    const int32_t width = command.Width;
    const int32_t height = command.Height;
    constexpr std::string_view ReadsOutside = "ShapeBlit reading outside the shape's picture";

    // The shape as a picture of indices (FillShape), its colour or blended pixels' blit's source.
    const auto picture = [&]() -> std::optional<uint32_t>
    {
        const MCOptionalRef<DataImage> found =
            ImageOf(data, Seed(ImageKind::ShapeFill, width, height), "a transformed shape",
                    [&]
                    {
                        Extent extent{0, width, height};
                        int32_t widest = 0;
                        extent.Size = ShapeExtent(data, height, widest);
                        return extent;
                    });

        if (!found.has_value())
        {
            return std::nullopt;
        }

        DataImage& image = found->get();

        if (image.PictureId == 0)
        {
            image.PictureId = ++_LastPictureId;
        }

        // The picture of the shape (by its number) through the table (by its slot's number).
        uint64_t key = image.PictureId;

        if (command.Table.has_value())
        {
            const std::optional<TableSlot> slot = TableSlotOf(*command.Table);

            if (!slot.has_value())
            {
                return std::nullopt;
            }

            key = key << 32 | slot->Id;
        }

        return PictureForKey(key, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                             [&](std::span<uint8_t> out) { FillShape(data, height, width, command.Table, out); });
    };

    // The shape's opaque pixels' indices, as the original's transform writes them (its colours' blit, blended, leaves
    // the indices): the composite tells what stands on the ground by them (the water, the relief's light, the rain's
    // wet look).
    const auto indices = [&]
    { BlitPicture(target, command.Blit, width, height, ReadsOutside, picture, false, BlitPixels::OpaqueIndices); };

    // Port-only: the HD art pack's frame instead, where it has one, its edges smooth unless it holds its blended pixels
    // (an effect's). A translucent shape's blended pixels that its frame leaves out are blended first, with smooth
    // edges, so the frame's edge lies over them.
    if (const std::shared_ptr<const MCHdArt::Frame> hd = HdShape(command.ShapeTable, command.ShapeNum, width, height);
        hd != nullptr && HdPictureOf(*hd).has_value())
    {
        const std::optional<std::pair<Quad, Quad>> place = BlitPlace(command.Blit, width, height, ReadsOutside);

        if (!place)
        {
            return;
        }

        const HdEdges edges = HdEdgesOf(*hd, command.ShapeTable);
        const bool smooth = edges == HdEdges::Smooth;
        const bool blendsOwn = !hd->Blended && MCAgShapeIsAlpha(command.ShapeTable, command.ShapeNum);

        if (blendsOwn && smooth)
        {
            BlitPicture(target, command.Blit, width, height, ReadsOutside, picture, true, BlitPixels::Blended);
        }

        // An effect's frame part of the way to its next (or a mech's legs turning) shows the pack's in-between
        // instead, over the two frames' bounds, when the transform draws its whole frame at full size (no clipping to
        // follow): mirrored, the in-between's last column comes first.
        const auto& [at, from] = *place;
        const bool whole = command.Blit.FullSize && command.Blit.Columns == width && command.Blit.Rows == height &&
                           from[1] == 0 && from[3] == 1 && (from[2] == 1 ? from[0] == 0 : from[0] == width - 1);
        const std::optional<HdInBetweenFrame> inBetween =
            whole ? HdInBetween(command.ShapeTable, command.ShapeNum, *hd) : std::nullopt;
        bool drawn = false;

        if (inBetween.has_value())
        {
            const bool mirrored = from[2] < 0;
            const int32_t left =
                mirrored ? at[0] + width - inBetween->Width + inBetween->FrameX : at[0] - inBetween->FrameX;
            const int32_t top = at[1] - inBetween->FrameY;
            const Quad betweenAt = {left, top, left + inBetween->Width - 1, top + inBetween->Height - 1};
            const Quad betweenFrom = {mirrored ? inBetween->Width - 1 : 0, 0, from[2], 1};
            drawn = AddHd(SurfaceFor(target), *inBetween->Frame, betweenAt, betweenFrom, command.Table, std::nullopt,
                          HdColours::Corrected, HdEdgesOf(*inBetween->Frame, command.ShapeTable), HdMarks::Standing);
        }

        if (!drawn)
        {
            AddHd(SurfaceFor(target), *hd, at, from, command.Table, std::nullopt, HdColours::Corrected, edges,
                  HdMarks::Standing);
        }

        // A frame keeping its own edges has its blended pixels blended over it as the original draws them.
        if (blendsOwn && !smooth)
        {
            BlitPicture(target, command.Blit, width, height, ReadsOutside, picture, false, BlitPixels::Blended);
        }

        indices();
        return;
    }

    BlitPicture(target, command.Blit, width, height, ReadsOutside, picture,
                MCHdArt::SmoothEdges(command.ShapeTable.data()));
    indices();
}

auto MCVulkanRenderer::HdEdgesOf(const MCHdArt::Frame& frame, std::span<const uint8_t> table) -> HdEdges
{
    return frame.Blended || MCHdArt::KeepsFrameEdges(table.data()) ? HdEdges::Frame : HdEdges::Smooth;
}

std::optional<uint32_t> MCVulkanRenderer::HdPictureOf(const MCHdArt::Frame& frame)
{
    BeginRecording();
    auto found = _HdPictures.find(frame.Id);

    if (found == _HdPictures.end() && _HdRefused.contains(frame.Id))
    {
        return std::nullopt;
    }

    if (found == _HdPictures.end())
    {
        const auto width = static_cast<uint32_t>(frame.Width);
        const auto height = static_cast<uint32_t>(frame.Height);
        const auto originalWidth = static_cast<uint32_t>(frame.OriginalWidth());
        const auto originalHeight = static_cast<uint32_t>(frame.OriginalHeight());
        const bool bc7 = frame.Format == MCHdArt::ColorFormat::Bc7;
        uint32_t colorWidth = bc7 ? static_cast<uint32_t>(MCBc7::Padded(frame.Width)) : width;
        uint32_t colorHeight = bc7 ? static_cast<uint32_t>(MCBc7::Padded(frame.Height)) : height;

        if (frame.Indices.size() != static_cast<size_t>(originalWidth) * originalHeight * MCHdArt::IndexBytes ||
            frame.Colors.size() != static_cast<size_t>(colorWidth) * colorHeight * (bc7 ? HdBc7Bytes : HdRgbaBytes))
        {
            NotSupported("an HD art frame whose pixels don't match its size");
            return std::nullopt;
        }

        if (!_Bc7.has_value())
        {
            _Bc7 = SDL_GPUTextureSupportsFormat(_Device.get(), SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM,
                                                SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        }

        // A GPU without BC7 gets the colours decoded here.
        const bool sendBc7 = bc7 && *_Bc7;
        std::vector<uint8_t> decoded;
        std::span<const uint8_t> colors = frame.Colors;

        if (bc7 && !sendBc7)
        {
            decoded = MCBc7::Decode(frame.Colors, frame.Width, frame.Height);
            colors = decoded;
            colorWidth = width;
            colorHeight = height;
        }

        Picture picture;
        auto made = EnsureTexture(picture.Texture, SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                  originalWidth, originalHeight);

        if (made)
        {
            made = EnsureTexture(picture.HdColors,
                                 sendBc7 ? SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                 SDL_GPU_TEXTUREUSAGE_SAMPLER, colorWidth, colorHeight);
        }

        if (!made)
        {
            SDL_Log("MCVulkanRenderer: HD art frame %ux%u: %s", width, height, made.error().c_str());
            Release(picture.Texture);
            Release(picture.HdColors);
            _HdRefused.insert(frame.Id);
            return std::nullopt;
        }

        std::ranges::copy(frame.Indices, QueueUpload(picture.Texture.Handle, 0, 0, originalWidth, originalHeight,
                                                     static_cast<uint32_t>(MCHdArt::IndexBytes))
                                             .begin());
        std::ranges::copy(colors, QueueUpload(picture.HdColors.Handle, 0, 0, colorWidth, colorHeight,
                                              sendBc7 ? HdBc7Bytes : HdRgbaBytes)
                                      .begin());
        picture.Bytes = frame.Indices.size() + colors.size();
        found = _HdPictures.emplace(frame.Id, picture).first;
        _HdPictureBytes += picture.Bytes;
        ++_FrameUploads.Pictures;
        _FrameUploads.PictureBytes += static_cast<int64_t>(picture.Bytes);
    }

    return UsePicture(found->second);
}

bool MCVulkanRenderer::AddSharpGlyph(uint16_t surface, std::span<const uint8_t> font, std::span<const uint8_t> glyph,
                                     int32_t columns, int32_t rows, const Quad& at, int32_t sourceX, int32_t sourceY,
                                     const MCColorTable& table)
{
    // Kept by the glyph's bytes (and size), so the same glyph from any font or place is redrawn once; a two-tone glyph
    // by its layers as well (its font's say how its bytes stack).
    uint64_t key = SharpKey(SharpKind::Glyph, glyph, columns, columns, rows);
    std::optional<MCSharpText::MCGlyphLayers> layers;

    if (MCSharpText::Layered(glyph, columns, columns, rows))
    {
        layers = MCSharpText::LayersOf(glyph, columns, columns, rows, FontLayers(font));

        for (const uint8_t byte : layers->Bytes)
        {
            MixHash(key, byte);
        }

        if (const std::optional<MCSharpText::MCDropShadow>& shadow = layers->Shadow; shadow.has_value())
        {
            for (const int32_t offset : {shadow->FromX, shadow->FromY, shadow->ToX, shadow->ToY})
            {
                MixHash(key, static_cast<uint64_t>(offset));
            }
        }
    }

    // One picture per layer (a one-colour glyph's one), each by the glyph's key and its place, all redrawn together
    // the first time one is missing; every layer is up before any is drawn.
    const size_t count = layers.has_value() ? layers->Bytes.size() : 1;
    std::optional<std::vector<std::vector<uint8_t>>> redrawn;
    std::vector<Picture*> pictures;

    for (size_t layer = 0; layer < count; ++layer)
    {
        const auto redrawLayer = [&]
        {
            if (!redrawn.has_value())
            {
                redrawn = layers.has_value()
                              ? MCSharpText::RedrawLayers(glyph, columns, columns, rows, *layers)
                              : std::vector<std::vector<uint8_t>>{MCSharpText::Redraw(glyph, columns, columns, rows)};
            }

            return layer < redrawn->size() ? (*redrawn)[layer] : std::vector<uint8_t>{};
        };

        Picture* picture = SharpPicture((key ^ layer) * HashPrime, redrawLayer, columns, rows);

        if (picture == nullptr)
        {
            return false;
        }

        pictures.push_back(picture);
    }

    // Back to front, each blended over the one before: whether the layer runs on past the glyph's border, and the drawn
    // part's place in the glyph.
    const Quad from = {sourceX, sourceY, 1, 1};

    for (size_t layer = 0; layer < count; ++layer)
    {
        DrawSharpPicture(surface, *pictures[layer], at, from, TableRow(table),
                         layers.has_value() && layers->RunsOn(layer) ? SharpGlyphRunsOn : 0u);
    }

    return true;
}

bool MCVulkanRenderer::AddSharpLines(uint16_t surface, std::span<const uint8_t> art, int32_t columns, int32_t rows,
                                     const Quad& at, const Quad& from, uint32_t tableRow)
{
    const uint64_t key = SharpKey(SharpKind::Lines, art, columns, columns, rows);
    return AddSharpPicture(
        surface, key, [&] { return MCSharpLines::Redraw(art, columns, columns, rows); }, columns, rows, at, from,
        tableRow);
}

uint64_t MCVulkanRenderer::SharpKey(SharpKind kind, std::span<const uint8_t> pixels, int32_t pitch, int32_t columns,
                                    int32_t rows)
{
    // Kept by the pixels' bytes (and size), so the same glyph from any font or place is redrawn once.
    uint64_t key = HashBasis;
    MixHash(key, static_cast<uint64_t>(kind));
    MixHash(key, static_cast<uint64_t>(columns));
    MixHash(key, static_cast<uint64_t>(rows));

    for (int32_t row = 0; row < rows; ++row)
    {
        for (int32_t column = 0; column < columns; ++column)
        {
            const size_t offset = static_cast<size_t>(row) * static_cast<size_t>(pitch) + static_cast<size_t>(column);
            MixHash(key, offset < pixels.size() ? pixels[offset] : MCSharpText::NoInk);
        }
    }

    return key;
}

MCVulkanRenderer::Picture* MCVulkanRenderer::SharpPicture(uint64_t key,
                                                          const std::function<std::vector<uint8_t>()>& redraw,
                                                          int32_t columns, int32_t rows)
{
    BeginRecording();
    auto found = _SharpGlyphs.find(key);

    if (found == _SharpGlyphs.end())
    {
        const std::vector<uint8_t> redrawn = redraw();
        const auto width = static_cast<uint32_t>(columns * MCSharpText::Scale);
        const auto height = static_cast<uint32_t>(rows * MCSharpText::Scale);
        Picture picture;

        if (redrawn.empty() || !EnsureTexture(picture.Texture, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                              SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height))
        {
            return nullptr;
        }

        std::ranges::copy(redrawn, QueueUpload(picture.Texture.Handle, 0, 0, width, height, HdRgbaBytes).begin());
        found = _SharpGlyphs.emplace(key, picture).first;
        _SharpGlyphBytes += redrawn.size();
        ++_FrameUploads.Pictures;
        _FrameUploads.PictureBytes += static_cast<int64_t>(redrawn.size());
    }

    return &found->second;
}

void MCVulkanRenderer::DrawSharpPicture(uint16_t surface, Picture& picture, const Quad& at, const Quad& from,
                                        uint32_t tableRow, uint32_t flags)
{
    // The colour field carries the redrawn picture's scale, and the flags above it (shaders/draw.pshader's
    // KindSharpGlyph).
    Add(surface, SourceKind::Picture, UsePicture(picture), at, from, KindSharpGlyph | AlphaBlend | TableBefore,
        static_cast<uint32_t>(MCSharpText::Scale) | flags, tableRow, 0);
}

bool MCVulkanRenderer::AddSharpPicture(uint16_t surface, uint64_t key,
                                       const std::function<std::vector<uint8_t>()>& redraw, int32_t columns,
                                       int32_t rows, const Quad& at, const Quad& from, uint32_t tableRow)
{
    Picture* picture = SharpPicture(key, redraw, columns, rows);

    if (picture == nullptr)
    {
        return false;
    }

    DrawSharpPicture(surface, *picture, at, from, tableRow, 0);
    return true;
}

const MCSharpText::MCFontLayers& MCVulkanRenderer::FontLayers(std::span<const uint8_t> font)
{
    const MCAddressKey key = MCAddressKey::OfData(font);
    auto found = _FontLayers.find(key);

    if (found == _FontLayers.end())
    {
        found = _FontLayers.emplace(key, MCSharpText::LayersOf(font)).first;
    }

    return found->second;
}

bool MCVulkanRenderer::ScaledHdArt(uint16_t surface) const
{
    return DrawnLarger(surface) && MCHdArt::Active();
}

bool MCVulkanRenderer::DrawnLarger(uint16_t surface) const
{
    const Surface& drawn = _Surfaces[surface];
    return SharpScreenSurface(surface) || drawn.DrawnWidth > drawn.RecordedWidth;
}

bool MCVulkanRenderer::SharpScreenSurface(uint16_t surface) const
{
    return _ScaledScreen && _Surfaces[surface].Key == _ScaledScreen;
}

bool MCVulkanRenderer::AddHd(uint16_t surface, const MCHdArt::Frame& frame, const Quad& at, const Quad& from,
                             const std::optional<MCColorTable>& table, std::optional<uint8_t> key, HdColours colours,
                             HdEdges edges, HdMarks marks)
{
    const std::optional<uint32_t> picture = HdPictureOf(frame);

    if (!picture)
    {
        return false;
    }

    // The colour field carries the frame's scale (shaders/draw.pshader's KindHd), and the key a keyed copy leaves out.
    uint32_t op = (edges == HdEdges::Smooth ? KindHdSmooth : KindHd) | AlphaBlend | (key.has_value() ? ColorKey : 0) |
                  (colours == HdColours::AsMade ? HdAsMade : 0) | (frame.Blended ? HdLight : 0u) | SwaysOf(frame);
    uint32_t before = 0;

    if (table.has_value())
    {
        op |= TableBefore;
        before = TableRow(*table);
    }

    const uint32_t color = static_cast<uint32_t>(frame.Scale) | static_cast<uint32_t>(key.value_or(0)) << 8;

    if (Add(surface, SourceKind::Picture, *picture, at, from, op, color, before, 0))
    {
        ++_HdDrawn;
    }

    // Its mark pass: what stands on the ground wherever the frame shows, past the original's pixels too, which the
    // original's index pass leaves the ground's (draw.pshader's HdPixelAt).
    if (marks == HdMarks::Standing && !frame.Blended && (op & Sways) == 0)
    {
        Add(surface, SourceKind::Picture, *picture, at, from, op | IndexOnly, color, before, 0);
    }

    return true;
}

void MCVulkanRenderer::Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels)
{
    const AuditScope audit(*this, "Write", target);
    const uint16_t surface = SurfaceFor(target);
    const auto count = static_cast<int32_t>(pixels.size());

    // Each piece of the row goes into a row of the frame's strip, drawn from there.
    for (int32_t done = 0; done < count; done += StripWidth)
    {
        const int32_t length = std::min(count - done, StripWidth);
        const uint32_t row = _StripRows++;
        _StripPixels.resize(static_cast<size_t>(_StripRows) * StripWidth);
        std::memcpy(&_StripPixels[static_cast<size_t>(row) * StripWidth], pixels.data() + done,
                    static_cast<size_t>(length));
        const Quad at = {x + done, y, x + done + length - 1, y};
        const Quad from = {0, static_cast<int32_t>(row), 1, 1};
        Add(surface, SourceKind::Strip, 0, at, from, KindTexture | OpaqueSource, 0, 0, 0);
    }
}

void MCVulkanRenderer::Pixel(MCWindow& target, int32_t x, int32_t y, uint8_t color)
{
    const AuditScope audit(*this, "Pixel", target);
    const uint16_t surface = SurfaceFor(target);
    const Quad at = {x, y, x, y};
    Add(surface, SourceKind::None, 0, at, std::nullopt, KindFill, color, 0, 0);
}

void MCVulkanRenderer::Shape(MCWindow& target, const MCShapeCommand& command)
{
    const AuditScope audit(*this, "Shape", target, command.ShapeTable, command.ShapeNum);

    // Port-only: a unit's sun shadow, clipped as the shape is.
    if (MCRenderer::DrawingSunShadow())
    {
        const bool rowsKnown = command.ClipBottom >= command.ClipTop;
        AddSunShadow(target, command.ShapeTable, command.ShapeNum, command.Left, command.Top - command.SkipRows, false,
                     rowsKnown ? std::optional(Quad{command.Lo, command.ClipTop, command.Hi, command.ClipBottom})
                               : std::nullopt);
        return;
    }

    const uint16_t surface = SurfaceFor(target);
    const std::span<const uint8_t> header =
        command.ShapeTable.subspan(MCShapeOffset(command.ShapeTable, command.ShapeNum));
    const std::span<const uint8_t> data = header.subspan(0x18);

    // The whole shape when its header covers the rows drawn (so a shape scrolling past an edge keeps one atlas place).
    const int32_t headerRows = MCRead32(header, 0x14) - MCRead32(header, 0x0c) + 1;
    const int32_t rows = std::max(command.SkipRows + command.Rows, headerRows);
    const MCOptionalRef<DataImage> found = AtlasForData(
        data, Seed(ImageKind::Shape, 0, rows), "a shape",
        [&]
        {
            Extent extent;
            extent.Height = rows;
            extent.Size = ShapeExtent(data, rows, extent.Width);
            return extent;
        },
        [&](std::span<uint8_t> out, const Extent& extent) { DecodeShape(data, rows, extent.Width, out); });

    if (!found.has_value() || found->get().Measured.Width == 0)
    {
        return;
    }

    const DataImage& image = found->get();
    const int32_t width = image.Measured.Width;
    const std::optional<AtlasPlace>& place = image.Place;

    if (!place)
    {
        NotSupported("Shape larger than an atlas page");
        return;
    }

    const Quad at = {std::max(command.Left, command.Lo), command.Top, std::min(command.Left + width - 1, command.Hi),
                     command.Top + command.Rows - 1};
    const Quad from = {place->X + at[0] - command.Left, place->Y + command.SkipRows, 1, 1};

    if (at[2] < at[0])
    {
        return;
    }

    uint32_t op = KindTexture;
    uint32_t before = 0;
    uint32_t after = 0;
    // Port-only: line art (the purchase screen's sold-out outlines) on a sharp screen is redrawn as smooth strokes at
    // the output's resolution (MCSharpLines), in the colours its table gives its bytes; the original's draw keeps the
    // indices.
    bool lineArt = false;

    if ((command.Op == MCShapeOp::Draw || command.Op == MCShapeOp::Xlat) && SharpScreenSurface(surface) &&
        MCHdArt::LineArt(command.ShapeTable.data(), command.ShapeNum))
    {
        std::vector<uint8_t> decoded(static_cast<size_t>(width) * static_cast<size_t>(rows) * DecodedBytes);
        DecodeShape(data, rows, width, decoded);
        std::vector<uint8_t> art(static_cast<size_t>(width) * static_cast<size_t>(rows), MCSharpText::NoInk);

        for (size_t pixel = 0; pixel < art.size(); ++pixel)
        {
            if (decoded[pixel * DecodedBytes + 1] == DecodedDrawn)
            {
                art[pixel] = decoded[pixel * DecodedBytes];
            }
        }

        const Quad artFrom = {at[0] - command.Left, command.SkipRows, 1, 1};
        const uint32_t tableRow = command.Op == MCShapeOp::Xlat ? TableRow(*command.Table) : IdentityRow;
        lineArt = AddSharpLines(surface, art, width, rows, at, artFrom, tableRow);
        op |= lineArt ? IndexOnly : 0u;
    }

    switch (command.Op)
    {
        case MCShapeOp::Draw:
            break;
        case MCShapeOp::Xlat:
        {
            op |= TableBefore;
            before = TableRow(*command.Table);
            break;
        }
        case MCShapeOp::Alpha:
        {
            op |= AlphaBlend;
            break;
        }
        case MCShapeOp::XlatAlpha:
        {
            op |= AlphaBlend | TableAfter;
            after = TableRow(*command.Table);
            break;
        }
        case MCShapeOp::Fill:
        {
            op |= FillSkipped;
            break;
        }
        case MCShapeOp::XlatFill:
        {
            op |= FillSkipped | TableBefore;
            before = TableRow(*command.Table);
            break;
        }
    }

    // Port-only: a smooth-edged draw's place (window pixels and its first source pixel), a window pixel wider each way
    // the pane allows, as its smooth edges reach a little past its pixels (draw.pshader's SmoothCoverage).
    const auto widened = [&](const Quad& rect, const Quad& source)
    {
        const bool rowsKnown = command.ClipBottom >= command.ClipTop;
        const int32_t left = std::max(rect[0] - 1, command.Lo);
        const int32_t top = rowsKnown ? std::max(rect[1] - 1, command.ClipTop) : rect[1];
        const int32_t right = std::min(rect[2] + 1, command.Hi);
        const int32_t bottom = rowsKnown ? std::min(rect[3] + 1, command.ClipBottom) : rect[3];
        return std::pair{Quad{left, top, right, bottom},
                         Quad{source[0] - (rect[0] - left), source[1] - (rect[1] - top), source[2], source[3]}};
    };

    // Port-only: a colour pass with smooth edges, its coverage taken between the shape's own pixels (its place in the
    // atlas).
    const auto addSmooth = [&](const Quad& rect, const Quad& source, uint32_t smoothOp)
    {
        if (Add(surface, SourceKind::Atlas, place->Page, rect, source, smoothOp, 0, before, after))
        {
            std::array<int32_t, 20>& draw = _Draws.back();
            draw[16] = place->X;
            draw[17] = place->Y;
            draw[18] = place->X + width - 1;
            draw[19] = place->Y + rows - 1;
        }
    };

    const bool translucent = command.Op == MCShapeOp::Alpha || command.Op == MCShapeOp::XlatAlpha;

    // Port-only: a shape the HD art pack has a frame of keeps the original's indices (a translucent one's opaque
    // pixels') and is shown from the pack (its bounds: the header's), its edges smooth unless the frame holds its
    // blended pixels (an effect's; drawn through a table, XlatAlpha's haze, they take the table's change of colour:
    // draw.pshader's TableShift). A translucent shape's blended pixels that its frame leaves out (a shadow, glass) are
    // drawn first, with smooth edges, so the frame's edge lies over them.
    if (!lineArt && (command.Op == MCShapeOp::Draw || command.Op == MCShapeOp::Xlat || translucent))
    {
        const int32_t boundsWidth = MCRead32(header, 0x10) - MCRead32(header, 0x08) + 1;

        if (const std::shared_ptr<const MCHdArt::Frame> hd =
                HdShape(command.ShapeTable, command.ShapeNum, boundsWidth, headerRows);
            hd != nullptr && HdPictureOf(*hd).has_value())
        {
            // The frame covers the bounds, which may be wider than the shape's widest row.
            const Quad hdAt = {at[0], at[1], std::min(command.Left + boundsWidth - 1, command.Hi), at[3]};
            const Quad shapeFrom = {at[0] - command.Left, command.SkipRows, 1, 1};
            const HdEdges edges = HdEdgesOf(*hd, command.ShapeTable);
            const bool smooth = edges == HdEdges::Smooth;
            const bool blendsOwn = translucent && !hd->Blended;

            if (blendsOwn && smooth)
            {
                const auto [blendedAt, blendedFrom] = widened(at, from);
                addSmooth(blendedAt, blendedFrom, op | SmoothEdges | BlendedOnly);
            }

            const auto [frameAt, frameFrom] = smooth ? widened(hdAt, shapeFrom) : std::pair{hdAt, shapeFrom};
            AddPaintedShadow(surface, *hd, hdAt[0] - shapeFrom[0], hdAt[1] - shapeFrom[1],
                             Quad{command.Lo, command.ClipTop, command.Hi, command.ClipBottom});
            // A forest's index pass comes first, marking the sway, which its HD frame takes off its painted shadow.
            const uint32_t indexOp = op | IndexOnly | (translucent ? OpaqueOnly : 0u) | SwaysOf(*hd);

            if ((indexOp & Sways) != 0)
            {
                Add(surface, SourceKind::Atlas, place->Page, at, from, indexOp, 0, before, after);
            }

            // An effect's frame part of the way to its next (or a mech's legs turning) shows the pack's in-between
            // there instead (over the two frames' bounds, clipped as the command's rows and columns; widened as the
            // frame for smooth edges), over the frame's own indices.
            const std::optional<HdInBetweenFrame> inBetween = HdInBetween(command.ShapeTable, command.ShapeNum, *hd);
            bool drawn = false;

            if (inBetween.has_value())
            {
                const int32_t betweenX = command.Left - inBetween->FrameX;
                const int32_t betweenY = command.Top - command.SkipRows - inBetween->FrameY;
                const int32_t left = std::max(betweenX, command.Lo);
                const int32_t top = std::max(betweenY, command.ClipTop);
                const int32_t right = std::min(betweenX + inBetween->Width - 1, command.Hi);
                const int32_t bottom = std::min(betweenY + inBetween->Height - 1, command.ClipBottom);
                const HdEdges betweenEdges = HdEdgesOf(*inBetween->Frame, command.ShapeTable);
                const Quad betweenAt = {left, top, right, bottom};
                const Quad betweenFrom = {left - betweenX, top - betweenY, 1, 1};
                const auto [placedAt, placedFrom] = betweenEdges == HdEdges::Smooth ? widened(betweenAt, betweenFrom)
                                                                                    : std::pair{betweenAt, betweenFrom};
                drawn = left <= right && top <= bottom &&
                        AddHd(surface, *inBetween->Frame, placedAt, placedFrom, command.Table, std::nullopt,
                              HdColours::Corrected, betweenEdges, HdMarks::Standing);
            }

            if (!drawn)
            {
                AddHd(surface, *hd, frameAt, frameFrom, command.Table, std::nullopt, HdColours::Corrected, edges,
                      HdMarks::Standing);
            }

            // A translucent shape with no colour it shows opaque keeps no indices: its index pass would draw nothing.
            if ((indexOp & Sways) == 0 && translucent && !AnyOpaqueAlphaColor(image.Colors))
            {
                NoteBlended(surface, at);
            }
            else if ((indexOp & Sways) == 0)
            {
                Add(surface, SourceKind::Atlas, place->Page, at, from, indexOp, 0, before, after);
            }

            // A frame keeping its own edges has its blended pixels blended over it as the original draws them.
            if (blendsOwn && !smooth)
            {
                Add(surface, SourceKind::Atlas, place->Page, at, from, op | BlendedOnly, 0, before, after);
            }

            return;
        }
    }

    // Port-only: a shape with smooth edges (with HD art on: the mechs' shadows) writes its indices as always, then its
    // colour through the blend pipeline with its coverage taken between its own pixels.
    if (command.Op == MCShapeOp::Draw && MCHdArt::SmoothEdges(command.ShapeTable.data()))
    {
        Add(surface, SourceKind::Atlas, place->Page, at, from, op | IndexOnly, 0, before, after);
        addSmooth(at, from, op | SmoothEdges);
        return;
    }

    Add(surface, SourceKind::Atlas, place->Page, at, from, op, 0, before, after);
}

void MCVulkanRenderer::FastShape(MCWindow& target, const MCFastShapeCommand& command)
{
    const AuditScope audit(*this, "FastShape", target, command.Shape);
    const uint16_t surface = SurfaceFor(target);
    const std::span<const uint8_t> shape = command.Shape;
    const int32_t height = MCRead16(shape, 8);
    const int32_t width = MCRead16(shape, 10) + 1;

    // The shape's bytes: its header and row offsets, then the rows' packets, one row after another, so they end where
    // the row that starts last ends.
    const auto rowStart = [&](int32_t row) -> size_t { return MCRead16(shape, 0xc + static_cast<size_t>(row) * 2); };
    const std::optional<size_t> size = FastShapeSize(shape);

    if (!size.has_value())
    {
        Fatal(0, "MCVulkanRenderer: a fast shape runs past its registered block");
    }

    const MCOptionalRef<DataImage> found = AtlasForData(
        shape, Seed(ImageKind::FastShape, width, height), "a fast shape", [&] { return Extent{*size, width, height}; },
        [&](std::span<uint8_t> out, const Extent& extent)
        {
            std::ranges::fill(out.first(static_cast<size_t>(width) * height * 2), uint8_t{0});

            for (int32_t row = 0; row < height; ++row)
            {
                // A row reaching past the last row's end would be outside the bytes the shape is known by.
                const std::optional<size_t> rowBytes = FastShapeRowBytes(shape.subspan(rowStart(row)), width);

                if (!rowBytes.has_value() || rowStart(row) + *rowBytes > extent.Size)
                {
                    Fatal(0, "MCVulkanRenderer: a fast shape's rows are out of order");
                }

                DecodeFastRow(shape.subspan(rowStart(row)), width, out.subspan(static_cast<size_t>(row) * width * 2));
            }
        });

    if (!found.has_value() || found->get().Measured.Height <= 0 || found->get().Measured.Width <= 0)
    {
        return;
    }

    const std::optional<AtlasPlace>& place = found->get().Place;

    if (!place)
    {
        NotSupported("FastShape larger than an atlas page");
        return;
    }

    // Shape columns LeftSkip.. are drawn from ClipX0 when the left is clipped, else from column 0 at StartX; Limit
    // of them a row.
    const int32_t left = command.LeftSkip > 0 ? command.ClipX0 : command.StartX;
    const int32_t column = command.LeftSkip > 0 ? command.LeftSkip : 0;
    const Quad at = {left, command.Top, left + command.Limit - 1, command.Top + command.EndRow - command.FirstRow - 1};
    const Quad from = {place->X + column, place->Y + command.FirstRow, 1, 1};
    uint32_t op = KindTexture;
    uint32_t before = 0;

    if (command.Table.has_value())
    {
        op |= TableBefore | SkipKeyAfterTable;
        before = TableRow(*command.Table);
    }
    else
    {
        op |= SkipRunKey;
    }

    if (command.Alpha)
    {
        op |= AlphaBlend;
    }

    // Port-only: an overlay the HD art pack has a frame of is shown from it, the original's draw keeping its indices; a
    // translucent one's frame holds its blended pixels too (MCHdArt::Frame::Blended), and the original draws nothing.
    // An opaque one's edges are smooth (KindHdSmooth), reaching a window pixel past it each way the pane allows.
    if (const std::shared_ptr<const MCHdArt::Frame> hd = MCHdArt::FindOverlay(shape.first(*size), width, height);
        hd != nullptr && hd->Blended == command.Alpha)
    {
        const HdEdges edges = hd->Blended ? HdEdges::Frame : HdEdges::Smooth;
        Quad frameAt = at;
        Quad frameFrom = {column, command.FirstRow, 1, 1};

        if (edges == HdEdges::Smooth && command.ClipX1 >= command.ClipX0 && command.ClipY1 >= command.ClipY0)
        {
            frameAt = {std::max(at[0] - 1, command.ClipX0), std::max(at[1] - 1, command.ClipY0),
                       std::min(at[2] + 1, command.ClipX1), std::min(at[3] + 1, command.ClipY1)};
            frameFrom = {column - (at[0] - frameAt[0]), command.FirstRow - (at[1] - frameAt[1]), 1, 1};
        }

        AddPaintedShadow(surface, *hd, at[0] - column, at[1] - command.FirstRow,
                         Quad{command.ClipX0, command.ClipY0, command.ClipX1, command.ClipY1});
        // A forest's index pass comes first, marking the sway, which its HD frame takes off its painted shadow.
        const uint32_t sways = command.Alpha ? 0u : SwaysOf(*hd);

        if (sways != 0)
        {
            Add(surface, SourceKind::Atlas, place->Page, at, from, op | IndexOnly | sways, 0, before, 0);
        }

        if (AddHd(surface, *hd, frameAt, frameFrom, command.Table, std::nullopt, HdColours::Corrected, edges))
        {
            if (command.Alpha || sways != 0)
            {
                return;
            }

            op |= IndexOnly;
        }
    }

    Add(surface, SourceKind::Atlas, place->Page, at, from, op, 0, before, 0);
}

void MCVulkanRenderer::Tile(MCWindow& target, const MCTileCommand& command)
{
    const AuditScope audit(*this, "Tile", target, command.Tile);
    const uint16_t surface = SurfaceFor(target);

    // A terrain layer drew this pass's tiles from the mesh.
    if (static_cast<int32_t>(surface) == _TerrainLayerSurface)
    {
        if (_Audited.has_value())
        {
            _Audited->Sharp = HdTerrain();
        }

        return;
    }

    const std::span<const uint8_t> tile = command.Tile;
    const MCOptionalRef<DataImage> found = AtlasForData(
        tile, Seed(ImageKind::Tile, 0, 0), "a terrain tile",
        [&] { return Extent{TileOffset(tile, tile[2]), VfxTileWidth(tile), tile[2]}; },
        [&](std::span<uint8_t> out, const Extent& extent) { DecodeTile(tile, extent.Width, out); });

    if (!found.has_value() || found->get().Measured.Width == 0)
    {
        return;
    }

    const int32_t width = found->get().Measured.Width;
    const std::optional<AtlasPlace>& place = found->get().Place;

    if (!place)
    {
        NotSupported("Tile larger than an atlas page");
        return;
    }

    const int32_t lo = command.Unclipped ? command.Left : std::max(command.Left, command.Lo);
    const int32_t hi = command.Unclipped ? command.Left + width - 1 : std::min(command.Left + width - 1, command.Hi);
    const Quad at = {lo, command.Top, hi, command.Top + command.EndRow - command.FirstRow - 1};
    const Quad from = {place->X + lo - command.Left, place->Y + command.FirstRow, 1, 1};

    if (hi < lo)
    {
        return;
    }

    uint32_t op = KindTexture;
    uint32_t color = 0;
    uint32_t before = 0;

    if (command.Fill)
    {
        op |= UseColor;
        color = 0x10;
    }
    else if (command.Table.has_value())
    {
        op |= TableBefore;
        before = TableRow(*command.Table);
    }

    Add(surface, SourceKind::Atlas, place->Page, at, from, op, color, before, 0);
}

void MCVulkanRenderer::Polygon(MCWindow& target, const MCPolygonCommand& command)
{
    const AuditScope audit(*this, "Polygon", target);

    if (command.Kind == MCPolygonKind::DitheredGouraud || command.Kind == MCPolygonKind::Illuminate)
    {
        NotSupported("Polygon (dithered)");
        return;
    }

    if (command.Vertices.empty())
    {
        return;
    }

    const uint16_t surface = SurfaceFor(target);
    SourceRef source;
    int32_t textureSize[2] = {0, 0};
    uint32_t op = KindFill;
    uint32_t color = 0;
    uint32_t before = 0;
    uint32_t after = 0;

    switch (command.Kind)
    {
        case MCPolygonKind::Flat:
        {
            color = ((static_cast<uint32_t>(command.Vertices[0].C) + 0x8000) >> 16) & 0xff;
            break;
        }
        case MCPolygonKind::Gouraud:
        {
            op = KindGouraud;
            break;
        }
        case MCPolygonKind::Translate:
        {
            op = KindFill | TableAfter;
            after = TableRow(*command.Table);
            break;
        }
        case MCPolygonKind::Map:
        {
            if (!command.Texture.has_value())
            {
                NotSupported("Polygon mapped from a window without pixels");
                return;
            }

            const MCWindow& texture = command.Texture->get();
            const std::optional<SourceRef> found = SourceFor(surface, texture);

            if (!found)
            {
                NotSupported("Polygon mapped from a window without pixels");
                return;
            }

            source = *found;
            textureSize[0] = texture.XMax + 1;
            textureSize[1] = texture.YMax + 1;
            op = KindTexelWalk;

            if ((command.MapFlags & VfxMapXlat) != 0)
            {
                op |= TableBefore;
                before = TableRow(*command.Table);
            }

            if ((command.MapFlags & VfxMapTransparent) != 0)
            {
                op |= SkipKeyAfterTable;
            }

            // Port-only: a picture the HD art pack has (a tactical or briefing map's) is shown from it at the output's
            // resolution, turned and scaled as the polygon maps it; the walk then writes the indices alone. A map read
            // through the lookaside table shows the original.
            if ((command.MapFlags & VfxMapXlat) == 0)
            {
                if (const std::shared_ptr<const MCHdArt::Frame> hd = HdPicture(texture);
                    hd != nullptr && AddHdMap(surface, *hd, command))
                {
                    op |= IndexOnly;
                }

                // Port-only: on a window shown larger, a picture the game draws (the fog of war) mapped with its key
                // transparent has its edges drawn smooth at the output's resolution, as a mech's shadow's are; the
                // walk then writes the indices alone.
                else if ((command.MapFlags & VfxMapTransparent) != 0 && ScaledHdArt(surface) &&
                         MCRenderer::FrameSurfaceOf(texture) != nullptr && AddSmoothMap(surface, source, command))
                {
                    op |= IndexOnly;
                }
            }
            break;
        }
        default:
            break;
    }

    // Port-only: on a window shown larger with HD art on, a flat or Gouraud polygon (a laser's beam) is drawn again at
    // the output's resolution with smooth edges (KindSmoothPolygon); the spans then write the indices alone.
    if ((command.Kind == MCPolygonKind::Flat || command.Kind == MCPolygonKind::Gouraud) && ScaledHdArt(surface) &&
        AddSmoothPolygon(surface, command))
    {
        op |= IndexOnly;
    }

    // The spans never share a pixel: they all read the target's copy taken before the polygon.
    bool join = false;
    MCPolygonSpans(command, _Spans,
                   [&](const MCSpan& span)
                   {
                       const Quad at = {span.X0, span.Y, span.X1, span.Y};
                       const Quad from = {0, 0, textureSize[0], textureSize[1]};

                       if (AddSpan(surface, source, at, from, op, color, before, after, span, join))
                       {
                           join = true;
                       }
                   });
}

namespace
{
    /// <summary>A textured polygon's vertex as the HD map takes it: its window position and its texel, in pixels.</summary>
    using MappedPoint = std::array<float, 4>;

    /// <summary>
    /// The affine map from window to texture positions that three of a textured polygon's vertices give (pixel centres to
    /// texel centres): u = m0 x + m1 y + m2, v = m3 x + m4 y + m5; none when they lie in a line.
    /// </summary>
    std::optional<std::array<float, 6>> TextureMapping(MappedPoint c0, MappedPoint c1, MappedPoint c2)
    {
        constexpr float Centre = 0.5f;
        constexpr float Flat = 1e-6f;

        for (MappedPoint* point : {&c0, &c1, &c2})
        {
            std::ranges::for_each(*point, [](float& value) { value += Centre; });
        }

        const float x1 = c1[0] - c0[0];
        const float y1 = c1[1] - c0[1];
        const float x2 = c2[0] - c0[0];
        const float y2 = c2[1] - c0[1];
        const float det = x1 * y2 - x2 * y1;

        if (std::abs(det) < Flat)
        {
            return std::nullopt;
        }

        // The texture's steps along the two sides, through the inverse of the window's.
        const float u1 = c1[2] - c0[2];
        const float v1 = c1[3] - c0[3];
        const float u2 = c2[2] - c0[2];
        const float v2 = c2[3] - c0[3];
        const float ux = (u1 * y2 - u2 * y1) / det;
        const float uy = (u2 * x1 - u1 * x2) / det;
        const float vx = (v1 * y2 - v2 * y1) / det;
        const float vy = (v2 * x1 - v1 * x2) / det;
        return std::array{ux, uy, c0[2] - ux * c0[0] - uy * c0[1], vx, vy, c0[3] - vx * c0[0] - vy * c0[1]};
    }
}

namespace
{
    /// <summary>
    /// The affine map from window to texture positions of a textured polygon: from its first two vertices and its last
    /// (a quad's corners 0, 1 and 3), their texels 16.16; none for fewer than three vertices, or ones in a line.
    /// </summary>
    std::optional<std::array<float, 6>> PolygonMapping(const MCPolygonCommand& command)
    {
        constexpr size_t Triangle = 3;
        constexpr float Fixed = 65536.0f;

        if (command.Vertices.size() < Triangle)
        {
            return std::nullopt;
        }

        const auto point = [&command](const MCScreenVertex& vertex)
        {
            return MappedPoint{static_cast<float>(command.OriginX + vertex.X),
                               static_cast<float>(command.OriginY + vertex.Y), static_cast<float>(vertex.U) / Fixed,
                               static_cast<float>(vertex.V) / Fixed};
        };

        return TextureMapping(point(command.Vertices[0]), point(command.Vertices[1]), point(command.Vertices.back()));
    }
}

bool MCVulkanRenderer::AddMapped(uint16_t surface, SourceKind source, uint32_t sourceIndex, uint32_t op, uint32_t color,
                                 const std::array<float, 6>& mapping, const MCPolygonCommand& command)
{
    // The vertices' bounding box, clipped to the pane (the game's mapped polygons are rectangles); the shader leaves out
    // what lies outside the texture.
    Quad at = {std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(),
               std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()};

    for (const MCScreenVertex& vertex : command.Vertices)
    {
        const int32_t x = command.OriginX + vertex.X;
        const int32_t y = command.OriginY + vertex.Y;
        at = {std::min(at[0], x), std::min(at[1], y), std::max(at[2], x), std::max(at[3], y)};
    }

    at = {std::max(at[0], command.OriginX), std::max(at[1], command.OriginY),
          std::min(at[2], command.OriginX + command.XMax), std::min(at[3], command.OriginY + command.YMax)};

    if (!Add(surface, source, sourceIndex, at, Quad{0, 0, 1, 1}, op, color, 0, 0))
    {
        return false;
    }

    // The map in span and walk.xy (floats), the texture's size in walk.zw.
    std::array<int32_t, 20>& draw = _Draws.back();
    std::ranges::transform(mapping, draw.begin() + 12, [](float value) { return std::bit_cast<int32_t>(value); });
    const MCWindow& texture = command.Texture->get();
    draw[18] = texture.XMax + 1;
    draw[19] = texture.YMax + 1;
    return true;
}

void MCVulkanRenderer::AddSunShadow(MCWindow& target, std::span<const uint8_t> table, int32_t shapeNum, int32_t left,
                                    int32_t top, bool mirrored, const std::optional<Quad>& clip)
{
    const MCSceneLights::MCLighting lighting = MCSceneLights::ShownLighting();
    const float up = lighting.SunShadowUp;

    if (GHdSunShadows == 0 || up <= 0.0f)
    {
        return;
    }

    const std::span<const uint8_t> header = table.subspan(MCShapeOffset(table, shapeNum));
    const int32_t yMin = MCRead32(header, ShapeYMinAt);
    int32_t width = MCRead32(header, ShapeXMaxAt) - MCRead32(header, ShapeXMinAt) + 1;
    int32_t height = MCRead32(header, ShapeYMaxAt) - yMin + 1;
    std::shared_ptr<const MCHdArt::Frame> frame = HdShape(table, shapeNum, width, height);

    if (frame == nullptr)
    {
        return;
    }

    const auto foot = static_cast<float>(top - yMin);

    // A mech's legs drawn as their turn middle (HDTurns) cast the middle, where the draw places it (as Shape and
    // ShapeBlit do: mirrored, the middle's last column comes first).
    if (const std::optional<HdInBetweenFrame> inBetween = HdInBetween(table, shapeNum, *frame); inBetween.has_value())
    {
        left = mirrored ? left + width - inBetween->Width + inBetween->FrameX : left - inBetween->FrameX;
        top -= inBetween->FrameY;
        width = inBetween->Width;
        height = inBetween->Height;
        frame = inBetween->Frame;
    }

    const std::optional<uint32_t> picture = HdPictureOf(*frame);

    if (!picture)
    {
        return;
    }

    // A point h window pixels above the foot (the hot spot's row) casts its shadow right * h right and up * h up of
    // it; the draw maps each window point back to the frame's (a picture point, the frame's original pixels).
    const float right = lighting.SunShadowRight;
    const float x0 = static_cast<float>(left);
    const float y0 = static_cast<float>(top);
    const float step = mirrored ? -1.0f : 1.0f;
    const float firstColumn = mirrored ? x0 + static_cast<float>(width) : -x0;
    const std::array<float, 6> mapping = {step, step * right / up, firstColumn - step * right * foot / up,
                                          0.0f, 1.0f / up,         foot - foot / up - y0};

    // The shadow's bounds: the frame's corners cast.
    float minX = std::numeric_limits<float>::max();
    float minY = minX;
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = maxX;

    for (const float x : {x0, x0 + static_cast<float>(width)})
    {
        for (const float y : {y0, y0 + static_cast<float>(height)})
        {
            const float above = foot - y;
            minX = std::min(minX, x + right * above);
            maxX = std::max(maxX, x + right * above);
            minY = std::min(minY, foot - up * above);
            maxY = std::max(maxY, foot - up * above);
        }
    }

    const Quad bounds = clip.value_or(Quad{0, 0, target.XMax, target.YMax});
    const Quad at = {std::max(static_cast<int32_t>(std::floor(minX)), bounds[0]),
                     std::max(static_cast<int32_t>(std::floor(minY)), bounds[1]),
                     std::min(static_cast<int32_t>(std::ceil(maxX)) - 1, bounds[2]),
                     std::min(static_cast<int32_t>(std::ceil(maxY)) - 1, bounds[3])};

    if (at[2] < at[0] || at[3] < at[1])
    {
        return;
    }

    AddMappedSunShadow(SurfaceFor(target), *frame, *picture, at, mapping, 0);
}

void MCVulkanRenderer::AddPaintedShadow(uint16_t surface, const MCHdArt::Frame& frame, int32_t x, int32_t y,
                                        const Quad& clip)
{
    const std::array<float, 2> reach = MCSceneLights::PaintedShadowReach();

    if (!frame.PaintedShadow || (reach[0] <= 0.0f && reach[1] <= 0.0f))
    {
        return;
    }

    const std::optional<uint32_t> picture = HdPictureOf(frame);

    if (!picture)
    {
        return;
    }

    // The frame's bounds, reaching right and up as far as the shadow does.
    const auto right = static_cast<int32_t>(std::ceil(reach[0]));
    const auto up = static_cast<int32_t>(std::ceil(reach[1]));
    const Quad at = {std::max(x, clip[0]), std::max(y - up, clip[1]),
                     std::min(x + frame.OriginalWidth() - 1 + right, clip[2]),
                     std::min(y + frame.OriginalHeight() - 1, clip[3])};

    if (at[2] < at[0] || at[3] < at[1])
    {
        return;
    }

    // The reach in the frame's pixels, right and down, in 16ths: op.z's low and high halves (LengthenedShadowPixel).
    constexpr float Sixteenths = 16.0f;
    const auto half = [](float pixels)
    { return static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(std::lround(pixels * Sixteenths)))); };
    const std::array<float, 6> mapping = {1.0f, 0.0f, static_cast<float>(-x), 0.0f, 1.0f, static_cast<float>(-y)};
    AddMappedSunShadow(surface, frame, *picture, at, mapping, half(reach[0]) | half(-reach[1]) << 16);
}

bool MCVulkanRenderer::AddMappedSunShadow(uint16_t surface, const MCHdArt::Frame& frame, uint32_t picture,
                                          const Quad& at, const std::array<float, 6>& mapping, uint32_t reach)
{
    if (!Add(surface, SourceKind::Picture, picture, at, Quad{0, 0, 1, 1}, KindHdMap | AlphaBlend | SunShadowDraw,
             static_cast<uint32_t>(frame.Scale), reach, 0))
    {
        return false;
    }

    // The map in span and walk.xy (floats), the picture's size in walk.zw, as AddMapped lays them.
    std::array<int32_t, 20>& draw = _Draws.back();
    std::ranges::transform(mapping, draw.begin() + 12, [](float value) { return std::bit_cast<int32_t>(value); });
    draw[18] = frame.OriginalWidth();
    draw[19] = frame.OriginalHeight();
    ++_HdDrawn;
    return true;
}

bool MCVulkanRenderer::AddHdMap(uint16_t surface, const MCHdArt::Frame& frame, const MCPolygonCommand& command)
{
    const std::optional<std::array<float, 6>> mapping = PolygonMapping(command);
    const std::optional<uint32_t> picture = mapping.has_value() ? HdPictureOf(frame) : std::nullopt;

    if (!picture)
    {
        return false;
    }

    // The colour field carries the frame's scale and the key (a transparent map's colour, 255).
    constexpr uint32_t Key = 255;
    const uint32_t color = static_cast<uint32_t>(frame.Scale) | Key << 8;
    const uint32_t keyed = (command.MapFlags & VfxMapTransparent) != 0 ? ColorKey : 0;

    if (AddMapped(surface, SourceKind::Picture, *picture, KindHdMap | AlphaBlend | keyed | HdAsMade, color, *mapping,
                  command))
    {
        ++_HdDrawn;
    }

    return true;
}

bool MCVulkanRenderer::AddSmoothPolygon(uint16_t surface, const MCPolygonCommand& command)
{
    constexpr size_t Segment = 2;
    constexpr size_t Triangle = 3;
    constexpr size_t MostCorners = 4;
    const size_t corners = command.Vertices.size();

    if (corners < Triangle || corners > MostCorners)
    {
        return false;
    }

    // A polygon one row high draws nothing (the walk's first check).
    if (std::ranges::all_of(command.Vertices,
                            [&command](const MCScreenVertex& vertex) { return vertex.Y == command.Vertices[0].Y; }))
    {
        return false;
    }

    const auto corner = [&command](size_t i)
    {
        const MCScreenVertex& vertex = command.Vertices[i % command.Vertices.size()];
        return std::array{command.OriginX + vertex.X, command.OriginY + vertex.Y};
    };

    // Each corner's turn: the sign of the cross product of the sides meeting there.
    std::array<int32_t, MostCorners> turns{};

    for (size_t i = 0; i < corners; ++i)
    {
        const auto a = corner(i + corners - 1);
        const auto b = corner(i);
        const auto c = corner(i + 1);
        const int64_t cross =
            static_cast<int64_t>(b[0] - a[0]) * (c[1] - b[1]) - static_cast<int64_t>(b[1] - a[1]) * (c[0] - b[0]);
        turns[i] = cross > 0 ? 1 : cross < 0 ? -1 : 0;
    }

    const auto turning = std::span(turns).first(corners);
    const auto left = static_cast<size_t>(std::ranges::count(turning, 1));
    const auto right = static_cast<size_t>(std::ranges::count(turning, -1));

    // Each corner's colour index, rounded as the walk starts a side from it (a flat polygon's all its fill's). Four
    // Gouraud corners needn't lie on a plane, so the shader steps them as the walk does: down the sides, then across.
    constexpr float Fixed = 65536.0f;
    constexpr float HalfIndex = 0.5f;
    constexpr uint32_t Round = 0x8000;
    const float flat =
        static_cast<float>(((static_cast<uint32_t>(command.Vertices[0].C) + Round) >> 16) & 0xff) + HalfIndex;

    // Adds the colour pass over the polygon's corners `drawn` (`count` of them): its edges smooth, `turn` the way round
    // its corners go. With `skipped` (a concave quad's half), the triangle without that corner: the quad's two sides
    // smooth, the diagonal hard (its pixels this half's when `owns`).
    const auto add = [&](std::span<const size_t> drawn, int32_t turn, std::optional<size_t> skipped, bool owns)
    {
        Quad at = {std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(),
                   std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()};

        for (size_t i = 0; i < drawn.size(); ++i)
        {
            if (skipped.has_value() && drawn[i] == *skipped)
            {
                continue;
            }

            const auto [x, y] = corner(drawn[i]);
            at = {std::min(at[0], x), std::min(at[1], y), std::max(at[2], x), std::max(at[3], y)};
        }

        at = {std::max(at[0], command.OriginX), std::max(at[1], command.OriginY),
              std::min(at[2], command.OriginX + command.XMax), std::min(at[3], command.OriginY + command.YMax)};

        if (!Add(surface, SourceKind::None, 0, at, std::nullopt, KindSmoothPolygon, 0, 0, 0))
        {
            return;
        }

        // The corners in source (x in the low 16 bits, y in the high), their colour indices in span, the corners'
        // count and turn in walk.xy, and a half's left-out corner (plus 1) and whether it owns the diagonal in walk.zw.
        std::array<int32_t, 20>& draw = _Draws.back();
        constexpr uint32_t LowHalf = 0xffff;
        constexpr int HalfShift = 16;

        for (size_t i = 0; i < MostCorners; ++i)
        {
            const size_t vertex = drawn[std::min(i, drawn.size() - 1)];
            const auto [x, y] = corner(vertex);
            draw[4 + i] =
                static_cast<int32_t>((static_cast<uint32_t>(x) & LowHalf) | (static_cast<uint32_t>(y) << HalfShift));
            const float index = command.Kind == MCPolygonKind::Flat
                                    ? flat
                                    : static_cast<float>(command.Vertices[vertex].C) / Fixed + HalfIndex;
            draw[12 + i] = std::bit_cast<int32_t>(index);
        }

        draw[16] = static_cast<int32_t>(drawn.size());
        draw[17] = turn;
        draw[18] = skipped.has_value() ? static_cast<int32_t>(*skipped) + 1 : 0;
        draw[19] = owns ? 1 : 0;
    };

    constexpr std::array<size_t, MostCorners> all = {0, 1, 2, 3};

    // Whether the walk fills the whole polygon: going round it, its corners go down from one top to one bottom and
    // back up (the walk stops at a side running upwards). A convex polygon's always do.
    const auto filledWhole = [&]
    {
        std::array<int32_t, MostCorners> steps{};
        size_t moves = 0;

        for (size_t i = 0; i < corners; ++i)
        {
            const int32_t dy = command.Vertices[(i + 1) % corners].Y - command.Vertices[i].Y;

            if (dy != 0)
            {
                steps[moves++] = dy > 0 ? 1 : -1;
            }
        }

        size_t changes = 0;

        for (size_t i = 0; i < moves; ++i)
        {
            changes += steps[i] != steps[(i + 1) % moves] ? 1 : 0;
        }

        constexpr size_t OneTopOneBottom = 2;
        return changes == OneTopOneBottom;
    };

    // Port-only: a polygon of no area (corners in a line, or a beam's sides on one point: a path out and back), which the
    // walk draws as lines of pixels (all of them when it fills the polygon whole): corners in a line as a line between
    // the two farthest apart, a path as its sides (each once). A line is the coverage of two sides along it, either way
    // round. (A crossed quad whose halves' areas cancel isn't a path, and is left to the original's pixels.)
    int64_t area = 0;

    for (size_t i = 0; i < corners; ++i)
    {
        const auto [ax, ay] = corner(i);
        const auto [bx, by] = corner(i + 1);
        area += static_cast<int64_t>(ax) * by - static_cast<int64_t>(bx) * ay;
    }

    if (area == 0)
    {
        if (!filledWhole())
        {
            return false;
        }

        const auto apart = [&corner](size_t a, size_t b)
        {
            const auto [ax, ay] = corner(a);
            const auto [bx, by] = corner(b);
            return static_cast<int64_t>(bx - ax) * (bx - ax) + static_cast<int64_t>(by - ay) * (by - ay);
        };

        if (left + right == 0)
        {
            std::array<size_t, Segment> ends = {0, 1};

            for (size_t a = 0; a < corners; ++a)
            {
                for (size_t b = a + 1; b < corners; ++b)
                {
                    if (apart(a, b) > apart(ends[0], ends[1]))
                    {
                        ends = {a, b};
                    }
                }
            }

            add(ends, 1, std::nullopt, false);
            return true;
        }

        // A path: each side's way back is a side too.
        const auto back = [&](size_t i)
        {
            for (size_t j = 0; j < corners; ++j)
            {
                if (corner(j) == corner(i + 1) && corner(j + 1) == corner(i))
                {
                    return true;
                }
            }

            return false;
        };

        for (size_t i = 0; i < corners; ++i)
        {
            if (apart(i, (i + 1) % corners) > 0 && !back(i))
            {
                return false;
            }
        }

        std::vector<std::array<size_t, Segment>> sides;

        for (size_t i = 0; i < corners; ++i)
        {
            const std::array<size_t, Segment> side = {i, (i + 1) % corners};
            const auto same = [&](const std::array<size_t, Segment>& other)
            {
                return (corner(other[0]) == corner(side[0]) && corner(other[1]) == corner(side[1])) ||
                       (corner(other[0]) == corner(side[1]) && corner(other[1]) == corner(side[0]));
            };

            if (apart(side[0], side[1]) > 0 && std::ranges::none_of(sides, same))
            {
                sides.push_back(side);
                add(side, 1, std::nullopt, false);
            }
        }

        return true;
    }

    // Convex, one way round (the coverage is the nearest edge's distance); the turn's sign goes to the shader.
    if (left == 0 || right == 0)
    {
        add(std::span(all).first(corners), left > 0 ? 1 : -1, std::nullopt, false);
        return true;
    }

    // Port-only: a quad with one corner turned in (a thin beam's rounding) that the walk fills whole is drawn as the
    // two triangles either side of the diagonal from that corner.
    if (corners != MostCorners || std::min(left, right) != 1 || left + right != MostCorners || !filledWhole())
    {
        return false;
    }

    const int32_t turn = left > right ? 1 : -1;
    const auto in = static_cast<size_t>(std::ranges::find(turning, -turn) - turning.begin());
    add(all, turn, (in + 1) % MostCorners, true);
    add(all, turn, (in + MostCorners - 1) % MostCorners, false);
    return true;
}

bool MCVulkanRenderer::AddSmoothMap(uint16_t surface, SourceRef source, const MCPolygonCommand& command)
{
    const std::optional<std::array<float, 6>> mapping = PolygonMapping(command);

    if (!mapping)
    {
        return false;
    }

    AddMapped(surface, source.Kind, source.Index, KindSmoothMap | AlphaBlend, 0, *mapping, command);
    return true;
}

void MCVulkanRenderer::MapQuad(MCWindow& target, const MCMapQuadCommand& command)
{
    const AuditScope audit(*this, "MapQuad", target);
    const uint16_t surface = SurfaceFor(target);
    const MCWindow& texture = command.Texture.get();
    const std::optional<SourceRef> source = SourceFor(surface, texture);

    if (!source)
    {
        NotSupported("MapQuad from a window without pixels");
        return;
    }

    const Quad from = {0, 0, texture.XMax + 1, texture.YMax + 1};
    bool join = false;
    MCMapQuadSpans(command, _Spans,
                   [&](const MCSpan& span)
                   {
                       const Quad at = {span.X0, span.Y, span.X1, span.Y};

                       if (AddSpan(surface, *source, at, from, KindTexelWalk | SkipKeyAfterTable, 0, 0, 0, span, join))
                       {
                           join = true;
                       }
                   });
}

void MCVulkanRenderer::Line(MCWindow& target, const MCLineCommand& command)
{
    const AuditScope audit(*this, "Line", target);
    const uint16_t surface = SurfaceFor(target);
    uint32_t op = KindFill;
    uint32_t after = 0;

    if (command.Table.has_value())
    {
        op |= TableAfter;
        after = TableRow(*command.Table);
    }

    // Port-only: a line of one colour shown at the output's resolution: its pixels write their indices alone, and its
    // colour is drawn along it smooth (KindSmoothLine).
    const bool smooth = !command.Table.has_value() && ScaledHdArt(surface);

    if (smooth)
    {
        op |= IndexOnly;
    }

    // The pixels in runs along a row (a line never comes back to a pixel).
    bool join = false;
    bool open = false;
    std::optional<std::array<int32_t, 2>> first;
    std::array<int32_t, 2> last{};
    int32_t runY = 0;
    int32_t runX0 = 0;
    int32_t runX1 = 0;
    const auto flush = [&]
    {
        if (open)
        {
            const Quad at = {runX0, runY, runX1, runY};

            if (Add(surface, SourceKind::None, 0, at, std::nullopt, op, command.Color, 0, after, join))
            {
                join = true;
            }
        }
    };

    MCLinePixels(command,
                 [&](int32_t x, int32_t y)
                 {
                     if (!first.has_value())
                     {
                         first = std::array{x, y};
                     }

                     last = {x, y};

                     if (open && y == runY && (x == runX1 + 1 || x == runX0 - 1))
                     {
                         runX0 = std::min(runX0, x);
                         runX1 = std::max(runX1, x);
                         return;
                     }

                     flush();
                     open = true;
                     runY = y;
                     runX0 = x;
                     runX1 = x;
                 });
    flush();

    if (smooth && first.has_value())
    {
        // Out past its ends' pixels' centres by half its width and its edge's ramp (a pixel for the original's).
        const auto reach = static_cast<int32_t>(std::ceil(std::max(command.Width, 1.0f) / 2.0f + 0.5f));
        const Quad at = {std::min((*first)[0], last[0]) - reach, std::min((*first)[1], last[1]) - reach,
                         std::max((*first)[0], last[0]) + reach, std::max((*first)[1], last[1]) + reach};

        if (Add(surface, SourceKind::None, 0, at, std::nullopt, KindSmoothLine, command.Color, 0, 0))
        {
            std::array<int32_t, 20>& draw = _Draws.back();
            std::ranges::copy(std::array{(*first)[0], (*first)[1], last[0], last[1]}, draw.begin() + 12);

            // A wider line's half width (shaders/draw.pshader's SmoothShapePixel); 0, the original's, as before.
            if (command.Width != 1.0f)
            {
                draw[17] = std::bit_cast<int32_t>(command.Width / 2.0f);
            }
        }
    }
}

void MCVulkanRenderer::Ellipse(MCWindow& target, const MCEllipseCommand& command)
{
    const AuditScope audit(*this, "Ellipse", target);
    const uint16_t surface = SurfaceFor(target);
    uint32_t op = KindFill;

    if (command.Alpha)
    {
        op |= AlphaBlend;
    }

    // Port-only: an opaque ellipse shown at the output's resolution: its pixels write their indices alone, and its
    // colour is drawn smooth (KindSmoothEllipse).
    const bool smooth = !command.Alpha && ScaledHdArt(surface);

    if (smooth)
    {
        op |= IndexOnly;
    }

    // Each pixel once (MCEllipseRuns), so the runs can share the target's copy.
    bool join = false;
    MCEllipseRuns(command,
                  [&](int32_t y, int32_t x0, int32_t x1)
                  {
                      const Quad at = {x0, y, x1, y};

                      if (Add(surface, SourceKind::None, 0, at, std::nullopt, op, command.Color, 0, 0, join))
                      {
                          join = true;
                      }
                  });

    if (smooth)
    {
        const Quad at = {std::max(command.CenterX - command.Width - 1, command.Clip.X0),
                         std::max(command.CenterY - command.Height - 1, command.Clip.Y0),
                         std::min(command.CenterX + command.Width + 1, command.Clip.X1),
                         std::min(command.CenterY + command.Height + 1, command.Clip.Y1)};

        if (at[2] >= at[0] && at[3] >= at[1] &&
            Add(surface, SourceKind::None, 0, at, std::nullopt, KindSmoothEllipse, command.Color, 0, 0))
        {
            std::array<int32_t, 20>& draw = _Draws.back();
            std::ranges::copy(std::array{command.CenterX, command.CenterY, command.Width, command.Height},
                              draw.begin() + 12);
            draw[16] = command.Fill ? 1 : 0;
        }
    }
}

void MCVulkanRenderer::Shadow(MCWindow& target, const MCShadowCommand& command)
{
    const AuditScope audit(*this, "Shadow", target);
    const uint16_t surface = SurfaceFor(target);
    // The band's box: its ends and their widths (a ground width down the screen Aspect of one across).
    const float across = std::max(command.Width0, command.Width1);
    const float down = across * command.Aspect;
    const Quad at = {static_cast<int32_t>(std::floor(std::min(command.X0, command.X1) - across)),
                     static_cast<int32_t>(std::floor(std::min(command.Y0, command.Y1) - down)),
                     static_cast<int32_t>(std::ceil(std::max(command.X0, command.X1) + across)),
                     static_cast<int32_t>(std::ceil(std::max(command.Y0, command.Y1) + down))};

    if (Add(surface, SourceKind::None, 0, at, std::nullopt, KindShadow, 0, 0, 0))
    {
        std::array<int32_t, 20>& draw = _Draws.back();
        std::ranges::transform(std::array{command.X0, command.Y0, command.X1, command.Y1, command.Width0,
                                          command.Width1, command.Strength, command.Aspect},
                               draw.begin() + 12, [](float value) { return std::bit_cast<int32_t>(value); });
    }
}

void MCVulkanRenderer::Particle(MCWindow& target, const MCParticleCommand& command)
{
    const AuditScope audit(*this, "Particle", target);
    const uint16_t surface = SurfaceFor(target);
    // Its box: its ends and its edge (a ring's outer one), and a pixel for the smooth edge.
    const float across = command.Radius + std::max(command.Ring, 0.0f) + 1.0f;
    const float down = across * command.Aspect + 1.0f;
    const Quad at = {static_cast<int32_t>(std::floor(std::min(command.X0, command.X1) - across)),
                     static_cast<int32_t>(std::floor(std::min(command.Y0, command.Y1) - down)),
                     static_cast<int32_t>(std::ceil(std::max(command.X0, command.X1) + across)),
                     static_cast<int32_t>(std::ceil(std::max(command.Y0, command.Y1) + down))};
    const uint32_t color = static_cast<uint32_t>(command.Red) | static_cast<uint32_t>(command.Green) << 8 |
                           static_cast<uint32_t>(command.Blue) << 16 | static_cast<uint32_t>(command.Alpha) << 24;

    if (Add(surface, SourceKind::None, 0, at, std::nullopt, KindSmoothEllipse | ParticleDraw, color, 0, 0))
    {
        std::array<int32_t, 20>& draw = _Draws.back();
        std::ranges::transform(std::array{command.X0, command.Y0, command.X1, command.Y1, command.Radius,
                                          command.Aspect, command.Softness, command.Ring},
                               draw.begin() + 12, [](float value) { return std::bit_cast<int32_t>(value); });
        // Its style and a raindrop's tip in the source's slots, which a particle has none of; the light it caught in
        // the op's (its table's, which it has none of either).
        draw[4] = static_cast<int32_t>(command.Style);
        draw[5] = std::bit_cast<int32_t>(command.Tip);
        draw[10] = static_cast<int32_t>(command.Glint);
    }
}

void MCVulkanRenderer::StatusBar(MCWindow& target, const MCStatusBarCommand& command)
{
    const AuditScope audit(*this, "StatusBar", target);
    // The AlphaTable row status-bar frames are darkened through (0x008011d0 in MCX.EXE).
    constexpr int32_t StatusFrameAlpha = 0x108;
    const uint16_t surface = SurfaceFor(target);
    // The frame and the bar are the alpha colours whose AlphaTable rows the software renderer maps them through.
    const auto frame = static_cast<uint32_t>(StatusFrameAlpha);
    const auto fill = static_cast<uint32_t>(command.AlphaColor);
    const MCRect& box = command.Box;
    const int32_t width = box.X1 - box.X0;
    const auto blends = [](uint32_t color) { return color < SpecialColor.size() && SpecialColor[color] != 0; };

    // Port-only: on a surface shown at the output's resolution with HD art on, a bar that blends (an opaque colour
    // writes indices) is drawn there (KindSmoothBar): its fill ends where the game measured it, inside its last pixel;
    // its frame, on whole pixels, as before.
    const bool smooth = ScaledHdArt(surface) && blends(frame) && blends(fill);
    const uint32_t op = (smooth ? KindSmoothBar : KindFill) | AlphaBlend | GivenAlpha;

    // The frame's top and bottom rows (the pixels between the corners), then the body rows: their left and right
    // pixels through the frame's table, then the bar through the fill's. A pixel the bar shares with the right
    // edge is mapped by both, in that order (each part reads the pixels the one before left). A smooth part ends at
    // window x right (exclusive).
    bool join = false;
    const auto add = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t table, bool joins,
                         std::optional<float> right = std::nullopt)
    {
        const Quad at = {x0, y0, x1, y1};

        if (Add(surface, SourceKind::None, 0, at, std::nullopt, op, 0, 0, table, joins && join))
        {
            join = true;

            if (smooth)
            {
                _Draws.back()[12] = std::bit_cast<int32_t>(right.value_or(static_cast<float>(x1 + 1)));
            }
        }
    };

    for (const int32_t row : {command.FrameTop, command.FrameBottom})
    {
        // A box of one row has the same frame row twice; it is mapped once.
        if (row >= box.Y0 && row <= box.Y1)
        {
            add(box.X0 + 1, row, box.X0 + width - 1, row, frame, true);
        }

        if (command.FrameTop == command.FrameBottom)
        {
            break;
        }
    }

    // The body rows: the box's rows less the frame rows, in up to three runs.
    std::vector<std::pair<int32_t, int32_t>> body;
    int32_t first = box.Y0;

    for (int32_t y = box.Y0; y <= box.Y1 + 1; ++y)
    {
        if (y > box.Y1 || y == command.FrameTop || y == command.FrameBottom)
        {
            if (y > first)
            {
                body.emplace_back(first, y - 1);
            }

            first = y + 1;
        }
    }

    for (const auto& [y0, y1] : body)
    {
        add(box.X0, y0, box.X0, y1, frame, true);
        // A box one pixel wide maps its one pixel twice.
        add(box.X0 + width, y0, box.X0 + width, y1, frame, width != 0);
    }

    if (command.BarLength != 0)
    {
        // The fill covers its length plus one pixels from the frame's left column on, so it ends (exclusive) two
        // pixels past that column plus its length: the measured length reaches into the pixel past the rounded one's.
        const float right =
            static_cast<float>(box.X0 + 2) + (smooth ? command.FillLength : static_cast<float>(command.BarLength));
        const int32_t last = static_cast<int32_t>(std::ceil(right)) - 1;

        for (const auto& [y0, y1] : body)
        {
            add(box.X0 + 1, y0, last, y1, fill, false, right);
        }
    }
}

void MCVulkanRenderer::Glyph(MCWindow& target, const MCGlyphCommand& command)
{
    const AuditScope audit(*this, "Glyph", target, command.Font, command.Character);
    const uint16_t surface = SurfaceFor(target);
    const std::span<const uint8_t> font = command.Font;
    const auto glyphAt = static_cast<uint32_t>(MCRead32(font, 0x10 + static_cast<size_t>(command.Character) * 4));
    const int32_t pitch = MCRead32(font, glyphAt);
    const std::span<const uint8_t> source =
        font.subspan(glyphAt + GlyphPixelsAt +
                     static_cast<size_t>(static_cast<intptr_t>(command.SourceY) * pitch + command.SourceX));
    const int32_t columns = command.Columns;
    const int32_t rows = command.Rows;

    if (columns <= 0 || rows <= 0)
    {
        return;
    }

    // The rows lie a pitch apart: the bytes from the first row's start to the last row's end (the pitch in the seed).
    const size_t size = static_cast<size_t>(rows - 1) * static_cast<size_t>(pitch) + static_cast<size_t>(columns);
    const MCOptionalRef<DataImage> found = AtlasForData(
        source, Seed(ImageKind::Glyph, columns, rows), "a font", [&] { return Extent{size, columns, rows}; },
        [&](std::span<uint8_t> out, const Extent&)
        {
            for (int32_t row = 0; row < rows; ++row)
            {
                for (int32_t i = 0; i < columns; ++i)
                {
                    out[(row * columns + i) * 2] = source[row * pitch + i];
                    out[(row * columns + i) * 2 + 1] = 1;
                }
            }
        });

    if (!found.has_value())
    {
        return;
    }

    const std::optional<AtlasPlace>& place = found->get().Place;

    if (!place)
    {
        NotSupported("Glyph larger than an atlas page");
        return;
    }

    const Quad at = {command.X, command.Y, command.X + columns - 1, command.Y + rows - 1};
    const Quad from = {place->X, place->Y, 1, 1};
    uint32_t op = KindTexture;
    uint32_t before = 0;

    if (command.Table.has_value())
    {
        op |= TableBefore | SkipKeyAfterTable;
        before = TableRow(*command.Table);

        // Port-only: on a sharp screen, the text is shown from its glyphs redrawn sharp (whole, the drawn part shown);
        // the original's draw keeps the indices.
        const int32_t height = VfxFontHeight(font);
        const size_t whole = static_cast<size_t>(pitch) * static_cast<size_t>(std::max(height, 0));

        if (SharpScreenSurface(surface) && glyphAt + GlyphPixelsAt + whole <= font.size() &&
            AddSharpGlyph(surface, font, font.subspan(glyphAt + GlyphPixelsAt, whole), pitch, height, at,
                          command.SourceX, command.SourceY, *command.Table))
        {
            op |= IndexOnly;
        }
    }

    Add(surface, SourceKind::Atlas, place->Page, at, from, op, 0, before, 0);
}

// The terrain ---------------------------------------------------------------------------------------------------------

std::expected<void, std::string> MCVulkanRenderer::TerrainLayer(MCWindow& target, const MCTerrainFrame& frame)
{
    if (!frame.Mesh.has_value() || frame.Mesh->get().Cols <= 0 || frame.Mesh->get().Rows <= 0)
    {
        return std::unexpected("the terrain layer has no mesh");
    }

    // The haze reads the fog of war's flags on the GPU.
    if (!frame.Fog.has_value() || MCRenderer::FrameSurfaceOf(frame.Fog->get()) == nullptr)
    {
        return std::unexpected("the fog of war isn't a frame surface");
    }

    const MCTerrainMesh& mesh = frame.Mesh->get();
    const uint16_t surface = SurfaceFor(target);
    const uint16_t fog = SurfaceFor(frame.Fog->get());

    if (_TerrainMesh.Version != mesh.Version || _TerrainMesh.HdPack != HdPackInUse() ||
        _TerrainMesh.Relief != ReliefMapsInUse())
    {
        if (auto uploaded = UploadTerrainMesh(mesh); !uploaded)
        {
            return uploaded;
        }
    }

    if (!_TerrainMesh.Usable)
    {
        return std::unexpected("the terrain mesh isn't on the GPU");
    }

    TerrainDraw draw;
    draw.FogIndex = fog;
    draw.Frame = frame;
    draw.Frame.Mesh.reset();
    draw.Frame.Fog.reset();
    draw.Frame.Haze = {};

    // The pane's clip, within the surface.
    const Surface& drawn = _Surfaces[surface];
    draw.Frame.Clip = MCRect{std::max(frame.Clip.X0, 0), std::max(frame.Clip.Y0, 0),
                             std::min(frame.Clip.X1, static_cast<int32_t>(drawn.RecordedWidth) - 1),
                             std::min(frame.Clip.Y1, static_cast<int32_t>(drawn.RecordedHeight) - 1)};

    if (!frame.AllFilled)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            draw.HazeRows[i] = frame.Haze[i].has_value() ? TableRow(*frame.Haze[i]) : 0;
        }
    }

    // The mesh's cells the grid covers (terrain.vshader launches an instance each).
    const int32_t firstRow = std::max(frame.FirstRow, _TerrainMesh.FirstRow);
    const int32_t lastRow = std::min(frame.LastRow, _TerrainMesh.FirstRow + _TerrainMesh.Rows - 1);
    const int32_t firstCol = std::max(frame.FirstCol, _TerrainMesh.FirstCol);
    const int32_t lastCol = std::min(frame.LastCol, _TerrainMesh.FirstCol + _TerrainMesh.Cols - 1);

    if (lastRow >= firstRow && lastCol >= firstCol && !Empty(draw.Frame.Clip))
    {
        draw.CellCount = static_cast<uint32_t>((lastRow - firstRow + 1) * (lastCol - firstCol + 1));
        Record record;
        record.SurfaceIndex = surface;
        record.TerrainDraw = static_cast<int32_t>(_TerrainDraws.size());
        record.Writes = draw.Frame.Clip;
        _TerrainDraws.push_back(draw);
        _Records.push_back(record);
    }

    _TerrainLayerSurface = surface;
    ++_TerrainLayersDrawn;
    return {};
}

void MCVulkanRenderer::EndTerrainLayer(MCWindow& /*target*/)
{
    _TerrainLayerSurface = -1;
}

std::span<uint8_t> MCVulkanRenderer::QueueBufferUpload(std::shared_ptr<SDL_GPUBuffer> buffer, uint32_t size)
{
    const size_t offset = (_Staging.size() + UploadAlignment - 1) & ~(UploadAlignment - 1);
    _Staging.resize(offset + size);
    _BufferUploads.push_back(BufferUpload{std::move(buffer), size, offset});
    return std::span(_Staging).subspan(offset, size);
}

void MCVulkanRenderer::ReleaseTerrainMesh()
{
    Release(_TerrainMesh.Atlas);
    Release(_TerrainMesh.HdAtlas);
    Release(_TerrainMesh.HdAtlas2);
    Release(_TerrainMesh.ReliefAtlas);
    Release(_TerrainMesh.ReliefAtlas2);
    _TerrainMesh = TerrainMeshState{};
}

std::expected<void, std::string> MCVulkanRenderer::UploadTerrainMesh(const MCTerrainMesh& mesh)
{
    BeginRecording();
    ReleaseTerrainMesh();
    _TerrainMesh.Version = mesh.Version;
    _TerrainMesh.Upload = ++_TerrainUploads;
    _TerrainMesh.CellTiles = mesh.CellTiles;
    _TerrainMesh.CellElevations = mesh.CellElevations;
    _TerrainMesh.Shapes.resize(mesh.Tiles.size());
    _TerrainMesh.HdPack = HdPackInUse();
    _TerrainMesh.FirstRow = mesh.FirstRow;
    _TerrainMesh.FirstCol = mesh.FirstCol;
    _TerrainMesh.Cols = mesh.Cols;
    _TerrainMesh.Rows = mesh.Rows;

    // The tiles in shelves across the atlas.
    struct Place
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    std::vector<Place> places(mesh.Tiles.size());
    int32_t shelfX = 0;
    int32_t shelfY = 0;
    int32_t shelfHeight = 0;

    // Port-only: the HD art pack's colours for the tiles (all at one scale). Then the places lie on whole BC7 blocks
    // (4 x 4, so a place's HD copy, at the scale, starts on a block and has room for its padded blocks).
    std::vector<std::shared_ptr<const MCHdArt::Frame>> hdTiles(mesh.Tiles.size());
    int32_t hdScale = 0;

    if (_TerrainMesh.HdPack != 0)
    {
        for (size_t i = 0; i < mesh.Tiles.size(); ++i)
        {
            const std::span<const uint8_t> tile = mesh.Tiles[i];

            if (const int32_t width = VfxTileWidth(tile); width != 0)
            {
                if (auto frame = MCHdArt::FindTerrainTile(tile, width, tile[2]);
                    frame != nullptr && (hdScale == 0 || frame->Scale == hdScale))
                {
                    hdScale = frame->Scale;
                    hdTiles[i] = std::move(frame);
                }
            }
        }
    }

    // The seam pass's runs hold a column in a byte (TerrainRunWidest), so a wider tile keeps the map's ground original.
    const auto tooWide = [](std::span<const uint8_t> tile) { return VfxTileWidth(tile) > TerrainRunWidest; };

    if (const auto wide = hdScale != 0 ? std::ranges::find_if(mesh.Tiles, tooWide) : mesh.Tiles.end();
        wide != mesh.Tiles.end())
    {
        SDL_Log("MCVulkanRenderer: the HD terrain is off for this map: a tile is %d pixels wide, more than its runs "
                "hold (%d)",
                VfxTileWidth(*wide), TerrainRunWidest);
        hdScale = 0;
        std::ranges::fill(hdTiles, nullptr);
    }

    const int32_t align = hdScale != 0 ? MCBc7::BlockSide : 1;
    const auto aligned = [align](int32_t value) { return (value + align - 1) / align * align; };
    // Port-only: with HD ground, each place has its rows' runs beside it (TerrainRunColumns), for the seam pass.
    const int32_t runColumns = hdScale != 0 ? TerrainRunColumns : 0;

    for (size_t i = 0; i < mesh.Tiles.size(); ++i)
    {
        const std::span<const uint8_t> tile = mesh.Tiles[i];
        Place& place = places[i];
        place.Width = VfxTileWidth(tile);
        place.Height = tile[2];

        if (place.Width == 0)
        {
            continue;
        }

        if (shelfX + aligned(place.Width + runColumns) > TerrainAtlasWidth)
        {
            shelfY += shelfHeight;
            shelfX = 0;
            shelfHeight = 0;
        }

        place.X = shelfX;
        place.Y = shelfY;
        shelfX += aligned(place.Width + runColumns);
        shelfHeight = std::max(shelfHeight, aligned(place.Height));
    }

    const auto atlasHeight =
        static_cast<uint32_t>(std::bit_ceil(static_cast<uint32_t>(std::max(shelfY + shelfHeight, 1))));
    SDL_GPUBufferCreateInfo cellsInfo{};
    cellsInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    cellsInfo.size = static_cast<uint32_t>(mesh.CellTiles.size() * TerrainCellInts * sizeof(uint32_t));
    SDL_GPUBufferCreateInfo imagesInfo{};
    imagesInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    imagesInfo.size = static_cast<uint32_t>(std::max<size_t>(places.size(), 1) * TerrainImageInts * sizeof(int32_t));

    if (mesh.CellTiles.size() != static_cast<size_t>(mesh.Cols) * mesh.Rows ||
        mesh.CellElevations.size() != mesh.CellTiles.size() || mesh.CellBlocked.size() != mesh.CellTiles.size())
    {
        return std::unexpected(std::format("the terrain mesh's {}x{} cells come with {} tiles, {} elevations and {} "
                                           "blocked counts",
                                           mesh.Cols, mesh.Rows, mesh.CellTiles.size(), mesh.CellElevations.size(),
                                           mesh.CellBlocked.size()));
    }

    if (atlasHeight > 16384)
    {
        return std::unexpected(std::format("the terrain's {} tiles need a {}x{} atlas", mesh.Tiles.size(),
                                           TerrainAtlasWidth, atlasHeight));
    }

    if (auto made = EnsureTexture(_TerrainMesh.Atlas, SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                  TerrainAtlasWidth, atlasHeight);
        !made)
    {
        return made;
    }

    _TerrainMesh.Cells =
        MCGpuHandle<SDL_GPUBuffer>(SDL_CreateGPUBuffer(_Device.get(), &cellsInfo), MCGpuRelease{_Device});
    _TerrainMesh.Images =
        MCGpuHandle<SDL_GPUBuffer>(SDL_CreateGPUBuffer(_Device.get(), &imagesInfo), MCGpuRelease{_Device});

    if (_TerrainMesh.Cells == nullptr || _TerrainMesh.Images == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUBuffer(terrain)"));
    }

    // Port-only: the HD tiles in shelves of their own across up to two atlases (ShelveHdFrames), each place on whole
    // BC7 blocks.
    for (size_t i = 0; i < hdTiles.size(); ++i)
    {
        hdTiles[i] = places[i].Width != 0 ? hdTiles[i] : nullptr;
    }

    // The atlases a set of frames is shelved across, made in their format; or why there are none.
    const auto shelve = [this](std::span<const std::shared_ptr<const MCHdArt::Frame>> frames,
                               std::array<Texture*, HdTerrainAtlases> atlases, SDL_GPUTextureFormat format,
                               std::vector<HdPlace>& shelved) -> std::expected<void, std::string>
    {
        const std::optional<std::array<uint32_t, HdTerrainAtlases>> heights = ShelveHdFrames(frames, shelved);

        if (!heights)
        {
            return std::unexpected(std::format("its tiles need more than {} {}x{} atlases", HdTerrainAtlases,
                                               MaxHdAtlasSide, MaxHdAtlasSide));
        }

        for (size_t atlas = 0; atlas < atlases.size(); ++atlas)
        {
            if ((*heights)[atlas] == 0)
            {
                continue;
            }

            if (auto made = MakeHdTerrainAtlas(*atlases[atlas], (*heights)[atlas], format); !made)
            {
                return made;
            }
        }

        return {};
    };

    std::vector<HdPlace> hdPlaces;

    if (hdScale != 0)
    {
        if (auto made = shelve(hdTiles, {&_TerrainMesh.HdAtlas, &_TerrainMesh.HdAtlas2},
                               SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM, hdPlaces);
            !made)
        {
            SDL_Log("MCVulkanRenderer: the HD terrain is off for this map: %s", made.error().c_str());
            hdScale = 0;
        }
    }

    if (hdScale == 0)
    {
        std::ranges::fill(hdTiles, nullptr);
    }

    hdPlaces.resize(mesh.Tiles.size());
    _TerrainMesh.HdScale = hdScale;

    // Port-only (HD lighting's relief maps): the HD tiles' relief maps (all at one scale), shelved the same way in BC5
    // atlases of their own; a tile without one is lit by its brightness.
    std::vector<std::shared_ptr<const MCHdArt::Frame>> reliefTiles(mesh.Tiles.size());
    std::vector<HdPlace> reliefPlaces;
    int32_t reliefScale = 0;
    _TerrainMesh.Relief = ReliefMapsInUse();

    for (size_t i = 0; i < hdTiles.size() && _TerrainMesh.Relief; ++i)
    {
        if (hdTiles[i] != nullptr)
        {
            const std::span<const uint8_t> tile = mesh.Tiles[i];

            if (auto frame = MCHdArt::FindTerrainRelief(tile, places[i].Width, tile[2]);
                frame != nullptr && frame->Format == MCHdArt::ColorFormat::Bc5 &&
                (reliefScale == 0 || frame->Scale == reliefScale))
            {
                reliefScale = frame->Scale;
                reliefTiles[i] = std::move(frame);
            }
        }
    }

    if (reliefScale != 0)
    {
        if (auto made = shelve(reliefTiles, {&_TerrainMesh.ReliefAtlas, &_TerrainMesh.ReliefAtlas2},
                               SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM, reliefPlaces);
            !made)
        {
            SDL_Log("MCVulkanRenderer: the relief maps are off for this map: %s", made.error().c_str());
            Release(_TerrainMesh.ReliefAtlas);
            Release(_TerrainMesh.ReliefAtlas2);
            reliefScale = 0;
        }
    }

    if (reliefScale == 0)
    {
        std::ranges::fill(reliefTiles, nullptr);
    }

    reliefPlaces.resize(mesh.Tiles.size());
    _TerrainMesh.ReliefScale = reliefScale;

    if (reliefScale != 0)
    {
        SDL_Log(
            "MCVulkanRenderer: relief maps: %zu of %zu tiles at %dx, BC5 atlases %ux%u and %ux%u",
            static_cast<size_t>(std::ranges::count_if(reliefTiles, [](const auto& tile) { return tile != nullptr; })),
            mesh.Tiles.size(), reliefScale, _TerrainMesh.ReliefAtlas.Width, _TerrainMesh.ReliefAtlas.Height,
            _TerrainMesh.ReliefAtlas2.Width, _TerrainMesh.ReliefAtlas2.Height);
    }

    if (hdScale != 0)
    {
        const double megabytes = (static_cast<double>(_TerrainMesh.HdAtlas.Width) * _TerrainMesh.HdAtlas.Height +
                                  static_cast<double>(_TerrainMesh.HdAtlas2.Width) * _TerrainMesh.HdAtlas2.Height) /
                                 (1024.0 * 1024.0);
        SDL_Log("MCVulkanRenderer: HD terrain: %zu of %zu tiles, BC7 atlases %ux%u and %ux%u (%.0f MB)",
                static_cast<size_t>(std::ranges::count_if(hdTiles, [](const auto& tile) { return tile != nullptr; })),
                mesh.Tiles.size(), _TerrainMesh.HdAtlas.Width, _TerrainMesh.HdAtlas.Height, _TerrainMesh.HdAtlas2.Width,
                _TerrainMesh.HdAtlas2.Height, megabytes);
    }

    for (size_t i = 0; i < places.size(); ++i)
    {
        const Place& place = places[i];

        if (place.Width != 0 && hdTiles[i] != nullptr)
        {
            UploadHdTile(hdPlaces[i].Atlas == 0 ? _TerrainMesh.HdAtlas : _TerrainMesh.HdAtlas2, *hdTiles[i],
                         hdPlaces[i].X, hdPlaces[i].Y);
        }

        if (place.Width != 0 && reliefTiles[i] != nullptr)
        {
            UploadHdTile(reliefPlaces[i].Atlas == 0 ? _TerrainMesh.ReliefAtlas : _TerrainMesh.ReliefAtlas2,
                         *reliefTiles[i], reliefPlaces[i].X, reliefPlaces[i].Y);
        }

        if (place.Width != 0)
        {
            const int32_t stride = place.Width + runColumns;
            const std::span<uint8_t> texels =
                QueueUpload(_TerrainMesh.Atlas.Handle, static_cast<uint32_t>(place.X), static_cast<uint32_t>(place.Y),
                            static_cast<uint32_t>(stride), static_cast<uint32_t>(place.Height), 2);
            DecodeTile(mesh.Tiles[i], stride, texels);
            _TerrainMesh.Shapes[i].Colors = TileColors(mesh.Tiles[i]);

            if (runColumns != 0)
            {
                WriteTileRuns(place.Width, place.Height, texels);
            }

            ++_FrameUploads.AtlasImages;
            _FrameUploads.AtlasBytes += static_cast<int64_t>(stride) * place.Height * 2;
        }
    }

    const std::span<int32_t> images = UploadAs<int32_t>(QueueBufferUpload(_TerrainMesh.Images, imagesInfo.size));
    std::ranges::fill(images, 0);

    for (size_t i = 0; i < places.size(); ++i)
    {
        const std::vector<uint8_t>& tile = mesh.Tiles[i];
        const std::span<int32_t> image = images.subspan(i * TerrainImageInts, TerrainImageInts);
        image[0] = places[i].X;
        image[1] = places[i].Y;
        image[2] = places[i].Width;
        image[3] = places[i].Height;
        image[4] = tile[0];
        image[5] = tile[1];
        // Port-only: the HD tile's atlas (1 or 2; 0 none) and its place there in whole blocks (x low, y high 16 bits).
        constexpr uint32_t HalfShift = 16;
        image[6] = hdTiles[i] != nullptr ? static_cast<int32_t>(hdPlaces[i].Atlas + 1) : 0;
        image[7] =
            static_cast<int32_t>(hdPlaces[i].X / MCBc7::BlockSide | hdPlaces[i].Y / MCBc7::BlockSide << HalfShift);
        // Port-only: its relief map's atlas (1 or 2; 0 none) and place there, as the HD tile's.
        image[8] = reliefTiles[i] != nullptr ? static_cast<int32_t>(reliefPlaces[i].Atlas + 1) : 0;
        image[9] = static_cast<int32_t>(reliefPlaces[i].X / MCBc7::BlockSide | reliefPlaces[i].Y / MCBc7::BlockSide
                                                                                   << HalfShift);
        // The width its header gives, which VFX_nTile_draw's test for a tile off the pane reads.
        image[12] = tile[3];
        TerrainTileShape& shape = _TerrainMesh.Shapes[i];
        shape.Width = image[2];
        shape.Height = image[3];
        shape.HotX = image[4];
        shape.HotY = image[5];
        shape.HeaderWidth = image[12];
    }

    const std::span<uint32_t> cells = UploadAs<uint32_t>(QueueBufferUpload(_TerrainMesh.Cells, cellsInfo.size));

    for (size_t i = 0; i < mesh.CellTiles.size(); ++i)
    {
        cells[i * TerrainCellInts] = mesh.CellTiles[i];
        cells[i * TerrainCellInts + 1] = mesh.CellElevations[i];
        cells[i * TerrainCellInts + 2] = mesh.CellBlocked[i];
        cells[i * TerrainCellInts + 3] = 0;
    }

    _TerrainMesh.Usable = true;
    return {};
}

uint64_t MCVulkanRenderer::HdPackInUse()
{
    return MCHdArt::Active() ? MCHdArt::PackVersion() : 0;
}

std::expected<void, std::string> MCVulkanRenderer::MakeHdTerrainAtlas(Texture& atlas, uint32_t height,
                                                                      SDL_GPUTextureFormat format)
{
    if (format == SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM && !_Bc7.has_value())
    {
        _Bc7 = SDL_GPUTextureSupportsFormat(_Device.get(), SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                            SDL_GPU_TEXTUREUSAGE_SAMPLER);
    }

    // An RGBA8 atlas would take four times the memory: without BC7, the ground stays the original's (and without BC5,
    // it's lit by its brightness).
    const bool sampled =
        format == SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM
            ? *_Bc7
            : SDL_GPUTextureSupportsFormat(_Device.get(), format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);

    if (!sampled)
    {
        return std::unexpected(std::format("the GPU doesn't sample its format ({})", static_cast<int>(format)));
    }

    return EnsureTexture(atlas, format, SDL_GPU_TEXTUREUSAGE_SAMPLER, MaxHdAtlasSide, height);
}

bool MCVulkanRenderer::ReliefMapsInUse()
{
    return MCSceneLights::ReliefMapsOn();
}

void MCVulkanRenderer::UploadHdTile(Texture& atlas, const MCHdArt::Frame& frame, uint32_t x, uint32_t y)
{
    const auto width = static_cast<uint32_t>(MCBc7::Padded(frame.Width));
    const auto height = static_cast<uint32_t>(MCBc7::Padded(frame.Height));
    std::vector<uint8_t> encoded;
    std::span<const uint8_t> blocks = frame.Colors;

    if (frame.Format == MCHdArt::ColorFormat::Rgba8)
    {
        encoded = MCBc7::Encode(frame.Colors, frame.Width, frame.Height);
        blocks = encoded;
    }

    if (blocks.size() != static_cast<size_t>(width) * height * HdBc7Bytes)
    {
        NotSupported("an HD terrain tile whose blocks don't match its size");
        return;
    }

    std::ranges::copy(blocks, QueueUpload(atlas.Handle, x, y, width, height, HdBc7Bytes).begin());
    ++_FrameUploads.AtlasImages;
    _FrameUploads.AtlasBytes += static_cast<int64_t>(blocks.size());
}

void MCVulkanRenderer::DrawTerrain(SDL_GPUCommandBuffer& commands, SDL_GPURenderPass& pass, const Surface& surface,
                                   const TerrainDraw& draw, std::span<const std::byte> frameUniforms)
{
    const Surface& fog = _Surfaces[draw.FogIndex];

    if (!_TerrainMesh.Usable || fog.Index.Handle == nullptr || draw.CellCount == 0)
    {
        return;
    }

    const MCTerrainFrame& frame = draw.Frame;
    TerrainUniforms uniforms{};
    uniforms.TargetSize[0] = static_cast<float>(surface.MappedWidth);
    uniforms.TargetSize[1] = static_cast<float>(surface.MappedHeight);
    uniforms.TargetSize[2] = static_cast<float>(surface.Target.Width);
    uniforms.TargetSize[3] = static_cast<float>(surface.Target.Height);
    uniforms.Origin[0] = frame.OriginX;
    uniforms.Origin[1] = frame.OriginY;
    uniforms.Origin[2] = frame.ElevStep;
    uniforms.Steps[0] = frame.StepX;
    uniforms.Steps[1] = frame.StepY;
    uniforms.Steps[2] = _TerrainMesh.FirstRow;
    uniforms.Steps[3] = _TerrainMesh.FirstCol;
    uniforms.Mesh[0] = _TerrainMesh.Cols;
    uniforms.Mesh[1] = _TerrainMesh.HdAtlas.Handle != nullptr ? _TerrainMesh.HdScale : 0;
    uniforms.Mesh[2] = static_cast<int32_t>(fog.RecordedWidth);
    uniforms.Mesh[3] = static_cast<int32_t>(fog.RecordedHeight);
    uniforms.Grid[0] = frame.FirstRow;
    uniforms.Grid[1] = frame.FirstCol;
    uniforms.Grid[2] = frame.LastRow;
    uniforms.Grid[3] = frame.LastCol;
    uniforms.Corners[0] = frame.MinX;
    uniforms.Corners[1] = frame.MaxX;
    uniforms.Corners[2] = frame.MinY;
    uniforms.Corners[3] = frame.MaxY;
    uniforms.Clip[0] = frame.Clip.X0;
    uniforms.Clip[1] = frame.Clip.Y0;
    uniforms.Clip[2] = frame.Clip.X1;
    uniforms.Clip[3] = frame.Clip.Y1;
    uniforms.Haze[0] = frame.AllFilled ? 1u : 0u;
    uniforms.Haze[1] = draw.HazeRows[0];
    uniforms.Haze[2] = draw.HazeRows[1];
    uniforms.Haze[3] = draw.HazeRows[2];
    uniforms.Pane[0] = frame.PaneX;
    uniforms.Pane[1] = frame.PaneY;
    uniforms.Pane[3] = _TerrainMesh.ReliefAtlas.Handle != nullptr ? _TerrainMesh.ReliefScale : 0;

    SDL_BindGPUGraphicsPipeline(&pass, _TerrainPipeline.get());
    const SDL_GPUTextureSamplerBinding fogBinding{fog.Index.Handle.get(), _Nearest.get()};
    SDL_BindGPUVertexSamplers(&pass, 0, &fogBinding, 1);
    SDL_GPUBuffer* const buffers[2] = {_TerrainMesh.Cells.get(), _TerrainMesh.Images.get()};
    SDL_BindGPUVertexStorageBuffers(&pass, 0, buffers, 2);
    SDL_GPUTexture* const hdAtlas =
        _TerrainMesh.HdAtlas.Handle != nullptr ? _TerrainMesh.HdAtlas.Handle.get() : _PaletteTexture.Handle.get();
    SDL_GPUTexture* const hdAtlas2 =
        _TerrainMesh.HdAtlas2.Handle != nullptr ? _TerrainMesh.HdAtlas2.Handle.get() : hdAtlas;
    SDL_GPUTexture* const reliefAtlas = _TerrainMesh.ReliefAtlas.Handle != nullptr
                                            ? _TerrainMesh.ReliefAtlas.Handle.get()
                                            : _PaletteTexture.Handle.get();
    SDL_GPUTexture* const reliefAtlas2 =
        _TerrainMesh.ReliefAtlas2.Handle != nullptr ? _TerrainMesh.ReliefAtlas2.Handle.get() : reliefAtlas;
    const SDL_GPUTextureSamplerBinding bindings[DrawSamplers] = {
        {_TerrainMesh.Atlas.Handle.get(), _Nearest.get()},
        {_TableTexture.Handle.get(), _Nearest.get()},
        {_AlphaTexture.Handle.get(), _Nearest.get()},
        {_FitTexture.Handle.get(), _Nearest.get()},
        {_PaletteTexture.Handle.get(), _Nearest.get()},
        {hdAtlas, _Nearest.get()},
        {hdAtlas2, _Nearest.get()},
        {reliefAtlas, _Nearest.get()},
        {reliefAtlas2, _Nearest.get()},
    };

    SDL_BindGPUFragmentSamplers(&pass, 0, bindings, DrawSamplers);
    SDL_PushGPUVertexUniformData(&commands, 0, &uniforms, sizeof(uniforms));
    SDL_PushGPUFragmentUniformData(&commands, 0, frameUniforms.data(), static_cast<uint32_t>(frameUniforms.size()));
    SDL_DrawGPUPrimitives(&pass, 6, draw.CellCount, 0, 0);

    // Port-only: with HD ground, every tile again, faded into the ones drawn before it across their seams, once every
    // tile is drawn (terrain.vshader's seam pass, KindTerrainSeam).
    if (_TerrainMesh.HdAtlas.Handle != nullptr)
    {
        uniforms.Pane[2] = 1;
        SDL_BindGPUGraphicsPipeline(&pass, _TerrainSeamPipeline.get());
        SDL_PushGPUVertexUniformData(&commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(&pass, 6, draw.CellCount, 0, 0);
    }

    // The draws that follow read their own buffer.
    SDL_GPUBuffer* const drawBuffer = _DrawBuffer.get();
    SDL_BindGPUVertexStorageBuffers(&pass, 0, &drawBuffer, 1);
}

std::vector<uint8_t> MCVulkanRenderer::GroundLooks(const Surface& surface, const TerrainDraw& draw,
                                                   std::span<const std::byte> frameUniforms) const
{
    std::vector<uint8_t> looks;
    const auto add = [&looks](std::span<const std::byte> bytes)
    {
        const auto* first = reinterpret_cast<const uint8_t*>(bytes.data());
        looks.insert(looks.end(), first, first + bytes.size());
    };

    const auto addValue = [&add](const auto& value) { add(std::as_bytes(std::span(&value, 1))); };
    const MCTerrainFrame& frame = draw.Frame;
    const Surface& fog = _Surfaces[draw.FogIndex];

    for (const uint32_t size : {surface.MappedWidth, surface.MappedHeight, surface.Target.Width, surface.Target.Height,
                                fog.RecordedWidth, fog.RecordedHeight})
    {
        addValue(size);
    }

    for (const int32_t number : {frame.StepX, frame.StepY, frame.ElevStep, frame.MinX, frame.MaxX, frame.MinY,
                                 frame.MaxY, frame.PaneX, frame.PaneY, frame.Clip.X0, frame.Clip.Y0, frame.Clip.X1,
                                 frame.Clip.Y1, frame.AllFilled ? 1 : 0, static_cast<int32_t>(draw.FogIndex)})
    {
        addValue(number);
    }

    // The haze's tables as the table texture holds them.
    for (const uint32_t row : draw.HazeRows)
    {
        if (const size_t at = static_cast<size_t>(row) * 256; at + 256 <= _Tables.size())
        {
            add(std::as_bytes(std::span(_Tables).subspan(at, 256)));
        }
    }

    // The palette, but the colours the cycle shows from others (theirs don't show), and the HD pack's.
    for (size_t i = 0; i < _Colors.size(); ++i)
    {
        const bool cycled = _Cycle.Step >= 0 && i - _Cycle.First < _Cycle.Sources.size();
        const SDL_Color color = cycled ? SDL_Color{} : _Colors[i];
        addValue(std::array{color.r, color.g, color.b});
    }

    addValue(_HdPaletteVersion);
    add(frameUniforms);
    return looks;
}

auto MCVulkanRenderer::PlanGround(uint16_t surfaceIndex, const TerrainDraw& draw, std::span<const uint8_t> looks,
                                  std::span<const uint8_t> seen, int32_t fogColumns, int32_t cycleStep) const
    -> GroundPlan
{
    GroundPlan plan;
    const KeptGround& kept = _KeptGround;
    const Surface& surface = _Surfaces[surfaceIndex];
    const MCTerrainFrame& now = draw.Frame;
    const MCTerrainFrame& then = kept.Frame;

    if (!kept.Held || kept.SurfaceIndex != surfaceIndex || kept.SurfaceKey != surface.Key ||
        kept.Upload != _TerrainMesh.Upload || !std::ranges::equal(kept.Looks, looks) ||
        kept.Seen.size() != seen.size() || kept.FogColumns != fogColumns || fogColumns <= 0 ||
        (kept.CycleStep < 0) != (cycleStep < 0))
    {
        return plan;
    }

    // The view's move (window pixels). The kept pixels move with it when that is a whole number of drawn pixels: the
    // pixels showing a window pixel then move with it (draw.pshader's WindowPixel is in whole numbers), and the
    // shaders' sums there come out the same but for rounding (exactly, when a window pixel is a power of two drawn).
    const int32_t moveX = now.OriginX - then.OriginX;
    const int32_t moveY = now.OriginY - then.OriginY;
    const bool moved = moveX != 0 || moveY != 0;
    const MCRect& clip = now.Clip;
    const auto drawnMove = [](int32_t move, uint32_t window, uint32_t drawn) -> std::optional<int32_t>
    {
        const int64_t scaled = static_cast<int64_t>(move) * drawn;
        return window != 0 && scaled % window == 0 ? std::optional(static_cast<int32_t>(scaled / window))
                                                   : std::nullopt;
    };

    const std::optional<int32_t> shiftX = drawnMove(moveX, surface.MappedWidth, surface.Target.Width);
    const std::optional<int32_t> shiftY = drawnMove(moveY, surface.MappedHeight, surface.Target.Height);

    if (moved && (!shiftX.has_value() || !shiftY.has_value() || std::abs(moveX) > clip.X1 - clip.X0 ||
                  std::abs(moveY) > clip.Y1 - clip.Y0))
    {
        return plan;
    }

    const bool fogChanged = !std::ranges::equal(seen, kept.Seen);
    const bool cycled = cycleStep != kept.CycleStep;
    plan.Whole = false;
    plan.Shift = {*shiftX, *shiftY};

    if (!moved && !fogChanged && !cycled)
    {
        return plan;
    }

    // What the view moved onto (window pixels).
    std::vector<MCRect> parts;

    if (moveX != 0)
    {
        parts.push_back(moveX > 0 ? MCRect{clip.X0, clip.Y0, clip.X0 + moveX - 1, clip.Y1}
                                  : MCRect{clip.X1 + moveX + 1, clip.Y0, clip.X1, clip.Y1});
    }

    if (moveY != 0)
    {
        parts.push_back(moveY > 0 ? MCRect{clip.X0, clip.Y0, clip.X1, clip.Y0 + moveY - 1}
                                  : MCRect{clip.X0, clip.Y1 + moveY + 1, clip.X1, clip.Y1});
    }

    // The indices showing a cycled colour, themselves or through the haze's tables.
    std::bitset<256> cycledColors;

    for (size_t i = 0; cycled && i < _Cycle.Sources.size() && _Cycle.First + i < cycledColors.size(); ++i)
    {
        cycledColors.set(_Cycle.First + i);
    }

    for (const uint32_t row : draw.HazeRows)
    {
        for (size_t i = 0, at = static_cast<size_t>(row) * 256; cycled && i < 256 && at + 256 <= _Tables.size(); ++i)
        {
            if (cycledColors.test(_Tables[at + i]))
            {
                cycledColors.set(i);
            }
        }
    }

    // A cell's draw in a frame (terrain.vshader's tests), with its haze when the fog changed.
    const TerrainMeshState& mesh = _TerrainMesh;
    const int32_t fogRows = static_cast<int32_t>(seen.size()) / fogColumns;
    const auto seenAt = [fogColumns, fogRows](std::span<const uint8_t> flags, int32_t row, int32_t col)
    {
        const auto r = static_cast<uint32_t>(row);
        const uint32_t c = static_cast<uint32_t>(col) & 0xffffu;
        const auto columns = static_cast<uint32_t>(fogColumns);
        const auto rows = static_cast<uint32_t>(fogRows);
        const uint32_t index = columns * r + c;
        return r < rows && c <= columns && index < columns * rows ? static_cast<int32_t>(flags[index]) : 0;
    };

    const auto shapeOf = [&mesh](int32_t row, int32_t col) -> const TerrainTileShape*
    {
        const int32_t r = row - mesh.FirstRow;
        const int32_t c = col - mesh.FirstCol;

        if (r < 0 || c < 0 || r >= mesh.Rows || c >= mesh.Cols)
        {
            return nullptr;
        }

        const uint32_t tile = mesh.CellTiles[static_cast<size_t>(r) * mesh.Cols + c];
        return tile < mesh.Shapes.size() ? &mesh.Shapes[tile] : nullptr;
    };

    const auto cornerOf = [&mesh](int32_t row, int32_t col, size_t corner)
    {
        const uint32_t elevations =
            mesh.CellElevations[static_cast<size_t>(row - mesh.FirstRow) * mesh.Cols + (col - mesh.FirstCol)];
        return static_cast<int32_t>(elevations >> (corner * 8) & 255u);
    };

    // A cell's box in the window (the seam pass's, the widest), from its top-left vertex's point.
    const auto boxOf = [](const MCTerrainFrame& frame, const TerrainTileShape& shape, int32_t x, int32_t y)
    {
        const int32_t atX = frame.PaneX + x - shape.HotX;
        const int32_t atY = frame.PaneY + y - shape.HotY;
        return MCRect{atX - TerrainSeamReach, atY - TerrainSeamReach, atX + shape.Width - 1 + TerrainSeamReach,
                      atY + shape.Height - 1 + TerrainSeamReach};
    };

    const auto drawOf = [&](const MCTerrainFrame& frame, std::span<const uint8_t> flags, int32_t row, int32_t col)
    {
        GroundCellDraw cell;
        const TerrainTileShape* const shape = shapeOf(row, col);

        if (shape == nullptr || row < frame.FirstRow || col < frame.FirstCol || row > frame.LastRow ||
            col > frame.LastCol)
        {
            return cell;
        }

        constexpr std::array<std::array<int32_t, 2>, 4> Corners{{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
        std::array<std::array<int32_t, 2>, 4> points{};
        bool clipped = true;

        for (size_t i = 0; i < Corners.size(); ++i)
        {
            const int32_t r = row + Corners[i][0];
            const int32_t c = col + Corners[i][1];
            points[i] = {frame.OriginX + (c - r) * frame.StepX,
                         frame.OriginY + (r + c) * frame.StepY - cornerOf(row, col, i) * frame.ElevStep};
            clipped = clipped && (points[i][0] < frame.MinX || points[i][0] > frame.MaxX || points[i][1] < frame.MinY ||
                                  points[i][1] > frame.MaxY);
        }

        if (clipped)
        {
            return cell;
        }

        const MCRect box = boxOf(frame, *shape, points[0][0], points[0][1]);
        const MCRect firstBox{box.X0 + TerrainSeamReach, box.Y0 + TerrainSeamReach, box.X1 - TerrainSeamReach,
                              box.Y1 - TerrainSeamReach};
        cell.Seam = !Empty(Intersection(box, frame.Clip));
        cell.First = firstBox.X0 > frame.Clip.X0 - shape->HeaderWidth && !Empty(Intersection(firstBox, frame.Clip));
        cell.Seen = frame.AllFilled ? AllHazed
                    : fogChanged    ? seenAt(flags, row, col) + seenAt(flags, row, col + 1) +
                                          seenAt(flags, row + 1, col + 1) + seenAt(flags, row + 1, col)
                                    : 0;
        return cell;
    };

    // The cells whose draw changed, or whose colours cycled, in either frame's grid.
    for (int32_t row = std::min(now.FirstRow, then.FirstRow); row <= std::max(now.LastRow, then.LastRow); ++row)
    {
        for (int32_t col = std::min(now.FirstCol, then.FirstCol); col <= std::max(now.LastCol, then.LastCol); ++col)
        {
            const GroundCellDraw before = drawOf(then, kept.Seen, row, col);
            const GroundCellDraw after = drawOf(now, seen, row, col);
            const TerrainTileShape* const shape = shapeOf(row, col);

            if (shape != nullptr &&
                (before != after || (after.Seam && (shape->Colors & cycledColors).any()) ||
                 (after.Seam && cycled && (after.Seen == 0 || after.Seen == AllHazed) && cycledColors.test(0x10))))
            {
                const int32_t x = now.OriginX + (col - row) * now.StepX;
                const int32_t y = now.OriginY + (row + col) * now.StepY - cornerOf(row, col, 0) * now.ElevStep;
                parts.push_back(boxOf(now, *shape, x, y));
            }
        }
    }

    // In drawn pixels, a pixel wider each way (one near a window pixel's edge may show either; drawing more again
    // changes nothing), in the clip.
    const auto width = static_cast<int64_t>(surface.Target.Width);
    const auto height = static_cast<int64_t>(surface.Target.Height);
    const auto windowWidth = static_cast<int64_t>(std::max(surface.MappedWidth, 1u));
    const auto windowHeight = static_cast<int64_t>(std::max(surface.MappedHeight, 1u));

    for (const MCRect& part : parts)
    {
        const MCRect cut = Intersection(part, clip);

        if (Empty(cut))
        {
            continue;
        }

        plan.Redrawn.push_back(
            MCRect{static_cast<int32_t>(std::max<int64_t>(cut.X0 * width / windowWidth - 1, 0)),
                   static_cast<int32_t>(std::max<int64_t>(cut.Y0 * height / windowHeight - 1, 0)),
                   static_cast<int32_t>(std::min<int64_t>((cut.X1 + 1) * width / windowWidth + 1, width - 1)),
                   static_cast<int32_t>(std::min<int64_t>((cut.Y1 + 1) * height / windowHeight + 1, height - 1))});
    }

    // Parts drawn as one where their bounds add little, then as few as MostGroundParts.
    for (bool merged = true; merged;)
    {
        merged = false;

        for (size_t i = 0; i < plan.Redrawn.size() && !merged; ++i)
        {
            for (size_t j = i + 1; j < plan.Redrawn.size() && !merged; ++j)
            {
                const MCRect both = Union(plan.Redrawn[i], plan.Redrawn[j]);

                if (Area(both) <= Area(plan.Redrawn[i]) + Area(plan.Redrawn[j]) + GroundMergeSlack)
                {
                    plan.Redrawn[i] = both;
                    plan.Redrawn.erase(plan.Redrawn.begin() + static_cast<ptrdiff_t>(j));
                    merged = true;
                }
            }
        }
    }

    if (plan.Redrawn.size() > MostGroundParts)
    {
        MCRect all{0, 0, -1, -1};

        for (const MCRect& part : plan.Redrawn)
        {
            all = Union(all, part);
        }

        plan.Redrawn.assign(1, all);
    }

    int64_t redrawn = 0;

    for (const MCRect& part : plan.Redrawn)
    {
        redrawn += Area(part);
    }

    if (redrawn * 100 > width * height * WholeGroundPercent)
    {
        return GroundPlan{};
    }

    return plan;
}

void MCVulkanRenderer::DrawKeptGround(SDL_GPUCommandBuffer& commands, SDL_GPURenderPass& pass, const Surface& surface,
                                      const MCRect& clip, KeptGroundMode mode, std::optional<size_t> from,
                                      std::array<int32_t, 2> shift)
{
    const auto width = static_cast<float>(surface.Target.Width);
    const auto height = static_cast<float>(surface.Target.Height);
    const QuadUniforms quad{{0.0f, 0.0f, width, height}, {width, height, 0.0f, 0.0f}};
    const GroundUniforms ground{{shift[0], shift[1], static_cast<int32_t>(mode), 0},
                                {clip.X0, clip.Y0, clip.X1, clip.Y1},
                                {static_cast<int32_t>(std::max(surface.MappedWidth, 1u)),
                                 static_cast<int32_t>(std::max(surface.MappedHeight, 1u)),
                                 static_cast<int32_t>(std::max(surface.Target.Width, 1u)),
                                 static_cast<int32_t>(std::max(surface.Target.Height, 1u))}};
    const SDL_GPUTextureSamplerBinding bindings[2] = {
        {from.has_value() ? _KeptGround.Colors[*from].Handle.get() : _Blank.Handle.get(), _Nearest.get()},
        {from.has_value() ? _KeptGround.Indices[*from].Handle.get() : _Blank.Handle.get(), _Nearest.get()},
    };

    SDL_BindGPUGraphicsPipeline(&pass, mode == KeptGroundMode::Over      ? _GroundOverPipeline.get()
                                       : mode == KeptGroundMode::Indices ? _GroundIndexPipeline.get()
                                                                         : _GroundPipeline.get());
    SDL_BindGPUFragmentSamplers(&pass, 0, bindings, 2);
    SDL_PushGPUVertexUniformData(&commands, 0, &quad, sizeof(quad));
    SDL_PushGPUFragmentUniformData(&commands, 0, &ground, sizeof(ground));
    SDL_DrawGPUPrimitives(&pass, 6, 1, 0, 0);

    // The draws that follow read their own buffer, bound again. SDL's Metal driver takes this draw, which reads none,
    // as having bound one, and binds a buffer again only when it is another: so the terrain's goes first.
    for (SDL_GPUBuffer* const buffer : {_TerrainMesh.Cells.get(), _DrawBuffer.get()})
    {
        if (buffer != nullptr)
        {
            SDL_BindGPUVertexStorageBuffers(&pass, 0, &buffer, 1);
        }
    }
}

// The frame ----------------------------------------------------------------------------------------------------------

void MCVulkanRenderer::TakeCopy(SDL_GPUCommandBuffer& commands, Surface& surface, const MCRect& rect)
{
    surface.Copied = rect;
    surface.DrawnSinceCopy.clear();

    if (Empty(rect))
    {
        return;
    }

    // The drawn pixels showing the rectangle's window pixels, and one more each way (a drawn pixel near the edge may
    // show either side of it).
    const int64_t windowWidth = std::max<int64_t>(surface.MappedWidth, 1);
    const int64_t windowHeight = std::max<int64_t>(surface.MappedHeight, 1);
    const int64_t drawnWidth = surface.Target.Width;
    const int64_t drawnHeight = surface.Target.Height;
    const auto x0 = static_cast<int32_t>(std::max<int64_t>(rect.X0 * drawnWidth / windowWidth - 1, 0));
    const auto y0 = static_cast<int32_t>(std::max<int64_t>(rect.Y0 * drawnHeight / windowHeight - 1, 0));
    const auto x1 = static_cast<int32_t>(
        std::min<int64_t>((static_cast<int64_t>(rect.X1) + 1) * drawnWidth / windowWidth + 1, drawnWidth - 1));
    const auto y1 = static_cast<int32_t>(
        std::min<int64_t>((static_cast<int64_t>(rect.Y1) + 1) * drawnHeight / windowHeight + 1, drawnHeight - 1));

    if (x1 < x0 || y1 < y0)
    {
        return;
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(&commands);
    SDL_GPUTextureLocation from{};
    from.texture = surface.Index.Handle.get();
    from.x = static_cast<uint32_t>(x0);
    from.y = static_cast<uint32_t>(y0);
    SDL_GPUTextureLocation to = from;
    to.texture = surface.Copy.Handle.get();
    SDL_CopyGPUTextureToTexture(pass, &from, &to, static_cast<uint32_t>(x1 - x0 + 1),
                                static_cast<uint32_t>(y1 - y0 + 1), 1, false);
    SDL_EndGPUCopyPass(pass);
}

void MCVulkanRenderer::SetScreenScale(const MCWindow* screen, float scaleX, float scaleY, int32_t right, int32_t bottom)
{
    _ScaledScreen = screen != nullptr && (scaleX > 1.0f || scaleY > 1.0f) ? SurfaceKeyOf(*screen) : MCAddressKey();
    _ScreenScaleX = scaleX;
    _ScreenScaleY = scaleY;
    _ScreenRight = right;
    _ScreenBottom = bottom;
}

void MCVulkanRenderer::PlaceSurfaces(std::span<const MCUnderlay> underlays)
{
    for (Surface& surface : _Surfaces)
    {
        surface.DrawnWidth = surface.RecordedWidth;
        surface.DrawnHeight = surface.RecordedHeight;
        surface.MappedWidth = surface.RecordedWidth;
        surface.MappedHeight = surface.RecordedHeight;

        if (!surface.Key)
        {
            continue;
        }

        // Port-only: a sharp screen, drawn at the size it's shown as far as the view reaches.
        if (surface.Key == _ScaledScreen && surface.RecordedWidth > 0 && surface.RecordedHeight > 0)
        {
            surface.MappedWidth =
                std::clamp(static_cast<uint32_t>(std::max(_ScreenRight, 1)), 1u, surface.RecordedWidth);
            surface.MappedHeight =
                std::clamp(static_cast<uint32_t>(std::max(_ScreenBottom, 1)), 1u, surface.RecordedHeight);
            surface.DrawnWidth = static_cast<uint32_t>(
                std::max(1L, std::lround(static_cast<float>(surface.MappedWidth) * _ScreenScaleX)));
            surface.DrawnHeight = static_cast<uint32_t>(
                std::max(1L, std::lround(static_cast<float>(surface.MappedHeight) * _ScreenScaleY)));
            continue;
        }

        for (const MCUnderlay& underlay : underlays)
        {
            if (SourceSurfaceKey(underlay) == surface.Key && !Empty(underlay.Rect))
            {
                surface.DrawnWidth = static_cast<uint32_t>(std::max(
                    1L, std::lround(static_cast<float>(underlay.Rect.X1 - underlay.Rect.X0 + 1) * _UnderlayScaleX)));
                surface.DrawnHeight = static_cast<uint32_t>(std::max(
                    1L, std::lround(static_cast<float>(underlay.Rect.Y1 - underlay.Rect.Y0 + 1) * _UnderlayScaleY)));
                break;
            }
        }
    }
}

std::expected<void, std::string> MCVulkanRenderer::MakeSurfaceTextures(SDL_GPUCommandBuffer& commands, Surface& surface)
{
    constexpr SDL_GPUTextureUsageFlags target = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    for (auto made : {EnsureTexture(surface.Target, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, target, surface.DrawnWidth,
                                    surface.DrawnHeight),
                      EnsureTexture(surface.Index, IndexFormat, target, surface.DrawnWidth, surface.DrawnHeight),
                      EnsureTexture(surface.Copy, IndexFormat, SDL_GPU_TEXTUREUSAGE_SAMPLER, surface.DrawnWidth,
                                    surface.DrawnHeight),
                      EnsureTexture(surface.Glints, GlintFormat, target, surface.DrawnWidth, surface.DrawnHeight)})
    {
        if (!made)
        {
            return made;
        }
    }

    // Index 0 everywhere, in the colour of palette entry 0; no glints.
    SDL_GPUColorTargetInfo clear[SurfaceTargets]{};
    clear[0].texture = surface.Target.Handle.get();
    clear[0].clear_color = SDL_FColor{_Colors[0].r / 255.0f, _Colors[0].g / 255.0f, _Colors[0].b / 255.0f, 1.0f};
    clear[1].texture = surface.Index.Handle.get();
    clear[2].texture = surface.Glints.Handle.get();

    for (SDL_GPUColorTargetInfo& info : clear)
    {
        info.load_op = SDL_GPU_LOADOP_CLEAR;
        info.store_op = SDL_GPU_STOREOP_STORE;
    }

    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(&commands, clear, SurfaceTargets, nullptr));
    surface.Copied = MCRect{0, 0, -1, -1};
    surface.DrawnSinceCopy.clear();
    return {};
}

std::expected<void, std::string> MCVulkanRenderer::Execute(SDL_GPUCommandBuffer& commands,
                                                           std::span<const MCUnderlay> underlays)
{
    if (!_Recording && _Uploads.empty() && _BufferUploads.empty())
    {
        return {};
    }

    BeginRecording();
    PlaceSurfaces(underlays);

    // The new tables (before the palette goes up: a colour change refits them all).
    if (auto uploaded = UploadTables(); !uploaded)
    {
        return uploaded;
    }

    if (_AlphaColorsChanged)
    {
        UploadAlphaColors();
    }

    // The HD art pack's colours go in the palette's second row; they change with the pack in use.
    if (const uint64_t pack = MCHdArt::Active() ? MCHdArt::PackVersion() : 0; pack != _HdPaletteVersion)
    {
        _HdPaletteVersion = pack;
        _ColorsChanged = true;
    }

    if (_ColorsChanged)
    {
        const std::span<uint8_t> palette = QueueUpload(_PaletteTexture.Handle, 0, 0, 256, PaletteRows, 4);
        const auto& madeWith = _HdPaletteVersion != 0 ? MCHdArt::MadeWith() : std::nullopt;

        for (size_t i = 0; i < 256; ++i)
        {
            const std::span<uint8_t> shown = palette.subspan(i * 4, 4);
            const std::span<uint8_t> then = palette.subspan((256 + i) * 4, 4);
            shown[0] = _Colors[i].r;
            shown[1] = _Colors[i].g;
            shown[2] = _Colors[i].b;
            shown[3] = 255;

            if (madeWith.has_value())
            {
                std::copy_n(madeWith->begin() + static_cast<ptrdiff_t>(i * 3), 3, then.begin());
                then[3] = 255;
            }
            else
            {
                std::ranges::copy(shown, then.begin());
            }
        }

        _ColorsChanged = false;
    }

    // The frame's written pixels.
    if (_StripRows > 0)
    {
        if (_Strip.Handle == nullptr || _Strip.Height < _StripRows)
        {
            if (auto made = EnsureTexture(_Strip, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                          StripWidth, std::bit_ceil(std::max(_StripRows, 64u)));
                !made)
            {
                return made;
            }
        }

        std::memcpy(QueueUpload(_Strip.Handle, 0, 0, StripWidth, _StripRows, 1).data(), _StripPixels.data(),
                    _StripPixels.size());
    }

    // Everything up in one transfer: the uploads, then the draws.
    const size_t drawOffset = (_Staging.size() + UploadAlignment - 1) & ~(UploadAlignment - 1);
    const size_t drawBytes = _Draws.size() * sizeof(_Draws[0]);
    const size_t total = drawOffset + drawBytes;

    if (_Transfer == nullptr || _TransferSize < total)
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<uint32_t>(total + total / 4);
        _Transfer = MCGpuHandle<SDL_GPUTransferBuffer>(SDL_CreateGPUTransferBuffer(_Device.get(), &info),
                                                       MCGpuRelease{_Device});
        _TransferSize = _Transfer != nullptr ? info.size : 0;

        if (_Transfer == nullptr)
        {
            return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer"));
        }
    }

    if (drawBytes > 0 && (_DrawBuffer == nullptr || _DrawBufferSize < drawBytes))
    {
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        info.size = static_cast<uint32_t>(drawBytes + drawBytes / 2);
        _DrawBuffer = MCGpuHandle<SDL_GPUBuffer>(SDL_CreateGPUBuffer(_Device.get(), &info), MCGpuRelease{_Device});
        _DrawBufferSize = _DrawBuffer != nullptr ? info.size : 0;

        if (_DrawBuffer == nullptr)
        {
            return std::unexpected(SdlError("SDL_CreateGPUBuffer"));
        }
    }

    void* const memory = SDL_MapGPUTransferBuffer(_Device.get(), _Transfer.get(), true);

    if (memory == nullptr)
    {
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer"));
    }

    const std::span<uint8_t> mapped(static_cast<uint8_t*>(memory), total);
    std::ranges::copy(_Staging, mapped.begin());

    if (drawBytes > 0)
    {
        std::memcpy(mapped.subspan(drawOffset, drawBytes).data(), _Draws.data(), drawBytes);
    }

    SDL_UnmapGPUTransferBuffer(_Device.get(), _Transfer.get());
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(&commands);

    for (const Upload& upload : _Uploads)
    {
        if (upload.Texture == nullptr)
        {
            continue;
        }

        if (upload.From != nullptr)
        {
            SDL_GPUTextureLocation from{};
            from.texture = upload.From.get();
            from.x = upload.X;
            from.y = upload.Y;
            SDL_GPUTextureLocation to{};
            to.texture = upload.Texture.get();
            to.x = upload.X;
            to.y = upload.Y;
            SDL_CopyGPUTextureToTexture(copyPass, &from, &to, upload.Width, upload.Height, 1, false);
            continue;
        }

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = upload.Buffer != nullptr ? upload.Buffer.get() : _Transfer.get();
        source.offset = static_cast<uint32_t>(upload.Offset);
        source.pixels_per_row = upload.RowPixels != 0 ? upload.RowPixels : upload.Width;
        source.rows_per_layer = upload.Height;
        SDL_GPUTextureRegion destination{};
        destination.texture = upload.Texture.get();
        destination.x = upload.X;
        destination.y = upload.Y;
        destination.w = upload.Width;
        destination.h = upload.Height;
        destination.d = 1;
        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
    }

    if (drawBytes > 0)
    {
        SDL_GPUTransferBufferLocation source{_Transfer.get(), static_cast<uint32_t>(drawOffset)};
        SDL_GPUBufferRegion destination{_DrawBuffer.get(), 0, static_cast<uint32_t>(drawBytes)};
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }

    for (const BufferUpload& upload : _BufferUploads)
    {
        SDL_GPUTransferBufferLocation source{_Transfer.get(), static_cast<uint32_t>(upload.Offset)};
        SDL_GPUBufferRegion destination{upload.Buffer.get(), 0, upload.Size};
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    }

    SDL_EndGPUCopyPass(copyPass);

    // A surface now drawn at another size (its window or the rectangle it's shown in changed) starts again.
    for (Surface& surface : _Surfaces)
    {
        if (surface.Key && surface.Target.Handle != nullptr &&
            (surface.Target.Width != surface.DrawnWidth || surface.Target.Height != surface.DrawnHeight))
        {
            if (auto made = MakeSurfaceTextures(commands, surface); !made)
            {
                return made;
            }
        }
    }

    // The kept surfaces first (the fog of war, which the terrain layer reads), then those shown as underlays (the
    // screen's see-through draws read them finished), then the rest.
    const auto rank = [&](uint16_t index)
    {
        const Surface& ranked = _Surfaces[index];

        if (const std::shared_ptr<MCWindow> window = ranked.Window.lock();
            ranked.Key && window != nullptr && MCRenderer::KeptSurface(*window))
        {
            return 0;
        }

        return std::ranges::any_of(underlays,
                                   [&](const MCUnderlay& underlay) { return SourceSurfaceKey(underlay) == ranked.Key; })
                   ? 1
                   : 2;
    };

    std::vector<int32_t> ranks(_Surfaces.size());

    for (size_t i = 0; i < _Surfaces.size(); ++i)
    {
        ranks[i] = rank(static_cast<uint16_t>(i));
    }

    std::vector<uint32_t> order(_Records.size());
    std::iota(order.begin(), order.end(), 0u);
    std::ranges::stable_sort(order, [&](uint32_t a, uint32_t b)
                             { return ranks[_Records[a].SurfaceIndex] < ranks[_Records[b].SurfaceIndex]; });

    SDL_GPURenderPass* pass = nullptr;
    int32_t passSurface = -1;
    FrameUniforms passFrame{};
    // Port-only: what the pass has bound and the command buffer has pushed (none: not yet), so a draw call binding
    // the same as the one before binds nothing again.
    SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
    std::optional<std::array<SDL_GPUTextureSamplerBinding, DrawSamplers>> boundSamplers;
    std::optional<FrameUniforms> pushedFrame;
    const auto forgetBound = [&]
    {
        boundPipeline = nullptr;
        boundSamplers.reset();
        pushedFrame.reset();
    };

    // The surfaces a pass has begun on this frame.
    std::vector<bool> begun(_Surfaces.size(), false);
    const auto endPass = [&]
    {
        if (pass != nullptr)
        {
            SDL_EndGPURenderPass(pass);
            pass = nullptr;
            passSurface = -1;
        }
    };

    // A pass's frame uniforms: the frame's colours; on a surface shown over an underlay, the key is drawn transparent.
    const auto framePass = [&](const Surface& surface)
    {
        FrameUniforms frame{};
        frame.Cycle[0] = _Cycle.First;
        frame.Cycle[1] = _Cycle.Step;
        frame.Cycle[2] = std::ranges::any_of(underlays, [&](const MCUnderlay& underlay)
                                             { return surface.Key && TargetSurfaceKey(underlay) == surface.Key; })
                             ? 1
                             : 0;

        for (size_t i = 0; i < 8; ++i)
        {
            frame.CycleSources[i] = _Cycle.Sources[i];
        }

        frame.Look[0] =
            GHdLighting != HdLightingOff ? std::clamp(MCSceneLights::ShownLighting().GlowOvals, 0.0f, 1.0f) : 1.0f;
        frame.Look[1] = GHdSunShadows != 0 ? std::clamp(MCSceneLights::ShownLighting().SunShadow, 0.0f, 1.0f) : 0.0f;
        frame.Look[2] = MCSceneLights::RecastsSunShadows() ? 1.0f : 0.0f;
        return frame;
    };

    // The target's scale, and the source's when it is a surface.
    const auto scaleOf = [](const Surface& of, std::span<int32_t, 4> out)
    {
        out[0] = static_cast<int32_t>(std::max(of.MappedWidth, 1u));
        out[1] = static_cast<int32_t>(std::max(of.MappedHeight, 1u));
        out[2] = static_cast<int32_t>(std::max(of.Target.Width, 1u));
        out[3] = static_cast<int32_t>(std::max(of.Target.Height, 1u));
    };

    // A record's frame uniforms on a surface: the pass's, with the target's scale and the source's.
    const auto recordFrame = [&](const Surface& surface, const Record& record)
    {
        FrameUniforms frame = passFrame;
        scaleOf(surface, frame.Scale);
        std::ranges::fill(frame.SourceScale, 1);

        if (record.Source == SourceKind::Surface && _Surfaces[record.SourceIndex].Index.Handle != nullptr)
        {
            scaleOf(_Surfaces[record.SourceIndex], frame.SourceScale);
        }
        else if (record.Source == SourceKind::TargetCopy)
        {
            scaleOf(surface, frame.SourceScale);
        }

        return frame;
    };

    // Draws `count` draws from `record`'s first on, which share its bindings, in render pass `into` on `surface`.
    const auto drawBatch = [&](SDL_GPURenderPass* into, Surface& surface, const Record& record, uint32_t count)
    {
        SDL_GPUTexture* source = _Blank.Handle.get();

        switch (record.Source)
        {
            case SourceKind::None:
                break;
            case SourceKind::Atlas:
            {
                source = _Pages[record.SourceIndex].Texture.Handle.get();
                break;
            }
            case SourceKind::Picture:
            {
                source = _FramePictures[record.SourceIndex].get();
                break;
            }
            case SourceKind::Surface:
            {
                if (_Surfaces[record.SourceIndex].Index.Handle != nullptr)
                {
                    source = _Surfaces[record.SourceIndex].Index.Handle.get();
                }
                break;
            }
            case SourceKind::TargetCopy:
            {
                source = surface.Copy.Handle.get();
                break;
            }
            case SourceKind::Strip:
            {
                source = _Strip.Handle.get();
                break;
            }
        }

        SDL_GPUTexture* hdColors = _PaletteTexture.Handle.get();

        if (record.Source == SourceKind::Picture)
        {
            if (const auto hd = _FrameHdColors.find(record.SourceIndex); hd != _FrameHdColors.end())
            {
                hdColors = hd->second.get();
            }
        }

        const std::array<SDL_GPUTextureSamplerBinding, DrawSamplers> bindings{{
            {source, _Nearest.get()},
            {_TableTexture.Handle.get(), _Nearest.get()},
            {_AlphaTexture.Handle.get(), _Nearest.get()},
            {_FitTexture.Handle.get(), _Nearest.get()},
            {_PaletteTexture.Handle.get(), _Nearest.get()},
            {hdColors, _Nearest.get()},
            {hdColors, _Nearest.get()},
            {_PaletteTexture.Handle.get(), _Nearest.get()},
            {_PaletteTexture.Handle.get(), _Nearest.get()},
        }};

        if (SDL_GPUGraphicsPipeline* const pipeline =
                record.Shadow ? _ShadowPipeline.get() : _Pipelines[static_cast<size_t>(record.Variant)].For(record);
            pipeline != boundPipeline)
        {
            SDL_BindGPUGraphicsPipeline(into, pipeline);
            boundPipeline = pipeline;
        }

        const auto sameBinding = [](const SDL_GPUTextureSamplerBinding& a, const SDL_GPUTextureSamplerBinding& b)
        { return a.texture == b.texture && a.sampler == b.sampler; };

        if (!boundSamplers.has_value() || !std::ranges::equal(*boundSamplers, bindings, sameBinding))
        {
            SDL_BindGPUFragmentSamplers(into, 0, bindings.data(), DrawSamplers);
            boundSamplers = bindings;
        }

        const BatchUniforms batch{{static_cast<float>(surface.MappedWidth), static_cast<float>(surface.MappedHeight),
                                   static_cast<float>(surface.Target.Width), static_cast<float>(surface.Target.Height)},
                                  {record.First, 0, 0, 0}};
        SDL_PushGPUVertexUniformData(&commands, 0, &batch, sizeof(batch));

        if (const FrameUniforms frame = recordFrame(surface, record);
            !pushedFrame.has_value() || std::memcmp(&*pushedFrame, &frame, sizeof(frame)) != 0)
        {
            SDL_PushGPUFragmentUniformData(&commands, 0, &frame, sizeof(frame));
            pushedFrame = frame;
        }

        SDL_DrawGPUPrimitives(into, 6, count, 0, 0);
    };

    // A render pass's start on a surface's size: the viewport, and the draws' buffer.
    const auto startPass = [&](SDL_GPURenderPass* started, const Surface& surface)
    {
        forgetBound();
        const SDL_GPUViewport viewport{
            0.0f, 0.0f, static_cast<float>(surface.Target.Width), static_cast<float>(surface.Target.Height),
            0.0f, 1.0f};
        SDL_SetGPUViewport(started, &viewport);
        SDL_GPUBuffer* const drawBuffer = _DrawBuffer.get();
        SDL_BindGPUVertexStorageBuffers(started, 0, &drawBuffer, 1);
    };

    // Port-only: the terrain layers each surface draws this frame (the kept ground is a surface's one), and the
    // surface whose kept ground holds the frame's, for its terrain layer to copy in.
    std::vector<uint32_t> terrainLayers(_Surfaces.size(), 0);
    int32_t keptSurface = -1;

    for (const Record& record : _Records)
    {
        terrainLayers[record.SurfaceIndex] += record.TerrainDraw >= 0 ? 1 : 0;
    }

    // Port-only: brings the kept ground up to date for terrain layer `draw` on `surface` (KeptGround), in a render pass
    // of its own before the surface's; whether it then holds the frame's ground.
    const auto keepGround = [&](Surface& surface, uint16_t surfaceIndex, const TerrainDraw& draw)
    {
        const std::shared_ptr<MCWindow> fog = _Surfaces[draw.FogIndex].Window.lock();

        if (fog == nullptr || fog->Bytes().empty())
        {
            return false;
        }

        std::vector<uint8_t> seen(fog->Bytes().size());
        std::ranges::transform(fog->Bytes(), seen.begin(),
                               [](uint8_t flag) { return flag == UINT8_MAX ? uint8_t{1} : uint8_t{0}; });
        const int32_t fogColumns = fog->XMax + 1;
        // The terrain's frame uniforms (a record with no source's).
        const FrameUniforms frame = recordFrame(surface, Record{});
        FrameUniforms unstepped = frame;
        unstepped.Cycle[1] = 0;
        std::vector<uint8_t> looks = GroundLooks(surface, draw, std::as_bytes(std::span(&unstepped, 1)));
        const GroundPlan plan = PlanGround(surfaceIndex, draw, looks, seen, fogColumns, _Cycle.Step);
        KeptGround& kept = _KeptGround;
        const std::array<int32_t, 2> origin{draw.Frame.OriginX, draw.Frame.OriginY};
        const bool moving = origin != kept.LastOrigin;
        kept.LastOrigin = origin;

        // Drawn whole while the view moves, the next frame would draw it whole again: drawn directly instead.
        if (plan.Whole && moving)
        {
            return false;
        }

        const bool shifted = !plan.Whole && (plan.Shift[0] != 0 || plan.Shift[1] != 0);

        if (plan.Whole || shifted || !plan.Redrawn.empty())
        {
            // Moved: made in the other pair from this one, shifted; else drawn again in place.
            const size_t into = shifted ? 1 - kept.Current : kept.Current;
            constexpr SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            const uint32_t width = surface.Target.Width;
            const uint32_t height = surface.Target.Height;

            for (auto made :
                 {EnsureTexture(kept.Colors[into], SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage, width, height),
                  EnsureTexture(kept.Indices[into], IndexFormat, usage, width, height),
                  EnsureTexture(kept.Glints, GlintFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, width, height)})
            {
                if (!made)
                {
                    SDL_Log("MCVulkanRenderer: the ground isn't kept: %s", made.error().c_str());

                    for (size_t i = 0; i < kept.Colors.size(); ++i)
                    {
                        Release(kept.Colors[i]);
                        Release(kept.Indices[i]);
                    }

                    Release(kept.Glints);
                    kept.Held = false;
                    return false;
                }
            }

            // Drawn whole over nothing; shifted, every pixel is copied or drawn again; else kept but where drawn again.
            SDL_GPUColorTargetInfo targets[SurfaceTargets]{};
            targets[0].texture = kept.Colors[into].Handle.get();
            targets[1].texture = kept.Indices[into].Handle.get();
            targets[2].texture = kept.Glints.Handle.get();

            for (SDL_GPUColorTargetInfo& target : targets)
            {
                target.load_op = plan.Whole ? SDL_GPU_LOADOP_CLEAR
                                 : shifted  ? SDL_GPU_LOADOP_DONT_CARE
                                            : SDL_GPU_LOADOP_LOAD;
                target.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f};
                target.store_op = SDL_GPU_STOREOP_STORE;
            }

            targets[2].load_op = SDL_GPU_LOADOP_DONT_CARE;
            targets[2].store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_GPURenderPass* const keeping = SDL_BeginGPURenderPass(&commands, targets, SurfaceTargets, nullptr);
            startPass(keeping, surface);

            if (shifted)
            {
                DrawKeptGround(commands, *keeping, surface, draw.Frame.Clip, KeptGroundMode::Copy, kept.Current,
                               plan.Shift);
            }

            if (plan.Whole)
            {
                DrawTerrain(commands, *keeping, surface, draw, std::as_bytes(std::span(&frame, 1)));
            }

            for (const MCRect& part : plan.Redrawn)
            {
                const SDL_Rect scissor{part.X0, part.Y0, part.X1 - part.X0 + 1, part.Y1 - part.Y0 + 1};
                SDL_SetGPUScissor(keeping, &scissor);
                DrawKeptGround(commands, *keeping, surface, draw.Frame.Clip, KeptGroundMode::Clear);
                DrawTerrain(commands, *keeping, surface, draw, std::as_bytes(std::span(&frame, 1)));
            }

            SDL_EndGPURenderPass(keeping);
            forgetBound();
            kept.Current = into;
        }

        kept.Held = true;
        kept.SurfaceIndex = surfaceIndex;
        kept.SurfaceKey = surface.Key;
        kept.Upload = _TerrainMesh.Upload;
        kept.Frame = draw.Frame;
        kept.Seen = std::move(seen);
        kept.FogColumns = fogColumns;
        kept.CycleStep = _Cycle.Step;
        kept.Looks = std::move(looks);
        return true;
    };

    for (size_t k = 0; k < order.size(); ++k)
    {
        const Record& record = _Records[order[k]];
        Surface& surface = _Surfaces[record.SurfaceIndex];

        if (record.Resize)
        {
            endPass();

            // A new surface starts as zeros, as the window's new pixels do.
            if (auto made = MakeSurfaceTextures(commands, surface); !made)
            {
                return made;
            }

            continue;
        }

        if (surface.Target.Handle == nullptr)
        {
            continue;
        }

        if (record.ReadsCopy)
        {
            const bool covered = Contains(surface.Copied, record.Reads) &&
                                 std::ranges::none_of(surface.DrawnSinceCopy, [&](const MCRect& drawn)
                                                      { return Overlaps(drawn, record.Reads); });

            if (!covered)
            {
                // One copy for this and the draws reading the target that follow it, as long as none reads what
                // another of them draws.
                MCRect bounds = record.Reads;
                std::vector<MCRect> groupWrites{record.Writes};

                for (size_t j = k + 1; j < order.size(); ++j)
                {
                    const Record& next = _Records[order[j]];

                    if (next.Resize || next.SurfaceIndex != record.SurfaceIndex || !next.ReadsCopy ||
                        std::ranges::any_of(groupWrites,
                                            [&](const MCRect& drawn) { return Overlaps(drawn, next.Reads); }))
                    {
                        break;
                    }

                    bounds = Union(bounds, next.Reads);
                    groupWrites.push_back(next.Writes);
                }

                endPass();
                TakeCopy(commands, surface, bounds);
            }
        }

        if (passSurface != record.SurfaceIndex)
        {
            endPass();
            SDL_GPUColorTargetInfo targets[SurfaceTargets]{};
            targets[0].texture = surface.Target.Handle.get();
            targets[1].texture = surface.Index.Handle.get();
            targets[2].texture = surface.Glints.Handle.get();

            // Port-only: a surface's first pass of the frame starting with a fill of all of it (the world view's
            // wipe) needn't load what the surface held, every texel of which the fill writes.
            const bool firstPass = !begun[record.SurfaceIndex];
            const bool overwritten = firstPass && FillsSurface(record, surface);
            begun[record.SurfaceIndex] = true;
            passFrame = framePass(surface);

            // Port-only: with HD ground, a surface's one terrain layer, drawn first on it (over what it held), comes
            // from the kept ground (KeptGround); not on a surface whose key is drawn transparent, which the kept
            // ground's coverage would leave out.
            if (firstPass && record.TerrainDraw >= 0 && _TerrainMesh.HdAtlas.Handle != nullptr &&
                terrainLayers[record.SurfaceIndex] == 1 && passFrame.Cycle[2] == 0)
            {
                keptSurface =
                    keepGround(surface, record.SurfaceIndex, _TerrainDraws[static_cast<size_t>(record.TerrainDraw)])
                        ? record.SurfaceIndex
                        : -1;
            }

            for (SDL_GPUColorTargetInfo& target : targets)
            {
                target.load_op = overwritten ? SDL_GPU_LOADOP_DONT_CARE : SDL_GPU_LOADOP_LOAD;
                target.store_op = SDL_GPU_STOREOP_STORE;
            }

            pass = SDL_BeginGPURenderPass(&commands, targets, SurfaceTargets, nullptr);
            passSurface = record.SurfaceIndex;
            startPass(pass, surface);
        }

        if (record.TerrainDraw >= 0)
        {
            const TerrainDraw& draw = _TerrainDraws[static_cast<size_t>(record.TerrainDraw)];

            if (keptSurface == record.SurfaceIndex)
            {
                DrawKeptGround(commands, *pass, surface, draw.Frame.Clip, KeptGroundMode::Over, _KeptGround.Current);
                DrawKeptGround(commands, *pass, surface, draw.Frame.Clip, KeptGroundMode::Indices, _KeptGround.Current);
                keptSurface = -1;
            }
            else
            {
                const FrameUniforms frame = recordFrame(surface, record);
                DrawTerrain(commands, *pass, surface, draw, std::as_bytes(std::span(&frame, 1)));
            }

            // The terrain binds and pushes its own.
            forgetBound();
            surface.DrawnSinceCopy.push_back(record.Writes);
            continue;
        }

        // Records that follow on with the same bindings, and need no new copy, go in the same draw call.
        uint32_t count = record.Count;
        size_t last = k;

        while (last + 1 < order.size())
        {
            const Record& next = _Records[order[last + 1]];
            const Record& previous = _Records[order[last]];

            if (next.Resize || next.TerrainDraw >= 0 || next.SurfaceIndex != record.SurfaceIndex ||
                next.Source != record.Source || next.SourceIndex != record.SourceIndex || next.Blend != record.Blend ||
                next.IndexOnly != record.IndexOnly || next.Shadow != record.Shadow || next.Marks != record.Marks ||
                next.Variant != record.Variant || next.ReadsCopy || record.ReadsCopy ||
                next.First != previous.First + previous.Count)
            {
                break;
            }

            count += next.Count;
            ++last;
        }

        drawBatch(pass, surface, record, count);

        for (size_t i = k; i <= last; ++i)
        {
            surface.DrawnSinceCopy.push_back(_Records[order[i]].Writes);
        }

        if (surface.DrawnSinceCopy.size() > 64)
        {
            MCRect all{0, 0, -1, -1};

            for (const MCRect& drawn : surface.DrawnSinceCopy)
            {
                all = Union(all, drawn);
            }

            surface.DrawnSinceCopy.assign(1, all);
        }

        k = last;
    }

    endPass();

    for (Surface& surface : _Surfaces)
    {
        surface.LastBlended = std::move(surface.Blended);
        surface.Blended.clear();

        if (!surface.Key)
        {
            Release(surface.Target);
            Release(surface.Index);
            Release(surface.Copy);
            Release(surface.Glints);
        }
    }

    _Records.clear();
    _Draws.clear();
    _Uploads.clear();
    _BufferUploads.clear();
    _TerrainDraws.clear();
    _Staging.clear();
    _FramePictures.clear();
    _FrameHdColors.clear();
    _StripPixels.clear();
    _StripRows = 0;

    // The commands that read the retired textures are submitted (SDL frees them once the GPU is done).
    for (Texture& texture : _Retired)
    {
        Release(texture);
    }

    _Retired.clear();

    _RetiredTransfers.clear();

    if (MCFrameLog::Enabled() && (_FrameUploads.Pictures > 0 || _FrameUploads.AtlasImages > 0))
    {
        MCFrameLog::Note(std::format("GPU uploads: {} pictures ({} bytes), {} atlas images ({} bytes)",
                                     _FrameUploads.Pictures, _FrameUploads.PictureBytes, _FrameUploads.AtlasImages,
                                     _FrameUploads.AtlasBytes));
    }

    _LastFrameUploads = _FrameUploads;
    _FrameUploads = UploadTally{};
    _Recording = false;
    ++_Frame;
    return {};
}

void MCVulkanRenderer::OnShowHdPicture(MCTexture& texture, std::shared_ptr<const MCHdArt::Frame> frame, int32_t x,
                                       int32_t y)
{
    TextureState& state = StateOf(texture);

    // A movie shows each frame once: the one shown before leaves the GPU now rather than when unused for a while.
    if (state.ShownHd != nullptr && state.ShownHd != frame)
    {
        ForgetHdPicture(state.ShownHd->Id);
    }

    state.ShownHd = std::move(frame);
    state.ShownHdX = x;
    state.ShownHdY = y;
}

void MCVulkanRenderer::OnTextureReleased(MCTexture& texture)
{
    if (texture.Hardware == nullptr)
    {
        return;
    }

    auto& state = static_cast<TextureState&>(*texture.Hardware);

    if (state.ShownHd != nullptr)
    {
        ForgetHdPicture(state.ShownHd->Id);
    }

    if (state.Copy.Handle != nullptr)
    {
        _Retired.push_back(state.Copy);
    }

    if (state.Stream != nullptr)
    {
        _RetiredTransfers.push_back(state.Stream);
    }

    texture.Hardware.reset();
    texture.Dirty = true;
}

void MCVulkanRenderer::OnFrameSurfaceRemoved(const MCWindow& window)
{
    const MCAddressKey key = MCAddressKey::Of(window);

    for (Surface& surface : _Surfaces)
    {
        if (surface.Key == key)
        {
            surface.Key = MCAddressKey();
            surface.Window.reset();
            surface.RecordedWidth = 0;
            surface.RecordedHeight = 0;
        }
    }
}

std::shared_ptr<SDL_GPUTexture> MCVulkanRenderer::SurfaceTexture(const MCWindow& window, uint32_t& width,
                                                                 uint32_t& height) const
{
    const MCAddressKey canonical = SurfaceKeyOf(window);

    for (const Surface& surface : _Surfaces)
    {
        if (surface.Key == canonical && surface.Target.Handle != nullptr)
        {
            width = surface.Target.Width;
            height = surface.Target.Height;
            return surface.Target.Handle;
        }
    }

    width = 0;
    height = 0;
    return nullptr;
}

std::shared_ptr<SDL_GPUTexture> MCVulkanRenderer::SurfaceIndexTexture(const MCWindow& window) const
{
    const MCAddressKey canonical = SurfaceKeyOf(window);
    const auto found = std::ranges::find_if(_Surfaces, [&](const Surface& surface)
                                            { return surface.Key == canonical && surface.Index.Handle != nullptr; });
    return found != _Surfaces.end() ? found->Index.Handle : nullptr;
}

std::shared_ptr<SDL_GPUTexture> MCVulkanRenderer::SurfaceGlintTexture(const MCWindow& window) const
{
    const MCAddressKey canonical = SurfaceKeyOf(window);
    const auto found = std::ranges::find_if(_Surfaces, [&](const Surface& surface)
                                            { return surface.Key == canonical && surface.Glints.Handle != nullptr; });
    return found != _Surfaces.end() ? found->Glints.Handle : nullptr;
}

std::pair<float, float> MCVulkanRenderer::SurfaceScale(const MCWindow& window) const
{
    const MCAddressKey canonical = SurfaceKeyOf(window);

    for (const Surface& surface : _Surfaces)
    {
        if (surface.Key == canonical && surface.Target.Handle != nullptr && surface.MappedWidth > 0 &&
            surface.MappedHeight > 0)
        {
            return {static_cast<float>(surface.Target.Width) / static_cast<float>(surface.MappedWidth),
                    static_cast<float>(surface.Target.Height) / static_cast<float>(surface.MappedHeight)};
        }
    }

    return {1.0f, 1.0f};
}

std::expected<void, std::string> MCVulkanRenderer::Flush(std::span<const MCUnderlay> underlays)
{
    if (!_Recording && _Uploads.empty() && _BufferUploads.empty())
    {
        return {};
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));
    }

    if (auto executed = Execute(*commands, underlays); !executed)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return executed;
    }

    if (!SDL_SubmitGPUCommandBuffer(commands))
    {
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBuffer"));
    }

    return {};
}

auto MCVulkanRenderer::Download(std::span<const std::reference_wrapper<const Surface>> surfaces)
    -> std::expected<std::vector<std::vector<uint8_t>>, std::string>
{
    std::vector<std::vector<uint8_t>> pixels(surfaces.size());

    if (surfaces.empty())
    {
        return pixels;
    }

    std::vector<uint32_t> offsets;
    uint32_t total = 0;

    for (const Surface& surface : surfaces)
    {
        offsets.push_back(total);
        total += surface.Index.Width * surface.Index.Height * IndexPixelBytes;
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));
    }

    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = total;
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(_Device.get(), &info);

    if (buffer == nullptr)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer(download)"));
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(commands);

    for (size_t i = 0; i < surfaces.size(); ++i)
    {
        SDL_GPUTextureRegion source{};
        const Surface& surface = surfaces[i];
        source.texture = surface.Index.Handle.get();
        source.w = surface.Index.Width;
        source.h = surface.Index.Height;
        source.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = buffer;
        destination.offset = offsets[i];
        destination.pixels_per_row = surface.Index.Width;
        destination.rows_per_layer = surface.Index.Height;
        SDL_DownloadFromGPUTexture(pass, &source, &destination);
    }

    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);

    if (fence == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(_Device.get(), buffer);
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBufferAndAcquireFence"));
    }

    SDL_WaitForGPUFences(_Device.get(), true, &fence, 1);
    SDL_ReleaseGPUFence(_Device.get(), fence);
    const void* const memory = SDL_MapGPUTransferBuffer(_Device.get(), buffer, false);

    if (memory == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(_Device.get(), buffer);
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer(download)"));
    }

    const std::span<const uint8_t> mapped(static_cast<const uint8_t*>(memory), total);

    for (size_t i = 0; i < surfaces.size(); ++i)
    {
        // The palette indices: each pixel's R.
        const Surface& surface = surfaces[i];
        const size_t size = static_cast<size_t>(surface.Index.Width) * surface.Index.Height;
        const std::span<const uint8_t> surfacePixels = mapped.subspan(offsets[i], size * IndexPixelBytes);
        pixels[i].resize(size);

        for (size_t pixel = 0; pixel < size; ++pixel)
        {
            pixels[i][pixel] = surfacePixels[pixel * IndexPixelBytes];
        }
    }

    SDL_UnmapGPUTransferBuffer(_Device.get(), buffer);
    SDL_ReleaseGPUTransferBuffer(_Device.get(), buffer);
    return pixels;
}

auto MCVulkanRenderer::ReadShown(const MCWindow& screen, std::span<const MCUnderlay> underlays)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    if (auto flushed = Flush(underlays); !flushed)
    {
        return std::unexpected(flushed.error());
    }

    // The surface whose window has the address (with its indices), as an index into the surfaces.
    const auto surfaceOf = [this](MCAddressKey canonical) -> std::optional<size_t>
    {
        for (size_t i = 0; i < _Surfaces.size(); ++i)
        {
            if (canonical && _Surfaces[i].Key == canonical && _Surfaces[i].Index.Handle != nullptr)
            {
                return i;
            }
        }

        return std::nullopt;
    };

    const std::optional<size_t> screenIndex = surfaceOf(SurfaceKeyOf(screen));

    if (!screenIndex.has_value())
    {
        return std::vector<uint8_t>{};
    }

    const Surface& screenSurface = _Surfaces[*screenIndex];
    const int32_t width = screen.XMax + 1;
    const int32_t height = screen.YMax + 1;
    const bool scaledScreen = screenSurface.Index.Width != static_cast<uint32_t>(width) ||
                              screenSurface.Index.Height != static_cast<uint32_t>(height);

    if (scaledScreen && (screenSurface.RecordedWidth != static_cast<uint32_t>(width) ||
                         screenSurface.RecordedHeight != static_cast<uint32_t>(height)))
    {
        return std::unexpected(std::format("the screen's surface is {}x{}, the screen {}x{}",
                                           screenSurface.RecordedWidth, screenSurface.RecordedHeight, width, height));
    }

    // The screen and the world surfaces under it, in the order the composite draws them (a later one over an earlier).
    struct Shown
    {
        MCRect Rect;
        size_t World;
    };

    std::vector<size_t> wanted{*screenIndex};
    std::vector<Shown> shown;

    for (const MCUnderlay& underlay : underlays)
    {
        const std::optional<size_t> world = surfaceOf(SourceSurfaceKey(underlay));

        if (TargetSurfaceKey(underlay) != screenSurface.Key || !world.has_value() || *world == *screenIndex)
        {
            continue;
        }

        const auto found = std::ranges::find(wanted, *world);
        shown.push_back(Shown{underlay.Rect, static_cast<size_t>(found - wanted.begin())});

        if (found == wanted.end())
        {
            wanted.push_back(*world);
        }
    }

    std::vector<std::reference_wrapper<const Surface>> wantedSurfaces;

    for (const size_t index : wanted)
    {
        wantedSurfaces.emplace_back(_Surfaces[index]);
    }

    auto downloaded = Download(wantedSurfaces);

    if (!downloaded)
    {
        return std::unexpected(downloaded.error());
    }

    // A sharp screen (drawn at the size it's shown) gives each window pixel the drawn pixel holding its centre, as the
    // draws that read a surface do; the window beyond the drawn part is index 0.
    if (scaledScreen)
    {
        std::vector<uint8_t> window(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
        const std::vector<uint8_t>& drawn = (*downloaded)[0];
        const uint64_t mappedWidth = std::max(screenSurface.MappedWidth, 1u);
        const uint64_t mappedHeight = std::max(screenSurface.MappedHeight, 1u);

        for (uint64_t y = 0; y < std::min<uint64_t>(mappedHeight, static_cast<uint64_t>(height)); ++y)
        {
            const uint64_t drawnY = (2 * y + 1) * screenSurface.Index.Height / (2 * mappedHeight);

            for (uint64_t x = 0; x < std::min<uint64_t>(mappedWidth, static_cast<uint64_t>(width)); ++x)
            {
                const uint64_t drawnX = (2 * x + 1) * screenSurface.Index.Width / (2 * mappedWidth);
                window[y * static_cast<uint64_t>(width) + x] = drawn[drawnY * screenSurface.Index.Width + drawnX];
            }
        }

        (*downloaded)[0] = std::move(window);
    }

    const std::vector<uint8_t>& gpu = (*downloaded)[0];
    std::vector<uint8_t> pixels = gpu;

    for (const Shown& over : shown)
    {
        const Surface& world = _Surfaces[wanted[over.World]];
        const std::span<const uint8_t> worldPixels = (*downloaded)[over.World];

        for (int32_t y = std::max(over.Rect.Y0, 0); y <= std::min(over.Rect.Y1, height - 1); ++y)
        {
            for (int32_t x = std::max(over.Rect.X0, 0); x <= std::min(over.Rect.X1, width - 1); ++x)
            {
                const size_t at = static_cast<size_t>(y) * width + x;
                pixels[at] = ShownIndex(gpu[at], x, y, over.Rect, worldPixels, world.Index.Width, world.Index.Height);
            }
        }
    }

    return pixels;
}

auto MCVulkanRenderer::Compare(std::span<const MCUnderlay> /*underlays*/, std::span<const SDL_Color> colors)
    -> std::expected<Comparison, std::string>
{
    // Every live surface, downloaded at once, with its window (held while it is compared).
    std::vector<std::reference_wrapper<const Surface>> surfaces;
    std::vector<std::shared_ptr<MCWindow>> windows;

    for (const Surface& surface : _Surfaces)
    {
        const std::shared_ptr<MCWindow> window = surface.Window.lock();

        if (surface.Index.Handle != nullptr && surface.Key && window != nullptr &&
            SurfaceKeyOf(*window) == surface.Key && window->Pixels != nullptr &&
            static_cast<uint32_t>(window->XMax + 1) == surface.RecordedWidth &&
            static_cast<uint32_t>(window->YMax + 1) == surface.RecordedHeight)
        {
            surfaces.emplace_back(surface);
            windows.push_back(window);
        }
    }

    Comparison result;

    if (surfaces.empty())
    {
        return result;
    }

    auto downloaded = Download(surfaces);

    if (!downloaded)
    {
        return std::unexpected(downloaded.error());
    }

    for (size_t surfaceIndex = 0; surfaceIndex < surfaces.size(); ++surfaceIndex)
    {
        const MCWindow& window = *windows[surfaceIndex];
        const Surface& compared = surfaces[surfaceIndex];
        const std::vector<uint8_t>& gpu = (*downloaded)[surfaceIndex];
        const int32_t width = window.XMax + 1;
        const int32_t height = window.YMax + 1;
        // A surface drawn scaled is compared drawn pixel by drawn pixel with the window pixel each one shows.
        const auto drawnWidth = static_cast<int32_t>(compared.Index.Width);
        const auto drawnHeight = static_cast<int32_t>(compared.Index.Height);
        // The software renderer maps what blended draws covered through AlphaTable; the GPU blended colours there.
        std::vector<uint8_t> blended(static_cast<size_t>(width) * height);

        for (const MCRect& rect : compared.LastBlended)
        {
            for (int32_t y = std::max(rect.Y0, 0); y <= std::min(rect.Y1, height - 1); ++y)
            {
                const int32_t x0 = std::max(rect.X0, 0);
                const int32_t x1 = std::min(rect.X1, width - 1);

                if (x1 >= x0)
                {
                    std::fill_n(&blended[static_cast<size_t>(y) * width + x0], x1 - x0 + 1, uint8_t{1});
                }
            }
        }

        const int64_t differentBefore = result.Different;

        for (int32_t y = 0; y < drawnHeight; ++y)
        {
            const auto wy = static_cast<int32_t>((2 * static_cast<int64_t>(y) + 1) * height / (2 * drawnHeight));

            for (int32_t x = 0; x < drawnWidth; ++x)
            {
                const auto wx = static_cast<int32_t>((2 * static_cast<int64_t>(x) + 1) * width / (2 * drawnWidth));
                const uint8_t cpu = window.Pixels[static_cast<size_t>(wy) * width + wx];
                const uint8_t drawn = gpu[static_cast<size_t>(y) * drawnWidth + x];

                if (cpu != drawn && blended[static_cast<size_t>(wy) * width + wx] == 0)
                {
                    if (result.First.empty())
                    {
                        result.First = std::format("{}x{} surface (drawn {}x{}) at ({}, {}): software {} GPU {}", width,
                                                   height, drawnWidth, drawnHeight, wx, wy, cpu, drawn);
                    }

                    ++result.Different;
                }
            }
        }

        // -gpudump <folder>: the first differing frame of each surface, as both renderers drew it.
        const std::filesystem::path& dump = MCRenderer::MirrorDumpFolder();

        if (!dump.empty() && !colors.empty() && result.Different != differentBefore &&
            _Dumped.insert(compared.Key).second)
        {
            using Image = std::tuple<std::string_view, std::span<const uint8_t>, int32_t, int32_t>;

            for (const auto& [name, pixels, imageWidth, imageHeight] :
                 {Image{"software", window.Bytes(), width, height}, Image{"gpu", gpu, drawnWidth, drawnHeight}})
            {
                SDL_Surface* image = SDL_CreateSurface(imageWidth, imageHeight, SDL_PIXELFORMAT_INDEX8);

                if (image == nullptr)
                {
                    continue;
                }

                // The colours shown, or false colours while they are all black (a fade).
                std::array<SDL_Color, 256> shown{};
                const bool black = std::all_of(colors.begin(), colors.begin() + 256, [](const SDL_Color& color)
                                               { return color.r == 0 && color.g == 0 && color.b == 0; });

                for (size_t i = 0; i < 256; ++i)
                {
                    shown[i] = black ? SDL_Color{static_cast<uint8_t>(i), static_cast<uint8_t>(i * 7),
                                                 static_cast<uint8_t>(i * 13), 255}
                                     : colors[i];
                }

                SDL_SetPaletteColors(SDL_CreateSurfacePalette(image), shown.data(), 0, 256);

                for (int32_t y = 0; y < imageHeight; ++y)
                {
                    std::memcpy(
                        static_cast<uint8_t*>(image->pixels) + static_cast<ptrdiff_t>(y) * image->pitch,
                        pixels.subspan(static_cast<size_t>(y) * imageWidth, static_cast<size_t>(imageWidth)).data(),
                        static_cast<size_t>(imageWidth));
                }

                const std::filesystem::path path =
                    dump / std::format("mirror{}_{}x{}_{}.png", _Mirror.Frames, width, height, name);
                SDL_SavePNG(image, MCFileSystem::ToUtf8(path).c_str());
                SDL_DestroySurface(image);
            }
        }
    }

    if (result.Different != 0)
    {
        if (_Mirror.DifferentFrames < 5)
        {
            SDL_Log("MCVulkanRenderer: frame %lld differs in %lld pixels: %s", static_cast<long long>(_Mirror.Frames),
                    static_cast<long long>(result.Different), result.First.c_str());
        }

        if (_Mirror.FirstFrame < 0)
        {
            _Mirror.FirstFrame = _Mirror.Frames;
            _Mirror.First = result.First;
        }

        ++_Mirror.DifferentFrames;
    }

    ++_Mirror.Frames;
    _Mirror.Last = result;
    return result;
}
