#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTimerManager.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCInventoryBlock.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "main/MCGamePaths.h"
#include "logistics/MCMainMenu.h"
#include "main/MCGameContext.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCWeaponBans.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"
#include "object/MCObjectTypeManager.h"
#include "platform/MCFileSystem.h"

// The shop and the repair bay (P3-log-2): the shop's lists, prices and stock, the unit limit, what a mech can mount,
// the repair order, the session screen's player order, and buying and selling by dragging between the panes.

using namespace MCScreenInput;

namespace
{
    /// <summary>A test context whose GUI system has a 640x480 screen window and timers (no display): for the rows.</summary>
    struct GuiContext
    {
        GuiContext()
        {
            Scope.Context().SetGuiSystem(std::make_unique<MCGuiSystem>());
            GuiSystem().ScreenWidth = 640;
            GuiSystem().ScreenHeight = 480;
            GuiSystem().TimerManager = std::make_unique<MCGuiTimerManager>();
            GuiSystem().MakeScreen(640, 480);
        }

        MCTestContextScope Scope;
    };

    /// <summary>
    /// What the shop lists need to read the retail profiles: the FastFiles, the master component list, the profile path,
    /// a GUI context for their rows, and a <c>GlobalLog()</c> with its block store and empty unit lists (no screens).
    /// </summary>
    class ShopFixture
    {
    public:
        ShopFixture()
        {
            MCTestGame::OpenFastFiles();

            if (MasterComponentList.empty())
            {
                InitMasterComponentListExcel(GamePath(ObjectPath, "compbas", ".csv"), 0xff, 1.0f, 0.0f);
            }

            ProfilePath = "data\\missions\\profiles\\";
            _Saved = GlobalLog();
            SetGlobalLog(_Logistics);
        }

        ~ShopFixture() { SetGlobalLog(_Saved); }

        /// <summary>Makes the test a match playing <paramref name="rules"/> (MultiPlayer()->Played; 0: none).</summary>
        void PlayMatch(uint32_t rules)
        {
            auto game = std::make_shared<MCMultiPlayer>();
            game->Played.Rules = rules;
            _Gui.Scope.Context().SetMultiPlayer(rules != 0 ? std::move(game) : nullptr);
        }

    private:
        GuiContext _Gui;
        std::shared_ptr<MCLogistics> _Logistics = std::make_shared<MCLogistics>();
        std::weak_ptr<MCLogistics> _Saved;

    public:
        MCLogMechList& Mechs = *_Logistics->MechList;
        MCLogVehicleList& Vehicles = *_Logistics->VehicleList;
        MCLogMechList& ForceMechs = *_Logistics->ForceMechList;
        MCLogVehicleList& ForceVehicles = *_Logistics->ForceVehicleList;
    };

    /// <summary>A pilot for hire with <paramref name="rank"/> and <paramref name="callsign"/> (no profile read).</summary>
    std::shared_ptr<MCPurPilotData> Pilot(int32_t rank, std::string_view callsign, int32_t descIndex)
    {
        auto pilot = std::make_shared<MCPurPilotData>();
        pilot->Rank = rank;
        pilot->Callsign = callsign;
        pilot->DescIndex = descIndex;
        return pilot;
    }

    /// <summary>The callsigns of <paramref name="pilots"/> in list order.</summary>
    std::vector<std::string> Callsigns(const MCPurPilotList& pilots)
    {
        std::vector<std::string> names;

        for (const auto& pilot : pilots.Pilots)
        {
            names.push_back(pilot->Callsign);
        }

        return names;
    }

    /// <summary>
    /// A mech profile's price by the shop's rule, read straight from the profile: its ResourcePoints, 40 per armor point,
    /// each item's component price and 50 per internal structure point.
    /// </summary>
    int32_t ProfilePrice(std::string_view name)
    {
        MCFitIniFile file;
        REQUIRE_EQ(file.Open(GamePath(ProfilePath, name, ".fit")), 0);
        int32_t price = 0;
        REQUIRE_EQ(file.SeekBlock("General"), 0);
        REQUIRE_EQ(file.ReadIdLong("ResourcePoints", price), 0);
        REQUIRE_EQ(file.SeekBlock("CurArmorPoints"), 0);

        for (const std::string_view location :
             {"Head", "CenterTorso", "LeftTorso", "RightTorso", "LeftArm", "RightArm", "LeftLeg", "RightLeg",
              "RearCenterTorso", "RearLeftTorso", "RearRightTorso"})
        {
            uint8_t points = 0;
            REQUIRE_EQ(file.ReadIdUChar(location, points), 0);
            price += 40 * points;
        }

        REQUIRE_EQ(file.SeekBlock("InventoryInfo"), 0);
        uint8_t other = 0;
        uint8_t weapons = 0;
        uint8_t ammo = 0;
        file.ReadIdUChar("NumOther", other);
        file.ReadIdUChar("NumWeapons", weapons);
        file.ReadIdUChar("NumAmmo", ammo);

        for (int32_t item = 0; item < other + weapons + ammo; ++item)
        {
            REQUIRE_EQ(file.SeekBlock(std::format("Item:{}", item)), 0);
            uint8_t masterID = 0;
            REQUIRE_EQ(file.ReadIdUChar("MasterID", masterID), 0);
            price += MasterComponentList[masterID].ResourcePoints;
        }

        int32_t internal = 0;

        for (const std::string_view location :
             {"Head", "CenterTorso", "LeftTorso", "RightTorso", "LeftArm", "RightArm", "LeftLeg", "RightLeg"})
        {
            REQUIRE_EQ(file.SeekBlock(location), 0);
            uint8_t points = 0;
            REQUIRE_EQ(file.ReadIdUChar("CurInternalStructure", points), 0);
            internal += points;
        }

        return price + 50 * internal;
    }

    /// <summary>The first master component of <paramref name="form"/> from <paramref name="first"/> on, or -1.</summary>
    int32_t FindComponent(MCComponentForm form, int32_t first = 0)
    {
        for (int32_t id = first; id < NumMasterComponents(); ++id)
        {
            if (MasterComponentList[id].Form == form)
            {
                return id;
            }
        }

        return -1;
    }

    /// <summary>Fills every armor and internal structure location of <paramref name="mech"/> (<paramref name="points"/> each).</summary>
    void FillMech(MCLogMech& mech, uint8_t points)
    {
        for (auto& location : mech.Armor)
        {
            location = {points, points};
        }

        for (auto& location : mech.Internals)
        {
            location = {points, points};
        }
    }
}

TEST_CASE("logistics: the session screen lists player ids in order, empty slots last")
{
    const std::array<uint32_t, 3> ids = {0x30, 0x10, 0x20};
    const auto sorted = MCSessionScreen::SortedPlayerIds(ids);
    const uint32_t none = MCSessionScreen::NoPlayer;
    CHECK(sorted == (std::array<uint32_t, MCSessionScreen::MaxPlayers>{0x10, 0x20, 0x30, none, none, none}));

    // A full session.
    const std::array<uint32_t, 6> full = {6, 5, 4, 3, 2, 1};
    CHECK(MCSessionScreen::SortedPlayerIds(full) ==
          (std::array<uint32_t, MCSessionScreen::MaxPlayers>{1, 2, 3, 4, 5, 6}));
}

TEST_CASE("logistics: a unit sells for half its value in the campaign, all of it in a single mission")
{
    REQUIRE(MultiPlayer() == nullptr);
    const bool saved = Solo;
    Solo = false;
    CHECK_EQ(MCInventoryBlock::SalePrice(2500), 1250);
    CHECK_EQ(MCInventoryBlock::SalePrice(75), 37);
    Solo = true;
    CHECK_EQ(MCInventoryBlock::SalePrice(2500), 2500);
    Solo = saved;
}

TEST_CASE("logistics: pilots for hire are listed by rank, then callsign")
{
    MCPurPilotList pilots;
    pilots.Insert(Pilot(1, "Delta", 1));
    pilots.Insert(Pilot(0, "Alpha", 2));
    pilots.Insert(Pilot(1, "Charlie", 3));
    pilots.Insert(Pilot(2, "Bravo", 4));
    CHECK(Callsigns(pilots) == (std::vector<std::string>{"Alpha", "Charlie", "Delta", "Bravo"}));
    CHECK_EQ(pilots.GetPilotCount(), 4);
    CHECK_EQ(pilots.GetVisiblePilotCount(), 4);

    // Hiring one takes it off the visible list but not off the list.
    pilots.SetPilotStatus(3, MCPurPilotData::Hired);
    CHECK_EQ(pilots.GetVisiblePilotCount(), 3);
    std::shared_ptr<MCPurPilotData> found;
    REQUIRE_EQ(pilots.GetPilotInfo(1, found), 0);
    CHECK_EQ(found->Status, MCPurPilotData::Hired);
    CHECK_EQ(pilots.GetPilotInfo(4, found), -1);
    pilots.SetPilotStatus(4, MCPurPilotData::OffShop);
    CHECK_EQ(pilots.GetVisiblePilotCount(), 2);

    // Original behaviour (OB-165): the callsign walk ignores rank, so a lower-ranked pilot with a later callsign goes
    // after the higher-ranked ones it passes.
    MCPurPilotList quirk;
    quirk.Insert(Pilot(1, "Bravo", 1));
    quirk.Insert(Pilot(0, "Zulu", 2));
    CHECK(Callsigns(quirk) == (std::vector<std::string>{"Bravo", "Zulu"}));
}

namespace
{
    /// <summary>
    /// <paramref name="units"/> made <paramref name="count"/> units with no profile behind them: only their number counts.
    /// </summary>
    template <typename Units> void Fill(Units& units, size_t count)
    {
        using Unit = typename Units::value_type::element_type;
        units.clear();

        for (size_t i = 0; i < count; ++i)
        {
            units.push_back(std::make_unique<Unit>());
        }
    }

    /// <summary>
    /// The shop's player owning <paramref name="spareMechs"/> spare mechs, besides 10 spare vehicles and a force of 3
    /// mechs and 2 vehicles (15 units).
    /// </summary>
    void FillShop(ShopFixture& shop, size_t spareMechs)
    {
        Fill(shop.Mechs.Mechs, spareMechs);
        Fill(shop.Vehicles.Vehicles, 10);
        Fill(shop.ForceMechs.Mechs, 3);
        Fill(shop.ForceVehicles.Vehicles, 2);
    }
}

TEST_CASE("logistics: the unit limit caps a purchase at the room left")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    // MoreUnits off (the tests' default): the original's limit, 50 warned from 40.
    REQUIRE_EQ(GMoreUnits, 0);
    CHECK_EQ(MaxUnits(), 50);
    CHECK_EQ(UnitWarning(), 40);
    FillShop(shop, 30);
    CHECK_EQ(NumUnits(), 45);
    CHECK_EQ(MaxPurchase(10), 5);
    CHECK_EQ(MaxPurchase(3), 3);
    CHECK_EQ(MaxPurchase(5), 5);
    // Unlimited stock (-1) is capped by the room.
    CHECK_EQ(MaxPurchase(-1), 5);
    CHECK(!CheckMaxUnits());
    FillShop(shop, 35);
    CHECK_EQ(MaxPurchase(10), 0);
}

/// <summary>
/// Port-only (MoreUnits): with MoreUnits a player outside a match may own 100 units, warned from 80: a purchase past
/// the original's 50 is capped only by the room under 100.
/// </summary>
TEST_CASE("logistics: with MoreUnits a player outside a match owns up to 100 units, warned from 80")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    GMoreUnits = 1;
    CHECK_EQ(MaxUnits(), 100);
    CHECK_EQ(UnitWarning(), 80);
    FillShop(shop, 30);
    CHECK_EQ(NumUnits(), 45);
    CHECK_EQ(MaxPurchase(10), 10);
    CHECK_EQ(MaxPurchase(-1), 55);
    FillShop(shop, 80);
    CHECK_EQ(NumUnits(), 95);
    CHECK_EQ(MaxPurchase(10), 5);
    FillShop(shop, 85);
    CHECK_EQ(MaxPurchase(10), 0);
    GMoreUnits = 0;
}

/// <summary>
/// Port-only (MoreUnits): a match keeps the original's limit, 50 warned from 40, whatever the player's MoreUnits says
/// (every player buys under the same rule).
/// </summary>
TEST_CASE("logistics: in a match the unit limit stays the original's, whatever MoreUnits says")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    GMoreUnits = 1;
    // A match (whatever its rules).
    shop.PlayMatch(MCMatchRule::MechBraking);
    CHECK_EQ(MaxUnits(), 50);
    CHECK_EQ(UnitWarning(), 40);
    FillShop(shop, 30);
    CHECK_EQ(MaxPurchase(10), 5);
    FillShop(shop, 35);
    CHECK_EQ(MaxPurchase(10), 0);
    GMoreUnits = 0;
}

TEST_CASE("game: the shop prices a mech by its profile and keeps its stock")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    MCPurMechList mechs;
    mechs.AddMech("PM100100", 2, "PM100100", 0, "PM100100", 1);
    REQUIRE_EQ(mechs.GetMechCount(), 1);
    std::shared_ptr<MCPurMech> mech;
    REQUIRE_EQ(mechs.GetMechInfo(0, mech), 0);
    CHECK_EQ(mech->Variants[0]->Cost, ProfilePrice("PM100100"));
    CHECK_EQ(mech->Variants[0]->Name, std::string("Commando A"));
    CHECK_EQ(mech->Variants[0]->NumAvailable, 2);
    CHECK_EQ(mech->Variants[1]->NumAvailable, 1);
    CHECK_EQ(mech->Variants[2]->NumAvailable, 0);
    CHECK_EQ(mech->CurVariant, 0);

    // Stock changes add up and stop at none.
    CHECK_EQ(mechs.ModMech("PM100100", -5, 3, 1), 0);
    CHECK_EQ(mech->Variants[0]->NumAvailable, 0);
    CHECK_EQ(mech->Variants[1]->NumAvailable, 2);
    CHECK_EQ(mech->Variants[2]->NumAvailable, 3);
    CHECK_EQ(mechs.ModMech("PM999999", 1, 1, 1), -1);

    // A mech out of its first variant shows the next one in stock; the newest mech comes first.
    mechs.AddMech("PM100100", 0, "PM100100", 0, "PM100100", 4);
    REQUIRE_EQ(mechs.GetMechInfo(0, mech), 0);
    CHECK_EQ(mech->CurVariant, 1);
    CHECK_EQ(mech->Variants[1]->NumAvailable, 4);
    mechs.Clear();
    CHECK_EQ(mechs.GetMechCount(), 0);
}

TEST_CASE("game: the shop lists vehicles by tonnage and keeps their stock")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    MCPurVehicleList vehicles;

    for (const std::string_view name : {"PV20500", "PV20000", "PV21000", "PV20100"})
    {
        vehicles.AddVehicle(name, 1);
    }

    REQUIRE_EQ(vehicles.GetVehicleCount(), 4);

    for (size_t i = 1; i < vehicles.Vehicles.size(); ++i)
    {
        MCTest::Scope scope(std::format("vehicle {}", i));
        CHECK(vehicles.Vehicles[i - 1]->Data->CurTonnage <= vehicles.Vehicles[i]->Data->CurTonnage);
    }

    // A vehicle's price is its own plus its components'.
    for (const auto& vehicle : vehicles.Vehicles)
    {
        MCTest::Scope scope(vehicle->Data->FileName);
        int32_t price = vehicle->Data->BaseCost;

        for (const std::shared_ptr<MCLogInventoryItem>& item : vehicle->Data->Inventory->Items)
        {
            price += MasterComponentList[item->MasterID].ResourcePoints * item->Count;
        }

        CHECK_EQ(vehicle->Data->Cost, price);
    }

    CHECK_EQ(vehicles.ModVehicle("PV20000", 3), 0);
    CHECK_EQ(vehicles.ModVehicle("PV20000", -10), 0);
    std::shared_ptr<MCPurVehicle> found;

    for (int32_t i = 0; i < vehicles.GetVehicleCount(); ++i)
    {
        vehicles.GetVehicleInfo(i, found);

        if (found->Data->FileName == "PV20000")
        {
            break;
        }
    }

    CHECK_EQ(found->Data->NumAvailable, 0);
    CHECK_EQ(vehicles.ModVehicle("PV99999", 1), -1);
}

TEST_CASE("game: a mech mounts a component its free tonnage, one of each equipment and its chassis allow")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    const int32_t weapon = FindComponent(MCComponentForm::WeaponEnergy);
    const int32_t ecm = FindComponent(MCComponentForm::Ecm);
    const int32_t sensor = FindComponent(MCComponentForm::Sensor);
    REQUIRE(weapon >= 0 && ecm >= 0 && sensor >= 0);

    MCLogMech mech;
    mech.Inventory = std::make_unique<MCInventoryList>();
    MCInventoryList& inventory = *mech.Inventory;
    mech.CurTonnage = 30.0f;
    mech.UsedTonnage = 25.0f;
    mech.NameIndex = 4;
    const auto id = [](int32_t value) { return static_cast<uint8_t>(value); };

    // Five tons free.
    CHECK(MCCompInventoryBlock::CanMount(id(weapon), 5.0f, mech));
    CHECK(!MCCompInventoryBlock::CanMount(id(weapon), 5.5f, mech));
    CHECK(!MCCompInventoryBlock::CanMount(id(weapon), 0.0f, std::nullopt));

    // One ECM: a second is refused, a sensor still fits.
    inventory.AddItem(id(ecm), inventory.CreateStat(0, 0, 0, 1, 0xff), false);
    CHECK(!MCCompInventoryBlock::CanMount(id(ecm), 0.0f, mech));
    CHECK(MCCompInventoryBlock::CanMount(id(sensor), 0.0f, mech));

    // The restricted components fit only the mechs of name index 5, 14 and 16.
    for (uint8_t restricted : RestrictedComps)
    {
        MCTest::Scope scope(std::format("component {}", restricted));

        if (IsEquipment(MasterComponentList[restricted].Form))
        {
            continue;
        }

        mech.NameIndex = 4;
        CHECK(!MCCompInventoryBlock::CanMount(restricted, 0.0f, mech));

        for (int32_t allowed : {5, 14, 16})
        {
            mech.NameIndex = allowed;
            CHECK(MCCompInventoryBlock::CanMount(restricted, 0.0f, mech));
        }
    }
}

/// <summary>
/// Port: MOST SKILLED PILOT takes the pilots for hire more skilled than it off the shop; those hired, and those of its
/// rank or under, stay as they were. Elite (the original's) takes none.
/// </summary>
TEST_CASE("logistics: the most skilled pilot for hire takes the more skilled off the shop")
{
    MCLogistics logistics;
    logistics.PurPilotList = std::make_unique<MCPurPilotList>();
    MCPurPilotList& pilots = *logistics.PurPilotList;

    for (int32_t rank = 0; rank < 4; rank++)
    {
        pilots.Insert(Pilot(rank, std::format("rank {}", rank), rank));
    }

    std::shared_ptr<MCPurPilotData> hired = Pilot(3, "hired ace", 9);
    hired->Status = MCPurPilotData::Hired;
    pilots.Insert(hired);
    logistics.HidePilotsAbove(MCPilotRank::Elite);
    CHECK_EQ(pilots.GetVisiblePilotCount(), 4);
    logistics.HidePilotsAbove(MCPilotRank::Regular);

    for (const std::shared_ptr<MCPurPilotData>& pilot : pilots.Pilots)
    {
        MCTest::Scope scope(pilot->Callsign);
        const int32_t expected = pilot == hired    ? MCPurPilotData::Hired
                                 : pilot->Rank > 1 ? MCPurPilotData::OffShop
                                                   : MCPurPilotData::ForHire;
        CHECK_EQ(pilot->Status, expected);
    }

    CHECK_EQ(pilots.GetVisiblePilotCount(), 2);
}

/// <summary>
/// Port: each of the game's weapons is of its family (MCWeaponFamilyOf), Inner Sphere's and Clan's, by the table's own
/// names; nothing else is; ammo is banned once every weapon firing it is, and nothing outside a match.
/// </summary>
TEST_CASE("game: each weapon is of its family, its ammo banned once every weapon firing it is")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    const std::map<uint32_t, std::set<std::string>> expected = {
        {MCMatchRule::NoLrms,
         {"LRM Rack", "LR Missile/15", "LR Missile/20", "C LR Missile Rack", "C LR Missile/15", "C LR Missile/20"}},
        {MCMatchRule::NoSrms,
         {"SR Missile Pack", "SR Missile/6", "ST SR Missile Pack", "C SR Missile Pack", "C SR Missile/6",
          "C ST-SR Missile Pack", "C ST-SR Missile/6"}},
        {MCMatchRule::NoThunderbolts, {"Heavy Thunderbolt"}},
        {MCMatchRule::NoPpcs, {"PPC", "ER PPC", "C ER PPC"}},
        {MCMatchRule::NoLasers,
         {"Large Laser", "Large ER Laser", "Laser", "C Large ER Laser", "C ER Laser", "Small Laser"}},
        {MCMatchRule::NoPulseLasers,
         {"Large X Pulse Laser", "Large PS Laser", "Pulse Laser", "C Large PS Laser", "C Pulse  Laser"}},
        {MCMatchRule::NoFlamers, {"Heavy Flamer", "C Heavy Flamer"}},
        {MCMatchRule::NoAutocannons,
         {"Light Autocannon", "Autocannon", "Hvy Autocannon", "Lt. Ultra Autocannon", "Light LBX Autocannon",
          "LBX Autocannon", "Heavy LBX Autocannon", "C Lt. Ultra Autocannon", "C  Ultra Autocannon",
          "C Hvy Ultra Autocannon", "C Light LBX", "C LBX Autocannon", "C Heavy LBX Autocannon"}},
        {MCMatchRule::NoGaussRifles, {"Rail Gun", "Light Gauss Rifle", "Gauss Rifle", "C Gauss Rifle"}},
        {MCMatchRule::NoMachineGuns, {"Machine Gun", "C Machine Gun"}},
        {MCMatchRule::NoLongToms, {"Long Tom", "Sniper Cannon"}},
    };

    std::map<uint32_t, std::set<std::string>> found;
    // Each ammo's weapons' families.
    std::map<int32_t, uint32_t> ammoFamilies;

    for (int32_t id = 0; id < NumMasterComponents(); id++)
    {
        const MCMasterComponent& component = MasterComponentList[static_cast<size_t>(id)];
        const uint32_t family = MCWeaponFamilyOf(component);
        const bool weapon = component.Form == MCComponentForm::WeaponEnergy ||
                            component.Form == MCComponentForm::WeaponBallistic ||
                            component.Form == MCComponentForm::WeaponMissile;
        MCTest::Scope scope(component.Name);
        CHECK_EQ(weapon, family != 0);

        if (component.MasterID < 0 || !weapon)
        {
            CHECK(component.Form == MCComponentForm::Ammo || !MCComponentBanned(id, MCMatchRule::WeaponBans));
            continue;
        }

        found[family].insert(component.Name);
        CHECK(MCComponentBanned(id, family));
        CHECK(!MCComponentBanned(id, MCMatchRule::WeaponBans & ~family));
        CHECK(!MCComponentBanned(id, 0));

        if (component.AmmoMasterId != 0)
        {
            ammoFamilies[component.AmmoMasterId] |= family;
        }
    }

    CHECK(found == expected);
    CHECK_EQ(WeaponFamilies.size(), expected.size());
    REQUIRE(!ammoFamilies.empty());

    for (const auto& [ammo, families] : ammoFamilies)
    {
        MCTest::Scope scope(MasterComponentList[static_cast<size_t>(ammo)].Name);
        CHECK(MasterComponentList[static_cast<size_t>(ammo)].Form == MCComponentForm::Ammo);
        CHECK(MCComponentBanned(ammo, families));
        CHECK(!MCComponentBanned(ammo, 0));

        // Any one of its families not banned keeps it.
        for (const MCWeaponFamily& family : WeaponFamilies)
        {
            if ((families & family.Rule) != 0)
            {
                CHECK(!MCComponentBanned(ammo, MCMatchRule::WeaponBans & ~family.Rule));
            }
        }
    }
}

/// <summary>
/// Port: in a match banning a weapon family, a mech comes without its weapons of it and their ammo: their tonnage free
/// for others, its value that much less; the shop sells none of them, its mechs for sale cost that much less, a vehicle
/// carrying one isn't sold, and none mounts on a mech.
/// </summary>
TEST_CASE("game: a match's weapon bans take a mech's banned weapons off and off the shop")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    constexpr std::string_view Profile = "PM100100";
    const std::shared_ptr<MCLogMech> stock = shop.Mechs.AddMech(Profile, false, false, false);

    // The Commando's SRMs (four SRM/2s and their ammo; a medium laser stays), which one of the vehicles carries too.
    constexpr uint32_t ban = MCMatchRule::NoSrms;
    uint8_t bannedWeapon = 0;
    bool bannedAmmo = false;

    for (const std::shared_ptr<MCLogInventoryItem>& item : stock->Inventory->Items)
    {
        const MCMasterComponent& master = MasterComponentList[item->MasterID];
        bannedWeapon = MCWeaponFamilyOf(master) == ban ? item->MasterID : bannedWeapon;
        bannedAmmo = bannedAmmo || (master.Form == MCComponentForm::Ammo && MCComponentBanned(item->MasterID, ban));
    }

    REQUIRE(bannedWeapon != 0);
    REQUIRE(bannedAmmo);
    int32_t removedPoints = 0;
    float removedTons = 0.0f;

    for (const std::shared_ptr<MCLogInventoryItem>& item : stock->Inventory->Items)
    {
        if (MCComponentBanned(item->MasterID, ban))
        {
            const MCMasterComponent& master = MasterComponentList[item->MasterID];
            removedPoints += master.ResourcePoints * static_cast<int32_t>(item->Stats.size());
            removedTons += master.Tonnage * static_cast<float>(item->Stats.size());
        }
    }

    REQUIRE(removedPoints > 0);
    shop.PlayMatch(ban);
    const std::shared_ptr<MCLogMech> stripped = shop.Mechs.AddMech(Profile, false, false, false);
    CHECK(std::ranges::none_of(stripped->Inventory->Items, [ban](const std::shared_ptr<MCLogInventoryItem>& item)
                               { return MCComponentBanned(item->MasterID, ban); }));
    CHECK_EQ(stripped->Inventory->NumItems() < stock->Inventory->NumItems(), true);
    CHECK(std::abs(stripped->UsedTonnage - (stock->UsedTonnage - removedTons)) < 0.001f);
    CHECK(std::abs(stripped->WeaponTonnage - (stock->WeaponTonnage - removedTons)) < 0.001f);
    CHECK_EQ(stripped->FreeTonnage, stock->FreeTonnage);
    CHECK_EQ(stripped->ResourcePoints, stock->ResourcePoints - removedPoints);
    CHECK(stripped->BattleRating < stock->BattleRating);

    // The banned weapon doesn't mount; one of another family does.
    uint8_t allowedWeapon = 0;

    for (int32_t id = 0; id < NumMasterComponents() && allowedWeapon == 0; id++)
    {
        const uint32_t family = MCWeaponFamilyOf(MasterComponentList[static_cast<size_t>(id)]);
        allowedWeapon = family != 0 && family != ban ? static_cast<uint8_t>(id) : 0;
    }

    REQUIRE(allowedWeapon != 0);
    CHECK(!MCCompInventoryBlock::CanMount(bannedWeapon, 0.0f, *stripped));
    CHECK(MCCompInventoryBlock::CanMount(allowedWeapon, 0.0f, *stripped));

    // The shop: its mech without them for that much less, the components banned not sold, the vehicles carrying one gone.
    MCLogistics& logistics = *GlobalLog();
    logistics.PurMechList = std::make_unique<MCPurMechList>();
    logistics.PurVehicleList = std::make_unique<MCPurVehicleList>();
    logistics.PurMechList->AddMech(Profile, 1, Profile, 1, Profile, 1);

    for (const std::string_view name : {"PV20500", "PV20000", "PV21000", "PV20100"})
    {
        logistics.PurVehicleList->AddVehicle(name, 1);
    }

    MCInventoryList& components = *logistics.PurchaseComponents;

    for (const uint8_t id : {bannedWeapon, allowedWeapon})
    {
        components.AddItem(id, components.CreateStat(0, 0, 0, 1, 0xff), false);
    }

    const auto carriesBanned = [ban](const MCInventoryList& inventory)
    {
        return std::ranges::any_of(inventory.Items, [ban](const std::shared_ptr<MCLogInventoryItem>& item)
                                   { return MCComponentBanned(item->MasterID, ban); });
    };

    const auto vehiclesCarrying =
        std::ranges::count_if(logistics.PurVehicleList->Vehicles, [&](const std::shared_ptr<MCPurVehicle>& vehicle)
                              { return carriesBanned(*vehicle->Data->Inventory); });
    const int32_t vehicles = logistics.PurVehicleList->GetVehicleCount();
    REQUIRE(vehiclesCarrying > 0);
    REQUIRE(vehiclesCarrying < vehicles);
    logistics.HideBannedWeapons(ban);

    for (const std::unique_ptr<MCPurMechData>& variant : logistics.PurMechList->Mechs.front()->Variants)
    {
        CHECK_EQ(variant->Cost, ProfilePrice(Profile) - removedPoints);
        CHECK(!carriesBanned(*variant->Inventory));
    }

    CHECK_EQ(components.NumItems(), 1);
    CHECK_EQ(components.GetMasterIDFromIndex(0), allowedWeapon);
    CHECK_EQ(logistics.PurVehicleList->GetVehicleCount(), vehicles - static_cast<int32_t>(vehiclesCarrying));
    CHECK(std::ranges::none_of(logistics.PurVehicleList->Vehicles, [&](const std::shared_ptr<MCPurVehicle>& vehicle)
                               { return carriesBanned(*vehicle->Data->Inventory); }));

    // Written as the drop writes a unit's profile and read back, a mech and a vehicle without them are what they were:
    // the vehicle's counts are its inventory's, also for one whose items aren't its stock ones (a deploy message's).
    const auto copies = [](const MCInventoryList& inventory)
    {
        std::multiset<uint8_t> ids;

        for (const std::shared_ptr<MCLogInventoryItem>& item : inventory.Items)
        {
            for (size_t copy = 0; copy < item->Stats.size(); copy++)
            {
                ids.insert(item->MasterID);
            }
        }

        return ids;
    };

    constexpr std::string_view SrmCarrier = "PV20500";

    // The temp folder the profiles go in, each of its folders made as the game makes it (in the tests' user folder).
    for (size_t end = SaveTempPath.find('\\'); end != std::string::npos; end = SaveTempPath.find('\\', end + 1))
    {
        MCFileSystem::MakeDirectory(std::string_view(SaveTempPath).substr(0, end));
    }

    const std::shared_ptr<MCLogVehicle> vehicle = shop.Vehicles.AddVehicle(SrmCarrier, false, false, false);
    CHECK(!carriesBanned(*vehicle->Inventory));
    REQUIRE_EQ(MCMissionLogisticsBridge::LogisticsVehicleProfileWriter("banvehicle", *vehicle, false), 0);
    MCFitIniFile vehicleFile;
    REQUIRE_EQ(vehicleFile.Open(GamePath(SaveTempPath, "banvehicle", ".fit")), 0);
    CHECK(copies(*shop.Vehicles.AddVehicle(vehicleFile, false, false, false)->Inventory) ==
          copies(*vehicle->Inventory));
    REQUIRE_EQ(MCMissionLogisticsBridge::LogisticsMechProfileWriter("banmech", *stripped, false), 0);
    MCFitIniFile mechFile;
    REQUIRE_EQ(mechFile.Open(GamePath(SaveTempPath, "banmech", ".fit")), 0);
    CHECK(copies(*shop.Mechs.AddMech(mechFile, false, false, false)->Inventory) == copies(*stripped->Inventory));

    shop.PlayMatch(0);
    const std::shared_ptr<MCLogVehicle> received = shop.Vehicles.AddVehicle(SrmCarrier, false, false, false);
    REQUIRE(carriesBanned(*received->Inventory));
    MCRemoveBanned(*received->Inventory, ban);
    REQUIRE_EQ(MCMissionLogisticsBridge::LogisticsVehicleProfileWriter("banreceived", *received, false), 0);
    MCFitIniFile receivedFile;
    REQUIRE_EQ(receivedFile.Open(GamePath(SaveTempPath, "banreceived", ".fit")), 0);
    CHECK(copies(*shop.Vehicles.AddVehicle(receivedFile, false, false, false)->Inventory) ==
          copies(*received->Inventory));
}

TEST_CASE("logistics: armor repair fills the head first, then the most damaged location")
{
    MCLogMech mech;
    FillMech(mech, 12);
    mech.Armor[0] = {9, 6};
    // The left arm at half, the center torso at three quarters (it counts a fifth more damaged: 60%).
    mech.Armor[4] = {12, 6};
    mech.Armor[1] = {16, 12};

    // Three points all go to the head.
    MCMechRepairBlock::RepairArmor(mech, 3);
    CHECK_EQ(mech.Armor[0].CurArmor, 9);
    CHECK_EQ(mech.Armor[4].CurArmor, 6);
    CHECK_EQ(mech.Armor[1].CurArmor, 12);

    // Then the left arm (50%, 58%), the center torso (60%), the center torso (65% < the arm's 67%), the left arm.
    MCMechRepairBlock::RepairArmor(mech, 2);
    CHECK_EQ(mech.Armor[4].CurArmor, 8);
    CHECK_EQ(mech.Armor[1].CurArmor, 12);
    MCMechRepairBlock::RepairArmor(mech, 3);
    CHECK_EQ(mech.Armor[1].CurArmor, 14);
    CHECK_EQ(mech.Armor[4].CurArmor, 9);

    // A head point short goes there before the rest.
    mech.Armor[0].CurArmor = 8;
    MCMechRepairBlock::RepairArmor(mech, 1);
    CHECK_EQ(mech.Armor[0].CurArmor, 9);
    CHECK_EQ(mech.Armor[4].CurArmor, 9);

    // A negative count repairs it all.
    MCMechRepairBlock::RepairArmor(mech, -1);

    for (const auto& location : mech.Armor)
    {
        CHECK_EQ(location.CurArmor, location.MaxArmor);
    }
}

TEST_CASE("logistics: internal structure repair goes to the most damaged location")
{
    MCLogMech mech;
    FillMech(mech, 6);
    // The head at a third counts 30% more damaged (23%); the left arm at a third counts as it is.
    mech.Internals[0] = {3, 1};
    mech.Internals[4] = {6, 2};
    MCMechRepairBlock::RepairInternal(mech, 1);
    CHECK_EQ(mech.Internals[0].CurArmor, 2);
    CHECK_EQ(mech.Internals[4].CurArmor, 2);
    // The head at two thirds counts 47%; the arm's 33% is lower.
    MCMechRepairBlock::RepairInternal(mech, 1);
    CHECK_EQ(mech.Internals[0].CurArmor, 2);
    CHECK_EQ(mech.Internals[4].CurArmor, 3);
    MCMechRepairBlock::RepairInternal(mech, -1);

    for (const auto& location : mech.Internals)
    {
        CHECK_EQ(location.CurArmor, location.MaxArmor);
    }
}

TEST_CASE("game: the repair bay lists weapons by damage, lowest first")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    ShopFixture shop;
    // Three weapons with different damage, in the inventory in id order.
    std::vector<int32_t> ids;

    for (int32_t id = 0; id < NumMasterComponents() && ids.size() < 3; ++id)
    {
        const MCComponentForm form = MasterComponentList[id].Form;
        const bool newDamage = std::ranges::none_of(
            ids, [id](int32_t other) { return MasterComponentList[other].Damage == MasterComponentList[id].Damage; });

        if (IsWeapon(form) && newDamage)
        {
            ids.push_back(id);
        }
    }

    REQUIRE_EQ(ids.size(), size_t{3});
    MCInventoryList inventory;

    for (int32_t id : ids)
    {
        inventory.AddItem(static_cast<uint8_t>(id), inventory.CreateStat(0, 0, 0, 1, 0xff), false);
    }

    std::vector<int32_t> entries = {0, 1, 2};
    std::vector<int32_t> hits = {10, 20, 30};
    MCMechRepairBlock::SortByDamage(entries, hits, inventory);
    auto damage = [&inventory](int32_t entry)
    { return MasterComponentList[inventory.GetMasterIDFromIndex(entry)].Damage; };
    CHECK(damage(entries[0]) < damage(entries[1]));
    CHECK(damage(entries[1]) < damage(entries[2]));

    // Each damage figure stays with its entry.
    for (size_t i = 0; i < entries.size(); ++i)
    {
        CHECK_EQ(hits[i], 10 * (entries[i] + 1));
    }
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

    /// <summary>The purchase dialog's accept button.</summary>
    void Accept()
    {
        Click(316, 299);
        Settle();
    }

    /// <summary>The store's first row dragged onto the inventory.</summary>
    void BuyFirst()
    {
        Drag(300, 80, 100, 200);
        Settle();
    }

    /// <summary>The inventory's first row dragged onto the store.</summary>
    void SellFirst()
    {
        Drag(100, 120, 300, 200);
        Settle();
    }

    /// <summary><paramref name="pilot"/>'s row in the shown screen's inventory list, or null.</summary>
    std::shared_ptr<MCPilotInventoryBlock> PilotRow(const MCLogWarrior& pilot)
    {
        const std::shared_ptr<MCLogInvScreen> screen = MCLogInvScreen::Of(*GlobalLog()->CurrentScreen());
        return screen != nullptr ? screen->InventoryPane->RowOf<MCPilotInventoryBlock>(pilot) : nullptr;
    }

    /// <summary><paramref name="pilot"/>'s inventory row dragged onto the unit pane's first row.</summary>
    void DragPilot(MCLogWarrior& pilot)
    {
        const std::shared_ptr<MCPilotInventoryBlock> shown = PilotRow(pilot);
        REQUIRE(shown != nullptr);
        MCPilotInventoryBlock& row = *shown;
        Drag(row.GlobalX() + 40, row.GlobalY() + row.Height() / 2, 300, 60);
    }

    /// <summary>The unit in inventory row 0 of <paramref name="list"/>, if any.</summary>
    std::shared_ptr<MCLogMech> FirstInventoryMech(MCLogMechList& list)
    {
        for (const std::shared_ptr<MCLogMech>& mech : list.Mechs)
        {
            if (mech->ReadWithWidgets && mech->ListIndex == 0)
            {
                return mech;
            }
        }

        return nullptr;
    }
}

TEST_CASE_ISOLATED("game: the purchase screen buys and sells mechs, vehicles, components and pilots by dragging")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    GlobalLog()->SetUpPurchaseScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen);
    ResourcePoints = 100000;

    // A mech: the shown variant's price, one more in the inventory, one less in stock.
    {
        MCTest::Scope scope("mech");
        MCPurMech& purMech = *GlobalLog()->PurMechList->Mechs.front();

        for (const auto& variant : purMech.Variants)
        {
            variant->NumAvailable = 2;
        }

        const MCPurMechData& shown = *purMech.Variants[static_cast<size_t>(purMech.CurVariant)];
        const int32_t mechs = GlobalLog()->MechList->GetMechCount();
        BuyFirst();
        Accept();
        CHECK_EQ(GlobalLog()->MechList->GetMechCount(), mechs + 1);
        CHECK_EQ(ResourcePoints, 100000 - shown.Cost);
        CHECK_EQ(shown.NumAvailable, 1);
    }

    // Selling the inventory's first mech back returns half its value.
    {
        MCTest::Scope scope("mech sale");
        // The value as the sale works it out: the mech's price as it is now. (The pin ends before the sale: kept, it
        // would keep the sold mech alive, and the inventory information would still show it.)
        const int32_t value = []
        {
            const std::shared_ptr<MCLogMech> sold = FirstInventoryMech(*GlobalLog()->MechList);
            REQUIRE(sold != nullptr);
            REQUIRE(!sold->Required);
            sold->CalcMechCost(false);
            return sold->ResourcePoints;
        }();
        const int32_t points = ResourcePoints;
        const int32_t mechs = GlobalLog()->MechList->GetMechCount();
        SellFirst();
        Accept();
        CHECK_EQ(GlobalLog()->MechList->GetMechCount(), mechs - 1);
        CHECK_EQ(ResourcePoints, points + value / 2);
    }

    // A vehicle.
    {
        MCTest::Scope scope("vehicle");
        Click(200, 350);
        Settle();
        MCPurVehicleData& data = *GlobalLog()->PurVehicleList->Vehicles.front()->Data;
        data.NumAvailable = 2;
        const int32_t vehicles = GlobalLog()->VehicleList->GetVehicleCount();
        const int32_t points = ResourcePoints;
        BuyFirst();
        Accept();
        CHECK_EQ(GlobalLog()->VehicleList->GetVehicleCount(), vehicles + 1);
        CHECK_EQ(ResourcePoints, points - data.Cost);
        CHECK_EQ(data.NumAvailable, 1);
    }

    // A component: the store row drawn first (row 0).
    {
        MCTest::Scope scope("component");
        Click(200, 280);
        Settle();
        REQUIRE(!MCLogInvScreen::StoreList()->Rows().empty());
        const std::shared_ptr<MCCompPurchaseBlock> row =
            std::dynamic_pointer_cast<MCCompPurchaseBlock>(MCLogInvScreen::StoreList()->Rows().front().Lock());
        REQUIRE(row != nullptr);
        std::shared_ptr<MCLogInventoryItem> stock = row->Item();
        REQUIRE(stock != nullptr);
        stock->Count = 3;
        const uint8_t masterID = stock->MasterID;
        MCInventoryList& spares = *GlobalLog()->ComponentInventory;
        const int32_t index = spares.GetIndexFromMasterID(masterID);
        const int32_t before = index < 0 ? 0 : spares.GetItemInfo(index)->Count;
        const int32_t points = ResourcePoints;
        BuyFirst();
        Accept();
        std::shared_ptr<MCLogInventoryItem> bought = spares.GetItemInfo(spares.GetIndexFromMasterID(masterID));
        REQUIRE(bought != nullptr);
        CHECK_EQ(bought->Count, before + 1);
        CHECK_EQ(stock->Count, 2);
        CHECK_EQ(ResourcePoints, points - MasterComponentList[masterID].ResourcePoints);
    }

    // A pilot: hired, off the store, paid for.
    {
        MCTest::Scope scope("pilot");
        // The campaign starts with no pilot for hire: one goes up for hire, as a later mission's shop puts them.
        REQUIRE(GlobalLog()->PurPilotList->GetPilotCount() > 0);
        GlobalLog()->PurPilotList->Pilots.back()->Status = MCPurPilotData::ForHire;
        GlobalLog()->PurchaseScreen->CreatePurVehiclePane(true);
        Click(200, 180);
        Settle();
        REQUIRE(!MCLogInvScreen::StoreList()->Rows().empty());
        const std::shared_ptr<MCPilotPurchaseBlock> row =
            std::dynamic_pointer_cast<MCPilotPurchaseBlock>(MCLogInvScreen::StoreList()->Rows().front().Lock());
        REQUIRE(row != nullptr);
        std::shared_ptr<MCPurPilotData> hired = row->Pilot();
        REQUIRE(hired != nullptr);
        const int32_t pilots = GlobalLog()->WarriorList->GetWarriorCount();
        const int32_t visible = GlobalLog()->PurPilotList->GetVisiblePilotCount();
        const int32_t points = ResourcePoints;
        const int32_t cost = hired->Cost;
        BuyFirst();
        Accept();
        CHECK_EQ(GlobalLog()->WarriorList->GetWarriorCount(), pilots + 1);
        CHECK_EQ(hired->Status, MCPurPilotData::Hired);
        CHECK_EQ(GlobalLog()->PurPilotList->GetVisiblePilotCount(), visible - 1);
        CHECK_EQ(ResourcePoints, points - cost);
    }

    // Not enough points: nothing is bought.
    {
        MCTest::Scope scope("too dear");
        Click(200, 120);
        Settle();
        ResourcePoints = 10;
        const int32_t mechs = GlobalLog()->MechList->GetMechCount();
        BuyFirst();
        CHECK_EQ(GlobalLog()->MechList->GetMechCount(), mechs);
        CHECK_EQ(ResourcePoints, 10);
    }
}

namespace
{
    /// <summary>The menus' message dialog's OK, clicked.</summary>
    void CloseMessage()
    {
        MCLogDialogButton& ok = *GlobalLog()->MessageDialog->OkButton;
        Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
        Settle();
    }
}

/// <summary>
/// Port-only (MoreUnits): with MoreUnits a campaign's shop sells past the original's 50 units, a mech at a time dragged
/// from the store onto the inventory: from 50 a purchase goes through with no word; at 80 it warns how many the player
/// owns of the 100; at 100 it says the inventory is at its maximum, and there it sells no more.
/// </summary>
TEST_CASE_ISOLATED("game: with MoreUnits the shop sells past 50 units up to 100, warning from 80")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t Points = 10'000'000;
    constexpr int32_t Stock = 10;
    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    GlobalLog()->SetUpPurchaseScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen);
    GMoreUnits = 1;
    ResourcePoints = Points;
    MCTestGame::StockStore(Stock);
    const MCReusableDialog& message = *GlobalLog()->MessageDialog;

    // From 50: bought, no word.
    MCTestGame::OwnUnits(50);
    BuyFirst();
    Accept();
    CHECK_EQ(NumUnits(), 51);
    CHECK(!message.ShowWindow);

    // From 80: bought, with the warning of the 100.
    MCTestGame::OwnUnits(79);
    BuyFirst();
    Accept();
    CHECK_EQ(NumUnits(), 80);
    REQUIRE(message.ShowWindow);
    CHECK_EQ(message.Text, MCFormatPrintf(LoadGameString(0x372, 0xfe), 80, 100));
    CloseMessage();

    // The 100th: bought, the inventory at its maximum.
    MCTestGame::OwnUnits(99);
    BuyFirst();
    Accept();
    CHECK_EQ(NumUnits(), 100);
    REQUIRE(message.ShowWindow);
    CHECK_EQ(message.Text, LoadGameString(0x374, 0xfe));
    CloseMessage();

    // At 100: none sold, the same word.
    BuyFirst();
    CHECK_EQ(NumUnits(), 100);
    REQUIRE(message.ShowWindow);
    CHECK_EQ(message.Text, LoadGameString(0x374, 0xfe));
    CloseMessage();
}

TEST_CASE_ISOLATED("game: a pilot dragged onto a force mech on the repair screen becomes its pilot")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();
    GlobalLog()->SetUpRepairScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->RepairScreen);

    // The pilots tab: the inventory's first pilot, and the force's first mech.
    Click(202, 180);
    Settle();
    std::shared_ptr<MCLogWarrior> pilot;

    for (const std::shared_ptr<MCLogWarrior>& warrior : GlobalLog()->WarriorList->Warriors)
    {
        if (warrior->ListIndex == 0)
        {
            pilot = warrior;
        }
    }

    std::shared_ptr<MCLogMech> mech;

    for (const std::shared_ptr<MCLogMech>& forceMech : GlobalLog()->ForceMechList->Mechs)
    {
        if (forceMech->ReadWithWidgets && forceMech->SlotIndex == 0)
        {
            mech = forceMech;
        }
    }

    REQUIRE(pilot != nullptr);
    REQUIRE(mech != nullptr);
    const int32_t id = pilot->Id;
    std::shared_ptr<MCLogWarrior> previous;
    REQUIRE_EQ(GlobalLog()->AssignedWarriorList->GetWarriorInfo(mech->PilotIndex, previous), 0);
    REQUIRE(previous != nullptr && previous->Id != id);
    const int32_t previousId = previous->Id;

    // A mech with a pilot takes no other: the inventory's pilots are greyed and can't be picked up.
    Click(300, 60);
    Settle();
    REQUIRE(GlobalLog()->RepairScreen->SelectedMech() == mech);
    Click(202, 180);
    Settle();
    CHECK(PilotRow(*pilot)->GreyedOut);
    DragPilot(*pilot);
    Settle();
    std::shared_ptr<MCLogWarrior> flying;
    REQUIRE_EQ(GlobalLog()->AssignedWarriorList->GetWarriorInfo(mech->PilotIndex, flying), 0);
    CHECK_EQ(flying->Id, previousId);

    // The mech's pilot dragged off its portrait into the inventory leaves the seat empty.
    const std::shared_ptr<MCMechRepairBlock> shown = GlobalLog()->RepairScreen->RowOf(*mech);
    REQUIRE(shown != nullptr);
    Drag(shown->GlobalX() + 30, shown->GlobalY() + 0x40, 100, 200);
    Settle();
    CHECK_EQ(mech->PilotIndex, -1);
    bool unseated = false;

    for (const std::shared_ptr<MCLogWarrior>& warrior : GlobalLog()->WarriorList->Warriors)
    {
        unseated = unseated || warrior->Id == previousId;
    }

    CHECK(unseated);

    // Now the pilot dropped on the selected mech takes the seat.
    Click(202, 180);
    Settle();
    CHECK(!PilotRow(*pilot)->GreyedOut);
    DragPilot(*pilot);
    Settle();
    REQUIRE_EQ(GlobalLog()->AssignedWarriorList->GetWarriorInfo(mech->PilotIndex, flying), 0);
    REQUIRE(flying != nullptr);
    CHECK_EQ(flying->Id, id);
}

namespace
{
    /// <summary>FNV-1a of the repair screen's unit pane as it draws its content.</summary>
    uint32_t UnitPaneHash()
    {
        MCScrollPane& pane = *GlobalLog()->RepairScreen->UnitPane;
        MCLogPort picture;
        picture.Init(pane.Width(), pane.Height());
        pane.DrawContentTo(picture.Frame(), 0, 0);
        uint32_t hash = 0x811c9dc5;

        for (const uint8_t pixel : picture.Pixels())
        {
            hash = (hash ^ pixel) * 0x01000193;
        }

        return hash;
    }

    /// <summary>Whether every force unit's <c>SlotIndex</c> is its place in the force (mechs, then vehicles).</summary>
    bool ForceSlotsInOrder()
    {
        int32_t slot = 0;

        for (const std::shared_ptr<MCLogMech>& mech : GlobalLog()->ForceMechList->Mechs)
        {
            if (mech->SlotIndex != slot++)
            {
                return false;
            }
        }

        for (const std::shared_ptr<MCLogVehicle>& vehicle : GlobalLog()->ForceVehicleList->Vehicles)
        {
            if (vehicle->SlotIndex != slot++)
            {
                return false;
            }
        }

        return true;
    }
}

TEST_CASE_ISOLATED("game: a mech picked up from the inventory shows in the repair screen's force once dropped there")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();

    // The campaign starts with every mech in the force: one bought for the inventory.
    GlobalLog()->SetUpPurchaseScreen(true);
    Settle();
    ResourcePoints = 100000;

    for (const auto& variant : GlobalLog()->PurMechList->Mechs.front()->Variants)
    {
        variant->NumAvailable = 2;
    }

    BuyFirst();
    Accept();
    GlobalLog()->SetUpRepairScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->RepairScreen);

    // The mechs tab and its first mech.
    Click(200, 120);
    Settle();
    const std::shared_ptr<MCLogMech> mech = FirstInventoryMech(*GlobalLog()->MechList);
    REQUIRE(mech != nullptr);
    const int32_t forceMechs = GlobalLog()->ForceMechList->GetMechCount();
    CHECK(ForceSlotsInOrder());
    const uint32_t before = UnitPaneHash();

    // Picked up and moved over the inventory, it joins the force list, but the force's rows don't change until the
    // drop (the original painted the rows' picture only then).
    SendMouse(7, 100, 120, false);
    SendMouse(1, 100, 120, true);
    MCTestGame::RunFrame(1.0f / 15.0f);

    for (int32_t step = 1; step <= 4; step++)
    {
        SendMouse(7, 100 + step * 5, 120 + step * 5, true);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    CHECK(mech->Assigned);
    CHECK_EQ(GlobalLog()->ForceMechList->GetMechCount(), forceMechs + 1);
    CHECK_EQ(UnitPaneHash(), before);

    // Dropped on the force: listed there, every unit at its row.
    for (int32_t step = 1; step <= 4; step++)
    {
        SendMouse(7, 120 + step * 45, 140 + step * 15, true);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    SendMouse(4, 300, 200, false);
    Settle();
    CHECK(mech->Assigned);
    CHECK_EQ(GlobalLog()->ForceMechList->GetMechCount(), forceMechs + 1);
    CHECK(ForceSlotsInOrder());
    CHECK(GlobalLog()->RepairScreen->SelectedMech() == mech);
    CHECK(UnitPaneHash() != before);
}

namespace
{
    /// <summary>
    /// A right-click on <paramref name="item"/>'s row in the shown screen's inventory list: pressed on the row, a frame,
    /// let go there, as the game's input code sends them, and the screen settled.
    /// </summary>
    template <typename Row, typename Item> void RightClickRow(const Item& item)
    {
        // The row's place only: the row isn't kept through the click, as nothing but the list keeps it in play.
        const auto [x, y] = [&]
        {
            const std::shared_ptr<MCLogInvScreen> screen = MCLogInvScreen::Of(*GlobalLog()->CurrentScreen());
            REQUIRE(screen != nullptr);
            const std::shared_ptr<Row> row = screen->InventoryPane->RowOf<Row>(item);
            REQUIRE(row != nullptr);
            return std::pair{row->GlobalX() + 40, row->GlobalY() + row->Height() / 2};
        }();
        SendMouse(7, x, y, false);
        SendMouse(MCGuiEventType::RightButtonDown, x, y, false);
        MCTestGame::RunFrame(1.0f / 15.0f);
        SendMouse(MCGuiEventType::RightButtonUp, x, y, false);
        Settle();
    }
}

TEST_CASE_ISOLATED("game: a right-click on an inventory mech or vehicle offers it for sale in the store and puts it in "
                   "the force on the repair screen")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Settle();
    NewCampaign();
    Settle();

    // The campaign starts with every unit in the force: two mechs and two vehicles bought for the inventory.
    GlobalLog()->SetUpPurchaseScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen);
    ResourcePoints = 1000000;

    for (const auto& variant : GlobalLog()->PurMechList->Mechs.front()->Variants)
    {
        variant->NumAvailable = 3;
    }

    BuyFirst();
    Accept();
    BuyFirst();
    Accept();
    Click(200, 350);
    Settle();
    GlobalLog()->PurVehicleList->Vehicles.front()->Data->NumAvailable = 3;
    BuyFirst();
    Accept();
    BuyFirst();
    Accept();

    // The row is picked up and put down: the unit leaves the inventory for the force, its row goes with it, and the
    // sale is offered; accepted, the unit is gone. (Release 2 crashed here: the sale read the unit from the row the
    // list had just dropped.)
    {
        MCTest::Scope scope("vehicle sale");
        const int32_t vehicles = GlobalLog()->VehicleList->GetVehicleCount();
        const int32_t forceVehicles = GlobalLog()->ForceVehicleList->GetVehicleCount();
        REQUIRE(vehicles >= 2);
        RightClickRow<MCVehicleInventoryBlock>(*GlobalLog()->VehicleList->Vehicles.front());
        Accept();
        CHECK_EQ(GlobalLog()->VehicleList->GetVehicleCount(), vehicles - 1);
        CHECK_EQ(GlobalLog()->ForceVehicleList->GetVehicleCount(), forceVehicles);
    }

    {
        MCTest::Scope scope("mech sale");
        Click(200, 120);
        Settle();
        const int32_t mechs = GlobalLog()->MechList->GetMechCount();
        const int32_t forceMechs = GlobalLog()->ForceMechList->GetMechCount();
        const int32_t points = ResourcePoints;
        std::shared_ptr<MCLogMech> sold = FirstInventoryMech(*GlobalLog()->MechList);
        REQUIRE(sold != nullptr);
        sold->CalcMechCost(false);
        const int32_t value = sold->ResourcePoints;
        RightClickRow<MCMechInventoryBlock>(*sold);
        // Let go before the sale, as the player holds nothing of it.
        sold.reset();
        Accept();
        CHECK_EQ(GlobalLog()->MechList->GetMechCount(), mechs - 1);
        CHECK_EQ(GlobalLog()->ForceMechList->GetMechCount(), forceMechs);
        CHECK_EQ(ResourcePoints, points + value / 2);
    }

    // On the repair screen the unit put down joins the force.
    GlobalLog()->SetUpRepairScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->RepairScreen);

    {
        MCTest::Scope scope("mech into the force");
        Click(200, 120);
        Settle();
        const std::shared_ptr<MCLogMech> mech = FirstInventoryMech(*GlobalLog()->MechList);
        REQUIRE(mech != nullptr);
        const int32_t forceMechs = GlobalLog()->ForceMechList->GetMechCount();
        RightClickRow<MCMechInventoryBlock>(*mech);
        CHECK(mech->Assigned);
        CHECK_EQ(GlobalLog()->ForceMechList->GetMechCount(), forceMechs + 1);
        CHECK(GlobalLog()->RepairScreen->SelectedMech() == mech);
    }

    {
        MCTest::Scope scope("vehicle into the force");
        Click(200, 350);
        Settle();
        REQUIRE(GlobalLog()->VehicleList->GetVehicleCount() > 0);
        const std::shared_ptr<MCLogVehicle> vehicle = GlobalLog()->VehicleList->Vehicles.front();
        const int32_t forceVehicles = GlobalLog()->ForceVehicleList->GetVehicleCount();
        RightClickRow<MCVehicleInventoryBlock>(*vehicle);
        CHECK(vehicle->Assigned);
        CHECK_EQ(GlobalLog()->ForceVehicleList->GetVehicleCount(), forceVehicles + 1);
    }
}
