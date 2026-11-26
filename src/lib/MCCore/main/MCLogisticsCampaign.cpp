#include "stdafx.h"
#include "main/MCLogistics.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "ai/MCMoveGeometry.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "main/MCSystemConfig.h"
#include "main/MCGameStrings.h"
#include "main/MCTelemetry.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "object/MCObjectTypeManager.h"
#include "vfx/MCVfxFunctions.h"
#include "sprite/MCHdArt.h"
#include "platform/MCFrameLog.h"

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

    // The units placed show in their slots (the original made each unit's block there as it placed it; DEPLOY shows
    // the slots as they are).
    NumberForce();
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

    /// <summary>
    /// Port fix: the most copies a save's unit entry holds (NumAvailable), far more than a campaign could buy, and few
    /// enough that making them stays quick (a save asking 2^31 hung the load making them).
    /// </summary>
    constexpr int32_t MostCopies = 1000;

    /// <summary>
    /// A unit entry's NumAvailable (1 when missing); none past MostCopies, setting <paramref name="tooMany"/> (the
    /// save is then refused).
    /// </summary>
    int32_t CopiesOf(MCFitIniFile& file, bool& tooMany)
    {
        const int32_t copies = file.Read<int32_t>("NumAvailable").value_or(1);

        if (copies > MostCopies)
        {
            tooMany = true;
            return 0;
        }

        return copies;
    }

    /// <summary>
    /// Port fix: the most a save's packet (its settings, a unit's or pilot's profile, a mission's results) unpacks to:
    /// over a thousand times the largest in the game's own saves, starts and results (13.6 KB), so no real save meets
    /// it (MCPacketFile::SetMostUnpacked).
    /// </summary>
    constexpr int32_t MostSavePacket = 16 << 20;

    /// <summary>Opens the save (or a mission's results) at <paramref name="path"/>, its packets held to MostSavePacket.</summary>
    int32_t OpenSave(MCPacketFile& file, const std::string& path)
    {
        const int32_t result = file.Open(path);
        file.SetMostUnpacked(MostSavePacket);
        return result;
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

    /// <summary>The scale of a match's drop zone marks for each world meter across the map's diagonal (1998's).</summary>
    constexpr float MarkerScalePerMeter = 0.0017667845f;
    /// <summary>The Clans' first drop zone (MultiPlayer homeTeam 1): the last three.</summary>
    constexpr int32_t ClanFirstZone = 3;
    /// <summary>The home team the Clans are.</summary>
    constexpr int32_t ClanHomeTeam = 1;

    /// <summary>Entry <paramref name="name"/> of a terrain file's TerrainData block; a missing one is reported.</summary>
    template <MCFitValue T> T ReadTerrainEntry(MCFitIniFile& file, std::string_view name)
    {
        return ReadRequired<T>(file, name, std::format("Could not find variable {} in terrain .FIT file", name));
    }

    /// <summary>
    /// The drop zones' marks of the mission on map <paramref name="mapFile"/>, as 1998's briefing worked them out as it
    /// drew the mission's map (MCBriefingScreen::DrawBackground): the scale from its terrain file, the zone in a match,
    /// the map picture's size.
    /// </summary>
    MCLogistics::MCZoneMarks ReadZoneMarks(std::string_view mapFile)
    {
        MCFitIniFile terrainFile;
        int32_t result = terrainFile.Open(GamePath(TerrainPath, mapFile, ".fit"));
        Assert(result == 0, result, "Could not find terrain file from .TGA ");
        result = terrainFile.SeekBlock("TerrainData");
        Assert(result == 0, result, "Could not find TerrainData block in terrain .FIT file");
        const auto verticesBlockSide = ReadTerrainEntry<int32_t>(terrainFile, "VerticesBlockSide");
        const auto blocksMapSide = ReadTerrainEntry<int32_t>(terrainFile, "BlocksMapSide");
        const auto metersPerVertex = ReadTerrainEntry<float>(terrainFile, "MetersPerVertex");
        terrainFile.Close();
        MCLogistics::MCZoneMarks marks;

        if (MultiPlayer() != nullptr)
        {
            const auto side = static_cast<double>(blocksMapSide * verticesBlockSide);
            marks.Scale = static_cast<float>(std::sqrt(side * side + side * side) * metersPerVertex *
                                             static_cast<double>(MarkerScalePerMeter));
            marks.Zone = MultiPlayer()->HomeTeam == ClanHomeTeam ? ClanFirstZone : 0;
        }

        const std::optional<MCLogistics::MCPicturePixels> picture =
            MCLogistics::MapPictureAt(GamePath(TerrainPath, mapFile, ".log.tga"));

        if (!picture.has_value())
        {
            SDL_Log("MCLogistics: the briefing map of %.*s can't be read", static_cast<int>(mapFile.size()),
                    mapFile.data());
            return marks;
        }

        marks.MapWidth = picture->Width;
        marks.MapHeight = picture->Height;
        return marks;
    }

    /// <summary>Notes <paramref name="reason"/> as why the load refused its file, and gives back its error.</summary>
    int32_t Refused(MCTelemetry::MCRefusedLoad& read, MCTelemetry::MCLoadRefusal reason, int32_t result)
    {
        read.Reason = reason;
        return result;
    }
}

struct MCLogistics::MCCampaignReading
{
    MCCampaignReading(MCLogistics& logistics, std::string_view saveName, std::string_view extension, bool newCampaign,
                      bool loadForce);
    MCCampaignReading(const MCCampaignReading&) = delete;
    MCCampaignReading& operator=(const MCCampaignReading&) = delete;
    ~MCCampaignReading();

    std::weak_ptr<MCLogistics> Logistics;
    std::string SaveName;
    std::string Extension;
    bool NewCampaign = false;
    bool LoadForce = false;
    MCTelemetry::MCRefusedLoad Read;
    MCPacketFile PacketFile;
    MCFitIniFile File;
    /// <summary>A match's quick start, read with the save's start: there is no force to read.</summary>
    bool QuickStart = false;
    /// <summary>The save's mission number (-1 with none): a campaign's further on comes back from that mission.</summary>
    int32_t SavedMission = 0;
    /// <summary>The refusal's error; 0 while the reading goes on.</summary>
    int32_t Result = 0;
    /// <summary>The shop as its reading goes (a campaign's, not a match's); none before and after.</summary>
    std::shared_ptr<MCShopReading> Shop;
    /// <summary>A force entry asked for more copies than a save may give (the save is refused once the force is read).</summary>
    bool TooManyCopies = false;
    /// <summary>The operation and mission of this point in the campaign (the mission start's save's name).</summary>
    int32_t OperationNumber = 0;
    int32_t MissionNumber = 0;
    /// <summary>Set by a step that left more to do: it runs again.</summary>
    bool Again = false;
    /// <summary>Every step run, or one refused (<see cref="EndCampaignRead"/>).</summary>
    bool Finished = false;
};

MCLogistics::MCCampaignReading::MCCampaignReading(MCLogistics& logistics, std::string_view saveName,
                                                  std::string_view extension, bool newCampaign, bool loadForce)
    : Logistics(logistics.weak_from_this())
    , SaveName(saveName)
    , Extension(extension)
    , NewCampaign(newCampaign)
    , LoadForce(loadForce)
{
    // The file's kind, for the reports: its extension, without the dot, in lower case.
    std::string kind(extension.substr(extension.starts_with('.') ? 1 : 0));
    std::ranges::transform(kind, kind.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<uint8_t>(c))); });
    Read = {.Kind = std::move(kind), .Player = loadForce};
}

MCLogistics::MCCampaignReading::~MCCampaignReading()
{
    // Dropped before its end (its change called off, the lists part read): no campaign, as when refused.
    if (const std::shared_ptr<MCLogistics> logistics = Logistics.lock(); logistics != nullptr && !Finished)
    {
        logistics->CurrentMission = -1;
    }
}

auto MCLogistics::LoadCampaign(std::string_view saveName, std::string_view extension, bool newCampaign, bool loadForce)
    -> int32_t
{
    MCCampaignReading reading(*this, saveName, extension, newCampaign, loadForce);

    for (const MCCampaignStep& step : CampaignSteps())
    {
        do
        {
            if (reading.Result != 0)
            {
                break;
            }

            const MCFrameLog::Scope part(step.Part);
            reading.Again = false;
            reading.Result = (this->*step.Read)(reading);
        } while (reading.Again);
    }

    return EndCampaignRead(reading);
}

auto MCLogistics::LoadCampaignPieces(std::string_view saveName, std::string_view extension, bool newCampaign,
                                     bool loadForce, const std::shared_ptr<int32_t>& result)
    -> std::vector<MCScreenChange::MCPiece>
{
    const auto reading = std::make_shared<MCCampaignReading>(*this, saveName, extension, newCampaign, loadForce);
    std::vector<MCScreenChange::MCPiece> pieces;

    // Each step a wait, done once its reading leaves nothing more: a step with more (the shop's mechs) goes on in the
    // next frame.
    for (const MCCampaignStep& step : CampaignSteps())
    {
        pieces.push_back({.Until = [reading, step]
                          {
                              const std::shared_ptr<MCLogistics> logistics = reading->Logistics.lock();

                              if (logistics == nullptr || reading->Result != 0)
                              {
                                  return true;
                              }

                              const MCFrameLog::Scope part(step.Part);
                              reading->Again = false;
                              reading->Result = ((*logistics).*step.Read)(*reading);
                              return !reading->Again || reading->Result != 0;
                          }});
    }

    pieces.push_back({.Run = [reading, result]
                      {
                          if (const std::shared_ptr<MCLogistics> logistics = reading->Logistics.lock();
                              logistics != nullptr)
                          {
                              *result = logistics->EndCampaignRead(*reading);
                          }
                      }});
    return pieces;
}

auto MCLogistics::CampaignSteps() -> std::span<const MCCampaignStep>
{
    static constexpr std::array<MCCampaignStep, 12> Steps = {{
        {.Part = "campaign.palette", .Read = &MCLogistics::ReadCampaignPalette},
        {.Part = "campaign.save", .Read = &MCLogistics::ReadCampaignStart},
        {.Part = "campaign.pilots", .Read = &MCLogistics::ReadCampaignForce},
        {.Part = "campaign.mechs", .Read = &MCLogistics::ReadCampaignMechs},
        {.Part = "campaign.vehicles", .Read = &MCLogistics::ReadCampaignVehicles},
        {.Part = "campaign.components", .Read = &MCLogistics::ReadCampaignComponents},
        {.Part = "campaign.mission", .Read = &MCLogistics::ReadCampaignMission},
        {.Part = "campaign.shop", .Read = &MCLogistics::ReadCampaignShop},
        {.Part = "campaign.shop mechs", .Read = &MCLogistics::ReadCampaignShopMechs},
        {.Part = "campaign.shop goods", .Read = &MCLogistics::ReadCampaignShopGoods},
        {.Part = "campaign.shop end", .Read = &MCLogistics::ReadCampaignShopEnd},
        {.Part = "campaign.inventory", .Read = &MCLogistics::ReadCampaignBlocks},
    }};
    return Steps;
}

auto MCLogistics::EndCampaignRead(MCCampaignReading& reading) -> int32_t
{
    reading.Finished = true;

    if (reading.Result != 0)
    {
        // Port fix: a refused save had emptied the lists (and taken its mission number, if it got that far), and the
        // campaign stayed open: the main menu's RETURN TO GAME showed the screens over the emptied lists, where a mech
        // whose pilot was gone, the shop never read, or the mission start's save crashed, and SAVE GAME saved them.
        // With no campaign, both wait for the next load.
        SDL_Log("MCLogistics: %s%s refused (%d); no campaign loaded", reading.SaveName.c_str(),
                reading.Extension.c_str(), reading.Result);
        CurrentMission = -1;
        MCTelemetry::LoadRefused(reading.Read);
    }

    return reading.Result;
}

auto MCLogistics::ReadCampaignPalette(MCCampaignReading&) -> int32_t
{
    // Port-only (Redux HD): what's kept across missions, from the files this game reads now (a match or not).
    LoadComponentListIfSwitched();
    ReadKeptTablesIfSwitched();
    MCDeploy::SetLocked(false);
    GuiSystem().ActivatePaletteFromTga(ArtPath + "logart\\lsrupm05.tga");

    return 0;
}

auto MCLogistics::ReadCampaignStart(MCCampaignReading& reading) -> int32_t
{
    using MCTelemetry::MCLoadRefusal;
    const std::string_view saveName = reading.SaveName;
    const std::string_view extension = reading.Extension;
    const bool newCampaign = reading.NewCampaign;
    MCTelemetry::MCRefusedLoad& read = reading.Read;
    MCPacketFile& packetFile = reading.PacketFile;
    MCFitIniFile& file = reading.File;
    bool& quickStart = reading.QuickStart;
    int32_t& savedMission = reading.SavedMission;

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
    int32_t result = OpenSave(packetFile, GamePath(SavePath, saveName, extension));
    Assert(result == 0, 0, " campaign file NOT Valid! ");

    if (result != 0)
    {
        return Refused(read, MCLoadRefusal::Unopened, result);
    }

    read.Bytes = packetFile.FileSize();
    read.Packets = packetFile.GetNumPackets();
    result = packetFile.SeekPacket(0);
    Assert(result == 0, 0, " could not find initial campaign file ");

    if (result != 0)
    {
        return Refused(read, MCLoadRefusal::NotPacked, result);
    }

    result = file.Open(packetFile, packetFile.GetPacketSize());
    Assert(result == 0, 0, " could not open initial campaign file ");

    if (result != 0)
    {
        return Refused(read, MCLoadRefusal::Unreadable, result);
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

        read.Planet = CurPlanet;
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

    savedMission = 0;

    if (MultiPlayer() == nullptr)
    {
        if (const MCFitResult<int32_t> number = file.Read<int32_t>("MissionNumber"); number.has_value())
        {
            read.Mission = *number;
            read.Scenarios = static_cast<int32_t>(Mission()->Scenarios.size());

            // Issue #25: the number picks the campaign's scenario (or, one past the last, its end); one the campaign
            // doesn't have refuses the save.
            if (*number < 0 || static_cast<size_t>(*number) > Mission()->Scenarios.size())
            {
                SDL_Log("MCLogistics: %.*s names mission %d of %zu", static_cast<int>(saveName.size()), saveName.data(),
                        *number, Mission()->Scenarios.size());
                return Refused(read, MCLoadRefusal::MissionPast, READ_ERR);
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

    return 0;
}

auto MCLogistics::ReadCampaignForce(MCCampaignReading& reading) -> int32_t
{
    MCPacketFile& packetFile = reading.PacketFile;
    MCFitIniFile& file = reading.File;

    if (MultiPlayer() == nullptr && !reading.QuickStart)
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
    }

    return 0;
}

auto MCLogistics::ReadCampaignMechs(MCCampaignReading& reading) -> int32_t
{
    MCPacketFile& packetFile = reading.PacketFile;
    MCFitIniFile& file = reading.File;
    bool& tooManyCopies = reading.TooManyCopies;

    if (MultiPlayer() == nullptr && !reading.QuickStart)
    {
        // Port fix: a unit entry asking for more copies than MostCopies refuses the save (none of them made, once its
        // vehicles are read).
        const uint32_t numMechs = ReadForceCount(file, "Mechs", "NumMechs", " could not read mech count ");
        ReadForceEntries(
            file, "Mech", 0, numMechs, " could not find mech block ", " could not find Mech Data ",
            [&](const std::string& profile)
            {
                for (int32_t copy = CopiesOf(file, tooManyCopies); copy > 0; copy--)
                {
                    MechList->AddMech(profile, false, true, true);
                }
            },
            [&](int32_t packet)
            {
                for (int32_t copy = CopiesOf(file, tooManyCopies); copy > 0; copy--)
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
    }

    return 0;
}

auto MCLogistics::ReadCampaignVehicles(MCCampaignReading& reading) -> int32_t
{
    using MCTelemetry::MCLoadRefusal;
    const bool newCampaign = reading.NewCampaign;
    const bool loadForce = reading.LoadForce;
    MCTelemetry::MCRefusedLoad& read = reading.Read;
    MCPacketFile& packetFile = reading.PacketFile;
    MCFitIniFile& file = reading.File;
    bool& tooManyCopies = reading.TooManyCopies;
    int32_t result = 0;

    if (MultiPlayer() == nullptr && !reading.QuickStart)
    {
        const uint32_t numVehicles = ReadForceCount(file, "Vehicles", "NumVehicles", " could not read vehicle count ");
        ReadForceEntries(
            file, "Vehicle", 0, numVehicles, " could not find Vehicle block ", " could not find vehicle Data ",
            [&](const std::string& profile)
            {
                for (int32_t copy = CopiesOf(file, tooManyCopies); copy > 0; copy--)
                {
                    VehicleList->AddVehicle(profile, false, false, true);
                }
            },
            [&](int32_t packet)
            {
                const int32_t copies = CopiesOf(file, tooManyCopies);
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

        if (tooManyCopies)
        {
            return Refused(read, MCLoadRefusal::Other, READ_ERR);
        }
    }

    return 0;
}

auto MCLogistics::ReadCampaignComponents(MCCampaignReading& reading) -> int32_t
{
    using MCTelemetry::MCLoadRefusal;
    MCTelemetry::MCRefusedLoad& read = reading.Read;
    MCFitIniFile& file = reading.File;
    int32_t result = 0;

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
        // Which entry ended the load, of how many, for the reports.
        const auto badEntry = [&read, count](int32_t index, int32_t error)
        {
            read.At = index;
            read.Entries = static_cast<int32_t>(count);
            return Refused(read, MCLoadRefusal::Components, error);
        };

        for (int32_t index = 0; index < static_cast<int32_t>(count); index++)
        {
            // Original behaviour: a bad component entry ends the load here, returning the error.
            result = file.SeekBlock(std::format("Componant{}", index));

            if (result != 0)
            {
                return badEntry(index, result);
            }

            const MCFitResult<uint8_t> masterID = file.Read<uint8_t>("ComponantID");

            if (!masterID.has_value())
            {
                return badEntry(index, std::to_underlying(masterID.error()));
            }

            const MCFitResult<int32_t> available = file.Read<int32_t>("NumAvailable");

            if (!available.has_value())
            {
                return badEntry(index, std::to_underlying(available.error()));
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

    return 0;
}

auto MCLogistics::ReadCampaignMission(MCCampaignReading& reading) -> int32_t
{
    using MCTelemetry::MCLoadRefusal;
    const bool newCampaign = reading.NewCampaign;
    const bool loadForce = reading.LoadForce;
    MCTelemetry::MCRefusedLoad& read = reading.Read;
    MCFitIniFile& file = reading.File;
    int32_t& savedMission = reading.SavedMission;
    int32_t result = 0;

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
            return Refused(read, CurrentMission < 0 ? MCLoadRefusal::NoMission : MCLoadRefusal::CampaignEnd, READ_ERR);
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
        result = OpenSave(resultFile, GamePath(SavePath, resultName, ".pkk"));

        if (result != 0)
        {
            return Refused(read, MCLoadRefusal::NoResults, result);
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

    return 0;
}

auto MCLogistics::ReadCampaignShop(MCCampaignReading& reading) -> int32_t
{
    MCPacketFile& packetFile = reading.PacketFile;
    int32_t result = 0;

    if (MultiPlayer() == nullptr)
    {
        // The purchase options of this point in the campaign, and an automatic save when a new mission starts.
        MCFitIniFile masterFile;
        result = masterFile.Open(GamePath(MissionPath, MissionName, ".fit"));
        Assert(result == 0, 0, " could not open master mission file ");
        result = masterFile.SeekBlock("OpInfo");
        Assert(result == 0, 0, " could not find operation information in master mission file");
        reading.OperationNumber =
            ReadRequired<int32_t>(masterFile, std::format("Scenario{}Operation", LastLogisticsMissionState),
                                  " could not find operation number in master mission file ");
        reading.MissionNumber =
            ReadRequired<int32_t>(masterFile, std::format("Scenario{}Mission", LastLogisticsMissionState),
                                  " could not find mission number in master mission file ");
        reading.Shop = BeginCampaignShop(PurchaseFile, packetFile);
    }
    else
    {
        SetUpMPPurchasing(PurchaseFile);
        ReadyStoreDescriptions();
    }

    return 0;
}

auto MCLogistics::ReadCampaignShopMechs(MCCampaignReading& reading) -> int32_t
{
    if (reading.Shop != nullptr)
    {
        reading.Again = ReadShopMechs(*reading.Shop);
    }

    return 0;
}

auto MCLogistics::ReadCampaignShopGoods(MCCampaignReading& reading) -> int32_t
{
    if (reading.Shop != nullptr)
    {
        ReadShopGoods(*reading.Shop);
    }

    return 0;
}

auto MCLogistics::ReadCampaignShopEnd(MCCampaignReading& reading) -> int32_t
{
    if (reading.Shop == nullptr)
    {
        return 0;
    }

    const bool loadForce = reading.LoadForce;
    const std::string oldPurchaseFile = EndCampaignShop(*reading.Shop);
    reading.Shop.reset();

    if (LastLogisticsMissionState < CurrentMission && !loadForce)
    {
        SetUpOldPurchasing(oldPurchaseFile);
        // "Operation %d Mission %d" (a printf format from the string table).
        const std::string format = LoadGameString(CurPlanet == 0 ? 0x37a : 0x386, 199);
        SaveCampaign(MCFormatPrintf(format.c_str(), reading.OperationNumber, reading.MissionNumber));
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

    ReadyStoreDescriptions();
    return 0;
}

auto MCLogistics::ReadCampaignBlocks(MCCampaignReading& reading) -> int32_t
{
    MCPacketFile& packetFile = reading.PacketFile;

    packetFile.Close();
    // The spares' rows numbered; then the force's, and each mech's battle rating worked out, as the 1998 'MECH BAY's rows
    // of the force did as its lists were shown on them (its mech tab). Its condition is worked out as a card shows it.
    NumberMechs();
    NumberPilots();
    ReIndexInventory();
    NumberVehicles();
    NumberForce();

    for (const std::shared_ptr<MCLogMech>& mech : ForceMechList->Mechs)
    {
        mech->CalcBR();
    }

    return 0;
}

auto MCLogistics::NumberForce() -> void
{
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

auto MCLogistics::ReadyStoreDescriptions() const -> void
{
    for (const std::shared_ptr<MCPurMech>& purMech : PurMechList->Mechs)
    {
        PrepareInfoDescription(purMech->Variants[static_cast<size_t>(purMech->CurVariant)]->Description);
    }

    for (const std::shared_ptr<MCPurVehicle>& purVehicle : PurVehicleList->Vehicles)
    {
        PrepareInfoDescription(purVehicle->Data->Description);
    }

    for (const std::shared_ptr<MCLogInventoryItem>& item : PurchaseComponents->Items)
    {
        PrepareInfoDescription(item->Description);
    }

    for (const std::shared_ptr<MCPurPilotData>& pilot : PurPilotList->Pilots)
    {
        if (pilot->Status == MCPurPilotData::ForHire)
        {
            PrepareInfoDescription(pilot->Description);
        }
    }
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

    // Port-only (SPEC-memory.md's "From the briefing"): the next mission known, its HD art is read from now, behind
    // the briefing's screens, and its load keeps it.
    StartMissionHold(fileName);

    // And the lance's mechs placed already (the last mission's), as each placed later (MCMechBriefBlock::PlaceInEmptySlot).
    for (int32_t index = 0; ForceMechList != nullptr && index < ForceMechList->GetMechCount(); ++index)
    {
        if (std::shared_ptr<MCLogMech> mech; ForceMechList->GetMechInfo(index, mech) == 0 && mech->Deployed)
        {
            MCHdArt::HoldMechAhead(mech->HdFolders);
        }
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

    // The briefing's text, which BRIEFING shows (MCCampaignMenu); the 1998 briefing formatted it into its mission
    // pane's picture here. The texts below are read into the briefing path's buffer, as the original did: a missing
    // one leaves the path.
    std::string text = GamePath(MissionPath, briefingFile);
    BriefingTextPath = text;

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
    ZoneMarks = ReadZoneMarks(MissionFileName);
}
