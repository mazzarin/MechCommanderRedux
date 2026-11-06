#include "stdafx.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGameSystemReader.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCLineElement.h"
#include "engine/MCEllipseElement.h"
#include "engine/MCVfxElement.h"
#include "vfx/MCVfxFunctions.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCNightLights.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCAIControl.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCMasterComponent.h"
#include "object/MCCollisionSystem.h"
#include "object/MCContactSystem.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCMoverGroup.h"
#include "object/MCGroundVehicleControlData.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCNetControl.h"
#include "object/MCObjectSystem.h"
#include "object/MCPlayerControl.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCEffectSystem.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCGVAppearance.h"
#include "sprite/MCElementalActor.h"
#include "sprite/MCPUAppearance.h"
#include "terrain/MCTerrain.h"
#include "object/MCObjectType.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCMoverMath.h"

auto MCGroundVehicle::RelViewFacingTo(MCVector3D goal) -> float
{
    return RelFacingTo(goal, GroundVehicleTurret);
}

auto MCGroundVehicle::GetBodyState() -> int32_t
{
    // The original reads +0x74 of either appearance: a pop-up turret's gives the bits of its shapeMaxY.
    if (GvAppearance == 0)
    {
        return std::bit_cast<int32_t>(static_cast<MCPUAppearance*>(Appearance.get())->ShapeMaxY);
    }

    return static_cast<int32_t>(static_cast<MCGVAppearance*>(Appearance.get())->CurrentState);
}

auto MCGroundVehicle::IsCaptureable() -> int
{
    if ((Captureable != 0 || !Salvage.empty()) && IsCaptured() == 0 && IsDestroyed() == 0)
    {
        return 1;
    }

    return 0;
}

auto MCGroundVehicle::GetRefitPoints() -> float
{
    if (Refitter != 0)
    {
        return Armor[GroundVehicleTurret].CurArmor;
    }

    return 0.0f;
}

auto MCGroundVehicle::BurnRefitPoints(float pointsToBurn) -> int
{
    if (Refitter != 0 && pointsToBurn <= Armor[GroundVehicleTurret].CurArmor)
    {
        Armor[GroundVehicleTurret].CurArmor -= pointsToBurn;
        return 1;
    }

    return 0;
}

MCGroundVehicle::MCGroundVehicle()
{
    ObjectClass = MCObjectClass::GroundVehicle;
    Body = std::vector<MCBodyLocation>(NumGroundVehicleLocations);
    Armor = std::vector<MCArmorLocation>(NumGroundVehicleLocations);
    AmmoTruck = 0;
    RefitBuddy.reset();
    BlipFrame = 0;
}

MCGroundVehicle::~MCGroundVehicle() = default;

auto MCGroundVehicle::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    int32_t result = MCGameObject::Init(objType);

    if (result != 0)
    {
        return result;
    }

    auto& vehicleType = static_cast<MCGroundVehicleType&>(*objType);
    CollisionsOn = 1;

    for (int32_t location = 0; location < NumGroundVehicleLocations; location++)
    {
        BodyAt(location).MaxInternalStructure = vehicleType.InternalStructure[location];
        BodyAt(location).HasCase = 0;
        BodyAt(location).DamageState = 0;
    }

    Alignment = vehicleType.Alignment;
    InternalStructureTonnage = vehicleType.InternalStructureTonnage;
    PathLockLevel = vehicleType.CrashBlockSelf;
    Chassis = vehicleType.Chassis;
    TonnageClass = vehicleType.TonnageClass;
    AmmoTruck = vehicleType.AmmoTruck;
    CrashAvoidSelf = vehicleType.CrashAvoidSelf;
    CrashAvoidPath = vehicleType.CrashAvoidPath;
    PathLockRange = vehicleType.CrashBlockPath;
    CrashYieldTime = vehicleType.CrashYieldTime;

    if (vehicleType.RefitPoints != 0)
    {
        Refitter = true;
    }

    MineSweeper = vehicleType.MineSweeper;
    // Port: none in a match without mines (MCMatchRule::NoMines).
    MinesToLay = MCMatchPlays(MCMatchRule::NoMines) ? 0 : vehicleType.MinesToLay;

    if (MinesToLay > 0)
    {
        MineLayer = true;
    }

    ElementalCarrier = vehicleType.ElementalCarrier;
    Seats = vehicleType.Seats;
    Control = nullptr;
    Dynamics = vehicleType.DynamicsType->CreateInstance(*this);

    const uint32_t appearanceId = vehicleType.AppearName;
    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(appearanceId);

    if (apprType == nullptr)
    {
        return -0x2fff7;
    }

    switch (appearanceId & 0xff000000)
    {
        case 0x5000000:
        {
            Appearance = std::make_unique<MCGVAppearance>();
            auto& vehicleAppearance = static_cast<MCGVAppearance&>(*Appearance);

            vehicleAppearance.Init();

            if ((apprType->AppearanceNum & 0xff000000) != 0x5000000)
            {
                return -0x2fff6;
            }

            result = vehicleAppearance.Init(apprType, WeakAs<MCGameObject>());

            if (result != 0)
            {
                return result;
            }

            GvAppearance = true;
            WeaponsDeployed = true;
            break;
        }

        case 0x9000000:
        {
            Appearance = std::make_unique<MCPUAppearance>();
            auto& turretAppearance = static_cast<MCPUAppearance&>(*Appearance);

            turretAppearance.Init();

            if ((apprType->AppearanceNum & 0xff000000) != 0x9000000)
            {
                return -0x2fff6;
            }

            result = turretAppearance.Init(apprType, WeakAs<MCGameObject>());

            if (result != 0)
            {
                return result;
            }

            GvAppearance = false;
            WeaponsDeployed = false;
            break;
        }

        default:
            break;
    }

    ObjectClass = MCObjectClass::GroundVehicle;
    DistanceSinceMarkSeen = 1000.0f;
    return 0;
}

auto MCGroundVehicle::SetControl(uint32_t controlType, uint32_t controlData, int32_t controlParam) -> int32_t
{
    switch (controlType)
    {
        case 1:
            Control = std::make_unique<MCPlayerControl>(WeakAs<MCGameObject>());
            break;
        case 2:
            Control = std::make_unique<MCGroundVehicleAIControl>(*this);
            break;
        case 3:
            Control = std::make_unique<MCGroundVehicleNetControl>(*this);
            break;
        default:
            return -0x5fffb;
    }

    if (controlData != 2 && controlData != 0xffffffff)
    {
        return -0x5fff9;
    }

    Control->ControlData = std::make_unique<MCGroundVehicleControlData>();
    return 0;
}

auto MCGroundVehicle::LoadProfile(MCFitIniFile& vehicleFile) -> int32_t
{
    static constexpr std::array<std::string_view, NumGroundVehicleLocations> locationNames = {"Front", "Left", "Right",
                                                                                              "Rear", "Turret"};

    if (const int32_t result = vehicleFile.SeekBlock("Header"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> fileType = vehicleFile.Read<std::string>("FileType");

    if (!fileType.has_value())
    {
        return std::to_underlying(fileType.error());
    }

    if (*fileType != "GroundVehicleProfile")
    {
        return -1;
    }

    if (const int32_t result = vehicleFile.SeekBlock("General"); result != 0)
    {
        return result;
    }

    CrewName = vehicleFile.Read<std::string>("Crew").value_or("");
    NotMineYet = vehicleFile.Read<bool>("NotMineYet").value_or(true);
    DescIndex = vehicleFile.Read<int32_t>("DescIndex").value_or(-1);
    DebugStatus = LoadGameString(static_cast<uint32_t>(DescIndex + 700), 0xfe);
    MCFitReader read(vehicleFile);
    char fileStatus = 0;
    read.Value("NameIndex", NameIndex);
    read.Value("CurTonnage", Tonnage);
    read.Value("Status", fileStatus);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    Status = fileStatus;
    read.Value("icon", IconName);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    BattleRating = vehicleFile.Read<int32_t>("BattleRating").value_or(-1);

    if (const int32_t result = vehicleFile.SeekBlock("Engine"); result != 0)
    {
        return result;
    }

    uint8_t moveSpeed = 0;
    read.Value("Tonnage", EngineTonnage);
    read.Value("Rating", EngineRating);
    read.Value("MaxMoveSpeed", moveSpeed);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    MaxRunSpeed = static_cast<float>(moveSpeed);

    if (vehicleFile.SeekBlock("MovementSystem") == 0)
    {
        MCGameSystemReader::Optional(vehicleFile, "CrashAvoidSelf", CrashAvoidSelf);
        MCGameSystemReader::Optional(vehicleFile, "CrashAvoidPath", CrashAvoidPath);
        MCGameSystemReader::Optional(vehicleFile, "CrashBlockSelf", PathLockLevel);
        MCGameSystemReader::Optional(vehicleFile, "CrashBlockPath", PathLockRange);
        MCGameSystemReader::Optional(vehicleFile, "CrashYieldTime", CrashYieldTime);
    }

    if (const int32_t result = vehicleFile.SeekBlock("Armor"); result != 0)
    {
        return result;
    }

    read.Value("Type", ArmorType);
    read.Value("Tonnage", ArmorTonnage);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = vehicleFile.SeekBlock("InventoryInfo"); result != 0)
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

    for (int32_t item = 0; item < firstWeapon; item++)
    {
        if (const int32_t result = vehicleFile.SeekBlock(std::format("Item:{}", item)); result != 0)
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
        if (const int32_t result = vehicleFile.SeekBlock(std::format("Item:{}", item)); result != 0)
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
        ObjectTypeManager().Load(
            static_cast<int32_t>(
                WeaponFXTable[static_cast<int8_t>(MasterComponentList[Inventory[item].MasterID].WeaponEffect)]),
            1);
    }

    for (int32_t item = firstAmmo; item < numItems; item++)
    {
        if (const int32_t result = vehicleFile.SeekBlock(std::format("Item:{}", item)); result != 0)
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

        if (const MCFitResult<int32_t> longAmount = vehicleFile.Read<int32_t>("Amount"); longAmount.has_value())
        {
            amount = *longAmount;
        }
        else
        {
            const MCFitResult<uint8_t> byteAmount = vehicleFile.Read<uint8_t>("Amount");

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

    for (int32_t location = 0; location < NumGroundVehicleLocations; location++)
    {
        if (const int32_t result = vehicleFile.SeekBlock(locationNames[location]); result != 0)
        {
            return result;
        }

        MCBodyLocation& bodyLocation = BodyAt(location);
        bodyLocation.HasCase = 0;
        uint8_t internalStructure = 0;
        read.Value("CurInternalStructure", internalStructure);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        bodyLocation.CurInternalStructure = static_cast<float>(internalStructure);
        const double structureLeft =
            static_cast<double>(internalStructure) / static_cast<double>(bodyLocation.MaxInternalStructure);

        if (structureLeft == 0.0)
        {
            bodyLocation.DamageState = 2;
        }
        else if (structureLeft > 0.5)
        {
            bodyLocation.DamageState = 0;
        }
        else
        {
            bodyLocation.DamageState = 1;
        }

        uint8_t points = 0;
        read.Value("MaxArmorPoints", Armor[location].MaxArmor);
        read.Value("CurArmorPoints", points);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        Armor[location].CurArmor = static_cast<float>(points);
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

    if (Refitter)
    {
        // The refit pool lives in the turret's armor slot.
        const uint8_t refitPoints = static_cast<uint8_t>(TypeAs<MCGroundVehicleType>().RefitPoints);
        Armor[GroundVehicleTurret].MaxArmor = refitPoints;
        Armor[GroundVehicleTurret].CurArmor = static_cast<float>(refitPoints);
    }

    return 0;
}

auto MCGroundVehicle::CalcCV(int calcMax) -> int32_t
{
    if (BattleRating != -1)
    {
        return BattleRating;
    }

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

    // Defense: structure, armor, tonnage, the speed class and the other equipment.
    double defense = 0.0;

    for (int32_t location = 0; location < NumGroundVehicleLocations; location++)
    {
        defense += calcMax != 0 ? static_cast<double>(BodyAt(location).MaxInternalStructure)
                                : BodyAt(location).CurInternalStructure;
    }

    for (int32_t location = 0; location < NumGroundVehicleLocations; location++)
    {
        defense += calcMax != 0 ? static_cast<double>(Armor[location].MaxArmor) : Armor[location].CurArmor;
    }

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

auto MCGroundVehicle::GetPositionFromHS(uint32_t hotSpot) -> MCVector3D
{
    return Position;
}

auto MCGroundVehicle::OnScreen() -> int
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
        const float screenY =
            ((relative.X * camera->SinAngle + camera->HalfHeight) - relative.Y * camera->SinAngle) - relative.Z;
        ScreenPos.Y = screenY;

        if (ScreenPos.X < 0.0f || screenY < 0.0f || camera->ViewWidth < ScreenPos.X || camera->ViewHeight < screenY)
        {
            WindowsVisible = 0;
        }
        else
        {
            WindowsVisible = 1;
        }
    }

    if (Appearance != nullptr && Appearance->RecalcBounds(*camera) != 0)
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

auto MCGroundVehicle::Disable(uint32_t cause) -> void
{
    MCMover::Disable(cause);
    DeathTimer = 0.0f;
    Smoke = CreateObjectAs<MCSmoke>(0x1c2);

    if (Smoke != nullptr)
    {
        Smoke->SetOwner(SharedAs<MCBaseObject>());
        Smoke->SetOwnerPosition(Position);
    }
}

auto MCGroundVehicle::CreateVehiclePilot() -> void
{
    const std::shared_ptr<MCMover> marine = CreateObjectAs<MCMover>(DefaultPilotId);
    VehiclePilot = marine;

    if (marine == nullptr)
    {
        Fatal(-1, " Couldnt create Marine for vehicle ");
    }

    marine->SetAwake(1);
    std::string profileName;
    profileName = GamePath(ProfilePath, MarineProfileName, ".fit");
    MCFitIniFile profileFile;
    const int32_t result = profileFile.Open(profileName);

    if (result != 0)
    {
        Fatal(result, " Unable to open Vehicle Marine Profile ");
    }

    if (marine->LoadProfile(profileFile) != 0)
    {
        Fatal(-1, " Bad Vehicle Marine Profile File ");
    }

    profileFile.Close();

    // The vehicle's pilot bails out as the marine.
    std::shared_ptr<MCMechWarrior> warrior = Pilot.lock();
    marine->SetPilot(warrior);
    warrior->SetVehicle(marine);
    warrior->Lobotomy();
    marine->SetControl(2, 3, -1);
    marine->SetTeam(GetTeam());
    LockObject(VehiclePilot)->SetPosition(Position);
    LockObject(VehiclePilot)->SetLastValidPosition(Position);
    LockObject(VehiclePilot)->SetFrame(Frame);
    auto marineAppearance = std::static_pointer_cast<MCElementalActor>(LockObject(VehiclePilot)->GetAppearance());

    if (marineAppearance != nullptr)
    {
        marineAppearance->SetGesture(0);
        marineAppearance->FadeTableIndex = GetAlignment() == -1 ? 0x1d : 0x20;
    }

    std::shared_ptr<MCMover> newMarine = LockObject(VehiclePilot);
    newMarine->IdNumber = IdNumber + 1000;
    newMarine->SetPartId(0xfff - NumMarines++);
    newMarine->SetAlignment(GetAlignment());
    std::shared_ptr<MCObjectList> list = GetAlignment() == -1 ? ClanMechList() : InnerSphereMechList();

    if (list != nullptr)
    {
        list->Add(marine);
    }

    marine->SetExists(1);
    marine->SetPotentialContact(0);
    GameObjectMap().AddObject(*marine);
    warrior = Pilot.lock();
    warrior->ClearAttackOrders();
    warrior->ClearMoveOrders();
    warrior->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, MCVector3D(0.0f, 0.0f, 0.0f), -1, 1);
}

namespace
{
    /// <summary>Moves the smoke along with the vehicle and runs it; frees it once the smoke time is 30 s past.</summary>
    void UpdateSmoke(MCGroundVehicle& vehicle)
    {
        MCSmoke& smoke = *vehicle.Smoke;
        smoke.SetOwner(vehicle.SharedAs<MCBaseObject>());
        smoke.SetOwnerPosition(vehicle.Position);
        smoke.SetOwnerVelocity(vehicle.Velocity);
        smoke.Update();
        vehicle.DeathTimer -= FrameLength;

        if (vehicle.DeathTimer > -30.0)
        {
            return;
        }

        vehicle.Smoke.reset();
    }
}

auto MCGroundVehicle::Update() -> int32_t
{
    if (Withdrawing != 0 && Pilot.lock()->Status == 2)
    {
        CollisionsOn = 0;
        return 1;
    }

    TerrainNormal = MCTerrain::GetTerrainNormal(Position);
    UpdatePathLock(0);

    if (const std::shared_ptr<MCPotentialContact> contact = PotentialContact.lock())
    {
        // A team that isn't in the mission (the allied one) has nothing to update.
        const auto update = [&contact](std::shared_ptr<MCTeam> team)
        {
            if (team != nullptr)
            {
                contact->UpdateStatus(*team);
            }
        };

        if (Team.lock()->Id == 1)
        {
            update(InnerSphereTeam());
            update(AlliedTeam());
        }
        else if (Team.lock()->Id == 0)
        {
            update(ClanTeam());
            update(AlliedTeam());
        }
        else
        {
            update(InnerSphereTeam());
            update(ClanTeam());
        }
    }

    if (DeselectTime != 0.0f && DeselectTime < ScenarioTime)
    {
        DeselectTime = 0.0f;
        Selected = 0;
    }

    if (IsDestroyed() != 0 && DeathTimer < 0.0)
    {
        // The wreck: only its appearance and smoke go on.
        if (Appearance != nullptr)
        {
            Appearance->Visible = OnScreen();
            Appearance->Update();
        }

        if (Smoke == nullptr)
        {
            return 1;
        }

        UpdateSmoke(*this);
        return 1;
    }

    if (IsDestroyed() == 0 || DeathTimer < 0.0)
    {
        const bool moved = MCTerrain::MetersPerVertex <= DistanceSinceMarkSeen;

        if (GetAwake() == 0 || IsDisabled() != 0 || DistanceSinceMarkSeen < MCTerrain::MetersPerVertex)
        {
            if (IsDisabled() != 0 && DeathTimer != 0.0f && Smoke != nullptr)
            {
                UpdateSmoke(*this);
            }
        }
        else
        {
            // Every vertex travelled, the vehicle marks what it sees.
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

        // Port-only (Redux HD): at night its headlight shows the ground ahead and gives it away
        // (mission/MCNightLights.h).
        if (GetAwake() != 0 && IsDisabled() == 0)
        {
            MCNightLights::Mark(*this, 0.0f, moved);
        }
    }
    else
    {
        // Just destroyed: when the death timer runs out, it blows up and its crew bails out.
        DeathTimer -= FrameLength;

        if (DeathTimer < 0.0)
        {
            if (GvAppearance == 0)
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

            auto& vehicleType = TypeAs<MCGroundVehicleType>();
            vehicleType.CreateExplosion(Position, vehicleType.ExplDmg, vehicleType.ExplRad);
            DeathExplosionDone = true;
            // Port fix (OB-148): the original left a disabled vehicle's smoke behind, never freed.
            Smoke = CreateObjectAs<MCSmoke>(0x1c2);
            CollisionsOn = 0;

            if (MultiPlayer() != nullptr)
            {
                return 1;
            }

            if (GetAwake() == 0)
            {
                return 1;
            }

            CreateVehiclePilot();
            return 1;
        }
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

    int avoiding = 0;

    if (IsDisabled() == 0)
    {
        avoiding = CrashAvoidanceSystem();
    }

    float speed = 0.0f;

    if (avoiding == 0)
    {
        speed = Dynamics->GetVelocity();
    }

    if (GvAppearance != 0)
    {
        auto& vehicleAppearance = static_cast<MCGVAppearance&>(*Appearance);

        if (speed != 0.0)
        {
            vehicleAppearance.SetTypeId(MCGVActorState::Damaged);
        }
        else if (Refitting == 0)
        {
            vehicleAppearance.SetTypeId(MCGVActorState::Normal);
        }
        else
        {
            vehicleAppearance.SetTypeId(MCGVActorState::Extra);
        }
    }

    int visibleNow = 0;

    if (Appearance != nullptr)
    {
        Appearance->Update();
        visibleNow = OnScreen();
        Appearance->Visible = visibleNow;

        if (GvAppearance == 0)
        {
            // A pop-up turret opens for a target; its weapons work once it is up.
            const int combat = Pilot.lock()->GetLastTarget() != nullptr ||
                                       Pilot.lock()->CurTacOrder.Code == MCTacticalOrderCode::AttackPoint
                                   ? 1
                                   : 0;
            WeaponsDeployed = static_cast<MCPUAppearance*>(Appearance.get())->SetCombatMode(combat) == 2;
        }
    }

    if (Withdrawing != 0 && visibleNow == 0 && Pilot.lock()->Status != 2)
    {
        ObjType->HandleDestruction(*this, 0);
    }

    // Slopes slow the vehicle: by the hill factor times the cosine of the angle between its heading and the
    // terrain's normal.
    MCFrameOfRef turned = Frame;
    speed = -speed;
    MCVector3D normal = MCTerrain::GetTerrainNormal(Position);
    MCVector3D heading = Frame.J;
    const double headingLength =
        std::sqrt(static_cast<double>(heading.X) * heading.X + static_cast<double>(heading.Y) * heading.Y +
                  static_cast<double>(heading.Z) * heading.Z);

    if (headingLength != 0.0)
    {
        heading.X = static_cast<float>(heading.X / headingLength);
        heading.Y = static_cast<float>(heading.Y / headingLength);
        heading.Z = static_cast<float>(heading.Z / headingLength);
    }

    const double normalLength =
        std::sqrt(static_cast<double>(normal.X) * normal.X + static_cast<double>(normal.Y) * normal.Y +
                  static_cast<double>(normal.Z) * normal.Z);

    if (normalLength != 0.0)
    {
        normal.X = static_cast<float>(normal.X / normalLength);
        normal.Y = static_cast<float>(normal.Y / normalLength);
        normal.Z = static_cast<float>(normal.Z / normalLength);
    }

    const double headingDotNormal = static_cast<double>(heading.Z) * normal.Z +
                                    static_cast<double>(heading.Y) * normal.Y +
                                    static_cast<double>(heading.X) * normal.X;
    const double slope = AcosMatherr(headingDotNormal) * 57.2957795132;

    if (slope != 90.0)
    {
        speed = static_cast<float>(speed - std::cos(slope * MCMoverMath::DegreesToRadians) * GvHillSpeedFactor * speed);
    }

    MCMoverMath::RotateAboutK(turned, static_cast<float>(std::sin(MCMoverMath::HalfPi / 2.0)),
                              static_cast<float>(std::cos(MCMoverMath::HalfPi / 2.0)));
    Velocity.Y = turned.J.Y * speed;
    Velocity.X = turned.J.X * speed;
    Velocity.Z = turned.J.Z * speed;
    MCVector3D move;
    move.X = static_cast<float>(static_cast<double>(Velocity.X) * FrameLength * WorldUnitsPerMeter);
    move.Y = Velocity.Y * FrameLength * WorldUnitsPerMeter;
    move.Z = Velocity.Z * FrameLength * WorldUnitsPerMeter;

    if (NewMoveChunk != 0)
    {
        // A new move chunk: warp to its first step when too far off.
        if (StatusChunk.JumpOrder == 0)
        {
            const int32_t tileR = MoveChunk.StepPos[0][0];
            const int32_t tileC = MoveChunk.StepPos[0][1];
            MCVector3D stepPos;
            stepPos = MapTileCellToWorldPos(tileR, tileC, MoveChunk.StepPos[0][2], MoveChunk.StepPos[0][3]);
            // Original behaviour (OB-006): z is measured against 0, not the vehicle's elevation.
            const float dx = Position.X - stepPos.X;
            const float dz = -stepPos.Z;
            const float dy = Position.Y - stepPos.Y;

            if (WarpFactor < std::sqrt(dz * dz + dy * dy + dx * dx))
            {
                move.X = stepPos.X - Position.X;
                move.Y = stepPos.Y - Position.Y;
                move.Z = stepPos.Z;
            }

            if (tileR < 0 || GameMap().Height <= tileR || tileC < 0 || GameMap().Width <= tileC)
            {
                Fatal(0, " gvehicl.update: newMoveChunk stepPos not on map! ");
            }
        }

        NewMoveChunk = false;
    }

    DistanceSinceMarkSeen =
        static_cast<float>(std::sqrt(static_cast<double>(move.X) * move.X + static_cast<double>(move.Y) * move.Y +
                                     static_cast<double>(move.Z) * move.Z) +
                           DistanceSinceMarkSeen);
    MCVector3D newPosition;
    newPosition.X = move.X + Position.X;
    newPosition.Y = move.Y + Position.Y;
    newPosition.Z = move.Z + Position.Z;
    SetPosition(newPosition);

    if (IsDisabled() == 0)
    {
        UpdatePathLock(1);
    }

    SweepTime = FrameLength + SweepTime;
    MineCheck();

    // A mine layer lays one per tile: at the cell in the middle, or once it has waited MineWaitTime.
    if ((MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0) && MineLayer != 0 &&
        Pilot.lock()->CurTacOrder.MoveParams.Mode == 1 &&
        (GetObjPosition()->TileC != CellColToMine || GetObjPosition()->TileR != CellRowToMine))
    {
        MineLayTime = FrameLength + MineLayTime;

        if ((GetObjPosition()->CellC == 1 && GetObjPosition()->CellR == 1) || MineWaitTime < MineLayTime)
        {
            CellColToMine = GetObjPosition()->TileC;
            const int32_t tileR = GetObjPosition()->TileR;
            const int32_t tileC = CellColToMine;
            CellRowToMine = tileR;
            MineLayTime = 0.0f;
            MCMapTile& tile = GameMap().Map[GameMap().Width * tileR + tileC];

            if (Alignment == -1)
            {
                tile.Overlay = (tile.Overlay & 0xffffdfff) | 0x4000;
            }
            else
            {
                tile.Overlay = (tile.Overlay & 0xfffff7ff) | 0x1000;
            }

            if (MultiPlayer() != nullptr)
            {
                MultiPlayer()->AddMineChunk(tileR * 3, tileC * 3, Alignment == -1 ? 1 : 0, 2, 0);
            }
        }
    }

    Position.Z = MCTerrain::GetTerrainElevation(Position);
    return 1;
}

namespace
{
    /// <summary>A world point on <see cref="Eye"/>'s screen (the camera's inline projection).</summary>
    MCVector2D EyeProject(const MCVector3D& point)
    {
        const float sx = point.X - Eye()->Position.X;
        const float sy = point.Y - Eye()->Position.Y;
        const float dz = point.Z - Eye()->Position.Z;
        MCVector2D screen;
        screen.X = sx * Eye()->CosAngle + sy * Eye()->CosAngle + Eye()->HalfWidth;
        screen.Y = ((sx * Eye()->SinAngle + Eye()->HalfHeight) - sy * Eye()->SinAngle) - dz;
        return screen;
    }
}

auto MCGroundVehicle::Render() -> void
{
    if (GamePaused != 0)
    {
        OnScreen();
    }

    if (Withdrawing != 0 && Pilot.lock()->Status == 2)
    {
        return;
    }

    int tagged = 0;

    if (Alignment == HomeTeam()->Alignment)
    {
        if (WindowsVisible == Turn)
        {
            if (GetAwake() == 0)
            {
                if (IsRevealed() != 0)
                {
                    Appearance->Render(0);
                }
            }
            else
            {
                Appearance->Render(0);
            }

            if (Smoke != nullptr)
            {
                Smoke->Render();
            }
        }
    }
    else
    {
        const int32_t contactType = GetContactType(HomeTeam()->Id, tagged);

        if (contactType == 1)
        {
            if (WindowsVisible == Turn)
            {
                // A wreck draws behind the living.
                Appearance->Render(IsDestroyed() == 0 && IsDisabled() == 0 ? 0 : 150);

                if (Smoke != nullptr)
                {
                    Smoke->Render();
                }
            }
        }
        else if (contactType == 2)
        {
            // A sensor contact: a blip sized by tonnage.
            const std::span<uint8_t> shape = SensorBlipShape(GetTonnage());

            if (!shape.empty())
            {
                if (VfxShapeCount(shape) <= BlipFrame)
                {
                    if (SoundSystem() != nullptr && UseSound != 0)
                    {
                        SoundSystem()->PlayDigitalSample(0x14, 1, *this, false, true);
                    }

                    BlipFrame = 0;
                }

                ElementList().OpenGroup(-100000, true);
                auto& element =
                    ElementList().Make<MCVfxElement>(shape, ScreenPos.X, ScreenPos.Y, BlipFrame, 0, std::nullopt, 0);
                ElementList().Add(element);
                BlipTime = FrameLength + BlipTime;

                if (0.067 < BlipTime)
                {
                    BlipFrame = static_cast<int32_t>(BlipTime * (1.0 / 0.067) + BlipFrame + 0.5);
                    BlipTime = 0.0f;
                }
            }
        }
    }

    if (DrawExtents != 0)
    {
        // Debug: the extent radius as an ellipse.
        DrawExtentEllipse(Position, MCVector2D(GetExtentRadius(), GetExtentRadius()));
    }

    if (DrawTerrainGrid != 0)
    {
        // Debug: the move path's steps as lines.
        MCMovePath& path = Pilot.lock()->GetMovePath();
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
            MCVector2D fromScreen = EyeProject(from);
            MCVector2D toScreen = EyeProject(to);
            ElementList().OpenGroup(-100000, true);
            ElementList().Add(ElementList().Make<MCLineElement>(fromScreen, toScreen, 0xfd, std::nullopt, -100000, -1));
        }
    }

    // The selected vehicle's queued orders: waypoint markers, joined by lines when the queue is drawn as a path.
    if (!WaypointMarkerShapes().empty() && Selected != 0 && Pilot.lock() != nullptr &&
        Pilot.lock()->GetTacOrderQueueSize() > 0)
    {
        MCTacticalOrder tacOrder;
        tacOrder.Reset();
        const std::vector<MCQueuedTacOrder> queue = Pilot.lock()->GetTacOrderQueue();
        const auto numOrders = static_cast<int32_t>(queue.size());
        MCVector2D fromScreen = EyeProject(Position);
        const int32_t drawLines = DrawOrderLines;

        for (int32_t i = 0; i < numOrders; i++)
        {
            MCVector2D toScreen = EyeProject(queue[i].Point);
            tacOrder.Data[0] = queue[i].PackedData[0];
            tacOrder.Data[1] = queue[i].PackedData[1];
            tacOrder.Unpack();
            int32_t marker;

            if (tacOrder.Code == MCTacticalOrderCode::JumpToPoint || tacOrder.Code == MCTacticalOrderCode::JumpToObject)
            {
                marker = 4;
            }
            else
            {
                marker = tacOrder.MoveParams.WayPath.Mode[0] << 1;
            }

            if (drawLines != 0)
            {
                ElementList().OpenGroup(-99999, true);
                ElementList().Add(
                    ElementList().Make<MCLineElement>(fromScreen, toScreen, 0xeb, std::nullopt, -100000, -1));
                fromScreen = toScreen;
                marker++;
            }

            const int32_t bounds = VfxShapeBounds(WaypointMarkerShapes(), marker);
            ElementList().OpenGroup(-100000, true);
            auto& element = ElementList().Make<MCVfxElement>(
                WaypointMarkerShapes(), static_cast<float>((bounds >> 16) / 2) + toScreen.X,
                toScreen.Y - static_cast<float>(bounds >> 1 & 0x7fff), marker, 1, std::nullopt, 1);
            ElementList().Add(element);
        }
    }
}

auto MCGroundVehicle::RelFacingTo(MCVector3D goal, int32_t bodyPart) -> float
{
    double facing = MCMover::RelFacingTo(goal, -1);

    if (bodyPart == GroundVehicleTurret)
    {
        facing += TurretRotation;
    }

    if (facing < -180.0)
    {
        return static_cast<float>(facing + 360.0);
    }

    if (180.0f < facing)
    {
        facing = facing - 360.0;
    }

    return static_cast<float>(facing);
}

auto MCGroundVehicle::GetVitalInfo(std::span<uint8_t> vitalInfo) -> int32_t
{
    const int32_t size = MCMover::GetVitalInfo({});

    if (!vitalInfo.empty())
    {
        MCMover::GetVitalInfo(vitalInfo);
        vitalInfo[static_cast<size_t>(size)] = static_cast<uint8_t>(MovementEnabled);
    }

    return size + 1;
}

auto MCGroundVehicle::GetTotalEffectiveness() -> float
{
    if (IsDestroyed() != 0 || IsDisabled() != 0)
    {
        return 0.0f;
    }

    const float weaponRatio = MaxWeaponEffectiveness == 0.0f ? 1.0f : WeaponEffectiveness / MaxWeaponEffectiveness;
    // Each location's armor share, scaled to 0.4..1; a turret without armor counts in full.
    const auto armorFactor = [&](int32_t location)
    { return static_cast<float>(Armor[location].CurArmor / static_cast<float>(Armor[location].MaxArmor) * 0.6 + 0.4); };
    const float front = armorFactor(GroundVehicleFront);
    const float left = armorFactor(GroundVehicleLeft);
    const float right = armorFactor(GroundVehicleRight);
    const float rear = armorFactor(GroundVehicleRear);
    float turret = 1.0f;

    if (static_cast<float>(Armor[GroundVehicleTurret].MaxArmor) != 0.0)
    {
        turret = armorFactor(GroundVehicleTurret);
    }

    // Wounds wear the crew down.
    const float woundFactor[7] = {1.0f, 0.95f, 0.85f, 0.75f, 0.5f, 0.3f, 0.0f};
    int32_t wounds = static_cast<int32_t>(GetPilot()->Wounds);
    // Port fix: the original indexes the table unchecked.
    wounds = std::clamp(wounds, 0, 6);
    return turret * rear * right * left * front * woundFactor[wounds] * weaponRatio;
}
