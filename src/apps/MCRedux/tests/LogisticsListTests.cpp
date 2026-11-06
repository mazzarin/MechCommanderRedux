#include "stdafx.h"
#include "MCTest.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCForceMessages.h"
#include "main/MCLogistics.h"

namespace
{
    /// <summary>A logistics layer with empty lists and no screens, as <c>GlobalLog()</c> while it lives.</summary>
    class LogisticsScope
    {
    public:
        LogisticsScope() : _Saved(GlobalLog()) { SetGlobalLog(_Logistics); }

        ~LogisticsScope() { SetGlobalLog(_Saved); }

        LogisticsScope(const LogisticsScope&) = delete;
        LogisticsScope& operator=(const LogisticsScope&) = delete;

    private:
        std::weak_ptr<MCLogistics> _Saved;
        std::shared_ptr<MCLogistics> _Logistics = std::make_shared<MCLogistics>();

    public:
        MCLogistics& Logistics = *_Logistics;
    };

    /// <summary>A mech record with no profile behind it.</summary>
    std::shared_ptr<MCLogMech> Mech(int32_t sortKey, float tonnage, bool assigned = false)
    {
        auto mech = std::make_shared<MCLogMech>();
        mech->SortKey = sortKey;
        mech->CurTonnage = tonnage;
        mech->Assigned = assigned;
        mech->PilotIndex = -1;
        return mech;
    }

    /// <summary>A vehicle record with no profile behind it.</summary>
    std::shared_ptr<MCLogVehicle> Vehicle(float tonnage, bool assigned = false)
    {
        auto vehicle = std::make_shared<MCLogVehicle>();
        vehicle->CurTonnage = tonnage;
        vehicle->Assigned = assigned;
        return vehicle;
    }

    /// <summary>A pilot record with no profile behind it.</summary>
    std::shared_ptr<MCLogWarrior> Warrior(int32_t rank, std::string_view callsign, int32_t id = 0,
                                          bool assigned = false)
    {
        auto warrior = std::make_shared<MCLogWarrior>();
        warrior->Rank = rank;
        warrior->Callsign = callsign;
        warrior->Id = id;
        warrior->Assigned = assigned;
        warrior->Health = MCLogWarrior::FullHealth;
        return warrior;
    }

    /// <summary>The sort keys of <paramref name="list"/>'s mechs in list order.</summary>
    std::vector<int32_t> SortKeys(const MCLogMechList& list)
    {
        std::vector<int32_t> keys;

        for (const std::shared_ptr<MCLogMech>& mech : list.Mechs)
        {
            keys.push_back(mech->SortKey);
        }

        return keys;
    }

    /// <summary>The tonnages of <paramref name="list"/>'s vehicles in list order.</summary>
    std::vector<float> Tonnages(const MCLogVehicleList& list)
    {
        std::vector<float> tonnages;

        for (const std::shared_ptr<MCLogVehicle>& vehicle : list.Vehicles)
        {
            tonnages.push_back(vehicle->CurTonnage);
        }

        return tonnages;
    }

    /// <summary>The callsigns of <paramref name="list"/>'s pilots in list order.</summary>
    std::vector<std::string> Callsigns(const MCLogWarriorList& list)
    {
        std::vector<std::string> callsigns;

        for (const std::shared_ptr<MCLogWarrior>& warrior : list.Warriors)
        {
            callsigns.push_back(warrior->Callsign);
        }

        return callsigns;
    }

    /// <summary>A copy record for an inventory (no master component list needed).</summary>
    std::shared_ptr<MCLogInventoryStat> Copy(uint8_t statID, int32_t itemNum)
    {
        auto stat = std::make_shared<MCLogInventoryStat>();
        stat->StatID = statID;
        stat->ItemNum = itemNum;
        stat->Amount = 1;
        return stat;
    }

    /// <summary>An item record of component <paramref name="masterID"/> holding <paramref name="copies"/>.</summary>
    std::shared_ptr<MCLogInventoryItem> Item(uint8_t masterID, std::vector<std::shared_ptr<MCLogInventoryStat>> copies)
    {
        auto item = std::make_shared<MCLogInventoryItem>();
        item->MasterID = masterID;
        item->Count = static_cast<int32_t>(copies.size());
        item->Stats = std::move(copies);
        return item;
    }

    /// <summary>The DirectInput scan codes of <paramref name="word"/> (letters only).</summary>
    std::vector<int16_t> ScanCodes(std::string_view word)
    {
        // A..Z on a US keyboard.
        static constexpr std::array<int16_t, 26> letters = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                                            49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
        std::vector<int16_t> codes;

        for (const char letter : word)
        {
            codes.push_back(letters[static_cast<size_t>(letter - 'A')]);
        }

        return codes;
    }
}

TEST_CASE("logistics: a mech list keeps its mechs by tonnage when sorted, first when not")
{
    MCLogMechList list;
    list.AddMech(Mech(1, 50.0f), true);
    list.AddMech(Mech(2, 30.0f), true);
    list.AddMech(Mech(3, 70.0f), true);
    // An equal tonnage goes before the mech it equals.
    list.AddMech(Mech(4, 50.0f), true);
    CHECK(SortKeys(list) == (std::vector<int32_t>{2, 4, 1, 3}));
    list.AddMech(Mech(5, 90.0f), false);
    CHECK(SortKeys(list) == (std::vector<int32_t>{5, 2, 4, 1, 3}));

    // Positions: a negative one reads the first mech (the original walked from the head); past the end there is none.
    std::shared_ptr<MCLogMech> mech;
    CHECK_EQ(list.GetMechInfo(2, mech), 0);
    CHECK_EQ(mech->SortKey, 4);
    CHECK_EQ(list.GetMechInfo(-1, mech), 0);
    CHECK_EQ(mech->SortKey, 5);
    CHECK_EQ(list.GetMechInfo(5, mech), -1);
    CHECK(mech == nullptr);
    CHECK_EQ(list.GetMechPilotIndex(9), -1);

    // Taking one out keeps it alive; removing deletes it.
    std::shared_ptr<MCLogMech> extracted = list.ExtractMech(1);
    REQUIRE(extracted != nullptr);
    CHECK_EQ(extracted->SortKey, 2);
    CHECK(list.ExtractMech(9) == nullptr);
    CHECK_EQ(list.RemoveMech(*extracted), -1);
    CHECK_EQ(list.GetMechIndex(extracted), -1);
    CHECK_EQ(list.RemoveMech(0), 0);
    CHECK(SortKeys(list) == (std::vector<int32_t>{4, 1, 3}));
    CHECK_EQ(list.RemoveMech(3), -1);
    list.Clear();
    CHECK_EQ(list.GetMechCount(), 0);
}

TEST_CASE("logistics: a pilot list orders pilots by rank, then callsign, and heals the living")
{
    MCLogWarriorList list;
    list.AddWarrior(Warrior(1, "Hunter", 1), true);
    list.AddWarrior(Warrior(0, "Lynx", 2), true);
    list.AddWarrior(Warrior(1, "Beast", 3), true);
    list.AddWarrior(Warrior(2, "Ace", 4), true);
    list.AddWarrior(Warrior(1, "Zed", 5), true);
    CHECK(Callsigns(list) == (std::vector<std::string>{"Lynx", "Beast", "Hunter", "Zed", "Ace"}));
    // Unsorted: at the head.
    list.AddWarrior(Warrior(3, "Rookie", 6), false);
    CHECK_EQ(list.Warriors.front()->Callsign, std::string("Rookie"));
    CHECK(list.Exists("Beast"));
    CHECK(!list.Exists("beast"));
    CHECK_EQ(list.GetID(1), 2);
    CHECK_EQ(list.GetID(6), -1);

    // A pilot is removed by its id compared as a byte (OB-098): id 300 is never found as 300 & 0xff.
    list.AddWarrior(Warrior(0, "Stray", 300), false);
    CHECK_EQ(list.RemoveWarrior(static_cast<uint8_t>(300)), -1);
    CHECK_EQ(list.RemoveWarrior(3), 0);
    CHECK(!list.Exists("Beast"));
    std::string profile = "kept";
    CHECK_EQ(list.GetWarriorProfile(99, profile), -1);
    CHECK_EQ(profile, std::string("kept"));

    // Between missions the living, unsold pilots heal; a pilot can't heal below no wounds.
    MCLogWarrior& hurt = *list.Warriors.back();
    hurt.SetWounds(4);
    CHECK_EQ(hurt.Health, 2.0f);
    MCLogWarrior& scratched = *list.Warriors.front();
    scratched.SetWounds(1);
    MCLogWarrior& dead = *list.Warriors[1];
    dead.SetWounds(6);
    CHECK_EQ(dead.WarriorStatus, MCLogWarrior::StatusKilled);
    CHECK(dead.Sold);
    CHECK_EQ(dead.Health, 0.0f);
    list.Heal(2);
    CHECK_EQ(hurt.Wounds, 2.0f);
    CHECK_EQ(hurt.Health, 4.0f);
    CHECK_EQ(scratched.Wounds, 0.0f);
    CHECK_EQ(scratched.Health, MCLogWarrior::FullHealth);
    CHECK_EQ(dead.Health, 0.0f);
}

TEST_CASE("logistics: reordering moves units changing side next to each other into place (OB-097)")
{
    LogisticsScope scope;
    MCLogistics& logistics = scope.Logistics;

    // Two neighbours assigned in one go: both go to the head of the force, the later in front.
    logistics.MechList->AddMech(Mech(30, 0.0f, true), false);
    logistics.MechList->AddMech(Mech(20, 0.0f, true), false);
    logistics.MechList->AddMech(Mech(10, 0.0f), false);
    logistics.ForceMechList->AddMech(Mech(40, 0.0f, true), false);
    logistics.ReorderMechs();
    CHECK(SortKeys(*logistics.MechList) == (std::vector<int32_t>{10}));
    CHECK(SortKeys(*logistics.ForceMechList) == (std::vector<int32_t>{30, 20, 40}));

    // Two neighbours unassigned in one go: both back in the mech list by sort key.
    for (const std::shared_ptr<MCLogMech>& mech : logistics.ForceMechList->Mechs)
    {
        mech->Assigned = mech->SortKey == 40;
    }

    logistics.ReorderMechs();
    CHECK(SortKeys(*logistics.MechList) == (std::vector<int32_t>{10, 20, 30}));
    CHECK(SortKeys(*logistics.ForceMechList) == (std::vector<int32_t>{40}));

    // Vehicles go back by tonnage.
    logistics.ForceVehicleList->Vehicles.push_back(Vehicle(5.0f));
    logistics.ForceVehicleList->Vehicles.push_back(Vehicle(20.0f));
    logistics.VehicleList->Vehicles.push_back(Vehicle(10.0f));
    logistics.ReorderVehicles();
    CHECK(Tonnages(*logistics.VehicleList) == (std::vector<float>{5.0f, 10.0f, 20.0f}));
    CHECK(logistics.ForceVehicleList->Vehicles.empty());

    // Pilots: assigned ones into the assigned list by rank, unassigned ones back by rank, then callsign.
    logistics.WarriorList->AddWarrior(Warrior(2, "Ace", 1, true), false);
    logistics.WarriorList->AddWarrior(Warrior(0, "Lynx", 2, true), false);
    logistics.WarriorList->AddWarrior(Warrior(1, "Hunter", 3), false);
    logistics.ReorderWarriors();
    CHECK(Callsigns(*logistics.AssignedWarriorList) == (std::vector<std::string>{"Lynx", "Ace"}));
    CHECK(Callsigns(*logistics.WarriorList) == (std::vector<std::string>{"Hunter"}));

    for (const std::shared_ptr<MCLogWarrior>& warrior : logistics.AssignedWarriorList->Warriors)
    {
        warrior->Assigned = false;
    }

    logistics.WarriorList->AddWarrior(Warrior(0, "Bravo", 4), true);
    logistics.ReorderWarriors();
    CHECK(Callsigns(*logistics.WarriorList) == (std::vector<std::string>{"Bravo", "Lynx", "Hunter", "Ace"}));
    CHECK(logistics.AssignedWarriorList->Warriors.empty());
}

TEST_CASE("logistics: a pilot's index shifts with the pilots before it, and required units must be in the drop")
{
    LogisticsScope scope;
    MCLogistics& logistics = scope.Logistics;
    auto first = Mech(0, 50.0f);
    first->PilotIndex = 0;
    first->Chassis = 7;
    auto second = Mech(0, 50.0f);
    second->PilotIndex = 2;
    second->Chassis = 8;
    MCLogMech& deployed = *first;
    logistics.ForceMechList->AddMech(std::move(second), false);
    logistics.ForceMechList->AddMech(std::move(first), false);
    logistics.ShiftPilots(1, 3);
    CHECK_EQ(logistics.ForceMechList->GetMechPilotIndex(0), 0);
    CHECK_EQ(logistics.ForceMechList->GetMechPilotIndex(1), 5);

    // A required mech in the mech list needs a deployed force mech of its chassis.
    auto required = Mech(0, 50.0f);
    required->Required = true;
    required->Chassis = 7;
    logistics.MechList->AddMech(std::move(required), false);
    CHECK(!logistics.RequiredAssigned());
    deployed.Deployed = true;
    CHECK(logistics.RequiredAssigned());

    // A required force mech has to be deployed itself.
    logistics.ForceMechList->Mechs.back()->Required = true;
    CHECK(!logistics.RequiredAssigned());
    logistics.ForceMechList->Mechs.back()->Deployed = true;
    CHECK(logistics.RequiredAssigned());

    // A required vehicle needs a force vehicle of its chassis, deployed or not; a required force vehicle must deploy.
    auto vehicle = Vehicle(10.0f);
    vehicle->Required = true;
    vehicle->Chassis = 3;
    logistics.VehicleList->Vehicles.push_back(std::move(vehicle));
    CHECK(!logistics.RequiredAssigned());
    auto forceVehicle = Vehicle(10.0f);
    forceVehicle->Chassis = 3;
    logistics.ForceVehicleList->Vehicles.push_back(std::move(forceVehicle));
    CHECK(logistics.RequiredAssigned());
    logistics.ForceVehicleList->Vehicles.front()->Required = true;
    CHECK(!logistics.RequiredAssigned());

    // In multiplayer nothing is required.
    logistics.MultiplayerInitialized = true;
    CHECK(logistics.RequiredAssigned());
    logistics.MultiplayerInitialized = false;
}

TEST_CASE("logistics: a vehicle leaving the force keeps the earlier vehicles' drop slots (OB-183)")
{
    LogisticsScope scope;
    MCLogistics& logistics = scope.Logistics;

    // Force vehicles 0..3; vehicles 0, 1 and 3 are deployed, a mech holds another slot.
    for (auto& lance : logistics.DeploySlots)
    {
        lance.fill({-1, -1});
    }

    logistics.DeploySlots[0][0].Vehicle = 0;
    logistics.DeploySlots[0][1].Vehicle = 1;
    logistics.DeploySlots[1][2].Vehicle = 3;
    logistics.DeploySlots[2][3].Unit = 0;

    // Vehicle 1 leaves: its slot empties, vehicle 0 keeps its slot, vehicle 3 is now the force's third (2).
    logistics.RemoveVehicleFromSlots(1);
    CHECK_EQ(logistics.DeploySlots[0][0].Vehicle, 0);
    CHECK_EQ(logistics.DeploySlots[0][1].Vehicle, -1);
    CHECK_EQ(logistics.DeploySlots[1][2].Vehicle, 2);
    CHECK_EQ(logistics.DeploySlots[2][3].Unit, 0);
    CHECK_EQ(logistics.DeploySlots[2][3].Vehicle, -1);

    // An undeployed vehicle leaving moves only the later ones.
    logistics.RemoveVehicleFromSlots(1);
    CHECK_EQ(logistics.DeploySlots[0][0].Vehicle, 0);
    CHECK_EQ(logistics.DeploySlots[1][2].Vehicle, 1);
}

TEST_CASE("logistics: an inventory's lookups stop at the copies it holds (OB-091)")
{
    MCInventoryList list;
    std::vector<std::shared_ptr<MCLogInventoryStat>> copies;
    copies.push_back(Copy(0, 0));
    copies.push_back(Copy(1, 1));
    list.Items.push_back(Item(9, std::move(copies)));
    std::vector<std::shared_ptr<MCLogInventoryStat>> single;
    single.push_back(Copy(2, 2));
    list.Items.push_back(Item(4, std::move(single)));

    // The lookups find a copy among the first Count copies of each item.
    CHECK_EQ(list.GetMasterID(1), 9);
    CHECK_EQ(list.GetMasterID(2), 4);
    CHECK_EQ(list.GetMasterID(7), 0xff);
    CHECK_EQ(list.GetIndexFromMasterID(4), 1);
    CHECK(list.GetItemStatIndex(2) == list.Items[1]);
    list.Items[0]->Count = 1;
    CHECK_EQ(list.GetMasterID(1), 0xff);
    CHECK_EQ(list.HitItem(1, 3), -1);

    // A count above the copies (AddCountToItem changes it alone) finds no copy past them.
    list.AddCountToItem(5, 4);
    CHECK_EQ(list.GetItemCount(4), 6);
    CHECK_EQ(list.GetItemStatID(4, 0), 2);
    CHECK_EQ(list.GetItemStatID(4, 3), -1);
    list.AddCountToItem(-10, 4);
    CHECK_EQ(list.GetItemCount(4), 0);

    // An id below every item's is not there (the list runs from the highest id down).
    CHECK_EQ(list.GetItemCount(2), 0);
    CHECK_EQ(list.RemoveItem(2, -1), -1);
    CHECK_EQ(list.GetMasterIDFromIndex(5), 0xff);
    CHECK_EQ(list.GetMasterIDFromIndex(-1), 9);
    list.Clear();
    CHECK_EQ(list.NumItems(), 0);
    CHECK_EQ(list.GetMasterIDFromIndex(-1), 0xff);
}

TEST_CASE("logistics: deploy and remove force messages pack and unpack")
{
    MCDeployForce mech;
    mech.IsMech = true;
    mech.ClanSide = true;
    mech.NameVariant = 2;
    mech.Lance = 1;
    mech.Slot = 3;
    mech.NameIndex = 17;
    mech.PilotNameIndex = 5;
    mech.Items = {0x90, 0x7b, 0x0d};
    const std::vector<uint8_t> bytes = PackDeployForce(mech);

    // The guaranteed header (type 32), the flags, the indexes, a pad byte, the count, then each id and a zero byte.
    REQUIRE_EQ(bytes.size(), 0xdu + 3 * 2);
    CHECK_EQ(bytes[0], 32);
    CHECK_EQ(bytes[1], 0x10);
    CHECK_EQ(bytes[8], 1 | 2 | (2 << 2) | (1 << 4) | (3 << 6));
    CHECK_EQ(bytes[9], 17);
    CHECK_EQ(bytes[10], 5);
    CHECK_EQ(bytes[11], 0);
    CHECK_EQ(bytes[12], 3);
    CHECK_EQ(bytes[0xd], 0x90);
    CHECK_EQ(bytes[0xe], 0);
    CHECK_EQ(bytes[0x11], 0x0d);

    REQUIRE(UnpackDeployForce(bytes).has_value());
    const MCDeployForce back = *UnpackDeployForce(bytes);
    CHECK(back.IsMech);
    CHECK(back.ClanSide);
    CHECK_EQ(back.NameVariant, 2);
    CHECK_EQ(back.Lance, 1);
    CHECK_EQ(back.Slot, 3);
    CHECK_EQ(back.NameIndex, 17);
    CHECK_EQ(back.PilotNameIndex, 5);
    CHECK(back.Items == mech.Items);

    // A vehicle of the Inner Sphere: no variant, no pilot. A count over 255 sends its low byte's worth.
    MCDeployForce vehicle;
    vehicle.NameIndex = 4;
    vehicle.Items.assign(300, 0x41);
    const std::vector<uint8_t> vehicleBytes = PackDeployForce(vehicle);
    CHECK_EQ(vehicleBytes.size(), 0xdu + 44 * 2);
    REQUIRE(UnpackDeployForce(vehicleBytes).has_value());
    const MCDeployForce vehicleBack = *UnpackDeployForce(vehicleBytes);
    CHECK(!vehicleBack.IsMech);
    CHECK(!vehicleBack.ClanSide);
    CHECK_EQ(vehicleBack.PilotNameIndex, 0xff);
    CHECK_EQ(vehicleBack.Items.size(), 44u);

    const std::vector<uint8_t> remove = PackRemoveForce(2, 1);
    REQUIRE_EQ(remove.size(), 10u);
    CHECK_EQ(remove[0], 33);
    CHECK_EQ(remove[1], 0x10);
    CHECK(UnpackRemoveForce(remove) == std::optional<int32_t>(9));

    // A message from another machine that is shorter than it says (issue #25) unpacks to nothing.
    CHECK(!UnpackDeployForce(std::span(bytes).first(bytes.size() - 1)).has_value());
    CHECK(!UnpackDeployForce(std::span(bytes).first(0xc)).has_value());
    CHECK(!UnpackRemoveForce(std::span(remove).first(9)).has_value());
}

TEST_CASE("logistics: a cheat code typed in full fires once, a wrong key starts over")
{
    LogisticsScope scope;
    MCLogistics& logistics = scope.Logistics;
    const bool cheats = CheatsOn;
    const int32_t points = ResourcePoints;
    CheatsOn = true;
    LogCurCheatChar = 0;
    ResourcePoints = 100;

    // POUNDOFFLESH: a million resource points.
    for (const int16_t key : ScanCodes("POUNDOFFLESH"))
    {
        logistics.ProcessCheatCode(key);
    }

    CHECK_EQ(ResourcePoints, 1000100);
    CHECK_EQ(LogCurCheatChar, 0);

    // A wrong key part way: nothing fires, the next key starts a code from its first letter.
    for (const int16_t key : ScanCodes("POUNDX"))
    {
        logistics.ProcessCheatCode(key);
    }

    CHECK_EQ(LogCurCheatChar, 0);

    for (const int16_t key : ScanCodes("OFFLESH"))
    {
        logistics.ProcessCheatCode(key);
    }

    CHECK_EQ(ResourcePoints, 1000100);

    // With the cheats off, nothing is matched.
    CheatsOn = false;

    for (const int16_t key : ScanCodes("POUNDOFFLESH"))
    {
        logistics.ProcessCheatCode(key);
    }

    CHECK_EQ(ResourcePoints, 1000100);
    CheatsOn = cheats;
    ResourcePoints = points;
    LogCurCheatChar = 0;
}
