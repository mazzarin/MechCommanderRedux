#pragma once

#include <cstddef>
#include <cstdint>

class MCCamera;
class MCVector3D;
class MCVfxAppearance;
struct MCWindow;

/// <summary>
/// Port-only (HD particles, PREFS "HDParticles"): what flies about a battle, drawn into the world as the GPU draws it
/// (MCRenderer::Particle; the software renderer draws none): flecks of an explosion's fire in its own colours, dirt thrown
/// up by shots into the ground, debris, smoke and embers from big explosions, embers and smoke rising from fires and
/// drifting with the wind, splashes and ripples where shots land in water, and wakes behind units wading through it. They lie in the world, so the
/// lights light them and the view scrolls over them, and units nearer the eye cover them.
/// They never touch the game: they throw their own dice (never the game's), take nothing from it but where things are and
/// change nothing in it. They move with the game's time (ScenarioTime), so they hold still while it's paused.
/// What shakes the ground near the view (a large artillery strike's explosions, a building's end; MCBlastShake) also
/// shakes the picture (PREFS "HDShake", with or without the particles): the presenter's composite moves the world it
/// shows (composite.pshader's shake), never the game's camera, so clicks land where they always did.
/// </summary>
namespace MCParticles
{
    /// <summary>
    /// How an explosion's flecks fly (GHdFlecks): breaking off its fire's edge as it burns, or fewer and heavier, thrown
    /// up out of it once and falling back.
    /// </summary>
    enum class MCFleckStyle : uint8_t
    {
        Edge,
        Falling,
    };

    /// <summary>How the particles and the shake show (the tools' --hd-particles and --hd-shake set them).</summary>
    struct MCParticleLook
    {
        /// <summary>How many particles fly, 1 as made (0.5: half as many).</summary>
        float Amount = 1.0f;
        /// <summary>How far a shaking explosion shakes the picture, 1 as made (0: not at all).</summary>
        float Shake = 1.0f;
    };

    /// <summary>The settings the next frames are drawn with.</summary>
    MCParticleLook& Look();

    /// <summary>The most particles at once: past it, no more are made until some have gone.</summary>
    inline constexpr size_t MaxParticles = 6000;

    /// <summary>
    /// How big a burst is, from the biggest of its explosion's frames: the side of a square of its fire's pixels (its
    /// bright ones), and of all of them (its fire, dust and smoke), in window pixels.
    /// </summary>
    struct MCBurstSize
    {
        float Fire = 0.0f;
        float Extent = 0.0f;
    };

    /// <summary>
    /// An explosion seen for the first time (MCExplosion::Render; a shot's hit or miss, a mine, a unit's or a building's
    /// end): <paramref name="appearance"/> drawn at <paramref name="position"/>. It bursts as big as its frames are, its
    /// flecks in its fire's colours; it shakes the picture when <paramref name="shakes"/> (MCExplosion::Shakes).
    /// </summary>
    void AddExplosion(const MCVfxAppearance& appearance, const MCVector3D& position, bool shakes);

    /// <summary>
    /// An explosion drawn this frame (MCExplosion::Render, each frame it's drawn): with flecks breaking off its fire's
    /// edge (MCFleckStyle::Edge), they break off the frame <paramref name="appearance"/> drew at
    /// <paramref name="position"/>, as many as its edge is long.
    /// </summary>
    void AddExplosionFrame(const MCVfxAppearance& appearance, const MCVector3D& position);

    /// <summary>
    /// A burst at <paramref name="position"/>, as big as <paramref name="size"/>: dirt and dust off the ground, a splash in
    /// water; with a big fire, debris, smoke and embers too; falling flecks (MCFleckStyle::Falling) in the game's fire's
    /// colours. It shakes the picture when <paramref name="shakes"/>.
    /// </summary>
    void AddBurst(const MCVector3D& position, MCBurstSize size, bool shakes);

    /// <summary>
    /// A fire's flame <paramref name="flame"/> burning at <paramref name="position"/> (MCFire::Render): embers and smoke
    /// rise from its bright pixels, as many as they are big.
    /// </summary>
    void AddFire(const MCVfxAppearance& flame, const MCVector3D& position);

    /// <summary>
    /// A unit (a mech, a vehicle) drawn this frame, known by <paramref name="unit"/> (its appearance), standing at
    /// <paramref name="position"/>, its sprite <paramref name="halfWidth"/> window pixels to each side: in water, it
    /// leaves ripples and foam, more as it goes faster.
    /// </summary>
    void AddMover(const void* unit, const MCVector3D& position, float halfWidth);

    /// <summary>
    /// Moves the particles on to the game's time and adds those in <paramref name="camera"/>'s view to the frame's draw list
    /// (MCCamera::Render, once the objects have added theirs, before the list is sorted); makes what this frame's
    /// explosions, fires and units began.
    /// </summary>
    void Draw(const MCCamera& camera);

    /// <summary>Drops every particle and shake, and throws the dice from their start again (a mission's start).</summary>
    void Clear();

    /// <summary>How many particles there are.</summary>
    size_t Count();

    /// <summary>How many of them are flecks.</summary>
    size_t FleckCount();

    /// <summary>How far the shake moves the world shown, in its window's pixels.</summary>
    struct MCShake
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    /// <summary>
    /// How far the shake moves the world window <paramref name="view"/> shows now (its last drawn frame's); none when the
    /// shake is off or another window was drawn. It moves with the game's time, so while the game is paused it holds.
    /// </summary>
    MCShake ShownShake(const MCWindow& view);
}

/// <summary>Port-only: PREFS "HDParticles": 1 draws the particles (MCParticles); 0 none, as the original. Off by default.</summary>
extern int GHdParticles;

/// <summary>
/// Port-only: PREFS "HDFlecks": how explosions' flecks fly, an MCParticles::MCFleckStyle (0, the default: breaking off
/// their fires' edges; 1: falling).
/// </summary>
extern int GHdFlecks;

/// <summary>
/// Port-only: PREFS "HDShake": 1 lets a large artillery strike's explosions and a building's end near the view shake the
/// picture (MCParticles::ShownShake); 0 not, as the original. Off by default.
/// </summary>
extern int GHdShake;
