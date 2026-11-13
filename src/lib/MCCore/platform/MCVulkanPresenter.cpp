#include "stdafx.h"
#include "platform/MCVulkanPresenter.h"
#include "platform/MCFrameLog.h"
#include "platform/MCVulkanShaders.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
#include "terrain/MCGroundMesh.h"
#include "vfx/MCVfx.h"

namespace
{
    std::string SdlError(std::string_view what)
    {
        return std::format("{}: {}", what, SDL_GetError());
    }

    /// <summary>
    /// Port-only: the frames presented the game may still have on the GPU as it shows another (as many as SDL's two
    /// swapchain images hold), whatever the swapchain's images (MCVulkanPresenter::_FramesInFlight).
    /// </summary>
    constexpr size_t FramesHeldOnGpu = MCVulkanPresenter::TwoImageFrames;
    /// <summary>Port-only: SDL's name for its Vulkan driver (SDL_GetGPUDeviceDriver).</summary>
    constexpr std::string_view VulkanDriver = "vulkan";

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
        /// <summary>
        /// How much the spotlights' beams show in the air (0 none), how much of a spotlight's light the units in its way
        /// hide behind them, and the texture's pixels a window row (a unit's feet, composite.pshader's FeetAt).
        /// </summary>
        float AirBeams[4] = {};
        /// <summary>
        /// How steeply the HD ground's relief maps stand its bumps (0: lit by its brightness), and how much of the sun's
        /// relief a forest's canopy shows (1: as the ground's).
        /// </summary>
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
        /// <summary>The lightning: how much it lights the world now (0 none) and from which side of the view (-1 to 1).</summary>
        float Flash[4] = {};
        /// <summary>
        /// The snow: its cover's levels under which the ground lies under a dusting and thick (MCWeather::ShownCover),
        /// the share of the ground covered (0 none), and 1 when it snows (its veil white).
        /// </summary>
        float Snow[4] = {};
        /// <summary>
        /// Port-only (HD weather's snow): the specks (MCWeather::ShownSpeckle), in the texture's pixels: each layer's
        /// a texel where the veil's sheets are thinnest; how many times as many where they're thickest, the longest
        /// fall's way from where a speck shows to where it lands (texels across and down) and how much of the light in
        /// the air the falling ones catch; each layer's life; where each one's lattice lies (MCSpeckLattice; the first
        /// two, then the last two); each one's colour and weakest strength; each one's strongest; each one's square's
        /// side (whole texels) or, for a plus, how far its arms reach (texels; -1: a square).
        /// </summary>
        float Specks[4] = {};
        float SpeckSheets[4] = {};
        float SpeckLives[4] = {};
        float SpeckMoved[2][4] = {};
        float SpeckLooks[4][4] = {};
        float SpeckStrongest[4] = {};
        float SpeckSides[4] = {};
        float SpeckArms[4] = {};
        /// <summary>
        /// How far each falling layer falls as a share of the longest fall (SpeckSheets' yz; 0 for the resting), and,
        /// last, 1 when the index target's alpha tells the ground from what stands on it (MCSceneLights::ReliefMapsOn).
        /// </summary>
        float SpeckFalls[4] = {};
        /// <summary>
        /// The rain's streaks (MCSpeckLayer::Length): each falling layer's length in texels down its lattice's columns
        /// (0: specks), then how many texels across the columns slant a texel down (MCSpeckle::Slant).
        /// </summary>
        float SpeckStreaks[4] = {};
        /// <summary>
        /// The rain's splashes on water (MCSpeckle::OnWater): its specks a texel, how long each lives, its weakest and
        /// strongest; then its colour (rgb).
        /// </summary>
        float SpeckWater[4] = {};
        float SpeckWaterLook[4] = {};
        /// <summary>
        /// Port-only (HD weather and lighting): how the fog of war's edge darkens the snow, the wet ground's sky and the
        /// living water's foam and glints by how many of a tile's corners are seen: 0 none (no edge: 1s), 1 to 3
        /// MCTerrainFogEdgeShades' (rgb, 1 where it doesn't).
        /// </summary>
        float FogEdge[MCTerrainFogEdgeLevels + 1][4] = {
            {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
        /// <summary>
        /// Port-only (HD lighting): the light tiles' side in the world's texels (0: none) and how many there are across
        /// and down (LightTiles).
        /// </summary>
        float LightTiles[4] = {};
        /// <summary>The spotlights among the lights, a bit a light (the first 32 in the first word).</summary>
        uint32_t SpotLights[4] = {};
        /// <summary>One a foot's puddle reacting: its ground point (meters), its age (seconds) and its strength.</summary>
        float PuddleSteps[MCWeather::MaxPuddleSteps][4] = {};
        /// <summary>MCWeather::PrintCircles a foot: its footprint's circles, their middles and radii (meters).</summary>
        float PuddlePrints[MCWeather::MaxPuddleSteps * MCWeather::PrintCircles][4] = {};
        /// <summary>One a fire: its flames' centre, and how far to each side and above it the air shimmers.</summary>
        float Heat[MCSceneLights::MaxHeat][4] = {};
        /// <summary>Three a light: the ground under it, its height and reach, its colour.</summary>
        float Lights[MCSceneLights::MaxLights * 3][4] = {};
        /// <summary>
        /// MCSceneLights::LightUnits a light: the units in a spotlight's way (MCSceneLight::Units), each's feet, its box's
        /// half width and its height (0: none).
        /// </summary>
        float LightUnits[MCSceneLights::MaxLights * MCSceneLights::LightUnits][4] = {};
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
    /// <summary>The specks' lattices wrap every this many texels (composite.pshader's SpeckWrapMask).</summary>
    constexpr double SpeckWrap = 65536.0;
    static_assert(std::tuple_size_v<decltype(MCSpeckLattice::Moved)> ==
                      std::tuple_size_v<decltype(MCWeather::MCSpeckle::Layers)>,
                  "a lattice for each layer of specks");
    using MCVulkanShaders::CompositeSamplers;
    /// <summary>The puddle pass reaches this many texels past the world each way (composite.pshader's PuddleBorder).</summary>
    constexpr uint32_t PuddleBorder = 1;
    /// <summary>
    /// The puddles' field's format: a puddle's edge is where the field crosses the puddles' level, so it is kept whole.
    /// </summary>
    constexpr SDL_GPUTextureFormat PuddleFormat = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    /// <summary>
    /// The light tiles' side, in the world's texels (composite.pshader's lightTiles): a tile meets few lights, and a
    /// SIMD group's pixels mostly share one.
    /// </summary>
    constexpr float LightTileTexels = 32.0f;
    /// <summary>A light's box is this many texels wider each way than its reach, against its tile's rounding.</summary>
    constexpr float LightTilePadding = 1.0f;
    /// <summary>
    /// A spotlight's beam in the air (composite.pshader's AirBeamsAt) reaches this far past where its middle meets the
    /// ground (a share of the way from its lamp), and is this many texels wide at its lamp (AirBeamLength,
    /// AirBeamLampWidth).
    /// </summary>
    constexpr float AirBeamLength = 1.1f;
    constexpr float AirBeamLampWidth = 1.0f;
    /// <summary>A spotlight's pool is taken as this many radians wider each way along the ground, against rounding.</summary>
    constexpr float SpotWedgePadding = 0.02f;

    /// <summary>
    /// The box (left, top, right, bottom; the ground's units, a texel across and lighting.w of one down) of the ground a
    /// spotlight's pool can light (composite.pshader's SpotPool: nothing outside its beam): the wedge of its reach
    /// (<paramref name="reach"/>, from the ground under its lamp at <paramref name="x"/>, <paramref name="y"/>) round the
    /// way it aims (<paramref name="spotX"/>, <paramref name="spotY"/>, from its lamp <paramref name="height"/> up),
    /// as wide as its beam (the cosine <paramref name="edge"/> off its middle) spreads along the ground. None when its
    /// beam takes in the ground right under its lamp, where it lights every way.
    /// </summary>
    std::optional<std::array<float, 4>> SpotWedge(float x, float y, float reach, float height, float spotX, float spotY,
                                                  float edge)
    {
        constexpr float QuarterTurn = std::numbers::pi_v<float> / 2.0f;
        // The beam's half width, and how far below the ground's plane its middle points.
        const float half = std::acos(std::clamp(edge, -1.0f, 1.0f));
        const float down = std::atan2(height, std::hypot(spotX, spotY));

        if (half + down + SpotWedgePadding >= QuarterTurn)
        {
            return std::nullopt;
        }

        // A way within `half` of the beam's middle turns at most this far from it along the ground.
        const float spread = std::asin(std::sin(half) / std::cos(down)) + SpotWedgePadding;
        const float aim = std::atan2(spotY, spotX);
        std::array<float, 4> box{x, y, x, y};
        const auto take = [&](float angle)
        {
            const float px = x + reach * std::cos(angle);
            const float py = y + reach * std::sin(angle);
            box = {std::min(box[0], px), std::min(box[1], py), std::max(box[2], px), std::max(box[3], py)};
        };

        take(aim - spread);
        take(aim + spread);

        // The wedge's arc reaches furthest each way where it crosses a quarter turn.
        for (int quarter = -4; quarter <= 4; ++quarter)
        {
            const float angle = static_cast<float>(quarter) * QuarterTurn;

            if (std::abs(angle - aim) <= spread)
            {
                take(angle);
            }
        }

        return box;
    }

    /// <summary>The lights' bits come in words of this many (composite.pshader's LightWordBits).</summary>
    constexpr size_t LightWordBits = 32;
    /// <summary>
    /// A light tile's texels: a word of its lights' bits in each, its bytes red to alpha (a little-endian word's), read
    /// as the shaders read every other byte.
    /// </summary>
    constexpr SDL_GPUTextureFormat LightTileFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    static_assert(std::endian::native == std::endian::little, "a light tile's words go up as their bytes");

    /// <summary>A light tile: the lights whose pool may reach it, a bit a light (the first LightWordBits in the first).</summary>
    using LightTile = std::array<uint32_t, 2>;
    /// <summary>A light tile's texels side by side (composite.pshader's LightWords), a word each.</summary>
    constexpr uint32_t LightTileTexelsWide = std::tuple_size_v<LightTile>;

    /// <summary>A world's light tiles, LightTileTexels on a side from its top left: Across x Down of them, row by row.</summary>
    struct LightTiles
    {
        uint32_t Across = 0;
        uint32_t Down = 0;
        std::vector<LightTile> Tiles;
    };

    /// <summary>
    /// The light tiles of a world <paramref name="width"/> x <paramref name="height"/> texels for the first
    /// <paramref name="count"/> lights of <paramref name="uniforms"/>: each light in every tile its box meets, the
    /// ground within its reach of its point or beam (composite.pshader's LightAt: heights stretched as the ground's,
    /// lighting.w; at least a texel; a spotlight's, the wedge of it its beam meets, SpotWedge), and a spotlight's also in
    /// every tile the box of its beam in the air meets (AirBeamsAt), so a tile holds every light that can light one of
    /// its texels. A texel off the world takes its
    /// nearest tile's lights, so a box off it marks the tiles at its edge.
    /// </summary>
    LightTiles MakeLightTiles(const CompositeUniforms& uniforms, size_t count, float width, float height)
    {
        LightTiles made;
        made.Across = std::max(static_cast<uint32_t>(std::ceil(width / LightTileTexels)), 1u);
        made.Down = std::max(static_cast<uint32_t>(std::ceil(height / LightTileTexels)), 1u);
        made.Tiles.assign(static_cast<size_t>(made.Across) * made.Down, LightTile{});
        // The tiles a box from low to high texels meets, in tiles of a row or column of `tiles`; all of them for a box
        // that isn't a number.
        const auto tilesMet = [](float low, float high, uint32_t tiles)
        {
            const float last = static_cast<float>(tiles - 1);
            const float first = low > 0.0f ? std::min(std::floor(low / LightTileTexels), last) : 0.0f;
            const float end = high < last * LightTileTexels ? std::max(std::floor(high / LightTileTexels), 0.0f) : last;
            return std::pair{static_cast<uint32_t>(first), static_cast<uint32_t>(end)};
        };

        // A texel down the screen is this much of the ground's distance across (LightAt's ground).
        const float stretch = uniforms.Lighting[3];

        for (size_t i = 0; i < count; ++i)
        {
            const std::span<const float, 4> ends = uniforms.Lights[i * 3];
            const std::span<const float, 4> shape = uniforms.Lights[i * 3 + 1];
            const float reach = std::max(shape[1], 1.0f) + LightTilePadding;
            const float reachDown =
                stretch > 0.0f ? reach / stretch + LightTilePadding : std::numeric_limits<float>::infinity();
            const uint32_t bit = 1u << (i % LightWordBits);
            // Marks the tiles a box from (left, top) to (right, bottom) texels meets.
            const auto mark = [&](float left, float top, float right, float bottom)
            {
                const auto [x0, x1] = tilesMet(left, right, made.Across);
                const auto [y0, y1] = tilesMet(top, bottom, made.Down);

                for (uint32_t y = y0; y <= y1; ++y)
                {
                    for (uint32_t x = x0; x <= x1; ++x)
                    {
                        made.Tiles[static_cast<size_t>(y) * made.Across + x][i / LightWordBits] |= bit;
                    }
                }
            };

            // A spotlight's pool lights only the wedge its beam meets (from its point).
            const bool spot = (uniforms.SpotLights[i / LightWordBits] & bit) != 0;
            const bool point = ends[0] == ends[2] && ends[1] == ends[3];
            const std::optional<std::array<float, 4>> wedge =
                spot && point && stretch > 0.0f ? SpotWedge(ends[0], ends[1] * stretch, reach, shape[0], shape[2],
                                                            shape[3] * stretch, uniforms.Lights[i * 3 + 2][3])
                                                : std::nullopt;

            if (wedge.has_value())
            {
                const std::array<float, 4>& box = *wedge;
                mark(box[0] - LightTilePadding, box[1] / stretch - LightTilePadding, box[2] + LightTilePadding,
                     box[3] / stretch + LightTilePadding);
            }
            else
            {
                mark(std::min(ends[0], ends[2]) - reach, std::min(ends[1], ends[3]) - reachDown,
                     std::max(ends[0], ends[2]) + reach, std::max(ends[1], ends[3]) + reachDown);
            }

            if (spot)
            {
                // The beam in the air: from its lamp, its height up the screen from the ground under it, along the way
                // to where its middle meets the ground, AirBeamLength of it, widening as it spreads (the cosine of its
                // edge off its middle).
                const float lampX = ends[0];
                const float lampY = ends[1] - shape[0];
                const float beamX = shape[2];
                const float beamY = shape[3] + shape[0];
                const float edge = uniforms.Lights[i * 3 + 2][3];
                const float spread = std::sqrt(std::max(1.0f - edge * edge, 0.0f)) / edge;
                const float wide =
                    AirBeamLampWidth + AirBeamLength * std::hypot(beamX, beamY) * spread + LightTilePadding;
                const float endX = lampX + beamX * AirBeamLength;
                const float endY = lampY + beamY * AirBeamLength;
                mark(std::min(lampX, endX) - wide, std::min(lampY, endY) - wide, std::max(lampX, endX) + wide,
                     std::max(lampY, endY) + wide);
            }
        }

        return made;
    }

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
    /// Port-only (HD weather's snow and rain): sets <paramref name="uniforms"/>' specks (MCWeather::ShownSpeckle) for a
    /// world texture <paramref name="width"/> by <paramref name="height"/>, <paramref name="across"/> and
    /// <paramref name="down"/> texels a view pixel, its ground's map (uniforms' Ground) and time (Haze's) set; moving
    /// <paramref name="lattice"/> on by the game's time since it last moved, each layer's as its specks fall, and all as
    /// far as the ground point at the texture's middle then has moved on the screen since (as the view pans, not as it
    /// zooms about it). A rain streak layer's lattice lies along the streaks' slant.
    /// </summary>
    void SetSpeckle(CompositeUniforms& uniforms, MCSpeckLattice& lattice, float across, float down, float width,
                    float height)
    {
        const MCWeather::MCSpeckle speckle = MCWeather::ShownSpeckle();
        // The rain's streaks' lattices lie along their slant: a texel's column is its across less the slant times its
        // down, so a streak falling along it keeps its column and a pan moves it as it moves the world.
        const float slant = speckle.Slant * across / down;
        const float time = uniforms.Haze[2];
        const std::array<float, 2> ground{uniforms.Ground[0], uniforms.Ground[1]};
        const std::array<float, 2> scale{across, down};
        const std::array<float, 2> middle{0.5f * width, 0.5f * height};

        if (lattice.Placed && lattice.Scale[0] > 0.0f && lattice.Scale[1] > 0.0f)
        {
            const double step = std::max(time - lattice.Time, 0.0f);
            std::array<double, 2> panned{};

            for (size_t axis = 0; axis < middle.size(); ++axis)
            {
                panned[axis] = ground[axis] - middle[axis] +
                               (middle[axis] - lattice.Ground[axis]) * scale[axis] / lattice.Scale[axis];
            }

            const auto wrapped = [](double moved) { return moved - SpeckWrap * std::floor(moved / SpeckWrap); };

            for (size_t layer = 0; layer < lattice.Moved.size(); ++layer)
            {
                const MCWeather::MCSpeckLayer& shown = speckle.Layers[layer];
                const std::array<double, 2> fell{shown.Across * scale[0] * step, shown.Down * scale[1] * step};
                // A streak layer's lattice lies along the slant (its first axis across its columns): what moved it
                // down moves it back across as far as the slant takes that.
                const double slid = shown.Length > 0.0f ? slant * (panned[1] + fell[1]) : 0.0;
                lattice.Moved[layer][0] = wrapped(lattice.Moved[layer][0] + panned[0] + fell[0] - slid);
                lattice.Moved[layer][1] = wrapped(lattice.Moved[layer][1] + panned[1] + fell[1]);
            }
        }

        lattice.Placed = true;
        lattice.Time = time;
        lattice.Ground = ground;
        lattice.Scale = scale;
        uniforms.SpeckSheets[0] = speckle.ThickSheets;
        uniforms.SpeckSheets[3] = speckle.Catch;
        const auto longest = std::ranges::max_element(speckle.Layers, {},
                                                      [](const MCWeather::MCSpeckLayer& layer) { return layer.Fall; });

        if (longest->Fall > 0.0f && longest->Down > 0.0f)
        {
            uniforms.SpeckSheets[1] = longest->Across / longest->Down * longest->Fall * across;
            uniforms.SpeckSheets[2] = longest->Fall * down;
        }

        for (size_t layer = 0; layer < speckle.Layers.size(); ++layer)
        {
            const MCWeather::MCSpeckLayer& shown = speckle.Layers[layer];
            uniforms.Specks[layer] = std::max(shown.Density, 0.0f) / (across * down);
            uniforms.SpeckLives[layer] = shown.Life;
            uniforms.SpeckMoved[layer / 2][layer % 2 * 2] = static_cast<float>(lattice.Moved[layer][0]);
            uniforms.SpeckMoved[layer / 2][layer % 2 * 2 + 1] = static_cast<float>(lattice.Moved[layer][1]);
            std::ranges::copy(std::array{shown.Red, shown.Green, shown.Blue, shown.Weakest},
                              uniforms.SpeckLooks[layer]);
            uniforms.SpeckStrongest[layer] = shown.Strongest;
            // As a hard particle is drawn: whole texels, at least one (draw.pshader's HardParticlePixel).
            uniforms.SpeckSides[layer] = std::max(std::round(shown.Side * across), 1.0f);
            uniforms.SpeckArms[layer] = shown.Arm > 0.0f ? shown.Arm * across : -1.0f;
            uniforms.SpeckFalls[layer] = shown.Fall > 0.0f ? shown.Fall / longest->Fall : 0.0f;

            // A streak a texel thick, as faint as a drop thinner than one shows (its width's share of the texel).
            if (shown.Length > 0.0f && layer < MCWeather::MCSpeckle::Resting)
            {
                const float thickness = std::min(shown.Side * across, 1.0f);
                uniforms.SpeckLooks[layer][3] *= thickness;
                uniforms.SpeckStrongest[layer] *= thickness;
                uniforms.SpeckSides[layer] = 1.0f;
                uniforms.SpeckStreaks[layer] = shown.Length * down;
            }
        }

        uniforms.SpeckStreaks[3] = slant;
        const MCWeather::MCSpeckLayer& water = speckle.OnWater;
        std::ranges::copy(
            std::array{std::max(water.Density, 0.0f) / (across * down), water.Life, water.Weakest, water.Strongest},
            uniforms.SpeckWater);
        std::ranges::copy(std::array{water.Red, water.Green, water.Blue, 0.0f}, uniforms.SpeckWaterLook);

        uniforms.SpeckFalls[MCWeather::MCSpeckle::Resting] = MCSceneLights::ReliefMapsOn() ? 1.0f : 0.0f;
    }

    /// <summary>
    /// Sets <paramref name="uniforms"/>' lighting (sprite/MCSceneLights.h) to the lights world window
    /// <paramref name="world"/> shows, its pixels scaled to its texture's (<paramref name="width"/> by
    /// <paramref name="height"/>), and distances in the texture's pixels across, with the snow's specks moved on in
    /// <paramref name="specks"/>; returns how many lights it set (their tiles: <see cref="TileLights"/>). Reads the
    /// game's lights and weather: the game's thread, as the frame is presented.
    /// </summary>
    size_t SetLighting(CompositeUniforms& uniforms, MCSpeckLattice& specks, const MCWindow& world, float width,
                       float height)
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
            uniforms.AirBeams[1] = lighting.Shadow;
            uniforms.AirBeams[2] = down;
            uniforms.ReliefLook[0] = MCSceneLights::ReliefMapsOn() ? lighting.ReliefMaps : 0.0f;
            uniforms.ReliefLook[1] = lighting.CanopyRelief;
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

        {
            // The fog of war's edge's shades, for the weather and the living water (composite.pshader's FogEdgeShade).
            const std::array<std::array<float, 3>, MCTerrainFogEdgeLevels> fogEdge = MCTerrainFogEdgeShades();

            for (size_t level = 0; level < MCTerrainFogEdgeLevels; ++level)
            {
                std::copy(fogEdge[level].begin(), fogEdge[level].end(), uniforms.FogEdge[level + 1]);
            }
        }

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
            const MCWeather::MCFlash flash = MCWeather::ShownFlash();
            uniforms.Flash[0] = std::clamp(flash.Light, 0.0f, 1.0f);
            uniforms.Flash[1] = std::clamp(flash.Side, -1.0f, 1.0f);
            // The snow's cover (with the relief maps, which tell the ground from what stands on it) and its white veil.
            const MCWeather::MCSnowCover cover = MCWeather::ShownCover();
            uniforms.Snow[0] = cover.Dusting;
            uniforms.Snow[1] = cover.Thick;
            uniforms.Snow[2] = std::clamp(cover.Share, 0.0f, 1.0f);
            uniforms.Snow[3] = look.Snows ? 1.0f : 0.0f;
            SetSpeckle(uniforms, specks, across, down, width, height);

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

            for (size_t k = 0; k < light.Units.size(); ++k)
            {
                const MCSceneUnitBox& unit = light.Units[k];
                const std::span<float, 4> shown = uniforms.LightUnits[i * MCSceneLights::LightUnits + k];
                shown[0] = (unit.X + 0.5f) * across;
                shown[1] = (unit.Y + 0.5f) * down;
                shown[2] = unit.HalfWidth * across;
                shown[3] = unit.Height * down;
            }

            // A spotlight: one aimed (composite.pshader's AirBeamsAt), with an angle its beam fades out at.
            if (!(light.SpotCos <= 0.0f) &&
                (uniforms.Lights[i * 3 + 1][2] != 0.0f || uniforms.Lights[i * 3 + 1][3] != 0.0f))
            {
                uniforms.SpotLights[i / LightWordBits] |= 1u << (i % LightWordBits);
            }
        }

        return count;
    }

    /// <summary>
    /// The tiles of the first <paramref name="count"/> lights <see cref="SetLighting"/> set in
    /// <paramref name="uniforms"/>, for a world texture <paramref name="width"/> by <paramref name="height"/> (none
    /// without lights), their size set in the uniforms. Reads the uniforms alone, so it runs with the composite.
    /// </summary>
    LightTiles TileLights(CompositeUniforms& uniforms, size_t count, float width, float height)
    {
        if (count == 0)
        {
            return {};
        }

        LightTiles tiles = MakeLightTiles(uniforms, count, width, height);
        uniforms.LightTiles[0] = LightTileTexels;
        uniforms.LightTiles[1] = static_cast<float>(tiles.Across);
        uniforms.LightTiles[2] = static_cast<float>(tiles.Down);
        return tiles;
    }

    /// <summary>The part of <paramref name="rect"/> on a screen <paramref name="width"/> x <paramref name="height"/>.</summary>
    MCRect OnScreen(const MCRect& rect, int width, int height)
    {
        return MCRect{std::max(rect.X0, 0), std::max(rect.Y0, 0), std::min(rect.X1, width - 1),
                      std::min(rect.Y1, height - 1)};
    }

    /// <summary>Port-only: <paramref name="first"/> and <paramref name="then"/>, the errors of one call.</summary>
    std::string Joined(std::string first, std::string_view then)
    {
        if (first.empty())
        {
            return std::string(then);
        }

        return then.empty() ? first : std::format("{}; {}", first, then);
    }

    /// <summary>Port-only: done, or <paramref name="errors"/> when there are any.</summary>
    std::expected<void, std::string> Outcome(std::string errors)
    {
        if (errors.empty())
        {
            return {};
        }

        return std::unexpected(std::move(errors));
    }

    /// <summary>
    /// Port-only: an underlay's world as its frame was planned (<see cref="MCVulkanPresenter::Plan"/>), in the order
    /// of the frame's underlays.
    /// </summary>
    struct PlannedWorld
    {
        /// <summary>Whether it's composited: its picture there, its rectangle on the screen, its GPU surface drawn.</summary>
        bool Shown = false;
        /// <summary>Its picture's frame surface (the GPU's).</summary>
        MCAddressKey Key;
        /// <summary>Where it's shown, in screen pixels (may reach past the screen).</summary>
        MCRect Rect{};
        /// <summary>Its picture's size in use: its GPU surface's as sealed, else its window's.</summary>
        std::array<uint32_t, 2> Size{};
        /// <summary>Its uniforms' lighting (<see cref="SetLighting"/>), and the lights it set.</summary>
        CompositeUniforms Uniforms{};
        size_t Lights = 0;
    };

    /// <summary>
    /// Port-only: the frames run ahead's wait for a swapchain image once drawn (smoothed, ms) past which the display
    /// paces the frames (MCVulkanPresenter::ShowBeforeInput), and how much of each new wait the smoothing takes in.
    /// </summary>
    constexpr float PacedImageWaitMs = 0.25f;
    constexpr float ImageWaitSmoothing = 0.25f;

    /// <summary>Port-only: the frame log's parts of a frame run inline, and of one run ahead (the render thread's).</summary>
    constexpr std::string_view PreparePart = "present.prepare";
    constexpr std::string_view CompositePart = "present.composite";
    constexpr std::string_view PrepareAheadPart = "render.prepare";
    constexpr std::string_view CompositeAheadPart = "render.composite";
}

/// <summary>
/// Port-only: what a frame's run reads, taken as it's presented (<see cref="MCVulkanPresenter::Plan"/>).
/// </summary>
struct MCVulkanPresenter::PlannedFrame
{
    /// <summary>The frame presented, while it runs inline (its spans the display's); none for a frame run ahead.</summary>
    const MCFrame* Frame = nullptr;
    /// <summary>The screen's size.</summary>
    int Width = 0;
    int Height = 0;
    /// <summary>Where the composite is shown from.</summary>
    ShownLayout Layout;
    MCColorCycle Cycle;
    int Fade = 0;
    /// <summary>The palette (<see cref="MCFrame::PaletteColors"/>), and whether it changed since the last frame.</summary>
    std::array<SDL_Color, 256> Palette{};
    bool PaletteChanged = false;
    /// <summary>Whether the GPU's surfaces are composited (<see cref="ShowsGpuSurfaces"/>), and the screen's.</summary>
    bool GpuSurfaces = false;
    MCAddressKey ScreenKey;
    /// <summary>The renderer's commands for the frame (none: nothing recorded).</summary>
    MCVulkanRenderer::SealedFrame Run;
    /// <summary>Port-only (HD lighting): the bloom's brightness it starts from; none without a bloom.</summary>
    std::optional<float> BloomFrom;
    std::vector<PlannedWorld> Worlds;
    /// <summary>What went wrong running it ahead (empty: nothing).</summary>
    std::string Error;
};

namespace
{

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

    // Port-only: the third swapchain image, asked for the vsync the presenter starts with before the window is claimed,
    // so its swapchain is made once, with the images it keeps (two without vsync).
    if (const char* const driver = SDL_GetGPUDeviceDriver(device);
        ThirdImageEverywhere || (driver != nullptr && std::string_view(driver) == VulkanDriver))
    {
        presenter->_ThirdImage = true;
        presenter->AskFramesInFlight(vsync);
    }

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
    // The composite, with the weather (and the snow's specks, or not) and without, and the GPU world's passes before
    // it, each composite.pshader built for it alone, with the format each draws.
    constexpr SDL_GPUTextureFormat Rgba = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    const std::array composites{
        std::tuple{&MCVulkanShaders::Composite, &presenter->_CompositeShader, &presenter->_CompositePipeline, Rgba},
        std::tuple{&MCVulkanShaders::CompositeDry, &presenter->_DryShader, &presenter->_DryPipeline, Rgba},
        std::tuple{&MCVulkanShaders::CompositeUnspecked, &presenter->_UnspeckedShader, &presenter->_UnspeckedPipeline,
                   Rgba},
        std::tuple{&MCVulkanShaders::CompositeBright, &presenter->_BrightShader, &presenter->_BrightPipeline, Rgba},
        std::tuple{&MCVulkanShaders::CompositeField, &presenter->_FieldShader, &presenter->_FieldPipeline, Rgba},
        std::tuple{&MCVulkanShaders::CompositePuddle, &presenter->_PuddleShader, &presenter->_PuddlePipeline,
                   PuddleFormat},
    };

    for (const auto& [info, shader, made, format] : composites)
    {
        auto loaded = MCVulkanShaders::Load(presenter->_Device, *info);

        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }

        *shader = std::move(*loaded);

        SDL_GPUColorTargetDescription target{};
        target.format = format;
        SDL_GPUGraphicsPipelineCreateInfo pipeline{};
        pipeline.vertex_shader = presenter->_QuadShader.get();
        pipeline.fragment_shader = shader->get();
        pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline.target_info.num_color_targets = 1;
        pipeline.target_info.color_target_descriptions = &target;
        *made = MCGpuHandle<SDL_GPUGraphicsPipeline>(SDL_CreateGPUGraphicsPipeline(device, &pipeline),
                                                     MCGpuRelease{presenter->_Device});

        if (*made == nullptr)
        {
            return std::unexpected(
                SdlError(std::format("SDL_CreateGPUGraphicsPipeline({})", MCVulkanShaders::Name(info->Shader))));
        }
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

            // Port-only: what touches the run's side of the renderer waits for a frame run ahead; and as the next
            // frame is drawn, the one run ahead is shown once it's done.
            presenter->_Renderer->WaitForRun = [waiting = presenter.get()] { waiting->_Thread.Wait(); };
            presenter->_Renderer->Checkpoint = [showing = presenter.get()] { showing->ShowFinished(); };
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

    // Port-only: a frame running ahead finishes, and isn't shown.
    _Thread.Wait();
    TakeBack();
    _Unshown.reset();
    SDL_WaitForGPUIdle(_Device.get());

    for (SDL_GPUFence* const fence : _Sent)
    {
        SDL_ReleaseGPUFence(_Device.get(), fence);
    }

    _Sent.clear();

    if (_Renderer != nullptr && MCRenderer::Hardware() == _Renderer)
    {
        MCRenderer::SetHardware(nullptr, MCGpuDrawing::Off);
    }

    // Whoever still holds the renderer (a test) holds no pointer to this presenter.
    if (_Renderer != nullptr)
    {
        _Renderer->RecordingStarts = nullptr;
        _Renderer->WaitForRun = nullptr;
        _Renderer->Checkpoint = nullptr;
    }

    _Renderer.reset();

    for (Texture& texture : {std::ref(_Screen), std::ref(_Ops), std::ref(_Tables), std::ref(_Palette), std::ref(_Frame),
                             std::ref(_NoLightTiles)})
    {
        Release(texture);
    }

    for (std::vector<Texture>& textures :
         {std::ref(_Underlays), std::ref(_Blooms), std::ref(_Fields), std::ref(_Puddles), std::ref(_LightTiles)})
    {
        for (Texture& texture : textures)
        {
            Release(texture);
        }
    }

    _Upload.reset();
    _LightTileUpload.reset();
    _Nearest.reset();
    _Linear.reset();
    _CompositePipeline.reset();
    _DryPipeline.reset();
    _UnspeckedPipeline.reset();
    _BrightPipeline.reset();
    _FieldPipeline.reset();
    _PuddlePipeline.reset();
    _QuadShader.reset();
    _CompositeShader.reset();
    _DryShader.reset();
    _UnspeckedShader.reset();
    _BrightShader.reset();
    _FieldShader.reset();
    _PuddleShader.reset();

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
    // Port-only: a frame running ahead is shown first, so the swapchain changes with nothing on the render thread.
    if (auto finished = FinishFrames(); !finished)
    {
        _LateError = finished.error();
    }

    _VSync = on;
    // Port-only: the third image with vsync alone: without it, a third image can leave Windows' compositor holding the
    // game's frames after an overlay (_ThirdImage).
    AskFramesInFlight(on);
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

void MCVulkanPresenter::AskFramesInFlight(bool vsync)
{
    const uint32_t frames = _ThirdImage && vsync ? ThirdImageFrames : TwoImageFrames;

    // Each change makes the claimed window's swapchain again: none while the count stands.
    if (frames == _FramesInFlight)
    {
        return;
    }

    // SDL takes the count before it makes the swapchain again, so it stands even when that fails, for the swapchain
    // made next.
    if (!SDL_SetGPUAllowedFramesInFlight(_Device.get(), frames))
    {
        SDL_Log("MCVulkanPresenter: %u swapchain images (from %u) asked, not made yet: %s", frames, _FramesInFlight,
                SDL_GetError());
    }

    _FramesInFlight = frames;
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
    // The screen's surface as the frame sealed last placed it: drawn into, so its run makes its textures.
    return _Renderer != nullptr && MCRenderer::GpuDrawing() == MCGpuDrawing::On &&
           _Renderer->SealedSize(MCVulkanRenderer::SurfaceKey(frame.Screen.get())).has_value();
}

auto MCVulkanPresenter::Prepare(const MCFrame& frame, bool read)
    -> std::expected<MCVulkanRenderer::SealedFrame, std::string>
{
    if (_Renderer == nullptr || MCRenderer::GpuDrawing() == MCGpuDrawing::Off)
    {
        return MCVulkanRenderer::SealedFrame();
    }

    _Renderer->SetColors(frame.PaletteColors, frame.Cycle);

    if (!read)
    {
        PlaceUnderlays(frame);
    }

    return _Renderer->Seal(frame.Underlays);
}

auto MCVulkanPresenter::Plan(const MCFrame& frame, bool read)
    -> std::expected<std::shared_ptr<PlannedFrame>, std::string>
{
    auto planned = std::make_shared<PlannedFrame>();
    planned->Frame = &frame;
    planned->Width = frame.Width;
    planned->Height = frame.Height;
    planned->Layout = ShownLayout{frame.View, frame.ScaledOutput() ? frame.OutputWidth : 0,
                                  frame.ScaledOutput() ? frame.OutputHeight : 0};
    planned->Cycle = frame.Cycle;
    planned->Fade = frame.Fade;
    std::ranges::copy(frame.PaletteColors.first(std::min(frame.PaletteColors.size(), planned->Palette.size())),
                      planned->Palette.begin());
    planned->PaletteChanged = frame.PaletteChanged;
    auto sealed = Prepare(frame, read);

    if (!sealed)
    {
        return std::unexpected(sealed.error());
    }

    planned->Run = std::move(*sealed);
    // Until the GPU has drawn the screen once, the screen's own (blank) pixels are shown.
    planned->GpuSurfaces = ShowsGpuSurfaces(frame);
    planned->ScreenKey = MCVulkanRenderer::SurfaceKey(frame.Screen.get());
    PlanComposite(frame, *planned);
    return planned;
}

void MCVulkanPresenter::PlanComposite(const MCFrame& frame, PlannedFrame& planned)
{
    // Port-only (HD lighting): the bloom, from the brightness the lighting starts it at.
    if (const MCSceneLights::MCLighting lighting = MCSceneLights::ShownLighting();
        GHdLighting != HdLightingOff && lighting.Bloom > 0.0f)
    {
        planned.BloomFrom = lighting.BloomFrom;
    }

    planned.Worlds.resize(frame.Underlays.size());
    _Specks.resize(std::max(_Specks.size(), frame.Underlays.size()));

    for (size_t i = 0; i < frame.Underlays.size(); ++i)
    {
        const MCUnderlay& underlay = frame.Underlays[i];
        PlannedWorld& world = planned.Worlds[i];
        world.Rect = underlay.Rect;
        const std::shared_ptr<const MCWindow> source = underlay.Source.lock();

        if (const MCRect on = OnScreen(underlay.Rect, frame.Width, frame.Height);
            source == nullptr || on.X1 < on.X0 || on.Y1 < on.Y0)
        {
            continue;
        }

        if (planned.GpuSurfaces)
        {
            // The GPU's surface as the frame sealed it: its textures that size once the frame has run.
            world.Key = MCVulkanRenderer::SurfaceKey(*source);
            const std::optional<MCVulkanRenderer::SealedPlace> place = _Renderer->SealedSize(world.Key);

            if (!place.has_value())
            {
                continue;
            }

            world.Size = {place->Width, place->Height};
        }
        else
        {
            world.Size = {static_cast<uint32_t>(source->XMax + 1), static_cast<uint32_t>(source->YMax + 1)};
        }

        world.Shown = true;
        world.Lights = SetLighting(world.Uniforms, _Specks[i], *source, static_cast<float>(world.Size[0]),
                                   static_cast<float>(world.Size[1]));
    }
}

std::expected<void, std::string> MCVulkanPresenter::RunPlanned(SDL_GPUCommandBuffer& commands, PlannedFrame& planned)
{
    // A frame run ahead logs its parts as the render thread's.
    const bool ahead = planned.Frame == nullptr;

    if (MCFrameLog::Scope part(ahead ? PrepareAheadPart : PreparePart); true)
    {
        if (planned.Run != nullptr)
        {
            if (auto ran = _Renderer->Run(commands, *planned.Run); !ran)
            {
                return ran;
            }
        }

        if (auto uploaded = Upload(commands, planned); !uploaded)
        {
            return uploaded;
        }
    }

    if (MCFrameLog::Scope part(ahead ? CompositeAheadPart : CompositePart); true)
    {
        Composite(commands, planned);
    }

    return {};
}

std::expected<void, std::string> MCVulkanPresenter::Upload(SDL_GPUCommandBuffer& commands, const PlannedFrame& planned)
{
    const uint32_t width = static_cast<uint32_t>(planned.Width);
    const uint32_t height = static_cast<uint32_t>(planned.Height);
    const bool scaled = planned.Layout.Scaled();
    const uint32_t frameWidth = scaled ? static_cast<uint32_t>(planned.Layout.OutputWidth) : width;
    const uint32_t frameHeight = scaled ? static_cast<uint32_t>(planned.Layout.OutputHeight) : height;
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
    std::vector<UploadPart> parts;
    // The underlays' pictures, held while their pixels go up.
    std::vector<std::shared_ptr<const MCWindow>> sources;

    if (!planned.GpuSurfaces && planned.Frame == nullptr)
    {
        return std::unexpected("MCVulkanPresenter: a frame run ahead without the GPU's surfaces (its pixels are gone)");
    }

    if (!planned.GpuSurfaces)
    {
        // The software renderer's frame, read where it lies: the game's thread.
        const MCFrame& frame = *planned.Frame;

        for (const MCUnderlay& underlay : frame.Underlays)
        {
            sources.push_back(underlay.Source.lock());
        }

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

        if (!frame.Underlays.empty())
        {
            parts.push_back({std::as_bytes(frame.Ops), *_Ops.Handle, width, height, 1, 0});
            parts.push_back({std::as_bytes(MCRenderer::OpTables()), *_Tables.Handle, 256,
                             static_cast<uint32_t>(MCRenderer::OpTableCount()), 1, 0});
        }

        for (size_t i = 0; i < frame.Underlays.size(); ++i)
        {
            const MCWindow& source = *sources[i];
            const auto sourceWidth = static_cast<uint32_t>(source.XMax + 1);
            const auto sourceHeight = static_cast<uint32_t>(source.YMax + 1);
            parts.push_back({std::as_bytes(source.Bytes()), *_Underlays[i].Handle, sourceWidth, sourceHeight, 1, 0});
        }
    }

    if (planned.PaletteChanged || paletteNew)
    {
        // SDL_Color is r, g, b, a bytes: RGBA's layout. The alpha isn't used. The colour cycle is the shader's.
        parts.push_back({std::as_bytes(std::span(planned.Palette)), *_Palette.Handle, 256, 1, 4, 0});
    }

    return UploadParts(commands, parts, _Upload, _UploadSize);
}

std::expected<void, std::string> MCVulkanPresenter::UploadParts(SDL_GPUCommandBuffer& commands,
                                                                std::span<UploadPart> parts,
                                                                MCGpuHandle<SDL_GPUTransferBuffer>& buffer,
                                                                uint32_t& bufferSize)
{
    if (parts.empty())
    {
        return {};
    }

    uint32_t total = 0;

    for (UploadPart& part : parts)
    {
        part.Offset = AlignUpload(total);
        total = part.Offset + part.Width * part.Height * part.BytesPerPixel;
    }

    if (buffer == nullptr || bufferSize < total)
    {
        buffer.reset();
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = total + total / 4;
        buffer = MCGpuHandle<SDL_GPUTransferBuffer>(SDL_CreateGPUTransferBuffer(_Device.get(), &info),
                                                    MCGpuRelease{_Device});
        bufferSize = buffer != nullptr ? info.size : 0;

        if (buffer == nullptr)
        {
            return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer"));
        }
    }

    void* const mapped = SDL_MapGPUTransferBuffer(_Device.get(), buffer.get(), true);

    if (mapped == nullptr)
    {
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer"));
    }

    for (const UploadPart& part : parts)
    {
        std::memcpy(static_cast<uint8_t*>(mapped) + part.Offset, part.Data.data(),
                    static_cast<size_t>(part.Width) * part.Height * part.BytesPerPixel);
    }

    SDL_UnmapGPUTransferBuffer(_Device.get(), buffer.get());
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(&commands);

    for (const UploadPart& part : parts)
    {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = buffer.get();
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

void MCVulkanPresenter::Composite(SDL_GPUCommandBuffer& commands, PlannedFrame& planned)
{
    // The target: the screen's size, or the window pixels a scaled-up view covers. Screen pixels per target pixel, and
    // the screen pixel at the target's corner.
    const ShownLayout& layout = planned.Layout;
    const bool scaled = layout.Scaled();
    const bool gpuSurfaces = planned.GpuSurfaces;
    const float width = static_cast<float>(scaled ? layout.OutputWidth : planned.Width);
    const float height = static_cast<float>(scaled ? layout.OutputHeight : planned.Height);
    const float mapX = scaled ? static_cast<float>(layout.View.w) / width : 1.0f;
    const float mapY = scaled ? static_cast<float>(layout.View.h) / height : 1.0f;
    const float originX = scaled ? static_cast<float>(layout.View.x) : 0.0f;
    const float originY = scaled ? static_cast<float>(layout.View.y) : 0.0f;
    const float screenMap[4] = {originX, originY, mapX, mapY};

    // Port-only (HD lighting): the light tiles bound where there are none (the bloom's pass, the screen's), filled with
    // none when made, in the light tiles' copy pass.
    const bool noLightTilesNew = _NoLightTiles.Handle == nullptr;

    if (auto made = Ensure(_NoLightTiles, LightTileFormat, SDL_GPU_TEXTUREUSAGE_SAMPLER, LightTileTexelsWide, 1, true);
        !made)
    {
        SDL_Log("MCVulkanPresenter: the frame isn't composited: %s", made.error().c_str());
        return;
    }

    // The screen's texture and its allocated size; the GPU's surfaces have no op plane or op tables (the screen is
    // bound in their place, unread).
    SDL_GPUTexture* screen = _Screen.Handle.get();
    uint32_t screenWidth = static_cast<uint32_t>(planned.Width);
    uint32_t screenHeight = static_cast<uint32_t>(planned.Height);
    // The GPU's surfaces drawn from, held while the composite is recorded. Found by their keys: the run's side of the
    // renderer, as the frame's run left it.
    std::vector<std::shared_ptr<SDL_GPUTexture>> held;

    if (gpuSurfaces)
    {
        screen = held.emplace_back(_Renderer->SurfaceTexture(planned.ScreenKey, screenWidth, screenHeight)).get();

        if (screen == nullptr)
        {
            SDL_Log("MCVulkanPresenter: the frame isn't composited: the screen's surface wasn't made");
            return;
        }
    }

    SDL_GPUTexture* ops = gpuSurfaces ? screen : _Ops.Handle.get();
    SDL_GPUTexture* tables = gpuSurfaces ? screen : _Tables.Handle.get();
    const float source = gpuSurfaces ? 1.0f : 0.0f;
    // The screen's drawn pixels per screen pixel: above 1 for a sharp screen (MCFrame::SharpScreen), drawn at the size
    // it's shown.
    const auto [drawnX, drawnY] =
        gpuSurfaces ? _Renderer->SurfaceScale(planned.ScreenKey) : std::pair<float, float>{1.0f, 1.0f};
    const float screenSize[4] = {static_cast<float>(planned.Width), static_cast<float>(planned.Height),
                                 static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    // Each underlay's world: its texture, its size in use and allocated (the GPU's surface is exactly its size, as its
    // draws saw it), and its bloom (made before the composite's pass: none, null, without one).
    struct World
    {
        SDL_GPUTexture* Texture = nullptr;
        std::array<float, 4> Size{};
        SDL_GPUTexture* Bloom = nullptr;
        /// <summary>The GPU world's palette indices (the living water's), else the op plane as the screen's draw binds it.</summary>
        SDL_GPUTexture* Indices = nullptr;
        /// <summary>Port-only (HD weather): the light the GPU world's raindrops caught, else the screen
        /// (unread).</summary>
        SDL_GPUTexture* Glints = nullptr;
    };

    std::vector<World> worlds(planned.Worlds.size());

    for (size_t i = 0; i < planned.Worlds.size(); ++i)
    {
        const PlannedWorld& plannedWorld = planned.Worlds[i];
        World& world = worlds[i];
        world.Indices = ops;
        world.Glints = screen;

        if (!plannedWorld.Shown)
        {
            continue;
        }

        if (!gpuSurfaces)
        {
            world.Texture = _Underlays[i].Handle.get();
            world.Size = {static_cast<float>(plannedWorld.Size[0]), static_cast<float>(plannedWorld.Size[1]),
                          static_cast<float>(_Underlays[i].Width), static_cast<float>(_Underlays[i].Height)};
            continue;
        }

        uint32_t worldWidth = 0;
        uint32_t worldHeight = 0;
        std::shared_ptr<SDL_GPUTexture> texture = _Renderer->SurfaceTexture(plannedWorld.Key, worldWidth, worldHeight);

        // Its surface as the frame drew it, the size its lighting was planned for.
        if (texture == nullptr || worldWidth != plannedWorld.Size[0] || worldHeight != plannedWorld.Size[1])
        {
            continue;
        }

        world.Texture = held.emplace_back(std::move(texture)).get();
        world.Size = {static_cast<float>(worldWidth), static_cast<float>(worldHeight), static_cast<float>(worldWidth),
                      static_cast<float>(worldHeight)};
        world.Bloom = planned.BloomFrom.has_value()
                          ? Bloom(commands, i, *world.Texture, worldWidth, worldHeight, *planned.BloomFrom)
                          : nullptr;

        if (SDL_GPUTexture* const indices = held.emplace_back(_Renderer->SurfaceIndexTexture(plannedWorld.Key)).get();
            indices != nullptr)
        {
            world.Indices = indices;
        }

        if (SDL_GPUTexture* const glints = held.emplace_back(_Renderer->SurfaceGlintTexture(plannedWorld.Key)).get();
            glints != nullptr)
        {
            world.Glints = glints;
        }
    }

    // Each underlay's rectangle on the target and its uniforms, with its lights and their tiles (null: none, every
    // light tried everywhere), which go up before the pass that reads them.
    struct UnderlayDraw
    {
        std::array<float, 4> Rect{};
        CompositeUniforms Uniforms{};
        LightTiles Lights;
        SDL_GPUTexture* LightTileTexture = nullptr;
        /// <summary>Port-only (HD lighting): the GPU world's field (null: none, without a bloom or clouds).</summary>
        SDL_GPUTexture* Field = nullptr;
        /// <summary>Port-only (HD weather): the GPU world's puddles' field (null: none, without puddles).</summary>
        SDL_GPUTexture* Puddles = nullptr;
    };

    std::vector<std::optional<UnderlayDraw>> underlayDraws(planned.Worlds.size());

    for (size_t i = 0; i < planned.Worlds.size(); ++i)
    {
        const PlannedWorld& plannedWorld = planned.Worlds[i];
        const World& world = worlds[i];

        if (world.Texture == nullptr)
        {
            continue;
        }

        const MCRect& rect = plannedWorld.Rect;
        const MCRect on = OnScreen(rect, planned.Width, planned.Height);
        UnderlayDraw& drawn = underlayDraws[i].emplace();
        drawn.Rect = {(static_cast<float>(on.X0) - originX) / mapX, (static_cast<float>(on.Y0) - originY) / mapY,
                      static_cast<float>(on.X1 - on.X0 + 1) / mapX, static_cast<float>(on.Y1 - on.Y0 + 1) / mapY};

        // The rectangle and its part on the screen, in target pixels, over the lighting planned.
        CompositeUniforms& uniforms = drawn.Uniforms;
        uniforms = plannedWorld.Uniforms;
        std::ranges::copy(screenSize, uniforms.ScreenSize);
        std::ranges::copy(screenMap, uniforms.ScreenMap);
        std::ranges::copy(std::array{(static_cast<float>(rect.X0) - originX) / mapX,
                                     (static_cast<float>(rect.Y0) - originY) / mapY,
                                     static_cast<float>(rect.X1 - rect.X0 + 1) / mapX,
                                     static_cast<float>(rect.Y1 - rect.Y0 + 1) / mapY},
                          uniforms.Shown);
        std::ranges::copy(world.Size, uniforms.WorldSize);
        std::ranges::copy(std::array{1.0f, source, drawnX, drawnY}, uniforms.Mode);
        drawn.Lights = TileLights(uniforms, plannedWorld.Lights, world.Size[0], world.Size[1]);

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
    }

    // Port-only (HD lighting): the light tiles go up, each underlay's into its texture. One that can't shows its world
    // with every light tried everywhere, as the same light.
    static constexpr LightTile NoLights{};
    std::vector<UploadPart> tileParts;
    _LightTiles.resize(std::max(_LightTiles.size(), underlayDraws.size()));

    if (noLightTilesNew)
    {
        tileParts.push_back({std::as_bytes(std::span(&NoLights, 1)), *_NoLightTiles.Handle, LightTileTexelsWide, 1,
                             sizeof(uint32_t), 0});
    }

    for (size_t i = 0; i < underlayDraws.size(); ++i)
    {
        if (!underlayDraws[i].has_value() || underlayDraws[i]->Lights.Tiles.empty())
        {
            continue;
        }

        UnderlayDraw& drawn = *underlayDraws[i];

        if (auto made = Ensure(_LightTiles[i], LightTileFormat, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                               drawn.Lights.Across * LightTileTexelsWide, drawn.Lights.Down, false);
            !made)
        {
            SDL_Log("MCVulkanPresenter: underlay %zu's lights shown without their tiles: %s", i, made.error().c_str());
            drawn.Uniforms.LightTiles[0] = 0.0f;
            continue;
        }

        drawn.LightTileTexture = _LightTiles[i].Handle.get();
        tileParts.push_back({std::as_bytes(std::span(drawn.Lights.Tiles)), *drawn.LightTileTexture,
                             drawn.Lights.Across * LightTileTexelsWide, drawn.Lights.Down, sizeof(uint32_t), 0});
    }

    if (auto uploaded = UploadParts(commands, tileParts, _LightTileUpload, _LightTileUploadSize); !uploaded)
    {
        SDL_Log("MCVulkanPresenter: the lights shown without their tiles: %s", uploaded.error().c_str());

        for (std::optional<UnderlayDraw>& drawn : underlayDraws)
        {
            if (drawn.has_value())
            {
                drawn->Uniforms.LightTiles[0] = 0.0f;
                drawn->LightTileTexture = nullptr;
            }
        }
    }

    // Port-only (HD lighting): each GPU world's field (composite.pshader's Field), its bloom and its clouds' cover at
    // its bloom's texels, which the composite's pass reads. One that can't be made shows its world without either.
    _Fields.resize(std::max(_Fields.size(), underlayDraws.size()));

    for (size_t i = 0; i < underlayDraws.size(); ++i)
    {
        if (!gpuSurfaces || !underlayDraws[i].has_value())
        {
            continue;
        }

        UnderlayDraw& drawn = *underlayDraws[i];
        CompositeUniforms& uniforms = drawn.Uniforms;

        if (uniforms.LightLook[1] <= 0.0f && uniforms.Clouds[0] <= 0.0f)
        {
            continue;
        }

        const World& world = worlds[i];
        Texture& field = _Fields[i];
        const uint32_t fieldWidth = (static_cast<uint32_t>(world.Size[0]) + BloomScale - 1) / BloomScale;
        const uint32_t fieldHeight = (static_cast<uint32_t>(world.Size[1]) + BloomScale - 1) / BloomScale;

        if (auto made = Ensure(field, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                               SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, fieldWidth,
                               fieldHeight, false);
            !made)
        {
            SDL_Log("MCVulkanPresenter: underlay %zu shown without its bloom and clouds' shadows: %s", i,
                    made.error().c_str());
            uniforms.LightLook[1] = 0.0f;
            uniforms.Clouds[0] = 0.0f;
            continue;
        }

        SDL_GPUColorTargetInfo fieldTarget{};
        fieldTarget.texture = field.Handle.get();
        // Only the texels the composite reads are drawn (FieldAt).
        fieldTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        fieldTarget.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* fieldPass = SDL_BeginGPURenderPass(&commands, &fieldTarget, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(fieldPass, _FieldPipeline.get());
        const float allocatedWidth = static_cast<float>(field.Width);
        const float allocatedHeight = static_cast<float>(field.Height);
        const SDL_GPUViewport fieldViewport{0.0f, 0.0f, allocatedWidth, allocatedHeight, 0.0f, 1.0f};
        SDL_SetGPUViewport(fieldPass, &fieldViewport);
        // The pass reads the bloom's mips alone (the world bound for the rest, and for mips it hasn't, unread).
        SDL_GPUTexture* const mips = world.Bloom != nullptr ? world.Bloom : world.Texture;
        const CompositeBindings bindings{{
            {world.Texture, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {mips, _Linear.get()},
            {world.Texture, _Nearest.get()},
            {_NoLightTiles.Handle.get(), _Nearest.get()},
            {_NoLightTiles.Handle.get(), _Nearest.get()},
        }};
        // The texels in use and the next across and down, which the world's last pixels read toward.
        const uint32_t drawnWidth = std::min(fieldWidth + 1, field.Width);
        const uint32_t drawnHeight = std::min(fieldHeight + 1, field.Height);
        DrawComposite(commands, *fieldPass, bindings,
                      {{0.0f, 0.0f, static_cast<float>(drawnWidth), static_cast<float>(drawnHeight)},
                       {allocatedWidth, allocatedHeight, 0.0f, 0.0f}},
                      uniforms);
        SDL_EndGPURenderPass(fieldPass);
        drawn.Field = field.Handle.get();
        std::ranges::copy(std::array{static_cast<float>(fieldWidth), static_cast<float>(fieldHeight), allocatedWidth,
                                     allocatedHeight},
                          uniforms.BloomSize);
    }

    // Port-only (HD weather): each GPU world's puddles' field (composite.pshader's PuddleCell), PuddleBorder texels past
    // it each way, which the composite's wet look reads. One that can't be made shows its world without puddles.
    _Puddles.resize(std::max(_Puddles.size(), underlayDraws.size()));

    for (size_t i = 0; i < underlayDraws.size(); ++i)
    {
        if (!gpuSurfaces || !underlayDraws[i].has_value())
        {
            continue;
        }

        UnderlayDraw& drawn = *underlayDraws[i];
        CompositeUniforms& uniforms = drawn.Uniforms;

        if (uniforms.WetLook[0] <= 0.0f || uniforms.WetLook[1] <= 0.0f)
        {
            continue;
        }

        const World& world = worlds[i];
        Texture& puddles = _Puddles[i];
        const uint32_t puddlesWidth = static_cast<uint32_t>(world.Size[0]) + 2 * PuddleBorder;
        const uint32_t puddlesHeight = static_cast<uint32_t>(world.Size[1]) + 2 * PuddleBorder;

        if (auto made = Ensure(puddles, PuddleFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                               puddlesWidth, puddlesHeight, false);
            !made)
        {
            SDL_Log("MCVulkanPresenter: underlay %zu shown without its puddles: %s", i, made.error().c_str());
            uniforms.WetLook[1] = 0.0f;
            continue;
        }

        SDL_GPUColorTargetInfo puddleTarget{};
        puddleTarget.texture = puddles.Handle.get();
        // Only the texels the composite reads are drawn (PuddleShown).
        puddleTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        puddleTarget.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* puddlePass = SDL_BeginGPURenderPass(&commands, &puddleTarget, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(puddlePass, _PuddlePipeline.get());
        const float allocatedWidth = static_cast<float>(puddles.Width);
        const float allocatedHeight = static_cast<float>(puddles.Height);
        const SDL_GPUViewport puddleViewport{0.0f, 0.0f, allocatedWidth, allocatedHeight, 0.0f, 1.0f};
        SDL_SetGPUViewport(puddlePass, &puddleViewport);
        // The pass reads the world and its indices alone (the world bound for the rest, unread).
        const CompositeBindings bindings{{
            {world.Texture, _Nearest.get()},
            {world.Indices, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {world.Texture, _Nearest.get()},
            {world.Texture, _Linear.get()},
            {world.Texture, _Nearest.get()},
            {_NoLightTiles.Handle.get(), _Nearest.get()},
            {_NoLightTiles.Handle.get(), _Nearest.get()},
        }};
        DrawComposite(commands, *puddlePass, bindings,
                      {{0.0f, 0.0f, static_cast<float>(puddlesWidth), static_cast<float>(puddlesHeight)},
                       {allocatedWidth, allocatedHeight, 0.0f, 0.0f}},
                      uniforms);
        SDL_EndGPURenderPass(puddlePass);
        drawn.Puddles = puddles.Handle.get();
    }

    SDL_GPUColorTargetInfo target{};
    target.texture = _Frame.Handle.get();
    // The first quad covers every pixel.
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(&commands, &target, 1, nullptr);
    const SDL_GPUViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_GPUGraphicsPipeline* bound = nullptr;

    auto draw = [&](const std::array<float, 4>& rect, SDL_GPUTexture& world, SDL_GPUTexture& bloom,
                    SDL_GPUTexture& indices, SDL_GPUTexture& glints, SDL_GPUTexture& lightTiles,
                    SDL_GPUTexture& puddles, CompositeUniforms uniforms)
    {
        uniforms.Cycle[0] = planned.Cycle.First;
        uniforms.Cycle[1] = planned.Cycle.Step;

        for (size_t i = 0; i < 8; ++i)
        {
            uniforms.CycleSources[i] = planned.Cycle.Sources[i];
        }

        uniforms.Fade[0] = static_cast<float>(planned.Fade) / 255.0f;

        const CompositeBindings bindings{{
            {screen, _Nearest.get()},
            {&indices, _Nearest.get()},
            {&world, _Nearest.get()},
            {tables, _Nearest.get()},
            {_Palette.Handle.get(), _Nearest.get()},
            {&bloom, _Linear.get()},
            {&glints, _Nearest.get()},
            {&lightTiles, _Nearest.get()},
            {&puddles, _Nearest.get()},
        }};

        // Port-only (HD weather): without the rain's wet look, the weather's veil (the rain's or the falling snow's),
        // the snow's cover and its specks where none shows, and without the specks where only they don't
        // (composite.pshader's WetAt, VeiledAt, SnowAt and SpecksAt leave the colour as it is then).
        const bool specked = std::ranges::any_of(uniforms.Specks, [](float specks) { return specks > 0.0f; });
        const bool weathered =
            specked || uniforms.WetLook[0] > 0.0f || uniforms.Weather[3] > 0.0f || uniforms.Snow[2] > 0.0f;
        SDL_GPUGraphicsPipeline* const pipeline = specked     ? _CompositePipeline.get()
                                                  : weathered ? _UnspeckedPipeline.get()
                                                              : _DryPipeline.get();

        if (pipeline != bound)
        {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound = pipeline;
        }

        DrawComposite(commands, *pass, bindings, {{rect[0], rect[1], rect[2], rect[3]}, {width, height, 0.0f, 0.0f}},
                      uniforms);
    };

    // The screen alone, then each underlay's rectangle with its surface (the screen bound for a field it hasn't, and no
    // light tiles' texture for puddles it hasn't).
    draw({0.0f, 0.0f, width, height}, *screen, *screen, *ops, *screen, *_NoLightTiles.Handle, *_NoLightTiles.Handle,
         CompositeUniforms{{screenSize[0], screenSize[1], screenSize[2], screenSize[3]},
                           {screenMap[0], screenMap[1], screenMap[2], screenMap[3]},
                           {0.0f, 0.0f, 1.0f, 1.0f},
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, source, drawnX, drawnY}});

    for (size_t i = 0; i < underlayDraws.size(); ++i)
    {
        if (!underlayDraws[i].has_value())
        {
            continue;
        }

        const UnderlayDraw& drawn = *underlayDraws[i];
        const World& world = worlds[i];
        draw(drawn.Rect, *world.Texture, drawn.Field != nullptr ? *drawn.Field : *screen, *world.Indices, *world.Glints,
             drawn.LightTileTexture != nullptr ? *drawn.LightTileTexture : *_NoLightTiles.Handle,
             drawn.Puddles != nullptr ? *drawn.Puddles : *_NoLightTiles.Handle, drawn.Uniforms);
    }

    SDL_EndGPURenderPass(pass);
}

SDL_GPUTexture* MCVulkanPresenter::Bloom(SDL_GPUCommandBuffer& commands, size_t index, SDL_GPUTexture& world,
                                         uint32_t width, uint32_t height, float from)
{
    // A texel for BloomScale x BloomScale of the world's pixels, which must hold the bloom's levels.
    const uint32_t bloomWidth = (width + BloomScale - 1) / BloomScale;
    const uint32_t bloomHeight = (height + BloomScale - 1) / BloomScale;

    if (std::max(bloomWidth, bloomHeight) < (1u << (BloomLevels - 1)))
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
    SDL_BindGPUGraphicsPipeline(pass, _BrightPipeline.get());
    const float allocatedWidth = static_cast<float>(bloom.Width);
    const float allocatedHeight = static_cast<float>(bloom.Height);
    const SDL_GPUViewport viewport{0.0f, 0.0f, allocatedWidth, allocatedHeight, 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);
    CompositeUniforms uniforms{};
    const float worldWidth = static_cast<float>(width);
    const float worldHeight = static_cast<float>(height);
    std::ranges::copy(std::array{worldWidth, worldHeight, worldWidth, worldHeight}, uniforms.WorldSize);
    uniforms.LightLook[2] = from;
    // The pass reads the world alone: it is bound for every texture.
    const CompositeBindings bindings{{
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Nearest.get()},
        {&world, _Linear.get()},
        {&world, _Nearest.get()},
        {_NoLightTiles.Handle.get(), _Nearest.get()},
        {_NoLightTiles.Handle.get(), _Nearest.get()},
    }};

    DrawComposite(commands, *pass, bindings,
                  {{0.0f, 0.0f, static_cast<float>(bloomWidth), static_cast<float>(bloomHeight)},
                   {allocatedWidth, allocatedHeight, 0.0f, 0.0f}},
                  uniforms);
    SDL_EndGPURenderPass(pass);
    SDL_GenerateMipmapsForGPUTexture(&commands, bloom.Handle.get());
    return bloom.Handle.get();
}

std::expected<void, std::string> MCVulkanPresenter::HoldFramesOnGpu()
{
    // With three images SDL lets three frames be on the GPU (ThirdImageFrames): the game waits as two let it, so what
    // it shows is no older than with two. With two (vsync off) SDL's own wait as it takes the image is for the same
    // frame, which it then finds done.
    while (_Sent.size() >= FramesHeldOnGpu)
    {
        SDL_GPUFence* const oldest = _Sent.front();
        const bool waited = SDL_WaitForGPUFences(_Device.get(), true, &oldest, 1);
        SDL_ReleaseGPUFence(_Device.get(), oldest);
        _Sent.erase(_Sent.begin());

        if (!waited)
        {
            return std::unexpected(SdlError("SDL_WaitForGPUFences (the frame before last's)"));
        }
    }

    return {};
}

std::expected<void, std::string> MCVulkanPresenter::Present(const MCFrame& frame)
{
    // Port-only: the frame run ahead before this one is shown first (frames reach the screen in order, and one runs at a
    // time); its errors, and those of the frames shown since the last present, are this one's.
    std::string errors;

    if (auto finished = FinishFrames(); !finished)
    {
        errors = std::move(finished.error());
    }

    auto plan = Plan(frame, false);

    if (!plan)
    {
        return std::unexpected(Joined(std::move(errors), plan.error()));
    }

    PlannedFrame& planned = **plan;

    // Port-only: ahead, on the render thread while the game makes the next frame, and shown at the game's next check.
    // The GPU's surfaces only: the software renderer's frame lies in the display's memory, which the next frame redraws.
    if (frame.Ahead && planned.GpuSurfaces)
    {
        planned.Frame = nullptr;
        _Unshown = planned.Layout;
        _Running = std::move(*plan);
        _Thread.Post([this, running = _Running] { RunAhead(*running); });
        return Outcome(std::move(errors));
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        Recycle(planned);
        return std::unexpected(Joined(std::move(errors), SdlError("SDL_AcquireGPUCommandBuffer")));
    }

    auto ran = RunPlanned(*commands, planned);
    Recycle(planned);

    if (!ran)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected(Joined(std::move(errors), ran.error()));
    }

    if (auto shown = ShowFrame(*commands, planned.Layout); !shown)
    {
        return std::unexpected(Joined(std::move(errors), shown.error()));
    }

    if (_Renderer != nullptr && MCRenderer::GpuDrawing() == MCGpuDrawing::Mirror)
    {
        if (auto compared = _Renderer->Compare(frame.Underlays, frame.Colors); !compared)
        {
            return std::unexpected(Joined(std::move(errors), compared.error()));
        }
    }

    return Outcome(std::move(errors));
}

std::expected<void, std::string> MCVulkanPresenter::ShowFrame(SDL_GPUCommandBuffer& commands, const ShownLayout& layout)
{
    SDL_GPUTexture* swapchain = nullptr;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;

    if (MCFrameLog::Scope part("present.acquire"); true)
    {
        // How long the image (and the frames held on the GPU) kept the frame waiting.
        const uint64_t waitStart = SDL_GetPerformanceCounter();
        const struct Waited
        {
            float& Ms;
            uint64_t Start;

            ~Waited()
            {
                Ms = static_cast<float>(static_cast<double>(SDL_GetPerformanceCounter() - Start) * 1000.0 /
                                        static_cast<double>(SDL_GetPerformanceFrequency()));
            }
        } waited{_LastImageWaitMs, waitStart};

        if (auto held = HoldFramesOnGpu(); !held)
        {
            SDL_SubmitGPUCommandBuffer(&commands);
            return held;
        }

        if (!SDL_WaitAndAcquireGPUSwapchainTexture(&commands, _Window.get(), &swapchain, &swapchainWidth,
                                                   &swapchainHeight))
        {
            SDL_SubmitGPUCommandBuffer(&commands);
            return std::unexpected(SdlError("SDL_WaitAndAcquireGPUSwapchainTexture"));
        }
    }

    // No swapchain image (a minimised or hidden window): the frame is composited but not shown.
    if (swapchain != nullptr)
    {
        const MCViewport shown = MCPresentViewport(static_cast<int>(swapchainWidth), static_cast<int>(swapchainHeight),
                                                   layout.View.w, layout.View.h, _Presentation);
        SDL_GPUBlitInfo blit{};
        blit.source.texture = _Frame.Handle.get();

        // A frame composited at the output's size is shown whole.
        if (layout.Scaled())
        {
            blit.source.w = static_cast<uint32_t>(layout.OutputWidth);
            blit.source.h = static_cast<uint32_t>(layout.OutputHeight);
        }
        else
        {
            blit.source.x = static_cast<uint32_t>(layout.View.x);
            blit.source.y = static_cast<uint32_t>(layout.View.y);
            blit.source.w = static_cast<uint32_t>(layout.View.w);
            blit.source.h = static_cast<uint32_t>(layout.View.h);
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
            SDL_BlitGPUTexture(&commands, &blit);
        }
    }

    if (MCFrameLog::Scope part("present.submit"); _ThirdImage)
    {
        // Its fence, for the frame after next to wait on (HoldFramesOnGpu): the work submitted before it is done too.
        SDL_GPUFence* const fence = SDL_SubmitGPUCommandBufferAndAcquireFence(&commands);

        if (fence == nullptr)
        {
            return std::unexpected(SdlError("SDL_SubmitGPUCommandBufferAndAcquireFence"));
        }

        _Sent.push_back(fence);
    }
    else if (!SDL_SubmitGPUCommandBuffer(&commands))
    {
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBuffer"));
    }

    return {};
}

void MCVulkanPresenter::RunAhead(PlannedFrame& planned)
{
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        planned.Error = SdlError("SDL_AcquireGPUCommandBuffer (the render thread's)");
        return;
    }

    if (auto ran = RunPlanned(*commands, planned); !ran)
    {
        SDL_CancelGPUCommandBuffer(commands);
        planned.Error = ran.error();
        return;
    }

    MCFrameLog::Scope part("render.submit");

    if (!SDL_SubmitGPUCommandBuffer(commands))
    {
        planned.Error = SdlError("SDL_SubmitGPUCommandBuffer (the render thread's)");
    }
}

void MCVulkanPresenter::Recycle(PlannedFrame& planned)
{
    if (planned.Run != nullptr)
    {
        _Renderer->Recycle(std::move(planned.Run));
    }
}

std::expected<void, std::string> MCVulkanPresenter::Show()
{
    if (!_Unshown.has_value())
    {
        return {};
    }

    const ShownLayout layout = *_Unshown;
    _Unshown.reset();
    MCFrameLog::Scope part("present.show");
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer (showing a frame run ahead)"));
    }

    auto shown = ShowFrame(*commands, layout);
    _ImageWaitMs += (_LastImageWaitMs - _ImageWaitMs) * ImageWaitSmoothing;
    return shown;
}

void MCVulkanPresenter::TakeBack()
{
    if (_Running == nullptr)
    {
        return;
    }

    const std::shared_ptr<PlannedFrame> running = std::move(_Running);
    Recycle(*running);

    // A frame whose run failed isn't shown: what it composited may be half done.
    if (!running->Error.empty())
    {
        _Unshown.reset();
        KeepLate(std::format("a frame run ahead failed: {}", running->Error));
    }
}

void MCVulkanPresenter::KeepLate(std::string_view error)
{
    SDL_Log("MCVulkanPresenter: %.*s", static_cast<int>(error.size()), error.data());
    _LateError = Joined(std::move(_LateError), error);
}

std::expected<void, std::string> MCVulkanPresenter::FinishFrames()
{
    if (_Running != nullptr)
    {
        MCFrameLog::Scope part("present.wait");
        _Thread.Wait();
        TakeBack();
    }

    if (auto shown = Show(); !shown)
    {
        KeepLate(std::format("a frame run ahead wasn't shown: {}", shown.error()));
    }

    return Outcome(std::exchange(_LateError, std::string()));
}

void MCVulkanPresenter::ShowFinished()
{
    if (_Running != nullptr)
    {
        if (!_Thread.Idle())
        {
            return;
        }

        TakeBack();
    }

    if (auto shown = Show(); !shown)
    {
        KeepLate(std::format("a frame run ahead wasn't shown: {}", shown.error()));
    }
}

void MCVulkanPresenter::ShowBeforeInput()
{
    // Paced by the display (the frames run ahead wait for their swapchain image once drawn), the frame run ahead is
    // shown before the next frame's input is read, its drawing waited for: the wait for the image, which takes in the
    // wait for the drawing, then comes before the input, as when frames were shown at present, rather than after it, a
    // frame's more lag. Else (the game's thread holds the pace) once it's drawn, the next frame's work going on
    // meanwhile.
    if (_ImageWaitMs > PacedImageWaitMs)
    {
        ShowPresented();
        return;
    }

    ShowFinished();
}

void MCVulkanPresenter::ShowPresented()
{
    // The errors come with the next present, as a check's do.
    if (auto finished = FinishFrames(); !finished)
    {
        _LateError = std::move(finished.error());
    }
}

std::expected<std::vector<SDL_Color>, std::string> MCVulkanPresenter::ReadFrame(const MCFrame& frame)
{
    // Port-only: nothing running or waiting to be shown as the frame is read (their errors come with the next present).
    if (auto finished = FinishFrames(); !finished)
    {
        _LateError = std::move(finished.error());
    }

    auto plan = Plan(frame, true);

    if (!plan)
    {
        return std::unexpected(plan.error());
    }

    PlannedFrame& planned = **plan;
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(_Device.get());

    if (commands == nullptr)
    {
        Recycle(planned);
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));
    }

    auto ran = RunPlanned(*commands, planned);
    Recycle(planned);

    if (!ran)
    {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected(ran.error());
    }

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
    // Port-only: nothing running or waiting to be shown as the screen is read (their errors come with the next
    // present).
    if (auto finished = FinishFrames(); !finished)
    {
        _LateError = std::move(finished.error());
    }

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
    // Port-only: the frame run ahead before this one goes first (to no swapchain image, as the window's minimised).
    std::string errors;

    if (auto finished = FinishFrames(); !finished)
    {
        errors = std::move(finished.error());
    }

    if (_Renderer == nullptr || MCRenderer::GpuDrawing() == MCGpuDrawing::Off)
    {
        return Outcome(std::move(errors));
    }

    _Renderer->SetColors(frame.PaletteColors, frame.Cycle);
    PlaceUnderlays(frame);

    if (auto flushed = _Renderer->Flush(frame.Underlays); !flushed)
    {
        return std::unexpected(Joined(std::move(errors), flushed.error()));
    }

    return Outcome(std::move(errors));
}

MCVulkanPresenter::RenderThread::RenderThread() : _Thread([this](std::stop_token stop) { Loop(std::move(stop)); })
{
}

MCVulkanPresenter::RenderThread::~RenderThread() = default;

void MCVulkanPresenter::RenderThread::Post(std::function<void()> job)
{
    {
        std::unique_lock lock(_Lock);
        // One job at a time: the last one returns first.
        _Returned.wait(lock, [this] { return !_Busy; });
        _Job = std::move(job);
        _Busy = true;
    }

    _Posted.notify_one();
}

bool MCVulkanPresenter::RenderThread::Idle() const
{
    const std::scoped_lock lock(_Lock);
    return !_Busy;
}

void MCVulkanPresenter::RenderThread::Wait()
{
    // On the thread itself, what it calls runs in its own order: nothing to wait for.
    if (std::this_thread::get_id() == _Thread.get_id())
    {
        return;
    }

    std::unique_lock lock(_Lock);
    _Returned.wait(lock, [this] { return !_Busy; });
}

void MCVulkanPresenter::RenderThread::Loop(std::stop_token stop)
{
    std::unique_lock lock(_Lock);

    while (_Posted.wait(lock, stop, [this] { return _Job != nullptr; }))
    {
        std::function<void()> job = std::move(_Job);
        _Job = nullptr;
        lock.unlock();
        job();
        // What the job holds goes before the caller sees it done.
        job = nullptr;
        lock.lock();
        _Busy = false;
        _Returned.notify_all();
    }
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
            _Renderer->SetScreenScale(frame.Screen.get(), scaleX, scaleY, frame.View.x + frame.View.w,
                                      frame.View.y + frame.View.h);
            return;
        }
    }
    else
    {
        _Renderer->SetUnderlayScale(1.0f, 1.0f);
    }

    _Renderer->SetScreenScale(std::nullopt, 1.0f, 1.0f, 0, 0);
}

MCViewport MCVulkanPresenter::Viewport(int viewWidth, int viewHeight) const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(_Window.get(), &width, &height);
    return MCPresentViewport(width, height, viewWidth, viewHeight, _Presentation);
}
