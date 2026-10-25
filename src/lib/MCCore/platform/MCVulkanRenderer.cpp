#include "stdafx.h"
#include "platform/MCVulkanRenderer.h"
#include "platform/MCFrameLog.h"
#include "platform/MCSoftwareRenderer.h"
#include "platform/MCVulkanShaders.h"
#include "lib/MCFatal.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxClip.h"

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

    // shaders/draw.pshader's kinds and flags.
    constexpr uint32_t KindFill = 0;
    constexpr uint32_t KindHash = 1;
    constexpr uint32_t KindTexture = 2;
    constexpr uint32_t KindGouraud = 3;
    constexpr uint32_t KindTexelWalk = 4;
    constexpr uint32_t KindHd = 5;
    /// <summary>The kind's bits of a draw's op.</summary>
    constexpr uint32_t KindMask = 15;
    constexpr uint32_t OpaqueSource = 1u << 4;
    constexpr uint32_t FillSkipped = 1u << 5;
    constexpr uint32_t SkipRunKey = 1u << 6;
    constexpr uint32_t TableBefore = 1u << 7;
    constexpr uint32_t SkipKeyAfterTable = 1u << 8;
    constexpr uint32_t ColorKey = 1u << 9;
    constexpr uint32_t UseColor = 1u << 10;
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

    /// <summary>An atlas page's width and height.</summary>
    constexpr int32_t PageSize = 2048;
    /// <summary>More pages than this and the atlas starts again at the next frame.</summary>
    constexpr size_t MaxPages = 6;
    /// <summary>A picture not used for this many frames is dropped.</summary>
    constexpr uint64_t PictureFrames = 300;
    /// <summary>The bytes the pictures may take before the oldest go.</summary>
    constexpr size_t PictureBudget = 128 * 1024 * 1024;
    /// <summary>The same for the HD art frames on the GPU.</summary>
    constexpr size_t HdPictureBudget = 512 * 1024 * 1024;
    /// <summary>The bytes a pixel of an HD art frame's RGBA8 colours, and of its BC7 colours (a 16-byte block per 4x4).</summary>
    constexpr uint32_t HdRgbaBytes = 4;
    constexpr uint32_t HdBc7Bytes = 1;
    /// <summary>
    /// draw.pshader's textures: the source, the tables, the alpha colours, the fits, the palette and the HD colours (an
    /// HD art frame's, or the terrain's; the palette where a draw has none).
    /// </summary>
    constexpr uint32_t DrawSamplers = 6;
    /// <summary>
    /// The palette texture's rows: the colours shown, then the ones the HD art pack was made from (the shown ones when
    /// there is none), which draw.pshader corrects HD colours by.
    /// </summary>
    constexpr uint32_t PaletteRows = 2;
    /// <summary>The HD terrain atlas's widest and tallest (what the GPUs the port runs on sample).</summary>
    constexpr uint32_t MaxHdAtlasSide = 16384;
    /// <summary>The most colour tables held at once (the table texture's height).</summary>
    constexpr uint32_t MaxTableRows = 8192;
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
    };

    /// <summary>shaders/draw.pshader's alpha colour kinds.</summary>
    constexpr float AlphaNothing = 0.0f;
    constexpr float AlphaOpaque = 1.0f;
    constexpr float AlphaBlended = 2.0f;
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

    /// <summary>The bytes a fast-shape row's packets take, covering <paramref name="width"/> pixels.</summary>
    size_t FastRowEnd(std::span<const uint8_t> bytes, int32_t width)
    {
        size_t data = 0;

        for (int32_t count = 0; count < width;)
        {
            const uint32_t c = bytes[data++];

            if (c >= 0x80)
            {
                count += static_cast<int32_t>(c) - 0x80;
                data += c - 0x80;
            }
            else
            {
                count += static_cast<int32_t>(c);
                ++data;
            }
        }

        return data;
    }

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

    /// <summary>The width a tile's spans reach (0 for a tile without pixels).</summary>
    int32_t TileWidth(std::span<const uint8_t> tile)
    {
        int32_t width = 0;

        for (int32_t row = 0; row < tile[2]; ++row)
        {
            const uint32_t offset = TileOffset(tile, row);
            const int32_t length = static_cast<int32_t>(TileOffset(tile, row + 1) - offset);

            if (length > 1)
            {
                width = std::max(width, tile[offset] + length - 1);
            }
        }

        return width;
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

    // The terrain layer: its own vertex shader, the draws' fragment shader.
    auto terrain = MCVulkanShaders::Load(device, MCVulkanShaders::TerrainVertex);

    if (!terrain)
    {
        return std::unexpected(terrain.error());
    }

    renderer->_TerrainShader = std::move(*terrain);

    // A surface's two targets: its colours and its indices. Opaque draws write both; blended draws blend their
    // premultiplied colour over the colours (ONE, ONE_MINUS_SRC_ALPHA) and leave the indices.
    const auto makePipeline = [&](SDL_GPUShader& vertexShader, bool blend, bool indexOnly = false)
    {
        SDL_GPUColorTargetDescription targets[2]{};
        targets[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        targets[1].format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;

        if (indexOnly)
        {
            targets[0].blend_state.enable_color_write_mask = true;
            targets[0].blend_state.color_write_mask = 0;
        }

        if (blend)
        {
            SDL_GPUColorTargetBlendState& colors = targets[0].blend_state;
            colors.enable_blend = true;
            colors.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            colors.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colors.color_blend_op = SDL_GPU_BLENDOP_ADD;
            colors.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            colors.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colors.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            targets[1].blend_state.enable_color_write_mask = true;
            targets[1].blend_state.color_write_mask = 0;
        }

        SDL_GPUGraphicsPipelineCreateInfo pipeline{};
        pipeline.vertex_shader = &vertexShader;
        pipeline.fragment_shader = renderer->_FragmentShader.get();
        pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline.target_info.num_color_targets = 2;
        pipeline.target_info.color_target_descriptions = targets;
        return MCGpuHandle<SDL_GPUGraphicsPipeline>(SDL_CreateGPUGraphicsPipeline(device.get(), &pipeline),
                                                    MCGpuRelease{device});
    };

    renderer->_Opaque = makePipeline(*renderer->_VertexShader, false);
    renderer->_Blend = makePipeline(*renderer->_VertexShader, true);
    renderer->_IndexOnly = makePipeline(*renderer->_VertexShader, false, true);
    renderer->_TerrainPipeline = makePipeline(*renderer->_TerrainShader, false);

    if (renderer->_Opaque == nullptr || renderer->_Blend == nullptr || renderer->_IndexOnly == nullptr ||
        renderer->_TerrainPipeline == nullptr)
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
    _Opaque.reset();
    _Blend.reset();
    _IndexOnly.reset();
    _TerrainPipeline.reset();
    _VertexShader.reset();
    _FragmentShader.reset();
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

void MCVulkanRenderer::TrimPictures(std::unordered_map<uint64_t, Picture>& pictures, size_t& bytes, size_t budget,
                                    size_t bytesPerPixel)
{
    const auto drop = [&](auto it)
    {
        bytes -= it->second.Bytes != 0
                     ? it->second.Bytes
                     : static_cast<size_t>(it->second.Texture.Width) * it->second.Texture.Height * bytesPerPixel;
        Release(it->second.Texture);
        Release(it->second.HdColors);
        return pictures.erase(it);
    };

    for (auto it = pictures.begin(); it != pictures.end();)
    {
        it = it->second.LastFrame + PictureFrames < _Frame ? drop(it) : std::next(it);
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

bool MCVulkanRenderer::Add(uint16_t surfaceIndex, SourceKind source, uint32_t sourceIndex, const Quad& rect,
                           const std::optional<Quad>& sourceAt, uint32_t op, uint32_t color, uint32_t before,
                           uint32_t after, bool join)
{
    Surface& surface = _Surfaces[surfaceIndex];
    std::array<int32_t, 20> draw{};
    draw[0] = std::max(rect[0], 0);
    draw[1] = std::max(rect[1], 0);
    draw[2] = std::min(rect[2], static_cast<int32_t>(surface.RecordedWidth) - 1);
    draw[3] = std::min(rect[3], static_cast<int32_t>(surface.RecordedHeight) - 1);

    if (draw[2] < draw[0] || draw[3] < draw[1])
    {
        return false;
    }

    if (sourceAt.has_value())
    {
        // The source pixel of the clipped corner.
        const Quad& from = *sourceAt;
        draw[4] = from[0] + (draw[0] - rect[0]) * from[2];
        draw[5] = from[1] + (draw[1] - rect[1]) * from[3];
        draw[6] = from[2];
        draw[7] = from[3];
    }

    draw[8] = static_cast<int32_t>(op);
    draw[9] = static_cast<int32_t>(color);
    draw[10] = static_cast<int32_t>(before);
    draw[11] = static_cast<int32_t>(after);

    const MCRect written{draw[0], draw[1], draw[2], draw[3]};
    const bool blend = (op & (AlphaBlend | TableAfter | SmoothEdges)) != 0;
    const bool indexOnly = (op & IndexOnly) != 0;
    // A run of draws with the same target, bindings and pipeline: a copy within the surface only when the draws are
    // one command's and apart (else each must see the ones before).
    const auto continues = [&](const Record& last)
    {
        return !last.Resize && last.TerrainDraw < 0 && last.SurfaceIndex == surfaceIndex && last.Source == source &&
               last.SourceIndex == sourceIndex && last.Blend == blend && last.IndexOnly == indexOnly &&
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
        fresh.First = static_cast<uint32_t>(_Draws.size());
        _Records.push_back(fresh);
    }

    Record& record = _Records.back();

    // Mirror mode leaves the blended pixels out of its comparison (an HD art frame's leave the indices under them
    // the original's).
    if (blend && (op & KindMask) != KindHd)
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
        else if (SpecialColor[color] == 0)
        {
            kind[0] = AlphaOpaque;
        }
        else if (entry.Alpha == 0.0f && entry.BackgroundWeight == 0.0f)
        {
            // A colour dodge (background * 255 / (255 - RGB)) is drawn as added light of that colour.
            kind[0] = AlphaBlended;
            blend[0] = entry.R / 255.0f;
            blend[1] = entry.G / 255.0f;
            blend[2] = entry.B / 255.0f;
            blend[3] = 0.0f;
        }
        else
        {
            // background * B2 + RGB * A.
            kind[0] = AlphaBlended;
            blend[0] = std::clamp(entry.R * entry.Alpha / 255.0f, 0.0f, 1.0f);
            blend[1] = std::clamp(entry.G * entry.Alpha / 255.0f, 0.0f, 1.0f);
            blend[2] = std::clamp(entry.B * entry.Alpha / 255.0f, 0.0f, 1.0f);
            blend[3] = std::clamp(1.0f - entry.BackgroundWeight, 0.0f, 1.0f);
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
    image.Place = AtlasFor(extent.Width, extent.Height, [&](std::span<uint8_t> out) { decode(out, extent); });
    return found;
}

void MCVulkanRenderer::OnDataChanged(MCAddressKey first, size_t size)
{
    _DataImages.erase(_DataImages.lower_bound({first, 0}), _DataImages.lower_bound({first + size, 0}));

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

void MCVulkanRenderer::Clear(MCWindow& target, const MCRect& rect, uint8_t color)
{
    const uint16_t surface = SurfaceFor(target);
    const Quad at = {rect.X0, rect.Y0, rect.X1, rect.Y1};
    Add(surface, SourceKind::None, 0, at, std::nullopt, KindFill, color, 0, 0);
}

void MCVulkanRenderer::Hash(MCWindow& target, const MCRect& rect, uint8_t color)
{
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

    Add(surface, source->Kind, source->Index, at, from, op, color, 0, 0);
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
                                   bool smoothEdges)
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
    const uint32_t op = KindTexture | OpaqueSource | AlphaBlend | (smoothEdges ? SmoothEdges : 0u);

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
    const std::span<const uint8_t> data =
        command.ShapeTable.subspan(MCShapeOffset(command.ShapeTable, command.ShapeNum) + 0x18);
    const int32_t width = command.Width;
    const int32_t height = command.Height;

    // Port-only: the HD art pack's frame instead, where it has one (the transform writes no indices either).
    if (const std::shared_ptr<const MCHdArt::Frame> hd =
            MCHdArt::Find(command.ShapeTable.data(), command.ShapeNum, width, height);
        hd != nullptr)
    {
        const std::optional<std::pair<Quad, Quad>> place =
            BlitPlace(command.Blit, width, height, "ShapeBlit reading outside the shape's picture");

        if (!place)
        {
            return;
        }

        if (AddHd(SurfaceFor(target), *hd, place->first, place->second, command.Table))
        {
            return;
        }
    }

    BlitPicture(
        target, command.Blit, width, height, "ShapeBlit reading outside the shape's picture",
        [&]() -> std::optional<uint32_t>
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
        },
        MCHdArt::SmoothEdges(command.ShapeTable.data()));
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

bool MCVulkanRenderer::AddHd(uint16_t surface, const MCHdArt::Frame& frame, const Quad& at, const Quad& from,
                             const std::optional<MCColorTable>& table)
{
    const std::optional<uint32_t> picture = HdPictureOf(frame);

    if (!picture)
    {
        return false;
    }

    // The colour field carries the frame's scale (shaders/draw.pshader's KindHd).
    uint32_t op = KindHd | AlphaBlend;
    uint32_t before = 0;

    if (table.has_value())
    {
        op |= TableBefore;
        before = TableRow(*table);
    }

    if (Add(surface, SourceKind::Picture, *picture, at, from, op, static_cast<uint32_t>(frame.Scale), before, 0))
    {
        ++_HdDrawn;
    }

    return true;
}

void MCVulkanRenderer::Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels)
{
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
    const uint16_t surface = SurfaceFor(target);
    const Quad at = {x, y, x, y};
    Add(surface, SourceKind::None, 0, at, std::nullopt, KindFill, color, 0, 0);
}

void MCVulkanRenderer::Shape(MCWindow& target, const MCShapeCommand& command)
{
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

    // Port-only: an opaque shape the HD art pack has a frame of keeps the original's indices and is shown from the
    // pack (its bounds: the header's).
    if (command.Op == MCShapeOp::Draw || command.Op == MCShapeOp::Xlat)
    {
        const int32_t boundsWidth = MCRead32(header, 0x10) - MCRead32(header, 0x08) + 1;

        if (const std::shared_ptr<const MCHdArt::Frame> hd =
                MCHdArt::Find(command.ShapeTable.data(), command.ShapeNum, boundsWidth, headerRows);
            hd != nullptr)
        {
            // The frame covers the bounds, which may be wider than the shape's widest row.
            const Quad hdAt = {at[0], at[1], std::min(command.Left + boundsWidth - 1, command.Hi), at[3]};
            const Quad shapeFrom = {at[0] - command.Left, command.SkipRows, 1, 1};

            if (AddHd(surface, *hd, hdAt, shapeFrom, command.Table))
            {
                op |= IndexOnly;
            }
        }
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

    // Port-only: a shape with smooth edges (with HD art on: the mechs' shadows) writes its indices as always, then its
    // colour through the blend pipeline with its coverage taken between its own pixels (its place in the atlas).
    if (command.Op == MCShapeOp::Draw && (op & IndexOnly) == 0 && MCHdArt::SmoothEdges(command.ShapeTable.data()))
    {
        Add(surface, SourceKind::Atlas, place->Page, at, from, op | IndexOnly, 0, before, after);

        if (Add(surface, SourceKind::Atlas, place->Page, at, from, op | SmoothEdges, 0, before, after))
        {
            std::array<int32_t, 20>& draw = _Draws.back();
            draw[16] = place->X;
            draw[17] = place->Y;
            draw[18] = place->X + width - 1;
            draw[19] = place->Y + rows - 1;
        }

        return;
    }

    Add(surface, SourceKind::Atlas, place->Page, at, from, op, 0, before, after);
}

void MCVulkanRenderer::FastShape(MCWindow& target, const MCFastShapeCommand& command)
{
    const uint16_t surface = SurfaceFor(target);
    const std::span<const uint8_t> shape = command.Shape;
    const int32_t height = MCRead16(shape, 8);
    const int32_t width = MCRead16(shape, 10) + 1;

    // The shape's bytes: its header and row offsets, then the rows' packets, one row after another, so they end where
    // the row that starts last ends.
    const auto rowStart = [&](int32_t row) -> size_t { return MCRead16(shape, 0xc + static_cast<size_t>(row) * 2); };
    const MCOptionalRef<DataImage> found = AtlasForData(
        shape, Seed(ImageKind::FastShape, width, height), "a fast shape",
        [&]
        {
            size_t last = 0xc + static_cast<size_t>(height) * 2;
            size_t end = last;

            for (int32_t row = 0; row < height; ++row)
            {
                last = std::max(last, rowStart(row));
            }

            if (height > 0)
            {
                end = std::max(end, last + FastRowEnd(shape.subspan(last), width));
            }

            return Extent{end, width, height};
        },
        [&](std::span<uint8_t> out, const Extent& extent)
        {
            std::ranges::fill(out.first(static_cast<size_t>(width) * height * 2), uint8_t{0});

            for (int32_t row = 0; row < height; ++row)
            {
                // A row reaching past the last row's end would be outside the bytes the shape is known by.
                if (rowStart(row) + FastRowEnd(shape.subspan(rowStart(row)), width) > extent.Size)
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

    Add(surface, SourceKind::Atlas, place->Page, at, from, op, 0, before, 0);
}

void MCVulkanRenderer::Tile(MCWindow& target, const MCTileCommand& command)
{
    const uint16_t surface = SurfaceFor(target);

    // A terrain layer drew this pass's tiles from the mesh.
    if (static_cast<int32_t>(surface) == _TerrainLayerSurface)
    {
        return;
    }

    const std::span<const uint8_t> tile = command.Tile;
    const MCOptionalRef<DataImage> found = AtlasForData(
        tile, Seed(ImageKind::Tile, 0, 0), "a terrain tile",
        [&] { return Extent{TileOffset(tile, tile[2]), TileWidth(tile), tile[2]}; },
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
            break;
        }
        default:
            break;
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

void MCVulkanRenderer::MapQuad(MCWindow& target, const MCMapQuadCommand& command)
{
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
    const uint16_t surface = SurfaceFor(target);
    uint32_t op = KindFill;
    uint32_t after = 0;

    if (command.Table.has_value())
    {
        op |= TableAfter;
        after = TableRow(*command.Table);
    }

    // The pixels in runs along a row (a line never comes back to a pixel).
    bool join = false;
    bool open = false;
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
}

void MCVulkanRenderer::Ellipse(MCWindow& target, const MCEllipseCommand& command)
{
    const uint16_t surface = SurfaceFor(target);
    uint32_t op = KindFill;

    if (command.Alpha)
    {
        op |= AlphaBlend;
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
}

void MCVulkanRenderer::StatusBar(MCWindow& target, const MCStatusBarCommand& command)
{
    // The AlphaTable row status-bar frames are darkened through (0x008011d0 in MCX.EXE).
    constexpr int32_t StatusFrameAlpha = 0x108;
    const uint16_t surface = SurfaceFor(target);
    // The frame and the bar are the alpha colours whose AlphaTable rows the software renderer maps them through.
    const auto frame = static_cast<uint32_t>(StatusFrameAlpha);
    const auto fill = static_cast<uint32_t>(command.AlphaColor);
    const MCRect& box = command.Box;
    const int32_t width = box.X1 - box.X0;
    const uint32_t op = KindFill | AlphaBlend | GivenAlpha;

    // The frame's top and bottom rows (the pixels between the corners), then the body rows: their left and right
    // pixels through the frame's table, then the bar through the fill's. A pixel the bar shares with the right
    // edge is mapped by both, in that order (each part reads the pixels the one before left).
    bool join = false;
    const auto add = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t table, bool joins)
    {
        const Quad at = {x0, y0, x1, y1};

        if (Add(surface, SourceKind::None, 0, at, std::nullopt, op, 0, 0, table, joins && join))
        {
            join = true;
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
        for (const auto& [y0, y1] : body)
        {
            add(box.X0 + 1, y0, box.X0 + command.BarLength + 1, y1, fill, false);
        }
    }
}

void MCVulkanRenderer::Glyph(MCWindow& target, const MCGlyphCommand& command)
{
    const uint16_t surface = SurfaceFor(target);
    const std::span<const uint8_t> font = command.Font;
    const auto glyphAt = static_cast<uint32_t>(MCRead32(font, 0x10 + static_cast<size_t>(command.Character) * 4));
    const int32_t pitch = MCRead32(font, glyphAt);
    const std::span<const uint8_t> source = font.subspan(
        glyphAt + 4 + static_cast<size_t>(static_cast<intptr_t>(command.SourceY) * pitch + command.SourceX));
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

    if (_TerrainMesh.Version != mesh.Version || _TerrainMesh.HdPack != HdPackInUse())
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

    // The mesh's rows the grid covers.
    const int32_t firstRow = std::max(frame.FirstRow, _TerrainMesh.FirstRow);
    const int32_t lastRow = std::min(frame.LastRow, _TerrainMesh.FirstRow + _TerrainMesh.Rows - 1);

    if (lastRow >= firstRow && !Empty(draw.Frame.Clip))
    {
        draw.FirstCell = static_cast<uint32_t>((firstRow - _TerrainMesh.FirstRow) * _TerrainMesh.Cols);
        draw.CellCount = static_cast<uint32_t>((lastRow - firstRow + 1) * _TerrainMesh.Cols);
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
    _TerrainMesh = TerrainMeshState{};
}

std::expected<void, std::string> MCVulkanRenderer::UploadTerrainMesh(const MCTerrainMesh& mesh)
{
    BeginRecording();
    ReleaseTerrainMesh();
    _TerrainMesh.Version = mesh.Version;
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

            if (const int32_t width = TileWidth(tile); width != 0)
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

    const int32_t align = hdScale != 0 ? MCBc7::BlockSide : 1;
    const auto aligned = [align](int32_t value) { return (value + align - 1) / align * align; };

    for (size_t i = 0; i < mesh.Tiles.size(); ++i)
    {
        const std::span<const uint8_t> tile = mesh.Tiles[i];
        Place& place = places[i];
        place.Width = TileWidth(tile);
        place.Height = tile[2];

        if (place.Width == 0)
        {
            continue;
        }

        if (shelfX + aligned(place.Width) > TerrainAtlasWidth)
        {
            shelfY += shelfHeight;
            shelfX = 0;
            shelfHeight = 0;
        }

        place.X = shelfX;
        place.Y = shelfY;
        shelfX += aligned(place.Width);
        shelfHeight = std::max(shelfHeight, aligned(place.Height));
    }

    const auto atlasHeight =
        static_cast<uint32_t>(std::bit_ceil(static_cast<uint32_t>(std::max(shelfY + shelfHeight, 1))));
    SDL_GPUBufferCreateInfo cellsInfo{};
    cellsInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    cellsInfo.size = static_cast<uint32_t>(mesh.CellTiles.size() * 8);
    SDL_GPUBufferCreateInfo imagesInfo{};
    imagesInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    imagesInfo.size = static_cast<uint32_t>(std::max<size_t>(places.size(), 1) * 32);

    if (mesh.CellTiles.size() != static_cast<size_t>(mesh.Cols) * mesh.Rows ||
        mesh.CellElevations.size() != mesh.CellTiles.size())
    {
        return std::unexpected(std::format("the terrain mesh's {}x{} cells come with {} tiles and {} elevations",
                                           mesh.Cols, mesh.Rows, mesh.CellTiles.size(), mesh.CellElevations.size()));
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

    if (hdScale != 0)
    {
        if (auto made = MakeHdTerrainAtlas(static_cast<uint32_t>(TerrainAtlasWidth * hdScale),
                                           static_cast<uint32_t>((shelfY + shelfHeight) * hdScale));
            !made)
        {
            SDL_Log("MCVulkanRenderer: the HD terrain is off for this map: %s", made.error().c_str());
            hdScale = 0;
            std::ranges::fill(hdTiles, nullptr);
        }
    }

    _TerrainMesh.HdScale = hdScale;

    if (hdScale != 0)
    {
        SDL_Log("MCVulkanRenderer: HD terrain: %zu of %zu tiles, a %ux%u BC7 atlas (%.0f MB)",
                static_cast<size_t>(std::ranges::count_if(hdTiles, [](const auto& tile) { return tile != nullptr; })),
                mesh.Tiles.size(), _TerrainMesh.HdAtlas.Width, _TerrainMesh.HdAtlas.Height,
                static_cast<double>(_TerrainMesh.HdAtlas.Width) * _TerrainMesh.HdAtlas.Height / (1024.0 * 1024.0));
    }

    for (size_t i = 0; i < places.size(); ++i)
    {
        const Place& place = places[i];

        if (place.Width != 0 && hdTiles[i] != nullptr)
        {
            UploadHdTile(*hdTiles[i], static_cast<uint32_t>(place.X * hdScale),
                         static_cast<uint32_t>(place.Y * hdScale));
        }

        if (place.Width != 0)
        {
            DecodeTile(mesh.Tiles[i], place.Width,
                       QueueUpload(_TerrainMesh.Atlas.Handle, static_cast<uint32_t>(place.X),
                                   static_cast<uint32_t>(place.Y), static_cast<uint32_t>(place.Width),
                                   static_cast<uint32_t>(place.Height), 2));
            ++_FrameUploads.AtlasImages;
            _FrameUploads.AtlasBytes += static_cast<int64_t>(place.Width) * place.Height * 2;
        }
    }

    const std::span<int32_t> images = UploadAs<int32_t>(QueueBufferUpload(_TerrainMesh.Images, imagesInfo.size));
    std::ranges::fill(images, 0);

    for (size_t i = 0; i < places.size(); ++i)
    {
        const std::vector<uint8_t>& tile = mesh.Tiles[i];
        const std::span<int32_t> image = images.subspan(i * 8, 8);
        image[0] = places[i].X;
        image[1] = places[i].Y;
        image[2] = places[i].Width;
        image[3] = places[i].Height;
        image[4] = tile[0];
        image[5] = tile[1];
        image[6] = hdTiles[i] != nullptr ? 1 : 0;
    }

    const std::span<uint32_t> cells = UploadAs<uint32_t>(QueueBufferUpload(_TerrainMesh.Cells, cellsInfo.size));

    for (size_t i = 0; i < mesh.CellTiles.size(); ++i)
    {
        cells[i * 2] = mesh.CellTiles[i];
        cells[i * 2 + 1] = mesh.CellElevations[i];
    }

    _TerrainMesh.Usable = true;
    return {};
}

uint64_t MCVulkanRenderer::HdPackInUse()
{
    return MCHdArt::Active() ? MCHdArt::PackVersion() : 0;
}

std::expected<void, std::string> MCVulkanRenderer::MakeHdTerrainAtlas(uint32_t width, uint32_t height)
{
    if (width > MaxHdAtlasSide || height > MaxHdAtlasSide)
    {
        return std::unexpected(std::format("its tiles need a {}x{} HD atlas", width, height));
    }

    if (!_Bc7.has_value())
    {
        _Bc7 = SDL_GPUTextureSupportsFormat(_Device.get(), SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                            SDL_GPU_TEXTUREUSAGE_SAMPLER);
    }

    // An RGBA8 atlas would take four times the memory: without BC7, the ground stays the original's.
    if (!*_Bc7)
    {
        return std::unexpected(std::string("the GPU doesn't sample BC7"));
    }

    return EnsureTexture(_TerrainMesh.HdAtlas, SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                         width, height);
}

void MCVulkanRenderer::UploadHdTile(const MCHdArt::Frame& frame, uint32_t x, uint32_t y)
{
    const auto width = static_cast<uint32_t>(MCBc7::Padded(frame.Width));
    const auto height = static_cast<uint32_t>(MCBc7::Padded(frame.Height));
    std::vector<uint8_t> encoded;
    std::span<const uint8_t> blocks = frame.Colors;

    if (frame.Format != MCHdArt::ColorFormat::Bc7)
    {
        encoded = MCBc7::Encode(frame.Colors, frame.Width, frame.Height);
        blocks = encoded;
    }

    if (blocks.size() != static_cast<size_t>(width) * height * HdBc7Bytes)
    {
        NotSupported("an HD terrain tile whose blocks don't match its size");
        return;
    }

    std::ranges::copy(blocks, QueueUpload(_TerrainMesh.HdAtlas.Handle, x, y, width, height, HdBc7Bytes).begin());
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
    uniforms.TargetSize[0] = static_cast<float>(surface.RecordedWidth);
    uniforms.TargetSize[1] = static_cast<float>(surface.RecordedHeight);
    uniforms.TargetSize[2] = static_cast<float>(surface.Target.Width);
    uniforms.TargetSize[3] = static_cast<float>(surface.Target.Height);
    uniforms.Origin[0] = frame.OriginX;
    uniforms.Origin[1] = frame.OriginY;
    uniforms.Origin[2] = frame.ElevStep;
    uniforms.Origin[3] = static_cast<int32_t>(draw.FirstCell);
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

    SDL_BindGPUGraphicsPipeline(&pass, _TerrainPipeline.get());
    const SDL_GPUTextureSamplerBinding fogBinding{fog.Index.Handle.get(), _Nearest.get()};
    SDL_BindGPUVertexSamplers(&pass, 0, &fogBinding, 1);
    SDL_GPUBuffer* const buffers[2] = {_TerrainMesh.Cells.get(), _TerrainMesh.Images.get()};
    SDL_BindGPUVertexStorageBuffers(&pass, 0, buffers, 2);
    SDL_GPUTexture* const hdAtlas =
        _TerrainMesh.HdAtlas.Handle != nullptr ? _TerrainMesh.HdAtlas.Handle.get() : _PaletteTexture.Handle.get();
    const SDL_GPUTextureSamplerBinding bindings[DrawSamplers] = {
        {_TerrainMesh.Atlas.Handle.get(), _Nearest.get()}, {_TableTexture.Handle.get(), _Nearest.get()},
        {_AlphaTexture.Handle.get(), _Nearest.get()},      {_FitTexture.Handle.get(), _Nearest.get()},
        {_PaletteTexture.Handle.get(), _Nearest.get()},    {hdAtlas, _Nearest.get()},
    };

    SDL_BindGPUFragmentSamplers(&pass, 0, bindings, DrawSamplers);
    SDL_PushGPUVertexUniformData(&commands, 0, &uniforms, sizeof(uniforms));
    SDL_PushGPUFragmentUniformData(&commands, 0, frameUniforms.data(), static_cast<uint32_t>(frameUniforms.size()));
    SDL_DrawGPUPrimitives(&pass, 6, draw.CellCount, 0, 0);

    // The draws that follow read their own buffer.
    SDL_GPUBuffer* const drawBuffer = _DrawBuffer.get();
    SDL_BindGPUVertexStorageBuffers(&pass, 0, &drawBuffer, 1);
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
    const int64_t windowWidth = std::max<int64_t>(surface.RecordedWidth, 1);
    const int64_t windowHeight = std::max<int64_t>(surface.RecordedHeight, 1);
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

void MCVulkanRenderer::PlaceSurfaces(std::span<const MCUnderlay> underlays)
{
    for (Surface& surface : _Surfaces)
    {
        surface.DrawnWidth = surface.RecordedWidth;
        surface.DrawnHeight = surface.RecordedHeight;

        if (!surface.Key)
        {
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

    for (auto made :
         {EnsureTexture(surface.Target, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, target, surface.DrawnWidth,
                        surface.DrawnHeight),
          EnsureTexture(surface.Index, SDL_GPU_TEXTUREFORMAT_R8_UNORM, target, surface.DrawnWidth, surface.DrawnHeight),
          EnsureTexture(surface.Copy, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, surface.DrawnWidth,
                        surface.DrawnHeight)})
    {
        if (!made)
        {
            return made;
        }
    }

    // Index 0 everywhere, in the colour of palette entry 0.
    SDL_GPUColorTargetInfo clear[2]{};
    clear[0].texture = surface.Target.Handle.get();
    clear[0].clear_color = SDL_FColor{_Colors[0].r / 255.0f, _Colors[0].g / 255.0f, _Colors[0].b / 255.0f, 1.0f};
    clear[1].texture = surface.Index.Handle.get();

    for (SDL_GPUColorTargetInfo& info : clear)
    {
        info.load_op = SDL_GPU_LOADOP_CLEAR;
        info.store_op = SDL_GPU_STOREOP_STORE;
    }

    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(&commands, clear, 2, nullptr));
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
    const auto endPass = [&]
    {
        if (pass != nullptr)
        {
            SDL_EndGPURenderPass(pass);
            pass = nullptr;
            passSurface = -1;
        }
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
            SDL_GPUColorTargetInfo targets[2]{};
            targets[0].texture = surface.Target.Handle.get();
            targets[1].texture = surface.Index.Handle.get();

            for (SDL_GPUColorTargetInfo& target : targets)
            {
                target.load_op = SDL_GPU_LOADOP_LOAD;
                target.store_op = SDL_GPU_STOREOP_STORE;
            }

            pass = SDL_BeginGPURenderPass(&commands, targets, 2, nullptr);
            passSurface = record.SurfaceIndex;
            const SDL_GPUViewport viewport{
                0.0f, 0.0f, static_cast<float>(surface.Target.Width), static_cast<float>(surface.Target.Height),
                0.0f, 1.0f};
            SDL_SetGPUViewport(pass, &viewport);
            SDL_GPUBuffer* const drawBuffer = _DrawBuffer.get();
            SDL_BindGPUVertexStorageBuffers(pass, 0, &drawBuffer, 1);

            // The frame's colours; on a surface shown over an underlay, the key is drawn transparent.
            passFrame = FrameUniforms{};
            passFrame.Cycle[0] = _Cycle.First;
            passFrame.Cycle[1] = _Cycle.Step;
            passFrame.Cycle[2] =
                std::ranges::any_of(underlays, [&](const MCUnderlay& underlay)
                                    { return surface.Key && TargetSurfaceKey(underlay) == surface.Key; })
                    ? 1
                    : 0;

            for (size_t i = 0; i < 8; ++i)
            {
                passFrame.CycleSources[i] = _Cycle.Sources[i];
            }
        }

        if (record.TerrainDraw >= 0)
        {
            FrameUniforms frame = passFrame;
            frame.Scale[0] = static_cast<int32_t>(std::max(surface.RecordedWidth, 1u));
            frame.Scale[1] = static_cast<int32_t>(std::max(surface.RecordedHeight, 1u));
            frame.Scale[2] = static_cast<int32_t>(std::max(surface.Target.Width, 1u));
            frame.Scale[3] = static_cast<int32_t>(std::max(surface.Target.Height, 1u));
            std::ranges::fill(frame.SourceScale, 1);
            DrawTerrain(commands, *pass, surface, _TerrainDraws[static_cast<size_t>(record.TerrainDraw)],
                        std::as_bytes(std::span(&frame, 1)));
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
                next.IndexOnly != record.IndexOnly || next.ReadsCopy || record.ReadsCopy ||
                next.First != previous.First + previous.Count)
            {
                break;
            }

            count += next.Count;
            ++last;
        }

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

        const SDL_GPUTextureSamplerBinding bindings[DrawSamplers] = {
            {source, _Nearest.get()},
            {_TableTexture.Handle.get(), _Nearest.get()},
            {_AlphaTexture.Handle.get(), _Nearest.get()},
            {_FitTexture.Handle.get(), _Nearest.get()},
            {_PaletteTexture.Handle.get(), _Nearest.get()},
            {hdColors, _Nearest.get()},
        };

        SDL_BindGPUGraphicsPipeline(pass, record.IndexOnly ? _IndexOnly.get()
                                          : record.Blend   ? _Blend.get()
                                                           : _Opaque.get());
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, DrawSamplers);
        const BatchUniforms batch{{static_cast<float>(surface.RecordedWidth),
                                   static_cast<float>(surface.RecordedHeight), static_cast<float>(surface.Target.Width),
                                   static_cast<float>(surface.Target.Height)},
                                  {record.First, 0, 0, 0}};
        SDL_PushGPUVertexUniformData(&commands, 0, &batch, sizeof(batch));

        // The target's scale, and the source's when it is a surface.
        const auto scaleOf = [](const Surface& of, std::span<int32_t, 4> out)
        {
            out[0] = static_cast<int32_t>(std::max(of.RecordedWidth, 1u));
            out[1] = static_cast<int32_t>(std::max(of.RecordedHeight, 1u));
            out[2] = static_cast<int32_t>(std::max(of.Target.Width, 1u));
            out[3] = static_cast<int32_t>(std::max(of.Target.Height, 1u));
        };

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

        SDL_PushGPUFragmentUniformData(&commands, 0, &frame, sizeof(frame));
        SDL_DrawGPUPrimitives(pass, 6, count, 0, 0);

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

void MCVulkanRenderer::OnTextureReleased(MCTexture& texture)
{
    if (texture.Hardware == nullptr)
    {
        return;
    }

    auto& state = static_cast<TextureState&>(*texture.Hardware);

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
        total += (surface.Index.Width * surface.Index.Height + 3) & ~3u;
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
        const Surface& surface = surfaces[i];
        const size_t size = static_cast<size_t>(surface.Index.Width) * surface.Index.Height;
        const std::span<const uint8_t> surfacePixels = mapped.subspan(offsets[i], size);
        pixels[i].assign(surfacePixels.begin(), surfacePixels.end());
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
    const auto width = static_cast<int32_t>(screenSurface.Index.Width);
    const auto height = static_cast<int32_t>(screenSurface.Index.Height);

    if (width != screen.XMax + 1 || height != screen.YMax + 1)
    {
        return std::unexpected(std::format("the screen's surface is {}x{}, the screen {}x{}", width, height,
                                           screen.XMax + 1, screen.YMax + 1));
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
                SDL_SavePNG(image, path.string().c_str());
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
