#pragma once

#include "abl/MCAblModule.h"

#include "color/MCPalette.h"
#include "mission/MCScenarioObjectives.h"
#include "mission/MCScenarioPart.h"
#include "object/MCObjectQueue.h"
#include "platform/MCRegisteredBlock.h"

// The scenario: one battle (mission\scenario.cpp). Load reads the scenario FIT and starts every game system (palette,
// cameras, objects, sprites, terrain, ABL, teams, warriors, parts, objectives); Run is the per-frame update; Unload
// shuts it all down again.

class MCGuiObject;
class MCFitIniFile;
class MCMechWarrior;
class MCBaseObject;
class MCGameObject;
class MCMover;

/// <summary>The current battle: its settings, warriors, parts, objectives and ABL brain.</summary>
/// <remarks>
/// A game context system (<see cref="Scenario"/>), made by <c>MCMission::StartScenario</c>. It is installed empty and
/// then loaded, as everything it starts reaches for it; it is unloaded while still installed, as everything it stops
/// does too.
/// </remarks>
class MCScenario
{
public:
    /// <summary>The longest frame the simulation takes (seconds).</summary>
    static constexpr float MaxFrameLength = 0.25f;
    /// <summary>The frame length used when the clock gave none (seconds).</summary>
    static constexpr float DefaultFrameLength = 0.05f;
    /// <summary>The turns the scenario runs before the player gets control.</summary>
    static constexpr int32_t DefaultStartUpTurns = 10;
    /// <summary>Kept limit: the sensor contact blips the <c>SensorContactShape</c> packet file holds (data format).</summary>
    static constexpr int32_t SensorContactShapeCount = 6;

    MCScenario();
    ~MCScenario();
    MCScenario(const MCScenario&) = delete;
    MCScenario& operator=(const MCScenario&) = delete;

    /// <summary>
    /// Loads scenario <paramref name="scenarioName"/> and starts every game system. <paramref name="terrainName"/>,
    /// when given, loads that terrain instead of the FIT's (the editor path; no move maps then).
    /// </summary>
    /// <returns>0, or an error code.</returns>
    int32_t Load(std::string_view scenarioName, std::string_view terrainName = {});

    /// <summary>Shuts every game system down and frees the scenario's data (the scenario must still be installed).</summary>
    void Unload();

    /// <summary>
    /// Advances the clocks (<c>ScenarioTime</c>, <c>Turn</c>), plays the time-limit warnings, counts the start-up turns
    /// down and starts the music when they are over.
    /// </summary>
    int32_t Update();

    /// <summary>Draws the camera views into <paramref name="window"/>.</summary>
    static int32_t Render(MCGuiObject& window);

    /// <summary>One frame: updates the world, runs the scenario's ABL brain and takes its result.</summary>
    int32_t Run();

    /// <summary>Creates the object of part <paramref name="partNumber"/> and gives it its pilot, team and commander.</summary>
    void CreatePartObject(int32_t partNumber);

    /// <summary>
    /// Moves the not-yet-created scenario object with part id <paramref name="partId"/> from the scenario object list
    /// into play.
    /// </summary>
    void CreateScenarioObject(int32_t partId);

    /// <summary>Takes part <paramref name="partNumber"/>'s object out of play and marks the part destroyed.</summary>
    void DestroyPartObject(int32_t partNumber);

    /// <summary>Starts the timers of the objectives that have a time limit.</summary>
    void StartObjectiveTimers();

    /// <summary>(Re)starts objective <paramref name="objectiveNumber"/>'s timer at <paramref name="time"/> milliseconds.</summary>
    /// <returns>0, or <see cref="MCObjectiveList::BadObjective"/>.</returns>
    int32_t SetObjectiveTimer(int32_t objectiveNumber, float time) const;

    /// <summary>Seconds left on objective <paramref name="objectiveNumber"/>'s timer (0 when none).</summary>
    float CheckObjectiveTimer(int32_t objectiveNumber) const;

    /// <summary>
    /// The resource points the scenario earned: the points of the objectives that succeeded (all of them when the
    /// mission ended the scenario early), or 0 when the scenario was lost.
    /// </summary>
    int32_t CalcResourcePointsEarned() const;

    /// <summary>Adds the unused-tonnage bonus as an extra, succeeded objective.</summary>
    void SetupBonus();

    /// <summary>
    /// Runs the brain's <c>handlemessage</c> function with a multiplayer message (the runtime's
    /// <c>MissionMessageCode</c> and <c>MissionMessageParam</c>).
    /// </summary>
    void HandleMultiplayMessage(int32_t code, int32_t param) const;

    /// <summary>Checks whether any of the warriors is fighting an enemy (for the music).</summary>
    void CheckAnyoneInCombat() const;

    /// <summary>Warrior <paramref name="number"/> (1-based), or null.</summary>
    std::shared_ptr<MCMechWarrior> Warrior(uint32_t number) const;

    /// <summary>The number of warriors (<c>NumWarriors</c>).</summary>
    uint32_t NumWarriors() const { return _Warriors.empty() ? 0 : static_cast<uint32_t>(_Warriors.size() - 1); }

    /// <summary>The number of parts (<c>NumParts</c>); <see cref="Parts"/> is 1-based.</summary>
    uint32_t NumParts() const { return Parts.empty() ? 0 : static_cast<uint32_t>(Parts.size() - 1); }

    /// <summary>Sensor contact blip <paramref name="index"/> (0 .. 5).</summary>
    std::span<uint8_t> SensorContactShape(int32_t index) const
    {
        return _SensorContactShapes[static_cast<size_t>(index)].Bytes();
    }

    /// <summary>The waypoint marker shapes (null when the file is missing).</summary>
    std::span<uint8_t> WaypointMarkers() const { return _WaypointMarkers.Bytes(); }

    /// <summary>The parts, 1-based (entry 0 is unused).</summary>
    std::vector<MCPart> Parts;
    /// <summary>The objectives.</summary>
    MCObjectiveList Objectives;
    /// <summary>The parts the script creates later, as their groups list them.</summary>
    std::vector<MCCreatedPart> CreatedParts;
    /// <summary>The objects made at the start but not yet in play (created later by the script).</summary>
    std::unique_ptr<MCObjectQueue> ScenarioObjectList;
    /// <summary><c>ScenarioScript</c>: the ABL script (and the scenario's name on the results).</summary>
    std::string ScenarioScript;
    /// <summary>The script's module handle from <c>AblPreProcess</c>; -1 before.</summary>
    int32_t ScenarioScriptHandle = -1;
    /// <summary>The scenario's ABL brain.</summary>
    std::shared_ptr<MCAblModule> ScenarioBrain;
    /// <summary>The parameters passed to the brain each frame.</summary>
    MCAblParam ScenarioBrainParams{};
    /// <summary>The brain's <c>handlemessage</c> function, if it has one.</summary>
    MCAblSymbolId ScenarioBrainHandleMessage = MCAblSymbolId::None;
    /// <summary>The palette before the scenario's (the interface's), shown again by <see cref="Unload"/>.</summary>
    std::unique_ptr<MCPalette> OldPalette;
    /// <summary><c>CaptureChance</c> (0-4; 2 when missing or out of range).</summary>
    uint8_t CaptureChance = 2;
    /// <summary><c>scenarioTuneNum</c>: the music track started after the start-up turns.</summary>
    uint8_t ScenarioTuneNum = 0;
    /// <summary><c>MaxVisualRange</c>.</summary>
    float MaxVisualRange = 0.0f;
    /// <summary><c>FireVisualRange</c>: how far a firing object is revealed.</summary>
    float FireVisualRange = 0.0f;
    /// <summary><c>MaxWeaponRange</c>.</summary>
    float MaxWeaponRange = 0.0f;
    /// <summary><c>BaseSensorRange</c>.</summary>
    float BaseSensorRange = 0.0f;
    /// <summary><c>AlwaysRevealed</c>: the whole map is revealed.</summary>
    uint8_t AlwaysRevealed = 0;
    /// <summary><c>GodMode</c>.</summary>
    uint8_t GodMode = 0;
    /// <summary>Set when the scenario FIT has an <c>Output</c> block.</summary>
    bool HasOutputBlock = false;
    /// <summary><c>CycleLength</c>: seconds between the water colour cycles.</summary>
    float CycleLength = 0.0f;
    /// <summary><c>TimeLeft</c>: the time limit in seconds; -1 = none.</summary>
    int32_t TimeLimit = -1;
    /// <summary>Set once the two-minute warning played.</summary>
    bool TwoMinuteWarningPlayed = false;
    /// <summary>Set once the thirty-second warning played.</summary>
    bool ThirtySecondWarningPlayed = false;
    /// <summary>Set during the start-up turns (the "loading" static on the interface).</summary>
    bool StartingUp = false;
    /// <summary>Start-up turns left, times 10 (100 at the start).</summary>
    int32_t StartUpCountdown = 0;
    /// <summary>The turns the scenario runs before the player gets control.</summary>
    int32_t StartUpTurns = 0;
    /// <summary>Set until the scenario music starts after the start-up turns.</summary>
    bool MusicPending = false;
    /// <summary>The real time the scenario started (milliseconds; 0 until the start-up turns are over).</summary>
    uint32_t MissionStartTime = 0;
    /// <summary>Seconds since <see cref="MissionStartTime"/>.</summary>
    float RunningTime = 0.0f;
    /// <summary>Tons of unused drop weight per bonus unit (<c>BonusTonnageDivisor</c>).</summary>
    int32_t BonusTonnageDivisor = 5;
    /// <summary>Resource points per bonus unit (<c>BonusPointsPerTon</c>).</summary>
    int32_t BonusPointsPerTon = 200;

private:
    /// <summary>Reads the game system file: ranges, rules, difficulty and the object systems' settings.</summary>
    /// <returns>The fire block's settings, which the effect system takes later.</returns>
    std::pair<int32_t, float> LoadGameSystem();
    /// <summary>Shows the scenario's palette (the interface's is kept in <see cref="OldPalette"/>).</summary>
    void LoadPalette(MCFitIniFile& file);
    /// <summary>Makes the teams and commanders and hands out the support strikes.</summary>
    static void LoadForces(MCFitIniFile& file);
    /// <summary>Reads the music, game scale and visual range blocks and makes the draw list.</summary>
    void LoadSettings(MCFitIniFile& file);
    /// <summary>Loads the sensor contact blips.</summary>
    void LoadSensorContactShapes(MCFitIniFile& file);
    /// <summary>Starts the craters, cameras, objects, sprites, appearances, contacts, effects and collisions.</summary>
    /// <returns>0, or the effect system's FIT error.</returns>
    static int32_t LoadSystems(MCFitIniFile& file, int32_t maxFiresBurning, float maxFireBurnTime);
    /// <summary>Loads the terrain and, unless <paramref name="terrainName"/> is given, its move maps.</summary>
    static void LoadTerrain(MCFitIniFile& file, std::string_view terrainName);
    /// <summary>Loads the ABL libraries and the scenario's own brain.</summary>
    void LoadScript(MCFitIniFile& file);
    /// <summary>Loads the warriors, their brains and brain parameters.</summary>
    void LoadWarriors(MCFitIniFile& file);
    /// <summary>Reads the parts and creates their objects.</summary>
    void LoadParts(MCFitIniFile& file);
    /// <summary>Links the trains, elemental carriers and buses to the parts they carry.</summary>
    void LoadCarriers(MCFitIniFile& file);
    /// <summary>Reads the objectives and the home side's share of them.</summary>
    void LoadObjectives(MCFitIniFile& file);
    /// <summary>Hands out each commander's strikes and builds its groups from the part numbers of its mates.</summary>
    void LoadGroups(MCFitIniFile& file);

    /// <summary>The warriors, 1-based (entry 0 is null).</summary>
    std::vector<std::shared_ptr<MCMechWarrior>> _Warriors;
    /// <summary>The six blips of the <c>SensorContactShape</c> packet file, for objects seen only on sensors.</summary>
    std::array<MCRegisteredBlock, SensorContactShapeCount> _SensorContactShapes;
    /// <summary>The waypoint marker shapes (<c>waypoints.shp</c>).</summary>
    MCRegisteredBlock _WaypointMarkers;
    /// <summary>Set by <see cref="Load"/>, cleared by <see cref="Unload"/>.</summary>
    bool _Loaded = false;
};

/// <summary>The current scenario (null outside one).</summary>
std::shared_ptr<MCScenario> Scenario();

/// <summary>The waypoint marker shapes of the current scenario (empty outside one, or without the file).</summary>
std::span<uint8_t> WaypointMarkerShapes();

/// <summary>The scenario clock used for timing: <c>ScenarioTime</c>, or the real running time in multiplayer.</summary>
extern float ActualTime;
/// <summary>Single-step mode: step the mech animations a frame forward; zeroed each turn.</summary>
extern int NextStep;
/// <summary>Single-step mode: step the mech animations a frame back; zeroed each turn.</summary>
extern int PrevStep;
/// <summary>The turn the scenario ends at; -1 = not ending.</summary>
extern int32_t ScenarioEndTurn;
/// <summary>Kept limit: part ids run from 0x200 to 0xfff (the scenario's group numbering, and the wire's).</summary>
inline constexpr int32_t MoverRosterSize = 0xe00;
/// <summary>Every mover by part id - 0x200.</summary>
extern std::array<std::weak_ptr<MCMover>, MoverRosterSize> MoverRoster;
/// <summary>Mission "MineLayThrottle" (50 when missing): a mine layer's top throttle while laying.</summary>
extern int32_t MineLayThrottle;
/// <summary>Mission "MineSweepThrottle" (50 when missing): a sweeper's top throttle after clearing a mine.</summary>
extern int32_t MineSweepThrottle;
/// <summary>Mission "MineWaitTime": the seconds a mine layer waits on a cell before laying.</summary>
extern float MineWaitTime;
/// <summary>Kept limit: the <c>VisualRangeTable</c> of the game system file has 256 entries (data format).</summary>
inline constexpr int32_t VisualRangeTableSize = 256;
/// <summary>The <c>VisualRangeTable</c> of the game system file (the scenario may replace it).</summary>
extern std::array<int32_t, VisualRangeTableSize> VisualRangeTable;

struct MCMatchOptions;
enum class MCTimeOfDay : uint8_t;

/// <summary>
/// Port-only: the share of the day's sight a unit has at dusk, and at night, and the least a level keeps, in tiles: as
/// in the editor's dusk and night missions' tables (MCEditor's MissionTimeOfDay.h, held to these by the tests).
/// </summary>
inline constexpr double DuskSight = 0.8;
inline constexpr double NightSight = 0.6;
inline constexpr int32_t LeastSight = 1;

/// <summary>
/// Port-only: the sight table at <paramref name="time"/>, from the day's <paramref name="dayTable"/>: at dusk or night
/// each level's cut to <see cref="DuskSight"/> or <see cref="NightSight"/> of it.
/// </summary>
std::array<int32_t, VisualRangeTableSize> MCSightAt(const std::array<int32_t, VisualRangeTableSize>& dayTable,
                                                    MCTimeOfDay time);

/// <summary>
/// Port-only: the sight table a match plays, from the game system's <paramref name="gameSystem"/>: at its host's time
/// of day when every player's game plays that time's sight (MCMatchRule::TimeOfDaySight); none, the mission's own.
/// </summary>
std::optional<std::array<int32_t, VisualRangeTableSize>> MCMatchSight(
    const MCMatchOptions& match, const std::array<int32_t, VisualRangeTableSize>& gameSystem);

/// <summary>
/// Port-only: <paramref name="table"/> with each level's range cut to <paramref name="share"/> of it (rounded, no less
/// than <see cref="LeastSight"/>).
/// </summary>
std::array<int32_t, VisualRangeTableSize> MCSightCut(const std::array<int32_t, VisualRangeTableSize>& table,
                                                     double share);

struct MCWeatherPlay;
enum class MCWeatherKind : uint8_t;

/// <summary>
/// Port-only: what the weather changes in a match's play: its host's weather (else <paramref name="missionWeather"/>)
/// when every player's game plays the weather's play (MCMatchRule::WeatherPlay); none, nothing.
/// </summary>
MCWeatherPlay MCMatchWeatherPlay(const MCMatchOptions& match, MCWeatherKind missionWeather);
/// <summary><c>RevealTacMap</c>: the tactical map shows the revealed picture.</summary>
extern bool DrawRevealedTacMap;
/// <summary><c>AlwaysDraw</c> from the game system file.</summary>
extern uint8_t ForceAlways;
/// <summary>
/// The sensor blip shape a contact of <paramref name="tonnage"/> shows: the large one over 50 tons, the medium one over
/// 35, else the small one (empty when the scenario has none).
/// </summary>
std::span<uint8_t> SensorBlipShape(float tonnage);
