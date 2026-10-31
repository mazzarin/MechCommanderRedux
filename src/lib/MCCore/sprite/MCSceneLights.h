#pragma once

#include <cstdint>
#include <memory>
#include <span>

class MCAppearance;
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

/// <summary>Port-only (HD lighting): the air over a fire, which shimmers (composite.pshader's heat haze).</summary>
struct MCSceneHeat
{
    /// <summary>The flames' centre, in the world window's pixels.</summary>
    float X = 0.0f;
    float Y = 0.0f;
    /// <summary>How far to each side of it the air shimmers, and how far above it.</summary>
    float HalfWidth = 0.0f;
    float Rise = 0.0f;
};

namespace MCSceneLights
{
    /// <summary>The most lights a frame shows (the brightest; composite.pshader's MaxLights).</summary>
    inline constexpr size_t MaxLights = 64;
    /// <summary>The most fires whose air shimmers in a frame (the biggest; composite.pshader's MaxHeat).</summary>
    inline constexpr size_t MaxHeat = 16;

    /// <summary>Which of a glow's pixels shine: all of a light's glow, only the fire of an explosion (not its dust).</summary>
    enum class MCGlowPixels
    {
        All,
        Bright,
    };

    /// <summary>How the lights are shown (composite.pshader's lighting and lightTone; the tools' --hd-lighting sets them).</summary>
    struct MCLighting
    {
        /// <summary>Every light's brightness is scaled by this.</summary>
        float Strength = 0.6f;
        /// <summary>The world's own light, 1 as the original drew it; below 1 the scene is dusk and the lights show more.</summary>
        float Ambient = 1.0f;
        /// <summary>Of the light, what the air scatters back even off black ground.</summary>
        float Scatter = 0.01f;
        /// <summary>Lit colours brighter than this (or than the world's own) are eased toward white instead of clipped.</summary>
        float Knee = 0.72f;
        /// <summary>The most light a surface takes: light piling up from many lights eases toward it.</summary>
        float Most = 0.8f;
        /// <summary>A pixel brighter than this is taken for a light itself and keeps its own brightness.</summary>
        float SelfLit = 0.55f;
        /// <summary>
        /// The world's relief: its own shading taken for height, this many of the window's pixels from black to white,
        /// so a surface brighter toward a light faces it and takes more of it (0: every surface lit as flat ground).
        /// </summary>
        float Facing = 24.0f;
        /// <summary>How much of a light a unit's shadow keeps off the ground behind it (0: no shadows).</summary>
        float Shadow = 0.9f;
        /// <summary>
        /// How much more of its hue a light gives what it lights, its brightness kept (0: only its colour as it adds
        /// up): a daylit world, near white already, shows the light's colour where the light alone mostly whitens it.
        /// </summary>
        float Tint = 2.0f;
        /// <summary>The bloom: how much of the world's brightest pixels' light spreads around them (0: none).</summary>
        float Bloom = 1.0f;
        /// <summary>The brightness (of a pixel's brightest component) a pixel blooms above.</summary>
        float BloomFrom = 0.8f;
        /// <summary>The most the air over a fire shimmers, in the window's pixels (0: no heat haze).</summary>
        float Haze = 1.5f;
    };

    /// <summary>The settings the next frames are shown with (the tools set them; DUSK takes its own, see Shown).</summary>
    MCLighting& Lighting();

    /// <summary>The settings the frames are shown with: <see cref="Lighting"/>'s, at DUSK its strength and dimming.</summary>
    MCLighting ShownLighting();

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

    /// <summary>How long a gun's muzzle flash lasts, in seconds of the game's time.</summary>
    inline constexpr float MuzzleFlashSeconds = 0.15f;

    /// <summary>
    /// The flash of a gun fired from <paramref name="muzzle"/> (in the world), <paramref name="left"/> of its
    /// <see cref="MuzzleFlashSeconds"/> still to go: a short warm light, fading, on the shooter and the ground by it.
    /// Flashes from one muzzle at once (a salvo's) light as one.
    /// </summary>
    void AddMuzzleFlash(const MCVector3D& muzzle, float left);

    /// <summary>
    /// A unit that casts shadows from the lights by it (a mech, a vehicle): <paramref name="appearance"/> as it last
    /// placed itself on the screen (its bounds, its owner's place), its owner at <paramref name="position"/>.
    /// </summary>
    void AddCaster(const MCAppearance& appearance, const MCVector3D& position);

    /// <summary>
    /// Adds the units' shadows from the frame's lights to the draw list (MCCamera::Render, once the objects have added
    /// their lights and units, before the list is sorted): drawn after the ground and its craters and before every
    /// object, so a unit covers its shadow on the ground behind it, and one standing in another's shadow takes it.
    /// </summary>
    void AddShadows();

    /// <summary>
    /// The air over the fire of the frame <paramref name="appearance"/> last drew (its Render): it shimmers above the
    /// frame's bright pixels, as wide and as high as they are big.
    /// </summary>
    void AddHeat(const MCVfxAppearance& appearance);

    /// <summary>Ends the frame's lights (MCCamera::Render, once the world is drawn): they are what the window shows.</summary>
    void Finish();

    /// <summary>
    /// The lights the world window <paramref name="view"/> shows (its last finished frame's, in its pixels, the brightest
    /// <see cref="MaxLights"/>); none when HD lighting is off or nothing lit it.
    /// </summary>
    std::span<const MCSceneLight> Shown(const MCWindow& view);

    /// <summary>
    /// The fires whose air shimmers in the world window <paramref name="view"/> (as <see cref="Shown"/>, the biggest
    /// <see cref="MaxHeat"/>).
    /// </summary>
    std::span<const MCSceneHeat> ShownHeat(const MCWindow& view);

    /// <summary>The game's time (ScenarioTime) the last finished frame was drawn at: the haze's, still while paused.</summary>
    float ShownTime();

    /// <summary>
    /// The view's tilt: the screen height a ground distance takes across the screen per screen width it takes along it
    /// (MCTerrain's projection, ProjectionSin / ProjectionCos). A light's pool is an ellipse this much flatter.
    /// </summary>
    float GroundAspect();
}

/// <summary>
/// Port-only: PREFS "HDLighting": 0 draws the world as the original did; 1 lights it around shots, fires and explosions;
/// 2 the same at dusk, the world dimmed and the lights stronger. Off by default.
/// </summary>
extern int GHdLighting;

/// <summary>GHdLighting's values.</summary>
enum MCHdLightingSetting : int
{
    HdLightingOff = 0,
    HdLightingOn = 1,
    HdLightingDusk = 2,
};
