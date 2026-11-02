#include "stdafx.h"
#include "platform/MCVulkanPresenter.h"
#include "platform/MCFrameLog.h"
#include "platform/MCVulkanShaders.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
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
        /// <summary>
        /// How the lights take the world's relief: its height from black to white and the texels its slope spans; then
        /// the sun's way along the ground, times its strength.
        /// </summary>
        float LightFacing[4] = {};
        /// <summary>How much more of their hue the lights give, the bloom's strength and the brightness it starts at.</summary>
        float LightLook[4] = {};
        /// <summary>The bloom texture's size in use and allocated.</summary>
        float BloomSize[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        /// <summary>The heat haze: its most shimmer and its waves' length (texels), the game's time, the fires in Heat.</summary>
        float Haze[4] = {};
        /// <summary>The living water: its strength (0 none) and its colour cycle's phase.</summary>
        float Water[4] = {};
        /// <summary>The ground's map: the texel of the map's middle, then the meters east and north per texel off it.</summary>
        float Ground[4] = {};
        float GroundAxes[4] = {};
        /// <summary>The clouds' shadows: how much light they take (0 none), their cover, their size and their speed.</summary>
        float Clouds[4] = {};
        /// <summary>The shake: the world's texels the world shown is moved by.</summary>
        float Shake[4] = {};
        /// <summary>The world's own light's colour (times Lighting's), and how much of their colour its colours lose.</summary>
        float AmbientLook[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        /// <summary>How much the spotlights' beams show in the air (0 none).</summary>
        float AirBeams[4] = {};
        /// <summary>How steeply the HD ground's relief maps stand its bumps (0: lit by its brightness).</summary>
        float ReliefLook[4] = {};
        /// <summary>How far a forest's canopy sways at the height of a gust (meters, 0 none), and how much brighter.</summary>
        float Sway[4] = {};
        /// <summary>The weather's overcast, its colours' grey, wet land's darkening and its veil (0: a clear sky).</summary>
        float Weather[4] = {};
        /// <summary>
        /// The wet look: how much sky wet ground mirrors, the puddles' level, the bright metal's gloss and the feet's
        /// puddles reacting in PuddleSteps.
        /// </summary>
        float WetLook[4] = {};
        /// <summary>One a foot's puddle reacting: its ground point (meters), its age (seconds) and its strength.</summary>
        float PuddleSteps[MCWeather::MaxPuddleSteps][4] = {};
        /// <summary>MCWeather::PrintCircles a foot: its footprint's circles, their middles and radii (meters).</summary>
        float PuddlePrints[MCWeather::MaxPuddleSteps * MCWeather::PrintCircles][4] = {};
        /// <summary>One a fire: its flames' centre, and how far to each side and above it the air shimmers.</summary>
        float Heat[MCSceneLights::MaxHeat][4] = {};
        /// <summary>Three a light: the ground under it, its height and reach, its colour.</summary>
        float Lights[MCSceneLights::MaxLights * 3][4] = {};
    };

    /// <summary>
    /// The window pixels to each side the relief's slope is taken across (HD lighting's facing): wide enough to follow a
    /// boulder's shading rather than its texture's grain.
    /// </summary>
    constexpr float FacingSpan = 2.0f;
    /// <summary>The length of the heat haze's waves, in the window's pixels.</summary>
    constexpr float HazeWaveLength = 9.0f;
    /// <summary>
    /// The bloom texture's mip levels (composite.pshader's BloomLevels): its last spreads a bright pixel over 2^5 of
    /// the first's texels, which are two of the world's.
    /// </summary>
    constexpr uint32_t BloomLevels = 6;
    /// <summary>The bloom's first level has a texel for this many of the world's pixels each way (composite.pshader's).</summary>
    constexpr uint32_t BloomScale = 2;
    /// <summary>A ground map whose axes span less (texels squared per square meter) shows no ground to lay water on.</summary>
    constexpr float MinGroundDeterminant = 1e-6f;
    /// <summary>The smallest clouds, in meters (their size divides the ground's meters).</summary>
    constexpr float MinCloudSize = 1.0f;
    /// <summary>The composite's mode for the bloom's bright pass (composite.pshader's BrightPass).</summary>
    constexpr float BrightPassMode = 2.0f;
    /// <summary>The composite shader's textures (composite.pshader's t0 to t6).</summary>
    constexpr size_t CompositeSamplers = 7;
    /// <summary>A quad's vertices (quad.vshader's two triangles).</summary>
    constexpr uint32_t QuadVertices = 6;

    using CompositeBindings = std::array<SDL_GPUTextureSamplerBinding, CompositeSamplers>;

    /// <summary>Draws a quad (<paramref name="quad"/>) of the composite in <paramref name="pass"/>.</summary>
    void DrawComposite(SDL_GPUCommandBuffer& commands, SDL_GPURenderPass& pass, const CompositeBindings& bindings,
                       const QuadUniforms& quad, const CompositeUniforms& uniforms)
    {
        SDL_BindGPUFragmentSamplers(&pass, 0, bindings.data(), static_cast<uint32_t>(bindings.size()));
        SDL_PushGPUVertexUniformData(&commands, 0, &quad, sizeof(quad));
        SDL_PushGPUFragmentUniformData(&commands, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(&pass, QuadVertices, 1, 0, 0);
    }

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

        if (GHdLighting != HdLightingOff)
        {
            std::ranges::copy(lighting.AmbientTint, uniforms.AmbientLook);
            uniforms.AmbientLook[3] = lighting.AmbientGrey;
            uniforms.AirBeams[0] = lighting.AirBeams;
            uniforms.ReliefLook[0] = MCSceneLights::ReliefMapsOn() ? lighting.ReliefMaps : 0.0f;
        }

        uniforms.LightTone[0] = lighting.Scatter;
        uniforms.LightTone[1] = lighting.Knee;
        uniforms.LightTone[2] = lighting.Most;
        uniforms.LightTone[3] = lighting.SelfLit;
        // The relief in the texture's pixels, its slope across FacingSpan of the window's each side.
        uniforms.LightFacing[0] = lighting.Facing * across;
        uniforms.LightFacing[1] = std::max(across * FacingSpan, 1.0f);

        if (GHdLighting != HdLightingOff && GHdRelief != 0)
        {
            // The sun's relief: the way away from the sun along the ground, as long as the shadow a pixel of the
            // relief's height casts (the sun shadows' window pixels right and up per pixel of height, a screen height
            // of ground GroundAspect of one).
            const float relief = lighting.SunRelief * uniforms.LightFacing[0];
            uniforms.LightFacing[2] = relief * lighting.SunShadowRight;
            uniforms.LightFacing[3] = -relief * lighting.SunShadowUp / MCSceneLights::GroundAspect();
        }

        uniforms.LightLook[0] = lighting.Tint;
        uniforms.LightLook[1] = GHdLighting != HdLightingOff ? lighting.Bloom : 0.0f;
        uniforms.LightLook[2] = lighting.BloomFrom;
        // The fires' air, its shimmer and its waves in the texture's pixels.
        const std::span<const MCSceneHeat> heat = MCSceneLights::ShownHeat(world);
        const size_t fires = lighting.Haze > 0.0f ? std::min(heat.size(), MCSceneLights::MaxHeat) : 0;
        uniforms.Haze[0] = lighting.Haze * across;
        uniforms.Haze[1] = HazeWaveLength * across;
        uniforms.Haze[2] = MCSceneLights::ShownTime();
        uniforms.Haze[3] = static_cast<float>(fires);

        // The living water and the clouds' shadows, laid on the ground: the ground's map inverted, from the texture's
        // pixels to meters (none for a window whose ground isn't known, or a view that shows no ground).
        const MCSceneGround ground = MCSceneLights::ShownGround(world).value_or(MCSceneGround{});
        const float eastX = ground.EastX * across;
        const float eastY = ground.EastY * down;
        const float northX = ground.NorthX * across;
        const float northY = ground.NorthY * down;
        const float determinant = eastX * northY - northX * eastY;
        const bool flat = std::abs(determinant) < MinGroundDeterminant;
        uniforms.Water[0] = !flat && GHdLighting != HdLightingOff ? lighting.Water : 0.0f;
        uniforms.Water[1] = MCSceneLights::ShownWaterPhase();
        uniforms.Ground[0] = ground.OriginX * across;
        uniforms.Ground[1] = ground.OriginY * down;

        // Port-only (HD weather): the overcast, its veil and the wet look, with the drops (the GPU's drawing alone).
        const bool weather =
            !flat && MCWeather::Shown() != MCWeatherKind::Clear && MCRenderer::GpuDrawing() == MCGpuDrawing::On;

        if (weather)
        {
            const MCWeather::MCWeatherLook look = MCWeather::ShownLook();
            uniforms.Weather[0] = std::clamp(look.Dark, 0.0f, 1.0f);
            uniforms.Weather[1] = std::clamp(look.Grey, 0.0f, 1.0f);
            uniforms.Weather[2] = std::clamp(look.Wet, 0.0f, 1.0f);
            uniforms.Weather[3] = std::clamp(look.Veil, 0.0f, 1.0f);

            // The wet look tells the ground from what stands on it, and its slopes, by the relief maps.
            if (MCSceneLights::ReliefMapsOn())
            {
                uniforms.WetLook[0] = std::clamp(look.Sheen, 0.0f, 1.0f);
                uniforms.WetLook[1] = MCWeather::PuddleLevel(look.Puddles);
                uniforms.WetLook[2] = std::clamp(look.Gloss, 0.0f, 1.0f);
                const std::span<const MCWeather::MCPuddleStep> steps = MCWeather::ShownPuddleSteps();
                const size_t count = std::min(steps.size(), MCWeather::MaxPuddleSteps);
                uniforms.WetLook[3] = static_cast<float>(count);

                for (size_t i = 0; i < count; ++i)
                {
                    const MCWeather::MCPuddleStep& step = steps[i];
                    std::ranges::copy(std::array{step.East, step.North, step.Age, step.Strength},
                                      uniforms.PuddleSteps[i]);

                    for (size_t k = 0; k < MCWeather::PrintCircles; ++k)
                    {
                        const MCWeather::MCGroundCircle& circle = step.Print[k];
                        std::ranges::copy(std::array{circle.East, circle.North, circle.Radius, 0.0f},
                                          uniforms.PuddlePrints[i * MCWeather::PrintCircles + k]);
                    }
                }
            }

            uniforms.AmbientLook[3] = 1.0f - (1.0f - uniforms.AmbientLook[3]) * (1.0f - uniforms.Weather[1]);
        }

        // The overcast hides the clouds' shadows.
        if (!flat && GHdClouds != 0 && !weather)
        {
            uniforms.Clouds[0] = std::clamp(lighting.Clouds, 0.0f, 1.0f);
            uniforms.Clouds[1] = std::clamp(lighting.CloudCover, 0.0f, 1.0f);
            uniforms.Clouds[2] = std::max(lighting.CloudSize, MinCloudSize);
            uniforms.Clouds[3] = lighting.CloudSpeed;
        }

        if (!flat && GHdSway != 0)
        {
            uniforms.Sway[0] = std::max(lighting.Sway, 0.0f);
            uniforms.Sway[1] = std::max(lighting.SwayLight, 0.0f);
        }

        if (!flat)
        {
            uniforms.GroundAxes[0] = northY / determinant;
            uniforms.GroundAxes[1] = -northX / determinant;
            uniforms.GroundAxes[2] = -eastY / determinant;
            uniforms.GroundAxes[3] = eastX / determinant;
        }

        // The shake, in the texture's whole pixels.
        const MCParticles::MCShake shake = MCParticles::ShownShake(world);
        uniforms.Shake[0] = std::round(shake.X * across);
        uniforms.Shake[1] = std::round(shake.Y * down);

        for (size_t i = 0; i < fires; ++i)
        {
            uniforms.Heat[i][0] = (heat[i].X + 0.5f) * across;
            uniforms.Heat[i][1] = (heat[i].Y + 0.5f) * down;
            uniforms.Heat[i][2] = std::max(heat[i].HalfWidth * across, 1.0f);
            uniforms.Heat[i][3] = std::max(heat[i].Rise * down, 1.0f);
        }

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
            uniforms.Lights[i * 3 + 1][2] = light.SpotX * across;
            uniforms.Lights[i * 3 + 1][3] = light.SpotY * down;
            uniforms.Lights[i * 3 + 2][0] = light.Red;
            uniforms.Lights[i * 3 + 2][1] = light.Green;
            uniforms.Lights[i * 3 + 2][2] = light.Blue;
            uniforms.Lights[i * 3 + 2][3] = light.SpotCos;
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

    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    presenter->_Linear =
        MCGpuHandle<SDL_GPUSampler>(SDL_CreateGPUSampler(device, &sampler), MCGpuRelease{presenter->_Device});

    if (presenter->_Linear == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUSampler(linear)"));
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

    for (std::vector<Texture>* textures : {&_Underlays, &_Blooms})
    {
        for (Texture& texture : *textures)
        {
            Release(texture);
        }
    }

    _Upload.reset();
    _Nearest.reset();
    _Linear.reset();
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
                                                           uint32_t height, bool exact, uint32_t levels)
{
    if (texture.Handle != nullptr && texture.Levels == levels &&
        (exact ? texture.Width == width && texture.Height == height
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
    info.num_levels = levels;
    texture.Handle = MCGpuHandle<SDL_GPUTexture>(SDL_CreateGPUTexture(_Device.get(), &info), MCGpuRelease{_Device});

    if (texture.Handle == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateGPUTexture"));
    }

    texture.Width = width;
    texture.Height = height;
    texture.Levels = levels;
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
    // Each underlay's world: its texture, its size in use and allocated (the GPU's surface is exactly its size, as its
    // draws saw it), its window, and its bloom (made before the composite's pass: none, null, without one).
    struct World
    {
        SDL_GPUTexture* Texture = nullptr;
        std::array<float, 4> Size{};
        std::shared_ptr<const MCWindow> Source;
        SDL_GPUTexture* Bloom = nullptr;
        /// <summary>The GPU world's palette indices (the living water's), else the op plane as the screen's draw binds it.</summary>
        SDL_GPUTexture* Indices = nullptr;
        /// <summary>Port-only (HD weather): the light the GPU world's raindrops caught, else the screen
        /// (unread).</summary>
        SDL_GPUTexture* Glints = nullptr;
    };

    std::vector<World> worlds(frame.Underlays.size());

    for (size_t i = 0; i < frame.Underlays.size(); ++i)
    {
        World& world = worlds[i];
        world.Source = frame.Underlays[i].Source.lock();
        world.Indices = ops;
        world.Glints = screen;

        if (world.Source == nullptr)
        {
            continue;
        }

        if (!gpuSurfaces)
        {
            world.Texture = _Underlays[i].Handle.get();
            world.Size = {static_cast<float>(world.Source->XMax + 1), static_cast<float>(world.Source->YMax + 1),
                          static_cast<float>(_Underlays[i].Width), static_cast<float>(_Underlays[i].Height)};
            continue;
        }

        uint32_t worldWidth = 0;
        uint32_t worldHeight = 0;
        world.Texture = held.emplace_back(_Renderer->SurfaceTexture(*world.Source, worldWidth, worldHeight)).get();

        if (world.Texture != nullptr)
        {
            world.Size = {static_cast<float>(worldWidth), static_cast<float>(worldHeight),
                          static_cast<float>(worldWidth), static_cast<float>(worldHeight)};
            world.Bloom = Bloom(commands, i, *world.Texture, worldWidth, worldHeight);

            if (SDL_GPUTexture* const indices = held.emplace_back(_Renderer->SurfaceIndexTexture(*world.Source)).get();
                indices != nullptr)
            {
                world.Indices = indices;
            }

            if (SDL_GPUTexture* const glints = held.emplace_back(_Renderer->SurfaceGlintTexture(*world.Source)).get();
                glints != nullptr)
            {
                world.Glints = glints;
            }
        }
    }

    SDL_GPUColorTargetInfo target{};
    target.texture = _Frame.Handle.get();
    // The first quad covers every pixel.
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commands, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, _CompositePipeline.get());
    const SDL_GPUViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);

    auto draw = [&](float x, float y, float w, float h, SDL_GPUTexture& world, SDL_GPUTexture& bloom,
                    SDL_GPUTexture& indices, SDL_GPUTexture& glints, CompositeUniforms uniforms)
    {
        uniforms.Cycle[0] = frame.Cycle.First;
        uniforms.Cycle[1] = frame.Cycle.Step;

        for (size_t i = 0; i < 8; ++i)
        {
            uniforms.CycleSources[i] = frame.Cycle.Sources[i];
        }

        uniforms.Fade[0] = static_cast<float>(frame.Fade) / 255.0f;

        const CompositeBindings bindings{{
            {screen, _Nearest.get()},
            {&indices, _Nearest.get()},
            {&world, _Nearest.get()},
            {tables, _Nearest.get()},
            {_Palette.Handle.get(), _Nearest.get()},
            {&bloom, _Linear.get()},
            {&glints, _Nearest.get()},
        }};

        DrawComposite(commands, *pass, bindings, {{x, y, w, h}, {width, height, 0.0f, 0.0f}}, uniforms);
    };

    // The screen alone, then each underlay's rectangle with its surface (the screen bound for a bloom it hasn't).
    draw(0.0f, 0.0f, width, height, *screen, *screen, *ops, *screen,
         CompositeUniforms{{screenSize[0], screenSize[1], screenSize[2], screenSize[3]},
                           {screenMap[0], screenMap[1], screenMap[2], screenMap[3]},
                           {0.0f, 0.0f, 1.0f, 1.0f},
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, source, drawnX, drawnY}});

    for (size_t i = 0; i < frame.Underlays.size(); ++i)
    {
        const MCUnderlay& underlay = frame.Underlays[i];
        const World& world = worlds[i];
        const int x0 = std::max(underlay.Rect.X0, 0);
        const int y0 = std::max(underlay.Rect.Y0, 0);
        const int x1 = std::min(underlay.Rect.X1, frame.Width - 1);
        const int y1 = std::min(underlay.Rect.Y1, frame.Height - 1);

        if (x1 < x0 || y1 < y0 || world.Texture == nullptr)
        {
            continue;
        }

        // The rectangle and its part on the screen, in target pixels.
        CompositeUniforms uniforms{
            {screenSize[0], screenSize[1], screenSize[2], screenSize[3]},
            {screenMap[0], screenMap[1], screenMap[2], screenMap[3]},
            {(static_cast<float>(underlay.Rect.X0) - originX) / mapX,
             (static_cast<float>(underlay.Rect.Y0) - originY) / mapY,
             static_cast<float>(underlay.Rect.X1 - underlay.Rect.X0 + 1) / mapX,
             static_cast<float>(underlay.Rect.Y1 - underlay.Rect.Y0 + 1) / mapY},
            {world.Size[0], world.Size[1], world.Size[2], world.Size[3]},
            {1.0f, source, drawnX, drawnY},
        };

        SetLighting(uniforms, *world.Source, world.Size[0], world.Size[1]);

        if (world.Bloom != nullptr)
        {
            const Texture& bloom = _Blooms[i];
            uniforms.BloomSize[0] = std::ceil(world.Size[0] / static_cast<float>(BloomScale));
            uniforms.BloomSize[1] = std::ceil(world.Size[1] / static_cast<float>(BloomScale));
            uniforms.BloomSize[2] = static_cast<float>(bloom.Width);
            uniforms.BloomSize[3] = static_cast<float>(bloom.Height);
        }
        else
        {
            uniforms.LightLook[1] = 0.0f;
        }

        draw((static_cast<float>(x0) - originX) / mapX, (static_cast<float>(y0) - originY) / mapY,
             static_cast<float>(x1 - x0 + 1) / mapX, static_cast<float>(y1 - y0 + 1) / mapY, *world.Texture,
             world.Bloom != nullptr ? *world.Bloom : *screen, *world.Indices, *world.Glints, uniforms);
    }

    SDL_EndGPURenderPass(pass);
}

SDL_GPUTexture* MCVulkanPresenter::Bloom(SDL_GPUCommandBuffer& commands, size_t index, SDL_GPUTexture& world,
                                         uint32_t width, uint32_t height)
{
    const MCSceneLights::MCLighting lighting = MCSceneLights::ShownLighting();
    // A texel for BloomScale x BloomScale of the world's pixels, which must hold the bloom's levels.
    const uint32_t bloomWidth = (width + BloomScale - 1) / BloomScale;
    const uint32_t bloomHeight = (height + BloomScale - 1) / BloomScale;

    if (GHdLighting == HdLightingOff || lighting.Bloom <= 0.0f ||
        std::max(bloomWidth, bloomHeight) < (1u << (BloomLevels - 1)))
    {
        return nullptr;
    }

    if (_Blooms.size() <= index)
    {
        _Blooms.resize(index + 1);
    }

    Texture& bloom = _Blooms[index];

    if (auto made = Ensure(bloom, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, bloomWidth, bloomHeight,
                           false, BloomLevels);
        !made)
    {
        SDL_Log("MCVulkanPresenter: underlay %zu shown without its bloom: %s", index, made.error().c_str());
        return nullptr;
    }

    SDL_GPUColorTargetInfo target{};
    target.texture = bloom.Handle.get();
    // Cleared: the part not in use adds nothing to the mips by the edge of the part that is.
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commands, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, _CompositePipeline.get());
    const float allocatedWidth = static_cast<float>(bloom.Width);
    const float allocatedHeight = static_cast<float>(bloom.Height);
    const SDL_GPUViewport viewport{0.0f, 0.0f, allocatedWidth, allocatedHeight, 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);
    CompositeUniforms uniforms{};
    const float worldWidth = static_cast<float>(width);
    const float worldHeight = static_cast<float>(height);
    std::ranges::copy(std::array{worldWidth, worldHeight, worldWidth, worldHeight}, uniforms.WorldSize);
    uniforms.Mode[0] = BrightPassMode;
    uniforms.Mode[1] = 1.0f;
    uniforms.LightLook[2] = lighting.BloomFrom;
    // The pass reads the world alone: it is bound for every texture.
    const CompositeBindings bindings{{
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Linear.get()},
        {&world, _Nearest.get()},
    }};

    DrawComposite(commands, *pass, bindings,
                  {{0.0f, 0.0f, static_cast<float>(bloomWidth), static_cast<float>(bloomHeight)},
                   {allocatedWidth, allocatedHeight, 0.0f, 0.0f}},
                  uniforms);
    SDL_EndGPURenderPass(pass);
    SDL_GenerateMipmapsForGPUTexture(&commands, bloom.Handle.get());
    return bloom.Handle.get();
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
