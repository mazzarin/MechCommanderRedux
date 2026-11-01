#include "stdafx.h"
#include "mission/MCMission.h"
#include "ai/MCMoveGeometry.h"
#include "lib/MCFatal.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCInventoryBlock.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "main/MCGamePaths.h"
#include "logistics/MCConnectMenu.h"
#include "gui/MCGuiSmackerWindow.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "color/MCPalette.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiPort.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPurchaseDlg.h"
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
#include "network/MCMultiPlayer.h"
#include "main/MCGameContext.h"
#include "object/MCGameSystemReader.h"
#include "object/MCMasterComponent.h"
#include "object/MCObjectTypeManager.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "vfx/MCVfxFunctions.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCSmacker.h"
#include "platform/MCWin32Defs.h"
#include "logistics/MCBriefingScreen.h"

int32_t GlobalGameSegment = 0;
float MinPilotSkill = 0.0f;
float MaxPilotSkill = 0.0f;
uint32_t ScenarioResult = 0;
int SomethingOnFire = 0;

namespace
{
    /// <summary>
    /// An empty movie or scenario name list (the port's name): the mission heap's <c>malloc(0)</c> returned null.
    /// </summary>
    constexpr int32_t NoRamForMissionLists = static_cast<int32_t>(0xFACC0003);
    /// <summary>The logistics music.</summary>
    constexpr int32_t LogisticsTrack = 0x17;
    /// <summary>The briefing music.</summary>
    constexpr int32_t BriefingTrack = 0x16;

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
    }

    if (SoundSystem() != nullptr && UseSound != 0)
    {
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
    if (MasterComponentList.empty())
    {
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
            std::string text = LoadGameString(0x370, 0xfe);
            MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
            dialog.SetText(text.data());
            dialog.SetTwoButton(false);
            dialog.OkButton->Callback().SetExec(CancelToMPlayer);
            dialog.OkButton->SetUpPicture(const_cast<char*>("bh_okay.tga"));
            dialog.OkButton->SetDownPicture(const_cast<char*>("bg_okay.tga"));
            dialog.OkButton->Disabled = false;
            dialog.OkButton->Draw();
            dialog.Activate();
            GlobalLog()->SetCurrentScreen(GlobalLog()->MainScreen);
            GlobalLog()->LogisticsState = 1;
            GlobalLog()->ShowLogScreen(false, false);
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
                if (SoundSystem() != nullptr)
                {
                    SoundSystem()->StopDigitalMusic();
                }

                const std::string scenarioName = (MultiPlayer() == nullptr || GlobalLog() == nullptr)
                                                     ? Scenarios[static_cast<size_t>(CurrentScenario)]
                                                     : std::string(GlobalLog()->MpMissionName);
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
        FeatureScreen = MCMakeGui<MCGuiObject>();
        FeatureScreen->Init(0, 0, 640, 480, {});
        // The picture is copied onto the screen's own (MCX.EXE never freed it).
        MCGuiOwned<MCGuiPort> picturePort = MCMakeGui<MCGuiPort>();
        picturePort->Init(const_cast<char*>("features.tga"));
        picturePort->CopyTo(FeatureScreen->Port().Frame(), 0, 0, false);
        ScreenWindow()->AddChild(FeatureScreen);
        FeatureScreen->ShowGuiWindow(true);
        GuiSystem().ActivatePalette(palette, 0, 0x100);
    }

    if (FeatureScreenDone != 0)
    {
        FeatureScreen.reset();
        State = MCMissionState::Quit;
    }

    return true;
}

auto MCMission::StartScenario(std::string_view name) -> void
{
    // A copy: the name may live in the logistics phase this frees.
    std::string scenarioName(name);

    SoundSystem()->PlayBettySample(0x13);
    EndScenarioRequested = 0;
    ResultsScreen = MCMakeGui<MCMissionResultsScreen>();
    ResultsScreen->Init();

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

    if (const int32_t result = Scenario()->Load(scenarioName); result != 0)
    {
        Fatal(result, " Couldnt init scenario ");
    }

    TacticalInterface()->StartScenario();
    LoadWindowStatus();
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
}

auto MCMission::RequestScenario(std::string_view scenarioName) -> void
{
    _RequestedScenario = std::string(scenarioName);
}

auto MCMission::StopScenarioCallbacks() -> void
{
    RemoveCallback(_InterfaceUpdateCallback);
    TacticalInterface()->HideTags();
    RemoveCallback(ScenarioCallback);
}

auto MCMission::CloseResultsScreen() -> void
{
    if (ResultsScreen != nullptr)
    {
        // Destroyed while still the mission's, as in the original (it ends the scenario).
        ResultsScreen->Destroy();
        ResultsScreen.reset();
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

    if (ScenarioResult > 3)
    {
        if (Solo != 0)
        {
            // A won solo (quick-start) mission: back to the main screen.
            MCLogistics& logistics = StartLogistics();
            LastLogisticsMissionState = 0;
            State = MCMissionState::Logistics;
            logistics.CurrentScreen()->ShowGuiWindow(false);
            logistics.SetCurrentScreen(logistics.MainScreen);
            logistics.LogisticsState = 1;
            logistics.ShowLogScreen(true, true);
            Solo = false;
            return;
        }

        if (static_cast<uint32_t>(CurrentScenario) == LastScenario)
        {
            GameOver = true;
        }
    }

    if (ScenarioResult < 3 && Solo != 0)
    {
        // A lost solo mission: restart the campaign at its first briefing.
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
        return;
    }

    if (GlobalGameSegment == 0 && !GameOver)
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

        if (MultiPlayer()->InMission != 0)
        {
            if (IsMPlayerGame != 0)
            {
                KillTheGame();
            }

            MCGameContext::Current().SetMultiPlayer(nullptr);
        }
    }
    else
    {
        State = MCMissionState::Quit;
        WaitTimer = WaitTime;
        CurrentScenario = -1;
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
