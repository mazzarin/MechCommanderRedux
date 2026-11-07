#include "stdafx.h"
#include "main/MCLogistics.h"
#include "ai/MCMoveGeometry.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCLogChatInput.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "main/MCWeaponBans.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"

namespace
{
    /// <summary>The counts read from a purchase file's Header block.</summary>
    struct MCPurchaseHeader
    {
        int32_t NumMechs = 0;
        int32_t NumVehicles = 0;
        int32_t NumComponents = 0;
        int32_t NumWarriors = 0;
        int32_t NumGifts = 0;
    };

    /// <summary>Opens purchase file <paramref name="name"/><c>.fit</c> under <c>MissionPath</c>.</summary>
    void OpenPurchaseFile(MCFitIniFile& file, std::string_view name, std::string_view error)
    {
        const int32_t result = file.Open(GamePath(MissionPath, name, ".fit"));
        Assert(result == 0, 0, error);
    }

    /// <summary>Reads the Header block of an open purchase file (NumGifts too when <paramref name="gifts"/>).</summary>
    MCPurchaseHeader ReadPurchaseHeader(MCFitIniFile& file, bool gifts)
    {
        MCPurchaseHeader header{};
        const int32_t result = file.SeekBlock("Header");
        Assert(result == 0, 0, " could not find header block in purchasing file ");
        header.NumMechs = ReadRequired<int32_t>(file, "NumMechs", " could not read NumMechs in purchasing file ");
        header.NumVehicles =
            ReadRequired<int32_t>(file, "NumVehicles", " could not read NumVehicles in purchasing file ");
        header.NumComponents =
            ReadRequired<int32_t>(file, "NumComponants", " could not read NumComponants in purchasing file ");
        header.NumWarriors =
            ReadRequired<int32_t>(file, "NumWarriors", " could not read NumWarriors in purchasing file ");

        if (gifts)
        {
            header.NumGifts = file.Read<int32_t>("NumGifts").value_or(0);
        }

        return header;
    }

    /// <summary>
    /// Adds the Gift# blocks' mechs and vehicles (all "pv" profiles) to the player's lists. A block without a count
    /// gives none and keeps the file name of the block before it, as the original's buffers did.
    /// </summary>
    void ReadPurchaseGifts(MCFitIniFile& file, int32_t numGifts, MCLogMechList& mechs, MCLogVehicleList& vehicles)
    {
        std::string fileName;
        int32_t numAvailable = 0;

        for (int32_t gift = 0; gift < numGifts; ++gift)
        {
            int32_t result = file.SeekBlock(std::format("Gift{}", gift));
            Assert(result == 0, 0, " could not find Gift block in purchasing file ");
            std::string giftType;
            Assert(ReadText(file, "GiftType", 2, giftType), 0, " No gift type ");
            bool read = ReadEntry(file, "NumAvailable", numAvailable);

            if (read)
            {
                read = ReadText(file, "Filename", 9, fileName);
            }

            Assert(read, 0, "Error reading Gift data ");
            const std::string profile = "pv" + fileName;
            const bool required = file.Read<bool>("Required").value_or(false);

            if (giftType.starts_with('V'))
            {
                for (int32_t i = 0; i < numAvailable; ++i)
                {
                    vehicles.AddVehicle(profile, required, false, true);
                }
            }
            else if (giftType.starts_with('M'))
            {
                for (int32_t i = 0; i < numAvailable; ++i)
                {
                    mechs.AddMech(profile, required, true, true);
                }
            }
        }
    }

    /// <summary>
    /// Adds the Mech# blocks (last first) to the shop. A variant without a count has none; its file name carries
    /// over from the block before, as the original's buffers did.
    /// </summary>
    void ReadPurchaseMechs(MCFitIniFile& file, int32_t numMechs, MCPurMechList& mechs)
    {
        std::string fileA;
        std::string fileJ;
        std::string fileW;
        int32_t countA = 0;
        int32_t countJ = 0;
        int32_t countW = 0;

        for (int32_t mech = numMechs - 1; mech >= 0; --mech)
        {
            const int32_t result = file.SeekBlock(std::format("Mech{}", mech));
            Assert(result == 0, 0, " could not find mech block in purchasing file ");

            if (ReadEntry(file, "TypeAAvailable", countA))
            {
                ReadText(file, "TypeAFile", 9, fileA);
            }

            if (ReadEntry(file, "TypeJAvailable", countJ))
            {
                ReadText(file, "TypeJFile", 9, fileJ);
            }

            if (ReadEntry(file, "TypeWAvailable", countW))
            {
                ReadText(file, "TypeWFile", 9, fileW);
            }

            mechs.AddMech(fileA, countA, fileJ, countJ, fileW, countW);
        }
    }

    /// <summary>
    /// Adds the Vehicle# blocks (last first) to the shop. <paramref name="prefix"/>: whether the file name gets "pv"
    /// in front (the multiplayer files; the campaign ones name the profile in full).
    /// </summary>
    void ReadPurchaseVehicles(MCFitIniFile& file, int32_t numVehicles, MCPurVehicleList& vehicles, bool prefix)
    {
        std::string fileName;
        int32_t numAvailable = 0;

        for (int32_t vehicle = numVehicles - 1; vehicle >= 0; --vehicle)
        {
            const int32_t result = file.SeekBlock(std::format("Vehicle{}", vehicle));
            Assert(result == 0, 0, " could not find vehicle block in purchasing file ");
            bool read = ReadEntry(file, "NumAvailable", numAvailable);

            if (read)
            {
                read = ReadText(file, "Filename", 9, fileName);
            }

            Assert(read, 0, "Error reading Purchasing vehicle data ");
            vehicles.AddVehicle(prefix ? "pv" + fileName : fileName, numAvailable);
        }
    }

    /// <summary>
    /// Adds the Componant# blocks to the shop's component list, each with its widgets. <paramref name="blockError"/>:
    /// the campaign files' message has a typo ("omponent") the multiplayer one lacks.
    /// </summary>
    void ReadPurchaseComponents(MCFitIniFile& file, int32_t numComponents, MCInventoryList& components,
                                std::string_view blockError)
    {
        for (int32_t component = 0; component < numComponents; ++component)
        {
            const int32_t result = file.SeekBlock(std::format("Componant{}", component));
            Assert(result == 0, static_cast<uint32_t>(result), blockError);
            const auto masterID =
                ReadRequired<uint8_t>(file, "ComponantID", " Could not find Purchasing Component masterID");
            const auto numAvailable =
                ReadRequired<int32_t>(file, "NumAvailable", " Could not find Purchasing Component Num Available");
            components.AddItem(
                masterID,
                components.CreateStat(static_cast<uint8_t>(component), 0, 0, static_cast<int16_t>(numAvailable), 0xff),
                true);
            components.LoadDescription(components.GetIndexFromMasterID(masterID), std::nullopt);
        }
    }

    /// <summary>
    /// Reads Warrior# block <paramref name="warrior"/>'s Profile (and Status into <paramref name="status"/> when it
    /// isn't null) and opens the profile's General block.
    /// </summary>
    std::string ReadPurchaseWarrior(MCFitIniFile& file, int32_t warrior, MCOptionalRef<int32_t> status,
                                    MCFitIniFile& pilotFile)
    {
        int32_t result = file.SeekBlock(std::format("Warrior{}", warrior));
        Assert(result == 0, static_cast<uint32_t>(result), " Could not find Purchasing Warrior block");
        std::string profile = ReadRequiredText(file, "Profile", 0x7f, " Could not find Purchasing Warrior profile");

        if (status.has_value())
        {
            status->get() = ReadRequired<int32_t>(file, "Status", " Could not find Purchasing Warrior Status");
        }

        result = pilotFile.Open(GamePath(WarriorPath, profile, ".fit"));
        Assert(result == 0, static_cast<uint32_t>(result), " could not open Purchasing Pilot profile file ");
        result = pilotFile.SeekBlock("General");
        Assert(result == 0, static_cast<uint32_t>(result), " Could find General Block in PIlot file ");
        return profile;
    }

    /// <summary>
    /// A pilot's status in the shop from the player's own copy of it (matched by DescIndex): 3 sold, 1 alive, 2 dead;
    /// <paramref name="status"/> when the player has none. The last match wins.
    /// </summary>
    int32_t OwnPilotStatus(const MCLogWarriorList& list, int32_t descIndex, int32_t status)
    {
        for (const std::shared_ptr<MCLogWarrior>& warrior : list.Warriors)
        {
            if (warrior->DescIndex == descIndex)
            {
                status = warrior->Sold ? 3 : (0.0f < warrior->Health ? 1 : 2);
            }
        }

        return status;
    }
}

auto MCLogistics::SetUpCampaignPurchasing(std::string_view purchaseFile, MCPacketFile& file) -> std::string
{
    MCFitIniFile purchasing;
    OpenPurchaseFile(purchasing, purchaseFile, " could not open purchasing file ");
    int32_t result = purchasing.SeekBlock("PurchaseCosts");
    Assert(result == 0, static_cast<uint32_t>(result), "Could not find PurchaseCosts block in purchasing file");
    ArmorCost = ReadRequired<int32_t>(purchasing, "Armor", "Could not read Armor in purchasing file");
    InternalCost = ReadRequired<int32_t>(purchasing, "Internal", "Could not read Internal in purchasing file");
    EngineCost = ReadRequired<int32_t>(purchasing, "Engine", "Could not read Engine in purchasing file");
    ClanCostFactor = ReadRequired<float>(purchasing, "clan", "Could not read clan in purchasing file.");
    result = purchasing.SeekBlock("PilotCosts");
    Assert(result == 0, static_cast<uint32_t>(result), "Could not find PilotCosts block in purchasing file");
    PilotCosts[0] = ReadRequired<int32_t>(purchasing, "Green", "Could not read Green pilot in purchasing file");
    PilotCosts[1] = ReadRequired<int32_t>(purchasing, "Regular", "Could not read Regular pilot in purchasing file");
    PilotCosts[2] = ReadRequired<int32_t>(purchasing, "Veteran", "Could not read Veteran pilot in purchasing file");
    PilotCosts[3] = ReadRequired<int32_t>(purchasing, "Elite", "Could not read Elite pilot in purchasing file.");
    CampaignBriefingName.clear();

    if (purchasing.SeekBlock("CampaignBriefing") == 0)
    {
        CampaignBriefingName =
            ReadRequiredText(purchasing, "Filename", 0x29, "Could not read Campaign Briefing cinema name");
    }

    // Original behaviour: a missing Mission block leaves the block before it current.
    purchasing.SeekBlock(std::format("Mission{}", CurrentMission));
    std::string missionPurchaseFile =
        ReadRequiredText(purchasing, "PurchaseFile", 0xf9, " could not read PurchaseFile in purchasing file");

    if (!ReadText(purchasing, "OperationCinema", 0x29, OperationCinema))
    {
        OperationCinema.clear();
    }

    AutoPlayMovie = purchasing.Read<int32_t>("AutoPlay").value_or(0) != 0;

    if (const MCFitResult<int32_t> operation = purchasing.Read<int32_t>("Operation"); operation.has_value())
    {
        Operation = *operation;
        BriefingScreen->OperationPicture = std::make_shared<MCLogPort>();
        BriefingScreen->OperationPicture->Load(CurPlanet == 0
                                                   ? std::format("{}logart\\lsb_op{}.tga", ArtPath, Operation)
                                                   : std::format("{}logart\\mcxcard{}.tga", ArtPath, Operation));
    }
    else
    {
        Operation = 0;
    }

    purchasing.Close();
    SetUpPurchasing(file);
    return missionPurchaseFile;
}

namespace
{
    /// <summary>Replaces the shop's lists (mechs, vehicles, pilots, components) with new, empty ones.</summary>
    void ResetShop(MCLogistics& logistics)
    {
        logistics.PurMechList = std::make_unique<MCPurMechList>();
        logistics.PurVehicleList = std::make_unique<MCPurVehicleList>();
        logistics.PurPilotList = std::make_unique<MCPurPilotList>();
        logistics.PurchaseComponents = std::make_unique<MCInventoryList>();
    }
}

auto MCLogistics::SetUpMPPurchasing(std::string_view purchaseFile) -> void
{
    MCFitIniFile purchasing;
    ResetShop(*this);
    OpenPurchaseFile(purchasing, purchaseFile, " could not open mission purchasing file ");
    const MCPurchaseHeader header = ReadPurchaseHeader(purchasing, true);
    ReadPurchaseGifts(purchasing, header.NumGifts, *MechList, *VehicleList);
    ReadPurchaseMechs(purchasing, header.NumMechs, *PurMechList);
    ReadPurchaseVehicles(purchasing, header.NumVehicles, *PurVehicleList, true);
    ReadPurchaseComponents(purchasing, header.NumComponents, *PurchaseComponents,
                           " Could not find Purchasing Component block");

    for (int32_t warrior = 0; warrior < header.NumWarriors; ++warrior)
    {
        MCFitIniFile pilotFile;
        const std::string profile = ReadPurchaseWarrior(purchasing, warrior, std::nullopt, pilotFile);
        const std::string callsign =
            ReadRequiredText(pilotFile, "Callsign", 0xff, " Could not find Callsign in General Block ");
        pilotFile.Close();

        // Only pilots the player does not already have are for hire.
        if (!WarriorList->Exists(callsign) && !AssignedWarriorList->Exists(callsign))
        {
            PurPilotList->AddPilot(profile, 0);
        }
    }

    // Port: the match's MOST SKILLED PILOT takes those more skilled off the shop, its weapon bans the banned weapons.
    HidePilotsAbove(MCMostSkilledPilot(MultiPlayer()->Played.Rules.value_or(0)));
    HideBannedWeapons(MCPlayedWeaponBans());
    purchasing.Close();
}

auto MCLogistics::HideBannedWeapons(uint32_t bans) const -> void
{
    if (bans == 0)
    {
        return;
    }

    // The banned components aren't sold.
    std::erase_if(PurchaseComponents->Items, [bans](const std::shared_ptr<MCLogInventoryItem>& item)
                  { return MCComponentBanned(item->MasterID, bans); });

    // A mech for sale comes without them, and costs that much less: their space is free for others.
    for (const std::shared_ptr<MCPurMech>& mech : PurMechList->Mechs)
    {
        for (const std::unique_ptr<MCPurMechData>& variant : mech->Variants)
        {
            if (variant != nullptr && variant->Inventory != nullptr)
            {
                variant->Cost -= MCRemoveBanned(*variant->Inventory, bans);
                variant->CalcBR();
            }
        }
    }

    // A vehicle can't be refitted, so one carrying a banned weapon isn't sold.
    std::erase_if(PurVehicleList->Vehicles,
                  [bans](const std::shared_ptr<MCPurVehicle>& vehicle)
                  {
                      return std::ranges::any_of(vehicle->Data->Inventory->Items,
                                                 [bans](const std::shared_ptr<MCLogInventoryItem>& item)
                                                 { return MCComponentBanned(item->MasterID, bans); });
                  });
}

auto MCLogistics::HidePilotsAbove(MCPilotRank most) const -> void
{
    for (const std::shared_ptr<MCPurPilotData>& pilot : PurPilotList->Pilots)
    {
        if (pilot->Rank > static_cast<int32_t>(most) && pilot->Status == MCPurPilotData::ForHire)
        {
            pilot->Status = MCPurPilotData::OffShop;
        }
    }
}

auto MCLogistics::SetUpPurchasing(MCPacketFile& file) -> void
{
    MCFitIniFile purchasing;
    ResetShop(*this);

    // The shop is the campaign file's last packet.
    file.SeekPacket(file.GetNumPackets() - 1);
    const int32_t size = file.GetPacketSize();
    Assert(size > 0, static_cast<uint32_t>(size), " Bad Purchase Data in Campaign File ");
    const int32_t result = purchasing.Open(file, static_cast<uint32_t>(size));
    Assert(result == 0, 0, " could not open mission purchasing file ");
    const MCPurchaseHeader header = ReadPurchaseHeader(purchasing, true);
    ReadPurchaseGifts(purchasing, header.NumGifts, *MechList, *VehicleList);
    ReadPurchaseMechs(purchasing, header.NumMechs, *PurMechList);
    ReadPurchaseVehicles(purchasing, header.NumVehicles, *PurVehicleList, false);
    ReadPurchaseComponents(purchasing, header.NumComponents, *PurchaseComponents,
                           " Could not find Purchasing omponent block");

    for (int32_t warrior = 0; warrior < header.NumWarriors; ++warrior)
    {
        int32_t status = 0;
        MCFitIniFile pilotFile;
        const std::string profile = ReadPurchaseWarrior(purchasing, warrior, status, pilotFile);
        const auto descIndex =
            ReadRequired<int32_t>(pilotFile, "DescIndex", " Could not find DescIndex in General Block ");
        pilotFile.Close();
        // A pilot the player has (or had) shows as sold, alive or dead.
        int32_t ownStatus = OwnPilotStatus(*WarriorList, descIndex, -1);

        if (ownStatus == -1)
        {
            ownStatus = OwnPilotStatus(*AssignedWarriorList, descIndex, -1);
        }

        PurPilotList->AddPilot(profile, ownStatus != -1 ? ownStatus : status);
    }

    purchasing.Close();
}

auto MCLogistics::SetUpOldPurchasing(std::string_view purchaseFile) const -> void
{
    MCFitIniFile purchasing;
    OpenPurchaseFile(purchasing, purchaseFile, " could not open mission purchasing file ");
    const MCPurchaseHeader header = ReadPurchaseHeader(purchasing, false);

    // Changes to the shop: the counts are added to what is there (the J and W variants' files are read, unused).
    std::string fileA;
    std::string unusedFile;
    int32_t countA = 0;
    int32_t countJ = 0;
    int32_t countW = 0;

    for (int32_t mech = header.NumMechs - 1; mech >= 0; --mech)
    {
        const int32_t result = purchasing.SeekBlock(std::format("Mech{}", mech));
        Assert(result == 0, 0, " could not find mech block in purchasing file ");

        if (ReadEntry(purchasing, "TypeAAvailable", countA))
        {
            ReadText(purchasing, "TypeAFile", 9, fileA);
        }

        if (ReadEntry(purchasing, "TypeJAvailable", countJ))
        {
            ReadText(purchasing, "TypeJFile", 9, unusedFile);
        }

        if (ReadEntry(purchasing, "TypeWAvailable", countW))
        {
            ReadText(purchasing, "TypeWFile", 9, unusedFile);
        }

        PurMechList->ModMech(fileA, countA, countJ, countW);
    }

    std::string fileName;
    int32_t numAvailable = 0;

    for (int32_t vehicle = header.NumVehicles - 1; vehicle >= 0; --vehicle)
    {
        const int32_t result = purchasing.SeekBlock(std::format("Vehicle{}", vehicle));
        Assert(result == 0, 0, " could not find vehicle block in purchasing file ");
        bool read = ReadEntry(purchasing, "NumAvailable", numAvailable);

        if (read)
        {
            read = ReadText(purchasing, "Filename", 9, fileName);
        }

        Assert(read, 0, "Error reading Purchasing vehicle data ");
        PurVehicleList->ModVehicle("pv" + fileName, numAvailable);
    }

    for (int32_t component = 0; component < header.NumComponents; ++component)
    {
        const int32_t result = purchasing.SeekBlock(std::format("Componant{}", component));
        Assert(result == 0, static_cast<uint32_t>(result), " Could not find Purchasing omponent block");
        const auto masterID =
            ReadRequired<uint8_t>(purchasing, "ComponantID", " Could not find Purchasing Component masterID");
        const auto count =
            ReadRequired<int32_t>(purchasing, "NumAvailable", " Could not find Purchasing Component Num Available");
        PurchaseComponents->AddCountToItem(count, masterID);
    }

    for (int32_t warrior = 0; warrior < header.NumWarriors; ++warrior)
    {
        int32_t status = -1;
        MCFitIniFile pilotFile;
        ReadPurchaseWarrior(purchasing, warrior, status, pilotFile);
        const auto descIndex =
            ReadRequired<int32_t>(pilotFile, "DescIndex", " Could not find DescIndex in General Block ");
        pilotFile.Close();

        // Status 4 takes a pilot for hire off the shop; status 0 puts one back.
        for (int32_t i = 0; i < PurPilotList->GetPilotCount(); ++i)
        {
            std::shared_ptr<MCPurPilotData> pilot;
            PurPilotList->GetPilotInfo(i, pilot);

            if (pilot->DescIndex == descIndex)
            {
                if (pilot->Status == 0 && status == 4)
                {
                    PurPilotList->SetPilotStatus(descIndex, 4);
                }

                if (pilot->Status == 4 && status == 0)
                {
                    PurPilotList->SetPilotStatus(descIndex, 0);
                }
            }
        }
    }

    purchasing.Close();
}
