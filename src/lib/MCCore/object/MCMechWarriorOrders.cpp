#include "stdafx.h"
#include "object/MCMechWarrior.h"
#include "abl/MCScrollingTextWindow.h"
#include "lib/MCFatal.h"
#include "main/MCGameSession.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMoverGroup.h"
#include "object/MCBattleMech.h"
#include "sprite/MCMechActor.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTreeBuildingType.h"
#include "sound/MCRadio.h"
#include "terrain/MCTerrain.h"

// The pilot's orders: the three order slots and the current order, the main decision tree, and the order calls.

namespace
{
    /// <summary>Whether movement cell (cellR, cellC) of tile (tileR, tileC) can be entered.</summary>
    bool CellPassable(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC)
    {
        // Port fix: the walks can leave the map, where the original reads outside it. Off the map is impassable.
        if (!GameMap().OnMap(tileR, tileC))
        {
            return false;
        }

        return GameMap().Map[GameMap().Width * tileR + tileC].GetCellPassable(cellR, cellC) != 0;
    }

    /// <summary>Whether the movement cell under <paramref name="position"/> can be entered.</summary>
    bool PositionPassable(MCVector3D position)
    {
        int32_t tileR;
        int32_t tileC;
        int32_t cellR;
        int32_t cellC;
        MCScenarioMap::WorldToMapPos(position, tileR, tileC, cellR, cellC);
        return CellPassable(tileR, tileC, cellR, cellC);
    }

    /// <summary>Stores <paramref name="point"/> as a tactical order's first way point (the original's inline copy).</summary>
    void SetFirstWayPoint(MCTacticalOrder& order, MCVector3D point)
    {
        order.MoveParams.WayPath.Points[0] = point.X;
        order.MoveParams.WayPath.Points[1] = point.Y;
        order.MoveParams.WayPath.Points[2] = point.Z;
    }

    /// <summary>
    /// The refit flag orderGetFixed reads at +0x130 of its target: a repair bay's mechBay. On a refit vehicle the
    /// original read the mover's ECM tracker pointer there; the port gives what that pointer compared as.
    /// </summary>
    int32_t RepairBayKind(MCGameObject& target)
    {
        if (target.ObjectClass == MCObjectClass::TreeBuilding)
        {
            return static_cast<MCTreeBuilding&>(target).MechBay;
        }

        if (IsMoverClass(target.ObjectClass))
        {
            return static_cast<MCMover&>(target).EcmTracker.lock() != nullptr ? 2 : 0;
        }

        return 0;
    }

    /// <summary>What the attack orders print in GameSystemWindow when it exists.</summary>
    void PrintAttackOrder(MCMechWarrior& pilot)
    {
        if (!HasGameSystemWindow())
        {
            return;
        }

        auto print = [](std::string line) { PrintGameSystem(std::move(line)); };
        print("");
        print("-----------------------------------");
        print(std::format("{}:", pilot.Name));
        const auto mover = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle));
        const MCMasterComponent& weapon = MasterComponentList[mover->Inventory[mover->LongestRangeWeapon].MasterID];
        print(std::format("Longest Range Weapon = {} ({:.4f})", weapon.Name, weapon.WeaponRange[3]));
        print(std::format("Optimal Range = {:.4f}", mover->OptimalRange));
        print("-----------------------------------");
    }
}

auto MCMechWarrior::ClearCurTacOrder(int updateTacOrder, int updateBrain) -> void
{
    CurTacOrder.Reset();

    if (updateTacOrder == 0)
    {
        ClearMoveOrders();
        TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-14));
    }

    ClearAttackOrders();
    LastTacOrder.LastTime = ScenarioTime;

    if (updateTacOrder == 0)
    {
        return;
    }

    // Fall back to the order underneath: alarm to player (or general), player to general.
    MCTacticalOrder newOrder;
    newOrder.Reset();

    switch (OrderState)
    {
        case MCOrderState::General:
        {
            if (NewTacOrderReceivedOf(MCOrderState::General) == 0)
            {
                TacOrderOf(MCOrderState::General).Reset();
            }
            break;
        }
        case MCOrderState::Player:
        {
            if (NewTacOrderReceivedOf(MCOrderState::Player) == 0)
            {
                TacOrderOf(MCOrderState::Player).Reset();
                PlayerOrderFromQueue = 0;
            }

            newOrder = TacOrderOf(MCOrderState::General);
            OrderState = MCOrderState::General;
            break;
        }
        case MCOrderState::Alarm:
        {
            if (NewTacOrderReceivedOf(MCOrderState::Alarm) == 0)
            {
                TacOrderOf(MCOrderState::Alarm).Reset();
            }

            AlarmPriority = 0;

            if (TacOrderOf(MCOrderState::Player).Code != MCTacticalOrderCode::None)
            {
                newOrder = TacOrderOf(MCOrderState::Player);
                OrderState = MCOrderState::Player;
            }
            else
            {
                newOrder = TacOrderOf(MCOrderState::General);
                OrderState = MCOrderState::General;
            }
            break;
        }
        default:
            break;
    }

    if (PlayerOrderFromQueue == 0)
    {
        ClearMoveOrders();
        TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-14));
    }

    for (std::unique_ptr<MCMovePath>& path : MoveOrders.Path)
    {
        path->NumSteps = 0;
    }

    const MCMoveState moveState = MoveOrders.MoveState;
    const MCMoveState moveStateGoal = MoveOrders.MoveStateGoal;
    MoveOrders.Reset();
    PathManager().Remove(*this);
    MoveOrders.MoveState = moveState;
    MoveOrders.MoveStateGoal = moveStateGoal;
    AttackOrders.Reset();
    int32_t message = -1;
    newOrder.Execute(*this, message);

    if (OrderState == MCOrderState::Player)
    {
        RadioMessage(message, 1);
    }
}

auto MCMechWarrior::SetCurTacOrder(MCTacticalOrder tacOrder) -> void
{
    CurTacOrder = tacOrder;
    LastTacOrder = tacOrder;
}

auto MCMechWarrior::SetGeneralTacOrder(MCTacticalOrder order) -> void
{
    TacOrderOf(MCOrderState::General) = order;
    NewTacOrderReceivedOf(MCOrderState::General) = 1;
}

auto MCMechWarrior::SetPlayerTacOrder(MCTacticalOrder order, int fromQueue) -> void
{
    TacOrderOf(MCOrderState::Player) = order;
    NewTacOrderReceivedOf(MCOrderState::Player) = 1;
    PlayerOrderFromQueue = fromQueue;

    if (fromQueue == 0)
    {
        ClearTacOrderQueue();
    }
}

auto MCMechWarrior::SetAlarmTacOrder(MCTacticalOrder order, int32_t priority) -> void
{
    if (AlarmPriority <= priority)
    {
        TacOrderOf(MCOrderState::Alarm) = order;
        NewTacOrderReceivedOf(MCOrderState::Alarm) = 1;
        AlarmPriority = priority;
    }
}

auto MCMechWarrior::UpdateActions() -> void
{
    if (std::static_pointer_cast<MCMover>(LockObject(Vehicle))->IsCaptured() != 0)
    {
        ClearCurTacOrder(1, 0);
        SetLastTarget(nullptr, 0, 0);
        return;
    }

    if (CombatUpdateTime <= ScenarioTime)
    {
        CombatDecisionTree();
    }

    MovementDecisionTree();
}

auto MCMechWarrior::MainDecisionTree() -> int32_t
{
    // The current order: when done, the next queued player order or the one underneath takes over.
    const bool server = MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0;

    if (server && CurTacOrder.Code == MCTacticalOrderCode::None && TacOrderQueueExecuting && !QueuedOrders.Empty() &&
        NewTacOrderReceivedOf(MCOrderState::Player) == 0)
    {
        ExecuteTacOrderQueue();
    }

    if (CurTacOrder.Code != MCTacticalOrderCode::None && CurTacOrder.Status(*this) == 1)
    {
        Assert(MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0, 0, " MechWarrior.mainDecisionTree: client! ");

        if (OrderState == MCOrderState::Player && TacOrderQueueExecuting)
        {
            ExecuteTacOrderQueue();
        }

        ClearCurTacOrder(1, 0);
    }

    if (OnHomeTeam() != 0 && CurTacOrder.Code == MCTacticalOrderCode::None && TimeOfLastOrders < 0.0f)
    {
        TimeOfLastOrders = ScenarioTime;
    }

    if (BrainUpdateTime <= ScenarioTime || CombatUpdateTime <= ScenarioTime || MovementUpdateTime <= ScenarioTime)
    {
        std::shared_ptr<MCGameObject> target = GetLastTarget();
        std::optional<MCVector3D> targetPoint;
        bool update = true;

        if (target == nullptr)
        {
            if (CurTacOrder.Code != MCTacticalOrderCode::AttackPoint)
            {
                update = false;
            }
            else
            {
                targetPoint = AttackOrders.TargetPoint;
            }
        }

        if (update)
        {
            WeaponsStatus.resize(std::max(WeaponsStatus.size(),
                                          size_t{std::static_pointer_cast<MCMover>(LockObject(Vehicle))->NumWeapons}));
            WeaponsStatusResult = CalcWeaponsStatus(target, WeaponsStatus, targetPoint);
        }
    }

    if (BrainUpdateTime <= ScenarioTime)
    {
        if (Alignment != -1 || Duh == 0)
        {
            RunBrain();
        }

        BrainUpdateTime = BrainUpdateFrequency + BrainUpdateTime;
    }

    std::shared_ptr<MCGameObject> target = GetLastTarget();

    if (target != nullptr && LastTargetTime == ScenarioTime)
    {
        WeaponsStatus.resize(
            std::max(WeaponsStatus.size(), size_t{std::static_pointer_cast<MCMover>(LockObject(Vehicle))->NumWeapons}));
        WeaponsStatusResult = CalcWeaponsStatus(target, WeaponsStatus, std::nullopt);
    }

    CheckAlarms();

    // Take a new order: an alarm order overrides the player's, which overrides the general one.
    MCTacticalOrder newOrder;
    newOrder.Reset();

    switch (OrderState)
    {
        case MCOrderState::General:
        {
            if (NewTacOrderReceivedOf(MCOrderState::Alarm) != 0)
            {
                ClearCurTacOrder(0, 0);
                newOrder = TacOrderOf(MCOrderState::Alarm);
                OrderState = MCOrderState::Alarm;
            }
            else if (NewTacOrderReceivedOf(MCOrderState::Player) != 0)
            {
                TacOrderOf(MCOrderState::General).Reset();
                newOrder = TacOrderOf(MCOrderState::Player);
                OrderState = MCOrderState::Player;
            }
            else if (NewTacOrderReceivedOf(MCOrderState::General) != 0)
            {
                newOrder = TacOrderOf(MCOrderState::General);
            }
            break;
        }
        case MCOrderState::Player:
        {
            if (NewTacOrderReceivedOf(MCOrderState::Alarm) != 0)
            {
                newOrder = TacOrderOf(MCOrderState::Alarm);
                OrderState = MCOrderState::Alarm;
            }
            else if (NewTacOrderReceivedOf(MCOrderState::Player) != 0)
            {
                TacOrderOf(MCOrderState::General).Reset();
                newOrder = TacOrderOf(MCOrderState::Player);
            }
            break;
        }
        case MCOrderState::Alarm:
        {
            if (NewTacOrderReceivedOf(MCOrderState::Player) != 0)
            {
                // Original behaviour (OB-008): the player's order runs, but orderState stays ALARM.
                AlarmPriority = 0;
                TacOrderOf(MCOrderState::Alarm).Reset();
                newOrder = TacOrderOf(MCOrderState::Player);
            }
            else if (NewTacOrderReceivedOf(MCOrderState::Alarm) != 0)
            {
                newOrder = TacOrderOf(MCOrderState::Alarm);
            }
            break;
        }
        default:
            break;
    }

    if (newOrder.Code != MCTacticalOrderCode::None)
    {
        // A move to a point or object keeps the path walked (a new path replaces it when planned).
        if (newOrder.Code != MCTacticalOrderCode::MoveToPoint && newOrder.Code != MCTacticalOrderCode::MoveToObject)
        {
            MoveOrders.Path[0]->NumSteps = 0;
        }

        MoveOrders.Path[1]->NumSteps = 0;
        const int32_t run = MoveOrders.Run;
        const MCMoveState moveState = MoveOrders.MoveState;
        const MCMoveState moveStateGoal = MoveOrders.MoveStateGoal;
        MoveOrders.Reset();
        MoveOrders.Run = run;
        PathManager().Remove(*this);
        MoveOrders.MoveState = moveState;
        MoveOrders.MoveStateGoal = moveStateGoal;
        AttackOrders.Reset();
        int32_t message = -1;
        newOrder.Execute(*this, message);

        if (OrderState == MCOrderState::Player)
        {
            RadioMessage(message, 1);
        }

        SetCurTacOrder(newOrder);
        TimeOfLastOrders = -1.0f;
    }

    NewTacOrderReceivedOf(MCOrderState::General) = 0;
    NewTacOrderReceivedOf(MCOrderState::Player) = 0;
    NewTacOrderReceivedOf(MCOrderState::Alarm) = 0;
    UpdateActions();
    return 0;
}

auto MCMechWarrior::OrderWait(int unitOrder, MCOrderOrigin origin, int32_t seconds, int clearLastTarget) -> int32_t
{
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::Wait, unitOrder);
    order.DelayedTime = static_cast<float>(seconds) + ScenarioTime;
    ClearMoveOrders();
    ClearAttackOrders();

    if (clearLastTarget != 0)
    {
        SetLastTarget(nullptr, 0, 0);
    }

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return order.Status(*this);
}

auto MCMechWarrior::OrderStop(int unitOrder, int setTacOrder) -> int32_t
{
    MCTacticalOrder order;
    order.Reset();
    order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Stop, unitOrder);
    ClearTacOrderQueue();
    ClearMoveOrders();
    ClearAttackOrders();
    SetLastTarget(nullptr, 0, 0);

    // Port-only (MechBraking): the player's STOP, which a quick stop brakes (MCMechActor::QuickStop).
    if (const auto mech = std::dynamic_pointer_cast<MCBattleMech>(GetVehicle()); mech != nullptr)
    {
        static_cast<MCMechActor&>(*mech->Appearance).QuickStopAsked = true;
    }

    return order.Status(*this);
}

auto MCMechWarrior::OrderMoveToPoint(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCVector3D location,
                                     int32_t selectionIndex, uint32_t params) -> int32_t
{
    const uint32_t escapeTile = (params >> 6) & 1;
    const uint32_t run = params & 1;
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::MoveToPoint, unitOrder);
    SetFirstWayPoint(order, location);
    order.MoveParams.WayPath.Mode[0] = run != 0 ? 1 : 0;
    order.MoveParams.Wait = (params >> 1) & 1;
    order.SelectionIndex = selectionIndex;
    order.MoveParams.Mode = (params >> 3) & 1;
    order.MoveParams.EscapeTile = escapeTile;
    const int32_t result = order.Status(*this);

    if (result == 1)
    {
        return 1;
    }

    ClearMoveGoal();
    ClearMoveWayPath();
    SetMoveGoal(0, location, nullptr);
    MoveOrders.TimeOfLastStep = ScenarioTime;
    MoveOrders.Run = run;

    if (setTacOrder != 0)
    {
        ClearAttackOrders();
    }

    uint32_t moveParams = escapeTile != 0 ? 0x2101 : 0x101;

    // The player's order to a unit's point (or to one mover) reports a blocked move on the radio.
    if (setTacOrder != 0 && origin == MCOrderOrigin::Player && (unitOrder == 0 || GetPoint() == LockObject(Vehicle)))
    {
        moveParams |= 0x1000;
    }

    PathManager().Request(*this, selectionIndex, moveParams, 255.0f, 15);

    if (setTacOrder != 0 && result == 0 && origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return result;
}

auto MCMechWarrior::OrderMoveToObject(int unitOrder, int setTacOrder, MCOrderOrigin origin,
                                      std::shared_ptr<MCGameObject> target, int32_t selectionIndex, uint32_t params)
    -> int32_t
{
    const uint32_t faceObject = (params >> 2) & 1;

    if (target == nullptr)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::MoveToObject, unitOrder);
    order.SelectionIndex = selectionIndex;
    order.MoveParams.WayPath.Mode[0] = (params & 1) != 0 ? 1 : 0;
    order.MoveParams.FaceObject = faceObject;
    order.MoveParams.Mode = (params >> 3) & 1;
    order.Target = target;
    order.MoveParams.Wait = 0;
    const int32_t result = order.Status(*this);

    if (result == 1)
    {
        return 1;
    }

    MCVector3D goal = target->GetPosition();
    SetMoveGoal(static_cast<uint32_t>(target->PartId), goal, target);
    MoveOrders.Run = params & 1;

    if (setTacOrder != 0)
    {
        ClearAttackOrders();
    }

    uint32_t moveParams = faceObject != 0 ? 0x101 : 0x100;

    if (setTacOrder != 0 && origin == MCOrderOrigin::Player && (unitOrder == 0 || GetPoint() == LockObject(Vehicle)))
    {
        moveParams |= 0x1000;
    }

    RequestMovePath(selectionIndex, moveParams, 12);
    MoveOrders.GoalObjectPosition = target->GetPosition();

    if (setTacOrder != 0 && result == 0 && origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return result;
}

auto MCMechWarrior::OrderJumpToPoint(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCVector3D location,
                                     int32_t selectionIndex) -> int32_t
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    const float jumpRange = mover->GetJumpRange();

    if (mover->DistanceFrom(location) <= jumpRange)
    {
        // A mech can't land on a blocked cell.
        if (mover->ObjectClass == MCObjectClass::BattleMech && !PositionPassable(location))
        {
            return 1;
        }

        MCTacticalOrder order;
        order.Reset();
        order.Reset(origin, MCTacticalOrderCode::JumpToPoint, unitOrder);
        order.SelectionIndex = selectionIndex;
        SetFirstWayPoint(order, location);
        const int32_t result = order.Status(*this);

        if (result != 1 && setTacOrder != 0)
        {
            ClearMoveOrders();
            ClearAttackOrders();

            if (result == 0 && origin == MCOrderOrigin::Commander)
            {
                SetGeneralTacOrder(order);
            }
        }
    }

    return 1;
}

auto MCMechWarrior::OrderJumpToObject(int unitOrder, int setTacOrder, MCOrderOrigin origin,
                                      std::shared_ptr<MCGameObject> target, int32_t selectionIndex) -> int32_t
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    MCVector3D location = target->GetPosition();

    if (IsMoverClass(target->ObjectClass) && target->GetTeam() == mover->GetTeam())
    {
        return 1;
    }

    const float jumpRange = mover->GetJumpRange();

    if (mover->DistanceFrom(location) <= jumpRange)
    {
        if (mover->ObjectClass == MCObjectClass::BattleMech && !PositionPassable(location))
        {
            return 1;
        }

        MCTacticalOrder order;
        order.Reset();
        order.Reset(origin, MCTacticalOrderCode::JumpToPoint, unitOrder);
        order.SelectionIndex = selectionIndex;
        SetFirstWayPoint(order, location);
        order.Target = target;
        const int32_t result = order.Status(*this);

        if (result != 1 && setTacOrder != 0)
        {
            ClearMoveOrders();
            ClearAttackOrders();

            if (result == 0 && origin == MCOrderOrigin::Commander)
            {
                SetGeneralTacOrder(order);
            }
        }
    }

    return 1;
}

auto MCMechWarrior::OrderTraversePath(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCWayPath& wayPath,
                                      uint32_t params) -> int32_t
{
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::TraversePath, unitOrder);
    order.MoveParams.WayPath = wayPath;
    order.MoveParams.Mode = (params >> 3) & 1;
    const int32_t result = order.Status(*this);

    if (result == 1)
    {
        return 1;
    }

    MCVector3D firstPoint(order.MoveParams.WayPath.Points[0], order.MoveParams.WayPath.Points[1],
                          order.MoveParams.WayPath.Points[2]);
    SetMoveGoal(0, firstPoint, nullptr);
    SetMoveWayPath(wayPath, 0);

    if (setTacOrder != 0)
    {
        ClearAttackOrders();
    }

    RequestMovePath(-1, 0x101, 13);

    if (setTacOrder != 0 && result == 0 && origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return result;
}

auto MCMechWarrior::OrderPatrolPath(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCWayPath& wayPath) -> int32_t
{
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::PatrolPath, unitOrder);
    order.MoveParams.WayPath = wayPath;
    const int32_t result = order.Status(*this);

    if (result == 1)
    {
        return 1;
    }

    MCVector3D firstPoint(order.MoveParams.WayPath.Points[0], order.MoveParams.WayPath.Points[1],
                          order.MoveParams.WayPath.Points[2]);
    SetMoveGoal(0, firstPoint, nullptr);
    SetMoveWayPath(wayPath, 1);

    if (setTacOrder != 0)
    {
        ClearAttackOrders();
    }

    RequestMovePath(-1, 0x101, 14);

    if (setTacOrder != 0 && result == 0 && origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return result;
}

auto MCMechWarrior::OrderPowerUp(int unitOrder, MCOrderOrigin origin) -> int32_t
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));

    if (static_cast<int8_t>(mover->Status) != 5)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::PowerUp, unitOrder);
    const int32_t result = order.Status(*this);

    if (result == 1)
    {
        return 1;
    }

    ClearMoveOrders();
    ClearAttackOrders();

    if (mover != nullptr && mover->CanPowerUp() != 0)
    {
        mover->StartUp();
    }

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }
    else if (origin == MCOrderOrigin::Self)
    {
        SetAlarmTacOrder(order, 255);
    }

    return result;
}

auto MCMechWarrior::OrderPowerDown(int unitOrder, MCOrderOrigin origin) -> int32_t
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    const int8_t vehicleStatus = static_cast<int8_t>(mover->Status);

    if (vehicleStatus == 5 || vehicleStatus == 4)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::PowerDown, unitOrder);
    const int32_t result = order.Status(*this);

    if (result == 1)
    {
        return 1;
    }

    ClearMoveOrders();
    ClearAttackOrders();

    if (mover != nullptr)
    {
        mover->ShutDown();
    }

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return result;
}

auto MCMechWarrior::OrderAttackObject(int unitOrder, MCOrderOrigin origin, std::shared_ptr<MCGameObject> target,
                                      int32_t type, int32_t method, int32_t range, int32_t aimLocation, uint32_t params)
    -> int32_t
{
    const uint32_t pursue = (params >> 4) & 1;
    const uint32_t obliterate = (params >> 5) & 1;
    const int conserveAmmo = type == 3 ? 1 : 0;

    if (target == nullptr)
    {
        ClearAttackOrders();
        return 1;
    }

    // Only a ram needs no weapons.
    if (std::static_pointer_cast<MCMover>(LockObject(Vehicle))->NumWeapons == 0 && method != 2)
    {
        ClearAttackOrders();
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::AttackObject, unitOrder);
    order.Target = target;
    order.AttackParams.Type = type;
    order.AttackParams.Method = method;
    order.AttackParams.AimLocation = aimLocation;

    if (method == 2)
    {
        range = -3;
    }

    order.AttackParams.Range = range;
    order.AttackParams.Pursue = pursue;
    order.AttackParams.Obliterate = obliterate;

    if (order.Status(*this) == 1)
    {
        return 1;
    }

    OrderUseFireRange(range);

    if (pursue == 0)
    {
        ClearMoveOrders();
    }
    else
    {
        OrderMoveToObject(unitOrder, 0, origin, target, -1, params | 4);
    }

    AttackOrders.Type = type;
    SetAttackTarget(target);
    AttackOrders.Pursue = pursue;
    AttackOrders.AimLocation = aimLocation;
    SetLastTarget(target, obliterate, conserveAmmo);

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    PrintAttackOrder(*this);
    return 0;
}

auto MCMechWarrior::OrderAttackPoint(int unitOrder, MCOrderOrigin origin, MCVector3D location, int32_t type,
                                     int32_t method, int32_t range, uint32_t params) -> int32_t
{
    const uint32_t pursue = (params >> 4) & 1;
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::AttackPoint, unitOrder);
    order.AttackParams.Type = type;
    order.AttackParams.Method = method;
    order.AttackParams.Range = range;
    order.AttackParams.TargetPoint = location;
    order.AttackParams.Pursue = pursue;

    if (order.Status(*this) == 1)
    {
        return 1;
    }

    OrderUseFireRange(range);

    if (pursue == 0)
    {
        ClearMoveOrders();
    }
    else
    {
        OrderMoveToPoint(unitOrder, 0, origin, location, -1, params);
    }

    AttackOrders.Type = type;
    SetAttackTarget(nullptr);
    SetAttackTargetPoint(location);
    AttackOrders.AimLocation = -1;
    AttackOrders.Pursue = pursue;
    SetLastTarget(nullptr, 0, 0);

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    PrintAttackOrder(*this);
    return 0;
}

auto MCMechWarrior::OrderWithdraw(int unitOrder, MCOrderOrigin origin, MCVector3D location) -> int32_t
{
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::Withdraw, unitOrder);
    SetFirstWayPoint(order, location);
    const MCVector3D goal = CalcWithdrawGoal(1000.0f);
    const int32_t result = OrderMoveToPoint(unitOrder, 1, origin, goal, -1, 1);
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    Assert(mover != nullptr, 0, " orderWithdraw:Warrior has no Vehicle ");
    mover->Withdrawing = true;

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    CurTacOrder.Code = MCTacticalOrderCode::Withdraw;
    return result;
}

auto MCMechWarrior::OrderEject(int unitOrder, int setTacOrder, MCOrderOrigin origin) -> int32_t
{
    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::Eject, unitOrder);
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    Assert(mover != nullptr, 0, " orderWithdraw:Warrior has no Vehicle ");
    mover->HandleEjection();

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return 1;
}

auto MCMechWarrior::OrderUseFireRange(int32_t range) -> int32_t
{
    OrderFireRange = std::static_pointer_cast<MCMover>(LockObject(Vehicle))->GetFireRange(range);
    return 1;
}

auto MCMechWarrior::OrderRefit(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params) -> int32_t
{
    if (target == nullptr || target->ObjectClass != MCObjectClass::BattleMech)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::Refit, 0);
    order.Target = target;
    order.SelectionIndex = -1;
    order.MoveParams.WayPath.Mode[0] = static_cast<uint8_t>(params & 1);
    order.MoveParams.FaceObject = 1;
    order.MoveParams.Wait = 0;

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return 0;
}

auto MCMechWarrior::OrderGetFixed(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params)
    -> int32_t
{
    if (target == nullptr)
    {
        return 1;
    }

    if (target->ObjectClass != MCObjectClass::TreeBuilding && target->GetRefitPoints() <= 0.0)
    {
        return 1;
    }

    // A mech bay fixes mechs, a vehicle bay vehicles.
    const MCObjectClass vehicleClass = LockObject(Vehicle)->ObjectClass;
    const int32_t bayKind = RepairBayKind(*target);

    if ((vehicleClass == MCObjectClass::BattleMech && bayKind == 0) ||
        (vehicleClass == MCObjectClass::GroundVehicle && bayKind == 1))
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::GetFixed, 0);
    order.Target = target;
    order.SelectionIndex = -1;
    order.MoveParams.WayPath.Mode[0] = static_cast<uint8_t>(params & 1);
    order.MoveParams.FaceObject = 1;
    order.MoveParams.Wait = 0;

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return 0;
}

auto MCMechWarrior::OrderLoadIntoCarrier(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params)
    -> int32_t
{
    if (LockObject(Vehicle)->ObjectClass != MCObjectClass::Elemental || target == nullptr ||
        target->ObjectClass != MCObjectClass::GroundVehicle ||
        std::static_pointer_cast<MCGroundVehicle>(target)->ElementalCarrier == 0)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::LoadIntoCarrier, 0);
    order.Target = target;
    order.SelectionIndex = -1;
    order.MoveParams.WayPath.Mode[0] = static_cast<uint8_t>(params & 1);
    order.MoveParams.FaceObject = 1;
    order.MoveParams.Wait = 0;

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return 0;
}

auto MCMechWarrior::OrderDeployElementals(MCOrderOrigin origin, uint32_t params) -> int32_t
{
    if (LockObject(Vehicle)->ObjectClass != MCObjectClass::GroundVehicle ||
        std::static_pointer_cast<MCGroundVehicle>(LockObject(Vehicle))->ElementalCarrier == 0)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::DeployElementals, 0);
    order.MoveParams.Wait = 0;
    order.MoveParams.WayPath.Mode[0] = static_cast<uint8_t>(params & 1);

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return 0;
}

auto MCMechWarrior::OrderCapture(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params) -> int32_t
{
    // Original behaviour: the test reads isCaptureable() == 0 (the slot's name may not match its meaning).
    if (target == nullptr || target->IsCaptureable() != 0 || target->GetAlignment() == Alignment ||
        target->GetCaptureBlocker(Alignment) != nullptr)
    {
        return 1;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Reset(origin, MCTacticalOrderCode::Capture, 0);
    order.Target = target;
    order.SelectionIndex = -1;
    order.MoveParams.WayPath.Mode[0] = static_cast<uint8_t>(params & 1);
    order.MoveParams.FaceObject = 1;
    order.MoveParams.Wait = 0;

    if (origin == MCOrderOrigin::Commander)
    {
        SetGeneralTacOrder(order);
    }

    return 0;
}
