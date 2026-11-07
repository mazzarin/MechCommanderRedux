#include "stdafx.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "object/MCBattleMech.h"
#include "object/MCForces.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechWarrior.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "platform/MCFileSystem.h"
#include "terrain/MCTacticalMap.h"

float TotalScenarioTime = 0.0f;
float TotalLogisticsTime = 0.0f;

namespace
{
    /// <summary>The block names of a mech's eight body locations, in location order.</summary>
    constexpr std::array<std::string_view, 8> MechLocationNames = {"Head",    "CenterTorso", "LeftTorso", "RightTorso",
                                                                   "LeftArm", "RightArm",    "LeftLeg",   "RightLeg"};

    /// <summary>The names of a mech's eleven armor facings, in armor order.</summary>
    constexpr std::array<std::string_view, 11> MechArmorNames = {
        "Head",    "CenterTorso", "LeftTorso",       "RightTorso",    "LeftArm",       "RightArm",
        "LeftLeg", "RightLeg",    "RearCenterTorso", "RearLeftTorso", "RearRightTorso"};

    /// <summary>The block names of a vehicle's five locations.</summary>
    constexpr std::array<std::string_view, 5> VehicleLocationNames = {"Front", "Left", "Right", "Rear", "Turret"};

    /// <summary>The names of a pilot's four skills, in skill order.</summary>
    constexpr std::array<std::string_view, 4> SkillNames = {"Piloting", "Jumping", "Sensors", "Gunnery"};

    /// <summary>The master component forms a profile lists as weapons (FacesForward); form 10 is ammo.</summary>
    bool IsWeaponForm(MCComponentForm form)
    {
        return form == MCComponentForm::Weapon || form == MCComponentForm::WeaponEnergy ||
               form == MCComponentForm::WeaponBallistic || form == MCComponentForm::WeaponMissile;
    }

    /// <summary>The CRT's <c>DeleteFileA</c> on a game path.</summary>
    void DeleteFile(std::string_view fileName)
    {
        MCFileSystem::RemoveFile(fileName);
    }

    /// <summary>The name of profile <paramref name="index"/> ("tpak<i>n</i>").</summary>
    std::string ProfileName(uint32_t index)
    {
        return std::format("tpak{}", index);
    }

    /// <summary>The temp folder's FIT file <paramref name="name"/>.</summary>
    std::string TempFit(std::string_view name)
    {
        return GamePath(SaveTempPath, name, ".fit");
    }

    /// <summary>
    /// Copies <paramref name="fileName"/> into <paramref name="field"/> as the original's <c>strncpy</c> of 9 did: at
    /// most 9 characters (the names written are "tpak<i>n</i>", well inside it).
    /// </summary>
    void CopyProfileName(std::string& field, std::string_view fileName)
    {
        field = fileName.substr(0, 9);
    }

    /// <summary>Starts block <paramref name="kind"/><paramref name="index"/> with its PacketNum.</summary>
    void WritePacketBlock(MCFitIniFile& file, std::string_view kind, uint32_t index, uint32_t packetNum)
    {
        file.WriteBlock(std::format("{}{}", kind, index));
        file.WriteIdULong("PacketNum", packetNum);
    }

    /// <summary>Starts inventory block "Item:<paramref name="item"/>".</summary>
    void WriteItemBlock(MCFitIniFile& file, int32_t item)
    {
        file.WriteBlock(std::format("Item:{}", item));
    }

    /// <summary>Writes the 50 "Componant" blocks: each one's comment line, id and the count from <paramref name="countOf"/>.</summary>
    template <typename CountFn> void WriteComponents(MCFitIniFile& file, CountFn countOf)
    {
        file.WriteBlock("Components");
        file.WriteIdULong("NumComponents", static_cast<uint32_t>(FitComponents.size()));

        for (size_t i = 0; i < FitComponents.size(); i++)
        {
            file.WriteBlock(std::format("Componant{}", i));
            file.WriteLine(FitComponents[i].Comment);
            file.WriteIdUChar("ComponantId", FitComponents[i].Id);
            file.WriteIdLong("NumAvailable", countOf(FitComponents[i].Id));
        }
    }

    /// <summary>Writes the skills block <paramref name="block"/> from <paramref name="valueOf"/> (skill index to value).</summary>
    template <typename ValueFn> void WriteSkills(MCFitIniFile& file, std::string_view block, ValueFn valueOf)
    {
        file.WriteBlock(block);

        for (size_t i = 0; i < SkillNames.size(); i++)
        {
            file.WriteIdChar(SkillNames[i], valueOf(i));
        }
    }

    /// <summary>
    /// Copies file <paramref name="fileName"/> into packet <paramref name="packet"/> of <paramref name="packFile"/>.
    /// Returns the open error, if any.
    /// </summary>
    int32_t PackFileInto(MCPacketFile& packFile, MCFile& file, std::string_view fileName, int32_t packet)
    {
        const int32_t result = file.Open(fileName);

        if (result != 0)
        {
            return result;
        }

        const uint32_t size = file.FileSize();
        std::vector<uint8_t> buffer(size);
        file.Read(buffer);
        packFile.WritePacket(packet, buffer, MCPacketStorage::Lzd);
        file.Close();
        return 0;
    }

    /// <summary>Packs profile files tpak0 .. tpak<paramref name="count"/>-1 into packets 1 .. <paramref name="count"/>.</summary>
    int32_t PackProfiles(MCPacketFile& packFile, MCFile& file, int32_t count)
    {
        for (int32_t i = 0; i < count; i++)
        {
            const int32_t result = PackFileInto(packFile, file, TempFit(ProfileName(static_cast<uint32_t>(i))), i + 1);

            if (result != 0)
            {
                return result;
            }
        }

        return 0;
    }

    /// <summary>Deletes profile files tpak0 .. tpak<paramref name="count"/>-1.</summary>
    void DeleteProfiles(int32_t count)
    {
        for (int32_t i = 0; i < count; i++)
        {
            DeleteFile(TempFit(ProfileName(static_cast<uint32_t>(i))));
        }
    }

    /// <summary>The warrior at <paramref name="index"/> of <paramref name="list"/> (walked from the head).</summary>
    std::shared_ptr<MCLogWarrior> WarriorAt(MCLogWarriorList& list, int32_t index)
    {
        return list.Warriors[static_cast<size_t>(index)];
    }

    /// <summary>How many warriors of <paramref name="list"/> are (or are not) assigned.</summary>
    uint32_t CountWarriors(MCLogWarriorList& list, bool assigned)
    {
        return static_cast<uint32_t>(std::ranges::count_if(list.Warriors,
                                                           [&](const std::shared_ptr<MCLogWarrior>& warrior)
                                                           { return warrior->Assigned == assigned; }));
    }

    /// <summary>How many of <paramref name="parts"/> pass <paramref name="test"/>.</summary>
    template <typename Part, typename Test>
    uint32_t CountParts(const std::vector<std::shared_ptr<Part>>& parts, Test test)
    {
        return static_cast<uint32_t>(
            std::ranges::count_if(parts, [&](const std::shared_ptr<Part>& part) { return test(*part); }));
    }

    /// <summary>
    /// Runs <paramref name="write"/> on each of <paramref name="parts"/> that passes <paramref name="test"/>, with its
    /// index among them. Returns the first error.
    /// </summary>
    template <typename Part, typename Test, typename Write>
    int32_t WriteParts(const std::vector<std::shared_ptr<Part>>& parts, Test test, Write write)
    {
        uint32_t index = 0;

        for (const std::shared_ptr<Part>& part : parts)
        {
            if (!test(*part))
            {
                continue;
            }

            if (const int32_t result = write(*part, index); result != 0)
            {
                return result;
            }

            ++index;
        }

        return 0;
    }

    /// <summary>
    /// A purchasable pilot's Status in the purchase file: 3 when a pilot of the player's with the same description
    /// was sold, 1 when one is alive, 2 when one is dead; otherwise the pilot's own status. The player's unassigned
    /// pilots are searched first, then the assigned ones; the last match of a list wins.
    /// </summary>
    int32_t PurchasePilotStatus(MCLogistics& logistics, MCPurPilotData& pilot)
    {
        auto search = [pilot](MCLogWarriorList& list)
        {
            int32_t status = -1;

            for (int32_t i = 0; i < list.GetWarriorCount(); i++)
            {
                std::shared_ptr<MCLogWarrior> warrior;
                list.GetWarriorInfo(i, warrior);

                if (warrior->DescIndex != pilot.DescIndex)
                {
                    continue;
                }

                if (warrior->Sold != 0)
                {
                    status = 3;
                }
                else
                {
                    status = warrior->Health > 0.0f ? 1 : 2;
                }
            }

            return status;
        };

        int32_t status = search(*logistics.WarriorList);

        if (status == -1)
        {
            status = search(*logistics.AssignedWarriorList);
        }

        if (status == -1)
        {
            status = pilot.Status;
        }

        return status;
    }

    /// <summary>Writes the purchase file: what the shop has left of each mech, vehicle, component and pilot.</summary>
    int32_t WritePurchaseFile(std::string_view fileName)
    {
        MCLogistics& logistics = *Mission()->Logistics;
        MCFitIniFile file;

        if (const int32_t result = file.Create(fileName); result != 0)
        {
            return result;
        }

        file.WriteBlock("Header");
        file.WriteIdLong("NumGifts", 0);
        file.WriteIdLong("NumMechs", logistics.PurMechList->GetMechCount());
        file.WriteIdLong("NumVehicles", logistics.PurVehicleList->GetVehicleCount());
        file.WriteIdLong("NumComponants", logistics.PurchaseComponents->NumItems());
        file.WriteIdLong("NumWarriors", logistics.PurPilotList->GetPilotCount());

        for (int32_t i = 0; i < logistics.PurMechList->GetMechCount(); i++)
        {
            file.WriteBlock(std::format("Mech{}", i));
            std::shared_ptr<MCPurMech> mech;
            logistics.PurMechList->GetMechInfo(i, mech);
            file.WriteIdLong("TypeAAvailable", mech->Variants[0]->NumAvailable);
            file.WriteIdLong("TypeWAvailable", mech->Variants[1]->NumAvailable);
            file.WriteIdLong("TypeJAvailable", mech->Variants[2]->NumAvailable);
            file.WriteIdString("TypeAFile", mech->Variants[0]->FileName);
            file.WriteIdString("TypeWFile", mech->Variants[1]->FileName);
            file.WriteIdString("TypeJFile", mech->Variants[2]->FileName);
        }

        for (int32_t i = 0; i < logistics.PurVehicleList->GetVehicleCount(); i++)
        {
            file.WriteBlock(std::format("Vehicle{}", i));
            std::shared_ptr<MCPurVehicle> vehicle;
            logistics.PurVehicleList->GetVehicleInfo(i, vehicle);
            file.WriteIdLong("NumAvailable", vehicle->Data->NumAvailable);
            file.WriteIdString("Filename", vehicle->Data->FileName);
        }

        for (int32_t i = 0; i < logistics.PurchaseComponents->NumItems(); i++)
        {
            file.WriteBlock(std::format("Componant{}", i));
            std::shared_ptr<MCLogInventoryItem> item = logistics.PurchaseComponents->GetItemInfo(i);
            file.WriteIdUChar("ComponantID", item->MasterID);
            file.WriteIdLong("NumAvailable", item->Count);
        }

        for (int32_t i = 0; i < logistics.PurPilotList->GetPilotCount(); i++)
        {
            file.WriteBlock(std::format("Warrior{}", i));
            std::shared_ptr<MCPurPilotData> pilot;
            logistics.PurPilotList->GetPilotInfo(i, pilot);
            file.WriteIdString("Profile", pilot->FileName);
            file.WriteIdLong("Status", PurchasePilotStatus(logistics, *pilot));
        }

        file.Close();
        return 0;
    }

    /// <summary>
    /// Writes the pilots of the player's list: first the unassigned ones as "Warriors", then the assigned ones as
    /// "AssWarriors", each list walked from its last pilot to its first. Returns the first error.
    /// </summary>
    int32_t WriteWarriors(MCFitIniFile& file, uint32_t& numWarriors, uint32_t& numAssigned)
    {
        MCLogistics& logistics = *Mission()->Logistics;

        // The pilots of one list that are (or are not) assigned, from the last to the first, from packet
        // firstPacket on.
        auto writeList = [&file](MCLogWarriorList& list, bool assigned, uint32_t firstPacket) -> int32_t
        {
            uint32_t packet = firstPacket;

            for (int32_t i = list.GetWarriorCount() - 1; i >= 0; i--)
            {
                const std::shared_ptr<MCLogWarrior> warrior = WarriorAt(list, i);

                if ((warrior->Assigned != 0) != assigned)
                {
                    continue;
                }

                WritePacketBlock(file, "Warrior", packet, packet);

                if (const int32_t result =
                        MCMissionLogisticsBridge::LogisticsWarriorProfileWriter(ProfileName(packet), *warrior);
                    result != 0)
                {
                    return result;
                }

                ++packet;
            }

            return 0;
        };

        file.WriteBlock("Warriors");
        numWarriors = CountWarriors(*logistics.WarriorList, false);
        file.WriteIdULong("NumWarriors", numWarriors);

        if (const int32_t result = writeList(*logistics.WarriorList, false, 0); result != 0)
        {
            return result;
        }

        file.WriteBlock("AssWarriors");
        numAssigned = CountWarriors(*logistics.AssignedWarriorList, true);
        file.WriteIdULong("NumAssWarriors", numAssigned);
        return writeList(*logistics.AssignedWarriorList, true, numWarriors);
    }

    /// <summary>
    /// Packs the starting fit <paramref name="fitName"/>, the <paramref name="numProfiles"/> profiles and the purchase
    /// file <paramref name="purchaseName"/> into packet file <paramref name="packFile"/> (already created).
    /// </summary>
    int32_t PackGame(MCPacketFile& packFile, std::string_view fitName, int32_t numProfiles,
                     std::string_view purchaseName)
    {
        packFile.Reserve(numProfiles + 2, true);
        MCFile source;

        if (const int32_t result = PackFileInto(packFile, source, fitName, 0); result != 0)
        {
            return result;
        }

        if (const int32_t result = PackProfiles(packFile, source, numProfiles); result != 0)
        {
            return result;
        }

        return PackFileInto(packFile, source, purchaseName, numProfiles + 1);
    }

    /// <summary>The scenario's warrior <paramref name="index"/> (1-based; null out of range).</summary>
    std::shared_ptr<MCMechWarrior> ScenarioWarrior(uint32_t index)
    {
        if (static_cast<int32_t>(index) < 1 || Scenario()->NumWarriors() < index)
        {
            return nullptr;
        }

        return Scenario()->Warrior(index);
    }

    /// <summary>
    /// Whether a mission unit goes back to logistics: a player mech still under player control, not one that
    /// only joins on a win (NotMineYet) unless the mission was won.
    /// </summary>
    bool ReturnsFromMission(MCObjectClass objectClass, int32_t alignment, int32_t netPlayerId, int notMineYet)
    {
        return objectClass == MCObjectClass::BattleMech && alignment == HomeTeam()->Alignment && netPlayerId != -1 &&
               (notMineYet == 0 || ScenarioResult > 3);
    }

    /// <summary>Writes the logistics inventory's component counts.</summary>
    void WriteInventoryComponents(MCFitIniFile& file, MCLogistics& logistics)
    {
        WriteComponents(file, [&logistics](uint8_t id) { return logistics.ComponentInventory->GetItemCount(id); });
    }

    /// <summary>Writes the items of <paramref name="inventory"/> whose form passes <paramref name="test"/>, each stat an item.</summary>
    template <typename Test, typename Write>
    void WriteInventoryItems(MCInventoryList& inventory, Test test, int32_t& item, Write write)
    {
        for (const std::shared_ptr<MCLogInventoryItem>& entry : inventory.Items)
        {
            if (!test(MasterComponentList[entry->MasterID].Form))
            {
                continue;
            }

            for (const std::shared_ptr<MCLogInventoryStat>& stat : entry->Stats)
            {
                write(entry->MasterID, *stat, item);
                ++item;
            }
        }
    }

    /// <summary>The copies of <paramref name="inventory"/>'s components whose form passes <paramref name="test"/>.</summary>
    template <typename Test> uint8_t CountInventoryItems(const MCInventoryList& inventory, Test test)
    {
        size_t copies = 0;

        for (const std::shared_ptr<MCLogInventoryItem>& entry : inventory.Items)
        {
            copies += test(MasterComponentList[entry->MasterID].Form) ? entry->Stats.size() : 0;
        }

        return static_cast<uint8_t>(copies);
    }

    /// <summary>The other equipment: neither a weapon nor ammo.</summary>
    bool IsOtherForm(MCComponentForm form)
    {
        return !IsWeaponForm(form) && form != MCComponentForm::Ammo;
    }

    /// <summary>Ammo.</summary>
    bool IsAmmoForm(MCComponentForm form)
    {
        return form == MCComponentForm::Ammo;
    }
}

void DestroyAllFitFiles(std::string_view path)
{
    for (const std::string& name : MCFileSystem::FindFiles(std::format("{}*.fit", path)))
    {
        DeleteFile(std::format("{}{}", path, name));
    }
}

namespace MCMissionLogisticsBridge
{
    int32_t MissionResultsStartingFitWriter(std::string_view fileName)
    {
        const std::string fitName = TempFit(fileName);
        MCFitIniFile file;

        if (const int32_t result = file.Create(fitName); result != 0)
        {
            return result;
        }

        file.WriteBlock("Planet");
        file.WriteIdLong("Setting", CurPlanet);
        file.WriteBlock("General");
        file.WriteIdString("PurchaseFile", CurPlanet == 0 ? "purchase" : "xpur");
        file.WriteBlock("ResourcePoints");
        file.WriteIdULong("numPoints", static_cast<uint32_t>(Scenario()->CalcResourcePointsEarned()));

        // The surviving pilots of the player's mechs.
        file.WriteBlock("Warriors");
        auto warriorReturns = [](MCMechWarrior& warrior)
        {
            const auto vehicle = std::static_pointer_cast<MCMover>(LockObject(warrior.Vehicle));
            return ReturnsFromMission(vehicle->ObjectClass, warrior.Alignment, vehicle->NetPlayerId,
                                      warrior.NotMineYet);
        };

        const auto scenarioWarriors = static_cast<int32_t>(Scenario()->NumWarriors());
        uint32_t numWarriors = 0;

        for (uint32_t i = 1; static_cast<int32_t>(i) <= scenarioWarriors; i++)
        {
            if (warriorReturns(*ScenarioWarrior(i)))
            {
                ++numWarriors;
            }
        }

        file.WriteIdULong("NumWarriors", numWarriors);
        uint32_t packet = 0;

        for (uint32_t i = 1; static_cast<int32_t>(i) <= scenarioWarriors; i++)
        {
            const std::shared_ptr<MCMechWarrior> warrior = ScenarioWarrior(i);

            if (!warriorReturns(*warrior))
            {
                continue;
            }

            WritePacketBlock(file, "Warrior", packet, packet);

            if (const int32_t result = MissionResultsWarriorProfileWriter(ProfileName(packet), *warrior); result != 0)
            {
                return result;
            }

            ++packet;
        }

        // The player's mechs, then the salvaged ones.
        file.WriteBlock("Mechs");
        auto mechReturns = [](const std::shared_ptr<MCBaseObject>& object)
        {
            auto& mech = static_cast<MCBattleMech&>(*object);
            return object->ObjectClass == MCObjectClass::BattleMech &&
                   ReturnsFromMission(object->ObjectClass, mech.GetAlignment(), mech.NetPlayerId, mech.NotMineYet);
        };

        auto salvagedMech = [](const std::shared_ptr<MCGameObject>& salvage)
        { return salvage != nullptr && salvage->ObjectClass == MCObjectClass::BattleMech; };

        const std::shared_ptr<MCTacticalMap> tacMap = TacticalMap();
        const auto numMechs = static_cast<uint32_t>(std::ranges::count_if(*InnerSphereMechList(), mechReturns) +
                                                    std::ranges::count_if(tacMap->Salvage.Pins(), salvagedMech));
        file.WriteIdULong("NumMechs", numMechs);
        uint32_t mechIndex = 0;
        packet = numWarriors;

        for (std::shared_ptr<MCBaseObject> object : *InnerSphereMechList())
        {
            if (!mechReturns(object))
            {
                continue;
            }

            WritePacketBlock(file, "Mech", mechIndex, packet);

            if (const int32_t result =
                    MissionResultsMechProfileWriter(ProfileName(packet), static_cast<MCBattleMech&>(*object), false);
                result != 0)
            {
                return result;
            }

            ++mechIndex;
            ++packet;
        }

        for (const std::shared_ptr<MCGameObject>& salvage : tacMap->Salvage.Pins())
        {
            if (!salvagedMech(salvage))
            {
                continue;
            }

            WritePacketBlock(file, "Mech", mechIndex, packet);
            auto& mech = static_cast<MCBattleMech&>(*salvage);
            mech.NotMineYet = false;

            if (const int32_t result = MissionResultsMechProfileWriter(ProfileName(packet), mech, true); result != 0)
            {
                return result;
            }

            ++mechIndex;
            ++packet;
        }

        file.WriteIdULong("NumVehicles", 0);

        // The components salvaged on the map.
        WriteComponents(file,
                        [tacMap](uint8_t id)
                        {
                            int32_t count = 0;

                            for (const std::shared_ptr<MCGameObject>& salvage : tacMap->Salvage.Pins())
                            {
                                if (salvage == nullptr)
                                {
                                    continue;
                                }

                                for (const MCSalvageItem& item : salvage->GetSalvage())
                                {
                                    if (item.ItemId == id)
                                    {
                                        count += item.NumItems;
                                    }
                                }
                            }

                            return count;
                        });
        file.Close();

        // Everything goes into one packet file: the starting fit, then the profiles.
        MCPacketFile packFile;
        packFile.Create(GamePath(SavePath, fileName, ".pkk"));
        const auto numProfiles = static_cast<int32_t>(numMechs + numWarriors);
        packFile.Reserve(numProfiles + 1, true);
        MCFile source;

        if (const int32_t result = PackFileInto(packFile, source, fitName, 0); result != 0)
        {
            return result;
        }

        if (const int32_t result = PackProfiles(packFile, source, numProfiles); result != 0)
        {
            return result;
        }

        packFile.Close();
        DeleteFile(fitName);
        DeleteProfiles(numProfiles);
        DeleteFile(TempFit("bridge"));

        for (int32_t i = 0; i < 12; i++)
        {
            DeleteFile(TempFit(std::format("mech{:04}", i)));
            DeleteFile(TempFit(std::format("warr{:04}", i)));
        }

        return 0;
    }

    int32_t MissionResultsMechProfileWriter(std::string_view fileName, MCBattleMech& mech, bool notAssigned)
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(TempFit(fileName)); result != 0)
        {
            return result;
        }

        file.WriteBlock("Header");
        file.WriteIdString("FileType", "MechProfile");
        file.WriteBlock("General");
        file.WriteIdString("MechType", mech.IfaceName);
        file.WriteIdString("Name", mech.DebugStatus);
        file.WriteIdFloat("CurTonnage", mech.GetTonnage());
        file.WriteIdString("icon", mech.IconName);
        file.WriteIdChar("Status", static_cast<char>(mech.Status));
        file.WriteIdULong("Chassis", static_cast<uint32_t>(mech.GetObjectType()->ObjTypeNum));
        file.WriteIdLong("Pilot", mech.PilotId == -1 ? -8 : mech.PilotId);
        file.WriteIdBoolean("Assigned", !notAssigned);
        file.WriteIdBoolean("NotMineYet", mech.NotMineYet);
        file.WriteIdLong("DescIndex", mech.DescIndex);
        file.WriteIdLong("NameIndex", mech.NameIndex);
        file.WriteIdLong("NameVariant", mech.NameVariant);
        file.WriteBlock("Engine");
        file.WriteIdFloat("Tonnage", mech.EngineTonnage);
        file.WriteIdULong("Rating", mech.EngineRating);
        file.WriteIdUChar("MaxRunSpeed", static_cast<uint8_t>(static_cast<int32_t>(mech.MaxRunSpeed)));
        file.WriteBlock("Armor");
        file.WriteIdUChar("Type", mech.ArmorType);
        file.WriteIdFloat("Tonnage", mech.ArmorTonnage);
        file.WriteBlock("MaxArmorPoints");

        for (size_t i = 0; i < MechArmorNames.size(); i++)
        {
            file.WriteIdUChar(MechArmorNames[i], mech.Armor[i].MaxArmor);
        }

        file.WriteBlock("CurArmorPoints");

        for (size_t i = 0; i < MechArmorNames.size(); i++)
        {
            file.WriteIdUChar(MechArmorNames[i], static_cast<uint8_t>(static_cast<int32_t>(mech.Armor[i].CurArmor)));
        }

        file.WriteBlock("InventoryInfo");
        file.WriteIdUChar("NumOther", mech.NumOther);
        file.WriteIdUChar("NumWeapons", mech.NumWeapons);
        file.WriteIdUChar("NumAmmo", mech.NumAmmos);

        // Each location's CASE, structure, hot spot and critical spaces (component, hit).
        for (int32_t location = 0; location < 8; location++)
        {
            const MCBodyLocation& body = mech.BodyAt(location);
            file.WriteBlock(MechLocationNames[static_cast<size_t>(location)]);
            file.WriteIdUChar("CASE", static_cast<uint8_t>(body.HasCase));
            file.WriteIdUChar("CurInternalStructure",
                              static_cast<uint8_t>(static_cast<int32_t>(body.CurInternalStructure)));
            file.WriteIdUChar("HotSpotNumber", body.HotSpotNumber);

            for (int32_t space = 0; space < NumLocationCriticalSpaces[location]; space++)
            {
                const MCCriticalSpace& critical = body.CriticalSpaces[space];
                // Port: the original printed "hit a component" for hit spaces (a debug trace).
                const std::array<uint8_t, 2> values = {critical.InventoryID, static_cast<uint8_t>(critical.Hit)};
                file.WriteIdUCharArray(std::format("Component:{}", space), values);
            }
        }

        // The inventory: the other equipment, then the weapons with their facing, then the ammo with its amount.
        const int32_t numOther = mech.NumOther;
        const int32_t numWeapons = mech.NumWeapons;
        const int32_t numAmmo = mech.NumAmmos;
        int32_t item = 0;

        for (; item < numOther; item++)
        {
            WriteItemBlock(file, item);
            file.WriteIdUChar("MasterID", mech.Inventory[item].MasterID);
        }

        for (; item < numOther + numWeapons; item++)
        {
            WriteItemBlock(file, item);
            file.WriteIdUChar("MasterID", mech.Inventory[item].MasterID);
            file.WriteIdUChar("FacesForward", mech.Inventory[item].FacesForward);
        }

        for (; item < numOther + numWeapons + numAmmo; item++)
        {
            WriteItemBlock(file, item);
            file.WriteIdUChar("MasterID", mech.Inventory[item].MasterID);
            file.WriteIdLong("Amount", mech.Inventory[item].Amount);
        }

        file.Close();
        return 0;
    }

    int32_t MissionResultsWarriorProfileWriter(std::string_view fileName, MCMechWarrior& warrior)
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(TempFit(fileName)); result != 0)
        {
            return result;
        }

        file.WriteBlock("General");
        file.WriteIdString("Name", warrior.Name);
        file.WriteIdString("Callsign", warrior.Callsign);
        file.WriteIdLong("paintScheme", warrior.PaintScheme);
        file.WriteIdString("pilotAudio", warrior.AudioStr);
        file.WriteIdString("pilotVideo", warrior.VideoStr);
        file.WriteIdString("Picture", warrior.Picture);
        file.WriteIdString("Brain", warrior.BrainStr);
        file.WriteIdBoolean("Assigned", true);

        // Statuses 3, 5 and 6: the pilot got out.
        if (warrior.Status == 3 || warrior.Status == 5 || warrior.Status == 6)
        {
            file.WriteIdBoolean("Ejected", true);
        }

        file.WriteIdBoolean("NotMineYet", warrior.NotMineYet);
        file.WriteIdLong("DescIndex", warrior.DescIndex);
        file.WriteIdLong("NameIndex", warrior.NameIndex);
        file.WriteBlock("PersonalityTraits");
        file.WriteIdChar("Professionalism", warrior.Professionalism);
        file.WriteIdChar("Decorum", warrior.Decorum);
        file.WriteIdChar("Aggressiveness", static_cast<char>(warrior.GetAggressiveness(1)));
        file.WriteIdChar("Courage", warrior.Courage);
        WriteSkills(file, "Skills",
                    [&warrior](size_t i) { return static_cast<char>(static_cast<int32_t>(warrior.SkillRank[i])); });
        WriteSkills(file, "OriginalSkills",
                    [&warrior](size_t i) { return static_cast<char>(warrior.OriginalSkills[i]); });
        WriteSkills(file, "LatestSkills", [&warrior](size_t i) { return static_cast<char>(warrior.LatestSkills[i]); });
        file.WriteBlock("SkillPoints");

        for (size_t i = 0; i < SkillNames.size(); i++)
        {
            file.WriteIdFloat(SkillNames[i], warrior.SkillPoints[i]);
        }

        file.WriteBlock("Status");
        file.WriteIdChar("Wounds", static_cast<char>(static_cast<int32_t>(warrior.Wounds)));
        file.Close();
        return 0;
    }

    int32_t LogisticsStartingFitWriter(std::string_view fileName, bool skipDeployed)
    {
        MCLogistics& logistics = *Mission()->Logistics;
        const std::string fitName = TempFit(fileName);
        MCFitIniFile file;

        if (const int32_t result = file.Create(fitName); result != 0)
        {
            return result;
        }

        file.WriteBlock("Planet");
        file.WriteIdLong("Setting", CurPlanet);
        file.WriteBlock("General");
        file.WriteIdString("PurchaseFile", GlobalLog()->PurchaseFile);
        file.WriteBlock("ResourcePoints");
        file.WriteIdULong("numPoints", static_cast<uint32_t>(ResourcePoints));

        uint32_t numWarriors = 0;
        uint32_t numAssWarriors = 0;

        if (const int32_t result = WriteWarriors(file, numWarriors, numAssWarriors); result != 0)
        {
            return result;
        }

        // The unassigned mechs.
        auto unassigned = [](MCLogPart& part) { return part.Assigned == 0; };
        file.WriteBlock("Mechs");
        const uint32_t numMechs = CountParts(logistics.MechList->Mechs, unassigned);
        file.WriteIdULong("NumMechs", numMechs);

        if (const int32_t result = WriteParts(logistics.MechList->Mechs, unassigned,
                                              [&](MCLogMech& mech, uint32_t index)
                                              {
                                                  const uint32_t packet = index + numAssWarriors + numWarriors;
                                                  WritePacketBlock(file, "Mech", index, packet);
                                                  return LogisticsMechProfileWriter(ProfileName(packet), mech, false);
                                              });
            result != 0)
        {
            return result;
        }

        // The force's mechs (without the deployed ones when asked, and without the ones not the player's yet).
        auto forceMechWritten = [skipDeployed](MCLogMech& part)
        { return part.Assigned != 0 && (part.Deployed == 0 || !skipDeployed) && part.NotMineYet == 0; };
        file.WriteBlock("AssMechs");
        const uint32_t numAssMechs = CountParts(logistics.ForceMechList->Mechs, forceMechWritten);
        file.WriteIdULong("NumAssMechs", numAssMechs);

        if (const int32_t result = WriteParts(logistics.ForceMechList->Mechs, forceMechWritten,
                                              [&](MCLogMech& mech, uint32_t index)
                                              {
                                                  const uint32_t packet =
                                                      index + numMechs + numAssWarriors + numWarriors;
                                                  WritePacketBlock(file, "Mech", index + numMechs, packet);
                                                  return LogisticsMechProfileWriter(ProfileName(packet), mech, false);
                                              });
            result != 0)
        {
            return result;
        }

        // The vehicles not deployed: the unassigned ones, then the force's.
        auto unassignedWaiting = [](MCLogVehicle& part) { return part.Assigned == 0 && part.Deployed == 0; };
        auto forceWaiting = [](MCLogVehicle& part) { return part.Assigned != 0 && part.Deployed == 0; };
        file.WriteBlock("Vehicles");
        const uint32_t numVehicles = CountParts(logistics.VehicleList->Vehicles, unassignedWaiting) +
                                     CountParts(logistics.ForceVehicleList->Vehicles, forceWaiting);
        file.WriteIdULong("NumVehicles", numVehicles);
        // The index runs on over both lists.
        uint32_t vehicleIndex = 0;
        auto writeVehicle = [&](MCLogVehicle& vehicle, uint32_t) -> int32_t
        {
            const uint32_t packet = vehicleIndex + numAssMechs + numMechs + numAssWarriors + numWarriors;
            WritePacketBlock(file, "Vehicle", vehicleIndex, packet);
            const int32_t written = LogisticsVehicleProfileWriter(ProfileName(packet), vehicle, false);

            if (written == 0)
            {
                ++vehicleIndex;
            }

            return written;
        };

        if (const int32_t result = WriteParts(logistics.VehicleList->Vehicles, unassignedWaiting, writeVehicle);
            result != 0)
        {
            return result;
        }

        if (const int32_t result = WriteParts(logistics.ForceVehicleList->Vehicles, forceWaiting, writeVehicle);
            result != 0)
        {
            return result;
        }

        // The deployed vehicles of the force.
        auto forceDeployed = [](MCLogVehicle& part)
        { return part.Assigned != 0 && part.Deployed != 0 && part.NotMineYet == 0; };
        file.WriteBlock("AssVehicles");
        const uint32_t numAssVehicles = CountParts(logistics.ForceVehicleList->Vehicles, forceDeployed);
        file.WriteIdULong("NumAssVehicles", numAssVehicles);

        if (const int32_t result =
                WriteParts(logistics.ForceVehicleList->Vehicles, forceDeployed,
                           [&](MCLogVehicle& vehicle, uint32_t index)
                           {
                               const uint32_t packet =
                                   index + numVehicles + numAssMechs + numMechs + numAssWarriors + numWarriors;
                               WritePacketBlock(file, "Vehicle", index + numVehicles, packet);
                               return LogisticsVehicleProfileWriter(ProfileName(packet), vehicle, false);
                           });
            result != 0)
        {
            return result;
        }

        WriteInventoryComponents(file, logistics);
        file.Close();

        const std::string purchaseName = GamePath(SavePath, fileName, ".pur");

        if (const int32_t result = WritePurchaseFile(purchaseName); result != 0)
        {
            return result;
        }

        // The starting fit, the profiles and the purchase file go into one packet file.
        MCPacketFile packFile;
        packFile.Create(GamePath(SavePath, fileName, ".pkk"));
        const auto numProfiles =
            static_cast<int32_t>(numAssVehicles + numVehicles + numAssMechs + numMechs + numAssWarriors + numWarriors);

        if (const int32_t result = PackGame(packFile, fitName, numProfiles, purchaseName); result != 0)
        {
            return result;
        }

        packFile.Close();
        DeleteFile(fitName);
        DeleteFile(purchaseName);
        // Original behaviour: only the first vehicles + mechs + warriors profiles are deleted; the rest are left in
        // the temp folder (and overwritten next time).
        DeleteProfiles(static_cast<int32_t>(numVehicles + numMechs + numWarriors));
        return 0;
    }

    int32_t LogisticsMechProfileWriter(std::string_view fileName, MCLogMech& mech, bool writeRequired)
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(TempFit(fileName)); result != 0)
        {
            return result;
        }

        file.WriteBlock("Header");
        file.WriteIdString("FileType", "MechProfile");
        file.WriteBlock("General");
        file.WriteIdString("MechType", mech.FileName);
        file.WriteIdString("Name", mech.MechName);
        file.WriteIdFloat("CurTonnage", mech.CurTonnage);
        file.WriteIdString("icon", mech.IconName);
        file.WriteIdChar("Status", mech.Status);
        file.WriteIdULong("Chassis", mech.Chassis);
        file.WriteIdLong("NameIndex", mech.NameIndex);
        file.WriteIdLong("NameVariant", mech.NameVariant);
        file.WriteIdBoolean("Assigned", mech.Assigned);
        file.WriteIdBoolean("NotMineYet", mech.NotMineYet);
        int32_t pilot = mech.PilotIndex;

        if (pilot == -8 && mech.NotMineYet == 0)
        {
            pilot = -1;
        }

        file.WriteIdLong("Pilot", pilot);

        if (writeRequired)
        {
            file.WriteIdBoolean("Required", mech.Required);
        }

        file.WriteIdLong("DescIndex", mech.DescIndex);
        file.WriteBlock("Engine");
        file.WriteIdFloat("Tonnage", mech.EngineTonnage);
        file.WriteIdULong("Rating", mech.EngineRating);
        file.WriteIdUChar("MaxRunSpeed", mech.MaxRunSpeed);
        file.WriteBlock("Armor");
        file.WriteIdUChar("Type", mech.ArmorType);
        file.WriteIdFloat("Tonnage", mech.ArmorTonnage);
        file.WriteBlock("MaxArmorPoints");

        for (size_t i = 0; i < MechArmorNames.size(); i++)
        {
            file.WriteIdUChar(MechArmorNames[i], mech.Armor[i].MaxArmor);
        }

        file.WriteBlock("CurArmorPoints");

        for (size_t i = 0; i < MechArmorNames.size(); i++)
        {
            file.WriteIdUChar(MechArmorNames[i], mech.Armor[i].CurArmor);
        }

        // The critical slots are rebuilt from the inventory as it is written: other equipment, weapons, then ammo.
        mech.NumOther = 0;
        mech.NumWeapons = 0;
        mech.NumAmmo = 0;

        for (auto& location : mech.ItemSlots)
        {
            for (auto& slot : location)
            {
                slot = {0xff, 0, 0xff};
            }
        }

        int32_t item = 0;
        WriteInventoryItems(*mech.Inventory, IsOtherForm, item,
                            [&](uint8_t masterID, MCLogInventoryStat& stat, int32_t itemNum)
                            {
                                mech.PlaceItem(masterID, itemNum, stat.Hits);
                                WriteItemBlock(file, itemNum);
                                file.WriteIdUChar("MasterID", masterID);
                                stat.ItemNum = itemNum;
                                ++mech.NumOther;
                            });
        WriteInventoryItems(*mech.Inventory, IsWeaponForm, item,
                            [&](uint8_t masterID, MCLogInventoryStat& stat, int32_t itemNum)
                            {
                                mech.PlaceItem(masterID, itemNum, stat.Hits);
                                WriteItemBlock(file, itemNum);
                                file.WriteIdUChar("MasterID", masterID);
                                stat.ItemNum = itemNum;
                                file.WriteIdUChar("FacesForward",
                                                  mech.Inventory->GetFacing(static_cast<uint8_t>(itemNum)));
                                ++mech.NumWeapons;
                            });
        WriteInventoryItems(*mech.Inventory, IsAmmoForm, item,
                            [&](uint8_t masterID, MCLogInventoryStat& stat, int32_t itemNum)
                            {
                                mech.PlaceItem(masterID, itemNum, stat.Hits);
                                WriteItemBlock(file, itemNum);
                                file.WriteIdUChar("MasterID", masterID);
                                file.WriteIdLong("Amount", -1);
                                stat.ItemNum = itemNum;
                                ++mech.NumAmmo;
                            });

        file.WriteBlock("InventoryInfo");
        file.WriteIdUChar("NumOther", mech.NumOther);
        file.WriteIdUChar("NumWeapons", mech.NumWeapons);
        file.WriteIdUChar("NumAmmo", mech.NumAmmo);

        for (int32_t location = 0; location < 8; location++)
        {
            file.WriteBlock(MechLocationNames[static_cast<size_t>(location)]);
            file.WriteIdUChar("CASE", static_cast<uint8_t>(mech.HasCase[location]));
            file.WriteIdUChar("CurInternalStructure", mech.Internals[location].CurArmor);
            file.WriteIdUChar("HotSpotNumber", mech.HotSpotNumber[location]);

            for (int32_t space = 0; space < NumLocationCriticalSpaces[location]; space++)
            {
                const MCLogMech::ItemSlot& slot = mech.ItemSlots[location][space];
                const std::array<uint8_t, 2> values = {slot.Row, slot.Column};
                file.WriteIdUCharArray(std::format("Component:{}", space), values);
            }
        }

        CopyProfileName(mech.ProfileName, fileName);
        file.Close();
        return 0;
    }

    int32_t LogisticsVehicleProfileWriter(std::string_view fileName, MCLogVehicle& vehicle, bool writeRequired)
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(TempFit(fileName)); result != 0)
        {
            return result;
        }

        file.WriteBlock("Header");
        file.WriteIdString("FileType", "GroundVehicleProfile");
        file.WriteBlock("General");
        file.WriteIdString("Name", vehicle.FileName);
        file.WriteIdFloat("CurTonnage", vehicle.CurTonnage);
        file.WriteIdLong("NameIndex", vehicle.NameIndex);
        file.WriteIdString("icon", vehicle.IconName);
        file.WriteIdChar("Status", vehicle.Status);
        file.WriteIdString("Crew", vehicle.Crew);
        file.WriteIdULong("Chassis", vehicle.Chassis);
        file.WriteIdBoolean("Assigned", vehicle.Assigned);
        file.WriteIdBoolean("NotMineYet", vehicle.NotMineYet);
        file.WriteIdBoolean("Deployed", vehicle.Deployed);

        if (writeRequired)
        {
            file.WriteIdBoolean("Required", vehicle.Required);
        }

        file.WriteIdLong("DescIndex", vehicle.DescIndex);
        file.WriteBlock("Engine");
        file.WriteIdFloat("Tonnage", vehicle.EngineTonnage);
        file.WriteIdULong("Rating", vehicle.EngineRating);
        file.WriteIdUChar("MaxMoveSpeed", vehicle.MaxMoveSpeed);
        file.WriteBlock("Armor");
        file.WriteIdUChar("Type", vehicle.ArmorType);
        file.WriteIdFloat("Tonnage", vehicle.ArmorTonnage);

        for (size_t location = 0; location < VehicleLocationNames.size(); location++)
        {
            file.WriteBlock(VehicleLocationNames[location]);
            file.WriteIdUChar("CurInternalStructure", vehicle.CurInternalStructure[location]);
            file.WriteIdUChar("MaxArmorPoints", vehicle.MaxArmorPoints[location]);
            file.WriteIdUChar("CurArmorPoints", vehicle.CurArmorPoints[location]);
        }

        // Port: the counts are the inventory's, as the mech's writer counts them: a match's weapon bans can take items
        // off (MCWeaponBans.h), and a vehicle from a deploy message has the sender's items under its stock counts.
        // Where they were the inventory's (every vehicle before), the file is as before.
        vehicle.NumOther = CountInventoryItems(*vehicle.Inventory, IsOtherForm);
        vehicle.NumWeapons = CountInventoryItems(*vehicle.Inventory, IsWeaponForm);
        vehicle.NumAmmo = CountInventoryItems(*vehicle.Inventory, IsAmmoForm);
        file.WriteBlock("InventoryInfo");
        file.WriteIdUChar("NumOther", vehicle.NumOther);
        file.WriteIdUChar("NumWeapons", vehicle.NumWeapons);
        file.WriteIdUChar("NumAmmo", vehicle.NumAmmo);

        int32_t item = 0;
        WriteInventoryItems(*vehicle.Inventory, IsOtherForm, item,
                            [&file](uint8_t masterID, const MCLogInventoryStat&, int32_t itemNum)
                            {
                                WriteItemBlock(file, itemNum);
                                file.WriteIdUChar("MasterID", masterID);
                            });
        WriteInventoryItems(*vehicle.Inventory, IsWeaponForm, item,
                            [&file](uint8_t masterID, MCLogInventoryStat& stat, int32_t itemNum)
                            {
                                WriteItemBlock(file, itemNum);
                                file.WriteIdUChar("MasterID", masterID);
                                file.WriteIdUChar("FacesForward", stat.Facing);
                            });
        WriteInventoryItems(*vehicle.Inventory, IsAmmoForm, item,
                            [&file](uint8_t masterID, const MCLogInventoryStat&, int32_t itemNum)
                            {
                                WriteItemBlock(file, itemNum);
                                file.WriteIdUChar("MasterID", masterID);
                                file.WriteIdLong("Amount", -1);
                            });

        CopyProfileName(vehicle.ProfileName, fileName);
        file.Close();
        return 0;
    }

    int32_t LogisticsWarriorProfileWriter(std::string_view fileName, MCLogWarrior& warrior)
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(TempFit(fileName)); result != 0)
        {
            return result;
        }

        file.WriteBlock("General");
        file.WriteIdString("Name", warrior.Name);
        file.WriteIdString("Callsign", warrior.Callsign);
        file.WriteIdLong("paintScheme", warrior.PaintScheme);
        file.WriteIdString("pilotAudio", warrior.PilotAudio);
        file.WriteIdString("pilotVideo", warrior.PilotVideo);
        file.WriteIdString("Picture", warrior.Picture);
        file.WriteIdString("Brain", warrior.Brain);
        file.WriteIdBoolean("Assigned", warrior.Assigned);
        file.WriteIdBoolean("NotMineYet", warrior.NotMineYet);
        file.WriteIdLong("NameIndex", warrior.NameIndex);
        file.WriteIdLong("DescIndex", warrior.DescIndex);
        file.WriteBlock("PersonalityTraits");
        file.WriteIdChar("Professionalism", warrior.Personality[0]);
        file.WriteIdChar("Decorum", warrior.Personality[1]);
        file.WriteIdChar("Aggressiveness", warrior.Personality[2]);
        file.WriteIdChar("Courage", warrior.Personality[3]);
        WriteSkills(file, "Skills", [&warrior](size_t i) { return warrior.Skills[i]; });
        WriteSkills(file, "OriginalSkills",
                    [&warrior](size_t i) { return static_cast<char>(warrior.OriginalSkills[i]); });
        WriteSkills(file, "LatestSkills", [&warrior](size_t i) { return warrior.StartingSkills[i]; });
        file.WriteBlock("SkillPoints");

        for (size_t i = 0; i < SkillNames.size(); i++)
        {
            file.WriteIdFloat(SkillNames[i], warrior.SkillPoints[i]);
        }

        file.WriteBlock("Status");
        file.WriteIdChar("Wounds", static_cast<char>(static_cast<int32_t>(warrior.Wounds)));
        CopyProfileName(warrior.FileName, fileName);
        file.Close();
        return 0;
    }

    int32_t LogisticsSaveGame(std::string_view fileName)
    {
        MCLogistics& logistics = *Mission()->Logistics;
        const std::string fitName = TempFit(fileName);
        MCFitIniFile file;

        if (const int32_t result = file.Create(fitName); result != 0)
        {
            return result;
        }

        file.WriteBlock("Planet");
        file.WriteIdLong("Setting", CurPlanet);
        file.WriteBlock("General");
        file.WriteIdString("PurchaseFile", CurPlanet == 0 ? "purchase" : "xpur");
        file.WriteIdLong("MissionNumber", logistics.CurrentMission);
        file.WriteIdFloat("LastScenarioTime", TotalScenarioTime);
        file.WriteIdFloat("LastLogisticsTime", TotalLogisticsTime);
        file.WriteBlock("ResourcePoints");
        file.WriteIdULong("numPoints", static_cast<uint32_t>(ResourcePoints));

        uint32_t numWarriors = 0;
        uint32_t numAssWarriors = 0;

        if (const int32_t result = WriteWarriors(file, numWarriors, numAssWarriors); result != 0)
        {
            return result;
        }

        // Every mech: the unassigned ones, then the force; every vehicle the same.
        auto unassigned = [](MCLogPart& part) { return part.Assigned == 0; };
        auto assigned = [](MCLogPart& part) { return part.Assigned != 0; };
        file.WriteBlock("Mechs");
        const uint32_t numMechs = CountParts(logistics.MechList->Mechs, unassigned);
        file.WriteIdULong("NumMechs", numMechs);

        if (const int32_t result = WriteParts(logistics.MechList->Mechs, unassigned,
                                              [&](MCLogMech& mech, uint32_t index)
                                              {
                                                  const uint32_t packet = index + numAssWarriors + numWarriors;
                                                  WritePacketBlock(file, "Mech", index, packet);
                                                  return LogisticsMechProfileWriter(ProfileName(packet), mech, true);
                                              });
            result != 0)
        {
            return result;
        }

        file.WriteBlock("AssMechs");
        const uint32_t numAssMechs = CountParts(logistics.ForceMechList->Mechs, assigned);
        file.WriteIdULong("NumAssMechs", numAssMechs);

        if (const int32_t result = WriteParts(logistics.ForceMechList->Mechs, assigned,
                                              [&](MCLogMech& mech, uint32_t index)
                                              {
                                                  const uint32_t packet =
                                                      index + numMechs + numAssWarriors + numWarriors;
                                                  WritePacketBlock(file, "Mech", index + numMechs, packet);
                                                  return LogisticsMechProfileWriter(ProfileName(packet), mech, true);
                                              });
            result != 0)
        {
            return result;
        }

        file.WriteBlock("Vehicles");
        const uint32_t numVehicles = CountParts(logistics.VehicleList->Vehicles, unassigned);
        file.WriteIdULong("NumVehicles", numVehicles);

        if (const int32_t result =
                WriteParts(logistics.VehicleList->Vehicles, unassigned,
                           [&](MCLogVehicle& vehicle, uint32_t index)
                           {
                               const uint32_t packet = index + numAssMechs + numMechs + numAssWarriors + numWarriors;
                               WritePacketBlock(file, "Vehicle", index, packet);
                               return LogisticsVehicleProfileWriter(ProfileName(packet), vehicle, true);
                           });
            result != 0)
        {
            return result;
        }

        file.WriteBlock("AssVehicles");
        const uint32_t numAssVehicles = CountParts(logistics.ForceVehicleList->Vehicles, assigned);
        file.WriteIdULong("NumAssVehicles", numAssVehicles);

        if (const int32_t result =
                WriteParts(logistics.ForceVehicleList->Vehicles, assigned,
                           [&](MCLogVehicle& vehicle, uint32_t index)
                           {
                               const uint32_t packet =
                                   numVehicles + numWarriors + numAssWarriors + numMechs + numAssMechs + index;
                               WritePacketBlock(file, "Vehicle", numVehicles + index, packet);
                               return LogisticsVehicleProfileWriter(ProfileName(packet), vehicle, true);
                           });
            result != 0)
        {
            return result;
        }

        WriteInventoryComponents(file, logistics);
        file.Close();

        const std::string purchaseName = GamePath(SavePath, fileName, ".pur");

        if (const int32_t result = WritePurchaseFile(purchaseName); result != 0)
        {
            return result;
        }

        MCPacketFile packFile;

        if (const int32_t result = packFile.Create(GamePath(SavePath, fileName, ".sav")); result != 0)
        {
            return result;
        }

        const auto numProfiles =
            static_cast<int32_t>(numVehicles + numWarriors + numAssWarriors + numMechs + numAssMechs + numAssVehicles);

        if (const int32_t result = PackGame(packFile, fitName, numProfiles, purchaseName); result != 0)
        {
            return result;
        }

        packFile.Close();
        DeleteFile(fitName);
        DeleteFile(purchaseName);
        DeleteProfiles(numProfiles);
        return 0;
    }
}
