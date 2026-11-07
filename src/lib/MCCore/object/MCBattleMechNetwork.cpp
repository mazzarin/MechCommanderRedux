#include "stdafx.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCAIControl.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCCollisionSystem.h"
#include "object/MCDebris.h"
#include "object/MCDebrisType.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCMoverGroup.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCMechControlData.h"
#include "object/MCMechDynamics.h"
#include "object/MCNetControl.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectType.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCEffectSystem.h"
#include "object/MCMechWarrior.h"
#include "sound/MCRadio.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCMechActor.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"

auto MCBattleMech::NetUpdateMovePath(char& newRotate, char& newThrottleSetting, float& newRotatePerSec,
                                     int32_t& newGestureStateGoal, MCMoveState& newMoveState, int32_t& minThrottle,
                                     int32_t& maxThrottle) -> int
{
    auto& dynType = static_cast<MCMechDynamicsType&>(*TypeAs<MCBattleMechType>().DynamicsType);
    auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);
    MCMovePath& path = Pilot.lock()->GetMovePath();
    const int running = LegStatus == 0 && MoveChunk.Run != 0 ? 1 : 0;
    newThrottleSetting = static_cast<char>(controlData.Throttle);
    newRotatePerSec = 0.0f;

    if ((LegStatus != 0 && LegStatus != 1 && LegStatus != 2) || path.NumSteps < 1)
    {
        newGestureStateGoal = 1;
        return 0;
    }

    int32_t step = path.CurStep;

    if (step == path.NumSteps)
    {
        return 1;
    }

    MCVector3D destination = path.StepList[step].Destination;
    LastValidPosition = destination;
    const auto distance = static_cast<float>(DistanceFrom(destination));
    const int32_t numSteps = path.NumSteps;
    const float margin = step == numSteps - 1 ? LastStepMargin() : MoveMarginOfError[0];

    if (margin <= distance)
    {
        if (static_cast<int8_t>(path.StepList[step].Direction) > 7)
        {
            newGestureStateGoal = 6;
            return 0;
        }
    }
    else
    {
        step++;
        Pilot.lock()->MoveOrders.TimeOfLastStep = ScenarioTime;
        path.CurStep = step;

        if (numSteps <= step)
        {
            return 1;
        }

        if (static_cast<int8_t>(path.StepList[step].Direction) > 7)
        {
            newGestureStateGoal = 6;
            return 0;
        }

        destination = path.StepList[step].Destination;
    }

    const float facing = RelFacingTo(destination, -1);
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    const MCMoveState moveState = warrior->MoveOrders.MoveState;
    const MCMoveState moveStateGoal = warrior->MoveOrders.MoveStateGoal;
    const auto walkThrottle = [&]() -> char
    {
        const char throttle = static_cast<char>(controlData.Throttle);

        if (GetBodyState() != 2)
        {
            return 100;
        }

        const char speed = static_cast<char>(Pilot.lock()->MoveOrders.SpeedThrottle);

        if (throttle < speed - 10)
        {
            return static_cast<char>(throttle + 10);
        }

        if (speed <= throttle && speed + 10 <= throttle)
        {
            return static_cast<char>(throttle - 10);
        }

        return speed;
    };

    // The turn per second is limited to the yaw rate (not scaled by the frame here).
    const float maxRate = static_cast<float>(dynType.MaxMechYawRate);

    if (moveState == MCMoveState::Forward && moveStateGoal == MCMoveState::Forward)
    {
        if (LegStatus == 2)
        {
            newGestureStateGoal = 5;
            newThrottleSetting = 100;
        }
        else if (running == 0)
        {
            newGestureStateGoal = 2;
        }
        else
        {
            newThrottleSetting = 100;
            newGestureStateGoal = 3;
        }

        if (facing >= -5.0f && facing <= 5.0f)
        {
            return 0;
        }

        newRotatePerSec = -(facing / FrameLength);

        if (newRotatePerSec > maxRate)
        {
            newRotatePerSec = maxRate;
        }
        else if (newRotatePerSec < -maxRate)
        {
            newRotatePerSec = -maxRate;
        }
        else if (newGestureStateGoal == 2)
        {
            newThrottleSetting = walkThrottle();
        }

        newRotate = static_cast<char>(static_cast<int32_t>(static_cast<double>(newRotatePerSec) / maxRate * 64.0f));
        return 0;
    }

    if (moveState == MCMoveState::Reverse && moveStateGoal == MCMoveState::Reverse)
    {
        newGestureStateGoal = 4;
        newRotatePerSec = facing >= 0.0f ? -((facing - 180.0f) / FrameLength) : -((facing + 180.0f) / FrameLength);

        if (newRotatePerSec > maxRate)
        {
            newRotatePerSec = maxRate;
            newThrottleSetting = static_cast<char>(controlData.Throttle - 10);
        }
        else if (newRotatePerSec < -maxRate)
        {
            newRotatePerSec = -maxRate;
            newThrottleSetting = static_cast<char>(controlData.Throttle - 10);
        }
        else
        {
            newThrottleSetting = walkThrottle();
        }

        newRotate = static_cast<char>(static_cast<int32_t>(static_cast<double>(newRotatePerSec) / maxRate * 64.0f));
        return 0;
    }

    // Otherwise pivot: forward for goals 1 and 3, backward for 2 and 4; from forward or reverse, any other goal
    // stops.
    MCMoveState pivotState{};

    if (moveStateGoal == MCMoveState::Forward || moveStateGoal == MCMoveState::PivotForward)
    {
        pivotState = MCMoveState::PivotForward;
    }
    else if (moveStateGoal == MCMoveState::Reverse || moveStateGoal == MCMoveState::PivotReverse)
    {
        pivotState = MCMoveState::PivotReverse;
    }
    else if (moveState == MCMoveState::Forward || moveState == MCMoveState::Reverse)
    {
        pivotState = MCMoveState::Forward;
    }
    else
    {
        return 0;
    }

    warrior->PausePath();
    newMoveState = pivotState;
    return 0;
}

auto MCBattleMech::NetUpdateMovement() -> void
{
    auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);
    int32_t minThrottle = 0x23;
    int32_t maxThrottle = 100;
    const int32_t bodyState = GetBodyState();
    MCMovePath& path = Pilot.lock()->GetMovePath();
    std::optional<MCVector3D> destination = path.CurrentDestination();

    if (path.NumStepsWhenNotPaused > 0 && bodyState == 0)
    {
        StartUpThisFrame = true;
    }

    if (path.NumSteps - 1 <= path.CurStep && destination &&
        static_cast<float>(DistanceFrom(*destination)) < MoveMarginOfError[1])
    {
        // At the end of the path: take up the body state the server sent.
        StartUpThisFrame = false;
        int32_t gesture = -1;

        switch (StatusChunk.BodyState)
        {
            case 1:
            {
                if (bodyState != 1)
                {
                    if (bodyState == 0)
                    {
                        SoundSystem()->PlayDigitalSample(0x3d, 1, *this, false, false);
                    }

                    gesture = 1;
                }
                break;
            }
            case 2:
            {
                if (bodyState != 0)
                {
                    SoundSystem()->PlayDigitalSample(0x3c, 1, *this, false, false);
                    gesture = 0;
                }
                break;
            }
            case 3:
            {
                if (bodyState != 8)
                {
                    gesture = 8;
                }
                break;
            }
            case 4:
            {
                if (bodyState != 7)
                {
                    gesture = 7;
                }
                break;
            }
            default:
                break;
        }

        if (gesture != -1)
        {
            Pilot.lock()->ClearMoveOrders();
            Appearance->SetGestureGoal(gesture);
            controlData.Throttle = static_cast<int8_t>(maxThrottle);
            return;
        }
    }

    if (DisableThisFrame != 0)
    {
        int32_t gesture = 8 - (RandomNumber(2) != 0 ? 1 : 0);

        if (HitFromBehindThisFrame != 0)
        {
            gesture = 7;
        }
        else if (HitFromFrontThisFrame != 0)
        {
            gesture = 8;
        }

        if (Appearance->SetGestureGoal(gesture) == 0)
        {
            DisableThisFrame = false;
            ShutDownThisFrame = false;
            StartUpThisFrame = false;
            HitFromFrontThisFrame = false;
            HitFromBehindThisFrame = false;
        }

        controlData.Throttle = static_cast<int8_t>(maxThrottle);
        return;
    }

    if (ShutDownThisFrame != 0)
    {
        const int32_t result = Appearance->SetGestureGoal(0);

        if (result == 0 || result == -0x1521ffff)
        {
            ShutDownThisFrame = false;
            StartUpThisFrame = false;

            if (result == -0x1521ffff)
            {
                Status = 5;
            }
        }

        controlData.Throttle = static_cast<int8_t>(maxThrottle);
        return;
    }

    if (StartUpThisFrame != 0)
    {
        const int32_t result = Appearance->SetGestureGoal(1);

        if (result == 0 || result == -0x1521ffff)
        {
            StartUpThisFrame = false;
            ShutDownThisFrame = false;

            if (result == -0x1521ffff)
            {
                Status = 0;
            }
        }

        controlData.Throttle = static_cast<int8_t>(maxThrottle);
        return;
    }

    if (Status == 4 || Status == 5 || Status == 1 || IsCaptured() != 0 || EngineBlowTime > -1.0f)
    {
        return;
    }

    if (UpdateJump() != 0)
    {
        return;
    }

    float newRotatePerSec = static_cast<float>(PivotTo());

    if (newRotatePerSec != 0.0f)
    {
        return;
    }

    char newRotate = 0;
    char newThrottleSetting = -1;
    int32_t newGestureStateGoal = -1;
    MCMoveState newMoveState = MCMoveState::NoChange;
    UpdateMoveStateGoal();
    NetUpdateMovePath(newRotate, newThrottleSetting, newRotatePerSec, newGestureStateGoal, newMoveState, minThrottle,
                      maxThrottle);
    PlanStop(newGestureStateGoal);

    if (newMoveState != MCMoveState::NoChange)
    {
        Pilot.lock()->MoveOrders.MoveState = newMoveState;
    }

    SetControlSettings(newRotate, newThrottleSetting, newRotatePerSec, newGestureStateGoal, minThrottle, maxThrottle);
    UpdateTorso(newRotatePerSec);
}

auto MCBattleMech::UpdateCriticalHitChunks(int32_t which) -> int32_t
{
    for (int32_t i = 0; i < NumCriticalHitChunks[which]; i++)
    {
        const uint8_t chunk = CriticalHitChunks[which][i];
        const int32_t bodyLocation = chunk >> 4;
        const int32_t criticalSpace = chunk & 0xf;

        // The chunks come from the server; one naming a body location or critical space the mech doesn't have, or a
        // space holding no item (id -1), is skipped (15 destroys the location).
        if (bodyLocation >= NumBodyLocations() ||
            (criticalSpace != 15 && criticalSpace >= std::ssize(BodyAt(bodyLocation).CriticalSpaces)))
        {
            continue;
        }

        if (criticalSpace != 15)
        {
            const auto item = static_cast<int8_t>(
                BodyAt(bodyLocation).CriticalSpaces[static_cast<size_t>(criticalSpace)].InventoryID);

            if (item < 0 || item >= std::ssize(Inventory))
            {
                continue;
            }
        }

        HandleCriticalHit(bodyLocation, criticalSpace);
    }

    NumCriticalHitChunks[which] = 0;
    return 0;
}

auto MCBattleMech::BuildStatusChunk() -> int32_t
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

    // The body state: 1 standing up, 2 standing, 3 and 4 fallen, 0 otherwise.
    const uint32_t bodyState = static_cast<uint32_t>(GetBodyState());

    if (static_cast<MCMechActor*>(Appearance.get())->CurrentGesture == 1 || bodyState < 9)
    {
        switch (bodyState)
        {
            case 1:
                StatusChunk.BodyState = 1;
                break;
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
                StatusChunk.BodyState = 0;
                break;
            case 7:
                StatusChunk.BodyState = 4;
                break;
            case 8:
                StatusChunk.BodyState = 3;
                break;
            default:
                StatusChunk.BodyState = 2;
                break;
        }
    }
    else
    {
        StatusChunk.BodyState = 0;
    }

    if (Pilot.lock() != nullptr)
    {
        if (InJump == 0)
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
                        Fatal(static_cast<int32_t>(targetClass), " BattleMech.buildStatusChunk: bad target type ");
                }
            }
        }
        else
        {
            StatusChunk.JumpOrder = 1;
            int32_t cellR = 0;
            int32_t cellC = 0;
            WorldCoordToMapCell(JumpGoal, cellR, cellC);
            StatusChunk.TargetCellRC[0] = static_cast<int16_t>(cellR);
            StatusChunk.TargetCellRC[1] = static_cast<int16_t>(cellC);
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
        Fatal(0, " BAD status chunk in mech: save stchunk.dbg file! ");
    }

    return 0;
}

auto MCBattleMech::HandleStatusChunk(int32_t updateAge, uint32_t chunk) -> int32_t
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

auto MCBattleMech::BuildMoveChunk() -> int32_t
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
            Fatal(0, " Bad mech movechunk: save mvchunk.dbg file! ");
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

auto MCBattleMech::HandleMoveChunk(uint32_t chunk) -> int32_t
{
    MoveChunk.Reset();
    MoveChunk.Data = chunk;

    // Steps off the map, from the server's message, are ignored.
    if (MoveChunk.Unpack(*this) && MoveChunk.OnMap())
    {
        MCMovePath& path = GetPilot()->GetMovePath();
        path.SetMoveChunk(MoveChunk);

        // Port-only (HDStops, MechBraking): whether the chunks showed the move's end, and where. A server that says so
        // (MCChunkWord, OB-189) is taken at its word; an older one's chunk with room left shows the end, and one of a
        // single cell keeps the last word.
        if (MoveChunk.Word != MCChunkWord::None || MoveChunk.NumSteps > 1)
        {
            ChunkShowedMoveEnd = MoveChunk.Word == MCChunkWord::None ? MoveChunk.NumSteps < MCMoveChunk::MaxSteps
                                                                     : MoveChunk.Word == MCChunkWord::EndsHere;
            ChunkMoveEnd = ChunkShowedMoveEnd ? std::optional(path.Goal) : std::nullopt;
        }

        // Skip ahead to the step nearest the mech.
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
