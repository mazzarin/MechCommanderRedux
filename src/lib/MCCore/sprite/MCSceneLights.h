#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

class MCAppearance;
class MCFitIniFile;
class MCFrameOfRef;
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
    /// <summary>
    /// A spotlight (a unit's headlight), its lamp <see cref="Height"/> above (X0, Y0): its aim, from there to where its
    /// beam's middle meets the ground, in the window's pixels. It lights the ground where its beam meets it, falling off
    /// as the square of the distance from the lamp; its colour is its light where its beam's middle meets the ground.
    /// (0, 0): a light that shines every way.
    /// </summary>
    float SpotX = 0.0f;
    float SpotY = 0.0f;
    /// <summary>A spotlight's beam: the cosine of the angle off its middle at which it has faded out (half its width).</summary>
    float SpotCos = 0.0f;
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

/// <summary>
/// Port-only (HD lighting): where the ground lies in the world window: the ground point <c>x</c> meters east and
/// <c>y</c> north of the map's middle shows at window pixel <c>Origin + East * x + North * y</c> (the ground's height
/// left out), so what moves over the ground (the water's ripples) moves with it as the view scrolls and zooms.
/// </summary>
struct MCSceneGround
{
    float OriginX = 0.0f;
    float OriginY = 0.0f;
    float EastX = 0.0f;
    float EastY = 0.0f;
    float NorthX = 0.0f;
    float NorthY = 0.0f;
};

/// <summary>
/// Port-only (HD lighting): the time of day a mission is set at (a map maker's choice, its scenario file's
/// [Environment] TimeOfDay; see MCSceneLights::ReadTimeOfDay). The original's missions are all by day.
/// </summary>
enum class MCTimeOfDay : uint8_t
{
    Day,
    Dusk,
    Night,
};

namespace MCSceneLights
{
    /// <summary>The most lights a frame shows (the brightest; composite.pshader's MaxLights).</summary>
    inline constexpr size_t MaxLights = 64;
    /// <summary>The most fires whose air shimmers in a frame (the biggest; composite.pshader's MaxHeat).</summary>
    inline constexpr size_t MaxHeat = 16;
    /// <summary>
    /// The shadows' group in the draw list (the lights' and the sun's): after the terrain's (50,000,000), the craters'
    /// (30,000,000) and the terrain's overlays' (10,000,000), before every object's (their screen heights, or less), so
    /// a shadow lies on the ground under every unit and object.
    /// </summary>
    inline constexpr int32_t ShadowDepth = 5000000;

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
        float Haze = 1.0f;
        /// <summary>
        /// How strong a light's own glow (MCLight's sprite: the original's white oval, its stand-in for the light) shows,
        /// 0 to 1 as drawn: the light it stands for already lights the world around it.
        /// </summary>
        float GlowOvals = 0.75f;
        /// <summary>
        /// How much the water lives (its colours flowing from one of the original's cycle's steps to the next instead
        /// of ticking, ripples and the sun's glints moving over it, the lights glinting off it, foam along its shores),
        /// 0 to 1 (0: the original's cycle).
        /// </summary>
        float Water = 1.0f;
        /// <summary>
        /// With sun shadows on (<see cref="GHdSunShadows"/>): how much of the light under it an object's shadow takes
        /// away, 0 to 1 (1: the original's solid black).
        /// </summary>
        float SunShadow = 0.55f;
        /// <summary>
        /// With sun shadows on: where a unit's shadow falls by day (the original's objects' baked shadows' way, measured
        /// on its trees), per window pixel a point of it stands above its foot: this many pixels right, and this many
        /// up. At dusk the sun is lower, the shadows longer; at night there is none (<see cref="ShownLighting"/>).
        /// </summary>
        float SunShadowRight = 0.6f;
        float SunShadowUp = 0.25f;
        /// <summary>
        /// With sun shadows on: a tool's sun (--hd-sun-turn) that goes once round the world in this many seconds of the
        /// mission's time, from the day's way, every object casting its own shadow (<see cref="RecastsSunShadows"/>); 0
        /// keeps it still, as the game does.
        /// </summary>
        float SunTurnSeconds = 0.0f;
        /// <summary>With the sun turning: its shadows this many times as long as the day's.</summary>
        float SunTurnLength = 1.0f;
        /// <summary>
        /// With relief on (<see cref="GHdRelief"/>): how much the sun shows the world's relief (its own shading taken
        /// for height, as <see cref="Facing"/>'s, or the HD ground's relief maps): a surface facing the sun takes more
        /// of the world's own light and one facing away less, the more the lower the sun (its shadows' way and length,
        /// <see cref="SunShadowRight"/> and Up; none at night), 0 to 1 (0: lit as flat ground, as drawn; 1: as much as
        /// that sun would).
        /// </summary>
        float SunRelief = 0.75f;
        /// <summary>
        /// With relief on and forests swaying (<see cref="GHdSway"/>, which marks their canopy): how much of the sun's
        /// relief a forest's canopy shows, 0 to 1 of the ground's (0: lit as drawn, so the crowns' tiny bumps don't
        /// glitter as they sway; the lights still show them).
        /// </summary>
        float CanopyRelief = 0.0f;
        /// <summary>
        /// With relief on: how steeply the HD art pack's relief maps (MCHdArt::FindTerrainRelief) stand the HD ground's
        /// bumps for the lights and the sun, where it has them: its slopes times this (0: the ground is lit by its
        /// brightness, as the rest; 3 about matches the brightness relief's strength).
        /// </summary>
        float ReliefMaps = 3.0f;
        /// <summary>
        /// With cloud shadows on (<see cref="GHdClouds"/>): how much of the world's own light a cloud's shadow takes
        /// away, 0 to 1 (a light's own bright pixels and the lights' light keep theirs).
        /// </summary>
        float Clouds = 0.3f;
        /// <summary>With cloud shadows on: the share of the ground under the clouds, 0 to 1.</summary>
        float CloudCover = 0.4f;
        /// <summary>With cloud shadows on: how big the clouds are (a cloud and the gap beside it, about), in meters.</summary>
        float CloudSize = 300.0f;
        /// <summary>With cloud shadows on: how fast they drift before the wind, in meters a second of the game's time.</summary>
        float CloudSpeed = 15.0f;
        /// <summary>
        /// With forests swaying (<see cref="GHdSway"/>): how far a forest's canopy sways at the height of a gust, in
        /// meters along the ground (its crowns lean before the wind and rock about it, each its own way).
        /// </summary>
        float Sway = 0.3f;
        /// <summary>
        /// With forests swaying: how much brighter the canopy shows at the height of a gust, its leaves turned up to it
        /// (0: as drawn).
        /// </summary>
        float SwayLight = 0.05f;
        /// <summary>The colour of the world's own light, times <see cref="Ambient"/> (white by day; night's is blue).</summary>
        std::array<float, 3> AmbientTint{1.0f, 1.0f, 1.0f};
        /// <summary>
        /// How much of their colour the world's own colours lose in its light, 0 to 1 (0 by day): at night the eye sees
        /// little colour but where a light shines.
        /// </summary>
        float AmbientGrey = 0.0f;
        /// <summary>
        /// How much a spotlight's beam shows in the air, from its lamp to where it meets the ground (headlights in a
        /// haze, at night), 0 to 1 of its light there (0: only the ground it lights shows).
        /// </summary>
        float AirBeams = 0.35f;
    };

    /// <summary>The settings the next frames are shown with (the tools set them; a time of day takes its own, see Shown).</summary>
    MCLighting& Lighting();

    /// <summary>
    /// Whether the HD ground takes the HD art pack's relief maps: HD art, HD lighting and relief (GHdRelief) on,
    /// MCLighting::ReliefMaps above 0.
    /// </summary>
    bool ReliefMapsOn();

    /// <summary>
    /// The settings the frames are shown with: <see cref="Lighting"/>'s, at dusk and at night (<see cref="TimeOfDay"/>)
    /// with that time's world light and lights' strength.
    /// </summary>
    MCLighting ShownLighting();

    /// <summary>
    /// Sets the time of day of the mission being played (MCScenario::Load: its scenario file's, see
    /// <see cref="ReadTimeOfDay"/>; the tools' --hd-time-of-day).
    /// </summary>
    void SetMissionTime(MCTimeOfDay time);

    /// <summary>The time of day of the mission being played (day until one is set).</summary>
    MCTimeOfDay MissionTime();

    /// <summary>
    /// The time of day the world is shown at: with HD lighting off, day (as drawn); at HD LIGHTING's DUSK or NIGHT, that;
    /// else the mission's own (<see cref="MissionTime"/>).
    /// </summary>
    MCTimeOfDay TimeOfDay();

    /// <summary>The time of day named <paramref name="name"/> ("day", "dusk" or "night", in any case); none for another.</summary>
    std::optional<MCTimeOfDay> TimeOfDayNamed(std::string_view name);

    /// <summary>
    /// The time of day a scenario file (<paramref name="file"/>) sets its mission at: its block "Environment"'s string
    /// "TimeOfDay" (Day, Dusk or Night), day without one, as the original's missions are. The original game reads
    /// neither, so a mission that sets it plays there as ever, by day.
    /// </summary>
    MCTimeOfDay ReadTimeOfDay(MCFitIniFile& file);

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

    /// <summary>A frame's lit pixels: their centre (from its hot spot, weighted by their light), mean light and count.</summary>
    struct MCGlowMeasure
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::array<float, 3> Color{};
        int32_t Pixels = 0;
    };

    /// <summary>
    /// The <paramref name="pixels"/> of the frame <paramref name="appearance"/> last drew (its Render), as
    /// <see cref="AddGlow"/> measures its light (with HD lighting on or off); none when it drew no such pixels.
    /// </summary>
    std::optional<MCGlowMeasure> MeasureDrawn(const MCVfxAppearance& appearance, MCGlowPixels pixels);

    /// <summary>
    /// The <paramref name="pixels"/> of frame <paramref name="frame"/> of <paramref name="shapeTable"/>, as
    /// <see cref="MeasureDrawn"/>'s; none for a table that isn't a VFX shape table or a frame without such pixels.
    /// </summary>
    std::optional<MCGlowMeasure> MeasureFrame(std::span<const uint8_t> shapeTable, int32_t frame, MCGlowPixels pixels);

    /// <summary>A colour a frame's pixels are drawn in (its light, as a glow's), and how many of them are.</summary>
    struct MCGlowColor
    {
        std::array<float, 3> Color{};
        int32_t Pixels = 0;
    };

    /// <summary>
    /// The colours of the <paramref name="pixels"/> of frame <paramref name="frame"/> of <paramref name="shapeTable"/>
    /// (those <see cref="MeasureFrame"/> measures), in palette order; none for a table that isn't a VFX shape table.
    /// </summary>
    std::vector<MCGlowColor> FrameColors(std::span<const uint8_t> shapeTable, int32_t frame, MCGlowPixels pixels);

    /// <summary>A pixel of a frame: where it is from the frame's hot spot (in its pixels, down the screen), and its light.</summary>
    struct MCGlowPixel
    {
        int32_t X = 0;
        int32_t Y = 0;
        std::array<float, 3> Color{};
    };

    /// <summary>
    /// The <paramref name="pixels"/> of frame <paramref name="frame"/> of <paramref name="shapeTable"/> on their
    /// outline (beside a pixel that isn't one of them); none for a table that isn't a VFX shape table.
    /// </summary>
    std::vector<MCGlowPixel> FrameOutline(std::span<const uint8_t> shapeTable, int32_t frame, MCGlowPixels pixels);

    /// <summary>A shape table's frame.</summary>
    struct MCTableFrame
    {
        std::span<const uint8_t> Table;
        int32_t Frame = 0;
    };

    /// <summary>
    /// The frame <paramref name="appearance"/> last drew (its Render), as <see cref="MeasureDrawn"/> measures it; none when
    /// it drew none of a VFX shape table. Its table is the appearance's shape's, for as long as it keeps it.
    /// </summary>
    std::optional<MCTableFrame> DrawnFrame(const MCVfxAppearance& appearance);

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

    /// <summary>
    /// The way the wind blows, a unit direction east (and north) on the ground: the water's waves roll and the clouds
    /// drift before it (composite.pshader's Wind).
    /// </summary>
    inline constexpr float WindEast = 0.9285f;
    inline constexpr float WindNorth = 0.3714f;

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
    /// At night (<see cref="TimeOfDay"/>), a unit's headlight (a mech's, a vehicle's): a warm spotlight on the ground
    /// ahead of it, widening as it goes. <paramref name="appearance"/> as it last placed itself on the screen, its owner
    /// at <paramref name="position"/>, facing as <paramref name="frame"/> does (MCMover::Frame) turned
    /// <paramref name="turn"/> degrees counterclockwise (a mech's torso's, MCBattleMech::TorsoRotation). Its lamp at
    /// <paramref name="lamp"/> in the world (a mech's cockpit), a small light drawn just over the unit (its parts' draw
    /// list group, <paramref name="depthFixup"/> less its screen height) and hidden as it faces away, its beam tipped
    /// down from its height to meet the ground ahead; with none (a vehicle's: the art has no point for it), a low lamp
    /// at its feet, not drawn.
    /// </summary>
    void AddHeadlight(const MCAppearance& appearance, const MCVector3D& position, const MCFrameOfRef& frame, float turn,
                      const std::optional<MCVector3D>& lamp, int32_t depthFixup);

    /// <summary>
    /// Whether a unit whose status (MCGameObject::Status) is <paramref name="status"/> has its headlight on: running (0)
    /// or starting up (3); not disabled (1), destroyed (2), shutting down (4) or shut down (5).
    /// </summary>
    bool HeadlightOn(int32_t status);

    /// <summary>
    /// Port-only (sun shadows): a frame of an object or a unit cast as its sun shadow (<see cref="AddRecastShadows"/>):
    /// frame <c>Frame</c> of shape table <c>Frames</c>, placed as its own draw (MCVfxElement's x, y, reverse and
    /// scale).
    /// </summary>
    struct MCSunShadowPart
    {
        std::span<const uint8_t> Frames;
        float X = 0.0f;
        float Y = 0.0f;
        int32_t Frame = 0;
        int32_t Reverse = 0;
        int32_t Scale = 1;
    };

    /// <summary>
    /// Port-only: whether the objects' shadows baked into their art, which fall the way the day's sun does, are hidden,
    /// every object and unit casting its own the sun's way instead (<see cref="AddRecastShadows"/>): with sun shadows on
    /// (GHdSunShadows) and HD art drawn by the GPU, at dusk (the sun low, the shadows longer) and night (no sun), and
    /// while a tool's sun turns (MCLighting::SunTurnSeconds).
    /// </summary>
    bool RecastsSunShadows();

    /// <summary>Port-only: whether sun shadows are on and the sun casts any (not at night).</summary>
    bool SunCasts();

    /// <summary>
    /// Port-only: while the objects re-cast their sun shadows and the sun casts any (dusk), how much farther than the
    /// day's sun the shown one casts the shadows painted into art that casts none of its own (a forest's:
    /// MCHdArt::Frame::PaintedShadow), in window pixels right and up; none otherwise, the painted shadow drawn as it is.
    /// </summary>
    std::array<float, 2> PaintedShadowReach();

    /// <summary>
    /// Port-only: while the objects re-cast their sun shadows and the sun casts any (<see cref="RecastsSunShadows"/>,
    /// <see cref="SunCasts"/>), adds <paramref name="parts"/> as one object's or unit's sun shadow
    /// (MCElement::SunShadow) in the shadows' group, then opens the caller's group again (<paramref name="depth"/> and
    /// <paramref name="sortElements"/>, as MCElementBuffer::OpenGroup takes them). Nothing otherwise.
    /// </summary>
    void AddRecastShadows(std::span<const MCSunShadowPart> parts, int32_t depth, bool sortElements);

    /// <summary>
    /// Adds the units' shadows from the frame's lights to the draw list (MCCamera::Render, once the objects have added
    /// their lights and units, before the list is sorted): drawn after the ground and its craters and before every
    /// object, so a unit covers its shadow on the ground behind it, and one standing in another's shadow takes it.
    /// </summary>
    void AddShadows();

    /// <summary>
    /// The air over the fire of the frame <paramref name="appearance"/> last drew (its Render; a burning fire's flame,
    /// not an explosion's fireball, which is gone before the air heats): it shimmers above the frame's bright pixels,
    /// as wide and as high as they are big.
    /// </summary>
    void AddHeat(const MCVfxAppearance& appearance);

    /// <summary>Ends the frame's lights (MCCamera::Render, once the world is drawn): they are what the window shows.</summary>
    void Finish();

    /// <summary>
    /// Port-only (HD weather): the lights added so far to the world being drawn (since <see cref="Begin"/>, its muzzle
    /// flashes too), in its window's pixels; none when HD lighting is off or no world is being drawn. The drops falling
    /// past them catch their light (MCWeather::Draw, once the objects have added theirs).
    /// </summary>
    std::vector<MCSceneLight> Drawing();

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
    /// Where the ground lies in the world window <paramref name="view"/> (as <see cref="Shown"/>, its last finished
    /// frame's); none when HD lighting, cloud shadows, swaying forests and the weather are off or another window was
    /// drawn.
    /// </summary>
    std::optional<MCSceneGround> ShownGround(const MCWindow& view);

    /// <summary>How far the water's colour cycle had gone toward its next step then (WaterCyclePhase).</summary>
    float ShownWaterPhase();

    /// <summary>
    /// The view's tilt: the screen height a ground distance takes across the screen per screen width it takes along it
    /// (MCTerrain's projection, ProjectionSin / ProjectionCos). A light's pool is an ellipse this much flatter.
    /// </summary>
    float GroundAspect();

    /// <summary>
    /// Port-only (HD weather): the light <paramref name="lights"/> (in a world window's pixels) shine on a point in the
    /// air <paramref name="height"/> of that window's pixels above the ground at window point (<paramref name="x"/>,
    /// <paramref name="y"/>), their colours times their strengths added: a light that shines every way as near as the
    /// point is to it, falling off as its pool on the ground does from under it and gone at its reach; a spotlight only
    /// within its beam, falling off as the square of the way from its lamp. A screen height of the ground is
    /// <paramref name="aspect"/> of its width (<see cref="GroundAspect"/>). What a raindrop falling there catches
    /// (MCWeather).
    /// </summary>
    std::array<float, 3> LightInAir(std::span<const MCSceneLight> lights, float x, float y, float height, float aspect);

    /// <summary>
    /// Port-only (HD weather): lights as <see cref="LightInAir"/> takes them, sorted once into the tiles of a window by
    /// where each can reach, so a point in the air (a blizzard's tens of thousands of flakes a frame) adds only the
    /// lights of its tile: the same sums, each light's added in the same order.
    /// </summary>
    class MCAirLights
    {
    public:
        MCAirLights() = default;

        /// <summary>
        /// <paramref name="lights"/> (in a world window's pixels) over that window, <paramref name="width"/> by
        /// <paramref name="height"/> pixels, its ground <paramref name="aspect"/> as LightInAir takes it.
        /// </summary>
        MCAirLights(std::vector<MCSceneLight> lights, float width, float height, float aspect);

        /// <summary>Whether there are no lights.</summary>
        bool Empty() const { return _Lights.empty(); }

        /// <summary>
        /// <see cref="LightInAir"/> of the lights at window point (<paramref name="x"/>, <paramref name="y"/>),
        /// <paramref name="height"/> above the ground.
        /// </summary>
        std::array<float, 3> At(float x, float y, float height) const;

    private:
        /// <summary>
        /// The tile column or row <paramref name="at"/> falls in, of <paramref name="count"/>: the nearest edge's past
        /// them.
        /// </summary>
        static int32_t TileOf(float at, int32_t count);

        std::vector<MCSceneLight> _Lights;
        float _Aspect = 1.0f;
        int32_t _Columns = 0;
        int32_t _Rows = 0;
        /// <summary>
        /// Each tile's lights (their indices, in order) are <see cref="_Indices"/> from its start to the next's.
        /// </summary>
        std::vector<uint32_t> _Starts;
        std::vector<uint32_t> _Indices;
    };
}

/// <summary>
/// Port-only: PREFS "HDLighting": 0 draws the world as the original did; 1 lights it around shots, fires and explosions,
/// at the mission's time of day (MCSceneLights::MissionTime: by day, but for a mission set at dusk or night); 2 the same,
/// every mission at dusk (the world dimmed, the lights stronger); 3 every mission at night (darker, its light blue and
/// its colours paler, the lights stronger still). Off by default.
/// </summary>
extern int GHdLighting;

/// <summary>
/// Port-only: PREFS "HDSunShadows": 0 draws objects' shadows as the original did, solid black; 1 as shadows, darkening
/// what lies under them (MCLighting::SunShadow; HD art's frames only: MCHdArt::MarkBakedShadows). Off by default.
/// </summary>
extern int GHdSunShadows;

/// <summary>
/// Port-only: PREFS "HDClouds": 1 lets the shadows of clouds drift over the world before the wind (MCLighting::Clouds;
/// the presenter's composite, composite.pshader's CloudsAt); 0 shows none, as the original. Off by default.
/// </summary>
extern int GHdClouds;

/// <summary>
/// Port-only: PREFS "HDSway": with HD art, 1 lets a forest's canopy sway before the wind's gusts (MCLighting::Sway; the
/// presenter's composite, composite.pshader's SwayedAt, over the canopy the renderer marks: MCVulkanRenderer's Sways);
/// 0 draws it still, as the original. Off by default.
/// </summary>
extern int GHdSway;

/// <summary>
/// Port-only: PREFS "HDRelief": with HD lighting on, 1 lets the sun show the world's relief (MCLighting::SunRelief)
/// and lights the HD ground by the HD art pack's relief maps where it has them (MCLighting::ReliefMaps); 0 lights the
/// world by day as flat ground and the HD ground's bumps by its brightness. Off by default.
/// </summary>
extern int GHdRelief;

/// <summary>GHdLighting's values.</summary>
enum MCHdLightingSetting : int
{
    HdLightingOff = 0,
    HdLightingOn = 1,
    HdLightingDusk = 2,
    HdLightingNight = 3,
};
