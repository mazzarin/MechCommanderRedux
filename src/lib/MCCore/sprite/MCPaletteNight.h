#pragma once

// Port-only (Redux HD): the night drawn through the palette, where HD lighting doesn't draw it (HD LIGHTING off, or the
// CPU renderer, which can't draw it), so a mission played at night looks it on every machine. The world is dimmed
// through the original's own darkening tables (the palette's second haze set: the pause darkens the screen with one of
// them), and a lit unit's headlight lifts the dimming where its beam falls, by HD lighting's own beam
// (MCSceneLights::GroundLight): its pool on the ground shows the art as drawn, fading back into the night at its edges
// and far end, and its beam in the air, from the lamp to the pool, lifts most of it.
// The units' overlays (their bars, the selection) are drawn over the world afterwards, so they stay as bright.

#include "sprite/MCSceneLights.h"
#include "vfx/MCVfx.h"

#include <cstdint>
#include <span>
#include <vector>

struct MCPane;
struct MCTableFit;

namespace MCPaletteNight
{
    /// <summary>
    /// The darkening table the night dims the world through, of the palette's 16 (1 the faintest): 42% of the day's
    /// brightness. HD lighting's night shows 35%, but lit by its moon's blue and its lights' glow.
    /// </summary>
    inline constexpr int32_t NightDim = 13;

    /// <summary>
    /// A rectangle of the world (inclusive, in its pane's coordinates) and the darkening table it's dimmed through: 1 to
    /// <see cref="NightDim"/>, or 0 for none (the art as drawn).
    /// </summary>
    struct MCShade
    {
        MCRect Rect;
        int32_t Dim = NightDim;
    };

    /// <summary>
    /// Whether the night is drawn this way now: the mission plays at night (MCSceneLights::PlayedTime) and HD lighting
    /// doesn't draw it (MCSceneLights::LightingDrawn).
    /// </summary>
    bool Shown();

    /// <summary>
    /// The darkening table a point takes whose night a beam lifts by <paramref name="lift"/>: the night's at 0 (in the
    /// dark), none at 1 (the ground lit as where a headlight's beam's middle meets the ground, or more).
    /// </summary>
    int32_t DimFor(float lift);
    /// <summary>
    /// How the night dims a colour where no beam lifts it (the palette's <see cref="NightDim"/> table as the GPU draws it
    /// over colours that aren't the palette's, MCTableFit): for what the composite lays over the world after it's
    /// dimmed (HD weather's specks). None when the night isn't drawn this way (<see cref="Shown"/>) or can't be.
    /// </summary>
    std::optional<MCTableFit> NightFit();

    /// <summary>
    /// How a world <paramref name="width"/> by <paramref name="height"/> pixels is dimmed for the night, lit by
    /// <paramref name="beams"/> (headlights' beams, MCSceneLights::MCHeadlight::Beam, in its pixels; along the ground a
    /// screen height <paramref name="aspect"/> of one): rectangles covering it, none overlapping, each with its table.
    /// </summary>
    std::vector<MCShade> Shades(int32_t width, int32_t height, std::span<const MCSceneLight> beams, float aspect);

    /// <summary>Starts the world's frame: no beams yet.</summary>
    void Begin();

    /// <summary>Adds a lit unit's beam (from MCSceneLights::AddHeadlight: a unit being drawn, so one the player sees).</summary>
    void AddBeam(const MCSceneLight& beam);

    /// <summary>
    /// Dims the world just drawn into <paramref name="pane"/> for the night, lifted in the frame's beams, when the night
    /// is drawn this way (<see cref="Shown"/>); then forgets the beams.
    /// </summary>
    void Draw(const MCPane& pane);
} // namespace MCPaletteNight
