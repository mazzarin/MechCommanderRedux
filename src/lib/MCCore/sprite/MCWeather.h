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
/// wind's gusts and splash where they land as the ground there takes them (MCSplashGround: droplets off rock and roads,
/// a dark dot soaking into soil, a soft puff in grass, rings on the water drawn and in puddles, a glint on a unit's
/// metal top when it stands in their way), and water drips off the units. With the HD ground's relief maps
/// (MCSceneLights::ReliefMapsOn, which tell the ground from what stands on it) the world is wet too: puddles lie in the
/// ground's low spots (not on grass, which soaks them up), wet ground mirrors the sky, units glisten, and a foot coming
/// down in a puddle sprays it up and the puddle reacts round it. The sky, the sheen and the puddles are the presenter's
/// composite's (composite.pshader); the drops are drawn into the world as the particles are (MCParticles::AddMark), so
/// units nearer the eye cover those falling behind them. With HD lighting, the drops and the droplets they throw up
/// catch the light of the lights they fall past (MCWeatherSettings::Light): drawn into the GPU world's glints, which the
/// composite adds over the lit world, so the night doesn't dim it.
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
        /// <summary>
        /// How far its showers come and go (0: it falls steadily): through a mission the rain builds and eases over
        /// minutes, in a lull falling this share of its drops fewer, in a burst this share more (the rest of its look
        /// following, see <see cref="LookAt"/>); its puddles spread as it falls, from none at a mission's start.
        /// </summary>
        float Showers = 0.0f;
    };

    /// <summary>How <paramref name="kind"/> shows, as made (a clear sky: nothing), steadily (its showers left out).</summary>
    MCWeatherLook LookOf(MCWeatherKind kind);

    /// <summary>
    /// What a drop lands on, which says how it splashes (with MCWeatherSettings::Ground above 0): hard ground (rock,
    /// roads, concrete, packed earth) throws droplets up, crisply; soil (dirt, ploughed fields) takes it with a dark
    /// dot as it soaks in, and grass with a soft puff among its blades, neither throwing droplets; water and puddles
    /// ring; a unit's metal top glints a moment, throwing droplets as hard ground does.
    /// </summary>
    enum class MCSplashGround : uint8_t
    {
        Hard,
        Soil,
        Grass,
        Water,
        Metal,
    };

    /// <summary>How the weather shows beyond its look (the tools' --hd-weather and --hd-showers set it).</summary>
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
        /// <summary>
        /// How far the showers come and go (MCWeatherLook::Showers), 1 as made (0: the rain falls steadily, its puddles
        /// as made from the start).
        /// </summary>
        float Showers = 1.0f;
        /// <summary>How fast the showers come and go and the puddles spread, 1 as made (8: eight times as fast).</summary>
        float ShowerPace = 1.0f;
        /// <summary>
        /// How much the drops, and the droplets they throw up, catch the lights they fall past (HD lighting's: a fire,
        /// a shot's flash, an explosion, a headlight's beam), mostly at dusk and night, 1 as made (0: none, each lit as
        /// what lies behind it; the tools' --hd-rain-light sets it). With it on, the rain's splashes are never so pale
        /// at dusk and night that the composite takes them for a light's own pixels: away from the lights they stay as
        /// dim as the night.
        /// </summary>
        float Light = 1.0f;
        /// <summary>
        /// How strongly drops splash as the ground they land on takes them (MCSplashGround), 1 as made (0: every land
        /// alike, droplets thrown up, as the rain first fell; the tools' --hd-splashes sets it).
        /// </summary>
        float Ground = 1.0f;
    };

    /// <summary>How many of each piece the splashes showing are made of.</summary>
    struct MCSplashPieces
    {
        /// <summary>Droplets thrown up (off hard ground and units' tops, out of puddles under a foot).</summary>
        size_t Droplets = 0;
        /// <summary>Dots where drops land (on hard ground, in water).</summary>
        size_t Dots = 0;
        /// <summary>Rings spreading on the water drawn and in puddles.</summary>
        size_t Rings = 0;
        /// <summary>Dark dots soaking into soil.</summary>
        size_t Soaks = 0;
        /// <summary>Soft puffs in grass.</summary>
        size_t Puffs = 0;
        /// <summary>Glints on units' metal tops.</summary>
        size_t Glints = 0;
        /// <summary>Drips falling off units.</summary>
        size_t Drips = 0;
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

    /// <summary>
    /// A weather's showers at a moment: how hard its rain falls (1 as made; less in a lull, more in a burst) and how far
    /// its puddles have spread (0 none, at a mission's start; 1 as made).
    /// </summary>
    struct MCShowers
    {
        float Rain = 1.0f;
        float Puddles = 1.0f;
    };

    /// <summary>
    /// <paramref name="kind"/>'s showers at the game's time <paramref name="time"/> (ScenarioTime): the rain building
    /// and easing by its own noise on the game's time (never the game's dice), the same every time a mission is played;
    /// the puddles filling toward how hard it falls, and drying slowly in a lull. Steady (as made) for a weather without
    /// showers, or with MCWeatherSettings::Showers 0.
    /// </summary>
    MCShowers ShowersAt(MCWeatherKind kind, float time);

    /// <summary>
    /// How <paramref name="kind"/> shows at the game's time <paramref name="time"/>: as made (<see cref="LookOf"/>),
    /// its showers' rain (<see cref="ShowersAt"/>) setting how many drops fall, how many of them near the eye, how
    /// thick the veil is, how dark and grey the overcast (less), how much drips and how loud it is heard; its puddles
    /// how much of the ground they lie on and how much wet ground mirrors the sky (damp ground less).
    /// </summary>
    MCWeatherLook LookAt(MCWeatherKind kind, float time);

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
    /// How much of the light at them (MCSceneLights::LightInAir) the drops catch at the time of day shown: the lights'
    /// strength (MCSceneLights::ShownLighting) times <see cref="MCWeatherSettings::Light"/>, all of it from dusk's
    /// world light down, a quarter by day, when the light shows little on the sky's.
    /// </summary>
    float CatchGain();

    /// <summary>
    /// The light a drop shows (a glint: added over the lit world, as it is) from <paramref name="caught"/>, the light
    /// at it, times <paramref name="gain"/> (<see cref="CatchGain"/>) and its <paramref name="facet"/> (0 to 1: how
    /// much its facets turn the light to the eye): in the lights' hue (further from grey), its brightest component in a
    /// palette ramp's few steps (none under the first: the drops by a light's edge stay dim).
    /// </summary>
    std::array<float, 3> Glint(const std::array<float, 3>& caught, float gain, float facet);

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

    /// <summary>How the weather shown looks now (<see cref="LookAt"/> of <see cref="Shown"/>, at ScenarioTime).</summary>
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
    /// The ground land drawn in palette entry <paramref name="index"/> is taken for: grass by its green (which the
    /// puddles leave dry too), soil by a brown's strong colour (its saturation: the dirt's and the fields' browns and
    /// tans), else hard ground (the rock's, the roads', concrete's and packed earth's greys and grey browns); the
    /// water's colours (MCParticles::IsWaterColor) are water.
    /// </summary>
    MCSplashGround GroundOf(uint8_t index);

    /// <summary>
    /// What a drop landing at world point <paramref name="point"/> (not on a unit) splashes on, as <paramref
    /// name="camera"/> shows the ground there: water where the water is drawn (not under a bridge) or a puddle lies,
    /// else what <see cref="GroundOf"/> takes most of the surface drawn round it for (MCParticles::DrawnRound's
    /// surface: the overlays laid on the ground, such as roads, over its tiles; a few pixels round it vote, as the
    /// dirt's art is speckled with the rock's colours); hard ground where none is drawn.
    /// </summary>
    MCSplashGround GroundAt(const MCVector3D& point, const MCCamera& camera);

    /// <summary>
    /// A drop of size <paramref name="size"/> (0 far from the eye, 1 between, 2 near) landing at world point
    /// <paramref name="point"/> as the game's time next goes on (on a unit's top when one stands in its way), splashing
    /// there: the tools' and tests' own drops (the rain's are thrown by <see cref="Draw"/>).
    /// </summary>
    void AddDrop(const MCVector3D& point, uint8_t size);

    /// <summary>How many of each piece the splashes showing now are made of.</summary>
    MCSplashPieces Pieces();

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
