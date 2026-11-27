#include "stdafx.h"
#include "ai/MCTacticalOrder.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCRefit.h"
#include "iface/MCTacticalInterface.h"
#include "iface/MCCommandParser.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCBigGameObject.h"
#include "object/MCMoverGroup.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTreeBuildingType.h"
#include "object/MCMechWarrior.h"
#include "sound/MCRadio.h"
#include "sprite/MCMechActor.h"
#include "terrain/MCTacticalMap.h"
#include "object/MCWeaponShotInfo.h"

namespace
{
    /// <summary>Degrees to radians, as MCX.EXE stores it (a hair under pi / 180).</summary>
    constexpr double DegreesToRadians = 0x1.1df46a2526c7ap-6;

    /// <summary>The packed code of a Guard order on a point (rather than an object).</summary>
    constexpr uint32_t GuardPointCode = 0x1f;

    /// <summary>The origin as the pilot's order functions take it.</summary>
    int32_t OriginValue(MCOrderOrigin origin)
    {
        return static_cast<int32_t>(origin);
    }

    /// <summary>The move flags of a move order: 1 run, 2 wait, 4 face the object, 8 mode 1, 0x40 escape the tile.</summary>
    uint32_t MoveFlags(const MCTacOrderMoveParams& params, bool faceObject, bool wait, bool escapeTile)
    {
        uint32_t flags = params.WayPath.Mode[0] == 1 ? 1 : 0;

        if (wait && params.Wait != 0)
        {
            flags |= 2;
        }

        if (faceObject && params.FaceObject != 0)
        {
            flags |= 4;
        }

        if (params.Mode == 1)
        {
            flags |= 8;
        }

        if (escapeTile && params.EscapeTile != 0)
        {
            flags |= 0x40;
        }

        return flags;
    }

    /// <summary>The attack flags of an attack order: 0x20 obliterate, 0x10 pursue.</summary>
    uint32_t AttackFlags(bool obliterate, int32_t pursue)
    {
        uint32_t flags = obliterate ? 0x20 : 0;

        if (pursue != 0)
        {
            flags |= 0x10;
        }

        return flags;
    }
}

auto MCTacticalOrder::Reset() -> void
{
    Time = NoTime;
    Id = 0;
    Origin = MCOrderOrigin::Self;
    UnitOrder = 0;
    Code = MCTacticalOrderCode::None;
    Target.reset();
    TargetObjectClass = 0;
    Stage = 0;
}

auto MCTacticalOrder::Reset(MCOrderOrigin origin, MCTacticalOrderCode code, int unitOrder) -> void
{
    Time = ScenarioTime;
    DelayedTime = NoTime;
    LastTime = NoTime;
    Id = 0;
    UnitOrder = unitOrder;
    Origin = origin;
    Code = code;
    MoveParams.WayPath.NumPoints = 0;
    MoveParams.WayPath.CurPoint = 0;
    MoveParams.WayPath.Points[0] = 0.0f;
    MoveParams.WayPath.Points[1] = 0.0f;
    MoveParams.WayPath.Points[2] = 0.0f;
    MoveParams.WayPath.Mode[0] = 0;
    MoveParams.FaceObject = 1;
    MoveParams.Wait = 0;
    MoveParams.Mode = 0;
    MoveParams.EscapeTile = 0;
    AttackParams.Type = 1;
    AttackParams.Method = 0;
    AttackParams.Range = -1;
    AttackParams.AimLocation = -1;
    AttackParams.Pursue = 1;
    AttackParams.Obliterate = 0;
    AttackParams.TargetPoint = MCVector3D(0.0f, 0.0f, 0.0f);
    Target.reset();
    TargetObjectClass = 0;
    SelectionIndex = -1;
    Stage = 1;
    PointLocalMoverId = 0xf;
    GroupFlags = 0;
}

auto MCTacticalOrder::InitWayPath(std::span<const MCWayPathPoint> path) -> void
{
    int32_t numPoints = 0;

    for (const MCWayPathPoint& point : path)
    {
        if (numPoints == MaxWayPoints)
        {
            Fatal(0, " Way Path Too Long ");
        }

        SetWayPoint(numPoints, point.Location);
        MoveParams.WayPath.Mode[numPoints] = point.Run ? 1 : 0;
        numPoints++;
    }

    MoveParams.WayPath.NumPoints = numPoints;
}

auto MCTacticalOrder::GetWayPoint(int32_t index) const -> MCVector3D
{
    const auto point = static_cast<size_t>(index) * 3;
    return MCVector3D(MoveParams.WayPath.Points[point], MoveParams.WayPath.Points[point + 1],
                      MoveParams.WayPath.Points[point + 2]);
}

auto MCTacticalOrder::SetWayPoint(int32_t index, MCVector3D wayPoint) -> void
{
    const auto point = static_cast<size_t>(index) * 3;
    MoveParams.WayPath.Points[point] = wayPoint.X;
    MoveParams.WayPath.Points[point + 1] = wayPoint.Y;
    MoveParams.WayPath.Points[point + 2] = wayPoint.Z;
}

auto MCTacticalOrder::GetRamTarget() const -> std::shared_ptr<MCGameObject>
{
    return Code == MCTacticalOrderCode::AttackObject && AttackParams.Method == 2 ? LockObject(Target) : nullptr;
}

auto MCTacticalOrder::GetJumpTarget() const -> std::shared_ptr<MCGameObject>
{
    return Code == MCTacticalOrderCode::JumpToPoint ? LockObject(Target) : nullptr;
}

auto MCTacticalOrder::IsCombatOrder() const -> bool
{
    return Code == MCTacticalOrderCode::AttackObject || Code == MCTacticalOrderCode::AttackPoint;
}

auto MCTacticalOrder::IsMoveOrder() const -> bool
{
    return Code == MCTacticalOrderCode::MoveToPoint || Code == MCTacticalOrderCode::MoveToObject;
}

auto MCTacticalOrder::IsWayPathOrder() const -> bool
{
    return Code == MCTacticalOrderCode::TraversePath || Code == MCTacticalOrderCode::PatrolPath;
}

auto MCTacticalOrder::IsJumpOrder() const -> bool
{
    return Code == MCTacticalOrderCode::JumpToPoint || Code == MCTacticalOrderCode::JumpToObject;
}

auto MCTacticalOrder::SetId(MCMechWarrior& pilot) -> void
{
    Assert(Id == 0, static_cast<uint32_t>(Id), " TacticalOrder.setId: id != 0 ");
    Id = pilot.NextTacOrderId;
    pilot.NextTacOrderId = Id == 0xff ? 1 : Id + 1;
}

auto MCTacticalOrder::GetParamData(float& timeStamp, std::span<int32_t, NumParams> paramList) const -> int32_t
{
    timeStamp = Time;
    const std::shared_ptr<MCGameObject> target = LockObject(Target);
    paramList[0] = static_cast<int32_t>(Code);
    paramList[1] = OriginValue(Origin);
    paramList[2] = UnitOrder != 0 ? 1 : 0;
    paramList[3] = target == nullptr ? 0 : target->PartId;
    paramList[4] = TargetObjectClass;
    paramList[5] = 0;
    paramList[6] = SelectionIndex;

    switch (Code)
    {
        case MCTacticalOrderCode::MoveToPoint:
        case MCTacticalOrderCode::Guard:
        {
            paramList[7] = MoveParams.WayPath.Mode[0] == 1 ? 1 : 0;
            paramList[8] = MoveParams.Wait;
            paramList[9] = static_cast<int32_t>(MoveParams.WayPath.Points[0]);
            paramList[10] = static_cast<int32_t>(MoveParams.WayPath.Points[1]);
            paramList[11] = static_cast<int32_t>(MoveParams.WayPath.Points[2]);
            break;
        }
        case MCTacticalOrderCode::MoveToObject:
        {
            paramList[7] = MoveParams.WayPath.Mode[0] == 1 ? 1 : 0;
            paramList[8] = MoveParams.Wait;
            paramList[9] = MoveParams.FaceObject;
            break;
        }
        case MCTacticalOrderCode::AttackObject:
        {
            paramList[7] = MoveParams.WayPath.Mode[0] == 1 ? 1 : 0;
            paramList[8] = AttackParams.Type;
            paramList[9] = AttackParams.Method;
            paramList[10] = AttackParams.Range;
            paramList[11] = AttackParams.AimLocation;
            paramList[12] = AttackParams.Pursue != 0 ? 1 : 0;
            paramList[13] = AttackParams.Obliterate != 0 ? 1 : 0;
            break;
        }
        default:
            break;
    }

    return static_cast<int32_t>(Code);
}

auto MCTacticalOrder::Pack() -> void
{
    // First word: code (5 bits, 0x1f for a Guard on a point), origin, unit order, group flags, point mover, attack
    // type and method, aim location.
    uint32_t header = static_cast<uint32_t>(AttackParams.Method) << 2 | static_cast<uint32_t>(AttackParams.Type);
    header = header << 4 | static_cast<uint32_t>(static_cast<int32_t>(PointLocalMoverId));
    header = header << 12 | GroupFlags;
    header = header << 1 | (UnitOrder != 0 ? 1u : 0u);
    header = header << 2 | static_cast<uint32_t>(Origin);
    header = header << 5;
    uint32_t word0 = header | static_cast<uint32_t>(AttackParams.AimLocation + 2) << 28;

    if (Code == MCTacticalOrderCode::Guard && LockObject(Target) == nullptr)
    {
        word0 |= GuardPointCode;
    }
    else
    {
        word0 |= static_cast<uint32_t>(Code);
    }

    Data[0] = word0;

    // The target (low 2 bits: 0 a mover's net roster index, 1 a terrain object part, 2 a train car part) or cell.
    auto encodeObject = [](const MCGameObject& object) -> uint32_t
    {
        const MCObjectClass objectClass = object.ObjectClass;

        if (IsMoverClass(objectClass))
        {
            return static_cast<uint32_t>(static_cast<const MCMover&>(object).NetRosterIndex) << 2;
        }

        if (objectClass == MCObjectClass::TrainCar)
        {
            const int32_t part = object.PartId - 0x7d000;
            const int32_t high = part / 100;
            return static_cast<uint32_t>(part - high * 100) << 2 | static_cast<uint32_t>(high) << 10 | 2;
        }

        const int32_t part = object.PartId - 0x1000;
        const int32_t block = part / 3200;
        const int32_t rest = part - block * 3200;
        return (static_cast<uint32_t>(block) << 9 | static_cast<uint32_t>(rest / 8)) << 5 |
               static_cast<uint32_t>(rest % 8) << 2 | 1;
    };

    auto encodeCell = [](MCVector3D position) -> uint32_t
    {
        int32_t cellR = 0;
        int32_t cellC = 0;
        WorldCoordToMapCell(position, cellR, cellC);
        return static_cast<uint32_t>(cellR) << 10 | static_cast<uint32_t>(cellC);
    };

    uint32_t targetBits = 0;

    switch (Code)
    {
        case MCTacticalOrderCode::MoveToPoint:
        case MCTacticalOrderCode::JumpToPoint:
            targetBits = encodeCell(GetWayPoint(0));
            break;
        case MCTacticalOrderCode::MoveToObject:
        case MCTacticalOrderCode::JumpToObject:
        case MCTacticalOrderCode::AttackObject:
        case MCTacticalOrderCode::Capture:
        case MCTacticalOrderCode::Refit:
        case MCTacticalOrderCode::GetFixed:
            targetBits = encodeObject(*LockObject(Target));
            break;
        case MCTacticalOrderCode::Guard:
        {
            const std::shared_ptr<MCGameObject> target = LockObject(Target);
            targetBits = target != nullptr ? encodeObject(*target) : encodeCell(GetWayPoint(0));
            break;
        }
        case MCTacticalOrderCode::AttackPoint:
            targetBits = encodeCell(AttackParams.TargetPoint);
            break;
        default:
            break;
    }

    // Second word: the target, then the move and attack flags and the fire range.
    uint32_t word1 = targetBits << 1 | (MoveParams.WayPath.Mode[0] == 1 ? 1u : 0u);
    word1 = word1 << 1 | static_cast<uint32_t>(MoveParams.Mode);
    word1 = word1 << 1 | (MoveParams.Wait != 0 ? 1u : 0u);
    word1 = word1 << 1 | (MoveParams.FaceObject != 0 ? 1u : 0u);
    word1 = word1 << 1 | (AttackParams.Obliterate != 0 ? 1u : 0u);
    word1 = word1 << 1 | (AttackParams.Pursue != 0 ? 1u : 0u);
    Data[1] = word1 << 3 | static_cast<uint32_t>(AttackParams.Range + 4);
}

auto MCTacticalOrder::Unpack() -> void
{
    const uint32_t word0 = Data[0];
    const uint32_t packedCode = word0 & 0x1f;
    const MCTacticalOrderCode code =
        packedCode == GuardPointCode ? MCTacticalOrderCode::Guard : static_cast<MCTacticalOrderCode>(packedCode);
    const int packedUnitOrder = static_cast<int>((static_cast<int32_t>(word0) >> 7) & 1);
    Reset(static_cast<MCOrderOrigin>((static_cast<int32_t>(word0) >> 5) & 3), code, packedUnitOrder);
    GroupFlags = (static_cast<int32_t>(word0) >> 8) & 0xfff;
    PointLocalMoverId = static_cast<int8_t>((static_cast<int32_t>(word0) >> 20) & 0xf);
    AttackParams.Method = (static_cast<int32_t>(word0) >> 26) & 3;
    const uint32_t word1 = Data[1];
    AttackParams.Type = (static_cast<int32_t>(word0) >> 24) & 3;
    AttackParams.AimLocation = ((static_cast<int32_t>(word0) >> 28) & 0xf) - 2;
    AttackParams.Range = static_cast<int32_t>(word1 & 7) - 4;
    AttackParams.Pursue = (static_cast<int32_t>(word1) >> 3) & 1;
    AttackParams.Obliterate = (static_cast<int32_t>(word1) >> 4) & 1;
    MoveParams.FaceObject = (static_cast<int32_t>(word1) >> 5) & 1;
    MoveParams.Wait = (static_cast<int32_t>(word1) >> 6) & 1;
    MoveParams.WayPath.Mode[0] = static_cast<uint8_t>((word1 >> 8) & 1);
    MoveParams.Mode = (static_cast<int32_t>(word1) >> 7) & 1;

    const uint32_t targetBits = static_cast<uint32_t>(static_cast<int32_t>(word1) >> 9);
    const uint32_t cellRow = static_cast<uint32_t>(static_cast<int32_t>(word1) >> 0x13) & 0x3ff;
    const uint32_t objectBits = static_cast<uint32_t>(static_cast<int32_t>(word1) >> 0xb);
    auto decodeObject = [&]()
    {
        switch (targetBits & 3)
        {
            case 0:
            {
                if (MultiPlayer() != nullptr)
                {
                    // Issue #25: an index past the roster (from another machine's order) is no target.
                    Target = MultiPlayer()->RosterMover(static_cast<int32_t>(objectBits & 0x1f));
                }

                return;
            }
            case 1:
            {
                Target = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(
                    static_cast<int32_t>(objectBits & 7) + 0x1000 +
                    (static_cast<int32_t>((static_cast<int32_t>(word1) >> 0x17) & 0xff) * 400 +
                     static_cast<int32_t>((static_cast<int32_t>(word1) >> 0xe) & 0x1ff)) *
                        8));
                return;
            }
            case 2:
            {
                Target = std::static_pointer_cast<MCGameObject>(
                    ObjectList()->FindObjectFromPart(static_cast<int32_t>(objectBits & 0xff) +
                                                     (static_cast<int32_t>((objectBits >> 8) & 0xff) + 0x1400) * 100));
                return;
            }
            default:
            {
                // Type 3 only comes from another machine's order; it has no target (the original was fatal).
                return;
            }
        }
    };

    auto decodeWayPoint = [&]()
    {
        SetWayPoint(0, MapCellToWorldPos(static_cast<int32_t>(cellRow), static_cast<int32_t>(targetBits & 0x3ff)));
        MoveParams.WayPath.NumPoints = 1;
    };

    switch (Code)
    {
        case MCTacticalOrderCode::MoveToPoint:
        case MCTacticalOrderCode::JumpToPoint:
            decodeWayPoint();
            break;
        case MCTacticalOrderCode::MoveToObject:
        case MCTacticalOrderCode::JumpToObject:
        case MCTacticalOrderCode::AttackObject:
        case MCTacticalOrderCode::Capture:
        case MCTacticalOrderCode::Refit:
        case MCTacticalOrderCode::GetFixed:
            decodeObject();
            break;
        case MCTacticalOrderCode::Guard:
        {
            if (packedCode == GuardPointCode)
            {
                decodeWayPoint();
            }
            else
            {
                decodeObject();
            }
            break;
        }
        case MCTacticalOrderCode::AttackPoint:
        {
            AttackParams.TargetPoint =
                MapCellToWorldPos(static_cast<int32_t>(cellRow), static_cast<int32_t>(targetBits & 0x3ff));
            AttackParams.TargetPoint.Z = MCTerrain::GetTerrainElevation(AttackParams.TargetPoint);
            break;
        }
        default:
            break;
    }

    if (LockObject(Target) != nullptr)
    {
        TargetObjectClass = static_cast<int32_t>(LockObject(Target)->ObjectClass);
    }
}

auto MCTacticalOrder::SetGroupFlag(int32_t localMoverId, bool set) -> void
{
    const uint32_t bit = 1u << (localMoverId & 0x1f);

    if (set)
    {
        GroupFlags |= bit;
    }
    else
    {
        GroupFlags &= ~bit;
    }
}

auto MCTacticalOrder::GetGroup(int32_t commanderId, std::span<std::shared_ptr<MCMover>> moverList,
                               std::shared_ptr<MCMover>& point) const -> int32_t
{
    // Issue #25: the commander and the point come from another machine's order; one past the rosters has no movers.
    if (MultiPlayer() == nullptr || commanderId < 0 || commanderId >= MaxLinkupPlayers)
    {
        point = nullptr;
        return 0;
    }

    const auto& roster = MultiPlayer()->PlayerMoverRoster[static_cast<size_t>(commanderId)];
    int32_t numMovers = 0;

    for (int32_t i = 0; i < MCMultiPlayer::MaxLocalMovers && numMovers < std::ssize(moverList); i++)
    {
        if ((GroupFlags >> i & 1) != 0)
        {
            moverList[static_cast<size_t>(numMovers)] = LockObject(roster[static_cast<size_t>(i)]);
            numMovers++;
        }
    }

    point = PointLocalMoverId >= 0 && PointLocalMoverId < MCMultiPlayer::MaxLocalMovers
                ? LockObject(roster[static_cast<size_t>(PointLocalMoverId)])
                : nullptr;
    return numMovers;
}

auto MCTacticalOrder::DelayOver(bool laterThanNone) -> bool
{
    const bool delayed = laterThanNone ? NoTime < DelayedTime : DelayedTime != NoTime;

    if (delayed)
    {
        if (ScenarioTime < DelayedTime)
        {
            return false;
        }

        DelayedTime = NoTime;
    }

    return true;
}

auto MCTacticalOrder::Execute(MCMechWarrior& pilot, int32_t& message) -> int32_t
{
    int32_t result = 0;
    const auto vehicle = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle));
    message = -1;

    // A new order ends any refit the vehicle was part of.
    if (LockObject(vehicle->RefitBuddy) != nullptr)
    {
        if (vehicle->ObjectClass == MCObjectClass::GroundVehicle)
        {
            static_cast<MCGroundVehicle&>(*vehicle).Refitting = false;
        }

        std::shared_ptr<MCGameObject> buddy = LockObject(vehicle->RefitBuddy);

        if (buddy->ObjectClass == MCObjectClass::GroundVehicle)
        {
            static_cast<MCGroundVehicle&>(*buddy).Refitting = false;
        }

        if (IsMoverClass(buddy->ObjectClass))
        {
            static_cast<MCMover&>(*buddy).RefitBuddy.reset();
        }
        else if (buddy->ObjectClass == MCObjectClass::TreeBuilding)
        {
            static_cast<MCTreeBuilding&>(*buddy).RefitBuddy.reset();
        }

        vehicle->RefitBuddy.reset();
    }

    const MCOrderOrigin origin = Origin;

    switch (Code)
    {
        case MCTacticalOrderCode::MoveToPoint:
        {
            if (!DelayOver(false))
            {
                return 0;
            }

            result = pilot.OrderMoveToPoint(UnitOrder, 1, origin, FirstWayPoint(), SelectionIndex,
                                            MoveFlags(MoveParams, false, true, true));
            message = -1;
            break;
        }

        case MCTacticalOrderCode::MoveToObject:
        {
            pilot.OrderMoveToObject(UnitOrder, 1, origin, LockObject(Target), SelectionIndex,
                                    MoveFlags(MoveParams, true, false, false));
            message = -1;
            break;
        }

        case MCTacticalOrderCode::JumpToPoint:
        {
            if (!DelayOver(false))
            {
                return 0;
            }

            result = pilot.OrderJumpToPoint(UnitOrder, 1, origin, FirstWayPoint(), -1);

            if (result == 0)
            {
                message = 2;
            }
            break;
        }
        case MCTacticalOrderCode::JumpToObject:
        {
            if (!DelayOver(false))
            {
                return 0;
            }

            if (LockObject(Target) == nullptr)
            {
                return 1;
            }

            result = pilot.OrderJumpToObject(UnitOrder, 1, origin, LockObject(Target), -1);

            if (result == 0)
            {
                message = 2;
            }
            break;
        }
        case MCTacticalOrderCode::TraversePath:
        {
            pilot.OrderTraversePath(UnitOrder, 1, origin, MoveParams.WayPath, MoveParams.Mode == 1 ? 8 : 0);
            message = 0;
            break;
        }
        case MCTacticalOrderCode::PatrolPath:
        {
            pilot.OrderPatrolPath(UnitOrder, 1, origin, MoveParams.WayPath);
            message = 0;
            break;
        }
        case MCTacticalOrderCode::Guard:
        {
            message = 0;
            AttackParams.Type = 1;
            AttackParams.Method = 0;
            AttackParams.Pursue = 1;
            AttackParams.Range = -1;
            break;
        }
        case MCTacticalOrderCode::Stop:
        {
            message = 3;
            pilot.OrderStop(UnitOrder, 1);
            break;
        }
        case MCTacticalOrderCode::PowerUp:
        {
            message = 0x10;

            if (!DelayOver(true))
            {
                return 0;
            }

            pilot.OrderPowerUp(UnitOrder, origin);
            break;
        }
        case MCTacticalOrderCode::PowerDown:
        {
            message = 0x10;

            if (!DelayOver(true))
            {
                return 0;
            }

            pilot.OrderPowerDown(UnitOrder, origin);
            break;
        }
        case MCTacticalOrderCode::Eject:
            pilot.OrderEject(UnitOrder, 1, origin);
            break;
        case MCTacticalOrderCode::AttackObject:
        {
            result = -1;
            message = 0x1b;

            if (LockObject(Target) == nullptr)
            {
                pilot.SetLastTarget(nullptr, 0, 0);
                std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle))->CalcOptimalRange(nullptr);
                result = pilot.OrderAttackPoint(UnitOrder, origin, AttackParams.TargetPoint, AttackParams.Type,
                                                AttackParams.Method, AttackParams.Range,
                                                AttackFlags(AttackParams.Obliterate != 0, AttackParams.Pursue));

                if (result != 0)
                {
                    break;
                }

                if (AttackParams.Range != -1 && AttackParams.Range != -4)
                {
                    message = 5;
                    break;
                }
            }
            else
            {
                std::shared_ptr<MCGameObject> attackTarget = LockObject(Target);

                if (attackTarget->InTransport() != 0)
                {
                    attackTarget = LockObject(static_cast<MCElemental&>(*attackTarget).Transport);
                    Target = attackTarget;
                }

                std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle))->CalcOptimalRange(attackTarget);
                TargetObjectClass = static_cast<int32_t>(attackTarget->ObjectClass);
                const int disabled = attackTarget->IsDisabled();
                AttackParams.Obliterate = disabled;

                if (attackTarget->IsDestroyed() != 0)
                {
                    break;
                }

                result = pilot.OrderAttackObject(UnitOrder, origin, attackTarget, AttackParams.Type,
                                                 AttackParams.Method, AttackParams.Range, AttackParams.AimLocation,
                                                 AttackFlags(disabled != 0, AttackParams.Pursue));

                if (result != 0)
                {
                    break;
                }

                message = 4;

                if (AttackParams.Method == 2)
                {
                    message = 7;
                    break;
                }

                if (AttackParams.Method == 1)
                {
                    message = 8;
                    break;
                }

                if (AttackParams.Range != -1 && AttackParams.Range != -4)
                {
                    message = 5;
                    break;
                }

                if (AttackParams.AimLocation != -1)
                {
                    message = 9;
                    break;
                }
            }

            if (AttackParams.Pursue == 0)
            {
                message = 6;
            }
            break;
        }

        case MCTacticalOrderCode::AttackPoint:
        {
            message = 0x1b;
            pilot.SetLastTarget(nullptr, 0, 0);
            std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle))->CalcOptimalRange(nullptr);
            result = pilot.OrderAttackPoint(UnitOrder, origin, AttackParams.TargetPoint, AttackParams.Type,
                                            AttackParams.Method, AttackParams.Range,
                                            AttackFlags(AttackParams.Obliterate != 0, AttackParams.Pursue));

            if (result == 0)
            {
                if (AttackParams.Range == -1 || AttackParams.Range == -4)
                {
                    message = AttackParams.Pursue != 0 ? -1 : 6;
                }
                else
                {
                    message = 5;
                }
            }
            break;
        }

        case MCTacticalOrderCode::HoldFire:
        {
            message = 3;
            pilot.OrderWait(UnitOrder, origin, 0, 1);
            break;
        }
        case MCTacticalOrderCode::Withdraw:
            pilot.OrderWithdraw(UnitOrder, origin, FirstWayPoint());
            break;
        case MCTacticalOrderCode::Capture:
        {
            result = 1;
            bool refuse = LockObject(Target) == nullptr || LockObject(Target)->IsCaptureable() == 0 ||
                          LockObject(Target)->GetAlignment() == pilot.Alignment ||
                          LockObject(Target)->GetCaptureBlocker(pilot.Alignment) != nullptr;

            if (!refuse && LockObject(Target)->IsBuilding() != 0)
            {
                // Only a vehicle with seats can empty a prison.
                const bool isPrison = (LockObject(Target)->ObjectClass == MCObjectClass::Building ||
                                       LockObject(Target)->ObjectClass == MCObjectClass::TreeBuilding) &&
                                      LockObject(Target)->IsPrison() != 0;

                if (isPrison && (vehicle->ObjectClass != MCObjectClass::GroundVehicle ||
                                 static_cast<MCGroundVehicle&>(*vehicle).Seats == 0))
                {
                    refuse = true;
                }
            }

            // Run, and face the object (not move flag 8).
            const uint32_t captureFlags =
                (MoveParams.WayPath.Mode[0] == 1 ? 1u : 0u) | (MoveParams.FaceObject != 0 ? 4u : 0u);

            if (refuse)
            {
                message = 0xb;
            }
            else
            {
                result = pilot.OrderMoveToObject(0, 0, origin, LockObject(Target), 0, captureFlags);

                if (result == 0)
                {
                    message = 10;
                }

                if (result != 1)
                {
                    break;
                }
            }

            Stage = StageDone;
            break;
        }

        case MCTacticalOrderCode::Refit:
        {
            // Port fix: the original calls target->getPilot() (and drops the result) before the null check.
            if (LockObject(Target) != nullptr)
            {
                LockObject(Target)->GetPilot();
            }

            result = 1;

            if (LockObject(Target) == nullptr || LockObject(Target)->ObjectClass != MCObjectClass::BattleMech ||
                std::static_pointer_cast<MCMover>(LockObject(Target))->NeedsRefit(0) == 0)
            {
                break;
            }

            if (vehicle->ObjectClass == MCObjectClass::GroundVehicle && vehicle->GetRefitPoints() > 0.0f &&
                LockObject(vehicle->RefitBuddy) == nullptr)
            {
                result = pilot.OrderMoveToObject(UnitOrder, 0, origin, LockObject(Target), SelectionIndex,
                                                 MoveParams.WayPath.Mode[0] == 1 ? 5 : 4);
                Time = 0.0f;

                if (result == 0)
                {
                    std::static_pointer_cast<MCMover>(LockObject(Target))->RefitBuddy = vehicle;
                    vehicle->RefitBuddy = LockObject(Target);
                    message = 0xe;
                    break;
                }
            }

            Stage = StageDone;
            break;
        }

        case MCTacticalOrderCode::GetFixed:
        {
            result = 1;

            if (LockObject(Target) == nullptr || LockObject(Target)->ObjectClass != MCObjectClass::TreeBuilding)
            {
                break;
            }

            const auto bay = std::static_pointer_cast<MCTreeBuilding>(LockObject(Target));

            if (bay->GetRefitPoints() > 0.0f && LockObject(bay->RefitBuddy) == nullptr &&
                ((vehicle->ObjectClass == MCObjectClass::BattleMech && bay->MechBay == 1) ||
                 (vehicle->ObjectClass == MCObjectClass::GroundVehicle && bay->MechBay == 0)))
            {
                int32_t tileR = 0;
                int32_t tileC = 0;
                WorldCoordToMapTile(bay->GetPosition(), tileR, tileC);
                result = pilot.OrderMoveToPoint(UnitOrder, 0, origin, MapTileCellToWorldPos(tileR, tileC, 2, 1),
                                                SelectionIndex, MoveParams.WayPath.Mode[0] == 1 ? 5 : 4);
                Time = 0.0f;

                if (result == 0)
                {
                    message = 0xe;
                    bay->RefitBuddy = vehicle;
                    vehicle->RefitBuddy = LockObject(Target);
                    break;
                }
            }

            Stage = StageDone;
            break;
        }

        case MCTacticalOrderCode::LoadIntoCarrier:
        {
            result = 1;

            if (vehicle->ObjectClass == MCObjectClass::Elemental && LockObject(Target) != nullptr &&
                LockObject(Target)->ObjectClass == MCObjectClass::GroundVehicle &&
                std::static_pointer_cast<MCGroundVehicle>(LockObject(Target))->ElementalCarrier != 0)
            {
                result = LockObject(Target)->GetPilot()->OrderMoveToObject(0, 0, origin, vehicle, -1, 4);

                if (result == 0)
                {
                    message = 0x27;
                    break;
                }
            }

            Stage = StageDone;
            break;
        }
        case MCTacticalOrderCode::DeployElementals:
        {
            result = 1;

            if (vehicle->ObjectClass == MCObjectClass::GroundVehicle &&
                LockObject(static_cast<MCGroundVehicle&>(*vehicle).Elementals[0]) != nullptr)
            {
                result = 0;
                LockObject(static_cast<MCGroundVehicle&>(*vehicle).Elementals[0])
                    ->PlayMessage(static_cast<MCRadioMessageType>(0x26), 0);
                Time = ScenarioTime + 5.0f;
                break;
            }

            Stage = StageDone;
            break;
        }
        default:
            break;
    }

    if (Origin == MCOrderOrigin::Player)
    {
        pilot.TriggerAlarm(MCPilotAlarmType::PlayerOrder, static_cast<uint32_t>(Code));
    }
    else
    {
        message = -1;
    }

    if (Code != MCTacticalOrderCode::Withdraw && static_cast<char>(LockObject(pilot.Vehicle)->Status) != 2)
    {
        std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle))->Withdrawing = false;
    }

    return result;
}

auto MCTacticalOrder::Status(MCMechWarrior& pilot) -> bool
{
    MCTacticalOrder queued;
    queued.Reset();
    const bool nextIsMove = pilot.PeekQueuedTacOrder(queued) == 0 && queued.Code == MCTacticalOrderCode::MoveToPoint;
    const auto vehicle = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle));

    // The delayed orders start once their time comes (the power orders test "later than -1").
    auto delayedStart = [&]() -> bool
    {
        if (NoTime < DelayedTime)
        {
            if (ScenarioTime < DelayedTime)
            {
                return false;
            }

            DelayedTime = NoTime;
            int32_t message = 0;
            Execute(pilot, message);
        }

        return true;
    };

    switch (Code)
    {
        case MCTacticalOrderCode::Wait:
            return !(ScenarioTime <= DelayedTime);
        case MCTacticalOrderCode::MoveToPoint:
        {
            if (!delayedStart())
            {
                return false;
            }

            MCVector3D point = FirstWayPoint();
            const double distance = vehicle->DistanceFrom(point);

            if (distance < (nextIsMove ? 8.0f : MoveMarginOfError[1]))
            {
                if (MoveParams.Wait != 0)
                {
                    Code = MCTacticalOrderCode::Wait;
                    return false;
                }

                return true;
            }

            return false;
        }

        case MCTacticalOrderCode::PatrolPath:
        case MCTacticalOrderCode::Guard:
        case MCTacticalOrderCode::WayPointsDone:
        case MCTacticalOrderCode::AttackPoint:
        case MCTacticalOrderCode::HoldFire:
        case MCTacticalOrderCode::Withdraw:
            return false;
        case MCTacticalOrderCode::MoveToObject:
        {
            if (LockObject(Target) == nullptr)
            {
                return true;
            }

            MCVector3D targetPos = LockObject(Target)->GetPosition();

            if (vehicle->DistanceFrom(targetPos) > MoveMarginOfError[1])
            {
                return false;
            }

            if (MoveParams.FaceObject != 0)
            {
                // Done only once the target is inside the fire arc.
                const float facing = pilot.GetVehicle()->RelViewFacingTo(LockObject(Target)->GetPosition());
                const float fireArc = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle))->GetFireArc();
                return !(facing < -fireArc) && !(fireArc < facing);
            }

            return true;
        }

        case MCTacticalOrderCode::JumpToPoint:
            return Stage == 3;
        case MCTacticalOrderCode::JumpToObject:
            return !(static_cast<MCBattleMech&>(*vehicle).InJump == 0 ||
                     static_cast<MCMechActor*>(vehicle->Appearance.get())->InJump != 0);
        case MCTacticalOrderCode::TraversePath:
            return Stage == 2;
        case MCTacticalOrderCode::Stop:
            return true;
        case MCTacticalOrderCode::PowerUp:
        {
            if (!delayedStart())
            {
                return false;
            }

            return pilot.GetVehicleStatus() == 0;
        }
        case MCTacticalOrderCode::PowerDown:
        {
            delayedStart();
            return false;
        }
        case MCTacticalOrderCode::AttackObject:
        {
            std::shared_ptr<MCGameObject> attackTarget = LockObject(Target);

            if (attackTarget == nullptr)
            {
                return true;
            }

            if (attackTarget->ObjectClass == MCObjectClass::Elemental &&
                LockObject(static_cast<MCElemental&>(*attackTarget).Transport) != nullptr)
            {
                attackTarget = LockObject(static_cast<MCElemental&>(*attackTarget).Transport);
                Target = attackTarget;
            }

            if (attackTarget->IsDestroyed() != 0)
            {
                return true;
            }

            return attackTarget->IsDisabled() != 0 && AttackParams.Obliterate == 0;
        }

        case MCTacticalOrderCode::Scramble:
        {
            MCVector3D point = FirstWayPoint();
            vehicle->DistanceFrom(point);
            return false;
        }

        case MCTacticalOrderCode::Capture:
            return CaptureStatus(pilot);
        case MCTacticalOrderCode::Refit:
            return RefitStatus(pilot);
        case MCTacticalOrderCode::GetFixed:
            return GetFixedStatus(pilot);
        case MCTacticalOrderCode::LoadIntoCarrier:
            return LoadIntoCarrierStatus(pilot);
        case MCTacticalOrderCode::DeployElementals:
            return DeployElementalsStatus(pilot);
        default:
            return true;
    }
}

auto MCTacticalOrder::CaptureStatus(MCMechWarrior& pilot) const -> bool
{
    if (Stage == StageDone)
    {
        return true;
    }

    const auto vehicle = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle));
    std::shared_ptr<MCGameObject> prize = LockObject(Target);
    MCVector3D prizePos = prize->GetPosition();
    const auto distance = static_cast<float>(vehicle->DistanceFrom(prizePos));
    const int32_t alignment = pilot.Alignment;

    if (prize->GetCaptureBlocker(alignment) != nullptr)
    {
        pilot.RadioMessage(MCRadioMessageType::CannotCapture, 1);
        return true;
    }

    if (distance >= 30.0f || prize->IsCaptureable() == 0)
    {
        return false;
    }

    const MCScenarioMap& map = GameMap();

    switch (prize->ObjectClass)
    {
        case MCObjectClass::BattleMech:
        {
            // A seated pilot takes over the mech.
            std::shared_ptr<const MCObjectPosition> position = prize->GetObjPosition();

            if (OverlayIsBridge[map.TileAt(position->TileR, position->TileC).OverlayType()] ||
                vehicle->ObjectClass != MCObjectClass::GroundVehicle)
            {
                return false;
            }

            auto& carrier = static_cast<MCGroundVehicle&>(*vehicle);

            for (int32_t seat = 0; seat < carrier.Seats; seat++)
            {
                if (carrier.Passengers[seat].lock() == nullptr)
                {
                    continue;
                }

                static_cast<MCMover&>(*prize).Pilot = carrier.Passengers[seat];
                carrier.Passengers[seat].reset();
                prize->SetAwake(1);
                TacticalInterface()->ActivateMech(prize->PartId);
                return true;
            }

            return false;
        }

        case MCObjectClass::GroundVehicle:
        {
            std::shared_ptr<const MCObjectPosition> position = prize->GetObjPosition();

            if (OverlayIsBridge[map.TileAt(position->TileR, position->TileC).OverlayType()])
            {
                return false;
            }

            prize->SetCaptured();

            if (!prize->GetSalvage().empty())
            {
                TacticalMap()->AddSalvage(prize);
            }

            pilot.RadioMessage(MCRadioMessageType::CapturedVehicle, 1);
            return true;
        }

        case MCObjectClass::Building:
        case MCObjectClass::TreeBuilding:
        {
            prize->SetCaptured();
            prize->SetAlignment(alignment);
            prize->SetCommanderId(vehicle->GetCommanderId());

            if (!prize->GetSalvage().empty())
            {
                TacticalMap()->AddSalvage(prize);
            }

            if (prize->IsPrison() != 0 && vehicle->ObjectClass == MCObjectClass::GroundVehicle)
            {
                // Original behaviour (OB-031): every seat pass moves all the prisoners into that seat, so only the
                // last prisoner is kept, in the first seat.
                auto& carrier = static_cast<MCGroundVehicle&>(*vehicle);
                std::span<std::weak_ptr<MCMechWarrior>> prisoners =
                    prize->ObjectClass == MCObjectClass::Building
                        ? std::span<std::weak_ptr<MCMechWarrior>>(static_cast<MCBuilding&>(*prize).PrisonSlots)
                        : std::span<std::weak_ptr<MCMechWarrior>>(static_cast<MCTreeBuilding&>(*prize).PrisonSlots);

                for (int32_t seat = 0; seat < carrier.Seats; seat++)
                {
                    for (std::weak_ptr<MCMechWarrior>& prisoner : prisoners)
                    {
                        if (const std::shared_ptr<MCMechWarrior> held = prisoner.lock())
                        {
                            carrier.Passengers[seat] = held;
                            prisoner.reset();
                        }
                    }
                }
            }

            pilot.RadioMessage(MCRadioMessageType::CapturedBuilding, 1);
            return true;
        }

        default:
            return false;
    }
}

auto MCTacticalOrder::RefitStatus(MCMechWarrior& pilot) -> bool
{
    const auto refitee = std::static_pointer_cast<MCMover>(LockObject(Target));
    const auto refitter = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle));

    if (LockObject(refitter->RefitBuddy) == nullptr || LockObject(refitee->RefitBuddy) == nullptr)
    {
        return true;
    }

    Assert(LockObject(refitter->RefitBuddy) == refitee && LockObject(refitee->RefitBuddy) == refitter, 0,
           "Refitee and refitter aren't pointing at each other.");

    switch (Stage)
    {
        case 1:
        {
            // Drive up to the mech.
            MCVector3D refiteePos = refitee->GetPosition();

            if (refitter->DistanceFrom(refiteePos) < RefitRange)
            {
                Stage++;
            }

            return false;
        }

        case 2:
        {
            // Power the mech down, and give it three seconds.
            if (Time == 0.0f)
            {
                refitee->GetPilot()->OrderPowerDown(UnitOrder, MCOrderOrigin::Self);
                Time = ScenarioTime;
            }
            else if (static_cast<double>(Time) + 3.0 < ScenarioTime)
            {
                Stage++;
                Time = ScenarioTime;
            }

            return false;
        }
        case 3:
        {
            // A round of repairs every RefitTime seconds, until the mech needs nothing more.
            auto& truck = static_cast<MCGroundVehicle&>(*refitter);
            truck.Refitting = true;

            if (static_cast<double>(RefitTime) + Time < ScenarioTime)
            {
                float pointsUsed = 0.0f;
                const int32_t finished = DoRefit(*refitee, refitter->GetRefitPoints(), pointsUsed, truck.AmmoTruck);
                refitter->BurnRefitPoints(pointsUsed);

                if (MultiPlayer() != nullptr)
                {
                    const int32_t shotType = -5 - (truck.AmmoTruck != 0 ? 1 : 0);
                    MCWeaponShotInfo shot;
                    shot.Init(nullptr, shotType, pointsUsed, 4, 0.0f);
                    MultiPlayer()->AddWeaponHitChunk(*refitter, shot, 0);
                    shot.Init(nullptr, shotType, pointsUsed, 0, 0.0f);
                    MultiPlayer()->AddWeaponHitChunk(*refitee, shot, 1);
                }

                Stage = static_cast<uint8_t>(Stage + finished);

                if (finished == 0)
                {
                    Time = ScenarioTime;
                }
            }

            return false;
        }

        case 4:
        {
            static_cast<MCGroundVehicle&>(*refitter).Refitting = false;
            refitee->GetPilot()->OrderPowerUp(UnitOrder, MCOrderOrigin::Self);
            refitee->RefitBuddy.reset();
            refitter->RefitBuddy.reset();
            return true;
        }
        case StageDone:
            return true;
        default:
            return false;
    }
}

auto MCTacticalOrder::GetFixedStatus(MCMechWarrior& pilot) -> bool
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(pilot.Vehicle));
    const auto bay = std::static_pointer_cast<MCTreeBuilding>(LockObject(Target));

    if (LockObject(mover->RefitBuddy) == nullptr || LockObject(bay->RefitBuddy) == nullptr)
    {
        return true;
    }

    Assert(LockObject(mover->RefitBuddy) == bay && LockObject(bay->RefitBuddy) == mover, 0,
           "Refitee and refitter aren't pointing at each other.");
    const float now = ScenarioTime;

    switch (Stage)
    {
        case 1:
        case 4:
        {
            // Drive into the bay (1), or out of it (4).
            if (pilot.GetMovePath().NumStepsWhenNotPaused == 0 && pilot.MovePathRequest.lock() == nullptr)
            {
                Stage++;
            }

            return false;
        }
        case 2:
        {
            if (Time == 0.0f)
            {
                pilot.OrderPowerDown(UnitOrder, MCOrderOrigin::Self);
                Time = ScenarioTime;
            }
            else if (static_cast<double>(Time) + 3.0 < ScenarioTime)
            {
                Stage++;
                Time = now;
            }

            return false;
        }
        case 3:
        {
            if (static_cast<double>(RefitTime) + Time < ScenarioTime)
            {
                float pointsUsed = 0.0f;
                const int32_t finished = DoRefit(*mover, bay->GetRefitPoints(), pointsUsed, 0);
                bay->BurnRefitPoints(pointsUsed);

                if (MultiPlayer() != nullptr)
                {
                    MCWeaponShotInfo shot;
                    shot.Init(nullptr, -5, pointsUsed, -1, 0.0f);
                    MultiPlayer()->AddWeaponHitChunk(*bay, shot, 0);
                    shot.Init(nullptr, -5, pointsUsed, 0, 0.0f);
                    MultiPlayer()->AddWeaponHitChunk(*mover, shot, 1);
                }

                Stage = static_cast<uint8_t>(Stage + finished);

                if (finished == 0)
                {
                    Time = ScenarioTime;
                }
                else
                {
                    // Repaired: power up and roll out of the bay.
                    // OB-197 (fixed with RepairWalkOut): the original powered up with an alarm order of the pilot's
                    // own, which took over from this order that frame: the path out was dropped and the mech stayed
                    // in the bay until ordered out. With RepairWalkOut the mech starts up here and this order stays,
                    // walking out (stage 4) and letting the bay go (stage 5).
                    if (MCRepairWalkOutInForce())
                    {
                        constexpr int8_t ShutDown = 5;

                        if (static_cast<int8_t>(mover->Status) == ShutDown && mover->CanPowerUp() != 0)
                        {
                            mover->StartUp();
                        }
                    }
                    else
                    {
                        pilot.OrderPowerUp(UnitOrder, MCOrderOrigin::Self);
                    }

                    int32_t tileR = 0;
                    int32_t tileC = 0;
                    WorldCoordToMapTile(LockObject(Target)->GetPosition(), tileR, tileC);
                    pilot.OrderMoveToPoint(UnitOrder, 0, MCOrderOrigin::Self,
                                           MapTileCellToWorldPos(tileR + 1, tileC, 1, 1), SelectionIndex, 0);
                }
            }

            return false;
        }
        case 5:
        {
            bay->RefitBuddy.reset();
            mover->RefitBuddy.reset();
            return true;
        }
        case StageDone:
            return true;
        default:
            return false;
    }
}

auto MCTacticalOrder::LoadIntoCarrierStatus(MCMechWarrior& pilot) -> bool
{
    std::shared_ptr<MCMoverGroup> group = pilot.GetGroup();
    std::shared_ptr<MCGameObject> carrier = LockObject(Target);

    if (group == nullptr)
    {
        return false;
    }

    switch (Stage)
    {
        case 1:
        {
            // Once within 200 m of the carrier, everyone stops.
            const MCVector3D carrierPos = carrier->GetPosition();
            const MCVector3D pos = LockObject(pilot.Vehicle)->GetPosition();
            const double dx = static_cast<double>(pos.X) - carrierPos.X;
            const double dy = static_cast<double>(pos.Y) - carrierPos.Y;
            const float dz = pos.Z - carrierPos.Z;

            if (std::sqrt((dy * dy + static_cast<double>(dz) * dz) + dx * dx) < 200.0)
            {
                pilot.ClearMoveOrders();

                for (int32_t i = 0; i < group->NumMovers(); i++)
                {
                    LockObject(group->Movers[i])->GetPilot()->ClearMoveOrders();
                }

                carrier->GetPilot()->ClearMoveOrders();
                Time = ScenarioTime;
                Stage++;
            }

            return false;
        }

        case 2:
        {
            // Three seconds later the group walks to the carrier.
            if (static_cast<double>(Time) + 3.0 < ScenarioTime)
            {
                for (int32_t i = 0; i < group->NumMovers(); i++)
                {
                    LockObject(group->Movers[i])
                        ->GetPilot()
                        ->OrderMoveToObject(0, 0, MCOrderOrigin::Self, carrier, -1, 4);
                }

                Stage++;
            }

            return false;
        }
        case 3:
        {
            // Five seconds after that, everyone is aboard.
            if (ScenarioTime <= static_cast<double>(Time) + 5.0)
            {
                return false;
            }

            auto& transport = static_cast<MCGroundVehicle&>(*carrier);

            for (int32_t i = 0; i < group->NumMovers(); i++)
            {
                const std::shared_ptr<MCMover> passenger = LockObject(group->Movers[i]);
                static_cast<MCElemental&>(*passenger).Transport = carrier;

                for (int32_t slot = 0; slot < 10; slot++)
                {
                    if (LockObject(transport.Elementals[slot]) == nullptr)
                    {
                        transport.Elementals[slot] = passenger;
                        break;
                    }
                }
            }

            return true;
        }

        case StageDone:
            return true;
        default:
            return false;
    }
}

auto MCTacticalOrder::DeployElementalsStatus(MCMechWarrior& pilot) -> bool
{
    if (Stage == StageDone)
    {
        return true;
    }

    if (ScenarioTime <= static_cast<double>(Time) + 5.0)
    {
        return false;
    }

    // Up to five at a time, set down in a ring around the carrier.
    int16_t toDeploy = 5;
    const auto transport = std::static_pointer_cast<MCGroundVehicle>(LockObject(pilot.Vehicle));
    int32_t slot = 0;

    for (; slot < 10; slot++)
    {
        const std::shared_ptr<MCMover> elemental = LockObject(transport->Elementals[slot]);

        if (elemental == nullptr)
        {
            continue;
        }

        toDeploy--;
        const double angle = static_cast<double>(toDeploy * 72) * DegreesToRadians;
        const float sinPart = static_cast<float>(std::sin(angle)) * 50.0f;
        const float offsetX = static_cast<float>(std::cos(angle) * 50.0 + sinPart);
        const float offsetY = static_cast<float>(std::cos(angle) * 50.0 - sinPart);
        const MCVector3D pos = transport->GetPosition();
        MCVector3D dropPos(pos.X + offsetX, pos.Y + offsetY, pos.Z);
        elemental->SetPosition(dropPos);
        static_cast<MCElemental&>(*elemental).Transport.reset();
        transport->Elementals[slot].reset();

        if (toDeploy == 0)
        {
            Time = ScenarioTime;
            break;
        }
    }

    return slot == 10;
}
