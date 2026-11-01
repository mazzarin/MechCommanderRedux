#include "stdafx.h"
#include "sprite/MCParticles.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "engine/MCElement.h"
#include "engine/MCElementBuffer.h"
#include "lib/MCVector3D.h"
#include "main/MCMissionGlobals.h"
#include "platform/MCRenderer.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCVfxAppearance.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

#include <map>
#include <numbers>
#include <random>
#include <unordered_map>

int GHdParticles = 0;
int GHdShake = 0;

namespace
{
    // Distances are in the world window's pixels (a height is one up the screen; a ground distance, across it), times in
    // seconds of the game's time, colours 0 to 1.

    /// <summary>The most time one frame moves the particles on (a long frame's, a load's).</summary>
    constexpr float MaxStep = 0.1f;
    /// <summary>Where the particles' dice start (each mission).</summary>
    constexpr uint32_t DiceSeed = 0x5eed2b0du;
    /// <summary>The pull down on what falls (sparks, debris, droplets).</summary>
    constexpr float Gravity = 520.0f;
    /// <summary>A burst this close above the ground hit the ground (or the water), not a unit.</summary>
    constexpr float GroundHitHeight = 6.0f;
    /// <summary>
    /// A fire this big throws a few chunks and more sparks (a shell's); one this big or more is a unit's or a building's
    /// end, which throws debris, smoke and embers and shakes the picture, the most at the last size. (The battles'
    /// shots' fires measure 3 to 28; their units' ends, 77.)
    /// </summary>
    constexpr float MediumFire = 24.0f;
    constexpr float BigFire = 40.0f;
    constexpr float HugeFire = 90.0f;
    /// <summary>A burst's smoke and embers start this much of its extent from its middle, at most.</summary>
    constexpr float BurstSpread = 0.15f;
    /// <summary>
    /// What flies sorts as an explosion's frames do (MCExplosion's Render, this far below its place on the screen), so
    /// it shows over the unit it flies off.
    /// </summary>
    constexpr int32_t AirDepthFixup = -150;
    /// <summary>What lies on the ground (debris at rest, ripples) sorts this far above its place, under units standing there.</summary>
    constexpr int32_t GroundDepthFixup = 30;
    /// <summary>Particles whose drawn mark lies wholly this far outside the view are moved on but not drawn.</summary>
    constexpr float ViewMargin = 4.0f;
    /// <summary>The share each way a particle's size, strength and shade may differ from its kind's.</summary>
    constexpr float Vary = 0.2f;
    /// <summary>How soft a mark's edge is (the share of its radius it fades over) where nothing else says.</summary>
    constexpr float SoftEdge = 0.5f;

    /// <summary>How fast the wind carries smoke and embers.</summary>
    constexpr float SmokeDrift = 12.0f;
    constexpr float EmberDrift = 16.0f;

    // Sparks: thin bright streaks that fly off a hit and fall, bouncing once, white hot cooling to orange and red.
    constexpr float SparkMinUp = -0.3f;
    constexpr float SparkSpeedLow = 70.0f;
    constexpr float SparkSpeedHigh = 210.0f;
    constexpr float SparkLift = 40.0f;
    constexpr float SparkLifeLow = 0.25f;
    constexpr float SparkLifeHigh = 0.6f;
    constexpr float SparkRadiusLow = 0.35f;
    constexpr float SparkRadiusHigh = 0.6f;
    constexpr float SparkDrag = 1.5f;
    /// <summary>A spark's streak: where it was this long ago.</summary>
    constexpr float SparkStreak = 0.03f;
    constexpr float SparkBounceSpeed = 30.0f;
    constexpr float SparkBounce = 0.35f;
    constexpr float SparkLight = 1.0f;
    /// <summary>How fast its light fades with its heat (a power of what's left of its life).</summary>
    constexpr float SparkFade = 0.6f;
    /// <summary>Its green and blue as it cools: the coolest's, and what the heat adds (blue by its square).</summary>
    constexpr float SparkGreenCool = 0.3f;
    constexpr float SparkGreenHot = 0.65f;
    constexpr float SparkBlueCool = 0.08f;
    constexpr float SparkBlueHot = 0.6f;

    // Embers: glowing motes that rise and drift with the wind, swaying and flickering.
    constexpr float EmberRiseLow = 18.0f;
    constexpr float EmberRiseHigh = 40.0f;
    constexpr float EmberLifeLow = 1.2f;
    constexpr float EmberLifeHigh = 2.8f;
    constexpr float EmberRadiusLow = 0.45f;
    constexpr float EmberRadiusHigh = 0.75f;
    constexpr float EmberDrag = 1.5f;
    constexpr float EmberSway = 12.0f;
    constexpr float EmberSwayRate = 1.7f;
    constexpr float EmberFlickerLow = 9.0f;
    constexpr float EmberFlickerHigh = 16.0f;
    /// <summary>Its light: the steady part and the flicker's.</summary>
    constexpr float EmberSteady = 0.65f;
    constexpr float EmberFlicker = 0.35f;
    /// <summary>Its green, the coolest's and what the flicker adds, and its blue.</summary>
    constexpr float EmberGreen = 0.42f;
    constexpr float EmberGreenFlicker = 0.2f;
    constexpr float EmberBlue = 0.08f;
    constexpr float EmberStreak = 0.04f;
    constexpr float EmberSoftness = 0.6f;
    constexpr float EmberLight = 0.9f;
    /// <summary>An ember fades in over this long, and out over this share of its life.</summary>
    constexpr float EmberFadeIn = 0.2f;
    constexpr float EmberFadeOut = 0.4f;

    // Smoke: soft puffs that rise, grow, drift with the wind and fade.
    constexpr float SmokeDrag = 0.8f;
    constexpr float SmokeFadeIn = 0.3f;
    constexpr float SmokeFadeOut = 1.3f;
    /// <summary>A puff grows slower as it ages: by its life's end, this share slower.</summary>
    constexpr float SmokeGrowSlows = 0.5f;

    // Debris: chunks that tumble through an arc, bounce, come to rest and fade; some glow hot and trail smoke.
    constexpr float DebrisMinUp = 0.25f;
    constexpr float DebrisSpin = 14.0f;
    constexpr float DebrisDrag = 0.2f;
    constexpr float DebrisBounceSpeed = 45.0f;
    constexpr float DebrisBounce = 0.3f;
    constexpr float DebrisSkid = 0.5f;
    constexpr uint8_t DebrisBounces = 2;
    constexpr float DebrisCools = 1.0f;
    constexpr float DebrisFades = 0.8f;
    constexpr float DebrisGlow = 0.9f;
    /// <summary>A hot chunk's glow's green and blue, for its red.</summary>
    constexpr float DebrisGlowGreen = 0.45f;
    constexpr float DebrisGlowBlue = 0.12f;
    constexpr float DebrisTrailEvery = 0.06f;
    constexpr float DebrisTrailLasts = 1.2f;
    /// <summary>A chunk is this many of its sizes long to each side, and this many thick (at least MinRadius).</summary>
    constexpr float DebrisLength = 0.9f;
    constexpr float DebrisThickness = 0.5f;
    constexpr float DebrisSoftness = 0.35f;
    /// <summary>The thinnest mark drawn.</summary>
    constexpr float MinRadius = 0.5f;

    // Droplets: water thrown up by a shot or a wading unit, falling back.
    constexpr float DropletUpLow = 60.0f;
    constexpr float DropletUpHigh = 170.0f;
    constexpr float DropletOutLow = 10.0f;
    constexpr float DropletOutHigh = 45.0f;
    constexpr float DropletLife = 1.5f;
    constexpr float DropletRadiusLow = 0.45f;
    constexpr float DropletRadiusHigh = 0.8f;
    constexpr float DropletAlpha = 0.8f;
    constexpr float DropletStreak = 0.02f;
    constexpr float DropletFades = 0.2f;
    /// <summary>The share of droplets falling back into water that ring it, this many of their sizes wide, this long.</summary>
    constexpr float DropletRippleShare = 0.3f;
    constexpr float DropletRippleGrows = 5.0f;
    constexpr float DropletRippleLife = 0.5f;
    constexpr float DropletRippleAlpha = 0.35f;

    // Ripples: rings spreading on the water and fading.
    constexpr float RippleWidth = 0.7f;
    constexpr float RippleFadeIn = 0.1f;
    constexpr float RippleFadeOut = 1.5f;
    /// <summary>Of a ripple's light, the share of what lies under it that it hides (foam is pale, not solid).</summary>
    constexpr float RippleCover = 0.6f;
    /// <summary>A chunk falling into water rings it this many of its sizes wide, this long.</summary>
    constexpr float SinkRippleGrows = 4.0f;
    constexpr float SinkRippleLife = 0.7f;
    constexpr float SinkRippleAlpha = 0.4f;

    // What a burst throws: so many, and so many more for each pixel of its fire's or its extent's side.
    constexpr float HitSparks = 4.0f;
    constexpr float HitSparksPerFire = 0.6f;
    constexpr float DirtClods = 3.0f;
    constexpr float DirtClodsPerExtent = 0.15f;
    constexpr float DustPuffs = 1.0f;
    constexpr float DustPuffsPerExtent = 0.03f;
    constexpr float GroundSparksPerFire = 0.3f;
    constexpr float MediumDebrisPerFire = 0.1f;
    constexpr float MediumSparksPerFire = 0.3f;
    constexpr float MediumSmokePerFire = 0.05f;
    constexpr float BigDebrisPerFire = 0.35f;
    constexpr float BigSparksPerFire = 0.5f;
    constexpr float BigSmokePerFire = 0.1f;
    constexpr float BigEmbersPerFire = 0.25f;
    constexpr float SplashDroplets = 8.0f;
    constexpr float SplashDropletsPerExtent = 0.35f;
    constexpr float SplashMist = 1.0f;
    constexpr float SplashMistPerExtent = 0.04f;
    constexpr int32_t SplashRings = 2;
    constexpr int32_t BigSplashRings = 3;
    /// <summary>A splash's rings: from this much of its extent to this much more each, apart by this long.</summary>
    constexpr float SplashRingFrom = 0.2f;
    constexpr float SplashRingTo = 0.8f;
    constexpr float SplashRingStep = 0.3f;
    constexpr float SplashRingDelay = 0.15f;
    constexpr float SplashRingLifeLow = 1.0f;
    constexpr float SplashRingLifeHigh = 1.5f;
    constexpr float SplashRingAlpha = 0.55f;
    /// <summary>A splash from a big fire throws its water up to this much more out (and half of that more up).</summary>
    constexpr float BigSplashOut = 1.0f;

    // Wakes: what a unit wading through water leaves.
    constexpr float WadeHeight = 4.0f;
    constexpr float WadeSpeed = 4.0f;
    /// <summary>A unit that moved this far in a frame was put there, not walking.</summary>
    constexpr float TeleportDistance = 40.0f;
    constexpr float WakeEvery = 0.2f;
    constexpr float WakeSpeed = 25.0f;
    constexpr float WakeEveryLeast = 0.5f;
    constexpr float WakeEveryMost = 2.0f;
    constexpr float StillRippleEvery = 1.6f;
    /// <summary>A wake's ring: from this much of the unit's half width to this much, moving or standing, this long.</summary>
    constexpr float WakeRingFrom = 0.35f;
    constexpr float WakeRingMoving = 1.5f;
    constexpr float WakeRingStill = 1.1f;
    constexpr float WakeRingLifeLow = 1.1f;
    constexpr float WakeRingLifeHigh = 1.5f;
    constexpr float WakeRingAlpha = 0.5f;
    constexpr float StillRingAlpha = 0.3f;
    /// <summary>The foam thrown up as it goes: so many a ripple, this much of a splash's speed out and up.</summary>
    constexpr float WakeFoam = 2.5f;
    constexpr float WakeFoamOut = 0.6f;
    constexpr float WakeFoamUp = 0.35f;

    // Fires: embers and smoke a second for each pixel of the side of a square of a flame's bright pixels.
    constexpr float FireEmbers = 0.25f;
    constexpr float FireSmoke = 0.12f;
    /// <summary>Embers start this much of that side from the flames' middle; smoke, this much above it and around it.</summary>
    constexpr float FireEmberSpread = 0.35f;
    constexpr float FireSmokeAbove = 0.4f;
    constexpr float FireSmokeSpread = 0.25f;

    // The shake: a fast wobble, fading, as strong as the burst is big and as near the view's middle it is.
    constexpr float ShakeLeast = 1.0f;
    constexpr float ShakeMost = 4.0f;
    /// <summary>The most the shakes together move the picture (times MCParticleLook::Shake).</summary>
    constexpr float ShakeCap = 5.0f;
    constexpr float ShakeFade = 0.25f;
    constexpr float ShakeLasts = 1.2f;
    constexpr float ShakeRateX = 11.0f;
    constexpr float ShakeRateY = 14.0f;
    /// <summary>The down wobble's phase, for the across one's: apart, so the picture moves round, not along a line.</summary>
    constexpr float ShakePhaseApart = 1.7f;
    /// <summary>A burst's wobble starts at a phase taken from where it is (the dice are the particles').</summary>
    constexpr float ShakePhaseX = 0.37f;
    constexpr float ShakePhaseY = 0.61f;
    /// <summary>A burst this many of the view's half widths from its middle shakes it half as much.</summary>
    constexpr float ShakeReach = 1.2f;

    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

    /// <summary>What a particle is: how it moves and how it's drawn.</summary>
    enum class MCParticleKind : uint8_t
    {
        Spark,
        Ember,
        Smoke,
        Debris,
        Droplet,
        Ripple,
    };

    /// <summary>One particle: what it is, where it is and how it moves, and how it looks.</summary>
    struct Particle
    {
        MCParticleKind Kind = MCParticleKind::Spark;
        /// <summary>Where it is in the world (Z: its height, as a position's).</summary>
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        /// <summary>How fast it goes: world units a second along the ground, pixels a second up.</summary>
        float VX = 0.0f;
        float VY = 0.0f;
        float VZ = 0.0f;
        /// <summary>The ground's height under it, as it last moved.</summary>
        float Ground = 0.0f;
        /// <summary>How long it has been (below 0: it starts in that long) and how long it lasts.</summary>
        float Age = 0.0f;
        float Life = 1.0f;
        /// <summary>Its radius (a chunk's size), and what it grows to (a ripple's) or by a second (smoke's).</summary>
        float Size = 1.0f;
        float Grow = 0.0f;
        /// <summary>Its colour, and its strength at its most.</summary>
        float Red = 1.0f;
        float Green = 1.0f;
        float Blue = 1.0f;
        float Alpha = 1.0f;
        /// <summary>How fast it eases toward its rise and the wind's drift (rising kinds), or loses speed (falling ones).</summary>
        float Drag = 0.0f;
        float Rise = 0.0f;
        float Drift = 0.0f;
        /// <summary>A chunk's turn and its turning a second; an ember's flicker phase and rate.</summary>
        float Turn = 0.0f;
        float Spin = 0.0f;
        /// <summary>A chunk trailing smoke: the time to its next puff (below 0: none).</summary>
        float Trail = -1.0f;
        uint8_t Bounces = 0;
        bool Hot = false;
        bool Resting = false;
    };

    /// <summary>The particles' own dice (never the game's): the same throws in the same order every run.</summary>
    class MCDice
    {
    public:
        void Restart() { _Engine.seed(DiceSeed); }

        /// <summary>A number in [0, 1), made the same way on every platform (24 of the engine's bits).</summary>
        float Unit() { return static_cast<float>(_Engine() >> 8) * (1.0f / static_cast<float>(1u << 24)); }

        float Between(float low, float high) { return low + (high - low) * Unit(); }

        /// <summary><paramref name="value"/>, more or less by up to <see cref="Vary"/> of it.</summary>
        float Around(float value) { return value * Between(1.0f - Vary, 1.0f + Vary); }

        /// <summary>A whole number of things, <paramref name="mean"/> on average (its fraction a chance of one more).</summary>
        int32_t Count(float mean) { return static_cast<int32_t>(std::floor(std::max(mean, 0.0f) + Unit())); }

    private:
        std::mt19937 _Engine{DiceSeed};
    };

    /// <summary>A burst this frame's explosions began: where, and how big.</summary>
    struct PendingBurst
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        MCParticles::MCBurstSize Size;
    };

    /// <summary>A flame burning this frame: its place, its bright pixels' centre (across and up from it) and size.</summary>
    struct PendingFire
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Across = 0.0f;
        float Up = 0.0f;
        float Size = 0.0f;
    };

    /// <summary>A unit drawn this frame: which, where and how wide.</summary>
    struct PendingMover
    {
        const void* Unit = nullptr;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float HalfWidth = 0.0f;
    };

    /// <summary>A unit seen in a frame: where it was, the frame it was last seen in and the time to its next ripple.</summary>
    struct Track
    {
        float X = 0.0f;
        float Y = 0.0f;
        uint64_t Seen = 0;
        float Wake = 0.0f;
    };

    /// <summary>A big burst's shake: when it began, how strong and its wobble's phase.</summary>
    struct Shake
    {
        float Start = 0.0f;
        float Strength = 0.0f;
        float Phase = 0.0f;
    };

    /// <summary>How a puff of smoke looks and moves.</summary>
    struct SmokeLook
    {
        float Grey = 0.3f;
        /// <summary>How much redder than grey (and less blue) it is: dust's brown.</summary>
        float Warmth = 0.0f;
        float Alpha = 0.3f;
        float Radius = 3.0f;
        float Grow = 3.0f;
        float LifeLow = 3.0f;
        float LifeHigh = 5.0f;
        float Rise = 12.0f;
    };

    /// <summary>How a burst's chunks look and fly.</summary>
    struct DebrisLook
    {
        float Red = 0.12f;
        float Green = 0.11f;
        float Blue = 0.1f;
        float SizeLow = 0.8f;
        float SizeHigh = 2.0f;
        float SpeedLow = 60.0f;
        float SpeedHigh = 210.0f;
        float LifeLow = 3.0f;
        float LifeHigh = 5.0f;
        /// <summary>The shares of chunks that glow hot as they cool, and that trail smoke.</summary>
        float HotShare = 0.35f;
        float TrailShare = 0.5f;
    };

    /// <summary>A splash's mist, its radius and growth a share of the splash's extent.</summary>
    constexpr SmokeLook SplashMistLook{.Grey = 0.85f,
                                       .Alpha = 0.35f,
                                       .Radius = 0.08f,
                                       .Grow = 0.25f,
                                       .LifeLow = 0.5f,
                                       .LifeHigh = 0.9f,
                                       .Rise = 10.0f};
    /// <summary>Dust off the ground, as a share of the burst's extent.</summary>
    constexpr SmokeLook DustLook{.Grey = 0.42f,
                                 .Warmth = 0.18f,
                                 .Alpha = 0.28f,
                                 .Radius = 0.12f,
                                 .Grow = 0.18f,
                                 .LifeLow = 1.2f,
                                 .LifeHigh = 2.0f,
                                 .Rise = 6.0f};
    /// <summary>A shell's smoke, as a share of the burst's extent.</summary>
    constexpr SmokeLook ShellSmokeLook{
        .Grey = 0.3f, .Alpha = 0.3f, .Radius = 0.1f, .Grow = 0.12f, .LifeLow = 1.5f, .LifeHigh = 3.0f, .Rise = 10.0f};
    /// <summary>A unit's end's black smoke, as a share of the burst's extent.</summary>
    constexpr SmokeLook BlackSmokeLook{
        .Grey = 0.2f, .Alpha = 0.4f, .Radius = 0.1f, .Grow = 0.1f, .LifeLow = 3.0f, .LifeHigh = 5.0f, .Rise = 16.0f};
    /// <summary>A fire's smoke, as a share of its flame's bright side.</summary>
    constexpr SmokeLook FireSmokeLook{.Grey = 0.36f,
                                      .Alpha = 0.22f,
                                      .Radius = 0.25f,
                                      .Grow = 0.22f,
                                      .LifeLow = 4.0f,
                                      .LifeHigh = 7.0f,
                                      .Rise = 11.0f};
    /// <summary>A trailing chunk's puffs, in pixels.</summary>
    constexpr SmokeLook TrailLook{
        .Grey = 0.22f, .Alpha = 0.25f, .Radius = 1.2f, .Grow = 4.0f, .LifeLow = 0.8f, .LifeHigh = 1.2f, .Rise = 6.0f};
    /// <summary>Clods of dirt thrown up off the ground.</summary>
    constexpr DebrisLook DirtLook{.Red = 0.22f,
                                  .Green = 0.17f,
                                  .Blue = 0.11f,
                                  .SizeLow = 0.5f,
                                  .SizeHigh = 1.1f,
                                  .SpeedLow = 40.0f,
                                  .SpeedHigh = 110.0f,
                                  .LifeLow = 1.5f,
                                  .LifeHigh = 2.5f,
                                  .HotShare = 0.0f,
                                  .TrailShare = 0.0f};
    /// <summary>A unit's or a building's wreckage.</summary>
    constexpr DebrisLook WreckLook{};
    /// <summary>Spray: the colour of droplets, mist and ripples.</summary>
    constexpr float SprayRed = 0.88f;
    constexpr float SprayGreen = 0.94f;
    constexpr float SprayBlue = 1.0f;

    /// <summary><paramref name="look"/> with its radius and growth times <paramref name="scale"/>.</summary>
    SmokeLook Scaled(SmokeLook look, float scale)
    {
        look.Radius *= scale;
        look.Grow *= scale;
        return look;
    }

    /// <summary>A shape table's bursts' size, measured once (by the table's place and length).</summary>
    using SizeKey = std::pair<const uint8_t*, size_t>;

    /// <summary>The particles, their dice, what this frame began and the shakes.</summary>
    struct State
    {
        MCParticles::MCParticleLook Look;
        std::vector<Particle> Particles;
        /// <summary>What a frame made (bursts, fires, wakes, and smoke trails as the particles moved), added at its end.</summary>
        std::vector<Particle> Made;
        MCDice Dice;
        /// <summary>The game's time the last frame was drawn at.</summary>
        std::optional<float> Time;
        uint64_t Frame = 0;
        std::vector<PendingBurst> Bursts;
        std::vector<PendingFire> Fires;
        std::vector<PendingMover> Movers;
        std::unordered_map<const void*, Track> Tracks;
        std::vector<Shake> Shakes;
        std::map<SizeKey, MCParticles::MCBurstSize> Sizes;
        std::weak_ptr<MCWindow> ShownWindow;
        MCParticles::MCShake Shown;
    };

    /// <summary>The particles, their dice and what the frame began.</summary>
    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>The pixels across the screen a world unit along the ground takes.</summary>
    float PixelsPerUnit()
    {
        return std::max(std::hypot(MCTerrain::ProjectionCos, MCTerrain::ProjectionSin), 1e-3f);
    }

    /// <summary>A point in <paramref name="camera"/>'s view, as ProjectToScreen puts it but to the pixel's fraction.</summary>
    MCVector2D Project(float x, float y, float z, const MCCamera& camera)
    {
        const float fromTop = MCTerrain::MapTopLeft3d100.Y - y;
        const float fromLeft = x - MCTerrain::MapTopLeft3d100.X;
        return MCVector2D((fromLeft - fromTop) * MCTerrain::ProjectionCos + MCTerrain::MapTopLeft2d100.X -
                              camera.ScreenUL.X + camera.HalfWidth,
                          (fromLeft + fromTop) * MCTerrain::ProjectionSin + MCTerrain::MapTopLeft2d100.Y - z -
                              camera.ScreenUL.Y + camera.HalfHeight);
    }

    /// <summary>The ground's height at (x, y) (0 without a terrain).</summary>
    float GroundUnder(float x, float y)
    {
        return Terrain() != nullptr ? MCTerrain::GetTerrainElevation(MCVector3D(x, y, 0.0f)) : 0.0f;
    }

    /// <summary>Whether the map's tile at (x, y) is water (as the craters' test).</summary>
    bool IsWater(float x, float y)
    {
        int32_t row = 0;
        int32_t col = 0;
        MCScenarioMap::WorldToMapTilePos(MCVector3D(x, y, 0.0f), row, col);
        const MCScenarioMap& map = GameMap();

        if (row < 0 || row >= map.Height || col < 0 || col >= map.Width)
        {
            return false;
        }

        const size_t at = static_cast<size_t>(map.Width) * static_cast<size_t>(row) + static_cast<size_t>(col);
        return at < map.Map.size() && map.Map[at].TerrainType() >= FirstWaterTerrain;
    }

    /// <summary>
    /// How big <paramref name="appearance"/>'s bursts are: its shape's frames' most bright pixels and most pixels, each
    /// as a square's side (MCSceneLights' measure); measured once a shape table.
    /// </summary>
    MCParticles::MCBurstSize SizeOf(State& state, const MCVfxAppearance& appearance)
    {
        const std::shared_ptr<MCShape> shape = appearance.CurrentShape.lock();

        if (shape == nullptr || !VfxIsShapeTable(shape->FrameList()))
        {
            return {};
        }

        const std::span<const uint8_t> table = shape->FrameList();
        const auto [found, isNew] = state.Sizes.try_emplace(SizeKey{table.data(), table.size()});

        if (isNew)
        {
            int32_t fire = 0;
            int32_t all = 0;

            for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
            {
                using enum MCSceneLights::MCGlowPixels;
                fire = std::max(
                    fire,
                    MCSceneLights::MeasureFrame(table, frame, Bright).value_or(MCSceneLights::MCGlowMeasure{}).Pixels);
                all = std::max(
                    all,
                    MCSceneLights::MeasureFrame(table, frame, All).value_or(MCSceneLights::MCGlowMeasure{}).Pixels);
            }

            found->second = {std::sqrt(static_cast<float>(fire)), std::sqrt(static_cast<float>(all))};
        }

        return found->second;
    }

    /// <summary>A particle at (x, y, z), made when there's room; null otherwise.</summary>
    Particle* Make(State& state, MCParticleKind kind, float x, float y, float z)
    {
        if (state.Particles.size() + state.Made.size() >= MCParticles::MaxParticles)
        {
            return nullptr;
        }

        Particle& made = state.Made.emplace_back();
        made.Kind = kind;
        made.X = x;
        made.Y = y;
        made.Z = z;
        made.Ground = GroundUnder(x, y);
        return &made;
    }

    /// <summary>A unit direction with at least <paramref name="minUp"/> of it up, its way round at random.</summary>
    MCVector3D Upward(MCDice& dice, float minUp)
    {
        const float up = dice.Between(minUp, 1.0f);
        const float around = dice.Between(0.0f, TwoPi);
        const float flat = std::sqrt(std::max(1.0f - up * up, 0.0f));
        return MCVector3D(flat * std::cos(around), flat * std::sin(around), up);
    }

    /// <summary>Sets <paramref name="particle"/> going <paramref name="speed"/> pixels a second <paramref name="way"/>.</summary>
    void Launch(Particle& particle, const MCVector3D& way, float speed, float lift)
    {
        const float perUnit = PixelsPerUnit();
        particle.VX = way.X * speed / perUnit;
        particle.VY = way.Y * speed / perUnit;
        particle.VZ = way.Z * speed + lift;
    }

    /// <summary>Sparks flying off <paramref name="at"/>.</summary>
    void Sparks(State& state, const PendingBurst& at, int32_t count)
    {
        MCDice& dice = state.Dice;

        for (int32_t i = 0; i < count; ++i)
        {
            Particle* spark = Make(state, MCParticleKind::Spark, at.X, at.Y, at.Z);

            if (spark == nullptr)
            {
                return;
            }

            Launch(*spark, Upward(dice, SparkMinUp), dice.Between(SparkSpeedLow, SparkSpeedHigh), SparkLift);
            spark->Life = dice.Between(SparkLifeLow, SparkLifeHigh);
            spark->Size = dice.Between(SparkRadiusLow, SparkRadiusHigh);
            spark->Drag = SparkDrag;
        }
    }

    /// <summary>Embers rising from around (x, y, z), up to <paramref name="spread"/> pixels off it.</summary>
    void Embers(State& state, float x, float y, float z, float spread, int32_t count)
    {
        MCDice& dice = state.Dice;
        const float perUnit = PixelsPerUnit();

        for (int32_t i = 0; i < count; ++i)
        {
            Particle* ember = Make(state, MCParticleKind::Ember, x + dice.Between(-spread, spread) / perUnit,
                                   y + dice.Between(-spread, spread) / perUnit, z + dice.Between(-spread, spread));

            if (ember == nullptr)
            {
                return;
            }

            ember->Life = dice.Between(EmberLifeLow, EmberLifeHigh);
            ember->Size = dice.Between(EmberRadiusLow, EmberRadiusHigh);
            ember->Rise = dice.Between(EmberRiseLow, EmberRiseHigh);
            ember->VZ = ember->Rise;
            ember->Drift = EmberDrift;
            ember->Drag = EmberDrag;
            ember->Turn = dice.Between(0.0f, TwoPi);
            ember->Spin = dice.Between(EmberFlickerLow, EmberFlickerHigh);
        }
    }

    /// <summary>Puffs of smoke rising from around (x, y, z), up to <paramref name="spread"/> pixels off it.</summary>
    void Smoke(State& state, float x, float y, float z, float spread, int32_t count, const SmokeLook& look)
    {
        MCDice& dice = state.Dice;
        const float perUnit = PixelsPerUnit();

        for (int32_t i = 0; i < count; ++i)
        {
            Particle* puff = Make(state, MCParticleKind::Smoke, x + dice.Between(-spread, spread) / perUnit,
                                  y + dice.Between(-spread, spread) / perUnit, z + dice.Between(0.0f, spread));

            if (puff == nullptr)
            {
                return;
            }

            const float grey = dice.Around(look.Grey);
            puff->Red = grey * (1.0f + look.Warmth);
            puff->Green = grey;
            puff->Blue = grey * (1.0f - look.Warmth);
            puff->Alpha = dice.Around(look.Alpha);
            puff->Size = dice.Around(look.Radius);
            puff->Grow = dice.Around(look.Grow);
            puff->Life = dice.Between(look.LifeLow, look.LifeHigh);
            puff->Rise = dice.Around(look.Rise);
            puff->VZ = puff->Rise;
            puff->Drift = SmokeDrift;
            puff->Drag = SmokeDrag;
        }
    }

    /// <summary>Chunks thrown up from <paramref name="at"/>.</summary>
    void Debris(State& state, const PendingBurst& at, int32_t count, const DebrisLook& look)
    {
        MCDice& dice = state.Dice;

        for (int32_t i = 0; i < count; ++i)
        {
            Particle* chunk = Make(state, MCParticleKind::Debris, at.X, at.Y, at.Z);

            if (chunk == nullptr)
            {
                return;
            }

            Launch(*chunk, Upward(dice, DebrisMinUp), dice.Between(look.SpeedLow, look.SpeedHigh), 0.0f);
            const float shade = dice.Around(1.0f);
            chunk->Red = look.Red * shade;
            chunk->Green = look.Green * shade;
            chunk->Blue = look.Blue * shade;
            chunk->Size = dice.Between(look.SizeLow, look.SizeHigh);
            chunk->Life = dice.Between(look.LifeLow, look.LifeHigh);
            chunk->Drag = DebrisDrag;
            chunk->Turn = dice.Between(0.0f, TwoPi);
            chunk->Spin = dice.Between(-DebrisSpin, DebrisSpin);
            chunk->Hot = dice.Unit() < look.HotShare;
            chunk->Trail = dice.Unit() < look.TrailShare ? 0.0f : -1.0f;
        }
    }

    /// <summary>
    /// A ring spreading on the ground at (x, y) from <paramref name="from"/> pixels to <paramref name="to"/> over
    /// <paramref name="life"/>, <paramref name="alpha"/> strong, starting in <paramref name="delay"/>.
    /// </summary>
    void Ripple(State& state, float x, float y, float from, float to, float life, float alpha, float delay)
    {
        Particle* ring = Make(state, MCParticleKind::Ripple, x, y, GroundUnder(x, y));

        if (ring != nullptr)
        {
            ring->Size = from;
            ring->Grow = to;
            ring->Life = life;
            ring->Alpha = alpha;
            ring->Age = -delay;
            ring->Red = SprayRed;
            ring->Green = SprayGreen;
            ring->Blue = SprayBlue;
            ring->Resting = true;
        }
    }

    /// <summary>
    /// Droplets thrown up from (x, y, z), <paramref name="out"/> and <paramref name="up"/> times a splash's usual speeds.
    /// </summary>
    void Droplets(State& state, float x, float y, float z, int32_t count, float out, float up)
    {
        MCDice& dice = state.Dice;
        const float perUnit = PixelsPerUnit();

        for (int32_t i = 0; i < count; ++i)
        {
            Particle* drop = Make(state, MCParticleKind::Droplet, x, y, z);

            if (drop == nullptr)
            {
                return;
            }

            const float around = dice.Between(0.0f, TwoPi);
            const float speed = dice.Between(DropletOutLow, DropletOutHigh) * out;
            drop->VX = std::cos(around) * speed / perUnit;
            drop->VY = std::sin(around) * speed / perUnit;
            drop->VZ = dice.Between(DropletUpLow, DropletUpHigh) * up;
            drop->Life = DropletLife;
            drop->Size = dice.Between(DropletRadiusLow, DropletRadiusHigh);
            drop->Red = SprayRed;
            drop->Green = SprayGreen;
            drop->Blue = SprayBlue;
            drop->Alpha = DropletAlpha;
        }
    }

    /// <summary>What a burst throws, as its place (a unit, the ground, water) and size have it.</summary>
    void Burst(State& state, const PendingBurst& at)
    {
        MCDice& dice = state.Dice;
        const float amount = std::max(state.Look.Amount, 0.0f);
        const auto count = [&](float mean) { return dice.Count(mean * amount); };
        const float fire = at.Size.Fire;
        const float extent = at.Size.Extent;
        const float ground = GroundUnder(at.X, at.Y);
        const bool onGround = at.Z - ground < GroundHitHeight;
        const bool big = fire >= BigFire;
        const float spread = extent * BurstSpread;

        if (onGround && IsWater(at.X, at.Y))
        {
            const float strength = std::clamp((fire - BigFire) / (HugeFire - BigFire), 0.0f, 1.0f);
            Droplets(state, at.X, at.Y, ground, count(SplashDroplets + extent * SplashDropletsPerExtent),
                     1.0f + BigSplashOut * strength, 1.0f + 0.5f * BigSplashOut * strength);
            Smoke(state, at.X, at.Y, ground, spread, count(SplashMist + extent * SplashMistPerExtent),
                  Scaled(SplashMistLook, extent));
            const int32_t rings = big ? BigSplashRings : SplashRings;

            for (int32_t i = 0; i < rings; ++i)
            {
                const auto ring = static_cast<float>(i);
                Ripple(state, at.X, at.Y, extent * SplashRingFrom, extent * (SplashRingTo + SplashRingStep * ring),
                       dice.Between(SplashRingLifeLow, SplashRingLifeHigh), SplashRingAlpha, SplashRingDelay * ring);
            }

            return;
        }

        if (onGround)
        {
            // Dirt thrown up, its dust, and sparks off the stones as hot as its fire.
            Debris(state, at, count(DirtClods + extent * DirtClodsPerExtent), DirtLook);
            Smoke(state, at.X, at.Y, ground, spread, count(DustPuffs + extent * DustPuffsPerExtent),
                  Scaled(DustLook, extent));
            Sparks(state, at, count(fire * GroundSparksPerFire));
        }
        else
        {
            Sparks(state, at, count(HitSparks + fire * HitSparksPerFire));
        }

        if (big)
        {
            Debris(state, at, count(fire * BigDebrisPerFire), WreckLook);
            Sparks(state, at, count(fire * BigSparksPerFire));
            Smoke(state, at.X, at.Y, at.Z, spread, count(fire * BigSmokePerFire), Scaled(BlackSmokeLook, extent));
            Embers(state, at.X, at.Y, at.Z, spread, count(fire * BigEmbersPerFire));
        }
        else if (fire >= MediumFire)
        {
            Debris(state, at, count(fire * MediumDebrisPerFire), WreckLook);
            Sparks(state, at, count(fire * MediumSparksPerFire));
            Smoke(state, at.X, at.Y, at.Z, spread, count(fire * MediumSmokePerFire), Scaled(ShellSmokeLook, extent));
        }
    }

    /// <summary>A big burst's shake, as strong as its fire is big and as near the view's middle it is.</summary>
    void AddShake(State& state, const PendingBurst& at, const MCCamera& camera, float now)
    {
        if (at.Size.Fire < BigFire || state.Look.Shake <= 0.0f)
        {
            return;
        }

        const float strength = std::clamp((at.Size.Fire - BigFire) / (HugeFire - BigFire), 0.0f, 1.0f);
        const MCVector2D seen = Project(at.X, at.Y, at.Z, camera);
        const float off = std::hypot(seen.X - camera.HalfWidth, seen.Y - camera.HalfHeight) /
                          std::max(camera.HalfWidth * ShakeReach, 1.0f);
        const float phase = std::fmod(std::abs(at.X * ShakePhaseX + at.Y * ShakePhaseY), TwoPi);
        state.Shakes.push_back(
            {now, state.Look.Shake * (ShakeLeast + (ShakeMost - ShakeLeast) * strength) / (1.0f + off * off), phase});
    }

    /// <summary>How far the shakes move the picture at <paramref name="now"/>, together at most ShakeCap.</summary>
    MCParticles::MCShake ShakeAt(const State& state, float now)
    {
        MCParticles::MCShake shake;

        for (const Shake& one : state.Shakes)
        {
            const float since = now - one.Start;

            if (since < 0.0f || since > ShakeLasts)
            {
                continue;
            }

            const float strength = one.Strength * std::exp(-since / ShakeFade);
            shake.X += strength * std::sin(TwoPi * ShakeRateX * since + one.Phase);
            shake.Y += strength * std::sin(TwoPi * ShakeRateY * since + one.Phase * ShakePhaseApart);
        }

        const float most = ShakeCap * std::max(state.Look.Shake, 0.0f);

        if (const float length = std::hypot(shake.X, shake.Y); length > most)
        {
            shake.X *= most / length;
            shake.Y *= most / length;
        }

        return shake;
    }

    /// <summary>A flame's embers and smoke for <paramref name="step"/> of its burning.</summary>
    void Burn(State& state, const PendingFire& fire, float step)
    {
        MCDice& dice = state.Dice;
        const float amount = std::max(state.Look.Amount, 0.0f);
        // Its bright pixels' centre: across the screen is along both of the ground's axes (MCTerrain::ProjectTerrain).
        const float across = fire.Across / (2.0f * std::max(MCTerrain::ProjectionCos, 1e-3f));
        const float x = fire.X + across;
        const float y = fire.Y + across;
        const float z = fire.Z + fire.Up;
        Embers(state, x, y, z, fire.Size * FireEmberSpread, dice.Count(FireEmbers * fire.Size * amount * step));
        Smoke(state, x, y, z + fire.Size * FireSmokeAbove, fire.Size * FireSmokeSpread,
              dice.Count(FireSmoke * fire.Size * amount * step), Scaled(FireSmokeLook, fire.Size));
    }

    /// <summary>A unit's wake for <paramref name="step"/>: ripples at its feet in water, and foam as it goes.</summary>
    void Wade(State& state, const PendingMover& mover, float step)
    {
        auto [found, isNew] = state.Tracks.try_emplace(mover.Unit);
        Track& track = found->second;
        const float moved = isNew ? 0.0f : std::hypot(mover.X - track.X, mover.Y - track.Y) * PixelsPerUnit();
        track.X = mover.X;
        track.Y = mover.Y;
        track.Seen = state.Frame;
        const float ground = GroundUnder(mover.X, mover.Y);

        if (mover.Z - ground > WadeHeight || !IsWater(mover.X, mover.Y))
        {
            return;
        }

        const float speed = moved < TeleportDistance ? moved / step : 0.0f;
        const bool moving = speed > WadeSpeed;
        track.Wake -= step;

        if (track.Wake > 0.0f)
        {
            return;
        }

        MCDice& dice = state.Dice;
        track.Wake =
            moving ? WakeEvery * std::clamp(WakeSpeed / speed, WakeEveryLeast, WakeEveryMost) : StillRippleEvery;
        Ripple(state, mover.X, mover.Y, mover.HalfWidth * WakeRingFrom,
               mover.HalfWidth * (moving ? WakeRingMoving : WakeRingStill),
               dice.Between(WakeRingLifeLow, WakeRingLifeHigh), moving ? WakeRingAlpha : StillRingAlpha, 0.0f);

        if (moving)
        {
            Droplets(state, mover.X, mover.Y, ground, dice.Count(WakeFoam * std::max(state.Look.Amount, 0.0f)),
                     WakeFoamOut, WakeFoamUp);
        }
    }

    /// <summary>A fallen particle reaching the ground: it bounces, rests or is gone.</summary>
    void Land(State& state, Particle& particle)
    {
        particle.Z = particle.Ground;

        switch (particle.Kind)
        {
            case MCParticleKind::Spark:
            {
                if (particle.Bounces == 0 && -particle.VZ > SparkBounceSpeed)
                {
                    particle.VZ = -particle.VZ * SparkBounce;
                    particle.VX *= SparkBounce;
                    particle.VY *= SparkBounce;
                    ++particle.Bounces;
                    return;
                }

                particle.Age = particle.Life;
                return;
            }

            case MCParticleKind::Droplet:
            {
                if (state.Dice.Unit() < DropletRippleShare && IsWater(particle.X, particle.Y))
                {
                    Ripple(state, particle.X, particle.Y, particle.Size, particle.Size * DropletRippleGrows,
                           DropletRippleLife, DropletRippleAlpha, 0.0f);
                }

                particle.Age = particle.Life;
                return;
            }

            case MCParticleKind::Debris:
            {
                if (IsWater(particle.X, particle.Y))
                {
                    Ripple(state, particle.X, particle.Y, particle.Size, particle.Size * SinkRippleGrows,
                           SinkRippleLife, SinkRippleAlpha, 0.0f);
                    particle.Age = particle.Life;
                    return;
                }

                if (particle.Bounces < DebrisBounces && -particle.VZ > DebrisBounceSpeed)
                {
                    particle.VZ = -particle.VZ * DebrisBounce;
                    particle.VX *= DebrisSkid;
                    particle.VY *= DebrisSkid;
                    particle.Spin *= DebrisSkid;
                    ++particle.Bounces;
                    return;
                }

                particle.VX = 0.0f;
                particle.VY = 0.0f;
                particle.VZ = 0.0f;
                particle.Spin = 0.0f;
                particle.Trail = -1.0f;
                particle.Resting = true;
                return;
            }

            default:
                return;
        }
    }

    /// <summary>Moves <paramref name="particle"/> on by <paramref name="step"/>.</summary>
    void Move(State& state, Particle& particle, float step)
    {
        particle.Age += step;

        if (particle.Age < 0.0f || particle.Resting)
        {
            return;
        }

        const float perUnit = PixelsPerUnit();

        switch (particle.Kind)
        {
            case MCParticleKind::Spark:
            case MCParticleKind::Debris:
            case MCParticleKind::Droplet:
            {
                const float keep = std::exp(-particle.Drag * step);
                particle.VX *= keep;
                particle.VY *= keep;
                particle.VZ = particle.VZ * keep - Gravity * step;
                particle.Turn += particle.Spin * step;
                break;
            }

            case MCParticleKind::Ember:
            case MCParticleKind::Smoke:
            {
                // Toward its rise and the wind's drift; an ember sways across the wind as it goes.
                const float ease = 1.0f - std::exp(-particle.Drag * step);
                const float sway = particle.Kind == MCParticleKind::Ember
                                       ? EmberSway * std::sin(EmberSwayRate * particle.Age + particle.Turn)
                                       : 0.0f;
                const float towardX =
                    (MCSceneLights::WindEast * particle.Drift - MCSceneLights::WindNorth * sway) / perUnit;
                const float towardY =
                    (MCSceneLights::WindNorth * particle.Drift + MCSceneLights::WindEast * sway) / perUnit;
                particle.VX += (towardX - particle.VX) * ease;
                particle.VY += (towardY - particle.VY) * ease;
                particle.VZ += (particle.Rise - particle.VZ) * ease;
                break;
            }

            default:
                return;
        }

        particle.X += particle.VX * step;
        particle.Y += particle.VY * step;
        particle.Z += particle.VZ * step;
        particle.Ground = GroundUnder(particle.X, particle.Y);

        if (particle.Kind == MCParticleKind::Debris && particle.Trail >= 0.0f && particle.Age < DebrisTrailLasts)
        {
            particle.Trail -= step;

            while (particle.Trail < 0.0f)
            {
                particle.Trail += DebrisTrailEvery;
                Smoke(state, particle.X, particle.Y, particle.Z, 0.0f, 1, TrailLook);
            }
        }

        if (particle.Z < particle.Ground)
        {
            if (particle.Kind == MCParticleKind::Ember || particle.Kind == MCParticleKind::Smoke)
            {
                particle.Z = particle.Ground;
            }
            else if (particle.VZ < 0.0f)
            {
                Land(state, particle);
            }
        }
    }

    /// <summary>A colour component as a byte.</summary>
    uint8_t Byte(float value)
    {
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    /// <summary>Sets <paramref name="command"/>'s colour: the light it adds, and the share of what's under it it hides.</summary>
    void SetColor(MCParticleCommand& command, float red, float green, float blue, float alpha)
    {
        command.Red = Byte(red);
        command.Green = Byte(green);
        command.Blue = Byte(blue);
        command.Alpha = Byte(alpha);
    }

    /// <summary>Where a moving particle was <paramref name="ago"/> seconds ago, in the view (its streak's tail).</summary>
    MCVector2D Behind(const Particle& particle, float ago, const MCCamera& camera)
    {
        return Project(particle.X - particle.VX * ago, particle.Y - particle.VY * ago, particle.Z - particle.VZ * ago,
                       camera);
    }

    /// <summary>How <paramref name="particle"/> is drawn, at <paramref name="at"/> in the view.</summary>
    MCParticleCommand Drawn(const Particle& particle, const MCVector2D& at, const MCCamera& camera)
    {
        MCParticleCommand command;
        command.X0 = at.X;
        command.Y0 = at.Y;
        command.X1 = at.X;
        command.Y1 = at.Y;
        command.Radius = particle.Size;
        command.Softness = SoftEdge;
        const float age = std::clamp(particle.Age / particle.Life, 0.0f, 1.0f);
        const float left = particle.Life - particle.Age;

        switch (particle.Kind)
        {
            case MCParticleKind::Spark:
            {
                const MCVector2D tail = Behind(particle, SparkStreak, camera);
                command.X0 = tail.X;
                command.Y0 = tail.Y;
                const float heat = 1.0f - age;
                const float light = SparkLight * std::pow(heat, SparkFade);
                SetColor(command, light, (SparkGreenCool + SparkGreenHot * heat) * light,
                         (SparkBlueCool + SparkBlueHot * heat * heat) * light, 0.0f);
                break;
            }

            case MCParticleKind::Ember:
            {
                const MCVector2D tail = Behind(particle, EmberStreak, camera);
                command.X0 = tail.X;
                command.Y0 = tail.Y;
                command.Softness = EmberSoftness;
                const float flicker =
                    EmberSteady + EmberFlicker * std::sin(particle.Age * particle.Spin + particle.Turn);
                const float fade =
                    std::min(particle.Age / EmberFadeIn, 1.0f) * std::min(left / (EmberFadeOut * particle.Life), 1.0f);
                const float light = EmberLight * flicker * fade;
                SetColor(command, light, (EmberGreen + EmberGreenFlicker * flicker) * light, EmberBlue * light, 0.0f);
                break;
            }

            case MCParticleKind::Smoke:
            {
                command.Radius = particle.Size + particle.Grow * particle.Age * (1.0f - SmokeGrowSlows * age);
                command.Softness = 1.0f;
                const float alpha =
                    particle.Alpha * std::min(particle.Age / SmokeFadeIn, 1.0f) * std::pow(1.0f - age, SmokeFadeOut);
                SetColor(command, particle.Red * alpha, particle.Green * alpha, particle.Blue * alpha, alpha);
                break;
            }

            case MCParticleKind::Debris:
            {
                // A chunk tumbling end over end, glowing as it cools when it's hot, fading once it has lain a while.
                const float half = particle.Size * DebrisLength;
                const float dx = std::cos(particle.Turn) * half;
                const float dy = std::sin(particle.Turn) * half;
                command.X0 = at.X - dx;
                command.Y0 = at.Y - dy;
                command.X1 = at.X + dx;
                command.Y1 = at.Y + dy;
                command.Radius = std::max(particle.Size * DebrisThickness, MinRadius);
                command.Softness = DebrisSoftness;
                const float fade = std::min(left / DebrisFades, 1.0f);
                const float heat = particle.Hot ? std::max(1.0f - particle.Age / DebrisCools, 0.0f) * DebrisGlow : 0.0f;
                SetColor(command, (particle.Red + heat) * fade, (particle.Green + DebrisGlowGreen * heat) * fade,
                         (particle.Blue + DebrisGlowBlue * heat) * fade, fade);
                break;
            }

            case MCParticleKind::Droplet:
            {
                const MCVector2D tail = Behind(particle, DropletStreak, camera);
                command.X0 = tail.X;
                command.Y0 = tail.Y;
                const float alpha = particle.Alpha * std::min(left / DropletFades, 1.0f);
                SetColor(command, particle.Red * alpha, particle.Green * alpha, particle.Blue * alpha, alpha);
                break;
            }

            case MCParticleKind::Ripple:
            {
                const float out = 1.0f - (1.0f - age) * (1.0f - age);
                command.Radius = particle.Size + (particle.Grow - particle.Size) * out;
                command.Ring = RippleWidth;
                command.Aspect = MCSceneLights::GroundAspect();
                const float alpha =
                    particle.Alpha * std::min(age / RippleFadeIn, 1.0f) * std::pow(1.0f - age, RippleFadeOut);
                SetColor(command, particle.Red * alpha, particle.Green * alpha, particle.Blue * alpha,
                         alpha * RippleCover);
                break;
            }
        }

        return command;
    }

    /// <summary>A particle in the frame's draw list, drawn into the pane the list draws into (MCRenderer::Particle).</summary>
    class MCParticleElement final : public MCElement
    {
    public:
        MCParticleElement(const MCParticleCommand& command, int32_t depth) : MCElement(depth), _Command(command) {}

        void Draw() override
        {
            const MCPane& pane = *GlobalPane;
            const std::shared_ptr<MCWindow> window = pane.Window.lock();

            if (window == nullptr)
            {
                return;
            }

            MCParticleCommand placed = _Command;
            placed.X0 += static_cast<float>(pane.X0);
            placed.Y0 += static_cast<float>(pane.Y0);
            placed.X1 += static_cast<float>(pane.X0);
            placed.Y1 += static_cast<float>(pane.Y0);
            MCRenderer::For(*window).Particle(*window, placed);
        }

    private:
        MCParticleCommand _Command;
    };

    /// <summary>
    /// Adds the particles whose marks show in <paramref name="camera"/>'s view to the draw list, each sorted by the ground
    /// under it.
    /// </summary>
    void AddElements(const State& state, const MCCamera& camera)
    {
        const float right = camera.HalfWidth * 2.0f + ViewMargin;
        const float bottom = camera.HalfHeight * 2.0f + ViewMargin;

        for (const Particle& particle : state.Particles)
        {
            if (particle.Age < 0.0f)
            {
                continue;
            }

            const MCParticleCommand command =
                Drawn(particle, Project(particle.X, particle.Y, particle.Z, camera), camera);
            const float across = command.Radius + command.Ring;
            const float down = across * command.Aspect;

            if (std::max(command.X0, command.X1) + across < -ViewMargin ||
                std::max(command.Y0, command.Y1) + down < -ViewMargin ||
                std::min(command.X0, command.X1) - across > right || std::min(command.Y0, command.Y1) - down > bottom)
            {
                continue;
            }

            const MCVector2D foot = Project(particle.X, particle.Y, particle.Ground, camera);
            const int32_t depth =
                (particle.Resting ? GroundDepthFixup : AirDepthFixup) - static_cast<int32_t>(std::floor(foot.Y));
            ElementList().OpenGroup(depth, true);
            ElementList().Add(ElementList().Make<MCParticleElement>(command, depth));
        }
    }

    /// <summary>Drops every particle, shake and pending burst, fire and unit, and restarts the dice.</summary>
    void ClearState(State& state)
    {
        state.Particles.clear();
        state.Made.clear();
        state.Bursts.clear();
        state.Fires.clear();
        state.Movers.clear();
        state.Tracks.clear();
        state.Shakes.clear();
        state.Sizes.clear();
        state.Shown = {};
        state.Time.reset();
        state.Dice.Restart();
    }

    /// <summary>Whether explosions' bursts are wanted (for the particles or the shake).</summary>
    bool Wanted()
    {
        return GHdParticles != 0 || GHdShake != 0;
    }
}

auto MCParticles::Look() -> MCParticleLook&
{
    return TheState().Look;
}

auto MCParticles::AddExplosion(const MCVfxAppearance& appearance, const MCVector3D& position) -> void
{
    if (Wanted())
    {
        AddBurst(position, SizeOf(TheState(), appearance));
    }
}

auto MCParticles::AddBurst(const MCVector3D& position, MCBurstSize size) -> void
{
    if (Wanted())
    {
        TheState().Bursts.push_back({position.X, position.Y, position.Z, size});
    }
}

auto MCParticles::AddFire(const MCVfxAppearance& flame, const MCVector3D& position) -> void
{
    if (GHdParticles == 0)
    {
        return;
    }

    if (const auto bright = MCSceneLights::MeasureDrawn(flame, MCSceneLights::MCGlowPixels::Bright); bright.has_value())
    {
        TheState().Fires.push_back(
            {position.X, position.Y, position.Z, bright->X, -bright->Y, std::sqrt(static_cast<float>(bright->Pixels))});
    }
}

auto MCParticles::AddMover(const void* unit, const MCVector3D& position, float halfWidth) -> void
{
    if (GHdParticles != 0)
    {
        TheState().Movers.push_back({unit, position.X, position.Y, position.Z, halfWidth});
    }
}

auto MCParticles::Draw(const MCCamera& camera) -> void
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
    ++state.Frame;

    if (GHdParticles != 0)
    {
        for (Particle& particle : state.Particles)
        {
            Move(state, particle, step);
        }

        std::erase_if(state.Particles, [](const Particle& particle) { return particle.Age >= particle.Life; });

        // Fires and wakes go on only as the game's time does (no dice thrown while it holds).
        if (step > 0.0f)
        {
            for (const PendingFire& fire : state.Fires)
            {
                Burn(state, fire, step);
            }

            for (const PendingMover& mover : state.Movers)
            {
                Wade(state, mover, step);
            }

            std::erase_if(state.Tracks, [&](const auto& track) { return track.second.Seen != state.Frame; });
        }
    }
    else
    {
        state.Particles.clear();
        state.Tracks.clear();
    }

    for (const PendingBurst& burst : state.Bursts)
    {
        if (GHdShake != 0)
        {
            AddShake(state, burst, camera, now);
        }

        if (GHdParticles != 0)
        {
            Burst(state, burst);
        }
    }

    std::ranges::move(state.Made, std::back_inserter(state.Particles));
    state.Made.clear();
    state.Bursts.clear();
    state.Fires.clear();
    state.Movers.clear();
    std::erase_if(state.Shakes, [&](const Shake& shake) { return now - shake.Start > ShakeLasts; });
    state.ShownWindow = GlobalPane->Window;
    state.Shown = GHdShake != 0 ? ShakeAt(state, now) : MCShake{};

    if (GHdParticles != 0)
    {
        AddElements(state, camera);
    }
}

auto MCParticles::Clear() -> void
{
    ClearState(TheState());
}

auto MCParticles::Count() -> size_t
{
    return TheState().Particles.size();
}

auto MCParticles::ShownShake(const MCWindow& view) -> MCShake
{
    const State& state = TheState();
    return GHdShake != 0 && state.ShownWindow.lock().get() == &view ? state.Shown : MCShake{};
}
