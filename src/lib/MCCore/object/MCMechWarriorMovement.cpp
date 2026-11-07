#include "stdafx.h"
#include "object/MCMechWarrior.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "object/MCForces.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCTeam.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "sound/MCRadio.h"
#include "terrain/MCTerrain.h"

// The pilot's movement: its move goal and paths, and the movement decision tree.

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

    /// <summary>
    /// Lifts the path locks of the pilot's vehicle and, on a ramming attack, of the mover rammed, so that the path
    /// finder can plan through them (calcMovePath repeats this around each path it plans); returns the search the
    /// paths are planned with.
    /// </summary>
    MCPathSearch BeginPathCalc(MCMechWarrior& pilot, MCMover& mover)
    {
        MCPathSearch search;
        search.MovingObject = mover.SharedAs<MCGameObject>();
        mover.UpdatePathLock(0);

        if (pilot.CurTacOrder.Code == MCTacticalOrderCode::AttackObject && pilot.CurTacOrder.AttackParams.Method == 2)
        {
            search.RamObject = LockObject(pilot.CurTacOrder.Target);

            if (search.RamObject != nullptr && IsMoverClass(search.RamObject->ObjectClass))
            {
                static_cast<MCMover&>(*search.RamObject).UpdatePathLock(0);
            }
        }

        return search;
    }

    /// <summary>Puts back the path locks <see cref="BeginPathCalc"/> lifted for <paramref name="search"/>.</summary>
    void EndPathCalc(const MCPathSearch& search, MCMover& mover)
    {
        if (search.RamObject != nullptr && IsMoverClass(search.RamObject->ObjectClass))
        {
            static_cast<MCMover&>(*search.RamObject).UpdatePathLock(1);
        }

        mover.UpdatePathLock(1);
    }

    /// <summary>The flags calcMovePath adds for the path finder: 0x40, and 0x80 unless the mover is an elemental.</summary>
    uint32_t PathFinderParams(const MCMover& mover, uint32_t moveParams)
    {
        if (mover.ObjectClass != MCObjectClass::Elemental)
        {
            moveParams |= 0x80;
        }

        return moveParams | 0x40;
    }

    /// <summary>The distance past its fire range a mover may stand before its attack move goes on: two vertices.</summary>
    double AttackRangeSlack()
    {
        return static_cast<double>(MetersPerWorldUnit) * MCTerrain::MetersPerVertex +
               static_cast<double>(MetersPerWorldUnit) * MCTerrain::MetersPerVertex;
    }

}

auto MCMechWarrior::SetMoveGoal(uint32_t type, MCVector3D& location, std::shared_ptr<MCGameObject> obj) -> int32_t
{
    MoveOrders.GoalType = static_cast<int32_t>(type);

    if (type == 0)
    {
        if (static_cast<double>(location.Z) < -10.0)
        {
            location.Z = MCTerrain::GetTerrainElevation(location);
        }

        MoveOrders.GoalLocation = location;
        MoveOrders.GoalObject.reset();
        return 0;
    }

    if (type != 0xffffffff)
    {
        if (static_cast<double>(location.Z) < -10.0)
        {
            location.Z = MCTerrain::GetTerrainElevation(location);
        }

        MoveOrders.GoalLocation = location;

        if (obj == nullptr)
        {
            obj = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(static_cast<int32_t>(type)));
        }

        MoveOrders.GoalObject = obj;
        return 0;
    }

    return ClearMoveGoal();
}

auto MCMechWarrior::ClearMoveGoal() -> int32_t
{
    MoveOrders.Origin = 1;
    MoveOrders.GoalType = -1;
    MoveOrders.GoalObject.reset();
    MoveOrders.GoalLocation = MCVector3D(-999999.0f, -999999.0f, -999999.0f);
    return 0;
}

auto MCMechWarrior::PausePath() -> void
{
    if (MoveOrders.Path[0] != nullptr)
    {
        MoveOrders.Path[0]->NumSteps = 0;
    }
}

auto MCMechWarrior::ResumePath() -> void
{
    MCMovePath& path = *MoveOrders.Path[0];

    path.NumSteps = path.NumStepsWhenNotPaused;
}

auto MCMechWarrior::ReachedPathEnd() -> void
{
    MCVector3D nextPoint;
    const int haveNextPoint = GetNextWayPoint(nextPoint, 0);

    if (MoveOrders.PathType == 1)
    {
        if (haveNextPoint != 0)
        {
            const int32_t selectionIndex = CurTacOrder.SelectionIndex;
            MoveOrders.Path[0]->NumSteps = 0;
            RequestMovePath(selectionIndex, 0x281, 1);
            return;
        }
    }
    else
    {
        if (MoveOrders.PathType != 2)
        {
            return;
        }

        if (MoveOrders.Path[0]->GlobalStep != MoveOrders.NumGlobalSteps - 1)
        {
            // On to the next leg of the global path.
            const int32_t selectionIndex = CurTacOrder.SelectionIndex;
            MoveOrders.Path[0]->NumSteps = 0;
            RequestMovePath(selectionIndex, 0x281, 2);
            return;
        }

        if (haveNextPoint != 0)
        {
            return;
        }
    }

    ClearMoveOrders();

    if (CurTacOrder.IsMoveOrder() != 0 || CurTacOrder.IsWayPathOrder() != 0)
    {
        ClearCurTacOrder(1, 0);
    }

    TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-9));
}

auto MCMechWarrior::GetMoveDistanceLeft() -> float
{
    MCMovePath& path = *MoveOrders.Path[0];
    float distance = 0.0f;

    if (path.NumStepsWhenNotPaused > 0)
    {
        distance = path.GetDistanceLeft(GetVehicle()->GetPosition(), -1);

        if (MoveOrders.PathType == 2)
        {
            distance = distance + static_cast<float>(MoveOrders.GlobalPath[path.GlobalStep].CostToGoal);
        }
    }

    return distance;
}

auto MCMechWarrior::IsJumping(MCVector3D& jumpGoal) const -> int
{
    if (LockObject(Vehicle) != nullptr)
    {
        return std::static_pointer_cast<MCMover>(LockObject(Vehicle))->IsJumping(jumpGoal);
    }

    return 0;
}

auto MCMechWarrior::GetMovePath() -> MCMovePath&
{
    if (MoveOrders.Path[0]->NumStepsWhenNotPaused != 0)
    {
        return *MoveOrders.Path[0];
    }

    // The path walked is done: the next leg (if planned) becomes the current one.
    MoveOrders.Path[0]->Clear();
    std::swap(MoveOrders.Path[0], MoveOrders.Path[1]);
    MCMovePath& path = *MoveOrders.Path[0];

    if (path.NumStepsWhenNotPaused <= 0)
    {
        return path;
    }

    std::shared_ptr<MCGameObject> goalObject = LockObject(MoveOrders.GoalObject);
    const int32_t goalType = MoveOrders.GoalType;

    if (goalType == -1)
    {
        path.NumStepsWhenNotPaused = 0;
        return path;
    }

    std::shared_ptr<MCBaseObject> goal;

    if (goalType != 0)
    {
        goal = goalObject;

        if (goal == nullptr)
        {
            goal = ObjectList()->FindObjectFromPart(goalType);
        }

        if (goal == nullptr)
        {
            path.NumStepsWhenNotPaused = 0;
            return path;
        }

        path.Target = static_cast<MCGameObject&>(*goal).GetPosition();
    }

    if (static_cast<double>(MoveOrders.YieldTime) <= -1.0)
    {
        if (static_cast<double>(MoveOrders.WaitForPointTime) <= -1.0)
        {
            MoveOrders.YieldTime = -1.0f;
            MoveOrders.YieldState = 0;
        }
        else
        {
            path.NumSteps = 0;
        }
    }
    else
    {
        path.NumSteps = 0;
        MoveOrders.YieldTime = static_cast<float>(static_cast<double>(ScenarioTime) + 1.5);
    }

    SetMoveGoal(goal != nullptr ? static_cast<uint32_t>(goal->PartId) : 0, path.Goal, nullptr);
    MCMovePath& curPath = *MoveOrders.Path[0];

    if (curPath.GlobalStep == MoveOrders.NumGlobalSteps - 1)
    {
        MoveOrders.GlobalGoalLocation = curPath.StepList[curPath.NumStepsWhenNotPaused - 1].Destination;
        CurTacOrder.SetWayPoint(0, MoveOrders.GlobalGoalLocation);
    }

    return curPath;
}

auto MCMechWarrior::SetMoveWayPath(const MCWayPath& wayPath, int patrol) -> void
{
    MoveOrders.WayPath.clear();

    for (int32_t i = 0; i < wayPath.NumPoints; i++)
    {
        MoveOrders.WayPath.emplace_back(wayPath.Points[i * 3], wayPath.Points[i * 3 + 1], wayPath.Points[i * 3 + 2]);
    }

    MoveOrders.CurWayPt = 0;
    MoveOrders.CurWayDir = patrol != 0 ? 1 : 0;
}

auto MCMechWarrior::ClearMoveWayPath() -> void
{
    MoveOrders.WayPath.clear();
    MoveOrders.CurWayPt = 0;
    MoveOrders.CurWayDir = 0;
}

auto MCMechWarrior::SetMoveGlobalPath(std::span<const MCGlobalPathStep> path) -> void
{
    if (std::ssize(path) > MCGlobalMap::MaxPathSteps)
    {
        Fatal(0, " Global Path Too Long ");
    }

    std::ranges::copy(path, MoveOrders.GlobalPath.begin());
    MoveOrders.NumGlobalSteps = static_cast<int8_t>(path.size());
    MoveOrders.CurGlobalStep = 0;
}

auto MCMechWarrior::RequestMovePath(int32_t selectionIndex, uint32_t moveParams, int32_t source) -> void
{
    PathManager().Request(*this, selectionIndex, moveParams, 255.0f, source);
}

auto MCMechWarrior::CalcMovePath(int32_t selectionIndex, uint32_t moveParams, int32_t source) -> int32_t
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));

    // Start where the vehicle stands (its last valid position when on a blocked cell), or where it lands.
    MCVector3D start;
    MCVector3D jumpGoal;

    if (IsJumping(jumpGoal) == 0)
    {
        // The original tests for a queued jump order here, but both branches read the same position.
        start = mover->GetPosition();
        std::shared_ptr<const MCObjectPosition> position = mover->GetObjPosition();

        if (!CellPassable(position->TileR, position->TileC, position->CellR, position->CellC))
        {
            start = mover->LastValidPosition;
        }
    }
    else
    {
        start = jumpGoal;
    }

    int32_t startTileR;
    int32_t startTileC;
    int32_t startCellR;
    int32_t startCellC;
    MCScenarioMap::WorldToMapPos(start, startTileR, startTileC, startCellR, startCellC);
    const int32_t startArea = GlobalMoveMap().CalcArea(startTileR, startTileC);

    const uint32_t escapeTile = (moveParams >> 13) & 1;
    std::shared_ptr<MCGameObject> goalObject = LockObject(MoveOrders.GoalObject);
    MCVector3D goal = MoveOrders.GoalLocation;

    if (MoveOrders.GoalType == -1)
    {
        LastMoveCalcErr = -1;
        TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-1));
        return LastMoveCalcErr;
    }

    std::shared_ptr<MCGameObject> goalObj;

    if (MoveOrders.GoalType != 0)
    {
        if (goalObject == nullptr)
        {
            goalObject = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(MoveOrders.GoalType));
        }

        goalObj = goalObject;

        if (goalObj == nullptr)
        {
            LastMoveCalcErr = -2;
            TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-2));
            return LastMoveCalcErr;
        }
    }

    // Which of the two paths to plan: the current one (0) or, while one is walked, the next leg (1).
    int32_t pathNum = -1;

    if ((moveParams & 0x100) != 0)
    {
        MoveOrders.OriginalGlobalGoal[0] = goal;
        MoveOrders.PathType = 0;
        MoveOrders.NumGlobalSteps = 0;
        const MCMoveState stateGoal = MoveOrders.MoveStateGoal;

        if (stateGoal == MCMoveState::PivotForward || stateGoal == MCMoveState::PivotReverse ||
            stateGoal == MCMoveState::PivotTarget)
        {
            MoveOrders.MoveState = MCMoveState::Forward;
            MoveOrders.MoveStateGoal = MCMoveState::Forward;
        }

        pathNum = 0;
    }

    const bool yielding = static_cast<double>(MoveOrders.YieldTime) > -1.0;

    if ((moveParams & 0x200) != 0)
    {
        // Start over toward the original goal.
        if (goalObj == nullptr)
        {
            SetMoveGoal(0, MoveOrders.OriginalGlobalGoal[0], nullptr);
        }
        else
        {
            MCVector3D goalPosition = goalObj->GetPosition();
            SetMoveGoal(static_cast<uint32_t>(goalObj->PartId), goalPosition, goalObj);
        }

        goal = MoveOrders.GoalLocation;

        for (int32_t i = 0; i < 2; i++)
        {
            if (MoveOrders.Path[i] != nullptr)
            {
                MoveOrders.Path[i]->Clear();
            }
        }

        MoveOrders.PathType = 0;
        MoveOrders.NumGlobalSteps = 0;
        MoveOrders.MoveState = MCMoveState::Forward;
        MoveOrders.MoveStateGoal = MCMoveState::Forward;
        pathNum = 0;
    }

    if (static_cast<double>(goal.X) < -666000.0)
    {
        LastMoveCalcErr = 0;
        return 0;
    }

    if (pathNum == -1)
    {
        pathNum = MoveOrders.Path[0]->NumStepsWhenNotPaused != 0 ? 1 : 0;
    }

    int32_t numSteps = 0;
    enum class Next
    {
        GlobalLeg,
        GlobalPath,
        TrimFailed,
        TooClose,
    };

    Next next;

    if (MoveOrders.PathType != 0)
    {
        if (MoveOrders.PathType == 2)
        {
            MoveOrders.CurGlobalStep++;
        }

        next = Next::GlobalLeg;
    }
    else
    {
        if (escapeTile == 0)
        {
            if (mover->NetPlayerId > -1 && CurTacOrder.Code != MCTacticalOrderCode::None &&
                CurTacOrder.Origin == MCOrderOrigin::Player)
            {
                moveParams |= 0x800;
            }

            if (mover->CalcMoveGoal(goalObj, goal, 6, 6, 6, selectionIndex, goal, moveParams) != 0)
            {
                LastMoveCalcErr = -3;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-3));
                return LastMoveCalcErr;
            }
        }

        MoveOrders.OriginalGlobalGoal[1] = goal;

        if (escapeTile != 0)
        {
            // Escape from a blocked cell: the nearest open cell toward the goal.
            Assert(pathNum == 0, static_cast<uint32_t>(pathNum),
                   " Warrior.calcMovePath: escapePath should be pathNum 0 ");
            MoveOrders.PathType = 1;
            const MCPathSearch search = BeginPathCalc(*this, *mover);
            MCVector3D escapeGoal;
            MCMovePath& path = *MoveOrders.Path[pathNum];
            numSteps =
                mover->CalcEscapePath(search, path, start, goal, PathFinderParams(*mover, moveParams), escapeGoal);
            EndPathCalc(search, *mover);

            if (numSteps < 1)
            {
                LastMoveCalcErr = -5;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-5));
                return LastMoveCalcErr;
            }

            path.NumSteps = numSteps;
            path.NumStepsWhenNotPaused = numSteps;
            MoveOrders.GlobalGoalLocation = path.StepList[numSteps - 1].Destination;
            CurTacOrder.SetWayPoint(0, MoveOrders.GlobalGoalLocation);
            uint32_t goalId = 0;

            if (goalObj != nullptr)
            {
                MoveOrders.Path[pathNum]->Target = goalObj->GetPosition();
                goalId = static_cast<uint32_t>(goalObj->PartId);
            }

            SetMoveGoal(goalId, goal, nullptr);
            MoveOrders.NextUpdate = MovementUpdateFrequency + ScenarioTime;

            if (pathNum == 0)
            {
                if (yielding)
                {
                    LastMoveCalcErr = 0;
                    MoveOrders.YieldTime = static_cast<float>(static_cast<double>(ScenarioTime) + 1.5);
                    MoveOrders.Path[0]->NumSteps = 0;
                    return 0;
                }

                if (static_cast<double>(MoveOrders.WaitForPointTime) > -1.0)
                {
                    LastMoveCalcErr = 0;
                    MoveOrders.Path[0]->NumSteps = 0;
                    return 0;
                }

                MoveOrders.YieldTime = -1.0f;
                MoveOrders.YieldState = 0;
            }

            LastMoveCalcErr = 0;
            return 0;
        }

        if (mover->DistanceFrom(goal) < MoveMarginOfError[1] && (moveParams & 1) == 0)
        {
            // Already there.
            MoveOrders.Origin = 1;
            MoveOrders.GoalType = -1;
            MoveOrders.GoalObject.reset();
            MoveOrders.GoalLocation = MCVector3D(-999999.0f, -999999.0f, -999999.0f);
            LastMoveCalcErr = -4;
            TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-4));
            return LastMoveCalcErr;
        }

        int32_t goalTileR;
        int32_t goalTileC;
        int32_t goalCellR;
        int32_t goalCellC;
        MCScenarioMap::WorldToMapPos(goal, goalTileR, goalTileC, goalCellR, goalCellC);
        bool simple = std::abs(goalTileR - startTileR) <= SimpleMovePathRange &&
                      std::abs(goalTileC - startTileC) <= SimpleMovePathRange;
        const int32_t longRange = LongRangeMovementEnabled[Team.lock()->Id];
        const bool startAreaOpen = startArea >= 0 && GlobalMoveMap().Areas[startArea].Closed == 0;

        next = Next::GlobalPath;
        bool planLocal = simple;

        if (!simple && longRange == 0)
        {
            // Without long range movement, head SimpleMovePathRange tiles toward the goal.
            const float facing = mover->RelFacingTo(goal, -1);
            const float range = static_cast<float>(static_cast<double>(SimpleMovePathRange) * MetersPerWorldUnit *
                                                   MCTerrain::MetersPerVertex);
            goal = mover->RelativePosition(-facing, range, 2);
            MoveOrders.OriginalGlobalGoal[1] = goal;
            MCScenarioMap::WorldToMapPos(goal, goalTileR, goalTileC, goalCellR, goalCellC);
            simple = true;
            planLocal = true;
        }

        if (planLocal)
        {
            MoveOrders.PathType = 1;
            const MCPathSearch search = BeginPathCalc(*this, *mover);
            numSteps = mover->CalcMovePath(search, *MoveOrders.Path[pathNum], 1, start, goal, {},
                                           PathFinderParams(*mover, moveParams));
            EndPathCalc(search, *mover);

            if (numSteps < 1)
            {
                if (startArea == -1)
                {
                    next = Next::GlobalPath; // Handled below: the alarm order to walk out.
                }
                else if (longRange == 0)
                {
                    LastMoveCalcErr = -5;
                    TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-5));
                    return LastMoveCalcErr;
                }
                else
                {
                    next = Next::GlobalPath;
                }
            }
            else
            {
                if (selectionIndex >= 1)
                {
                    numSteps -= (selectionIndex / GroupMoveTrailLen[1]) * GroupMoveTrailLen[0];
                }

                if (numSteps <= 0)
                {
                    next = Next::TrimFailed;
                }
                else
                {
                    MCMovePath& path = *MoveOrders.Path[pathNum];
                    MoveOrders.GlobalGoalLocation = path.StepList[numSteps - 1].Destination;
                    path.NumSteps = numSteps;
                    path.NumStepsWhenNotPaused = numSteps;
                    CurTacOrder.SetWayPoint(0, MoveOrders.GlobalGoalLocation);
                    uint32_t goalId = 0;

                    if (goalObj != nullptr)
                    {
                        MoveOrders.Path[pathNum]->Target = goalObj->GetPosition();
                        goalId = static_cast<uint32_t>(goalObj->PartId);
                    }

                    SetMoveGoal(goalId, goal, nullptr);
                    MoveOrders.NextUpdate = MovementUpdateFrequency + ScenarioTime;
                    next = simple ? Next::GlobalLeg : Next::GlobalPath;
                }
            }
        }

        if (next == Next::GlobalPath)
        {
            if (planLocal && numSteps < 1 && startArea == -1)
            {
                // No local path out of an area-less tile: first walk out as an alarm order.
                Assert(pathNum == 0 || pathNum == 1, static_cast<uint32_t>(pathNum),
                       " Warrior.calcMovePath: pathNum should be 0 or 1 in Line 2117 ");
                MCTacticalOrder alarmOrder;
                alarmOrder.Reset();
                alarmOrder.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::MoveToPoint, 0);
                alarmOrder.SetWayPoint(0, goal);
                alarmOrder.MoveParams.WayPath.Mode[0] = MoveOrders.Run != 0 ? 1 : 0;
                alarmOrder.MoveParams.EscapeTile = 1;
                alarmOrder.MoveParams.Wait = 0;
                SetAlarmTacOrder(alarmOrder, 255);
                LastMoveCalcErr = -13;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-13));
                return LastMoveCalcErr;
            }

            // A global path: area by area through the doors.
            MoveOrders.GlobalGoalLocation = goal;
            CurTacOrder.SetWayPoint(0, MoveOrders.GlobalGoalLocation);
            const int32_t goalArea = GlobalMoveMap().CalcArea(goalTileR, goalTileC);
            int32_t numGlobalSteps = -1;

            if (startAreaOpen)
            {
                numGlobalSteps = GlobalMoveMap().CalcPath(startArea, goalArea, MoveOrders.GlobalPath);
            }

            if (numGlobalSteps == -1)
            {
                Assert(pathNum == 0 || pathNum == 1, static_cast<uint32_t>(pathNum),
                       " Warrior.calcMovePath: pathNum should be 0 or 1 in Line 2157 ");
                MCTacticalOrder alarmOrder;
                alarmOrder.Reset();
                alarmOrder.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::MoveToPoint, 0);
                alarmOrder.SetWayPoint(0, goal);
                alarmOrder.MoveParams.WayPath.Mode[0] = MoveOrders.Run != 0 ? 1 : 0;
                alarmOrder.MoveParams.EscapeTile = 1;
                alarmOrder.MoveParams.Wait = 0;
                SetAlarmTacOrder(alarmOrder, 255);
                LastMoveCalcErr = -13;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-13));
                return LastMoveCalcErr;
            }

            if (numGlobalSteps == 0)
            {
                ClearMoveOrders();
                LastMoveCalcErr = -7;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-7));

                if ((moveParams & 0x1000) != 0)
                {
                    RadioMessage(MCRadioMessageType::MoveBlocked, 1);
                }

                return LastMoveCalcErr;
            }

            MoveOrders.PathType = 2;
            MoveOrders.NumGlobalSteps = static_cast<int8_t>(numGlobalSteps);
            MoveOrders.CurGlobalStep = 0;
            next = Next::GlobalLeg;
        }
    }

    if (next == Next::GlobalLeg)
    {
        const int8_t pathType = MoveOrders.PathType;

        if (pathType != 2)
        {
            if (pathType != 1 && pathType != 0)
            {
                Fatal(0, " Bad Move Path Type ");
            }

            if (pathNum != 0)
            {
                LastMoveCalcErr = 0;
                return 0;
            }
        }
        else
        {
            // Plan the leg of the global path at curGlobalStep.
            const int32_t step = MoveOrders.CurGlobalStep;

            if (step == MoveOrders.NumGlobalSteps)
            {
                LastMoveCalcErr = 0;
                return 0;
            }

            if (step != 0)
            {
                MCGlobalPathStep prevStep = MoveOrders.GlobalPath[step - 1];
                start = MCGlobalMap::GetDoorWorldPos(prevStep.GoalCell);
            }

            const int32_t lastStep = MoveOrders.NumGlobalSteps - 1;
            MCGlobalPathStep& curStep = MoveOrders.GlobalPath[step];

            if (step < lastStep && GlobalMoveMap().Doors[curStep.GoalDoor].Open == 0)
            {
                // The door out is shut: plan again from the start.
                LastMoveCalcErr = -11;
                ClearMoveWayPath();
                MoveOrders.TimeOfLastStep = ScenarioTime;
                SetMoveGlobalPath({});
                PathManager().Request(*this, selectionIndex, 0x201, 255.0f, source);
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(LastMoveCalcErr));
                return LastMoveCalcErr;
            }

            if (MoveOrders.Path[pathNum] != nullptr)
            {
                MoveOrders.Path[pathNum]->Clear();
            }

            bool trimFailed = false;

            if (step < lastStep)
            {
                const MCPathSearch search = BeginPathCalc(*this, *mover);
                numSteps = mover->CalcMovePath(search, *MoveOrders.Path[pathNum], start, curStep.ThruArea,
                                               curStep.GoalDoor, MoveOrders.GlobalGoalLocation, goal, curStep.GoalCell,
                                               PathFinderParams(*mover, moveParams));
                EndPathCalc(search, *mover);
            }
            else
            {
                goal = MoveOrders.OriginalGlobalGoal[1];
                const MCPathSearch search = BeginPathCalc(*this, *mover);
                numSteps = mover->CalcMovePath(search, *MoveOrders.Path[pathNum], 2, start, goal, curStep.GoalCell,
                                               PathFinderParams(*mover, moveParams));
                EndPathCalc(search, *mover);

                if (numSteps >= 1 && selectionIndex > 0)
                {
                    numSteps -= (selectionIndex / GroupMoveTrailLen[1]) * GroupMoveTrailLen[0];

                    if (numSteps < 1)
                    {
                        trimFailed = true;
                    }
                    else if (pathNum == 0)
                    {
                        MoveOrders.GlobalGoalLocation = MoveOrders.Path[0]->StepList[numSteps - 1].Destination;
                        CurTacOrder.SetWayPoint(0, MoveOrders.GlobalGoalLocation);
                    }
                }
            }

            if (trimFailed)
            {
                ClearMoveOrders();
                LastMoveCalcErr = -4;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-4));
                return LastMoveCalcErr;
            }

            if (numSteps < 1)
            {
                MoveOrders.CurGlobalStep--;
                LastMoveCalcErr = numSteps != -999 ? -8 : -12;
                TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(LastMoveCalcErr));
                return LastMoveCalcErr;
            }

            MCMovePath& path = *MoveOrders.Path[pathNum];
            path.NumSteps = numSteps;
            path.NumStepsWhenNotPaused = numSteps;
            path.GlobalStep = step;

            if (pathNum != 0)
            {
                LastMoveCalcErr = 0;
                return 0;
            }

            uint32_t goalId = 0;

            if (goalObj != nullptr)
            {
                MoveOrders.Path[0]->Target = goalObj->GetPosition();
                goalId = static_cast<uint32_t>(goalObj->PartId);
            }

            SetMoveGoal(goalId, goal, nullptr);
        }

        // The new path waits while the vehicle yields to another or waits for its point.
        if (!yielding)
        {
            if (static_cast<double>(MoveOrders.WaitForPointTime) <= -1.0)
            {
                MoveOrders.YieldTime = -1.0f;
                MoveOrders.YieldState = 0;
            }
            else
            {
                MoveOrders.Path[0]->NumSteps = 0;
            }
        }
        else
        {
            MoveOrders.YieldTime = static_cast<float>(static_cast<double>(ScenarioTime) + 1.5);
            MoveOrders.Path[0]->NumSteps = 0;
        }

        LastMoveCalcErr = 0;
        return 0;
    }

    // next == Next::TrimFailed: the group's trail cut the whole path.
    ClearMoveOrders();
    LastMoveCalcErr = -4;
    TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-4));
    return LastMoveCalcErr;
}

auto MCMechWarrior::GetNextWayPoint(MCVector3D& nextPoint, int incWayPoint) const -> int
{
    MCTacticalOrder order;
    order.Reset();

    if (PeekQueuedTacOrder(order) == 0 && order.Code == MCTacticalOrderCode::MoveToPoint)
    {
        nextPoint = order.GetWayPoint(0);
        return 1;
    }

    return 0;
}

auto MCMechWarrior::MoveGoesOnPast(const MCMovePath& path) const -> bool
{
    if (MoveOrders.PathType == 2 && path.GlobalStep < MoveOrders.NumGlobalSteps - 1)
    {
        return true;
    }

    const std::unique_ptr<MCMovePath>& next = MoveOrders.Path[1];

    if (next != nullptr && next.get() != &path && next->NumStepsWhenNotPaused > 0)
    {
        return true;
    }

    MCVector3D nextPoint;
    return GetNextWayPoint(nextPoint, 0) != 0;
}

auto MCMechWarrior::CalcWithdrawGoal(float withdrawRange) const -> MCVector3D
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    MCVector3D escapeVector;

    if (Team.lock() == InnerSphereTeam() || Team.lock() == AlliedTeam())
    {
        escapeVector = ClanTeam()->CalcEscapeVector(*mover, withdrawRange);
    }
    else
    {
        escapeVector = InnerSphereTeam()->CalcEscapeVector(*mover, withdrawRange);
    }

    Assert(mover != nullptr, 0, " Warrior has NULL Vehicle ");

    if (std::sqrt(escapeVector.Z * escapeVector.Z + escapeVector.Y * escapeVector.Y +
                  escapeVector.X * escapeVector.X) == 0.0f)
    {
        return mover->GetPosition();
    }

    // Walk out along the escape vector in half-cell steps.
    const float stepLength = static_cast<float>(MCTerrain::MetersPerVertexDivMapcellDim * 0.5);
    escapeVector.X = escapeVector.X * stepLength;
    escapeVector.Y = escapeVector.Y * stepLength;
    escapeVector.Z = escapeVector.Z * stepLength;
    MCVector3D goal = mover->GetPosition();

    auto withdrawDistance = [mover](const MCVector3D& point) -> double
    {
        const MCVector3D offset = point - mover->GetPosition();
        return std::sqrt((static_cast<double>(offset.Z) * offset.Z + static_cast<double>(offset.Y) * offset.Y) +
                         static_cast<double>(offset.X) * offset.X) *
               MetersPerWorldUnit;
    };

    double distance = static_cast<float>(withdrawDistance(goal));
    int32_t lastTileR;
    int32_t lastTileC;
    MCScenarioMap::WorldToMapTilePos(goal, lastTileR, lastTileC);

    while (distance < withdrawRange)
    {
        int32_t tileR;
        int32_t tileC;
        MCScenarioMap::WorldToMapTilePos(goal, tileR, tileC);

        if (tileR != lastTileR || tileC != lastTileC)
        {
            // Stop at the map's edge or at a tile with no open cell.
            if (tileR < 0 || tileR >= GameMap().Height || tileC < 0 || tileC >= GameMap().Width)
            {
                break;
            }

            Assert(tileR < GameMap().Height && tileC < GameMap().Width, 0, " Map Tile out of bounds ");

            if ((GameMap().Map[GameMap().Width * tileR + tileC].Cells & 0x55554000) == 0)
            {
                break;
            }

            lastTileR = tileR;
            lastTileC = tileC;
        }

        goal.X = goal.X + escapeVector.X;
        goal.Y = escapeVector.Y + goal.Y;
        goal.Z = escapeVector.Z + goal.Z;
        distance = withdrawDistance(goal);
    }

    return goal;
}

auto MCMechWarrior::MovingOverBlownBridge() -> int
{
    if (!PollMovePath())
    {
        return 0;
    }

    std::shared_ptr<const MCObjectPosition> position =
        std::static_pointer_cast<MCMover>(LockObject(Vehicle))->GetObjPosition();
    const int32_t tileR = position->TileR;
    const int32_t tileC = position->TileC;

    if (OverlayIsBridge[GameMap().Map[GameMap().Width * tileR + tileC].Overlay & 0x7f] != 0)
    {
        const int32_t area = GlobalMoveMap().CalcArea(tileR, tileC);

        // Port fix: the original read the area table at -1 for a tile outside every area.
        if (area >= 0 && GlobalMoveMap().Areas[area].Closed != 0)
        {
            return 1;
        }
    }

    const int32_t bridgeArea = GetMovePath().CrossesBridge(-1, 3);

    if (bridgeArea >= 0 && GlobalMoveMap().Areas[bridgeArea].Closed != 0)
    {
        return 1;
    }

    if (MoveOrders.PathType == 2)
    {
        for (int32_t step = MoveOrders.CurGlobalStep; step < MoveOrders.NumGlobalSteps; step++)
        {
            const MCGlobalMapArea& area = GlobalMoveMap().Areas[MoveOrders.GlobalPath[step].ThruArea];

            if (area.Type != 1 && area.Type != 2)
            {
                return 0;
            }

            if (area.Closed != 0)
            {
                return 1;
            }
        }
    }

    return 0;
}

auto MCMechWarrior::MovementDecisionTree() -> int
{
    // A move that makes no progress for MoveTimeOut seconds is given up.
    if (static_cast<double>(MoveOrders.TimeOfLastStep) > -1.0 &&
        MoveOrders.TimeOfLastStep < static_cast<double>(ScenarioTime) - MoveTimeOut)
    {
        ClearMoveOrders();

        if ((CurTacOrder.IsMoveOrder() != 0 || CurTacOrder.IsWayPathOrder() != 0) &&
            CurTacOrder.Time < static_cast<double>(ScenarioTime) - MoveTimeOut)
        {
            RadioMessage(MCRadioMessageType::MoveBlocked, 1);
            ClearCurTacOrder(1, 0);
        }

        TriggerAlarm(MCPilotAlarmType::NoMovePath, static_cast<uint32_t>(-10));
    }

    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));

    // An elemental that can't jump drops a path through a closed gate.
    if (mover->ObjectClass == MCObjectClass::Elemental && static_cast<MCElemental&>(*mover).ElementalCanJump == 0 &&
        PollMovePath() && GetMovePath().NumSteps > 0 && GetMovePath().CrossesClosedGate(-1, 2) > 0)
    {
        ClearMoveWayPath();

        for (int32_t i = 0; i < 2; i++)
        {
            if (MoveOrders.Path[i] != nullptr)
            {
                MoveOrders.Path[i]->Clear();
            }
        }

        MoveOrders.MoveState = MCMoveState::Forward;
        MoveOrders.MoveStateGoal = MCMoveState::Forward;
        MoveOrders.YieldTime = -1.0f;
        MoveOrders.WaitForPointTime = -1.0f;
        MoveOrders.TimeOfLastStep = -1.0f;
        MoveOrders.YieldState = 0;
        MoveOrders.MoveStateGoalChanged = 0;
        SetMoveGlobalPath({});
        PathManager().Remove(*this);
    }

    if (static_cast<double>(MoveOrders.YieldTime) > -1.0 && MoveOrders.YieldTime < ScenarioTime)
    {
        // Done yielding: plan again when on (or heading over) a blown bridge, and three times in four anyway.
        MoveOrders.YieldTime = MoveYieldTime + ScenarioTime;
        std::shared_ptr<const MCObjectPosition> position = mover->GetObjPosition();
        const int32_t tileR = position->TileR;
        const int32_t tileC = position->TileC;
        bool replan;

        if (OverlayIsBridge[GameMap().Map[GameMap().Width * tileR + tileC].Overlay & 0x7f] != 0 &&
            GlobalMoveMap().Areas[GlobalMoveMap().CalcArea(tileR, tileC)].Closed != 0)
        {
            replan = true;
        }
        else
        {
            replan = RandomNumber(100) < 75;
        }

        bool request = replan;

        if (PollMovePath() && GetMovePath().CrossesBridge(-1, 3) >= 0 && !replan)
        {
            request = MovingOverBlownBridge() != 0;
        }

        if (request)
        {
            RequestMovePath(CurTacOrder.SelectionIndex, 0x201, 3);
        }
    }

    // Plan the next leg of a global path ahead of time.
    if (MoveOrders.PathType == 2 && MoveOrders.CurGlobalStep < MoveOrders.NumGlobalSteps - 1 &&
        MoveOrders.Path[1]->NumStepsWhenNotPaused == 0 && MovePathRequest.lock() == nullptr)
    {
        RequestMovePath(CurTacOrder.SelectionIndex, 1, 4);
    }

    if (ScenarioTime < MovementUpdateTime)
    {
        return 1;
    }

    const MCTacticalOrderCode code = CurTacOrder.Code;
    MovementUpdateTime = MovementUpdateFrequency + ScenarioTime;

    std::shared_ptr<MCGameObject> target;

    if (code == MCTacticalOrderCode::None || code == MCTacticalOrderCode::Stop)
    {
        target = GetLastTarget();
    }
    else
    {
        target = LockObject(MoveOrders.GoalObject);

        if (MoveOrders.GoalType != -1)
        {
            if (MoveOrders.GoalType == 0)
            {
                // Moving to a point: plan again once the path is walked.
                if (static_cast<double>(MoveOrders.YieldTime) > -1.0 || IsJumping() != 0 ||
                    MoveOrders.MoveStateGoalChanged != 0 || static_cast<double>(MoveOrders.WaitForPointTime) > -1.0 ||
                    GetMovePath().NumSteps != 0 || MovePathRequest.lock() != nullptr)
                {
                    return 1;
                }

                RequestMovePath(CurTacOrder.SelectionIndex, 0x101, 9);
                return 1;
            }

            // Moving to an object: follow it when it moves on.
            if (target == nullptr)
            {
                return 1;
            }

            const MCVector3D targetPosition = target->GetPosition();
            const double dx = static_cast<double>(targetPosition.X) - MoveOrders.GoalObjectPosition.X;
            const double dy = static_cast<double>(targetPosition.Y) - MoveOrders.GoalObjectPosition.Y;
            const float dz = targetPosition.Z - MoveOrders.GoalObjectPosition.Z;

            if (50.0 < std::sqrt((dx * dx + dy * dy) + static_cast<double>(dz) * dz))
            {
                MCVector3D goal = target->GetPosition();
                SetMoveGoal(static_cast<uint32_t>(target->PartId), goal, target);
                RequestMovePath(CurTacOrder.SelectionIndex, 0x101, 10);
                return 1;
            }

            std::shared_ptr<MCGameObject> lastTargetNow = GetLastTarget();
            mover->RelViewFacingTo(targetPosition);

            if (lastTargetNow == nullptr || lastTargetNow != target || AttackOrders.Pursue == 0)
            {
                return 1;
            }

            int wantMove = 0;
            uint32_t extraParams = 0;
            bool move = false;

            if (IsMoverClass(lastTargetNow->ObjectClass))
            {
                if (mover->LastOptimalRangeCalc < static_cast<MCMover&>(*lastTargetNow).LastWeaponEffectivenessCalc &&
                    mover->CalcOptimalRange(nullptr) != 0)
                {
                    wantMove = 1;
                    extraParams = 8;
                }
            }
            else if (GetMovePath().NumSteps > 0)
            {
                return 1;
            }

            MCVector3D lastTargetPosition = lastTargetNow->GetPosition();
            const auto distance = static_cast<float>(mover->DistanceFrom(lastTargetPosition));
            const float fireRange = mover->GetFireRange(CurTacOrder.AttackParams.Range);
            const double slack = AttackRangeSlack();

            if (slack < static_cast<double>(distance) - fireRange ||
                static_cast<double>(distance) - fireRange < -slack || WeaponsStatusResult == -3)
            {
                extraParams = 8;
                move = true;
            }
            else if (WeaponsStatusResult >= 0)
            {
                int32_t notReady = 0;
                int32_t notLocked = 0;
                int32_t hot = 0;

                for (int32_t i = 0; i < mover->NumWeapons; i++)
                {
                    if (WeaponStatus(i) == -1)
                    {
                        notReady++;
                    }

                    if (WeaponStatus(i) == -4)
                    {
                        notLocked++;
                    }

                    if (WeaponStatus(i) == -6)
                    {
                        hot++;
                    }
                }

                bool hold = false;

                switch (mover->ObjectClass)
                {
                    case MCObjectClass::BattleMech:
                    {
                        if (WeaponsStatusResult == 0)
                        {
                            if (notReady < 1 && notLocked < 1 && hot < 1)
                            {
                                extraParams = 8;
                                move = true;
                            }
                            else
                            {
                                hold = true;
                            }
                        }
                        break;
                    }
                    case MCObjectClass::GroundVehicle:
                    {
                        if (WeaponsStatusResult == 0 && notReady == 0)
                        {
                            if (notLocked < 1 && hot < 1)
                            {
                                extraParams = 0x400;
                                move = true;
                            }
                            else
                            {
                                hold = true;
                            }
                        }
                        break;
                    }
                    case MCObjectClass::Elemental:
                    {
                        if (WeaponsStatusResult == 0 && notReady == 0)
                        {
                            if (notLocked < 1 && hot < 1)
                            {
                                extraParams = 8;
                                move = true;
                            }
                            else
                            {
                                hold = true;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }

                if (hold && GetMovePath().NumSteps == 0 && MoveOrders.MoveStateGoalChanged == 0)
                {
                    MoveOrders.MoveStateGoal = MCMoveState::PivotTarget;
                }
            }

            if (!move && wantMove == 0)
            {
                return 1;
            }

            MoveOrders.MoveStateGoal = MCMoveState::Forward;
            MCVector3D goal = lastTargetNow->GetPosition();
            SetMoveGoal(static_cast<uint32_t>(lastTargetNow->PartId), goal, lastTargetNow);
            RequestMovePath(CurTacOrder.SelectionIndex, extraParams | 0x101, 11);
            return 1;
        }

        if (code == MCTacticalOrderCode::Withdraw)
        {
            MCVector3D goal = CalcWithdrawGoal(1000.0f);
            SetMoveGoal(0, goal, nullptr);
            RequestMovePath(CurTacOrder.SelectionIndex, 0x101, 5);
            return 1;
        }

        if (CurTacOrder.IsCombatOrder() != 0)
        {
            std::shared_ptr<MCGameObject> attackTarget = GetLastTarget();
            MCVector3D targetPosition;

            if (attackTarget == nullptr)
            {
                if (CurTacOrder.Code != MCTacticalOrderCode::AttackPoint)
                {
                    return 1;
                }

                targetPosition = AttackOrders.TargetPoint;
            }
            else
            {
                targetPosition = attackTarget->GetPosition();
            }

            if (CurTacOrder.AttackParams.Method == 2)
            {
                // Ramming: head for the target itself.
                MCVector3D goal = attackTarget->GetPosition();
                SetMoveGoal(static_cast<uint32_t>(attackTarget->PartId), goal, attackTarget);
                RequestMovePath(CurTacOrder.SelectionIndex, 0x101, 6);
                return 1;
            }

            if (AttackOrders.Pursue == 0)
            {
                MoveOrders.MoveStateGoal = MCMoveState::PivotTarget;
                return 1;
            }

            int wantMove = 0;
            uint32_t extraParams = 0;
            bool move = false;

            if (attackTarget != nullptr && IsMoverClass(attackTarget->ObjectClass) &&
                mover->LastOptimalRangeCalc < static_cast<MCMover&>(*attackTarget).LastWeaponEffectivenessCalc &&
                mover->CalcOptimalRange(nullptr) != 0)
            {
                wantMove = 1;
                extraParams = 8;
            }

            const auto distance = static_cast<float>(mover->DistanceFrom(targetPosition));
            const float fireRange = mover->GetFireRange(CurTacOrder.AttackParams.Range);
            const double slack = AttackRangeSlack();

            if (slack < static_cast<double>(distance) - fireRange ||
                static_cast<double>(distance) - fireRange < -slack || WeaponsStatusResult == -3)
            {
                extraParams = 8;
                move = true;
            }
            else if (WeaponsStatusResult >= 0)
            {
                int32_t notReady = 0;
                int32_t notLocked = 0;
                int32_t hot = 0;

                for (int32_t i = 0; i < mover->NumWeapons; i++)
                {
                    if (WeaponStatus(i) == -1)
                    {
                        notReady++;
                    }

                    if (WeaponStatus(i) == -4)
                    {
                        notLocked++;
                    }

                    if (WeaponStatus(i) == -6)
                    {
                        hot++;
                    }
                }

                const MCObjectClass objectClass = mover->ObjectClass;

                if (WeaponsStatusResult == 0 &&
                    ((objectClass == MCObjectClass::BattleMech) ||
                     ((objectClass == MCObjectClass::GroundVehicle || objectClass == MCObjectClass::Elemental) &&
                      notReady == 0)))
                {
                    if (notReady < 1 && notLocked < 1 && hot < 1)
                    {
                        extraParams = 8;
                        move = true;
                    }
                    else if (GetMovePath().NumSteps == 0 && MoveOrders.MoveStateGoalChanged == 0)
                    {
                        MoveOrders.MoveStateGoal = MCMoveState::PivotTarget;
                    }
                }
            }

            if (!move && wantMove == 0)
            {
                return 1;
            }

            MoveOrders.MoveStateGoal = MCMoveState::Forward;

            if (attackTarget == nullptr)
            {
                SetMoveGoal(0, targetPosition, nullptr);
                RequestMovePath(CurTacOrder.SelectionIndex, 0x101, 8);
                return 1;
            }

            SetMoveGoal(static_cast<uint32_t>(attackTarget->PartId), targetPosition, attackTarget);
            RequestMovePath(CurTacOrder.SelectionIndex, extraParams | 0x101, 7);
            return 1;
        }

        target = GetLastTarget();
    }

    if (target == nullptr)
    {
        return 1;
    }

    MoveOrders.MoveStateGoal = MCMoveState::PivotTarget;
    return 1;
}
