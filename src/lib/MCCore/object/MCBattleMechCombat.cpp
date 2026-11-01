#include "stdafx.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "abl/MCScrollingTextWindow.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "main/MCGameSession.h"
#include "main/MCMissionGlobals.h"
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
#include "object/MCDebris.h"
#include "object/MCDebrisType.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMoverGroup.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCNetControl.h"
#include "object/MCObjectSystem.h"
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
#include "object/MCObjectTypeManager.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponFireChunk.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCSensorSystem.h"
#include "object/MCTeam.h"

auto MCBattleMech::GetWeaponHeat(int32_t weaponIndex) -> float
{
    return MasterComponentList[Inventory[weaponIndex].MasterID].RangeOrHeat;
}

auto MCBattleMech::IsWeaponReady(int32_t weaponIndex) -> int
{
    if (Inventory[weaponIndex].Disabled != 0)
    {
        return 0;
    }

    if (ScenarioTime < Inventory[weaponIndex].ReadyTime)
    {
        return 0;
    }

    return 1;
}

auto MCBattleMech::CalcAttackChance(std::shared_ptr<MCGameObject> target, int32_t aimLocation, float targetTime,
                                    int32_t weaponIndex, float modifiers, int32_t& range,
                                    std::optional<MCVector3D> targetPoint) -> float
{
    if (weaponIndex < NumOther || NumOther + NumWeapons <= weaponIndex)
    {
        return -1000.0f;
    }

    if (Pilot.lock() != nullptr)
    {
        modifiers +=
            RankVersusChassisCombatModifier[static_cast<int8_t>(Pilot.lock()->Rank)][static_cast<int8_t>(MechClass)];
    }

    return MCMover::CalcAttackChance(target, aimLocation, targetTime, weaponIndex, modifiers, range, targetPoint);
}

auto MCBattleMech::CalcHitLocation(std::optional<MCVector3D> attackerPosition, int32_t weaponIndex,
                                   int32_t attackSource, int32_t attackType) -> int32_t
{
    int32_t row = MechHitSectionTable[attackSource];
    double angle;

    if (!attackerPosition)
    {
        angle = static_cast<double>(RandomNumber(360) - 180);
    }
    else
    {
        angle = RelFacingTo(*attackerPosition, -1);
    }

    // The side the shot comes from: front, rear, left, right.
    int32_t side;

    if (!(angle < -45.0 || 45.0 < angle))
    {
        side = 0;
    }
    else if (-135.0 < angle && angle < -45.0)
    {
        side = 2;
    }
    else if (45.0 < angle && angle < 135.0f)
    {
        side = 3;
    }
    else
    {
        side = 1;
    }

    if (attackSource == 3)
    {
        // A mech lying down is hit from above or below.
        if (std::static_pointer_cast<MCMechActor>(GetAppearance())->CurrentStateGesture == 7)
        {
            side = 0;
            row = 1;
        }

        if (std::static_pointer_cast<MCMechActor>(GetAppearance())->CurrentStateGesture == 8)
        {
            side = 1;
            row = 1;
        }
    }

    int32_t roll = RandomNumber(100);
    int32_t location = 0;

    do
    {
        const int32_t chance = MechHitLocationTable[(side + row * 4) * NumMechArmorLocations + location];

        if (roll < chance)
        {
            return location;
        }

        roll -= chance;
        location++;
    } while (location < NumMechArmorLocations);

    return location;
}

auto MCBattleMech::TransferHitLocation(int32_t hitLocation) -> int32_t
{
    if (hitLocation < 0 || hitLocation >= NumMechBodyLocations)
    {
        Assert(false, 0, "(hitLocation >= 0) && (hitLocation < NumMechBodyLocations)", R"(L:\mcx\Object\Mech.cpp)");
    }

    return MechTransferHitTable[hitLocation];
}

auto MCBattleMech::HitInventoryItem(int32_t itemIndex, int setupOnly) -> int
{
    static constexpr std::array<std::string_view, NumMechBodyLocations> LocationNames = {
        "HEAD", "CTORSO", "LTORSO", "RTORSO", "LARM", "RARM", "LLEG", "RLEG"};
    MCInventoryItem& item = Inventory[itemIndex];

    // Port fix: Health is unsigned here (the original's was a signed byte). A component already at 0 that was hit
    // again (a second critical on the same space, then its limb being blown off) wrapped to 255 and no longer read
    // as damaged or destroyed, so the payload list showed it in its normal colour. It stays at 0.
    if (item.Health > 0)
    {
        item.Health--;
    }

    const uint32_t masterId = item.MasterID;
    const uint32_t location = item.BodyLocation;

    if (HasGameSystemWindow() && setupOnly == 0 && location <= 7)
    {
        // Reported only while the location's armor (front and rear, for the torso) still stands.
        int report = 0;

        switch (location)
        {
            case 1:
                report = Armor[1].CurArmor > 0.0f && Armor[8].CurArmor > 0.0f;
                break;
            case 2:
                report = Armor[2].CurArmor > 0.0f && Armor[9].CurArmor > 0.0f;
                break;
            case 3:
                report = Armor[3].CurArmor > 0.0f && Armor[10].CurArmor > 0.0f;
                break;
            default:
                report = Armor[location].CurArmor > 0.0f;
                break;
        }

        if (report != 0)
        {
            PrintGameSystem("");
            PrintGameSystem("***********************************");
            PrintGameSystem(std::format("INTERNAL COMPONENT HIT: {} ({})", DebugStatus, Pilot.lock()->Name));
            const std::shared_ptr<MCGameObject> badGuy = LockObject(BadGuy);
            const std::string_view attackerName =
                badGuy != nullptr ? std::string_view(static_cast<MCMover&>(*badGuy).DebugStatus) : "???";
            PrintGameSystem(std::format("{} in {} by {}", MasterComponentList[masterId].Name, LocationNames[location],
                                        attackerName));
        }
    }

    const MCMasterComponent& component = MasterComponentList[masterId];

    if (component.Form == MCComponentForm::Actuator || component.Form == MCComponentForm::Gyroscope)
    {
        PilotingCheck(0, 0.0f);
    }

    const auto disableLevel = static_cast<int8_t>(component.DisableLevel);

    if (GetInventoryDamage(itemIndex) == disableLevel)
    {
        // Disabled: the component stops working; smoke from the hot spot above the weapons it sits nearest.
        int32_t smokeSpot = 1;
        item.Disabled = 1;

        switch (component.Form)
        {
            case MCComponentForm::Simple:
            case MCComponentForm::Cockpit:
                smokeSpot = 2;
                break;
            case MCComponentForm::Sensor:
            {
                if (SensorSystem.lock() != nullptr)
                {
                    SensorSystem.lock()->Disable();
                }
                break;
            }
            case MCComponentForm::Engine:
            {
                smokeSpot = 1;
                EngineBlowTime = static_cast<float>(ScenarioTime + 5.0);
                Disable(1);
                break;
            }
            case MCComponentForm::Weapon:
            case MCComponentForm::WeaponEnergy:
            case MCComponentForm::WeaponBallistic:
            case MCComponentForm::WeaponMissile:
            {
                CalcWeaponEffectiveness(0);

                if (LongestRangeWeapon == static_cast<uint32_t>(itemIndex) ||
                    ShortestRangeWeapon == static_cast<uint32_t>(itemIndex))
                {
                    CalcLongestRangeWeapon();
                }

                CalcOptimalRange(nullptr);
                [[fallthrough]];
            }
            case MCComponentForm::Actuator:
                smokeSpot = 0;
                break;
            case MCComponentForm::Case:
                BodyAt(item.BodyLocation).HasCase = 0;
                break;
            case MCComponentForm::PowerAmplifier:
            case MCComponentForm::Bulk:
                smokeSpot = 1;
                break;
            case MCComponentForm::Ecm:
            {
                Team.lock()->RemoveEcm(*EcmTracker.lock());
                EcmTracker.reset();
                break;
            }
            case MCComponentForm::Jammer:
            {
                Team.lock()->RemoveJammer(*JammerTracker.lock());
                JammerTracker.reset();
                break;
            }
            default:
                break;
        }

        if (setupOnly == 0)
        {
            if (UseSound != 0)
            {
                SoundSystem()->PlayDigitalSample(0x13, 1, *this, false, false);
            }

            std::shared_ptr<MCGameObject> sparks = CreateObject(0x3f);

            if (sparks != nullptr)
            {
                MCVector3D sparkPos = GetPositionFromHS(TypeAs<MCBattleMechType>().NumWeapons + smokeSpot);
                sparks->SetPosition(sparkPos);

                AddToDefaultList(std::move(sparks));
            }

            for (int32_t i = 0; i < MaxSmokes; i++)
            {
                if (Smoke[i] == nullptr)
                {
                    Smoke[i] = CreateObjectAs<MCSmoke>(0x1c2);
                    SmokeHotSpot[i] = RandomNumber(static_cast<int32_t>(TypeAs<MCBattleMechType>().NumWeapons));
                    SmokeTime[i] = 15.0f;
                    break;
                }
            }
        }
    }

    if (Inventory[itemIndex].Health == 0)
    {
        // Destroyed: the cockpit hurts the pilot, a leg actuator trips the mech, ammunition explodes.
        switch (component.Form)
        {
            case MCComponentForm::Cockpit:
                Pilot.lock()->Injure(6.0f, 0);
                break;
            case MCComponentForm::Actuator:
            {
                if (location == MechLeftLeg || location == MechRightLeg)
                {
                    PilotingCheck(0, 100.0f);
                    return 0;
                }
                break;
            }
            case MCComponentForm::Ammo:
            {
                AmmoExplosion(itemIndex);
                return 0;
            }
            default:
                break;
        }
    }

    return 0;
}

auto MCBattleMech::DestroyBodyLocation(int32_t location) -> void
{
    MCBodyLocation& bodyLocation = BodyAt(location);

    if (bodyLocation.DamageState == 2)
    {
        return;
    }

    bodyLocation.CurInternalStructure = 0.0f;
    bodyLocation.DamageState = 2;

    if (location == MechLeftLeg || location == MechRightLeg)
    {
        CalcLegStatus();
        PilotingCheck(0, 100.0f);
    }
    else if (location == MechCenterTorso)
    {
        CalcTorsoStatus();
    }

    // Everything in it is lost.
    for (int32_t i = 0; i < NumLocationCriticalSpaces[location]; i++)
    {
        MCCriticalSpace& space = BodyAt(location).CriticalSpaces[i];

        if (space.Hit == 0 && static_cast<int8_t>(space.InventoryID) != -1)
        {
            space.Hit = 1;
            HitInventoryItem(static_cast<int8_t>(space.InventoryID), 0);
        }
    }

    switch (location)
    {
        case MechCenterTorso:
        {
            // The center torso gone destroys the mech, unless it was already ruled dead.
            if (CenterTorsoInjuredTime < ScenarioTime && (EngineBlowTime <= -1.0f || EngineBlowTime < ScenarioTime))
            {
                Disable(0);
                return;
            }

            Pilot.lock()->HandleAlarm(MCPilotAlarmType::VehicleIncapacitated, 0);
            ObjType->HandleDestruction(*this, 0);
            return;
        }
        case MechRightTorso:
        {
            DestroyBodyLocation(MechRightArm);
            return;
        }
        case MechLeftTorso:
        {
            DestroyBodyLocation(MechLeftArm);
            return;
        }
        case MechLeftArm:
        {
            LeftArmBlownThisFrame = true;
            return;
        }
        case MechRightArm:
        {
            RightArmBlownThisFrame = true;
            return;
        }
        default:
            return;
    }
}

auto MCBattleMech::CalcCriticalHit(int32_t hitLocation) -> void
{
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return;
    }

    const int32_t location = MechArmorToBodyLocation[hitLocation];

    // Port fix: the original tests body[hitLocation].totalSpaces, reading past the eight body locations for a rear
    // torso hit (8..10); the body location the hit maps to is tested instead.
    if (BodyAt(location).TotalSpaces == 0)
    {
        return;
    }

    const int32_t numSpaces = NumLocationCriticalSpaces[location];
    const int32_t roll = RandomNumber(100);

    if (roll < CriticalHitTable[0])
    {
        return;
    }

    int32_t numCriticalHits;

    if (roll < CriticalHitTable[1])
    {
        numCriticalHits = 1;
    }
    else if (roll < CriticalHitTable[2])
    {
        numCriticalHits = 2;
    }
    else
    {
        // The worst roll blows a head, arm or leg clean off; a torso takes three hits.
        if (location < MechCenterTorso || MechRightTorso < location)
        {
            DestroyBodyLocation(location);

            if (MultiPlayer() != nullptr)
            {
                AddCriticalHitChunk(0, location, 15);
            }

            return;
        }

        numCriticalHits = 3;
    }

    do
    {
        MCBodyLocation& bodyLocation = BodyAt(location);
        int32_t spaceRoll = RandomNumber(bodyLocation.TotalSpaces);
        int32_t space = 0;

        for (; space < numSpaces; space++)
        {
            const uint8_t item = bodyLocation.CriticalSpaces[space].InventoryID;
            int32_t size = 0;

            if (item != 0xff)
            {
                size = static_cast<int8_t>(MasterComponentList[Inventory[item].MasterID].CriticalSpacesReq);
            }

            if (spaceRoll < size)
            {
                break;
            }

            spaceRoll -= size;
        }

        Assert(location >= 0 && location <= 7, static_cast<uint32_t>(location), " Bad bodyLocation in CriticalHit ");
        Assert(space >= 0 && space < NumLocationCriticalSpaces[location], static_cast<uint32_t>(space),
               " Bad Critical Hit Space ");
        MCCriticalSpace& criticalSpace = bodyLocation.CriticalSpaces[space];
        criticalSpace.Hit = 1;
        HitInventoryItem(static_cast<int8_t>(criticalSpace.InventoryID), 0);

        if (MultiPlayer() != nullptr)
        {
            AddCriticalHitChunk(0, location, space);
        }
    } while (--numCriticalHits != 0);
}

auto MCBattleMech::HandleCriticalHit(int32_t bodyLocation, int32_t criticalSpace) -> void
{
    if (criticalSpace == 15)
    {
        DestroyBodyLocation(bodyLocation);
        return;
    }

    HitInventoryItem(static_cast<int8_t>(BodyAt(bodyLocation).CriticalSpaces[criticalSpace].InventoryID), 0);
}

auto MCBattleMech::InjureBodyLocation(int32_t bodyLocation, float damage) -> int
{
    MCBodyLocation& location = BodyAt(bodyLocation);

    if (bodyLocation == MechCenterTorso && CenterTorsoInjuredTime < 0.0)
    {
        CenterTorsoInjuredTime = ScenarioTime;
    }

    if (damage <= location.CurInternalStructure)
    {
        location.CurInternalStructure -= damage;
    }
    else
    {
        location.CurInternalStructure = 0.0f;
    }

    if (0.0f < location.CurInternalStructure)
    {
        location.DamageState =
            0.5 < location.CurInternalStructure / static_cast<float>(location.MaxInternalStructure) ? 0 : 1;

        if (bodyLocation == MechLeftLeg || bodyLocation == MechRightLeg)
        {
            CalcLegStatus();
        }
        else if (bodyLocation == MechCenterTorso)
        {
            CalcTorsoStatus();
            CalcCriticalHit(MechCenterTorso);
            return 0;
        }

        CalcCriticalHit(bodyLocation);
        return 0;
    }

    DestroyBodyLocation(bodyLocation);
    return 1;
}

auto MCBattleMech::WeaponLocked(int32_t weaponIndex, MCVector3D targetPosition) -> float
{
    return RelFacingTo(targetPosition, Inventory[weaponIndex].BodyLocation);
}

namespace
{
    /// <summary>The rear armor location behind a body location (the centre's, the left side's or the right's).</summary>
    int32_t RearArmorLocation(int32_t bodyLocation)
    {
        switch (bodyLocation)
        {
            case MechLeftTorso:
            case MechLeftArm:
            case MechLeftLeg:
                return 9;
            case MechRightTorso:
            case MechRightArm:
            case MechRightLeg:
                return 10;
            default:
                return 8;
        }
    }

    /// <summary>Passes what is left of a shot on to the location a destroyed one transfers to.</summary>
    void TransferHit(MCBattleMech& mech, MCWeaponShotInfo& shotInfo, int32_t bodyLocation)
    {
        MCWeaponShotInfo transferInfo = shotInfo;
        transferInfo.HitLocation = MCBattleMech::TransferHitLocation(bodyLocation);

        if (MultiPlayer() == nullptr)
        {
            mech.HandleWeaponHit(transferInfo, 0);
        }
        else if (MultiPlayer()->IsServer != 0)
        {
            mech.HandleWeaponHit(transferInfo, 1);
        }
    }
}

auto MCBattleMech::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if ((MultiPlayer() == nullptr && CantHitMe != 0 && Pilot.lock()->OnHomeTeam() != 0))
    {
        return 0;
    }

    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    BadGuy = LockObject(shotInfo.Attacker);

    if (shotInfo.Damage <= 0.0f)
    {
        return 0;
    }

    const int32_t hitLocation = shotInfo.HitLocation;

    if (hitLocation == -1)
    {
        return 0;
    }

    if (IsDestroyed() != 0)
    {
        return 0;
    }

    const MCWeaponShotInfo originalShot = shotInfo;
    const int wasDisabled = IsDisabled();
    // Ammunition hits (and cause -4) go straight for the internal structure.
    int internalHit = shotInfo.MasterId == -4;

    if (shotInfo.MasterId > 0)
    {
        internalHit = MasterComponentList[shotInfo.MasterId].Form == MCComponentForm::Ammo;
    }

    DamageRateTally = shotInfo.Damage + DamageRateTally;
    TotalDamageTaken = shotInfo.Damage + TotalDamageTaken;
    // Which way the mech would fall.
    const float angle = TorsoRotation + shotInfo.EntryAngle;

    if (!(angle < -90.0 || 90.0 < angle))
    {
        HitFromFrontThisFrame = true;
    }
    else if (angle <= -91.0 || 91.0 <= angle)
    {
        HitFromBehindThisFrame = true;
    }

    const int32_t bodyLocation = MechArmorToBodyLocation[hitLocation];

    if (bodyLocation == MechHead && 2.0f <= shotInfo.Damage)
    {
        Pilot.lock()->Injure(1.0f, 1);
    }

    if (Armor[hitLocation].CurArmor <= 0.0f || internalHit != 0)
    {
        MCBodyLocation& location = BodyAt(bodyLocation);
        int caseHit = 0;

        if (location.CurInternalStructure <= 0.0f)
        {
            if (internalHit == 0 || location.HasCase == 0)
            {
                if (bodyLocation != MechCenterTorso && bodyLocation != MechHead)
                {
                    TransferHit(*this, shotInfo, bodyLocation);
                }
            }
            else
            {
                caseHit = 1;
            }
        }
        else if (shotInfo.Damage < location.CurInternalStructure)
        {
            InjureBodyLocation(bodyLocation, shotInfo.Damage);
        }
        else
        {
            const float internalStructure = location.CurInternalStructure;
            shotInfo.SetDamage(shotInfo.Damage - internalStructure);
            InjureBodyLocation(bodyLocation, internalStructure);

            if (0.0f < shotInfo.Damage)
            {
                if (internalHit != 0 && BodyAt(bodyLocation).HasCase != 0)
                {
                    caseHit = 1;
                }
                else if (bodyLocation != MechCenterTorso && bodyLocation != MechHead)
                {
                    TransferHit(*this, shotInfo, bodyLocation);
                }
            }
        }

        if (caseHit != 0)
        {
            // CASE vents the rest out the back.
            const int32_t rear = RearArmorLocation(bodyLocation);
            int holed = 0;

            if (shotInfo.Damage <= Armor[rear].CurArmor)
            {
                Armor[rear].CurArmor -= shotInfo.Damage;
            }
            else
            {
                Armor[rear].CurArmor = 0.0f;
                holed = 1;
            }

            shotInfo.SetDamage(0.0f);

            if (holed != 0)
            {
                PlayMessage(MCRadioMessageType::ArmorHoled, 0);
            }
        }
    }
    else if (shotInfo.Damage <= Armor[hitLocation].CurArmor)
    {
        Armor[hitLocation].CurArmor -= shotInfo.Damage;
    }
    else
    {
        // Through the armor.
        shotInfo.SetDamage(shotInfo.Damage - Armor[hitLocation].CurArmor);
        Armor[shotInfo.HitLocation].CurArmor = 0.0f;
        const float internalStructure = BodyAt(bodyLocation).CurInternalStructure;

        if (shotInfo.Damage < internalStructure)
        {
            InjureBodyLocation(bodyLocation, shotInfo.Damage);
        }
        else
        {
            shotInfo.SetDamage(shotInfo.Damage - internalStructure);
            InjureBodyLocation(bodyLocation, internalStructure);

            if (0.0f < shotInfo.Damage && bodyLocation != MechCenterTorso && bodyLocation != MechHead)
            {
                TransferHit(*this, shotInfo, bodyLocation);
            }
        }

        PlayMessage(MCRadioMessageType::ArmorHoled, 0);
    }

    std::shared_ptr<MCGameObject> attacker = LockObject(shotInfo.Attacker);
    auto triggerId = static_cast<uint32_t>(shotInfo.MasterId);
    MCPilotAlarmType alarm = MCPilotAlarmType::HitByWeaponFire;

    if (attacker == nullptr)
    {
        if (shotInfo.MasterId == -4 || shotInfo.MasterId >= 0)
        {
            triggerId = 0;
        }
    }
    else
    {
        triggerId = static_cast<uint32_t>(attacker->PartId);

        if (shotInfo.MasterId < 0 && shotInfo.MasterId != -4)
        {
            alarm = MCPilotAlarmType::Collision;
        }
    }

    Pilot.lock()->TriggerAlarm(alarm, triggerId);
    CurCV = CalcCV(0);

    if (wasDisabled == 0 && IsDisabled() != 0 && attacker != nullptr &&
        (attacker->ObjectClass == MCObjectClass::BattleMech || attacker->ObjectClass == MCObjectClass::GroundVehicle ||
         attacker->ObjectClass == MCObjectClass::Elemental || attacker->ObjectClass == MCObjectClass::Mover))
    {
        attacker->GetPilot()->TriggerAlarm(MCPilotAlarmType::KilledTarget, static_cast<uint32_t>(PartId));
    }

    shotInfo.Init(LockObject(originalShot.Attacker), originalShot.MasterId, originalShot.Damage,
                  originalShot.HitLocation, originalShot.EntryAngle);
    return 0;
}

namespace
{
    /// <summary>Ammo count that marks a weapon as never running out.</summary>
    constexpr int32_t UNLIMITED_SHOTS = 9999;

    /// <summary>Packs a weapon fire chunk, checks that it unpacks the same, queues it and logs it.</summary>
    void SendWeaponFireChunk(MCBattleMech& mech, MCWeaponFireChunk& chunk)
    {
        chunk.Pack();
        MCWeaponFireChunk check;
        check.Init();
        check.Data = chunk.Data;
        check.Unpack(mech);

        if (chunk.EqualTo(check) == 0)
        {
            Fatal(0, " Mech.fireWeapon: Bad WeaponFireChunk (save wfchunk.dbg file now) ");
        }

        mech.AddWeaponFireChunk(0, chunk);
    }

    /// <summary>
    /// Builds and sends the chunk for a shot at <paramref name="target"/> (a mover, train car, camera drone or
    /// terrain object) or, when it is null, at <paramref name="point"/>.
    /// </summary>
    void SendTargetFireChunk(MCBattleMech& mech, std::shared_ptr<MCGameObject> target, std::optional<MCVector3D> point,
                             int32_t weapon, int hit, float entryAngle, int32_t missiles, int32_t missilesPastAMS,
                             int32_t antiMissileShots, int32_t hitLocation)
    {
        MCWeaponFireChunk chunk;
        chunk.Init();

        if (target == nullptr)
        {
            chunk.BuildLocationTarget(*point, weapon, hit, missiles);
        }
        else if (target->ObjectClass == MCObjectClass::BattleMech ||
                 target->ObjectClass == MCObjectClass::GroundVehicle ||
                 target->ObjectClass == MCObjectClass::Elemental || target->ObjectClass == MCObjectClass::Mover)
        {
            chunk.BuildMoverTarget(static_cast<MCBigGameObject&>(*target), weapon, hit, entryAngle, missiles,
                                   missilesPastAMS, antiMissileShots, hitLocation);
        }
        else if (target->ObjectClass == MCObjectClass::TrainCar)
        {
            chunk.BuildTrainTarget(static_cast<MCBigGameObject&>(*target), weapon, hit, entryAngle, missiles);
        }
        else if (target->ObjectClass == MCObjectClass::CameraDrone)
        {
            chunk.BuildCameraDroneTarget(static_cast<MCBigGameObject&>(*target), weapon, hit, entryAngle, missiles);
        }
        else
        {
            chunk.BuildTerrainTarget(static_cast<MCBigGameObject&>(*target), weapon, hit, missiles);
        }

        SendWeaponFireChunk(mech, chunk);
    }

    /// <summary>A shot's damage must survive the chunk's quarter-point rounding.</summary>
    void CheckDamageRound(const MCWeaponShotInfo& shot)
    {
        const auto quarters = static_cast<int32_t>(shot.Damage * 4.0);
        Assert(shot.Damage == quarters * 0.25, 0, " WeaponHitChunk.build: damage round error ");
    }

    /// <summary>A shot with no effect object sets off a live mine where it lands.</summary>
    void CheckMineAt(MCVector3D& point)
    {
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        MCScenarioMap::WorldToMapPos(point, tileR, tileC, cellR, cellC);

        // Port fix: a miss can land off the map, where the original reads (and writes) outside it.
        if (!GameMap().OnMap(tileR, tileC))
        {
            return;
        }

        MCMapTile& tile = GameMap().Map[GameMap().Width * tileR + tileC];

        if ((tile.Overlay & 0x1800) == 0x1000 || (tile.Overlay & 0x6000) == 0x4000)
        {
            CreateExplosion(MineExplosion, point, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
            tile.Overlay |= 0x1800;
            tile.Overlay |= 0x6000;
        }
    }

    /// <summary>
    /// Sends a weapon effect on its way, at <paramref name="target"/> (from hot spot to hot spot) or, when it is
    /// null, at <paramref name="point"/>, carrying <paramref name="shot"/>; then adds it to the weapon list.
    /// </summary>
    void LaunchWeaponFX(MCBattleMech& mech, std::shared_ptr<MCGameObject> fx, std::shared_ptr<MCGameObject> target,
                        std::optional<MCVector3D> point, MCWeaponShotInfo& shot, int32_t sourceHotSpot,
                        int32_t targetHotSpot)
    {
        if (fx->ObjectClass == MCObjectClass::Bullet)
        {
            auto& bullet = static_cast<MCBullet&>(*fx);

            bullet.NewShot().Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation,
                                  shot.EntryAngle);

            if (target == nullptr)
            {
                bullet.Connect(mech, *point, sourceHotSpot);
            }
            else
            {
                bullet.Owner = mech.SharedAs<MCGameObject>();
                bullet.Target = target;
                bullet.OwnerHotSpot = sourceHotSpot;
                bullet.TargetHotSpot = targetHotSpot;
            }
        }
        else if (fx->ObjectClass == MCObjectClass::Laser)
        {
            auto& laser = static_cast<MCLaser&>(*fx);

            if (target == nullptr)
            {
                laser.Connect(mech.SharedAs<MCGameObject>(), *point, shot, sourceHotSpot);
            }
            else
            {
                laser.Source = mech.SharedAs<MCGameObject>();
                laser.Target = target;
                laser.SourceHotSpot = sourceHotSpot;
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
                projectile.Connect(mech.SharedAs<MCGameObject>(), *point, shot, sourceHotSpot);
            }
            else
            {
                projectile.Owner = mech.SharedAs<MCGameObject>();
                projectile.Target = target;
                projectile.OwnerHotSpot = sourceHotSpot;
                projectile.TargetHotSpot = targetHotSpot;
                projectile.ShotInfo.Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation,
                                         shot.EntryAngle);
            }
        }

        WeaponList()->Add(std::move(fx));
    }

    /// <summary>Firing gives a mech away to the other side's mechs within visual range.</summary>
    void RevealFiring(MCBattleMech& mech)
    {
        std::shared_ptr<MCObjectList> enemies = nullptr;
        uint8_t seenBy = 0;

        if (mech.Alignment == 1)
        {
            enemies = ClanMechList();
            seenBy = 2;
        }
        else if (mech.Alignment == -1)
        {
            enemies = InnerSphereMechList();
            seenBy = 1;
        }

        if (enemies == nullptr)
        {
            return;
        }

        for (std::shared_ptr<MCBaseObject> enemy : *enemies)
        {
            MCVector3D enemyPosition = static_cast<MCGameObject&>(*enemy).GetPosition();

            if (mech.DistanceFrom(enemyPosition) < Scenario()->MaxVisualRange)
            {
                Terrain()->MarkRadiusSeen(mech.Position, mech.Frame.J, 360.0f, Scenario()->FireVisualRange, seenBy);
                return;
            }
        }
    }

    /// <summary>Where a missed shot lands: scattered up to <paramref name="scatter"/> about the aim point.</summary>
    /// <param name="centred">Missiles scatter both ways; other shots (as the original computes them) only one.</param>
    MCVector3D MissPoint(std::shared_ptr<MCGameObject> target, std::optional<MCVector3D> targetPoint, float scatter,
                         int centred)
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
        const MCVector3D base = target != nullptr ? target->GetPosition() : *targetPoint;
        miss.X += base.X;
        miss.Y += base.Y;
        miss.Z += base.Z;
        return miss;
    }
}

auto MCBattleMech::FireWeapon(std::shared_ptr<MCGameObject> target, float targetTime, int32_t weaponIndex,
                              int32_t attackType, int32_t aimLocation, std::optional<MCVector3D> targetPoint) -> int32_t
{
    if (Status == 5 || Status == 4 || Status == 1 || Status == 2 || GetBodyState() == 0 || GetBodyState() == 7 ||
        GetBodyState() == 8)
    {
        return 1;
    }

    if (IsWeaponReady(weaponIndex) == 0)
    {
        return 3;
    }

    float distance;

    if (target == nullptr)
    {
        if (!targetPoint || LineOfSight(*targetPoint) == 0)
        {
            return 4;
        }

        distance = static_cast<float>(DistanceFrom(*targetPoint));
    }
    else
    {
        // A camera drone can't be shot for two seconds after launch.
        if (target->ObjectClass == MCObjectClass::CameraDrone &&
            ScenarioTime < std::static_pointer_cast<MCCameraDrone>(target)->LaunchTime + 2.0)
        {
            return 4;
        }

        if (target->IsDestroyed() != 0)
        {
            return 4;
        }

        if (LineOfSight(*target) == 0)
        {
            return 4;
        }

        MCVector3D targetPosition = target->GetPosition();
        distance = static_cast<float>(DistanceFrom(targetPosition));
    }

    const int32_t inRange = WeaponInRange(weaponIndex, distance);

    if ((MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0) && inRange == 0)
    {
        return 4;
    }

    const MCMasterComponent& weapon = MasterComponentList[Inventory[weaponIndex].MasterID];

    if (weapon.MissileType != 2 && weapon.MissileType != 1 && weapon.MissileType != 3)
    {
        // Direct fire needs a clear line.
        if (target == nullptr)
        {
            if (!targetPoint || LineOfFire(*targetPoint) == 0)
            {
                return 4;
            }
        }
        else if (LineOfFire(*target) == 0)
        {
            return 4;
        }
    }

    const int32_t numShots = GetWeaponShots(weaponIndex);

    if (numShots == 0)
    {
        return 4;
    }

    float entryAngle = 0.0f;

    if (target != nullptr)
    {
        entryAngle = target->RelFacingTo(Position, -1);
    }

    const int isStreak = weapon.WeaponFlags & 1;
    int32_t range = 0;
    int32_t hitChance =
        static_cast<int32_t>(CalcAttackChance(target, aimLocation, targetTime, weaponIndex, 0.0f, range, targetPoint));
    const int32_t hitRoll = RandomNumber(100);

    if (target != nullptr)
    {
        float points = SkillTry[SkillGunnery];

        if (target->GetAlignment() == -1)
        {
            Pilot.lock()->NumSkillUses[SkillGunnery][1]++;
        }
        else
        {
            points = SkillTry[SkillGunnery] * 0.1f;
        }

        Pilot.lock()->SkillPoints[SkillGunnery] = points + Pilot.lock()->SkillPoints[SkillGunnery];
    }

    // Aimed shots only from a standing mech.
    if (aimLocation != -1 && 0.0 < GetVelocity().Magnitude())
    {
        hitChance = 0;
    }

    int32_t hitLocation = -2;

    if (target != nullptr && hitRoll < hitChance)
    {
        float points = SkillSuccess[SkillGunnery];

        if (target->GetAlignment() == -1)
        {
            Pilot.lock()->NumSkillSuccesses[SkillGunnery][1]++;
        }
        else
        {
            points = SkillSuccess[SkillGunnery] * 0.1f;
        }

        Pilot.lock()->SkillPoints[SkillGunnery] = points + Pilot.lock()->SkillPoints[SkillGunnery];

        if (aimLocation != -1)
        {
            hitLocation = aimLocation;
        }
    }

    std::shared_ptr<MCMechWarrior> targetPilot;

    if (target != nullptr &&
        (target->ObjectClass == MCObjectClass::BattleMech || target->ObjectClass == MCObjectClass::GroundVehicle ||
         target->ObjectClass == MCObjectClass::Elemental || target->ObjectClass == MCObjectClass::Mover))
    {
        targetPilot = target->GetPilot();
        targetPilot->UpdateAttackerStatus(static_cast<uint32_t>(PartId), ScenarioTime);
    }

    StartWeaponRecycle(weaponIndex);

    const int32_t chunkWeapon = weaponIndex - NumOther;

    if (hitRoll < hitChance)
    {
        if (numShots != UNLIMITED_SHOTS)
        {
            DeductWeaponShot(weaponIndex, 1);
        }

        MCInventoryItem& item = Inventory[weaponIndex];
        const MCMasterComponent& fired = MasterComponentList[item.MasterID];

        if (fired.Form == MCComponentForm::WeaponMissile)
        {
            // Missiles: a streak fires them all, anything else about half; anti-missile systems take some out.
            const int32_t rackSize = fired.NumMissiles;
            int32_t missiles = rackSize;

            if (isStreak == 0)
            {
                missiles = static_cast<int32_t>((rackSize + 1.0) * 0.5);

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
            int32_t missilesLeft = missiles;

            if (target != nullptr)
            {
                missilesLeft = target->FireAntiMissileSystem(missiles, antiMissileShots);

                if (antiMissileShots > 0)
                {
                    target->ReduceAntiMissileAmmo(antiMissileShots);
                }
            }

            int32_t targetHotSpot = 0;
            const uint8_t weaponEffect = fired.WeaponEffect;
            const int32_t sourceHotSpot = BodyAt(item.BodyLocation).HotSpotNumber;

            if (missilesLeft > 0)
            {
                if (target == nullptr)
                {
                    hitLocation = -1;
                }
                else
                {
                    if (aimLocation == -1)
                    {
                        hitLocation = target->CalcHitLocation(GetPosition(), weaponIndex, 0, attackType);
                    }

                    if (target->ObjectClass == MCObjectClass::BattleMech)
                    {
                        // Port fix: the original reads body[hitLocation], past the eight body locations for a rear torso hit
                        // (8..10); the torso it maps to is read instead.
                        targetHotSpot = std::static_pointer_cast<MCBattleMech>(target)
                                            ->BodyAt(MechArmorToBodyLocation[hitLocation])
                                            .HotSpotNumber;
                    }
                }

                Assert(hitLocation != -2, 0, " Mech.FireWeapon: Bad Hit Location ");
                MCWeaponShotInfo shot;
                shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage * static_cast<float>(missilesLeft),
                          hitLocation, entryAngle);
                CheckDamageRound(shot);

                if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
                {
                    SendTargetFireChunk(*this, target, targetPoint, chunkWeapon, 1, entryAngle, missiles, missilesLeft,
                                        antiMissileShots, hitLocation);
                }

                std::shared_ptr<MCGameObject> fx = CreateObject(static_cast<int32_t>(WeaponFXTable[weaponEffect]));

                if (fx == nullptr)
                {
                    if (targetPoint.has_value())
                    {
                        CheckMineAt(*targetPoint);
                    }
                }
                else
                {
                    LaunchWeaponFX(*this, std::move(fx), target, targetPoint, shot, sourceHotSpot, targetHotSpot);

                    if (target == nullptr)
                    {
                        Pilot.lock()->ClearCurTacOrder(1, 0);
                    }
                }
            }
        }
        else
        {
            if (target == nullptr)
            {
                hitLocation = -1;
            }
            else if (aimLocation == -1)
            {
                hitLocation = target->CalcHitLocation(GetPosition(), weaponIndex, 0, attackType);
            }

            Assert(hitLocation != -2, 1, " Mech.FireWeapon: Bad Hit Location ");
            MCWeaponShotInfo shot;
            shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage, hitLocation, entryAngle);

            if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
            {
                SendTargetFireChunk(*this, target, targetPoint, chunkWeapon, 1, entryAngle, 0, 0, 0, hitLocation);
            }

            std::shared_ptr<MCGameObject> fx = CreateObject(static_cast<int32_t>(WeaponFXTable[fired.WeaponEffect]));

            if (fx == nullptr)
            {
                if (targetPoint.has_value())
                {
                    CheckMineAt(*targetPoint);
                }
            }
            else
            {
                const int32_t sourceHotSpot = BodyAt(item.BodyLocation).HotSpotNumber;
                int32_t targetHotSpot = 0;

                if (target != nullptr && target->ObjectClass == MCObjectClass::BattleMech)
                {
                    // Port fix: the original reads body[hitLocation], past the eight body locations for a rear torso hit
                    // (8..10); the torso it maps to is read instead.
                    targetHotSpot = std::static_pointer_cast<MCBattleMech>(target)
                                        ->BodyAt(MechArmorToBodyLocation[hitLocation])
                                        .HotSpotNumber;
                }

                LaunchWeaponFX(*this, std::move(fx), target, targetPoint, shot, sourceHotSpot, targetHotSpot);
            }

            if (target == nullptr)
            {
                Pilot.lock()->ClearCurTacOrder(1, 0);
            }
        }
    }
    else if (isStreak == 0)
    {
        // A miss (a streak doesn't fire without a lock): the shot lands somewhere near.
        if (numShots != UNLIMITED_SHOTS)
        {
            DeductWeaponShot(weaponIndex, 1);
        }

        MCInventoryItem& item = Inventory[weaponIndex];
        const MCMasterComponent& fired = MasterComponentList[item.MasterID];
        const float scatter = target != nullptr ? 25.0f : 5.0f;
        MCWeaponShotInfo shot;
        MCVector3D landing;
        int launch = 1;

        if (fired.Form == MCComponentForm::WeaponMissile)
        {
            const int32_t rackSize = fired.NumMissiles;
            int32_t missiles = static_cast<int32_t>(rackSize * 0.5 + 0.5);

            if (missiles < 1)
            {
                missiles = 1;
            }

            if (rackSize < missiles)
            {
                missiles = rackSize;
            }

            if (missiles > 0)
            {
                shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage * static_cast<float>(missiles), -1,
                          entryAngle);
                CheckDamageRound(shot);
                landing = MissPoint(target, targetPoint, scatter, 1);

                if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
                {
                    SendTargetFireChunk(*this, nullptr, landing, chunkWeapon, 0, 0.0f, missiles, 0, 0, 0);
                }
            }
            else
            {
                launch = 0;
            }
        }
        else
        {
            shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage, -1, entryAngle);
            landing = MissPoint(target, targetPoint, scatter, 0);

            if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
            {
                SendTargetFireChunk(*this, nullptr, landing, chunkWeapon, 0, 0.0f, 0, 0, 0, 0);
            }
        }

        if (launch != 0)
        {
            std::shared_ptr<MCGameObject> fx = CreateObject(static_cast<int32_t>(WeaponFXTable[fired.WeaponEffect]));

            if (fx != nullptr)
            {
                LaunchWeaponFX(*this, std::move(fx), nullptr, landing, shot, BodyAt(item.BodyLocation).HotSpotNumber,
                               0);
            }
            else
            {
                CheckMineAt(landing);
            }
        }
    }

    if (targetPilot != nullptr)
    {
        targetPilot->TriggerAlarm(MCPilotAlarmType::TargetOfWeaponFire, static_cast<uint32_t>(PartId));
    }

    RevealFiring(*this);

    if (Group.lock() != nullptr)
    {
        Group.lock()->HandleMateFiredWeapon(static_cast<uint32_t>(PartId));
    }

    return 0;
}

namespace
{
    /// <summary>Weapon effects in flight beyond which a network shot shows none.</summary>
    constexpr int32_t MAX_NETWORK_WEAPON_FX = 200;

    /// <summary>How many weapon effects are in flight.</summary>
    int32_t CountWeaponFX()
    {
        return static_cast<int32_t>(WeaponList()->Size());
    }
}

auto MCBattleMech::HandleWeaponFire(int32_t weaponIndex, std::shared_ptr<MCGameObject> target,
                                    std::optional<MCVector3D> targetPoint, int hit, float entryAngle,
                                    int32_t numMissiles, int32_t missilesPastAMS, int32_t antiMissileShots,
                                    int32_t hitLocation) -> int32_t
{
    const int32_t numShots = GetWeaponShots(weaponIndex);
    StartWeaponRecycle(weaponIndex);
    MCInventoryItem& item = Inventory[weaponIndex];
    const int isStreak = MasterComponentList[item.MasterID].WeaponFlags & 1;
    const MCMasterComponent& fired = MasterComponentList[item.MasterID];
    MCWeaponShotInfo shot;

    if (hit == 0)
    {
        Assert(target == nullptr, 0, " Mech.handleWeaponFire: target should be NULL with network miss! ");
        Assert(targetPoint.has_value(), 0, " Mech.handleWeaponFire: MUST have targetpoint with network miss! ");

        if (isStreak != 0)
        {
            CurMoverWeaponFireChunk.Unpack(*this);
            DebugWeaponFireChunk(CurMoverWeaponFireChunk, std::nullopt, SharedAs<MCGameObject>());
            Assert(false, 0, " Mech.handleWeaponFire: streaks shouldn't miss! ");
        }

        if (numShots != 9999)
        {
            DeductWeaponShot(weaponIndex, 1);
        }

        if (fired.Form == MCComponentForm::WeaponMissile)
        {
            if (numMissiles != 0)
            {
                std::shared_ptr<MCGameObject> fx;

                if (CountWeaponFX() < MAX_NETWORK_WEAPON_FX)
                {
                    fx = CreateObject(static_cast<int32_t>(WeaponFXTable[fired.WeaponEffect]));
                }

                if (fx != nullptr)
                {
                    const int32_t sourceHotSpot = BodyAt(item.BodyLocation).HotSpotNumber;
                    shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage * static_cast<float>(numMissiles),
                              -1, entryAngle);
                    CheckDamageRound(shot);
                    LaunchWeaponFX(*this, std::move(fx), nullptr, targetPoint, shot, sourceHotSpot, 0);
                }
                else if (targetPoint.has_value())
                {
                    CheckMineAt(*targetPoint);
                }
            }
        }
        else
        {
            shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage, -1, entryAngle);
            std::shared_ptr<MCGameObject> fx;

            if (CountWeaponFX() < MAX_NETWORK_WEAPON_FX)
            {
                fx = CreateObject(static_cast<int32_t>(WeaponFXTable[fired.WeaponEffect]));
            }

            if (fx != nullptr)
            {
                LaunchWeaponFX(*this, std::move(fx), nullptr, targetPoint, shot,
                               BodyAt(item.BodyLocation).HotSpotNumber, 0);
            }
            else if (targetPoint.has_value())
            {
                CheckMineAt(*targetPoint);
            }
        }
    }
    else
    {
        if (numShots != 9999)
        {
            DeductWeaponShot(weaponIndex, 1);
        }

        if (fired.Form == MCComponentForm::WeaponMissile)
        {
            if (antiMissileShots > 0)
            {
                target->ReduceAntiMissileAmmo(antiMissileShots);
            }

            int32_t targetHotSpot = 0;
            const int32_t sourceHotSpot = BodyAt(item.BodyLocation).HotSpotNumber;

            if (missilesPastAMS > 0)
            {
                std::shared_ptr<MCGameObject> fx;

                if (CountWeaponFX() < MAX_NETWORK_WEAPON_FX)
                {
                    fx = CreateObject(static_cast<int32_t>(WeaponFXTable[fired.WeaponEffect]));
                }

                if (fx != nullptr)
                {
                    Assert(hitLocation != -2, static_cast<uint32_t>(TargetRolo),
                           " Mech.handleWeaponFire: Bad Hit Location ");

                    if (target != nullptr && target->ObjectClass == MCObjectClass::BattleMech)
                    {
                        // Port fix: the original reads body[hitLocation], past the eight body locations for a rear torso hit
                        // (8..10); the torso it maps to is read instead.
                        targetHotSpot = std::static_pointer_cast<MCBattleMech>(target)
                                            ->BodyAt(MechArmorToBodyLocation[hitLocation])
                                            .HotSpotNumber;
                    }

                    shot.Init(SharedAs<MCGameObject>(), item.MasterID,
                              fired.Damage * static_cast<float>(missilesPastAMS), hitLocation, entryAngle);
                    CheckDamageRound(shot);
                    LaunchWeaponFX(*this, std::move(fx), target, targetPoint, shot, sourceHotSpot, targetHotSpot);

                    if (target == nullptr)
                    {
                        Pilot.lock()->ClearCurTacOrder(1, 0);
                    }
                }
                else if (targetPoint.has_value())
                {
                    CheckMineAt(*targetPoint);
                }
            }
        }
        else
        {
            shot.Init(SharedAs<MCGameObject>(), item.MasterID, fired.Damage, hitLocation, entryAngle);
            std::shared_ptr<MCGameObject> fx;

            if (CountWeaponFX() < MAX_NETWORK_WEAPON_FX)
            {
                fx = CreateObject(static_cast<int32_t>(WeaponFXTable[fired.WeaponEffect]));
            }

            if (fx != nullptr)
            {
                const int32_t sourceHotSpot = BodyAt(item.BodyLocation).HotSpotNumber;
                int32_t targetHotSpot = 0;

                if (target != nullptr && target->ObjectClass == MCObjectClass::BattleMech)
                {
                    // Port fix: the original reads body[hitLocation], past the eight body locations for a rear torso hit
                    // (8..10); the torso it maps to is read instead.
                    targetHotSpot = std::static_pointer_cast<MCBattleMech>(target)
                                        ->BodyAt(MechArmorToBodyLocation[hitLocation])
                                        .HotSpotNumber;
                }

                LaunchWeaponFX(*this, std::move(fx), target, targetPoint, shot, sourceHotSpot, targetHotSpot);

                if (target == nullptr)
                {
                    Pilot.lock()->ClearCurTacOrder(1, 0);
                }
            }
            else if (targetPoint.has_value())
            {
                CheckMineAt(*targetPoint);
            }
        }
    }

    if (target != nullptr &&
        (target->ObjectClass == MCObjectClass::BattleMech || target->ObjectClass == MCObjectClass::GroundVehicle ||
         target->ObjectClass == MCObjectClass::Elemental || target->ObjectClass == MCObjectClass::Mover))
    {
        std::shared_ptr<MCMechWarrior> targetPilot = target->GetPilot();
        targetPilot->UpdateAttackerStatus(static_cast<uint32_t>(PartId), ScenarioTime);
        targetPilot->TriggerAlarm(MCPilotAlarmType::TargetOfWeaponFire, static_cast<uint32_t>(PartId));
    }

    RevealFiring(*this);

    if (Group.lock() != nullptr)
    {
        Group.lock()->HandleMateFiredWeapon(static_cast<uint32_t>(PartId));
    }

    return 0;
}

namespace
{
    /// <summary>
    /// A weapon's damage per ten seconds: its damage times the missiles that land (in whole clusters for SRMs and
    /// LRMs, half the rack), over its recycle time.
    /// </summary>
    float WeaponDamageRate(const MCMasterComponent& weapon)
    {
        int32_t clusterSize = 1;
        int32_t numClusters = 1;

        if (weapon.Form == MCComponentForm::WeaponMissile && (weapon.MissileType == 1 || weapon.MissileType == 2))
        {
            clusterSize = weapon.MissileType == 1 ? ClusterSizeSrm : ClusterSizeLrm;
            numClusters = weapon.NumMissiles / 2 / clusterSize;

            if (weapon.NumMissiles / 2 % clusterSize != 0)
            {
                numClusters++;
            }
        }

        return static_cast<float>(clusterSize * numClusters) * weapon.Damage * 10.0f / weapon.RecycleTime;
    }
}

auto MCBattleMech::CalcMaxTargetDamage() -> float
{
    float total = 0.0f;

    for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
    {
        const float damage = WeaponDamageRate(MasterComponentList[Inventory[i].MasterID]) * 100.0f;

        if (0.0f < damage)
        {
            total = damage + total;
        }
    }

    MaxTargetDamage = total;
    return total;
}

auto MCBattleMech::CalcExpectedTargetDamage(std::shared_ptr<MCGameObject> target) -> float
{
    float total = 0.0f;

    if (GetPilot() == nullptr)
    {
        return 0.0f;
    }

    std::shared_ptr<MCGameObject> aimTarget;
    float targetTime;

    if (target == nullptr)
    {
        aimTarget = GetPilot()->GetLastTarget();

        if (aimTarget == nullptr)
        {
            return 0.0f;
        }

        targetTime = GetPilot()->LastTargetTime;
    }
    else
    {
        targetTime = GetPilot()->GetLastTarget() == target ? GetPilot()->LastTargetTime : 0.0f;
        aimTarget = target;
    }

    MCVector3D targetPosition = aimTarget->GetPosition();
    const auto distance = static_cast<float>(DistanceFrom(targetPosition));

    if (GetFireRange(-2) < distance)
    {
        return 0.0f;
    }

    for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
    {
        if (IsWeaponWorking(i) == 0)
        {
            continue;
        }

        const float damageRate = WeaponDamageRate(MasterComponentList[Inventory[i].MasterID]);
        int32_t aimLocation = -1;

        if (Pilot.lock() != nullptr && Pilot.lock()->CurTacOrder.IsCombatOrder() != 0)
        {
            aimLocation = Pilot.lock()->CurTacOrder.AttackParams.AimLocation;
        }

        int32_t range = 0;
        const double expected =
            static_cast<double>(CalcAttackChance(aimTarget, aimLocation, targetTime, i, 0.0f, range, std::nullopt)) *
            damageRate;

        if (0.0 < expected)
        {
            total = static_cast<float>(expected + total);
        }
    }

    MaxTargetDamage = total;
    return total;
}

auto MCBattleMech::IsWeaponWorking(int32_t weaponIndex) -> int
{
    if (Inventory[weaponIndex].Disabled != 0)
    {
        return 0;
    }

    return GetWeaponShots(weaponIndex) != 0 ? 1 : 0;
}

auto MCBattleMech::DamageLoadedComponents() -> void
{
    for (int32_t location = 0; location < NumMechBodyLocations; location++)
    {
        for (int32_t i = 0; i < NumLocationCriticalSpaces[location]; i++)
        {
            const MCCriticalSpace& space = BodyAt(location).CriticalSpaces[i];

            if (space.Hit != 0)
            {
                HitInventoryItem(static_cast<int8_t>(space.InventoryID), 1);
            }
        }
    }
}
