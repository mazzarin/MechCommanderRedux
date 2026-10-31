#include "stdafx.h"
#include "object/MCProjectileLaser.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCCraterManager.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCPolygonElement.h"
#include "gui/MCGuiSystem.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCExplosion.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectSystem.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCSmoke.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCArmAppearance.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>Radians to degrees, as MCX.EXE stores it.</summary>
    constexpr double RadiansToDegrees = 0x1.ca5dc1a6402aap+5;

    /// <summary>
    /// The camera's isometric projection of <paramref name="point"/>: the offset from the eye, turned by the view angle
    /// and dropped by its height.
    /// </summary>
    void ProjectPoint(const MCVector3D& point, int32_t& screenX, int32_t& screenY)
    {
        const float dx = point.X - Eye()->Position.X;
        const float dy = point.Y - Eye()->Position.Y;
        const float dz = point.Z - Eye()->Position.Z;
        const float x = dy * Eye()->CosAngle + dx * Eye()->CosAngle + Eye()->HalfWidth;
        const float y = ((dx * Eye()->SinAngle + Eye()->HalfHeight) - dy * Eye()->SinAngle) - dz;
        screenX = static_cast<int32_t>(x);
        screenY = static_cast<int32_t>(y);
    }

    /// <summary>A screen vertex in palette colour <paramref name="color"/> (u, v and w zero).</summary>
    MCScreenVertex ColorVertex(const MCVector3D& point, uint8_t color)
    {
        MCScreenVertex vertex;
        ProjectPoint(point, vertex.X, vertex.Y);
        vertex.C = static_cast<MCFixed16>(color) << 16;
        vertex.U = 0;
        vertex.V = 0;
        vertex.W = 0;
        return vertex;
    }

    /// <summary>The hot spot a shot at <paramref name="target"/> aims for: none for a turret.</summary>
    uint32_t AimHotSpot(const MCGameObject& target, int32_t targetHotSpot)
    {
        return target.ObjectClass == MCObjectClass::Turret ? 0xffffffff : static_cast<uint32_t>(targetHotSpot);
    }
} // namespace

MCProjectileLaser::MCProjectileLaser()
{
    Frame.ResetToWorldFrame();
    Frame.I = UnitX;
    Frame.J = UnitY;
    Frame.K = UnitZ;
}

MCProjectileLaser::~MCProjectileLaser() = default;

auto MCProjectileLaser::IsVisible() -> bool
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (camera == nullptr)
    {
        return false;
    }

    ScreenPos = ProjectToScreen(Position, *camera);

    if (Appearance != nullptr && Appearance->RecalcBounds(*camera) != 0)
    {
        WindowsVisible = Turn;
        return true;
    }

    return false;
}

auto MCProjectileLaser::Update() -> int32_t
{
    const auto& laserType = TypeAs<MCProjectileLaserType>();

    if (JustCreated)
    {
        // Start at the owner's hot spot, facing its way, with the head one projectile length toward the target.
        std::shared_ptr<MCGameObject> shooter = LockObject(Owner);
        JustCreated = false;
        CollisionsOn = 0;

        if (shooter != nullptr)
        {
            Position = shooter->GetPositionFromHS(static_cast<uint32_t>(OwnerHotSpot));
            Frame = shooter->GetFrame();
        }

        if (laserType.SoundEffectId != 0xffffffff)
        {
            SoundSystem()->PlayDigitalSample(laserType.SoundEffectId, 1, *this, false, false);
        }

        HeadPosition = Position;
        ClosestDistanceSq = 1.0e8f;

        if (LockObject(Target) != nullptr)
        {
            // Port fix (OB-017): the original aimed at the target's hot spot numbered like the owner's (ownerHotSpot).
            SetTargetPosition(LockObject(Target)->GetPositionFromHS(AimHotSpot(*LockObject(Target), TargetHotSpot)));
        }

        // Port fix: the original reads an unset destination when there is no target position.
        const MCVector3D to = TargetPosition.value_or(HeadPosition);
        float dx = to.X - HeadPosition.X;
        float dy = to.Y - HeadPosition.Y;
        float dz = to.Z - HeadPosition.Z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (length != 0.0f)
        {
            dx /= length;
            dy /= length;
            dz /= length;
        }

        HeadPosition.X = dx * laserType.ProjectileLength + HeadPosition.X;
        HeadPosition.Y = dy * laserType.ProjectileLength + HeadPosition.Y;
        HeadPosition.Z = dz * laserType.ProjectileLength + HeadPosition.Z;
    }

    std::shared_ptr<MCGameObject> shooter = LockObject(Owner);

    if (shooter != nullptr)
    {
        Position = shooter->GetPositionFromHS(static_cast<uint32_t>(OwnerHotSpot));
    }

    bool arrived = false;
    const bool visibleNow = IsVisible();

    if (Appearance != nullptr)
    {
        Appearance->Visible = visibleNow;
        Appearance->Update();
    }

    // A mech facing the other way draws the bolt mirrored.
    DrawRotation = -150;

    // Port fix: the original reads the owner's class without checking it for null.
    if (shooter != nullptr && shooter->ObjectClass == MCObjectClass::BattleMech)
    {
        MCFrameOfRef ownerFrame = shooter->GetFrame();
        const float cosFacing = UnitX.Y * ownerFrame.I.Y + UnitX.X * ownerFrame.I.X + UnitX.Z * ownerFrame.I.Z;
        float facing = static_cast<float>(MCFrameOfRef::MyAcos(cosFacing) * RadiansToDegrees);

        if (ownerFrame.I.Y < 0.0f)
        {
            facing = -facing;
        }

        DrawRotation = std::abs(facing) <= 90.0f ? -150 : 150;
    }

    // Move the head toward the target; once its ground distance stops shrinking, it has arrived.
    const MCVector3D from = HeadPosition;

    if (LockObject(Target) != nullptr)
    {
        // Port fix (OB-017): aim at the hot spot the hit effect plays at (the original used ownerHotSpot).
        SetTargetPosition(LockObject(Target)->GetPositionFromHS(AimHotSpot(*LockObject(Target), TargetHotSpot)));
    }

    const MCVector3D to = TargetPosition.value_or(from);
    const double step = static_cast<double>(laserType.Velocity) * FrameLength;
    const double dxWide = static_cast<double>(to.X) - from.X;
    const auto dx = static_cast<float>(dxWide);
    float dy = to.Y - from.Y;
    float dz = to.Z - from.Z;
    const double dxSq = static_cast<double>(dx) * dx;
    const double dySq = static_cast<double>(dy) * dy;
    const double groundDistanceSq = dxSq + dySq;

    if (ClosestDistanceSq <= groundDistanceSq)
    {
        arrived = true;
    }
    else
    {
        ClosestDistanceSq = static_cast<float>(groundDistanceSq);
    }

    const double travelLength = std::sqrt(static_cast<double>(dz) * dz + dxSq + dySq);
    const auto travelLengthF = static_cast<float>(travelLength);
    double unitX = dxWide;

    if (travelLength != 0.0)
    {
        unitX = static_cast<double>(dx) / travelLengthF;
        dy = static_cast<float>(static_cast<double>(dy) / travelLengthF);
        dz = static_cast<float>(static_cast<double>(dz) / travelLengthF);
    }

    const double moveX = unitX * step;
    const auto moveY = static_cast<float>(dy * step);
    const auto moveZ = static_cast<float>(dz * step);
    HeadPosition.X = static_cast<float>(moveX + HeadPosition.X);
    HeadPosition.Y = moveY + HeadPosition.Y;
    HeadPosition.Z = moveZ + HeadPosition.Z;

    // The shape: a bulge bulgeLength behind the head, bulgeWidth to each side, and the tail projectileLength back.
    const auto backX = static_cast<float>(-moveX);
    float backY = -moveY;
    float backZ = -moveZ;
    const double backLength = std::sqrt(static_cast<double>(backZ) * backZ + static_cast<double>(backY) * backY +
                                        static_cast<double>(backX) * backX);
    const auto backLengthF = static_cast<float>(backLength);
    double backXWide = -moveX;

    if (backLength != 0.0)
    {
        backXWide = static_cast<double>(backX) / backLengthF;
        backY = static_cast<float>(static_cast<double>(backY) / backLengthF);
        backZ = static_cast<float>(static_cast<double>(backZ) / backLengthF);
    }

    const float bulgeLength = laserType.BulgeLength;
    const float bulgeWidth = laserType.BulgeWidth;
    BulgeSide1.Y = backY * bulgeLength + HeadPosition.Y;
    BulgeSide1.X = static_cast<float>(backXWide * bulgeLength + HeadPosition.X);
    BulgeSide1.Z = backZ * bulgeLength + HeadPosition.Z;
    BulgeSide2 = BulgeSide1;
    BulgeCenter = BulgeSide1;
    const double backYWidth = static_cast<double>(backY) * bulgeWidth;
    const auto backXWidth = static_cast<float>(backXWide * bulgeWidth);
    BulgeSide2.X = static_cast<float>(backYWidth + BulgeSide2.X);
    BulgeSide2.Y -= backXWidth;
    BulgeSide1.X = static_cast<float>(BulgeSide1.X - backYWidth);
    BulgeSide1.Y += backXWidth;
    TailPosition.Y = backY * laserType.ProjectileLength + HeadPosition.Y;
    TailPosition.X = static_cast<float>(backXWide * laserType.ProjectileLength + HeadPosition.X);
    TailPosition.Z = backZ * laserType.ProjectileLength + HeadPosition.Z;

    // With a smoke trail, arrival is instead the tail coming within closeDistance (plus a step) of the target.
    if (Smoke != nullptr)
    {
        const MCVector3D tail = TailPosition;
        arrived = false;

        if (LockObject(Target) != nullptr)
        {
            // Port fix (OB-017): see above.
            SetTargetPosition(LockObject(Target)->GetPositionFromHS(AimHotSpot(*LockObject(Target), TargetHotSpot)));
        }

        const MCVector3D smokeTo = TargetPosition.value_or(tail);
        const float smokeStep = laserType.Velocity * FrameLength;
        float sx = smokeTo.X - tail.X;
        float sy = smokeTo.Y - tail.Y;
        float sz = smokeTo.Z - tail.Z;
        const double reach = static_cast<double>(smokeStep) + laserType.CloseDistance;
        const float sySq = sy * sy;
        const float sxSq = sx * sx;
        const double groundSq = static_cast<double>(sx) * sx + sySq;

        if (groundSq <= reach * reach)
        {
            Smoke->StopSmoking();
            arrived = true;
        }

        const double smokeLength = std::sqrt(static_cast<double>(sz) * sz + sxSq + sySq);

        if (smokeLength != 0.0)
        {
            sx = static_cast<float>(sx / smokeLength);
            sy = static_cast<float>(sy / smokeLength);
            sz = static_cast<float>(sz / smokeLength);
        }

        MCVector3D smokeVelocity;
        smokeVelocity.X = sx * smokeStep;
        smokeVelocity.Y = sy * smokeStep;
        smokeVelocity.Z = sz * smokeStep;
        SmokeDisplacement.X += smokeVelocity.X;
        SmokeDisplacement.Y += smokeVelocity.Y;
        SmokeDisplacement.Z += smokeVelocity.Z;
        Smoke->SetOwnerPosition(TailPosition);
        Smoke->SetOwnerVelocity(smokeVelocity);
        Smoke->Update();
    }

    if (Light != nullptr)
    {
        MCVector3D lightPos = TailPosition;
        Light->SetPosition(lightPos);
        Light->Update();
    }

    if (!arrived)
    {
        return 1;
    }

    // Arrived: apply the shot (in multiplayer only the server does, and sends it on).
    if (LockObject(Target) != nullptr)
    {
        if (MultiPlayer() == nullptr)
        {
            LockObject(Target)->HandleWeaponHit(ShotInfo, 0);
        }
        else if (MultiPlayer()->IsServer != 0)
        {
            LockObject(Target)->HandleWeaponHit(ShotInfo, 1);
        }
    }

    std::shared_ptr<MCGameObject> effect = CreateObject(static_cast<int32_t>(
        LockObject(Target) == nullptr ? laserType.ProjectileMissEffect : laserType.ProjectileHitEffect));

    if (effect == nullptr)
    {
        return 0;
    }

    if (LockObject(Target) != nullptr)
    {
        MCVector3D hitPos = LockObject(Target)->GetPositionFromHS(AimHotSpot(*LockObject(Target), TargetHotSpot));
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
        CraterManager().AddCrater(7, *TargetPosition, 1);

        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        MCScenarioMap::WorldToMapPos(*TargetPosition, tileR, tileC, cellR, cellC);

        // Port fix: a miss can land off the map, where the original reads (and writes) outside it.
        if (!GameMap().OnMap(tileR, tileC))
        {
            return 0;
        }

        MCMapTile& tile = GameMap().Map[GameMap().Width * tileR + tileC];

        if ((tile.Overlay & 0x1800) == 0x1000 || (tile.Overlay & 0x6000) == 0x4000)
        {
            CreateExplosion(MineExplosion, *TargetPosition, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
            tile.Overlay |= 0x1800;
            tile.Overlay |= 0x6000;
        }
    }

    return 0;
}

auto MCProjectileLaser::Render() -> void
{
    if (GamePaused != 0)
    {
        OnScreen();
    }

    if (JustCreated)
    {
        return;
    }

    if (Appearance != nullptr)
    {
        Appearance->Render(DrawRotation);
    }

    // Two quads, head-bulge-tail-centre on each side, in the friendly or enemy colours.
    const auto& laserType = TypeAs<MCProjectileLaserType>();
    const bool enemy = LockObject(Owner) == nullptr || LockObject(Owner)->GetAlignment() == -1;
    const uint8_t headColor = enemy ? laserType.EColor[0] : laserType.FColor[0];
    // Original behaviour (OB-020): the other corners test "no owner" and then ask that null owner for its
    // alignment, so with an owner they always take the enemy colours (without one, MCX.EXE crashes).
    const std::array<uint8_t, 4>& cornerColors = laserType.EColor;

    MCPolyElementData side1;
    side1.NumVertices = 4;
    side1.Vertices[0] = ColorVertex(HeadPosition, headColor);
    side1.Vertices[1] = ColorVertex(BulgeSide1, cornerColors[1]);
    side1.Vertices[2] = ColorVertex(TailPosition, cornerColors[2]);
    side1.Vertices[3] = ColorVertex(BulgeCenter, cornerColors[3]);
    MCPolyElementData side2;
    side2.NumVertices = 4;
    side2.Vertices[0] = side1.Vertices[0];
    side2.Vertices[1] = ColorVertex(BulgeSide2, cornerColors[1]);
    side2.Vertices[2] = side1.Vertices[2];
    side2.Vertices[3] = side1.Vertices[3];

    const int32_t depth = -side1.Vertices[0].Y;
    ElementList().OpenGroup(depth, true);
    ElementList().Add(ElementList().Make<MCPolygonElement>(side1, depth));
    ElementList().Add(ElementList().Make<MCPolygonElement>(side2, depth));

    // Port-only (HD lighting): the bolt lights the ground and units along it, in its head's colour, as wide as its bulge.
    const MCScreenVertex& head = side1.Vertices[0];
    const MCScreenVertex& tail = side1.Vertices[2];
    const float bulge = std::hypot(static_cast<float>(side1.Vertices[1].X - side2.Vertices[1].X),
                                   static_cast<float>(side1.Vertices[1].Y - side2.Vertices[1].Y));
    MCSceneLights::AddBeam(static_cast<float>(head.X), static_cast<float>(head.Y), static_cast<float>(tail.X),
                           static_cast<float>(tail.Y), bulge * 0.5f, headColor);

    if (Smoke != nullptr)
    {
        Smoke->Render();
    }

    if (Light != nullptr)
    {
        Light->Render();
    }
}

auto MCProjectileLaser::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    const uint32_t appearId = objType->AppearName;
    JustCreated = true;

    if (appearId != 0)
    {
        std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(appearId);

        if (apprType == nullptr)
        {
            return -0x2d;
        }

        if ((apprType->AppearanceNum & 0xff000000) != 0x6000000)
        {
            return -0x38;
        }

        Appearance = std::make_unique<MCArmAppearance>();
        Appearance->Init();

        if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
        {
            return result;
        }
    }

    const auto& laserType = static_cast<MCProjectileLaserType&>(*objType);

    if (static_cast<int32_t>(laserType.SmokeObjectId) != -1)
    {
        Smoke = CreateObjectAs<MCSmoke>(static_cast<int32_t>(laserType.SmokeObjectId));
    }

    if (static_cast<int32_t>(laserType.LightObjectId) != -1)
    {
        Light = CreateObject(static_cast<int32_t>(laserType.LightObjectId));
    }

    ObjectClass = MCObjectClass::ProjectileLaser;
    return 0;
}

auto MCProjectileLaser::Connect(std::shared_ptr<MCGameObject> source, MCVector3D targetPos,
                                std::optional<MCWeaponShotInfo> shotInfo, int32_t sourceHotSpot) -> void
{
    Owner = source;
    OwnerHotSpot = sourceHotSpot;
    SetTargetPosition(targetPos);

    if (shotInfo.has_value())
    {
        const MCWeaponShotInfo shot = *shotInfo;
        ShotInfo.Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation, shot.EntryAngle);
    }
}
