#include "stdafx.h"
#include "object/MCCameraDrone.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCCameraDroneType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMoverMath.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCWeaponShotInfo.h"
#include "sprite/MCGVAppearance.h"
#include "terrain/MCTerrain.h"

int32_t NumCameraDrones = 0;

MCCameraDrone::MCCameraDrone()
{
    Frame.ResetToWorldFrame();
}

MCCameraDrone::~MCCameraDrone() = default;

auto MCCameraDrone::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCCameraDrone::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(objType->AppearName);

    if (apprType == nullptr)
    {
        return -0x2102fffd;
    }

    Appearance = std::make_shared<MCGVAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x5000000)
    {
        return -0x2323fff7;
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    ObjectClass = MCObjectClass::CameraDrone;
    const auto& type = static_cast<MCCameraDroneType&>(*objType);
    MaxVelocity = type.MaxVelocity;
    HitPoints = type.MaxDamage;
    CurCV = type.BrValue;
    MaxCV = type.BrValue;

    if (0 < type.BrValue)
    {
        SetPotentialContact(1);
    }

    // The drone starts turned 45 degrees from the world frame.
    Frame.ResetToWorldFrame();
    MCMoverMath::RotateAboutK(Frame, static_cast<float>(std::sin(MCMoverMath::HalfPi / 2.0)),
                              static_cast<float>(std::cos(MCMoverMath::HalfPi / 2.0)));
    CollisionsOn = 1;
    LaunchTime = ScenarioTime;
    return 0;
}

auto MCCameraDrone::Update() -> int32_t
{
    const float speed = MaxVelocity;

    if (IsDestroyed() != 0)
    {
        return 1;
    }

    // Fly straight at the target tile's corner.
    const float step = FrameLength * speed * WorldUnitsPerMeter;
    const float targetX = static_cast<float>(TargetTileCol) * MCTerrain::MetersPerVertex - WorldUnitsMapSide * 0.5f;
    const float targetY = WorldUnitsMapSide * 0.5f - static_cast<float>(TargetTileRow) * MCTerrain::MetersPerVertex;
    float dirX = targetX - Position.X;
    float dirY = targetY - Position.Y;
    float dirZ = 0.0f;
    const float distance = std::sqrt(dirX * dirX + dirY * dirY);

    if (distance != 0.0)
    {
        dirX = dirX / distance;
        dirY = dirY / distance;
        dirZ = 0.0f / distance;
    }

    Position.X = dirX * step + Position.X;
    Position.Y = dirY * step + Position.Y;
    Position.Z = dirZ * step + Position.Z;

    // Leaving the map ends the drone (clamped to the edge on its way out).
    const float halfSide = WorldUnitsMapSide * 0.5f;
    const float movedX = Position.X;

    if (movedX < -halfSide)
    {
        Position.X = -halfSide;
    }

    const float clampedX = Position.X;

    if (halfSide < clampedX)
    {
        Position.X = halfSide;
    }

    const float movedY = Position.Y;

    if (-halfSide > movedY)
    {
        Position.Y = -halfSide;
    }

    if (Position.Y > halfSide)
    {
        Position.Y = halfSide;
        return 0;
    }

    if (!(-halfSide <= movedY && halfSide >= clampedX && -halfSide <= movedX))
    {
        return 0;
    }

    int32_t tileR = 0;
    int32_t tileC = 0;
    MCScenarioMap::WorldToMapTilePos(Position, tileR, tileC);

    if (tileR < 0 || tileR >= GameMap().Height || tileC < 0 || tileC >= GameMap().Width)
    {
        return 0;
    }

    GameObjectMap().UpdateObject(*this);
    const uint8_t seenBy = Alignment == 1 ? 1 : 2;
    MCFrameOfRef lookFrame = GetFrame();
    Terrain()->MarkRadiusSeen(Position, lookFrame.J, 360.0f, Scenario()->MaxVisualRange * 0.5f, seenBy);

    // Within a tile of the target: on to the next one.
    if (std::abs(TargetTileCol - tileC) > 1 || std::abs(TargetTileRow - tileR) > 1)
    {
        return 1;
    }

    FindNextTargetTile();
    return 1;
}

auto MCCameraDrone::Render() -> void
{
    const int visibleNow = OnScreen();
    Appearance->Visible = visibleNow;
    Appearance->Update();

    if (visibleNow != 0)
    {
        WindowsVisible = Turn;
        Appearance->Render(-150);
    }
}

auto MCCameraDrone::HandleEvent(MCObjectEvent& event) -> int32_t
{
    if (event.Type == 0)
    {
        if (event.Id == 0x1c)
        {
            Selected = 1;
        }
        else if (event.Id == 0x1d)
        {
            Selected = 0;
        }
    }

    return 0;
}

auto MCCameraDrone::OnScreen() -> int
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (camera == nullptr)
    {
        return 0;
    }

    ScreenPos = ProjectToScreen(Position, *camera);

    const bool shown = Appearance->RecalcBounds(*camera) != 0;
    // Port-only (SPEC-first-use.md: the camera's surroundings): its packets read ahead in or near the view.
    Appearance->ReadAheadNear(*camera, shown);

    if (shown)
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

auto MCCameraDrone::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    if (IsDestroyed() == 0 && 0.0f < shotInfo.Damage)
    {
        BadGuy = LockObject(shotInfo.Attacker);
        HitPoints = static_cast<int32_t>(static_cast<float>(HitPoints) - shotInfo.Damage);

        if (HitPoints < 1)
        {
            ObjType->HandleDestruction(*this, 0);
            ObjType->CreateExplosion(Position, 0.0f, 0.0f);
            Appearance->SetTypeId(MCGVActorState::Destroyed);
        }
    }

    return 0;
}

auto MCCameraDrone::FindNextTargetTile() -> void
{
    MCMoverMath::RotateAboutK(Frame, static_cast<float>(std::sin(MCMoverMath::HalfPi)),
                              static_cast<float>(std::cos(MCMoverMath::HalfPi)));

    // An outward square spiral: each leg turns a quarter, and every other leg is one tile longer.
    SpiralDirection++;

    if (SpiralDirection > 3)
    {
        SpiralDirection = 0;
    }

    if (SpiralDirection % 2 != 0)
    {
        SpiralLength++;
    }

    const int32_t tileR = GetObjPosition()->TileR;
    const int32_t tileC = GetObjPosition()->TileC;

    switch (SpiralDirection)
    {
        case 0:
        {
            TargetTileCol = tileC;
            TargetTileRow = tileR - SpiralLength;
            break;
        }

        case 1:
        {
            TargetTileCol = tileC + SpiralLength;
            TargetTileRow = tileR;
            break;
        }

        case 2:
        {
            TargetTileCol = tileC;
            TargetTileRow = tileR + SpiralLength;
            break;
        }

        case 3:
        {
            TargetTileCol = tileC - SpiralLength;
            TargetTileRow = tileR;
            break;
        }

        default:
            break;
    }
}
