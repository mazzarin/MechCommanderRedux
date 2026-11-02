#include "stdafx.h"
#include "sprite/MCWeather.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCVector2D.h"
#include "lib/MCVector3D.h"
#include "main/MCMissionGlobals.h"
#include "platform/MCRenderer.h"
#include "sprite/MCParticles.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

#include <map>
#include <numbers>
#include <random>

int GHdWeather = 0;

namespace
{
    /// <summary>The weathers by name (a scenario file's Weather, the tools' --hd-weather).</summary>
    constexpr std::array<std::pair<std::string_view, MCWeatherKind>, 3> Weathers{{
        {"Clear", MCWeatherKind::Clear},
        {"Rain", MCWeatherKind::Rain},
        {"Downpour", MCWeatherKind::Downpour},
    }};

    /// <summary>
    /// Rain and a downpour as the mock-ups made them (tools/upscale/rain-proto, render_rain2.py and 3), their drops 1.2
    /// times as many: drawn in the game, as many covered 0.85 of the mock-up's pixels (mission 20's road). Their rain is
    /// heard faintly, as from far above the battle: a downpour 14 dB under the rain loop as levelled (about 25 dB under
    /// the game's sounds as they play), rain about 6 dB under that. A downpour's thunder rolls in about twice a minute,
    /// the nearest at its loudest about 12 dB under the thunder as levelled (its loudest quarter second -12 dBFS).
    /// Both come and go in showers, a downpour less far (it never stops), rain a little further.
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
                                                .Showers = 0.45f};
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
                                                    .Thunder = 2.0f,
                                                    .ThunderHeard = 0.5f,
                                                    .Showers = 0.35f};

    /// <summary>The longest step the drops move in one frame, in seconds of the game's time.</summary>
    constexpr float MaxStep = 0.1f;
    /// <summary>The drops' dice's first seed (their own: never the game's, nor the particles').</summary>
    constexpr uint32_t DiceSeed = 0x7a1d5eedu;
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
    /// A drop's size, as near the eye it falls (a top-down view: the nearer, the higher): how far it falls on screen and
    /// how fast (view pixels), its streak's length, its strength, its width (the draw rounds it to whole pixels of the
    /// output: 1, 2 or 3 at the closest zoom) and its brightest tip; its splash on land (droplets thrown up, as far as
    /// Throw, and a dot) and in water (a ring that wide, lasting that long, that strong).
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
         .RingAlpha = 0.35f},
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
         .RingAlpha = 0.5f},
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
         .RingAlpha = 0.6f},
    }};
    constexpr size_t NearDrop = 2;

    /// <summary>The palette's colours the rain is drawn in (its water ramp, 0x5c to 0x5f, and pale greys, 0x1c and 0x1d).</summary>
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
    // rain's strength to the power HeardFollow (a channel volume: a lull of 0.65 about 5.6 dB quieter).
    constexpr float DarkFollow = 0.75f;
    constexpr float BetweenFollow = 0.5f;
    constexpr float HeardFollow = 0.75f;
    // The puddles fill toward how hard the rain falls (1 as made) over PuddleFill seconds of the showers' time, and dry
    // toward it more slowly, over PuddleDry; taken each PuddleStep seconds. Damp ground, without puddles yet, mirrors
    // DampSheen of the sky wet ground does.
    constexpr float PuddleFill = 240.0f;
    constexpr float PuddleDry = 720.0f;
    constexpr float PuddleStep = 1.0f;
    constexpr float DampSheen = 0.4f;

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

    /// <summary>The puddles' field at a ground point (meters east and north of the map's middle; PuddleField's).</summary>
    float PuddleField(float east, float north)
    {
        return Layered(east / PuddleSize, north / PuddleSize, PuddleApartX, PuddleApartY);
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

    /// <summary>The drops' own dice: the same throws in the same order every run.</summary>
    class MCDice
    {
    public:
        /// <summary>Throws from the start again.</summary>
        void Restart() { _Engine.seed(DiceSeed); }

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
        std::mt19937 _Engine{DiceSeed};
    };

    /// <summary>A falling drop: where it lands (the world, its height there), its size and look, how far it has fallen.</summary>
    struct Drop
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        uint8_t Size = 0;
        bool OnUnit = false;
        float Fall = 0.0f;
        float Speed = 0.0f;
        float Length = 0.0f;
        float Alpha = 0.0f;
        float Slant = 0.0f;
        float Age = 0.0f;
        int32_t Depth = 0;
    };

    /// <summary>What a splash leaves for a while: a droplet thrown up, a dot, a ring, a drip falling.</summary>
    enum class SplashKind : uint8_t
    {
        Droplet,
        Dot,
        Ring,
        Drip,
    };

    /// <summary>
    /// A splash's piece: placed from a world point (X, Y, Z) by view pixels (Across, Up), moving at (VAcross, VUp) and
    /// falling (Gravity) for Life seconds; a ring growing to Size; drawn in its colour at its strength at Depth.
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
        int32_t Depth = 0;
    };

    /// <summary>
    /// Droplets thrown up from a point: how many, how fast out and up (times a splash's usual speeds), from within how many
    /// view pixels to its sides, within how many seconds of each other, how big and how strong.
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
    using FrameKey = std::tuple<const uint8_t*, size_t, int32_t, bool>;

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
    /// The weather's state: its settings and the mission's weather; the drops falling and the splashes showing, and the
    /// most there may be (for this frame's view); the units drawn and the feet come down this frame (Draw empties them
    /// each frame); the feet's puddles still reacting (the newest MCWeather::MaxPuddleSteps); the shapes' frames'
    /// columns and footprints' shapes measured so far (kept for the mission); the dice, the rain's and the feet's (so the
    /// rain falls the same whoever steps in it); the game's time the last frame was drawn at.
    /// </summary>
    struct State
    {
        MCWeather::MCWeatherSettings Settings;
        MCWeatherKind MissionWeather = MCWeatherKind::Clear;
        std::vector<Drop> Drops;
        std::vector<Splash> Splashes;
        size_t Most = 0;
        std::vector<StandingUnit> Units;
        std::vector<PendingStep> Steps;
        std::vector<MCWeather::MCPuddleStep> PuddleSteps;
        std::map<FrameKey, FrameColumns> Frames;
        std::map<FrameKey, std::optional<PrintShape>> Prints;
        MCDice Dice;
        MCDice StepDice;
        std::optional<float> Time;
        /// <summary>The puddles' levels for their shares of the ground, measured once (PuddleLevel).</summary>
        std::vector<float> PuddleSamples;
        /// <summary>
        /// How far the puddles have spread each PuddleStep seconds of the showers' time from a mission's start (Filled),
        /// for showers as far as FilledFor; taken as far as asked for.
        /// </summary>
        std::vector<float> Filled;
        float FilledFor = 0.0f;
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

    /// <summary>The puddles' level that leaves <paramref name="share"/> of the ground under them (composite.pshader's).</summary>
    float PuddleLevel(State& state, float share)
    {
        if (share <= 0.0f)
        {
            return 0.0f;
        }

        if (state.PuddleSamples.empty())
        {
            state.PuddleSamples.reserve(static_cast<size_t>(LevelSamples) * LevelSamples);

            for (int32_t row = 0; row < LevelSamples; ++row)
            {
                for (int32_t col = 0; col < LevelSamples; ++col)
                {
                    state.PuddleSamples.push_back(
                        PuddleField(static_cast<float>(col) * LevelStep, static_cast<float>(row) * LevelStep));
                }
            }

            std::ranges::sort(state.PuddleSamples);
        }

        const auto at =
            static_cast<size_t>(std::clamp(share, 0.0f, 1.0f) * static_cast<float>(state.PuddleSamples.size() - 1));
        return state.PuddleSamples[at];
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
    const FrameColumns* ColumnsOf(State& state, std::span<const uint8_t> table, int32_t frame, bool reverse)
    {
        if (!VfxIsShapeTable(table) || frame < 0 || frame >= VfxShapeCount(table))
        {
            return nullptr;
        }

        const auto [found, isNew] = state.Frames.try_emplace(FrameKey{table.data(), table.size(), frame, reverse});
        FrameColumns& columns = found->second;

        if (!isNew)
        {
            return &columns;
        }

        std::vector<std::pair<int16_t, int16_t>> pixels;
        VfxShapeVisit(table, frame, [&](int32_t x, int32_t y, uint8_t)
                      { pixels.emplace_back(static_cast<int16_t>(reverse ? -x : x), static_cast<int16_t>(y)); });

        if (pixels.empty())
        {
            return &columns;
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

        return &columns;
    }

    /// <summary>
    /// A footprint's shape (shape table <paramref name="table"/>'s frame <paramref name="frame"/>) as MCWeather::
    /// PrintCircles circles round its pixels, measured once: its pixels grouped round as many seeds (each the pixel
    /// farthest from those before; the first, from their middle), each seed moved to its group's middle PrintRounds times;
    /// each circle reaching its group's farthest pixel. None for a frame without pixels.
    /// </summary>
    const PrintShape* PrintShapeOf(State& state, std::span<const uint8_t> table, int32_t frame)
    {
        if (!VfxIsShapeTable(table) || frame < 0 || frame >= VfxShapeCount(table))
        {
            return nullptr;
        }

        const auto [found, isNew] = state.Prints.try_emplace(FrameKey{table.data(), table.size(), frame, false});
        std::optional<PrintShape>& shape = found->second;

        if (!isNew)
        {
            return shape.has_value() ? &*shape : nullptr;
        }

        // The pixels' middles.
        std::vector<std::pair<float, float>> pixels;
        VfxShapeVisit(table, frame, [&](int32_t x, int32_t y, uint8_t)
                      { pixels.emplace_back(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f); });

        if (pixels.empty())
        {
            return nullptr;
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

        return &circles;
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
    /// Whether ground drawn in palette entry <paramref name="drawn"/> at world point (x, y, z) lies in a puddle,
    /// <paramref name="margin"/> inside its level: where the composite lays them, with the wet look (the relief maps),
    /// not on grass.
    /// </summary>
    bool InPuddle(State& state, const MCWeather::MCWeatherLook& look, std::optional<uint8_t> drawn, float x, float y,
                  float z, float margin)
    {
        if (!drawn.has_value() || IsGrassIndex(*drawn) || look.Puddles <= 0.0f || !MCSceneLights::ReliefMapsOn())
        {
            return false;
        }

        const auto [east, north] = FlatGround(x, y, z);
        return PuddleField(east, north) < PuddleLevel(state, look.Puddles) - margin;
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
                                      .Depth = depth});
            Splash& droplet = state.Splashes.back();
            droplet.Life = 2.0f * droplet.VUp / Gravity;
        }
    }

    /// <summary>
    /// <paramref name="drop"/> landing: a dot where it lands; in water or a puddle, a ring (a near drop's two); on land
    /// or a unit, droplets thrown up.
    /// </summary>
    void Throw(State& state, const Drop& drop, const MCCamera& camera)
    {
        const DropSize& size = DropSizes[drop.Size];
        MCDice& dice = state.Dice;
        const MCWeather::MCWeatherLook look = MCWeather::ShownLook();
        const std::optional<uint8_t> drawn =
            drop.OnUnit ? std::nullopt : MCParticles::GroundDrawnAt(drop.X, drop.Y, drop.Z, camera);
        // A bridge's road is drawn over the water (the ground drawn under it).
        const bool wet = !MCParticles::BridgedAt(drop.X, drop.Y) &&
                         (InPuddle(state, look, drawn, drop.X, drop.Y, drop.Z, PuddleMargin) ||
                          (drawn.has_value() && MCParticles::IsWaterColor(*drawn)));
        Splash dot{.Kind = SplashKind::Dot,
                   .X = drop.X,
                   .Y = drop.Y,
                   .Z = drop.Z,
                   .Size = size.Dot * 0.5f,
                   .Life = DotLife,
                   .Color = wet ? PalestBlue : SplashGrey,
                   .Alpha = DotAlpha,
                   .Depth = drop.Depth};
        state.Splashes.push_back(dot);

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
        const PrintShape* print = PrintShapeOf(state, step.Print, step.PrintFrame);
        const float cosine = std::max(MCTerrain::ProjectionCos, MinProjection);
        const float sine = std::max(MCTerrain::ProjectionSin, MinProjection);

        for (size_t k = 0; k < puddle.Print.size(); ++k)
        {
            if (print == nullptr)
            {
                puddle.Print[k] = {east, north, PrintFallback};
                continue;
            }

            // The flat projection undone (GroundPointAt's): view pixels across and down to meters east and north; a
            // radius as wide as the view's.
            const ViewCircle& circle = (*print)[k];
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
        const float area = (width + LandLeft + LandRight) * (height + LandBelow);
        const float rate = look.Drops * state.Settings.Amount * area / (DropsArea * DropsArea) * SheetMost;
        state.Most = static_cast<size_t>(MaxDrops * std::max(burst, 1.0f) * area / (DropsArea * DropsArea));
        const int32_t count = dice.Count(rate * step);

        for (int32_t i = 0; i < count && state.Drops.size() + state.Splashes.size() < state.Most; ++i)
        {
            const float viewX = dice.Between(-LandLeft, width + LandRight);
            const float viewY = dice.Between(0.0f, height + LandBelow);
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
            const MCVector2D at = Project(ground.X, ground.Y, ground.Z, camera);
            drop.Depth = -static_cast<int32_t>(std::floor(at.Y));

            for (const StandingUnit& unit : state.Units)
            {
                if (const std::optional<float> top = HitOn(unit, at.X, at.Y); top.has_value())
                {
                    drop.OnUnit = true;
                    drop.Z = ground.Z + (at.Y - *top);
                    drop.Depth = unit.Depth - 1;
                    break;
                }
            }

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

    /// <summary>Sets <paramref name="command"/>'s colour, premultiplied: <paramref name="color"/> at <paramref name="alpha"/>.</summary>
    void SetColor(MCParticleCommand& command, const Rgb& color, float alpha)
    {
        const auto byte = [](float value)
        { return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
        command.Red = byte(color.Red * alpha);
        command.Green = byte(color.Green * alpha);
        command.Blue = byte(color.Blue * alpha);
        command.Alpha = byte(alpha);
    }

    /// <summary>Whether a mark round (x0, y0)-(x1, y1), <paramref name="reach"/> about it, shows in the view.</summary>
    bool InView(const MCCamera& camera, float x0, float y0, float x1, float y1, float reach)
    {
        const float right = camera.HalfWidth * 2.0f + ViewMargin;
        const float bottom = camera.HalfHeight * 2.0f + ViewMargin;
        return std::max(x0, x1) + reach >= -ViewMargin && std::max(y0, y1) + reach >= -ViewMargin &&
               std::min(x0, x1) - reach <= right && std::min(y0, y1) - reach <= bottom;
    }

    /// <summary>Adds <paramref name="command"/> to the draw list at <paramref name="depth"/> if it shows.</summary>
    void Mark(const MCCamera& camera, const MCParticleCommand& command, int32_t depth)
    {
        if (InView(camera, command.X0, command.Y0, command.X1, command.Y1,
                   command.Radius + std::max(command.Ring, 0.0f)))
        {
            MCParticles::AddMark(command, depth);
        }
    }

    /// <summary>Adds the falling drops to the draw list: hard-edged streaks, slanted, their tails toward the wind.</summary>
    void AddDrops(const State& state, const MCCamera& camera)
    {
        for (const Drop& drop : state.Drops)
        {
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
            Mark(camera, command, drop.Depth);
        }
    }

    /// <summary>Adds the splashes' pieces and the drips to the draw list, hard edged.</summary>
    void AddSplashes(const State& state, const MCCamera& camera)
    {
        for (const Splash& splash : state.Splashes)
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
                    alpha *= std::lerp(1.0f, DropletFadeTo, age);
                    break;
                }

                case SplashKind::Dot:
                    break;

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

            command.X1 = command.X0;
            command.Y1 = splash.Kind == SplashKind::Drip ? command.Y0 - DripLength : command.Y0;
            SetColor(command, splash.Color, alpha);
            Mark(camera, command, splash.Depth);
        }
    }

    /// <summary>
    /// Moves the drops and splashes on by <paramref name="step"/> seconds; those landed splash, on what
    /// <paramref name="camera"/> shows drawn under them.
    /// </summary>
    void Move(State& state, float step, const MCCamera& camera)
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

        for (const Drop& drop : landed)
        {
            Throw(state, drop, camera);
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
        state.StepDice.Restart();
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

auto MCWeather::SetMissionWeather(MCWeatherKind weather) -> void
{
    TheState().MissionWeather = weather;
}

auto MCWeather::MissionWeather() -> MCWeatherKind
{
    return TheState().MissionWeather;
}

auto MCWeather::Shown() -> MCWeatherKind
{
    switch (GHdWeather)
    {
        case HdWeatherOff:
            return MCWeatherKind::Clear;
        case HdWeatherRain:
            return MCWeatherKind::Rain;
        case HdWeatherDownpour:
            return MCWeatherKind::Downpour;
        default:
            return MissionWeather();
    }
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

    SDL_Log("MCWeather: the scenario's [Environment] Weather \"%s\" is none of Clear, Rain and Downpour; shown clear",
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
    std::vector<std::pair<const FrameColumns*, const MCSceneLights::MCSunShadowPart*>> shapes;

    for (const MCSceneLights::MCSunShadowPart& part : parts)
    {
        const FrameColumns* columns = ColumnsOf(state, part.Frames, part.Frame, part.Reverse != 0);

        if (columns == nullptr || columns->Top.empty())
        {
            continue;
        }

        const auto x = static_cast<int32_t>(std::floor(part.X)) + columns->Left;
        left = std::min(left, x);
        right = std::max(right, x + static_cast<int32_t>(columns->Top.size()) - 1);
        shapes.emplace_back(columns, &part);
    }

    if (shapes.empty())
    {
        return;
    }

    unit.Left = left;
    unit.Top.assign(static_cast<size_t>(right - left + 1), std::numeric_limits<float>::infinity());

    for (const auto& [columns, part] : shapes)
    {
        const auto x0 = static_cast<int32_t>(std::floor(part->X)) + columns->Left - left;
        const float y = std::floor(part->Y);

        for (size_t i = 0; i < columns->Top.size(); ++i)
        {
            if (columns->Top[i] <= columns->Bottom[i])
            {
                float& top = unit.Top[static_cast<size_t>(x0) + i];
                top = std::min(top, y + static_cast<float>(columns->Top[i]));
            }
        }

        for (const auto& [ex, ey] : columns->LowerEdges)
        {
            const float edgeY = y + static_cast<float>(ey);

            if (edgeY < feetY - static_cast<float>(DripAbove))
            {
                unit.LowerEdges.emplace_back(std::floor(part->X) + static_cast<float>(ex), edgeY);
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
           InPuddle(TheState(), ShownLook(), MCParticles::GroundDrawnAt(point.X, point.Y, point.Z, camera), point.X,
                    point.Y, point.Z, StepMargin);
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
        state.PuddleSteps.clear();
        return;
    }

    // The drops fall and splash only as the game's time goes (no dice thrown while it holds).
    if (step > 0.0f)
    {
        Move(state, step, camera);
        AgePuddleSteps(state, step);
        Rain(state, camera, look, step, now, ShowersAt(Shown(), now).Rain);
        Drip(state, look, step);

        for (const PendingStep& foot : state.Steps)
        {
            Step(state, foot, camera);
        }
    }

    state.Units.clear();
    state.Steps.clear();
    AddSplashes(state, camera);
    AddDrops(state, camera);
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
