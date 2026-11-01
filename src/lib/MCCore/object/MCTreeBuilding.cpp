#include "stdafx.h"
#include "object/MCTreeBuilding.h"
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
#include "object/MCContactSystem.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectSystem.h"
#include "object/MCSensorSystem.h"
#include "object/MCTeam.h"
#include "object/MCTreeBuildingType.h"
#include "object/MCVertexPlacement.h"
#include "object/MCWeaponShotInfo.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCVfxAppearance.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

MCTreeBuilding::MCTreeBuilding()
{
    Frame.ResetToWorldFrame();
}

MCTreeBuilding::~MCTreeBuilding()
{
    if (SensorSystem.lock() != nullptr)
    {
        SensorSystemManager().FreeSensor(*SensorSystem.lock());
    }
}

auto MCTreeBuilding::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCTreeBuilding::SetTerrainPosition(MCVector2D& offset, MCVector2D& numbers) -> void
{
    PixelOffsetX = static_cast<int32_t>(offset.X);
    PixelOffsetY = static_cast<int32_t>(offset.Y);
    VertexNumber = static_cast<int32_t>(numbers.X);
    BlockNumber = static_cast<int32_t>(numbers.Y);
}

auto MCTreeBuilding::IsPrison() -> int
{
    return std::ranges::any_of(PrisonSlots,
                               [](const std::weak_ptr<MCMechWarrior>& slot) { return slot.lock() != nullptr; })
               ? 1
               : 0;
}

auto MCTreeBuilding::GetRefitPoints() -> float
{
    if (!CanRefit)
    {
        return 0.0f;
    }

    return static_cast<float>(static_cast<int32_t>(TypeAs<MCTreeBuildingType>().DmgLevel)) - Damage;
}

auto MCTreeBuilding::BurnRefitPoints(float points) -> int
{
    if (!CanRefit)
    {
        return 0;
    }

    // Spent refit points count as damage; never more than are left.
    DamageObject(points < GetRefitPoints() ? points : GetRefitPoints());
    return 1;
}

auto MCTreeBuilding::IsVisible() -> bool
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

    // The shadow can stick out past the building: on screen when any of its box is.
    bool shadowOnScreen = false;

    if (const std::span<uint8_t> shadow = TypeAs<MCTreeBuildingType>().NormalShadow.Bytes(); !shadow.empty())
    {
        const int32_t minXY = VfxShapeMinxy(shadow, 0);
        const float left = static_cast<float>(minXY >> 16) + ScreenPos.X;
        const float top = static_cast<float>(static_cast<int16_t>(minXY)) + ScreenPos.Y;
        const int32_t resolution = VfxShapeResolution(shadow, 0);

        if (0.0f <= static_cast<float>(resolution >> 16) + left &&
            0.0f <= static_cast<float>(static_cast<int16_t>(resolution)) + top)
        {
            const auto viewRight = static_cast<int16_t>(std::floor(static_cast<double>(Eye()->ViewWidth)));
            const auto viewBottom = static_cast<int16_t>(std::floor(static_cast<double>(Eye()->ViewHeight)));
            shadowOnScreen = left <= static_cast<float>(viewRight) && top <= static_cast<float>(viewBottom);
        }
    }

    if (!shadowOnScreen && visible == 0)
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

auto MCTreeBuilding::IsCaptureable() -> int
{
    if (MultiPlayer() == nullptr)
    {
        return Captureable && IsCaptured() == 0 && IsDestroyed() == 0 ? 1 : 0;
    }

    return Captureable && IsDestroyed() == 0 ? 1 : 0;
}

auto MCTreeBuilding::Update() -> int32_t
{
    if (!JustCreated)
    {
        return 1;
    }

    // Set the building on its vertex.
    JustCreated = false;
    Position = PlaceOnVertex(Position, BlockNumber, VertexNumber, PixelOffsetX, PixelOffsetY);
    const MCVertexCell cell = MCVertexCell::Of(BlockNumber, VertexNumber);
    CellColumn = cell.Col;
    VertexWorldX = cell.WorldX();
    CellRow = cell.Row;
    VertexWorldY = cell.WorldY();
    CellElevation = cell.Elevation(" tbldg MapTile Out of Bounds ");
    Appearance->Visible = true;
    Appearance->Update();
    Appearance->RecalcBounds(EyeRef());

    if (CanRefit)
    {
        Appearance->SetTypeId(MCActorState::Normal, 0);
    }

    return 1;
}

auto MCTreeBuilding::SetAlignment(int32_t align) -> void
{
    MCBigGameObject::SetAlignment(align);

    if (IsDestroyed() != 0 || SensorSystem.lock() == nullptr)
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

auto MCTreeBuilding::HandleEvent(MCObjectEvent& event) -> int32_t
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

auto MCTreeBuilding::LightOnFire(float timeToBurn) -> void
{
    const auto& type = TypeAs<MCTreeBuildingType>();

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
                // Original behaviour (OB-154): nothing updates or draws this fire.
                std::shared_ptr<MCFire> fire = FireObject.Light(std::static_pointer_cast<MCFire>(newFire));
                fire->SetPotentialContact(3);
                fire->BurningObject = SharedAs<MCGameObject>();
                fire->SetTonnage(40.0f);
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
        Burning = true;
    }
}

auto MCTreeBuilding::IsRevealed() -> int
{
    return MCVertexCell::Of(BlockNumber, VertexNumber).AnyCornerVisible() ? 1 : 0;
}

auto MCTreeBuilding::Render() -> void
{
    if (JustCreated)
    {
        return;
    }

    const auto& type = TypeAs<MCTreeBuildingType>();

    // Burning: the type's burn damage every TimeToBurnDamage seconds.
    if (FireObject == nullptr)
    {
        Burning = false;
    }
    else
    {
        const double burnSum = static_cast<double>(FrameLength) + BurnTime;
        BurnTime = static_cast<float>(burnSum);

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

    if (Appearance != nullptr)
    {
        Appearance->Visible = IsVisible();

        // When the collapse animation ends, settle on the matching rubble state.
        if (Appearance->Update() == 0 && Collapsing)
        {
            const MCActorState state = Appearance->CurrentState;
            Collapsing = false;
            Collapsed = true;

            if (state == MCActorState::BlowingUp1)
            {
                Appearance->SetTypeId(MCActorState::Damaged, 0xff);
            }
            else if (state == MCActorState::Destroyed)
            {
                Appearance->SetTypeId(MCActorState::FallenDamaged, 0xff);
            }

            Appearance->Update();
        }
    }

    if (GetContactType(HomeTeam()->Id) == 2)
    {
        // A sensor contact: a blip sized by tonnage.
        if (const std::span<uint8_t> shape = SensorBlipShape(GetTonnage()); !shape.empty())
        {
            if (VfxShapeCount(shape) < BlipFrame)
            {
                if (SoundSystem() != nullptr && UseSound != 0)
                {
                    SoundSystem()->PlayDigitalSample(0x14, 1, *this, false, true);
                }

                BlipFrame = 0;
            }

            ElementList().OpenGroup(-100000, true);
            ElementList().Add(
                ElementList().Make<MCVfxElement>(shape, ScreenPos.X, ScreenPos.Y, BlipFrame, 0, std::nullopt, 0));
            BlipTime = FrameLength + BlipTime;

            if (0.067 < BlipTime)
            {
                BlipFrame = static_cast<int32_t>(BlipTime * (1.0 / 0.067) + BlipFrame + 0.5);
                BlipTime = 0.0f;
            }
        }
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

    // Hazed by how many corners of its vertex square the home team sees; drawn (with its shadow) when any is. (The
    // original also reads each corner's seen bit and drops it.)
    const int32_t numVisible = MCVertexCell::Of(BlockNumber, VertexNumber).VisibleCorners();
    int32_t hazeLevel = 0;

    if (numVisible != 0 && numVisible != 4 && Eye()->HazeLevel != 0x7fff)
    {
        hazeLevel = HazeLevelFor(numVisible);
    }

    Appearance->HazeLevel = hazeLevel;
    const std::optional<MCColorTable> hazePalette = GamePalette().GetHazePalette(hazeLevel);

    if (numVisible == 0)
    {
        if (SoundHandle != 0xffffffff)
        {
            SoundSystem()->StopDigitalSample(SoundHandle);
            SoundHandle = 0xffffffff;
        }
    }
    else
    {
        // Standing, it sorts with the terrain; fallen or burning down, by its screen row.
        const bool standing = Appearance->CurrentState == MCActorState::Normal;
        Appearance->Render(standing ? 0 : static_cast<int32_t>(ScreenPos.Y));
        const std::span<uint8_t> shadow = standing ? type.NormalShadow.Bytes() : type.DestroyedShadow.Bytes();

        if (!shadow.empty())
        {
            ElementList().OpenGroup(static_cast<int32_t>(ScreenPos.Y), true);
            ElementList().Add(ElementList().Make<MCVfxElement>(shadow, ScreenPos.X, ScreenPos.Y, 0, 0, hazePalette, 0));
        }

        if (SoundHandle == 0xffffffff && type.NormalEffectId != 0xffffffff)
        {
            SoundHandle =
                static_cast<uint32_t>(SoundSystem()->PlayDigitalSample(type.NormalEffectId, 0, *this, true, false));
        }
    }

    if (DrawExtents)
    {
        // Debug: the extent radius as an ellipse.
        DrawExtentEllipse(Position, MCVector2D(ObjType->ExtentRadius, ObjType->ExtentRadius));
    }
}

auto MCTreeBuilding::SetSensorData(std::shared_ptr<MCTeam> newTeam, float range, bool setTeam) -> void
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

auto MCTreeBuilding::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
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

    Appearance = std::make_shared<MCVfxAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x2000000)
    {
        return static_cast<int32_t>(0xdcdc0009);
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    const auto& type = TypeAs<MCTreeBuildingType>();
    ObjectClass = MCObjectClass::TreeBuilding;
    HitOnce = false;
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
    CanRefit = type.CanRefit;
    MechBay = type.MechBay;
    Name = LoadGameString(static_cast<uint32_t>(type.BuildingName), 0xfe);

    // Original behaviour (OB-016): with no team (TeamID -1) this reads TeamTable[-1], which in MCX.EXE is homeTeam.
    TypeTeam = type.TeamId == -1 ? HomeTeam() : TeamById(type.TeamId);
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

    Captureable = false;
    RefitBuddy.reset();

    // Damage level 0: already rubble.
    if (type.DmgLevel == 0)
    {
        CollisionsOn = 0;
        Status = 2;
        HitOnce = true;
    }

    return 0;
}

auto MCTreeBuilding::CreateBuildingMarines() -> void
{
    // A random direction, set 1.5 extent radii out on the ground (z stays the unscaled unit component).
    LetOutBuildingMarines(
        *this, TypeAs<MCTreeBuildingType>().NumMarines,
        [this](MCMover& marine)
        {
            const float extent = ObjType->ExtentRadius;
            MCVector3D offset;
            offset.X = static_cast<float>(RandomNumber(static_cast<int32_t>(extent + extent))) - extent;
            offset.Y = static_cast<float>(RandomNumber(static_cast<int32_t>(extent + extent))) - extent;
            offset.Z = static_cast<float>(RandomNumber(0)) - 0.0f;
            const double length =
                std::sqrt(static_cast<double>(offset.Z) * offset.Z + static_cast<double>(offset.Y) * offset.Y +
                          static_cast<double>(offset.X) * offset.X);

            if (length != 0.0)
            {
                offset.X = static_cast<float>(offset.X / length);
                offset.Y = static_cast<float>(offset.Y / length);
                offset.Z = static_cast<float>(offset.Z / length);
            }

            offset.X = static_cast<float>(static_cast<double>(extent) * offset.X * 1.5);
            offset.Y = static_cast<float>(static_cast<double>(extent) * offset.Y * 1.5);
            MCVector3D marinePosition;
            marinePosition.X = offset.X + Position.X;
            marinePosition.Y = offset.Y + Position.Y;
            marinePosition.Z = offset.Z + Position.Z;
            marine.SetPosition(marinePosition);
            marine.SetLastValidPosition(Position + offset);
            GameObjectMap().AddObject(marine);
        });
}

auto MCTreeBuilding::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    if (IsDestroyed() != 0)
    {
        return 0;
    }

    const float newDamage = GetDamage() + shotInfo.Damage;
    SetDamage(newDamage);
    HitOnce = true;
    auto& type = TypeAs<MCTreeBuildingType>();

    if (newDamage < static_cast<float>(static_cast<int32_t>(type.DmgLevel)) || Collapsed || Collapsing)
    {
        return 0;
    }

    // Collapses: the shooter's pilot is alarmed, the marines come out, and the sensor, fire and explosion follow.
    Collapsing = true;
    CollisionsOn = 0;
    Status = 2;

    if (IsCaptured() != 0)
    {
        TacticalMap()->RemoveSalvage(*this, 1);
    }

    if (std::shared_ptr<MCGameObject> attacker = LockObject(shotInfo.Attacker);
        attacker != nullptr && IsMoverClass(attacker->ObjectClass))
    {
        attacker->GetPilot()->TriggerAlarm(MCPilotAlarmType::KilledTarget, static_cast<uint32_t>(PartId));
    }

    if (MultiPlayer() == nullptr)
    {
        CreateBuildingMarines();
    }

    if (type.DamageEffectId != 0xffffffff && 5 < Turn)
    {
        SoundSystem()->PlayDigitalSample(type.DamageEffectId, 1, *this, true, false);
    }

    if (SensorSystem.lock() != nullptr)
    {
        SensorSystem.lock()->Disable();
    }

    // Original behaviour: hitOnce was set just above, so the collapse always plays the destroyed state, never state 1.
    Appearance->SetTypeId(HitOnce ? MCActorState::Destroyed : MCActorState::BlowingUp1, 0xff);

    if (!Burning)
    {
        if (type.BlownEffectId != 0xffffffff)
        {
            std::shared_ptr<MCGameObject> newFire = CreateObject(static_cast<int32_t>(type.BlownEffectId));

            if (newFire != nullptr)
            {
                newFire->SetPosition(Position);

                if (newFire->ObjectClass == MCObjectClass::Fire)
                {
                    std::shared_ptr<MCFire> fire = FireObject.Light(std::static_pointer_cast<MCFire>(newFire));
                    fire->SetPotentialContact(3);
                    fire->BurningObject = SharedAs<MCGameObject>();
                    fire->SetTonnage(40.0f);
                    FireObject.Burn();
                    Burning = true;
                }
                else
                {
                    AddToDefaultList(std::move(newFire));
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
    return 0;
}
