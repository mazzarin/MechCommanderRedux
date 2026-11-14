#include "stdafx.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "main/MCOriginalBugFixes.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "engine/MCLineElement.h"
#include "engine/MCVfxElement.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCCraterManager.h"
#include "vfx/MCVfxFunctions.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCNightLights.h"
#include "mission/MCScenario.h"
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
#include "object/MCForces.h"
#include "object/MCContactSystem.h"
#include "object/MCDebris.h"
#include "object/MCDebrisType.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMoverGroup.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCMechControlData.h"
#include "object/MCNetControl.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "object/MCPlayerControl.h"
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
#include "sprite/MCParticles.h"
#include "sprite/MCWeather.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCMoverMath.h"

auto MCBattleMech::IsCrippled() -> int
{
    return LegStatus == 2 || LegStatus == 3 ? 1 : 0;
}

auto MCBattleMech::RelViewFacingTo(MCVector3D goal) -> float
{
    return RelFacingTo(goal, -1);
}

MCBattleMech::MCBattleMech()
{
    ObjectClass = MCObjectClass::BattleMech;
    Body = std::vector<MCBodyLocation>(NumMechBodyLocations);
    Armor = std::vector<MCArmorLocation>(NumMechArmorLocations);
    BlipFrame = 0;
    OverlayWeightClass = 1;
}

MCBattleMech::~MCBattleMech() = default;

auto MCBattleMech::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    int32_t result = MCGameObject::Init(objType);

    if (result != 0)
    {
        return result;
    }

    auto& mechType = static_cast<MCBattleMechType&>(*objType);
    CollisionsOn = 1;

    for (int32_t location = 0; location < NumMechBodyLocations; location++)
    {
        BodyAt(location).MaxInternalStructure = mechType.InternalStructure[location];
    }

    Chassis = mechType.Chassis;
    Alignment = mechType.MechType;
    EndoSteel = static_cast<int32_t>(mechType.EndoSteel);
    InternalStructureTonnage = mechType.InternalStructureTonnage;
    TonnageClass = mechType.TonnageClass;
    CrashAvoidSelf = mechType.CrashAvoidSelf;
    PathLockLevel = mechType.CrashBlockSelf;
    CrashAvoidPath = mechType.CrashAvoidPath;
    PathLockRange = mechType.CrashBlockPath;
    CrashYieldTime = mechType.CrashYieldTime;
    Control = nullptr;
    Dynamics = mechType.DynamicsType->CreateInstance(*this);

    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(mechType.AppearName);

    if (apprType == nullptr)
    {
        return -0x5fff7;
    }

    Appearance = std::make_unique<MCMechActor>();
    auto& actor = static_cast<MCMechActor&>(*Appearance);

    if ((apprType->AppearanceNum & 0xff000000) != 0x1000000)
    {
        return -0x5fff6;
    }

    result = actor.Init(apprType, WeakAs<MCGameObject>());

    if (result != 0)
    {
        return result;
    }

    ObjectClass = MCObjectClass::BattleMech;
    DistanceSinceMarkSeen = 1000.0f;
    return 0;
}

auto MCBattleMech::SetControl(uint32_t controlType, uint32_t controlData, int32_t controlParam) -> int32_t
{
    switch (controlType)
    {
        case 1:
            Control = std::make_unique<MCPlayerControl>(WeakAs<MCGameObject>());
            break;
        case 2:
            Control = std::make_unique<MCMechAIControl>(*this);
            break;
        case 3:
            Control = std::make_unique<MCMechNetControl>(*this);
            break;
        default:
            return -0x5fffb;
    }

    if (controlData != 1 && controlData != 0xffffffff)
    {
        return -0x5fff9;
    }

    Control->ControlData = std::make_unique<MCMechControlData>();
    return 0;
}

auto MCBattleMech::LoadProfile(MCFitIniFile& mechFile) -> int32_t
{
    static constexpr std::array<std::string_view, NumMechBodyLocations> bodyLocationNames = {
        "Head", "CenterTorso", "LeftTorso", "RightTorso", "LeftArm", "RightArm", "LeftLeg", "RightLeg"};
    static constexpr std::array<std::string_view, NumMechArmorLocations> armorLocationNames = {
        "Head",    "CenterTorso", "LeftTorso",       "RightTorso",    "LeftArm",       "RightArm",
        "LeftLeg", "RightLeg",    "RearCenterTorso", "RearLeftTorso", "RearRightTorso"};

    if (const int32_t result = mechFile.SeekBlock("Header"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> fileType = mechFile.Read<std::string>("FileType");

    if (!fileType.has_value())
    {
        return std::to_underlying(fileType.error());
    }

    if (*fileType != "MechProfile")
    {
        return -1;
    }

    if (const int32_t result = mechFile.SeekBlock("General"); result != 0)
    {
        return result;
    }

    DebugStatus = mechFile.Read<std::string>("Name").value_or("");
    ChassisBR = mechFile.Read<int32_t>("ChassisBR").value_or(100);
    MCFitReader read(mechFile);
    read.Value("CurTonnage", Tonnage);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    DescIndex = mechFile.Read<int32_t>("DescIndex").value_or(-1);
    IfaceName = LoadGameString(static_cast<uint32_t>(DescIndex + 300), 0xfe);
    read.Value("NameIndex", NameIndex);
    read.Value("NameVariant", NameVariant);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    PilotId = mechFile.Read<int32_t>("Pilot").value_or(-1);
    Status = 0;
    read.Value("icon", IconName);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    NotMineYet = mechFile.Read<bool>("NotMineYet").value_or(true);

    if (const int32_t result = mechFile.SeekBlock("Engine"); result != 0)
    {
        return result;
    }

    uint8_t runSpeed = 0;
    read.Value("Tonnage", EngineTonnage);
    read.Value("Rating", EngineRating);
    read.Value("MaxRunSpeed", runSpeed);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    MaxRunSpeed = static_cast<float>(runSpeed);

    if (mechFile.SeekBlock("MovementSystem") == 0)
    {
        // Original behaviour (OB-149), as the type's: the yield time gets the last long read (zero when missing).
        int32_t value = 0;
        const auto readLong = [&](std::string_view name)
        {
            const MCFitResult<int32_t> result = mechFile.Read<int32_t>(name);

            if (result.has_value())
            {
                value = *result;
                return true;
            }

            if (result.error() == MCFitError::VariableNotFound)
            {
                value = 0;
            }

            return false;
        };

        if (readLong("CrashAvoidSelf"))
        {
            CrashAvoidSelf = value;
        }

        if (readLong("CrashAvoidPath"))
        {
            CrashAvoidPath = value;
        }

        if (readLong("CrashBlockSelf"))
        {
            PathLockLevel = value;
        }

        if (readLong("CrashBlockPath"))
        {
            PathLockRange = value;
        }

        if (mechFile.Read<float>("CrashYieldTime").has_value())
        {
            CrashYieldTime = static_cast<float>(value);
        }
    }

    if (const int32_t result = mechFile.SeekBlock("Armor"); result != 0)
    {
        return result;
    }

    read.Value("Type", ArmorType);
    read.Value("Tonnage", ArmorTonnage);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = mechFile.SeekBlock("MaxArmorPoints"); result != 0)
    {
        return result;
    }

    for (int32_t location = 0; location < NumMechArmorLocations; location++)
    {
        read.Value(armorLocationNames[location], Armor[location].MaxArmor);
    }

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = mechFile.SeekBlock("CurArmorPoints"); result != 0)
    {
        return result;
    }

    for (int32_t location = 0; location < NumMechArmorLocations; location++)
    {
        uint8_t points = 0;
        read.Value(armorLocationNames[location], points);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        Armor[location].CurArmor = static_cast<float>(points);
    }

    if (const int32_t result = mechFile.SeekBlock("InventoryInfo"); result != 0)
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
    const int32_t numItems = NumAmmos + NumOther + NumWeapons;
    Inventory = std::vector<MCInventoryItem>(static_cast<size_t>(numItems));
    NumAntiMissileSystems = 0;

    for (int32_t item = 0; item < firstWeapon; item++)
    {
        if (const int32_t result = mechFile.SeekBlock(std::format("Item:{}", item)); result != 0)
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

        if (MasterComponentList[other.MasterID].Form == MCComponentForm::JumpJet)
        {
            NumJumpJets++;
        }
    }

    for (int32_t item = firstWeapon; item < firstAmmo; item++)
    {
        if (const int32_t result = mechFile.SeekBlock(std::format("Item:{}", item)); result != 0)
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
        // Damage per ten seconds, then scaled by the long range over 24.
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
        if (const int32_t result = mechFile.SeekBlock(std::format("Item:{}", item)); result != 0)
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

        if (const MCFitResult<int32_t> longAmount = mechFile.Read<int32_t>("Amount"); longAmount.has_value())
        {
            amount = *longAmount;
        }
        else
        {
            const MCFitResult<uint8_t> byteAmount = mechFile.Read<uint8_t>("Amount");

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
        ammo.AmmoIndex = -1;
        ammo.StartAmount = ammo.Amount;
        ammo.Health = MasterComponentList[ammo.MasterID].Health;
        ammo.Disabled = 0;
        ammo.ReadyTime = 0.0f;
        ammo.BodyLocation = 0xff;
    }

    for (int32_t location = 0; location < NumMechBodyLocations; location++)
    {
        if (const int32_t result = mechFile.SeekBlock(bodyLocationNames[location]); result != 0)
        {
            return result;
        }

        MCBodyLocation& bodyLocation = BodyAt(location);
        uint8_t hasCase = 0;
        uint8_t internalStructure = 0;
        read.Value("CASE", hasCase);
        bodyLocation.HasCase = hasCase;
        read.Value("CurInternalStructure", internalStructure);
        bodyLocation.CurInternalStructure = static_cast<float>(internalStructure);
        read.Value("HotSpotNumber", bodyLocation.HotSpotNumber);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        const float structureLeft =
            bodyLocation.CurInternalStructure / static_cast<float>(bodyLocation.MaxInternalStructure);

        if (structureLeft == 0.0f)
        {
            bodyLocation.DamageState = 2;
        }
        else if (structureLeft > 0.5f)
        {
            bodyLocation.DamageState = 0;
        }
        else
        {
            bodyLocation.DamageState = 1;
        }

        const int32_t numSpaces = NumLocationCriticalSpaces[location];
        bodyLocation.CriticalSpaces.resize(static_cast<size_t>(numSpaces));
        bodyLocation.TotalSpaces = 0;

        for (int32_t space = 0; space < numSpaces; space++)
        {
            // The inventory item in the space (0xff for none) and whether it is hit.
            std::array<uint8_t, 2> entry{};

            if (const MCFitResult<uint32_t> result =
                    mechFile.ReadArray<uint8_t>(std::format("Component:{}", space), entry);
                !result.has_value())
            {
                return std::to_underlying(result.error());
            }

            MCCriticalSpace& criticalSpace = BodyAt(location).CriticalSpaces[space];
            criticalSpace.InventoryID = entry[0];
            criticalSpace.Hit = entry[1];

            if (entry[0] == 0xff)
            {
                continue;
            }

            MCInventoryItem& item = Inventory[entry[0]];
            item.BodyLocation = static_cast<uint8_t>(location);
            BodyAt(location).TotalSpaces += static_cast<int8_t>(MasterComponentList[item.MasterID].CriticalSpacesReq);
            const uint32_t masterID = item.MasterID;

            switch (MasterComponentList[masterID].Form)
            {
                case MCComponentForm::Cockpit:
                    Cockpit = entry[0];
                    break;
                case MCComponentForm::Sensor:
                {
                    Sensor = entry[0];
                    SensorSystem = SensorSystemManager().NewSensor();
                    SensorSystem.lock()->Owner = SharedAs<MCGameObject>();
                    SensorSystem.lock()->SetRange(MasterComponentList[Inventory[Sensor].MasterID].RangeOrHeat);
                    break;
                }
                case MCComponentForm::Actuator:
                {
                    if (static_cast<int32_t>(masterID) == MasterArmActuatorID)
                    {
                        if (location == MechLeftArm)
                        {
                            LeftArmActuator = entry[0];
                        }
                        else if (location == MechRightArm)
                        {
                            RightArmActuator = entry[0];
                        }
                    }
                    else if (static_cast<int32_t>(masterID) == MasterLegActuatorID)
                    {
                        if (location == MechLeftLeg)
                        {
                            LeftLegActuator = entry[0];
                        }
                        else if (location == MechRightLeg)
                        {
                            RightLegActuator = entry[0];
                        }
                    }
                    break;
                }
                case MCComponentForm::Engine:
                    Engine = entry[0];
                    break;
                case MCComponentForm::HeatSink:
                case MCComponentForm::Weapon:
                case MCComponentForm::WeaponEnergy:
                case MCComponentForm::WeaponMissile:
                    item.BodyLocation = static_cast<uint8_t>(location);
                    break;
                case MCComponentForm::WeaponBallistic:
                {
                    item.BodyLocation = static_cast<uint8_t>(location);

                    if (static_cast<int32_t>(masterID) == MasterClanAntiMissileSystemID ||
                        static_cast<int32_t>(masterID) == MasterInnerSphereAntiMissileSystemID)
                    {
                        if (NumAntiMissileSystems == MaxAntiMissileSystems)
                        {
                            Fatal(0, "Too many Anti-Missile Systems");
                        }

                        AntiMissileSystem[NumAntiMissileSystems] = entry[0];
                        NumAntiMissileSystems++;
                    }
                    break;
                }
                case MCComponentForm::Ammo:
                    item.BodyLocation = static_cast<uint8_t>(location);
                    break;
                case MCComponentForm::LifeSupport:
                    LifeSupport = entry[0];
                    break;
                case MCComponentForm::Gyroscope:
                    Gyro = entry[0];
                    break;
                case MCComponentForm::Ecm:
                    Ecm = entry[0];
                    break;
                case MCComponentForm::Probe:
                    Probe = entry[0];
                    break;
                case MCComponentForm::Jammer:
                    Jammer = entry[0];
                    break;
                default:
                    break;
            }
        }
    }

    CalcAmmoTotals();

    // Each weapon (and anti-missile system) finds its ammunition's tally, and each ammunition bin its own.
    const auto ammoTallyOf = [this](int32_t ammoMasterId) -> int16_t
    {
        for (int32_t ammoType = 0; ammoType < NumAmmoTypes(); ammoType++)
        {
            if (ammoMasterId == AmmoTypeTotal[ammoType].MasterId)
            {
                return static_cast<int16_t>(ammoType);
            }
        }

        return -1;
    };

    const auto setAmmoIndex = [](MCInventoryItem& item, int16_t tally)
    {
        if (tally >= 0)
        {
            item.AmmoIndex = tally;
        }
    };

    for (int32_t item = firstWeapon; item < firstAmmo; item++)
    {
        setAmmoIndex(Inventory[item],
                     ammoTallyOf(static_cast<int32_t>(MasterComponentList[Inventory[item].MasterID].AmmoMasterId)));
    }

    for (int32_t item = firstAmmo; item < numItems; item++)
    {
        setAmmoIndex(Inventory[item], ammoTallyOf(static_cast<int32_t>(Inventory[item].MasterID)));
    }

    for (int32_t item = 0; item < firstWeapon; item++)
    {
        const int32_t masterID = Inventory[item].MasterID;

        if (masterID == MasterClanAntiMissileSystemID || masterID == MasterInnerSphereAntiMissileSystemID)
        {
            setAmmoIndex(Inventory[item],
                         ammoTallyOf(static_cast<int32_t>(MasterComponentList[masterID].AmmoMasterId)));
        }
    }

    CalcLongestRangeWeapon();
    CalcLegStatus();
    CalcTorsoStatus();
    MaxCV = CalcCV(1);
    CurCV = CalcCV(0);
    MaxTargetDamage = CalcMaxTargetDamage();

    if (ObjType->ExplosionObject > 0)
    {
        ObjectTypeManager().Load(ObjType->ExplosionObject, 1);
    }

    MechClass = static_cast<uint8_t>(GetMechClass());
    return 0;
}

auto MCBattleMech::CalcCV(int calcMax) -> int32_t
{
    double cv = ChassisBR;
    const int32_t numItems = NumAmmos + NumWeapons + NumOther;

    for (int32_t i = 0; i < numItems; i++)
    {
        if (calcMax != 0 || Inventory[i].Disabled == 0)
        {
            cv += MasterComponentList[Inventory[i].MasterID].BattleRating;
        }
    }

    return static_cast<int32_t>(cv);
}

auto MCBattleMech::CalcLegStatus() -> int32_t
{
    const uint8_t leftLeg = BodyAt(MechLeftLeg).DamageState;

    if (BodyAt(MechRightLeg).DamageState == 2)
    {
        if (leftLeg == 2)
        {
            LegStatus = 3;

            if (Pilot.lock() != nullptr)
            {
                Pilot.lock()->TriggerAlarm(MCPilotAlarmType::VehicleIncapacitated, 0x42);
                return LegStatus;
            }

            return LegStatus;
        }

        if (LegStatus == 2)
        {
            return LegStatus;
        }
    }
    else if (leftLeg != 2)
    {
        LegStatus = 0;
        return LegStatus;
    }

    Pilot.lock()->RadioMessage(MCRadioMessageType::Crippled, 0);
    LegStatus = 2;
    return 2;
}

auto MCBattleMech::CalcTorsoStatus() -> int32_t
{
    if (BodyAt(MechCenterTorso).DamageState == 1)
    {
        TorsoStatus = 1;
        return 1;
    }

    TorsoStatus = 0;
    return TorsoStatus;
}

auto MCBattleMech::CanPowerUp() -> int
{
    return 1;
}

auto MCBattleMech::GetPositionFromHS(uint32_t hotSpot) -> MCVector3D
{
    auto& mechType = TypeAs<MCBattleMechType>();

    if (mechType.NumOthers + mechType.NumWeapons <= hotSpot)
    {
        hotSpot = 0;
    }

    auto& actor = static_cast<MCMechActor&>(*Appearance);
    const uint32_t gesture = MCMechActor::GetHotSpotIndex(static_cast<uint32_t>(actor.CurrentGesture));
    int32_t frameNumber = actor.CurrentFrame[0];
    const std::span<const float> offsets = mechType.GestureHotSpots(gesture);
    const int32_t numFrames = static_cast<int32_t>(mechType.NumFramesPerHotSpot[gesture]);

    if (numFrames <= frameNumber)
    {
        frameNumber = numFrames - 1;
    }

    // Port fix: some packets hold fewer hot spots than numWeapons + numOthers (cm.hsp's gestures 0-14 hold 3 of 6),
    // and the original reads past them into the heap. Read hot spot 0's offset, as the range check above does (the
    // mount's turn below still uses the real hot spot).
    uint32_t dataHotSpot = hotSpot;

    if constexpr (FixShortHotSpotPackets)
    {
        if (numFrames > 0 && gesture < mechType.HotSpotPacketShippedFloats.size() &&
            mechType.HotSpotPacketShippedFloats[gesture] / (static_cast<uint32_t>(numFrames) * 3) <= dataHotSpot)
        {
            dataHotSpot = 0;
        }
    }

    const int32_t index = numFrames * static_cast<int32_t>(dataHotSpot) + frameNumber;
    const float offsetX = offsets[index * 3];
    const float offsetY = offsets[index * 3 + 1];
    const float offsetZ = offsets[index * 3 + 2];

    // The body's facing, plus the torso's (and an arm's) for the weapons mounted on them.
    const double exactFacing = MCMoverMath::ExactFrameFacing(Frame);
    const float facing = static_cast<float>(exactFacing);
    double turned = exactFacing;

    if (hotSpot < mechType.NumWeapons)
    {
        switch (mechType.WeaponHotSpots[hotSpot])
        {
            case 1:
                turned = static_cast<double>(facing) + TorsoRotation;
                break;
            case 2:
                turned = static_cast<double>(LeftArmRotation) + TorsoRotation + facing;
                break;
            case 3:
                turned = static_cast<double>(RightArmRotation) + TorsoRotation + facing;
                break;
            default:
                break;
        }
    }
    else if (hotSpot < mechType.NumWeapons + 3)
    {
        turned = static_cast<double>(facing) + TorsoRotation;
    }

    return PlaceHotSpot(offsetX, offsetY, offsetZ, turned);
}

auto MCBattleMech::PlaceHotSpot(float offsetX, float offsetY, float offsetZ, double turned) const -> MCVector3D
{
    double s;
    double c;
    MCMoverMath::SnappedFacing(turned, s, c);
    MCVector3D result;
    result.X = static_cast<float>(c * offsetX + s * offsetY) * 20.0f + Position.X;
    result.Z = offsetZ * 20.0f + Position.Z;
    result.Y = static_cast<float>((c * offsetY - s * offsetX) * 20.0f + Position.Y);
    return result;
}

auto MCBattleMech::CockpitPosition() -> std::optional<MCVector3D>
{
    const auto& mechType = TypeAs<MCBattleMechType>();
    const uint32_t cockpit = mechType.NumWeapons + CockpitHotSpot;
    // Whether gesture's packet holds the cockpit's offsets.
    const auto holds = [&](uint32_t gesture)
    {
        const uint32_t frames = mechType.NumFramesPerHotSpot[gesture];
        return frames > 0 && gesture < mechType.HotSpotPacketShippedFloats.size() &&
               mechType.HotSpotPacketShippedFloats[gesture] / (frames * 3) > cockpit;
    };

    if (mechType.NumOthers + mechType.NumWeapons <= cockpit)
    {
        return std::nullopt;
    }

    const auto& actor = static_cast<MCMechActor&>(*Appearance);

    if (holds(MCMechActor::GetHotSpotIndex(static_cast<uint32_t>(actor.CurrentGesture))))
    {
        return GetPositionFromHS(cockpit);
    }

    for (uint32_t gesture = 0; gesture < mechType.NumHotSpotPackets; ++gesture)
    {
        if (holds(gesture))
        {
            const std::span<const float> offsets = mechType.GestureHotSpots(gesture);
            const size_t index = static_cast<size_t>(mechType.NumFramesPerHotSpot[gesture]) * cockpit * 3;
            const double turned = MCMoverMath::ExactFrameFacing(Frame) + TorsoRotation;
            return PlaceHotSpot(offsets[index], offsets[index + 1], offsets[index + 2], turned);
        }
    }

    return std::nullopt;
}

auto MCBattleMech::OnScreen() -> int
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

    const bool shown = Appearance != nullptr && Appearance->RecalcBounds(*camera) != 0;

    // Port-only (SPEC-first-use.md: the camera's surroundings): in or near the view, each part's packet is read whole
    // ahead of its frames' first draws, so a pan or its walk brings it into view with them in hand, and a packet drawn
    // is read in one go, not a frame at a time.
    if (Appearance != nullptr)
    {
        Appearance->ReadAheadNear(*camera, shown);
    }

    if (shown)
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

namespace
{
    /// <summary>The 4-byte value in slot <paramref name="slot"/> of a 32-byte foot hot spot packet.</summary>
    template <typename T> T HotSpotValue(std::span<const uint8_t> packet, size_t slot)
    {
        std::array<uint8_t, sizeof(T)> bytes{};
        std::ranges::copy(packet.subspan(slot * sizeof(T), sizeof(T)), bytes.begin());
        return std::bit_cast<T>(bytes);
    }

    /// <summary>Pi, as MCX.EXE stores it (a hair under the true value).</summary>
    constexpr double MCX_PI = 0x1.921fb5443e88cp+1;

    /// <summary>
    /// Turns (<paramref name="x"/>, <paramref name="y"/>) by <paramref name="degrees"/> (MC2's inline Rotate): 45 and
    /// -45 exactly, otherwise the sine at full precision and the cosine through a float angle.
    /// </summary>
    void RotateXY(float& x, float& y, float degrees)
    {
        double s;
        double c;

        if (degrees == 45.0f)
        {
            s = 0.70710677f;
            c = 0.70710677f;
        }
        else if (degrees == -45.0f)
        {
            s = -0.70710677f;
            c = 0.70710677f;
        }
        else
        {
            s = std::sin(static_cast<double>(degrees) * MCMoverMath::DegreesToRadians);
            c = static_cast<float>(
                std::cos(static_cast<double>(static_cast<float>(degrees * MCMoverMath::DegreesToRadians))));
        }

        const double oldX = x;
        x = static_cast<float>(c * x + s * y);
        y = static_cast<float>(c * y - s * oldX);
    }

    /// <summary>Turns (<paramref name="x"/>, <paramref name="y"/>) half a circle, by MCX.EXE's pi.</summary>
    void RotateXYHalf(float& x, float& y)
    {
        const double s = std::sin(MCX_PI);
        const double c = std::cos(MCX_PI);
        const double oldX = x;
        x = static_cast<float>(c * x + s * y);
        y = static_cast<float>(c * y - s * oldX);
    }

    /// <summary>A frame's facing in degrees from the world's x axis, negative when its i axis points to -y.</summary>
    float FrameFacing(MCFrameOfRef& frame)
    {
        return static_cast<float>(MCMoverMath::ExactFrameFacing(frame));
    }

    /// <summary>
    /// Throws off an arm (debris type <paramref name="debrisId"/>): framed the torso's way, flying sideways at a
    /// random angle from <paramref name="angle"/>, painted as the mech.
    /// </summary>
    void ThrowArm(MCBattleMech& mech, uint32_t debrisId, float angle, const MCDrawnArm& arm, float blow)
    {
        std::shared_ptr<MCGameObject> piece = CreateObject(static_cast<int32_t>(debrisId));

        if (piece == nullptr)
        {
            return;
        }

        MCFrameOfRef armFrame = mech.Frame;
        const double torso = static_cast<double>(mech.TorsoRotation) * MCMoverMath::DegreesToRadians;
        MCMoverMath::RotateAboutK(armFrame, static_cast<float>(std::sin(torso)), static_cast<float>(std::cos(torso)));
        piece->SetFrame(armFrame);
        MCVector3D flight = mech.Frame.J;
        const float length = std::sqrt(flight.X * flight.X + flight.Y * flight.Y + flight.Z * flight.Z);

        if (length != 0.0f)
        {
            flight.X = flight.X / length;
            flight.Y = flight.Y / length;
            flight.Z = flight.Z / length;
        }

        auto& debris = static_cast<MCDebris&>(*piece);
        debris.RandomAngle(angle);
        RotateXY(flight.X, flight.Y, angle);

        if (FrameFacing(armFrame) >= 0.0f)
        {
            RotateXYHalf(flight.X, flight.Y);
        }

        piece->SetVelocity(flight);
        piece->SetPosition(mech.Position);
        const int32_t paint = static_cast<MCMechActor*>(mech.Appearance.get())->FadeTableIndex;
        debris.SetPaintScheme(paint);
        // Port-only (HD art's arms): drawn as the mech's own arm, torn off.
        debris.TearOff(mech, arm, paint, blow);

        AddToDefaultList(std::move(piece));
    }

    /// <summary>
    /// Leaves a footprint at hot spot offset (<paramref name="offsetX"/>, <paramref name="offsetY"/>) turned by
    /// -<paramref name="angle"/> degrees, rotation <paramref name="direction"/> (of 16), with a step sound. Port-only
    /// (HD particles, HD weather): the foot splashes in water or a puddle.
    /// </summary>
    void MakeFootprint(MCBattleMech& mech, float offsetX, float offsetY, float angle, int32_t direction)
    {
        RotateXY(offsetX, offsetY, -angle);
        MCVector3D printPos;
        printPos.Z = mech.Position.Z;
        printPos.X = offsetX * 20.0f + mech.Position.X;
        printPos.Y = offsetY * 20.0f + mech.Position.Y;
        const int32_t printType = mech.TypeAs<MCBattleMechType>().FootprintType;
        CraterManager().AddCrater(printType, printPos, direction);
        SoundSystem()->PlayDigitalSample(0xd, 1, mech, false, false);
        const float halfWidth = static_cast<MCMechActor*>(mech.Appearance.get())->ShapeMaxX * 0.5f;
        MCParticles::AddStep(printPos, halfWidth);
        const std::span<const std::vector<uint8_t>> prints = CraterManager().Shapes();
        MCWeather::AddStep(printPos,
                           printType >= 0 && static_cast<size_t>(printType) < prints.size()
                               ? std::span<const uint8_t>(prints[static_cast<size_t>(printType)])
                               : std::span<const uint8_t>(),
                           direction);
    }

    /// <summary>The frames either side of a foot's frame (its hot spot packet's) its print may be made in.</summary>
    constexpr int32_t FootFrameSlack = 2;

    /// <summary>A footprint's rotation (of 16) for <paramref name="degrees"/>.</summary>
    int32_t FootprintDirection(float degrees)
    {
        auto direction = static_cast<int32_t>(std::floor(static_cast<double>(degrees * (1.0f / 22.5f))));

        if (direction < 0)
        {
            direction += 16;
        }

        return direction;
    }
}

auto MCBattleMech::Update() -> int32_t
{
    TerrainNormal = MCTerrain::GetTerrainNormal(Position);
    UpdatePathLock(0);

    if (IsDestroyed() != 0 || IsDisabled() != 0)
    {
        CollisionsOn = 0;
    }

    if (Withdrawing != 0 && Pilot.lock()->Status == 2)
    {
        CollisionsOn = 0;
        return 1;
    }

    auto& actor = static_cast<MCMechActor&>(*Appearance);

    if (IsDestroyed() != 0)
    {
        if (JumpFX[0] != nullptr || JumpFX[1] != nullptr)
        {
            EndJumpFX();
        }

        int32_t result = Dynamics->Update();

        if (result != 1)
        {
            return result;
        }

        // The wreck keeps sliding along its frame's j axis turned an eighth of a circle.
        const float speed = -actor.GetVelocityMagnitude();
        MCFrameOfRef turned = Frame;
        MCMoverMath::RotateAboutK(turned, static_cast<float>(std::sin(MCMoverMath::HalfPi / 2.0)),
                                  static_cast<float>(std::cos(MCMoverMath::HalfPi / 2.0)));
        Velocity.X = speed * turned.J.X;
        Velocity.Y = speed * turned.J.Y;
        Velocity.Z = speed * turned.J.Z;
        MCVector3D newPosition;
        newPosition.X =
            static_cast<float>(static_cast<double>(Velocity.X) * FrameLength * WorldUnitsPerMeter + Position.X);
        newPosition.Y = Velocity.Y * FrameLength * WorldUnitsPerMeter + Position.Y;
        newPosition.Z = Velocity.Z * FrameLength * WorldUnitsPerMeter + Position.Z;
        SetPosition(newPosition);
        const int visibleNow = OnScreen();

        actor.SetGestureGoal(8);
        actor.Visible = visibleNow;
        actor.SetCombatMode(false);
        result = actor.Update();

        if (result != 1)
        {
            return result;
        }

        // Once the death animation is done, it blows up and leaves a crater.
        if (LyingDead == 0)
        {
            LyingDead = actor.LyingStill;
        }

        if (LyingDead != 0)
        {
            DeathTimer -= FrameLength;

            if (DeathTimer < 0.4 && DeathExplosionDone == 0)
            {
                auto& mechType = TypeAs<MCBattleMechType>();
                mechType.CreateExplosion(Position, mechType.ExplDmg, mechType.ExplRad);
                DeathExplosionDone = true;
                return 1;
            }

            if (DeathTimer < 0.0 && WreckDone == 0)
            {
                actor.Wrecked = true;
                CraterManager().AddCrater(6, Position, 0);
                TacticalInterface()->RemoveMech(PartId);
                WreckDone = true;
                return 1;
            }
        }
    }
    else
    {
        if (GetAwake() != 0 && IsDisabled() == 0 && Scenario()->GodMode == 0)
        {
            const bool moved = MCTerrain::MetersPerVertex <= DistanceSinceMarkSeen;

            if (moved)
            {
                // Every vertex travelled, the mech marks what it sees.
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

            // Port-only (Redux HD): at night its headlight shows the ground ahead, turned with its torso, and gives it
            // away (mission/MCNightLights.h).
            MCNightLights::Mark(*this, TorsoRotation, moved);
        }

        if (DeselectTime != 0.0f && DeselectTime < ScenarioTime)
        {
            DeselectTime = 0.0f;
            Selected = 0;
        }

        int32_t result = Control->Update();

        if (result != 1)
        {
            return result;
        }

        if (GetAwake() == 0 && actor.SetGestureGoal(0) == 0)
        {
            ShutDownThisFrame = false;
        }

        result = Dynamics->Update();

        if (result != 1)
        {
            return result;
        }

        int avoiding = 0;

        if (IsDisabled() == 0)
        {
            // The original looks at the pilot's attack order here and does nothing with it.
            if (GetPilot()->CurTacOrder.Code == MCTacticalOrderCode::AttackObject &&
                GetPilot()->CurTacOrder.AttackParams.Method == 2)
            {
                GetPilot();
            }

            avoiding = CrashAvoidanceSystem();
        }

        float speed = 0.0f;

        if (avoiding == 0)
        {
            speed = actor.GetVelocityMagnitude();
        }

        const int32_t gesture = actor.CurrentGesture;
        MCFrameOfRef turned = Frame;
        speed = -speed;

        if (gesture == 20)
        {
            // Jumping: the actor's jump velocity.
            float jumpSpeed = 0.0f;

            if (actor.Airborne != 0)
            {
                jumpSpeed = actor.GetVelocityMagnitude();
            }

            Velocity.X = jumpSpeed * actor.JumpDirection.X;
            Velocity.Y = jumpSpeed * actor.JumpDirection.Y;
            Velocity.Z = jumpSpeed * actor.JumpDirection.Z;
        }
        else
        {
            MCMoverMath::RotateAboutK(turned, static_cast<float>(std::sin(MCMoverMath::HalfPi / 2.0)),
                                      static_cast<float>(std::cos(MCMoverMath::HalfPi / 2.0)));
            Velocity.Y = turned.J.Y * speed;
            Velocity.X = turned.J.X * speed;
            Velocity.Z = turned.J.Z * speed;

            if (JumpFX[0] != nullptr || JumpFX[1] != nullptr)
            {
                EndJumpFX();
            }
        }

        // Port-only (MechBraking): a quick stop never walks into a cell another unit holds (its own locks are
        // off while it updates): it stops dead short of one, as the original.
        if (actor.QuickStopped && actor.InTransition && (Velocity.X != 0.0f || Velocity.Y != 0.0f))
        {
            const float metres = std::hypot(Velocity.X, Velocity.Y);
            const float ahead = (metres * FrameLength + QuickStopClearance) / metres * WorldUnitsPerMeter;
            const MCVector3D look(Position.X + Velocity.X * ahead, Position.Y + Velocity.Y * ahead, Position.Z);
            int32_t tileR = 0;
            int32_t tileC = 0;
            int32_t cellR = 0;
            int32_t cellC = 0;
            MCScenarioMap::WorldToMapPos(look, tileR, tileC, cellR, cellC);
            const std::shared_ptr<MCObjectPosition> at = ObjPosition.lock();
            const bool own =
                at != nullptr && at->TileR == tileR && at->TileC == tileC && at->CellR == cellR && at->CellC == cellC;
            const MCScenarioMap& map = GameMap();

            if (!own && (!map.OnMap(tileR, tileC) || map.GetCellPathLocked(tileR, tileC, cellR, cellC) ||
                         map.TileAt(tileR, tileC).GetCellPassable(cellR, cellC) == 0))
            {
                Velocity = MCVector3D(0.0f, 0.0f, 0.0f);
                actor.EndQuickStop();
            }
        }

        const float velocityZ = Velocity.Z;
        MCVector3D move;
        move.X = static_cast<float>(static_cast<double>(Velocity.X) * FrameLength * WorldUnitsPerMeter);
        move.Y = Velocity.Y * FrameLength * WorldUnitsPerMeter;
        Velocity.Z = 0.0f;
        move.Z = velocityZ * FrameLength * WorldUnitsPerMeter;

        if (NewMoveChunk != 0)
        {
            // A new move chunk: warp to its first step when too far off.
            if (StatusChunk.JumpOrder == 0)
            {
                const int32_t tileR = MoveChunk.StepPos[0][0];
                const MCVector3D stepPos = MapTileCellToWorldPos(tileR, MoveChunk.StepPos[0][1],
                                                                 MoveChunk.StepPos[0][2], MoveChunk.StepPos[0][3]);
                // Original behaviour (OB-006): measures z against 0, not the mech's elevation.
                const float dx = Position.X - stepPos.X;
                const float dz = -stepPos.Z;
                const float dy = Position.Y - stepPos.Y;

                if (WarpFactor < std::sqrt(dx * dx + dz * dz + dy * dy))
                {
                    move.X = stepPos.X - Position.X;
                    move.Y = stepPos.Y - Position.Y;
                    move.Z = stepPos.Z;
                }

                if (tileR < 0 || GameMap().Height <= tileR || MoveChunk.StepPos[0][1] < 0 ||
                    GameMap().Width <= MoveChunk.StepPos[0][1])
                {
                    Fatal(0, " mech.update: newMoveChunk stepPos not on map! ");
                }
            }

            NewMoveChunk = false;
        }

        MCVector3D newPosition;
        newPosition.X = move.X + Position.X;
        newPosition.Y = move.Y + Position.Y;
        newPosition.Z = move.Z + Position.Z;
        SetPosition(newPosition);
        DistanceSinceMarkSeen =
            static_cast<float>(std::sqrt((static_cast<double>(move.Y) * move.Y + static_cast<double>(move.Z) * move.Z) +
                                         static_cast<double>(move.X) * move.X) +
                               DistanceSinceMarkSeen);

        if (IsDisabled() == 0)
        {
            UpdatePathLock(1);
        }

        MineCheck();
        Position.Z = MCTerrain::GetTerrainElevation(Position);

        // Arms blown off this frame fly off to the side they were on.
        const float facing = FrameFacing(Frame);
        auto& controlData = static_cast<MCMechControlData&>(*Control->ControlData);
        auto& mechType = TypeAs<MCBattleMechType>();

        if (controlData.BlowRightArm != 0)
        {
            if (0.0f <= facing + TorsoRotation)
            {
                ThrowArm(*this, mechType.LeftArmDebrisId, -180.0f, actor.DrawnRightArm, RightArmBlow);
            }
            else
            {
                ThrowArm(*this, mechType.RightArmDebrisId, 0.0f, actor.DrawnRightArm, RightArmBlow);
            }

            actor.RightArmGone = true;
        }

        if (controlData.BlowLeftArm != 0)
        {
            if (0.0f <= facing + TorsoRotation)
            {
                ThrowArm(*this, mechType.RightArmDebrisId, 0.0f, actor.DrawnLeftArm, LeftArmBlow);
            }
            else
            {
                ThrowArm(*this, mechType.LeftArmDebrisId, -180.0f, actor.DrawnLeftArm, LeftArmBlow);
            }

            actor.LeftArmGone = true;
        }

        const int visibleNow = OnScreen();

        if (Withdrawing != 0 && visibleNow == 0 && Pilot.lock()->Status != 2)
        {
            ObjType->HandleDestruction(*this, 0);
        }

        actor.Visible = visibleNow;
        actor.SetMovePath(Pilot.lock()->GetMovePath());
        int combat = 1;

        if (Pilot.lock()->GetLastTarget() == nullptr &&
            Pilot.lock()->CurTacOrder.Code != MCTacticalOrderCode::AttackObject &&
            Pilot.lock()->CurTacOrder.Code != MCTacticalOrderCode::AttackPoint)
        {
            combat = 0;
        }

        actor.SetCombatMode(combat);
        actor.Update();

        if (IsJumping() == 0)
        {
            if (IsDestroyed() == 0 && IsDisabled() == 0)
            {
                CollisionsOn = 1;
            }
        }
        else
        {
            CollisionsOn = 0;
        }

        // Footprints: each foot prints once when its hot spot packet's frame comes round (within two frames), and
        // is re-armed by the walking gestures once past it. Port-only (HDStops): as the legs are drawn.
        if (visibleNow != 0 && FootPrints != 0 && gesture != 20 && IsRevealed() != 0)
        {
            const int32_t gestureNow = actor.DrawnGesture();
            const uint32_t packetIndex = MCMechActor::GetHotSpotIndex(static_cast<uint32_t>(gestureNow));
            const int32_t frameNow = actor.DrawnLegsFrame();

            // The original's test let the index equal the packet count, reading the 32 bytes past the data.
            if (packetIndex < mechType.NumHotSpotPackets)
            {
                // A packet is 8 values of 4 bytes: the first foot's frame (int), its x, y offsets (floats), one
                // unused, then the same for the second foot.
                const std::span<const uint8_t> packet =
                    std::span<const uint8_t>(mechType.HotSpotData).subspan(packetIndex * 0x20, 0x20);
                const int walking = gestureNow == 4 || gestureNow == 7 || gestureNow == 11;
                const float stepFacing = FrameFacing(Frame);
                const auto snapped = static_cast<int32_t>(std::floor(static_cast<double>(stepFacing * 0.025f)));
                // OB-185 (fixed with HDFootprints): the original printed a foot as its legs came within two frames of
                // its frame, so two early, behind the foot by that much walking; turned the foot's offset by the
                // facing floored to 40 degrees (its legs' 9 stored facings taken for the whole circle; they're
                // mirrored to 17), as much as 40 degrees off the legs as drawn; and took the legs as mirrored left of
                // straight ahead and straight back, which are drawn unmirrored, printing each foot at the other's
                // place there. A print could land between the feet. With HDFootprints a foot prints at its frame (or
                // up to two after, should frames be skipped), turned and mirrored as its legs are drawn (with HDTurns,
                // as last drawn in this gesture, eased, maybe as a turn middle; else as drawn at the eased facing).
                const bool drawnInGesture =
                    GHdTurns != 0 && actor.DrawnLegs.has_value() && actor.DrawnLegsGesture == gestureNow;
                const float legsFacing =
                    GHdTurns != 0 && actor.TurnedFacing.has_value() ? *actor.TurnedFacing : stepFacing;
                const std::optional<MCDrawnFacing> drawn = GHdFootprints == 0 ? std::nullopt
                                                           : drawnInGesture
                                                               ? actor.DrawnLegs
                                                               : actor.MechTree->DrawnFacing(gestureNow, 0, legsFacing);
                // A mirrored actor swaps the feet's offsets. (The original also checks, dead, for a half turn.)
                const int mirrored = drawn.has_value() ? drawn->Mirrored : actor.Reverse[0] != 0;
                const size_t firstOffset = mirrored ? 1 : 5;
                const size_t secondOffset = mirrored ? 5 : 1;
                const int32_t firstFrame = HotSpotValue<int32_t>(packet, 0);
                const int32_t secondFrame = HotSpotValue<int32_t>(packet, 4);
                const float angle = drawn.has_value() ? drawn->Degrees : static_cast<float>(snapped) * 40.0f;
                const int32_t earliest = drawn.has_value() ? 0 : FootFrameSlack;
                const auto atStep = [&](int32_t footFrame)
                { return footFrame - earliest <= frameNow && frameNow <= footFrame + FootFrameSlack; };

                if (!atStep(secondFrame))
                {
                    if (walking)
                    {
                        SecondStepPrinted = false;
                    }
                }
                else if (SecondStepPrinted == 0)
                {
                    SecondStepPrinted = true;
                    MakeFootprint(*this, HotSpotValue<float>(packet, firstOffset),
                                  HotSpotValue<float>(packet, firstOffset + 1), angle, FootprintDirection(angle));
                }

                if (!atStep(firstFrame))
                {
                    if (walking)
                    {
                        FirstStepPrinted = false;
                    }
                }
                else if (FirstStepPrinted == 0)
                {
                    FirstStepPrinted = true;
                    // The original turned this print's shape by the exact facing.
                    const int32_t direction = FootprintDirection(drawn.has_value() ? drawn->Degrees : stepFacing);
                    MakeFootprint(*this, HotSpotValue<float>(packet, secondOffset),
                                  HotSpotValue<float>(packet, secondOffset + 1), angle, direction);
                }
            }
        }

        for (const std::shared_ptr<MCGameObject>& jet : JumpFX)
        {
            if (jet != nullptr)
            {
                jet->Update();
            }
        }
    }

    for (int32_t i = 0; i < MaxSmokes; i++)
    {
        if (Smoke[i] == nullptr)
        {
            continue;
        }

        SmokeTime[i] -= FrameLength;

        if (0.0 <= SmokeTime[i])
        {
            Smoke[i]->SetOwner(SharedAs<MCBaseObject>());
            Smoke[i]->SetOwnerPosition(GetPositionFromHS(static_cast<uint32_t>(SmokeHotSpot[i])));
            Smoke[i]->OwnerHotSpot = static_cast<uint32_t>(SmokeHotSpot[i]);
            Smoke[i]->SetOwnerVelocity(Velocity);
            Smoke[i]->DepthBias = -50;
            Smoke[i]->Update();
        }
        else
        {
            Smoke[i].reset();
        }
    }

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

auto MCBattleMech::Render() -> void
{
    if (GamePaused != 0)
    {
        OnScreen();
    }

    if (Withdrawing != 0 && Pilot.lock()->Status == 2)
    {
        return;
    }

    auto& actor = static_cast<MCMechActor&>(*Appearance);
    int tagged = 0;
    int drawMech = 0;

    if (Alignment == HomeTeam()->Alignment)
    {
        if (WindowsVisible == Turn)
        {
            if (GetAwake() == 0)
            {
                if (IsRevealed() != 0)
                {
                    actor.Render(0);
                    drawMech = 1;
                }
            }
            else
            {
                actor.Render(InJump != 0 ? -150 : 0);
                drawMech = 1;
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
                actor.Render(InJump != 0 ? -150 : 0);
                drawMech = 1;
            }
        }
        else if (contactType == 2)
        {
            // A sensor contact: a blip sized by tonnage.
            RenderSensorBlip(SensorBlipShape(GetTonnage()));
        }
    }

    if (drawMech != 0)
    {
        for (const std::shared_ptr<MCSmoke>& smoke : Smoke)
        {
            if (smoke != nullptr)
            {
                smoke->Render();
            }
        }

        for (const std::shared_ptr<MCGameObject>& jet : JumpFX)
        {
            if (jet != nullptr)
            {
                jet->Render();
            }
        }
    }

    if (DrawTerrainGrid != 0)
    {
        // Debug: the move path's steps as lines.
        MCMovePath& path = Pilot.lock()->GetMovePath();
        Assert(true, 0, " NULL move path--bad thing ");
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
            ElementList().Add(ElementList().Make<MCLineElement>(fromScreen, toScreen, 0xfc, std::nullopt, -100000, -1));
        }
    }

    // The selected mech's queued orders: waypoint markers, joined by lines when the queue is drawn as a path.
    if (GetCommanderId() == HomeCommander()->GetId() && !WaypointMarkerShapes().empty() && Selected != 0 &&
        Pilot.lock() != nullptr && Pilot.lock()->GetTacOrderQueueSize() > 0)
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

auto MCBattleMech::RelFacingTo(MCVector3D goal, int32_t bodyPart) -> float
{
    double facing = MCMover::RelFacingTo(goal, -1);

    switch (bodyPart)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 8:
        case 9:
        case 10:
            facing += TorsoRotation;
            break;
        case 4:
            facing += static_cast<double>(LeftArmRotation) + TorsoRotation;
            break;
        case 5:
            facing += static_cast<double>(RightArmRotation) + TorsoRotation;
            break;
        default:
            break;
    }

    if (facing < -180.0)
    {
        return static_cast<float>(facing + 360.0);
    }

    if (facing > 180.0f)
    {
        facing -= 360.0;
    }

    return static_cast<float>(facing);
}

auto MCBattleMech::GetBodyState() -> int32_t
{
    return MechStateByGesture[static_cast<MCMechActor*>(Appearance.get())->CurrentGesture];
}

auto MCBattleMech::HandleEjection() -> int
{
    if (Pilot.lock() == nullptr || (Pilot.lock()->Status != 0 && Pilot.lock()->Status != 1))
    {
        return 1;
    }

    GetPilot()->Eject();
    EjectOrderGiven = 1;
    DestroyBodyLocation(MechHead);
    // The ejection seat's beam, from the cockpit hot spot up and away.
    std::shared_ptr<MCGameObject> beam = CreateObject(0x1e4);

    if (beam != nullptr)
    {
        auto& mechType = TypeAs<MCBattleMechType>();
        MCVector3D cockpit = GetPositionFromHS(mechType.NumWeapons + 1);
        beam->SetPosition(cockpit);
        cockpit.X = static_cast<float>(cockpit.X - 1000.0);
        cockpit.Y = static_cast<float>(cockpit.Y + 1000.0);
        cockpit.Z = static_cast<float>(cockpit.Z + 300.0);
        static_cast<MCProjectileLaser*>(beam.get())
            ->Connect(SharedAs<MCGameObject>(), cockpit, std::nullopt, static_cast<int32_t>(mechType.NumWeapons + 1));

        AddToDefaultList(std::move(beam));
    }

    Disable(3);
    TacticalInterface()->RemoveMech(PartId);

    if (Alignment == HomeTeam()->Alignment)
    {
        FriendlyDestroyed = 1;
        return 1;
    }

    EnemyDestroyed = 1;
    return 1;
}

auto MCBattleMech::GetVitalInfo(std::span<uint8_t> vitalInfo) -> int32_t
{
    const int32_t size = MCMover::GetVitalInfo({});

    if (!vitalInfo.empty())
    {
        MCMover::GetVitalInfo(vitalInfo);
    }

    return size + 6;
}

auto MCBattleMech::IsCaptureable() -> int
{
    if (Captureable != 0 && Alignment == HomeTeam()->Alignment && IsDestroyed() == 0)
    {
        return 1;
    }

    return 0;
}

auto MCBattleMech::GetTotalEffectiveness() -> float
{
    const float weaponRatio = WeaponEffectiveness / MaxWeaponEffectiveness;
    float armorFactor = 0.0f;

    if (IsDestroyed() == 0 && IsDisabled() == 0)
    {
        // Head, arms, centre torso (the worse of front and back) and side torsos (front and back), each as a share
        // of its full armor.
        const std::span<const MCArmorLocation> locations = Armor;
        const float head =
            locations[MechHead].CurArmor / static_cast<float>(locations[MechHead].MaxArmor) * 0.6f + 0.4f;
        float centre = locations[MechCenterTorso].CurArmor;
        uint8_t centreMax = locations[MechCenterTorso].MaxArmor;

        if (locations[8].CurArmor < centre)
        {
            centreMax = locations[8].MaxArmor;
            centre = locations[8].CurArmor;
        }

        const float arms = (locations[MechRightArm].CurArmor + locations[MechLeftArm].CurArmor) /
                           static_cast<float>(locations[MechRightArm].MaxArmor + locations[MechLeftArm].MaxArmor);
        const float armFactor = arms * 0.25f + 0.75f;
        const float sides = (locations[10].CurArmor + locations[9].CurArmor + locations[MechRightTorso].CurArmor +
                             locations[MechLeftTorso].CurArmor) /
                            static_cast<float>(locations[10].MaxArmor + locations[9].MaxArmor +
                                               locations[MechRightTorso].MaxArmor + locations[MechLeftTorso].MaxArmor);
        armorFactor = armFactor * (arms * 0.4f + 0.6f) * (centre / static_cast<float>(centreMax) + 1.0f) * 0.5f *
                      (sides * 0.25f + 0.75f) * head;
    }

    // Wounds wear the pilot down.
    const float woundFactor[7] = {1.0f, 0.95f, 0.85f, 0.75f, 0.5f, 0.3f, 0.0f};
    auto wounds = static_cast<uint32_t>(static_cast<int32_t>(std::floor(GetPilot()->Wounds)));

    if (6 < wounds)
    {
        wounds = 6;
    }

    return woundFactor[wounds] * armorFactor * weaponRatio;
}
