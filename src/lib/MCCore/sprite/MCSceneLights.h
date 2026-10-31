#pragma once

#include <cstdint>
#include <memory>
#include <span>

class MCVector3D;
class MCVfxAppearance;
struct MCPane;
struct MCWindow;

/// <summary>
/// Port-only (HD lighting, PREFS "HDLighting"): a light the world view casts this frame. The original draws a light's
/// glow (MCLight) and a laser's beam but lights nothing around them; the presenter's composite (composite.pshader) adds
/// these lights to the world under the interface, so the ground and the units by a shot, a fire or an explosion take
/// its colour.
/// </summary>
struct MCSceneLight
{
    /// <summary>The ground under the light, in the world window's pixels: a point, or a beam from (X0, Y0) to (X1, Y1).</summary>
    float X0 = 0.0f;
    float Y0 = 0.0f;
    float X1 = 0.0f;
    float Y1 = 0.0f;
    /// <summary>How far above that ground the light is, in the window's pixels: the higher, the softer its pool.</summary>
    float Height = 0.0f;
    /// <summary>The ground distance (in the window's pixels across the screen) at which it has faded out.</summary>
    float Reach = 0.0f;
    /// <summary>Its colour times its strength: 1 doubles a lit surface right under it.</summary>
    float Red = 0.0f;
    float Green = 0.0f;
    float Blue = 0.0f;
};

namespace MCSceneLights
{
    /// <summary>The most lights a frame shows (the brightest; composite.pshader's MaxLights).</summary>
    inline constexpr size_t MaxLights = 64;

    /// <summary>Which of a glow's pixels shine: all of a light's glow, only the fire of an explosion (not its dust).</summary>
    enum class MCGlowPixels
    {
        All,
        Bright,
    };

    /// <summary>How the lights are shown (the tools' --hd-lighting sets them).</summary>
    struct MCLighting
    {
        /// <summary>Every light's brightness is scaled by this.</summary>
        float Strength = 1.0f;
        /// <summary>The world's own light, 1 as the original drew it; below 1 the scene is dusk and the lights show more.</summary>
        float Ambient = 1.0f;
    };

    /// <summary>The settings the next frames are shown with.</summary>
    MCLighting& Lighting();

    /// <summary>
    /// Starts the lights of the world drawn into <paramref name="pane"/> (MCCamera::Render, as its draw list starts).
    /// The lights added until <see cref="Finish"/> are in its window's pixels, as the game draws the world (its pane,
    /// MCViewWindow's, starts at the window's corner).
    /// </summary>
    void Begin(const MCPane& pane);

    /// <summary>Adds a light.</summary>
    void Add(MCSceneLight light);

    /// <summary>
    /// The light of a glow: frame <paramref name="frame"/> of <paramref name="shapeTable"/> drawn with its hot spot at
    /// (<paramref name="x"/>, <paramref name="y"/>), <paramref name="altitude"/> pixels above the ground under
    /// it. It shines from its <paramref name="pixels"/>' centre, in their mean colour, as far as their size: a glow that
    /// grows or fades lights more or less. Nothing for a table that isn't a VFX shape table (a delta animation's).
    /// </summary>
    void AddGlow(std::span<const uint8_t> shapeTable, int32_t frame, float x, float y, float altitude,
                 MCGlowPixels pixels);

    /// <summary>
    /// The light of the frame <paramref name="appearance"/> last drew (its Render), as <see cref="AddGlow"/>'s: its owner
    /// at <paramref name="position"/>, as high above the ground there.
    /// </summary>
    void AddGlow(const MCVfxAppearance& appearance, const MCVector3D& position, MCGlowPixels pixels);

    /// <summary>
    /// The light of a beam from (<paramref name="x0"/>, <paramref name="y0"/>) to (<paramref name="x1"/>,
    /// <paramref name="y1"/>), <paramref name="width"/> pixels to each side, in palette colour
    /// <paramref name="color"/>.
    /// </summary>
    void AddBeam(float x0, float y0, float x1, float y1, float width, uint8_t color);

    /// <summary>
    /// A beam as <see cref="AddBeam"/>'s, in the mean colour of frame <paramref name="frame"/> of
    /// <paramref name="shapeTable"/> (a PPC's bolt, stretched along it).
    /// </summary>
    void AddShapeBeam(float x0, float y0, float x1, float y1, float width, std::span<const uint8_t> shapeTable,
                      int32_t frame);

    /// <summary>Ends the frame's lights (MCCamera::Render, once the world is drawn): they are what the window shows.</summary>
    void Finish();

    /// <summary>
    /// The lights the world window <paramref name="view"/> shows (its last finished frame's, in its pixels, the brightest
    /// <see cref="MaxLights"/>); none when HD lighting is off or nothing lit it.
    /// </summary>
    std::span<const MCSceneLight> Shown(const MCWindow& view);

    /// <summary>
    /// The view's tilt: the screen height a ground distance takes across the screen per screen width it takes along it
    /// (MCTerrain's projection, ProjectionSin / ProjectionCos). A light's pool is an ellipse this much flatter.
    /// </summary>
    float GroundAspect();
}

/// <summary>Port-only: light the world around shots, fires and explosions (PREFS "HDLighting"; off by default).</summary>
extern int GHdLighting;
