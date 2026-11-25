#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "MCTestArchive.h"
#include "RemadeScreens.h"
#include "lib/MCPacketFile.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogisticsTabs.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMechBriefBlock.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCArtModes.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogMechList.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "main/MCMissionGlobals.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCPrefs.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBattleMech.h"
#include "object/MCObjectType.h"
#include "network/MCRoomEnd.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"
#include "gui/MCGuiGlobals.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCHoldLists.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCSpriteTree.h"
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

    // The multiplayer menu (remade: the main menu's panel, the main screen logistics' under it).
    ShowMultiPlayer();
    Settle();
    CHECK(MCMainMenuScreens::Shown(MCMainPanel::Multiplayer));
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    CHECK_EQ(logistics->LogisticsState, 10);
    CHECK(!Solo);
    MCTestGuest::ClickOn(MCMainPanel::Multiplayer, "back");
    Settle();
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    CHECK(MCMainMenuScreens::Shown(MCMainPanel::Main));
    CHECK_EQ(logistics->LogisticsState, 1);

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
    const MCTestGame::OldMainMenu oldMenu;
    REQUIRE(MCTestGame::StartLogistics([&saves] { saves.emplace(Saves); }));
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    CHECK(!logistics->MainScreen->Element<MCLogButton>(MCTestGame::MainMenuLoadButton)->Disabled);

    // The pinned saves are empty files: reading each as the screen opens logs it by name (no assert), and lists it by
    // name.
    LoadScreen();
    Settle();

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
        // Port-only (SPEC-menus.md's stage 1): the scenario loads behind the loading screen, and plays once it has
        // faded out (MCMission's changes of screen, gui/MCScreenChange.h).
        const auto playing = [] { return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario; };

        for (int32_t frame = 0; frame < mostFrames && !playing(); frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        return playing();
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

namespace
{
    /// <summary>A unit's part type: a mech's.</summary>
    constexpr int32_t MechPartType = 1;

    /// <summary>The remade campaign screen on DEPLOY.</summary>
    MCCampaignScreen& ShowDeploy()
    {
        return MCRemadeTest::CampaignScreen(MCCampaignScreen::MCTab::Deploy);
    }

    /// <summary>READY AREA's card of <paramref name="part"/>; none when it's in the drop.</summary>
    std::optional<size_t> ReadyCardOf(const MCLogPart& part)
    {
        const std::vector<std::shared_ptr<MCLogPart>> ready = MCDeploy::ReadyUnits();
        const auto found = std::ranges::find_if(ready, [&part](const auto& unit) { return unit.get() == &part; });
        return found != ready.end() ? std::optional(static_cast<size_t>(found - ready.begin())) : std::nullopt;
    }

    /// <summary>DEPLOY's left drag of <paramref name="part"/>'s card from READY AREA onto drop slot <paramref name="slot"/>.</summary>
    void CarryInto(MCCampaignScreen& screen, const MCLogPart& part, size_t slot)
    {
        const std::optional<size_t> card = ReadyCardOf(part);
        REQUIRE(card.has_value());
        screen.PickUpReady(*card, false);
        REQUIRE(screen.Carried().get() == &part);
        screen.HoldOver(slot);
        screen.PutDown();
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    }

    /// <summary>DEPLOY's left drag of drop slot <paramref name="slot"/>'s card back onto READY AREA.</summary>
    void CarryOut(MCCampaignScreen& screen, size_t slot)
    {
        screen.PickUpSlot(slot, false);
        REQUIRE(screen.Carried() != nullptr);
        screen.HoldOver(std::nullopt);
        screen.PutDown();
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    }

    /// <summary>
    /// On DEPLOY, READY AREA's first unit (a mech) carried into the first drop slot, the drop's limit made its tons more.
    /// </summary>
    std::shared_ptr<MCLogMech> DeployFirstReady(MCCampaignScreen& screen)
    {
        const std::vector<std::shared_ptr<MCLogPart>> ready = MCDeploy::ReadyUnits();
        REQUIRE(!ready.empty());
        REQUIRE_EQ(ready.front()->PartType, MechPartType);
        const std::shared_ptr<MCLogMech> mech = std::static_pointer_cast<MCLogMech>(ready.front());
        MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(mech->CurTonnage);
        CarryInto(screen, *mech, 0);
        REQUIRE(MCDeploy::SlotUnit(0) == mech);
        return mech;
    }

    /// <summary>DEPLOY's BEGIN MISSION, then frames until the scenario is up; whether it is.</summary>
    bool BeginMission(MCCampaignScreen& screen)
    {
        constexpr int32_t mostFrames = 300;
        REQUIRE(MCRemadeTest::Press(screen, "begin"));
        const auto playing = [] { return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario; };

        for (int32_t frame = 0; frame < mostFrames && !playing(); frame++)
        {
            MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        }

        return playing();
    }
}

TEST_CASE_ISOLATED("game: units dragged into the drop slots count against the drop tonnage")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The 1998 screens' own test: their 1998 box with them, as where the menus can't start (until stage 9's step 2).
    MCReusableDialog::OldBoxOnly() = true;

    // The 1998 campaign screens' own test (kept until stage 9 removes them): the remade screens off.
    MCCampaignMenu::Remade() = false;

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

namespace
{
    /// <summary>
    /// Writes the HD pack's archive of <paramref name="folder"/> in <paramref name="pack"/>: a numbered frame of each of
    /// <paramref name="frameBytes"/>' sizes, its bytes its number.
    /// </summary>
    /// <returns>The bytes it stores, all of which the hold keeps.</returns>
    size_t WriteFramesArchive(const std::filesystem::path& pack, const std::filesystem::path& folder,
                              std::span<const size_t> frameBytes)
    {
        std::vector<MCTestArchive::ArchiveEntry> entries;
        size_t stored = 0;

        for (size_t frame = 0; frame < frameBytes.size(); frame++)
        {
            entries.push_back(
                MCTestArchive::Raw(std::format("{}.hdf", frame),
                                   std::vector<uint8_t>(frameBytes[frame], static_cast<uint8_t>(frame + 1))));
            stored += frameBytes[frame];
        }

        const std::filesystem::path file = std::filesystem::path(pack / folder).concat(".hda");
        std::filesystem::create_directories(file.parent_path());
        MCTestArchive::WriteBytes(file, MCTestArchive::BuildArchive(entries));
        return stored;
    }
}

/// <summary>
/// Port-only (SPEC-memory.md's "From the briefing"): a mech put in the lance on DEPLOY, in an empty slot or dropped on
/// another's, has its HD art read from then on, behind the screen, and held for the mission
/// (MCHdArt::HoldMechAhead); taken out before those reads are made, they're still made, and it stays held.
/// </summary>
TEST_CASE_ISOLATED("game: a mech put in the lance at the briefing is held from then, and stays held when taken out")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCScratchFolder pack("mc_briefing_tests", "lance-hold");
    MCTestGame::BootOptions.Started = [folder = pack.Path()]
    {
        GHdArt = 1;
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.Hold = true;
        MCHdArt::SetReaderSettings(settings);
        MCHdArt::SetPackFolder(folder);
    };

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    MCCampaignScreen& screen = ShowDeploy();
    const std::string mission = Mission()->Scenarios.at(static_cast<size_t>(Mission()->CurrentScenario));
    REQUIRE(MCHdArt::HoldsFor(mission));

    // Two mechs from READY AREA, standing in for mechs of chassis the mission doesn't list (one bought at the store):
    // mission 1's own force is in the mission's list, read from the briefing's start.
    const std::vector<std::shared_ptr<MCLogPart>> ready = MCDeploy::ReadyUnits();
    REQUIRE(!ready.empty());
    REQUIRE_EQ(ready.front()->PartType, MechPartType);
    const std::shared_ptr<MCLogMech> first = std::static_pointer_cast<MCLogMech>(ready.front());
    const auto other = std::ranges::find_if(ready, [&first](const auto& unit)
                                            { return unit->PartType == MechPartType && unit != first; });
    REQUIRE(other != ready.end());
    const std::shared_ptr<MCLogMech> second = std::static_pointer_cast<MCLogMech>(*other);

    // Two chassis with a folder each of their own (in neither the mission's list nor the other's), and in the pack an
    // archive for each of those folders: every mech chassis in the object file, its folders worked out as a picked
    // mech's are (MechTreeFolders).
    std::map<uint32_t, std::vector<std::filesystem::path>> chassisFolders;
    MCPacketFile objects;
    REQUIRE_EQ(objects.Open(GamePath(ObjectPath, ObjectPakName)), 0);

    for (int32_t number = 1; number < objects.GetNumPackets(); ++number)
    {
        if (std::vector<std::filesystem::path> folders = MechTreeFolders(static_cast<uint32_t>(number));
            !folders.empty())
        {
            chassisFolders.emplace(static_cast<uint32_t>(number), std::move(folders));
        }
    }

    const std::vector<std::filesystem::path> missionFolders = MCHdArt::BriefingFolders(mission);
    const auto ownFolder = [&](uint32_t chassis, uint32_t otherChassis)
    {
        const std::vector<std::filesystem::path>& folders = chassisFolders.at(chassis);
        const std::vector<std::filesystem::path>& others = chassisFolders.at(otherChassis);
        const auto found = std::ranges::find_if(
            folders, [&](const std::filesystem::path& folder)
            { return !std::ranges::contains(missionFolders, folder) && !std::ranges::contains(others, folder); });
        return found != folders.end() ? *found : std::filesystem::path();
    };

    std::filesystem::path firstFolder;
    std::filesystem::path secondFolder;

    for (const uint32_t a : std::views::keys(chassisFolders))
    {
        for (const uint32_t b : std::views::keys(chassisFolders))
        {
            if (firstFolder.empty() && a != b && !ownFolder(a, b).empty() && !ownFolder(b, a).empty())
            {
                first->Chassis = a;
                first->HdFolders = chassisFolders.at(a);
                second->Chassis = b;
                second->HdFolders = chassisFolders.at(b);
                firstFolder = ownFolder(a, b);
                secondFolder = ownFolder(b, a);
            }
        }
    }

    REQUIRE(!firstFolder.empty());
    constexpr std::array<size_t, 3> FirstFrameBytes{3000, 5000, 7000};
    constexpr std::array<size_t, 2> SecondFrameBytes{4000, 6000};
    const size_t stored = WriteFramesArchive(pack.Path(), firstFolder, FirstFrameBytes) +
                          WriteFramesArchive(pack.Path(), secondFolder, SecondFrameBytes);
    MCHdArt::WaitForHold();
    const size_t before = MCHdArt::HeldBytes();

    // With the readers paused (nothing read meanwhile): the first in an empty slot, the second dropped on it (the first
    // out), then the second taken out.
    MCHdArt::HoldReads(true);
    MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(first->CurTonnage + second->CurTonnage);
    CarryInto(screen, *first, 0);
    REQUIRE(first->Deployed != 0);
    CarryInto(screen, *second, 0);
    REQUIRE(second->Deployed != 0);
    REQUIRE(first->Deployed == 0);
    CarryOut(screen, 0);
    REQUIRE(second->Deployed == 0);
    CHECK_EQ(MCHdArt::HeldBytes(), before);

    // Let go: the reads both placings asked for are made, and what they read stays held.
    MCHdArt::HoldReads(false);
    MCHdArt::WaitForHold();
    CHECK_EQ(MCHdArt::HeldBytes(), before + stored);
    Settle();
    CHECK_EQ(MCHdArt::HeldBytes(), before + stored);
    CHECK(MCHdArt::HoldsFor(mission));
}

/// <summary>
/// Port-only (SPEC-memory.md's manifest: the players' part, worked out as each mech is picked): a mech's HD folders
/// worked out from its chassis with no mission loaded (MechTreeFolders, as a mech joining the force has them) are
/// those its mission's load draws it from, for every mech in the campaign's first mission, player's and enemy's; and
/// every stock mission names the sprite file the work-out reads (MechSpriteFile).
/// </summary>
TEST_CASE_ISOLATED("game: a picked mech's HD folders are worked out as its mission's load draws it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // Booted first: the missions' files lie in the game's archives.
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::StartCampaignMission());

    for (const MCHoldLists::Mission& listed : MCHoldLists::Missions)
    {
        MCTest::Scope scope{std::string(listed.Name)};
        MCFitIniFile file;
        REQUIRE_EQ(file.Open(GamePath(MissionPath, listed.Name, ".fit")), 0);
        REQUIRE_EQ(file.SeekBlock("SpriteSystem"), 0);
        CHECK(MCIEquals(file.Read<std::string>("SpriteFileName").value_or(""), MechSpriteFile));
    }

    const std::shared_ptr<MCSpriteManager> sprites = SpriteManager();
    REQUIRE(sprites != nullptr);
    size_t mechs = 0;

    for (const MCPart& part : Scenario()->Parts)
    {
        const auto object = std::dynamic_pointer_cast<MCGameObject>(part.Object.lock());

        if (object == nullptr || object->ObjectClass != MCObjectClass::BattleMech)
        {
            continue;
        }

        const auto actor = std::static_pointer_cast<MCMechActor>(object->GetAppearance());
        const auto tree = std::static_pointer_cast<MCSpriteTree>(actor->GetAppearanceType());
        REQUIRE(tree != nullptr);
        const uint32_t chassis = static_cast<uint32_t>(object->GetObjectType()->ObjTypeNum);
        MCTest::Scope scope{std::format("chassis {}", chassis)};
        std::vector<std::filesystem::path> drawn = tree->HdFolders(sprites->Uses90PixelParts());

        for (std::filesystem::path& folder : drawn)
        {
            folder /= "0";
        }

        CHECK(MechTreeFolders(chassis) == drawn);
        ++mechs;
    }

    CHECK(mechs > 1);
}

namespace
{
    /// <summary>The most frames a mission ended at once takes to bring its results up.</summary>
    constexpr int32_t MostFramesToResults = 300;

    /// <summary>
    /// The campaign's first mission started (MCTestGame::StartCampaignMission), ended as <paramref name="result"/> (as a
    /// quick-start mission's when <paramref name="solo"/>), and its results closed: the way back done.
    /// </summary>
    void PlayToResultsAndBack(uint32_t result, bool solo);

    /// <summary>
    /// The mission playing ended as <paramref name="result"/> (as a quick-start mission's when <paramref name="solo"/>),
    /// and its results closed: the way back done.
    /// </summary>
    void EndToResultsAndBack(uint32_t result, bool solo)
    {
        Solo = solo;
        ScenarioResult = result;
        Mission()->EndScenarioRequested = -1;

        for (int32_t frame = 0; frame < MostFramesToResults && Mission()->State != MCMissionState::Results; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        REQUIRE(Mission()->State == MCMissionState::Results);
        Mission()->CloseResultsScreen();
        Settle();
    }

    void PlayToResultsAndBack(uint32_t result, bool solo)
    {
        REQUIRE(MCTestGame::StartCampaignMission());
        EndToResultsAndBack(result, solo);
    }
}

/// <summary>
/// Port-only (SPEC-memory.md's "Between missions"): a mission's held art is kept past its end only where a mission plays
/// next. A campaign mission's win holds the next one from its results' close, a loss keeps its own; a won quick-start
/// mission, back to SOLO MISSIONS, lets go of it, as a match does unless its rematch follows.
/// </summary>
TEST_CASE_ISOLATED("game: the hold is kept past a mission's end only where a mission plays next")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCScratchFolder pack("mc_briefing_tests", "between-missions");
    MCTestGame::BootOptions.Started = [folder = pack.Path()]
    {
        GHdArt = 1;
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.Hold = true;
        MCHdArt::SetReaderSettings(settings);
        MCHdArt::SetPackFolder(folder);
    };

    REQUIRE(MCTestGame::StartLogistics());
    constexpr uint32_t Lost = 0;
    constexpr uint32_t Won = 4;

    // The campaign's win: its next mission held from the results' close, its own let go of.
    PlayToResultsAndBack(Won, false);
    const std::vector<std::string>& missions = Mission()->Scenarios;
    REQUIRE(missions.size() > 1);
    CHECK(MCHdArt::HoldsFor(missions[1]));
    CHECK(!MCHdArt::HoldsFor(missions[0]));

    // What the way back keeps, by where it goes: a loss replays the mission; a quick-start win goes to SOLO
    // MISSIONS; a match keeps it for its rematch alone.
    const auto keeps = [](uint32_t result, bool solo)
    {
        const uint32_t wasResult = ScenarioResult;
        const bool wasSolo = Solo;
        ScenarioResult = result;
        Solo = solo;
        const bool kept = Mission()->KeepsHoldPastEnd();
        ScenarioResult = wasResult;
        Solo = wasSolo;
        return kept;
    };

    CHECK(keeps(Lost, false));
    CHECK(keeps(Won, false));
    CHECK(!keeps(Won, true));
    MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
    CHECK(!MCRoomEnd::RematchAfterMatch());
    CHECK(!keeps(Won, false));
    MCGameContext::Current().SetMultiPlayer(nullptr);

    // A quick-start mission won: SOLO MISSIONS (stage 2's landing; remade in the main menu's panel, stage 7), and
    // nothing held.
    PlayToResultsAndBack(Won, true);
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
    CHECK(MCMainMenuScreens::Shown(MCMainPanel::Solo));
    CHECK(!MCHdArt::HoldsFor(missions[0]));
    CHECK(!MCHdArt::HoldsFor(missions[1]));
    CHECK_EQ(MCHdArt::HeldBytes(), 0u);
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 2: the menus' data outlives a mission): the mission keeps the logistics' art while it
/// plays, and the way back takes it instead of loading it again; art loaded from other files (the game's files switched
/// meanwhile) is loaded again.
/// </summary>
TEST_CASE_ISOLATED("game: the way back from a mission takes the menus' art kept across it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    constexpr uint32_t Won = 4;
    const std::span<const uint8_t> shapes = GlobalLog()->MechRepShapes[0].Bytes();
    const std::shared_ptr<MCLogPort> button = GlobalLog()->ScreenButtonPorts[0][0];
    REQUIRE(!shapes.empty());
    REQUIRE(button != nullptr);

    REQUIRE(MCTestGame::StartCampaignMission());
    REQUIRE(Mission()->KeptLogisticsArt != nullptr);
    CHECK(Mission()->KeptLogisticsArt->Files.MechRepShapes[0].Bytes().data() == shapes.data());
    CHECK(Mission()->KeptLogisticsArt->Files.ScreenButtonPorts[0][0] == button);
    EndToResultsAndBack(Won, false);
    CHECK(Mission()->KeptLogisticsArt == nullptr);
    CHECK(GlobalLog()->MechRepShapes[0].Bytes().data() == shapes.data());
    CHECK(GlobalLog()->ScreenButtonPorts[0][0] == button);

    // Kept from other files: loaded again (the old picture, still held here, isn't the new one).
    REQUIRE(MCTestGame::StartCampaignMission());
    REQUIRE(Mission()->KeptLogisticsArt != nullptr);
    Mission()->KeptLogisticsArt->Key.Switches++;
    EndToResultsAndBack(Won, false);
    REQUIRE(GlobalLog()->ScreenButtonPorts[0][0] != nullptr);
    CHECK(GlobalLog()->ScreenButtonPorts[0][0] != button);
}

/// <summary>
/// A unit in a drop slot taken out of the force on another screen, then sold, is gone when the briefing screen shows
/// again: its slot shows empty (release 8 kept the slot's block of the unit that no longer was, and drew it).
/// </summary>
TEST_CASE_ISOLATED("game: a deployed unit sold on another screen leaves its drop slot empty")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    MCCampaignScreen& screen = ShowDeploy();
    std::weak_ptr<MCLogMech> sold = DeployFirstReady(screen);
    REQUIRE(!sold.expired());

    // On PURCHASING: the mech leaves the force (off its drop slot) for SPARE, the right button's move, and is sold
    // there, dragged onto the store (gone).
    MCLogisticsTabs& tabs = screen.Logistics();
    screen.ChooseTab(MCCampaignScreen::MCTab::Purchasing);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    const auto cardOf =
        [&tabs, &sold](MCLogisticsTabs::MCPlace place, const std::vector<MCCampaignScreen::MCCard>& cards)
    {
        size_t card = 0;

        while (card < cards.size() && tabs.UnitAt(place, card) != sold.lock())
        {
            card++;
        }

        REQUIRE(card < cards.size());
        return card;
    };

    tabs.Send(MCLogisticsTabs::MCPlace::Force, cardOf(MCLogisticsTabs::MCPlace::Force, tabs.ForceCards()));
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    CHECK(MCDeploy::SlotUnit(0) == nullptr);
    tabs.Drop(MCLogisticsTabs::MCPlace::Spare, cardOf(MCLogisticsTabs::MCPlace::Spare, tabs.SpareCards()),
              MCLogisticsTabs::MCPlace::Store);
    REQUIRE(tabs.DealOpen());
    tabs.DealConfirm();
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    // Owned no more: in neither the force nor SPARE.
    const auto owned = [&sold](const MCLogMechList& list) { return std::ranges::contains(list.Mechs, sold.lock()); };
    REQUIRE(!owned(*GlobalLog()->ForceMechList));
    REQUIRE(!owned(*GlobalLog()->MechList));

    // Back to DEPLOY: shown with the slot empty.
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK(MCDeploy::SlotUnit(0) == nullptr);
    REQUIRE(!screen.Groups().empty());
    CHECK_EQ(screen.Groups()[0].Cards[0].Kind, std::to_underlying(MCCampaignScreen::MCCardKind::Empty));
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

    // The 1998 campaign screens' own test (kept until stage 9 removes them): the remade screens off.
    MCCampaignMenu::Remade() = false;

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

    // The main menu's PREFERENCES, then ACCEPT.
    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Hd);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(savedLighting(), MCFitResult<int32_t>(HdLightingNight));

    // No mode shown now: the preferences change nothing.
    prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Hd);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    GHdLighting = HdLightingOff;
    std::filesystem::remove(prefsPath);
}

/// <summary>
/// A setting changed on Preferences' HD section reaches the mission launched next: HD LIGHTING stepped from OFF to NIGHT
/// there and accepted is written to PREFS, and the new campaign's first mission, begun from the remade briefing, shows
/// at night, its world much darker than with HD LIGHTING off.
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

    // The preferences, HD, three steps of HD LIGHTING's switch (ON, DUSK, NIGHT), ACCEPT.
    const std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Hd);
    REQUIRE(prefs != nullptr);
    constexpr int32_t stepsToNight = 3;
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "hd-lighting", SDL_SCANCODE_RIGHT, stepsToNight));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "hd-lighting"), "Night");
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(prefsPath)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefs.Read<int32_t>("HDLighting"), MCFitResult<int32_t>(HdLightingNight));
    }

    // The new campaign's first mission begun from the remade briefing (its BEGIN MISSION's request).
    REQUIRE(MCTestGame::StartCampaignMission());
    CHECK(MCSceneLights::TimeOfDay() == MCTimeOfDay::Night);

    // Until units move, as MCTestGame::StartMission waits.
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

    // The 1998 campaign screens' own test (kept until stage 9 removes them): the remade screens off.
    MCCampaignMenu::Remade() = false;

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

    // Its saves in a folder of its own, gone with it whatever its result: a refused one left behind would be the
    // next test's to read.
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
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
    // Its save in a folder of its own, gone with it whatever its result.
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
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
}

namespace
{
    /// <summary>The logistics state of the repair screen, where the main menu's RETURN TO GAME goes back to.</summary>
    constexpr int32_t RepairScreenState = 4;
    /// <summary>
    /// Loads save <paramref name="name"/> as a player does: the main menu's LOAD GAME (stage 5's), the save picked,
    /// LOAD pressed.
    /// </summary>
    void LoadFromScreen(std::string_view name)
    {
        REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
        Settle();
        REQUIRE(MCSavesMenu::Screen().has_value());
        MCSavesScreen& screen = MCSavesMenu::Screen()->get();
        const auto save = std::ranges::find(screen.Saves(), name);
        REQUIRE(save != screen.Saves().end());
        screen.Pick(static_cast<int>(save - screen.Saves().begin()));
        screen.PressMain();
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

    // Its saves in a folder of its own, gone with it whatever its result: the refused one left behind (as a failed run
    // left it, 24 Nov) was every later test's in its worker to read as the game started.
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
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
        LoadFromScreen("p3refused");
        REQUIRE(MCSavesMenu::Screen().has_value());
        MCSavesScreen& loadGame = MCSavesMenu::Screen()->get();
        CHECK(loadGame.Shown());
        CHECK_EQ(logistics->CurrentMission, -1);
        REQUIRE(loadGame.Dialog());
        CHECK(loadGame.Dialog()->Shown());
        CHECK_EQ(loadGame.Dialog()->Saying().Question, std::string(MCSavesMenu::RefusedText));
        MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
        Settle();
        CHECK(!loadGame.Dialog()->Shown());
        CHECK(loadGame.Shown());

        // Back on the main menu, neither goes anywhere.
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
        Settle();
        REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Main());
        ReturnToGame();
        Settle();
        CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
        SaveScreen();
        Settle();
        CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());

        // The next load fills the lists again and opens the campaign.
        LoadFromScreen("p3good");
        CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
        CHECK(!loadGame.Shown());
        CHECK_EQ(logistics->CurrentMission, goodMission);
        CHECK(!logistics->ForceMechList->Mechs.empty());
        CHECK(!logistics->AssignedWarriorList->Warriors.empty());
        CHECK(!logistics->PurMechList->Mechs.empty());
        logistics->SetUpMainScreen(false);
        Settle();
    }
}

namespace
{
    /// <summary>The good save beside the damaged ones, and how long before them it was written.</summary>
    constexpr std::string_view GoodSave = "Kaio, good";
    constexpr int64_t GoodSaveHoursAgo = 2;
    constexpr int64_t SecondsPerHour = 60 * 60;
    /// <summary>A foreign file's length and the pattern of its bytes (no packet file's header).</summary>
    constexpr size_t ForeignBytes = 256;
    constexpr uint32_t ForeignStep = 37;
    constexpr uint32_t ForeignStart = 11;
    /// <summary>How much of its settings a short save has lost.</summary>
    constexpr size_t ShortBy = 40;
    /// <summary>A mission number no campaign has, and a campaign (the [Planet] Setting) the game hasn't.</summary>
    constexpr int32_t NoSuchMission = 9999;
    constexpr int32_t NoSuchCampaign = 5;

    /// <summary>
    /// The settings (a save's first packet) of campaign <paramref name="setting"/>'s mission <paramref name="mission"/>
    /// (without its General block for none).
    /// </summary>
    std::string SettingsOf(int32_t setting, std::optional<int32_t> mission)
    {
        const std::string general =
            mission.has_value() ? std::format("[General]\r\nl MissionNumber={}\r\n", *mission) : std::string();
        return std::format(
            "FITini\r\n[Planet]\r\nl Setting={}\r\n{}[ResourcePoints]\r\nul numPoints=1000\r\nFITend\r\n", setting,
            general);
    }

    /// <summary>Writes <paramref name="bytes"/> as save <paramref name="name"/>'s whole file.</summary>
    void WriteSaveFile(std::string_view name, std::span<const uint8_t> bytes)
    {
        MCFile file;
        REQUIRE_EQ(file.Create(GamePath(SavePath, name, ".sav")), 0);
        REQUIRE_EQ(file.Write(bytes), static_cast<int32_t>(bytes.size()));
    }

    /// <summary>Writes save <paramref name="name"/> as a packet file whose one packet is <paramref name="settings"/>.</summary>
    void WriteSettingsSave(std::string_view name, std::string_view settings)
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(GamePath(SavePath, name, ".sav")), 0);
        out.Reserve(1);
        out.WritePacket(0, std::span(reinterpret_cast<const uint8_t*>(settings.data()), settings.size()),
                        MCPacketStorage::Raw);
    }

    /// <summary>Save <paramref name="name"/>'s whole file.</summary>
    std::vector<uint8_t> SaveFileBytes(std::string_view name)
    {
        MCFile file;
        REQUIRE_EQ(file.Open(GamePath(SavePath, name, ".sav")), 0);
        std::vector<uint8_t> bytes(file.FileSize());
        REQUIRE_EQ(file.Read(bytes), static_cast<int32_t>(bytes.size()));
        return bytes;
    }

    /// <summary>
    /// The damaged saves a player's folder can hold, by name: an empty file, a foreign one, a packet file whose settings
    /// aren't a FIT file, one cut short inside its settings (its points gone), one naming no mission, and ones naming a
    /// campaign the game hasn't and missions their campaign hasn't (below it, and past it).
    /// </summary>
    std::vector<std::string> WriteDamagedSaves()
    {
        std::vector<std::string> names;
        const auto named = [&names](std::string_view name) -> std::string_view { return names.emplace_back(name); };

        WriteSaveFile(named("Damaged, empty"), {});
        std::vector<uint8_t> foreign(ForeignBytes);

        for (size_t index = 0; index < foreign.size(); index++)
        {
            foreign[index] = static_cast<uint8_t>(ForeignStart + ForeignStep * index);
        }

        WriteSaveFile(named("Damaged, foreign"), foreign);
        WriteSettingsSave(named("Damaged, not settings"),
                          std::string_view(reinterpret_cast<const char*>(foreign.data()), foreign.size()));
        WriteSettingsSave("Damaged, short", SettingsOf(0, 0));
        std::vector<uint8_t> whole = SaveFileBytes(named("Damaged, short"));
        REQUIRE(whole.size() > ShortBy);
        whole.resize(whole.size() - ShortBy);
        WriteSaveFile("Damaged, short", whole);
        WriteSettingsSave(named("Damaged, no mission"), SettingsOf(0, std::nullopt));
        WriteSettingsSave(named("Damaged, campaign"), SettingsOf(NoSuchCampaign, 0));
        WriteSettingsSave(named("Damaged, mission below"), SettingsOf(0, -1));
        WriteSettingsSave(named("Damaged, mission past"), SettingsOf(0, NoSuchMission));
        return names;
    }
}

/// <summary>
/// Damaged saves in the save folder (a player's, or one a test left: the refused test's naming the campaign's end, which
/// tripped 1998's load and save screens' asserts in every later test of its worker as each start made them, 24 Nov)
/// don't stop the menus reading the others: each is logged by its name, kept off CONTINUE and CAMPAIGNS, and listed by
/// its name alone, on LOAD GAME (whose LOAD says it couldn't be loaded) and 1998's screens. The good save beside them
/// still shows and plays. They're there before the main menu first shows.
/// </summary>
TEST_CASE_ISOLATED(
    "game: damaged saves are left off CONTINUE and CAMPAIGNS and named on LOAD GAME; the rest still play")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    std::vector<std::string> damaged;
    REQUIRE(MCTestGame::StartLogistics(
        [&]
        {
            pinned.emplace();
            damaged = WriteDamagedSaves();
        }));
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    // As the game starts: no CONTINUE (no save can be read).
    CHECK(MCTestGuest::Panel(MCMainPanel::Main).ContinueLine().empty());

    // A good save, older than the damaged ones, and the game's own save of the campaign's end (the refused test's).
    NewCampaign();
    Settle();
    const int32_t goodMission = logistics->CurrentMission;
    REQUIRE_EQ(logistics->SaveCampaign(GoodSave), 0);
    std::filesystem::last_write_time(MCFileSystem::Resolve(GamePath(SavePath, GoodSave, ".sav")),
                                     std::filesystem::file_time_type::clock::now() -
                                         std::chrono::seconds(GoodSaveHoursAgo * SecondsPerHour));
    logistics->CurrentMission = static_cast<int32_t>(Mission()->Scenarios.size());
    REQUIRE_EQ(logistics->SaveCampaign("Damaged, the campaign's end"), 0);
    damaged.emplace_back("Damaged, the campaign's end");
    logistics->CurrentMission = goodMission;

    // 1998's load and save screens, as each start makes them (the leftover refused save tripped their asserts in every
    // later test of its worker, 24 Nov): every save in their lists, the damaged ones by name alone.
    for (const std::shared_ptr<MCSplashScreen>& screen : {logistics->LoadScreen.Lock(), logistics->SaveScreen.Lock()})
    {
        REQUIRE(screen != nullptr);
        REQUIRE(screen->FilePane != nullptr);
        screen->FilePane->SetStartDirectory(SavePath);
        std::vector<MCFileScrollPane::File>& files = screen->FilePane->Files;

        for (const std::string& name : damaged)
        {
            MCTest::Scope entry(std::format("1998's list, {}", name));
            const auto file = std::ranges::find(files, name, &MCFileScrollPane::File::Name);
            REQUIRE(file != files.end());
            CHECK_EQ(file->Operation, 0);
        }

        const auto good = std::ranges::find(files, std::string(GoodSave), &MCFileScrollPane::File::Name);
        REQUIRE(good != files.end());
        CHECK_EQ(good->Operation, 1);
        CHECK_EQ(good->Mission, 1);
        CHECK_EQ(good->ResourcePoints, static_cast<uint32_t>(ResourcePoints));
    }

    // The main menu as a start finds it (no campaign loaded): CONTINUE the good save.
    logistics->SetUpMainScreen(false);
    Settle();
    logistics->CurrentMission = -1;
    MCTestGuest::Panel(MCMainPanel::Main).Refresh();
    Settle();
    MCSaves& saves = MCSaves::Game();
    REQUIRE(saves.Latest().has_value());
    CHECK_EQ(saves.Latest()->get().Name, std::string(GoodSave));
    CHECK_EQ(MCTestGuest::Panel(MCMainPanel::Main).ContinueLine(), "Original campaign · Operation 1, mission 1");

    for (const std::string& name : damaged)
    {
        MCTest::Scope entry(name);
        const MCOptionalRef<const MCSaves::MCSave> save = saves.Find(name);
        REQUIRE(save.has_value());
        CHECK(!save->get().Read);
    }

    // CAMPAIGNS: the original's CONTINUE the good save; the expansion not started.
    const MCMainMenuModel::MCCampaignList campaigns = MCMainMenuModel::Campaigns();
    REQUIRE_EQ(campaigns.Rows.size(), size_t{2});
    CHECK_EQ(campaigns.Rows[0].Line, "MechCommander · Operation 1, mission 1");
    CHECK_EQ(campaigns.Latest[0], std::string(GoodSave));
    CHECK_EQ(campaigns.Rows[1].Line, "Desperate Measures · Not started");
    CHECK(campaigns.Latest[1].empty());

    // LOAD GAME: every save, the damaged ones by name alone; LOAD on one says it couldn't be loaded.
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    Settle();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& loadGame = MCSavesMenu::Screen()->get();
    REQUIRE_EQ(loadGame.Saves().size(), damaged.size() + 1);

    for (size_t row = 0; row < loadGame.Saves().size(); row++)
    {
        MCTest::Scope entry(loadGame.Saves()[row]);
        // A named save is listed by its name too, with its campaign, as every save the game can read.
        const bool good = loadGame.Saves()[row] == GoodSave;
        CHECK_EQ(loadGame.Rows()[row].Mission, loadGame.Saves()[row]);
        CHECK_EQ(loadGame.Rows()[row].Campaign, std::string(good ? "Original" : ""));
    }

    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    Settle();
    LoadFromScreen("Damaged, the campaign's end");
    REQUIRE(loadGame.Dialog());
    CHECK(loadGame.Dialog()->Shown());
    CHECK_EQ(loadGame.Dialog()->Saying().Question, std::string(MCSavesMenu::RefusedText));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    Settle();
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    Settle();

    // The good one plays.
    LoadFromScreen(GoodSave);
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(logistics->CurrentMission, goodMission);
    logistics->SetUpMainScreen(false);
    Settle();
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
        LoadFromScreen(name);
        REQUIRE(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    }

    /// <summary>
    /// DEPLOY's BEGIN MISSION, refused with <paramref name="units"/> owned over the limit of <paramref name="most"/>: no
    /// mission, and the message saying how many units to sell first.
    /// </summary>
    void BeginRefused(MCCampaignScreen& screen, int32_t units, int32_t most)
    {
        REQUIRE(MCRemadeTest::Press(screen, "begin"));
        Settle();
        CHECK(Scenario() == nullptr);
        MCRemadeTest::CloseDialog(MCFormatPrintf(LoadGameString(0x373, 0xfe), units, most, units - most));
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
/// room for more, and DEPLOY's BEGIN MISSION starts the mission; past 200 (a mission's salvage can take it there) BEGIN
/// MISSION asks to sell down to 200 first.
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
    LoadCampaignOwning(Save, 201);
    RemoveSave(Save);
    CHECK_EQ(NumUnits(), 201);
    MCCampaignScreen& screen = ShowDeploy();
    DeployFirstReady(screen);
    BeginRefused(screen, 201, 200);

    // Sold down to 60: room in the shop for 140 more, and the mission starts.
    SellDownTo(60);
    CHECK_EQ(MaxPurchase(-1), 140);
    CHECK(!CheckMaxUnits());
    CHECK(BeginMission(screen));
}

/// <summary>
/// A save holding more units than the original's 50 (60: one made with MoreUnits, loaded without it or by an older
/// build) loads whole, as the original's did a save taken past its limit by salvage: the shop sells nothing more, and
/// DEPLOY's BEGIN MISSION asks to sell down to 50 first, then starts the mission.
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
    MCRemadeTest::CloseDialog(LoadGameString(0x374, 0xfe));
    MCCampaignScreen& screen = ShowDeploy();
    DeployFirstReady(screen);
    BeginRefused(screen, 60, 50);
    SellDownTo(50);
    CHECK(BeginMission(screen));
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
