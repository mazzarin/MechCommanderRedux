#pragma once

// The mission: the game's top-level state machine between logistics, briefings, Smacker movies and scenarios
// (mission\mission.cpp). It reads the campaign's mission FIT (movies, scenarios, game segments), starts and ends each
// scenario, and shows the mission results screen afterwards.

#include "gui/MCGuiRef.h"

class MCFitIniFile;
class MCGuiCallback;
class MCLogistics;
class MCMissionResultsScreen;

/// <summary>
/// The states of <see cref="MCMission::Run"/>. They are also read from the mission FIT (<c>GameState</c>,
/// <c>NextGameState</c>), so they keep the original's numbers.
/// </summary>
enum class MCMissionState : int32_t
{
    /// <summary>Start: on to logistics (or the first movie).</summary>
    Start = 0,
    /// <summary>Logistics is up.</summary>
    Logistics = 3,
    /// <summary>The results screen is up.</summary>
    Results = 6,
    /// <summary>A scenario is running.</summary>
    Scenario = 7,
    /// <summary>A movie started from logistics is playing.</summary>
    MoviePlaying = 8,
    /// <summary>Play movie <see cref="MCMission::CurrentMovie"/>.</summary>
    PlayMovie = 10,
    /// <summary>Start scenario <see cref="MCMission::CurrentScenario"/>.</summary>
    StartScenario = 11,
    /// <summary>A game segment's movie is to start (nothing in the mission handles it).</summary>
    SegmentMovie = 12,
    /// <summary>A game segment's movie is playing.</summary>
    SegmentMoviePlaying = 13,
    /// <summary>Quit the game.</summary>
    Quit = 16,
    /// <summary>The demo's feature screen.</summary>
    FeatureScreen = 20
};

/// <summary>
/// The game's top-level state machine. <see cref="Run"/> is called every frame (through the <c>RunMission</c>
/// callback) and switches on <see cref="State"/>: logistics, movies, the scenario, the results screen, the feature
/// screen (demo) and quitting.
/// </summary>
/// <remarks>A game context system (<see cref="Mission"/>), made by the game's start-up.</remarks>
class MCMission
{
public:
    /// <summary>Registers the <c>RunMission</c> callback.</summary>
    MCMission();

    /// <summary>Takes the mission down (<see cref="Shutdown"/>) if it hasn't been.</summary>
    ~MCMission();
    MCMission(const MCMission&) = delete;
    MCMission& operator=(const MCMission&) = delete;

    /// <summary>Loads the mission FIT <paramref name="missionName"/> (campaign control file, or a game segment).</summary>
    /// <returns>0, or an error code.</returns>
    int32_t Load(std::string_view missionName);

    /// <summary>Reloads the movie and scenario lists from <paramref name="missionName"/> (a new campaign).</summary>
    int32_t ReloadCampaign(std::string_view missionName);

    /// <summary>
    /// Reads the <c>GameSegment%d</c> block <paramref name="segment"/>: game state, movie, scenario and next state.
    /// </summary>
    int32_t SetupNextSegment(int32_t segment);

    /// <summary>Frees the FIT, callbacks, scenario and logistics (twice is harmless).</summary>
    void Shutdown();

    /// <summary>One frame of the state machine.</summary>
    int32_t Run();

    /// <summary>
    /// Leaves logistics and starts scenario <paramref name="scenarioName"/>: creates the results screen and the
    /// scenario, and hooks <c>PlayScenario</c> and the interface callback.
    /// </summary>
    void StartScenario(std::string_view scenarioName);

    /// <summary>
    /// Asks for scenario <paramref name="scenarioName"/> to start: <see cref="Run"/> starts it after its frame's work,
    /// once the click or network handler that asked has returned.
    /// </summary>
    /// <remarks>
    /// Starting the scenario frees logistics, and its load draws frames. Started from inside a logistics handler, those
    /// frames drew the screen whose handler was still running after logistics had gone.
    /// </remarks>
    void RequestScenario(std::string_view scenarioName);

    /// <summary>
    /// Ends the scenario: writes the results for logistics, frees the scenario and goes back to logistics (or on to
    /// the next movie / game over).
    /// </summary>
    void EndScenario();

    /// <summary>Unhooks and frees the scenario's per-frame callbacks (the results screen is up).</summary>
    void StopScenarioCallbacks();

    /// <summary>Closes the results screen (which ends the scenario the first time).</summary>
    void CloseResultsScreen();

    /// <summary>Writes the interface window states (zoom, tac map, palette) to <c>windows.fit</c>.</summary>
    static void SaveWindowStatus();

    /// <summary>Restores the interface window states from <c>windows.fit</c>.</summary>
    static void LoadWindowStatus();

    /// <summary>The state machine's current state.</summary>
    MCMissionState State = MCMissionState::Start;
    /// <summary>The state to go to after the current one (<c>NextGameState</c> in the mission FIT).</summary>
    MCMissionState NextState = MCMissionState::Start;
    /// <summary>The results screen shown after a scenario (created in <see cref="StartScenario"/>).</summary>
    MCGuiHandle<MCMissionResultsScreen> ResultsScreen;
    /// <summary>Index into <see cref="Movies"/> of the movie to play next (<c>SmackerMovieId</c>); -1 = none.</summary>
    int32_t CurrentMovie = 0;
    /// <summary>The Smacker movie names (<c>Movie%d</c>).</summary>
    std::vector<std::string> Movies;
    /// <summary>Index into <see cref="Scenarios"/> of the current scenario (<c>ScenarioId</c>); -1 = none.</summary>
    int32_t CurrentScenario = 0;
    /// <summary>The scenario names (<c>Scenario%d</c>).</summary>
    std::vector<std::string> Scenarios;
    /// <summary>The campaign's last scenario (<c>LastScenario</c>).</summary>
    uint32_t LastScenario = 0;
    /// <summary>Copy of <see cref="WaitTime"/> made at scenario start and end.</summary>
    float WaitTimer = 0.0f;
    /// <summary><c>WaitTime</c> from the FIT (120 when missing).</summary>
    float WaitTime = 0.0f;
    /// <summary>The logistics phase (also <c>GlobalLog()</c>), while it exists.</summary>
    std::shared_ptr<MCLogistics> Logistics;
    /// <summary>
    /// Nonzero when the scenario should end now, whatever the script says: 1 from the network, -1 when the player
    /// quits from the pause menu. <see cref="Run"/> then opens the results screen, and the objectives' points all
    /// count.
    /// </summary>
    int32_t EndScenarioRequested = 0;
    /// <summary>Set when the last scenario of the campaign is won.</summary>
    bool GameOver = false;
    /// <summary>Runs <c>PlayScenario</c> every frame while a scenario is up.</summary>
    std::shared_ptr<MCGuiCallback> ScenarioCallback;

private:
    /// <summary>Starts a new logistics phase after a scenario.</summary>
    MCLogistics& StartLogistics();
    /// <summary>The demo's feature screen (state 20).</summary>
    /// <returns>False when its picture is missing (the frame ends there).</returns>
    bool RunFeatureScreen();
    /// <summary>
    /// Reads the movie list (<c>Movies</c> block, with <c>WaitTime</c>) and the scenario list (<c>Scenarios</c> block,
    /// with <c>LastScenario</c> when <paramref name="readLastScenario"/>) of <paramref name="file"/>.
    /// </summary>
    int32_t ReadLists(MCFitIniFile& file, bool campaign, bool readInDemo);

    /// <summary>The open mission FIT.</summary>
    std::unique_ptr<MCFitIniFile> _MissionFile;
    /// <summary>The callback that runs <c>RunMission</c> every frame.</summary>
    std::shared_ptr<MCGuiCallback> _MissionCallback;
    /// <summary>Runs <c>UpdateMouseStateCallback</c> every frame while a scenario is up.</summary>
    std::shared_ptr<MCGuiCallback> _InterfaceUpdateCallback;
    /// <summary>When the current logistics phase began.</summary>
    SYSTEMTIME _LogisticsStart{};
    /// <summary>Which logistics music is playing: 0 none, 1 logistics (track 0x17), 2 briefing (track 0x16).</summary>
    int32_t _PlayingLogisticsMusic = 0;
    /// <summary>Whether the demo's feature-screen music has started.</summary>
    bool _FeatureMusicPlaying = false;
    /// <summary>The scenario <see cref="RequestScenario"/> asked for, until <see cref="Run"/> starts it.</summary>
    std::optional<std::string> _RequestedScenario;
};

/// <summary>The mission (null before the game's start-up makes it).</summary>
std::shared_ptr<MCMission> Mission();

/// <summary>The scenario callback: runs the scenario one frame and updates the sound system.</summary>
void PlayScenario();

/// <summary>The mission callback: runs the mission one frame (and keeps <c>FrameLength</c> between scenarios).</summary>
void RunMission();

/// <summary>The game segment a demo/segment build starts at; 0 for the normal campaign.</summary>
extern int32_t GlobalGameSegment;
/// <summary>The lowest skill a pilot has (gamesys <c>SkillMin</c>).</summary>
extern float MinPilotSkill;
/// <summary>The highest skill a pilot can reach by skill-ups (gamesys <c>SkillMax</c>).</summary>
extern float MaxPilotSkill;
/// <summary>
/// The scenario's outcome from its ABL script (0 while running; below 3 lost, 3 quit, above 3 won).
/// </summary>
extern uint32_t ScenarioResult;
/// <summary>Whether any fire is burning (for the fire sound).</summary>
extern int SomethingOnFire;
/// <summary>The Smacker movies (<c>"data\movies\"</c>).</summary>
