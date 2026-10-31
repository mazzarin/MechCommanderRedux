#include "stdafx.h"
#include "sprite/MCSceneLights.h"
#include "appear/MCAppearance.h"
#include "camera/MCCameraList.h"
#include "color/MCPalette.h"
#include "engine/MCElement.h"
#include "engine/MCElementBuffer.h"
#include "lib/MCVector3D.h"
#include "main/MCMissionGlobals.h"
#include "object/MCObjectDrawing.h"
#include "platform/MCRenderer.h"
#include "sprite/MCShape.h"
#include "sprite/MCVfxAppearance.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxFunctions.h"

int GHdLighting = 0;

namespace
{
    /// <summary>A palette component's full value (VFX keeps 6-bit components).</summary>
    constexpr float PaletteFull = 63.0f;
    /// <summary>A glow lights the ground this many times the side of a square of its pixels away.</summary>
    constexpr float GlowReachPerSide = 2.5f;
    /// <summary>How much a glow of white pixels lights the ground under it (1 doubles it).</summary>
    constexpr float GlowGain = 1.0f;
    /// <summary>
    /// A light's colour is this much further from grey than its pixels' mean: the mean of a glow's white core and tinted
    /// rim is paler than the light it stands for.
    /// </summary>
    constexpr float LightSaturation = 1.6f;
    /// <summary>A beam lights this many times its width to each side, and at least BeamMinReach.</summary>
    constexpr float BeamReachPerWidth = 10.0f;
    constexpr float BeamMinReach = 28.0f;
    /// <summary>How much a beam of white lights the ground under it.</summary>
    constexpr float BeamGain = 0.9f;
    /// <summary>A beam's height above the ground (the arms and torsos it runs between), in pixels.</summary>
    constexpr float BeamHeight = 10.0f;
    /// <summary>DUSK's world light (the world dimmed to it) and its lights' strength.</summary>
    constexpr float DuskAmbient = 0.6f;
    constexpr float DuskStrength = 1.0f;
    /// <summary>A muzzle flash's reach, its colour (a propellant's) and how much it lights the ground under it.</summary>
    constexpr float MuzzleFlashReach = 36.0f;
    constexpr std::array<float, 3> MuzzleFlashColor{1.0f, 0.72f, 0.38f};
    constexpr float MuzzleFlashGain = 1.2f;
    /// <summary>Flashes closer than this (in pixels each way) are one muzzle's.</summary>
    constexpr float SameMuzzle = 2.0f;
    /// <summary>A light's pool is as soft as one this much of its reach high, at least (composite.pshader's LightSoftness).</summary>
    constexpr float LightSoftness = 0.3f;
    /// <summary>
    /// The shadows' group in the draw list: after the terrain's (50,000,000), the craters' (30,000,000) and the
    /// terrain's overlays' (10,000,000), before every object's (their screen heights, or less).
    /// </summary>
    constexpr int32_t ShadowDepth = 5000000;
    /// <summary>The most shadows a frame draws (the strongest).</summary>
    constexpr size_t MaxShadows = 96;
    /// <summary>Of a unit's sprite, the share of its width its body is: its shadow's width at its feet.</summary>
    constexpr float CasterBody = 0.6f;
    /// <summary>A shadow is at most this many times as long as its unit is tall.</summary>
    constexpr float ShadowLengthPerHeight = 2.5f;
    /// <summary>A shadow's width at its tip per its width at the feet: it spreads as it goes.</summary>
    constexpr float ShadowSpread = 1.5f;
    /// <summary>A light that lights a unit's feet less than this (1 doubles them) casts no shadow from it.</summary>
    constexpr float FaintestShadow = 0.04f;
    /// <summary>A bright pixel (MCGlowPixels::Bright): one with a component at least this (fire, not dust or smoke).</summary>
    constexpr float BrightPixel = 0.7f;
    /// <summary>
    /// The air over a fire shimmers this many times the side of a square of its bright pixels to each side of their
    /// centre, and this many times above it.
    /// </summary>
    constexpr float HeatWidthPerSide = 0.9f;
    constexpr float HeatRisePerSide = 2.5f;

    struct State
    {
        MCSceneLights::MCLighting Lighting;
        /// <summary>The window whose world is being drawn.</summary>
        std::weak_ptr<MCWindow> Drawing;
        std::vector<MCSceneLight> Adding;
        /// <summary>The frame's muzzle flashes, one a muzzle, added to its lights as it finishes.</summary>
        std::vector<MCSceneLight> Flashes;
        /// <summary>The units casting shadows: their feet, half their width and their height, in the window's pixels.</summary>
        struct Caster
        {
            float X = 0.0f;
            float Y = 0.0f;
            float HalfWidth = 0.0f;
            float Height = 0.0f;
        };

        std::vector<Caster> Casters;
        /// <summary>The frame's fires whose air shimmers.</summary>
        std::vector<MCSceneHeat> Heat;
        /// <summary>The window the last finished lights are for, those lights, its fires and the game's time then.</summary>
        std::weak_ptr<MCWindow> ShownWindow;
        std::vector<MCSceneLight> Shown;
        std::vector<MCSceneHeat> ShownHeat;
        float ShownTime = 0.0f;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>The art's key colour (magenta, in VFX's 6-bit components): its pixels are holes, never shown.</summary>
    constexpr MCVfxRgb KeyColor{63, 0, 63};

    /// <summary>A pixel's light: its colour, and how much of it shows.</summary>
    struct PixelLight
    {
        std::array<float, 3> Color{};
        float Coverage = 1.0f;
    };

    /// <summary>
    /// The light pixel <paramref name="color"/> sends out: a translucent colour's own colour and coverage (a colour
    /// dodge's added light whole), else its palette colour; none from the key colour (a hit's light ends on a frame of
    /// it).
    /// </summary>
    PixelLight LightOf(uint8_t color, bool translucent)
    {
        if (translucent && SpecialColor[color] != 0)
        {
            const MCAlphaBlend blend = AlphaColorBlend(MCAlphaColors[color]);

            // A blend's colour is premultiplied by its coverage.
            if (blend.Coverage <= 0.0f)
            {
                return {{blend.R, blend.G, blend.B}, 1.0f};
            }

            return {{std::min(blend.R / blend.Coverage, 1.0f), std::min(blend.G / blend.Coverage, 1.0f),
                     std::min(blend.B / blend.Coverage, 1.0f)},
                    blend.Coverage};
        }

        const MCVfxRgb rgb = GamePalette().Colors()[color];

        if (rgb.R == KeyColor.R && rgb.G == KeyColor.G && rgb.B == KeyColor.B)
        {
            return {{}, 0.0f};
        }

        return {{static_cast<float>(rgb.R) / PaletteFull, static_cast<float>(rgb.G) / PaletteFull,
                 static_cast<float>(rgb.B) / PaletteFull},
                1.0f};
    }

    /// <summary>A glow's lit pixels: their centre (from the hot spot, weighted by their light), mean light and count.</summary>
    struct Glow
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::array<float, 3> Color{};
        int32_t Pixels = 0;
    };

    std::optional<Glow> MeasureGlow(std::span<const uint8_t> shapeTable, int32_t frame,
                                    MCSceneLights::MCGlowPixels shining)
    {
        if (!VfxIsShapeTable(shapeTable) || frame < 0 || frame >= VfxShapeCount(shapeTable))
        {
            return std::nullopt;
        }

        const bool translucent = MCAgShapeIsAlpha(shapeTable, frame);
        double sumX = 0.0;
        double sumY = 0.0;
        double weight = 0.0;
        std::array<double, 3> sum{};
        int32_t pixels = 0;
        VfxShapeVisit(shapeTable, frame,
                      [&](int32_t x, int32_t y, uint8_t color)
                      {
                          const PixelLight light = LightOf(color, translucent);
                          const double w =
                              (static_cast<double>(light.Color[0]) + light.Color[1] + light.Color[2]) * light.Coverage;

                          if (w <= 0.0 || (shining == MCSceneLights::MCGlowPixels::Bright &&
                                           std::ranges::max(light.Color) < BrightPixel))
                          {
                              return;
                          }

                          for (size_t i = 0; i < sum.size(); ++i)
                          {
                              sum[i] += light.Color[i] * light.Coverage;
                          }

                          sumX += x * w;
                          sumY += y * w;
                          weight += w;
                          ++pixels;
                      });

        if (pixels == 0)
        {
            return std::nullopt;
        }

        Glow glow;
        glow.X = static_cast<float>(sumX / weight);
        glow.Y = static_cast<float>(sumY / weight);

        for (size_t i = 0; i < sum.size(); ++i)
        {
            glow.Color[i] = static_cast<float>(sum[i] / pixels);
        }

        glow.Pixels = pixels;
        return glow;
    }

    /// <summary>A colour <see cref="LightSaturation"/> times further from the grey of its mean.</summary>
    std::array<float, 3> Saturated(const std::array<float, 3>& color)
    {
        const float grey = (color[0] + color[1] + color[2]) / 3.0f;
        std::array<float, 3> saturated{};

        for (size_t i = 0; i < color.size(); ++i)
        {
            saturated[i] = std::max(grey + (color[i] - grey) * LightSaturation, 0.0f);
        }

        return saturated;
    }

    /// <summary>The frame of a shape table an appearance drew, and the shape that holds the table.</summary>
    struct DrawnFrame
    {
        std::shared_ptr<MCShape> Shape;
        std::span<const uint8_t> Table;
        int32_t Frame = 0;
    };

    /// <summary>
    /// The frame <paramref name="appearance"/> last drew (its Render), when it drew one of a VFX shape table: as
    /// MCVfxElement and MCDeltaElement draw it, a delta animation's change over its first shape (its fire).
    /// </summary>
    std::optional<DrawnFrame> FrameDrawn(const MCVfxAppearance& appearance)
    {
        std::shared_ptr<MCShape> shape = appearance.CurrentShape.lock();
        const MCVfxAppearance::MCFrameDrawing drawing = appearance.FrameDrawing();

        if (shape == nullptr || !VfxIsShapeTable(shape->FrameList()) ||
            drawing == MCVfxAppearance::MCFrameDrawing::Nothing)
        {
            return std::nullopt;
        }

        const std::span<const uint8_t> table = shape->FrameList();
        const int32_t last = VfxShapeCount(table) - 1;
        const int32_t frame = drawing == MCVfxAppearance::MCFrameDrawing::Delta
                                  ? std::min(appearance.CurrentFrame + 1, last)
                                  : std::min(appearance.CurrentFrame, last);
        return DrawnFrame{std::move(shape), table, frame};
    }

    /// <summary>How high <paramref name="position"/> is above the ground under it (0 without a terrain).</summary>
    float AltitudeOf(const MCVector3D& position)
    {
        return Terrain() != nullptr ? std::max(position.Z - MCTerrain::GetTerrainElevation(position), 0.0f) : 0.0f;
    }

    /// <summary>Where a light shines on a point of the ground from (its point, or a beam's nearest), and how much.</summary>
    struct Reaching
    {
        /// <summary>The ground under the light, in the window's pixels across and the ground's down (see LightReaching).</summary>
        float X = 0.0f;
        float Y = 0.0f;
        float Distance = 0.0f;
        /// <summary>Its light there (its components' mean), as the composite pools it without the relief.</summary>
        float Light = 0.0f;
    };

    /// <summary>
    /// How <paramref name="light"/> reaches the ground at window (<paramref name="x"/>, <paramref name="y"/>), as the
    /// composite's LightAt pools it; distances along the ground, a screen height <paramref name="aspect"/> of one.
    /// </summary>
    Reaching LightReaching(const MCSceneLight& light, float x, float y, float aspect)
    {
        const float ax = light.X0;
        const float ay = light.Y0 / aspect;
        const float abx = light.X1 - light.X0;
        const float aby = (light.Y1 - light.Y0) / aspect;
        const float length2 = abx * abx + aby * aby;
        const float t =
            length2 > 0.0f ? std::clamp(((x - ax) * abx + (y / aspect - ay) * aby) / length2, 0.0f, 1.0f) : 0.0f;
        Reaching reaching;
        reaching.X = ax + abx * t;
        reaching.Y = ay + aby * t;
        const float dx = x - reaching.X;
        const float dy = y / aspect - reaching.Y;
        const float d2 = dx * dx + dy * dy;
        const float reach2 = std::max(light.Reach * light.Reach, 1.0f);
        reaching.Distance = std::sqrt(d2);

        if (d2 < reach2)
        {
            const float soft = std::max(light.Height, light.Reach * LightSoftness);
            const float x2 = d2 / reach2;
            const float fade = 1.0f - x2 * x2;
            reaching.Light =
                (light.Red + light.Green + light.Blue) / 3.0f * fade * fade * soft * soft / (d2 + soft * soft);
        }

        return reaching;
    }

    /// <summary>The frame's shadows, drawn into the world's light (MCRenderer::Shadow) when the draw list comes to them.</summary>
    class MCShadowElement final : public MCElement
    {
    public:
        MCShadowElement(std::vector<MCShadowCommand> shadows, std::weak_ptr<MCWindow> window)
            : MCElement(ShadowDepth), _Shadows(std::move(shadows)), _Window(std::move(window))
        {
        }

        void Draw() override
        {
            if (const std::shared_ptr<MCWindow> window = _Window.lock(); window != nullptr)
            {
                MCRenderer& renderer = MCRenderer::For(*window);

                for (const MCShadowCommand& shadow : _Shadows)
                {
                    renderer.Shadow(*window, shadow);
                }
            }
        }

    private:
        std::vector<MCShadowCommand> _Shadows;
        std::weak_ptr<MCWindow> _Window;
    };

    /// <summary>What sorts the brightest lights first: their light over the ground they reach.</summary>
    float Brightness(const MCSceneLight& light)
    {
        return (light.Red + light.Green + light.Blue) * light.Reach;
    }

    /// <summary>A beam's light.</summary>
    void AddBeamLight(float x0, float y0, float x1, float y1, float width, const std::array<float, 3>& pixels)
    {
        const std::array<float, 3> color = Saturated(pixels);
        MCSceneLight light;
        // The ground under a beam is its height lower on the screen.
        light.X0 = x0;
        light.Y0 = y0 + BeamHeight;
        light.X1 = x1;
        light.Y1 = y1 + BeamHeight;
        light.Height = BeamHeight;
        light.Reach = std::max(BeamMinReach, width * BeamReachPerWidth);
        light.Red = color[0] * BeamGain;
        light.Green = color[1] * BeamGain;
        light.Blue = color[2] * BeamGain;
        MCSceneLights::Add(light);
    }
}

auto MCSceneLights::Lighting() -> MCLighting&
{
    return TheState().Lighting;
}

auto MCSceneLights::ShownLighting() -> MCLighting
{
    MCLighting shown = TheState().Lighting;

    if (GHdLighting == HdLightingDusk)
    {
        shown.Ambient = DuskAmbient;
        shown.Strength = DuskStrength;
    }

    return shown;
}

auto MCSceneLights::Begin(const MCPane& pane) -> void
{
    State& state = TheState();
    state.Drawing = pane.Window;
    state.Adding.clear();
    state.Flashes.clear();
    state.Casters.clear();
    state.Heat.clear();
}

auto MCSceneLights::Add(MCSceneLight light) -> void
{
    if (GHdLighting != HdLightingOff && !TheState().Drawing.expired())
    {
        TheState().Adding.push_back(light);
    }
}

auto MCSceneLights::AddGlow(std::span<const uint8_t> shapeTable, int32_t frame, float x, float y, float altitude,
                            MCGlowPixels pixels) -> void
{
    if (GHdLighting == HdLightingOff || TheState().Drawing.expired())
    {
        return;
    }

    const std::optional<Glow> glow = MeasureGlow(shapeTable, frame, pixels);

    if (!glow.has_value())
    {
        return;
    }

    MCSceneLight light;
    // It shines from its pixels' centre; the ground under that is its hot spot's, altitude lower on the screen.
    light.X0 = light.X1 = x + glow->X;
    light.Y0 = light.Y1 = y + altitude;
    light.Height = std::max(altitude - glow->Y, 0.0f);
    light.Reach = GlowReachPerSide * std::sqrt(static_cast<float>(glow->Pixels));
    const std::array<float, 3> color = Saturated(glow->Color);
    light.Red = color[0] * GlowGain;
    light.Green = color[1] * GlowGain;
    light.Blue = color[2] * GlowGain;
    Add(light);
}

auto MCSceneLights::AddGlow(const MCVfxAppearance& appearance, const MCVector3D& position, MCGlowPixels pixels) -> void
{
    if (GHdLighting == HdLightingOff || TheState().Drawing.expired())
    {
        return;
    }

    if (const std::optional<DrawnFrame> drawn = FrameDrawn(appearance); drawn.has_value())
    {
        AddGlow(drawn->Table, drawn->Frame, appearance.ScreenPos.X, appearance.ScreenPos.Y, AltitudeOf(position),
                pixels);
    }
}

auto MCSceneLights::AddBeam(float x0, float y0, float x1, float y1, float width, uint8_t color) -> void
{
    if (GHdLighting == HdLightingOff || TheState().Drawing.expired())
    {
        return;
    }

    AddBeamLight(x0, y0, x1, y1, width, LightOf(color, false).Color);
}

auto MCSceneLights::AddShapeBeam(float x0, float y0, float x1, float y1, float width,
                                 std::span<const uint8_t> shapeTable, int32_t frame) -> void
{
    if (GHdLighting == HdLightingOff || TheState().Drawing.expired())
    {
        return;
    }

    if (const std::optional<Glow> glow = MeasureGlow(shapeTable, frame, MCGlowPixels::All); glow.has_value())
    {
        AddBeamLight(x0, y0, x1, y1, width, glow->Color);
    }
}

auto MCSceneLights::AddMuzzleFlash(const MCVector3D& muzzle, float left) -> void
{
    State& state = TheState();
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (GHdLighting == HdLightingOff || state.Drawing.expired() || left <= 0.0f || camera == nullptr)
    {
        return;
    }

    const MCVector2D at = ProjectToScreen(muzzle, *camera);
    const float altitude = AltitudeOf(muzzle);
    const float fade = std::min(left / MuzzleFlashSeconds, 1.0f);
    MCSceneLight light;
    // The ground under the muzzle is its altitude lower on the screen.
    light.X0 = light.X1 = at.X;
    light.Y0 = light.Y1 = at.Y + altitude;
    light.Height = altitude;
    light.Reach = MuzzleFlashReach;
    light.Red = MuzzleFlashColor[0] * MuzzleFlashGain * fade;
    light.Green = MuzzleFlashColor[1] * MuzzleFlashGain * fade;
    light.Blue = MuzzleFlashColor[2] * MuzzleFlashGain * fade;
    const auto sameMuzzle = [&](const MCSceneLight& flash)
    { return std::abs(flash.X0 - light.X0) < SameMuzzle && std::abs(flash.Y0 - light.Y0) < SameMuzzle; };

    if (const auto flash = std::ranges::find_if(state.Flashes, sameMuzzle); flash != state.Flashes.end())
    {
        flash->Red = std::max(flash->Red, light.Red);
        flash->Green = std::max(flash->Green, light.Green);
        flash->Blue = std::max(flash->Blue, light.Blue);
        return;
    }

    state.Flashes.push_back(light);
}

auto MCSceneLights::AddCaster(const MCAppearance& appearance, const MCVector3D& position) -> void
{
    State& state = TheState();

    if (GHdLighting == HdLightingOff || state.Drawing.expired())
    {
        return;
    }

    // Its feet on the ground: a jumping unit's are its altitude lower on the screen.
    const float feet = appearance.ScreenPos.Y + AltitudeOf(position);
    const float halfWidth = (appearance.LowerRight.X - appearance.UpperLeft.X) * 0.5f;
    const float height = feet - appearance.UpperLeft.Y;

    if (halfWidth > 0.0f && height > 0.0f)
    {
        state.Casters.push_back({appearance.ScreenPos.X, feet, halfWidth, height});
    }
}

auto MCSceneLights::AddShadows() -> void
{
    State& state = TheState();
    const float darkness = ShownLighting().Shadow;

    if (GHdLighting == HdLightingOff || state.Drawing.expired() || darkness <= 0.0f || state.Casters.empty())
    {
        return;
    }

    const float aspect = GroundAspect();
    std::vector<MCShadowCommand> shadows;
    std::vector<std::pair<const MCSceneLight*, Reaching>> reaching;

    for (const State::Caster& caster : state.Casters)
    {
        reaching.clear();
        float total = 0.0f;

        for (const std::vector<MCSceneLight>* lights : {&state.Adding, &state.Flashes})
        {
            for (const MCSceneLight& light : *lights)
            {
                const Reaching reaches = LightReaching(light, caster.X, caster.Y, aspect);
                total += reaches.Light;
                reaching.emplace_back(&light, reaches);
            }
        }

        const float width = caster.HalfWidth * CasterBody;

        for (const auto& [light, reaches] : reaching)
        {
            // None from a faint light, or one within the unit's sprite (its own fire, its guns' flashes).
            if (reaches.Light < FaintestShadow || reaches.Distance <= caster.HalfWidth)
            {
                continue;
            }

            // Away from the light, as far as the unit hides it from the ground: a light above the unit's top casts it
            // the unit's height over the light's above that times the light's distance; a lower one, the longest.
            const float longest = ShadowLengthPerHeight * caster.Height;
            const float length =
                light->Height > caster.Height
                    ? std::min(reaches.Distance * caster.Height / (light->Height - caster.Height), longest)
                    : longest;
            const float awayX = (caster.X - reaches.X) / reaches.Distance;
            const float awayY = (caster.Y / aspect - reaches.Y) / reaches.Distance;
            MCShadowCommand shadow;
            shadow.X0 = caster.X;
            shadow.Y0 = caster.Y;
            shadow.X1 = caster.X + awayX * length;
            shadow.Y1 = caster.Y + awayY * length * aspect;
            shadow.Width0 = width;
            shadow.Width1 = width * ShadowSpread;
            // It takes away this light's share of the light at the unit's feet.
            shadow.Strength = darkness * reaches.Light / total;
            shadow.Aspect = aspect;
            shadows.push_back(shadow);
        }
    }

    if (shadows.empty())
    {
        return;
    }

    if (shadows.size() > MaxShadows)
    {
        std::ranges::partial_sort(shadows, shadows.begin() + MaxShadows,
                                  [](const MCShadowCommand& a, const MCShadowCommand& b)
                                  { return a.Strength > b.Strength; });
        shadows.resize(MaxShadows);
    }

    ElementList().OpenGroup(ShadowDepth, false);
    ElementList().Add(ElementList().Make<MCShadowElement>(std::move(shadows), state.Drawing));
}

auto MCSceneLights::AddHeat(const MCVfxAppearance& appearance) -> void
{
    State& state = TheState();

    if (GHdLighting == HdLightingOff || state.Drawing.expired())
    {
        return;
    }

    const std::optional<DrawnFrame> drawn = FrameDrawn(appearance);
    const std::optional<Glow> fire =
        drawn.has_value() ? MeasureGlow(drawn->Table, drawn->Frame, MCGlowPixels::Bright) : std::nullopt;

    if (!fire.has_value())
    {
        return;
    }

    const float side = std::sqrt(static_cast<float>(fire->Pixels));
    state.Heat.push_back({appearance.ScreenPos.X + fire->X, appearance.ScreenPos.Y + fire->Y, side * HeatWidthPerSide,
                          side * HeatRisePerSide});
}

auto MCSceneLights::Finish() -> void
{
    State& state = TheState();

    if (state.Drawing.expired())
    {
        return;
    }

    state.Adding.insert(state.Adding.end(), state.Flashes.begin(), state.Flashes.end());
    state.Flashes.clear();

    if (state.Adding.size() > MaxLights)
    {
        std::ranges::partial_sort(state.Adding, state.Adding.begin() + MaxLights,
                                  [](const MCSceneLight& a, const MCSceneLight& b)
                                  { return Brightness(a) > Brightness(b); });
        state.Adding.resize(MaxLights);
    }

    state.Shown = std::move(state.Adding);
    state.Adding.clear();

    if (state.Heat.size() > MaxHeat)
    {
        std::ranges::partial_sort(state.Heat, state.Heat.begin() + MaxHeat,
                                  [](const MCSceneHeat& a, const MCSceneHeat& b)
                                  { return a.HalfWidth * a.Rise > b.HalfWidth * b.Rise; });
        state.Heat.resize(MaxHeat);
    }

    state.ShownHeat = std::move(state.Heat);
    state.Heat.clear();
    state.ShownTime = ScenarioTime;
    state.ShownWindow = std::exchange(state.Drawing, {});
}

auto MCSceneLights::Shown(const MCWindow& view) -> std::span<const MCSceneLight>
{
    const State& state = TheState();

    if (GHdLighting == HdLightingOff || state.ShownWindow.lock().get() != &view)
    {
        return {};
    }

    return state.Shown;
}

auto MCSceneLights::ShownHeat(const MCWindow& view) -> std::span<const MCSceneHeat>
{
    const State& state = TheState();

    if (GHdLighting == HdLightingOff || state.ShownWindow.lock().get() != &view)
    {
        return {};
    }

    return state.ShownHeat;
}

auto MCSceneLights::ShownTime() -> float
{
    return TheState().ShownTime;
}

auto MCSceneLights::GroundAspect() -> float
{
    return MCTerrain::ProjectionCos != 0.0f ? MCTerrain::ProjectionSin / MCTerrain::ProjectionCos : 1.0f;
}
