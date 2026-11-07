#include "stdafx.h"
#include "object/MCBuilding.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "lib/MCDice.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBuildingMarines.h"
#include "object/MCBuildingType.h"
#include "object/MCCollisionSystem.h"
#include "object/MCContactSystem.h"
#include "object/MCForces.h"
#include "object/MCMover.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectSystem.h"
#include "object/MCSensorSystem.h"
#include "object/MCTeam.h"
#include "object/MCVertexPlacement.h"
#include "object/MCWeaponShotInfo.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCVfxBuildingAppearance.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

MCBuilding::MCBuilding() = default;

MCBuilding::~MCBuilding()
{
    if (SensorSystem.lock() != nullptr)
    {
        SensorSystemManager().FreeSensor(*SensorSystem.lock());
    }
}

auto MCBuilding::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCBuilding::IsPrison() -> int
{
    return std::ranges::any_of(PrisonSlots,
                               [](const std::weak_ptr<MCMechWarrior>& slot) { return slot.lock() != nullptr; })
               ? 1
               : 0;
}

auto MCBuilding::SetTerrainPosition(MCVector2D& offset, MCVector2D& numbers) -> void
{
    PixelOffsetX = static_cast<int32_t>(offset.X);
    PixelOffsetY = static_cast<int32_t>(offset.Y);

    if (TileNum != 10)
    {
        const auto& type = TypeAs<MCBuildingType>();

        if (type.BasePixelOffsetX != 0)
        {
            PixelOffsetX = type.BasePixelOffsetX;
        }

        if (type.BasePixelOffsetY != 0)
        {
            PixelOffsetY = type.BasePixelOffsetY;
        }
    }

    VertexNumber = static_cast<int32_t>(numbers.X);
    BlockNumber = static_cast<int32_t>(numbers.Y);
}

auto MCBuilding::IsVisible() -> bool
{
    if (Eye() == nullptr || Eye()->Active == 0)
    {
        return false;
    }

    int visible = MCCamera::VertexProject(BlockNumber, VertexNumber, ScreenPos);

    if (Appearance != nullptr)
    {
        visible = Appearance->RecalcBounds(EyeRef());
    }

    if (visible == 0)
    {
        return false;
    }

    // Back on screen after a gap: the looping sound is started afresh.
    if (WindowsVisible < Turn - 2)
    {
        SoundHandle = 0xffffffff;
    }

    WindowsVisible = Turn;
    return true;
}

auto MCBuilding::Update() -> int32_t
{
    if (!JustCreated)
    {
        return 1;
    }

    // Set the building on its vertex. The type's collision offsets, when set, replace the pixel offsets.
    auto& type = TypeAs<MCBuildingType>();
    JustCreated = false;
    const int32_t offsetX = type.CollisionOffsetX != 0 ? type.CollisionOffsetX : PixelOffsetX;
    const int32_t offsetY = type.CollisionOffsetY != 0 ? type.CollisionOffsetY : PixelOffsetY;
    Position = PlaceOnVertex(Position, BlockNumber, VertexNumber, offsetX, offsetY);
    const MCVertexCell cell = MCVertexCell::Of(BlockNumber, VertexNumber);
    CellColumn = cell.Col;
    VertexWorldX = cell.WorldX();
    CellRow = cell.Row;
    VertexWorldY = cell.WorldY();
    CellElevation = cell.Elevation(" bldng MapTile Out of Bounds ");

    // No extent radius in the FIT: measure it from the appearance's bounds.
    if (type.ExtentRadius < 0.0)
    {
        Appearance->Visible = true;
        Appearance->Update();
        Appearance->CalcCollideBounds();
        const float dx = Appearance->UpperLeft.X - Appearance->LowerRight.X;
        const float dy = Appearance->UpperLeft.Y - Appearance->LowerRight.Y;
        const float radius = std::sqrt(dx * dx + dy * dy) / WorldUnitsPerMeter * 1.25f;

        if (static_cast<float>(CollisionSystem().GridRadius()) < radius)
        {
            Fatal(static_cast<int32_t>(std::floor(static_cast<double>(radius))), " Object extent radius TOO large ");
        }

        type.ExtentRadius = radius;
    }

    if (type.ExtentRadius != 0.0)
    {
        CollisionsOn = 1;
    }

    return 1;
}

auto MCBuilding::SetAlignment(int32_t align) -> void
{
    MCBigGameObject::SetAlignment(align);

    if (SensorSystem.lock() == nullptr)
    {
        return;
    }

    if (Alignment == -1)
    {
        SensorSystem.lock()->SetTeam(ClanTeam());
    }
    else if (Alignment == 1)
    {
        SensorSystem.lock()->SetTeam(InnerSphereTeam());
    }
    else if (Alignment == 0)
    {
        SensorSystem.lock()->SetTeam(AlliedTeam());
    }
}

auto MCBuilding::HandleEvent(MCObjectEvent& event) -> int32_t
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

auto MCBuilding::LightOnFire(float timeToBurn) -> void
{
    const auto& type = TypeAs<MCBuildingType>();

    if (type.BlownEffectId == 0xffffffff)
    {
        return;
    }

    if (FireObject == nullptr)
    {
        std::shared_ptr<MCGameObject> effect = CreateObject(static_cast<int32_t>(type.BlownEffectId));

        if (effect != nullptr)
        {
            effect->SetPosition(Position);

            if (effect->ObjectClass == MCObjectClass::Fire)
            {
                // Original behaviour (OB-154): nothing updates or draws this fire, so it burns (and the building
                // takes its burn damage) until the building is destroyed.
                std::shared_ptr<MCFire> fire = FireObject.Light(std::static_pointer_cast<MCFire>(effect));
                fire->SetPotentialContact(3);
                fire->BurningObject = SharedAs<MCGameObject>();
                fire->SetTonnage(40.0f);
            }
            else
            {
                DestroyObject(*effect);
            }
        }
    }

    if (FireObject != nullptr)
    {
        FireObject->AddTimeLeftToBurn(timeToBurn);
        Burning = true;
    }
}

auto MCBuilding::IsCaptureable() -> int
{
    if (MultiPlayer() == nullptr)
    {
        return Captureable && IsCaptured() == 0 && IsDestroyed() == 0 ? 1 : 0;
    }

    return Captureable && IsDestroyed() == 0 ? 1 : 0;
}

auto MCBuilding::IsRevealed() -> int
{
    return MCVertexCell::Of(BlockNumber, VertexNumber).AnyCornerVisible() ? 1 : 0;
}

auto MCBuilding::Render() -> void
{
    if (JustCreated)
    {
        return;
    }

    const bool visible = IsVisible();

    if (Appearance != nullptr)
    {
        Appearance->Visible = visible;
        Appearance->TileNum = TileNum;
        Appearance->Update();
    }

    // Burning: the type's burn damage every TimeToBurnDamage seconds.
    if (FireObject == nullptr)
    {
        Burning = false;
    }
    else
    {
        const double burnSum = static_cast<double>(FrameLength) + BurnTime;
        BurnTime = static_cast<float>(burnSum);
        const auto& type = TypeAs<MCBuildingType>();

        if (type.TimeToBurnDamage < burnSum)
        {
            BurnTime = 0.0f;
            MCWeaponShotInfo shot;
            shot.Init(nullptr, -1, type.BurnDamagePerTime, 0, 0.0f);

            if (MultiPlayer() == nullptr)
            {
                HandleWeaponHit(shot, 0);
            }
            else if (MultiPlayer()->IsServer != 0)
            {
                HandleWeaponHit(shot, 1);
            }
        }
    }

    if (GetContactType(HomeTeam()->Id) == 2)
    {
        // A sensor contact: a blip sized by tonnage.
        RenderSensorBlip(SensorBlipShape(GetTonnage()));
    }

    if (WindowsVisible != Turn)
    {
        if (SoundHandle != 0xffffffff)
        {
            SoundSystem()->StopDigitalSample(SoundHandle);
            SoundHandle = 0xffffffff;
        }

        return;
    }

    // Hazed by how many corners of its vertex square the home team sees; drawn when any is. (The original also reads
    // each corner's seen bit and drops it.)
    const int32_t numVisible = MCVertexCell{CellRow, CellColumn}.VisibleCorners();
    int32_t hazeLevel = 0;

    if (numVisible != 4 && Eye()->HazeLevel != 0x7fff)
    {
        hazeLevel = HazeLevelFor(numVisible);
    }

    Appearance->HazeLevel = hazeLevel;

    if (numVisible == 0 || JustCreated)
    {
        if (SoundHandle != 0xffffffff)
        {
            SoundSystem()->StopDigitalSample(SoundHandle);
            SoundHandle = 0xffffffff;
        }
    }
    else
    {
        Appearance->Render(0);
        const auto& type = TypeAs<MCBuildingType>();

        if (SoundHandle == 0xffffffff && type.NormalEffectId != 0xffffffff)
        {
            SoundHandle =
                static_cast<uint32_t>(SoundSystem()->PlayDigitalSample(type.NormalEffectId, 0, *this, true, false));
        }
    }

    if (DrawExtents)
    {
        // Debug: the extent radius as an ellipse.
        const float diameter = ObjType->ExtentRadius + ObjType->ExtentRadius;
        DrawExtentEllipse(Position, MCVector2D(Eye()->CosAngle * diameter, Eye()->SinAngle * diameter));
    }
}

auto MCBuilding::SetDamage(float newDamage) -> void
{
    Damage = newDamage;
    // Sixteen damage frames across the damage level.
    const auto& type = TypeAs<MCBuildingType>();
    int32_t frame =
        static_cast<int32_t>(newDamage / (static_cast<double>(static_cast<int32_t>(type.DmgLevel)) * 0.0625));

    if (frame > 15)
    {
        frame = 15;
    }

    Appearance->SetDamageLvl(static_cast<uint32_t>(frame));
}

auto MCBuilding::SetSensorData(std::shared_ptr<MCTeam> newTeam, float range, bool setTeam) -> void
{
    if (!(-1.0 < range))
    {
        return;
    }

    if (SensorSystem.lock() == nullptr)
    {
        SensorSystem = SensorSystemManager().NewSensor();

        if (SensorSystem.lock() == nullptr)
        {
            Fatal(0, " No RAM for Sensor System ");
        }
    }

    SensorSystem.lock()->Owner = SharedAs<MCGameObject>();

    if (setTeam)
    {
        SensorSystem.lock()->SetTeam(newTeam);
    }

    SensorSystem.lock()->SetRange(range);
}

auto MCBuilding::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    SetExists(1);
    JustCreated = true;
    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(objType->AppearName);

    if (apprType == nullptr)
    {
        return static_cast<int32_t>(0xdcdc0007);
    }

    Appearance = std::make_shared<MCVfxBuildingAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x7000000)
    {
        return static_cast<int32_t>(0xdcdc0009);
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    const auto& type = TypeAs<MCBuildingType>();
    ObjectClass = MCObjectClass::Building;
    SoundHandle = 0xffffffff;

    if (0.0 < type.ExtentRadius)
    {
        CollisionsOn = 1;
    }

    Tonnage = type.BaseTonnage;
    ExplRadius = type.ExplRad;
    ExplDamage = type.ExplDmg;
    MaxCV = type.BattleRating;
    CurCV = type.BattleRating;
    Name = LoadGameString(static_cast<uint32_t>(type.BuildingName), 0xfe);

    // Original behaviour (OB-016): a building with no team (TeamID -1) reads TeamTable[-1], the global before it in
    // MCX.EXE: homeTeam.
    Team = type.TeamId == -1 ? HomeTeam() : TeamById(type.TeamId);
    const float range = type.SensorRange;

    if (-1.0 < range)
    {
        switch (type.TeamId)
        {
            case 0:
            {
                SetSensorData(InnerSphereTeam(), range, false);
                SetAlignment(1);
                break;
            }

            case 1:
            {
                SetSensorData(ClanTeam(), range, false);
                SetAlignment(-1);
                break;
            }

            case 2:
            {
                SetSensorData(AlliedTeam(), range, false);
                SetAlignment(0);
                break;
            }

            default:
                break;
        }
    }

    return 0;
}

auto MCBuilding::CreateBuildingMarines() -> void
{
    // Somewhere within the extent radius of the building.
    LetOutBuildingMarines(*this, TypeAs<MCBuildingType>().NumMarines,
                          [this](MCMover& marine)
                          {
                              const float extentX = ObjType->ExtentRadius;
                              const float extentY = ObjType->ExtentRadius;
                              const float offsetX =
                                  static_cast<float>(RandomNumber(static_cast<int32_t>(extentX + extentX))) - extentX;
                              const float offsetY =
                                  static_cast<float>(RandomNumber(static_cast<int32_t>(extentY + extentY))) - extentY;
                              const float offsetZ = static_cast<float>(RandomNumber(0));
                              MCVector3D marinePosition;
                              marinePosition.X = offsetX + Position.X;
                              marinePosition.Y = offsetY + Position.Y;
                              marinePosition.Z = offsetZ + Position.Z;
                              marine.SetPosition(marinePosition);
                              GameObjectMap().AddObject(marine);
                              marine.BounceToAdjCell();
                              marine.BounceToAdjCell();
                          });
}

auto MCBuilding::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    if (IsDestroyed() != 0)
    {
        return 0;
    }

    float newDamage = GetDamage() + shotInfo.Damage;
    auto& type = TypeAs<MCBuildingType>();
    const auto destroyLevel = static_cast<float>(static_cast<int32_t>(type.DmgLevel));

    if (destroyLevel <= newDamage)
    {
        // Destroyed: sensor off, a fire (or more burn time), the explosion, salvage gone and the marines out.
        if (SensorSystem.lock() != nullptr)
        {
            SensorSystem.lock()->Disable();
        }

        if (!Burning)
        {
            if (type.BlownEffectId != 0xffffffff)
            {
                std::shared_ptr<MCGameObject> effect = CreateObject(static_cast<int32_t>(type.BlownEffectId));

                if (effect != nullptr)
                {
                    effect->SetPosition(Position);

                    if (effect->ObjectClass == MCObjectClass::Fire)
                    {
                        std::shared_ptr<MCFire> fire = FireObject.Light(std::static_pointer_cast<MCFire>(effect));
                        fire->SetPotentialContact(3);
                        fire->BurningObject = SharedAs<MCGameObject>();
                        fire->SetTonnage(40.0f);
                        FireObject.Burn();
                        Burning = true;
                    }
                    else
                    {
                        AddToDefaultList(std::move(effect));
                    }
                }
            }
        }
        else if (FireObject != nullptr)
        {
            FireObject->AddTimeLeftToBurn(2.0f);
        }

        // Port-only (HD shake): its end shakes the picture.
        type.CreateExplosion(Position, ExplDamage, ExplRadius, MCBlastShake::Shakes);
        Status = 2;

        if (IsCaptured() != 0)
        {
            TacticalMap()->RemoveSalvage(*this, 1);
        }

        newDamage = destroyLevel;

        if (MultiPlayer() == nullptr)
        {
            CreateBuildingMarines();
        }
    }

    SetDamage(newDamage);
    return 0;
}
