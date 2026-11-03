#include "stdafx.h"
#include "object/MCBullet.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCCraterManager.h"
#include "lib/MCFrameOfRef.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBulletType.h"
#include "object/MCExplosion.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectSystem.h"
#include "object/MCSmoke.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCArmAppearance.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>Radians to degrees, as MCX.EXE stores it.</summary>
    constexpr double RadiansToDegrees = 0x1.ca5dc1a6402aap+5;
} // namespace

MCBullet::MCBullet() = default;

MCBullet::~MCBullet() = default;

auto MCBullet::IsVisible() -> bool
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (camera == nullptr)
    {
        return false;
    }

    ScreenPos = ProjectToScreen(Position, *camera);

    if (Appearance != nullptr && Appearance->RecalcBounds(*camera) == 0)
    {
        return false;
    }

    WindowsVisible = Turn;
    return true;
}

auto MCBullet::Update() -> int32_t
{
    const auto& bulletType = TypeAs<MCBulletType>();

    if (JustCreated)
    {
        JustCreated = false;
        CollisionsOn = 0;

        if (LockObject(Owner) != nullptr)
        {
            Position = LockObject(Owner)->GetPositionFromHS(static_cast<uint32_t>(OwnerHotSpot));
        }

        if (bulletType.SoundEffectId != 0xffffffff)
        {
            SoundSystem()->PlayDigitalSample(bulletType.SoundEffectId, 1, *this, false, false);
        }

        BulletPosition = Position;
        ClosestDistanceSq = 1.0e8f;
        MuzzleFlashLeft = MCSceneLights::MuzzleFlashSeconds;
    }
    else
    {
        MuzzleFlashLeft = std::max(MuzzleFlashLeft - FrameLength, 0.0f);
    }

    std::shared_ptr<MCGameObject> shooter = LockObject(Owner);

    if (shooter != nullptr)
    {
        Position = shooter->GetPositionFromHS(static_cast<uint32_t>(OwnerHotSpot));
    }

    const bool visibleNow = IsVisible();

    if (Appearance != nullptr)
    {
        Appearance->Visible = visibleNow;
        Appearance->Update();
    }

    // Fly toward the target (following it if it moves); once the ground distance stops shrinking, we're there.
    MCVector3D from = BulletPosition;

    if (LockObject(Target) != nullptr)
    {
        // Port fix (OB-017): follow the hot spot the hit effect plays at (the original used ownerHotSpot).
        const uint32_t hotSpot = LockObject(Target)->ObjectClass == MCObjectClass::Turret
                                     ? 0xffffffff
                                     : static_cast<uint32_t>(TargetHotSpot);
        SetTargetPosition(LockObject(Target)->GetPositionFromHS(hotSpot));
    }

    // Port fix: the original leaves the destination uninitialised when there is no target position.
    const MCVector3D to = TargetPosition.value_or(MCVector3D(0.0f, 0.0f, 0.0f));
    const double step = static_cast<double>(bulletType.Velocity) * FrameLength;
    const double dxWide = static_cast<double>(to.X) - from.X;
    const auto dx = static_cast<float>(dxWide);
    const float dy = to.Y - from.Y;
    const float dz = to.Z - from.Z;
    const double dxSq = static_cast<double>(dx) * dx;
    const double dySq = static_cast<double>(dy) * dy;
    const double groundDistanceSq = dxSq + dySq;
    const bool arrived =
        Approach(ClosestDistanceSq, groundDistanceSq, bulletType.Velocity, FrameLength, LockObject(Target) != nullptr);
    int32_t result = arrived ? 0 : 1;

    MCVector3D velocity;
    double unitX = dxWide;
    velocity.Y = dy;
    velocity.Z = dz;
    const double length = std::sqrt(static_cast<double>(dz) * dz + dxSq + dySq);
    const auto lengthF = static_cast<float>(length);

    if (length != 0.0)
    {
        unitX = static_cast<double>(dx) / lengthF;
        velocity.Y = static_cast<float>(static_cast<double>(dy) / lengthF);
        velocity.Z = static_cast<float>(static_cast<double>(dz) / lengthF);
    }

    velocity.X = static_cast<float>(unitX * step);
    velocity.Y = static_cast<float>(velocity.Y * step);
    velocity.Z = static_cast<float>(velocity.Z * step);
    BulletPosition.X += velocity.X;
    BulletPosition.Y += velocity.Y;
    BulletPosition.Z += velocity.Z;

    if (Smoke != nullptr)
    {
        result = 1;
        Smoke->SetOwnerPosition(BulletPosition);
        Smoke->SetOwnerVelocity(velocity);
        Smoke->Update();

        if (arrived)
        {
            Smoke->StopSmoking();
            result = 0;
        }
    }

    if (Light != nullptr)
    {
        MCVector3D lightPos = BulletPosition;
        Light->SetPosition(lightPos);
        Light->Update();
    }

    // A mech facing the other way draws the bullet mirrored.
    DrawRotation = -150;

    // Port fix: the original reads the owner's class without checking it for null.
    if (shooter != nullptr && shooter->ObjectClass == MCObjectClass::BattleMech)
    {
        const MCFrameOfRef frame = shooter->GetFrame();
        float cosFacing = UnitX.Y * frame.I.Y + UnitX.X * frame.I.X + UnitX.Z * frame.I.Z;

        if (cosFacing < -1.0f)
        {
            cosFacing = -1.0f;
        }

        if (1.0f < cosFacing)
        {
            cosFacing = 1.0f;
        }

        double facing = AcosMatherr(static_cast<double>(cosFacing)) * RadiansToDegrees;

        if (frame.I.Y < 0.0f)
        {
            facing = -facing;
        }

        DrawRotation = std::abs(facing) <= 90.0 ? -150 : 150;
    }

    if (result != 0)
    {
        return result;
    }

    // Arrived: apply the shots (in multiplayer only the server does, and sends them on).
    if (LockObject(Target) != nullptr && (MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0))
    {
        const int sendChunks = MultiPlayer() != nullptr ? 1 : 0;

        for (MCWeaponShotInfo& shot : Shots)
        {
            LockObject(Target)->HandleWeaponHit(shot, sendChunks);
        }
    }

    std::shared_ptr<MCGameObject> effect = CreateObject(
        static_cast<int32_t>(LockObject(Target) == nullptr ? bulletType.BulletMissEffect : bulletType.BulletHitEffect));

    if (effect == nullptr)
    {
        return result;
    }

    if (LockObject(Target) != nullptr)
    {
        const uint32_t hotSpot = LockObject(Target)->ObjectClass == MCObjectClass::Turret
                                     ? 0xffffffff
                                     : static_cast<uint32_t>(TargetHotSpot);
        MCVector3D hitPos = LockObject(Target)->GetPositionFromHS(hotSpot);
        effect->SetPosition(hitPos);
    }
    else if (TargetPosition.has_value())
    {
        effect->SetPosition(*TargetPosition);
    }

    AddToDefaultList(std::move(effect));

    // A miss leaves a crater and sets off a live mine where it lands.
    if (LockObject(Target) == nullptr && TargetPosition.has_value())
    {
        CraterManager().AddCrater(6, *TargetPosition, 1);

        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        MCScenarioMap::WorldToMapPos(*TargetPosition, tileR, tileC, cellR, cellC);

        // Port fix: a miss can land off the map, where the original reads (and writes) outside it.
        if (!GameMap().OnMap(tileR, tileC))
        {
            return result;
        }

        MCMapTile& tile = GameMap().Map[GameMap().Width * tileR + tileC];

        if ((tile.Overlay & 0x1800) == 0x1000 || (tile.Overlay & 0x6000) == 0x4000)
        {
            CreateExplosion(MineExplosion, *TargetPosition, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
            tile.Overlay |= 0x1800;
            tile.Overlay |= 0x6000;
        }
    }

    return result;
}

auto MCBullet::Render() -> void
{
    const bool firstFrame = JustCreated;

    if (!firstFrame && Appearance != nullptr)
    {
        Appearance->Render(DrawRotation);
    }

    if (Smoke != nullptr && !firstFrame)
    {
        Smoke->Render();
    }

    if (Light != nullptr)
    {
        Light->Render();
    }

    // Port-only (HD lighting): the gun's flash, at its hot spot (Position follows it).
    if (!firstFrame)
    {
        MCSceneLights::AddMuzzleFlash(Position, MuzzleFlashLeft);
    }
}

auto MCBullet::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    JustCreated = true;

    if (std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(objType->AppearName);
        apprType != nullptr)
    {
        if ((apprType->AppearanceNum & 0xff000000) != 0x6000000)
        {
            return static_cast<int32_t>(0xdcdc0010);
        }

        Appearance = std::make_unique<MCArmAppearance>();
        Appearance->Init();

        if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
        {
            return result;
        }
    }

    const auto& bulletType = static_cast<MCBulletType&>(*objType);

    if (static_cast<int32_t>(bulletType.SmokeObjectId) != -1)
    {
        Smoke = CreateObjectAs<MCSmoke>(static_cast<int32_t>(bulletType.SmokeObjectId));
    }

    if (static_cast<int32_t>(bulletType.LightObjectId) != -1)
    {
        Light = CreateObject(static_cast<int32_t>(bulletType.LightObjectId));
    }

    ObjectClass = MCObjectClass::Bullet;
    return 0;
}

auto MCBullet::Approach(float& closestDistanceSq, double groundDistanceSq, float velocity, float frameLength,
                        bool followsTarget) -> bool
{
    // The target's gain the original's test allows at 15 fps, less this frame's: none at 15 fps or slower.
    const double slack = followsTarget ? static_cast<double>(velocity) *
                                             (static_cast<double>(PacedFrameLength) - static_cast<double>(frameLength))
                                       : 0.0;
    bool arrived = false;

    if (slack <= 0.0)
    {
        arrived = closestDistanceSq <= groundDistanceSq;
    }
    else
    {
        const double distance = std::sqrt(groundDistanceSq);
        const double step = static_cast<double>(velocity) * frameLength;
        arrived = distance <= step || std::sqrt(static_cast<double>(closestDistanceSq)) + slack <= distance;
    }

    if (!arrived && groundDistanceSq < closestDistanceSq)
    {
        closestDistanceSq = static_cast<float>(groundDistanceSq);
    }

    return arrived;
}

auto MCBullet::Connect(MCGameObject& source, MCVector3D targetPos, int32_t sourceHotSpot) -> void
{
    Owner = source.SharedAs<MCGameObject>();
    OwnerHotSpot = sourceHotSpot;
    SetTargetPosition(targetPos);
}
