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
    /// Port-only (MechBraking, HDStops): whether <paramref name="warrior"/>'s move goes on past <paramref name="path"/>'s
    /// end: a next leg of a global path, a next path, or a queued way point to go on to.
    /// </summary>
    auto MoveGoesOn(const MCMechWarrior& warrior, const MCMovePath& path) -> bool
    {
        const MCMoveOrders& orders = warrior.MoveOrders;

        if (orders.PathType == 2 && path.GlobalStep < orders.NumGlobalSteps - 1)
        {
            return true;
        }

        if (orders.Path[1] != nullptr && orders.Path[1]->NumStepsWhenNotPaused > 0)
        {
            return true;
        }

        MCVector3D nextPoint;
        return warrior.GetNextWayPoint(nextPoint, 0) != 0;
    }

    /// <summary>
    /// Port-only (MechBraking): whether <paramref name="warrior"/>'s move ends where its path does: no jump left on the
    /// way, and (but on a multiplayer client) it goes on no further (<see cref="MoveGoesOn"/>).
    /// </summary>
    auto MoveEndsHere(const MCMechWarrior& warrior, const MCMovePath& path) -> bool
    {
        for (int32_t step = path.CurStep; step < path.NumSteps; ++step)
        {
            if (static_cast<int8_t>(path.StepList[static_cast<size_t>(step)].Direction) > 7)
            {
                return false;
            }
        }

        return MultiPlayerClient() || !MoveGoesOn(warrior, path);
    }

    /// <summary>
    /// Port-only (MechBraking): the metres from <paramref name="position"/> along <paramref name="path"/>'s steps left to
    /// its end, as walked (from step to step; the steps' own distances to the goal count cells).
    /// </summary>
    auto WalkLeft(const MCMovePath& path, const MCVector3D& position) -> float
    {
        double units = 0.0;
        MCVector3D from = position;

        for (int32_t step = path.CurStep; step < path.NumSteps; ++step)
        {
            const MCVector3D& to = path.StepList[static_cast<size_t>(step)].Destination;
            units += std::hypot(static_cast<double>(to.X) - from.X, static_cast<double>(to.Y) - from.Y);
            from = to;
        }

        return static_cast<float>(units * MetersPerWorldUnit);
    }
} // namespace

auto MCBattleMech::PlanStop(int32_t& newGestureStateGoal) -> void
{
    auto& actor = static_cast<MCMechActor&>(*Appearance);
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    const MCMovePath& path = warrior->GetMovePath();
    const bool moving = path.NumSteps > 0 && path.CurStep < path.NumSteps;
    // Only a stride the mover keeps going is fitted to a stop (not one it limps, jumps or halts).
    const bool keepsGoing = newGestureStateGoal == MCStopState::Walk || newGestureStateGoal == MCStopState::Run;
    std::optional<float> metresLeft;

    // The move ends where the original would stop it: within its last step's margin of error (metres) of the end.
    if (moving && (keepsGoing || actor.PlannedStop) && MoveEndsHere(*warrior, path))
    {
        metresLeft = WalkLeft(path, Position) - MoveMarginOfError[1];
    }

    if (actor.FitStop(metresLeft))
    {
        newGestureStateGoal = MCStopState::Stand;
    }
}

auto MCBattleMech::LastStepMargin() const -> float
{
    // Port-only (MechBraking): a mech braking to a halt there counts as there within the slack its stop ends in.
    if (Appearance != nullptr && static_cast<const MCMechActor&>(*Appearance).PlannedStop)
    {
        return MoveMarginOfError[1] + BrakeArrivalSlack;
    }

    return MoveMarginOfError[1];
}

auto MCBattleMech::MoveHeldUp() const -> bool
{
    const std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();

    // A multiplayer client's path is the server's latest steps: it can't tell a mech held up from one there.
    if (MultiPlayerClient())
    {
        return true;
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

    return path != nullptr && MoveGoesOn(*warrior, *path);
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
