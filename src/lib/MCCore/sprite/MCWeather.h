#pragma once

#include "sprite/MCSceneLights.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

class MCCamera;
class MCFitIniFile;
class MCVector3D;

/// <summary>
/// Port-only (HD weather): the weather a mission is played in (a map maker's choice, its scenario file's [Environment]
/// Weather; see MCWeather::Read). The original's missions are all clear.
/// </summary>
enum class MCWeatherKind : uint8_t
{
    Clear,
    Rain,
    Downpour,
};

/// <summary>
/// Port-only (HD weather, PREFS "HDWeather"): rain over the battle, drawn by the GPU (none with the software renderer).
/// The sky clouds over (the world's own light dimmed and greyed, wet land darker, a grey veil drifting over it in sheets
/// before the wind; no sun: its shadows faint, its relief and the water's glints gone), drops fall slanted before the
/// wind's gusts and splash where they land (droplets on land, rings on the water drawn and in puddles, on a unit's top
/// when it stands in their way), and water drips off the units. With the HD ground's relief maps (MCSceneLights::
/// ReliefMapsOn, which tell the ground from what stands on it) the world is wet too: puddles lie in the ground's low
/// spots (not on grass, which soaks them up), wet ground mirrors the sky, units glisten, and a foot coming down in a
/// puddle sprays it up and the puddle reacts round it. The sky, the sheen and the puddles are the presenter's
/// composite's (composite.pshader); the drops are drawn into the world as the particles are (MCParticles::AddMark), so
/// units nearer the eye cover those falling behind them.
/// Like the particles, the drops never touch the game: they throw their own dice (never the game's), take nothing from
/// it but where things are and change nothing in it, and move with the game's time (ScenarioTime).
/// </summary>
namespace MCWeather
{
    /// <summary>How a weather shows.</summary>
    struct MCWeatherLook
    {
        /// <summary>How much of the world's own light the overcast takes, 0 to 1 (0: a clear sky, the sun's).</summary>
        float Dark = 0.0f;
        /// <summary>How much of their colour the world's own colours lose under it, 0 to 1.</summary>
        float Grey = 0.0f;
        /// <summary>How much darker wet land shows (the water as it is), 0 to 1.</summary>
        float Wet = 0.0f;
        /// <summary>How much of a rain-grey veil hangs over the world, drifting in sheets (0 none).</summary>
        float Veil = 0.0f;
        /// <summary>Drops landing a second on a square of the view 100 of its pixels a side (0: none fall).</summary>
        float Drops = 0.0f;
        /// <summary>Of the drops, the shares far from the eye (thin, faint), between and near it (thick, bright).</summary>
        std::array<float, 3> Sizes{};
        /// <summary>The share of the ground puddles lie on, 0 to 1.</summary>
        float Puddles = 0.0f;
        /// <summary>How much of the sky flat wet ground mirrors, 0 to 1 (more on slopes turned from the eye).</summary>
        float Sheen = 0.0f;
        /// <summary>How much of the sky the bright metal of what stands on the ground (units, buildings) mirrors.</summary>
        float Gloss = 0.0f;
        /// <summary>Drips a second falling from a unit's lower edges.</summary>
        float Drips = 0.0f;
        /// <summary>
        /// How loud its rain is heard (the HD audio pack's rain loop, MCSoundSystem::UpdateRain), as a channel volume (its
        /// gain that squared): 0 none, 1 the loop as levelled.
        /// </summary>
        float Heard = 0.0f;
        /// <summary>
        /// Lightning strikes a minute, on average, out of the view (0 none): each lights the world in a flicker
        /// (<see cref="Flash"/>), and its thunder (the HD audio pack's, MCSoundSystem::UpdateThunder) is heard a moment
        /// after, the sooner, louder and higher the nearer it struck.
        /// </summary>
        float Strikes = 0.0f;
        /// <summary>How much the nearest strike's flash lights the world, 0 to 1 (composite.pshader's FlashLight in full).</summary>
        float Flash = 0.0f;
        /// <summary>How loud the nearest strike's thunder is heard, as a channel volume, as <see cref="Heard"/>.</summary>
        float ThunderHeard = 0.0f;
    };

    /// <summary>How <paramref name="kind"/> shows, as made (a clear sky: nothing).</summary>
    MCWeatherLook LookOf(MCWeatherKind kind);

    /// <summary>How the weather shows beyond its look (the tools' --hd-weather sets it).</summary>
    struct MCWeatherSettings
    {
        /// <summary>How many drops fall, 1 as made (0.5: half as many).</summary>
        float Amount = 1.0f;
        /// <summary>How big a foot's splash in a puddle is, 1 as made (0: none; the tools' --hd-steps sets it).</summary>
        float Steps = 1.0f;
        /// <summary>How loud the thunder is heard, 1 as made (0: none; the tools' --hd-thunder sets it).</summary>
        float Thunder = 1.0f;
        /// <summary>How brightly lightning flashes, 1 as made (0: none; the tools' --hd-lightning sets it).</summary>
        float Flash = 1.0f;
        /// <summary>How often lightning strikes, 1 as made (the tools' --hd-lightning sets it).</summary>
        float Strikes = 1.0f;
    };

    /// <summary>A strike's thunder: how far off it struck (0 the nearest, 1 the farthest) and to which side (-1 to 1).</summary>
    struct MCThunderClap
    {
        float Far = 0.0f;
        float Side = 0.0f;
    };

    /// <summary>
    /// The lightning lighting the world: how much (0 none, 1 the nearest strike's whole flash, MCWeatherLook::Flash of
    /// composite.pshader's FlashLight) and from which side of the view (-1 left to 1 right).
    /// </summary>
    struct MCFlash
    {
        float Light = 0.0f;
        float Side = 0.0f;
    };

    /// <summary>The most feet's puddles the composite shows reacting at once, the newest (its MaxPuddleSteps).</summary>
    constexpr size_t MaxPuddleSteps = 8;

    /// <summary>The circles a footprint's shape is taken as, its ripple spreading from their outline (its PrintCircles).</summary>
    constexpr size_t PrintCircles = 4;

    /// <summary>A circle on the ground as the composite takes it: its middle (meters east and north) and its radius.</summary>
    struct MCGroundCircle
    {
        float East = 0.0f;
        float North = 0.0f;
        float Radius = 0.0f;
    };

    /// <summary>
    /// A foot come down in a puddle, which reacts round it (composite.pshader's SteppedPuddleAt): its ground point as the
    /// composite takes it (meters east and north of the map's middle), how long ago it came down (seconds of the game's
    /// time), how big its splash is (MCWeatherSettings::Steps), and its footprint's shape, as circles.
    /// </summary>
    struct MCPuddleStep
    {
        float East = 0.0f;
        float North = 0.0f;
        float Age = 0.0f;
        float Strength = 0.0f;
        std::array<MCGroundCircle, PrintCircles> Print{};
    };

    /// <summary>The settings the next frames are shown with.</summary>
    MCWeatherSettings& Settings();

    /// <summary>
    /// Sets the weather of the mission being played (MCScenario::Load: its scenario file's, see <see cref="Read"/>; the
    /// tools' --hd-weather).
    /// </summary>
    void SetMissionWeather(MCWeatherKind weather);

    /// <summary>The weather of the mission being played (clear until one is set).</summary>
    MCWeatherKind MissionWeather();

    /// <summary>
    /// The weather the world is shown in: with HD weather off (GHdWeather), clear; at its RAIN or DOWNPOUR, that; else
    /// the mission's own (<see cref="MissionWeather"/>).
    /// </summary>
    MCWeatherKind Shown();

    /// <summary>How the weather shown looks (<see cref="LookOf"/> of <see cref="Shown"/>).</summary>
    MCWeatherLook ShownLook();

    /// <summary>
    /// How loud the rain shown is heard (<see cref="MCWeatherLook::Heard"/>): none while none is drawn (a clear sky,
    /// HD weather off, the software renderer).
    /// </summary>
    float Heard();

    /// <summary>
    /// How the weather shown is heard: its look (<see cref="ShownLook"/>, its thunder as loud as <see
    /// cref="MCWeatherSettings::Thunder"/> says) while it is drawn, else a clear sky's (nothing heard).
    /// </summary>
    MCWeatherLook HeardLook();

    /// <summary>The lightning's flash at the game's time the weather was drawn at last (none while none strikes).</summary>
    MCFlash ShownFlash();

    /// <summary>
    /// The thunder of a strike whose sound has come (MCWeatherLook::Strikes: a moment after its flash), taken once; none
    /// while none is due. One not taken soon after it came (the sound off) is dropped.
    /// </summary>
    std::optional<MCThunderClap> TakeThunder();

    /// <summary>The weather named <paramref name="name"/> ("clear", "rain" or "downpour", in any case); none for another.</summary>
    std::optional<MCWeatherKind> Named(std::string_view name);

    /// <summary>
    /// The weather a scenario file (<paramref name="file"/>) sets its mission in: its block "Environment"'s string
    /// "Weather" (Clear, Rain or Downpour), clear without one, as the original's missions are. The original game reads
    /// neither, so a mission that sets it plays there as ever, under a clear sky.
    /// </summary>
    MCWeatherKind Read(MCFitIniFile& file);

    /// <summary>
    /// A unit (a mech, a vehicle, an elemental) drawn this frame: its <paramref name="parts"/> as drawn (in the camera's
    /// view), standing at <paramref name="position"/> in the world, at (<paramref name="feetX"/>,
    /// <paramref name="feetY"/>) in the view, its parts at draw list depth <paramref name="depth"/>. Drops falling where
    /// it stands land on its top, and water drips off its lower edges.
    /// </summary>
    void AddUnit(std::span<const MCSceneLights::MCSunShadowPart> parts, const MCVector3D& position, float feetX,
                 float feetY, int32_t depth);

    /// <summary>
    /// A unit's foot coming down at <paramref name="foot"/> (MCBattleMech's footprints, made or not): in a puddle, it
    /// sprays droplets up and the puddle reacts round it, from the outline of the footprint it leaves (shape table
    /// <paramref name="print"/>'s frame <paramref name="printFrame"/>, MCCraterManager's; a small round one without).
    /// </summary>
    void AddStep(const MCVector3D& foot, std::span<const uint8_t> print = {}, int32_t printFrame = -1);

    /// <summary>
    /// Whether a foot coming down at world point <paramref name="point"/> lands in a puddle, as <paramref name="camera"/>
    /// shows the ground there: none but in rain shown with the wet look, nor on a bridge.
    /// </summary>
    bool PuddleAt(const MCVector3D& point, const MCCamera& camera);

    /// <summary>The feet's puddles reacting now (the newest MaxPuddleSteps), oldest first.</summary>
    std::span<const MCPuddleStep> ShownPuddleSteps();

    /// <summary>
    /// Moves the drops on to the game's time and adds those in <paramref name="camera"/>'s view to the frame's draw list
    /// (MCCamera::Render, once the objects have added theirs, before the list is sorted); throws this frame's.
    /// </summary>
    void Draw(const MCCamera& camera);

    /// <summary>Drops every drop and splash, and throws the dice from their start again (a mission's start).</summary>
    void Clear();

    /// <summary>How many drops are falling, and splashes and drips showing.</summary>
    size_t Count();

    /// <summary>
    /// The puddles' level (composite.pshader's PuddleField under it is a puddle) that leaves <paramref name="share"/> of
    /// the ground under them, measured once.
    /// </summary>
    float PuddleLevel(float share);
}

/// <summary>
/// Port-only: PREFS "HDWeather": 0 shows every mission under a clear sky, as the original; 1 in the mission's own
/// weather (MCWeather::MissionWeather: clear, but for a mission set in rain); 2 every mission in rain; 3 every mission
/// in a downpour. Off by default.
/// </summary>
extern int GHdWeather;

/// <summary>GHdWeather's values.</summary>
enum MCHdWeatherSetting : int
{
    HdWeatherOff = 0,
    HdWeatherOn = 1,
    HdWeatherRain = 2,
    HdWeatherDownpour = 3,
};
