#include "stdafx.h"
#include "platform/MCUiGpu.h"
#include "platform/MCGpuLimits.h"
#include "platform/MCVulkanShaders.h"

namespace
{
    constexpr uint32_t PixelBytes = 4;
    constexpr float ByteMax = 255.0f;

    /// <summary>A vertex as ui.vshader reads it: its place and texture place, then its premultiplied colour.</summary>
    struct MCGpuVertex
    {
        std::array<float, 4> Place;
        std::array<float, 4> Color;
    };

    /// <summary>ui.vshader's uniforms.</summary>
    struct MCUiUniforms
    {
        std::array<float, 4> TargetSize;
        std::array<uint32_t, 4> Batch;
    };

    std::string SdlError(std::string_view what)
    {
        return std::format("the menus' GPU drawing, {}: {}", what, SDL_GetError());
    }

    /// <summary>A draw's run in the vertices' buffer.</summary>
    struct MCRun
    {
        uint32_t First = 0;
        uint32_t Count = 0;
        SDL_GPUTexture* Texture = nullptr;
        SDL_GPUSampler* Sampler = nullptr;
        SDL_Rect Scissor{};
    };

    /// <summary><paramref name="rect"/> kept within a target of <paramref name="width"/> x <paramref name="height"/>.</summary>
    SDL_Rect Within(const SDL_Rect& rect, uint32_t width, uint32_t height)
    {
        const int left = std::clamp(rect.x, 0, static_cast<int>(width));
        const int top = std::clamp(rect.y, 0, static_cast<int>(height));
        const int right = std::clamp(rect.x + rect.w, left, static_cast<int>(width));
        const int bottom = std::clamp(rect.y + rect.h, top, static_cast<int>(height));
        return SDL_Rect{left, top, right - left, bottom - top};
    }
}

MCUiGpu::~MCUiGpu() = default;

std::expected<std::unique_ptr<MCUiGpu>, std::string> MCUiGpu::Create(std::shared_ptr<SDL_GPUDevice> device)
{
    auto ui = std::make_unique<MCUiGpu>(Key{});
    ui->_Device = std::move(device);
    auto vertex = MCVulkanShaders::Load(ui->_Device, MCVulkanShaders::UiVertex);

    if (!vertex)
    {
        return std::unexpected(vertex.error());
    }

    auto fragment = MCVulkanShaders::Load(ui->_Device, MCVulkanShaders::UiFragment);

    if (!fragment)
    {
        return std::unexpected(fragment.error());
    }

    ui->_Vertex = std::move(*vertex);
    ui->_Fragment = std::move(*fragment);
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ui->_Nearest =
        MCGpuHandle<SDL_GPUSampler>(SDL_CreateGPUSampler(ui->_Device.get(), &sampler), MCGpuRelease{ui->_Device});
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    ui->_Linear =
        MCGpuHandle<SDL_GPUSampler>(SDL_CreateGPUSampler(ui->_Device.get(), &sampler), MCGpuRelease{ui->_Device});

    if (ui->_Nearest == nullptr || ui->_Linear == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUSampler"));
    }

    return ui;
}

auto MCUiGpu::PipelineFor(SDL_GPUTextureFormat format)
    -> std::expected<std::reference_wrapper<SDL_GPUGraphicsPipeline>, std::string>
{
    if (const auto found = _Pipelines.find(format); found != _Pipelines.end())
    {
        return std::ref(*found->second);
    }

    SDL_GPUColorTargetDescription target{};
    target.format = format;
    SDL_GPUColorTargetBlendState& blend = target.blend_state;
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};
    pipeline.vertex_shader = _Vertex.get();
    pipeline.fragment_shader = _Fragment.get();
    pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.num_color_targets = 1;
    pipeline.target_info.color_target_descriptions = &target;
    MCGpuHandle<SDL_GPUGraphicsPipeline> made(SDL_CreateGPUGraphicsPipeline(_Device.get(), &pipeline),
                                              MCGpuRelease{_Device});

    if (made == nullptr)
    {
        return std::unexpected(
            SdlError(std::format("SDL_CreateGPUGraphicsPipeline(format {})", static_cast<int>(format))));
    }

    SDL_GPUGraphicsPipeline& kept = *made;
    _Pipelines.emplace(format, std::move(made));
    return std::ref(kept);
}

auto MCUiGpu::Upload(SDL_GPUCopyPass& pass, std::span<const uint8_t> pixels, uint32_t width, uint32_t height)
    -> std::expected<MCGpuHandle<SDL_GPUTexture>, std::string>
{
    const size_t bytes = static_cast<size_t>(width) * height * PixelBytes;

    if (width == 0 || height == 0 || pixels.size() < bytes)
    {
        return std::unexpected(std::format("the menus' {}x{} picture has {} bytes", width, height, pixels.size()));
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    MCGpuHandle<SDL_GPUTexture> texture(SDL_CreateGPUTexture(_Device.get(), &info), MCGpuRelease{_Device});
    SDL_GPUTransferBufferCreateInfo transfer{};
    transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer.size = static_cast<uint32_t>(bytes);
    MCGpuHandle<SDL_GPUTransferBuffer> buffer(SDL_CreateGPUTransferBuffer(_Device.get(), &transfer),
                                              MCGpuRelease{_Device});

    if (texture == nullptr || buffer == nullptr)
    {
        return std::unexpected(SdlError(std::format("a {}x{} picture's texture", width, height)));
    }

    void* mapped = SDL_MapGPUTransferBuffer(_Device.get(), buffer.get(), false);

    if (mapped == nullptr)
    {
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer(a picture)"));
    }

    std::memcpy(mapped, pixels.data(), bytes);
    SDL_UnmapGPUTransferBuffer(_Device.get(), buffer.get());
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = buffer.get();
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_GPUTextureRegion region{};
    region.texture = texture.get();
    region.w = width;
    region.h = height;
    region.d = 1;
    // The transfer buffer is let go of now; SDL keeps it until the upload has run.
    SDL_UploadToGPUTexture(&pass, &source, &region, false);
    return texture;
}

auto MCUiGpu::UploadPicture(SDL_GPUCopyPass& pass, const MCUiTexture& picture)
    -> std::expected<MCGpuHandle<SDL_GPUTexture>, std::string>
{
    const auto width = static_cast<uint32_t>(std::max(picture.Width, 0));
    const auto height = static_cast<uint32_t>(std::max(picture.Height, 0));

    // Every GPU holds a picture this size; a larger one is measured against this one's largest texture.
    if (std::max(picture.Width, picture.Height) <= MCGpuLimits::VulkanLeast)
    {
        return Upload(pass, picture.Pixels, width, height);
    }

    if (!_LargestSide.has_value())
    {
        _LargestSide = MCGpuLimits::LargestTextureSide(*_Device);
    }

    const SDL_Point fitted = MCUiFittedSize(picture.Width, picture.Height, *_LargestSide);

    if (fitted.x == picture.Width && fitted.y == picture.Height)
    {
        return Upload(pass, picture.Pixels, width, height);
    }

    const std::expected<MCSdlHandle<SDL_Surface>, std::string> scaled =
        MCUiScaled(picture, SDL_Rect{0, 0, picture.Width, picture.Height}, fitted.x, fitted.y);

    if (!scaled)
    {
        return std::unexpected(std::format("the menus' GPU drawing, {}", scaled.error()));
    }

    const SDL_Surface& copy = **scaled;

    if (copy.pitch != fitted.x * static_cast<int>(PixelBytes))
    {
        return std::unexpected(std::format("the menus' GPU drawing, picture {}'s {}x{} copy has rows of {} bytes",
                                           picture.Id, fitted.x, fitted.y, copy.pitch));
    }

    SDL_Log("Menus: picture %llu (%dx%d) is over the GPU's %d a side: drawn from a %dx%d copy",
            static_cast<unsigned long long>(picture.Id), picture.Width, picture.Height, *_LargestSide, fitted.x,
            fitted.y);
    return Upload(pass, std::span(static_cast<const uint8_t*>(copy.pixels), static_cast<size_t>(copy.pitch) * copy.h),
                  static_cast<uint32_t>(fitted.x), static_cast<uint32_t>(fitted.y));
}

std::expected<void, std::string> MCUiGpu::FillVertices(SDL_GPUCopyPass& pass, std::span<const std::byte> bytes)
{
    const auto size = static_cast<uint32_t>(bytes.size());

    if (size > _VertexCapacity)
    {
        // The first buffer holds a screen's worth (a few hundred boxes and letters); it doubles from there.
        constexpr uint32_t FirstVertices = 4096;
        uint32_t capacity = std::max<uint32_t>(_VertexCapacity, sizeof(MCGpuVertex) * FirstVertices);

        while (capacity < size)
        {
            capacity *= 2;
        }

        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        info.size = capacity;
        _Vertices = MCGpuHandle<SDL_GPUBuffer>(SDL_CreateGPUBuffer(_Device.get(), &info), MCGpuRelease{_Device});
        SDL_GPUTransferBufferCreateInfo transfer{};
        transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer.size = capacity;
        _VertexTransfer = MCGpuHandle<SDL_GPUTransferBuffer>(SDL_CreateGPUTransferBuffer(_Device.get(), &transfer),
                                                             MCGpuRelease{_Device});

        if (_Vertices == nullptr || _VertexTransfer == nullptr)
        {
            _VertexCapacity = 0;
            return std::unexpected(SdlError(std::format("a {}-byte vertex buffer", capacity)));
        }

        _VertexCapacity = capacity;
    }

    // Cycled: a frame still drawing from the last fill keeps its own.
    void* mapped = SDL_MapGPUTransferBuffer(_Device.get(), _VertexTransfer.get(), true);

    if (mapped == nullptr)
    {
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer(the vertices)"));
    }

    std::memcpy(mapped, bytes.data(), bytes.size());
    SDL_UnmapGPUTransferBuffer(_Device.get(), _VertexTransfer.get());
    const SDL_GPUTransferBufferLocation source{.transfer_buffer = _VertexTransfer.get(), .offset = 0};
    const SDL_GPUBufferRegion region{.buffer = _Vertices.get(), .offset = 0, .size = size};
    SDL_UploadToGPUBuffer(&pass, &source, &region, true);
    return {};
}

std::expected<void, std::string> MCUiGpu::Draw(SDL_GPUCommandBuffer& commands, SDL_GPUTexture& target,
                                               SDL_GPUTextureFormat format, uint32_t width, uint32_t height,
                                               const MCUiDraws& draws, bool clear, float shown)
{
    auto pipeline = PipelineFor(format);

    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }

    std::vector<MCGpuVertex> vertices;
    std::vector<MCRun> runs;
    SDL_GPUCopyPass* const pass = SDL_BeginGPUCopyPass(&commands);

    if (pass == nullptr)
    {
        return std::unexpected(SdlError("SDL_BeginGPUCopyPass"));
    }

    const auto upload = [&]() -> std::expected<void, std::string>
    {
        if (_White == nullptr)
        {
            constexpr std::array<uint8_t, PixelBytes> White = {255, 255, 255, 255};
            auto white = Upload(*pass, White, 1, 1);

            if (!white)
            {
                return std::unexpected(white.error());
            }

            _White = std::move(*white);
        }

        for (const MCUiDraw& draw : draws.Draws)
        {
            SDL_GPUTexture* texture = _White.get();

            if (draw.Texture != nullptr)
            {
                const MCUiTexture& picture = *draw.Texture;
                auto found = _Textures.find(picture.Id);

                if (found == _Textures.end())
                {
                    auto made = UploadPicture(*pass, picture);

                    if (!made)
                    {
                        return std::unexpected(made.error());
                    }

                    found = _Textures.emplace(picture.Id, MCKept{.Picture = {}, .Texture = std::move(*made)}).first;
                }

                found->second.Picture = draw.Texture;
                texture = found->second.Texture.get();
            }

            const MCUiGeometry& geometry = *draw.Geometry;
            MCRun run{.First = static_cast<uint32_t>(vertices.size()),
                      .Count = static_cast<uint32_t>(geometry.Indices.size()),
                      .Texture = texture,
                      .Sampler = draw.Texture != nullptr && draw.Texture->Smooth ? _Linear.get() : _Nearest.get(),
                      .Scissor = Within(
                          draw.Scissor.value_or(SDL_Rect{0, 0, static_cast<int>(width), static_cast<int>(height)}),
                          width, height)};

            for (const int32_t index : geometry.Indices)
            {
                if (index < 0 || static_cast<size_t>(index) >= geometry.Vertices.size())
                {
                    return std::unexpected(
                        std::format("a menus' draw names vertex {} of {}", index, geometry.Vertices.size()));
                }

                const MCUiVertex& vertex = geometry.Vertices[static_cast<size_t>(index)];
                vertices.push_back(MCGpuVertex{
                    .Place = {static_cast<float>(MCUiPlace(vertex.X, draw.X)),
                              static_cast<float>(MCUiPlace(vertex.Y, draw.Y)), vertex.U, vertex.V},
                    .Color = {vertex.Color[0] * shown / ByteMax, vertex.Color[1] * shown / ByteMax,
                              vertex.Color[2] * shown / ByteMax, vertex.Color[3] / ByteMax},
                });
            }

            if (run.Count > 0 && run.Scissor.w > 0 && run.Scissor.h > 0)
            {
                runs.push_back(run);
            }
        }

        return vertices.empty() ? std::expected<void, std::string>{}
                                : FillVertices(*pass, std::as_bytes(std::span(vertices)));
    };

    auto uploaded = upload();
    SDL_EndGPUCopyPass(pass);

    if (!uploaded)
    {
        return uploaded;
    }

    SDL_GPUColorTargetInfo color{};
    color.texture = &target;
    color.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f};
    SDL_GPURenderPass* const render = SDL_BeginGPURenderPass(&commands, &color, 1, nullptr);

    if (render == nullptr)
    {
        return std::unexpected(SdlError("SDL_BeginGPURenderPass"));
    }

    if (!runs.empty())
    {
        SDL_BindGPUGraphicsPipeline(render, &pipeline->get());
        SDL_GPUBuffer* const buffer = _Vertices.get();
        SDL_BindGPUVertexStorageBuffers(render, 0, &buffer, 1);
        const SDL_GPUViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        SDL_SetGPUViewport(render, &viewport);
    }

    for (const MCRun& run : runs)
    {
        SDL_SetGPUScissor(render, &run.Scissor);
        const SDL_GPUTextureSamplerBinding binding{.texture = run.Texture, .sampler = run.Sampler};
        SDL_BindGPUFragmentSamplers(render, 0, &binding, 1);
        const MCUiUniforms uniforms{
            .TargetSize = {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f},
            .Batch = {run.First, 0, 0, 0},
        };

        SDL_PushGPUVertexUniformData(&commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(render, run.Count, 1, 0, 0);
    }

    SDL_EndGPURenderPass(render);
    // The textures of pictures nothing holds any more are let go of (SDL keeps them until the GPU is done with them).
    std::erase_if(_Textures, [](const auto& entry) { return entry.second.Picture.expired(); });
    return {};
}

SDL_Point MCUiFittedSize(int32_t width, int32_t height, int32_t largest)
{
    const int32_t longest = std::max(width, height);

    if (largest <= 0 || longest <= largest)
    {
        return SDL_Point{width, height};
    }

    const double scale = static_cast<double>(largest) / static_cast<double>(longest);
    const auto fit = [&](int32_t side)
    { return std::clamp(static_cast<int32_t>(std::lround(static_cast<double>(side) * scale)), 1, largest); };
    return SDL_Point{fit(width), fit(height)};
}
