#include "stdafx.h"
#include "MCTest.h"
#include "fixtures/MCRegisteredData.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPreferencesMenu.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameSession.h"
#include "main/MCGameStrings.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogistics.h"
#include "main/MCSystemConfig.h"
#include "camera/MCCameraList.h"
#include "color/MCPalette.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCPresenter.h"
#include "platform/MCRenderer.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCSpriteManager.h"
#include "terrain/MCTerrainTiles.h"

namespace
{
    /// <summary>The data paths SYSTEM.CFG sets, as one tuple of references (to save and put back).</summary>
    auto SettingPaths()
    {
        return std::tie(SavePath, TerrainPath, PalettePath, ArtPath, FontPath, SoundPath, SpritePath, ShapesPath,
                        ObjectPath, MissionPath, WarriorPath, ProfilePath, CameraPath, TilePath, Tile90Path,
                        InterfacePath, MoviePath, MissionName, CDsoundPath, CDspritePath, CDmoviePath, SaveTempPath);
    }

    /// <summary>The values of <see cref="SettingPaths"/>.</summary>
    using MCSettingPathValues =
        decltype(std::apply([](const auto&... paths) { return std::make_tuple(paths...); }, SettingPaths()));

    /// <summary>The settings SYSTEM.CFG and PREFS.CFG write, put back when the test ends.</summary>
    class SavedSettings
    {
    public:
        SavedSettings() = default;

        ~SavedSettings()
        {
            SettingPaths() = _PathValues;
            std::tie(UseSound, UseMusic, DebugGameSystem, AblDebuggerEnabled, GNoSound) = _Switches;
            std::tie(Use90PixelSprite, Only45Pixel, Force16MB, Force32MB, GFullScreen, GStretchToFit, GSoftwareCursor,
                     GShowFps, GShowFpsPreference, GRenderer, GRendererPreference) = _Display;
            std::tie(DisplayWidth, DisplayHeight, LanguageOffset, GameDifficulty, MusicVolume, RadioVolume, SfxVolume,
                     InterfaceVolume) = _Numbers;
            GScrollSpeed = _ScrollSpeed;
        }

        SavedSettings(const SavedSettings&) = delete;
        SavedSettings& operator=(const SavedSettings&) = delete;

    private:
        MCSettingPathValues _PathValues{SettingPaths()};
        std::tuple<int32_t, int32_t, bool, bool, bool> _Switches{UseSound, UseMusic, DebugGameSystem,
                                                                 AblDebuggerEnabled, GNoSound};
        std::tuple<int, bool, bool, bool, int, int, int, int, int, int, int> _Display{
            Use90PixelSprite, Only45Pixel, Force16MB,          Force32MB, GFullScreen,        GStretchToFit,
            GSoftwareCursor,  GShowFps,    GShowFpsPreference, GRenderer, GRendererPreference};
        std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t> _Numbers{
            DisplayWidth, DisplayHeight, LanguageOffset, GameDifficulty,
            MusicVolume,  RadioVolume,   SfxVolume,      InterfaceVolume};
        int _ScrollSpeed{GScrollSpeed};
    };

    /// <summary>SYSTEM.CFG as the retail game ships it, with <paramref name="extraBlocks"/> before the ABL block.</summary>
    std::string SystemConfigText(std::string_view extraBlocks, std::string_view debuggerEnabled)
    {
        return std::format("FITini\r\n\r\n{}"
                           "[ABL]\r\n"
                           "ul SymbolTableHeapSize = 4095999\r\n"
                           "ul StackHeapSize = 4095999\r\n"
                           "ul CodeHeapSize = 4095999\r\n"
                           "ul RunTimeStackSize = 2048999\r\n"
                           "ul MaxCodeBlockSize = 204799\r\n"
                           "ul MaxRegisteredModules = 800\r\n"
                           "ul MaxStaticVariables = 4000\r\n"
                           "l MaxWatchesPerModule = 20\r\n"
                           "l MaxBreakPointsPermOdule = 20\r\n"
                           "ul DebuggerEnabled = {}\r\n"
                           "ul IncludeDebugInfo = 0\r\n\r\n"
                           "[systemPaths]\r\n"
                           "st terrainPath = \"t\\terrain\\\"\r\n"
                           "st palettePath = \"t\\palette\\\"\r\n"
                           "st artPath = \"t\\art\\\"\r\n"
                           "st fontPath = \"t\\fonts\\\"\r\n"
                           "st savePath = \"t\\savegame\\\"\r\n"
                           "st spritePath = \"t\\sprites\\\"\r\n"
                           "st CDspritePath = \"E:\\data\\sprites\\\"\r\n"
                           "st shapesPath = \"t\\shapes\\\"\r\n"
                           "st soundPath = \"t\\sound\\\"\r\n"
                           "st CDsoundPath = \"t\\cdsound\\\"\r\n"
                           "st objectPath = \"t\\objects\\\"\r\n"
                           "st cameraPath = \"t\\cameras\\\"\r\n"
                           "st tilePath = \"t\\tiles\\\"\r\n"
                           "st tile90Path = \"t\\tiles90\\\"\r\n"
                           "st missionPath = \"t\\missions\\\"\r\n"
                           "st missionName = \"MechCmdr1\"\r\n"
                           "st warriorPath = \"t\\missions\\warriors\\\"\r\n"
                           "st profilePath = \"t\\missions\\profiles\\\"\r\n"
                           "st interfacepath = \"t\\iface\\\"\r\n"
                           "st CDmoviepath = \"E:\\data\\movies\\\"\r\n"
                           "st moviepath = \"t\\movies\\\"\r\n\r\n"
                           "FITend\r\n",
                           extraBlocks, debuggerEnabled);
    }

    /// <summary>Reads <paramref name="text"/> as SYSTEM.CFG from a memory source.</summary>
    void ReadSystemText(std::string_view text)
    {
        MCTestContextScope scope;
        scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>()).AddFile("system.cfg", text);
        MCFitIniFile file;
        REQUIRE_EQ(file.Open("system.cfg"), 0);
        ReadSystemConfig(file);
    }

    /// <summary>Reads <paramref name="text"/> as PREFS.CFG from a memory source, with a GUI system for gamma.</summary>
    void ReadPrefsText(std::string_view text)
    {
        MCTestContextScope scope;
        scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>()).AddFile("prefs.cfg", text);
        scope.Context().SetGuiSystem(std::make_unique<MCGuiSystem>());
        MCFitIniFile file;
        REQUIRE_EQ(file.Open("prefs.cfg"), 0);
        ReadPreferences(file);
    }
}

/// <summary>
/// SYSTEM.CFG's empty UseSound and UseMusic blocks switch sound and music on (music only with sound), the
/// DebugGameSystem block the game system window, the ABL block the debugger, and every path comes from systemPaths
/// (names matched with case ignored), the temporary folder under the save path being this process's own.
/// </summary>
TEST_CASE("system config: SYSTEM.CFG gives the sound switches, the ABL debugger and the data paths")
{
    SavedSettings saved;
    GNoSound = false;
    DebugGameSystem = false;

    ReadSystemText(SystemConfigText("[UseSound]\r\n\r\n[UseMusic]\r\n\r\n", "0"));
    CHECK_EQ(UseSound, 1);
    CHECK_EQ(UseMusic, 1);
    CHECK(!DebugGameSystem);
    CHECK(!AblDebuggerEnabled);
    CHECK_EQ(SavePath, std::string("t\\savegame\\"));
    CHECK_EQ(SaveTempPath, std::format("t\\savegame\\temp\\{}\\", MCPort::ProcessId()));
    CHECK_EQ(TerrainPath, std::string("t\\terrain\\"));
    CHECK_EQ(PalettePath, std::string("t\\palette\\"));
    CHECK_EQ(ArtPath, std::string("t\\art\\"));
    CHECK_EQ(FontPath, std::string("t\\fonts\\"));
    CHECK_EQ(SoundPath, std::string("t\\sound\\"));
    CHECK_EQ(SpritePath, std::string("t\\sprites\\"));
    CHECK_EQ(ShapesPath, std::string("t\\shapes\\"));
    CHECK_EQ(ObjectPath, std::string("t\\objects\\"));
    CHECK_EQ(MissionPath, std::string("t\\missions\\"));
    CHECK_EQ(WarriorPath, std::string("t\\missions\\warriors\\"));
    CHECK_EQ(ProfilePath, std::string("t\\missions\\profiles\\"));
    CHECK_EQ(CameraPath, std::string("t\\cameras\\"));
    CHECK_EQ(TilePath, std::string("t\\tiles\\"));
    CHECK_EQ(Tile90Path, std::string("t\\tiles90\\"));
    CHECK_EQ(InterfacePath, std::string("t\\iface\\"));
    CHECK_EQ(MoviePath, std::string("t\\movies\\"));
    CHECK_EQ(MissionName, std::string("MechCmdr1"));
    CHECK_EQ(CDsoundPath, std::string("t\\cdsound\\"));
    CHECK_EQ(CDspritePath, std::string("E:\\data\\sprites\\"));
    CHECK_EQ(CDmoviePath, std::string("E:\\data\\movies\\"));

    // Music without sound stays off; no UseMusic block switches both off.
    ReadSystemText(SystemConfigText("[UseMusic]\r\n\r\n[DebugGameSystem]\r\n\r\n", "1"));
    CHECK_EQ(UseSound, 0);
    CHECK_EQ(UseMusic, 0);
    CHECK(DebugGameSystem);
    CHECK(AblDebuggerEnabled);
    ReadSystemText(SystemConfigText("[UseSound]\r\n\r\n", "0"));
    CHECK_EQ(UseSound, 0);
    CHECK_EQ(UseMusic, 0);

    // The port's switch for the tests silences both whatever the file says.
    GNoSound = true;
    ReadSystemText(SystemConfigText("[UseSound]\r\n\r\n[UseMusic]\r\n\r\n", "0"));
    CHECK_EQ(UseSound, 0);
    CHECK_EQ(UseMusic, 0);
}

/// <summary>
/// PREFS.CFG as the retail game ships it: Brightness lands in the gamma level (the original's quirk: it wins over
/// Gamma), the full-size sprites are always used, and what the file lacks takes the defaults (difficulty 1, music
/// and radio 0x40, effects 0x60, language 0, the original's scroll speed).
/// </summary>
TEST_CASE("system config: PREFS.CFG gives the display, difficulty and volumes, with defaults for what it lacks")
{
    SavedSettings saved;
    LanguageOffset = 5;
    GRenderer = 0;

    ReadPrefsText("FITini \r\n\r\n[MechCommander]\r\nb PaletteCycle=TRUE\r\nb DirectDraw=TRUE\r\nb Use90Pixel=FALSE\r\n"
                  "b Force45Pixel=TRUE\r\nb Force16Mb=TRUE\r\nb Force32Mb=TRUE\r\nl Gamma=3\r\nl Difficulty=2\r\n"
                  "l Brightness=1\r\nl MusicVolume=0\r\nl RadioVolume=27\r\nl SFXVolume=27\r\nl Resolution=2\r\n"
                  "st Renderer=\"software\"\r\nb ShowFps=TRUE\r\nFITend \r\n");
    // (The scope is gone, so the GUI system's settings can't be read here; the next case checks them.)
    CHECK_EQ(GFullScreen, 1);
    CHECK_EQ(Use90PixelSprite, 1);
    CHECK(!Only45Pixel);
    CHECK(Force16MB);
    CHECK(!Force32MB);
    CHECK_EQ(GameDifficulty, 2);
    CHECK_EQ(MusicVolume, 0);
    CHECK_EQ(RadioVolume, 27);
    CHECK_EQ(SfxVolume, 27);
    // No InterfaceVolume (a retail file): the interface plays at the effects volume.
    CHECK_EQ(InterfaceVolume, 27);
    CHECK_EQ(DisplayWidth, 1024);
    CHECK_EQ(DisplayHeight, 768);
    CHECK_EQ(GRenderer, static_cast<int>(MCRendererKind::Software));
    CHECK_EQ(GRendererPreference, GRenderer);
    CHECK_EQ(GShowFps, 1);
    CHECK_EQ(LanguageOffset, 0);

    ReadPrefsText("FITini\r\n[MechCommander]\r\nb Force32Mb=TRUE\r\nFITend\r\n");
    CHECK(!Force16MB);
    CHECK(Force32MB);
    CHECK_EQ(GFullScreen, 0);
    CHECK_EQ(GameDifficulty, 1);
    CHECK_EQ(MusicVolume, 0x40);
    CHECK_EQ(RadioVolume, 0x40);
    CHECK_EQ(SfxVolume, 0x60);
    CHECK_EQ(InterfaceVolume, 0x60);

    // The port's key, when there.
    ReadPrefsText("FITini\r\n[MechCommander]\r\nl SFXVolume=100\r\nl InterfaceVolume=12\r\nFITend\r\n");
    CHECK_EQ(SfxVolume, 100);
    CHECK_EQ(InterfaceVolume, 12);
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);

    // SCROLL SPEED: one of its choices, else AUTO (100 percent of the original's pace is AUTO's).
    ReadPrefsText("FITini\r\n[MechCommander]\r\nl ScrollSpeed=250\r\nFITend\r\n");
    CHECK_EQ(GScrollSpeed, 250);
    ReadPrefsText("FITini\r\n[MechCommander]\r\nl ScrollSpeed=100\r\nFITend\r\n");
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);
    ReadPrefsText("FITini\r\n[MechCommander]\r\nl ScrollSpeed=175\r\nFITend\r\n");
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);
}

/// <summary>OB-168, kept: Brightness and Gamma fill the same gamma level, Brightness last.</summary>
TEST_CASE("system config: Brightness overrides Gamma, and without it the gamma level is 0")
{
    SavedSettings saved;
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("a.cfg", std::string_view("FITini\r\n[MechCommander]\r\nb PaletteCycle=TRUE\r\nl Gamma=3\r\n"
                                            "l Brightness=1\r\nFITend\r\n"));
    files.AddFile("b.cfg", std::string_view("FITini\r\n[MechCommander]\r\nl Gamma=3\r\nFITend\r\n"));
    scope.Context().SetGuiSystem(std::make_unique<MCGuiSystem>());

    MCFitIniFile first;
    REQUIRE_EQ(first.Open("a.cfg"), 0);
    ReadPreferences(first);
    CHECK_EQ(GuiSystem().GammaLevel, 1);
    CHECK_EQ(GuiSystem().PaletteCycle, 1);

    MCFitIniFile second;
    REQUIRE_EQ(second.Open("b.cfg"), 0);
    ReadPreferences(second);
    CHECK_EQ(GuiSystem().GammaLevel, 0);
    CHECK_EQ(GuiSystem().PaletteCycle, 0);
}

/// <summary>
/// The game session made at the start holds the mission (with the campaign loaded), the sound system and the palette
/// callback, while SYSTEM.CFG's FastFiles are open; the GUI system's stop takes all of them down, with the tactical
/// interface and the cursor shapes' memory.
/// </summary>
TEST_CASE_ISOLATED("game: the game session holds the game's systems and the stop takes them all down")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(TacticalInterface() != nullptr);
    const MCAddressKey cursor = MCAddressKey::OfData(GuiSystem().CursorShapeData(0));
    REQUIRE(cursor);
    CHECK(MCIsRegistered(cursor));
    MCGuiSystem& gui = GuiSystem();
    REQUIRE(gui.Session != nullptr);
    CHECK(Mission() != nullptr);
    CHECK(SoundSystem() != nullptr);
    CHECK(!MCGameContext::Current().FastFiles().Files().empty());
    CHECK(MultiPlayer() == nullptr);
    CHECK(gui.Session->DebuggerWindow() == nullptr);
    const MCAddressKey color = MCAddressKey::Of(gui.Session->ColorCallback());
    const auto listed = [&]
    {
        return std::ranges::any_of(
            gui.Callbacks(), [&](const std::weak_ptr<MCGuiCallback>& callback)
            { return callback.lock() != nullptr && MCAddressKey::Of(*callback.lock()) == color; });
    };

    CHECK(listed());

    gui.Stop();
    CHECK(gui.Session == nullptr);
    CHECK(Mission() == nullptr);
    CHECK(GlobalLog() == nullptr);
    CHECK(SoundSystem() == nullptr);
    CHECK(MCGameContext::Current().FastFiles().Files().empty());
    CHECK(!listed());
    CHECK(TacticalInterface() == nullptr);
    CHECK(GuiSystem().CursorShapeData(0).empty());
    CHECK(!MCIsRegistered(cursor));
}

/// <summary>
/// KillTheGame unwinds to RunGame (which stops the GUI system and returns the code) instead of ending the process:
/// 0 for a quit the player asked for, the code given for an error.
/// </summary>
TEST_CASE("game session: KillTheGame unwinds with its exit code")
{
    const auto quitCode = [](auto kill) -> std::optional<int32_t>
    {
        try
        {
            kill();
        }
        catch (const MCGameQuit& quit)
        {
            return quit.ExitCode;
        }

        return std::nullopt;
    };

    CHECK(quitCode([] { KillTheGame(); }) == std::optional<int32_t>(0));
    CHECK(quitCode([] { KillTheGame(1); }) == std::optional<int32_t>(1));
}

namespace
{
    /// <summary>
    /// Deletes the save games written while it lives (a finished mission saves the campaign), which would otherwise
    /// pile up in the test user folder.
    /// </summary>
    class SaveGameCleanup
    {
    public:
        SaveGameCleanup() : _Folder(MCTestGame::UserFolder() / "data" / "savegame")
        {
            for (const std::filesystem::path& file : Files())
            {
                _Before.insert(file);
            }
        }

        ~SaveGameCleanup()
        {
            for (const std::filesystem::path& file : Files())
            {
                if (!_Before.contains(file))
                {
                    std::error_code error;
                    std::filesystem::remove(file, error);
                }
            }
        }

        SaveGameCleanup(const SaveGameCleanup&) = delete;
        SaveGameCleanup& operator=(const SaveGameCleanup&) = delete;

    private:
        /// <summary>The files directly in the save folder.</summary>
        std::vector<std::filesystem::path> Files() const
        {
            std::vector<std::filesystem::path> files;
            std::error_code error;

            for (const auto& entry : std::filesystem::directory_iterator(_Folder, error))
            {
                if (entry.is_regular_file())
                {
                    files.push_back(entry.path());
                }
            }

            return files;
        }

        std::filesystem::path _Folder;
        std::set<std::filesystem::path> _Before;
    };

    /// <summary>Runs the game for <paramref name="frames"/> frames of a fifteenth of a second.</summary>
    void RunFrames(int32_t frames)
    {
        for (int32_t frame = 0; frame < frames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>
    /// A new campaign's first mission, played with the force's first mech and ended with <paramref name="result"/>
    /// (above 3 a win) once the results screen is closed.
    /// </summary>
    bool PlayFirstMission(uint32_t result)
    {
        if (!MCTestGame::StartLogistics())
        {
            return false;
        }

        RunFrames(30);
        NewCampaign();
        RunFrames(30);
        {
            // Pinned only for the set-up: starting the scenario takes logistics down.
            const std::shared_ptr<MCLogistics> logistics = GlobalLog();
            REQUIRE_EQ(logistics->CurrentMission, 0);
            REQUIRE_EQ(Mission()->CurrentScenario, 0);
            std::shared_ptr<MCLogMech> mech;
            REQUIRE_EQ(logistics->ForceMechList->GetMechInfo(0, mech), 0);
            logistics->DeploySlots[0][0].Unit = 0;
            mech->Deployed = true;
        }

        Mission()->StartScenario(Mission()->Scenarios[0]);

        for (int32_t frame = 0; frame < 3000; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);

            if (Mission()->State == MCMissionState::Scenario && Scenario() != nullptr && !Scenario()->StartingUp)
            {
                break;
            }
        }

        REQUIRE(Scenario() != nullptr);
        REQUIRE(!Scenario()->StartingUp);
        ScenarioResult = result;
        Mission()->EndScenarioRequested = -1;

        for (int32_t frame = 0; frame < 300 && Mission()->State != MCMissionState::Results; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        REQUIRE(Mission()->State == MCMissionState::Results);
        Mission()->CloseResultsScreen();
        RunFrames(30);
        return true;
    }
}

/// <summary>A won campaign mission moves the campaign on to the next one: its briefing comes up.</summary>
TEST_CASE_ISOLATED("game: winning a campaign mission moves the campaign on to the next mission")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    SaveGameCleanup saves;
    REQUIRE(PlayFirstMission(5));
    CHECK(Scenario() == nullptr);
    CHECK(Mission()->State == MCMissionState::Logistics);
    REQUIRE(GlobalLog() != nullptr);
    CHECK_EQ(GlobalLog()->CurrentMission, 1);
    CHECK_EQ(Mission()->CurrentScenario, 1);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
}

/// <summary>A lost campaign mission is played again: the same mission's briefing comes up.</summary>
TEST_CASE_ISOLATED("game: losing a campaign mission replays it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    SaveGameCleanup saves;
    REQUIRE(PlayFirstMission(2));
    CHECK(Scenario() == nullptr);
    CHECK(Mission()->State == MCMissionState::Logistics);
    REQUIRE(GlobalLog() != nullptr);
    CHECK_EQ(GlobalLog()->CurrentMission, 0);
    CHECK_EQ(Mission()->CurrentScenario, 0);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
}
