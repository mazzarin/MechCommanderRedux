#include "stdafx.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCLineElement.h"
#include "engine/MCVfxElement.h"
#include "engine/MCCraterManager.h"
#include "vfx/MCVfxFunctions.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCVector2D.h"
#include "lib/MCFitIniFile.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCAIControl.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCMasterComponent.h"
#include "object/MCContactSystem.h"
#include "object/MCElementalControlData.h"
#include "object/MCMoverGroup.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectSystem.h"
#include "object/MCPlayerControl.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCElementalActor.h"
#include "terrain/MCTerrain.h"
#include "object/MCObjectType.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCMoverMath.h"

MCElemental::MCElemental()
{
    ObjectClass = MCObjectClass::Elemental;
}

MCElemental::~MCElemental() = default;

auto MCElemental::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    int32_t result = MCGameObject::Init(objType);

    if (result != 0)
    {
        return result;
    }

    auto& elementalType = static_cast<MCElementalType&>(*objType);
    Alignment = elementalType.Alignment;
    CollisionsOn = 1;
    MaxHealth = elementalType.MaxHealth;
    Control = nullptr;
    Dynamics = elementalType.DynamicsType->CreateInstance(*this);

    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(elementalType.AppearName);

    if (apprType == nullptr)
    {
        return -0x2fff7;
    }

    Appearance = std::make_unique<MCElementalActor>();
    auto& actor = static_cast<MCElementalActor&>(*Appearance);

    actor.Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x8000000)
    {
        return -0x5fff6;
    }

    result = actor.Init(apprType, WeakAs<MCGameObject>());

    if (result != 0)
    {
        return result;
    }

    ObjectClass = MCObjectClass::Elemental;
    DistanceSinceMarkSeen = 1000.0f;
    Removed = false;
    ElementalCanJump = elementalType.CanJump;
    return 0;
}

auto MCElemental::SetControl(uint32_t controlType, uint32_t controlData, int32_t controlParam) -> int32_t
{
    switch (controlType)
    {
        case 1:
            Control = std::make_unique<MCPlayerControl>(WeakAs<MCGameObject>());
            break;
        case 2:
            Control = std::make_unique<MCElementalAIControl>(*this);
            break;
        case 3:
            // Keeps the control it has.
            break;
        default:
            return -0x5fffb;
    }

    if (controlData != 3)
    {
        return -0x5fff9;
    }

    Control->ControlData = std::make_unique<MCElementalControlData>();
    return 0;
}

auto MCElemental::LoadProfile(MCFitIniFile& elementalFile) -> int32_t
{
    if (const int32_t result = elementalFile.SeekBlock("Header"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> fileType = elementalFile.Read<std::string>("FileType");

    if (!fileType.has_value())
    {
        return std::to_underlying(fileType.error());
    }

    if (*fileType != "ElementalProfile")
    {
        return -1;
    }

    if (const int32_t result = elementalFile.SeekBlock("General"); result != 0)
    {
        return result;
    }

    DebugStatus = elementalFile.Read<std::string>("Name").value_or("");
    MCFitReader read(elementalFile);
    read.Value("CurTonnage", Tonnage);
    read.Value("CurHealth", CurHealth);
    read.Value("icon", IconName);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = elementalFile.SeekBlock("Engine"); result != 0)
    {
        return result;
    }

    uint8_t moveSpeed = 0;
    read.Value("MaxMoveSpeed", moveSpeed);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    MaxRunSpeed = static_cast<float>(moveSpeed);
    read.Value("JumpRange", JumpRange);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = elementalFile.SeekBlock("InventoryInfo"); result != 0)
    {
        return result;
    }

    read.Value("NumOther", NumOther);
    read.Value("NumWeapons", NumWeapons);
    read.Value("NumAmmo", NumAmmos);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    const int32_t firstWeapon = NumOther;
    const int32_t firstAmmo = NumOther + NumWeapons;
    const int32_t numItems = NumOther + NumAmmos + NumWeapons;
    Inventory = std::vector<MCInventoryItem>(static_cast<size_t>(numItems));
    NumAntiMissileSystems = 0;

    // An anti-missile system joins the list, whether it is listed with the other equipment or the weapons.
    const auto addAntiMissileSystem = [&](int32_t item)
    {
        const int32_t masterID = Inventory[item].MasterID;

        if (masterID != MasterClanAntiMissileSystemID && masterID != MasterInnerSphereAntiMissileSystemID)
        {
            return;
        }

        if (NumAntiMissileSystems == MaxAntiMissileSystems)
        {
            Fatal(0, "Too many Anti-Missile Systems");
        }

        AntiMissileSystem[NumAntiMissileSystems] = static_cast<uint8_t>(item);
        NumAntiMissileSystems++;
    };

    for (int32_t item = 0; item < firstWeapon; item++)
    {
        if (const int32_t result = elementalFile.SeekBlock(std::format("Item:{}", item)); result != 0)
        {
            return result;
        }

        MCInventoryItem& other = Inventory[item];
        read.Value("MasterID", other.MasterID);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        other.Health = MasterComponentList[other.MasterID].Health;
        other.Disabled = 0;
        other.Amount = 1;
        other.AmmoIndex = -1;
        other.ReadyTime = 0.0f;
        other.BodyLocation = 0xff;

        switch (MasterComponentList[other.MasterID].Form)
        {
            case MCComponentForm::Cockpit:
                Cockpit = static_cast<uint8_t>(item);
                break;
            case MCComponentForm::Sensor:
            {
                Sensor = static_cast<uint8_t>(item);
                SensorSystem = SensorSystemManager().NewSensor();
                SensorSystem.lock()->Owner = SharedAs<MCGameObject>();
                SensorSystem.lock()->SetRange(MasterComponentList[Inventory[item].MasterID].RangeOrHeat);
                break;
            }
            case MCComponentForm::Engine:
                Engine = static_cast<uint8_t>(item);
                break;
            case MCComponentForm::WeaponBallistic:
                addAntiMissileSystem(item);
                break;
            case MCComponentForm::LifeSupport:
                LifeSupport = static_cast<uint8_t>(item);
                break;
            case MCComponentForm::Ecm:
                Ecm = static_cast<uint8_t>(item);
                break;
            case MCComponentForm::Probe:
                Probe = static_cast<uint8_t>(item);
                break;
            default:
                break;
        }
    }

    for (int32_t item = firstWeapon; item < firstAmmo; item++)
    {
        if (const int32_t result = elementalFile.SeekBlock(std::format("Item:{}", item)); result != 0)
        {
            return result;
        }

        MCInventoryItem& weapon = Inventory[item];
        read.Value("MasterID", weapon.MasterID);
        read.Value("FacesForward", weapon.FacesForward);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        const MCMasterComponent& component = MasterComponentList[weapon.MasterID];
        weapon.Health = component.Health;
        weapon.Disabled = 0;
        weapon.Amount = 1;
        weapon.AmmoIndex = -1;
        weapon.ReadyTime = 0.0f;
        weapon.BodyLocation = 0xff;
        // As the mech's: damage per ten seconds, then scaled by the long range over 24.
        weapon.Effectiveness =
            static_cast<int16_t>(static_cast<int32_t>(component.Damage * 10.0 / component.RecycleTime));
        weapon.Effectiveness = static_cast<int16_t>(static_cast<int32_t>(
            static_cast<double>(component.WeaponRange[3]) * weapon.Effectiveness * static_cast<double>(1.0f / 24.0f)));
        weapon.RangeRatings.resize(static_cast<size_t>(NumRangeRatings) * 2);

        if (MasterComponentList[Inventory[item].MasterID].Form == MCComponentForm::WeaponBallistic)
        {
            addAntiMissileSystem(item);
        }

        ObjectTypeManager().Load(
            static_cast<int32_t>(
                WeaponFXTable[static_cast<int8_t>(MasterComponentList[Inventory[item].MasterID].WeaponEffect)]),
            1);
    }

    for (int32_t item = firstAmmo; item < numItems; item++)
    {
        if (const int32_t result = elementalFile.SeekBlock(std::format("Item:{}", item)); result != 0)
        {
            return result;
        }

        MCInventoryItem& ammo = Inventory[item];
        read.Value("MasterID", ammo.MasterID);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        // The amount is a long, or else a byte.
        int32_t amount = 0;

        if (const MCFitResult<int32_t> longAmount = elementalFile.Read<int32_t>("Amount"); longAmount.has_value())
        {
            amount = *longAmount;
        }
        else
        {
            const MCFitResult<uint8_t> byteAmount = elementalFile.Read<uint8_t>("Amount");

            if (!byteAmount.has_value())
            {
                return std::to_underlying(byteAmount.error());
            }

            amount = *byteAmount;
        }

        if (amount == -1)
        {
            amount = MasterComponentList[ammo.MasterID].LongValue;
        }

        ammo.Amount = static_cast<int16_t>(amount);
        ammo.StartAmount = ammo.Amount;
        ammo.AmmoIndex = -1;
        ammo.Health = MasterComponentList[ammo.MasterID].Health;
        ammo.Disabled = 0;
        ammo.ReadyTime = 0.0f;
        ammo.BodyLocation = 0xff;
    }

    CalcAmmoTotals();

    // Each weapon (and anti-missile system) finds its ammunition's tally.
    const auto setAmmoIndex = [this](MCInventoryItem& item, int32_t ammoMasterId)
    {
        for (int32_t ammoType = 0; ammoType < NumAmmoTypes(); ammoType++)
        {
            if (ammoMasterId == AmmoTypeTotal[ammoType].MasterId)
            {
                item.AmmoIndex = static_cast<int16_t>(ammoType);
                return;
            }
        }
    };

    for (int32_t item = firstWeapon; item < firstAmmo; item++)
    {
        setAmmoIndex(Inventory[item], static_cast<int32_t>(MasterComponentList[Inventory[item].MasterID].AmmoMasterId));
    }

    for (int32_t item = 0; item < firstWeapon; item++)
    {
        const int32_t masterID = Inventory[item].MasterID;

        if (masterID == MasterClanAntiMissileSystemID || masterID == MasterInnerSphereAntiMissileSystemID)
        {
            setAmmoIndex(Inventory[item], static_cast<int32_t>(MasterComponentList[masterID].AmmoMasterId));
        }
    }

    CalcLongestRangeWeapon();
    CalcWeaponEffectiveness(1);
    CalcWeaponEffectiveness(0);
    MaxCV = CalcCV(1);
    CurCV = CalcCV(0);

    if (!ElementalCanJump)
    {
        // Marines are worth a fixed amount.
        CurCV = 50000;
        MaxCV = 50000;
    }

    return 0;
}

auto MCElemental::CalcCV(int calcMax) -> int32_t
{
    // Offense: the weapons' ratings, scaled by the top speed.
    double offense = 0.0;
    const int32_t firstWeapon = NumOther;

    for (int32_t item = firstWeapon; item < firstWeapon + NumWeapons; item++)
    {
        if (calcMax != 0 || Inventory[item].Disabled == 0)
        {
            offense += MasterComponentList[Inventory[item].MasterID].BattleRating;
        }
    }

    offense *= (MaxRunSpeed - 18.0) * 0.05555555555555555 + 1.0;

    // Defense: health, tonnage, the speed class and the other equipment.
    double defense = static_cast<double>(calcMax != 0 ? MaxHealth : CurHealth);
    defense += TonnageClass;
    int32_t speedClass = 0;

    while (speedClass < 5 && static_cast<float>(TargetMoveModifierTable[speedClass][0]) < MaxRunSpeed)
    {
        speedClass++;
    }

    // Port fix: past the table (a top speed over 999) the original reads the word after it.
    if (speedClass == 5)
    {
        speedClass = 4;
    }

    defense += TargetMoveModifierTable[speedClass][1] * 10;

    for (int32_t item = 0; item < firstWeapon; item++)
    {
        if (calcMax != 0 || Inventory[item].Disabled == 0)
        {
            defense += MasterComponentList[Inventory[item].MasterID].BattleRating;
        }
    }

    return static_cast<int32_t>(defense + offense);
}

auto MCElemental::GetPositionFromHS(uint32_t hotSpot) -> MCVector3D
{
    return Position;
}

auto MCElemental::OnScreen() -> int
{
    const std::shared_ptr<MCCamera> camera = CameraList()->FindCameraFromIDNumber(1);
    ScreenPos.Y = 0.0f;
    ScreenPos.X = 0.0f;

    if (camera == nullptr || camera->Active == 0)
    {
        return 0;
    }

    if (UseOldProject == 0)
    {
        ScreenPos = ProjectToScreen(Position, *camera);
    }
    else
    {
        const MCVector3D relative(Position.X - camera->Position.X, Position.Y - camera->Position.Y,
                                  Position.Z - camera->Position.Z);
        ScreenPos.X = relative.Y * camera->CosAngle + relative.X * camera->CosAngle + camera->HalfWidth;
        ScreenPos.Y =
            ((relative.X * camera->SinAngle + camera->HalfHeight) - relative.Y * camera->SinAngle) - relative.Z;
    }

    if (Appearance != nullptr && Appearance->RecalcBounds(*camera) != 0)
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

auto MCElemental::Update() -> int32_t
{
    if (InTransport() != 0)
    {
        return 1;
    }

    if (IsDestroyed() != 0)
    {
        if (Removed != 0)
        {
            return 1;
        }

        // The body: when the death timer runs low it blows up and leaves a crater.
        DeathTimer -= FrameLength;

        if (DeathTimer < 0.4 && DeathExplosionDone == 0 && Withdrawing == 0)
        {
            ObjType->CreateExplosion(Position, 0.0f, 0.0f);
            CraterManager().AddCrater(7, Position, 0);
            DeathExplosionDone = true;
            return 1;
        }

        if (DeathTimer < 0.0)
        {
            return 1;
        }
    }
    else
    {
        if (DeselectTime != 0.0f && DeselectTime < ScenarioTime)
        {
            DeselectTime = 0.0f;
            Selected = 0;
        }

        if (GetAwake() != 0 && IsDisabled() == 0 && MCTerrain::MetersPerVertex <= DistanceSinceMarkSeen)
        {
            // Every vertex travelled, the elemental marks what it sees.
            if (Alignment == 1)
            {
                Terrain()->MarkSeen(Position, Frame.J, 360.0f, GetProbeEffect() + Scenario()->MaxVisualRange, 1);
            }
            else if (Alignment == -1)
            {
                Terrain()->MarkSeen(Position, Frame.J, 360.0f, GetProbeEffect() + Scenario()->MaxVisualRange, 2);
            }

            DistanceSinceMarkSeen = 0.0f;
        }

        int32_t result = Control->Update();

        if (result != 1)
        {
            return result;
        }

        result = Dynamics->Update();

        if (result != 1)
        {
            return result;
        }

        auto& controlData = static_cast<MCElementalControlData&>(*Control->ControlData);
        auto& actor = static_cast<MCElementalActor&>(*Appearance);

        if (controlData.Jump != 0)
        {
            actor.SetJumpParameters(controlData.JumpDistance);
            controlData.Throttle = 0;
        }

        // In the air the actor moves it (and nothing collides with it); on the ground the dynamics do.
        float speed = actor.GetVelocityMagnitude();

        if (actor.CurrentGesture == 2)
        {
            CollisionsOn = 0;
        }
        else
        {
            speed = Dynamics->GetVelocity();
            CollisionsOn = 1;
        }

        MCFrameOfRef turned = Frame;
        speed = -speed;
        MCMoverMath::RotateAboutK(turned, static_cast<float>(std::sin(MCMoverMath::HalfPi / 2.0)),
                                  static_cast<float>(std::cos(MCMoverMath::HalfPi / 2.0)));
        Velocity.Y = turned.J.Y * speed;
        Velocity.X = turned.J.X * speed;
        Velocity.Z = turned.J.Z * speed;
        MCVector3D move;
        move.X = static_cast<float>(static_cast<double>(Velocity.X) * FrameLength * WorldUnitsPerMeter);
        move.Y = Velocity.Y * FrameLength * WorldUnitsPerMeter;
        move.Z = Velocity.Z * FrameLength * WorldUnitsPerMeter;
        MCVector3D newPosition;
        newPosition.X = move.X + Position.X;
        newPosition.Y = move.Y + Position.Y;
        newPosition.Z = move.Z + Position.Z;
        SetPosition(newPosition);
        DistanceSinceMarkSeen =
            static_cast<float>(std::sqrt(static_cast<double>(move.X) * move.X + static_cast<double>(move.Y) * move.Y +
                                         static_cast<double>(move.Z) * move.Z) +
                               DistanceSinceMarkSeen);
        Position.Z = MCTerrain::GetTerrainElevation(Position);

        const int visibleNow = OnScreen();
        const int offScreen = visibleNow == 0 ? 1 : 0;

        if (Withdrawing != 0)
        {
            if (visibleNow == 0)
            {
                ObjType->HandleDestruction(*this, 0);
                Removed = true;
            }
        }

        // Original behaviour (OB-002): a marine off the screen is removed.
        if (offScreen && ElementalCanJump == 0)
        {
            RemoveMarine(0.8f);
            Removed = true;
        }

        if (Appearance != nullptr)
        {
            Appearance->Visible = visibleNow;
            Appearance->Update();
        }
    }

    return 1;
}

auto MCElemental::Render() -> void
{
    int tagged = 0;

    if (IsDestroyed() == 0)
    {
        if (Alignment == HomeTeam()->Alignment)
        {
            if (WindowsVisible == Turn)
            {
                Appearance->Render(0);
            }
        }
        else
        {
            const int32_t contactType = GetContactType(HomeTeam()->Id, tagged);

            if (contactType == 1)
            {
                if (WindowsVisible == Turn)
                {
                    Appearance->Render(0);
                }
            }
            else if (contactType == 2)
            {
                // A sensor contact: a blip sized by tonnage.
                RenderSensorBlip(SensorBlipShape(GetTonnage()));
            }
            else if (ElementalCanJump == 0)
            {
                // An unseen marine: drawn once revealed. Original behaviour (OB-002): removed when it isn't on the
                // screen.
                if (WindowsVisible == 0)
                {
                    OnScreen();
                }

                if (WindowsVisible == Turn)
                {
                    if (IsRevealed() != 0)
                    {
                        Appearance->Render(0);
                    }
                }
                else
                {
                    RemoveMarine(0.8f);
                    Removed = true;
                }
            }
        }
    }

    if (DrawTerrainGrid != 0)
    {
        // Debug: the move path's steps as lines.
        const MCMovePath& path = Pilot.lock()->GetMovePath();
        const int32_t numSteps = path.NumSteps;

        for (int32_t i = 0; i < numSteps; i++)
        {
            if (i == numSteps - 1)
            {
                continue;
            }

            MCVector3D from = path.StepList[i].Destination;
            MCVector3D to = path.StepList[i + 1].Destination;
            from.Z = MCTerrain::GetTerrainElevation(from);
            to.Z = MCTerrain::GetTerrainElevation(to);
            const auto project = [](const MCVector3D& point)
            {
                const float sx = point.X - Eye()->Position.X;
                const float sy = point.Y - Eye()->Position.Y;
                MCVector2D screen;
                screen.X = sx * Eye()->CosAngle + sy * Eye()->CosAngle + Eye()->HalfWidth;
                screen.Y =
                    ((sx * Eye()->SinAngle + Eye()->HalfHeight) - sy * Eye()->SinAngle) - (point.Z - Eye()->Position.Z);
                return screen;
            };

            MCVector2D fromScreen = project(from);
            MCVector2D toScreen = project(to);
            ElementList().OpenGroup(-100000, true);
            ElementList().Add(ElementList().Make<MCLineElement>(fromScreen, toScreen, 0xfe, std::nullopt, -100000, -1));
        }
    }
}

auto MCElemental::GetBodyState() -> int32_t
{
    return 0;
}

auto MCElemental::GetVitalInfo(std::span<uint8_t> vitalInfo) -> int32_t
{
    const int32_t size = MCMover::GetVitalInfo({});

    if (!vitalInfo.empty())
    {
        MCMover::GetVitalInfo(vitalInfo);
        const std::span<uint8_t> info = vitalInfo.subspan(static_cast<size_t>(size));
        std::memcpy(info.data(), &JumpRange, 4);
        const int32_t zero = 0; // the original copied a field nothing ever set
        std::memcpy(info.data() + 4, &zero, 4);
        std::memcpy(info.data() + 8, &MaxHealth, 4);
        std::memcpy(info.data() + 12, &CurHealth, 4);
    }

    return size + 0x10;
}

auto MCElemental::RemoveMarine(float deathTime) -> void
{
    DeathTimer = deathTime;
    Pilot.lock()->TriggerAlarm(MCPilotAlarmType::VehicleDestroyed, 0);
    Status = 2;
    DeathExplosionDone = false;
    TacticalInterface()->RemoveMech(PartId);
}
