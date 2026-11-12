#include "stdafx.h"
#include "main/MCLogistics.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "ai/MCMoveGeometry.h"
#include "logistics/MCLogChatInput.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSmuti.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCVehicleRepairBlock.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "main/MCSystemConfig.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "object/MCObjectTypeManager.h"
#include "vfx/MCVfxFunctions.h"

auto MCLogistics::LoadQuickStart(MCFitIniFile& file) -> void
{
    const int32_t homeTeam = MultiPlayer()->HomeTeam;
    CurDeployTonnage = 0;

    if (file.SeekBlock("HammerDown1") == 0)
    {
        HammerDown = true;
    }

    const int32_t result = file.SeekBlock(std::format("Side{}Units", homeTeam != 1 ? 1 : 0));
    Assert(result == 0, static_cast<uint32_t>(result), "Could not find SideUnit block in quickstart");
    MCBriefingScreen& briefing = *BriefingScreen;

    for (size_t slotIndex = 0; slotIndex < NumDropSlots; ++slotIndex)
    {
        const int32_t lance = static_cast<int32_t>(slotIndex / LanceSlots);
        const int32_t slot = static_cast<int32_t>(slotIndex % LanceSlots);
        DeploySlot& deploy = DeploySlots[slotIndex / LanceSlots][slotIndex % LanceSlots];

        if (!LocalDropSlot[slotIndex])
        {
            continue;
        }

        if (ReadRequired<uint32_t>(file, std::format("Slot{}UnitData", slotIndex),
                                   "could not read unitData in quickstart") == 0)
        {
            continue;
        }

        const auto type =
            ReadRequired<uint32_t>(file, std::format("Slot{}Type", slotIndex), "could not read unitType in quickstart");
        const std::string profileKey = std::format("Slot{}UnitProfile", slotIndex);

        if (type < 3)
        {
            // A mech: it goes at the head of the force, so every other mech's pilot index moves up one.
            for (const std::shared_ptr<MCLogMech>& other : ForceMechList->Mechs)
            {
                ++other->PilotIndex;
            }

            const std::string profile =
                ReadRequiredText(file, profileKey, 0xfe, "could not read mech profile string in quickstart");
            std::shared_ptr<MCLogMech> mech = ForceMechList->AddMech(profile, false, false, true);
            mech->Assigned = true;

            for (auto& lanceSlots : DeploySlots)
            {
                for (DeploySlot& other : lanceSlots)
                {
                    if (other.Unit >= 0)
                    {
                        ++other.Unit;
                    }
                }
            }

            const std::string pilotProfile = ReadRequiredText(file, std::format("Slot{}PilotProfile", slotIndex), 0xfe,
                                                              "could not read pilot profile string in quickstart");
            AssignedWarriorList->AddWarrior(pilotProfile, false);
            MCLogWarrior& warrior = *AssignedWarriorList->Warriors.front();
            warrior.Assigned = true;
            SetPilot(0, 0);
            const double tonnage = static_cast<double>(CurDeployTonnage) + mech->CurTonnage;

            if (HammerDown || tonnage <= static_cast<double>(MaxDeployTonnage))
            {
                CurDeployTonnage = static_cast<int32_t>(tonnage);
                SendAddMechMessage(*mech, lance, slot);
                warrior.DropLance = lance;
                warrior.DropSlot = slot;
                deploy.Unit = 0;
                mech->Deployed = true;
                warrior.Deployed = true;
            }
        }
        else
        {
            const std::string profile =
                ReadRequiredText(file, profileKey, 0xfe, "could not read vehicle profile string in quickstart");
            std::shared_ptr<MCLogVehicle> vehicle = ForceVehicleList->AddVehicle(profile, false, false, true);
            vehicle->Assigned = true;

            for (auto& lanceSlots : DeploySlots)
            {
                for (DeploySlot& other : lanceSlots)
                {
                    if (other.Vehicle >= 0)
                    {
                        ++other.Vehicle;
                    }
                }
            }

            const double tonnage = static_cast<double>(CurDeployTonnage) + vehicle->CurTonnage;

            if (HammerDown || tonnage <= static_cast<double>(MaxDeployTonnage))
            {
                CurDeployTonnage = static_cast<int32_t>(tonnage);
                SendAddVehicleMessage(*vehicle, lance, slot);
                vehicle->Deployed = true;
                deploy.Vehicle = 0;
            }
        }
    }

    // The units placed show in their slots (the original made each unit's block there as it placed it).
    briefing.ShowSlots();

    int32_t index = 0;

    for (const std::shared_ptr<MCLogMech>& mech : ForceMechList->Mechs)
    {
        mech->SlotIndex = index++;
    }

    for (const std::shared_ptr<MCLogVehicle>& vehicle : ForceVehicleList->Vehicles)
    {
        vehicle->SlotIndex = index++;
    }
}

auto MCLogistics::SaveCampaign(std::string_view fileName) -> int32_t
{
    return MCMissionLogisticsBridge::LogisticsSaveGame(fileName);
}

namespace
{
    /// <summary>
    /// The save's force entries of one kind: <c>count</c> blocks <paramref name="prefix"/>n (from
    /// <paramref name="first"/>), each naming a profile or a packet of the save. The original read the names into its
    /// block name buffer, so a profile name of 0x4f characters or more is taken as a packet entry.
    /// </summary>
    template <typename ProfileEntry, typename PacketEntry>
    void ReadForceEntries(MCFitIniFile& file, std::string_view prefix, int32_t first, uint32_t count,
                          std::string_view blockError, std::string_view packetError, ProfileEntry profileEntry,
                          PacketEntry packetEntry)
    {
        for (int32_t index = first; index < first + static_cast<int32_t>(count); index++)
        {
            std::string name = std::format("{}{}", prefix, index);
            int32_t result = file.SeekBlock(name);
            Assert(result == 0, 0, blockError);

            if (ReadText(file, "Profile", 0x4f, name))
            {
                profileEntry(name);
                continue;
            }

            const auto packet = ReadRequired<uint32_t>(file, "PacketNum", packetError);
            packetEntry(static_cast<int32_t>(packet + 1));
        }
    }

    /// <summary>The count of a force section (0 when the save has no such block).</summary>
    uint32_t ReadForceCount(MCFitIniFile& file, std::string_view block, std::string_view name, std::string_view error)
    {
        if (file.SeekBlock(block) != 0)
        {
            return 0;
        }

        return ReadRequired<uint32_t>(file, name, error);
    }

    /// <summary>A force entry's NumAvailable (1 when missing).</summary>
    int32_t CopiesOf(MCFitIniFile& file)
    {
        return file.Read<int32_t>("NumAvailable").value_or(1);
    }

    /// <summary>
    /// The Assigned flag of the [General] block of the profile in packet <paramref name="packet"/> (required, failing
    /// with <paramref name="readError"/>, when there is one).
    /// </summary>
    bool PacketAssigned(MCPacketFile& packetFile, int32_t packet, std::string_view seekError,
                        std::string_view openError, std::string_view blockError,
                        std::optional<std::string_view> readError)
    {
        MCFitIniFile profile;
        int32_t result = packetFile.SeekPacket(packet);
        Assert(result == 0, static_cast<uint32_t>(result), seekError);
        result = profile.Open(packetFile, packetFile.GetPacketSize());
        Assert(result == 0, static_cast<uint32_t>(result), openError);
        result = profile.SeekBlock("General");
        Assert(result == 0, static_cast<uint32_t>(result), blockError);

        if (readError.has_value())
        {
            return ReadRequired<bool>(profile, "Assigned", *readError);
        }

        return profile.Read<bool>("Assigned").value_or(false);
    }

    /// <summary>The mission tab's picture's width, the margin round its text, and the rows below the text.</summary>
    constexpr int32_t MissionPortWidth = 0xb3;
    constexpr int32_t MissionMargin = 2;
    constexpr int32_t MissionBottom = 10;
    constexpr int32_t MissionBack = 0x10;

    /// <summary>
    /// Port: the mission tab's content, <paramref name="text"/> (the briefing) formatted <paramref name="width"/> x
    /// <paramref name="height"/>: a view the mission pane draws each frame, so with HD art on its text shows sharp. The
    /// original formatted the text into a picture wiped to the key, copied that two pixels into one wiped to 0x10
    /// (MissionPortWidth wide, MissionBottom rows taller), which the pane copied keyed. The view draws the same: the
    /// margin in 0x10, the text over what the pane shows beneath, formatted by <paramref name="formatter"/> as it stood
    /// when the original formatted it.
    /// </summary>
    std::shared_ptr<MCLogPort> MissionTextPort(std::string text, const MCSmuti& formatter, int32_t width,
                                               int32_t height)
    {
        auto port = std::make_shared<MCLogPort>();
        const int32_t portHeight = height + MissionBottom;
        port->InitView(MissionPortWidth, portHeight);
        port->DrawContent = [text = std::move(text), formatter, width, height, portHeight](MCGuiPort& view)
        {
            const auto wipe = [&view](int32_t xPos, int32_t yPos, int32_t boxWidth, int32_t boxHeight)
            {
                if (boxWidth > 0 && boxHeight > 0)
                {
                    MCLogBlockPort box(view.Frame(), xPos, yPos, boxWidth, boxHeight, false);
                    VfxPaneWipe(box.Frame(), MissionBack);
                }
            };

            const int32_t right = MissionMargin + width;
            wipe(0, 0, MissionPortWidth, MissionMargin);
            wipe(0, MissionMargin + height, MissionPortWidth, portHeight - MissionMargin - height);
            wipe(0, MissionMargin, MissionMargin, height);
            wipe(right, MissionMargin, MissionPortWidth - right, height);
            MCLogBlockPort block(view.Frame(), MissionMargin, MissionMargin, width, height, true);
            MCSmuti writer = formatter;
            writer.Process(text, block, 0, 0);
        };

        return port;
    }
}

auto MCLogistics::LoadCampaign(std::string_view saveName, std::string_view extension, bool newCampaign, bool loadForce)
    -> int32_t
{
    const int32_t result = ReadCampaign(saveName, extension, newCampaign, loadForce);

    if (result != 0)
    {
        // Port fix: a refused save had emptied the lists (and taken its mission number, if it got that far), and the
        // campaign stayed open: the main menu's RETURN TO GAME showed the screens over the emptied lists, where a mech
        // whose pilot was gone, the shop never read, or the mission start's save crashed, and SAVE GAME saved them.
        // With no campaign, both wait for the next load.
        SDL_Log("MCLogistics: %.*s%.*s refused (%d); no campaign loaded", static_cast<int>(saveName.size()),
                saveName.data(), static_cast<int>(extension.size()), extension.data(), result);
        CurrentMission = -1;
    }

    return result;
}

auto MCLogistics::ReadCampaign(std::string_view saveName, std::string_view extension, bool newCampaign, bool loadForce)
    -> int32_t
{
    MCPacketFile packetFile;
    MCFitIniFile file;
    bool quickStart = false;
    BriefingScreen->ButtonsLocked = false;
    GuiSystem().ActivatePaletteFromTga(ArtPath + "logart\\lsrupm05.tga");

    // Start from empty lists and inventories.
    MechList->Clear();
    ForceMechList->Clear();
    WarriorList->Clear();
    AssignedWarriorList->Clear();
    VehicleList->Clear();
    ForceVehicleList->Clear();
    ComponentInventory = std::make_unique<MCInventoryList>();
    PurchaseComponents = std::make_unique<MCInventoryList>();

    if (PurMechList != nullptr)
    {
        PurMechList->Clear();
    }

    if (PurVehicleList != nullptr)
    {
        PurVehicleList->Clear();
    }

    if (PurPilotList != nullptr)
    {
        PurPilotList->Clear();
    }

    for (auto& lance : DeploySlots)
    {
        lance.fill({-1, -1});
    }

    // A save that can't be opened is refused (issue #25: the original went on reading it); the caller stays where it
    // is, and the next load fills the lists again.
    int32_t result = packetFile.Open(GamePath(SavePath, saveName, extension));
    Assert(result == 0, 0, " campaign file NOT Valid! ");

    if (result == 0)
    {
        result = packetFile.SeekPacket(0);
        Assert(result == 0, 0, " could not find initial campaign file ");
    }

    if (result == 0)
    {
        result = file.Open(packetFile, packetFile.GetPacketSize());
        Assert(result == 0, 0, " could not open initial campaign file ");
    }

    if (result != 0)
    {
        return result;
    }

    if (!newCampaign)
    {
        // The planet picks the master mission file (Solo play names it after the save).
        if (file.SeekBlock("Planet") == 0)
        {
            CurPlanet = ReadRequired<int32_t>(file, "Setting", " could not find Setting in Planet Block ");
        }
        else
        {
            CurPlanet = 0;
        }

        const std::string missionName =
            !Solo ? std::string(CurPlanet == 0 ? "mechcmdr1" : "xmechcmdr1") : std::format("campaign{}", saveName);
        MissionName = missionName;
        Mission()->ReloadCampaign(MissionName);
    }

    result = file.SeekBlock("General");
    Assert(result == 0, 0, " could not find General Block in campaign file ");

    if (MultiPlayer() == nullptr)
    {
        PurchaseFile = ReadRequiredText(file, "purchaseFile", 0x7f, " cound not read purchasing file in campain file ");
        ResetPlayerLights();
    }
    else
    {
        const std::string purchaseName = MPPurchaseFileName(file);
        MCFitIniFile purchasing;
        result = purchasing.Open(GamePath(MissionPath, purchaseName, ".fit"));
        Assert(result == 0, 0, " could not open purchasing file ");
        result = purchasing.SeekBlock("PilotCosts");
        Assert(result == 0, static_cast<uint32_t>(result), "Could not find PilotCosts block in purchasing file");
        PilotCosts[0] = ReadRequired<int32_t>(purchasing, "Green", "Could not read Green pilot in purchasing file");
        PilotCosts[1] = ReadRequired<int32_t>(purchasing, "Regular", "Could not read Regular pilot in purchasing file");
        PilotCosts[2] = ReadRequired<int32_t>(purchasing, "Veteran", "Could not read Veteran pilot in purchasing file");
        PilotCosts[3] = ReadRequired<int32_t>(purchasing, "Elite", "Could not read Elite pilot in purchasing file.");
    }

    RepairScreen->ClearSelection();
    int32_t savedMission = 0;

    if (MultiPlayer() == nullptr)
    {
        if (const MCFitResult<int32_t> number = file.Read<int32_t>("MissionNumber"); number.has_value())
        {
            // Issue #25: the number picks the campaign's scenario (or, one past the last, its end); one the campaign
            // doesn't have refuses the save.
            if (*number < 0 || static_cast<size_t>(*number) > Mission()->Scenarios.size())
            {
                SDL_Log("MCLogistics: %.*s names mission %d of %zu", static_cast<int>(saveName.size()), saveName.data(),
                        *number, Mission()->Scenarios.size());
                return READ_ERR;
            }

            savedMission = *number;
            CurrentMission = savedMission;
        }
        else
        {
            savedMission = -1;
        }

        result = file.SeekBlock("ResourcePoints");
        Assert(result == 0, 0, " could not find Resource Points ");
        ResourcePoints = static_cast<int32_t>(
            ReadRequired<uint32_t>(file, "numPoints", " Could not find resource points in campaign file "));
    }
    else
    {
        result = file.SeekBlock("Multiplayer");
        Assert(result == 0, 0, "This is not a multiplayer file!");
        MpMissionName = ReadRequiredText(file, "MissionName", 0x7f, "No mission file in save game file!");
        PlanningTime = file.Read<uint32_t>("PlanningTime").value_or(DefaultPlanningTime);

        // The team's resource points (typed on the session screen, or configured) are shared among its players.
        const int32_t teamPlayers = static_cast<int32_t>(MultiPlayer()->PlayersOnHomeTeam()->size());
        const MPMatchState& match = MultiPlayer()->Match;
        ResourcePoints = (MultiPlayer()->HomeTeam == 0 ? match.Team1Points : match.Team2Points) / teamPlayers;

        if (file.SeekBlock("MPQuickStart") == 0)
        {
            Mission()->CurrentScenario = -1;
            Mission()->CurrentMovie = 0;
            GetCurrentMission();
            LoadQuickStart(file);
            quickStart = true;
        }
    }

    if (MultiPlayer() == nullptr && !quickStart)
    {
        // The force: unassigned then assigned entries of each kind, numbered on from the unassigned ones. An entry
        // names a profile or a packet of this save.
        // Port fix: an assigned list whose unassigned block is missing starts at 0; the original started at a
        // leftover value (the extension pointer, the uninitialised count, or the mech loop's counter).
        const uint32_t numWarriors = ReadForceCount(file, "Warriors", "NumWarriors", " could not read warrior count ");
        ReadForceEntries(
            file, "Warrior", 0, numWarriors, " could not find warrior block ", " could not find warrior Data ",
            [&](const std::string& profile) { WarriorList->AddWarrior(profile, true); },
            [&](int32_t packet) { WarriorList->AddWarrior(packetFile, packet, true); });
        const uint32_t numAssignedWarriors =
            ReadForceCount(file, "AssWarriors", "NumAssWarriors", " could not read Assigned warrior count ");
        // Original behaviour: an assigned pilot given by profile joins the unassigned list.
        ReadForceEntries(
            file, "Warrior", static_cast<int32_t>(numWarriors), numAssignedWarriors, " could not find warrior block ",
            " could not find warrior Data ",
            [&](const std::string& profile) { WarriorList->AddWarrior(profile, true); },
            [&](int32_t packet) { AssignedWarriorList->AddWarrior(packetFile, packet, false); });

        const uint32_t numMechs = ReadForceCount(file, "Mechs", "NumMechs", " could not read mech count ");
        ReadForceEntries(
            file, "Mech", 0, numMechs, " could not find mech block ", " could not find Mech Data ",
            [&](const std::string& profile)
            {
                for (int32_t copy = CopiesOf(file); copy > 0; copy--)
                {
                    MechList->AddMech(profile, false, true, true);
                }
            },
            [&](int32_t packet)
            {
                for (int32_t copy = CopiesOf(file); copy > 0; copy--)
                {
                    MechList->AddMech(packetFile, packet);
                }
            });
        const uint32_t numAssignedMechs =
            ReadForceCount(file, "AssMechs", "NumAssMechs", " could not read assigned mech count ");
        // Original behaviour: an assigned mech given by profile joins the unassigned list.
        ReadForceEntries(
            file, "Mech", static_cast<int32_t>(numMechs), numAssignedMechs, " could not find mech block ",
            " could not find Mech Data ",
            [&](const std::string& profile) { MechList->AddMech(profile, false, true, true); },
            [&](int32_t packet) { ForceMechList->AddMech(packetFile, packet); });

        const uint32_t numVehicles = ReadForceCount(file, "Vehicles", "NumVehicles", " could not read vehicle count ");
        ReadForceEntries(
            file, "Vehicle", 0, numVehicles, " could not find Vehicle block ", " could not find vehicle Data ",
            [&](const std::string& profile)
            {
                for (int32_t copy = CopiesOf(file); copy > 0; copy--)
                {
                    VehicleList->AddVehicle(profile, false, false, true);
                }
            },
            [&](int32_t packet)
            {
                const int32_t copies = CopiesOf(file);
                // The packet's [General] Assigned says which list the vehicle goes to.
                const bool assigned =
                    PacketAssigned(packetFile, packet, " Vehicle Packet Not Found ", " Vehicle file could not open ",
                                   "Failed General Block in Vehicle", std::nullopt);
                MCLogVehicleList& list = assigned ? *ForceVehicleList : *VehicleList;

                for (int32_t copy = 0; copy < copies; copy++)
                {
                    result = packetFile.SeekPacket(packet);
                    Assert(result == 0, 0, " Vehicle Packet Not Found ");
                    list.AddVehicle(packetFile, packet);
                }
            });
        const uint32_t numAssignedVehicles =
            ReadForceCount(file, "AssVehicles", "NumAssVehicles", " could not read vehicle count ");
        ReadForceEntries(
            file, "Vehicle", static_cast<int32_t>(numVehicles), numAssignedVehicles, " could not find Vehicle block ",
            " could not find vehicle Data ",
            [&](const std::string& profile) { VehicleList->AddVehicle(profile, false, false, true); },
            [&](int32_t packet)
            {
                std::shared_ptr<MCLogVehicle> vehicle = ForceVehicleList->AddVehicle(packetFile, packet);

                if (loadForce || newCampaign)
                {
                    vehicle->Deployed = false;
                }
            });
    }

    // Every component the game knows (allcomp.fit), with no copies.
    {
        MCFitIniFile allComponents;
        result = allComponents.Open(GamePath(ObjectPath, "allcomp", ".fit"));
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find allcomp.fit ");
        result = allComponents.SeekBlock("Components");
        Assert(result == 0, 0, " could not read component block ");
        const auto count = ReadRequired<uint32_t>(allComponents, "NumComponents", " could not read component count ");

        for (int32_t index = 0; index < static_cast<int32_t>(count); index++)
        {
            result = allComponents.SeekBlock(std::format("Componant{}", index));
            Assert(result == 0, 0, " could not read component entry in allcomp ");
            const auto masterID =
                ReadRequired<uint8_t>(allComponents, "ComponantID", " could not read component entry in allcomp ");
            ComponentInventory->AddItem(
                masterID, ComponentInventory->CreateStat(static_cast<uint8_t>(index), 0, 0, 0, 0xff), true);
            ComponentInventory->LoadDescription(ComponentInventory->GetIndexFromMasterID(masterID), std::nullopt);
        }
    }

    // The save's components: the counts of the known ones, and any new ones.
    if (file.SeekBlock("Components") == 0)
    {
        const auto count = ReadRequired<uint32_t>(file, "NumComponents", " could not read component count ");

        for (int32_t index = 0; index < static_cast<int32_t>(count); index++)
        {
            // Original behaviour: a bad component entry ends the load here, returning the error.
            result = file.SeekBlock(std::format("Componant{}", index));

            if (result != 0)
            {
                return result;
            }

            const MCFitResult<uint8_t> masterID = file.Read<uint8_t>("ComponantID");

            if (!masterID.has_value())
            {
                return std::to_underlying(masterID.error());
            }

            const MCFitResult<int32_t> available = file.Read<int32_t>("NumAvailable");

            if (!available.has_value())
            {
                return std::to_underlying(available.error());
            }

            MCInventoryList& inventory = *ComponentInventory;

            if (inventory.GetIndexFromMasterID(*masterID) == -1)
            {
                inventory.AddItem(
                    *masterID,
                    inventory.CreateStat(static_cast<uint8_t>(index), 0, 0, static_cast<int16_t>(*available), 0xff),
                    true);
                inventory.LoadDescription(inventory.GetIndexFromMasterID(*masterID), std::nullopt);
            }
            else
            {
                inventory.AddCountToItem(*available, *masterID);
            }
        }
    }

    // Time passes between missions: the pilots heal.
    if (!newCampaign && !loadForce)
    {
        AssignedWarriorList->Heal(1);
        WarriorList->Heal(2);
    }

    if (CurrentMission == savedMission || newCampaign || MultiPlayer() != nullptr)
    {
        // The current scenario names a scenario of the campaign, which the briefing and the mission start read.
        if (MultiPlayer() == nullptr &&
            (CurrentMission < 0 || static_cast<size_t>(CurrentMission) >= Mission()->Scenarios.size()))
        {
            SDL_Log("MCLogistics: mission %d is past the campaign's %zu", CurrentMission, Mission()->Scenarios.size());
            return READ_ERR;
        }

        Mission()->CurrentScenario = CurrentMission;
        Mission()->CurrentMovie = CurrentMission + 1;
        GetCurrentMission();
    }
    else
    {
        // Coming back from a mission: apply its results (the "<mission>.pkk" save the mission wrote).
        const std::string& resultName = CurrentMission - 1 == -1 ? Mission()->Scenarios[Mission()->CurrentScenario]
                                                                 : Mission()->Scenarios[CurrentMission - 1];
        MCPacketFile resultFile;
        result = resultFile.Open(GamePath(SavePath, resultName, ".pkk"));

        if (result != 0)
        {
            return result;
        }

        result = resultFile.SeekPacket(0);
        Assert(result == 0, 0, " could not find mission result file ");
        // Port fix: the original reopened the campaign FitIniFile without closing it first.
        file.Close();
        result = file.Open(resultFile, resultFile.GetPacketSize());
        Assert(result == 0, 0, " could not open mission result file ");
        result = file.SeekBlock("General");
        Assert(result == 0, 0, " could not find General Block in mission file ");
        PurchaseFile = ReadRequiredText(file, "purchaseFile", 0x7f, " cound not read purchasing file in campain file ");
        result = file.SeekBlock("ResourcePoints");
        Assert(result == 0, 0, " could not find Resource Points ");
        const auto points =
            ReadRequired<uint32_t>(file, "numPoints", " Could not find resource points in mission file ");
        ResourcePoints = static_cast<int32_t>(points + static_cast<uint32_t>(ResourcePoints));

        const uint32_t numWarriors = ReadForceCount(file, "Warriors", "NumWarriors", " could not read warrior count ");
        ReadForceEntries(
            file, "Warrior", 0, numWarriors, " could not find warrior block ", " could not find warrior Data ",
            [](const std::string&) { Assert(false, 0, " Somehow game write out a profile instead of a packet ! "); },
            [&](int32_t packet)
            {
                // A pilot not in the list (5) joins it.
                if (AssignedWarriorList->ReplaceWarrior(resultFile, packet) == 5)
                {
                    AssignedWarriorList->AddWarrior(resultFile, packet, false);
                }
            });

        const uint32_t numMechs = ReadForceCount(file, "Mechs", "NumMechs", " could not read mech count ");
        static constexpr std::string_view assignedError =
            "could not find Assigned variable in [General] block in mech packet in save file";
        ReadForceEntries(
            file, "Mech", 0, numMechs, " could not find mech block ", " could not find Mech Data ",
            [&](const std::string& profile) { ForceMechList->AddMech(profile, false, true, true); },
            [&](int32_t packet)
            {
                const bool assigned =
                    PacketAssigned(resultFile, packet, "could not find mech packet in save file",
                                   "could not open mech packet in save file",
                                   "could not find [General] block in mech packet in save file", assignedError);

                // An assigned mech replaces its copy in the force; one not there (5), or an unassigned one, is added.
                if (assigned && ForceMechList->ReplaceMech(resultFile, packet) != 5)
                {
                    return;
                }

                (assigned ? ForceMechList : MechList)->AddMech(resultFile, packet);
            });

        // Force vehicles that were deployed are gone (the head of the list only).
        while (!ForceVehicleList->Vehicles.empty() && ForceVehicleList->Vehicles.front()->Deployed)
        {
            ForceVehicleList->RemoveVehicle(0);
        }

        // Salvaged mechs (not yet the player's) take the first pilot indexes; the others' move up past them.
        const auto salvaged = static_cast<int32_t>(std::ranges::count_if(
            ForceMechList->Mechs, [](const std::shared_ptr<MCLogMech>& mech) { return mech->NotMineYet; }));
        ShiftPilots(0, salvaged);
        int32_t nextPilot = 0;

        for (const std::shared_ptr<MCLogMech>& mech : ForceMechList->Mechs)
        {
            if (mech->NotMineYet)
            {
                mech->NotMineYet = false;
                mech->PilotIndex = nextPilot++;
            }
        }

        // Mechs whose pilot ejected (and lives), then those whose pilot died, leave the force with their pilot; the
        // scan restarts after each.
        auto releaseMechs = [&](auto leaves, bool modifierFirst)
        {
            for (int32_t index = 0; index < ForceMechList->GetMechCount();)
            {
                std::shared_ptr<MCLogMech> mech;
                ForceMechList->GetMechInfo(index, mech);
                std::shared_ptr<MCLogWarrior> pilot;
                AssignedWarriorList->GetWarriorInfo(mech->PilotIndex, pilot);
                Assert(pilot != nullptr, 0, " Warrior in an assigned mech is NULL ");

                if (!leaves(*pilot))
                {
                    index++;
                    continue;
                }

                std::shared_ptr<MCLogMech> released = ForceMechList->ExtractMech(index);
                std::shared_ptr<MCLogWarrior> releasedPilot = AssignedWarriorList->ExtractWarrior(released->PilotIndex);
                ShiftPilots(released->PilotIndex, -1);
                released->PilotIndex = -1;
                released->Deployed = false;
                released->Assigned = false;
                releasedPilot->Ejected = false;
                releasedPilot->Assigned = false;
                MCLogMech& moved = *released;
                MechList->AddMech(std::move(released), true);

                if (modifierFirst)
                {
                    moved.CalcPilotModifier();
                }

                WarriorList->AddWarrior(std::move(releasedPilot), true);

                if (!modifierFirst)
                {
                    moved.CalcPilotModifier();
                }

                index = 0;
            }
        };

        releaseMechs([](const MCLogWarrior& pilot) { return pilot.Ejected && pilot.Health > 0.0f; }, true);
        releaseMechs([](const MCLogWarrior& pilot) { return pilot.Health == 0.0f; }, false);

        if (file.SeekBlock("Components") == 0)
        {
            const auto count = ReadRequired<uint32_t>(file, "NumComponents", " could not read component count ");

            for (int32_t index = 0; index < static_cast<int32_t>(count); index++)
            {
                result = file.SeekBlock(std::format("Componant{}", index));
                Assert(result == 0, static_cast<uint32_t>(result), "Could not find Component Block");
                const auto masterID = ReadRequired<uint8_t>(file, "ComponantID", "Could not find Component Master ID");
                const auto available =
                    ReadRequired<int32_t>(file, "NumAvailable", "Could not find Component numAvailable");
                ComponentInventory->AddCountToItem(available, masterID);
            }
        }

        if (!loadForce)
        {
            AssignedWarriorList->Heal(1);
        }

        resultFile.Close();
    }

    if (MultiPlayer() == nullptr)
    {
        // The purchase options of this point in the campaign, and an automatic save when a new mission starts.
        MCFitIniFile masterFile;
        result = masterFile.Open(GamePath(MissionPath, MissionName, ".fit"));
        Assert(result == 0, 0, " could not open master mission file ");
        result = masterFile.SeekBlock("OpInfo");
        Assert(result == 0, 0, " could not find operation information in master mission file");
        const auto operationNumber =
            ReadRequired<int32_t>(masterFile, std::format("Scenario{}Operation", LastLogisticsMissionState),
                                  " could not find operation number in master mission file ");
        const auto missionNumber =
            ReadRequired<int32_t>(masterFile, std::format("Scenario{}Mission", LastLogisticsMissionState),
                                  " could not find mission number in master mission file ");
        const std::string oldPurchaseFile = SetUpCampaignPurchasing(PurchaseFile, packetFile);

        if (LastLogisticsMissionState < CurrentMission && !loadForce)
        {
            SetUpOldPurchasing(oldPurchaseFile);
            // "Operation %d Mission %d" (a printf format from the string table).
            const std::string format = LoadGameString(CurPlanet == 0 ? 0x37a : 0x386, 199);
            SaveCampaign(MCFormatPrintf(format.c_str(), operationNumber, missionNumber));
        }

        // Killed pilots leave the roster and can't be hired again.
        for (int32_t index = 0; index < WarriorList->GetWarriorCount();)
        {
            std::shared_ptr<MCLogWarrior> pilot;
            WarriorList->GetWarriorInfo(index, pilot);

            if (pilot == nullptr || pilot->WarriorStatus != MCLogWarrior::StatusKilled)
            {
                index++;
                continue;
            }

            const int32_t descIndex = pilot->DescIndex;

            // Original behaviour (OB-098): the pilot is removed by its id compared as a byte. Port fix: an id
            // the byte doesn't hold (256 or more) found no pilot, and the scan began again for ever; it moves on.
            if (WarriorList->RemoveWarrior(static_cast<uint8_t>(pilot->Id)) != 0)
            {
                index++;
                continue;
            }

            PurPilotList->SetPilotStatus(descIndex, 2);
            index = 0;
        }

        MCPurchaseScreen::CreatePurVehiclePane(false);
    }
    else
    {
        SetUpMPPurchasing(PurchaseFile);
        MCPurchaseScreen::CreatePurVehiclePane(false);
    }

    packetFile.Close();
    MCLogInvScreen& screen = *RepairScreen;
    MCLogInvScreen::CreateMechInvBlock();
    screen.CreatePilotInvBlock();
    MCLogInvScreen::CreateCompInvBlock();
    MCLogInvScreen::CreateVhclInvBlock();
    screen.SetUpMechInv(true, true);
    screen.CreateVehiclePane();
    return 0;
}

auto MCLogistics::GetCurrentMission() -> void
{
    // Port: the original allocated the FitIniFile and leaked it when the mission file would not open.
    MCFitIniFile file;
    const std::string& fileName =
        MultiPlayer() == nullptr ? Mission()->Scenarios[Mission()->CurrentScenario] : MpMissionName;

    if (file.Open(GamePath(MissionPath, fileName, ".fit")) != 0)
    {
        return;
    }

    int32_t result = file.SeekBlock("Campaign");
    Assert(result == 0, 0, " Could not find Campaign block in mission file ");
    MaxDeployTonnage =
        ReadRequired<int32_t>(file, "MaxTonnage", " Could not find MaxTonnage variable in mission file ");
    std::string briefingFile;

    if (MultiPlayer() == nullptr)
    {
        briefingFile =
            ReadRequiredText(file, "BriefingFile", 0x7f, " Could not find BriefingFile variable in mission file ");
    }
    else
    {
        // Each player on the team gets an equal share of the tonnage.
        MaxDeployTonnage /= static_cast<int32_t>(MultiPlayer()->PlayersOnHomeTeam()->size());
        briefingFile = ReadRequiredText(file, MultiPlayer()->HomeTeam == 0 ? "ISBriefingFile" : "ClanBriefingFile",
                                        0x7f, " Could not find BriefingFile variable in mission file ");
    }

    // Format the briefing text into a port the width of the mission pane (at least 0xbf high), then copy it into
    // the briefing screen's mission port with a 2-pixel margin.
    MCBriefingScreen& briefing = *BriefingScreen;
    const int32_t paneWidth = briefing.MissionPane->Width();
    // The texts below are read into the briefing path's buffer, as the original did: a missing one leaves the path.
    std::string text = GamePath(MissionPath, briefingFile);
    const int32_t height = std::max(GuiSystem().TextFormatter.ProcessFile(text, std::nullopt, paneWidth - 0x11), 0xbf);
    {
        // The port draws the text in place (MissionTextPort), formatted as the formatter stands now; the formatting
        // into a picture is kept for the state it leaves the formatter in.
        const MCSmuti formatter = GuiSystem().TextFormatter;
        MCLogPort textPort;
        textPort.Init(paneWidth - 0x11, height);
        VfxPaneWipe(textPort.Frame(), 0xff);
        GuiSystem().TextFormatter.ProcessFile(text, textPort, 0);
        briefing.MissionPort = MissionTextPort(MCSmuti::ReadFile(text), formatter, paneWidth - 0x11, height);
    }

    Assert(ReadText(file, "MapFile", 0xff, text), 0, " Could not find MapFile variable in mission file ");
    MissionFileName = text;
    const auto numDropZones =
        ReadRequired<int32_t>(file, "NumDropZones", " Could not read NumDropZones variable in mission file ");

    if (MultiPlayer() == nullptr)
    {
        LocalDropSlot.fill(false);
    }

    Assert(numDropZones < 7, 0, "Too many drop zones");

    // Port fix: a seventh or later zone wrote past the drop zone tables; it is left out.
    for (size_t zone = 0; zone < std::min(static_cast<size_t>(std::max(numDropZones, 0)), MaxDropZones); zone++)
    {
        file.SeekBlock(std::format("DropZone{}", zone));
        const auto numSlots =
            ReadRequired<int32_t>(file, "NumSlots", " Could not read NumSlots variable in mission file ");

        // Single player: the zone's slots are the ones the player may fill (a zone is a lance of four; more than four
        // slots run on into the next lance's).
        // Port fix: the original wrote the marks of a fourth or later zone past localDropSlot, over the drop zone
        // positions already read; the port only marks the three lances.
        if (MultiPlayer() == nullptr && zone < NumLances)
        {
            for (size_t slot = zone * LanceSlots;
                 slot < zone * LanceSlots + static_cast<size_t>(std::max(numSlots, 0)) && slot < NumDropSlots; slot++)
            {
                LocalDropSlot[slot] = true;
            }
        }

        DropZonePositions[zone].X =
            ReadRequired<float>(file, "PositionX", " Could not read PositionX variable in mission file ");
        DropZonePositions[zone].Y =
            ReadRequired<float>(file, "PositionY", " Could not read PositionY variable in mission file ");

        for (size_t slot = 0; slot < LanceSlots; slot++)
        {
            if (MultiPlayer() == nullptr && (zone >= NumLances || !LocalDropSlot[zone * LanceSlots + slot]))
            {
                break;
            }

            DeploySlotInfo& info = DeploySlotPlacements[zone][slot];
            info.OffsetX = ReadRequired<float>(file, std::format("OffsetX{}", slot),
                                               " Could not read OffsetX block in mission file ");
            info.OffsetY = ReadRequired<float>(file, std::format("OffsetY{}", slot),
                                               " Could not read OffsetY block in mission file ");
            info.Rotation = ReadRequired<float>(file, std::format("Rotation{}", slot),
                                                " Could not read Rotation block in mission file ");
        }
    }

    file.Close();
    BriefingScreen->DrawBackground();
}
