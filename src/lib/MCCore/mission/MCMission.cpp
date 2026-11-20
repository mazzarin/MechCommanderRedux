#include "stdafx.h"
#include "mission/MCMission.h"
#include "lib/MCReleaseData.h"
#include "ai/MCMoveGeometry.h"
#include "lib/MCFatal.h"
#include "lib/MCFirstUse.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCInventoryBlock.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "main/MCBenchmark.h"
#include "main/MCGamePaths.h"
#include "main/MCLogMechList.h"
#include "main/MCTelemetry.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCIdleCheck.h"
#include "gui/MCGuiSmackerWindow.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "camera/MCViewWindow.h"
#include "appear/MCAppearance.h"
#include "object/MCObjectSystem.h"
#include "platform/MCDisplay.h"
#include "platform/MCRenderer.h"
#include "color/MCPalette.h"
#include "engine/MCCraterManager.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiPort.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFile.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCControlsPanel.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCSystemConfig.h"
#include "main/MCGameSession.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCMissionResultsScreen.h"
#include "mission/MCScenario.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCRoomEnd.h"
#include "network/MCMultiPlayer.h"
#include "main/MCGameContext.h"
#include "object/MCGameSystemReader.h"
#include "object/MCMasterComponent.h"
#include "object/MCObjectTypeManager.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCTacticalMap.h"
#include "vfx/MCVfxFunctions.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCSmacker.h"
#include "platform/MCWin32Defs.h"
#include "logistics/MCBriefingScreen.h"

int32_t GlobalGameSegment = 0;
int32_t TestBriefingSeconds = 0;
float MinPilotSkill = 0.0f;
float MaxPilotSkill = 0.0f;
uint32_t ScenarioResult = 0;
int SomethingOnFire = 0;

namespace
{
    /// <summary>
    /// Port-only (20 Nov, the A10's 4 GB card: play's first frames with the world, its second and third, waited 16 and
    /// 20 ms for the GPU, still busy with the load's last copies): the load ends once the GPU has done all it was given,
    /// so play's frames don't wait on it. (Play's first two frames draw no world: MCScenario::Render waits for turn 2.)
    /// </summary>
    void WaitForTheLoadsGpuWork()
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();

        if (display == nullptr)
        {
            return;
        }

        using Ms = std::chrono::duration<double, std::milli>;
        const auto began = std::chrono::steady_clock::now();
        display->WaitForGpu();

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("the load's GPU work done: {:.1f} ms waited for it",
                                         Ms(std::chrono::steady_clock::now() - began).count()));
        }
    }

    /// <summary>
    /// An empty movie or scenario name list (the port's name): the mission heap's <c>malloc(0)</c> returned null.
    /// </summary>
    constexpr int32_t NoRamForMissionLists = static_cast<int32_t>(0xFACC0003);
    /// <summary>The logistics music.</summary>
    constexpr int32_t LogisticsTrack = 0x17;
    /// <summary>The briefing music.</summary>
    constexpr int32_t BriefingTrack = 0x16;
    /// <summary>Bytes in a megabyte, for the frame log's notes.</summary>
    constexpr double BytesPerMb = 1024.0 * 1024.0;

    /// <summary>
    /// Reads the names <c>&lt;prefix&gt;0</c> .. <c>&lt;prefix&gt;(count-1)</c> of the current block into
    /// <paramref name="names"/>.
    /// </summary>
    /// <returns>0, a FIT read error, or NoRamForMissionLists for a count of 0.</returns>
    int32_t ReadNameList(MCFitIniFile& file, std::string_view prefix, uint32_t count, std::vector<std::string>& names)
    {
        names.assign(count, std::string());

        if (count == 0)
        {
            return NoRamForMissionLists;
        }

        for (uint32_t i = 0; i < count; i++)
        {
            MCFitResult<std::string> name = file.Read<std::string>(std::format("{}{}", prefix, i));

            if (!name)
            {
                return std::to_underlying(name.error());
            }

            names[i] = std::move(*name);
        }

        return 0;
    }

    /// <summary>Reads entry <paramref name="name"/> as the original's ReadId calls did (zero when missing).</summary>
    template <MCFitValue T> int32_t ReadLegacy(MCFitIniFile& file, std::string_view name, T& value)
    {
        return MCGameSystemReader::ReadValue(file, name, value);
    }

    /// <summary>Reads a state entry (zero when missing).</summary>
    int32_t ReadState(MCFitIniFile& file, std::string_view name, MCMissionState& state)
    {
        uint32_t value = std::to_underlying(state);
        const int32_t result = ReadLegacy(file, name, value);
        state = static_cast<MCMissionState>(value);
        return result;
    }

    /// <summary>Switches a full-screen display back to 8 bits after a movie.</summary>
    void ResetFullScreenDisplay()
    {
        if (GFullScreen != 0)
        {
            GuiSystem().ResetDisplay(GuiSystem().Width(), GuiSystem().Height(), 8);
        }
    }

    /// <summary>A new callback running <paramref name="exec"/>, registered with the application.</summary>
    std::shared_ptr<MCGuiCallback> AddCallback(std::function<void()> exec)
    {
        auto callback = std::make_shared<MCGuiCallback>();
        callback->SetExec(std::move(exec));
        GuiSystem().AddCallback(callback);
        return callback;
    }

    /// <summary>Unregisters and frees <paramref name="callback"/>, if there is one.</summary>
    void RemoveCallback(std::shared_ptr<MCGuiCallback>& callback)
    {
        if (callback != nullptr)
        {
            if (HasGuiSystem())
            {
                GuiSystem().RemoveCallback(*callback);
            }

            callback.reset();
        }
    }

    /// <summary>
    /// Port-only, for test runs (-hdbriefing): reads ahead each of the player's mechs in the scenario's file, as its
    /// briefing reads each lance mech as it's placed (MCHdArt::HoldMechAhead).
    /// </summary>
    void HoldPlayersMechs(const std::string& scenarioName)
    {
        MCFitIniFile file;

        if (file.Open(GamePath(MissionPath, scenarioName, ".fit")) != 0 || file.SeekBlock("Parts") != 0)
        {
            SDL_Log("-hdbriefing: no parts in %s's file; its mechs are read at the load", scenarioName.c_str());
            return;
        }

        const uint32_t parts = file.Read<uint32_t>("NumParts").value_or(0);

        // Part n is the file's Part<n> block.
        for (uint32_t part = 1; part <= parts; ++part)
        {
            if (file.SeekBlock(std::format("Part{}", part)) == 0 && file.Read<bool>("PlayerPart").value_or(false))
            {
                if (const MCFitResult<uint32_t> chassis = file.Read<uint32_t>("ObjectNumber"); chassis.has_value())
                {
                    MCHdArt::HoldMechAhead(MechTreeFolders(*chassis));
                }
            }
        }
    }

    /// <summary>
    /// Port-only, for test runs (-hdbriefing): whether a game segment's load still waits for its TestBriefingSeconds,
    /// its hold started on the first call as the mission's briefing would start it (its list, then the player's mechs).
    /// </summary>
    bool WaitingAtBriefing(const std::string& scenarioName)
    {
        using Clock = std::chrono::steady_clock;
        static std::optional<Clock::time_point> started;

        if (GlobalGameSegment == 0 || TestBriefingSeconds <= 0)
        {
            return false;
        }

        if (!started.has_value())
        {
            started = Clock::now();
            StartMissionHold(scenarioName);
            HoldPlayersMechs(scenarioName);
            MCFrameLog::Note(std::format("briefing: the hold starts, {} s before the load", TestBriefingSeconds));
            MCHdArt::TraceNote("briefing: the hold starts");
        }

        if (Clock::now() - *started < std::chrono::seconds(TestBriefingSeconds))
        {
            return true;
        }

        // Once: a later scenario of the run loads at once.
        TestBriefingSeconds = 0;
        MCFrameLog::Note(std::format("briefing: Launch, {:.1f} MB held, {} reads",
                                     static_cast<double>(MCHdArt::HeldBytes()) / BytesPerMb, MCHdArt::HeldReads()));
        MCHdArt::TraceNote("briefing: Launch");
        return false;
    }
}

auto Mission() -> std::shared_ptr<MCMission>
{
    return MCGameContext::Current().Mission();
}

auto PlayScenario() -> void
{
    // Port: a running scenario draws on the whole window, whatever its size now.
    if (Mission() != nullptr && Mission()->State == MCMissionState::Scenario)
    {
        MCFollowWindowSize();
    }

    GlobalPane = ScreenPort()->SharedFrame();

    if (Scenario() != nullptr && ScenarioResult == 0)
    {
        ScenarioResult = static_cast<uint32_t>(Scenario()->Run());

        // Port-only (Redux HD): a benchmark's battle plays on past the mission's end (main/MCBenchmark.h).
        if (MCBenchmark::Running())
        {
            ScenarioResult = 0;
        }
    }

    if (SoundSystem() != nullptr && UseSound != 0)
    {
        MCFrameLog::Scope part("logic.sound");
        SoundSystem()->Update();
    }
}

auto RunMission() -> void
{
    Mission()->Run();

    if (Scenario() == nullptr)
    {
        FrameLength =
            static_cast<float>(static_cast<uint32_t>(PerfStopTime - PrevStart)) / static_cast<float>(CountsPerSecond);
    }
}

MCMission::MCMission()
{
    if (HasGuiSystem())
    {
        _MissionCallback = AddCallback(RunMission);
    }
}

MCMission::~MCMission()
{
    Shutdown();
    // The scenario is gone by now, so this only closes the screen (the original's went with the mission).
    CloseResultsScreen();
}

auto MCMission::ReadLists(MCFitIniFile& file, bool campaign, bool readInDemo) -> int32_t
{
    int32_t result = file.SeekBlock("Movies");

    if (result != 0)
    {
        return result;
    }

    uint32_t numMovies = 0;
    result = ReadLegacy(file, "NumMovies", numMovies);

    if (result != 0)
    {
        return result;
    }

    if (readInDemo)
    {
        bool inDemo = false;
        InDemo = ReadLegacy(file, "InDemo", inDemo) == 0 && inDemo ? 1 : 0;
    }

    // A campaign may have no movies; a game segment must have some.
    if (campaign && numMovies == 0)
    {
        Movies.clear();
    }
    else if (result = ReadNameList(file, "Movie", numMovies, Movies); result != 0)
    {
        return result;
    }

    if (ReadLegacy(file, "WaitTime", WaitTime) != 0)
    {
        WaitTime = 120.0f;
    }

    result = file.SeekBlock("Scenarios");

    if (result != 0)
    {
        return result;
    }

    uint32_t numScenarios = 0;
    result = ReadLegacy(file, "NumScenarios", numScenarios);

    if (result != 0)
    {
        return result;
    }

    if (campaign)
    {
        result = ReadLegacy(file, "LastScenario", LastScenario);

        if (result != 0)
        {
            return result;
        }
    }

    return ReadNameList(file, "Scenario", numScenarios, Scenarios);
}

auto StartMissionHold(const std::string& mission) -> void
{
    MCHdArt::StartBriefingHold(mission, MCHdArt::BriefingFolders(mission), MCSceneLights::ReliefMapsOn());
    MCFilesAhead::ReadPiecesAhead(MCHdArt::MissionFilePieces(mission));
}

namespace
{
    /// <summary>The count of MCReleaseData::Switches the component list was read at.</summary>
    uint32_t ComponentListSwitches = 0;
}

auto LoadComponentListIfSwitched() -> void
{
    // Port-only (Redux HD): read again when what's read has changed since (a match begun or ended), so a match plays
    // the release's weapons and single player the game folder's.
    if (!MasterComponentList.empty() && ComponentListSwitches == MCReleaseData::Switches())
    {
        return;
    }

    ComponentListSwitches = MCReleaseData::Switches();
    // MCX.EXE allocates this FIT (Fatal " Game System File " when out of memory) and never frees it.
    MCFitIniFile gameSystemFile;
    uint32_t readResult = static_cast<uint32_t>(gameSystemFile.Open(GamePath(MissionPath, "gamesys", ".fit")));
    Assert(readResult == 0, readResult, " Could not open GameSys.Fit file ");
    readResult = static_cast<uint32_t>(gameSystemFile.SeekBlock("General"));
    Assert(readResult == 0, readResult, " Could not find General Block in GameSys ");
    float maxVisualRange = 0.0f;
    float maxWeaponRange = 0.0f;
    float baseSensorRange = 0.0f;
    readResult = static_cast<uint32_t>(ReadLegacy(gameSystemFile, "MaxVisualRange", maxVisualRange));
    Assert(readResult == 0, readResult, " Could not find MaxVisualRange in GameSys ");
    readResult = static_cast<uint32_t>(ReadLegacy(gameSystemFile, "MaxWeaponRange", maxWeaponRange));
    Assert(readResult == 0, readResult, " Could not find MaxWeaponRange in GameSys ");
    readResult = static_cast<uint32_t>(ReadLegacy(gameSystemFile, "BaseSensorRange", baseSensorRange));
    Assert(readResult == 0, readResult, " Could not find BaseSensorRange in GameSys ");
    gameSystemFile.Close();

    std::string componentName = GamePath(ObjectPath, "compbas", ".csv");
    const int32_t loadResult =
        InitMasterComponentListExcel(componentName.data(), 0xff, maxVisualRange / maxWeaponRange, baseSensorRange);
    Assert(loadResult == 0, static_cast<uint32_t>(loadResult), " Could not load compBas.csv ");
}

auto MCMission::Load(std::string_view missionName) -> int32_t
{
    if (GlobalGameSegment != 0)
    {
        // A game segment build: the mission FIT is the segment file itself.
        CheatsOn = true;
        _MissionFile = std::make_unique<MCFitIniFile>();
        int32_t result = _MissionFile->Open(GamePath(MissionPath, missionName, ".fit"));

        if (result == 0)
        {
            result = ReadLists(*_MissionFile, false, false);
        }

        return result != 0 ? result : SetupNextSegment(GlobalGameSegment);
    }

    // The campaign: the control file names the campaign file, whose FIT holds the movies and scenarios.
    MCPort::GetSystemTime(_LogisticsStart);
    std::string campaignFile;

    {
        MCFitIniFile controlFile;
        int32_t result = controlFile.Open(GamePath(MissionPath, missionName, ".fit"));

        if (result == 0)
        {
            result = controlFile.SeekBlock("Control");
        }

        uint32_t numCampaigns = 0;

        if (result == 0)
        {
            result = ReadLegacy(controlFile, "NumCampaigns", numCampaigns);
        }

        // Port fix: MCX.EXE leaves the name uninitialised (and splits stack garbage) when there are 2+ campaigns.
        if (result == 0 && numCampaigns < 2)
        {
            result = controlFile.SeekBlock("Campaign0");

            if (result == 0)
            {
                result = ReadLegacy(controlFile, "CampaignFile", campaignFile);
            }
        }

        if (result != 0)
        {
            return result;
        }
    }

    _MissionFile = std::make_unique<MCFitIniFile>();
    const std::string campaignName = MCFileSystem::ToUtf8(MCFileSystem::FromUtf8(campaignFile).stem());

    if (const int32_t result = _MissionFile->Open(GamePath(MissionPath, campaignName, ".fit")); result != 0)
    {
        return result;
    }

    if (FileExists("ixtlriimceourl"))
    {
        CheatsOn = true;
    }

    if (const int32_t result = ReadLists(*_MissionFile, true, true); result != 0)
    {
        return result;
    }

    // The component list, which logistics needs before any scenario has loaded it.
    LoadComponentListIfSwitched();

    // Logistics.
    Logistics = std::make_shared<MCLogistics>();
    Logistics->Start();

    if (LaunchedFromLobby == 0)
    {
        // The result is dropped, as in the original.
        SetupNextSegment(GlobalGameSegment);
    }
    else
    {
        State = MCMissionState::Logistics;
        Assert(MultiPlayer() != nullptr, 0);

        if (MultiPlayer()->SetupLobbyGame() != 0)
        {
            // The original's message; port (personal): a rejoin's says why (network/MCLobbyUpdate.h). OK ends the game
            // (port, Redux HD: a room the menus opened says it over the INTERNET screen).
            const bool fromMenus = LobbyLaunchArgs.FromMenus;
            MCLobbyUpdate::JoinFailed();

            if (!fromMenus)
            {
                GlobalLog()->SetCurrentScreen(GlobalLog()->MainScreen);
                GlobalLog()->LogisticsState = 1;
                GlobalLog()->ShowLogScreen(false, false);
            }
        }
        else
        {
            // Port (Redux HD): a room the menus opened, come back to after an update: the menus' again.
            if (LobbyLaunchArgs.FromMenus)
            {
                MCLobbyUpdate::BackToMenus();
            }

            // Port-only (the personal build): straight into the room, as a LAN game's hosting and joining go (the
            // original waited in the ready room for the host's GO).
            GlobalLog()->SetUpSessionScreen();
        }
    }

    return 0;
}

auto MCMission::ReloadCampaign(std::string_view missionName) -> int32_t
{
    MCFitIniFile file;
    const int32_t result = file.Open(GamePath(MissionPath, missionName, ".fit"));
    return result != 0 ? result : ReadLists(file, true, false);
}

auto MCMission::SetupNextSegment(int32_t segment) -> int32_t
{
    int32_t result = _MissionFile->SeekBlock(std::format("GameSegment{}", segment));

    if (result == 0)
    {
        result = ReadState(*_MissionFile, "GameState", State);
    }

    if (result == 0)
    {
        result = ReadLegacy(*_MissionFile, "SmackerMovieId", reinterpret_cast<uint32_t&>(CurrentMovie));
    }

    if (result == 0)
    {
        result = ReadLegacy(*_MissionFile, "ScenarioId", reinterpret_cast<uint32_t&>(CurrentScenario));
    }

    if (result == 0)
    {
        result = ReadState(*_MissionFile, "NextGameState", NextState);
    }

    if (result != 0)
    {
        return result;
    }

    if (GlobalGameSegment == 0)
    {
        Logistics->GetCurrentMission();
        return 0;
    }

    State = MCMissionState::StartScenario;
    return 0;
}

auto MCMission::Shutdown() -> void
{
    Movies.clear();
    Scenarios.clear();
    _RequestedScenario.reset();
    _MissionFile.reset();
    RemoveCallback(ScenarioCallback);
    RemoveCallback(_InterfaceUpdateCallback);

    if (Scenario() != nullptr)
    {
        SaveWindowStatus();
        Scenario()->Unload();
        MCGameContext::Current().SetScenario(nullptr);
    }

    // Port-only: the mission's held art let go of when its session ends without the mission's end (MCHdArt::StartHold).
    MCHdArt::LetGoHold();
    RemoveCallback(_MissionCallback);

    if (GlobalLog() != nullptr)
    {
        if (Logistics != nullptr)
        {
            Logistics.reset();
        }

        SetGlobalLog({});
    }

    State = MCMissionState::Start;
}

auto MCMission::Run() -> int32_t
{
    KeepScreenBlack = false;
    // The states that end up back in logistics share the tails of the binary's switch; these flags stand in for its
    // two jump targets (reset the display first, or not).
    bool resetDisplay = false;
    bool toLogistics = false;

    switch (State)
    {
        case MCMissionState::Start:
        {
            if (CurrentMovie == -1)
            {
                break;
            }

            if (NextState == MCMissionState::PlayMovie)
            {
                State = MCMissionState::PlayMovie;
                break;
            }

            if (GFullScreen != 0)
            {
                GuiSystem().ResetDisplay(GuiSystem().Width(), GuiSystem().Height(), 8);
                GuiSystem().PaletteCycle = 1;
                GuiSystem().ActivatePalette(GamePalette().RgbData, 0, 0x100);
            }

            toLogistics = true;
            break;
        }
        case MCMissionState::Logistics:
        {
            if (Logistics != nullptr && Logistics->CurrentScreen()->IsShowing() == 0)
            {
                Logistics->ShowLogScreen(true, false);
            }

            // Port (personal): the reports' notice or question, once due and the main menu is free; then a key for
            // a command whose new default key the player uses for another (logistics/MCControlsPanel.h); a lobby's
            // room through a restart into an update (network/MCLobbyUpdate.h); an internet game's room asking a
            // player away whether it's still there (logistics/MCIdleCheck.h); port-only (Redux HD): a host's room
            // ending once its guests have left on its word (network/MCRoomEnd.h).
            ShowReportsNotice();
            AskGivenWayKeys();
            MCLobbyUpdate::Frame();
            MCIdleCheck::Frame();
            MCRoomEnd::Frame();

            EscapedSmackerMovie = 0;
            MovieOver = 0;
            GuiSystem().SetCursorVisible(true);

            if (SoundSystem() != nullptr)
            {
                if (Logistics == nullptr)
                {
                    break;
                }

                const bool onMainScreen = Logistics->CurrentScreen() == Logistics->MainScreen;

                if (onMainScreen && _PlayingLogisticsMusic != 1)
                {
                    SoundSystem()->PlayDigitalMusic(LogisticsTrack, true);
                    _PlayingLogisticsMusic = 1;
                }

                if (!onMainScreen && _PlayingLogisticsMusic != 2)
                {
                    SoundSystem()->PlayDigitalMusic(BriefingTrack, true);
                    _PlayingLogisticsMusic = 2;
                }
            }

            if (Logistics != nullptr && MultiPlayer() != nullptr)
            {
                MultiPlayer()->ProcessReceiveList();
            }
            break;
        }
        case MCMissionState::Results:
        {
            if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
            {
                MultiPlayer()->ProcessReceiveList();
            }

            // Port-only (Redux HD): the room's end from the results (network/MCRoomEnd.h).
            MCRoomEnd::Frame();
            break;
        }
        case MCMissionState::Scenario:
        {
            if ((ScenarioResult != 0 || EndScenarioRequested != 0) && !Scenario()->StartingUp)
            {
                Scenario()->SetupBonus();
                ResultsScreen->Activate();
                State = MCMissionState::Results;
                _PlayingLogisticsMusic = 0;
            }
            break;
        }
        case MCMissionState::MoviePlaying:
        {
            if (GuiSystem().SmackerWindow != nullptr)
            {
                break;
            }

            if (NextState == MCMissionState::PlayMovie)
            {
                State = MCMissionState::PlayMovie;
                CurrentMovie++;

                if (CurrentMovie == 2)
                {
                    NextState = MCMissionState::Logistics;
                }
                else if (CurrentMovie == 5)
                {
                    NextState = MCMissionState::Quit;
                    State = MCMissionState::Quit;
                }
                else
                {
                    NextState = MCMissionState::PlayMovie;
                }
                break;
            }

            if (GameOver)
            {
                State = MCMissionState::Quit;
                break;
            }

            resetDisplay = true;
            break;
        }
        case MCMissionState::PlayMovie:
        {
            const int32_t movie = CurrentMovie;

            if (movie == -1)
            {
                break;
            }

            if (EscapedSmackerMovie == 0)
            {
                if (SoundSystem() != nullptr)
                {
                    SoundSystem()->StopDigitalMusic();
                }

                std::string movieName = GamePath(CDmoviePath, Movies[static_cast<size_t>(movie)], ".smk");
                // MCX.EXE: when movie 1 (the opening) isn't installed, it scans drives C: to Z: for a CD-ROM holding
                // \data\movies\opening.smk. The port reads movies from the install only.
                GuiSystem().StartSmackerMovie(movieName);
                State = MCMissionState::MoviePlaying;
                _PlayingLogisticsMusic = 0;
                break;
            }

            EscapedSmackerMovie = 0;

            if (GameOver)
            {
                State = MCMissionState::Quit;
                break;
            }

            ResetFullScreenDisplay();
            _PlayingLogisticsMusic = 0;
            toLogistics = true;
            break;
        }
        case MCMissionState::StartScenario:
        {
            if (CurrentScenario != -1)
            {
                const std::string scenarioName = (MultiPlayer() == nullptr || GlobalLog() == nullptr)
                                                     ? Scenarios[static_cast<size_t>(CurrentScenario)]
                                                     : std::string(GlobalLog()->MpMissionName);

                // Port-only, for test runs: -hdbriefing's head start before a game segment's load.
                if (WaitingAtBriefing(scenarioName))
                {
                    break;
                }

                if (SoundSystem() != nullptr)
                {
                    SoundSystem()->StopDigitalMusic();
                }

                StartScenario(scenarioName);
                GuiSystem().SetCursorVisible(true);
            }
            break;
        }
        case MCMissionState::SegmentMoviePlaying:
        {
            // (The original waited here for a second movie window, which nothing in MCX.EXE ever opened.)
            if (NextState == MCMissionState::PlayMovie)
            {
                if (CurrentMovie++ != 0)
                {
                    State = MCMissionState::PlayMovie;
                    NextState = MCMissionState::Logistics;
                    break;
                }

                State = MCMissionState::SegmentMovie;
                NextState = MCMissionState::PlayMovie;
                break;
            }

            if (InDemo != 0 && CurrentMovie == 2)
            {
                ResetFullScreenDisplay();
                State = MCMissionState::FeatureScreen;
                break;
            }

            if (GameOver && CurrentMovie == 3)
            {
                State = MCMissionState::Quit;
                break;
            }

            resetDisplay = true;
            break;
        }
        case MCMissionState::Quit:
            MCInput::PostMessage(WM_CLOSE, 0, 0);
            break;
        case MCMissionState::FeatureScreen:
        {
            if (!RunFeatureScreen())
            {
                return 0;
            }
            break;
        }

        default:
            break;
    }

    if (_RequestedScenario.has_value())
    {
        const std::string scenarioName = std::move(*_RequestedScenario);
        _RequestedScenario.reset();
        StartScenario(scenarioName);
    }

    if (resetDisplay)
    {
        ResetFullScreenDisplay();
        toLogistics = true;
    }

    if (toLogistics)
    {
        State = MCMissionState::Logistics;

        if (GlobalGameSegment == 0)
        {
            Logistics->GetCurrentMission();
        }
    }

    if (SoundSystem() != nullptr && (Scenario() == nullptr || EventsToMissionResultsScreen != 0))
    {
        SoundSystem()->Update();
    }

    return 0;
}

auto MCMission::RunFeatureScreen() -> bool
{
    if (FeatureScreen != nullptr && !_FeatureMusicPlaying)
    {
        SoundSystem()->PlayDigitalMusic(8, false);
        _FeatureMusicPlaying = true;
    }

    if (FeatureScreen == nullptr)
    {
        // The demo's feature screen: features.tga with the palette from its own colour map.
        MCFile pictureFile;
        std::string fileName = std::format("{}{}", std::string_view(ArtPath), "features.tga");

        if (pictureFile.Open(fileName) != 0)
        {
            fileName = "features.tga";

            if (pictureFile.Open(fileName) != 0)
            {
                GeneralMsg(std::format("Error reading '{}'", fileName));
            }
        }

        const uint32_t size = pictureFile.FileSize();

        if (size == 0)
        {
            GeneralMsg(std::format("Error reading '{}'", fileName));
            return false;
        }

        std::vector<uint8_t> picture(size);
        pictureFile.Read(picture);
        pictureFile.Close();
        // The TGA's colour map starts at +0x12, as 256 BGR triples of 8-bit components.
        std::array<uint8_t, 0x300> palette = {};

        for (int32_t i = 0; i < 0x100; i++)
        {
            palette[i * 3 + 0] = picture[0x12 + i * 3 + 2] >> 2;
            palette[i * 3 + 1] = picture[0x12 + i * 3 + 1] >> 2;
            palette[i * 3 + 2] = picture[0x12 + i * 3 + 0] >> 2;
        }

        GuiSystem().SetCursorVisible(false);
        FeatureScreen = ScreenWindow()->CreateChild<MCGuiObject>(
            [](MCGuiObject& screen)
            {
                FeatureScreen = screen.weak_from_this();
                screen.Init(0, 0, 640, 480, {});
                // The picture is copied onto the screen's own (MCX.EXE never freed it).
                MCGuiPort picturePort;
                picturePort.Init(const_cast<char*>("features.tga"));
                picturePort.CopyTo(screen.Port().Frame(), 0, 0, false);
            });
        FeatureScreen->ShowGuiWindow(true);
        GuiSystem().ActivatePalette(palette, 0, 0x100);
    }

    if (FeatureScreenDone != 0)
    {
        CloseGuiObject(FeatureScreen);
        FeatureScreen.reset();
        State = MCMissionState::Quit;
    }

    return true;
}

auto MCMission::StartScenario(std::string_view name) -> void
{
    // A copy: the name may live in the logistics phase this frees.
    std::string scenarioName(name);
    // The mission's own name, kept: a campaign's load reads its logistics' copy of the file ("bridge").
    const std::string missionName = scenarioName;

    // Port-only: the last mission's held art let go of before this one's load reads (MCHdArt::StartHold); this one's,
    // held since its briefing (SPEC-memory.md's "From the briefing", MCHdArt::StartBriefingHold), kept for its load.
    if (!MCHdArt::HoldsFor(scenarioName))
    {
        MCHdArt::LetGoHold();
    }

    // Port (personal): the reports measure the load, then the play (main/MCTelemetry.h).
    MCTelemetry::PlayLoading(scenarioName, MultiPlayer() != nullptr);

    SoundSystem()->PlayBettySample(0x13);
    EndScenarioRequested = 0;
    // A child of the screen window, hidden until the scenario ends (Activate shows it).
    ResultsScreen = ScreenWindow()->CreateChild<MCMissionResultsScreen>(
        [this](MCMissionResultsScreen& screen)
        {
            ResultsScreen = screen.WeakAs<MCMissionResultsScreen>();
            screen.Init();
            screen.ShowGuiWindow(false);
        });

    if (GlobalGameSegment == 0)
    {
        GlobalLog()->PrepareScenario(scenarioName, "bridge");
    }

    if (Logistics != nullptr)
    {
        // Destroyed before the pointers to it are cleared, as in the original.
        Logistics.reset();
        SetGlobalLog({});
    }

    // How long logistics took: the time of day of (end - start) as a FILETIME, so whole days are dropped.
    SYSTEMTIME logisticsEnd{};
    MCPort::GetSystemTime(logisticsEnd);
    const uint64_t startTicks = MCPort::SystemTimeToFileTime(_LogisticsStart);
    const uint64_t endTicks = MCPort::SystemTimeToFileTime(logisticsEnd);
    SYSTEMTIME elapsed{};
    MCPort::FileTimeToSystemTime(endTicks - startTicks, elapsed);
    TotalLogisticsTime = static_cast<float>(elapsed.wMinute) * 60.0f + static_cast<float>(elapsed.wHour) * 3600.0f +
                         static_cast<float>(elapsed.wSecond) + static_cast<float>(elapsed.wMilliseconds / 1000);

    GamePalette().Activate();
    InitAlphaLookup(GamePalette().Colors());
    GuiSystem().PaletteCycle = 1;
    GuiSystem().SetCursorVisible(false);

    MCGameContext::Current().SetScenario(std::make_unique<MCScenario>());

    if (GlobalGameSegment == 0)
    {
        scenarioName = "bridge";
    }

    // Port: the scenario's windows are made at the window's size.
    MCFollowWindowSize();

    {
        // Port-only (SPEC-memory.md's "From the briefing"): the pieces of the game folder's files this load reads.
        struct MCNotePieces
        {
            MCNotePieces() { MCFilesAhead::StartNotingPieces(); }
            ~MCNotePieces() { MCFilesAhead::StopNotingPieces(); }
            MCNotePieces(const MCNotePieces&) = delete;
            MCNotePieces& operator=(const MCNotePieces&) = delete;
        };

        const MCNotePieces noting;

        if (const int32_t result = Scenario()->Load(scenarioName); result != 0)
        {
            Fatal(result, " Couldnt init scenario ");
        }
    }

    // Port-only (SPEC-memory.md's "The mission's manifest"): what this load handed the hold, kept as the mission's
    // list for its next briefing, so a mission the program has no list for (a mod's, a multiplayer map, one made in the
    // editor) is held from its second play on; and the pieces of the game's files it read, which its briefing reads
    // ahead from then on (what's still being read ahead of this load is of no more use).
    if (MCHdArt::NumberedFolders())
    {
        MCHdArt::KeepHoldList(missionName, Scenario()->HoldList(scenarioName).Folders);
    }

    MCFilesAhead::ForgetPieces();
    MCHdArt::KeepFilePieces(missionName, MCFilesAhead::NotedPieces());

    StartInterface();
    LoadWindowStatus();
    PrepareOpeningView();
    ScenarioResult = 0;
    State = MCMissionState::Scenario;
    WaitTimer = WaitTime;
    ARedrawScreen();
    ScenarioCallback = AddCallback(PlayScenario);

    if (_InterfaceUpdateCallback == nullptr)
    {
        _InterfaceUpdateCallback = std::make_shared<MCGuiCallback>();
    }

    _InterfaceUpdateCallback->SetExec(UpdateMouseStateCallback);
    GuiSystem().AddCallback(_InterfaceUpdateCallback);
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    MCTelemetry::PlayStarted(game != nullptr ? game->NumPlayers() : 1, game != nullptr && game->IsServer);
    WaitForTheLoadsGpuWork();
    MCFrameLog::Note("mission started");
    // Port-only (lib/MCFirstUse.h): from here the game's thread reads and unpacks nothing for the first time.
    MCFirstUses::PlayStarted();
}

auto MCMission::StartInterface() -> void
{
    TacticalInterface()->StartScenario();
    MCLogistics::ReadTexturesAhead(MCHdArt::ReadAs::Load);
    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();

    // Port-only (SPEC-memory.md's cache budget): the results screen's pictures, which it first draws the frame the
    // mission's end is decided, kept from here (the load's reads kept) until the hold lets go.
    if (const std::shared_ptr<MCMission> mission = Mission(); mission != nullptr && mission->ResultsScreen != nullptr)
    {
        mission->ResultsScreen->KeepPicturesForEnd();
    }

    // Port-only (SPEC-first-use.md: HD craters and footprints): theirs, read at the load, kept from here too.
    if (MCGameContext::Current().HasCraterManager())
    {
        CraterManager().KeepHdArtForMission();
    }

    // Port-only (SPEC-memory.md's fill): the mission's art was held under the loading bar (MCHdArt::StartLoadFill); the
    // hold goes on holding the archives the mission opens in play.
}

auto MCMission::PrepareOpeningView() -> void
{
    const std::shared_ptr<MCCamera> eye = Eye();

    if (eye == nullptr || !MCHdArt::NumberedFolders())
    {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    eye->FollowViewSize();

    {
        const MCHdArt::MCOpeningViewAsks asking;

        // Every object looked at (FindIf finds none).
        ObjectList()->FindIf(
            [&eye](MCBaseObject& object)
            {
                if (const std::shared_ptr<MCAppearance> appearance = object.GetAppearance();
                    appearance != nullptr && !appearance->Owner.expired() &&
                    MCAppearance::NearView(appearance->GetScreenPos(*eye), *eye))
                {
                    appearance->ReadAhead();
                }

                return false;
            });
    }

    MCHdArt::WaitForLoadReads();
    const std::vector<std::shared_ptr<const MCHdArt::Frame>> frames = MCHdArt::KeepOpeningView();
    size_t bytes = 0;

    for (const std::shared_ptr<const MCHdArt::Frame>& frame : frames)
    {
        bytes += MCHdArt::ColorsOf(*frame).size() + frame->Indices.size();
    }

    // Their pictures made and sent up now, shown nowhere, rather than in play's first frame.
    size_t onGpu = 0;

    if (const std::shared_ptr<MCRenderer> hardware = MCRenderer::Hardware();
        hardware != nullptr && MCRenderer::GpuDrawing() == MCGpuDrawing::On)
    {
        onGpu = hardware->PrepareHdPictures(frames);

        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr && onGpu > 0)
        {
            if (const std::expected<void, std::string> ran = display->RunRecorded(); !ran)
            {
                SDL_Log("MCMission: the opening view's %zu HD pictures didn't go up before play: %s", onGpu,
                        ran.error().c_str());
            }
        }
    }

    MCFrameLog::Note(
        std::format("the opening view: {} HD frames read ({:.1f} MB), {} on the GPU, {:.1f} ms", frames.size(),
                    static_cast<double>(bytes) / BytesPerMb, onGpu,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()));
}

auto MCMission::RequestScenario(std::string_view scenarioName) -> void
{
    _RequestedScenario = std::string(scenarioName);
}

auto MCMission::StopScenarioCallbacks() -> void
{
    MCTelemetry::PlayEnded();
    MCFirstUses::PlayEnded();
    RemoveCallback(_InterfaceUpdateCallback);
    TacticalInterface()->HideTags();
    RemoveCallback(ScenarioCallback);
}

auto MCMission::CloseResultsScreen() -> void
{
    if (ResultsScreen == nullptr)
    {
        return;
    }

    CloseGuiObject(ResultsScreen);
    ResultsScreen.reset();
    GuiSystem().CursorHidden = false;

    // The original's results screen did the rest in its destroy: end the scenario and pick the next state.
    if (Scenario() == nullptr)
    {
        return;
    }

    const auto nextState = [this]
    {
        if (GlobalGameSegment == 0)
        {
            if (!GameOver)
            {
                GamePaused = false;
                State = MCMissionState::Logistics;
                return;
            }

            if (InDemo != 0)
            {
                GamePaused = false;
                CurrentMovie = 2;
                State = MCMissionState::FeatureScreen;
                NextState = MCMissionState::FeatureScreen;
                return;
            }

            CurrentMovie = 3;
            State = MCMissionState::PlayMovie;
            NextState = MCMissionState::PlayMovie;
        }

        GamePaused = false;
    };

    // Port-only: the way back to logistics from the results (the screens made again, the campaign loaded, the briefing
    // set up, or a match's room: 75-100 ms on the M4 Pro, two-thirds of it looking for and reading files) is done in
    // the fade into logistics once the screen is nearly black, then what the fade into the screen it shows does
    // (FadeOutWhile), where the results screen sat frozen for it before its fade began. From the click the files it
    // will look for are read ahead on threads of their own (MCFilesAhead: those looked for in the archives while
    // logistics was up before, its screens made of the same files every time), and the next briefing's map with the
    // HD pictures; near black the game's thread makes the screens from them. A launcher's match ends at its results
    // (KillTheGame), at once as the original's.
    const std::function<void()> wayBack = EndScenarioLeavingWayBack();
    LetGoHoldUnlessKept();

    if (wayBack == nullptr)
    {
        nextState();
        return;
    }

    const auto back = [&wayBack, &nextState]
    {
        wayBack();
        nextState();
    };

    // A launcher's match ends there (BackToLogistics: KillTheGame).
    const std::shared_ptr<MCMultiPlayer> match = MultiPlayer();

    if (match != nullptr && !MCRoomEnd::TakesOnAfterMatch() && match->InMission && IsMPlayerGame)
    {
        back();
        return;
    }

    // What was read ahead and not taken goes once the screen is up, whatever happens on the way.
    struct MCForget
    {
        MCForget() = default;
        MCForget(const MCForget&) = delete;
        MCForget& operator=(const MCForget&) = delete;
        ~MCForget() { MCFilesAhead::Forget(); }
    } forget;

    MCFilesAhead::ReadAhead(MCFilesAhead::Noted(), MCGameContext::Current().FastFiles());

    if (const std::optional<size_t> briefed = BriefedAfterResults(); briefed.has_value())
    {
        MCLogistics::ReadBriefingMapAhead(Scenarios[*briefed]);
        // Port-only (SPEC-memory.md's "From the briefing": between missions): the next mission's hold from now, as its
        // briefing would start it: a loss's replay keeps what's held, a win's next lets go of the last mission's first.
        StartMissionHold(Scenarios[*briefed]);
    }

    // Port-only (SPEC-first-use.md's "Between missions"): the screens' whole set of HD pictures read ahead from the click
    // too, after the briefing's map: the briefing, or a match's games screen and its room (the rematch's, or the next
    // one made), finds its own at its first draw. Logistics' start asks again (MCLogistics::MakeAhead), for nothing.
    GuiSystem().ReadMenuSetAhead();

    FadeOutWhile(back);
}

auto MCMission::BriefedAfterResults() const -> std::optional<size_t>
{
    // The way back's own choice (EndScenarioLeavingWayBack, BackToLogistics): a solo mission lost goes back to its
    // campaign's start, won to the main screen.
    constexpr uint32_t LostBelow = 3;
    const bool won = ScenarioResult > LostBelow;

    if (MultiPlayer() != nullptr || GlobalGameSegment != 0 || GameOver || (won && Solo))
    {
        return std::nullopt;
    }

    const size_t briefed =
        Solo && ScenarioResult < LostBelow ? 0 : static_cast<size_t>(std::max(CurrentScenario, 0)) + (won ? 1 : 0);

    if (briefed >= Scenarios.size())
    {
        return std::nullopt;
    }

    return briefed;
}

auto MCMission::KeepsHoldPastEnd() const -> bool
{
    return MultiPlayer() != nullptr ? MCRoomEnd::RematchAfterMatch() : BriefedAfterResults().has_value();
}

auto MCMission::LetGoHoldUnlessKept() const -> void
{
    if (!KeepsHoldPastEnd())
    {
        MCHdArt::LetGoHold();
    }
}

auto MCMission::StartLogistics() -> MCLogistics&
{
    MCPort::GetSystemTime(_LogisticsStart);
    Logistics = std::make_shared<MCLogistics>();
    Logistics->Start();
    return *Logistics;
}

auto MCMission::EndScenario() -> void
{
    const std::function<void()> wayBack = EndScenarioLeavingWayBack();
    LetGoHoldUnlessKept();

    if (wayBack != nullptr)
    {
        wayBack();
    }
}

auto MCMission::EndScenarioLeavingWayBack() -> std::function<void()>
{
    // The play's end, if the results screen didn't mark it (twice is harmless).
    MCTelemetry::PlayEnded();
    MCFirstUses::PlayEnded();
    TotalScenarioTime = ScenarioTime;

    if (GlobalGameSegment == 0 && MultiPlayer() == nullptr)
    {
        std::string bridgeName = Scenario()->ScenarioScript;

        if (const int32_t result = MCMissionLogisticsBridge::MissionResultsStartingFitWriter(bridgeName.data());
            result != 0)
        {
            Fatal(result, " Unable to write Mission to Logistics Bridge File ");
        }
    }

    EndScenarioRequested = 0;
    RemoveCallback(ScenarioCallback);
    TacticalInterface()->EndScenario();
    SomethingOnFire = 0;

    if (SoundSystem() != nullptr)
    {
        SoundSystem()->PurgeSoundSystem();
    }

    // (MCX.EXE worked out the resource points here and threw them away: the results screen already counted them.)
    if (Scenario() != nullptr)
    {
        Scenario()->Unload();
        MCGameContext::Current().SetScenario(nullptr);
    }

    // Port-only (SPEC-memory.md's "Between missions"): the mission's held art isn't let go of here; the caller lets go of
    // it unless a mission plays next whose briefing or room takes it (KeepsHoldPastEnd; MCHdArt::StartBriefingHold lets
    // go of another mission's): a loss replays the same mission, a rematch the same map.

    // Port-only: each way back to logistics is returned to be done by the caller (EndScenario at once).
    if (ScenarioResult > 3)
    {
        if (Solo != 0)
        {
            // A won solo (quick-start) mission: back to the main screen.
            return [this]
            {
                MCLogistics& logistics = StartLogistics();
                LastLogisticsMissionState = 0;
                State = MCMissionState::Logistics;
                logistics.CurrentScreen()->ShowGuiWindow(false);
                logistics.SetCurrentScreen(logistics.MainScreen);
                logistics.LogisticsState = 1;
                logistics.ShowLogScreen(true, true);
                Solo = false;
            };
        }

        if (static_cast<uint32_t>(CurrentScenario) == LastScenario)
        {
            GameOver = true;
        }
    }

    if (ScenarioResult < 3 && Solo != 0)
    {
        // A lost solo mission: restart the campaign at its first briefing.
        return [this]
        {
            MCLogistics& logistics = StartLogistics();
            LastLogisticsMissionState = 0;
            State = MCMissionState::Logistics;
            Solo = true;
            logistics.CurrentMission = 0;
            CurrentScenario = 0;
            CurrentMovie = 1;
            char startName[] = "start1";
            char extension[] = ".pkk";
            logistics.LoadCampaign(startName, extension, true, false);
            logistics.SetUpBriefingScreen(false);
            logistics.ShowLogScreen(true, false);
        };
    }

    if (GlobalGameSegment == 0 && !GameOver)
    {
        return [this] { BackToLogistics(); };
    }

    State = MCMissionState::Quit;
    WaitTimer = WaitTime;
    CurrentScenario = -1;
    return {};
}

auto MCMission::BackToLogistics() -> void
{
    MCLogistics& logistics = StartLogistics();

    if (MultiPlayer() == nullptr)
    {
        // The campaign: a win moves on to the next mission, anything else replays this one.
        int32_t missionId = CurrentScenario;

        if (ScenarioResult < 4)
        {
            logistics.CurrentMission = missionId;
        }
        else
        {
            missionId++;
            logistics.CurrentMission = missionId;
            CurrentScenario = missionId;
        }

        CurrentMovie = missionId + 1;
        logistics.GetCurrentMission();
        const bool replay = ScenarioResult < 4;
        std::string startName =
            std::format("start{}", replay ? logistics.CurrentMission + 1 : logistics.CurrentMission);
        char extension[] = ".pkk";
        logistics.LoadCampaign(startName, extension, replay, false);
        logistics.SetUpBriefingScreen(false);
        logistics.ShowLogScreen(true, false);
        return;
    }

    LastLogisticsMissionState = 0;
    MultiPlayer()->ChatCallback = LogisticsChatCallback;
    State = MCMissionState::Logistics;
    logistics.CurrentScreen()->ShowGuiWindow(false);
    logistics.SetCurrentScreen(logistics.MainScreen);
    logistics.LogisticsState = 1;
    logistics.ShowLogScreen(true, true);

    // Port-only (Redux HD): a game the menus took to its room goes back to its games screen (network/MCRoomEnd.h).
    if (MCRoomEnd::AfterMatch())
    {
        return;
    }

    if (MultiPlayer()->InMission != 0)
    {
        if (IsMPlayerGame != 0)
        {
            KillTheGame();
        }

        MCGameContext::Current().SetMultiPlayer(nullptr);
    }
}

auto MCMission::SaveWindowStatus() -> void
{
    MCFitIniFile windowFile;
    // windows.tmp in the original: under the process ID, as copies of the game on one machine share the user folder
    // and leave a multiplayer mission together.
    const std::string tempName = std::format("windows.{}.tmp", MCPort::ProcessId());

    if (windowFile.Open(tempName, MCFileMode::Create) != 0)
    {
        return;
    }

    windowFile.WriteBlock("Info");

    if (MainHolder()->GetPane(0) != nullptr && MainHolder()->GetPane(0)->GetCamera() != nullptr)
    {
        // The original's zoomed-out mode (camera scale 1); the port's zoom isn't saved.
        windowFile.WriteIdBoolean("MainZoomed", false);
    }

    windowFile.WriteIdBoolean("TacHidden", TacticalMap()->IsHidden());
    windowFile.WriteIdBoolean("ShowPalette", TacticalMap()->PaletteFrame->IsShowing());
    windowFile.Close();
    MCFileSystem::RemoveFile("windows.fit");
    MCFileSystem::RenameFile(tempName, "windows.fit");
}

auto MCMission::LoadWindowStatus() -> void
{
    MCFitIniFile windowFile;
    MainHolder()->SetTiled(false);
    TacticalMap()->ShowGuiWindow(true);
    MainHolder()->ZoomActivePane();
    TacticalMap()->HideMe(false);

    if (windowFile.Open("windows.fit") != 0)
    {
        return;
    }

    if (windowFile.SeekBlock("Info") == 0)
    {
        TacticalMap()->PaletteFrame->ShowGuiWindow(windowFile.Read<bool>("ShowPalette").value_or(true));
    }

    windowFile.Close();
}
