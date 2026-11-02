#include "stdafx.h"
#include "sprite/MCSceneLights.h"
#include "appear/MCAppearance.h"
#include "camera/MCCameraList.h"
#include "color/MCPalette.h"
#include "color/MCWaterCycle.h"
#include "engine/MCElement.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCVector3D.h"
#include "main/MCMissionGlobals.h"
#include "object/MCObjectDrawing.h"
#include "platform/MCRenderer.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCParticles.h"
#include "sprite/MCShape.h"
#include "sprite/MCVfxAppearance.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxFunctions.h"

#include <map>

int GHdLighting = 0;
int GHdSunShadows = 0;
int GHdClouds = 0;
int GHdSway = 0;
int GHdRelief = 0;

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
    /// <summary>Dusk's world light (the world dimmed to it) and its lights' strength.</summary>
    constexpr float DuskAmbient = 0.6f;
    constexpr float DuskStrength = 1.0f;
    /// <summary>
    /// Port-only (sun shadows): dusk's sun, from the day's side (the art's lit one) but lower: its shadows this many
    /// times as long as the day's.
    /// </summary>
    constexpr float DuskSunLength = 2.5f;
    /// <summary>
    /// Port-only (sun shadows): how tall the trees stand over the shadows painted into a forest's art (MCHdArt::Frame::
    /// PaintedShadow), in window pixels: their shadows mostly run 12 to 19 pixels across, and the day's sun casts 0.6 of
    /// a pixel across for each pixel of height.
    /// </summary>
    constexpr float PaintedShadowHeight = 30.0f;
    /// <summary>
    /// Night's world light: dimmer than dusk's, the moon's blue (its tint about as bright as white), and the world's
    /// colours paler in it; the lights stronger.
    /// </summary>
    constexpr float NightAmbient = 0.35f;
    constexpr std::array<float, 3> NightTint{0.78f, 0.95f, 1.45f};
    constexpr float NightGrey = 0.5f;
    constexpr float NightStrength = 1.5f;
    /// <summary>
    /// A unit's headlight at night, a spotlight from its lamp (the window's pixels, along the ground): its beam's middle
    /// meets the ground this far ahead of the ground under the lamp, whatever the lamp's height (a higher lamp tips its
    /// beam down more), and it has faded out this far from there; a unit with no lamp in its art (a vehicle) has one this
    /// low at its feet, and a lamp at least this high; its beam fades out this many degrees off its middle, full within
    /// SpotFull of that; its colour (a lamp's warm white) and its light where its beam's middle meets the ground.
    /// </summary>
    constexpr float HeadlightAhead = 70.0f;
    constexpr float HeadlightReach = 180.0f;
    constexpr float HeadlightLowLamp = 12.0f;
    constexpr float HeadlightLowest = 6.0f;
    constexpr float HeadlightDegrees = 22.0f;
    constexpr std::array<float, 3> HeadlightColor{1.0f, 0.93f, 0.8f};
    constexpr float HeadlightGain = 0.8f;
    /// <summary>
    /// A unit's facing is measured this many world units ahead of it: far enough that the projection's rounding to
    /// pixels doesn't bend its way.
    /// </summary>
    constexpr float FacingStep = 1000.0f;
    /// <summary>A headlight's lamp's mark: its size (the window's pixels), softness and colour (premultiplied, added).</summary>
    struct LampMark
    {
        float Radius = 0.0f;
        float Softness = 0.0f;
        std::array<uint8_t, 3> Color{};
    };

    /// <summary>
    /// A headlight's lamp: a small hard-edged core, as the art draws a light, in a faint halo; shown fully facing the
    /// camera or sideways, fading out as the unit faces away (its facing's share up the screen, from LampHideFrom to
    /// LampHidden; composite.pshader's AirBeamHideFrom and AirBeamHidden for its beam in the air).
    /// </summary>
    constexpr LampMark LampCore{1.4f, 0.35f, {255, 246, 225}};
    constexpr LampMark LampHalo{4.5f, 1.0f, {110, 95, 70}};
    constexpr float LampHideFrom = 0.2f;
    constexpr float LampHidden = 0.7f;
    /// <summary>
    /// A spotlight's beam is full within this share of the way from its edge to its middle; the ground nearer it than
    /// where its beam's middle meets the ground takes at most SpotMost times the light there (composite.pshader's).
    /// </summary>
    constexpr float SpotFull = 0.5f;
    constexpr float SpotMost = 1.5f;
    /// <summary>The times of day by name (a scenario file's TimeOfDay, the tools' --hd-time-of-day).</summary>
    constexpr std::array<std::pair<std::string_view, MCTimeOfDay>, 3> TimesOfDay{{
        {"Day", MCTimeOfDay::Day},
        {"Dusk", MCTimeOfDay::Dusk},
        {"Night", MCTimeOfDay::Night},
    }};
    /// <summary>A muzzle flash's reach, its colour (a propellant's) and how much it lights the ground under it.</summary>
    constexpr float MuzzleFlashReach = 36.0f;
    constexpr std::array<float, 3> MuzzleFlashColor{1.0f, 0.72f, 0.38f};
    constexpr float MuzzleFlashGain = 1.2f;
    /// <summary>Flashes closer than this (in pixels each way) are one muzzle's.</summary>
    constexpr float SameMuzzle = 2.0f;
    /// <summary>A light's pool is as soft as one this much of its reach high, at least (composite.pshader's LightSoftness).</summary>
    constexpr float LightSoftness = 0.3f;
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
    /// <summary>The ground's map (MCSceneGround) is measured over this many meters each way.</summary>
    constexpr float GroundSpan = 100.0f;

    struct State
    {
        MCSceneLights::MCLighting Lighting;
        /// <summary>The time of day of the mission being played.</summary>
        MCTimeOfDay MissionTime = MCTimeOfDay::Day;
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
        MCSceneGround ShownGround;
        float ShownWaterPhase = 0.0f;
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

    using Glow = MCSceneLights::MCGlowMeasure;

    /// <summary>A pixel's light and its weight (its light times its coverage), when it's one of the pixels that shine.</summary>
    struct ShiningPixel
    {
        PixelLight Light;
        double Weight = 0.0;
    };

    /// <summary>
    /// Pixel <paramref name="color"/>'s light, when it's one of <paramref name="shining"/> (it sends out light, and for
    /// MCGlowPixels::Bright it's bright); none otherwise.
    /// </summary>
    std::optional<ShiningPixel> Shining(uint8_t color, bool translucent, MCSceneLights::MCGlowPixels shining)
    {
        const PixelLight light = LightOf(color, translucent);
        const double weight = (static_cast<double>(light.Color[0]) + light.Color[1] + light.Color[2]) * light.Coverage;

        if (weight <= 0.0 ||
            (shining == MCSceneLights::MCGlowPixels::Bright && std::ranges::max(light.Color) < BrightPixel))
        {
            return std::nullopt;
        }

        return ShiningPixel{light, weight};
    }

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
                          const std::optional<ShiningPixel> shone = Shining(color, translucent, shining);

                          if (!shone.has_value())
                          {
                              return;
                          }

                          const PixelLight& light = shone->Light;
                          const double w = shone->Weight;

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
    struct DrawnShape
    {
        std::shared_ptr<MCShape> Shape;
        std::span<const uint8_t> Table;
        int32_t Frame = 0;
    };

    /// <summary>
    /// The frame <paramref name="appearance"/> last drew (its Render), when it drew one of a VFX shape table: as
    /// MCVfxElement and MCDeltaElement draw it, a delta animation's change over its first shape (its fire).
    /// </summary>
    std::optional<DrawnShape> FrameDrawn(const MCVfxAppearance& appearance)
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
        return DrawnShape{std::move(shape), table, frame};
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
    /// A spotlight's (<paramref name="light"/>'s) light on the ground (<paramref name="dx"/>, <paramref name="dy"/>) off
    /// the ground under it (along the ground, a screen height <paramref name="aspect"/> of one), as a share of its light
    /// where its beam's middle meets the ground: as the composite's SpotPool.
    /// </summary>
    float SpotPool(const MCSceneLight& light, float dx, float dy, float aspect)
    {
        const std::array<float, 3> ray{dx, dy, -light.Height};
        const std::array<float, 3> aim{light.SpotX, light.SpotY / aspect, -light.Height};
        const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b)
        { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        const float lengths = std::sqrt(dot(ray, ray) * dot(aim, aim));

        if (lengths <= 0.0f)
        {
            return 0.0f;
        }

        const float full = light.SpotCos + (1.0f - light.SpotCos) * SpotFull;
        const float t = std::clamp((dot(ray, aim) / lengths - light.SpotCos) / (full - light.SpotCos), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t) * std::min(dot(aim, aim) / dot(ray, ray), SpotMost);
    }

    /// <summary>Whether <paramref name="light"/> is a spotlight (see MCSceneLight::SpotX).</summary>
    bool IsSpot(const MCSceneLight& light)
    {
        return light.SpotCos > 0.0f && (light.SpotX != 0.0f || light.SpotY != 0.0f);
    }

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
            const float pool = IsSpot(light) ? SpotPool(light, dx, dy, aspect) : soft * soft / (d2 + soft * soft);
            reaching.Light = (light.Red + light.Green + light.Blue) / 3.0f * fade * fade * pool;
        }

        return reaching;
    }

    /// <summary>The frame's shadows, drawn into the world's light (MCRenderer::Shadow) when the draw list comes to them.</summary>
    class MCShadowElement final : public MCElement
    {
    public:
        MCShadowElement(std::vector<MCShadowCommand> shadows, std::weak_ptr<MCWindow> window)
            : MCElement(MCSceneLights::ShadowDepth), _Shadows(std::move(shadows)), _Window(std::move(window))
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

auto MCSceneLights::ReliefMapsOn() -> bool
{
    return MCHdArt::Active() && GHdLighting != HdLightingOff && GHdRelief != 0 && TheState().Lighting.ReliefMaps > 0.0f;
}

auto MCSceneLights::ShownLighting() -> MCLighting
{
    MCLighting shown = TheState().Lighting;

    switch (TimeOfDay())
    {
        case MCTimeOfDay::Day:
            break;
        case MCTimeOfDay::Dusk:
        {
            shown.Ambient = DuskAmbient;
            shown.Strength = DuskStrength;
            shown.SunShadowRight *= DuskSunLength;
            shown.SunShadowUp *= DuskSunLength;
            break;
        }
        case MCTimeOfDay::Night:
        {
            shown.Ambient = NightAmbient;
            shown.AmbientTint = NightTint;
            shown.AmbientGrey = NightGrey;
            shown.Strength = NightStrength;
            // No sun.
            shown.SunShadowRight = 0.0f;
            shown.SunShadowUp = 0.0f;
            break;
        }
    }

    return shown;
}

auto MCSceneLights::SetMissionTime(MCTimeOfDay time) -> void
{
    TheState().MissionTime = time;
}

auto MCSceneLights::MissionTime() -> MCTimeOfDay
{
    return TheState().MissionTime;
}

auto MCSceneLights::TimeOfDay() -> MCTimeOfDay
{
    switch (GHdLighting)
    {
        case HdLightingOff:
            return MCTimeOfDay::Day;
        case HdLightingDusk:
            return MCTimeOfDay::Dusk;
        case HdLightingNight:
            return MCTimeOfDay::Night;
        default:
            return MissionTime();
    }
}

auto MCSceneLights::TimeOfDayNamed(std::string_view name) -> std::optional<MCTimeOfDay>
{
    const auto found =
        std::ranges::find_if(TimesOfDay, [name](const auto& time) { return MCIEquals(time.first, name); });
    return found != TimesOfDay.end() ? std::optional(found->second) : std::nullopt;
}

auto MCSceneLights::ReadTimeOfDay(MCFitIniFile& file) -> MCTimeOfDay
{
    if (file.SeekBlock("Environment") != 0)
    {
        return MCTimeOfDay::Day;
    }

    const MCFitResult<std::string> name = file.Read<std::string>("TimeOfDay");

    if (!name)
    {
        return MCTimeOfDay::Day;
    }

    if (const std::optional<MCTimeOfDay> time = TimeOfDayNamed(*name); time.has_value())
    {
        return *time;
    }

    SDL_Log("MCSceneLights: the scenario's [Environment] TimeOfDay \"%s\" is none of Day, Dusk and Night; shown by day",
            name->c_str());
    return MCTimeOfDay::Day;
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

    if (const std::optional<DrawnShape> drawn = FrameDrawn(appearance); drawn.has_value())
    {
        AddGlow(drawn->Table, drawn->Frame, appearance.ScreenPos.X, appearance.ScreenPos.Y, AltitudeOf(position),
                pixels);
    }
}

auto MCSceneLights::MeasureDrawn(const MCVfxAppearance& appearance, MCGlowPixels pixels) -> std::optional<MCGlowMeasure>
{
    const std::optional<DrawnShape> drawn = FrameDrawn(appearance);
    return drawn.has_value() ? MeasureGlow(drawn->Table, drawn->Frame, pixels) : std::nullopt;
}

auto MCSceneLights::MeasureFrame(std::span<const uint8_t> shapeTable, int32_t frame, MCGlowPixels pixels)
    -> std::optional<MCGlowMeasure>
{
    return MeasureGlow(shapeTable, frame, pixels);
}

auto MCSceneLights::FrameColors(std::span<const uint8_t> shapeTable, int32_t frame, MCGlowPixels pixels)
    -> std::vector<MCGlowColor>
{
    if (!VfxIsShapeTable(shapeTable) || frame < 0 || frame >= VfxShapeCount(shapeTable))
    {
        return {};
    }

    const bool translucent = MCAgShapeIsAlpha(shapeTable, frame);
    std::array<int32_t, MCPalette::ColorCount> counts{};
    VfxShapeVisit(shapeTable, frame,
                  [&](int32_t, int32_t, uint8_t color)
                  {
                      if (Shining(color, translucent, pixels).has_value())
                      {
                          ++counts[color];
                      }
                  });
    std::vector<MCGlowColor> colors;

    for (size_t color = 0; color < counts.size(); ++color)
    {
        if (counts[color] > 0)
        {
            colors.push_back({LightOf(static_cast<uint8_t>(color), translucent).Color, counts[color]});
        }
    }

    return colors;
}

auto MCSceneLights::FrameOutline(std::span<const uint8_t> shapeTable, int32_t frame, MCGlowPixels pixels)
    -> std::vector<MCGlowPixel>
{
    if (!VfxIsShapeTable(shapeTable) || frame < 0 || frame >= VfxShapeCount(shapeTable))
    {
        return {};
    }

    const bool translucent = MCAgShapeIsAlpha(shapeTable, frame);
    std::map<std::pair<int32_t, int32_t>, std::array<float, 3>> shining;
    VfxShapeVisit(shapeTable, frame,
                  [&](int32_t x, int32_t y, uint8_t color)
                  {
                      if (const std::optional<ShiningPixel> shone = Shining(color, translucent, pixels);
                          shone.has_value())
                      {
                          shining[{x, y}] = shone->Light.Color;
                      }
                  });
    std::vector<MCGlowPixel> outline;

    for (const auto& [at, color] : shining)
    {
        const auto [x, y] = at;

        if (!shining.contains({x - 1, y}) || !shining.contains({x + 1, y}) || !shining.contains({x, y - 1}) ||
            !shining.contains({x, y + 1}))
        {
            outline.push_back({x, y, color});
        }
    }

    return outline;
}

auto MCSceneLights::DrawnFrame(const MCVfxAppearance& appearance) -> std::optional<MCTableFrame>
{
    const std::optional<DrawnShape> drawn = FrameDrawn(appearance);
    return drawn.has_value() ? std::optional<MCTableFrame>({drawn->Table, drawn->Frame}) : std::nullopt;
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

auto MCSceneLights::AddHeadlight(const MCAppearance& appearance, const MCVector3D& position, const MCFrameOfRef& frame,
                                 float turn, const std::optional<MCVector3D>& lamp, int32_t depthFixup) -> void
{
    State& state = TheState();
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (TimeOfDay() != MCTimeOfDay::Night || state.Drawing.expired() || camera == nullptr)
    {
        return;
    }

    // Its facing on the ground (the frame turned an eighth of a turn, the facing its art is drawn at, as MCMover's),
    // turned as far again (counterclockwise, a mech's torso's), and that way on the screen along the ground (a screen
    // height GroundAspect of one).
    const float eighth = std::numbers::sqrt2_v<float> * 0.5f;
    const float bodyX = (frame.I.X - frame.J.X) * eighth;
    const float bodyY = (frame.I.Y - frame.J.Y) * eighth;
    const float radians = turn * std::numbers::pi_v<float> / 180.0f;
    const float facingX = bodyX * std::cos(radians) - bodyY * std::sin(radians);
    const float facingY = bodyX * std::sin(radians) + bodyY * std::cos(radians);
    const MCVector2D at = ProjectToScreen(position, *camera);
    const MCVector2D ahead = ProjectToScreen(
        MCVector3D(position.X + facingX * FacingStep, position.Y + facingY * FacingStep, position.Z), *camera);
    const float aspect = GroundAspect();
    const float screenX = ahead.X - at.X;
    const float screenY = ahead.Y - at.Y;
    const float wayX = screenX;
    const float wayY = screenY / aspect;
    const float way = std::hypot(wayX, wayY);

    if (way <= 0.0f)
    {
        return;
    }

    // The lamp, and the ground under it (its altitude lower on the screen); with none, a low one at the unit's feet (a
    // jumping unit's are its altitude lower on the screen).
    const std::optional<MCVector2D> lampAt =
        lamp.has_value() ? std::optional(ProjectToScreen(*lamp, *camera)) : std::nullopt;
    const float height = lamp.has_value() ? std::max(AltitudeOf(*lamp), HeadlightLowest) : HeadlightLowLamp;
    MCSceneLight light;
    light.X0 = light.X1 = lampAt.has_value() ? lampAt->X : appearance.ScreenPos.X;
    light.Y0 = light.Y1 =
        lampAt.has_value() ? lampAt->Y + AltitudeOf(*lamp) : appearance.ScreenPos.Y + AltitudeOf(position);
    light.Height = height;
    light.SpotX = wayX / way * HeadlightAhead;
    light.SpotY = wayY / way * HeadlightAhead * aspect;
    light.SpotCos = std::cos(HeadlightDegrees * std::numbers::pi_v<float> / 180.0f);
    light.Reach = HeadlightReach;
    light.Red = HeadlightColor[0] * HeadlightGain;
    light.Green = HeadlightColor[1] * HeadlightGain;
    light.Blue = HeadlightColor[2] * HeadlightGain;
    Add(light);

    // The lamp: hidden by the unit as it faces away (up the screen).
    const float up = -screenY / std::hypot(screenX, screenY);
    const float shows = 1.0f - std::clamp((up - LampHideFrom) / (LampHidden - LampHideFrom), 0.0f, 1.0f);

    if (!lampAt.has_value() || shows <= 0.0f)
    {
        return;
    }

    // Its halo, then its core over it, just over the unit's parts (drawn after them).
    const int32_t depth = static_cast<int32_t>(std::floor(static_cast<float>(depthFixup) - appearance.ScreenPos.Y)) - 1;

    for (const auto& [look, order] : {std::pair{LampHalo, 0}, std::pair{LampCore, 1}})
    {
        MCParticleCommand mark;
        mark.X0 = mark.X1 = lampAt->X;
        mark.Y0 = mark.Y1 = lampAt->Y;
        mark.Radius = look.Radius;
        mark.Softness = look.Softness;
        mark.Red = static_cast<uint8_t>(std::lround(look.Color[0] * shows));
        mark.Green = static_cast<uint8_t>(std::lround(look.Color[1] * shows));
        mark.Blue = static_cast<uint8_t>(std::lround(look.Color[2] * shows));
        MCParticles::AddMark(mark, depth - order);
    }
}

auto MCSceneLights::HeadlightOn(int32_t status) -> bool
{
    constexpr int32_t Running = 0;
    constexpr int32_t StartingUp = 3;
    return status == Running || status == StartingUp;
}

auto MCSceneLights::RecastsSunShadows() -> bool
{
    return GHdSunShadows != 0 && TimeOfDay() != MCTimeOfDay::Day && MCHdArt::Active() &&
           MCRenderer::GpuDrawing() == MCGpuDrawing::On;
}

auto MCSceneLights::SunCasts() -> bool
{
    return GHdSunShadows != 0 && ShownLighting().SunShadowUp > 0.0f;
}

auto MCSceneLights::PaintedShadowReach() -> std::array<float, 2>
{
    if (!RecastsSunShadows() || !SunCasts())
    {
        return {};
    }

    const MCLighting& day = TheState().Lighting;
    const MCLighting shown = ShownLighting();
    return {std::max(shown.SunShadowRight - day.SunShadowRight, 0.0f) * PaintedShadowHeight,
            std::max(shown.SunShadowUp - day.SunShadowUp, 0.0f) * PaintedShadowHeight};
}

auto MCSceneLights::AddRecastShadows(std::span<const MCSunShadowPart> parts, int32_t depth, bool sortElements) -> void
{
    if (!RecastsSunShadows() || !SunCasts())
    {
        return;
    }

    ElementList().OpenGroup(ShadowDepth, false);

    for (const MCSunShadowPart& part : parts)
    {
        if (part.Frames.empty())
        {
            continue;
        }

        auto& element = ElementList().Make<MCVfxElement>(part.Frames, part.X, part.Y, part.Frame, part.Reverse,
                                                         std::nullopt, part.Scale);
        element.SunShadow = true;
        ElementList().Add(element);
    }

    ElementList().OpenGroup(depth, sortElements);
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
            // None from a faint light, or one within the unit's sprite (its own fire, its guns' flashes, its headlight).
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

    const std::optional<DrawnShape> drawn = FrameDrawn(appearance);
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
    state.ShownWaterPhase = WaterCyclePhase();

    if (const std::shared_ptr<MCCamera> camera = ActiveMainCamera(); camera != nullptr)
    {
        const MCVector2D origin = ProjectToScreen(MCVector3D(0.0f, 0.0f, 0.0f), *camera);
        const MCVector2D east = ProjectToScreen(MCVector3D(GroundSpan, 0.0f, 0.0f), *camera);
        const MCVector2D north = ProjectToScreen(MCVector3D(0.0f, GroundSpan, 0.0f), *camera);
        state.ShownGround = {origin.X,
                             origin.Y,
                             (east.X - origin.X) / GroundSpan,
                             (east.Y - origin.Y) / GroundSpan,
                             (north.X - origin.X) / GroundSpan,
                             (north.Y - origin.Y) / GroundSpan};
    }

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

auto MCSceneLights::ShownGround(const MCWindow& view) -> std::optional<MCSceneGround>
{
    const State& state = TheState();

    if ((GHdLighting == HdLightingOff && GHdClouds == 0 && GHdSway == 0) || state.ShownWindow.lock().get() != &view)
    {
        return std::nullopt;
    }

    return state.ShownGround;
}

auto MCSceneLights::ShownWaterPhase() -> float
{
    return TheState().ShownWaterPhase;
}

auto MCSceneLights::GroundAspect() -> float
{
    return MCTerrain::ProjectionCos != 0.0f ? MCTerrain::ProjectionSin / MCTerrain::ProjectionCos : 1.0f;
}
