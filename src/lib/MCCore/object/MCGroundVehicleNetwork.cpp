#include "stdafx.h"
#include "object/MCGroundVehicle.h"
#include "ai/MCTacticalOrder.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCAIControl.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCCollisionSystem.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCMoverGroup.h"
#include "object/MCGroundVehicleControlData.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCNetControl.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCEffectSystem.h"
#include "object/MCMechWarrior.h"
#include "sound/MCSoundSystem.h"
#include "object/MCObjectType.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"

auto MCGroundVehicle::NetUpdateMovement() -> void
{
    auto& controlData = static_cast<MCGroundVehicleControlData&>(*Control->ControlData);
    MCMovePath& path = Pilot.lock()->GetMovePath();
    std::optional<MCVector3D> destination = path.CurrentDestination();

    if (path.CurStep == path.NumSteps - 1 && destination &&
        static_cast<float>(DistanceFrom(*destination)) < MoveMarginOfError[1])
    {
        // At the end of the path: stop once the server says the vehicle has.
        GetBodyState();

        if (StatusChunk.BodyState == 0)
        {
            Pilot.lock()->ClearMoveOrders();
            controlData.Throttle = 0;
        }
    }

    if (DisableThisFrame != 0)
    {
        DisableThisFrame = false;
        ShutDownThisFrame = false;
        StartUpThisFrame = false;
        Status = 1;
        controlData.Throttle = 0;
        return;
    }

    if (ShutDownThisFrame != 0)
    {
        controlData.Throttle = 0;
        ShutDownThisFrame = false;
        StartUpThisFrame = false;
        Status = 5;
        return;
    }

    if (StartUpThisFrame != 0)
    {
        controlData.Throttle = 100;
        StartUpThisFrame = false;
        Status = 0;
        return;
    }

    if (IsCaptured() != 0 || IsDisabled() != 0)
    {
        controlData.Throttle = 0;
        return;
    }

    if (EngineBlowTime > -1.0f)
    {
        return;
    }

    float newRotatePerSec = static_cast<float>(PivotTo());

    if (newRotatePerSec != 0.0f)
    {
        return;
    }

    int32_t minThrottle = -100;
    int32_t maxThrottle = 100;
    char newRotate = 0;
    char newThrottleSetting = 0;
    MCMoveState newMoveState = MCMoveState::NoChange;
    CalcThrottleLimits(minThrottle, maxThrottle);
    UpdateMoveStateGoal();
    NetUpdateMovePath(newRotate, newThrottleSetting, newRotatePerSec, newMoveState, minThrottle, maxThrottle);

    if (newMoveState != MCMoveState::NoChange)
    {
        Pilot.lock()->MoveOrders.MoveState = newMoveState;
    }

    SetControlSettings(newRotate, newThrottleSetting, newRotatePerSec, minThrottle, maxThrottle);
    UpdateTurret(newRotatePerSec);
}

auto MCGroundVehicle::BuildStatusChunk() -> int32_t
{
    StatusChunk.TargetCellRC[0] = -1;
    StatusChunk.TargetCellRC[1] = -1;
    StatusChunk.BodyState = 0;
    StatusChunk.TargetType = 0;
    StatusChunk.TargetId = 0;
    StatusChunk.TargetBlockOrTrainNumber = 0;
    StatusChunk.TargetVertexOrCarNumber = 0;
    StatusChunk.TargetItemNumber = 0;
    StatusChunk.EjectOrderGiven = 0;
    StatusChunk.JumpOrder = 0;
    StatusChunk.Data = 0;

    const int32_t bodyState = GetBodyState();
    StatusChunk.BodyState = bodyState < 0 || bodyState > 3 ? 0 : static_cast<uint32_t>(bodyState);

    if (Pilot.lock() != nullptr)
    {
        std::shared_ptr<MCGameObject> target = Pilot.lock()->GetLastTarget();

        if (target != nullptr)
        {
            const MCObjectClass targetClass = target->ObjectClass;

            switch (targetClass)
            {
                case MCObjectClass::Unused1:
                case MCObjectClass::Building:
                case MCObjectClass::Debris:
                case MCObjectClass::Tree:
                case MCObjectClass::TerrainObject:
                case MCObjectClass::Unused17:
                case MCObjectClass::MiscTerrainObject:
                case MCObjectClass::Jet:
                case MCObjectClass::TreeBuilding:
                case MCObjectClass::Turret:
                case MCObjectClass::Gate:
                case MCObjectClass::Light:
                {
                    // A terrain object: its block, vertex and item from the part id.
                    StatusChunk.TargetType = 2;
                    StatusChunk.TargetId = target->PartId;
                    const int32_t terrainPart = target->PartId - 0x1000;
                    StatusChunk.TargetBlockOrTrainNumber = terrainPart / 0xc80;
                    const int32_t inBlock = terrainPart % 0xc80;
                    StatusChunk.TargetVertexOrCarNumber = inBlock / 8;
                    StatusChunk.TargetItemNumber = static_cast<uint8_t>(inBlock % 8);
                    break;
                }

                case MCObjectClass::BattleMech:
                case MCObjectClass::GroundVehicle:
                case MCObjectClass::Elemental:
                {
                    StatusChunk.TargetType = 1;
                    StatusChunk.TargetId = std::static_pointer_cast<MCMover>(target)->NetRosterIndex;
                    break;
                }
                case MCObjectClass::CameraDrone:
                {
                    StatusChunk.TargetType = 3;
                    StatusChunk.TargetId = target->PartId;
                    StatusChunk.TargetBlockOrTrainNumber = 0x80;
                    StatusChunk.TargetVertexOrCarNumber = target->PartId - 0x802c8;
                    break;
                }
                case MCObjectClass::TrainCar:
                {
                    StatusChunk.TargetType = 3;
                    StatusChunk.TargetId = target->PartId;
                    const int32_t trainPart = target->PartId - 0x7d000;
                    StatusChunk.TargetBlockOrTrainNumber = trainPart / 100;
                    StatusChunk.TargetVertexOrCarNumber = trainPart % 100;
                    break;
                }

                default:
                    Fatal(static_cast<int32_t>(targetClass),
                          " GroundVehicle.buildStatusChunk: bad target object class ");
            }
        }
    }

    StatusChunk.EjectOrderGiven = EjectOrderGiven;
    StatusChunk.Pack();

    // Checks the chunk unpacks to what was packed.
    MCStatusChunk check;
    check.Data = StatusChunk.Data;
    check.Unpack();

    if (!StatusChunk.EqualTo(check))
    {
        Fatal(0, " BAD Statuschunk: save stchunk.dbg file! ");
    }

    return 0;
}

auto MCGroundVehicle::HandleStatusChunk(int32_t updateAge, uint32_t chunk) -> int32_t
{
    StatusChunk.TargetCellRC[0] = -1;
    StatusChunk.TargetCellRC[1] = -1;
    StatusChunk.Data = 0;
    StatusChunk.BodyState = 0;
    StatusChunk.TargetType = 0;
    StatusChunk.TargetId = 0;
    StatusChunk.TargetBlockOrTrainNumber = 0;
    StatusChunk.TargetVertexOrCarNumber = 0;
    StatusChunk.TargetItemNumber = 0;
    StatusChunk.EjectOrderGiven = 0;
    StatusChunk.JumpOrder = 0;
    StatusChunk.Data = chunk;
    StatusChunk.Unpack();

    if (StatusChunkUnpackErr != 0)
    {
        return 0;
    }

    int32_t targetPartId = 0;

    if (StatusChunk.JumpOrder == 0 && static_cast<int8_t>(StatusChunk.TargetType) > 0)
    {
        if (StatusChunk.TargetType == 1)
        {
            targetPartId = LockObject(MultiPlayer()->MoverRoster[StatusChunk.TargetId])->PartId;
        }
        else if (StatusChunk.TargetType < 4)
        {
            targetPartId = StatusChunk.TargetId;
        }
    }

    if (Pilot.lock() == nullptr)
    {
        return 0;
    }

    std::shared_ptr<MCGameObject> target;
    int keepTarget = 0;

    if (targetPartId != 0)
    {
        std::shared_ptr<MCGameObject> lastTarget = Pilot.lock()->GetLastTarget();

        if (lastTarget != nullptr && lastTarget->PartId == targetPartId)
        {
            keepTarget = 1;
        }
        else
        {
            target = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(targetPartId));
        }
    }

    if (keepTarget == 0)
    {
        Pilot.lock()->SetLastTarget(target, 0, 0);
    }

    if (EjectOrderGiven == 0 && StatusChunk.EjectOrderGiven != 0)
    {
        EjectOrderGiven = 1;
        HandleEjection();
    }

    return 0;
}

auto MCGroundVehicle::BuildMoveChunk() -> int32_t
{
    MoveChunk.Reset();

    if (Pilot.lock() != nullptr)
    {
        Pilot.lock()->GetMovePath();
        MoveChunk.Build(*this, *Pilot.lock()->MoveOrders.Path[0], *Pilot.lock()->MoveOrders.Path[1]);
    }

    MoveChunk.Pack(*this);

    // Checks the chunk unpacks to what was packed; a chunk that can't is replaced by an empty one.
    MCMoveChunk check;
    check.StepPos[0][0] = -1;
    check.StepPos[0][1] = -1;
    check.Run = 0;
    check.NumSteps = 0;
    check.Data = MoveChunk.Data;

    if (check.Unpack(*this))
    {
        if (MoveChunk.EqualTo(*this, check) == 0)
        {
            Fatal(0, " Bad gvehicl movechunk: save mvchunk.dbg file! ");
        }
    }
    else
    {
        MoveChunk.Reset();
        MoveChunk.Build(*this);
        MoveChunk.Pack(*this);
    }

    return 0;
}

auto MCGroundVehicle::HandleMoveChunk(uint32_t chunk) -> int32_t
{
    MoveChunk.Reset();
    MoveChunk.Data = chunk;

    // Steps off the map, from the server's message, are ignored.
    if (MoveChunk.Unpack(*this) && MoveChunk.OnMap())
    {
        MCMovePath& path = GetPilot()->GetMovePath();
        path.SetMoveChunk(MoveChunk);

        // Skip ahead to the step nearest the vehicle.
        if (path.NumStepsWhenNotPaused > 1)
        {
            int32_t step = path.NumStepsWhenNotPaused;

            do
            {
                step--;

                if (step < 1)
                {
                    break;
                }
            } while (MapCellDiagonal() < DistanceFrom(path.StepList[step].Destination));

            path.CurStep = step;
        }

        NewMoveChunk = true;
    }

    return 0;
}
