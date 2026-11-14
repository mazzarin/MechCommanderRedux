#include "stdafx.h"
#include "MCTest.h"
#include "lib/MCPacketFile.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMechBriefBlock.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCArtModes.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "logistics/MCUnitLimits.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"
#include "gui/MCGuiGlobals.h"
#include "sprite/MCSceneLights.h"
#include "platform/MCRenderer.h"
#include "platform/MCInput.h"
#include "platform/MCDisplay.h"

// The menus, the briefing screen and the mission/logistics bridge (P3-log-3): the registry version, the starting
// fit's components, a pilot's profile, the temp folder's clean-up, the menus' screens and states, the drop slots'
// tonnage limit and the operation movie's start delay.

using namespace MCScreenInput;

TEST_CASE("logistics: the stored version matches the build on its first 13 characters and a dot")
{
    constexpr std::string_view build = "MCX v1.8 1234";
    REQUIRE_EQ(build.size(), size_t{13});
    CHECK(RegistryVersionMatches("MCX v1.8 1234.", build));
    // What follows the dot doesn't matter; the build string past 13 characters neither.
    CHECK(RegistryVersionMatches("MCX v1.8 1234.5", build));
    CHECK(RegistryVersionMatches("MCX v1.8 1234.", "MCX v1.8 1234 extra"));
    // No dot, another character there, another build.
    CHECK(!RegistryVersionMatches("MCX v1.8 1234", build));
    CHECK(!RegistryVersionMatches("MCX v1.8 1234-", build));
    CHECK(!RegistryVersionMatches("MCX v1.8 1235.", build));
    CHECK(!RegistryVersionMatches("", build));
    // A build string shorter than 13 characters never matches: the stored one has the dot where the build ended.
    CHECK(!RegistryVersionMatches("MCX v1.8 1.", "MCX v1.8 1"));
    CHECK(!RegistryVersionMatches("MCX v1.8 1", "MCX v1.8 1"));
}

TEST_CASE("logistics: a starting fit counts 50 components, each once, each with its comment")
{
    CHECK_EQ(FitComponents.size(), size_t{50});
    std::set<uint8_t> ids;

    for (const MCFitComponent& component : FitComponents)
    {
        MCTest::Scope scope(std::string(component.Comment));
        CHECK(component.Comment.starts_with("// "));
        CHECK(ids.insert(component.Id).second);
    }

    // The first and the last of the file format's blocks.
    CHECK_EQ(FitComponents.front().Id, uint8_t{0x90});
    CHECK_EQ(FitComponents.back().Id, uint8_t{0xa1});
}

namespace
{
    /// <summary>A test context whose files are in memory (writes land in its scratch folder).</summary>
    struct MemoryFiles
    {
        MemoryFiles() : Files(Scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>())) {}

        MCTestContextScope Scope;
        MCMemoryFileSource& Files;
    };

    /// <summary>Entry <paramref name="name"/> of block <paramref name="block"/> of <paramref name="file"/>.</summary>
    template <MCFitValue T> T Entry(MCFitIniFile& file, std::string_view block, std::string_view name)
    {
        REQUIRE_EQ(file.SeekBlock(block), 0);
        const MCFitResult<T> value = file.Read<T>(name);
        REQUIRE(value.has_value());
        return *value;
    }
}

TEST_CASE("logistics: a pilot's profile from logistics holds its traits, skills and wounds")
{
    MemoryFiles memory;
    std::string name = "Jane Doe";
    std::string callsign = "Ghost";
    std::string picture = "pilot05.tga";
    std::string video = "video5";
    std::string audio = "audio5";
    std::string brain = "pbrain";
    MCLogWarrior warrior;
    warrior.Name = name;
    warrior.Callsign = callsign;
    warrior.Picture = picture;
    warrior.PilotVideo = video;
    warrior.PilotAudio = audio;
    warrior.Brain = brain;
    warrior.PaintScheme = 3;
    warrior.NameIndex = 7;
    warrior.DescIndex = 9;
    warrior.Assigned = true;
    warrior.Personality = {0x0a, 0x14, 0x1e, 0x28};
    warrior.Skills = {0x32, 0x33, 0x34, 0x35};
    warrior.OriginalSkills = {0x28, 0x29, 0x2a, 0x2b};
    warrior.StartingSkills = {0x2d, 0x2e, 0x2f, 0x30};
    warrior.SkillPoints[2] = 1.5f;
    warrior.Wounds = 2.0f;
    // A longer name was there: the profile's name replaces it.
    warrior.FileName = "oldprofile1";

    REQUIRE_EQ(MCMissionLogisticsBridge::LogisticsWarriorProfileWriter("tpak3", warrior), 0);
    CHECK_EQ(warrior.FileName, std::string("tpak3"));

    MCFitIniFile file;
    REQUIRE_EQ(file.Open(GamePath(SaveTempPath, "tpak3", ".fit")), 0);
    CHECK_EQ(Entry<std::string>(file, "General", "Name"), name);
    CHECK_EQ(Entry<std::string>(file, "General", "Callsign"), callsign);
    CHECK_EQ(Entry<std::string>(file, "General", "Picture"), picture);
    CHECK_EQ(Entry<std::string>(file, "General", "Brain"), brain);
    CHECK_EQ(Entry<int32_t>(file, "General", "paintScheme"), 3);
    CHECK_EQ(Entry<int32_t>(file, "General", "NameIndex"), 7);
    CHECK_EQ(Entry<int32_t>(file, "General", "DescIndex"), 9);
    CHECK(Entry<bool>(file, "General", "Assigned"));
    CHECK(!Entry<bool>(file, "General", "NotMineYet"));
    CHECK_EQ(Entry<char>(file, "PersonalityTraits", "Professionalism"), char{0x0a});
    CHECK_EQ(Entry<char>(file, "PersonalityTraits", "Courage"), char{0x28});
    CHECK_EQ(Entry<char>(file, "Skills", "Piloting"), char{0x32});
    CHECK_EQ(Entry<char>(file, "Skills", "Gunnery"), char{0x35});
    CHECK_EQ(Entry<char>(file, "OriginalSkills", "Jumping"), char{0x29});
    // "LatestSkills" are the skills the pilot started the mission with.
    CHECK_EQ(Entry<char>(file, "LatestSkills", "Sensors"), char{0x2f});
    CHECK_EQ(Entry<float>(file, "SkillPoints", "Sensors"), 1.5f);
    CHECK_EQ(Entry<char>(file, "Status", "Wounds"), char{2});
}

TEST_CASE("logistics: the temp folder's FIT files are deleted, nothing else")
{
    MemoryFiles memory;
    const std::string folder = SaveTempPath;
    memory.Files.AddUserFile(folder + "tpak0.fit", std::vector<uint8_t>{'a'});
    memory.Files.AddUserFile(folder + "bridge.fit", std::vector<uint8_t>{'b'});
    memory.Files.AddUserFile(folder + "keep.pak", std::vector<uint8_t>{'c'});
    memory.Files.AddUserFile("data\\save\\other.fit", std::vector<uint8_t>{'d'});
    DestroyAllFitFiles(folder);
    CHECK(!MCFileSystem::Exists(folder + "tpak0.fit"));
    CHECK(!MCFileSystem::Exists(folder + "bridge.fit"));
    CHECK(MCFileSystem::Exists(folder + "keep.pak"));
    CHECK(MCFileSystem::Exists("data\\save\\other.fit"));
}

namespace
{
    /// <summary>Runs the logistics screens for two seconds: a screen change or a dialog settles.</summary>
    void Settle()
    {
        for (int32_t frame = 0; frame < 30; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>Runs the game for <paramref name="seconds"/> in frames of a fifteenth of a second.</summary>
    void RunFor(float seconds)
    {
        for (float run = 0.0f; run < seconds; run += 1.0f / 15.0f)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }
}

TEST_CASE_ISOLATED("game: the main menu opens its screens in their states, and cancel comes back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE(logistics->CurrentScreen() == logistics->MainScreen);

    // The load screen: campaign saves, then single missions.
    LoadScreen();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->LoadScreen);
    CHECK_EQ(logistics->LogisticsState, 5);
    CHECK(!LoadingSolo);
    CHECK(logistics->LoadScreen->FilePane->SelectedFile == -1);
    Cancel();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    CHECK(WhackTimer);
    SoloLoadScreen();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->LoadScreen);
    CHECK(LoadingSolo);
    Cancel();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    CHECK(!LoadingSolo);

    // No campaign yet: nothing to save.
    if (logistics->CurrentMission == -1)
    {
        SaveScreen();
        Settle();
        CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    }

    // The preferences: cancel brings back the settings they opened with.
    const int32_t difficulty = GameDifficulty;
    const uint8_t music = SoundSystem()->MusicLevel;
    ShowPreferences();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->PrefScreen);
    CHECK_EQ(logistics->LogisticsState, 9);
    GameDifficulty = difficulty == 2 ? 0 : 2;
    SoundSystem()->MusicLevel = static_cast<uint8_t>(music == 10 ? 20 : 10);
    HardToggle();
    CHECK_EQ(GameDifficulty, 2);
    EasyToggle();
    CHECK_EQ(GameDifficulty, 0);
    CancelPrefs();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    CHECK_EQ(GameDifficulty, difficulty);
    CHECK_EQ(SoundSystem()->MusicLevel, music);

    // The multiplayer menu.
    ShowMultiPlayer();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MultiplayerScreen);
    CHECK_EQ(logistics->LogisticsState, 10);
    CHECK(!Solo);
    Cancel();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);

    // A new campaign opens on the briefing screen, and the menu's return goes back there.
    NewCampaign();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->BriefingScreen);
    CHECK(logistics->CurrentMission != -1);
}

/// <summary>
/// The main menu offers LOAD MISSION while there are saved games, and the load screen lists each by name, sorted (two
/// here, unreadable: listed without an operation, mission or points). The recorded logistics frames run with none.
/// </summary>
TEST_CASE_ISOLATED("game: the main menu offers LOAD MISSION while there are saves, and the load screen lists them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    static constexpr std::array<std::string_view, 2> Saves = {"Bravo", "Alpha"};
    std::optional<MCTestGame::PinnedSaves> saves;
    REQUIRE(MCTestGame::StartLogistics([&saves] { saves.emplace(Saves); }));
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    CHECK(!logistics->MainScreen->Element<MCLogButton>(MCTestGame::MainMenuLoadButton)->Disabled);

    {
        // The pinned saves are empty files: reading each as the screen opens reports it, and lists it by name.
        MCTest::ExpectAssert emptySaves("could not find packet 0 in save game file",
                                        static_cast<int32_t>(Saves.size()));
        LoadScreen();
        Settle();
    }

    REQUIRE(logistics->CurrentScreen() == logistics->LoadScreen);
    const std::vector<MCFileScrollPane::File>& files = logistics->LoadScreen->FilePane->Files;
    REQUIRE_EQ(files.size(), Saves.size());
    CHECK_EQ(files[0].Name, std::string("Alpha"));
    CHECK_EQ(files[1].Name, std::string("Bravo"));
    Cancel();
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
}

namespace
{
    /// <summary>The middle of drop slot <paramref name="slot"/>.</summary>
    POINT SlotMiddle(size_t slot)
    {
        const RECT& area = GlobalLog()->BriefingScreen->SlotRects[slot];
        return {(area.left + area.right) / 2, (area.top + area.bottom) / 2};
    }

    /// <summary>Drags <paramref name="mech"/>'s block (wherever it is) to (<paramref name="x"/>, <paramref name="y"/>).</summary>
    void DragMech(MCLogMech& mech, int32_t x, int32_t y)
    {
        const std::shared_ptr<MCMechBriefBlock> block = GlobalLog()->BriefingScreen->BlockOf(mech);
        REQUIRE(block != nullptr);
        const POINT from{block->GlobalX() + block->Width() / 2, block->GlobalY() + block->Height() / 2};
        Drag(from.x, from.y, x, y);
        Settle();
    }

    /// <summary>The first unit the deploy pane lists, as a mech (null when it is a vehicle or the pane is empty).</summary>
    std::shared_ptr<MCLogMech> FirstDeployMech(const MCBriefingScreen& briefing)
    {
        if (briefing.DeployUnits.empty() || briefing.DeployUnits.front()->PartType != 1)
        {
            return nullptr;
        }

        return std::static_pointer_cast<MCLogMech>(briefing.DeployUnits.front());
    }

    /// <summary>The briefing screen's launch button, its middle.</summary>
    constexpr POINT LaunchButton{0x230, 0x152};

    /// <summary>On the briefing screen, the first mech the deploy pane lists dragged into the first drop slot.</summary>
    void DeployFirstMech()
    {
        // Pinned only for the set-up: the launch takes logistics down.
        const std::shared_ptr<MCLogistics> pinned = GlobalLog();
        REQUIRE(pinned->CurrentScreen() == pinned->BriefingScreen);
        std::shared_ptr<MCLogMech> mech = FirstDeployMech(*pinned->BriefingScreen);
        REQUIRE(mech != nullptr);
        MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(mech->CurTonnage);
        DragMech(*mech, SlotMiddle(0).x, SlotMiddle(0).y);
        REQUIRE(mech->Deployed != 0);
    }

    /// <summary>The launch button, then frames until the scenario is up.</summary>
    /// <returns>Whether the scenario is up.</returns>
    bool Launch()
    {
        constexpr int32_t mostFrames = 300;
        RealClick(LaunchButton.x, LaunchButton.y);

        for (int32_t frame = 0; frame < mostFrames && Scenario() == nullptr; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        return Scenario() != nullptr;
    }

    /// <summary>
    /// From the main menu, a new campaign's first mission: its briefing, the first mech in the first drop slot, the
    /// launch button, then frames until the scenario is up.
    /// </summary>
    /// <returns>Whether the scenario is up.</returns>
    bool LaunchFirstMission()
    {
        NewCampaign();
        Settle();
        DeployFirstMech();
        return Launch();
    }
}

TEST_CASE_ISOLATED("game: units dragged into the drop slots count against the drop tonnage")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    MCBriefingScreen& briefing = *logistics->BriefingScreen;
    REQUIRE(logistics->CurrentScreen().get() == &briefing);
    REQUIRE(logistics->LocalDropSlot[0]);
    REQUIRE(!logistics->HammerDown);
    MCListView& pane = *briefing.DeployPane;
    REQUIRE(pane.NumberOfChildren() > 0);
    std::shared_ptr<MCLogMech> mech = FirstDeployMech(briefing);
    REQUIRE(mech != nullptr);
    REQUIRE(pane.RowOf<MCMechBriefBlock>(static_cast<const MCLogPart&>(*mech)) != nullptr);
    REQUIRE(mech->Deployed == 0);
    const auto tons = static_cast<int32_t>(mech->CurTonnage);
    const int32_t startTons = CurDeployTonnage;
    const auto& slot0 = logistics->DeploySlots[0][0];
    REQUIRE_EQ(slot0.Unit, -1);
    const POINT middle = SlotMiddle(0);
    const POINT paneMiddle{pane.GlobalX() + pane.Width() / 2, pane.GlobalY() + pane.Height() / 2};

    // Into the first slot: the lance's tonnage grows by the mech's, the block shows in the slot.
    MaxDeployTonnage = startTons + tons;
    DragMech(*mech, middle.x, middle.y);
    CHECK(mech->Deployed != 0);
    CHECK_EQ(slot0.Unit, logistics->ForceMechList->GetMechIndex(mech));
    CHECK_EQ(CurDeployTonnage, startTons + tons);
    const std::shared_ptr<MCBriefingBox> box = briefing.BriefingBox.Lock();
    REQUIRE(box != nullptr);
    CHECK(box->Mech() == mech);
    CHECK(box->ParentObject().get() == &briefing);
    const std::shared_ptr<MCMechBriefBlock> slotBlock = briefing.SlotBlocks[0].Lock();
    REQUIRE(slotBlock != nullptr);
    CHECK(slotBlock->Mech() == mech);
    CHECK(slotBlock->ParentObject().get() == &briefing);
    CHECK(pane.RowOf<MCMechBriefBlock>(static_cast<const MCLogPart&>(*mech)) == nullptr);
    CHECK(std::ranges::find(briefing.UndeployedMechs, logistics->ForceMechList->GetMechIndex(mech)) ==
          briefing.UndeployedMechs.end());

    // Back to the deploy pane: the slot is empty again.
    DragMech(*mech, paneMiddle.x, paneMiddle.y);
    CHECK(mech->Deployed == 0);
    CHECK_EQ(slot0.Unit, -1);
    CHECK_EQ(CurDeployTonnage, startTons);
    CHECK(briefing.SlotBlocks[0] == nullptr);
    CHECK(briefing.BlockOf(*mech) == pane.RowOf<MCMechBriefBlock>(static_cast<const MCLogPart&>(*mech)));
    CHECK(briefing.BlockOf(*mech) != nullptr);

    // A ton over the limit: refused with the message, the mech stays in the pane.
    MaxDeployTonnage = startTons + tons - 1;
    DragMech(*mech, middle.x, middle.y);
    CHECK(mech->Deployed == 0);
    CHECK_EQ(slot0.Unit, -1);
    CHECK_EQ(CurDeployTonnage, startTons);
    CHECK(logistics->MessageDialog->ShowWindow != 0);
    CHECK(briefing.SlotBlocks[0] == nullptr);
    CHECK(pane.RowOf<MCMechBriefBlock>(static_cast<const MCLogPart&>(*mech)) != nullptr);
}

/// <summary>
/// The launch button starts the mission once its click is handled: logistics and its screens are gone, and the
/// loading frames never draw the briefing screen whose click asked for it.
/// </summary>
TEST_CASE_ISOLATED("game: the launch button leaves logistics and starts the mission")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    std::weak_ptr<MCLogistics> logistics = GlobalLog();
    std::weak_ptr<MCBriefingScreen> briefing = GlobalLog()->BriefingScreen;
    REQUIRE(LaunchFirstMission());
    CHECK(Mission()->State == MCMissionState::Scenario);
    CHECK(GlobalLog() == nullptr);
    CHECK(logistics.expired());
    CHECK(briefing.expired());
}

/// <summary>
/// An art mode the debug console showed is the run's only: with the player's own HD LIGHTING at NIGHT, the mode
/// "original" turns it off; PREFS saved while it shows (as the in-mission INTERFACE SIZE keys save it) keeps NIGHT and
/// the mode stays shown; opening the preferences puts NIGHT back, and accepting them saves NIGHT, not the mode's.
/// </summary>
TEST_CASE_ISOLATED("game: an art mode the debug console showed ends at the preferences, which save the player's own")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    const auto savedLighting = [&]
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(prefsPath)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        return prefs.Read<int32_t>("HDLighting");
    };

    std::filesystem::remove(prefsPath);
    GHdLighting = HdLightingNight;
    const std::optional<MCArtMode> original = ArtModeNamed("original");
    REQUIRE(original.has_value());
    REQUIRE(ShowArtMode(*original).has_value());
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));

    SavePrefs();
    CHECK_EQ(savedLighting(), MCFitResult<int32_t>(HdLightingNight));
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));

    ShowPreferences();
    Settle();
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    WritePrefs();
    Settle();
    CHECK_EQ(savedLighting(), MCFitResult<int32_t>(HdLightingNight));

    // No mode shown now: the preferences change nothing.
    ShowPreferences();
    Settle();
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    CancelPrefs();
    GHdLighting = HdLightingOff;
    std::filesystem::remove(prefsPath);
}

/// <summary>
/// A setting changed on the preferences' HD OPTIONS page reaches the mission launched next: HD LIGHTING clicked from OFF
/// to NIGHT there and accepted is written to PREFS, and the new campaign's first mission shows at night, its world
/// much darker than with HD LIGHTING off.
/// </summary>
TEST_CASE_ISOLATED("game: HD LIGHTING set to NIGHT on the HD OPTIONS page darkens the mission launched next")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    REQUIRE_EQ(GHdLighting, static_cast<int>(HdLightingOff));

    // The preferences, OPTIONS, three clicks on HD LIGHTING (ON, DUSK, NIGHT), DONE, ACCEPT.
    ShowPreferences();
    Settle();
    const std::shared_ptr<MCLogTextButton> options = FindTextButton(*GlobalLog()->PrefScreen, "OPTIONS");
    REQUIRE(options != nullptr);
    RealClick(options->GlobalX() + options->Width() / 2, options->GlobalY() + options->Height() / 2);
    Settle();
    REQUIRE(HdOptionsShown());
    const std::shared_ptr<MCLogOptionList> list = HdOptionsList();
    REQUIRE(list != nullptr);
    const std::optional<int32_t> row = list->RowOf("HDLighting");
    REQUIRE(row.has_value());
    const std::optional<std::pair<int32_t, int32_t>> setting = list->SettingCentre(*row);
    REQUIRE(setting.has_value());
    constexpr int32_t clicksToNight = 3;

    for (int32_t click = 0; click < clicksToNight; click++)
    {
        RealClick(setting->first, setting->second);
    }

    CHECK_EQ(list->ChoiceName(*row), "NIGHT");
    HideHdOptions();
    WritePrefs();
    Settle();
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(prefsPath)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefs.Read<int32_t>("HDLighting"), MCFitResult<int32_t>(HdLightingNight));
    }

    REQUIRE(LaunchFirstMission());
    CHECK(MCSceneLights::TimeOfDay() == MCTimeOfDay::Night);

    // Past the scenario's start-up (its static) until units move, as MCTestGame::StartMission waits.
    constexpr int32_t startedTurn = 30;
    constexpr int32_t mostStartFrames = 3000;

    for (int32_t frame = 0; frame < mostStartFrames && Turn <= startedTurn; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    REQUIRE(Turn > startedTurn);

    // Paused, the world (clear of the interface: the panels at the left, the mech bar at the bottom) at night, then
    // with HD LIGHTING off.
    GamePaused = true;
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const auto worldLuma = [&]
    {
        constexpr int32_t settleFrames = 2;

        for (int32_t frame = 0; frame < settleFrames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        const std::expected<std::vector<SDL_Color>, std::string> frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE_EQ(frame->size(), static_cast<size_t>(width) * height);
        double sum = 0.0;
        size_t count = 0;

        for (int32_t y = height / 4; y < height - height / 8; y++)
        {
            for (int32_t x = width / 3; x < width; x++)
            {
                const SDL_Color color = (*frame)[static_cast<size_t>(y) * width + x];
                sum += 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
                count++;
            }
        }

        return sum / static_cast<double>(std::max<size_t>(count, 1));
    };

    const double night = worldLuma();
    MCScreenInput::SaveShownShot("hd options night mission");
    GHdLighting = HdLightingOff;
    const double off = worldLuma();
    MCScreenInput::SaveShownShot("hd options night mission lighting off");
    std::cout << std::format("  world luma: night {:.1f}, HD lighting off {:.1f}\n", night, off);
    // Night's world light is a third of the day's (MCSceneLights' night look).
    constexpr double atMostOfOff = 0.6;
    CHECK(night < off * atMostOfOff);

    GamePaused = false;
    std::filesystem::remove(prefsPath);
}

TEST_CASE_ISOLATED("game: the operation tab starts the briefing movie half a second later")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    MCBriefingScreen& briefing = *GlobalLog()->BriefingScreen;
    REQUIRE(GlobalLog()->CurrentScreen().get() == &briefing);
    briefing.SetUpMission();
    CHECK_EQ(briefing.CurrentTab, MCBriefingScreen::MissionTab);
    CHECK(briefing.SmackerWindow == nullptr);

    // The tab: the operation art at once, the movie after its timer (500 ms).
    briefing.SetUpOperation();
    CHECK_EQ(briefing.CurrentTab, MCBriefingScreen::OperationTab);
    CHECK(briefing.OperationShown);
    CHECK(!briefing.PlayMovie);
    RunFor(0.3f);
    CHECK(!briefing.PlayMovie);
    CHECK(briefing.SmackerWindow == nullptr);
    RunFor(0.4f);
    CHECK(briefing.PlayMovie);

    if (!GlobalLog()->OperationCinema.empty())
    {
        CHECK(briefing.SmackerWindow != nullptr);

        // The mission tab stops it.
        briefing.SetUpMission();
        CHECK(briefing.SmackerWindow == nullptr);
        CHECK(!briefing.PlayMovie);
    }
}

/// <summary>
/// OB-191: the operation tab says "Click to restart video." once it shows, and a click on it does: a second click
/// before the movie starts leaves one start, and a click while the movie plays (as one during Betty's "incoming
/// transmission" lands, once the movie has opened) starts it again after the delay; the mission tab clicked within the
/// delay drops the start. The original armed a start for each click, the later one stopping the movie the first had
/// started, and a click once the movie had started only stopped it: the operation picture stayed, without its movie;
/// and the mission tab left the start waiting, which flipped back to the operation tab.
/// </summary>
TEST_CASE_ISOLATED("game: a click on the operation tab restarts the briefing movie, however soon it comes")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    MCBriefingScreen& briefing = *GlobalLog()->BriefingScreen;
    REQUIRE(GlobalLog()->CurrentScreen().get() == &briefing);
    REQUIRE(!GlobalLog()->OperationCinema.empty());
    briefing.SetUpMission();
    REQUIRE(briefing.SmackerWindow == nullptr);

    // The operation tab (MCBriefingScreen::HandleClick's), the start's delay (500 ms) and a time well into the movie.
    constexpr int32_t TabX = 0xc8;
    constexpr int32_t TabY = 0x80;
    constexpr float SecondClickAfter = 0.2f;
    constexpr float PastDelay = 0.7f;
    constexpr float IntoMovie = 1.5f;
    const auto frameShown = [&briefing]() -> std::optional<uint32_t>
    {
        const std::shared_ptr<MCGuiSmackerWindow> window = briefing.SmackerWindow.Lock();

        if (window == nullptr || window->Movie == nullptr)
        {
            return std::nullopt;
        }

        return window->Movie->Player->FrameNum();
    };

    // Two clicks before the movie starts: it starts once, and plays on.
    MCScreenInput::Click(TabX, TabY);
    RunFor(SecondClickAfter);
    MCScreenInput::Click(TabX, TabY);
    CHECK_EQ(briefing.CurrentTab, MCBriefingScreen::OperationTab);
    RunFor(PastDelay);
    const std::optional<uint32_t> started = frameShown();
    REQUIRE(started.has_value());
    RunFor(IntoMovie);
    const std::optional<uint32_t> playing = frameShown();
    REQUIRE(playing.has_value());
    CHECK(*playing > *started);

    // A click while it plays: it stops, and starts again from its first frame after the delay.
    MCScreenInput::Click(TabX, TabY);
    CHECK(!frameShown().has_value());
    RunFor(PastDelay);
    const std::optional<uint32_t> restarted = frameShown();
    REQUIRE(restarted.has_value());
    CHECK(*restarted < *playing);
    RunFor(IntoMovie);
    const std::optional<uint32_t> playingAgain = frameShown();
    REQUIRE(playingAgain.has_value());
    CHECK(*playingAgain > *restarted);
    CHECK(briefing.OperationPictureShown);

    // The mission tab clicked within the delay: the start waiting is dropped, and the mission tab stays.
    constexpr int32_t MissionTabY = 0x110;
    MCScreenInput::Click(TabX, MissionTabY);
    CHECK(!frameShown().has_value());
    MCScreenInput::Click(TabX, TabY);
    RunFor(SecondClickAfter);
    MCScreenInput::Click(TabX, MissionTabY);
    RunFor(PastDelay);
    CHECK_EQ(briefing.CurrentTab, MCBriefingScreen::MissionTab);
    CHECK(!frameShown().has_value());
}

namespace
{
    /// <summary>What a campaign save holds that a load brings back: the units, pilots, spare parts and points.</summary>
    struct CampaignState
    {
        std::vector<std::string> Mechs;
        std::vector<std::string> ForceMechs;
        std::vector<int32_t> ForcePilots;
        std::vector<std::string> Pilots;
        std::vector<std::string> AssignedPilots;
        std::vector<std::string> Vehicles;
        std::vector<std::pair<int32_t, int32_t>> Components;
        int32_t Points = 0;
        int32_t Mission = 0;

        bool operator==(const CampaignState&) const = default;
    };

    /// <summary>The current campaign's <see cref="CampaignState"/>.</summary>
    CampaignState TakeState(const MCLogistics& logistics)
    {
        CampaignState state;

        for (const std::shared_ptr<MCLogMech>& mech : logistics.MechList->Mechs)
        {
            state.Mechs.push_back(mech->FileName);
        }

        for (const std::shared_ptr<MCLogMech>& mech : logistics.ForceMechList->Mechs)
        {
            state.ForceMechs.push_back(mech->FileName);
            state.ForcePilots.push_back(mech->PilotIndex);
        }

        for (const std::shared_ptr<MCLogWarrior>& warrior : logistics.WarriorList->Warriors)
        {
            state.Pilots.push_back(warrior->Callsign);
        }

        for (const std::shared_ptr<MCLogWarrior>& warrior : logistics.AssignedWarriorList->Warriors)
        {
            state.AssignedPilots.push_back(warrior->Callsign);
        }

        for (const MCLogVehicleList& list : {std::cref(*logistics.VehicleList), std::cref(*logistics.ForceVehicleList)})
        {
            for (const std::shared_ptr<MCLogVehicle>& vehicle : list.Vehicles)
            {
                state.Vehicles.push_back(vehicle->FileName);
            }
        }

        for (const std::shared_ptr<MCLogInventoryItem>& item : logistics.ComponentInventory->Items)
        {
            state.Components.emplace_back(item->MasterID, item->Count);
        }

        state.Points = ResourcePoints;
        state.Mission = logistics.CurrentMission;
        return state;
    }
}

TEST_CASE_ISOLATED("game: a saved campaign loads back with its units, pilots, spare parts and points")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE(!logistics->ForceMechList->Mechs.empty());
    REQUIRE(!logistics->ComponentInventory->Items.empty());
    // A spare part and some points of the player's own, so the save holds more than the campaign's start.
    logistics->ComponentInventory->Items.front()->Count += 3;
    ResourcePoints += 1234;
    const CampaignState saved = TakeState(*logistics);
    REQUIRE_EQ(logistics->SaveCampaign("p3roundtrip"), 0);

    // Spent and sold since: the load puts it all back (a reload of the force: no time passes, nobody heals).
    ResourcePoints = 1;
    logistics->MechList->Clear();
    REQUIRE_EQ(logistics->LoadCampaign("p3roundtrip", ".sav", false, true), 0);
    const CampaignState loaded = TakeState(*logistics);
    CHECK(loaded.Mechs == saved.Mechs);
    CHECK(loaded.ForceMechs == saved.ForceMechs);
    CHECK(loaded.ForcePilots == saved.ForcePilots);
    CHECK(loaded.Pilots == saved.Pilots);
    CHECK(loaded.AssignedPilots == saved.AssignedPilots);
    CHECK(loaded.Vehicles == saved.Vehicles);
    CHECK(loaded.Components == saved.Components);
    CHECK_EQ(loaded.Points, saved.Points);
    CHECK_EQ(loaded.Mission, saved.Mission);
    MCFileSystem::RemoveFile(GamePath(SavePath, "p3roundtrip", ".sav"));
    MCFileSystem::RemoveFile(GamePath(SavePath, "p3roundtrip", ".pur"));
}

TEST_CASE_ISOLATED("game: a save naming a mission the campaign doesn't have is refused")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const int32_t goodMission = logistics->CurrentMission;
    const auto scenarios = static_cast<int32_t>(Mission()->Scenarios.size());

    // One past the last scenario is the campaign's end and loads; further on, or below 0, picks no scenario.
    for (const int32_t mission : {scenarios + 1, scenarios + 1000, -1, std::numeric_limits<int32_t>::min()})
    {
        MCTest::Scope entry(std::format("mission {}", mission));
        logistics->CurrentMission = mission;
        REQUIRE_EQ(logistics->SaveCampaign("p3badmission"), 0);
        CHECK(logistics->LoadCampaign("p3badmission", ".sav", false, true) != 0);
    }

    logistics->CurrentMission = goodMission;
    REQUIRE_EQ(logistics->SaveCampaign("p3badmission"), 0);
    CHECK_EQ(logistics->LoadCampaign("p3badmission", ".sav", false, true), 0);
    CHECK_EQ(logistics->CurrentMission, goodMission);
    MCFileSystem::RemoveFile(GamePath(SavePath, "p3badmission", ".sav"));
    MCFileSystem::RemoveFile(GamePath(SavePath, "p3badmission", ".pur"));
}

namespace
{
    /// <summary>Replaces the one <paramref name="from"/> in <paramref name="text"/> with <paramref name="to"/>.</summary>
    void ReplaceOnce(std::string& text, std::string_view from, std::string_view to)
    {
        const size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        REQUIRE(text.find(from, at + 1) == std::string::npos);
        text.replace(at, from.size(), to);
    }

    /// <summary>A save's packets as stored, each with its storage.</summary>
    struct SavePackets
    {
        std::vector<std::vector<uint8_t>> Packets;
        std::vector<MCPacketStorage> Storages;
    };

    /// <summary>Save <paramref name="name"/>'s packets.</summary>
    SavePackets ReadSave(std::string_view name)
    {
        SavePackets save;
        MCPacketFile in;
        REQUIRE_EQ(in.Open(GamePath(SavePath, name, ".sav")), 0);

        for (int32_t packet = 0; packet < in.GetNumPackets(); ++packet)
        {
            REQUIRE_EQ(in.SeekPacket(packet), 0);
            std::vector<uint8_t> bytes(static_cast<size_t>(in.GetPacketSize()));
            REQUIRE_EQ(in.ReadPacket(packet, bytes), static_cast<int32_t>(bytes.size()));
            save.Packets.push_back(std::move(bytes));
            save.Storages.push_back(in.GetStorageType());
        }

        REQUIRE(!save.Packets.empty());
        return save;
    }

    /// <summary>
    /// Writes save <paramref name="name"/> as <paramref name="save"/>, its settings (its first packet, a FIT file's
    /// text) changed by <paramref name="change"/>.
    /// </summary>
    void WriteSave(std::string_view name, SavePackets save, const std::function<void(std::string&)>& change)
    {
        std::string settings(save.Packets.front().begin(), save.Packets.front().end());
        change(settings);
        save.Packets.front().assign(settings.begin(), settings.end());
        MCPacketFile out;
        REQUIRE_EQ(out.Create(GamePath(SavePath, name, ".sav")), 0);
        out.Reserve(static_cast<int32_t>(save.Packets.size()));

        for (size_t packet = 0; packet < save.Packets.size(); ++packet)
        {
            out.WritePacket(static_cast<int32_t>(packet), save.Packets[packet], save.Storages[packet]);
        }
    }
}

/// <summary>
/// Port fix: a save asking for more copies of a spare unit than a campaign could hold (NumAvailable past 1000; one
/// asking 2^31 hung the load making them) is refused, where the same save asking for none past it loads them. A new
/// campaign's save, its first mech made a spare one.
/// </summary>
TEST_CASE_ISOLATED("game: a save asking for more copies of a spare mech than a campaign could hold is refused")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The most copies a save's unit entry may ask for (MCLogisticsCampaign.cpp's MostCopies).
    constexpr int32_t MostCopies = 1000;
    constexpr std::string_view Save = "p3copies";
    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE_EQ(logistics->SaveCampaign(Save), 0);

    // The force's first mech saved as a spare one (the spares' blocks come first: Mech0), kept to start each try from.
    WriteSave(Save, ReadSave(Save),
              [](std::string& settings)
              {
                  ReplaceOnce(settings, "ul NumMechs=0", "ul NumMechs=1");
                  ReplaceOnce(settings, "ul NumAssMechs=3", "ul NumAssMechs=2");
              });
    const SavePackets spare = ReadSave(Save);
    REQUIRE_EQ(logistics->LoadCampaign(Save, ".sav", false, true), 0);
    CHECK_EQ(logistics->MechList->GetMechCount(), 1);

    // The spare mech's entry asking for that many copies.
    const auto askFor = [&](int32_t count)
    {
        WriteSave(Save, spare,
                  [count](std::string& settings)
                  {
                      const size_t block = settings.find("[Mech0]");
                      REQUIRE(block != std::string::npos);
                      const size_t lineEnd = settings.find('\n', block);
                      REQUIRE(lineEnd != std::string::npos);
                      const std::string_view newline = settings[lineEnd - 1] == '\r' ? "\r\n" : "\n";
                      settings.insert(lineEnd + 1, std::format("l NumAvailable={}{}", count, newline));
                  });
    };

    askFor(MostCopies);
    CHECK_EQ(logistics->LoadCampaign(Save, ".sav", false, true), 0);
    CHECK_EQ(logistics->MechList->GetMechCount(), MostCopies);
    askFor(MostCopies + 1);
    CHECK(logistics->LoadCampaign(Save, ".sav", false, true) != 0);
    CHECK_EQ(logistics->MechList->GetMechCount(), 0);

    MCFileSystem::RemoveFile(GamePath(SavePath, Save, ".sav"));
    MCFileSystem::RemoveFile(GamePath(SavePath, Save, ".pur"));
}

namespace
{
    /// <summary>The logistics state of the repair screen, where the main menu's RETURN TO GAME goes back to.</summary>
    constexpr int32_t RepairScreenState = 4;
    /// <summary>The asserts the load screen's listing makes for a save naming a mission the campaign hasn't: its
    /// operation and its mission number, each missing from the master mission file.</summary>
    constexpr int32_t UnlistedMissionAsserts = 2;

    /// <summary>
    /// Loads save <paramref name="name"/> as a player does: the main menu's load screen, the save picked, LOAD pressed;
    /// <paramref name="listingAsserts"/> the asserts listing the saves makes.
    /// </summary>
    void LoadFromScreen(std::string_view name, int32_t listingAsserts)
    {
        {
            MCTest::ExpectAssert listing("in master mission file", listingAsserts);
            LoadScreen();
            Settle();
        }

        MCFileScrollPane& pane = *GlobalLog()->LoadScreen->FilePane;
        const auto file = std::ranges::find(pane.Files, name, &MCFileScrollPane::File::Name);
        REQUIRE(file != pane.Files.end());
        pane.SetSelectedFile(static_cast<int32_t>(file - pane.Files.begin()));
        LoadGame();
        Settle();
    }
}

/// <summary>
/// A save the load refuses has emptied the campaign's lists, so it leaves no campaign: the load screen says the save
/// couldn't be loaded, and the main menu's RETURN TO GAME and SAVE GAME wait for the next load. A player's crashes on
/// 0.6.3 came back through RETURN TO GAME: in the mech lab, a mech dragged to the inventory or a portrait clicked read
/// a pilot gone from the emptied list, and on a new start the store, and the mission start's save, read the shop's
/// lists that no campaign had made (here they're there, empty, from the start). Refused before the save's mission
/// number is taken (past the campaign's end) and after it (the end itself, which picks no scenario).
/// </summary>
TEST_CASE_ISOLATED("game: a refused save leaves no campaign: RETURN TO GAME and SAVE GAME wait for the next load")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE(logistics->PurMechList != nullptr);
    REQUIRE(logistics->PurVehicleList != nullptr);
    REQUIRE(logistics->PurPilotList != nullptr);
    CHECK(logistics->PurMechList->Mechs.empty());
    CHECK(logistics->PurVehicleList->Vehicles.empty());
    CHECK(logistics->PurPilotList->Pilots.empty());
    const auto scenarios = static_cast<int32_t>(Mission()->Scenarios.size());

    for (const int32_t mission : {scenarios + 1, scenarios})
    {
        MCTest::Scope entry(std::format("mission {}", mission));

        // In a campaign's mech lab, with a good save of it and one naming the mission.
        NewCampaign();
        Settle();
        const int32_t goodMission = logistics->CurrentMission;
        REQUIRE(goodMission >= 0);
        REQUIRE_EQ(logistics->SaveCampaign("p3good"), 0);
        logistics->CurrentMission = mission;
        REQUIRE_EQ(logistics->SaveCampaign("p3refused"), 0);
        logistics->CurrentMission = goodMission;
        logistics->SetUpRepairScreen(true);
        Settle();

        // The main menu, and the save refused: the load screen stays up, saying so, with no campaign behind it.
        logistics->SetUpMainScreen(false);
        Settle();
        REQUIRE_EQ(logistics->PreviousState, RepairScreenState);
        LoadFromScreen("p3refused", UnlistedMissionAsserts);
        CHECK(logistics->CurrentScreen() == logistics->LoadScreen);
        CHECK_EQ(logistics->CurrentMission, -1);
        MCReusableDialog& message = *logistics->MessageDialog;
        REQUIRE(message.IsShowing());
        CHECK_EQ(message.Text, RefusedSaveText);
        MCGuiObject& ok = *message.OkButton;
        Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
        Settle();
        CHECK(!message.IsShowing());
        CHECK(logistics->CurrentScreen() == logistics->LoadScreen);

        // Back on the main menu, neither goes anywhere.
        Cancel();
        Settle();
        REQUIRE(logistics->CurrentScreen() == logistics->MainScreen);
        ReturnToGame();
        Settle();
        CHECK(logistics->CurrentScreen() == logistics->MainScreen);
        SaveScreen();
        Settle();
        CHECK(logistics->CurrentScreen() == logistics->MainScreen);

        // The next load fills the lists again and opens the campaign.
        LoadFromScreen("p3good", UnlistedMissionAsserts);
        CHECK(logistics->CurrentScreen() == logistics->BriefingScreen);
        CHECK(!message.IsShowing());
        CHECK_EQ(logistics->CurrentMission, goodMission);
        CHECK(!logistics->ForceMechList->Mechs.empty());
        CHECK(!logistics->AssignedWarriorList->Warriors.empty());
        CHECK(!logistics->PurMechList->Mechs.empty());
        logistics->SetUpMainScreen(false);
        Settle();
    }

    for (const std::string_view save : {"p3good", "p3refused"})
    {
        MCFileSystem::RemoveFile(GamePath(SavePath, save, ".sav"));
        MCFileSystem::RemoveFile(GamePath(SavePath, save, ".pur"));
    }
}

namespace
{
    /// <summary>
    /// A new campaign saved as <paramref name="name"/> with spare mechs making <paramref name="units"/> units (as a
    /// player with MoreUnits buys, or a mission's salvage brings), the inventory sold off since, then loaded from the
    /// load screen as a player does: its briefing comes up.
    /// </summary>
    void LoadCampaignOwning(std::string_view name, int32_t units)
    {
        NewCampaign();
        Settle();
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        MCTestGame::OwnUnits(units);
        REQUIRE_EQ(NumUnits(), units);
        REQUIRE_EQ(logistics->SaveCampaign(name), 0);
        logistics->MechList->Clear();
        LoadFromScreen(name, 0);
        REQUIRE(logistics->CurrentScreen() == logistics->BriefingScreen);
    }

    /// <summary>The menus' message dialog, showing <paramref name="text"/>, closed with its OK.</summary>
    void CloseMessage(std::string_view text)
    {
        MCReusableDialog& message = *GlobalLog()->MessageDialog;
        REQUIRE(message.IsShowing());
        CHECK_EQ(message.Text, text);
        MCGuiObject& ok = *message.OkButton;
        Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
        Settle();
        CHECK(!message.IsShowing());
    }

    /// <summary>
    /// The launch button, refused with <paramref name="units"/> owned over the limit of <paramref name="most"/>: no
    /// mission, and the message saying how many units to sell first.
    /// </summary>
    void LaunchRefused(int32_t units, int32_t most)
    {
        RealClick(LaunchButton.x, LaunchButton.y);
        Settle();
        CHECK(Scenario() == nullptr);
        CloseMessage(MCFormatPrintf(LoadGameString(0x373, 0xfe), units, most, units - most));
    }

    /// <summary>Spare mechs sold off (the inventory's first) until the player owns <paramref name="units"/>.</summary>
    void SellDownTo(int32_t units)
    {
        while (NumUnits() > units)
        {
            REQUIRE_EQ(GlobalLog()->MechList->RemoveMech(0), 0);
        }
    }

    /// <summary>Save <paramref name="name"/>'s files deleted.</summary>
    void RemoveSave(std::string_view name)
    {
        MCFileSystem::RemoveFile(GamePath(SavePath, name, ".sav"));
        MCFileSystem::RemoveFile(GamePath(SavePath, name, ".pur"));
    }
}

/// <summary>
/// Port-only (MoreUnits): with MoreUnits a save holding more than the original's 50 units loads whole, the shop has
/// room for more, and LAUNCH starts the mission; past 100 (a mission's salvage can take it there) LAUNCH asks to sell
/// down to 100 first.
/// </summary>
TEST_CASE_ISOLATED("game: with MoreUnits a save holding more than 50 units is bought into and launched")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Save = "p3moreunits";
    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    GMoreUnits = 1;
    LoadCampaignOwning(Save, 101);
    RemoveSave(Save);
    CHECK_EQ(NumUnits(), 101);
    DeployFirstMech();
    LaunchRefused(101, 100);

    // Sold down to 60: room in the shop for 40 more, and the mission starts.
    SellDownTo(60);
    CHECK_EQ(MaxPurchase(-1), 40);
    CHECK(!CheckMaxUnits());
    CHECK(Launch());
}

/// <summary>
/// A save holding more units than the original's 50 (60: one made with MoreUnits, loaded without it or by an older
/// build) loads whole, as the original's did a save taken past its limit by salvage: the shop sells nothing more, and
/// LAUNCH asks to sell down to 50 first, then starts the mission.
/// </summary>
TEST_CASE_ISOLATED("game: without MoreUnits a save holding more than 50 units launches once sold down to 50")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Save = "p3overlimit";
    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    REQUIRE_EQ(GMoreUnits, 0);
    LoadCampaignOwning(Save, 60);
    RemoveSave(Save);
    CHECK_EQ(NumUnits(), 60);
    CHECK(CheckMaxUnits());
    CloseMessage(LoadGameString(0x374, 0xfe));
    DeployFirstMech();
    LaunchRefused(60, 50);
    SellDownTo(50);
    CHECK(Launch());
}

TEST_CASE_ISOLATED("game: the mission's start file puts the deployed mech and its pilot at the drop zone")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    std::shared_ptr<MCLogMech> mech;
    REQUIRE_EQ(logistics->ForceMechList->GetMechInfo(0, mech), 0);
    REQUIRE(mech->PilotIndex >= 0);
    std::shared_ptr<MCLogWarrior> pilot;
    REQUIRE_EQ(logistics->AssignedWarriorList->GetWarriorInfo(mech->PilotIndex, pilot), 0);
    REQUIRE(logistics->LocalDropSlot[0]);

    // The force's first mech in the first lance's first slot.
    logistics->DeploySlots[0][0].Unit = 0;
    mech->Deployed = true;
    const std::string scenario = Mission()->Scenarios[static_cast<size_t>(Mission()->CurrentScenario)];
    REQUIRE_EQ(logistics->PrepareScenario(scenario, "p3start"), 0);

    MCFitIniFile start;
    REQUIRE_EQ(start.Open(GamePath(SaveTempPath, "p3start", ".fit")), 0);
    REQUIRE_EQ(start.SeekBlock("Parts"), 0);
    const auto numParts = start.Read<uint32_t>("NumParts");
    REQUIRE(numParts.has_value());
    REQUIRE_EQ(start.SeekBlock("Warriors"), 0);
    const auto numWarriors = start.Read<uint32_t>("NumWarriors");
    REQUIRE(numWarriors.has_value());

    // The mech is the last part: player controlled, a mech, from the profile written for the first deployed unit
    // (mech0000; its pilot's is warr0000), at the zone plus the slot's offset.
    const uint32_t part = *numParts;
    REQUIRE_EQ(start.SeekBlock(std::format("Part{}", part)), 0);
    CHECK_EQ(start.Read<uint32_t>("ControlType").value_or(0), 2u);
    CHECK_EQ(start.Read<uint32_t>("ControlDataType").value_or(0), 1u);
    CHECK_EQ(start.Read<uint32_t>("ObjectNumber").value_or(0), mech->Chassis);
    CHECK_EQ(start.Read<std::string>("ObjectProfile").value_or(""), std::string("mech0000"));
    CHECK_EQ(start.Read<float>("PositionX").value_or(0.0f),
             logistics->DeploySlotPlacements[0][0].OffsetX + logistics->DropZonePositions[0].X);
    CHECK_EQ(start.Read<float>("PositionY").value_or(0.0f),
             logistics->DeploySlotPlacements[0][0].OffsetY + logistics->DropZonePositions[0].Y);
    CHECK_EQ(start.Read<int32_t>("Active").value_or(0), 1);
    CHECK_EQ(mech->PartNumber, static_cast<int32_t>(part));

    // Its pilot is the last warrior, with the profile written for it and the pilot's brain.
    CHECK_EQ(start.Read<uint32_t>("Pilot").value_or(0), *numWarriors);
    REQUIRE_EQ(start.SeekBlock(std::format("Warrior{}", *numWarriors)), 0);
    CHECK_EQ(start.Read<std::string>("Profile").value_or(""), std::string("warr0000"));
    CHECK_EQ(start.Read<std::string>("Brain").value_or(""), pilot->Brain);

    // The player's commander has one group: that mech.
    REQUIRE_EQ(start.SeekBlock("Commander0Group:0"), 0);
    const auto mates = start.ReadArray<int32_t>("Mates");
    REQUIRE(mates.has_value());
    REQUIRE_EQ(mates->size(), 12u);
    CHECK_EQ((*mates)[0], static_cast<int32_t>(part));
    CHECK_EQ((*mates)[1], 0);
    CHECK_EQ(start.SeekBlock("Commander0Group:1"), BLOCK_NOT_FOUND);
}
