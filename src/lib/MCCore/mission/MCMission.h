#pragma once

// The mission: the game's top-level state machine between logistics, briefings, Smacker movies and scenarios
// (mission\mission.cpp). It reads the campaign's mission FIT (movies, scenarios, game segments), starts and ends each
// scenario, and shows the mission results screen afterwards.

#include "gui/MCGuiRef.h"
#include "gui/MCScreenChange.h"

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
    /// scenario, and hooks <c>PlayScenario</c> and the interface callback. Port-only: <see cref="LoadScenario"/>, then
    /// <see cref="EnterScenario"/>, at once (a game segment's start, the tests'); from a briefing they come in a change
    /// of screen (<see cref="RequestScenario"/>).
    /// </summary>
    void StartScenario(std::string_view scenarioName);

    /// <summary>
    /// Port-only: <see cref="StartScenario"/>'s first part: logistics left (the save, the menus taken down), the
    /// scenario made and loaded under the loading bar, its interface started and its opening view read; the state
    /// stays StartScenario.
    /// </summary>
    void LoadScenario(std::string_view scenarioName);

    /// <summary>Port-only: <see cref="StartScenario"/>'s last part: the scenario loaded plays (state Scenario) from the next frame.</summary>
    void EnterScenario();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 1): the player moves on from the results (their button, a match's REMATCH or
    /// timeout), in change of screen "out of the mission" (gui/MCScreenChange.h): the results fade out live, and once
    /// the screen is dark the results close (<see cref="CloseResultsScreen"/>'s way back), the next screen is readied a
    /// frame at a time, and it fades in. At once, as <see cref="CloseResultsScreen"/>, where no change of screen can run
    /// (a launcher's match ends at its results, at once as the original's).
    /// </summary>
    void LeaveResults();

    /// <summary>
    /// Port-only: starts the tactical interface on the scenario loaded (MCTacticalInterface::StartScenario), the HD copies
    /// of what it makes (the lance icons' pictures and their mechs' diagrams: MCMechIcon::AttachHdArt) read as the
    /// load's and kept, rather than in the scenario's first frame.
    /// </summary>
    static void StartInterface();

    /// <summary>
    /// Port-only (SPEC-memory.md's "Under the loading bar": the opening view): what play's first frames draw, read and
    /// unpacked by HD art's readers as the load's own reads, waited for, and put on the GPU before play, rather than on
    /// the game's thread as each is first drawn: every object and unit whose place lies in the camera's starting view
    /// (at the zoom play starts at: after LoadWindowStatus) or the ring around it the camera's surroundings read ahead
    /// (MCAppearance::NearView), each appearance's packets drawn now (MCAppearance::ReadAhead under
    /// MCHdArt::MCOpeningViewAsks). A unit's sets to come are read behind it, as in play.
    /// </summary>
    static void PrepareOpeningView();

    /// <summary>
    /// Asks for scenario <paramref name="scenarioName"/> to start: <see cref="Run"/> starts it after its frame's work,
    /// once the click or network handler that asked has returned. Port-only (SPEC-menus.md's stage 1): from logistics
    /// (BEGIN MISSION, a match's start), in a change of screen: the briefing fades out live, the loading screen fades in,
    /// the save, the menus' teardown and the load run behind it, it fades out, and the battlefield fades in.
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
    /// <summary>
    /// Port-only: <see cref="EndScenario"/> but for the way back to logistics, which it returns for the caller to do (the
    /// results screen's close does it in the fade into logistics: FadeOutWhile); none when the game doesn't go back
    /// there (on to the next movie or the game's end, a game's second segment).
    /// </summary>
    [[nodiscard]] std::function<void()> EndScenarioLeavingWayBack();

    /// <summary>Unhooks and frees the scenario's per-frame callbacks (the results screen is up).</summary>
    void StopScenarioCallbacks();

    /// <summary>Closes the results screen (which ends the scenario the first time).</summary>
    void CloseResultsScreen();

    /// <summary>
    /// Port-only: the campaign mission whose briefing the way back from the results shows, once the scenario has ended
    /// (EndScenarioLeavingWayBack): the next on a win, this one again otherwise (BackToLogistics), a lost solo mission's
    /// campaign's first (its start); none where the way back goes elsewhere (a won solo mission's main screen, a match,
    /// the campaign's end).
    /// </summary>
    std::optional<size_t> BriefedAfterResults() const;

    /// <summary>
    /// Port-only (SPEC-memory.md's "Between missions"): whether the mission's held art is kept past its end, once the
    /// scenario has ended (EndScenarioLeavingWayBack): a mission plays next whose briefing or room takes the hold (the
    /// campaign's, <see cref="BriefedAfterResults"/>; a match's rematch, MCRoomEnd::RematchAfterMatch). Where none
    /// does (a won solo mission's main screen, the campaign's end, a match's games screen, a launcher's game's end) the
    /// hold is let go of.
    /// </summary>
    bool KeepsHoldPastEnd() const;

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
    /// <summary>Port-only: lets go of the mission's held art once its scenario has ended, unless <see cref="KeepsHoldPastEnd"/>.</summary>
    void LetGoHoldUnlessKept() const;
    /// <summary>
    /// The way back to logistics after a campaign's mission (its next briefing, or the same one's again) or a match's
    /// (the main screen; a launcher's game ends there): <see cref="EndScenarioLeavingWayBack"/>'s last.
    /// </summary>
    void BackToLogistics();
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

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 1): change of screen "into the mission" from BEGIN MISSION (gui/MCScreenChange.h):
    /// the briefing fades out live; dark, the loading screen goes up in its place and fades in, and change "into the
    /// battle" follows (<see cref="StartIntoTheBattle"/>).
    /// </summary>
    MCScreenChange::MCChange IntoTheMission(const std::string& scenarioName);
    /// <summary>
    /// Port-only: change "into the battle", the loading screen in: the save, the menus' teardown and the load behind it
    /// (<see cref="LoadScenario"/>), then it fades out live and, dark, the battlefield fades in from its first frame
    /// (<see cref="EnterScenario"/>). At once where no change can run.
    /// </summary>
    void StartIntoTheBattle(const std::string& scenarioName);
    /// <summary>Port-only: the loading screen put up while the screen is dark, the briefing faded out.</summary>
    void ShowLoadingScreen();
    /// <summary>Port-only: the loading screen taken down (its shape let go of), the battlefield next.</summary>
    void TakeDownLoadingScreen();
    /// <summary>
    /// Port-only: the results closed while the screen is dark, a piece a frame: <see cref="CloseResultsScreen"/>'s way
    /// back (the scenario ended, the menus made again), then what a fade into the screen it shows does (IntoScreenPieces).
    /// </summary>
    std::vector<MCScreenChange::MCPiece> ResultsClosing();

    /// <summary>
    /// What the results lead to once the scenario has ended: logistics, or the campaign's ending movies (the original's
    /// in the results screen's destroy).
    /// </summary>
    void AfterResults();
    /// <summary>Port-only: whether the game ends at the results: a launcher's match (BackToLogistics: KillTheGame).</summary>
    bool EndsAtResults() const;
    /// <summary>
    /// Port-only: what the way back from the results reads ahead from the press, a piece each: the files the menus look
    /// for, the next briefing's map (and its mission's hold with <paramref name="hold"/>, once the scenario has ended),
    /// the menus' HD pictures.
    /// </summary>
    std::vector<MCScreenChange::MCPiece> WayBackAhead(bool hold) const;

    /// <summary>Port-only: the way back from the results (EndScenarioLeavingWayBack's), between the change's pieces.</summary>
    std::function<void()> _WayBack;
};

/// <summary>The mission (null before the game's start-up makes it).</summary>
std::shared_ptr<MCMission> Mission();

/// <summary>The scenario callback: runs the scenario one frame and updates the sound system.</summary>
void PlayScenario();

/// <summary>The mission callback: runs the mission one frame (and keeps <c>FrameLength</c> between scenarios).</summary>
void RunMission();

/// <summary>
/// Reads the component list (<c>compbas.csv</c>, its ranges scaled by <c>gamesys.fit</c>'s) when it hasn't been, or
/// when what's read has changed since (a match begun or ended: lib/MCReleaseData.h).
/// </summary>
void LoadComponentListIfSwitched();

/// <summary>
/// Port-only (SPEC-memory.md's "From the briefing"): the next mission is known, <paramref name="mission"/> (its
/// <c>.fit</c>'s name): its hold starts from its list (MCHdArt::StartBriefingHold of MCHdArt::BriefingFolders), the
/// ground's relief maps beside its tiles while they're drawn (MCSceneLights::ReliefMapsOn).
/// </summary>
void StartMissionHold(const std::string& mission);

/// <summary>The game segment a demo/segment build starts at; 0 for the normal campaign.</summary>
extern int32_t GlobalGameSegment;
/// <summary>
/// Port-only, for test runs (-hdbriefing; no player passes it): a game segment's seconds before its load, its hold started
/// as the mission's briefing would start it (MCHdArt::StartBriefingHold), so a run times the load with a briefing's head
/// start. 0: none.
/// </summary>
extern int32_t TestBriefingSeconds;
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
