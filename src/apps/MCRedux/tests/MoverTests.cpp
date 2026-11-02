#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "fakes/MCScriptedRandom.h"
#include "fixtures/MCRetailData.h"
#include "fixtures/MCTinyMap.h"
#include "ai/MCRefit.h"
#include "ai/MCTacticalOrder.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "object/MCElementalDynamics.h"
#include "object/MCGroundVehicleDynamics.h"
#include "object/MCMechDynamics.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCStatusChunk.h"
#include "object/MCTacOrderQueue.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCDebris.h"
#include "object/MCMechGameSystem.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The raw roll (as <c>rand</c> gives it) that makes RandomNumber(<paramref name="range"/>) return
    /// <paramref name="value"/>.</summary>
    int32_t RawRoll(int32_t value, int32_t range)
    {
        return (value * 0x8000 + range - 1) / range;
    }

    /// <summary>A move order to the centre of map cell (row, col), with the id <paramref name="id"/>, packed.</summary>
    MCTacticalOrder MoveOrder(const MCTinyMap& map, int32_t id, int32_t row, int32_t col)
    {
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 0);
        order.SetWayPoint(0, map.CellCentre(row, col));
        order.Id = id;
        order.Pack();
        return order;
    }

    /// <summary>The map cell (row, column) of a world position.</summary>
    std::pair<int32_t, int32_t> CellOf(MCVector3D position)
    {
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        GameMap().WorldToMapPos(position, tileR, tileC, cellR, cellC);
        return {tileR * MapCellDim + cellR, tileC * MapCellDim + cellC};
    }

    /// <summary>Saves a global table's values and puts them back when the test ends.</summary>
    template <typename T, size_t N> class Restore
    {
    public:
        explicit Restore(T (&table)[N]) : _Table(table) { std::ranges::copy(table, _Saved.begin()); }
        ~Restore() { std::ranges::copy(_Saved, std::begin(_Table)); }
        Restore(const Restore&) = delete;
        Restore& operator=(const Restore&) = delete;

    private:
        T (&_Table)[N];
        std::array<T, N> _Saved{};
    };
}

TEST_CASE("tac order queue: sixteen orders queue first in first out, and the seventeenth is refused")
{
    MCTacOrderQueue queue;
    CHECK(queue.Empty());

    for (int32_t i = 0; i < MCTacOrderQueue::MaxOrders; i++)
    {
        MCQueuedTacOrder order;
        order.Id = i + 1;
        CHECK(queue.Push(order));
    }

    MCQueuedTacOrder extra;
    extra.Id = 99;
    CHECK(!queue.Push(extra));
    CHECK_EQ(queue.Size(), MCTacOrderQueue::MaxOrders);
    CHECK_EQ(queue.Front().Id, 1);
    queue.PopFront();
    CHECK_EQ(queue.Front().Id, 2);
    CHECK_EQ(queue.Orders().back().Id, MCTacOrderQueue::MaxOrders);
    CHECK(queue.Push(extra));
    CHECK_EQ(queue.Orders().back().Id, 99);
    queue.Clear();
    CHECK(queue.Empty());
}

TEST_CASE("tac order ids: ids wrap at 255, so 241-255 come before 1-15")
{
    CHECK(CompareTacOrderId(3, 7) < 0);
    CHECK(CompareTacOrderId(7, 3) > 0);
    CHECK_EQ(CompareTacOrderId(5, 5), 0);
    // An id just past the wrap is newer than one just before it, and the other way round.
    CHECK(CompareTacOrderId(250, 2) < 0);
    CHECK(CompareTacOrderId(2, 250) > 0);
    CHECK_EQ(CompareTacOrderId(255, 1), -1);
    CHECK_EQ(CompareTacOrderId(1, 255), 1);
}

TEST_CASE("pilot: the first queued player order starts at once, the rest wait their turn")
{
    MCTinyMap map(8);
    const auto pilot = std::make_shared<MCMechWarrior>();

    REQUIRE_EQ(pilot->AddQueuedTacOrder(MoveOrder(map, 1, 3, 4)), 0);
    // Nothing else ran, so the first order became the player order at once.
    CHECK_EQ(pilot->NewTacOrderReceivedOf(MCOrderState::Player), 1);
    CHECK_EQ(pilot->TacOrderOf(MCOrderState::Player).Id, 1);
    CHECK_EQ(pilot->PlayerOrderFromQueue, 1);
    CHECK(pilot->TacOrderQueueExecuting);
    CHECK(pilot->QueuedOrders.Empty());

    // The player order is waiting: the next ones queue.
    REQUIRE_EQ(pilot->AddQueuedTacOrder(MoveOrder(map, 2, 5, 6)), 0);
    REQUIRE_EQ(pilot->AddQueuedTacOrder(MoveOrder(map, 3, 7, 8)), 0);
    CHECK_EQ(pilot->QueuedOrders.Size(), 2);

    // The queue lists the running order first.
    const std::vector<MCQueuedTacOrder> listed = pilot->GetTacOrderQueue();
    REQUIRE_EQ(std::ssize(listed), 3);
    CHECK_EQ(pilot->GetTacOrderQueueSize(), 3);

    for (int32_t i = 0; i < 3; i++)
    {
        MCTest::Scope scope(std::format("order {}", i));
        CHECK_EQ(listed[static_cast<size_t>(i)].Id, i + 1);
    }

    // Taking an order gives it back whole: its code and its way point.
    MCTacticalOrder taken;
    taken.Reset();
    REQUIRE_EQ(pilot->PeekQueuedTacOrder(taken), 0);
    CHECK_EQ(taken.Id, 2);
    REQUIRE_EQ(pilot->RemoveQueuedTacOrder(taken), 0);
    CHECK(taken.Code == MCTacticalOrderCode::MoveToPoint);
    CHECK_EQ(taken.Id, 2);
    CHECK(CellOf(taken.GetWayPoint(0)) == std::pair(5, 6));
    CHECK_EQ(pilot->QueuedOrders.Size(), 1);

    // The queue holds sixteen.
    for (int32_t id = 4; id < 4 + MCTacOrderQueue::MaxOrders - 1; id++)
    {
        REQUIRE_EQ(pilot->AddQueuedTacOrder(MoveOrder(map, id, 2, 2)), 0);
    }

    CHECK_EQ(pilot->QueuedOrders.Size(), MCTacOrderQueue::MaxOrders);
    CHECK_EQ(pilot->AddQueuedTacOrder(MoveOrder(map, 40, 2, 2)), 2);

    // A client drops the orders the server says it has run (those before the id it names).
    pilot->UpdateClientOrderQueue(6);
    REQUIRE(!pilot->QueuedOrders.Empty());
    CHECK_EQ(pilot->QueuedOrders.Front().Id, 6);

    pilot->ClearTacOrderQueue();
    CHECK(pilot->QueuedOrders.Empty());
    CHECK(!pilot->TacOrderQueueExecuting);
    CHECK_EQ(pilot->RemoveQueuedTacOrder(taken), 2);
}

TEST_CASE("pilot: a skill check succeeds on a roll under the skill, and scores its skill points")
{
    Restore triesSaved(SkillTry);
    Restore successesSaved(SkillSuccess);
    std::ranges::fill(SkillTry, 1.0f);
    std::ranges::fill(SkillSuccess, 10.0f);
    MCTestContextScope scope;
    MCScriptedRandom& dice = scope.Context().SetRandom(std::make_unique<MCScriptedRandom>());
    const auto pilot = std::make_shared<MCMechWarrior>();
    pilot->Skills[SkillGunnery] = 50;
    pilot->Skills[SkillSensors] = 50;

    // A roll of 30 against a skill of 50: made by 19 (the roll counts from 0, so 49 is the last success).
    dice.Returns({RawRoll(30, 100)});
    CHECK_EQ(pilot->CheckSkill(SkillGunnery, 1.0f), 19);
    CHECK_EQ(pilot->NumSkillUses[SkillGunnery][1], 1);
    CHECK_EQ(pilot->NumSkillSuccesses[SkillGunnery][1], 1);
    CHECK_EQ(pilot->SkillPoints[SkillGunnery], 11.0f);

    dice.Returns({RawRoll(49, 100)});
    CHECK_EQ(pilot->CheckSkill(SkillGunnery, 1.0f), 0);
    dice.Returns({RawRoll(50, 100)});
    CHECK_EQ(pilot->CheckSkill(SkillGunnery, 1.0f), -1);
    CHECK_EQ(pilot->NumSkillUses[SkillGunnery][1], 3);
    CHECK_EQ(pilot->NumSkillSuccesses[SkillGunnery][1], 2);
    CHECK_EQ(pilot->SkillPoints[SkillGunnery], 11.0f + 11.0f + 1.0f);

    // The factor scales the skill.
    dice.Returns({RawRoll(30, 100)});
    CHECK_EQ(pilot->CheckSkill(SkillGunnery, 0.5f), -6);

    // A sensor check never counts as a success.
    dice.Returns({RawRoll(10, 100)});
    CHECK_EQ(pilot->CheckSkill(SkillSensors, 1.0f), 39);
    CHECK_EQ(pilot->NumSkillSuccesses[SkillSensors][1], 0);
    CHECK_EQ(pilot->SkillPoints[SkillSensors], 1.0f);
    CHECK_EQ(dice.Remaining(), static_cast<size_t>(0));
}

TEST_CASE("pilot: an alarm keeps its first ten triggers until it is cleared")
{
    const auto pilot = std::make_shared<MCMechWarrior>();
    CHECK_EQ(pilot->AlarmOf(MCPilotAlarmType::HitByWeaponFire).NumTriggers, 0);

    for (uint32_t trigger = 1; trigger <= 10; trigger++)
    {
        CHECK_EQ(pilot->TriggerAlarm(MCPilotAlarmType::HitByWeaponFire, trigger * 100), 0);
    }

    // The eleventh is dropped.
    CHECK_EQ(pilot->TriggerAlarm(MCPilotAlarmType::HitByWeaponFire, 9999), -1);
    std::array<uint32_t, MCPilotAlarm::MaxTriggers> triggers{};
    REQUIRE_EQ(pilot->GetAlarmTriggers(MCPilotAlarmType::HitByWeaponFire, triggers), 10);

    for (uint32_t i = 0; i < 10; i++)
    {
        CHECK_EQ(triggers[i], (i + 1) * 100);
    }

    // Other alarms are untouched; clearing one empties it.
    CHECK_EQ(pilot->AlarmOf(MCPilotAlarmType::Collision).NumTriggers, 0);
    pilot->ClearAlarm(MCPilotAlarmType::HitByWeaponFire);
    CHECK_EQ(pilot->GetAlarmTriggers(MCPilotAlarmType::HitByWeaponFire, triggers), 0);
    CHECK_EQ(pilot->TriggerAlarm(MCPilotAlarmType::HitByWeaponFire, 5), 0);
    CHECK_EQ(pilot->AlarmOf(MCPilotAlarmType::HitByWeaponFire).NumTriggers, 1);

    // The brain's handlers go by the same numbers.
    CHECK_EQ(PilotAlarmFunctionName[std::to_underlying(MCPilotAlarmType::HitByWeaponFire)],
             std::string_view("handlehitbyweaponfire"));
    CHECK_EQ(PilotAlarmFunctionName[std::to_underlying(MCPilotAlarmType::GateClosing)],
             std::string_view("handlegateclosing"));
}

TEST_CASE("pilot: attackers are remembered by id, up to fifty, with the time of their last attack")
{
    const float savedTime = ScenarioTime;
    const auto pilot = std::make_shared<MCMechWarrior>();
    pilot->UpdateAttackerStatus(0x300, 10.0f);
    pilot->UpdateAttackerStatus(0x301, 12.0f);
    pilot->UpdateAttackerStatus(0x300, 20.0f);
    REQUIRE(pilot->GetAttackerInfo(0x300).has_value());
    CHECK_EQ(pilot->GetAttackerInfo(0x300)->LastTime, 20.0f);
    CHECK(!pilot->GetAttackerInfo(0x999).has_value());

    // The attackers of the last 5 seconds at 22: only the one that attacked at 20.
    ScenarioTime = 22.0f;
    const std::vector<uint32_t> list = pilot->GetAttackers(5.0f);
    REQUIRE_EQ(list.size(), 1u);
    CHECK_EQ(list[0], 0x300u);
    CHECK_EQ(pilot->GetAttackers(15.0f).size(), 2u);

    for (uint32_t id = 0x400; id < 0x400 + MCMechWarrior::MaxAttackers; id++)
    {
        pilot->UpdateAttackerStatus(id, 21.0f);
    }

    // With the two from before, the last two new ones don't fit.
    CHECK_EQ(std::ssize(pilot->Attackers), MCMechWarrior::MaxAttackers);
    CHECK(pilot->GetAttackerInfo(0x400 + MCMechWarrior::MaxAttackers - 3).has_value());
    CHECK(!pilot->GetAttackerInfo(0x400 + MCMechWarrior::MaxAttackers - 2).has_value());
    ScenarioTime = savedTime;
}

TEST_CASE("mover: an offset move goal steps off a blocked cell toward the target, onto the first open cell")
{
    MCTinyMap map(8);

    // A block three cells wide across row 12.
    for (int32_t col = 10; col <= 12; col++)
    {
        map.Block(12, col);
    }

    const auto mover = std::make_shared<MCMover>();
    MCVector3D goal;

    // From the middle of the block toward the east.
    REQUIRE_EQ(mover->CalcOffsetMoveGoal(map.CellCentre(12, 20), map.CellCentre(12, 11), goal), 0);
    const auto [row, col] = CellOf(goal);
    CHECK_EQ(row, 12);
    CHECK((col == 13 || col == 14));
    CHECK(map.Passable(row, col));

    // From an open cell, the goal is that cell's point.
    REQUIRE_EQ(mover->CalcOffsetMoveGoal(map.CellCentre(12, 20), map.CellCentre(5, 5), goal), 0);
    CHECK(CellOf(goal) == std::pair(5, 5));
    CHECK(std::abs(goal.X - map.CellCentre(5, 5).X) < 0.01f);
    CHECK(std::abs(goal.Y - map.CellCentre(5, 5).Y) < 0.01f);
}

TEST_CASE("mover: a range lock holds the path's next cells, as many as asked, and stops at a cell held already")
{
    MCTinyMap map(8);
    const auto pilot = std::make_shared<MCMechWarrior>();
    const auto mover = std::make_shared<MCMover>();
    mover->Pilot = pilot;

    // A straight path east along row 4, fifteen steps (more than the original's ten locks).
    MCMovePath& path = *pilot->MoveOrders.Path[0];
    path.SetNumSteps(15);

    for (int32_t i = 0; i < 15; i++)
    {
        MCPathStep& step = path.StepList[static_cast<size_t>(i)];
        const int32_t col = 2 + i;
        step.TileR = static_cast<int16_t>(4 / MapCellDim);
        step.CellR = static_cast<int16_t>(4 % MapCellDim);
        step.TileC = static_cast<int16_t>(col / MapCellDim);
        step.CellC = static_cast<int16_t>(col % MapCellDim);
    }

    auto locked = [](int32_t col)
    { return GameMap().GetCellPathLocked(4 / MapCellDim, col / MapCellDim, 4 % MapCellDim, col % MapCellDim); };

    REQUIRE_EQ(mover->SetPathRangeLock(1, 12), 0);
    CHECK_EQ(std::ssize(mover->PathRangeLocks), 12);

    for (int32_t i = 0; i < 15; i++)
    {
        MCTest::Scope scope(std::format("step {}", i));
        CHECK_EQ(locked(2 + i), i < 12);
    }

    // Locking again first lets go of the old locks; unlocking frees them all.
    REQUIRE_EQ(mover->SetPathRangeLock(1, 3), 0);
    CHECK(locked(4));
    CHECK(!locked(5));
    REQUIRE_EQ(mover->SetPathRangeLock(0, 0), 0);
    CHECK(mover->PathRangeLocks.empty());
    CHECK(!locked(2));

    // A cell another mover holds stops the locking there, keeping the cells before it.
    GameMap().SetCellPathLocked(4 / MapCellDim, 6 / MapCellDim, 4 % MapCellDim, 6 % MapCellDim, 1);
    CHECK_EQ(mover->SetPathRangeLock(1, 10), -1);
    CHECK_EQ(std::ssize(mover->PathRangeLocks), 4);
    CHECK(locked(5));
}

TEST_CASE("status chunk: the body state, a jump to a cell and an eject order pack into one word and unpack the same")
{
    MCStatusChunk chunk;
    chunk.BodyState = 3;
    chunk.JumpOrder = 1;
    chunk.EjectOrderGiven = 1;
    chunk.TargetType = 4;
    chunk.TargetCellRC = {123, 456};
    chunk.Pack();
    CHECK_EQ(StatusChunkUnpackErr, 0);

    MCStatusChunk received;
    received.Data = chunk.Data;
    received.Unpack();
    CHECK_EQ(StatusChunkUnpackErr, 0);
    CHECK_EQ(received.BodyState, 3u);
    CHECK_EQ(received.JumpOrder, 1);
    CHECK_EQ(received.EjectOrderGiven, 1);
    CHECK_EQ(static_cast<int32_t>(received.TargetType), 4);
    CHECK(received.TargetCellRC == (std::array<int16_t, 2>{123, 456}));
    CHECK(received.EqualTo(chunk));

    // A bad target type is reported.
    chunk.Reset();
    chunk.TargetType = 6;
    chunk.Pack();
    CHECK_EQ(StatusChunkUnpackErr, 5);
    chunk.Reset();
    CHECK_EQ(chunk.TargetCellRC[0], -1);
}

namespace
{
    /// <summary>A FIT file held in a test's memory source, opened.</summary>
    struct MemoryFit
    {
        explicit MemoryFit(std::string_view text)
        {
            Scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>()).AddFile("data\\test.fit", text);
            Result = File.Open("data\\test.fit");
        }

        MCTestContextScope Scope;
        MCFitIniFile File;
        int32_t Result = 0;
    };
}

TEST_CASE("dynamics types: the turn rates are at least 720, a missing pivot rate is a quarter of the turn rate")
{
    {
        MemoryFit fit("FITini\n[MechDynamics]\nl maxTorsoYawRate = 90\nl maxTorsoYaw = 60\nl maxArmYaw = 30\n"
                      "l maxMechYawRate = 400\nl maxLeftArmYawRate = 45\nl maxRightArmYawRate = 46\n"
                      "f maxAccel = 2.5\nf maxVelocity = 8.0\nFITend\n");
        REQUIRE_EQ(fit.Result, 0);
        auto mech = MCMechDynamicsType::Create(fit.File);
        REQUIRE(mech.has_value());
        CHECK_EQ((*mech)->MaxMechYawRate, 720);
        CHECK_EQ((*mech)->MaxMechPivotRate, 180);
        CHECK_EQ((*mech)->MaxTorsoYawRate, 90);
        CHECK_EQ((*mech)->MaxRightArmYawRate, 46);
        CHECK_EQ((*mech)->MaxVelocity, 8.0f);
        CHECK_EQ((*mech)->GetDynamicsTypeClass(), 1u);
    }

    {
        // The vehicle's acceleration is five times its top speed, whatever the FIT says; a pivot rate is kept.
        MemoryFit fit("FITini\n[VehicleDynamics]\nl maxTurretYawRate = 50\nl maxTurretYaw = 180\n"
                      "l maxVehicleYawRate = 900\nl maxVehiclePivotRate = 100\nf maxAccel = 1.0\n"
                      "f maxVelocity = 12.0\nFITend\n");
        REQUIRE_EQ(fit.Result, 0);
        auto vehicle = MCGroundVehicleDynamicsType::Create(fit.File);
        REQUIRE(vehicle.has_value());
        CHECK_EQ((*vehicle)->MaxVehicleYawRate, 900);
        CHECK_EQ((*vehicle)->MaxVehiclePivotRate, 100);
        CHECK_EQ((*vehicle)->MaxAccel, 60.0f);

        // A vehicle's dynamics starts with the type's acceleration.
        const auto mover = std::make_shared<MCMover>();
        std::unique_ptr<MCDynamics> dynamics = (*vehicle)->CreateInstance(*mover);
        CHECK_EQ(dynamics->GetDynamicsClass(), 2u);
        CHECK_EQ(static_cast<MCGroundVehicleDynamics&>(*dynamics).Accel, 60.0f);
        CHECK(LockObject(dynamics->Me) == mover);
    }

    {
        // A missing entry is the error the load stops with.
        MemoryFit fit("FITini\n[ElementalDynamics]\nl maxElementalYawRate = 300\nFITend\n");
        REQUIRE_EQ(fit.Result, 0);
        auto elemental = MCElementalDynamicsType::Create(fit.File);
        REQUIRE(!elemental.has_value());
        CHECK(elemental.error() == MCFitError::VariableNotFound);
        CHECK(!MCMechDynamicsType::Create(fit.File).has_value());
    }
}

TEST_CASE_ISOLATED("game: the mover settings are gamesys.fit's, the fire arcs halved and the cluster sizes fixed")
{
    std::unique_ptr<MCMemoryFileSource> data = MCRetailData::Load({"data\\missions\\gamesys.fit"});

    if (data == nullptr)
    {
        return;
    }

    MCTestContextScope scope;
    scope.Context().SetFiles(std::move(data));
    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("data\\missions\\gamesys.fit"), 0);
    REQUIRE_EQ(LoadMoverGameSystem(fit), 0);

    // What the file says, read again.
    REQUIRE_EQ(fit.SeekBlock("Mover:FireWeapon"), 0);
    const MCFitResult<std::vector<float>> arcs = fit.ReadArray<float>("FireArc");
    REQUIRE(arcs.has_value());
    REQUIRE_EQ(std::ssize(*arcs), 3);

    for (size_t i = 0; i < 3; i++)
    {
        MCTest::Scope scope(std::format("arc {}", i));
        CHECK_EQ(FireArc[i], static_cast<float>((*arcs)[i] * 0.5));
    }

    CHECK_EQ(ClusterSizeSrm, 2);
    CHECK_EQ(ClusterSizeLrm, 5);
    REQUIRE_EQ(fit.SeekBlock("Pathfinding"), 0);
    const MCFitResult<std::vector<int32_t>> longRange = fit.ReadArray<int32_t>("LongRangeMovementEnabled");
    REQUIRE(longRange.has_value());

    for (size_t i = 0; i < 3; i++)
    {
        CHECK_EQ(LongRangeMovementEnabled[i], (*longRange)[i] == 1 ? 1 : 0);
    }

    REQUIRE_EQ(fit.SeekBlock("Warrior"), 0);
    const MCFitResult<std::vector<char>> professionalism = fit.ReadArray<char>("ProfessionalismTable");
    REQUIRE(professionalism.has_value());
    REQUIRE_EQ(std::ssize(*professionalism), 10);
    CHECK_EQ(ProfessionalismOffsetTable[2][1], static_cast<int8_t>((*professionalism)[5]));
    REQUIRE_EQ(fit.SeekBlock("Skills"), 0);
    CHECK_EQ(SensorSkill, fit.Read<float>("Sensor Contact Skill").value_or(-1.0f));
}

namespace
{
    /// <summary>The first mech of mission 1 that isn't disabled, or null.</summary>
    std::shared_ptr<MCBattleMech> FirstMech()
    {
        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
        {
            std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover != nullptr && mover->ObjectClass == MCObjectClass::BattleMech && mover->IsDisabled() == 0)
            {
                return std::static_pointer_cast<MCBattleMech>(mover);
            }
        }

        return nullptr;
    }

    /// <summary>A shot of <paramref name="damage"/> at armor location <paramref name="location"/>, from no one.</summary>
    void Hit(MCBattleMech& mech, int32_t location, float damage)
    {
        MCWeaponShotInfo shot;
        shot.Init(nullptr, 0, damage, location, 0.0f);
        mech.HandleWeaponHit(shot, 0);
    }

    /// <summary>Every location's armor and internal structure, armor first.</summary>
    std::vector<float> Damage(const MCBattleMech& mech)
    {
        std::vector<float> values;

        for (const MCArmorLocation& armor : mech.Armor)
        {
            values.push_back(armor.CurArmor);
        }

        for (const MCBodyLocation& body : mech.Body)
        {
            values.push_back(body.CurInternalStructure);
        }

        return values;
    }
}

/// <summary>
/// A mech's damage, by the BattleTech rules the game follows: a hit takes the armor of its armor location (a rear
/// torso its own), then the internal structure of the body location behind it (a rear torso's is the front torso's),
/// and what a destroyed location doesn't take passes on inward (an arm to its side torso, a leg too); a destroyed
/// centre torso kills the mech.
/// </summary>
TEST_CASE_ISOLATED("game: a mech's hits take armor, then internal structure, then pass inward")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCBattleMech> mech = FirstMech();
    REQUIRE(mech != nullptr);
    REQUIRE_EQ(mech->NumArmorLocations(), NumMechArmorLocations);
    REQUIRE_EQ(mech->NumBodyLocations(), static_cast<int32_t>(NumMechBodyLocations));
    // gamesys.fit's transfers: an arm to its side torso, a leg too.
    REQUIRE_EQ(static_cast<int32_t>(MechTransferHitTable[MechLeftArm]), MechLeftTorso);
    REQUIRE_EQ(static_cast<int32_t>(MechTransferHitTable[MechRightLeg]), MechRightTorso);
    // No critical hits: the dice would choose what else breaks (an ammo bin going up hits more locations).
    CriticalHitTable[0] = 100;

    // A point at each armor location takes a point of that location's armor, and nothing else.
    for (int32_t location = 0; location < NumMechArmorLocations; location++)
    {
        MCTest::Scope scope(std::format("armor location {}", location));
        REQUIRE(mech->Armor[static_cast<size_t>(location)].CurArmor >= 1.0f);
        std::vector<float> expected = Damage(*mech);
        expected[static_cast<size_t>(location)] -= 1.0f;
        Hit(*mech, location, 1.0f);
        CHECK(Damage(*mech) == expected);
    }

    // Through the left arm's armor: the rest takes its internal structure.
    MCArmorLocation& leftArmArmor = mech->Armor[MechLeftArm];
    MCBodyLocation& leftArm = mech->BodyAt(MechLeftArm);
    REQUIRE(leftArm.CurInternalStructure > 3.0f);
    const float armStructure = leftArm.CurInternalStructure;
    Hit(*mech, MechLeftArm, leftArmArmor.CurArmor + 2.0f);
    CHECK_EQ(leftArmArmor.CurArmor, 0.0f);
    CHECK_EQ(leftArm.CurInternalStructure, armStructure - 2.0f);

    // A rear torso without armor: the hit takes the front torso's internal structure, not its armor.
    for (const auto [rear, front] : {std::pair(8, 1), std::pair(9, 2), std::pair(10, 3)})
    {
        MCTest::Scope scope(std::format("rear {}", rear));
        mech->Armor[static_cast<size_t>(rear)].CurArmor = 0.0f;
        const float frontArmor = mech->Armor[static_cast<size_t>(front)].CurArmor;
        const float structure = mech->BodyAt(front).CurInternalStructure;
        Hit(*mech, rear, 1.0f);
        CHECK_EQ(mech->Armor[static_cast<size_t>(front)].CurArmor, frontArmor);
        CHECK_EQ(mech->BodyAt(front).CurInternalStructure, structure - 1.0f);
    }

    // The arm destroyed: what it couldn't take goes to the left torso (its armor first).
    const float torsoArmor = mech->Armor[MechLeftTorso].CurArmor;
    REQUIRE(torsoArmor > 5.0f);
    Hit(*mech, MechLeftArm, leftArm.CurInternalStructure + 3.0f);
    CHECK_EQ(leftArm.CurInternalStructure, 0.0f);
    CHECK_EQ(static_cast<int32_t>(leftArm.DamageState), 2);
    CHECK_EQ(mech->Armor[MechLeftTorso].CurArmor, torsoArmor - 3.0f);
    // Later hits at the arm go straight to the torso.
    Hit(*mech, MechLeftArm, 1.0f);
    CHECK_EQ(mech->Armor[MechLeftTorso].CurArmor, torsoArmor - 4.0f);

    // A leg gone: the mech is down a leg.
    CHECK_EQ(static_cast<int32_t>(mech->LegStatus), 0);
    MCBodyLocation& rightLeg = mech->BodyAt(MechRightLeg);
    mech->Armor[MechRightLeg].CurArmor = 0.0f;
    Hit(*mech, MechRightLeg, rightLeg.CurInternalStructure);
    CHECK_EQ(static_cast<int32_t>(rightLeg.DamageState), 2);
    CHECK_EQ(static_cast<int32_t>(mech->LegStatus), 2);
    CHECK_EQ(mech->IsDisabled(), 0);

    // The centre torso destroyed: the mech is out of the fight.
    mech->Armor[MechCenterTorso].CurArmor = 0.0f;
    Hit(*mech, MechCenterTorso, mech->BodyAt(MechCenterTorso).CurInternalStructure + 10.0f);
    CHECK_EQ(static_cast<int32_t>(mech->BodyAt(MechCenterTorso).DamageState), 2);
    CHECK(mech->IsDisabled() != 0 || mech->IsDestroyed() != 0);
}

/// <summary>
/// Port-only (HD art's arms): a hit that takes off an arm notes how far it went past what the arm had left, a share of
/// the arm's whole structure, before the shot is cut to that and the rest passes inward; its armor taken first or
/// already gone. That picks how the arm shows (MCDebris::StyleOfBlow): just enough drops it, a quarter of an arm more
/// throws it, a whole arm more shatters it.
/// </summary>
TEST_CASE_ISOLATED("game: a hit taking off an arm notes how far past it went, for HD art's arms")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCBattleMech> mech = FirstMech();
    REQUIRE(mech != nullptr);
    // No critical hits: the dice could take an arm off with no blow.
    CriticalHitTable[0] = 100;
    constexpr float Exact = 1e-5f;

    // A hit the arm takes notes nothing.
    MCBodyLocation& rightArm = mech->BodyAt(MechRightArm);
    mech->Armor[MechRightArm].CurArmor = 0.0f;
    REQUIRE(rightArm.CurInternalStructure > 1.0f);
    Hit(*mech, MechRightArm, 1.0f);
    CHECK(static_cast<int32_t>(rightArm.DamageState) != 2);
    CHECK_EQ(mech->RightArmBlow, 0.0f);

    // Its armor gone, just what it has left: dropped.
    Hit(*mech, MechRightArm, rightArm.CurInternalStructure);
    CHECK_EQ(static_cast<int32_t>(rightArm.DamageState), 2);
    CHECK_EQ(mech->RightArmBlow, 0.0f);
    CHECK(MCDebris::StyleOfBlow(mech->RightArmBlow) == MCArmStyle::Dropped);

    // Through its armor, half an arm more than it has left: thrown.
    MCBodyLocation& leftArm = mech->BodyAt(MechLeftArm);
    const float whole = static_cast<float>(leftArm.MaxInternalStructure);
    REQUIRE(mech->Armor[MechLeftArm].CurArmor > 0.0f);
    Hit(*mech, MechLeftArm, mech->Armor[MechLeftArm].CurArmor + leftArm.CurInternalStructure + whole / 2.0f);
    CHECK_EQ(static_cast<int32_t>(leftArm.DamageState), 2);
    CHECK(std::abs(mech->LeftArmBlow - 0.5f) < Exact);
    CHECK(MCDebris::StyleOfBlow(mech->LeftArmBlow) == MCArmStyle::Thrown);

    // The bounds.
    CHECK(MCDebris::StyleOfBlow(0.24f) == MCArmStyle::Dropped);
    CHECK(MCDebris::StyleOfBlow(0.25f) == MCArmStyle::Thrown);
    CHECK(MCDebris::StyleOfBlow(0.99f) == MCArmStyle::Thrown);
    CHECK(MCDebris::StyleOfBlow(1.0f) == MCArmStyle::Shattered);
}

/// <summary>
/// A refit (a refit vehicle's repair) brings every armor and internal structure location back to full, the rear torsos
/// to their own armor, but a destroyed arm stays destroyed.
/// </summary>
TEST_CASE_ISOLATED("game: a refit restores every location but a destroyed arm")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCBattleMech> mech = FirstMech();
    REQUIRE(mech != nullptr);

    for (size_t location = 0; location < mech->Armor.size(); location++)
    {
        mech->Armor[location].CurArmor = std::floor(mech->Armor[location].MaxArmor / 2.0f);
    }

    for (int32_t location = 0; location < mech->NumBodyLocations(); location++)
    {
        MCBodyLocation& body = mech->BodyAt(location);
        body.CurInternalStructure = std::max(1.0f, std::floor(body.MaxInternalStructure / 2.0f));
        body.DamageState = 1;
    }

    // The right arm is gone.
    mech->BodyAt(MechRightArm).CurInternalStructure = 0.0f;
    mech->BodyAt(MechRightArm).DamageState = 2;
    mech->Armor[MechRightArm].CurArmor = 0.0f;
    REQUIRE(mech->NeedsRefit(0) != 0);

    int32_t passes = 0;
    float used = 0.0f;

    while (DoRefit(*mech, 1000.0f, used, 0) == 0 && passes < 1000)
    {
        passes++;
    }

    CHECK(passes < 1000);

    for (size_t location = 0; location < mech->Armor.size(); location++)
    {
        MCTest::Scope scope(std::format("armor location {}", location));

        if (location == MechRightArm)
        {
            CHECK_EQ(mech->Armor[location].CurArmor, 0.0f);
            continue;
        }

        CHECK_EQ(mech->Armor[location].CurArmor, static_cast<float>(mech->Armor[location].MaxArmor));
    }

    for (int32_t location = 0; location < mech->NumBodyLocations(); location++)
    {
        MCTest::Scope scope(std::format("body location {}", location));
        const MCBodyLocation& body = mech->BodyAt(location);

        if (location == MechRightArm)
        {
            CHECK_EQ(body.CurInternalStructure, 0.0f);
            CHECK_EQ(static_cast<int32_t>(body.DamageState), 2);
            continue;
        }

        CHECK_EQ(body.CurInternalStructure, static_cast<float>(body.MaxInternalStructure));
        CHECK_EQ(static_cast<int32_t>(body.DamageState), 0);
    }
}
