#include "stdafx.h"
#include "sprite/MCWeather.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCVector2D.h"
#include "lib/MCVector3D.h"
#include "lib/MCWorkers.h"
#include "main/MCMissionGlobals.h"
#include "platform/MCRenderer.h"
#include "sprite/MCPaletteNight.h"
#include "sprite/MCParticles.h"
#include "sprite/MCWeatherPace.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

#include <deque>
#include <map>
#include <numbers>
#include <random>
#include <utility>

int GHdWeather = 0;

namespace
{
    /// <summary>The weathers by name (a scenario file's Weather, the tools' --hd-weather).</summary>
    constexpr std::array<std::pair<std::string_view, MCWeatherKind>, 6> Weathers{{
        {"Clear", MCWeatherKind::Clear},
        {"Rain", MCWeatherKind::Rain},
        {"Downpour", MCWeatherKind::Downpour},
        {"LightSnow", MCWeatherKind::LightSnow},
        {"Snow", MCWeatherKind::Snow},
        {"Blizzard", MCWeatherKind::Blizzard},
    }};

    /// <summary>
    /// Rain and a downpour as the mock-ups made them (tools/upscale/rain-proto, render_rain2.py and 3), their drops 1.2
    /// times as many: drawn in the game, as many covered 0.85 of the mock-up's pixels (mission 20's road). Their rain is
    /// heard faintly, as from far above the battle: a downpour 14 dB under the rain loop as levelled (about 25 dB under
    /// the game's sounds as they play), rain about 6 dB under that. Lightning strikes a downpour about twice a minute,
    /// its thunder after it; heard alone, the thunder was lost in the battle (you, 2 Nov: "it's hard to tell the thunder
    /// sound effect versus anything that's actually happening in the battle ... unless there's lightning").
    /// Both come and go in showers: a downpour's lulls as light as rain, its bursts 1.6 times as heavy; rain's lulls a
    /// drizzle.
    /// </summary>
    constexpr MCWeather::MCWeatherLook RainLook{.Dark = 0.33f,
                                                .Grey = 0.2f,
                                                .Wet = 0.1f,
                                                .Veil = 0.05f,
                                                .Drops = 380.0f,
                                                .Sizes = {0.6f, 0.33f, 0.07f},
                                                .Puddles = 0.06f,
                                                .Sheen = 0.08f,
                                                .Gloss = 0.25f,
                                                .Drips = 3.0f,
                                                .Heard = 0.31f,
                                                .Showers = 0.7f};
    constexpr MCWeather::MCWeatherLook DownpourLook{.Dark = 0.4f,
                                                    .Grey = 0.3f,
                                                    .Wet = 0.14f,
                                                    .Veil = 0.12f,
                                                    .Drops = 1010.0f,
                                                    .Sizes = {0.5f, 0.4f, 0.1f},
                                                    .Puddles = 0.13f,
                                                    .Sheen = 0.12f,
                                                    .Gloss = 0.35f,
                                                    .Drips = 8.0f,
                                                    .Heard = 0.45f,
                                                    .Strikes = 2.0f,
                                                    .Flash = 0.6f,
                                                    .ThunderHeard = 1.0f,
                                                    .Showers = 0.6f};

    /// <summary>
    /// Light snow, snow and a blizzard as the mock-up made them (tools/upscale/rain-proto/render_snow.py: its flakes a
    /// second over its panel, a quarter of a 1920x1080 take at a zoom of 2.25, over the panel's 100-pixel squares of the
    /// view), each under a paler overcast than the rain's and greyer, its veil white. Snow falls steadily and silently.
    /// Light snow barely settles (you, 2 Nov: "it would be cool if the snow accumulates over time and maybe it fills in
    /// footprints"): over ten minutes a dusting in the lowest spots; snow covers about half the flat ground in four, a
    /// blizzard most of it in a minute and a half, driven before a strong wind; the prints fill in the faster the harder
    /// it snows.
    /// </summary>
    constexpr MCWeather::MCWeatherLook LightSnowLook{.Dark = 0.12f,
                                                     .Grey = 0.3f,
                                                     .Veil = 0.06f,
                                                     .Drops = 126.0f,
                                                     .Sizes = {0.45f, 0.4f, 0.15f},
                                                     .Snows = true,
                                                     .Drift = 0.25f,
                                                     .Cover = 0.2f,
                                                     .Settle = 600.0f,
                                                     .Fill = 180.0f};
    constexpr MCWeather::MCWeatherLook SnowLook{.Dark = 0.18f,
                                                .Grey = 0.4f,
                                                .Veil = 0.12f,
                                                .Drops = 273.0f,
                                                .Sizes = {0.45f, 0.4f, 0.15f},
                                                .Snows = true,
                                                .Drift = 0.35f,
                                                .Cover = 0.45f,
                                                .Settle = 240.0f,
                                                .Fill = 90.0f};
    constexpr MCWeather::MCWeatherLook BlizzardLook{.Dark = 0.25f,
                                                    .Grey = 0.5f,
                                                    .Veil = 0.28f,
                                                    .Drops = 756.0f,
                                                    .Sizes = {0.45f, 0.4f, 0.15f},
                                                    .Snows = true,
                                                    .Drift = 1.6f,
                                                    .Cover = 0.85f,
                                                    .Settle = 90.0f,
                                                    .Fill = 30.0f};

    /// <summary>The longest step the drops move in one frame, in seconds of the game's time.</summary>
    constexpr float MaxStep = 0.1f;
    /// <summary>The drops' dice's first seed (their own: never the game's, nor the particles').</summary>
    constexpr uint32_t DiceSeed = 0x7a1d5eedu;
    /// <summary>The lightning's dice's first seed (its own, so the rain falls the same with or without it).</summary>
    constexpr uint32_t StrikeSeed = 0x1b0c7a11u;
    /// <summary>A strike's wait, as a share of the average wait (a minute over the strikes a minute): least and most.</summary>
    constexpr float StrikeWaitLeast = 0.4f;
    constexpr float StrikeWaitMost = 1.6f;
    constexpr float SecondsAMinute = 60.0f;
    /// <summary>
    /// A strike flickers 1 to MaxFlickers times, the first at once and each next FlickerGapLeast to FlickerGapMost
    /// seconds after, FlickerLeast to FlickerMost as bright as the first; each fades as exp(-age / FlickerFade), the
    /// strike gone after FlashLife seconds.
    /// </summary>
    constexpr int32_t MaxFlickers = 3;
    constexpr float FlickerGapLeast = 0.07f;
    constexpr float FlickerGapMost = 0.22f;
    constexpr float FlickerLeast = 0.35f;
    constexpr float FlickerMost = 0.8f;
    constexpr float FlickerFade = 0.06f;
    constexpr float FlashLife = 0.8f;
    /// <summary>The farthest strike's flash, of the nearest's.</summary>
    constexpr float FarFlash = 0.3f;
    /// <summary>A strike's flash, of the look's: thrown between the lightest and the strongest.</summary>
    constexpr float StrikeLightest = 0.5f;
    constexpr float StrikeStrongest = 1.6f;
    /// <summary>A strike's thunder comes this many seconds after its flash: the nearest's, and the farthest's.</summary>
    constexpr float ThunderSoonest = 0.3f;
    constexpr float ThunderLatest = 3.5f;
    /// <summary>A strike's thunder not taken this many seconds after it came is dropped (the sound off).</summary>
    constexpr float ThunderStale = 1.0f;
    /// <summary>
    /// The most drops, splashes and drips at once on a square of the view DropsArea pixels a side (a downpour keeps about
    /// 350): past it, no more fall until some have gone.
    /// </summary>
    constexpr float MaxDrops = 500.0f;
    /// <summary>The side of the view's square MCWeatherLook::Drops and MaxDrops count over, in pixels.</summary>
    constexpr float DropsArea = 100.0f;
    /// <summary>The least sine and cosine of the view's tilt the ground's projection is undone with.</summary>
    constexpr float MinProjection = 1e-3f;
    /// <summary>Drops land this far (view pixels) round the view: their streaks fall into it from above and the left.</summary>
    constexpr float LandLeft = 20.0f;
    constexpr float LandRight = 40.0f;
    constexpr float LandBelow = 15.0f;
    /// <summary>A drop or a splash this far out of the view isn't drawn.</summary>
    constexpr float ViewMargin = 4.0f;
    /// <summary>A splash ring lies on the ground: units standing on it cover it (MCParticles' resting particles').</summary>
    constexpr int32_t GroundDepthFixup = 30;
    /// <summary>The pull on a droplet thrown up, view pixels a second a second.</summary>
    constexpr float Gravity = 490.0f;
    /// <summary>The steps to a drop's ground point under a view point: its ground's height taken again this often.</summary>
    constexpr int GroundSteps = 2;
    /// <summary>A pixel of the mock-ups' (drawn at a zoom of 2.25), in window pixels.</summary>
    constexpr float MockPixel = 1.0f / 2.25f;
    /// <summary>
    /// Grass soaks the puddles up (composite.pshader's GrassDry): a palette colour this much greener than its red and blue
    /// (VFX's 6-bit components) is grass.
    /// </summary>
    constexpr int32_t GrassGreen = 2;
    /// <summary>
    /// Land drawn in a palette colour at least this saturated (HSL, of VFX's 6-bit components, which run to
    /// ComponentMost) is soil: the dirt's and the fields' browns and tans (0x2a, 0x2b, 0x4x, 0x9x) are 0.29 and more;
    /// the rock's, packed earth's and concrete's grey browns (0x2c, 0x2d, 0x8x) 0.26 and less, the roads' greys none.
    /// </summary>
    constexpr float SoilSaturation = 0.27f;
    constexpr int32_t ComponentMost = 63;
    /// <summary>
    /// The pixels round a drop's landing point (view pixels right and down of it) whose ground votes on what it lands
    /// on, the landing point's first (it wins a tie): the dirt's art is speckled with the rock's grey browns. Pixels in
    /// shadow, no component of their colour reaching ShadowDark (VFX's 6-bit), don't vote: the forest floor's shade is
    /// as dark a grey as a road's asphalt (which, voting for nothing, is hard ground).
    /// </summary>
    constexpr std::array<std::pair<int32_t, int32_t>, 5> GroundVotes{{{0, 0}, {-2, -1}, {2, -1}, {-2, 1}, {2, 1}}};
    static_assert(GroundVotes[0] == std::pair<int32_t, int32_t>{0, 0}, "GroundUnder takes the first for the point's");
    constexpr uint8_t ShadowDark = 9;
    /// <summary>The kinds of ground (MCWeather::MCSplashGround's).</summary>
    constexpr size_t GroundKinds = 5;

    /// <summary>
    /// A drop's size, as near the eye it falls (a top-down view: the nearer, the higher): how far it falls on screen and
    /// how fast (view pixels), its streak's length, its strength, its width (the draw rounds it to whole pixels of the
    /// output: 1, 2 or 3 at the closest zoom) and its brightest tip; its splash on land (droplets thrown up, as far as
    /// Throw, and a dot) and in water (a ring that wide, lasting that long, that strong); fitting the ground
    /// (MCWeatherSettings::Ground), a dark dot that wide soaking into soil and a puff that wide in grass.
    /// </summary>
    struct DropSize
    {
        float FallLow = 0.0f;
        float FallHigh = 0.0f;
        float SpeedLow = 0.0f;
        float SpeedHigh = 0.0f;
        float LengthLow = 0.0f;
        float LengthHigh = 0.0f;
        float AlphaLow = 0.0f;
        float AlphaHigh = 0.0f;
        float Width = 0.0f;
        float Tip = 0.0f;
        int32_t DropletsLow = 0;
        int32_t DropletsHigh = 0;
        float Throw = 0.0f;
        float Dot = 0.0f;
        float Ring = 0.0f;
        float RingLife = 0.0f;
        float RingAlpha = 0.0f;
        float Soak = 0.0f;
        float Puff = 0.0f;
    };

    /// <summary>Far, between and near (MCWeatherLook::Sizes), as mock-up 2 drew them at a zoom of 2.25.</summary>
    constexpr std::array<DropSize, 3> DropSizes{{
        {.FallLow = 20.0f,
         .FallHigh = 36.0f,
         .SpeedLow = 230.0f,
         .SpeedHigh = 300.0f,
         .LengthLow = 3.1f,
         .LengthHigh = 4.9f,
         .AlphaLow = 0.3f,
         .AlphaHigh = 0.45f,
         .Width = 0.45f,
         .Tip = 0.45f,
         .Dot = 0.45f,
         .Ring = 1.8f,
         .RingLife = 0.3f,
         .RingAlpha = 0.35f,
         .Soak = 0.8f,
         .Puff = 1.0f},
        {.FallLow = 33.0f,
         .FallHigh = 62.0f,
         .SpeedLow = 310.0f,
         .SpeedHigh = 400.0f,
         .LengthLow = 5.8f,
         .LengthHigh = 8.4f,
         .AlphaLow = 0.38f,
         .AlphaHigh = 0.55f,
         .Width = 0.9f,
         .Tip = 0.9f,
         .DropletsLow = 2,
         .DropletsHigh = 3,
         .Throw = 1.0f,
         .Dot = 0.9f,
         .Ring = 3.6f,
         .RingLife = 0.45f,
         .RingAlpha = 0.5f,
         .Soak = 1.2f,
         .Puff = 1.5f},
        {.FallLow = 67.0f,
         .FallHigh = 115.0f,
         .SpeedLow = 420.0f,
         .SpeedHigh = 530.0f,
         .LengthLow = 10.7f,
         .LengthHigh = 15.1f,
         .AlphaLow = 0.45f,
         .AlphaHigh = 0.6f,
         .Width = 1.35f,
         .Tip = 1.35f,
         .DropletsLow = 3,
         .DropletsHigh = 5,
         .Throw = 1.6f,
         .Dot = 0.9f,
         .Ring = 5.3f,
         .RingLife = 0.6f,
         .RingAlpha = 0.6f,
         .Soak = 1.6f,
         .Puff = 2.1f},
    }};
    constexpr size_t NearDrop = 2;

    /// <summary>
    /// A flake's size, as near the eye it falls (as a drop's): how far it falls on screen and how fast (view pixels), how
    /// far it rocks to each side as it falls and how often (rocks a second), and how strong it shows.
    /// </summary>
    struct FlakeSize
    {
        float FallLow = 0.0f;
        float FallHigh = 0.0f;
        float SpeedLow = 0.0f;
        float SpeedHigh = 0.0f;
        float RockLow = 0.0f;
        float RockHigh = 0.0f;
        float RateLow = 0.0f;
        float RateHigh = 0.0f;
        float AlphaLow = 0.0f;
        float AlphaHigh = 0.0f;
    };

    /// <summary>
    /// Far, between and near (MCWeatherLook::Sizes), as the snow's mock-up drew them at a zoom of 2.25: far a pixel of
    /// its, between two across, near a plus three across.
    /// </summary>
    constexpr std::array<FlakeSize, 3> FlakeSizes{{
        {.FallLow = 45.0f * MockPixel,
         .FallHigh = 80.0f * MockPixel,
         .SpeedLow = 40.0f * MockPixel,
         .SpeedHigh = 60.0f * MockPixel,
         .RockLow = 1.0f * MockPixel,
         .RockHigh = 2.5f * MockPixel,
         .RateLow = 0.6f,
         .RateHigh = 1.0f,
         .AlphaLow = 0.45f,
         .AlphaHigh = 0.7f},
        {.FallLow = 75.0f * MockPixel,
         .FallHigh = 140.0f * MockPixel,
         .SpeedLow = 60.0f * MockPixel,
         .SpeedHigh = 95.0f * MockPixel,
         .RockLow = 2.0f * MockPixel,
         .RockHigh = 4.0f * MockPixel,
         .RateLow = 0.5f,
         .RateHigh = 0.9f,
         .AlphaLow = 0.6f,
         .AlphaHigh = 0.85f},
        {.FallLow = 150.0f * MockPixel,
         .FallHigh = 260.0f * MockPixel,
         .SpeedLow = 100.0f * MockPixel,
         .SpeedHigh = 150.0f * MockPixel,
         .RockLow = 3.0f * MockPixel,
         .RockHigh = 6.0f * MockPixel,
         .RateLow = 0.4f,
         .RateHigh = 0.8f,
         .AlphaLow = 0.7f,
         .AlphaHigh = 0.95f},
    }};
    static_assert(FlakeSizes.size() == DropSizes.size(), "a weather's Sizes pick a drop's or a flake's");
    constexpr size_t FarFlake = 0;
    constexpr size_t NearFlake = 2;

    /// <summary>
    /// The palette's colours the rain is drawn in (its water ramp, 0x5c to 0x5f, and pale greys, 0x1c and 0x1d);
    /// fitting the ground, wet soil's dark brown (0x91) and a glint's near white (0x1e).
    /// </summary>
    struct Rgb
    {
        float Red = 0.0f;
        float Green = 0.0f;
        float Blue = 0.0f;
    };

    constexpr Rgb WaterBlue{124.0f / 255.0f, 148.0f / 255.0f, 168.0f / 255.0f};
    constexpr Rgb PaleBlue{144.0f / 255.0f, 168.0f / 255.0f, 192.0f / 255.0f};
    constexpr Rgb PalestBlue{168.0f / 255.0f, 192.0f / 255.0f, 216.0f / 255.0f};
    constexpr Rgb SplashGrey{200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f};
    constexpr Rgb PaleGrey{220.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f};
    constexpr Rgb SoakBrown{44.0f / 255.0f, 24.0f / 255.0f, 0.0f};
    constexpr Rgb GlintWhite{236.0f / 255.0f, 236.0f / 255.0f, 236.0f / 255.0f};
    /// <summary>The snow's flakes: the palette's palest greys (0x1d to 0x1f).</summary>
    constexpr Rgb FlakeGrey = PaleGrey;
    constexpr Rgb FlakeWhite = GlintWhite;
    constexpr Rgb FlakeBrightest{252.0f / 255.0f, 252.0f / 255.0f, 252.0f / 255.0f};

    /// <summary>
    /// The wind's gusts: each drop's slant (view pixels across, downwind, for each it falls) is the gusts' where and when
    /// it falls, from WindLow to WindHigh, give or take WindJitter. The gusts are noise GustSize meters big sweeping
    /// downwind at GustSpeed meters a second.
    /// </summary>
    constexpr float WindLow = 0.08f;
    constexpr float WindHigh = 0.42f;
    constexpr float WindJitter = 0.03f;
    constexpr float GustSize = 60.0f;
    constexpr float GustSpeed = 30.0f;
    /// <summary>Where the veil's sheets are thick, more drops fall: from SheetLeast of the rate to SheetMost.</summary>
    constexpr float SheetLeast = 0.35f;
    constexpr float SheetMost = 1.65f;

    // Splashes: droplets thrown up off land, out and up (view pixels a second, times the size's Throw), a near drop's
    // twice as big, PaleDropletShare of them pale grey (the rest pale blue); a dot where it lands for DotLife; in water or a puddle a ring growing from RingFrom pixels, a near
    // drop's with a second inside it, SecondRingAfter later, SecondRingShare as wide, lasting and strong.
    constexpr float DropletOutLow = 11.0f;
    constexpr float DropletOutHigh = 31.0f;
    constexpr float DropletUpLow = 20.0f;
    constexpr float DropletUpHigh = 42.0f;
    constexpr float DropletSize = 0.5f * MockPixel;
    constexpr float DropletAlpha = 0.6f;
    constexpr float DropletFadeTo = 0.4f;
    constexpr float PaleDropletShare = 0.5f;
    constexpr float DotLife = 0.05f;
    constexpr float DotAlpha = 0.55f;
    constexpr float RingFrom = 0.7f;
    constexpr float RingPaleShare = 0.6f;
    constexpr float RingAlphaLeast = 0.75f;
    constexpr float RingSizeVary = 0.2f;
    constexpr float SecondRingAfter = 0.12f;
    constexpr float SecondRingShare = 0.55f;
    constexpr float SecondRingLife = 0.7f;
    constexpr float SecondRingAlpha = 0.8f;
    // Splashes fitting the ground (MCWeatherSettings::Ground; each mark's strength times it, at most whole): off hard
    // ground HardDroplets more droplets (from a drop that throws any), crisper, HardDropletAlpha strong and fading only
    // to HardFadeTo; into soil a dark dot (SoakBrown) the size's Soak to each side, lying on the ground, spreading from
    // SoakFrom of it over SoakSpread of its life as it soaks in, SoakAlpha strong, holding, then gone by SoakLife; in
    // grass a soft puff of spray (SplashGrey) the size's Puff to each side, PuffSoftness soft, spreading from PuffFrom
    // of it and rising PuffRise pixels, PuffAlpha strong, gone in PuffLife; on a flat stretch of a unit's metal top a
    // tick of a glint (GlintWhite) GlintInset pixels into it, GlintWidth to each side, GlintLength tall and GlintArm of
    // that across, GlintAlpha strong (a far drop's FarGlint of that), gone in GlintLife (two frames).
    constexpr int32_t HardDroplets = 1;
    constexpr float HardDropletAlpha = 0.8f;
    constexpr float HardFadeTo = 0.75f;
    constexpr float SoakFrom = 0.5f;
    constexpr float SoakSpread = 0.3f;
    constexpr float SoakAlpha = 0.5f;
    constexpr float SoakLife = 0.5f;
    constexpr float PuffSoftness = 0.35f;
    constexpr float PuffFrom = 0.5f;
    constexpr float PuffRise = 1.5f;
    constexpr float PuffAlpha = 0.5f;
    constexpr float PuffLife = 0.2f;
    constexpr float GlintWidth = 0.35f;
    constexpr float GlintLength = 1.0f;
    constexpr float GlintArm = 0.6f;
    constexpr float GlintAlpha = 0.6f;
    constexpr float GlintInset = 1.5f;
    constexpr float FarGlint = 0.6f;
    constexpr float GlintLife = 0.07f;
    // A foot coming down in a puddle (MCWeather::AddStep): a spray of StepDroplets droplets StepDropletSize to each side
    // of their middle, StepDropletAlpha strong, from within StepSpread pixels of it and StepStagger seconds of each
    // other, thrown StepOut times a splash's usual speeds out and StepUp times up (as high as a mech's shins); and the
    // puddle reacting round it for StepLife (the composite's SteppedPuddleAt), all times MCWeatherSettings::Steps. A
    // foot splashes only StepMargin inside the puddles' level (more than a drop: the composite's puddles take the
    // ground's dips too, so a foot at an edge may be out of one). The puddle reacts from the outline of its footprint,
    // taken as circles (PrintShapeOf, PrintRounds rounds), or of one PrintFallback meters round for a foot without one.
    constexpr float StepDroplets = 18.0f;
    constexpr float StepDropletSize = 1.75f * MockPixel;
    constexpr float StepDropletAlpha = 0.95f;
    constexpr float StepSpread = 5.0f;
    constexpr float StepStagger = 0.07f;
    constexpr float StepOut = 1.4f;
    constexpr float StepUp = 4.3f;
    constexpr float StepLife = 0.7f;
    constexpr float StepMargin = 0.04f;
    constexpr int32_t PrintRounds = 6;
    constexpr float PrintFallback = 4.0f;

    // Units: drops falling into a unit's footprint (FootDepth of its width deep behind its feet, at least FootLeast
    // pixels) land on its top, within Band pixels below its top edge (the farther back, the nearer its edge); water
    // drips off its lower edges at least DripAbove pixels above its feet, landing within DripLandsShare of its footprint
    // (at least DripLandsLeast pixels below where it fell from).
    constexpr float FootDepth = 0.3f;
    constexpr float FootLeast = 6.0f;
    constexpr float Band = 6.0f;
    /// <summary>
    /// A drop glints only where it lands on a flat stretch of a unit's top as the unit stands when it lands (it may
    /// have moved on while the drop fell): within OnTopReach pixels above its top to Band and OnTopReach below it, the
    /// columns FlatReach to each side drawn, their tops within FlatStep pixels of its, at least FlatInside columns in
    /// from its sides.
    /// </summary>
    constexpr float OnTopReach = 1.0f;
    constexpr int32_t FlatReach = 2;
    constexpr float FlatStep = 2.0f;
    constexpr int32_t FlatInside = 5;
    constexpr int32_t DripAbove = 4;
    constexpr float DripSize = 0.5f * MockPixel;
    constexpr float DripLength = MockPixel;
    constexpr float DripAlpha = 0.65f;
    constexpr float DripLandsLeast = 1.5f;
    constexpr float DripLandsShare = 0.5f;

    // The puddles, as composite.pshader lays them (PuddleField): noise PuddleSize meters big, two layers, the finer
    // PuddleApart off, none on grass; a drop rings in one only PuddleMargin inside its level, as the composite's puddles
    // also take the ground's dips, which the drops don't know. The level for a share of the ground is measured over a
    // grid of LevelSamples a side, LevelStep meters apart.
    constexpr float PuddleSize = 22.0f;
    constexpr float PuddleApartX = 31.7f;
    constexpr float PuddleApartY = -12.3f;
    constexpr float PuddleMargin = 0.02f;
    constexpr int32_t LevelSamples = 96;
    constexpr float LevelStep = 7.3f;
    // Nor are there puddles where BlockedDryFrom of the ground's map tile or more can't be entered: cliffs
    // (composite.pshader's).
    constexpr float BlockedDryFrom = 0.4f;
    // The veil's sheets (composite.pshader's SheetsAt): noise SheetSize meters big drifting downwind at SheetSpeed,
    // two layers, thick from SheetFrom of it to SheetTo.
    constexpr float SheetSize = 70.0f;
    constexpr float SheetSpeed = 25.0f;
    constexpr float SheetFrom = 0.3f;
    constexpr float SheetTo = 0.7f;
    constexpr float SheetApartX = 17.3f;
    constexpr float SheetApartY = -9.1f;
    // The showers (MCWeatherLook::Showers): noise on the game's time (times MCWeatherSettings::ShowerPace), a lattice
    // point each ShowerLength seconds, two layers, along its own row of the lattice (ShowerRow, chosen for a mission's
    // first minutes: the rain builds from as made to a burst, eases to a lull and builds again); the noise's middle,
    // ShowerSpread each way, spans a lull to a burst, eased at both ends. However far the setting takes them, a lull
    // leaves at least 1 - MostShowers of the rain.
    constexpr float ShowerLength = 120.0f;
    constexpr float ShowerRow = 35.0f;
    constexpr float ShowerApartX = 23.9f;
    constexpr float ShowerApartY = 7.7f;
    constexpr float ShowerSpread = 0.35f;
    constexpr float MostShowers = 0.8f;
    // How the look follows the showers' rain: the overcast's dark and grey DarkFollow as far as the drops (enough that a
    // lull shows lighter, and a burst darker, though the drops and the veil, lighter than the ground, follow too); the
    // drops near the eye as far, those between BetweenFollow as far (those far from it the rest); heard as loud as the
    // rain's strength to the power HeardFollow (a channel volume: a lull of 0.4 about 8 dB quieter, a burst of 1.6 about
    // 4 dB louder).
    constexpr float DarkFollow = 0.75f;
    constexpr float BetweenFollow = 0.5f;
    constexpr float HeardFollow = 0.5f;
    // The puddles fill toward how hard the rain falls (1 as made) over PuddleFill seconds of the showers' time, and dry
    // toward it more slowly, over PuddleDry; taken each PuddleStep seconds. Damp ground, without puddles yet, mirrors
    // DampSheen of the sky wet ground does.
    constexpr float PuddleFill = 240.0f;
    constexpr float PuddleDry = 720.0f;
    constexpr float PuddleStep = 1.0f;
    constexpr float DampSheen = 0.4f;
    // Drops catching the lights (MCWeatherSettings::Light, MCSceneLights::LightInAir), at the middle of their streaks:
    // shown in GlintSteps steps of their brightest component up to GlintMost, the last from GlintFull of light caught
    // (times the drop's facet), none under the first; in the lights' hue GlintSaturation times as far from grey (a
    // fire's light mixed with its glow oval's grey reads as the fire's). A drop's facet is CatchLeast to 1, hashed from
    // its place FacetScale to a world unit (not the dice: the rain falls the same with it or without). The drops catch
    // all of it from GlintDusk's world light down, DayGlint of it by day (a world light of 1).
    constexpr int32_t GlintSteps = 3;
    constexpr float GlintMost = 0.7f;
    constexpr float GlintFull = 0.6f;
    constexpr float GlintSaturation = 1.5f;
    constexpr float CatchLeast = 0.25f;
    constexpr float FacetScale = 8.0f;
    constexpr float GlintDusk = 0.6f;
    constexpr float DayGlint = 0.25f;
    // The snow's flakes (MCWeatherLook::Snows): each drifting downwind its weather's Drift across for each pixel it falls,
    // from FlakeCalm of it in a lull of the gusts to FlakeCalm + FlakeGusts at their height, give or take FlakeJitter
    // (the mock-up's); landing over the view as the drops do, and as far below it as FlakeBelow and upwind of it as the
    // farthest the nearest drift (their flakes falling into the view from there). A flake landed on land or a unit's top
    // rests FlakeRest seconds, fading (a pixel of the mock-up's, its brightest), and is gone on the water. At most
    // MaxFlakes falling and resting on a square of the view DropsArea pixels a side (a blizzard keeps about 1500).
    constexpr float FlakeCalm = 0.7f;
    constexpr float FlakeGusts = 0.6f;
    constexpr float FlakeJitter = 0.08f;
    constexpr float FlakeBelow = 60.0f;
    constexpr float FlakeRest = 0.6f;
    constexpr float MaxFlakes = 1800.0f;
    constexpr float FlakeDot = 0.5f * MockPixel;
    // Each size's flake's radius, far to near, and how far the near one's plus's arms reach each way from its middle.
    constexpr std::array<float, 3> FlakeRadii{FlakeDot, 2.0f * FlakeDot, FlakeDot};
    constexpr float FlakeArm = MockPixel;
    // Zoomed out past MCWeather::FullFlakeLines, the flakes left out (MCWeather::ShownSpeckle) are kept by the frame for
    // as long as the longest fall and rest; the share of a size's still falling is taken at StillFallingSpeeds speeds
    // through its range (exactly through its falls).
    constexpr size_t StillFallingSpeeds = 32;
    // Zoomed out past MCWeather::FullDropLines, the drops left out are drawn as streaks (MCSpeckLayer::Length), their
    // lattices laid along the wind's middle slant (WindLow to WindHigh's), each StreakStrength as strong as its drop
    // (a streak is on whole texels, a drop's a smoothed line thinner than one, brightest at its tip); their splashes as
    // specks a view pixel across, each showing SplashSpeckLife seconds, as strong as the real ones (a dot and the
    // droplets thrown up round it, each smaller than a pixel) over it, SplashSpeckAlpha, in their grey.
    constexpr float StreakStrength = 1.0f;
    constexpr float SplashSpeckLife = 0.15f;
    constexpr float SplashSpeckSide = 1.0f;
    constexpr float SplashSpeckAlpha = 0.7f;
    // On water, the splashes' specks stand for the rings (Rings) instead, WaterSpeckAlpha strong: as many as leave as
    // much light over their lives (each fading to none, SpeckFadeMean as strong on average) as the rings would.
    constexpr float WaterSpeckAlpha = 1.0f;
    constexpr float SpeckFadeMean = 0.5f;
    // The snow's cover (MCWeather::ShownCover), as composite.pshader lays it (SnowFieldAt): noise SnowSize meters big, two
    // layers, the finer SnowApart off; of the ground it covers, ThickShare lies thick, the rest under a dusting.
    constexpr float SnowSize = 30.0f;
    constexpr float SnowApartX = 23.1f;
    constexpr float SnowApartY = -7.9f;
    constexpr float ThickShare = 0.7f;

    // The frame's marks are made in parts of PartPieces pieces (splashes or drops), and what the drops landing land on
    // is found in parts of LandingPart drops, by the game's thread and at most MostHelpers threads helping it
    // (AddMarks, Landings, MCWorkers::HelpersFor).
    constexpr size_t PartPieces = 2048;
    constexpr size_t LandingPart = 256;
    constexpr size_t MostHelpers = 4;

    /// <summary>A value between 0 and 1 for a lattice point, the same every frame (composite.pshader's LatticeHash).</summary>
    float LatticeHash(float x, float y)
    {
        uint32_t h = static_cast<uint32_t>(static_cast<int32_t>(x)) * 1597334677u ^
                     static_cast<uint32_t>(static_cast<int32_t>(y)) * 3812015801u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        return static_cast<float>(static_cast<double>(h) / 4294967295.0);
    }

    /// <summary>Smooth value noise between 0 and 1 (composite.pshader's Noise).</summary>
    float Noise(float x, float y)
    {
        const float cornerX = std::floor(x);
        const float cornerY = std::floor(y);
        const float fx = x - cornerX;
        const float fy = y - cornerY;
        const float ux = fx * fx * (3.0f - 2.0f * fx);
        const float uy = fy * fy * (3.0f - 2.0f * fy);
        const float top = std::lerp(LatticeHash(cornerX, cornerY), LatticeHash(cornerX + 1.0f, cornerY), ux);
        const float bottom =
            std::lerp(LatticeHash(cornerX, cornerY + 1.0f), LatticeHash(cornerX + 1.0f, cornerY + 1.0f), ux);
        return std::lerp(top, bottom, uy);
    }

    /// <summary>Two layers of noise at (x, y), the finer one twice as fine and half as strong, off by (apartX, apartY).</summary>
    float Layered(float x, float y, float apartX, float apartY)
    {
        return (2.0f * Noise(x, y) + Noise(2.0f * x + apartX, 2.0f * y + apartY)) / 3.0f;
    }

    /// <summary>How much of the light at it a drop or droplet at world (x, y) catches: CatchLeast to 1, by its
    /// place.</summary>
    float Facet(float x, float y)
    {
        return CatchLeast + (1.0f - CatchLeast) * LatticeHash(std::floor(x * FacetScale), std::floor(y * FacetScale));
    }

    /// <summary>The puddles' field at a ground point (meters east and north of the map's middle; PuddleField's).</summary>
    float PuddleField(float east, float north)
    {
        return Layered(east / PuddleSize, north / PuddleSize, PuddleApartX, PuddleApartY);
    }

    /// <summary>The snow's field at a ground point (meters east and north of the map's middle; SnowFieldAt's noise).</summary>
    float SnowField(float east, float north)
    {
        return Layered(east / SnowSize, north / SnowSize, SnowApartX, SnowApartY);
    }

    /// <summary>How thick the veil's sheets are at a ground point at the game's time <paramref name="time"/> (SheetsAt's).</summary>
    float SheetsAt(float east, float north, float time)
    {
        const float drift = time * SheetSpeed;
        const float x = (east - drift * MCSceneLights::WindEast) / SheetSize;
        const float y = (north - drift * MCSceneLights::WindNorth) / SheetSize;
        const float noise = Layered(x, y, SheetApartX, SheetApartY);
        return std::clamp((noise - SheetFrom) / (SheetTo - SheetFrom), 0.0f, 1.0f);
    }

    /// <summary>The gusts at a ground point at the game's time: 0 calm to 1 at their height.</summary>
    float GustAt(float east, float north, float time)
    {
        const float drift = time * GustSpeed;
        const float x = (east - drift * MCSceneLights::WindEast) / GustSize;
        const float y = (north - drift * MCSceneLights::WindNorth) / GustSize;
        return Layered(x, y, SheetApartY, SheetApartX);
    }

    /// <summary>The weather's own dice: the same throws in the same order every run.</summary>
    class MCDice
    {
    public:
        /// <summary>Dice thrown from <paramref name="seed"/>.</summary>
        explicit MCDice(uint32_t seed = DiceSeed) : _Seed(seed), _Engine(seed) {}

        /// <summary>Throws from the start again.</summary>
        void Restart() { _Engine.seed(_Seed); }

        /// <summary>A number in [0, 1), made the same way on every platform (24 of the engine's bits).</summary>
        float Unit() { return static_cast<float>(_Engine() >> 8) * (1.0f / static_cast<float>(1u << 24)); }

        /// <summary>A number in [<paramref name="low"/>, <paramref name="high"/>).</summary>
        float Between(float low, float high) { return low + (high - low) * Unit(); }

        /// <summary>A whole number from <paramref name="low"/> to <paramref name="high"/>, both included.</summary>
        int32_t Between(int32_t low, int32_t high)
        {
            return low + std::min(static_cast<int32_t>(Unit() * static_cast<float>(high - low + 1)), high - low);
        }

        /// <summary>A whole number of things, <paramref name="mean"/> on average (its fraction a chance of one more).</summary>
        int32_t Count(float mean) { return static_cast<int32_t>(std::floor(std::max(mean, 0.0f) + Unit())); }

    private:
        uint32_t _Seed;
        std::mt19937 _Engine;
    };

    /// <summary>
    /// A falling drop or flake: where it lands (the world, its height there), its size and look, how far it has fallen; a
    /// flake's rocking (how far to each side, how often, from where in a rock).
    /// </summary>
    struct Drop
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        uint8_t Size = 0;
        bool OnUnit = false;
        bool Flake = false;
        float Rock = 0.0f;
        float RockRate = 0.0f;
        float Phase = 0.0f;
        float Fall = 0.0f;
        float Speed = 0.0f;
        float Length = 0.0f;
        float Alpha = 0.0f;
        float Slant = 0.0f;
        float Age = 0.0f;
        int32_t Depth = 0;
    };

    /// <summary>
    /// What a splash leaves for a while: a droplet thrown up, a dot, a ring, a drip falling; fitting the ground, a dark
    /// dot soaking into soil, a puff in grass, a glint on metal; a flake resting where it landed.
    /// </summary>
    enum class SplashKind : uint8_t
    {
        Droplet,
        Dot,
        Ring,
        Drip,
        Soak,
        Puff,
        Glint,
        Flake,
    };

    /// <summary>
    /// A splash's piece: placed from a world point (X, Y, Z) by view pixels (Across, Up), moving at (VAcross, VUp) and
    /// falling (Gravity) for Life seconds; a ring growing to Size; drawn in its colour at its strength at Depth, a
    /// droplet fading to FadeTo of it.
    /// </summary>
    struct Splash
    {
        SplashKind Kind = SplashKind::Dot;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Across = 0.0f;
        float Up = 0.0f;
        float VAcross = 0.0f;
        float VUp = 0.0f;
        /// <summary>A drip's fall to where it lands (view pixels below where it started).</summary>
        float Drop = 0.0f;
        float Size = 0.0f;
        float Age = 0.0f;
        float Life = 0.0f;
        Rgb Color;
        float Alpha = 0.0f;
        float FadeTo = DropletFadeTo;
        int32_t Depth = 0;
    };

    /// <summary>
    /// Droplets thrown up from a point: how many, how fast out and up (times a splash's usual speeds), from within how many
    /// view pixels to its sides, within how many seconds of each other, how big, how strong, and fading to how much.
    /// </summary>
    struct DropletThrow
    {
        int32_t Count = 0;
        float Out = 1.0f;
        float Up = 1.0f;
        float Spread = 0.0f;
        float Stagger = 0.0f;
        float Size = 0.0f;
        float Alpha = DropletAlpha;
        float FadeTo = DropletFadeTo;
    };

    /// <summary>A drop the tools or tests land (MCWeather::AddDrop): where, and its size.</summary>
    struct PendingDrop
    {
        MCVector3D Point;
        uint8_t Size = 0;
    };

    /// <summary>A foot coming down this frame (MCWeather::AddStep): where, and its footprint's shape table and frame.</summary>
    struct PendingStep
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        std::span<const uint8_t> Print;
        int32_t PrintFrame = -1;
    };

    /// <summary>A circle in the view: its middle (view pixels off a shape's hot spot) and its radius.</summary>
    struct ViewCircle
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Radius = 0.0f;
    };

    /// <summary>A footprint's shape as circles (PrintShapeOf).</summary>
    using PrintShape = std::array<ViewCircle, MCWeather::PrintCircles>;

    /// <summary>A shape's frame's columns, from its hot spot: each one's top and bottom pixel, and its lower edges.</summary>
    struct FrameColumns
    {
        int32_t Left = 0;
        std::vector<int16_t> Top;
        std::vector<int16_t> Bottom;
        /// <summary>Its pixels with none drawn under them (x, y from the hot spot).</summary>
        std::vector<std::pair<int16_t, int16_t>> LowerEdges;
    };

    /// <summary>A shape table's frame, mirrored or not, by the table's place and length and the frame's number.</summary>
    using FrameKey = std::tuple<MCAddressKey, size_t, int32_t, bool>;

    /// <summary>
    /// A unit drawn this frame: its columns in the view (their tops; Left the first's x), its feet, footprint and parts'
    /// depth, its lower edges in the view, and where it stands in the world.
    /// </summary>
    struct StandingUnit
    {
        int32_t Left = 0;
        std::vector<float> Top;
        float FeetY = 0.0f;
        float Footprint = 0.0f;
        int32_t Depth = 0;
        std::vector<std::pair<float, float>> LowerEdges;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float FeetX = 0.0f;
    };

    /// <summary>
    /// A lightning strike: the game's time it struck, how far off (0 the nearest, 1 the farthest), to which side (-1 to
    /// 1) and how strong (its flash, of the look's), and its flickers: each one's time after the strike and how bright
    /// (0: none).
    /// </summary>
    struct Strike
    {
        float Time = 0.0f;
        float Far = 0.0f;
        float Side = 0.0f;
        float Strength = 1.0f;
        std::array<float, MaxFlickers> At{};
        std::array<float, MaxFlickers> Bright{};
    };

    /// <summary>A strike's thunder, due at the game's time Due.</summary>
    struct PendingThunder
    {
        float Due = 0.0f;
        MCWeather::MCThunderClap Clap;
    };

    /// <summary>
    /// The flakes (or, in rain, the drops) of each size a frame of a zoomed-out view left out, at the game's time Time:
    /// as many a view pixel squared as would have landed where the veil's sheets are as thin as SheetLeast thins them,
    /// and SheetMost over SheetLeast times as many where they're thickest.
    /// </summary>
    struct LeftOutFlakes
    {
        float Time = 0.0f;
        std::array<float, FlakeSizes.size()> Thrown{};
        /// <summary>Flakes, not drops (a weather changing from snow to rain, or back, leaves the other's out).</summary>
        bool Snows = false;
    };

    static_assert(FlakeSizes.size() == DropSizes.size(), "a left-out count for each size of flake or drop");

    /// <summary>
    /// Marks of the frame's (all of them, or a part's: AddMarks), each at its depth, added to the draw list together once
    /// all are made (MCParticles::AddMarks): drawn as each added alone would be.
    /// </summary>
    struct Marks
    {
        std::vector<MCParticleCommand> Commands;
        std::vector<int32_t> Depths;
    };

    /// <summary>
    /// The weather's state: its settings and the mission's weather; the drops falling and the splashes showing, and the
    /// most there may be (for this frame's view); the units drawn, the feet come down and the drops landed by the tools
    /// this frame (Draw empties them each frame); the feet's puddles still reacting (the newest
    /// MCWeather::MaxPuddleSteps); the shapes' frames' columns and footprints' shapes measured so far (kept for the
    /// mission); the dice, the rain's, its splashes' and the feet's (so the rain falls the same whoever steps in it and
    /// however its splashes fit the ground); the game's time the last frame was drawn at.
    /// </summary>
    struct State
    {
        MCWeather::MCWeatherSettings Settings;
        MCWeatherKind MissionWeather = MCWeatherKind::Clear;
        std::optional<MCWeatherKind> MatchWeather;
        std::vector<Drop> Drops;
        std::vector<Splash> Splashes;
        size_t Most = 0;
        std::vector<StandingUnit> Units;
        std::vector<PendingStep> Steps;
        std::vector<PendingDrop> Landing;
        std::vector<MCWeather::MCPuddleStep> PuddleSteps;
        std::map<FrameKey, FrameColumns> Frames;
        std::map<FrameKey, std::optional<PrintShape>> Prints;
        MCDice Dice;
        MCDice SplashDice;
        MCDice StepDice;
        /// <summary>The lightning: its dice, the game's time till the next strike, the strikes still flashing, their thunder.</summary>
        MCDice StrikeDice{StrikeSeed};
        std::optional<float> NextStrike;
        std::vector<Strike> Strikes;
        std::vector<PendingThunder> Thunders;
        std::optional<float> Time;
        /// <summary>The puddles' levels for their shares of the ground, measured once (PuddleLevel).</summary>
        std::vector<float> PuddleSamples;
        /// <summary>The snow's levels for its shares of the ground, measured once (SnowLevel).</summary>
        std::vector<float> SnowSamples;
        /// <summary>
        /// How far the puddles have spread each PuddleStep seconds of the showers' time from a mission's start (Filled),
        /// for showers as far as FilledFor; taken as far as asked for.
        /// </summary>
        std::vector<float> Filled;
        float FilledFor = 0.0f;
        /// <summary>
        /// Port-only (HD weather's snow): the flakes a zoomed-out view left out, a frame's each, for as long as some may
        /// still be falling or resting; and the specks they make now (MCWeather::ShownSpeckle).
        /// </summary>
        std::deque<LeftOutFlakes> LeftOut;
        MCWeather::MCSpeckle Speckle;
        /// <summary>Port-only (HD weather's snow): falling snow drawn since MCWeather::TakeSnowDrawn.</summary>
        bool SnowDrawn = false;
        /// <summary>
        /// The marks of the frame's parts (AddMarks) and what the drops landing land on (Landings), kept from frame to
        /// frame so their room is.
        /// </summary>
        std::vector<Marks> MarkParts;
        std::vector<MCWeather::MCSplashGround> Grounds;
    };

    /// <summary>The weather's one state.</summary>
    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>Whether palette entry <paramref name="index"/> is grass's (green).</summary>
    bool IsGrassIndex(uint8_t index)
    {
        const MCVfxRgb color = GamePalette().Colors()[index];
        return color.G - std::max(color.R, color.B) >= GrassGreen;
    }

    /// <summary>A world point's place in the view (MCParticles::ViewPoint).</summary>
    MCVector2D Project(float x, float y, float z, const MCCamera& camera)
    {
        return MCParticles::ViewPoint(x, y, z, camera);
    }

    /// <summary>
    /// The ground point under view point (<paramref name="viewX"/>, <paramref name="viewY"/>): the flat projection
    /// undone, its ground's height taken and the point found again (GroundSteps times).
    /// </summary>
    MCVector3D GroundPointAt(float viewX, float viewY, const MCCamera& camera)
    {
        const float cosine = std::max(MCTerrain::ProjectionCos, MinProjection);
        const float sine = std::max(MCTerrain::ProjectionSin, MinProjection);
        float z = 0.0f;
        MCVector3D point;

        for (int step = 0; step <= GroundSteps; ++step)
        {
            const float across = (viewX - MCTerrain::MapTopLeft2d100.X + camera.ScreenUL.X - camera.HalfWidth) / cosine;
            const float down =
                (viewY + z - MCTerrain::MapTopLeft2d100.Y + camera.ScreenUL.Y - camera.HalfHeight) / sine;
            const float fromLeft = 0.5f * (across + down);
            const float fromTop = 0.5f * (down - across);
            point = MCVector3D(fromLeft + MCTerrain::MapTopLeft3d100.X, MCTerrain::MapTopLeft3d100.Y - fromTop, 0.0f);
            z = MCParticles::GroundAt(point.X, point.Y);
        }

        point.Z = z;
        return point;
    }

    /// <summary>
    /// The ground point the composite takes a world point for (MCSceneGround's map, which leaves the ground's height
    /// out): the point of flat ground drawn where it is, in meters east and north of the map's middle.
    /// </summary>
    std::pair<float, float> FlatGround(float x, float y, float z)
    {
        const float sine = std::max(MCTerrain::ProjectionSin, MinProjection);
        const float lift = 0.5f * z / sine;
        return {x - lift, y + lift};
    }

    /// <summary>
    /// The share of the ground at world point (x, y) that can't be entered (cliffs), as the GPU ground holds it
    /// (draw.pshader's BlockedAt): its map tile's at the tile's middle, blended toward its neighbours' (bilinear between
    /// the tiles' middles), so it doesn't step at the tiles' seams; a tile off the map counts as none.
    /// </summary>
    float BlockedShare(float x, float y)
    {
        const MCOptionalRef<const MCScenarioMap> loaded = LoadedGameMap();

        if (!loaded)
        {
            return 0.0f;
        }

        const MCScenarioMap& map = *loaded;
        // The point among the tiles' middles: tile (row, col)'s middle at (row + 0.5, col + 0.5) of the vertices.
        const double col = static_cast<double>(MCTerrain::OneOvermetersPerVertex) * x + VerticesMapSideDivTwo() - 0.5;
        const double row = (static_cast<double>(MetersMapSideDivTwo()) - y) * MCTerrain::OneOvermetersPerVertex - 0.5;
        const double firstCol = std::floor(col);
        const double firstRow = std::floor(row);
        const float acrossCols = static_cast<float>(col - firstCol);
        const float downRows = static_cast<float>(row - firstRow);
        const auto share = [&map](double tileR, double tileC)
        {
            const int32_t r = static_cast<int32_t>(tileR);
            const int32_t c = static_cast<int32_t>(tileC);
            return map.OnMap(r, c) ? static_cast<float>(map.TileAt(r, c).BlockedCells()) / (MapCellDim * MapCellDim)
                                   : 0.0f;
        };

        const float upper = std::lerp(share(firstRow, firstCol), share(firstRow, firstCol + 1.0), acrossCols);
        const float lower =
            std::lerp(share(firstRow + 1.0, firstCol), share(firstRow + 1.0, firstCol + 1.0), acrossCols);
        return std::lerp(upper, lower, downRows);
    }

    /// <summary>
    /// The level of a noise field (<paramref name="field"/> of a ground point) that leaves <paramref name="share"/> of the
    /// ground under it, from <paramref name="samples"/>: the field over a grid, measured the first time and sorted.
    /// </summary>
    float LevelOf(std::vector<float>& samples, float (*field)(float, float), float share)
    {
        if (share <= 0.0f)
        {
            return 0.0f;
        }

        if (samples.empty())
        {
            samples.reserve(static_cast<size_t>(LevelSamples) * LevelSamples);

            for (int32_t row = 0; row < LevelSamples; ++row)
            {
                for (int32_t col = 0; col < LevelSamples; ++col)
                {
                    samples.push_back(field(static_cast<float>(col) * LevelStep, static_cast<float>(row) * LevelStep));
                }
            }

            std::ranges::sort(samples);
        }

        const auto at = static_cast<size_t>(std::clamp(share, 0.0f, 1.0f) * static_cast<float>(samples.size() - 1));
        return samples[at];
    }

    /// <summary>The puddles' level that leaves <paramref name="share"/> of the ground under them (composite.pshader's).</summary>
    float PuddleLevel(State& state, float share)
    {
        return LevelOf(state.PuddleSamples, PuddleField, share);
    }

    /// <summary>How far showers come and go in <paramref name="kind"/>, as set (0: it falls steadily).</summary>
    float ShowersOf(const State& state, MCWeatherKind kind)
    {
        return std::clamp(MCWeather::LookOf(kind).Showers * state.Settings.Showers, 0.0f, MostShowers);
    }

    /// <summary>A smooth step from 0 at <paramref name="from"/> to 1 at <paramref name="to"/>.</summary>
    float SmoothStep(float from, float to, float x)
    {
        const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// <summary>
    /// How hard the rain falls at the showers' time <paramref name="time"/> (seconds), showers <paramref name="showers"/>
    /// far: 1 as made, 1 - showers at a lull's deepest, 1 + showers at a burst's height.
    /// </summary>
    float RainAt(float showers, float time)
    {
        const float noise = Layered(time / ShowerLength, ShowerRow, ShowerApartX, ShowerApartY);
        return 1.0f + showers * (2.0f * SmoothStep(0.5f - ShowerSpread, 0.5f + ShowerSpread, noise) - 1.0f);
    }

    /// <summary>
    /// How far the puddles have spread at the showers' time <paramref name="time"/> (seconds), showers
    /// <paramref name="showers"/> far: none at a mission's start, filling toward how hard the rain falls (PuddleFill),
    /// drying toward it slowly (PuddleDry); between the steps taken, as far as the two round it.
    /// </summary>
    float PuddlesAt(State& state, float showers, float time)
    {
        if (state.FilledFor != showers || state.Filled.empty())
        {
            state.Filled.assign(1, 0.0f);
            state.FilledFor = showers;
        }

        const float steps = std::isfinite(time) ? std::max(time, 0.0f) / PuddleStep : 0.0f;
        const float whole = std::floor(steps);
        const auto at = static_cast<size_t>(whole);

        while (state.Filled.size() <= at + 1)
        {
            const float before = state.Filled.back();
            const float rain = RainAt(showers, (static_cast<float>(state.Filled.size() - 1) + 0.5f) * PuddleStep);
            const float over = rain > before ? PuddleFill : PuddleDry;
            state.Filled.push_back(before + (rain - before) * (1.0f - std::exp(-PuddleStep / over)));
        }

        return std::lerp(state.Filled[at], state.Filled[at + 1], steps - whole);
    }

    /// <summary>A shape's frame's columns, measured once (mirrored when <paramref name="reverse"/>).</summary>
    MCOptionalRef<const FrameColumns> ColumnsOf(State& state, std::span<const uint8_t> table, int32_t frame,
                                                bool reverse)
    {
        if (!VfxIsShapeTable(table) || frame < 0 || frame >= VfxShapeCount(table))
        {
            return std::nullopt;
        }

        const auto [found, isNew] =
            state.Frames.try_emplace(FrameKey{MCAddressKey::OfData(table), table.size(), frame, reverse});
        FrameColumns& columns = found->second;

        if (!isNew)
        {
            return columns;
        }

        std::vector<std::pair<int16_t, int16_t>> pixels;
        VfxShapeVisit(table, frame, [&](int32_t x, int32_t y, uint8_t)
                      { pixels.emplace_back(static_cast<int16_t>(reverse ? -x : x), static_cast<int16_t>(y)); });

        if (pixels.empty())
        {
            return columns;
        }

        const auto [low, high] = std::ranges::minmax(pixels | std::views::keys);
        columns.Left = low;
        const auto width = static_cast<size_t>(high - low + 1);
        columns.Top.assign(width, std::numeric_limits<int16_t>::max());
        columns.Bottom.assign(width, std::numeric_limits<int16_t>::min());
        std::ranges::sort(pixels);

        for (size_t i = 0; i < pixels.size(); ++i)
        {
            const auto [x, y] = pixels[i];
            const auto column = static_cast<size_t>(x - low);
            columns.Top[column] = std::min(columns.Top[column], y);
            columns.Bottom[column] = std::max(columns.Bottom[column], y);

            // Sorted by column, then row: the pixel under this one is the next, or none is drawn.
            if (i + 1 == pixels.size() || pixels[i + 1].first != x || pixels[i + 1].second != y + 1)
            {
                columns.LowerEdges.emplace_back(x, y);
            }
        }

        return columns;
    }

    /// <summary>
    /// A footprint's shape (shape table <paramref name="table"/>'s frame <paramref name="frame"/>) as MCWeather::
    /// PrintCircles circles round its pixels, measured once: its pixels grouped round as many seeds (each the pixel
    /// farthest from those before; the first, from their middle), each seed moved to its group's middle PrintRounds times;
    /// each circle reaching its group's farthest pixel. None for a frame without pixels.
    /// </summary>
    MCOptionalRef<const PrintShape> PrintShapeOf(State& state, std::span<const uint8_t> table, int32_t frame)
    {
        if (!VfxIsShapeTable(table) || frame < 0 || frame >= VfxShapeCount(table))
        {
            return std::nullopt;
        }

        const auto [found, isNew] =
            state.Prints.try_emplace(FrameKey{MCAddressKey::OfData(table), table.size(), frame, false});
        std::optional<PrintShape>& shape = found->second;

        if (!isNew)
        {
            return shape.has_value() ? MCOptionalRef<const PrintShape>(*shape) : std::nullopt;
        }

        // The pixels' middles.
        std::vector<std::pair<float, float>> pixels;
        VfxShapeVisit(table, frame, [&](int32_t x, int32_t y, uint8_t)
                      { pixels.emplace_back(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f); });

        if (pixels.empty())
        {
            return std::nullopt;
        }

        const auto apart = [](const std::pair<float, float>& a, const std::pair<float, float>& b)
        { return std::hypot(a.first - b.first, a.second - b.second); };
        std::pair<float, float> middle;

        for (const auto& [x, y] : pixels)
        {
            middle.first += x / static_cast<float>(pixels.size());
            middle.second += y / static_cast<float>(pixels.size());
        }

        std::array<std::pair<float, float>, MCWeather::PrintCircles> seeds;

        for (size_t k = 0; k < seeds.size(); ++k)
        {
            // The pixel farthest from the seeds so far (from the middle, for the first).
            const auto nearestSeed = [&](const std::pair<float, float>& pixel)
            {
                float nearest = k == 0 ? apart(pixel, middle) : std::numeric_limits<float>::max();

                for (size_t j = 0; j < k; ++j)
                {
                    nearest = std::min(nearest, apart(pixel, seeds[j]));
                }

                return nearest;
            };

            seeds[k] = *std::ranges::max_element(pixels, {}, nearestSeed);
        }

        const auto groupOf = [&](const std::pair<float, float>& pixel)
        {
            return static_cast<size_t>(
                std::ranges::min_element(seeds, {}, [&](const auto& seed) { return apart(pixel, seed); }) -
                seeds.begin());
        };

        for (int32_t round = 0; round < PrintRounds; ++round)
        {
            std::array<std::pair<float, float>, MCWeather::PrintCircles> sums{};
            std::array<size_t, MCWeather::PrintCircles> counts{};

            for (const auto& pixel : pixels)
            {
                const size_t group = groupOf(pixel);
                sums[group].first += pixel.first;
                sums[group].second += pixel.second;
                ++counts[group];
            }

            for (size_t k = 0; k < seeds.size(); ++k)
            {
                if (counts[k] > 0)
                {
                    seeds[k] = {sums[k].first / static_cast<float>(counts[k]),
                                sums[k].second / static_cast<float>(counts[k])};
                }
            }
        }

        // Each circle round its group, reaching its farthest pixel's far side.
        PrintShape& circles = shape.emplace();

        for (size_t k = 0; k < seeds.size(); ++k)
        {
            circles[k] = {seeds[k].first, seeds[k].second, 0.0f};
        }

        for (const auto& pixel : pixels)
        {
            ViewCircle& circle = circles[groupOf(pixel)];
            circle.Radius = std::max(circle.Radius, apart(pixel, {circle.X, circle.Y}) + 0.5f);
        }

        return circles;
    }

    /// <summary>
    /// Whether <paramref name="unit"/>'s top at its column <paramref name="column"/> is a flat stretch: FlatInside
    /// columns in from its sides, the columns FlatReach to each side drawn, their tops within FlatStep of its (not its
    /// outline's edge, nor an aerial).
    /// </summary>
    bool FlatAt(const StandingUnit& unit, int32_t column)
    {
        if (column < FlatInside || static_cast<size_t>(column + FlatInside) >= unit.Top.size())
        {
            return false;
        }

        const float top = unit.Top[static_cast<size_t>(column)];
        const auto near = [&](int32_t at)
        {
            return at >= 0 && static_cast<size_t>(at) < unit.Top.size() &&
                   std::abs(unit.Top[static_cast<size_t>(at)] - top) <= FlatStep;
        };

        return near(column - FlatReach) && near(column + FlatReach);
    }

    /// <summary>
    /// Whether a drop landing at view point (x, y) lands on a flat stretch of one of <paramref name="units"/>' tops
    /// (FlatAt), where a glint shows.
    /// </summary>
    bool OnFlatTop(std::span<const StandingUnit> units, float x, float y)
    {
        return std::ranges::any_of(units,
                                   [&](const StandingUnit& unit)
                                   {
                                       const auto column = static_cast<int32_t>(std::floor(x)) - unit.Left;

                                       if (column < 0 || static_cast<size_t>(column) >= unit.Top.size())
                                       {
                                           return false;
                                       }

                                       const float top = unit.Top[static_cast<size_t>(column)];
                                       return y >= top - OnTopReach && y <= top + Band + OnTopReach &&
                                              FlatAt(unit, column);
                                   });
    }

    /// <summary>Where on <paramref name="unit"/> a drop meant for the ground at view point (x, y) lands, or none.</summary>
    std::optional<float> HitOn(const StandingUnit& unit, float x, float y)
    {
        const auto column = static_cast<int32_t>(std::floor(x)) - unit.Left;
        const float back = unit.FeetY - unit.Footprint;

        if (column < 0 || static_cast<size_t>(column) >= unit.Top.size() || y < back || y > unit.FeetY ||
            !std::isfinite(unit.Top[static_cast<size_t>(column)]))
        {
            return std::nullopt;
        }

        const float top = unit.Top[static_cast<size_t>(column)];
        return top + std::min(Band, unit.FeetY - top) * (y - back) / unit.Footprint;
    }

    /// <summary>
    /// Where a look's puddles lie (PuddlesOf): none, or where their field is under Level (as the composite lays them).
    /// </summary>
    struct Puddles
    {
        bool Shown = false;
        float Level = 0.0f;
    };

    /// <summary>
    /// Where <paramref name="look"/>'s puddles lie: only with the wet look (the relief maps), on as much of the ground as
    /// it says (PuddleLevel, its levels measured now if not yet).
    /// </summary>
    Puddles PuddlesOf(State& state, const MCWeather::MCWeatherLook& look)
    {
        return look.Puddles > 0.0f && MCSceneLights::ReliefMapsOn()
                   ? Puddles{.Shown = true, .Level = PuddleLevel(state, look.Puddles)}
                   : Puddles{};
    }

    /// <summary>
    /// Whether ground drawn in palette entry <paramref name="drawn"/> at world point (x, y, z) lies in one of
    /// <paramref name="puddles"/>, <paramref name="margin"/> inside its level: where the composite lays them, not on
    /// grass nor cliffs.
    /// </summary>
    bool InPuddle(const Puddles& puddles, std::optional<uint8_t> drawn, float x, float y, float z, float margin)
    {
        if (!puddles.Shown || !drawn.has_value() || IsGrassIndex(*drawn) || BlockedShare(x, y) >= BlockedDryFrom)
        {
            return false;
        }

        const auto [east, north] = FlatGround(x, y, z);
        return PuddleField(east, north) < puddles.Level - margin;
    }

    /// <summary>
    /// Ring <paramref name="ring"/> spreading on water or a puddle; with <paramref name="twice"/>, a second inside it a
    /// moment later.
    /// </summary>
    void Rings(State& state, Splash ring, bool twice)
    {
        state.Splashes.push_back(ring);

        if (twice)
        {
            ring.Age = -SecondRingAfter;
            ring.Size *= SecondRingShare;
            ring.Life *= SecondRingLife;
            ring.Alpha *= SecondRingAlpha;
            ring.Color = PaleBlue;
            state.Splashes.push_back(ring);
        }
    }

    /// <summary>
    /// The droplets <paramref name="thrown"/> up from world point (x, y, z) with <paramref name="dice"/>, spread round a
    /// little unevenly (each its own way out, most of them up), drawn at <paramref name="depth"/>.
    /// </summary>
    void Droplets(State& state, MCDice& dice, float x, float y, float z, const DropletThrow& thrown, int32_t depth)
    {
        for (int32_t k = 0; k < thrown.Count; ++k)
        {
            const float side = k % 2 == 0 ? -1.0f : 1.0f;
            const bool pale = dice.Unit() < PaleDropletShare;
            const float across = thrown.Spread > 0.0f ? dice.Between(-thrown.Spread, thrown.Spread) : 0.0f;
            const float late = thrown.Stagger > 0.0f ? dice.Between(0.0f, thrown.Stagger) : 0.0f;
            state.Splashes.push_back({.Kind = SplashKind::Droplet,
                                      .X = x,
                                      .Y = y,
                                      .Z = z,
                                      .Across = across,
                                      .VAcross = side * dice.Between(DropletOutLow, DropletOutHigh) * thrown.Out,
                                      .VUp = dice.Between(DropletUpLow, DropletUpHigh) * thrown.Up,
                                      .Size = thrown.Size,
                                      .Age = -late,
                                      .Color = pale ? PaleGrey : PalestBlue,
                                      .Alpha = thrown.Alpha,
                                      .FadeTo = thrown.FadeTo,
                                      .Depth = depth});
            Splash& droplet = state.Splashes.back();
            droplet.Life = 2.0f * droplet.VUp / Gravity;
        }
    }

    /// <summary>
    /// Whether ground drawn in palette entry <paramref name="drawn"/> (the ground's tile, MCParticles::GroundDrawnAt)
    /// at world point (x, y, z) is wet: the water drawn, or one of <paramref name="puddles"/> (<paramref name="margin"/>
    /// inside its level); never under a bridge, whose road is drawn over the water.
    /// </summary>
    bool IsWet(const Puddles& puddles, std::optional<uint8_t> drawn, float x, float y, float z, float margin)
    {
        return !MCParticles::BridgedAt(x, y) &&
               (InPuddle(puddles, drawn, x, y, z, margin) || (drawn.has_value() && MCParticles::IsWaterColor(*drawn)));
    }

    /// <summary>
    /// What a drop landing at world point (x, y, z), not on a unit, splashes on (MCWeather::GroundAt), as
    /// <paramref name="ground"/> is drawn, with <paramref name="puddles"/>.
    /// </summary>
    MCWeather::MCSplashGround GroundUnder(const Puddles& puddles, const MCParticles::MCDrawnGround& ground, float x,
                                          float y, float z)
    {
        std::array<MCParticles::MCDrawnPixel, GroundVotes.size()> drawn;
        ground.Round(x, y, z, GroundVotes, drawn);

        if (IsWet(puddles, drawn[0].Ground, x, y, z, PuddleMargin))
        {
            return MCWeather::MCSplashGround::Water;
        }

        // The land's kinds the pixels round it vote for (the water's colours at a shore vote for none, nor do shadows).
        std::array<int32_t, GroundKinds> votes{};
        std::optional<MCWeather::MCSplashGround> first;

        for (const MCParticles::MCDrawnPixel& pixel : drawn)
        {
            const std::optional<uint8_t> surface = pixel.Surface;

            if (!surface.has_value())
            {
                continue;
            }

            const MCWeather::MCSplashGround ground = MCWeather::GroundOf(*surface);
            const MCVfxRgb color = GamePalette().Colors()[*surface];

            if (ground == MCWeather::MCSplashGround::Water || std::max({color.R, color.G, color.B}) < ShadowDark)
            {
                continue;
            }

            first = first.value_or(ground);
            ++votes[static_cast<size_t>(ground)];
        }

        auto best = static_cast<size_t>(first.value_or(MCWeather::MCSplashGround::Hard));

        for (size_t kind = 0; kind < votes.size(); ++kind)
        {
            best = votes[kind] > votes[best] ? kind : best;
        }

        return static_cast<MCWeather::MCSplashGround>(best);
    }

    /// <summary>A mark of a splash's own (a dot, a soak, a puff, a glint) where <paramref name="drop"/>
    /// lands.</summary>
    Splash MarkOf(const Drop& drop, SplashKind kind, float size, float life, const Rgb& color, float alpha,
                  int32_t depth)
    {
        return {.Kind = kind,
                .X = drop.X,
                .Y = drop.Y,
                .Z = drop.Z,
                .Size = size,
                .Life = life,
                .Color = color,
                .Alpha = std::min(alpha, 1.0f),
                .Depth = depth};
    }

    /// <summary>
    /// <paramref name="drop"/> landing, its splash fitting the ground as strongly as MCWeatherSettings::Ground says:
    /// off hard ground, droplets thrown up, crisp; into soil, a dark dot soaking in; in grass, a soft puff; on a unit's
    /// metal top, a glint and droplets. False at a strength of 0 and in water or a puddle, which splash as ever.
    /// </summary>
    bool ThrowFitting(State& state, const Drop& drop, MCWeather::MCSplashGround ground, const MCCamera& camera)
    {
        const float strength = std::max(state.Settings.Ground, 0.0f);

        if (strength <= 0.0f || ground == MCWeather::MCSplashGround::Water)
        {
            return false;
        }

        const DropSize& size = DropSizes[drop.Size];
        MCDice& dice = state.SplashDice;
        const int32_t count = size.DropletsHigh > 0 ? dice.Between(size.DropletsLow, size.DropletsHigh) : 0;
        DropletThrow thrown{.Count = count,
                            .Out = size.Throw,
                            .Up = size.Throw,
                            .Size = drop.Size == NearDrop ? 2.0f * DropletSize : DropletSize};

        switch (ground)
        {
            case MCWeather::MCSplashGround::Soil:
            {
                state.Splashes.push_back(MarkOf(drop, SplashKind::Soak, size.Soak, SoakLife, SoakBrown,
                                                SoakAlpha * strength, GroundDepthFixup + drop.Depth));
                return true;
            }

            case MCWeather::MCSplashGround::Grass:
            {
                state.Splashes.push_back(
                    MarkOf(drop, SplashKind::Puff, size.Puff, PuffLife, SplashGrey, PuffAlpha * strength, drop.Depth));
                return true;
            }

            case MCWeather::MCSplashGround::Metal:
            {
                if (const MCVector2D at = Project(drop.X, drop.Y, drop.Z, camera); OnFlatTop(state.Units, at.X, at.Y))
                {
                    const float far = drop.Size == 0 ? FarGlint : 1.0f;
                    Splash& glint =
                        state.Splashes.emplace_back(MarkOf(drop, SplashKind::Glint, GlintWidth, GlintLife, GlintWhite,
                                                           GlintAlpha * far * strength, drop.Depth));
                    glint.Up = -GlintInset;
                }

                break;
            }

            case MCWeather::MCSplashGround::Hard:
            {
                state.Splashes.push_back(
                    MarkOf(drop, SplashKind::Dot, size.Dot * 0.5f, DotLife, SplashGrey, DotAlpha, drop.Depth));
                thrown.Count += count > 0 ? HardDroplets : 0;
                thrown.Alpha = HardDropletAlpha * strength;
                thrown.FadeTo = HardFadeTo;
                break;
            }

            case MCWeather::MCSplashGround::Water:
                break;
        }

        thrown.Alpha = std::min(thrown.Alpha, 1.0f);
        Droplets(state, dice, drop.X, drop.Y, drop.Z, thrown, drop.Depth);
        return true;
    }

    /// <summary>
    /// What <paramref name="drop"/> lands on, as <paramref name="ground"/> is drawn, with <paramref name="puddles"/>: a
    /// flake, the water drawn (not under a bridge) or not (Hard, for any land or a unit's top); a drop, a unit's top
    /// (Metal) or what GroundUnder takes the ground for. It changes nothing, and once the ground's tiles are read in
    /// (MCParticles::MCDrawnGround::ReadTiles) it only reads, so the helpers find many at once (Landings).
    /// </summary>
    MCWeather::MCSplashGround LandsOn(const Puddles& puddles, const MCParticles::MCDrawnGround& ground,
                                      const Drop& drop)
    {
        if (drop.Flake)
        {
            const std::optional<uint8_t> drawn = drop.OnUnit ? std::nullopt : ground.At(drop.X, drop.Y, drop.Z);
            return drawn.has_value() && MCParticles::IsWaterColor(*drawn) && !MCParticles::BridgedAt(drop.X, drop.Y)
                       ? MCWeather::MCSplashGround::Water
                       : MCWeather::MCSplashGround::Hard;
        }

        return drop.OnUnit ? MCWeather::MCSplashGround::Metal : GroundUnder(puddles, ground, drop.X, drop.Y, drop.Z);
    }

    /// <summary>
    /// <paramref name="drop"/> landing on <paramref name="ground"/> (LandsOn's): with MCWeatherSettings::Ground, as the
    /// ground there takes it (ThrowFitting); else a dot where it lands; in water or a puddle, a ring (a near drop's
    /// two); on land or a unit, droplets thrown up.
    /// </summary>
    void Throw(State& state, const Drop& drop, MCWeather::MCSplashGround ground, const MCCamera& camera)
    {
        // A flake rests a moment where it lands, a pixel of it, lying on the ground (or on a unit's top); on the water
        // (not under a bridge) it's gone.
        if (drop.Flake)
        {
            if (ground != MCWeather::MCSplashGround::Water)
            {
                state.Splashes.push_back(MarkOf(drop, SplashKind::Flake, FlakeDot, FlakeRest,
                                                drop.Size == FarFlake ? FlakeWhite : FlakeBrightest, drop.Alpha,
                                                drop.OnUnit ? drop.Depth : GroundDepthFixup + drop.Depth));
            }

            return;
        }

        const DropSize& size = DropSizes[drop.Size];
        MCDice& dice = state.SplashDice;

        if (ThrowFitting(state, drop, ground, camera))
        {
            return;
        }

        const bool wet = ground == MCWeather::MCSplashGround::Water;
        state.Splashes.push_back(MarkOf(drop, SplashKind::Dot, size.Dot * 0.5f, DotLife, wet ? PalestBlue : SplashGrey,
                                        DotAlpha, drop.Depth));

        if (wet)
        {
            Rings(state,
                  {.Kind = SplashKind::Ring,
                   .X = drop.X,
                   .Y = drop.Y,
                   .Z = drop.Z,
                   .Size = size.Ring * dice.Between(1.0f - RingSizeVary, 1.0f + RingSizeVary),
                   .Life = size.RingLife,
                   .Color = dice.Unit() < RingPaleShare ? PaleBlue : WaterBlue,
                   .Alpha = size.RingAlpha * dice.Between(RingAlphaLeast, 1.0f),
                   .Depth = GroundDepthFixup + drop.Depth},
                  drop.Size == NearDrop);

            if (drop.Size != NearDrop)
            {
                return;
            }
        }

        const int32_t count = size.DropletsHigh > 0 ? dice.Between(size.DropletsLow, size.DropletsHigh) : 0;
        Droplets(state, dice, drop.X, drop.Y, drop.Z,
                 {.Count = count,
                  .Out = size.Throw,
                  .Up = size.Throw,
                  .Size = drop.Size == NearDrop ? 2.0f * DropletSize : DropletSize},
                 drop.Depth);
    }

    /// <summary>
    /// <paramref name="step"/>'s foot coming down: in a puddle (as <paramref name="camera"/> shows the ground there), a
    /// spray of droplets thrown up round it, and the puddle reacting round it (the newest MCWeather::MaxPuddleSteps), as
    /// big as MCWeatherSettings::Steps has them.
    /// </summary>
    void Step(State& state, const PendingStep& step, const MCCamera& camera)
    {
        const float strength = std::max(state.Settings.Steps, 0.0f);

        if (strength <= 0.0f || !MCWeather::PuddleAt(MCVector3D(step.X, step.Y, step.Z), camera))
        {
            return;
        }

        const int32_t depth = -static_cast<int32_t>(std::floor(Project(step.X, step.Y, step.Z, camera).Y));
        MCDice& dice = state.StepDice;
        Droplets(state, dice, step.X, step.Y, step.Z,
                 {.Count = dice.Count(StepDroplets * strength),
                  .Out = StepOut * strength,
                  .Up = StepUp * strength,
                  .Spread = StepSpread * strength,
                  .Stagger = StepStagger,
                  .Size = StepDropletSize,
                  .Alpha = StepDropletAlpha},
                 depth);
        const auto [east, north] = FlatGround(step.X, step.Y, step.Z);
        MCWeather::MCPuddleStep& puddle =
            state.PuddleSteps.emplace_back(MCWeather::MCPuddleStep{.East = east, .North = north, .Strength = strength});
        const MCOptionalRef<const PrintShape> print = PrintShapeOf(state, step.Print, step.PrintFrame);
        const float cosine = std::max(MCTerrain::ProjectionCos, MinProjection);
        const float sine = std::max(MCTerrain::ProjectionSin, MinProjection);

        for (size_t k = 0; k < puddle.Print.size(); ++k)
        {
            if (!print)
            {
                puddle.Print[k] = {east, north, PrintFallback};
                continue;
            }

            // The flat projection undone (GroundPointAt's): view pixels across and down to meters east and north; a
            // radius as wide as the view's.
            const ViewCircle& circle = print->get()[k];
            const float across = circle.X / cosine;
            const float down = circle.Y / sine;
            puddle.Print[k] = {east + 0.5f * (across + down), north - 0.5f * (down - across),
                               circle.Radius / (cosine * std::numbers::sqrt2_v<float>)};
        }

        if (state.PuddleSteps.size() > MCWeather::MaxPuddleSteps)
        {
            state.PuddleSteps.erase(state.PuddleSteps.begin());
        }
    }

    /// <summary>The feet's puddles react on for <paramref name="step"/> seconds of the game's time, till StepLife.</summary>
    void AgePuddleSteps(State& state, float step)
    {
        for (MCWeather::MCPuddleStep& puddle : state.PuddleSteps)
        {
            puddle.Age += step;
        }

        std::erase_if(state.PuddleSteps, [](const MCWeather::MCPuddleStep& puddle) { return puddle.Age > StepLife; });
    }

    /// <summary>
    /// <paramref name="drop"/>'s depth in the draw list, where it lands on the ground; or, when it falls where a unit
    /// drawn this frame stands, on the unit's top, just before its parts.
    /// </summary>
    void LandOnUnits(const State& state, Drop& drop, const MCCamera& camera)
    {
        const MCVector2D at = Project(drop.X, drop.Y, drop.Z, camera);
        drop.Depth = -static_cast<int32_t>(std::floor(at.Y));

        for (const StandingUnit& unit : state.Units)
        {
            if (const std::optional<float> top = HitOn(unit, at.X, at.Y); top.has_value())
            {
                drop.OnUnit = true;
                drop.Z += at.Y - *top;
                drop.Depth = unit.Depth - 1;
                return;
            }
        }
    }

    /// <summary>
    /// The drops the tools and tests landed this frame (MCWeather::AddDrop), splashing at once under
    /// <paramref name="look"/>.
    /// </summary>
    void Land(State& state, const MCWeather::MCWeatherLook& look, const MCCamera& camera)
    {
        if (state.Landing.empty())
        {
            return;
        }

        const Puddles puddles = PuddlesOf(state, look);
        const MCParticles::MCDrawnGround ground(camera);

        for (const PendingDrop& pending : state.Landing)
        {
            // In snow, a flake as strong as its size's strongest.
            const bool flake = look.Snows;
            Drop drop{.X = pending.Point.X,
                      .Y = pending.Point.Y,
                      .Z = pending.Point.Z,
                      .Size = pending.Size,
                      .Flake = flake,
                      .Alpha = flake ? FlakeSizes[pending.Size].AlphaHigh : 0.0f};
            LandOnUnits(state, drop, camera);
            Throw(state, drop, LandsOn(puddles, ground, drop), camera);
        }
    }

    /// <summary>The share of the drops thrown that are of size <paramref name="index"/> (Rain's pick by the look's Sizes).</summary>
    float SizeShare(const MCWeather::MCWeatherLook& look, size_t index)
    {
        float below = 0.0f;

        for (size_t i = 0; i < index; ++i)
        {
            below += look.Sizes[i];
        }

        // The last takes every pick past the others'.
        const float upTo = index + 1 < look.Sizes.size() ? below + look.Sizes[index] : 1.0f;
        return std::clamp(upTo, 0.0f, 1.0f) - std::clamp(below, 0.0f, 1.0f);
    }

    /// <summary>
    /// Port-only (HD weather's snow, MCWeatherPace): the share of each size's flakes thrown real when the frames keep
    /// up with <paramref name="share"/> of them all: the farthest go first, their specks drawn as they are, then those
    /// between, the nearest last.
    /// </summary>
    std::array<float, FlakeSizes.size()> PacedFlakes(const MCWeather::MCWeatherLook& look, float share)
    {
        std::array<float, FlakeSizes.size()> kept{};
        float cut = 1.0f - share;

        for (size_t size = 0; size < kept.size(); ++size)
        {
            const float sizeShare = SizeShare(look, size);
            const float taken = std::min(cut, sizeShare);
            kept[size] = sizeShare > 0.0f ? 1.0f - taken / sizeShare : 1.0f;
            cut -= taken;
        }

        return kept;
    }

    /// <summary>
    /// The share of a size's flakes still falling <paramref name="age"/> seconds after they were thrown: those whose fall
    /// over their speed (each even through its range) is longer.
    /// </summary>
    template <typename Size> float StillFalling(const Size& size, float age)
    {
        if (age <= 0.0f)
        {
            return 1.0f;
        }

        float share = 0.0f;

        for (size_t i = 0; i < StillFallingSpeeds; ++i)
        {
            const float speed = std::lerp(size.SpeedLow, size.SpeedHigh,
                                          (static_cast<float>(i) + 0.5f) / static_cast<float>(StillFallingSpeeds));
            share += std::clamp((size.FallHigh - age * speed) / (size.FallHigh - size.FallLow), 0.0f, 1.0f);
        }

        return share / static_cast<float>(StillFallingSpeeds);
    }

    /// <summary>How fast a size's flakes in the air fall on average (each as long in it as its fall over its speed).</summary>
    template <typename Size> float SeenSpeed(const Size& size)
    {
        return (size.SpeedHigh - size.SpeedLow) / std::log(size.SpeedHigh / size.SpeedLow);
    }

    /// <summary>The longest a drop falls and its splash's specks (or, on water, its rings') show, seconds.</summary>
    float LongestDrop()
    {
        float longest = 0.0f;
        float shown = SplashSpeckLife;

        for (const DropSize& size : DropSizes)
        {
            longest = std::max(longest, size.FallHigh / size.SpeedLow);
            shown = std::max(shown, size.RingLife);
        }

        return longest + shown;
    }

    /// <summary>
    /// The light a ring spreading on water (Rings) leaves over its <paramref name="life"/>: its outline a view pixel
    /// wide round the ground's ellipse (<paramref name="aspect"/> as tall as wide), growing from RingFrom to
    /// <paramref name="size"/> as the root of its age, times its strength fading from <paramref name="alpha"/> (its
    /// middle between RingAlphaLeast and all of it) to none; view pixels squared times strength times seconds.
    /// </summary>
    float RingLight(float size, float life, float alpha, float aspect)
    {
        // Over its age a, 0 to 1: (1 - a) times (RingFrom + (size - RingFrom) * sqrt(a)).
        const float grown = 0.5f * RingFrom + (size - RingFrom) * (4.0f / 15.0f);
        return life * std::numbers::pi_v<float> * (1.0f + aspect) * grown * alpha * 0.5f * (1.0f + RingAlphaLeast);
    }

    /// <summary>The longest a flake falls and rests, seconds.</summary>
    float LongestFlake()
    {
        float longest = 0.0f;

        for (const FlakeSize& size : FlakeSizes)
        {
            longest = std::max(longest, size.FallHigh / size.SpeedLow);
        }

        return longest + FlakeRest;
    }

    /// <summary>
    /// The drops thrown this frame (<paramref name="step"/> seconds of the game's time) over <paramref name="camera"/>'s
    /// view, as many as the weather's rate, more where the veil's sheets are thick (no more at once than MaxDrops, more
    /// in a shower's <paramref name="burst"/>, MCShowers::Rain); those falling where a unit stands land on its top.
    /// </summary>
    void Rain(State& state, const MCCamera& camera, const MCWeather::MCWeatherLook& look, float step, float time,
              float burst)
    {
        MCDice& dice = state.Dice;
        const float width = camera.HalfWidth * 2.0f;
        const float height = camera.HalfHeight * 2.0f;
        // Flakes fall into the view from farther upwind and below it than drops, the nearest drifting farthest.
        const float landLeft =
            look.Snows ? LandLeft + look.Drift * (FlakeCalm + FlakeGusts) * FlakeSizes[NearFlake].FallHigh : LandLeft;
        const float landBelow = look.Snows ? FlakeBelow : LandBelow;
        // Port-only (HD weather): zoomed out past MCWeather::FullFlakeLines (MCWeather::FullDropLines in rain), the
        // view throws no more flakes or drops than one showing that many lines; the composite draws the rest as specks
        // or streaks (LeftOut).
        const float full = look.Snows ? MCWeather::FullFlakeLines : MCWeather::FullDropLines;
        const float kept = height > full ? (full / height) * (full / height) : 1.0f;
        const float area = (width + landLeft + LandRight) * (height + landBelow) * kept;
        const float rate = look.Drops * state.Settings.Amount * area / (DropsArea * DropsArea) * SheetMost;
        // Port-only (HD weather's snow): on a machine whose frames can't keep up with them all, fewer flakes are
        // thrown real (MCWeatherPacing), the farthest first; the specks draw the rest too.
        const float pace = look.Snows ? MCWeatherPacing::Share() : 1.0f;
        const std::array<float, FlakeSizes.size()> paced = PacedFlakes(look, pace);

        if (kept < 1.0f || pace < 1.0f)
        {
            LeftOutFlakes& left = state.LeftOut.emplace_back(LeftOutFlakes{.Time = time, .Snows = look.Snows});

            for (size_t size = 0; size < left.Thrown.size(); ++size)
            {
                left.Thrown[size] = (1.0f - kept * paced[size]) * look.Drops * state.Settings.Amount /
                                    (DropsArea * DropsArea) * SheetLeast * SizeShare(look, size) * step;
            }
        }

        const float most = look.Snows ? MaxFlakes : MaxDrops;
        state.Most = static_cast<size_t>(most * std::max(burst, 1.0f) * area / (DropsArea * DropsArea));
        const int32_t count = dice.Count(rate * step);

        for (int32_t i = 0; i < count && state.Drops.size() + state.Splashes.size() < state.Most; ++i)
        {
            const float viewX = dice.Between(-landLeft, width + LandRight);
            const float viewY = dice.Between(0.0f, height + landBelow);
            const float thin = dice.Unit() * SheetMost;
            const float pick = dice.Unit();
            const MCVector3D ground = GroundPointAt(viewX, viewY, camera);
            const auto [east, north] = FlatGround(ground.X, ground.Y, ground.Z);

            // More drops where the sheets are thick (thinned from the most).
            if (thin > SheetLeast + (SheetMost - SheetLeast) * SheetsAt(east, north, time))
            {
                continue;
            }

            uint8_t sizeIndex = 0;

            for (float sum = look.Sizes[0]; sizeIndex + 1u < DropSizes.size() && pick >= sum;)
            {
                ++sizeIndex;
                sum += look.Sizes[sizeIndex];
            }

            if (look.Snows)
            {
                // Paced out: a speck instead (the die thrown only then, so the whole share throws as before).
                if (paced[sizeIndex] < 1.0f && dice.Unit() >= paced[sizeIndex])
                {
                    continue;
                }

                const FlakeSize& flake = FlakeSizes[sizeIndex];
                Drop drop{.X = ground.X,
                          .Y = ground.Y,
                          .Z = ground.Z,
                          .Size = sizeIndex,
                          .Flake = true,
                          .Rock = dice.Between(flake.RockLow, flake.RockHigh),
                          .RockRate = dice.Between(flake.RateLow, flake.RateHigh),
                          .Phase = dice.Between(0.0f, 2.0f * std::numbers::pi_v<float>),
                          .Fall = dice.Between(flake.FallLow, flake.FallHigh),
                          .Speed = dice.Between(flake.SpeedLow, flake.SpeedHigh),
                          .Alpha = dice.Between(flake.AlphaLow, flake.AlphaHigh),
                          .Slant = look.Drift * (FlakeCalm + FlakeGusts * GustAt(east, north, time)) +
                                   dice.Between(-FlakeJitter, FlakeJitter)};
                LandOnUnits(state, drop, camera);
                state.Drops.push_back(drop);
                continue;
            }

            const DropSize& size = DropSizes[sizeIndex];
            Drop drop{.X = ground.X,
                      .Y = ground.Y,
                      .Z = ground.Z,
                      .Size = sizeIndex,
                      .Fall = dice.Between(size.FallLow, size.FallHigh),
                      .Speed = dice.Between(size.SpeedLow, size.SpeedHigh),
                      .Length = dice.Between(size.LengthLow, size.LengthHigh),
                      .Alpha = dice.Between(size.AlphaLow, size.AlphaHigh),
                      .Slant = WindLow + (WindHigh - WindLow) * GustAt(east, north, time) +
                               dice.Between(-WindJitter, WindJitter)};
            LandOnUnits(state, drop, camera);
            state.Drops.push_back(drop);
        }
    }

    /// <summary>Water dripping this frame off the lower edges of the units drawn, as many as the weather's drips.</summary>
    void Drip(State& state, const MCWeather::MCWeatherLook& look, float step)
    {
        MCDice& dice = state.Dice;

        for (const StandingUnit& unit : state.Units)
        {
            if (unit.LowerEdges.empty())
            {
                continue;
            }

            const int32_t count = dice.Count(look.Drips * state.Settings.Amount * step);

            for (int32_t i = 0; i < count && state.Drops.size() + state.Splashes.size() < state.Most; ++i)
            {
                const auto& [x, y] = unit.LowerEdges[static_cast<size_t>(
                    dice.Between(0, static_cast<int32_t>(unit.LowerEdges.size()) - 1))];
                const float lands =
                    std::max(DripLandsLeast, unit.FeetY - dice.Unit() * DripLandsShare * unit.Footprint - y);
                state.Splashes.push_back({.Kind = SplashKind::Drip,
                                          .X = unit.X,
                                          .Y = unit.Y,
                                          .Z = unit.Z,
                                          .Across = x - unit.FeetX,
                                          .Up = unit.FeetY - y,
                                          .Drop = lands,
                                          .Size = DripSize,
                                          .Life = std::sqrt(2.0f * lands / Gravity),
                                          .Color = PalestBlue,
                                          .Alpha = DripAlpha,
                                          .Depth = unit.Depth - 1});
            }
        }
    }

    /// <summary>A share, 0 to 1, as a byte.</summary>
    uint8_t Byte(float value)
    {
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    /// <summary>Sets <paramref name="command"/>'s colour, premultiplied: <paramref name="color"/> at <paramref name="alpha"/>.</summary>
    void SetColor(MCParticleCommand& command, const Rgb& color, float alpha)
    {
        command.Red = Byte(color.Red * alpha);
        command.Green = Byte(color.Green * alpha);
        command.Blue = Byte(color.Blue * alpha);
        command.Alpha = Byte(alpha);
    }

    /// <summary>
    /// What the rain's drops and droplets catch of the frame's lights: the lights (the window's pixels, binned with the
    /// view's tilt, MCSceneLights::GroundAspect) and how much of their light (MCWeather::CatchGain). None caught
    /// without lights.
    /// </summary>
    struct Catching
    {
        MCSceneLights::MCAirLights Lights;
        float Gain = 0.0f;
    };

    /// <summary>
    /// Sets <paramref name="command"/>'s glint (MCParticleCommand::Glint): the light <paramref name="catching"/> shows
    /// on a drop or droplet <paramref name="height"/> view pixels above the ground at view point (<paramref name="x"/>,
    /// <paramref name="y"/>), with <paramref name="facet"/>.
    /// </summary>
    void SetGlint(MCParticleCommand& command, const Catching& catching, float x, float y, float height, float facet)
    {
        if (catching.Lights.Empty() || catching.Gain <= 0.0f)
        {
            return;
        }

        const std::array<float, 3> glint = MCWeather::Glint(catching.Lights.At(x, y, height), catching.Gain, facet);
        command.Glint = static_cast<uint32_t>(Byte(glint[0])) | static_cast<uint32_t>(Byte(glint[1])) << 8 |
                        static_cast<uint32_t>(Byte(glint[2])) << 16;
    }

    /// <summary>
    /// <paramref name="color"/> made no paler than <paramref name="most"/> (its brightest component), its hue kept: at
    /// dusk and night, a splash's, so the composite doesn't take it for a light's own pixel (MCLighting::SelfLit).
    /// </summary>
    Rgb NoPalerThan(const Rgb& color, float most)
    {
        const float brightest = std::max({color.Red, color.Green, color.Blue});

        if (brightest <= most || brightest <= 0.0f)
        {
            return color;
        }

        const float scale = most / brightest;
        return {color.Red * scale, color.Green * scale, color.Blue * scale};
    }

    /// <summary>Whether a mark round (x0, y0)-(x1, y1), <paramref name="reach"/> about it, shows in the view.</summary>
    bool InView(const MCCamera& camera, float x0, float y0, float x1, float y1, float reach)
    {
        const float right = camera.HalfWidth * 2.0f + ViewMargin;
        const float bottom = camera.HalfHeight * 2.0f + ViewMargin;
        return std::max(x0, x1) + reach >= -ViewMargin && std::max(y0, y1) + reach >= -ViewMargin &&
               std::min(x0, x1) - reach <= right && std::min(y0, y1) - reach <= bottom;
    }

    /// <summary>
    /// Adds <paramref name="command"/> to <paramref name="marks"/> at <paramref name="depth"/> if it shows.
    /// </summary>
    void Mark(Marks& marks, const MCCamera& camera, const MCParticleCommand& command, int32_t depth)
    {
        if (InView(camera, command.X0, command.Y0, command.X1, command.Y1,
                   command.Radius + std::max(command.Ring, 0.0f)))
        {
            marks.Commands.push_back(command);
            marks.Depths.push_back(depth);
        }
    }

    /// <summary>
    /// Adds falling flake <paramref name="drop"/> to <paramref name="marks"/>, hard edged, upwind of where it lands and
    /// above it, rocking to each side: far, a dot; between, one twice as wide; near, a plus three wide, brightest where
    /// its arms cross; in the palette's palest greys, no paler than <paramref name="palest"/>, catching the light by
    /// the lights as <paramref name="catching"/> says.
    /// </summary>
    void AddFlake(Marks& marks, const Drop& drop, const MCCamera& camera, const Catching& catching, float palest)
    {
        const MCVector2D at = Project(drop.X, drop.Y, drop.Z, camera);
        const float left = std::max(drop.Fall - drop.Age * drop.Speed, 0.0f);
        const float rock =
            drop.Rock * std::sin(2.0f * std::numbers::pi_v<float> * drop.RockRate * drop.Age + drop.Phase);
        MCParticleCommand command;
        command.X0 = at.X - left * drop.Slant + rock;
        command.Y0 = at.Y - left;
        command.X1 = command.X0;
        command.Y1 = command.Y0;
        command.Style = MCParticleStyle::Hard;
        SetGlint(command, catching, command.X0, at.Y, left, Facet(drop.X, drop.Y));

        switch (drop.Size)
        {
            case FarFlake:
            {
                command.Radius = FlakeRadii[FarFlake];
                SetColor(command, NoPalerThan(FlakeWhite, palest), drop.Alpha);
                Mark(marks, camera, command, drop.Depth);
                break;
            }

            case NearFlake:
            {
                // Its arms, across and up, each a pixel of the mock-up's wide, crossing at its middle.
                command.Radius = FlakeRadii[NearFlake];
                SetColor(command, NoPalerThan(FlakeGrey, palest), drop.Alpha);
                command.X0 -= FlakeArm;
                command.X1 += FlakeArm;
                Mark(marks, camera, command, drop.Depth);
                command.X0 += FlakeArm;
                command.X1 = command.X0;
                command.Y0 -= FlakeArm;
                command.Y1 += FlakeArm;
                Mark(marks, camera, command, drop.Depth);
                break;
            }

            default:
            {
                command.Radius = FlakeRadii[drop.Size];
                SetColor(command, NoPalerThan(FlakeGrey, palest), drop.Alpha);
                Mark(marks, camera, command, drop.Depth);
                break;
            }
        }
    }

    /// <summary>
    /// Adds falling <paramref name="drops"/> to <paramref name="marks"/>: hard-edged streaks, slanted, their tails toward
    /// the wind, catching the light by the lights as <paramref name="catching"/> says; flakes as AddFlake draws them.
    /// </summary>
    void AddDrops(Marks& marks, std::span<const Drop> drops, const MCCamera& camera, const Catching& catching,
                  float palest)
    {
        for (const Drop& drop : drops)
        {
            if (drop.Flake)
            {
                AddFlake(marks, drop, camera, catching, palest);
                continue;
            }

            const DropSize& size = DropSizes[drop.Size];
            const MCVector2D at = Project(drop.X, drop.Y, drop.Z, camera);
            // How far it has still to fall; its head upwind of where it lands by the slant.
            const float left = std::max(drop.Fall - drop.Age * drop.Speed, 0.0f);
            const float length = std::min(drop.Length, drop.Fall - left);
            MCParticleCommand command;
            command.X1 = at.X - left * drop.Slant;
            command.Y1 = at.Y - left;
            command.X0 = command.X1 - length * drop.Slant;
            command.Y0 = command.Y1 - length;
            command.Radius = 0.5f * size.Width;
            command.Style = MCParticleStyle::RainStreak;
            command.Tip = size.Tip;
            SetColor(command, WaterBlue, drop.Alpha);
            // Its streak's middle, above the ground it falls to.
            const float middle = left + 0.5f * length;
            SetGlint(command, catching, at.X - middle * drop.Slant, at.Y, middle, Facet(drop.X, drop.Y));
            Mark(marks, camera, command, drop.Depth);
        }
    }

    /// <summary>
    /// Adds splashes' pieces and drips (<paramref name="splashes"/>) to <paramref name="marks"/>, hard edged, those in the
    /// air catching the light by the lights as <paramref name="catching"/> says; their colours no paler than
    /// <paramref name="palest"/>.
    /// </summary>
    void AddSplashes(Marks& marks, std::span<const Splash> splashes, const MCCamera& camera, const Catching& catching,
                     float palest)
    {
        for (const Splash& splash : splashes)
        {
            if (splash.Age < 0.0f)
            {
                continue;
            }

            const MCVector2D at = Project(splash.X, splash.Y, splash.Z, camera);
            const float age = std::clamp(splash.Age / splash.Life, 0.0f, 1.0f);
            MCParticleCommand command;
            command.X0 = at.X + splash.Across;
            command.Y0 = at.Y - splash.Up;
            command.Radius = splash.Size;
            command.Style = MCParticleStyle::Hard;
            float alpha = splash.Alpha;

            switch (splash.Kind)
            {
                case SplashKind::Droplet:
                {
                    command.X0 += splash.VAcross * splash.Age;
                    command.Y0 -= splash.VUp * splash.Age - 0.5f * Gravity * splash.Age * splash.Age;
                    alpha *= std::lerp(1.0f, splash.FadeTo, age);
                    break;
                }

                case SplashKind::Soak:
                {
                    // Lying on the ground, spreading a little as it soaks in, then drying away.
                    command.Radius = splash.Size * std::lerp(SoakFrom, 1.0f, std::min(age / SoakSpread, 1.0f));
                    command.Aspect = MCSceneLights::GroundAspect();
                    alpha *= 1.0f - age * age;
                    break;
                }

                case SplashKind::Puff:
                {
                    command.Radius = splash.Size * std::lerp(PuffFrom, 1.0f, std::sqrt(age));
                    command.Y0 -= PuffRise * age;
                    command.Style = MCParticleStyle::Soft;
                    command.Softness = PuffSoftness;
                    alpha *= 1.0f - age;
                    break;
                }

                case SplashKind::Glint:
                    break;

                case SplashKind::Dot:
                    break;

                case SplashKind::Flake:
                {
                    alpha *= 1.0f - age;
                    break;
                }

                case SplashKind::Ring:
                {
                    command.Radius = RingFrom + (splash.Size - RingFrom) * std::sqrt(age);
                    command.Ring = std::numeric_limits<float>::min();
                    command.Aspect = MCSceneLights::GroundAspect();
                    alpha *= 1.0f - age;
                    break;
                }

                case SplashKind::Drip:
                {
                    command.Y0 += 0.5f * Gravity * splash.Age * splash.Age;
                    break;
                }
            }

            // A drip a short streak falling, a glint a tick standing up.
            command.X1 = command.X0;
            command.Y1 = splash.Kind == SplashKind::Drip    ? command.Y0 - DripLength
                         : splash.Kind == SplashKind::Glint ? command.Y0 - GlintLength
                                                            : command.Y0;
            SetColor(command, NoPalerThan(splash.Color, palest), alpha);

            // A droplet or a drip in the air (a dot and a ring lie on the ground, which the lights light).
            if (splash.Kind == SplashKind::Droplet || splash.Kind == SplashKind::Drip)
            {
                SetGlint(command, catching, command.X0, at.Y, at.Y - command.Y0,
                         Facet(splash.X + splash.VAcross, splash.Y + splash.VUp));
            }

            Mark(marks, camera, command, splash.Depth);

            // A glint's arm across its tick, through its middle.
            if (splash.Kind == SplashKind::Glint)
            {
                const float arm = 0.5f * GlintLength * GlintArm;
                command.Y0 -= 0.5f * GlintLength;
                command.Y1 = command.Y0;
                command.X0 -= arm;
                command.X1 += arm;
                Mark(marks, camera, command, splash.Depth);
            }
        }
    }

    /// <summary>
    /// Port-only (HD weather's rain, past MCWeather::FullDropLines): the streaks and splashes the composite draws for the
    /// drops left out (Specks' rain): each size's still falling, as many as Thrown times their share still falling, as
    /// streaks as long and wide as theirs on average, falling at their speed along the wind's middle slant, in the
    /// drops' blue; and those that landed in the last SplashSpeckLife seconds as specks of their splashes on the
    /// ground, fading, in their grey no paler than <paramref name="palest"/>; on water, as specks leaving as much light
    /// as their rings (MCSpeckle::OnWater).
    /// </summary>
    MCWeather::MCSpeckle RainSpecks(const State& state, float now, float palest)
    {
        MCWeather::MCSpeckle speckle{.ThickSheets = SheetMost / SheetLeast,
                                     .Catch = MCWeather::CatchGain(),
                                     .Slant = 0.5f * (WindLow + WindHigh)};
        float landed = 0.0f;
        // Each size's rings still spreading on water (where the drops landed on it).
        std::array<float, DropSizes.size()> rings{};

        for (const LeftOutFlakes& left : state.LeftOut)
        {
            if (left.Snows)
            {
                continue;
            }

            const float age = now - left.Time;

            for (size_t size = 0; size < DropSizes.size(); ++size)
            {
                const float falling = StillFalling(DropSizes[size], age);
                speckle.Layers[size].Density += left.Thrown[size] * falling;
                landed += left.Thrown[size] * (StillFalling(DropSizes[size], age - SplashSpeckLife) - falling);
                rings[size] +=
                    left.Thrown[size] * (StillFalling(DropSizes[size], age - DropSizes[size].RingLife) - falling);
            }
        }

        // The drops' blue as AddDrops draws them (at any time of day: only their splashes are kept from being paler).
        const Rgb falls = WaterBlue;

        for (size_t size = 0; size < DropSizes.size(); ++size)
        {
            const DropSize& drop = DropSizes[size];
            MCWeather::MCSpeckLayer& layer = speckle.Layers[size];
            layer.Down = SeenSpeed(drop);
            layer.Across = speckle.Slant * layer.Down;
            layer.Fall = 0.5f * (drop.FallLow + drop.FallHigh);
            layer.Life = layer.Fall / layer.Down;
            layer.Length = 0.5f * (drop.LengthLow + drop.LengthHigh);
            layer.Side = drop.Width;
            layer.Red = falls.Red;
            layer.Green = falls.Green;
            layer.Blue = falls.Blue;
            layer.Weakest = StreakStrength * drop.AlphaLow;
            layer.Strongest = StreakStrength * drop.AlphaHigh;
        }

        // The sizes' streaks share the first layer's lattice (the composite's LayRainStreaks): it falls and lives as
        // their mean, each size weighted by how many of it are falling.
        float falling = 0.0f;
        float down = 0.0f;
        float life = 0.0f;

        for (size_t size = 0; size < DropSizes.size(); ++size)
        {
            const MCWeather::MCSpeckLayer& layer = speckle.Layers[size];
            falling += layer.Density;
            down += layer.Density * layer.Down;
            life += layer.Density * layer.Life;
        }

        if (falling > 0.0f)
        {
            MCWeather::MCSpeckLayer& shared = speckle.Layers[0];
            shared.Down = down / falling;
            shared.Across = speckle.Slant * shared.Down;
            shared.Life = life / falling;
        }

        const Rgb splashes = NoPalerThan(SplashGrey, palest);
        MCWeather::MCSpeckLayer& splash = speckle.Layers[MCWeather::MCSpeckle::Resting];
        splash.Density = landed;
        splash.Life = SplashSpeckLife;
        splash.Side = SplashSpeckSide;
        splash.Red = splashes.Red;
        splash.Green = splashes.Green;
        splash.Blue = splashes.Blue;
        splash.Weakest = SplashSpeckAlpha;
        splash.Strongest = SplashSpeckAlpha;

        // On water: the rings' light, each size's landing on it as fast as its rings showing over their life, each
        // leaving its ring's light and a near drop's second ring's; RingPaleShare of the first rings pale, the second
        // all.
        const float aspect = MCSceneLights::GroundAspect();
        float light = 0.0f;
        float pale = 0.0f;
        float showing = 0.0f;
        float lives = 0.0f;

        for (size_t size = 0; size < DropSizes.size(); ++size)
        {
            const DropSize& drop = DropSizes[size];
            const float first = RingLight(drop.Ring, drop.RingLife, drop.RingAlpha, aspect);
            const float second = size == NearDrop
                                     ? RingLight(drop.Ring * SecondRingShare, drop.RingLife * SecondRingLife,
                                                 drop.RingAlpha * SecondRingAlpha, aspect)
                                     : 0.0f;
            const float landing = rings[size] / drop.RingLife;
            light += landing * (first + second);
            pale += landing * (RingPaleShare * first + second);
            showing += rings[size];
            lives += rings[size] * drop.RingLife;
        }

        if (light > 0.0f && showing > 0.0f)
        {
            const float paleShare = pale / light;
            const Rgb ringColour = NoPalerThan({WaterBlue.Red + (PaleBlue.Red - WaterBlue.Red) * paleShare,
                                                WaterBlue.Green + (PaleBlue.Green - WaterBlue.Green) * paleShare,
                                                WaterBlue.Blue + (PaleBlue.Blue - WaterBlue.Blue) * paleShare},
                                               palest);
            MCWeather::MCSpeckLayer& water = speckle.OnWater;
            water.Density = light / (WaterSpeckAlpha * SpeckFadeMean);
            water.Life = lives / showing;
            water.Side = SplashSpeckSide;
            water.Red = ringColour.Red;
            water.Green = ringColour.Green;
            water.Blue = ringColour.Blue;
            water.Weakest = WaterSpeckAlpha;
            water.Strongest = WaterSpeckAlpha;
        }

        return speckle;
    }

    /// <summary>
    /// Port-only (Redux HD): <paramref name="speckle"/>'s colours through <paramref name="dim"/>, the dimming of the
    /// world they're laid over (MCPaletteNight::NightFit): a speck of colour c laid over the dimmed world shows as a
    /// flake or drop drawn into it before it was dimmed, so long as it shows c * Slope + Offset.
    /// </summary>
    void DimSpecks(MCWeather::MCSpeckle& speckle, const MCTableFit& dim)
    {
        const auto dimmed = [&dim](MCWeather::MCSpeckLayer& layer)
        {
            layer.Red = static_cast<float>(layer.Red * dim.Slope + dim.Offset[0]);
            layer.Green = static_cast<float>(layer.Green * dim.Slope + dim.Offset[1]);
            layer.Blue = static_cast<float>(layer.Blue * dim.Slope + dim.Offset[2]);
        };

        std::ranges::for_each(speckle.Layers, dimmed);
        dimmed(speckle.OnWater);
    }

    /// <summary>
    /// Port-only (HD weather's snow): the specks for the flakes the view left out (State::LeftOut) at the game's time
    /// <paramref name="now"/>, dropping those gone: each size's still falling, drifting downwind by the gusts at the
    /// view's middle, then those landed and resting, as AddFlake and AddSplashes draw them (no paler than
    /// <paramref name="palest"/>).
    /// </summary>
    MCWeather::MCSpeckle Specks(State& state, const MCCamera& camera, const MCWeather::MCWeatherLook& look, float now,
                                float palest)
    {
        const float longest = std::max(LongestFlake(), LongestDrop());

        while (!state.LeftOut.empty() && now - state.LeftOut.front().Time > longest)
        {
            state.LeftOut.pop_front();
        }

        if (state.LeftOut.empty())
        {
            return {};
        }

        if (!look.Snows)
        {
            return RainSpecks(state, now, palest);
        }

        MCWeather::MCSpeckle speckle{.ThickSheets = SheetMost / SheetLeast, .Catch = MCWeather::CatchGain()};
        std::array<float, FlakeSizes.size()> landed{};

        for (const LeftOutFlakes& left : state.LeftOut)
        {
            if (!left.Snows)
            {
                continue;
            }

            const float age = now - left.Time;

            for (size_t size = 0; size < FlakeSizes.size(); ++size)
            {
                const float falling = StillFalling(FlakeSizes[size], age);
                speckle.Layers[size].Density += left.Thrown[size] * falling;
                landed[size] += left.Thrown[size] * (StillFalling(FlakeSizes[size], age - FlakeRest) - falling);
            }
        }

        const MCVector3D middle = GroundPointAt(camera.HalfWidth, camera.HalfHeight, camera);
        const auto [east, north] = FlatGround(middle.X, middle.Y, middle.Z);
        const float slant = look.Drift * (FlakeCalm + FlakeGusts * GustAt(east, north, now));
        MCWeather::MCSpeckLayer& resting = speckle.Layers[MCWeather::MCSpeckle::Resting];
        resting.Life = FlakeRest;
        resting.Side = 2.0f * FlakeDot;

        for (size_t size = 0; size < FlakeSizes.size(); ++size)
        {
            const FlakeSize& flake = FlakeSizes[size];
            MCWeather::MCSpeckLayer& layer = speckle.Layers[size];
            layer.Down = SeenSpeed(flake);
            layer.Across = slant * layer.Down;
            layer.Fall = 0.5f * (flake.FallLow + flake.FallHigh);
            layer.Life = layer.Fall / layer.Down;
            layer.Side = 2.0f * FlakeRadii[size];
            layer.Arm = size == NearFlake ? FlakeArm : 0.0f;
            const Rgb falls = NoPalerThan(size == FarFlake ? FlakeWhite : FlakeGrey, palest);
            layer.Red = falls.Red;
            layer.Green = falls.Green;
            layer.Blue = falls.Blue;
            layer.Weakest = flake.AlphaLow;
            layer.Strongest = flake.AlphaHigh;
            // Each size's landed as many of the resting as there are of them (Throw's colours).
            const Rgb rests = NoPalerThan(size == FarFlake ? FlakeWhite : FlakeBrightest, palest);
            resting.Density += landed[size];
            resting.Red += landed[size] * rests.Red;
            resting.Green += landed[size] * rests.Green;
            resting.Blue += landed[size] * rests.Blue;
            resting.Weakest += landed[size] * flake.AlphaLow;
            resting.Strongest += landed[size] * flake.AlphaHigh;
        }

        if (resting.Density > 0.0f)
        {
            resting.Red /= resting.Density;
            resting.Green /= resting.Density;
            resting.Blue /= resting.Density;
            resting.Weakest /= resting.Density;
            resting.Strongest /= resting.Density;
        }

        return speckle;
    }

    /// <summary>
    /// The threads that help the game's thread make the marks and find what the landed drops land on (AddMarks,
    /// Landings), started with the first that do.
    /// </summary>
    MCWorkers& Helpers()
    {
        static MCWorkers helpers(MCWorkers::HelpersFor(MostHelpers));
        return helpers;
    }

    /// <summary>How many parts of <paramref name="size"/> <paramref name="pieces"/> make (the last the rest).</summary>
    size_t PartsOf(size_t pieces, size_t size)
    {
        return (pieces + size - 1) / size;
    }

    /// <summary>
    /// Part <paramref name="part"/> of <paramref name="pieces"/>, <paramref name="size"/> of them (the last part the
    /// rest).
    /// </summary>
    template <typename T> std::span<T> PartOf(std::span<T> pieces, size_t part, size_t size)
    {
        const size_t first = part * size;
        return pieces.subspan(first, std::min(size, pieces.size() - first));
    }

    /// <summary>
    /// Adds the frame's marks to the draw list: the splashes' (AddSplashes) and then the drops' (AddDrops), made in parts
    /// of PartPieces pieces by the helpers and this thread together (MCWorkers), each part's into its own of
    /// State::MarkParts, and added in their order, so the list is as one thread making them all would make it. Nothing
    /// they read changes while they run (the state, <paramref name="camera"/>, the lights <paramref name="catching"/>
    /// holds, the terrain's projection): this thread only runs parts of its own until all are done.
    /// </summary>
    void AddMarks(State& state, const MCCamera& camera, const Catching& catching, float palest)
    {
        const size_t splashParts = PartsOf(state.Splashes.size(), PartPieces);
        const size_t parts = splashParts + PartsOf(state.Drops.size(), PartPieces);

        if (state.MarkParts.size() < parts)
        {
            state.MarkParts.resize(parts);
        }

        const std::span<Marks> made = std::span(state.MarkParts).first(parts);
        const std::span<const Splash> splashes = state.Splashes;
        const std::span<const Drop> drops = state.Drops;
        Helpers().Run(parts,
                      [&](size_t part)
                      {
                          Marks& marks = made[part];
                          marks.Commands.clear();
                          marks.Depths.clear();

                          if (part < splashParts)
                          {
                              AddSplashes(marks, PartOf(splashes, part, PartPieces), camera, catching, palest);
                          }
                          else
                          {
                              AddDrops(marks, PartOf(drops, part - splashParts, PartPieces), camera, catching, palest);
                          }
                      });
        std::vector<MCParticles::MCMarkPart> marks;
        marks.reserve(parts);

        for (const Marks& part : made)
        {
            marks.push_back({part.Commands, part.Depths});
        }

        MCParticles::AddMarks(marks);
    }

    /// <summary>
    /// What each of the drops <paramref name="landed"/> lands on (LandsOn, as <paramref name="camera"/> shows the ground
    /// under <paramref name="look"/>), in State::Grounds, found in parts of LandingPart drops by the helpers and this
    /// thread together (MCWorkers). First, on this thread, the puddles' levels are measured if not yet (PuddlesOf) and
    /// the view's tiles read in (MCParticles::MCDrawnGround::ReadTiles), so the parts change nothing they share and only
    /// read what stays as it is while they run.
    /// </summary>
    std::span<const MCWeather::MCSplashGround> Landings(State& state, const MCWeather::MCWeatherLook& look,
                                                        std::span<const Drop> landed, const MCCamera& camera)
    {
        state.Grounds.resize(landed.size());

        if (landed.empty())
        {
            return {};
        }

        const Puddles puddles = PuddlesOf(state, look);
        const MCParticles::MCDrawnGround ground(camera);
        const size_t parts = PartsOf(landed.size(), LandingPart);

        // Read in only for the helpers: this thread alone reads them in as it goes.
        if (parts > 1 && Helpers().Helpers() > 0)
        {
            ground.ReadTiles();
        }

        const std::span<MCWeather::MCSplashGround> grounds = state.Grounds;
        Helpers().Run(parts,
                      [&](size_t part)
                      {
                          std::ranges::transform(PartOf(landed, part, LandingPart),
                                                 PartOf(grounds, part, LandingPart).begin(),
                                                 [&](const Drop& drop) { return LandsOn(puddles, ground, drop); });
                      });
        return grounds;
    }

    /// <summary>
    /// Moves the drops and splashes on by <paramref name="step"/> seconds; those landed splash, on what
    /// <paramref name="camera"/> shows drawn under them (under <paramref name="look"/>).
    /// </summary>
    void Move(State& state, const MCWeather::MCWeatherLook& look, float step, const MCCamera& camera)
    {
        for (Splash& splash : state.Splashes)
        {
            splash.Age += step;
        }

        std::erase_if(state.Splashes, [](const Splash& splash) { return splash.Age >= splash.Life; });
        std::vector<Drop> landed;

        for (Drop& drop : state.Drops)
        {
            drop.Age += step;

            if (drop.Age * drop.Speed >= drop.Fall)
            {
                landed.push_back(drop);
            }
        }

        std::erase_if(state.Drops, [](const Drop& drop) { return drop.Age * drop.Speed >= drop.Fall; });
        const std::span<const MCWeather::MCSplashGround> grounds = Landings(state, look, landed, camera);

        for (size_t i = 0; i < landed.size(); ++i)
        {
            Throw(state, landed[i], grounds[i], camera);
        }
    }

    /// <summary>
    /// Drops every drop and splash and the columns measured, and throws the dice from their start again; the units drawn
    /// and the feet come down this frame stay (the game's time went back after they were added).
    /// </summary>
    void ClearState(State& state)
    {
        state.Drops.clear();
        state.Splashes.clear();
        state.PuddleSteps.clear();
        state.Frames.clear();
        state.Prints.clear();
        state.Time.reset();
        state.Dice.Restart();
        state.SplashDice.Restart();
        state.StepDice.Restart();
        state.StrikeDice.Restart();
        state.NextStrike.reset();
        state.Strikes.clear();
        state.Thunders.clear();
        state.LeftOut.clear();
        state.Speckle = {};
    }

    /// <summary>
    /// The lightning: a strike about look.Strikes a minute (times the settings'), waits thrown between StrikeWaitLeast
    /// and StrikeWaitMost of the average, each strike's distance, side, strength and flickers thrown by its own dice
    /// and its thunder due a moment after; flashes gone and thunder long due are dropped.
    /// </summary>
    void Lightning(State& state, const MCWeather::MCWeatherLook& look, float step, float now)
    {
        std::erase_if(state.Strikes, [now](const Strike& strike) { return now - strike.Time > FlashLife; });
        std::erase_if(state.Thunders,
                      [now](const PendingThunder& thunder) { return now - thunder.Due > ThunderStale; });
        const float strikes = look.Strikes * state.Settings.Strikes;

        if (strikes <= 0.0f)
        {
            state.NextStrike.reset();
            return;
        }

        MCDice& dice = state.StrikeDice;
        const float average = SecondsAMinute / strikes;
        const auto wait = [&] { return average * dice.Between(StrikeWaitLeast, StrikeWaitMost); };
        // The first strike waits as the others do.
        float& next = state.NextStrike.has_value() ? *state.NextStrike : state.NextStrike.emplace(wait());
        next -= step;

        if (next > 0.0f)
        {
            return;
        }

        next = wait();
        Strike strike{.Time = now,
                      .Far = dice.Unit(),
                      .Side = dice.Between(-1.0f, 1.0f),
                      .Strength = dice.Between(StrikeLightest, StrikeStrongest)};
        const int32_t flickers = dice.Between(1, MaxFlickers);
        float at = 0.0f;

        for (int32_t i = 0; i < flickers; ++i)
        {
            strike.At[i] = at;
            strike.Bright[i] = i == 0 ? 1.0f : dice.Between(FlickerLeast, FlickerMost);
            at += dice.Between(FlickerGapLeast, FlickerGapMost);
        }

        state.Strikes.push_back(strike);
        state.Thunders.push_back({.Due = now + std::lerp(ThunderSoonest, ThunderLatest, strike.Far),
                                  .Clap = {.Far = strike.Far, .Side = strike.Side}});
    }
}

auto MCWeather::LookOf(MCWeatherKind kind) -> MCWeatherLook
{
    switch (kind)
    {
        case MCWeatherKind::Rain:
            return RainLook;
        case MCWeatherKind::Downpour:
            return DownpourLook;
        case MCWeatherKind::LightSnow:
            return LightSnowLook;
        case MCWeatherKind::Snow:
            return SnowLook;
        case MCWeatherKind::Blizzard:
            return BlizzardLook;
        case MCWeatherKind::Clear:
            break;
    }

    return {};
}

auto MCWeather::ShowersAt(MCWeatherKind kind, float time) -> MCShowers
{
    State& state = TheState();
    const float showers = ShowersOf(state, kind);

    if (showers <= 0.0f)
    {
        return {};
    }

    const float showersTime = time * std::max(state.Settings.ShowerPace, 0.0f);
    return {.Rain = RainAt(showers, showersTime), .Puddles = PuddlesAt(state, showers, showersTime)};
}

auto MCWeather::LookAt(MCWeatherKind kind, float time) -> MCWeatherLook
{
    MCWeatherLook look = LookOf(kind);

    if (ShowersOf(TheState(), kind) <= 0.0f)
    {
        return look;
    }

    const MCShowers showers = ShowersAt(kind, time);
    const float swing = showers.Rain - 1.0f;
    look.Dark *= 1.0f + DarkFollow * swing;
    look.Grey *= 1.0f + DarkFollow * swing;
    look.Veil *= showers.Rain;
    look.Drops *= showers.Rain;
    // The drops near the eye and between follow; those far from it take the rest.
    const float near = look.Sizes[NearDrop];
    const float between = look.Sizes[1];
    look.Sizes[NearDrop] = near * showers.Rain;
    look.Sizes[1] = between * (1.0f + BetweenFollow * swing);
    look.Sizes[0] = std::max(look.Sizes[0] + near + between - look.Sizes[NearDrop] - look.Sizes[1], 0.0f);
    look.Drips *= showers.Rain;
    look.Heard *= std::pow(showers.Rain, HeardFollow);
    look.Puddles *= showers.Puddles;
    look.Sheen *= std::lerp(DampSheen, 1.0f, showers.Puddles);
    return look;
}

auto MCWeather::Settings() -> MCWeatherSettings&
{
    return TheState().Settings;
}

auto MCWeather::CatchGain() -> float
{
    const MCSceneLights::MCLighting lighting = MCSceneLights::ShownLighting();
    const float dark = std::clamp((1.0f - lighting.Ambient) / (1.0f - GlintDusk), 0.0f, 1.0f);
    return TheState().Settings.Light * lighting.Strength * std::lerp(DayGlint, 1.0f, dark);
}

auto MCWeather::Glint(const std::array<float, 3>& caught, float gain, float facet) -> std::array<float, 3>
{
    const float brightest = std::max({caught[0], caught[1], caught[2]});
    const float shown = brightest * gain * facet;
    const int32_t step =
        std::min(static_cast<int32_t>(std::floor(shown / GlintFull * static_cast<float>(GlintSteps))), GlintSteps);

    if (brightest <= 0.0f || step <= 0)
    {
        return {};
    }

    // The lights' hue, further from grey, its brightest component the step's.
    const float grey = (caught[0] + caught[1] + caught[2]) / 3.0f;
    std::array<float, 3> hue{};
    std::ranges::transform(caught, hue.begin(), [&](float component)
                           { return std::max(grey + (component - grey) * GlintSaturation, 0.0f); });
    const float level =
        GlintMost * static_cast<float>(step) / static_cast<float>(GlintSteps) / std::max({hue[0], hue[1], hue[2]});
    return {hue[0] * level, hue[1] * level, hue[2] * level};
}

auto MCWeather::SetMissionWeather(MCWeatherKind weather) -> void
{
    TheState().MissionWeather = weather;
}

auto MCWeather::MissionWeather() -> MCWeatherKind
{
    return TheState().MissionWeather;
}

auto MCWeather::SetMatchWeather(std::optional<MCWeatherKind> weather) -> void
{
    TheState().MatchWeather = weather;
}

auto MCWeather::Shown() -> MCWeatherKind
{
    if (GHdWeather != HdWeatherOff && TheState().MatchWeather.has_value())
    {
        return *TheState().MatchWeather;
    }

    switch (GHdWeather)
    {
        case HdWeatherOff:
            return MCWeatherKind::Clear;
        case HdWeatherRain:
            return MCWeatherKind::Rain;
        case HdWeatherDownpour:
            return MCWeatherKind::Downpour;
        case HdWeatherLightSnow:
            return MCWeatherKind::LightSnow;
        case HdWeatherSnow:
            return MCWeatherKind::Snow;
        case HdWeatherBlizzard:
            return MCWeatherKind::Blizzard;
        default:
            return MissionWeather();
    }
}

auto MCWeather::SettingOf(MCWeatherKind kind) -> int
{
    switch (kind)
    {
        case MCWeatherKind::Rain:
            return HdWeatherRain;
        case MCWeatherKind::Downpour:
            return HdWeatherDownpour;
        case MCWeatherKind::LightSnow:
            return HdWeatherLightSnow;
        case MCWeatherKind::Snow:
            return HdWeatherSnow;
        case MCWeatherKind::Blizzard:
            return HdWeatherBlizzard;
        case MCWeatherKind::Clear:
            break;
    }

    return HdWeatherOff;
}

auto MCWeather::ShownLook() -> MCWeatherLook
{
    return LookAt(Shown(), ScenarioTime);
}

auto MCWeather::Heard() -> float
{
    return HeardLook().Heard;
}

auto MCWeather::HeardLook() -> MCWeatherLook
{
    if (MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        return {};
    }

    MCWeatherLook look = ShownLook();
    look.ThunderHeard *= Settings().Thunder;
    return look;
}

auto MCWeather::ShownFlash() -> MCFlash
{
    const State& state = TheState();

    if (!state.Time.has_value())
    {
        return {};
    }

    MCFlash flash;

    // The brightest strike's light and side.
    for (const Strike& strike : state.Strikes)
    {
        float light = 0.0f;

        for (size_t i = 0; i < strike.At.size(); ++i)
        {
            const float age = *state.Time - strike.Time - strike.At[i];
            light += age >= 0.0f ? strike.Bright[i] * std::exp(-age / FlickerFade) : 0.0f;
        }

        light = std::min(light, 1.0f) * std::lerp(1.0f, FarFlash, strike.Far) * strike.Strength;

        if (light > flash.Light)
        {
            flash = {.Light = light, .Side = strike.Side};
        }
    }

    flash.Light *= ShownLook().Flash * state.Settings.Flash;
    return flash;
}

auto MCWeather::TakeThunder() -> std::optional<MCThunderClap>
{
    State& state = TheState();

    if (!state.Time.has_value())
    {
        return std::nullopt;
    }

    const auto due = std::ranges::find_if(state.Thunders, [&state](const PendingThunder& thunder)
                                          { return thunder.Due <= *state.Time; });

    if (due == state.Thunders.end())
    {
        return std::nullopt;
    }

    const MCThunderClap clap = due->Clap;
    state.Thunders.erase(due);
    return clap;
}

auto MCWeather::Named(std::string_view name) -> std::optional<MCWeatherKind>
{
    const auto found =
        std::ranges::find_if(Weathers, [name](const auto& weather) { return MCIEquals(weather.first, name); });
    return found != Weathers.end() ? std::optional(found->second) : std::nullopt;
}

auto MCWeather::Read(MCFitIniFile& file) -> MCWeatherKind
{
    if (file.SeekBlock("Environment") != 0)
    {
        return MCWeatherKind::Clear;
    }

    const MCFitResult<std::string> name = file.Read<std::string>("Weather");

    if (!name)
    {
        return MCWeatherKind::Clear;
    }

    if (const std::optional<MCWeatherKind> weather = Named(*name); weather.has_value())
    {
        return *weather;
    }

    SDL_Log("MCWeather: the scenario's [Environment] Weather \"%s\" is none of Clear, Rain, Downpour, LightSnow, Snow "
            "and Blizzard; shown clear",
            name->c_str());
    return MCWeatherKind::Clear;
}

auto MCWeather::AddUnit(std::span<const MCSceneLights::MCSunShadowPart> parts, const MCVector3D& position, float feetX,
                        float feetY, int32_t depth) -> void
{
    if (Shown() == MCWeatherKind::Clear)
    {
        return;
    }

    State& state = TheState();
    StandingUnit unit{
        .FeetY = feetY, .Depth = depth, .X = position.X, .Y = position.Y, .Z = position.Z, .FeetX = feetX};
    int32_t left = std::numeric_limits<int32_t>::max();
    int32_t right = std::numeric_limits<int32_t>::min();
    std::vector<std::pair<std::reference_wrapper<const FrameColumns>,
                          std::reference_wrapper<const MCSceneLights::MCSunShadowPart>>>
        shapes;

    for (const MCSceneLights::MCSunShadowPart& part : parts)
    {
        const MCOptionalRef<const FrameColumns> found = ColumnsOf(state, part.Frames, part.Frame, part.Reverse != 0);

        if (!found || found->get().Top.empty())
        {
            continue;
        }

        const FrameColumns& columns = *found;
        const auto x = static_cast<int32_t>(std::floor(part.X)) + columns.Left;
        left = std::min(left, x);
        right = std::max(right, x + static_cast<int32_t>(columns.Top.size()) - 1);
        shapes.emplace_back(columns, part);
    }

    if (shapes.empty())
    {
        return;
    }

    unit.Left = left;
    unit.Top.assign(static_cast<size_t>(right - left + 1), std::numeric_limits<float>::infinity());

    for (const auto& [shape, kept] : shapes)
    {
        const FrameColumns& columns = shape;
        const MCSceneLights::MCSunShadowPart& part = kept;
        const auto x0 = static_cast<int32_t>(std::floor(part.X)) + columns.Left - left;
        const float y = std::floor(part.Y);

        for (size_t i = 0; i < columns.Top.size(); ++i)
        {
            if (columns.Top[i] <= columns.Bottom[i])
            {
                float& top = unit.Top[static_cast<size_t>(x0) + i];
                top = std::min(top, y + static_cast<float>(columns.Top[i]));
            }
        }

        for (const auto& [ex, ey] : columns.LowerEdges)
        {
            const float edgeY = y + static_cast<float>(ey);

            if (edgeY < feetY - static_cast<float>(DripAbove))
            {
                unit.LowerEdges.emplace_back(std::floor(part.X) + static_cast<float>(ex), edgeY);
            }
        }
    }

    unit.Footprint = std::max(FootLeast, FootDepth * static_cast<float>(right - left + 1));
    state.Units.push_back(std::move(unit));
}

auto MCWeather::AddStep(const MCVector3D& foot, std::span<const uint8_t> print, int32_t printFrame) -> void
{
    if (Shown() != MCWeatherKind::Clear)
    {
        TheState().Steps.push_back({foot.X, foot.Y, foot.Z, print, printFrame});
    }
}

auto MCWeather::PuddleAt(const MCVector3D& point, const MCCamera& camera) -> bool
{
    return !MCParticles::BridgedAt(point.X, point.Y) &&
           InPuddle(PuddlesOf(TheState(), ShownLook()), MCParticles::GroundDrawnAt(point.X, point.Y, point.Z, camera),
                    point.X, point.Y, point.Z, StepMargin);
}

auto MCWeather::GroundOf(uint8_t index) -> MCSplashGround
{
    if (MCParticles::IsWaterColor(index))
    {
        return MCSplashGround::Water;
    }

    if (IsGrassIndex(index))
    {
        return MCSplashGround::Grass;
    }

    // HSL's saturation: the colour's spread over its lightness's most (its sum's, or what it lacks of white's).
    const MCVfxRgb color = GamePalette().Colors()[index];
    const int32_t most = std::max({color.R, color.G, color.B});
    const int32_t least = std::min({color.R, color.G, color.B});
    const int32_t span = std::min(most + least, 2 * ComponentMost - most - least);
    const float saturation = span > 0 ? static_cast<float>(most - least) / static_cast<float>(span) : 0.0f;
    return saturation >= SoilSaturation ? MCSplashGround::Soil : MCSplashGround::Hard;
}

auto MCWeather::GroundAt(const MCVector3D& point, const MCCamera& camera) -> MCSplashGround
{
    return GroundUnder(PuddlesOf(TheState(), ShownLook()), MCParticles::MCDrawnGround(camera), point.X, point.Y,
                       point.Z);
}

auto MCWeather::AddDrop(const MCVector3D& point, uint8_t size) -> void
{
    if (Shown() != MCWeatherKind::Clear)
    {
        TheState().Landing.push_back({point, std::min<uint8_t>(size, static_cast<uint8_t>(DropSizes.size() - 1))});
    }
}

auto MCWeather::Pieces() -> MCSplashPieces
{
    MCSplashPieces pieces;

    for (const Splash& splash : TheState().Splashes)
    {
        switch (splash.Kind)
        {
            case SplashKind::Droplet:
                ++pieces.Droplets;
                break;
            case SplashKind::Dot:
                ++pieces.Dots;
                break;
            case SplashKind::Ring:
                ++pieces.Rings;
                break;
            case SplashKind::Drip:
                ++pieces.Drips;
                break;
            case SplashKind::Soak:
                ++pieces.Soaks;
                break;
            case SplashKind::Puff:
                ++pieces.Puffs;
                break;
            case SplashKind::Glint:
                ++pieces.Glints;
                break;
            case SplashKind::Flake:
                ++pieces.Flakes;
                break;
        }
    }

    return pieces;
}

auto MCWeather::ShownPuddleSteps() -> std::span<const MCPuddleStep>
{
    return TheState().PuddleSteps;
}

auto MCWeather::Draw(const MCCamera& camera) -> void
{
    State& state = TheState();
    const float now = ScenarioTime;

    // The game's time went back: another mission, or a game loaded.
    if (state.Time.has_value() && now < *state.Time)
    {
        ClearState(state);
    }

    const float step = state.Time.has_value() ? std::min(now - *state.Time, MaxStep) : 0.0f;
    state.Time = now;
    const MCWeatherLook look = ShownLook();

    if (Shown() == MCWeatherKind::Clear || MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        state.Drops.clear();
        state.Splashes.clear();
        state.Units.clear();
        state.Steps.clear();
        state.Landing.clear();
        state.PuddleSteps.clear();
        state.NextStrike.reset();
        state.Strikes.clear();
        state.Thunders.clear();
        state.LeftOut.clear();
        state.Speckle = {};
        return;
    }

    // The drops fall and splash only as the game's time goes (no dice thrown while it holds).
    if (step > 0.0f)
    {
        Move(state, look, step, camera);

        AgePuddleSteps(state, step);
        Rain(state, camera, look, step, now, ShowersAt(Shown(), now).Rain);
        Drip(state, look, step);
        Lightning(state, look, step, now);

        for (const PendingStep& foot : state.Steps)
        {
            Step(state, foot, camera);
        }

        Land(state, look, camera);
    }

    state.Units.clear();
    state.Steps.clear();
    state.Landing.clear();
    // Port-only (HD lighting): the drops catch the lights they fall past; at dusk and night, the splashes stay dim.
    Catching catching{.Gain = CatchGain()};

    if (catching.Gain > 0.0f)
    {
        catching.Lights = MCSceneLights::MCAirLights(MCSceneLights::Drawing(), camera.HalfWidth * 2.0f,
                                                     camera.HalfHeight * 2.0f, MCSceneLights::GroundAspect());
    }

    const bool night = state.Settings.Light > 0.0f && MCSceneLights::TimeOfDay() != MCTimeOfDay::Day;
    const float palest = night ? MCSceneLights::ShownLighting().SelfLit : 1.0f;
    state.Speckle = Specks(state, camera, look, now, palest);

    // Port-only (Redux HD): where the palette draws the night, it dims the world the flakes and drops are drawn into,
    // so the specks the composite lays over the world after it are dimmed as they would have been.
    if (const std::optional<MCTableFit> dim = MCPaletteNight::NightFit(); dim.has_value())
    {
        DimSpecks(state.Speckle, *dim);
    }

    AddMarks(state, camera, catching, palest);
    state.SnowDrawn = state.SnowDrawn || (look.Snows && !state.Drops.empty());
}

auto MCWeather::TakeSnowDrawn() -> bool
{
    return std::exchange(TheState().SnowDrawn, false);
}

auto MCWeather::Clear() -> void
{
    ClearState(TheState());
}

auto MCWeather::Count() -> size_t
{
    const State& state = TheState();
    return state.Drops.size() + state.Splashes.size();
}

auto MCWeather::PuddleLevel(float share) -> float
{
    return ::PuddleLevel(TheState(), share);
}

auto MCWeather::CoverAt(MCWeatherKind kind, float time) -> float
{
    const MCWeatherLook look = LookOf(kind);
    const MCWeatherSettings& settings = TheState().Settings;

    if (!look.Snows || look.Cover <= 0.0f || look.Settle <= 0.0f)
    {
        return 0.0f;
    }

    const float fallen = (std::isfinite(time) ? std::max(time, 0.0f) : 0.0f) * std::max(settings.SettlePace, 0.0f) +
                         std::max(settings.Settled, 0.0f);
    return look.Cover * (1.0f - std::exp(-fallen / look.Settle));
}

auto MCWeather::ShownCover() -> MCSnowCover
{
    if (MCRenderer::GpuDrawing() != MCGpuDrawing::On || !MCSceneLights::ReliefMapsOn())
    {
        return {};
    }

    const float share = CoverAt(Shown(), ScenarioTime);

    if (share <= 0.0f)
    {
        return {};
    }

    return {.Dusting = SnowLevel(share), .Thick = SnowLevel(share * ThickShare), .Share = share};
}

auto MCWeather::ShownSpeckle() -> MCSpeckle
{
    return Shown() != MCWeatherKind::Clear && MCRenderer::GpuDrawing() == MCGpuDrawing::On ? TheState().Speckle
                                                                                           : MCSpeckle{};
}

auto MCWeather::FilledIn(float age) -> float
{
    const float fill = LookOf(Shown()).Fill;

    if (fill <= 0.0f || ShownCover().Share <= 0.0f)
    {
        return 0.0f;
    }

    const float fallen = (std::isfinite(age) ? std::max(age, 0.0f) : 0.0f) * std::max(Settings().SettlePace, 0.0f);
    return 1.0f - std::exp(-fallen / fill);
}

auto MCWeather::SnowLevel(float share) -> float
{
    return LevelOf(TheState().SnowSamples, SnowField, share);
}
