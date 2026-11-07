#include "stdafx.h"
#include "object/MCTurret.h"
#include "object/MCTurretType.h"
#include "ai/MCMoveSystem.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCByteFlag.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCEllipseElement.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "lib/MCVector2D.h"
#include "lib/MCDice.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCScenario.h"
#include "mission/MCWeatherPlay.h"
#include "network/MCMultiPlayer.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCMasterComponent.h"
#include "object/MCFire.h"
#include "object/MCFireType.h"
#include "object/MCEffectSystem.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCBattleMech.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectQueue.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCGVAppearance.h"
#include "sprite/MCPUAppearance.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"
#include "object/MCObjectType.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponFireChunk.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCMoverMath.h"

namespace
{
    /// <summary>Sixty degrees in radians, as MCX.EXE stores it.</summary>
    constexpr double SIXTY_DEGREES = 0x1.0c152382d45b2p+0;

    /// <summary>The entry angle of each packed chunk value (turret.cpp's own copy of the table).</summary>
    const float ChunkEntryAngles[4] = {0.0f, 180.0f, -90.0f, 90.0f};

    /// <summary>A mech, vehicle, elemental or plain mover: something with a pilot.</summary>
    bool IsMoverClass(const MCGameObject& object)
    {
        const MCObjectClass objectClass = object.ObjectClass;
        return objectClass == MCObjectClass::BattleMech || objectClass == MCObjectClass::GroundVehicle ||
               objectClass == MCObjectClass::Elemental || objectClass == MCObjectClass::Mover;
    }

    /// <summary>The hot spot of the hit location, on a mech target; 0 otherwise.</summary>
    int32_t TargetHotSpotOf(std::shared_ptr<MCGameObject> target, int32_t hitLocation)
    {
        if (target != nullptr && target->ObjectClass == MCObjectClass::BattleMech)
        {
            // Port fix: the original reads body[hitLocation], past the eight body locations for a rear torso hit
            // (8..10); the torso it maps to is read instead.
            return std::static_pointer_cast<MCBattleMech>(target)
                ->BodyAt(MechArmorToBodyLocation[hitLocation])
                .HotSpotNumber;
        }

        return 0;
    }

    /// <summary>Makes the weapon's effect object (Fatal when it can't).</summary>
    std::shared_ptr<MCGameObject> CreateWeaponFX(const MCMasterComponent& weapon)
    {
        std::shared_ptr<MCGameObject> fx = CreateObject(static_cast<int32_t>(WeaponFXTable[weapon.WeaponEffect]));

        if (fx == nullptr)
        {
            Fatal(-1, " couldnt create weapon FX ");
        }

        return fx;
    }

    /// <summary>
    /// Sends a weapon effect from the turret (hot spot 0) at <paramref name="target"/> or, when it is null, at
    /// <paramref name="point"/>, carrying <paramref name="shot"/>; then adds it to the weapon list.
    /// </summary>
    void LaunchWeaponFX(MCTurret& turret, std::shared_ptr<MCGameObject> fx, std::shared_ptr<MCGameObject> target,
                        std::optional<MCVector3D> point, MCWeaponShotInfo& shot, int32_t targetHotSpot)
    {
        if (fx->ObjectClass == MCObjectClass::Bullet)
        {
            auto& bullet = static_cast<MCBullet&>(*fx);

            bullet.NewShot().Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation,
                                  shot.EntryAngle);

            if (target == nullptr)
            {
                bullet.Connect(turret, *point, 0);
            }
            else
            {
                bullet.Owner = turret.SharedAs<MCGameObject>();
                bullet.Target = target;
                bullet.OwnerHotSpot = 0;
                bullet.TargetHotSpot = targetHotSpot;
            }
        }
        else if (fx->ObjectClass == MCObjectClass::Laser)
        {
            auto& laser = static_cast<MCLaser&>(*fx);

            if (target == nullptr)
            {
                laser.Connect(turret.SharedAs<MCGameObject>(), *point, shot, 0);
            }
            else
            {
                laser.Source = turret.SharedAs<MCGameObject>();
                laser.Target = target;
                laser.SourceHotSpot = 0;
                laser.TargetHotSpot = targetHotSpot;
                laser.ShotInfo.Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation,
                                    shot.EntryAngle);
            }
        }
        else
        {
            auto& projectile = static_cast<MCProjectileLaser&>(*fx);

            if (target == nullptr)
            {
                projectile.Connect(turret.SharedAs<MCGameObject>(), *point, shot, 0);
            }
            else
            {
                projectile.Owner = turret.SharedAs<MCGameObject>();
                projectile.Target = target;
                projectile.OwnerHotSpot = 0;
                projectile.TargetHotSpot = targetHotSpot;
                projectile.ShotInfo.Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation,
                                         shot.EntryAngle);
            }
        }

        WeaponList()->Add(std::move(fx));
    }

    /// <summary>
    /// Builds, packs and checks the chunk for a hit on <paramref name="target"/> (a mover, train car, camera drone or
    /// terrain object) or a shot at <paramref name="point"/> when it is null; then queues and logs it.
    /// </summary>
    void SendFireChunk(MCTurret& turret, std::shared_ptr<MCGameObject> target, MCGameObject& logTarget,
                       std::optional<MCVector3D> point, int hit, float entryAngle, int32_t missiles,
                       int32_t missilesPastAMS, int32_t antiMissileShots, int32_t hitLocation)
    {
        MCWeaponFireChunk chunk;
        chunk.Init();

        if (target == nullptr)
        {
            chunk.BuildLocationTarget(*point, 0, hit, missiles);
        }
        else if (IsMoverClass(target->ObjectClass))
        {
            chunk.BuildMoverTarget(static_cast<MCBigGameObject&>(*target), 0, hit, entryAngle, missiles,
                                   missilesPastAMS, antiMissileShots, hitLocation);
        }
        else if (target->ObjectClass == MCObjectClass::TrainCar)
        {
            chunk.BuildTrainTarget(static_cast<MCBigGameObject&>(*target), 0, hit, entryAngle, missiles);
        }
        else if (target->ObjectClass == MCObjectClass::CameraDrone)
        {
            chunk.BuildCameraDroneTarget(static_cast<MCBigGameObject&>(*target), 0, hit, entryAngle, missiles);
        }
        else
        {
            chunk.BuildTerrainTarget(static_cast<MCBigGameObject&>(*target), 0, hit, missiles);
        }

        if (target != nullptr)
        {
            ObjectList()->FindObjectFromPart(chunk.TargetId);
        }

        chunk.Pack();
        MCWeaponFireChunk check;
        check.Init();
        check.Data = chunk.Data;
        check.Unpack(turret);

        if (chunk.EqualTo(check) == 0)
        {
            Fatal(0, " Turret.fireWeapon: Bad WeaponFireChunk (save wfchunk.dbg file now) ");
        }

        turret.AddWeaponFireChunk(0, chunk);
    }

    /// <summary>
    /// A missed shot's offset from its aim point, up to <paramref name="scatter"/>. Centred scatter (missiles in
    /// fireWeapon) spreads both ways; otherwise, as the original computes it, only one way.
    /// </summary>
    MCVector3D ScatterPoint(float scatter, int centred)
    {
        MCVector3D miss;
        miss.X = scatter;
        miss.Y = scatter;
        miss.Z = 0.0f;
        const auto offsetX = static_cast<float>(RandomNumber(static_cast<int32_t>(miss.X + miss.X)) - miss.X);
        const auto offsetY = static_cast<float>(RandomNumber(static_cast<int32_t>(miss.Y + miss.Y)) - miss.Y);
        const auto offsetZ = static_cast<float>(RandomNumber(static_cast<int32_t>(miss.Z + miss.Z)) - miss.Z);

        if (centred != 0)
        {
            miss.X = offsetX;
            miss.Y = offsetY;
        }
        else
        {
            miss.X = miss.X + offsetX;
            miss.Y = miss.Y + offsetY;
        }

        miss.Z = miss.Z + offsetZ;
        return miss;
    }

    /// <summary>
    /// Firing gives a turret away: when one of the other side's mechs is within visual range, the ground around the
    /// turret is seen by that side (with <see cref="MCTerrain::MarkRadiusSeen"/> from fireWeapon, markSeen from
    /// handleWeaponFire).
    /// </summary>
    void RevealFiring(MCTurret& turret, int radius)
    {
        std::shared_ptr<MCObjectList> enemies = nullptr;
        uint8_t seenBy = 0;

        if (turret.Alignment == 1)
        {
            enemies = ClanMechList();
            seenBy = 2;
        }
        else if (turret.Alignment == -1)
        {
            enemies = InnerSphereMechList();
            seenBy = 1;
        }
        else
        {
            return;
        }

        for (std::shared_ptr<MCBaseObject> enemy : *enemies)
        {
            MCVector3D enemyPosition = static_cast<MCGameObject&>(*enemy).GetPosition();

            if (turret.DistanceFrom(enemyPosition) < Scenario()->MaxVisualRange)
            {
                MCVector3D lookVector(0.0f, 1.0f, 0.0f);

                if (radius != 0)
                {
                    Terrain()->MarkRadiusSeen(turret.Position, lookVector, 360.0f, Scenario()->FireVisualRange, seenBy);
                }
                else
                {
                    Terrain()->MarkSeen(turret.Position, lookVector, 360.0f, Scenario()->FireVisualRange, seenBy);
                }

                return;
            }
        }
    }

    /// <summary>
    /// How many of the four corners of the turret's vertex square <paramref name="bits"/> marks (the turret's
    /// (row, col), (row + 1, col), (row + 1, col + 1), (row, col + 1)).
    /// </summary>
    int32_t CountVisibleCorners(MCByteFlag& bits, uint32_t row, uint32_t col, int stopAtFirst)
    {
        int32_t count = 0;
        const uint32_t corners[4][2] = {{row, col}, {row + 1, col}, {row + 1, col + 1}, {row, col + 1}};

        for (const auto& corner : corners)
        {
            if (bits.GetFlag(corner[0], corner[1]) != 0)
            {
                count++;

                if (stopAtFirst != 0)
                {
                    break;
                }
            }
        }

        return count;
    }

    /// <summary>The map row and column of a turret's terrain vertex.</summary>
    void VertexRowCol(const MCTurret& turret, uint32_t& row, uint32_t& col)
    {
        col = static_cast<uint32_t>((turret.BlockNumber % MCTerrain::BlocksMapSide) * MCTerrain::VerticesBlockSide +
                                    turret.VertexNumber % MCTerrain::VerticesBlockSide);
        row = static_cast<uint32_t>((turret.BlockNumber / MCTerrain::BlocksMapSide) * MCTerrain::VerticesBlockSide +
                                    turret.VertexNumber / MCTerrain::VerticesBlockSide);
    }
}

MCTurret::MCTurret() = default;

MCTurret::~MCTurret() = default;

auto MCTurret::IsVisible() -> int
{
    if (Eye() == nullptr || Eye()->Active == 0)
    {
        return 0;
    }

    int visible = MCCamera::VertexProject(BlockNumber, VertexNumber, ScreenPos);

    if (Appearance != nullptr)
    {
        visible = Appearance->RecalcBounds(EyeRef());
    }

    if (visible == 0)
    {
        return 0;
    }

    WindowsVisible = Turn;
    return 1;
}

auto MCTurret::Update() -> int32_t
{
    auto& type = TypeAs<MCTurretType>();

    if (JustCreated != 0)
    {
        // Set the turret on its vertex: the block's corner, the vertex within it, then the offset within the tile
        // (turned into the isometric grid's 60-degree axes).
        JustCreated = false;
        const int32_t verticesBlockSide = MCTerrain::VerticesBlockSide;
        float blockX = static_cast<float>(BlockNumber % MCTerrain::BlocksMapSide - MCTerrain::BlocksMapSide / 2) *
                       MCTerrain::MetersBlockSide;
        float blockY = static_cast<float>(MCTerrain::BlocksMapSide / 2 - BlockNumber / MCTerrain::BlocksMapSide) *
                       MCTerrain::MetersBlockSide;

        if ((MCTerrain::BlocksMapSide & 1) != 0)
        {
            blockX = blockX - MCTerrain::MetersBlockSide * 0.5f;
            blockY = MCTerrain::MetersBlockSide * 0.5f + blockY;
        }

        const float vertexX = static_cast<float>(VertexNumber % verticesBlockSide) * MCTerrain::MetersPerVertex;
        blockY = blockY - static_cast<float>(VertexNumber / verticesBlockSide) * MCTerrain::MetersPerVertex;
        const double offsetY = static_cast<double>(TileOffsetY);
        const double offsetX = static_cast<double>(TileOffsetX);
        double offsetAngle;

        if (offsetY == 0.0)
        {
            offsetAngle = 90.0;
        }
        else
        {
            offsetAngle = std::atan(offsetX / offsetY) * MCMoverMath::RadiansToDegrees;
        }

        Position.Y = blockY;
        const auto offsetDistance = static_cast<float>(std::sqrt(offsetY * offsetY + offsetX * offsetX));
        const double axisAngle = (60.0 - offsetAngle) * MCMoverMath::DegreesToRadians;
        const auto alongAxis = static_cast<float>(std::sin(axisAngle) * offsetDistance / std::sin(SIXTY_DEGREES));
        Position.X = vertexX + blockX;
        const float elevation = MCTerrain::GetTerrainElevation(Position);
        Position.X =
            static_cast<float>(std::cos(SIXTY_DEGREES) * alongAxis + std::cos(axisAngle) * offsetDistance + Position.X);
        Position.Y = Position.Y - alongAxis;
        Position.Z = elevation;

        TileCol = (BlockNumber % MCTerrain::BlocksMapSide) * verticesBlockSide + VertexNumber % verticesBlockSide;
        const int32_t halfMap = (verticesBlockSide * MCTerrain::BlocksMapSide) >> 1;
        TilePositionX = static_cast<float>(TileCol - halfMap) * MCTerrain::MetersPerVertex;
        TileRow = VertexNumber / verticesBlockSide + (BlockNumber / MCTerrain::BlocksMapSide) * verticesBlockSide;
        TilePositionY = static_cast<float>(halfMap - TileRow) * MCTerrain::MetersPerVertex;
        const auto inBounds = [&]
        { return TileRow < 0 || GameMap().Height <= TileRow || TileCol < 0 || GameMap().Width <= TileCol ? 0u : 1u; };
        Assert(inBounds(), 0, " tbldg MapTile Out of Bounds ");
        Assert(inBounds(), 0, " Map Tile out of bounds ");
        const MCMapTile& tile = GameMap().Map[GameMap().Width * TileRow + TileCol];
        const int32_t elevationLevel = static_cast<int32_t>((tile.Cells >> 7) & 0x3f) + GameMap().BaseElevation;
        Appearance->Visible = true;
        TileElevation = static_cast<float>(elevationLevel) * MCTerrain::MetersPerElevLevel;
        Appearance->Update();
        Appearance->RecalcBounds(EyeRef());
    }

    if (Destroyed != 0)
    {
        CollisionsOn = 0;
        return 1;
    }

    // The server drops a target that's out of reach or out of the fight.
    if ((MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0) && LockObject(Target) != nullptr)
    {
        const MCVector3D targetPosition = LockObject(Target)->GetPosition();
        const float dx = targetPosition.X - Position.X;
        const float dy = targetPosition.Y - Position.Y;
        const float reach = GetExtentRadius();

        if (LockObject(Target)->IsDisabled() != 0 || LockObject(Target)->IsDestroyed() != 0 ||
            static_cast<double>(reach) * reach < static_cast<double>(dy) * dy + static_cast<double>(dx) * dx)
        {
            Target.reset();
        }
    }

    // An awake turret shows its side the ground around it, once.
    Awake = GetAwake();
    bool unmarked = false;

    if (Alignment == 1)
    {
        unmarked = MarkedSeenInnerSphere == 0;
    }
    else if (Alignment == -1)
    {
        unmarked = MarkedSeenClan == 0;
    }

    if (1 < Turn && Awake != 0 && unmarked)
    {
        MCVector3D lookVector(0.0f, 0.0f, 0.0f);

        if (Alignment == 1)
        {
            Terrain()->MarkSeen(Position, lookVector, 360.0f, Scenario()->MaxVisualRange, 1);
            MarkedSeenInnerSphere = true;
        }
        else if (Alignment == -1)
        {
            Terrain()->MarkSeen(Position, lookVector, 360.0f, Scenario()->MaxVisualRange, 2);
            MarkedSeenClan = true;
        }
    }

    if (Awake != 0 && LockObject(Target) != nullptr)
    {
        // Turn toward the target and open up.
        float relAngle = RelFacingTo(LockObject(Target)->GetPosition(), -1) + TurretRotation;
        float turnStep = 0.0f;

        if (relAngle < -2.0 || 2.0 < relAngle)
        {
            turnStep = type.MaxTurretYawRate * FrameLength;

            if (!(relAngle < 0.0))
            {
                turnStep = -turnStep;
            }

            relAngle = -relAngle;

            // Original behaviour (OB-013): meant to cap the turn at the yaw rate, but whichever way the two differ
            // the full angle is taken, so the turret snaps onto its target.
            if (relAngle < turnStep || turnStep < relAngle)
            {
                turnStep = relAngle;
            }
        }

        TurretRotation = turnStep + TurretRotation;
        int32_t combatState = 0;

        if (FixedTurret == 0)
        {
            combatState = static_cast<MCPUAppearance*>(Appearance.get())->SetCombatMode(true);
        }

        if (WeaponDeployed == 0)
        {
            WeaponDeployed = FixedTurret == 0 ? (combatState == 2 ? 1 : 0) : true;
        }
    }
    else if (Awake == 0)
    {
        // Asleep: close up.
        if (FixedTurret == 0)
        {
            WeaponDeployed = static_cast<MCPUAppearance*>(Appearance.get())->SetCombatMode(false) == 2;
        }
    }
    else
    {
        // Nothing to shoot at: close up.
        int32_t combatState = 0;

        if (FixedTurret == 0)
        {
            combatState = static_cast<MCPUAppearance*>(Appearance.get())->SetCombatMode(false);
        }

        if (WeaponDeployed != 0)
        {
            WeaponDeployed = FixedTurret == 0 ? (combatState == 2 ? 1 : 0) : false;
        }
    }

    if (Destroyed == 0 && LockObject(Target) != nullptr && IsWeaponReady() != 0 && Awake != 0 &&
        (MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0))
    {
        FireWeapon(LockObject(Target));
    }

    if (!WeaponFireChunks[1].empty())
    {
        UpdateWeaponFireChunks(1);
    }

    return 1;
}

auto MCTurret::IsWeaponReady() const -> int
{
    return ReadyTime <= ScenarioTime && WeaponDeployed != 0 && WeaponEnabled != 0 ? 1 : 0;
}

auto MCTurret::IsWeaponMissile() -> int
{
    return MasterComponentList[TypeAs<MCTurretType>().WeaponType].Form == MCComponentForm::WeaponMissile ? 1 : 0;
}

auto MCTurret::CalcAttackChance(std::shared_ptr<MCGameObject> target, int32_t& range) -> float
{
    auto& type = TypeAs<MCTurretType>();
    // Port fix: the original measures to an uninitialised point when there's no target (no caller passes none).
    MCVector3D targetPosition(0.0f, 0.0f, 0.0f);

    if (target != nullptr)
    {
        targetPosition = target->GetPosition();
    }

    const int32_t pilotSkill = type.PilotSkill;
    const auto distance = static_cast<float>(DistanceFrom(targetPosition));

    if (WeaponRange[0] < distance)
    {
        range = WeaponRange[1] < distance ? 2 : 1;
    }
    else
    {
        range = 0;
    }

    // The weapon's own range bands; beyond the last it can't hit.
    const MCMasterComponent& weapon = MasterComponentList[type.WeaponType];
    float modifier;

    if (distance <= weapon.WeaponRange[0])
    {
        modifier = WeaponFireModifiers[14];
    }
    else if (distance <= weapon.WeaponRange[1])
    {
        modifier = WeaponFireModifiers[11];
    }
    else if (distance <= weapon.WeaponRange[2])
    {
        modifier = WeaponFireModifiers[12];
    }
    else if (distance <= weapon.WeaponRange[3])
    {
        // Port: at long range, less the match's weather's (MCWeatherPlay::LongAim; outside a match, nothing).
        modifier = WeaponFireModifiers[13] + MCWeatherPlayNow().LongAim;
    }
    else
    {
        return -1.0f;
    }

    const float chance = modifier + static_cast<float>(pilotSkill);

    if (target == nullptr)
    {
        return chance;
    }

    // A still target is easier.
    const MCVector3D velocity = target->GetVelocity();

    if (velocity.X * velocity.X + velocity.Y * velocity.Y != 0.0)
    {
        return chance;
    }

    return static_cast<float>(chance + 50.0);
}

auto MCTurret::LineOfFire(MCGameObject& target) -> int
{
    int32_t tileR;
    int32_t tileC;
    int32_t cellR;
    int32_t cellC;
    MCScenarioMap::WorldToMapPos(target.GetPosition(), tileR, tileC, cellR, cellC);

    if (Alignment != 1 && Alignment != -1)
    {
        // Port fix: for a neutral turret the original uses the tile column it just computed as the visibility bits'
        // pointer, and crashes.
        return 0;
    }

    const std::shared_ptr<MCTerrain> terrain = Terrain();
    MCByteFlag& visibleBits = Alignment == 1 ? *terrain->ISVisibleBits : *terrain->ClanVisibleBits;
    const int32_t numVisible =
        CountVisibleCorners(visibleBits, static_cast<uint32_t>(tileR), static_cast<uint32_t>(tileC), 0);
    const int lineClear = GameMap().LineOfFire(Position, target.GetPosition());

    if (numVisible == 0)
    {
        return 0;
    }

    MCVector3D targetPosition = target.GetPosition();
    return DistanceFrom(targetPosition) < MaxVisualRadius && lineClear != 0 ? 1 : 0;
}

auto MCTurret::RecordWeaponFireTime() -> void
{
    LastFireTime = ScenarioTime;
}

auto MCTurret::StartWeaponRecycle() -> void
{
    ReadyTime = MasterComponentList[TypeAs<MCTurretType>().WeaponType].RecycleTime + ScenarioTime;
}

auto MCTurret::ClearWeaponFireChunks(int32_t which) -> int32_t
{
    const auto numChunks = static_cast<int32_t>(WeaponFireChunks[which].size());
    WeaponFireChunks[which].clear();
    return numChunks;
}

auto MCTurret::AddWeaponFireChunk(int32_t which, MCWeaponFireChunk& chunk) -> int32_t
{
    chunk.Pack();
    WeaponFireChunks[which].push_back(chunk.Data);
    return static_cast<int32_t>(WeaponFireChunks[which].size());
}

auto MCTurret::AddWeaponFireChunks(int32_t which, std::span<const uint32_t> packedChunks) -> int32_t
{
    for (const uint32_t packed : packedChunks)
    {
        WeaponFireChunks[which].push_back(packed);
        // Unpacked into a scratch chunk (the result isn't kept).
        MCWeaponFireChunk chunk;
        chunk.Init();
        chunk.Data = packed;
        chunk.Unpack(*this);
    }

    return static_cast<int32_t>(WeaponFireChunks[which].size());
}

auto MCTurret::GrabWeaponFireChunks(int32_t which, std::span<uint32_t> packedChunks) -> int32_t
{
    const std::vector<uint32_t>& chunks = WeaponFireChunks[which];
    const size_t numGrabbed = std::min(chunks.size(), packedChunks.size());
    std::copy_n(chunks.begin(), numGrabbed, packedChunks.begin());
    return static_cast<int32_t>(numGrabbed);
}

auto MCTurret::UpdateWeaponFireChunks(int32_t which) -> int32_t
{
    // Replays the weapon fire the server sent.
    for (size_t i = 0; i < WeaponFireChunks[which].size(); i++)
    {
        MCWeaponFireChunk chunk;
        chunk.Init();
        chunk.Data = WeaponFireChunks[which][i];
        chunk.Unpack(*this);

        switch (chunk.TargetType)
        {
            case 0:
            case 1:
            case 2:
            {
                std::shared_ptr<MCBaseObject> chunkTarget;
                std::string_view missing;

                if (chunk.TargetType == 0)
                {
                    chunkTarget = MultiPlayer()->RosterMover(chunk.TargetId);
                    missing = " Turret.updateWeaponFireChunks: NULL Mover Target (save wfchunk.dbg file) ";
                }
                else
                {
                    chunkTarget = ObjectList()->FindObjectFromPart(chunk.TargetId);
                    missing = chunk.TargetType == 1
                                  ? " Turret.updateWeaponFireChunks: NULL Terrain Target (save wfchunk.dbg file) "
                                  : " Turret.updateWeaponFireChunks: NULL Special Target (save wfchunk.dbg file) ";
                }

                if (chunkTarget == nullptr)
                {
                    DebugWeaponFireChunk(chunk, std::nullopt, SharedAs<MCGameObject>());
                    Assert(false, 0, missing);
                }

                // The chunk came from the server, so a target that isn't there, or a hit on an armor location the
                // target doesn't have, is skipped (the original went on with them).
                const auto gameTarget = std::dynamic_pointer_cast<MCGameObject>(chunkTarget);
                const auto moverTarget = std::dynamic_pointer_cast<MCMover>(chunkTarget);

                if (gameTarget == nullptr ||
                    (chunk.Hit != 0 && moverTarget != nullptr && !moverTarget->IsHitLocation(chunk.HitLocation)))
                {
                    continue;
                }

                HandleWeaponFire(0, gameTarget, std::nullopt, chunk.Hit, ChunkEntryAngles[chunk.EntryAngle],
                                 chunk.NumMissiles, chunk.NumMissilesPastAms, chunk.NumAntiMissileShots,
                                 chunk.HitLocation);
                break;
            }

            case 3:
            {
                // A point on the ground: the middle of the target cell, at height 0.
                const float halfSide = WorldUnitsMapSide * 0.5f;
                MCVector3D point;
                point.X = (static_cast<float>(chunk.TargetCell[1]) + 0.5f) * MetersPerCell() - halfSide;
                point.Y =
                    (halfSide - static_cast<float>(chunk.TargetCell[0]) * MetersPerCell()) - MetersPerCell() * 0.5f;
                point.Z = 0.0f;
                HandleWeaponFire(0, nullptr, point, chunk.Hit, 0.0f, 0, 0, 0, 0);
                break;
            }

            default:
            {
                break;
            }
        }
    }

    WeaponFireChunks[which].clear();
    return 0;
}

auto MCTurret::GetPositionFromHS(uint32_t nodeId) -> MCVector3D
{
    auto& type = TypeAs<MCTurretType>();
    float x = Position.X;
    double y = Position.Y;
    float z = Position.Z;

    if (type.FireOffsetX != 0 || type.FireOffsetY != 0 || type.CenterOffsetX != 0 || type.CenterOffsetY != 0)
    {
        float muzzleX = 0.0f;
        double muzzleY = 0.0;
        float muzzleZ = 0.0f;

        if (nodeId != 0xffffffff)
        {
            const double angle = (static_cast<double>(TurretRotation) + 45.0) * MCMoverMath::DegreesToRadians;
            muzzleX = static_cast<float>(std::sin(angle) * type.FireOffsetX);
            muzzleY = std::cos(angle) * type.FireOffsetX;
            muzzleZ = static_cast<float>(type.FireOffsetY);
        }

        x = static_cast<float>(type.CenterOffsetX) + x + muzzleX;
        y = y - (static_cast<double>(type.CenterOffsetY) + muzzleY);
        z = z + muzzleZ;
    }

    return MCVector3D(x, static_cast<float>(y), z);
}

auto MCTurret::FireWeapon(std::shared_ptr<MCGameObject> target) -> void
{
    if (target == nullptr)
    {
        return;
    }

    // A camera drone can't be shot for two seconds after launch.
    if (target->ObjectClass == MCObjectClass::CameraDrone &&
        ScenarioTime < std::static_pointer_cast<MCCameraDrone>(target)->LaunchTime + 2.0)
    {
        return;
    }

    auto& type = TypeAs<MCTurretType>();
    const MCMasterComponent& weapon = MasterComponentList[type.WeaponType];
    // Indirect fire needs no clear line.
    const bool indirect = weapon.MissileType == 2 || weapon.MissileType == 1 || weapon.MissileType == 3;

    if (!indirect && LineOfFire(*target) == 0)
    {
        return;
    }

    const float entryAngle = target->RelFacingTo(Position, -1);
    const int isStreak = weapon.WeaponFlags & 1;
    int32_t range;
    const auto hitChance = static_cast<int32_t>(CalcAttackChance(target, range));

    if (static_cast<double>(hitChance) == -1.0)
    {
        return;
    }

    const int32_t hitRoll = RandomNumber(100);
    std::shared_ptr<MCMechWarrior> targetPilot;

    if (IsMoverClass(target->ObjectClass))
    {
        targetPilot = target->GetPilot();
        targetPilot->UpdateAttackerStatus(static_cast<uint32_t>(PartId), ScenarioTime);
    }

    StartWeaponRecycle();

    if (hitRoll < hitChance)
    {
        RecordWeaponFireTime();

        if (weapon.Form == MCComponentForm::WeaponMissile)
        {
            // Missiles: a streak fires them all, anything else about half; anti-missile systems take some out.
            const int32_t rackSize = weapon.NumMissiles;
            int32_t missiles = rackSize;

            if (isStreak == 0)
            {
                missiles = static_cast<int32_t>(rackSize * 0.5 + 0.5);

                if (missiles < 1)
                {
                    missiles = 1;
                }

                if (rackSize < missiles)
                {
                    missiles = rackSize;
                }
            }

            int32_t antiMissileShots = 0;
            const int32_t missilesLeft = target->FireAntiMissileSystem(missiles, antiMissileShots);

            if (0 < antiMissileShots)
            {
                target->ReduceAntiMissileAmmo(antiMissileShots);
            }

            if (0 < missilesLeft)
            {
                std::shared_ptr<MCGameObject> fx = CreateWeaponFX(weapon);
                const int32_t hitLocation = target->CalcHitLocation(GetPosition(), type.WeaponType, 0, 1);
                Assert(hitLocation != -2, 0, " Turret.FireWeapon: Bad Hit Location ");
                const int32_t targetHotSpot = TargetHotSpotOf(target, hitLocation);
                MCWeaponShotInfo shot;
                shot.Init(SharedAs<MCGameObject>(), type.WeaponType, weapon.Damage * static_cast<float>(missilesLeft),
                          hitLocation, entryAngle);

                if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
                {
                    SendFireChunk(*this, target, *target, std::nullopt, 1, entryAngle, missiles, missilesLeft,
                                  antiMissileShots, hitLocation);
                }

                LaunchWeaponFX(*this, std::move(fx), target, std::nullopt, shot, targetHotSpot);
            }
        }
        else
        {
            const int32_t hitLocation = target->CalcHitLocation(GetPosition(), type.WeaponType, 0, 1);
            Assert(hitLocation != -2, 0, " Turret.FireWeapon: Bad Hit Location 2 ");
            MCWeaponShotInfo shot;
            shot.Init(SharedAs<MCGameObject>(), type.WeaponType, weapon.Damage, hitLocation, entryAngle);

            if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
            {
                SendFireChunk(*this, target, *target, std::nullopt, 1, entryAngle, 0, 0, 0, hitLocation);
            }

            std::shared_ptr<MCGameObject> fx = CreateWeaponFX(weapon);
            LaunchWeaponFX(*this, std::move(fx), target, std::nullopt, shot, TargetHotSpotOf(target, hitLocation));
        }
    }
    else if (isStreak == 0)
    {
        // A miss (a streak doesn't fire without a lock): the shot lands up to 25 meters off.
        RecordWeaponFireTime();

        if (weapon.Form == MCComponentForm::WeaponMissile)
        {
            const int32_t rackSize = weapon.NumMissiles;
            int32_t missiles = static_cast<int32_t>(rackSize * 0.5 + 0.5);

            if (missiles < 1)
            {
                missiles = 1;
            }

            if (rackSize < missiles)
            {
                missiles = rackSize;
            }

            if (0 < missiles)
            {
                std::shared_ptr<MCGameObject> fx = CreateWeaponFX(weapon);
                MCWeaponShotInfo shot;
                shot.Init(SharedAs<MCGameObject>(), type.WeaponType, weapon.Damage * static_cast<float>(missiles), -1,
                          entryAngle);
                MCVector3D landing = ScatterPoint(25.0f, 1);
                const MCVector3D targetPosition = target->GetPosition();
                landing.X += targetPosition.X;
                landing.Y += targetPosition.Y;
                landing.Z += targetPosition.Z;

                if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
                {
                    SendFireChunk(*this, nullptr, *target, landing, 0, 0.0f, missiles, 0, 0, 0);
                }

                LaunchWeaponFX(*this, std::move(fx), nullptr, landing, shot, 0);
            }
        }
        else
        {
            MCWeaponShotInfo shot;
            shot.Init(SharedAs<MCGameObject>(), type.WeaponType, weapon.Damage, -1, entryAngle);
            std::shared_ptr<MCGameObject> fx = CreateWeaponFX(weapon);
            MCVector3D landing(25.0f, 25.0f, 0.0f);

            // Original behaviour (OB-012): the chunk goes out before the point is scattered and moved to the target,
            // so other players see the shot land at (25, 25, 0).
            if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
            {
                SendFireChunk(*this, nullptr, *target, landing, 0, 0.0f, 0, 0, 0, 0);
            }

            landing = ScatterPoint(25.0f, 0);
            const MCVector3D targetPosition = target->GetPosition();
            landing.X += targetPosition.X;
            landing.Y += targetPosition.Y;
            landing.Z += targetPosition.Z;
            LaunchWeaponFX(*this, std::move(fx), nullptr, landing, shot, 0);
        }
    }

    if (targetPilot != nullptr)
    {
        targetPilot->TriggerAlarm(MCPilotAlarmType::TargetOfWeaponFire, static_cast<uint32_t>(PartId));
    }

    RevealFiring(*this, 1);
}

auto MCTurret::HandleWeaponFire(int32_t, std::shared_ptr<MCGameObject> target, std::optional<MCVector3D> targetPoint,
                                int hit, float entryAngle, int32_t numMissiles, int32_t numHits,
                                int32_t numAntiMissiles, int32_t hitLocation) -> int32_t
{
    auto& type = TypeAs<MCTurretType>();
    const int32_t masterId = type.WeaponType;
    const MCMasterComponent& weapon = MasterComponentList[masterId];
    const int isStreak = weapon.WeaponFlags & 1;
    StartWeaponRecycle();

    if (hit != 0)
    {
        if (weapon.Form == MCComponentForm::WeaponMissile)
        {
            if (0 < numAntiMissiles)
            {
                target->ReduceAntiMissileAmmo(numAntiMissiles);
            }

            if (0 < numHits)
            {
                std::shared_ptr<MCGameObject> fx = CreateWeaponFX(weapon);
                Assert(hitLocation != -2, 0, " Turret.handleWeaponFire: Bad Hit Location ");
                const int32_t targetHotSpot = TargetHotSpotOf(target, hitLocation);
                MCWeaponShotInfo shot;
                shot.Init(SharedAs<MCGameObject>(), masterId, weapon.Damage * static_cast<float>(numHits), hitLocation,
                          entryAngle);
                LaunchWeaponFX(*this, std::move(fx), target, targetPoint, shot, targetHotSpot);
            }
        }
        else
        {
            MCWeaponShotInfo shot;
            shot.Init(SharedAs<MCGameObject>(), masterId, weapon.Damage, hitLocation, entryAngle);
            std::shared_ptr<MCGameObject> fx = CreateWeaponFX(weapon);
            LaunchWeaponFX(*this, std::move(fx), target, targetPoint, shot, TargetHotSpotOf(target, hitLocation));
        }
    }
    else if (isStreak == 0 && (weapon.Form != MCComponentForm::WeaponMissile || 0 < numHits))
    {
        // A miss lands up to 25 meters off a target, 5 off a point.
        std::shared_ptr<MCGameObject> fx;
        MCWeaponShotInfo shot;

        if (weapon.Form == MCComponentForm::WeaponMissile)
        {
            fx = CreateWeaponFX(weapon);
            shot.Init(SharedAs<MCGameObject>(), masterId, weapon.Damage * static_cast<float>(numMissiles), -1,
                      entryAngle);
        }
        else
        {
            shot.Init(SharedAs<MCGameObject>(), masterId, weapon.Damage, -1, entryAngle);
            fx = CreateWeaponFX(weapon);
        }

        MCVector3D landing = ScatterPoint(target != nullptr ? 25.0f : 5.0f, 0);
        const MCVector3D base = target != nullptr ? target->GetPosition() : *targetPoint;
        landing.X += base.X;
        landing.Y += base.Y;
        landing.Z += base.Z;
        LaunchWeaponFX(*this, std::move(fx), nullptr, landing, shot, 0);
    }

    if (target != nullptr && IsMoverClass(target->ObjectClass))
    {
        std::shared_ptr<MCMechWarrior> targetPilot = target->GetPilot();
        targetPilot->UpdateAttackerStatus(static_cast<uint32_t>(PartId), ScenarioTime);
        targetPilot->TriggerAlarm(MCPilotAlarmType::TargetOfWeaponFire, static_cast<uint32_t>(PartId));
    }

    RevealFiring(*this, 0);
    return 0;
}

auto MCTurret::SetAlignment(int32_t align) -> void
{
    MCBigGameObject::SetAlignment(align);
    Target.reset();
}

auto MCTurret::HandleEvent(MCObjectEvent& event) -> int32_t
{
    if (event.Type == 0)
    {
        switch (event.Id)
        {
            case 0x1c:
                Selected = 1;
                break;
            case 0x1d:
                Selected = 0;
                break;
        }
    }

    return 0;
}

auto MCTurret::LightOnFire(float timeToBurn) -> void
{
    auto& type = TypeAs<MCTurretType>();

    if (type.BlownEffectId == 0xffffffff)
    {
        // Nothing to burn: a point of damage instead.
        MCWeaponShotInfo shot;
        shot.Init(nullptr, -1, 1.0f, 0, 0.0f);

        if (MultiPlayer() == nullptr)
        {
            HandleWeaponHit(shot, 0);
        }
        else if (MultiPlayer()->IsServer != 0)
        {
            HandleWeaponHit(shot, 1);
        }

        return;
    }

    if (FireObject == nullptr)
    {
        std::shared_ptr<MCGameObject> newFire = CreateObject(static_cast<int32_t>(type.BlownEffectId));

        if (newFire != nullptr)
        {
            newFire->SetPosition(Position);

            if (newFire->ObjectClass == MCObjectClass::Fire)
            {
                FireObject.Light(std::static_pointer_cast<MCFire>(newFire));
                FireObject->SetPotentialContact(3);
                FireObject->BurningObject = SharedAs<MCGameObject>();
                FireObject->SetTonnage(40.0f);
            }
            else
            {
                DestroyObject(*newFire);
            }
        }
    }

    if (FireObject != nullptr)
    {
        FireObject->AddTimeLeftToBurn(timeToBurn);
        OnFire = true;
    }
}

auto MCTurret::IsRevealed() -> int
{
    MCByteFlag& visibleBits = Terrain()->HomeVisibleBits();
    uint32_t row;
    uint32_t col;
    VertexRowCol(*this, row, col);
    return CountVisibleCorners(visibleBits, row, col, 1) != 0 ? 1 : 0;
}

auto MCTurret::Render() -> void
{
    if (JustCreated != 0)
    {
        return;
    }

    if (Appearance != nullptr)
    {
        Appearance->Visible = IsVisible();
        Appearance->Update();
    }

    if (GetContactType(HomeTeam()->Id) == 2)
    {
        // A sensor contact: a blip sized by tonnage.
        RenderSensorBlip(SensorBlipShape(GetTonnage()));
    }

    if (WindowsVisible != Turn)
    {
        return;
    }

    // Drawn when the home team sees all corners of its vertex square (a fixed turret, or the pop-up of object type
    // 0x2f2, when it sees any). The original also handed the appearance a haze table for the seen corners, which
    // neither appearance reads.
    uint32_t row;
    uint32_t col;
    VertexRowCol(*this, row, col);
    MCByteFlag& visibleBits = Terrain()->HomeVisibleBits();
    const int32_t numVisible = CountVisibleCorners(visibleBits, row, col, 0);
    const bool anyCornerPopUp = numVisible != 0 && FixedTurret == 0 && ObjType->ObjTypeNum == 0x2f2;

    if ((numVisible != 0 && FixedTurret != 0) || (numVisible == 4 && FixedTurret == 0) || anyCornerPopUp)
    {
        Appearance->Render(0);

        if (FireObject != nullptr)
        {
            FireObject->Render();
        }
    }

    if (DrawExtents != 0)
    {
        // Debug: the extent radius as an ellipse.
        DrawExtentEllipse(Position, MCVector2D(ObjType->ExtentRadius, ObjType->ExtentRadius));
    }
}

auto MCTurret::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    int32_t result = MCGameObject::Init(objType);

    if (result != 0)
    {
        return result;
    }

    const uint32_t appearId = objType->AppearName;
    JustCreated = true;
    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(appearId);

    if (apprType == nullptr)
    {
        return static_cast<int32_t>(0xdcdc0007);
    }

    switch (appearId & 0xff000000)
    {
        case 0x5000000:
        {
            auto fixed = std::make_shared<MCGVAppearance>();
            MCGVAppearance& fixedAppearance = *fixed;
            Appearance = std::move(fixed);
            fixedAppearance.Init();

            if ((apprType->AppearanceNum & 0xff000000) != 0x5000000)
            {
                return -0x2fff6;
            }

            result = fixedAppearance.Init(apprType, WeakAs<MCGameObject>());

            if (result != 0)
            {
                return result;
            }

            WeaponDeployed = true;
            FixedTurret = true;
            break;
        }

        case 0x9000000:
        {
            auto popUp = std::make_shared<MCPUAppearance>();
            MCPUAppearance& popUpAppearance = *popUp;
            Appearance = std::move(popUp);
            popUpAppearance.Init();

            if ((apprType->AppearanceNum & 0xff000000) != 0x9000000)
            {
                return -0x2fff6;
            }

            result = popUpAppearance.Init(apprType, WeakAs<MCGameObject>());

            if (result != 0)
            {
                return result;
            }

            WeaponDeployed = false;
            FixedTurret = false;
            break;
        }

        default:
            return -0x2fff6;
    }

    auto& type = this->TypeAs<MCTurretType>();
    ObjectClass = MCObjectClass::Turret;
    Destroyed = false;
    Alignment = -1;
    ReadyTime = 0.0f;

    // The attack radius, when given, is the collision extent: whatever enters it is a candidate target.
    if (type.AttackRadius != 0.0)
    {
        type.ExtentRadius = type.AttackRadius;
    }

    if (0.0 < type.ExtentRadius)
    {
        CollisionsOn = 1;
    }

    SetPotentialContact(2);
    Tonnage = type.Tonnage;
    ExplDamage = type.ExplosionDamage;
    ExplRadius = type.ExplosionRadius;
    Name = LoadGameString(static_cast<uint32_t>(type.BuildingName), 0xfe);
    Smoke = nullptr;
    Target.reset();
    return 0;
}

auto MCTurret::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    const float newDamage = GetDamage() + shotInfo.Damage;
    SetDamage(newDamage);
    auto& type = TypeAs<MCTurretType>();

    if (newDamage < static_cast<float>(static_cast<int32_t>(type.DmgLevel)))
    {
        return 0;
    }

    // Destroyed: the wreck, its smoke, fire and explosion.
    Destroyed = true;

    if (FixedTurret == 0)
    {
        static_cast<MCPUAppearance*>(Appearance.get())->SetDestroyed();
    }
    else
    {
        static_cast<MCGVAppearance*>(Appearance.get())->SetTypeId(MCGVActorState::Destroyed);
    }

    if (Appearance != nullptr)
    {
        Appearance->Visible = OnScreen();
        Appearance->Update();
    }

    if (Smoke != nullptr)
    {
        Smoke->SetOwner(SharedAs<MCBaseObject>());
        Smoke->SetOwnerPosition(Position);
        Smoke->Update();
        SmokeTime = SmokeTime - FrameLength;

        if (SmokeTime <= -30.0)
        {
            Smoke.reset();
        }
    }

    CollisionsOn = 0;
    Status = 2;

    if (OnFire == 0)
    {
        if (type.BlownEffectId == 0xffffffff)
        {
            if (FireObject != nullptr)
            {
                FireObject->AddTimeLeftToBurn(2.0f);
            }
        }
        else
        {
            std::shared_ptr<MCGameObject> newFire = CreateObject(static_cast<int32_t>(type.BlownEffectId));

            if (newFire != nullptr)
            {
                newFire->SetPosition(Position);

                if (newFire->ObjectClass == MCObjectClass::Fire)
                {
                    FireObject.Light(std::static_pointer_cast<MCFire>(newFire));
                    FireObject->SetPotentialContact(3);
                    FireObject->BurningObject = SharedAs<MCGameObject>();
                    FireObject->SetTonnage(40.0f);
                    OnFire = true;
                }
                else
                {
                    AddToDefaultList(std::move(newFire));
                }
            }
        }

        // Port-only (HD shake): its end shakes the picture.
        type.CreateExplosion(Position, ExplDamage, ExplRadius, MCBlastShake::Shakes);
    }

    return 0;
}
