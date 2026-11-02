#include "stdafx.h"
#include "MCTest.h"
#include "main/MCGameContext.h"
#include "object/MCBigGameObject.h"
#include "object/MCCollisionGrid.h"
#include "object/MCContactSystem.h"
#include "object/MCForces.h"
#include "object/MCObjectType.h"
#include "object/MCSensorSystem.h"
#include "object/MCSortList.h"
#include "object/MCTeam.h"

namespace
{
    /// <summary>A test context with the forces (no allied team) and a contact system of its own.</summary>
    class ContactWorld
    {
    public:
        ContactWorld()
        {
            _Scope.Context().SetForces(std::make_unique<MCForces>(false));
            _Scope.Context().SetContactSystem(std::make_unique<MCContactSystem>(8));
        }

        /// <summary>A big object (part <paramref name="partId"/>) at x = <paramref name="x"/>, with a potential
        /// contact of the manager's.</summary>
        std::shared_ptr<MCPotentialContact> AddObject(int32_t partId, float x, int32_t alignment)
        {
            auto object = std::make_shared<MCBigGameObject>();
            object->PartId = partId;
            object->Position = MCVector3D(x, 0.0f, 0.0f);
            object->SetAlignment(alignment);
            std::shared_ptr<MCPotentialContact> contact =
                PotentialContactManager().Add(MCPotentialContactType::InnerSphere, *object, 0);
            Objects.push_back(std::move(object));
            return contact;
        }

        std::vector<std::shared_ptr<MCBigGameObject>> Objects;

    private:
        MCTestContextScope _Scope;
    };

    /// <summary>A game object of class <paramref name="objectClass"/> at (x, y) that takes part in collisions.</summary>
    std::shared_ptr<MCGameObject> CollidingObject(std::shared_ptr<MCObjectType> type, MCObjectClass objectClass,
                                                  float x, float y)
    {
        auto object = std::make_shared<MCGameObject>();
        object->ObjectClass = objectClass;
        object->ObjType = std::move(type);
        object->CollisionsOn = 1;
        object->Position = MCVector3D(x, y, 0.0f);
        return object;
    }

    /// <summary>The pairs a grid checks, each as (lower index, higher index) into <paramref name="objects"/>.</summary>
    std::set<std::pair<size_t, size_t>> CheckedPairs(const MCCollisionGrid& grid,
                                                     const std::vector<std::shared_ptr<MCGameObject>>& objects)
    {
        std::set<std::pair<size_t, size_t>> pairs;
        grid.CheckPairs(
            [&](MCGameObject& first, MCGameObject& second)
            {
                const auto index = [&](MCGameObject& object)
                {
                    return static_cast<size_t>(std::ranges::find_if(objects,
                                                                    [&object](const std::shared_ptr<MCGameObject>& held)
                                                                    { return held.get() == &object; }) -
                                               objects.begin());
                };

                pairs.insert(std::minmax(index(first), index(second)));
            });
        return pairs;
    }
}

TEST_CASE("forces: the clan fights alone, the Inner Sphere and its allies share a side")
{
    MCTestContextScope scope;
    scope.Context().SetForces(std::make_unique<MCForces>(true));
    REQUIRE(ClanTeam() != nullptr);
    REQUIRE(InnerSphereTeam() != nullptr);
    REQUIRE(AlliedTeam() != nullptr);
    CHECK_EQ(ClanTeam()->Alignment, -1);
    CHECK_EQ(InnerSphereTeam()->Alignment, 1);
    CHECK_EQ(AlliedTeam()->Alignment, 1);
    CHECK(TeamById(MCForces::ClanId) == ClanTeam());
    CHECK(HomeTeam() == nullptr);

    // Commanders exist as the scenario makes them; past them there are none.
    Forces().MakeCommanders(3);
    CHECK_EQ(NumCommanders(), 3);
    CHECK(CommanderById(2) != nullptr);
    CHECK(CommanderById(3) == nullptr);
    CHECK(CommanderById(-1) == nullptr);

    scope.Context().SetForces(std::make_unique<MCForces>(false));
    CHECK(AlliedTeam() == nullptr);
}

/// <summary>
/// A team's line-of-sight and sensor contacts are lists of contact ids, each contact knowing its slot. Taking one out
/// moves the last into its slot. The original's lists held 500, and its line-of-sight list checked the sensor list's
/// count (OB-146); the port's grow.
/// </summary>
TEST_CASE("team contacts: contacts come and go by slot, past the original's 500")
{
    ContactWorld world;
    std::shared_ptr<MCTeam> team = InnerSphereTeam();
    std::vector<std::shared_ptr<MCPotentialContact>> contacts;

    for (int32_t i = 0; i < 600; i++)
    {
        contacts.push_back(world.AddObject(0x200 + i, static_cast<float>(i), -1));
        team->AddLosContact(*contacts.back());
    }

    REQUIRE_EQ(team->NumLosContacts(), 600);
    CHECK_EQ(team->NumSensorContacts(), 0);
    CHECK_EQ(contacts[599]->TeamSlot[team->Id], 599);

    // The last fills the gap.
    team->RemoveLosContact(*contacts[10]);
    CHECK_EQ(team->NumLosContacts(), 599);
    CHECK_EQ(contacts[10]->TeamSlot[team->Id], -1);
    CHECK_EQ(contacts[599]->TeamSlot[team->Id], 10);
    CHECK_EQ(team->LosContacts()[10], contacts[599]->Id);

    // A sensor contact goes in the other list, its slot its own.
    team->AddSensorContact(*contacts[10]);
    CHECK_EQ(team->NumSensorContacts(), 1);
    CHECK_EQ(contacts[10]->TeamSlot[team->Id], 0);

    // Every slot still names its contact.
    for (int32_t slot = 0; slot < team->NumLosContacts(); slot++)
    {
        MCTest::Scope at("slot " + std::to_string(slot));
        CHECK_EQ(PotentialContactManager().Contact(team->LosContacts()[slot]).TeamSlot[team->Id], slot);
    }
}

/// <summary>
/// getContacts lists a team's contacts, enemies only when asked, sorted by distance (nearest first). The original
/// sorted them in a 200-entry list, so past 200 contacts the rest stayed unsorted (OB-143); the port sorts them all.
/// </summary>
TEST_CASE("team contacts: getContacts keeps the enemies and sorts every contact by distance")
{
    ContactWorld world;
    std::shared_ptr<MCTeam> team = InnerSphereTeam();
    constexpr int32_t count = 250;

    // Found farthest first; every fifth is a friend.
    for (int32_t i = 0; i < count; i++)
    {
        team->AddLosContact(*world.AddObject(0x200 + i, static_cast<float>(count - i) * 10.0f, i % 5 == 0 ? 1 : -1));
    }

    const auto looker = std::make_shared<MCGameObject>();
    std::vector<int32_t> found(count);
    REQUIRE_EQ(team->GetContacts(*looker, found, 0, 2), count);

    // Nearest first: the last found comes first.
    for (int32_t i = 0; i < count; i++)
    {
        MCTest::Scope at("place " + std::to_string(i));
        CHECK_EQ(found[i], 0x200 + count - 1 - i);
    }

    // Enemies only (criteria bit 1): the friends are left out, the order unsorted with sort type 0.
    const int32_t enemies = team->GetContacts(*looker, found, 1, 0);
    CHECK_EQ(enemies, count - count / 5);
    CHECK_EQ(found[0], 0x201);
    CHECK_EQ(found[3], 0x204);
    CHECK_EQ(found[4], 0x206);
}

/// <summary>A sensor's contacts are ids with slots too; the original held 200 and missed the rest (OB-145).</summary>
TEST_CASE("sensors: a sensor holds every contact in range, past the original's 200")
{
    ContactWorld world;
    std::shared_ptr<MCSensorSystem> sensor = SensorSystemManager().NewSensor();
    REQUIRE(sensor != nullptr);
    sensor->TeamIndex = MCForces::InnerSphereId;
    std::vector<std::shared_ptr<MCPotentialContact>> contacts;

    for (int32_t i = 0; i < 250; i++)
    {
        contacts.push_back(world.AddObject(0x200 + i, static_cast<float>(i), -1));
        sensor->AddSensorContact(*contacts.back());
    }

    REQUIRE_EQ(sensor->NumContacts(), 250);
    CHECK_EQ(contacts[249]->SensorSlot(sensor->Id), 249);
    CHECK_EQ(contacts[249]->NumSensors[MCForces::InnerSphereId], 1);

    // Added twice, counted once.
    sensor->AddSensorContact(*contacts[0]);
    CHECK_EQ(sensor->NumContacts(), 250);

    sensor->RemoveSensorContact(*contacts[0]);
    CHECK_EQ(sensor->NumContacts(), 249);
    CHECK_EQ(contacts[0]->SensorSlot(sensor->Id), -1);
    CHECK_EQ(contacts[0]->NumSensors[MCForces::InnerSphereId], 0);
    CHECK_EQ(contacts[249]->SensorSlot(sensor->Id), 0);
}

/// <summary>
/// Each mission's sensors stagger their first scans by the sensors alive before them, so a mission played again
/// scans as soon as the first time. (The port once counted every sensor ever made, so each restart put off the
/// radar 6.5 s more.)
/// </summary>
TEST_CASE("sensors: a mission played again scans as soon as the first time")
{
    constexpr size_t missions = 3;
    std::array<float, missions> firstScan{};
    std::array<float, missions> lastScan{};

    for (size_t mission = 0; mission < missions; mission++)
    {
        ContactWorld world;
        firstScan[mission] = SensorSystemManager().Sensor(0).NextScanTime;
        lastScan[mission] = SensorSystemManager().Sensor(MCSensorSystemManager::InitialSensors - 1).NextScanTime;
    }

    for (size_t mission = 1; mission < missions; mission++)
    {
        CHECK_EQ(firstScan[mission], firstScan[0]);
        CHECK_EQ(lastScan[mission], lastScan[0]);
    }
}

TEST_CASE("contacts: the manager hands out contacts lowest id first, reuses freed ones and grows")
{
    ContactWorld world;
    std::shared_ptr<MCPotentialContact> first = world.AddObject(0x200, 0.0f, -1);
    std::shared_ptr<MCPotentialContact> second = world.AddObject(0x201, 0.0f, -1);
    CHECK_EQ(first->Id, 0);
    CHECK_EQ(second->Id, 1);
    CHECK_EQ(PotentialContactManager().List(MCPotentialContactType::InnerSphere).size(), 2u);

    PotentialContactManager().Remove(*first);
    CHECK_EQ(PotentialContactManager().List(MCPotentialContactType::InnerSphere).size(), 1u);
    CHECK_EQ(world.AddObject(0x202, 0.0f, -1)->Id, 0);

    // Past the scenario's MaxPotentialContacts (8 here) the pool grows.
    for (int32_t i = 0; i < 10; i++)
    {
        world.AddObject(0x203 + i, 0.0f, -1);
    }

    CHECK_EQ(PotentialContactManager().List(MCPotentialContactType::InnerSphere).size(), 12u);
}

/// <summary>
/// The collision grid files each object in its cell (giants, wider than a cell, apart) and checks every pair in a
/// cell, with the cells to the right, below and below-right, and every object with the giants. The cell below-left is
/// never checked (OB-147). Turrets and gates never collide among themselves.
/// </summary>
TEST_CASE("collision grid: neighbouring cells and giants are checked, the cell below-left is not")
{
    // A 4 x 4 grid of 100-unit cells around the origin: cell (row, col) spans x from col * 100 - 250.
    MCCollisionGrid grid(4, 100);
    const auto small = std::make_shared<MCObjectType>();
    small->ExtentRadius = 10.0f;
    const auto giant = std::make_shared<MCObjectType>();
    giant->ExtentRadius = 150.0f;
    const auto cellCentre = [](int32_t index) { return static_cast<float>(index * 100 - 200); };
    std::vector<std::shared_ptr<MCGameObject>> objects;
    objects.push_back(CollidingObject(small, MCObjectClass::BattleMech, cellCentre(1), cellCentre(1))); // 0: (1, 1)
    objects.push_back(CollidingObject(small, MCObjectClass::BattleMech, cellCentre(2), cellCentre(1))); // 1: (1, 2)
    objects.push_back(CollidingObject(small, MCObjectClass::BattleMech, cellCentre(0), cellCentre(2))); // 2: (2, 0)
    objects.push_back(CollidingObject(small, MCObjectClass::BattleMech, cellCentre(3), cellCentre(3))); // 3: (3, 3)
    objects.push_back(CollidingObject(small, MCObjectClass::Turret, cellCentre(3), cellCentre(3)));     // 4: (3, 3)
    objects.push_back(CollidingObject(small, MCObjectClass::Turret, cellCentre(3), cellCentre(3)));     // 5: (3, 3)
    objects.push_back(CollidingObject(giant, MCObjectClass::BattleMech, 0.0f, 0.0f));                   // 6: giant

    for (const std::shared_ptr<MCGameObject>& object : objects)
    {
        grid.Add(*object);
    }

    REQUIRE_EQ(grid.Giants().size(), 1u);
    CHECK_EQ(grid.Cell(1, 1).size(), 1u);
    CHECK_EQ(grid.Cell(3, 3).size(), 3u);

    const std::set<std::pair<size_t, size_t>> pairs = CheckedPairs(grid, objects);
    // Side by side.
    CHECK(pairs.contains({0, 1}));
    // (1, 1) and (2, 0) touch at (1, 1)'s lower-left corner: never checked (OB-147).
    CHECK(!pairs.contains({0, 2}));
    // Far apart.
    CHECK(!pairs.contains({0, 3}));
    // In one cell, but two turrets never collide.
    CHECK(pairs.contains({3, 4}));
    CHECK(pairs.contains({3, 5}));
    CHECK(!pairs.contains({4, 5}));

    // The giant meets everyone.
    for (size_t i = 0; i < 6; i++)
    {
        MCTest::Scope at("object " + std::to_string(i));
        CHECK(pairs.contains({i, 6}));
    }

    // An object out of collisions isn't filed; clearing empties the grid.
    objects[0]->CollisionsOn = 0;
    grid.Clear();
    grid.Add(*objects[0]);
    CHECK_EQ(grid.Cell(1, 1).size(), 0u);
    CHECK(grid.Giants().empty());
}

TEST_CASE("sort list: values sort highest or lowest first, the cleared filler last")
{
    MCSortList list(6);
    list.Clear(true);
    CHECK_EQ(list.List[5].Id, 5);
    CHECK(list.List[5].Value < -3.0e38f);

    const std::array<float, 4> values = {3.0f, 7.0f, -1.0f, 5.0f};

    for (size_t i = 0; i < values.size(); i++)
    {
        list.List[i].Id = static_cast<int32_t>(10 + i);
        list.List[i].Value = values[i];
    }

    list.Sort(true);
    const std::array<int32_t, 4> highestFirst = {11, 13, 10, 12};

    for (size_t i = 0; i < highestFirst.size(); i++)
    {
        CHECK_EQ(list.List[i].Id, highestFirst[i]);
    }

    CHECK(list.List[4].Id == 4 || list.List[4].Id == 5);

    // Lowest first, as by distance: the filler is the largest float.
    list.Clear(false);

    for (size_t i = 0; i < values.size(); i++)
    {
        list.List[i].Id = static_cast<int32_t>(10 + i);
        list.List[i].Value = values[i];
    }

    list.Sort(false);
    const std::array<int32_t, 4> lowestFirst = {12, 10, 13, 11};

    for (size_t i = 0; i < lowestFirst.size(); i++)
    {
        CHECK_EQ(list.List[i].Id, lowestFirst[i]);
    }

    CHECK(list.List[5].Value > 3.0e38f);
}
