#include "stdafx.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "camera/MCCamera.h"
#include "engine/MCCraterManager.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "mission/MCWeatherPlay.h"
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
#include "object/MCMasterComponent.h"
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
#include "object/MCMoverGameSystem.h"
#include "sound/MCRadio.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTerrain.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCMechStops.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCMoverMath.h"

auto MCBattleMech::CanMove() -> int
{
    return LegStatus != 3 ? 1 : 0;
}

auto MCBattleMech::CanJump() -> int
{
    return NumJumpJets != 0 ? 1 : 0;
}

auto MCBattleMech::HandleStaticCollision() -> void
{
    const bool jumpFXOn = static_cast<MCMechActor*>(Appearance.get())->CurrentGesture != 0x14 &&
                          (JumpFX[0] != nullptr || JumpFX[1] != nullptr);

    if (!((CollisionsOn != 0 &&
           std::sqrt(Velocity.Z * Velocity.Z + Velocity.Y * Velocity.Y + Velocity.X * Velocity.X) > 0.0f) ||
          jumpFXOn))
    {
        return;
    }

    int32_t blockNumber = 0;
    int32_t vertexNumber = 0;
    GetBlockAndVertexNumber(blockNumber, vertexNumber);
    std::shared_ptr<MCObjectList> list = ObjectList()->FindList(std::format("TBlk{}", blockNumber));

    // Port fix: the original reads the objects of a missing list through null.
    if (list == nullptr)
    {
        return;
    }

    for (std::shared_ptr<MCBaseObject> object : *list)
    {
        auto other = std::static_pointer_cast<MCGameObject>(object);

        if (other->GetObjectType() == nullptr)
        {
            continue;
        }

        int collides = 0;
        int32_t otherBlock = -1;
        int32_t otherVertex = -1;

        switch (other->ObjectClass)
        {
            case MCObjectClass::Building:
            case MCObjectClass::Tree:
            case MCObjectClass::TerrainObject:
            case MCObjectClass::TreeBuilding:
            {
                other->GetBlockAndVertexNumber(otherBlock, otherVertex);
                collides = other->CollisionsOn;
                break;
            }
            case MCObjectClass::MiscTerrainObject:
            {
                GetBlockAndVertexNumber(otherBlock, otherVertex);

                // The walls (and, compared unsigned, an object of no kind).
                if (static_cast<uint32_t>(std::to_underlying(static_cast<MCMiscTerrainObject&>(*other).Kind)) > 6)
                {
                    collides = 1;
                }
                break;
            }
            default:
                break;
        }

        if (vertexNumber == otherVertex && collides != 0)
        {
            MCCollisionSystem::DetectStaticCollision(*this, *other);
        }
    }
}

auto MCBattleMech::PilotingCheck(uint32_t situation, float modifier) -> void
{
    if ((MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0) || PilotingCheckPending != 0)
    {
        return;
    }

    // Port: the match's weather's footing makes a fall likelier (MCWeatherPlay::Footing; outside a match, nothing).
    double roll = RandomNumber(100) + MCWeatherPlayNow().Footing;

    if ((situation & 2) != 0)
    {
        roll += 20.0;
    }

    if (BodyAt(MechRightLeg).CurInternalStructure == 0.0f || BodyAt(MechLeftLeg).CurInternalStructure == 0.0f)
    {
        roll += 100.0;
    }

    const MCInventoryItem& gyroItem = Inventory[Gyro];

    if (gyroItem.Health == 0)
    {
        roll += 100.0;
    }
    else if (static_cast<int32_t>(gyroItem.Health) < static_cast<int8_t>(MasterComponentList[gyroItem.MasterID].Health))
    {
        roll += 30.0;
    }

    if (Inventory[LeftLegActuator].Health == 0)
    {
        roll += 10.0;
    }

    if (Inventory[RightLegActuator].Health == 0)
    {
        roll += 10.0;
    }

    if ((situation & 1) == 0)
    {
        const int failed = static_cast<double>(Pilot.lock()->Skills[SkillPiloting]) <= roll ? 1 : 0;
        PilotingCheckPending = failed;
        Pilot.lock()->SkillPoints[SkillPiloting] += SkillTry[0];

        if (failed == 0)
        {
            Pilot.lock()->SkillPoints[SkillPiloting] += SkillSuccess[0];
        }
    }
    else
    {
        const int failed = static_cast<double>(Pilot.lock()->Skills[SkillJumping] + PilotJumpMod) <= roll ? 1 : 0;
        PilotingCheckPending = failed;
        Pilot.lock()->SkillPoints[SkillJumping] += SkillTry[1];

        if (failed == 0)
        {
            Pilot.lock()->SkillPoints[SkillJumping] += SkillSuccess[1];
        }
    }
}

auto MCBattleMech::MineCheck() -> void
{
    if ((MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0) || IsJumping() != 0)
    {
        return;
    }

    MCScenarioMap& map = GameMap();

    // The mine state bits of a tile's overlay: Inner Sphere 11..12, Clan 13..14; the spread counts 25..26, 27..28.
    if (SteppedOnMine != 0)
    {
        const MCMapTile& tile = map.Map[ObjPosition.lock()->TileR * map.Width + ObjPosition.lock()->TileC];
        const uint32_t state = Alignment == -1 ? tile.Overlay >> 11 : tile.Overlay >> 13;

        if ((state & 3) == 0)
        {
            SteppedOnMine = false;
            const int32_t tileR = ObjPosition.lock()->TileR;
            const int32_t tileC = ObjPosition.lock()->TileC;
            MCMapTile& here = map.Map[map.Width * tileR + tileC];

            if (GetAlignment() == -1)
            {
                here.Overlay = (here.Overlay & 0xffffefff) | 0x800;
            }
            else
            {
                here.Overlay = (here.Overlay & 0xffffbfff) | 0x2000;
            }

            if (MultiPlayer() != nullptr)
            {
                MultiPlayer()->AddMineChunk(tileR * 3, tileC * 3, Alignment != -1 ? 1 : 0, 1, 0);
                map = GameMap();
            }
        }
    }

    const uint32_t mine = Alignment == -1 ? map.GetInnerSphereMine(ObjPosition.lock()->TileR, ObjPosition.lock()->TileC,
                                                                   ObjPosition.lock()->CellR, ObjPosition.lock()->CellC)
                                          : map.GetClanMine(ObjPosition.lock()->TileR, ObjPosition.lock()->TileC,
                                                            ObjPosition.lock()->CellR, ObjPosition.lock()->CellC);

    if (mine == 0)
    {
        return;
    }

    int32_t firstRow = ObjPosition.lock()->TileR - 1;
    int32_t firstCol = ObjPosition.lock()->TileC - 1;

    if (firstRow < 0)
    {
        firstRow = 0;
    }

    if (firstCol < 0)
    {
        firstCol = 0;
    }

    const int32_t mapSide = MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide;

    if (mapSide <= firstCol + 3)
    {
        firstCol = mapSide - 1;
    }

    if (mapSide <= firstRow + 3)
    {
        firstRow = mapSide - 1;
    }

    for (int32_t row = firstRow; row < firstRow + 3; row++)
    {
        for (int32_t col = firstCol; col < firstCol + 3; col++)
        {
            const bool inMap = row >= 0 && row < GameMap().Height && col >= 0 && col < GameMap().Width;
            Assert(inMap, 0, " Map Tile out of bounds ");

            // Port fix: the original goes on to touch the tile past the map's edge.
            if (!inMap)
            {
                continue;
            }

            MCMapTile& tile = GameMap().Map[GameMap().Width * row + col];
            const bool innerSphere = GetAlignment() == -1;
            uint32_t count = ((innerSphere ? tile.Overlay >> 25 : tile.Overlay >> 27) & 3) + 1;

            if (count > 3)
            {
                count = 3;
            }

            if (GetAlignment() == -1)
            {
                tile.Overlay = (tile.Overlay & 0xf9ffffff) | (count << 25);
            }
            else
            {
                tile.Overlay = (tile.Overlay & 0xe7ffffff) | (count << 27);
            }
        }
    }

    const int32_t tileR = ObjPosition.lock()->TileR;
    const int32_t tileC = ObjPosition.lock()->TileC;
    MCMapTile& here = GameMap().Map[GameMap().Width * tileR + tileC];

    if (GetAlignment() == -1)
    {
        here.Overlay |= 0x1800;
    }
    else
    {
        here.Overlay |= 0x6000;
    }

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->AddMineChunk(tileR * 3 + ObjPosition.lock()->CellR, tileC * 3 + ObjPosition.lock()->CellC,
                                    Alignment != -1 ? 1 : 0, 3, 2);
    }

    Pilot.lock()->PausePath();
    MCVector3D position = GetPosition();
    CreateExplosion(MineExplosion, position, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
    const int32_t hitLocation = CalcHitLocation(std::nullopt, -1, 3, 0);
    MCWeaponShotInfo shotInfo;
    shotInfo.Init(nullptr, -2, MineBaseDamage, hitLocation, 0.0f);
    HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);

    if (GetPilot() != nullptr)
    {
        GetPilot()->RadioMessage(MCRadioMessageType::HittingMines, 1);
    }

    SteppedOnMine = true;
}

auto MCBattleMech::UpdateJump() -> int
{
    if (IsJumping() == 0)
    {
        return 0;
    }

    auto& actor = static_cast<MCMechActor&>(*Appearance);
    auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);

    if (actor.InJump == 0 && actor.JumpSetup == 0)
    {
        // Landed.
        InJump = false;
        JumpTime = ScenarioTime;
        MCMovePath& path = Pilot.lock()->GetMovePath();
        Pilot.lock()->ResumePath();
        LastValidPosition = Position;
        path.CurStep++;
        PilotingCheck(1, 0.0f);
    }

    if (actor.Airborne == 0)
    {
        if (MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0)
        {
            actor.SetJumpParameters(JumpGoal);

            if (static_cast<MCMechActor*>(Appearance.get())->InTransition == 0)
            {
                Appearance->SetGestureGoal(6);
                controlData.Throttle = 100;
            }
        }
        else if (DistanceFrom(JumpGoal) > 8.0f)
        {
            actor.SetJumpParameters(JumpGoal);

            if (static_cast<MCMechActor*>(Appearance.get())->InTransition == 0)
            {
                Appearance->SetGestureGoal(6);
                controlData.Throttle = 100;
                return 1;
            }
        }

        return 1;
    }

    // Turn toward the landing point: within two degrees, pivot by the pivot angle.
    float turn = RelFacingTo(JumpGoal, -1);

    if (turn >= -2.0f && turn <= 2.0f)
    {
        turn = turn < 0.0f ? -MechPivotAngle : MechPivotAngle;
    }

    const float maxRate =
        static_cast<float>(static_cast<MCMechDynamicsType&>(*TypeAs<MCBattleMechType>().DynamicsType).MaxMechYawRate);
    double rate = -(static_cast<double>(turn) / FrameLength);

    if (rate > maxRate)
    {
        rate = maxRate;
    }
    else if (rate < -maxRate)
    {
        rate = -maxRate;
    }

    controlData.Rotate = static_cast<int8_t>(static_cast<int32_t>(rate / maxRate * 64.0f));
    return 1;
}

auto MCBattleMech::PivotTo() -> int
{
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    MCMovePath& path = warrior->GetMovePath();
    const MCMoveState moveStateGoal = warrior->MoveOrders.MoveStateGoal;
    const MCMoveState moveState = warrior->MoveOrders.MoveState;
    const int32_t run =
        MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0 ? warrior->MoveOrders.Run : MoveChunk.Run;
    int hasTarget = 0;
    std::shared_ptr<MCGameObject> target = warrior->GetLastTarget();
    float targetFacing = 0.0f;
    const float maxPivot =
        static_cast<float>(
            static_cast<MCMechDynamicsType&>(*TypeAs<MCBattleMechType>().DynamicsType).MaxMechPivotRate) *
        FrameLength;

    if (target == nullptr)
    {
        if (warrior->CurTacOrder.Code == MCTacticalOrderCode::AttackPoint)
        {
            targetFacing = RelFacingTo(warrior->AttackOrders.TargetPoint, -1);
            hasTarget = 1;
        }
    }
    else
    {
        targetFacing = RelFacingTo(target->GetPosition(), -1);
        hasTarget = 1;
    }

    // Starts the pivot: a turn of <paramref name="turn"/> degrees, no faster than the pivot rate.
    const auto pivot = [&](float turn) -> int
    {
        if (maxPivot < std::fabs(turn))
        {
            turn = turn <= 0.0f ? -maxPivot : maxPivot;
        }

        auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);
        controlData.Rotate = static_cast<int8_t>(static_cast<int32_t>(static_cast<double>(turn) / maxPivot * 64.0f));
        controlData.Pivot = 1;
        UpdateTorso(turn);
        return 1;
    };

    const auto choosePivotDirection = [&]()
    {
        if (PivotDirection == 0xff)
        {
            PivotDirection = targetFacing >= 0.0f ? 1 : 0;
        }
    };

    const auto hasNextStep = [&]()
    { return path.NumStepsWhenNotPaused >= 1 && path.CurStep < path.NumStepsWhenNotPaused; };

    if (moveState == MCMoveState::PivotForward)
    {
        if (moveStateGoal == MCMoveState::PivotForward || moveStateGoal == MCMoveState::Forward)
        {
            if (!hasNextStep())
            {
                Pilot.lock()->MoveOrders.MoveStateGoal = MCMoveState::Forward;
            }
            else
            {
                const MCVector3D destination = path.StepList[path.CurStep].Destination;
                Appearance->SetGestureGoal(1);
                static_cast<MCMechControlData*>(Control->ControlData.get())->Throttle = 100;
                const float stepFacing = RelFacingTo(destination, -1);

                if (stepFacing < -15.0f || stepFacing > 15.0f)
                {
                    float turn = -stepFacing;

                    if (hasTarget != 0 && run == 0)
                    {
                        choosePivotDirection();

                        if (PivotDirection == 0)
                        {
                            if (stepFacing >= 0.0f)
                            {
                                turn = 360.0f - stepFacing;
                            }
                        }
                        else if (stepFacing < 0.0f)
                        {
                            turn = -360.0f - stepFacing;
                        }
                    }

                    return pivot(turn);
                }

                Pilot.lock()->MoveOrders.MoveState = MCMoveState::Forward;

                if (Pilot.lock()->MoveOrders.MoveStateGoalChanged != 0)
                {
                    Pilot.lock()->MoveOrders.MoveStateGoalChanged = 0;
                }
            }
        }
        else
        {
            Pilot.lock()->MoveOrders.MoveState = MCMoveState::Forward;
        }
    }
    else if (moveState == MCMoveState::PivotReverse)
    {
        if (moveStateGoal == MCMoveState::PivotReverse || moveStateGoal == MCMoveState::Reverse)
        {
            if (!hasNextStep())
            {
                Pilot.lock()->MoveOrders.MoveStateGoal = MCMoveState::Forward;
            }
            else
            {
                const MCVector3D destination = path.StepList[path.CurStep].Destination;
                Appearance->SetGestureGoal(1);
                static_cast<MCMechControlData*>(Control->ControlData.get())->Throttle = 100;
                const float stepFacing = RelFacingTo(destination, -1);

                if (stepFacing > -165.0f && stepFacing < 165.0f)
                {
                    bool turnLeft;

                    if (hasTarget == 0 || run != 0)
                    {
                        turnLeft = stepFacing < 0.0f;
                    }
                    else
                    {
                        choosePivotDirection();
                        turnLeft = PivotDirection != 0;
                    }

                    return pivot(turnLeft ? -180.0f - stepFacing : 180.0f - stepFacing);
                }

                std::shared_ptr<MCMechWarrior> orders = Pilot.lock();

                if (orders->MoveOrders.MoveStateGoalChanged != 0)
                {
                    orders->MoveOrders.MoveStateGoalChanged = 0;
                }

                if (moveStateGoal == MCMoveState::Reverse)
                {
                    orders->MoveOrders.MoveState = MCMoveState::Reverse;
                }
                else
                {
                    orders->MoveOrders.MoveStateGoal = MCMoveState::Forward;
                }
            }
        }
        else
        {
            Pilot.lock()->MoveOrders.MoveState = MCMoveState::Forward;
        }
    }
    else if (moveState != MCMoveState::PivotTarget)
    {
        if (moveStateGoal == MCMoveState::PivotTarget || moveStateGoal == MCMoveState::PivotForward ||
            moveStateGoal == MCMoveState::PivotReverse)
        {
            Pilot.lock()->MoveOrders.MoveState = moveStateGoal;
        }
    }
    else if (moveStateGoal != MCMoveState::PivotTarget)
    {
        Pilot.lock()->MoveOrders.MoveState = MCMoveState::Forward;
    }
    else if (run == 0 && hasTarget != 0)
    {
        Appearance->SetGestureGoal(1);
        static_cast<MCMechControlData*>(Control->ControlData.get())->Throttle = 100;
        const float fireArc = GetFireArc();

        if (targetFacing < -fireArc || fireArc < targetFacing)
        {
            return pivot(-targetFacing);
        }

        Pilot.lock()->MoveOrders.MoveStateGoal = MCMoveState::Forward;
    }
    else
    {
        Pilot.lock()->MoveOrders.MoveStateGoal = MCMoveState::Forward;
    }

    std::shared_ptr<MCMechWarrior> orders = Pilot.lock();

    if (!(orders->MoveOrders.YieldTime > -1.0f || orders->MoveOrders.WaitForPointTime > -1.0f))
    {
        orders->ResumePath();
    }

    PivotDirection = 0xff;
    return 0;
}

auto MCBattleMech::GetSpeedState() -> int32_t
{
    return MechSpeedStateArray[static_cast<MCMechActor*>(Appearance.get())->CurrentGesture];
}

auto MCBattleMech::UpdateMoveStateGoal() -> void
{
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    MCMovePath& path = warrior->GetMovePath();
    const MCMoveState moveStateGoal = warrior->MoveOrders.MoveStateGoal;

    if (path.NumSteps < 1)
    {
        if (moveStateGoal != MCMoveState::PivotTarget && moveStateGoal != MCMoveState::PivotForward &&
            moveStateGoal != MCMoveState::PivotReverse)
        {
            warrior->MoveOrders.MoveStateGoal = MCMoveState::Forward;
        }

        return;
    }

    const int32_t run =
        MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0 ? warrior->MoveOrders.Run : MoveChunk.Run;

    if (run != 0 || LegStatus == 2)
    {
        warrior->MoveOrders.MoveStateGoal = MCMoveState::Forward;
        return;
    }

    MCVector3D targetPosition;
    std::shared_ptr<MCGameObject> target = warrior->GetLastTarget();

    if (target == nullptr)
    {
        if (warrior->CurTacOrder.Code != MCTacticalOrderCode::AttackPoint)
        {
            warrior->MoveOrders.MoveStateGoal = MCMoveState::Forward;
            return;
        }

        targetPosition = warrior->AttackOrders.TargetPoint;
    }
    else
    {
        targetPosition = target->GetPosition();
    }

    if (path.NumStepsWhenNotPaused <= 0 || path.CurStep >= path.NumStepsWhenNotPaused)
    {
        return;
    }

    const double delta = RelFacingDelta(path.StepList[path.CurStep].Destination, targetPosition);
    std::shared_ptr<MCMechWarrior> orders = Pilot.lock();
    const double torsoArc = static_cast<MCMechDynamicsType&>(*TypeAs<MCBattleMechType>().DynamicsType).MaxTorsoYaw;

    if (orders->MoveOrders.MoveStateGoal == MCMoveState::Forward)
    {
        // The target is behind: walk backward.
        if (torsoArc < delta && 180.0 - delta <= torsoArc && orders->MoveOrders.MoveStateGoalChanged == 0)
        {
            orders->MoveOrders.MoveStateGoalChanged = 1;
            orders->MoveOrders.MoveStateGoal = MCMoveState::Reverse;
        }
    }
    else if (torsoArc < 180.0 - delta && delta <= torsoArc && orders->MoveOrders.MoveStateGoalChanged == 0)
    {
        orders->MoveOrders.MoveStateGoalChanged = 1;
        orders->MoveOrders.MoveStateGoal = MCMoveState::Forward;
    }
}

auto MCBattleMech::UpdateMovePath(char& newRotate, char& newThrottleSetting, float& newRotatePerSec,
                                  int32_t& newGestureStateGoal, MCMoveState& newMoveState, int32_t& minThrottle,
                                  int32_t& maxThrottle) -> int
{
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);
    auto& dynType = static_cast<MCMechDynamicsType&>(*TypeAs<MCBattleMechType>().DynamicsType);
    MCMovePath& path = warrior->GetMovePath();
    int running = LegStatus == 0 && warrior->MoveOrders.Run != 0 ? 1 : 0;
    newThrottleSetting = static_cast<char>(controlData.Throttle);
    newRotatePerSec = 0.0f;
    UpdateHustleTime();
    const bool hustling = static_cast<double>(ScenarioTime) < static_cast<double>(LastHustleTime) + 2.0;
    warrior = Pilot.lock();
    std::shared_ptr<MCMover> point = warrior->GetPoint();
    const bool groupMove = warrior->CurTacOrder.IsGroupOrder() != 0 && warrior->CurTacOrder.IsMoveOrder() != 0;

    if (running == 0 && !hustling && point != nullptr && point->IsDisabled() == 0 && point.get() != this && groupMove)
    {
        // Keep pace with the group's point: wait (at most five seconds while walking) when ahead of it.
        std::shared_ptr<MCMechWarrior> pointPilot = point->GetPilot();
        pointPilot->GetMovePath();
        const float pointDistanceLeft = pointPilot->GetMoveDistanceLeft();

        if (pointDistanceLeft <= warrior->GetMoveDistanceLeft())
        {
            warrior->MoveOrders.WaitForPointTime = -1.0f;

            if (warrior->MoveOrders.YieldTime <= -1.0f)
            {
                warrior->ResumePath();
            }
        }
        else
        {
            running = 0;
            const int32_t speedState = GetSpeedState();
            warrior = Pilot.lock();

            if (speedState == 2)
            {
                if (warrior->MoveOrders.WaitForPointTime <= -1.0f)
                {
                    warrior->MoveOrders.WaitForPointTime = ScenarioTime + 5.0f;
                }
            }
            else if (warrior->MoveOrders.WaitForPointTime < ScenarioTime)
            {
                warrior->PausePath();
                warrior->MoveOrders.WaitForPointTime = 999999.0f;
            }
        }
    }
    else
    {
        warrior->MoveOrders.WaitForPointTime = -1.0f;
    }

    int result = 0;

    if (LegStatus != 0 && LegStatus != 1 && LegStatus != 2)
    {
        newGestureStateGoal = 1;
        return 0;
    }

    if (path.NumSteps < 1)
    {
        newGestureStateGoal = 1;
        return 0;
    }

    int32_t step = path.CurStep;

    if (step == path.NumSteps)
    {
        result = 1;

        if (warrior->MoveOrders.PathType == 2 &&
            warrior->MoveOrders.Path[0]->GlobalStep < warrior->MoveOrders.NumGlobalSteps - 1)
        {
            result = 0;
        }

        if (warrior->MoveOrders.Path[0] != nullptr)
        {
            warrior->MoveOrders.Path[0]->Clear();
        }

        return result;
    }

    MCVector3D destination = path.StepList[step].Destination;
    LastValidPosition = destination;
    const auto distance = static_cast<float>(DistanceFrom(destination));
    const int32_t numSteps = path.NumSteps;
    const float margin = StepMargin(step, numSteps);

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
        // Reached the step: on to the next.
        step++;
        Pilot.lock()->MoveOrders.TimeOfLastStep = ScenarioTime;
        path.CurStep = step;

        if (numSteps <= step)
        {
            warrior = Pilot.lock();
            result = 1;

            if (warrior->MoveOrders.PathType == 2 &&
                warrior->MoveOrders.Path[0]->GlobalStep < warrior->MoveOrders.NumGlobalSteps - 1)
            {
                result = 0;
            }

            if (warrior->MoveOrders.Path[0] != nullptr)
            {
                warrior->MoveOrders.Path[0]->Clear();
            }

            return result;
        }

        if (static_cast<int8_t>(path.StepList[step].Direction) > 7)
        {
            newGestureStateGoal = 6;
            return 0;
        }

        destination = path.StepList[step].Destination;
    }

    const float facing = RelFacingTo(destination, -1);
    warrior = Pilot.lock();
    const MCMoveState moveState = warrior->MoveOrders.MoveState;
    const MCMoveState moveStateGoal = warrior->MoveOrders.MoveStateGoal;
    // Walking, the throttle creeps toward the ordered speed by tens.
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

    if (moveState == MCMoveState::Forward)
    {
        if (moveStateGoal != MCMoveState::Forward)
        {
            warrior->PausePath();

            if (moveStateGoal == MCMoveState::Reverse || moveStateGoal == MCMoveState::PivotReverse)
            {
                newMoveState = MCMoveState::PivotReverse;
            }
            else if (moveStateGoal == MCMoveState::PivotForward)
            {
                newMoveState = MCMoveState::PivotForward;
            }
            else
            {
                newMoveState = MCMoveState::Forward;
            }

            return result;
        }

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

        if (facing < -5.0f || facing > 5.0f)
        {
            const float turn = -facing;
            newRotatePerSec = turn;
            const float maxTurn = static_cast<float>(dynType.MaxMechYawRate) * FrameLength;

            if (std::fabs(turn) <= maxTurn)
            {
                if (newGestureStateGoal == 2)
                {
                    newThrottleSetting = walkThrottle();
                }
            }
            else
            {
                newRotatePerSec = turn <= 0.0f ? -maxTurn : maxTurn;
            }

            newRotate = static_cast<char>(
                static_cast<int32_t>(std::floor(static_cast<double>(newRotatePerSec) / maxTurn * 64.0)));
        }

        return result;
    }

    if (moveState == MCMoveState::Reverse)
    {
        if (moveStateGoal == MCMoveState::Reverse)
        {
            newGestureStateGoal = 4;
            const float turn = facing >= 0.0f ? facing - 180.0f : facing + 180.0f;
            newRotatePerSec = -turn;
            const float maxTurn = static_cast<float>(dynType.MaxMechYawRate) * FrameLength;
            char throttle;

            if (std::fabs(newRotatePerSec) <= maxTurn)
            {
                throttle = walkThrottle();
            }
            else
            {
                newRotatePerSec = newRotatePerSec <= 0.0f ? -maxTurn : maxTurn;
                throttle = static_cast<char>(controlData.Throttle - 10);
            }

            newThrottleSetting = throttle;
            newRotate = static_cast<char>(static_cast<int32_t>(static_cast<double>(newRotatePerSec) / maxTurn * 64.0f));
            return result;
        }

        warrior->PausePath();

        if (moveStateGoal == MCMoveState::Forward || moveStateGoal == MCMoveState::PivotForward)
        {
            newMoveState = MCMoveState::PivotForward;
        }
        else if (moveStateGoal == MCMoveState::PivotReverse)
        {
            newMoveState = MCMoveState::PivotReverse;
        }
        else
        {
            newMoveState = MCMoveState::Forward;
        }

        return result;
    }

    if (moveStateGoal == MCMoveState::Forward || moveStateGoal == MCMoveState::PivotForward)
    {
        warrior->PausePath();
        newMoveState = MCMoveState::PivotForward;
    }
    else if (moveStateGoal == MCMoveState::Reverse || moveStateGoal == MCMoveState::PivotReverse)
    {
        warrior->PausePath();
        newMoveState = MCMoveState::PivotReverse;
    }

    return result;
}

auto MCBattleMech::SetNextMovePath(char& newThrottleSetting, int32_t& newGestureStateGoal) -> void
{
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();

    if (warrior->PlayerOrderFromQueue != 0 && warrior->CurTacOrder.IsMoveOrder() != 0)
    {
        if (warrior->MoveOrders.Path[0] != nullptr)
        {
            warrior->MoveOrders.Path[0]->Clear();
        }

        return;
    }

    warrior->ClearMoveOrders();
    newGestureStateGoal = 1;
}

namespace
{
    /// <summary>Port-only: whether this is a multiplayer client, whose paths are the server's latest steps.</summary>
    auto MultiPlayerClient() -> bool
    {
        return MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0;
    }

    /// <summary>
    /// Port-only (MechBraking): the metres from <paramref name="position"/> along <paramref name="path"/>'s steps left
    /// before step <paramref name="until"/> (its end when none), as walked (from step to step; the steps' own distances
    /// to the goal count cells), then on to <paramref name="end"/> when given (a multiplayer client's move's end, past
    /// the server's latest steps).
    /// </summary>
    auto WalkLeft(const MCMovePath& path, const MCVector3D& position, const std::optional<MCVector3D>& end,
                  std::optional<int32_t> until = std::nullopt) -> float
    {
        double units = 0.0;
        MCVector3D from = position;
        const auto walk = [&](const MCVector3D& to)
        {
            units += std::hypot(static_cast<double>(to.X) - from.X, static_cast<double>(to.Y) - from.Y);
            from = to;
        };

        for (int32_t step = path.CurStep; step < until.value_or(path.NumSteps); ++step)
        {
            walk(path.StepList[static_cast<size_t>(step)].Destination);
        }

        if (end.has_value())
        {
            walk(*end);
        }

        return static_cast<float>(units * MetersPerWorldUnit);
    }

    /// <summary>Port-only (MechBraking): a movement cell as the stop claims key it.</summary>
    using MCCellKey = std::tuple<int32_t, int32_t, int32_t, int32_t>;

    auto KeyOf(const MCPathRangeLock& cell) -> MCCellKey
    {
        return {cell.TileR, cell.TileC, cell.CellR, cell.CellC};
    }

    auto CellOf(const MCPathStep& step) -> MCPathRangeLock
    {
        return {step.TileR, step.TileC, step.CellR, step.CellC};
    }

    /// <summary>
    /// Port-only (MechBraking): the cells mechs braking to a halt, or nearing where they will, have claimed to
    /// come to rest in (MCBattleMech::ClaimedCell), each with its mech's part id. Unlike the original's path locks they
    /// keep no one from passing: only from ending a move there. A claim whose mech has gone, or moved its claim, is
    /// dropped when next looked at (ClaimedByOther), so none outlives its mission.
    /// </summary>
    auto StopClaims() -> std::map<MCCellKey, int32_t>&
    {
        static std::map<MCCellKey, int32_t> claims;
        return claims;
    }

    /// <summary>
    /// Port-only (MechBraking): whether <paramref name="cell"/> is the claim of a mech other than
    /// <paramref name="self"/> (a claim whose mech has gone, or moved its claim, is dropped).
    /// </summary>
    auto ClaimedByOther(const MCPathRangeLock& cell, int32_t self) -> bool
    {
        auto& claims = StopClaims();
        const auto found = claims.find(KeyOf(cell));

        if (found == claims.end() || found->second == self)
        {
            return false;
        }

        const auto owner = std::dynamic_pointer_cast<MCBattleMech>(GetMoverFromPartId(found->second));

        if (owner == nullptr || owner->IsDestroyed() != 0 || !owner->ClaimedCell.has_value() ||
            KeyOf(*owner->ClaimedCell) != found->first)
        {
            claims.erase(found);
            return false;
        }

        // A disabled mech (shut down, legs gone) won't get there: its claim goes (its path locks aren't updated while
        // it is, so it can't let go itself).
        if (owner->IsDisabled() != 0)
        {
            owner->ClaimedCell.reset();
            claims.erase(found);
            return false;
        }

        return true;
    }

    /// <summary>
    /// Port-only (MechBraking): the mover other than <paramref name="self"/> that holds
    /// <paramref name="cell"/> (stands in it, or the original's lock ahead of it took it), if any.
    /// </summary>
    auto HolderOf(const MCPathRangeLock& cell, int32_t self) -> std::shared_ptr<MCMover>
    {
        const auto same = [&](int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC)
        { return tileR == cell.TileR && tileC == cell.TileC && cellR == cell.CellR && cellC == cell.CellC; };

        for (int32_t part = MCMover::FirstPartId; part < MCMover::EndPartId; ++part)
        {
            std::shared_ptr<MCMover> mover = part == self ? nullptr : GetMoverFromPartId(part);

            if (mover == nullptr)
            {
                continue;
            }

            if (const std::shared_ptr<MCObjectPosition> at = mover->ObjPosition.lock();
                at != nullptr && same(at->TileR, at->TileC, at->CellR, at->CellC))
            {
                return mover;
            }

            for (const MCPathRangeLock& lock : mover->PathRangeLocks)
            {
                if (same(lock.TileR, lock.TileC, lock.CellR, lock.CellC))
                {
                    return mover;
                }
            }
        }

        return nullptr;
    }

    /// <summary>
    /// Port-only (MechBraking): the first step of <paramref name="path"/> from which every step to
    /// its end is another mech's claim: there the move ends (MCBattleMech::CrashAvoidanceSystem), as the original's ends a
    /// cell short of an end another unit holds. None while its last step is no one else's.
    /// </summary>
    auto ClaimedTail(const MCMovePath& path, int32_t self) -> std::optional<int32_t>
    {
        if (MCMechBrakingVersion() < BrakingVersion2 || MultiPlayerClient())
        {
            return std::nullopt;
        }

        const int32_t end = path.NumStepsWhenNotPaused;
        int32_t tail = end;

        while (tail > std::max(path.CurStep, 0) &&
               ClaimedByOther(CellOf(path.StepList[static_cast<size_t>(tail - 1)]), self))
        {
            --tail;
        }

        return tail < end ? std::optional(tail) : std::nullopt;
    }

    /// <summary>
    /// Port-only (MechBraking): the step of <paramref name="path"/> at which the original's crash avoidance will end the
    /// move short of its end (MCBattleMech::CrashAvoidanceSystem: once the steps it looks ahead at, <paramref
    /// name="lookAhead"/> of them, reach the path's end with one of them another unit's or impassable): the first step
    /// whose look-ahead reaches the end, when a step from it on is taken, or the first of the path's last steps that are
    /// all other mechs' claims (ClaimedTail; <paramref name="self"/> the mech's part id), whichever is sooner. None while
    /// the way to the end is clear.
    /// </summary>
    auto HaltStep(const MCMovePath& path, int32_t lookAhead, int32_t self) -> std::optional<int32_t>
    {
        const int32_t end = path.NumStepsWhenNotPaused;
        const int32_t from = std::max(path.CurStep, end - lookAhead);
        // The move ends where its last steps are other mechs' claims.
        std::optional<int32_t> halt = ClaimedTail(path, self);

        if (halt.has_value())
        {
            halt = std::max(*halt, path.CurStep);
        }

        if (lookAhead < 1 || from >= end)
        {
            return halt;
        }

        int reachedEnd = 0;

        if (path.IsLocked(from, end - from, reachedEnd) || path.IsBlocked(from, end - from, reachedEnd))
        {
            return std::min(from, halt.value_or(from));
        }

        return halt;
    }
} // namespace

auto MCBattleMech::UpdatePathLock(int set) -> void
{
    MCMover::UpdatePathLock(set);

    if (set == 0 || MCMechBrakingVersion() < BrakingVersion2 || MultiPlayerClient())
    {
        return;
    }

    auto& actor = static_cast<MCMechActor&>(*Appearance);
    const std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    const MCMovePath& path = warrior->GetMovePath();
    const bool stepsLeft = path.NumStepsWhenNotPaused > 0 && path.CurStep < path.NumStepsWhenNotPaused;
    std::optional<MCPathRangeLock> wanted;

    if (IsDestroyed() != 0 || IsDisabled() != 0 || warrior->MoveOrders.YieldTime > -1.0f ||
        (stepsLeft && !MoveEndsWith(path)))
    {
        wanted.reset();
    }
    else if (stepsLeft)
    {
        // Braking, or within its stop and ClaimStrides strides of it: the cell it will come to rest in.
        const int32_t last =
            std::min(EndStopStep.value_or(path.NumStepsWhenNotPaused - 1), path.NumStepsWhenNotPaused - 1);
        const std::optional<float> claim = actor.ClaimMetres();

        if (last >= 0 &&
            (actor.PlannedStop || (claim.has_value() && WalkLeft(path, Position, std::nullopt, last + 1) <= *claim)))
        {
            wanted = CellOf(path.StepList[static_cast<size_t>(last)]);
        }
    }
    else if (actor.PlannedStop && actor.InTransition)
    {
        // Its path ended with the mech still braking: the cell its stop had, until it stands.
        wanted = ClaimedCell;
    }

    auto& claims = StopClaims();

    if (ClaimedCell.has_value() && (!wanted.has_value() || KeyOf(*wanted) != KeyOf(*ClaimedCell)))
    {
        if (const auto found = claims.find(KeyOf(*ClaimedCell)); found != claims.end() && found->second == PartId)
        {
            claims.erase(found);
        }

        ClaimedCell.reset();
    }

    // The first to claim a cell keeps it.
    if (wanted.has_value() && !ClaimedCell.has_value() && !ClaimedByOther(*wanted, PartId))
    {
        claims[KeyOf(*wanted)] = PartId;
        ClaimedCell = wanted;
    }
}

auto MCBattleMech::WalksBehind(const MCMovePath& path) -> bool
{
    auto& actor = static_cast<MCMechActor&>(*Appearance);
    // The first step ahead another unit holds, short of where the move's end is looked at (HaltStep's).
    const int32_t limit =
        EndStopStep.has_value() ? *EndStopStep + 1 : path.NumStepsWhenNotPaused - std::max(CrashAvoidPath, 1);
    std::optional<int32_t> held;

    for (int32_t step = path.CurStep + 1; step < limit && !held.has_value(); ++step)
    {
        const MCPathStep& pathStep = path.StepList[static_cast<size_t>(step)];

        if (GameMap().GetCellPathLocked(pathStep.TileR, pathStep.TileC, pathStep.CellR, pathStep.CellC))
        {
            held = step;
        }
    }

    // Only a unit slower than it is closed on: one running as fast keeps the distance by itself.
    bool slower = false;

    if (held.has_value())
    {
        const std::shared_ptr<MCMover> holder = HolderOf(CellOf(path.StepList[static_cast<size_t>(*held)]), PartId);
        const MCVector3D velocity = holder != nullptr ? holder->GetVelocity() : MCVector3D();
        slower = holder == nullptr || std::hypot(velocity.X, velocity.Y) < FollowSlower * CalcMaxSpeed();
    }

    // Within a run's stop and FollowStrides stride of that unit's cell, it walks; a stride further off, it runs again.
    const float behind = held.has_value() ? WalkLeft(path, Position, std::nullopt, *held) : 0.0f;
    const std::optional<float> keep = actor.StopAndStrides(MCStopState::Run, FollowStrides);
    const std::optional<float> letGo = actor.StopAndStrides(MCStopState::Run, FollowStrides + 1.0f);

    if (held.has_value() && slower && keep.has_value() && behind <= *keep)
    {
        Following = true;
    }
    else if (!held.has_value() || !slower || !letGo.has_value() || behind > *letGo)
    {
        Following = false;
    }

    // Not running yet, it starts a run only where it could stop from it before its move's end.
    const std::optional<float> runStart = actor.StopAndStrides(MCStopState::Run, RunStartStrides);
    const std::optional<float> endLeft =
        MoveEndsWith(path) ? std::optional(WalkLeft(path, Position, std::nullopt,
                                                    EndStopStep.value_or(path.NumStepsWhenNotPaused - 1) + 1))
                           : std::nullopt;
    const bool startsRun = actor.CurrentStateGesture != MCStopState::Run;
    return Following || (startsRun && runStart.has_value() && endLeft.has_value() && *endLeft <= *runStart);
}

auto MCBattleMech::PlanStop(int32_t& newGestureStateGoal) -> void
{
    auto& actor = static_cast<MCMechActor&>(*Appearance);
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    const MCMovePath& path = warrior->GetMovePath();
    // A multiplayer client's path is the server's latest steps, which may end at the step being walked into: its walk
    // goes on to the move's end the chunks showed.
    const std::optional<MCVector3D> end = MultiPlayerClient() && ChunkShowedMoveEnd ? ChunkMoveEnd : std::nullopt;
    const bool stepsLeft = path.NumSteps > 0 && path.CurStep < path.NumSteps;
    // Only a stride the mover keeps going is fitted to a stop (not one it limps, jumps or halts).
    const bool keepsGoing = newGestureStateGoal == MCStopState::Walk || newGestureStateGoal == MCStopState::Run;
    std::optional<float> metresLeft;
    StopStep.reset();
    EndStopStep.reset();

    if ((stepsLeft || end.has_value()) && (keepsGoing || actor.PlannedStop) && MoveEndsWith(path))
    {
        // A unit on the move's end: the original's crash avoidance ends the move once the mech reaches the step before
        // the one it halts at (within that step's margin of error), so the stop ends there. A multiplayer client's
        // stop ends where the server's chunks show the move ending.
        // Port-only (MechBraking): version 1 fits the stop to the path's end regardless.
        const std::optional<int32_t> halt = MultiPlayerClient() || MCMechBrakingVersion() < BrakingVersion2
                                                ? std::nullopt
                                                : HaltStep(path, CrashAvoidPath, PartId);

        if (halt.has_value())
        {
            StopStep = *halt - 1;

            if (*halt > path.CurStep)
            {
                metresLeft = WalkLeft(path, Position, std::nullopt, *halt) - MoveMarginOfError[0];
            }
        }
        else
        {
            // The move ends where the original would stop it: within its last step's margin of error of the end.
            const float walkLeft = WalkLeft(path, Position, end) - MoveMarginOfError[1];

            if (stepsLeft || walkLeft > 0.0f)
            {
                metresLeft = walkLeft;
            }
        }
    }

    EndStopStep = StopStep;
    HeldToWalk = false;

    // Port-only (MechBraking): running, it keeps its stopping distance behind a slower unit on its path, and
    // starts no run whose end is too near to stop from one (WalksBehind). The server's: its move chunks say walk.
    const bool mayFollow = MCMechBrakingVersion() >= BrakingVersion2 && !MultiPlayerClient() && stepsLeft &&
                           warrior->MoveOrders.YieldTime <= -1.0f;

    if (mayFollow && newGestureStateGoal == MCStopState::Run && !actor.PlannedStop)
    {
        HeldToWalk = WalksBehind(path);
        newGestureStateGoal = HeldToWalk ? MCStopState::Walk : newGestureStateGoal;
    }
    else if (!mayFollow || newGestureStateGoal != MCStopState::Run)
    {
        Following = false;
    }

    if (actor.FitStop(metresLeft))
    {
        newGestureStateGoal = MCStopState::Stand;
    }
}

auto MCBattleMech::MoveEndsWith(const MCMovePath& path) const -> bool
{
    for (int32_t step = path.CurStep; step < path.NumSteps; ++step)
    {
        if (static_cast<int8_t>(path.StepList[static_cast<size_t>(step)].Direction) > 7)
        {
            return false;
        }
    }

    // A multiplayer client's path is the server's latest steps: the move ends there once they counted down to its end.
    if (MultiPlayerClient())
    {
        return ChunkShowedMoveEnd;
    }

    const std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    return warrior != nullptr && !warrior->MoveGoesOnPast(path);
}

auto MCBattleMech::StepMargin(int32_t step, int32_t numSteps) const -> float
{
    const float margin = step == numSteps - 1 ? MoveMarginOfError[1] : MoveMarginOfError[0];

    // Port-only (MechBraking): a mech braking to a halt at the step its stop ends at counts as there within the slack
    // its stop ends in.
    if (step == StopStep.value_or(numSteps - 1) && Appearance != nullptr &&
        static_cast<const MCMechActor&>(*Appearance).PlannedStop)
    {
        return margin + BrakeArrivalSlack;
    }

    return margin;
}

auto MCBattleMech::MoveHeldUp() const -> bool
{
    const std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();

    // A multiplayer client's path is the server's latest steps: held up unless they counted down to the move's end.
    if (MultiPlayerClient())
    {
        return !ChunkShowedMoveEnd;
    }

    if (warrior == nullptr)
    {
        return false;
    }

    // Read as it stands: GetMovePath would swap in the next leg.
    const std::unique_ptr<MCMovePath>& path = warrior->MoveOrders.Path[0];

    if (path != nullptr && path->CurStep < path->NumStepsWhenNotPaused)
    {
        return true;
    }

    if (warrior->MovePathRequest.lock() != nullptr)
    {
        return true;
    }

    return path != nullptr && warrior->MoveGoesOnPast(*path);
}

auto MCBattleMech::UpdateTorso(float newRotatePerSec) -> void
{
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    std::shared_ptr<MCGameObject> target = warrior->GetLastTarget();
    double facing;

    if (target != nullptr)
    {
        facing = static_cast<double>(RelFacingTo(target->GetPosition(), -1)) + TorsoRotation + newRotatePerSec;
    }
    else if (warrior->CurTacOrder.Code == MCTacticalOrderCode::AttackPoint)
    {
        facing =
            static_cast<double>(RelFacingTo(warrior->GetAttackTargetPoint(), -1)) + TorsoRotation + newRotatePerSec;
    }
    else
    {
        facing = TorsoRotation;
    }

    if (facing < -180.0)
    {
        facing += 360.0;
    }
    else if (facing > 180.0f)
    {
        facing -= 360.0;
    }

    if (facing >= -2.0 && facing <= 2.0)
    {
        return;
    }

    double turn = -facing;
    auto& dynType = static_cast<MCMechDynamicsType&>(*TypeAs<MCBattleMechType>().DynamicsType);
    const float maxTurn = static_cast<float>(dynType.MaxTorsoYawRate) * FrameLength;

    if (maxTurn < std::fabs(turn))
    {
        turn = turn < 0.0 ? -maxTurn : maxTurn;
    }

    static_cast<MCMechControlData*>(Control->ControlData.get())->TorsoRotate =
        static_cast<int8_t>(static_cast<int32_t>(turn / maxTurn * 64.0f));
}

auto MCBattleMech::SetControlSettings(char& newRotate, char& newThrottleSetting, float& newRotatePerSec,
                                      int32_t& newGestureStateGoal, int32_t& minThrottle, int32_t& maxThrottle) -> void
{
    auto& actor = static_cast<MCMechActor&>(*Appearance);

    if (InJump != 0 && actor.InJump == 0)
    {
        InJump = false;
        Pilot.lock()->ResumePath();
    }

    if (newGestureStateGoal == 6)
    {
        // The path's step is a jump.
        std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
        MCMovePath& path = warrior->GetMovePath();
        warrior->PausePath();
        JumpGoal = path.StepList[path.CurStep].Destination;
        actor.SetJumpParameters(JumpGoal);
    }

    bool startJump = false;

    if (MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0)
    {
        std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();

        if (warrior->CurTacOrder.IsJumpOrder() != 0 && InJump == 0)
        {
            const auto& point = warrior->CurTacOrder.MoveParams.WayPath.Points;
            JumpGoal = MCVector3D(point[0], point[1], point[2]);
            newGestureStateGoal = 6;
            startJump = true;
        }
    }
    else if (StatusChunk.JumpOrder != 0 && InJump == 0)
    {
        JumpGoal = MapCellToWorldPos(StatusChunk.TargetCellRC[0], StatusChunk.TargetCellRC[1]);

        if (DistanceFrom(JumpGoal) > 8.0f)
        {
            newGestureStateGoal = 6;
            startJump = true;
        }
    }

    if (startJump)
    {
        actor.SetJumpParameters(JumpGoal);
    }

    const int32_t gestureGoal = newGestureStateGoal;
    auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);

    if (gestureGoal != -1 && static_cast<MCMechActor*>(Appearance.get())->InTransition == 0)
    {
        auto& mechActor = static_cast<MCMechActor&>(*Appearance);

        if (mechActor.SetGestureGoal(gestureGoal) == 0)
        {
            if (gestureGoal == 6)
            {
                InJump = true;
            }

            if (gestureGoal != 2)
            {
                controlData.Throttle = 100;
            }
        }
        else if (mechActor.InTransition == 0 && mechActor.CurrentStateGesture == 2)
        {
            // Walking: the throttle stays within the limits.
            if (newThrottleSetting != -1)
            {
                if (newThrottleSetting < minThrottle)
                {
                    newThrottleSetting = static_cast<char>(minThrottle);
                }
                else if (maxThrottle < newThrottleSetting)
                {
                    newThrottleSetting = static_cast<char>(maxThrottle);
                }

                controlData.Throttle = newThrottleSetting;
            }
        }
    }

    if (newRotate != 0)
    {
        controlData.Rotate = newRotate;
    }
}

auto MCBattleMech::UpdateMovement() -> void
{
    // Port-only (MechBraking): held to a walk only in a frame PlanStop says so (none when it returns before).
    HeldToWalk = false;
    auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);
    int32_t minThrottle = 0x23;
    int32_t maxThrottle = 100;
    // A fall: gesture 7 or 8 (at random unless forced).
    const auto fallGesture = [&]() -> int32_t
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

        return gesture;
    };

    if (DisableThisFrame != 0)
    {
        if (Appearance->SetGestureGoal(fallGesture()) == 0)
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
        SoundSystem()->PlayDigitalSample(0x3c, 1, *this, false, false);

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
        SoundSystem()->PlayDigitalSample(0x3d, 1, *this, false, false);

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

    if (Status == 4 || Status == 5 || Status == 1)
    {
        return;
    }

    if (IsCaptured() != 0 || EngineBlowTime > -1.0f)
    {
        return;
    }

    if (PilotingCheckPending != 0)
    {
        const int32_t result = Appearance->SetGestureGoal(fallGesture());

        if (result == 0 || result == -0x1521ffff)
        {
            PilotingCheckPending = false;
        }

        controlData.Throttle = static_cast<int8_t>(maxThrottle);
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

    if (UpdateMovePath(newRotate, newThrottleSetting, newRotatePerSec, newGestureStateGoal, newMoveState, minThrottle,
                       maxThrottle) != 0)
    {
        SetNextMovePath(newThrottleSetting, newGestureStateGoal);
    }

    PlanStop(newGestureStateGoal);

    if (newMoveState != MCMoveState::NoChange)
    {
        Pilot.lock()->MoveOrders.MoveState = newMoveState;
    }

    SetControlSettings(newRotate, newThrottleSetting, newRotatePerSec, newGestureStateGoal, minThrottle, maxThrottle);
    UpdateTorso(newRotatePerSec);
}

auto MCBattleMech::CreateJumpFX() -> void
{
    if (JumpFX[0] != nullptr || JumpFX[1] != nullptr)
    {
        return;
    }

    for (std::shared_ptr<MCGameObject>& jet : JumpFX)
    {
        jet = CreateObject(0x1c6);
        static_cast<MCJet&>(*jet).SetOwner(*this);
    }

    CraterManager().AddCrater(7, Position, 0);
}

auto MCBattleMech::EndJumpFX() -> void
{
    if (JumpFX[0] == nullptr && JumpFX[1] == nullptr)
    {
        return;
    }

    for (std::shared_ptr<MCGameObject>& jet : JumpFX)
    {
        jet.reset();
    }
}

auto MCBattleMech::GetJumpPosition(int32_t jet) -> MCVector3D
{
    if (jet < 0 || jet > 1)
    {
        jet = 0;
    }

    auto& actor = static_cast<MCMechActor&>(*Appearance);
    const int32_t frameNumber = actor.CurrentFrame[0];
    // OB-175: the jets' rows are the file's frames long, not the jump gesture's (one more).
    const MCVector3D offset = TypeAs<MCBattleMechType>().JumpJetOffset(jet, frameNumber);
    const float offsetX = offset.X;
    const float offsetY = offset.Y;
    const float offsetZ = offset.Z;
    const double facing = MCMoverMath::ExactFrameFacing(Frame);
    double s;
    double c;
    MCMoverMath::SnappedFacing(facing, s, c);
    MCVector3D base = Position;

    if (!actor.FrameHeights().empty())
    {
        // Lifted along the mech's up axis by the jump's height this frame.
        const float height = actor.FrameHeight(frameNumber) * 30.0f;
        base.X = Frame.K.X * height + base.X;
        base.Y = base.Y + Frame.K.Y * height;
        base.Z = base.Z + height * Frame.K.Z;
    }

    MCVector3D result;
    result.X = base.X + static_cast<float>(c * offsetX + offsetY * s) * 20.0f;
    result.Z = offsetZ * 20.0f + base.Z;
    result.Y = static_cast<float>((offsetY * c - s * offsetX) * 20.0f) + base.Y;
    return result;
}

auto MCBattleMech::CrashAvoidanceSystem() -> int
{
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return 0;
    }

    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    MCMovePath& path = warrior->GetMovePath();

    if (path.NumStepsWhenNotPaused == 0)
    {
        return 0;
    }

    if (static_cast<double>(warrior->MoveOrders.WaitForPointTime) > 999990.0)
    {
        return 0;
    }

    // Port-only (MechBraking): the rest of the path other mechs' claims to come to rest in, the move ends here,
    // as the original's ends short of an end another unit holds.
    if (MCMechBrakingVersion() >= BrakingVersion2)
    {
        if (const std::optional<int32_t> tail = ClaimedTail(path, PartId); tail.has_value() && path.CurStep >= *tail)
        {
            warrior->ReachedPathEnd();
            Control->ControlData->Brake();
            return 1;
        }
    }

    // A look a frame ahead along the frame turned by a quarter pi (its result is unused).
    const float speed = -static_cast<MCMechActor*>(Appearance.get())->GetVelocityMagnitude();
    MCFrameOfRef ahead = Frame;
    MCMoverMath::RotateAboutK(ahead, static_cast<float>(std::sin(MCMoverMath::HalfPi / 2.0)),
                              static_cast<float>(std::cos(MCMoverMath::HalfPi / 2.0)));
    MCVector3D lookAhead(ahead.J.X * speed * FrameLength * WorldUnitsPerMeter + Position.X,
                         ahead.J.Y * speed * FrameLength * WorldUnitsPerMeter + Position.Y,
                         WorldUnitsPerMeter * 0.0f + Position.Z);
    int32_t tileR;
    int32_t tileC;
    int32_t cellR;
    int32_t cellC;
    MCScenarioMap::WorldToMapPos(lookAhead, tileR, tileC, cellR, cellC);

    int cornerBlocked = 0;
    const int32_t direction = static_cast<int8_t>(path.StepList[path.CurStep].Direction);

    if (direction == 1 || direction == 3 || direction == 5 || direction == 7)
    {
        // A diagonal step: blocked when both cells beside it are locked.
        const int first =
            GetAdjacentCellPathLocked(ObjPosition.lock()->TileR, ObjPosition.lock()->TileC, ObjPosition.lock()->CellR,
                                      ObjPosition.lock()->CellC, AdjClippedCell[direction][0]);
        const int second =
            GetAdjacentCellPathLocked(ObjPosition.lock()->TileR, ObjPosition.lock()->TileC, ObjPosition.lock()->CellR,
                                      ObjPosition.lock()->CellC, AdjClippedCell[direction][1]);
        cornerBlocked = first != 0 && second != 0 ? 1 : 0;
    }

    int lockReachedEnd = 0;
    int blockReachedEnd = 0;
    const int locked = GetPathRangeLock(CrashAvoidPath, lockReachedEnd);
    const int blocked = GetPathRangeBlocked(CrashAvoidPath, blockReachedEnd);
    const int32_t closedGates = path.CrossesClosedGate(-1, 2);
    warrior = Pilot.lock();
    const bool clear = locked == 0 && blocked == 0 && cornerBlocked == 0 && closedGates < 1;

    if (warrior->MoveOrders.YieldTime > -1.0f)
    {
        // Yielding: go on once the way is clear.
        if (clear)
        {
            warrior->ResumePath();
            warrior->MoveOrders.YieldTime = -1.0f;
            return 0;
        }

        warrior->PausePath();
        return 1;
    }

    if (clear)
    {
        return 0;
    }

    if (lockReachedEnd == 0 && blockReachedEnd == 0)
    {
        warrior->PausePath();
        warrior->MoveOrders.YieldTime = ScenarioTime + CrashYieldTime;
        Control->ControlData->Brake();
        return 1;
    }

    warrior->ReachedPathEnd();
    Control->ControlData->Brake();
    return 1;
}

auto MCBattleMech::IsJumping(MCVector3D& jumpGoal) -> int
{
    jumpGoal = this->JumpGoal;

    return InJump;
}

auto MCBattleMech::GetJumpRange(int32_t& numOffsets, int32_t& jumpCost) -> float
{
    numOffsets = MechJumpOffsets[NumJumpJets < 7 ? NumJumpJets : 6];

    jumpCost = NumJumpJets != 0 ? DefaultMechJumpCost : 0;

    return static_cast<float>(static_cast<double>(NumJumpJets) * MCTerrain::MetersPerVertex * (2.0 / 3.0));
}

auto MCBattleMech::CalcMaxSpeed() -> float
{
    auto& actor = static_cast<MCMechActor&>(*Appearance);

    if (LegStatus == 0)
    {
        return actor.GetVelocityOfGesture(7);
    }

    if (LegStatus < 2)
    {
        return actor.GetVelocityOfGesture(4);
    }

    if (LegStatus == 2)
    {
        return actor.GetVelocityOfGesture(11);
    }

    return 0.0f;
}

auto MCBattleMech::CalcSlowSpeed() -> float
{
    if (LegStatus < 2)
    {
        return static_cast<float>(MaxRunSpeed * 0.25);
    }

    if (LegStatus == 2)
    {
        return static_cast<float>(MaxRunSpeed * 0.2);
    }

    return 0.0f;
}

auto MCBattleMech::CalcModerateSpeed() -> float
{
    if (LegStatus < 2)
    {
        return static_cast<float>(MaxRunSpeed * 0.4);
    }

    if (LegStatus == 2)
    {
        return static_cast<float>(MaxRunSpeed * 0.3);
    }

    return 0.0f;
}

auto MCBattleMech::CalcSpriteSpeed(float speed, uint32_t flags, int32_t& state, int32_t& throttle) -> int32_t
{
    auto& actor = static_cast<MCMechActor&>(*Appearance);
    state = 3;
    throttle = 100;
    const float walkSpeed = actor.GetVelocityOfGesture(4);
    const float runSpeed = actor.GetVelocityOfGesture(7);

    if (speed == 0.0)
    {
        state = 1;
        return 0;
    }

    if (speed < walkSpeed * 0.5)
    {
        state = 2;
        throttle = 50;
        return 1;
    }

    if (speed <= walkSpeed)
    {
        state = 2;
        throttle = static_cast<int32_t>(static_cast<double>(speed) / walkSpeed * 100.0);
        return 0;
    }

    if (speed < runSpeed)
    {
        if ((flags & 1) != 0)
        {
            state = 2;
            throttle = static_cast<int32_t>(static_cast<double>(speed) / walkSpeed * 100.0);
            return 2;
        }

        state = 3;
        return 2;
    }

    if (runSpeed < speed)
    {
        state = 3;
        return 3;
    }

    return 0;
}
