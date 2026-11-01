#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "lib/MCIDString.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "object/MCMasterComponent.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectType.h"
#include "object/MCObjectTypeManager.h"

namespace
{
    /// <summary>
    /// What the logistics lists need to read the retail profiles: the FastFiles, the master component list, the
    /// profile path and a <c>globalLogPtr</c> with its block store (no screens, so the lists are read without widgets).
    /// </summary>
    class LogisticsFixture
    {
    public:
        LogisticsFixture()
        {
            MCTestGame::OpenFastFiles();

            if (MasterComponentList.empty())
            {
                InitMasterComponentListExcel(GamePath(ObjectPath, "compbas", ".csv"), 0xff, 1.0f, 0.0f);
            }

            ProfilePath = "data\\missions\\profiles\\";
            _Logistics = std::make_shared<MCLogistics>();
            _Saved = GlobalLog();
            SetGlobalLog(_Logistics);
        }

        ~LogisticsFixture() { SetGlobalLog(_Saved); }

    private:
        std::shared_ptr<MCLogistics> _Logistics;
        std::weak_ptr<MCLogistics> _Saved;
    };

    /// <summary>Empties every critical slot of <paramref name="mech"/>, as the profile writer does before placing.</summary>
    void ClearSlots(MCLogMech& mech)
    {
        for (auto& location : mech.ItemSlots)
        {
            for (auto& slot : location)
            {
                slot = {0xff, 0, 0xff};
            }
        }
    }

    /// <summary>The used critical slots of <paramref name="location"/> that hold <paramref name="masterID"/>.</summary>
    int32_t CountHeld(const MCLogMech& mech, int32_t location, uint8_t masterID)
    {
        int32_t count = 0;

        for (const MCLogMech::ItemSlot& slot : mech.ItemSlots[location])
        {
            if (slot.Row != 0xff && slot.MasterID == masterID)
            {
                ++count;
            }
        }

        return count;
    }

    /// <summary>The first master component of <paramref name="form"/>, or -1.</summary>
    int32_t FindComponent(MCComponentForm form, int32_t first, int32_t last)
    {
        for (int32_t id = first; id <= last && id < NumMasterComponents(); ++id)
        {
            if (MasterComponentList[id].Form == form)
            {
                return id;
            }
        }

        return -1;
    }
}

TEST_CASE("game: logistics reads a mech profile into its list")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    LogisticsFixture fixture;

    MCLogMechList mechs;
    std::shared_ptr<MCLogMech> mech = mechs.AddMech("PM100100", false, true, false);
    REQUIRE(mech != nullptr);
    CHECK_EQ(mechs.GetMechCount(), 1);
    CHECK_EQ(mech->PartType, 1);
    CHECK_EQ(mech->ProfileName, std::string("PM100100"));
    CHECK_EQ(mech->MechName, std::string("COM-A"));
    CHECK_EQ(mech->CurTonnage, 25.0f);
    CHECK_EQ(mech->NameIndex, 4);
    CHECK_EQ(mech->SortKey, MechSort[4] * 3);
    CHECK_EQ(mech->ChassisBR, 25);
    CHECK_EQ(mech->BaseResourcePoints, 2500);
    CHECK(mech->ResourcePoints > mech->BaseResourcePoints);
    CHECK_EQ(mech->PilotIndex, -1);
    CHECK(mech->Internals[0].MaxArmor > 0);
    CHECK(!mech->WeightClassName.empty());
    CHECK(!mech->FileName.empty());

    // 13 other items, 5 weapons and 4 ammo bins, as copies of their components.
    CHECK_EQ(mech->NumOther, 13);
    CHECK_EQ(mech->NumWeapons, 5);
    CHECK_EQ(mech->NumAmmo, 4);
    int32_t copies = 0;

    const auto& items = mech->Inventory->Items;

    for (size_t index = 0; index < items.size(); ++index)
    {
        if (index + 1 < items.size())
        {
            CHECK(items[index]->MasterID > items[index + 1]->MasterID);
        }

        copies += static_cast<int32_t>(items[index]->Stats.size());
    }

    CHECK_EQ(copies, 22);
    CHECK_EQ(mech->Inventory->GetItemCount(4), 2);
    CHECK_EQ(mech->Inventory->GetMasterID(static_cast<uint8_t>(mech->Inventory->GetItemStatID(4, 0))), 4);
    CHECK(mech->BattleRating >= mech->ChassisBR);

    std::shared_ptr<MCLogMech> found;
    CHECK_EQ(mechs.GetMechInfo(0, found), 0);
    CHECK(found == mech);
    CHECK_EQ(mechs.GetMechIndex(mech), 0);
    mechs.Clear();
    CHECK_EQ(mechs.GetMechCount(), 0);
}

TEST_CASE("game: logistics reads a vehicle profile into its list")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    LogisticsFixture fixture;

    MCFitIniFile file;
    std::string path;
    path = GamePath(ProfilePath, "PV20000", ".fit");
    REQUIRE_EQ(file.Open(path), 0);
    MCLogVehicleList vehicles;
    std::shared_ptr<MCLogVehicle> vehicle = vehicles.AddVehicle(file, false, true, false);
    REQUIRE(vehicle != nullptr);
    CHECK_EQ(vehicles.GetVehicleCount(), 1);
    CHECK_EQ(vehicle->PartType, 2);
    CHECK_EQ(vehicle->Crew, std::string("PCREWA"));
    CHECK_EQ(vehicle->CurTonnage, 5.0f);
    CHECK_EQ(vehicle->NameIndex, 20);
    CHECK_EQ(vehicle->BaseVehicleResourcePoints, 500);
    CHECK(vehicle->VehicleResourcePoints >= 500);
    CHECK_EQ(vehicle->MaxMoveSpeed, 21);
    CHECK_EQ(vehicle->MaxArmorPoints[0], 6);
    CHECK_EQ(vehicle->CurArmorPoints[1], 5);
    vehicles.Clear();
    CHECK_EQ(vehicles.GetVehicleCount(), 0);
}

TEST_CASE("game: logistics inventory lists add, count and remove copies")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    LogisticsFixture fixture;

    MCInventoryList list;
    list.AddItem(4, list.CreateStat(0, 0, 0, 1, 0xff), false);
    list.AddItem(1, list.CreateStat(1, 0, 0, 1, 0xff), false);
    list.AddItem(4, list.CreateStat(2, 3, 0, 1, 0xff), false);
    CHECK_EQ(list.NumItems(), 2);
    CHECK_EQ(list.GetMasterIDFromIndex(0), 4);
    CHECK_EQ(list.GetMasterIDFromIndex(1), 1);
    CHECK_EQ(list.GetIndexFromMasterID(1), 1);
    CHECK_EQ(list.GetItemCount(4), 2);
    // The second copy of 4, numbered above the first, went in front of it.
    CHECK_EQ(list.GetItemStatID(4, 0), 2);
    CHECK_EQ(list.GetItemStatID(4, 1), 0);
    CHECK_EQ(list.GetItemStatID(4, 2), -1);
    CHECK_EQ(list.HitItem(2, 5), 0);
    CHECK_EQ(list.SetStatLoc(2, 3), 0);
    CHECK_EQ(list.GetMasterID(2), 4);
    CHECK_EQ(list.HitItem(9, 1), -1);
    const MCLogInventoryStat& hit = *list.GetItemInfo(0)->Stats.front();
    CHECK_EQ(hit.StatID, 2);
    CHECK_EQ(hit.Hits, 5);
    CHECK_EQ(hit.Amount, 1);
    CHECK_EQ(hit.Location, 3);
    CHECK_EQ(hit.ItemNum, 2);

    // A copy goes (reported as -1); the last copy takes the item with it (0).
    CHECK_EQ(list.RemoveItem(4, -1), -1);
    CHECK_EQ(list.GetItemCount(4), 1);
    CHECK_EQ(list.GetItemStatID(4, 0), 0);
    CHECK_EQ(list.RemoveItem(4, 0), 0);
    CHECK_EQ(list.NumItems(), 1);
    list.Clear();
    CHECK_EQ(list.NumItems(), 0);
}

TEST_CASE("game: logistics spreads weapons over the arms and side torsos")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    LogisticsFixture fixture;

    MCLogMechList mechs;
    std::shared_ptr<MCLogMech> mech = mechs.AddMech("PM100100", false, true, false);
    REQUIRE(mech != nullptr);

    // A large weapon (master id 100) and the first small energy weapon.
    const auto largeID = static_cast<uint8_t>(100);
    REQUIRE(MCLogMech::GetWeaponLarge(largeID));
    int32_t smallID = -1;

    for (int32_t id = 100; id < NumMasterComponents() && smallID < 0; ++id)
    {
        if (MasterComponentList[id].Form == MCComponentForm::WeaponEnergy &&
            !MCLogMech::GetWeaponLarge(static_cast<uint8_t>(id)))
        {
            smallID = id;
        }
    }

    REQUIRE(smallID >= 0);
    const int32_t weaponLocations[4] = {MechLeftTorso, MechRightTorso, MechLeftArm, MechRightArm};

    // Eight large weapons: two in each arm and side torso, not all in the left arm (OB-092).
    ClearSlots(*mech);

    for (int32_t item = 0; item < 8; ++item)
    {
        mech->PlaceItem(largeID, item, 0);
    }

    for (const int32_t location : weaponLocations)
    {
        MCTest::Scope scope(std::to_string(location));
        CHECK_EQ(CountHeld(*mech, location, largeID), 2);
    }

    // The first large weapon goes to a side torso, the first small weapon to an arm.
    ClearSlots(*mech);
    mech->PlaceItem(largeID, 0, 0);
    CHECK_EQ(CountHeld(*mech, MechLeftTorso, largeID), 1);
    mech->PlaceItem(static_cast<uint8_t>(smallID), 1, 0);
    CHECK_EQ(CountHeld(*mech, MechLeftArm, static_cast<uint8_t>(smallID)), 1);

    // Four small weapons: one in each.
    ClearSlots(*mech);

    for (int32_t item = 0; item < 4; ++item)
    {
        mech->PlaceItem(static_cast<uint8_t>(smallID), item, 0);
    }

    for (const int32_t location : weaponLocations)
    {
        MCTest::Scope scope(std::to_string(location));
        CHECK_EQ(CountHeld(*mech, location, static_cast<uint8_t>(smallID)), 1);
    }

    // A full left arm is passed over, and nothing spills into the right arm's first slot.
    ClearSlots(*mech);

    for (MCLogMech::ItemSlot& slot : mech->ItemSlots[MechLeftArm])
    {
        slot = {50, 0, 0};
    }

    mech->ItemSlots[MechRightArm][0] = {51, 0, 0};
    mech->PlaceItem(static_cast<uint8_t>(smallID), 52, 0);
    CHECK_EQ(mech->ItemSlots[MechRightArm][0].Row, 51);
    CHECK_EQ(mech->ItemSlots[MechRightArm][1].Row, 52);

    // With every arm and side torso full, the weapon gets no slot and nothing is overwritten.
    ClearSlots(*mech);

    for (const int32_t location : weaponLocations)
    {
        for (MCLogMech::ItemSlot& slot : mech->ItemSlots[location])
        {
            slot = {50, 0, 0};
        }
    }

    mech->PlaceItem(largeID, 53, 0);

    for (const auto& location : mech->ItemSlots)
    {
        for (const MCLogMech::ItemSlot& slot : location)
        {
            CHECK(slot.Row != 53);
        }
    }

    CHECK_EQ(mech->ItemSlots[MechLeftLeg][0].Row, 0xff);
    mechs.Clear();
}

TEST_CASE("game: logistics gives the Rail Gun and Light Gauss Rifle a critical slot (OB-182)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    LogisticsFixture fixture;

    MCLogMechList mechs;
    std::shared_ptr<MCLogMech> mech = mechs.AddMech("PM100100", false, true, false);
    REQUIRE(mech != nullptr);
    const auto railGun = static_cast<uint8_t>(98);
    const auto lightGauss = static_cast<uint8_t>(99);
    REQUIRE_EQ(MasterComponentList[railGun].Name, std::string("Rail Gun"));
    REQUIRE_EQ(MasterComponentList[lightGauss].Name, std::string("Light Gauss Rifle"));

    // Each takes one slot in an arm or side torso (OB-182: neither got one).
    ClearSlots(*mech);
    mech->PlaceItem(railGun, 0, 0);
    mech->PlaceItem(lightGauss, 1, 0);

    for (const uint8_t weapon : {railGun, lightGauss})
    {
        MCTest::Scope scope(std::to_string(weapon));
        int32_t held = 0;

        for (const int32_t location : {MechLeftArm, MechRightArm, MechLeftTorso, MechRightTorso})
        {
            held += CountHeld(*mech, location, weapon);
        }

        CHECK_EQ(held, 1);
    }

    mechs.Clear();
}

TEST_CASE("game: logistics puts each jump jet in one slot of the leg with fewer")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    LogisticsFixture fixture;

    MCLogMechList mechs;
    std::shared_ptr<MCLogMech> mech = mechs.AddMech("PM100100", false, true, false);
    REQUIRE(mech != nullptr);
    const int32_t jetID = FindComponent(MCComponentForm::JumpJet, 0, 99);
    REQUIRE(jetID >= 0);
    const auto jet = static_cast<uint8_t>(jetID);

    // One jet takes one slot, in the left leg.
    ClearSlots(*mech);
    mech->PlaceItem(jet, 0, 0);
    CHECK_EQ(CountHeld(*mech, MechLeftLeg, jet), 1);
    CHECK_EQ(CountHeld(*mech, MechRightLeg, jet), 0);

    // Four jets: two per leg.
    for (int32_t item = 1; item < 4; ++item)
    {
        mech->PlaceItem(jet, item, 0);
    }

    CHECK_EQ(CountHeld(*mech, MechLeftLeg, jet), 2);
    CHECK_EQ(CountHeld(*mech, MechRightLeg, jet), 2);

    // Another component in the left leg's first slot doesn't stop the jets after it being counted.
    ClearSlots(*mech);
    mech->ItemSlots[MechLeftLeg][0] = {40, 0, 0};

    for (int32_t item = 0; item < 3; ++item)
    {
        mech->PlaceItem(jet, item, 0);
    }

    CHECK_EQ(CountHeld(*mech, MechLeftLeg, jet), 2);
    CHECK_EQ(CountHeld(*mech, MechRightLeg, jet), 1);
    mechs.Clear();
}
