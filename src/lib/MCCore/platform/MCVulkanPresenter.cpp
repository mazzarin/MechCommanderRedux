#include "stdafx.h"
#include "platform/MCVulkanPresenter.h"
#include "platform/MCFrameLog.h"
#include "platform/MCVulkanShaders.h"
#include "sprite/MCSceneLights.h"
#include "vfx/MCVfx.h"

namespace
{
    std::string SdlError(std::string_view what)
    {
        return std::format("{}: {}", what, SDL_GetError());
    }

    /// <summary>The quad shader's uniforms (shaders/quad.vshader, cbuffer Quad).</summary>
    struct QuadUniforms
    {
        float Rect[4];
        float TargetSize[4];
    };

    /// <summary>The composite shader's uniforms (shaders/composite.pshader, cbuffer Composite).</summary>
    struct CompositeUniforms
    {
        float ScreenSize[4];
        float ScreenMap[4];
        float Shown[4];
        float WorldSize[4];
        float Mode[4];
        /// <summary>The colour cycle: the first index and the step (-1: none), then the eight sources.</summary>
        int32_t Cycle[4];
        uint32_t CycleSources[8];
        /// <summary>The screen fade: x, the amount taken off every component (0 none, 1 black).</summary>
        float Fade[4];
        /// <summary>Port-only (HD lighting): the lights, their strength, the world's own light and 1 / the tilt.</summary>
        float Lighting[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        /// <summary>The lights' tone: their scatter, the knee, the most light and the brightness of a light's own pixels.</summary>
        float LightTone[4] = {};
        /// <summary>Three a light: the ground under it, its height and reach, its colour.</summary>
        float Lights[MCSceneLights::MaxLights * 3][4] = {};
    };

    /// <summary>
    /// Sets <paramref name="uniforms"/>' lighting (sprite/MCSceneLights.h) to the lights world window
    /// <paramref name="world"/> shows, its pixels scaled to its texture's (<paramref name="width"/> by
    /// <paramref name="height"/>), and distances in the texture's pixels across.
    /// </summary>
    void SetLighting(CompositeUniforms& uniforms, const MCWindow& world, float width, float height)
    {
        const std::span<const MCSceneLight> lights = MCSceneLights::Shown(world);
        const MCSceneLights::MCLighting lighting = MCSceneLights::ShownLighting();
        const float across = width / static_cast<float>(world.XMax + 1);
        const float down = height / static_cast<float>(world.YMax + 1);
        const size_t count = std::min(lights.size(), MCSceneLights::MaxLights);
        uniforms.Lighting[0] = static_cast<float>(count);
        uniforms.Lighting[1] = lighting.Strength;
        uniforms.Lighting[2] = GHdLighting != HdLightingOff ? lighting.Ambient : 1.0f;
        uniforms.Lighting[3] = across / (down * MCSceneLights::GroundAspect());
        uniforms.LightTone[0] = lighting.Scatter;
        uniforms.LightTone[1] = lighting.Knee;
        uniforms.LightTone[2] = lighting.Most;
        uniforms.LightTone[3] = lighting.SelfLit;

        for (size_t i = 0; i < count; ++i)
        {
            // A light's point is a pixel's: its centre.
            const MCSceneLight& light = lights[i];
            const std::span<float, 4> ends = uniforms.Lights[i * 3];
            ends[0] = (light.X0 + 0.5f) * across;
            ends[1] = (light.Y0 + 0.5f) * down;
            ends[2] = (light.X1 + 0.5f) * across;
            ends[3] = (light.Y1 + 0.5f) * down;
            uniforms.Lights[i * 3 + 1][0] = light.Height * across;
            uniforms.Lights[i * 3 + 1][1] = light.Reach * across;
            uniforms.Lights[i * 3 + 2][0] = light.Red;
            uniforms.Lights[i * 3 + 2][1] = light.Green;
            uniforms.Lights[i * 3 + 2][2] = light.Blue;
        }
    }

    /// <summary>Upload offsets are kept on this boundary (the strictest any backend asks of a texture copy).</summary>
    constexpr uint32_t UploadAlignment = 512;

    uint32_t AlignUpload(uint32_t offset)
    {
        return (offset + UploadAlignment - 1) & ~(UploadAlignment - 1);
    }
}

std::expected<std::unique_ptr<MCVulkanPresenter>, std::string> MCVulkanPresenter::Create(
    std::shared_ptr<SDL_Window> window, bool vsync, const MCPresentation& presentation)
{
    auto presenter = std::make_unique<MCVulkanPresenter>(Key{});
    presenter->_Window = std::move(window);
    presenter->_Presentation = presentation;
    SDL_GPUDevice* device = SDL_CreateGPUDevice(MCVulkanShaders::Format, false, MCVulkanShaders::Driver);

    if (device == nullptr)
    {
        return std::unexpected(SdlError(std::format("SDL_CreateGPUDevice({})", MCVulkanShaders::Driver)));
    }

    presenter->_Device = std::shared_ptr<SDL_GPUDevice>(device, MCSdlDeleter{});

    if (!SDL_ClaimWindowForGPUDevice(device, presenter->_Window.get()))
    {
        return std::unexpected(SdlError("SDL_ClaimWindowForGPUDevice"));
    }

    presenter->_Claimed = true;
    presenter->SetVSync(vsync);

    auto quad = MCVulkanShaders::Load(presenter->_Device, MCVulkanShaders::Quad);

    if (!quad)
    {
        return std::unexpected(quad.error());
    }

    presenter->_QuadShader = std::move(*quad);
    auto composite = MCVulkanShaders::Load(presenter->_Device, MCVulkanShaders::Composite);

    if (!composite)
    {
        return std::unexpected(composite.error());
    }

    presenter->_CompositeShader = std::move(*composite);

    SDL_GPUColorTargetDescription target{};
    target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};
    pipeline.vertex_shader = presenter->_QuadShader.get();
    pipeline.fragment_shader = presenter->_CompositeShader.get();
    pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.num_color_targets = 1;
    pipeline.target_info.color_target_descriptions = &target;
    presenter->_CompositePipeline = MCGpuHandle<SDL_GPUGraphicsPipeline>(
        SDL_CreateGPUGraphicsPipeline(device, &pipeline), MCGpuRelease{presenter->_Device});

    if (presenter->_CompositePipeline == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUGraphicsPipeline(composite)"));
    }

    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    presenter->_Nearest =
        MCGpuHandle<SDL_GPUSampler>(SDL_CreateGPUSampler(device, &sampler), MCGpuRelease{presenter->_Device});

    if (presenter->_Nearest == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUSampler"));
    }

    // The renderer of the frame surfaces, drawing when asked to.
    const MCGpuDrawing drawing = MCRenderer::RequestedGpuDrawing();

    if (drawing != MCGpuDrawing::Off)
    {
        auto renderer = MCVulkanRenderer::Create(presenter->_Device);

        if (renderer)
        {
            presenter->_Renderer = std::move(*renderer);
            MCRenderer::SetHardware(presenter->_Renderer, drawing);

            // Port-only: each frame's draws see the screen placed as the frame will be shown.
            presenter->_Renderer->RecordingStarts = [placing = presenter.get()]
            {
                if (placing->FrameLayout)
                {
                    placing->PlaceUnderlays(placing->FrameLayout());
                }
            };
        }
        else
        {
            SDL_Log("MCVulkanPresenter: the GPU renderer can't start (%s); drawing in software",
                    renderer.error().c_str());
        }
    }

    return presenter;
}

MCVulkanPresenter::~MCVulkanPresenter()
{
    if (_Device == nullptr)
    {
        return;
    }

    SDL_WaitForGPUIdle(_Device.get());

    if (_Renderer != nullptr && MCRenderer::Hardware() == _Renderer)
    {
        MCRenderer::SetHardware(nullptr, MCGpuDrawing::Off);
    }

    // Whoever still holds the renderer (a test) holds no pointer to this presenter.
    if (_Renderer != nullptr)
    {
        _Renderer->RecordingStarts = nullptr;
    }

    _Renderer.reset();

    for (Texture& texture :
         {std::ref(_Screen), std::ref(_Ops), std::ref(_Tables), std::ref(_Palette), std::ref(_Frame)})
    {
        Release(texture);
    }

    for (Texture& texture : _Underlays)
    {
        Release(texture);
    }

    _Upload.reset();
    _Nearest.reset();
    _CompositePipeline.reset();
    _QuadShader.reset();
    _CompositeShader.reset();

    if (_Claimed)
    {
        SDL_ReleaseWindowFromGPUDevice(_Device.get(), _Window.get());
    }

    // The device is destroyed with its last holder: this, once the members are gone.
}

std::string MCVulkanPresenter::Name() const
{
    const std::string_view device =
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(_Device.get()), SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    return !device.empty() ? std::format("{} ({})", MCVulkanShaders::Driver, device)
                           : std::string(MCVulkanShaders::Driver);
}

void MCVulkanPresenter::SetVSync(bool on)
{
    _VSync = on;
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;

    if (!on)
    {
        if (SDL_WindowSupportsGPUPresentMode(_Device.get(), _Window.get(), SDL_GPU_PRESENTMODE_IMMEDIATE))
        {
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        }
        else if (SDL_WindowSupportsGPUPresentMode(_Device.get(), _Window.get(), SDL_GPU_PRESENTMODE_MAILBOX))
        {
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        }
    }

    SDL_SetGPUSwapchainParameters(_Device.get(), _Window.get(), SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

void MCVulkanPresenter::Release(Texture& texture)
{
    texture = Texture{};
}

std::expected<void, std::string> MCVulkanPresenter::Ensure(Texture& texture, SDL_GPUTextureFormat format,
                                                           SDL_GPUTextureUsageFlags usage, uint32_t width,
                                                           uint32_t height, bool exact)
{
    if (texture.Handle != nullptr && (exact ? texture.Width == width && texture.Height == height
                                            : texture.Width >= width && texture.Height >= height))
    {
        return {};
    }

    if (!exact)
    {
        // Grown with some room, so a zoom easing the surface's size a pixel at a time doesn't remake it every frame.
        width = std::max(texture.Width, width + width / 4);
        height = std::max(texture.Height, height + height / 4);
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
    return {};
}

bool MCVulkanPresenter::ShowsGpuSurfaces(const MCFrame& frame) const
{
    uint32_t width = 0;
    uint32_t height = 0;
    return _Renderer != nullptr && MCRenderer::GpuDrawing() == MCGpuDrawing::On &&
           _Renderer->SurfaceTexture(frame.Screen.get(), width, height) != nullptr;
}

std::expected<void, std::string> MCVulkanPresenter::Prepare(SDL_GPUCommandBuffer& commands, const MCFrame& frame,
                                                            bool& gpuSurfaces, bool read)
{
    if (_Renderer != nullptr && MCRenderer::GpuDrawing() != MCGpuDrawing::Off)
    {
        _Renderer->SetColors(frame.PaletteColors, frame.Cycle);

        if (!read)
        {
            PlaceUnderlays(frame);
        }

        if (auto executed = _Renderer->Execute(commands, frame.Underlays); !executed)
        {
            return executed;
        }
    }

    // Until the GPU has drawn the screen once, the screen's own (blank) pixels are shown.
    gpuSurfaces = ShowsGpuSurfaces(frame);
    return Upload(commands, frame, gpuSurfaces);
}

std::expected<void, std::string> MCVulkanPresenter::Upload(SDL_GPUCommandBuffer& commands, const MCFrame& frame,
                                                           bool gpuSurfaces)
{
    const uint32_t width = static_cast<uint32_t>(frame.Width);
    const uint32_t height = static_cast<uint32_t>(frame.Height);
    const uint32_t frameWidth = frame.ScaledOutput() ? static_cast<uint32_t>(frame.OutputWidth) : width;
    const uint32_t frameHeight = frame.ScaledOutput() ? static_cast<uint32_t>(frame.OutputHeight) : height;
    constexpr SDL_GPUTextureFormat R8 = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    constexpr SDL_GPUTextureFormat Rgba = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    bool paletteNew = _Palette.Handle == nullptr;

    for (auto made : {Ensure(_Palette, Rgba, SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, 1, true),
                      Ensure(_Frame, Rgba, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, frameWidth,
                             frameHeight, true)})
    {
        if (!made)
        {
            return made;
        }
    }

    // What goes up this frame: the palette when it changed; unless the GPU's surfaces are shown, the screen, and with
    // underlays their surfaces, the ops and the op tables in use.
    struct Part
    {
        std::span<const std::byte> Data;
        std::reference_wrapper<SDL_GPUTexture> Texture;
        uint32_t Width;
        uint32_t Height;
        uint32_t BytesPerPixel;
        uint32_t Offset;
    };

    std::vector<Part> parts;
    // The underlays' pictures, held while their pixels go up.
    std::vector<std::shared_ptr<const MCWindow>> sources;

    for (const MCUnderlay& underlay : frame.Underlays)
    {
        sources.push_back(underlay.Source.lock());
    }

    if (!gpuSurfaces)
    {
        for (auto made : {Ensure(_Screen, R8, SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height, true),
                          Ensure(_Ops, R8, SDL_GPU_TEXTUREUSAGE_SAMPLER, width, height, true),
                          Ensure(_Tables, R8, SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, 256, true)})
        {
            if (!made)
            {
                return made;
            }
        }

        if (_Underlays.size() < frame.Underlays.size())
        {
            _Underlays.resize(frame.Underlays.size());
        }

        for (size_t i = 0; i < frame.Underlays.size(); ++i)
        {
            const MCWindow& source = *sources[i];

            if (auto made =
                    Ensure(_Underlays[i], R8, SDL_GPU_TEXTUREUSAGE_SAMPLER, static_cast<uint32_t>(source.XMax + 1),
                           static_cast<uint32_t>(source.YMax + 1), false);
                !made)
            {
                return made;
            }
        }

        parts.push_back({std::as_bytes(frame.Pixels), *_Screen.Handle, width, height, 1, 0});
    }

    if (!gpuSurfaces && !frame.Underlays.empty())
    {
        parts.push_back({std::as_bytes(frame.Ops), *_Ops.Handle, width, height, 1, 0});
        parts.push_back({std::as_bytes(MCRenderer::OpTables()), *_Tables.Handle, 256,
                         static_cast<uint32_t>(MCRenderer::OpTableCount()), 1, 0});

        for (size_t i = 0; i < frame.Underlays.size(); ++i)
        {
            const MCWindow& source = *sources[i];
            const auto sourceWidth = static_cast<uint32_t>(source.XMax + 1);
            const auto sourceHeight = static_cast<uint32_t>(source.YMax + 1);
            parts.push_back({std::as_bytes(source.Bytes()), *_Underlays[i].Handle, sourceWidth, sourceHeight, 1, 0});
        }
    }

    if (frame.PaletteChanged || paletteNew)
    {
        // SDL_Color is r, g, b, a bytes: RGBA's layout. The alpha isn't used. The colour cycle is the shader's.
        parts.push_back({std::as_bytes(frame.PaletteColors), *_Palette.Handle, 256, 1, 4, 0});
    }

    if (parts.empty())
    {
        return {};
    }

    uint32_t total = 0;

    for (Part& part : parts)
    {
        part.Offset = AlignUpload(total);
        total = part.Offset + part.Width * part.Height * part.BytesPerPixel;
    }

    if (_Upload == nullptr || _UploadSize < total)
    {
        _Upload.reset();
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = total + total / 4;
        _Upload = MCGpuHandle<SDL_GPUTransferBuffer>(SDL_CreateGPUTransferBuffer(_Device.get(), &info),
                                                     MCGpuRelease{_Device});
        _UploadSize = _Upload != nullptr ? info.size : 0;

        if (_Upload == nullptr)
        {
            return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer"));
        }
    }

    void* const mapped = SDL_MapGPUTransferBuffer(_Device.get(), _Upload.get(), true);

    if (mapped == nullptr)
    {
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer"));
    }

    for (const Part& part : parts)
    {
        std::memcpy(static_cast<uint8_t*>(mapped) + part.Offset, part.Data.data(),
                    static_cast<size_t>(part.Width) * part.Height * part.BytesPerPixel);
    }

    SDL_UnmapGPUTransferBuffer(_Device.get(), _Upload.get());
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(&commands);

    for (const Part& part : parts)
    {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = _Upload.get();
        source.offset = part.Offset;
        source.pixels_per_row = part.Width;
        source.rows_per_layer = part.Height;
        SDL_GPUTextureRegion destination{};
        destination.texture = &part.Texture.get();
        destination.w = part.Width;
        destination.h = part.Height;
        destination.d = 1;
        SDL_UploadToGPUTexture(pass, &source, &destination, false);
    }

    SDL_EndGPUCopyPass(pass);
    return {};
}

void MCVulkanPresenter::Composite(SDL_GPUCommandBuffer& commands, const MCFrame& frame, bool gpuSurfaces)
{
    // The target: the screen's size, or the window pixels a scaled-up view covers. Screen pixels per target pixel, and
    // the screen pixel at the target's corner.
    const bool scaled = frame.ScaledOutput();
    const float width = static_cast<float>(scaled ? frame.OutputWidth : frame.Width);
    const float height = static_cast<float>(scaled ? frame.OutputHeight : frame.Height);
    const float mapX = scaled ? static_cast<float>(frame.View.w) / width : 1.0f;
    const float mapY = scaled ? static_cast<float>(frame.View.h) / height : 1.0f;
    const float originX = scaled ? static_cast<float>(frame.View.x) : 0.0f;
    const float originY = scaled ? static_cast<float>(frame.View.y) : 0.0f;
    const float screenMap[4] = {originX, originY, mapX, mapY};

    // The screen's texture and its allocated size; the GPU's surfaces have no op plane or op tables (the screen is
    // bound in their place, unread).
    SDL_GPUTexture* screen = _Screen.Handle.get();
    uint32_t screenWidth = static_cast<uint32_t>(frame.Width);
    uint32_t screenHeight = static_cast<uint32_t>(frame.Height);
    // The GPU's surfaces drawn from, held while the composite is recorded.
    std::vector<std::shared_ptr<SDL_GPUTexture>> held;

    if (gpuSurfaces)
    {
        screen = held.emplace_back(_Renderer->SurfaceTexture(frame.Screen.get(), screenWidth, screenHeight)).get();
    }

    SDL_GPUTexture* ops = gpuSurfaces ? screen : _Ops.Handle.get();
    SDL_GPUTexture* tables = gpuSurfaces ? screen : _Tables.Handle.get();
    const float source = gpuSurfaces ? 1.0f : 0.0f;
    // The screen's drawn pixels per screen pixel: above 1 for a sharp screen (MCFrame::SharpScreen), drawn at the size
    // it's shown.
    const auto [drawnX, drawnY] =
        gpuSurfaces ? _Renderer->SurfaceScale(frame.Screen.get()) : std::pair<float, float>{1.0f, 1.0f};
    const float screenSize[4] = {static_cast<float>(frame.Width), static_cast<float>(frame.Height),
                                 static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    SDL_GPUColorTargetInfo target{};
    target.texture = _Frame.Handle.get();
    // The first quad covers every pixel.
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commands, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, _CompositePipeline.get());
    const SDL_GPUViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);

    auto draw = [&](float x, float y, float w, float h, SDL_GPUTexture& world, CompositeUniforms uniforms)
    {
        uniforms.Cycle[0] = frame.Cycle.First;
        uniforms.Cycle[1] = frame.Cycle.Step;

        for (size_t i = 0; i < 8; ++i)
        {
            uniforms.CycleSources[i] = frame.Cycle.Sources[i];
        }

        uniforms.Fade[0] = static_cast<float>(frame.Fade) / 255.0f;

        const SDL_GPUTextureSamplerBinding bindings[5] = {
            {screen, _Nearest.get()},
            {ops, _Nearest.get()},
            {&world, _Nearest.get()},
            {tables, _Nearest.get()},
            {_Palette.Handle.get(), _Nearest.get()},
        };

        const QuadUniforms quad{{x, y, w, h}, {width, height, 0.0f, 0.0f}};
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 5);
        SDL_PushGPUVertexUniformData(&commands, 0, &quad, sizeof(quad));
        SDL_PushGPUFragmentUniformData(&commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
    };

    // The screen alone, then each underlay's rectangle with its surface.
    draw(0.0f, 0.0f, width, height, *screen,
         CompositeUniforms{{screenSize[0], screenSize[1], screenSize[2], screenSize[3]},
                           {screenMap[0], screenMap[1], screenMap[2], screenMap[3]},
                           {0.0f, 0.0f, 1.0f, 1.0f},
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, source, drawnX, drawnY}});

    for (size_t i = 0; i < frame.Underlays.size(); ++i)
    {
        const MCUnderlay& underlay = frame.Underlays[i];
        const int x0 = std::max(underlay.Rect.X0, 0);
        const int y0 = std::max(underlay.Rect.Y0, 0);
        const int x1 = std::min(underlay.Rect.X1, frame.Width - 1);
        const int y1 = std::min(underlay.Rect.Y1, frame.Height - 1);

        if (x1 < x0 || y1 < y0)
        {
            continue;
        }

        // The world's texture: its size in use and allocated (the GPU's surface is exactly its size, as its draws
        // saw it).
        SDL_GPUTexture* world = nullptr;
        float worldSize[4] = {};

        if (!gpuSurfaces)
        {
            const std::shared_ptr<const MCWindow> source = underlay.Source.lock();

            if (source == nullptr)
            {
                continue;
            }

            world = _Underlays[i].Handle.get();
            worldSize[0] = static_cast<float>(source->XMax + 1);
            worldSize[1] = static_cast<float>(source->YMax + 1);
            worldSize[2] = static_cast<float>(_Underlays[i].Width);
            worldSize[3] = static_cast<float>(_Underlays[i].Height);
        }
        else
        {
            uint32_t worldWidth = 0;
            uint32_t worldHeight = 0;
            const std::shared_ptr<const MCWindow> source = underlay.Source.lock();

            if (source == nullptr)
            {
                continue;
            }

            world = held.emplace_back(_Renderer->SurfaceTexture(*source, worldWidth, worldHeight)).get();

            if (world == nullptr)
            {
                continue;
            }

            worldSize[0] = worldSize[2] = static_cast<float>(worldWidth);
            worldSize[1] = worldSize[3] = static_cast<float>(worldHeight);
        }

        // The rectangle and its part on the screen, in target pixels.
        CompositeUniforms uniforms{
            {screenSize[0], screenSize[1], screenSize[2], screenSize[3]},
            {screenMap[0], screenMap[1], screenMap[2], screenMap[3]},
            {(static_cast<float>(underlay.Rect.X0) - originX) / mapX,
             (static_cast<float>(underlay.Rect.Y0) - originY) / mapY,
             static_cast<float>(underlay.Rect.X1 - underlay.Rect.X0 + 1) / mapX,
             static_cast<float>(underlay.Rect.Y1 - underlay.Rect.Y0 + 1) / mapY},
            {worldSize[0], worldSize[1], worldSize[2], worldSize[3]},
            {1.0f, source, drawnX, drawnY},
        };

        if (const std::shared_ptr<const MCWindow> lit = underlay.Source.lock(); lit != nullptr)
        {
            SetLighting(uniforms, *lit, worldSize[0], worldSize[1]);
        }

        draw((static_cast<float>(x0) - originX) / mapX, (static_cast<float>(y0) - originY) / mapY,
             static_cast<float>(x1 - x0 + 1) / mapX, static_cast<float>(y1 - y0 + 1) / mapY, *world, uniforms);
    }

    SDL_EndGPURenderPass(pass);
}

std::expected<void, std::string> MCVulkanPresenter::Present(const MCFrame& frame)
{
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));
    }

    bool gpuSurfaces = false;

    if (MCFrameLog::Scope part("present.prepare"); true)
    {
        if (auto prepared = Prepare(*commands, frame, gpuSurfaces, false); !prepared)
        {
            SDL_CancelGPUCommandBuffer(commands);
            return prepared;
        }
    }

    const bool drawing = _Renderer != nullptr && MCRenderer::GpuDrawing() != MCGpuDrawing::Off;

    if (MCFrameLog::Scope part("present.composite"); true)
    {
        Composite(*commands, frame, gpuSurfaces);
    }

    SDL_GPUTexture* swapchain = nullptr;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;

    if (MCFrameLog::Scope part("present.acquire");
        !SDL_WaitAndAcquireGPUSwapchainTexture(commands, _Window.get(), &swapchain, &swapchainWidth, &swapchainHeight))
    {
        SDL_SubmitGPUCommandBuffer(commands);
        return std::unexpected(SdlError("SDL_WaitAndAcquireGPUSwapchainTexture"));
    }

    // No swapchain image (a minimised or hidden window): the frame is composited but not shown.
    if (swapchain != nullptr)
    {
        const MCViewport shown = MCPresentViewport(static_cast<int>(swapchainWidth), static_cast<int>(swapchainHeight),
                                                   frame.View.w, frame.View.h, _Presentation);
        SDL_GPUBlitInfo blit{};
        blit.source.texture = _Frame.Handle.get();

        // A frame composited at the output's size is shown whole.
        if (frame.ScaledOutput())
        {
            blit.source.w = static_cast<uint32_t>(frame.OutputWidth);
            blit.source.h = static_cast<uint32_t>(frame.OutputHeight);
        }
        else
        {
            blit.source.x = static_cast<uint32_t>(frame.View.x);
            blit.source.y = static_cast<uint32_t>(frame.View.y);
            blit.source.w = static_cast<uint32_t>(frame.View.w);
            blit.source.h = static_cast<uint32_t>(frame.View.h);
        }

        blit.destination.texture = swapchain;
        blit.destination.x = static_cast<uint32_t>(std::lround(std::max(shown.X, 0.0f)));
        blit.destination.y = static_cast<uint32_t>(std::lround(std::max(shown.Y, 0.0f)));
        blit.destination.w = static_cast<uint32_t>(std::lround(shown.W));
        blit.destination.h = static_cast<uint32_t>(std::lround(shown.H));
        blit.load_op = SDL_GPU_LOADOP_CLEAR;
        blit.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
        blit.filter = _Presentation.Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;

        if (blit.destination.w != 0 && blit.destination.h != 0)
        {
            SDL_BlitGPUTexture(commands, &blit);
        }
    }

    if (MCFrameLog::Scope part("present.submit"); !SDL_SubmitGPUCommandBuffer(commands))
    {
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBuffer"));
    }

    if (drawing && MCRenderer::GpuDrawing() == MCGpuDrawing::Mirror)
    {
        if (auto compared = _Renderer->Compare(frame.Underlays, frame.Colors); !compared)
        {
            return std::unexpected(compared.error());
        }
    }

    return {};
}

std::expected<std::vector<SDL_Color>, std::string> MCVulkanPresenter::ReadFrame(const MCFrame& frame)
{
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));
    }

    bool gpuSurfaces = false;

    if (auto prepared = Prepare(*commands, frame, gpuSurfaces, true); !prepared)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected(prepared.error());
    }

    Composite(*commands, frame, gpuSurfaces);
    // The composite's size: the window pixels of a scaled frame, else the screen's.
    const auto width = static_cast<uint32_t>(frame.ScaledOutput() ? frame.OutputWidth : frame.Width);
    const auto height = static_cast<uint32_t>(frame.ScaledOutput() ? frame.OutputHeight : frame.Height);
    const uint32_t size = width * height * 4;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = size;
    SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(_Device.get(), &info);

    if (download == nullptr)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer(download)"));
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureRegion source{};
    source.texture = _Frame.Handle.get();
    source.w = width;
    source.h = height;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = download;
    destination.pixels_per_row = width;
    destination.rows_per_layer = height;
    SDL_DownloadFromGPUTexture(pass, &source, &destination);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);

    if (fence == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(_Device.get(), download);
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBufferAndAcquireFence"));
    }

    SDL_WaitForGPUFences(_Device.get(), true, &fence, 1);
    SDL_ReleaseGPUFence(_Device.get(), fence);
    std::vector<SDL_Color> pixels(static_cast<size_t>(width) * height);
    const void* mapped = SDL_MapGPUTransferBuffer(_Device.get(), download, false);

    if (mapped == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(_Device.get(), download);
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer(download)"));
    }

    // R8G8B8A8 is SDL_Color's byte order.
    std::memcpy(pixels.data(), mapped, size);
    SDL_UnmapGPUTransferBuffer(_Device.get(), download);
    SDL_ReleaseGPUTransferBuffer(_Device.get(), download);
    return pixels;
}

std::expected<std::vector<uint8_t>, std::string> MCVulkanPresenter::ReadScreen(const MCFrame& frame)
{
    if (_Renderer != nullptr && MCRenderer::GpuDrawing() == MCGpuDrawing::On)
    {
        auto shown = _Renderer->ReadShown(frame.Screen.get(), frame.Underlays);

        if (!shown || !shown->empty())
        {
            return shown;
        }
    }

    // The software renderer's pixels (also before the GPU has drawn the screen once, as the composite shows them).
    return MCPresenter::ReadScreen(frame);
}

std::expected<void, std::string> MCVulkanPresenter::Discard(const MCFrame& frame)
{
    if (_Renderer == nullptr || MCRenderer::GpuDrawing() == MCGpuDrawing::Off)
    {
        return {};
    }

    _Renderer->SetColors(frame.PaletteColors, frame.Cycle);
    PlaceUnderlays(frame);
    return _Renderer->Flush(frame.Underlays);
}

void MCVulkanPresenter::PlaceUnderlays(const MCFrame& frame)
{
    if (frame.ScaledOutput() && frame.View.w > 0 && frame.View.h > 0)
    {
        const float scaleX = static_cast<float>(frame.OutputWidth) / static_cast<float>(frame.View.w);
        const float scaleY = static_cast<float>(frame.OutputHeight) / static_cast<float>(frame.View.h);
        _Renderer->SetUnderlayScale(scaleX, scaleY);

        // Port-only: a sharp screen is drawn at the same scale, as far as the view reaches.
        if (frame.SharpScreen)
        {
            _Renderer->SetScreenScale(&frame.Screen.get(), scaleX, scaleY, frame.View.x + frame.View.w,
                                      frame.View.y + frame.View.h);
            return;
        }
    }
    else
    {
        _Renderer->SetUnderlayScale(1.0f, 1.0f);
    }

    _Renderer->SetScreenScale(nullptr, 1.0f, 1.0f, 0, 0);
}

MCViewport MCVulkanPresenter::Viewport(int viewWidth, int viewHeight) const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(_Window.get(), &width, &height);
    return MCPresentViewport(width, height, viewWidth, viewHeight, _Presentation);
}
