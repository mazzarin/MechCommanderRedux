#pragma once

#include "platform/MCAddressKey.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

class MCCamera;
class MCTerrain;
class MCTerrainWindow;
class MCVector2D;
class MCVector3D;
class MCVfxAppearance;
struct MCParticleCommand;
struct MCWindow;

/// <summary>
/// Port-only (HD particles, PREFS "HDParticles"): what flies about a battle, drawn into the world as the GPU draws it
/// (MCRenderer::Particle; the software renderer draws none): flecks of an explosion's fire in its own colours, dirt thrown
/// up by shots into the ground, debris, smoke and embers from big explosions, embers and smoke rising from fires and
/// drifting with the wind, splashes and ripples where shots land in water, wakes behind units wading through it and
/// splashes where their feet come down in it. They lie in the world, so the lights light them and the view scrolls over
/// them, and units nearer the eye cover them.
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
        /// <summary>How big a foot's splash in water is, 1 as made (0: none).</summary>
        float Steps = 1.0f;
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
    /// A unit (a mech, a vehicle) drawn this frame, known by <paramref name="unit"/> (its appearance's address), standing at
    /// <paramref name="position"/>, its sprite <paramref name="halfWidth"/> window pixels to each side: in water, it
    /// leaves ripples and foam, more as it goes faster.
    /// </summary>
    void AddMover(MCAddressKey unit, const MCVector3D& position, float halfWidth);

    /// <summary>
    /// A unit's foot coming down at <paramref name="foot"/> (MCBattleMech's footprints, made or not: the water takes none),
    /// its sprite <paramref name="halfWidth"/> window pixels to each side: in the water drawn there (not on a bridge
    /// over it: mechs can walk the water's edge, but no farther), it splashes.
    /// </summary>
    void AddStep(const MCVector3D& foot, float halfWidth);

    /// <summary>
    /// Adds a soft mark (<paramref name="command"/>, in the world pane's pixels) to the frame's draw list at
    /// <paramref name="depth"/>, drawn as a particle is, whatever HD particles' setting: a headlight's lamp
    /// (MCSceneLights::AddHeadlight).
    /// </summary>
    void AddMark(const MCParticleCommand& command, int32_t depth);

    /// <summary>
    /// Adds marks to the draw list as AddMark adds each (<paramref name="commands"/>, each at its
    /// <paramref name="depths"/>), but those at the same depth as one element: drawn the same and in the same order
    /// (the list keeps the order of what lies at the same depth, and nothing else is added between them), for a
    /// fraction of the cost (HD weather's flakes and drops, tens of thousands a frame).
    /// </summary>
    void AddMarks(std::span<const MCParticleCommand> commands, std::span<const int32_t> depths);

    /// <summary>Port-only (HD weather): a part of a frame's marks, made apart: each command at its depth.</summary>
    struct MCMarkPart
    {
        std::span<const MCParticleCommand> Commands;
        std::span<const int32_t> Depths;
    };

    /// <summary>
    /// <see cref="AddMarks"/> of the marks <paramref name="parts"/> hold, one part's after another's, as if they were
    /// made together in that order (the HD weather's marks, made in parts by MCWeather::Draw's helpers).
    /// </summary>
    void AddMarks(std::span<const MCMarkPart> parts);

    /// <summary>
    /// Moves the particles on to the game's time and adds those in <paramref name="camera"/>'s view to the frame's draw list
    /// (MCCamera::Render, once the objects have added theirs, before the list is sorted); makes what this frame's
    /// explosions, fires and units began.
    /// </summary>
    void Draw(const MCCamera& camera);

    /// <summary>
    /// Whether the ground drawn at world (<paramref name="x"/>, <paramref name="y"/>) in the main view is the water's,
    /// with no bridge over it.
    /// </summary>
    bool WaterAt(float x, float y);

    /// <summary>Drops every particle and shake, and throws the dice from their start again (a mission's start).</summary>
    void Clear();

    /// <summary>
    /// Port-only: measures how the bursts of every explosion whose type is loaded look (its shapes' fire, as the first of
    /// each to show would), so that none is measured in a mission's frame: at a mission's load, after Clear (the
    /// weapons' hits and misses and the units' own explosions load with the units). Kept by each shape's HD folder
    /// (MCShape::HdFolder), which a shape loaded again keeps. Does nothing while no burst is wanted.
    /// </summary>
    void MeasureExplosions();

    /// <summary>Port-only: how many bursts' looks have been measured, and how many explosions' bursts were begun.</summary>
    size_t LooksMeasured();
    size_t BurstsBegun();

    /// <summary>How many particles there are.</summary>
    size_t Count();

    /// <summary>
    /// World point (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>) in <paramref name="camera"/>'s view,
    /// as ProjectToScreen puts it but to the pixel's fraction (the particles' own placing, for MCWeather's drops).
    /// </summary>
    MCVector2D ViewPoint(float x, float y, float z, const MCCamera& camera);

    /// <summary>The ground's height at world point (<paramref name="x"/>, <paramref name="y"/>) (0 without a terrain).</summary>
    float GroundAt(float x, float y);

    /// <summary>
    /// The palette entry of the ground drawn at world point (<paramref name="x"/>, <paramref name="y"/>,
    /// <paramref name="z"/>): the pixel there of the last tile <paramref name="camera"/>'s terrain window drew over it
    /// (each block's at its top-left corner, row by row, as MCTerrainBlock::Draw); none outside the window's tiles. (The
    /// map's own water, its tiles' terrain types from FirstWaterTerrain on, lies a tile or so off the water drawn, too
    /// far for a splash, and some of those types are paved.)
    /// </summary>
    std::optional<uint8_t> GroundDrawnAt(float x, float y, float z, const MCCamera& camera);

    /// <summary>What is drawn at a pixel of the ground (<see cref="DrawnRound"/>).</summary>
    struct MCDrawnPixel
    {
        /// <summary>The ground's own palette entry there (<see cref="GroundDrawnAt"/>'s).</summary>
        std::optional<uint8_t> Ground;
        /// <summary>
        /// With what is laid on the ground: the pixel of the last overlay the terrain window drew over it (roads,
        /// runways, bridges' decks, walls; MCTerrainBlock::DrawOverlay) where one covers it, else <see cref="Ground"/>.
        /// Translucent overlays are taken as covering nothing.
        /// </summary>
        std::optional<uint8_t> Surface;
    };

    /// <summary>
    /// What is drawn round world point (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>) in
    /// <paramref name="camera"/>'s view: <paramref name="pixels"/>[k], the pixel <paramref name="offsets"/>[k] right
    /// and down of where the point is drawn (as many as both have).
    /// </summary>
    void DrawnRound(float x, float y, float z, const MCCamera& camera,
                    std::span<const std::pair<int32_t, int32_t>> offsets, std::span<MCDrawnPixel> pixels);

    /// <summary>
    /// Port-only (HD weather): the ground a camera's view draws, as <see cref="GroundDrawnAt"/> and
    /// <see cref="DrawnRound"/> find it, its terrain window and the terrain held once for many lookups rather than taken
    /// at each. MCWeather finds the ground under a frame's landing drops through one on several threads at once: taken
    /// at each lookup, their shared owners' counts went up and down on every thread, which held the threads up.
    /// </summary>
    class MCDrawnGround
    {
    public:
        /// <summary>The ground <paramref name="camera"/>'s view draws now (its window as it stands).</summary>
        explicit MCDrawnGround(const MCCamera& camera);

        /// <summary><see cref="GroundDrawnAt"/> of world point (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>).</summary>
        std::optional<uint8_t> At(float x, float y, float z) const;

        /// <summary>
        /// <see cref="DrawnRound"/> of world point (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>).
        /// </summary>
        void Round(float x, float y, float z, std::span<const std::pair<int32_t, int32_t>> offsets,
                   std::span<MCDrawnPixel> pixels) const;

        /// <summary>
        /// Reads in every tile and overlay the window draws that isn't read yet (the tile cache reads them as they're
        /// first looked up), so <see cref="At"/> and <see cref="Round"/> change nothing and only read from then on.
        /// </summary>
        void ReadTiles() const;

    private:
        const MCCamera& _Camera;
        std::shared_ptr<MCTerrainWindow> _Window;
        std::shared_ptr<MCTerrain> _Terrain;
    };

    /// <summary>
    /// Whether a bridge spans the map's tile at world point (<paramref name="x"/>, <paramref name="y"/>): its road is
    /// drawn over the water (<see cref="GroundDrawnAt"/>'s ground under it).
    /// </summary>
    bool BridgedAt(float x, float y);

    /// <summary>
    /// Whether palette entry <paramref name="index"/> is the water's (composite.pshader's IsWater): its blues, and the
    /// colour cycle's.
    /// </summary>
    bool IsWaterColor(uint8_t index);

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
