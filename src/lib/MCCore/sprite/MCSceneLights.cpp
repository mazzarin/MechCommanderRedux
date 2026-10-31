#include "stdafx.h"
#include "sprite/MCSceneLights.h"
#include "color/MCPalette.h"
#include "lib/MCVector3D.h"
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
    /// <summary>The lowest a light sits above the ground: one on it would light a sharp ring around itself.</summary>
    constexpr float MinHeight = 4.0f;
    /// <summary>A bright pixel (MCGlowPixels::Bright): one with a component at least this (fire, not dust or smoke).</summary>
    constexpr float BrightPixel = 0.7f;

    struct State
    {
        MCSceneLights::MCLighting Lighting;
        /// <summary>The window whose world is being drawn.</summary>
        std::weak_ptr<MCWindow> Drawing;
        std::vector<MCSceneLight> Adding;
        /// <summary>The window the last finished lights are for, and those lights.</summary>
        std::weak_ptr<MCWindow> ShownWindow;
        std::vector<MCSceneLight> Shown;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>A pixel's light: its colour, and how much of it shows.</summary>
    struct PixelLight
    {
        std::array<float, 3> Color{};
        float Coverage = 1.0f;
    };

    /// <summary>
    /// The light pixel <paramref name="color"/> sends out: a translucent colour's own colour and coverage (a colour
    /// dodge's added light whole), else its palette colour.
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

auto MCSceneLights::Begin(const MCPane& pane) -> void
{
    State& state = TheState();
    state.Drawing = pane.Window;
    state.Adding.clear();
}

auto MCSceneLights::Add(MCSceneLight light) -> void
{
    if (GHdLighting != 0 && !TheState().Drawing.expired())
    {
        TheState().Adding.push_back(light);
    }
}

auto MCSceneLights::AddGlow(std::span<const uint8_t> shapeTable, int32_t frame, float x, float y, float altitude,
                            MCGlowPixels pixels) -> void
{
    if (GHdLighting == 0 || TheState().Drawing.expired())
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
    light.Height = std::max(altitude - glow->Y, MinHeight);
    light.Reach = GlowReachPerSide * std::sqrt(static_cast<float>(glow->Pixels));
    const std::array<float, 3> color = Saturated(glow->Color);
    light.Red = color[0] * GlowGain;
    light.Green = color[1] * GlowGain;
    light.Blue = color[2] * GlowGain;
    Add(light);
}

auto MCSceneLights::AddGlow(const MCVfxAppearance& appearance, const MCVector3D& position, MCGlowPixels pixels) -> void
{
    if (GHdLighting == 0 || TheState().Drawing.expired())
    {
        return;
    }

    const std::shared_ptr<MCShape> shape = appearance.CurrentShape.lock();

    if (shape == nullptr || !VfxIsShapeTable(shape->FrameList()))
    {
        return;
    }

    // A delta animation draws its first shape and, over it, the frame's change (MCDeltaElement): the change is its fire.
    const std::span<const uint8_t> table = shape->FrameList();
    const int32_t frame = appearance.AppearType != nullptr && appearance.AppearType->Delta
                              ? std::min(appearance.CurrentFrame + 1, VfxShapeCount(table) - 1)
                              : appearance.CurrentFrame;
    const float altitude =
        Terrain() != nullptr ? std::max(position.Z - MCTerrain::GetTerrainElevation(position), 0.0f) : 0.0f;
    AddGlow(table, frame, appearance.ScreenPos.X, appearance.ScreenPos.Y, altitude, pixels);
}

auto MCSceneLights::AddBeam(float x0, float y0, float x1, float y1, float width, uint8_t color) -> void
{
    if (GHdLighting == 0 || TheState().Drawing.expired())
    {
        return;
    }

    AddBeamLight(x0, y0, x1, y1, width, LightOf(color, false).Color);
}

auto MCSceneLights::AddShapeBeam(float x0, float y0, float x1, float y1, float width,
                                 std::span<const uint8_t> shapeTable, int32_t frame) -> void
{
    if (GHdLighting == 0 || TheState().Drawing.expired())
    {
        return;
    }

    if (const std::optional<Glow> glow = MeasureGlow(shapeTable, frame, MCGlowPixels::All); glow.has_value())
    {
        AddBeamLight(x0, y0, x1, y1, width, glow->Color);
    }
}

auto MCSceneLights::Finish() -> void
{
    State& state = TheState();

    if (state.Drawing.expired())
    {
        return;
    }

    if (state.Adding.size() > MaxLights)
    {
        std::ranges::partial_sort(state.Adding, state.Adding.begin() + MaxLights,
                                  [](const MCSceneLight& a, const MCSceneLight& b)
                                  { return Brightness(a) > Brightness(b); });
        state.Adding.resize(MaxLights);
    }

    state.Shown = std::move(state.Adding);
    state.Adding.clear();
    state.ShownWindow = std::exchange(state.Drawing, {});
}

auto MCSceneLights::Shown(const MCWindow& view) -> std::span<const MCSceneLight>
{
    const State& state = TheState();

    if (GHdLighting == 0 || state.ShownWindow.lock().get() != &view)
    {
        return {};
    }

    return state.Shown;
}

auto MCSceneLights::GroundAspect() -> float
{
    return MCTerrain::ProjectionCos != 0.0f ? MCTerrain::ProjectionSin / MCTerrain::ProjectionCos : 1.0f;
}
