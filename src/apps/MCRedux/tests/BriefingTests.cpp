#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMechBriefBlock.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "logistics/MCUnitLimits.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"

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
        REQUIRE(mech.BriefBlock != nullptr);
        MCMechBriefBlock& block = *mech.BriefBlock;
        Drag(block.GlobalX() + block.Width() / 2, block.GlobalY() + block.Height() / 2, x, y);
        Settle();
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
    MCScrollPane& pane = *briefing.DeployPane;
    REQUIRE(pane.NumberOfChildren() > 0);
    std::shared_ptr<MCLogMech> mech = std::static_pointer_cast<MCMechBriefBlock>(pane.Child(0))->Mech();
    REQUIRE(mech != nullptr);
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
    CHECK(briefing.SlotBlocks[0].lock() == mech->BriefBlock);
    CHECK(mech->BriefBlock->ParentObject().get() == &briefing);
    CHECK(std::ranges::find(briefing.UndeployedMechs, logistics->ForceMechList->GetMechIndex(mech)) ==
          briefing.UndeployedMechs.end());

    // Back to the deploy pane: the slot is empty again.
    DragMech(*mech, paneMiddle.x, paneMiddle.y);
    CHECK(mech->Deployed == 0);
    CHECK_EQ(slot0.Unit, -1);
    CHECK_EQ(CurDeployTonnage, startTons);
    CHECK(briefing.SlotBlocks[0].expired());
    CHECK(mech->BriefBlock->ParentObject().get() == &pane);

    // A ton over the limit: refused with the message, the mech stays in the pane.
    MaxDeployTonnage = startTons + tons - 1;
    DragMech(*mech, middle.x, middle.y);
    CHECK(mech->Deployed == 0);
    CHECK_EQ(slot0.Unit, -1);
    CHECK_EQ(CurDeployTonnage, startTons);
    CHECK(logistics->MessageDialog->ShowWindow != 0);
    CHECK(mech->BriefBlock->ParentObject().get() == &pane);
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
    NewCampaign();
    Settle();
    std::weak_ptr<MCLogistics> logistics = GlobalLog();
    std::weak_ptr<MCBriefingScreen> briefing;
    {
        // Pinned only for the set-up: the launch takes logistics down.
        const std::shared_ptr<MCLogistics> pinned = GlobalLog();
        briefing = pinned->BriefingScreen;
        REQUIRE(pinned->CurrentScreen() == pinned->BriefingScreen);
        MCScrollPane& pane = *pinned->BriefingScreen->DeployPane;
        REQUIRE(pane.NumberOfChildren() > 0);
        std::shared_ptr<MCLogMech> mech = std::static_pointer_cast<MCMechBriefBlock>(pane.Child(0))->Mech();
        REQUIRE(mech != nullptr);
        MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(mech->CurTonnage);
        DragMech(*mech, SlotMiddle(0).x, SlotMiddle(0).y);
        REQUIRE(mech->Deployed != 0);
    }

    // The launch button.
    RealClick(0x230, 0x152);

    for (int32_t frame = 0; frame < 300 && Scenario() == nullptr; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    REQUIRE(Scenario() != nullptr);
    CHECK(Mission()->State == MCMissionState::Scenario);
    CHECK(GlobalLog() == nullptr);
    CHECK(logistics.expired());
    CHECK(briefing.expired());
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
