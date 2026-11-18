#include "stdafx.h"
#include "object/MCTurretType.h"
#include "object/MCTurret.h"
#include "camera/MCCamera.h"
#include "lib/MCFitIniFile.h"
#include "mission/MCScenario.h"
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
#include "object/MCFire.h"
#include "object/MCFireType.h"
#include "object/MCEffectSystem.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectEvent.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sound/MCSoundSystem.h"
#include "object/MCObjectType.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"

auto MCTurretType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newTurret = std::make_shared<MCTurret>();

    if (newTurret->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newTurret->IdNumber = NextIdNumber++;
    return newTurret;
}

auto MCTurretType::MadeTypes() const -> std::vector<int32_t>
{
    // The weapon is read with the type (WeaponType -1 before), its effect as MCTurret's CreateWeaponFX makes it.
    if (WeaponType < 0 || static_cast<size_t>(WeaponType) >= MasterComponentList.size())
    {
        return MadeTypesAnd({BlownEffectId});
    }

    return MadeTypesAnd(
        {BlownEffectId, WeaponFXTable[static_cast<uint8_t>(MasterComponentList[WeaponType].WeaponEffect)]});
}

auto MCTurretType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    MCFitIniFile turretFile;

    if (const int32_t result = turretFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = turretFile.SeekBlock("TurretData"); result != 0)
    {
        return result;
    }

    MCFitReader read(turretFile);
    read.Value("DmgLevel", DmgLevel);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    const MCFitResult<uint32_t> closed = turretFile.Read<uint32_t>("DmgLevelClosed");
    DmgLevelClosed = closed.value_or(DmgLevel);

    // Original behaviour (OB-011): the three effect ids test DmgLevelClosed's result, not their own; a type without
    // DmgLevelClosed loses all three. A missing id reads as zero, as the original's read stored it.
    const auto effectId = [&](std::string_view name) -> uint32_t
    {
        const MCFitResult<uint32_t> id = turretFile.Read<uint32_t>(name);

        if (!closed.has_value())
        {
            return 0xffffffff;
        }

        if (id.has_value())
        {
            return *id;
        }

        return id.error() == MCFitError::VariableNotFound ? 0 : 0xffffffff;
    };

    BlownEffectId = effectId("BlownEffectId");
    NormalEffectId = effectId("NormalEffectId");
    DamageEffectId = effectId("DamageEffectId");
    BasePixelOffsetX = turretFile.Read<int32_t>("BasePixelOffsetX").value_or(0);
    BasePixelOffsetY = turretFile.Read<int32_t>("BasePixelOffsetY").value_or(0);
    ExplosionRadius = turretFile.Read<float>("ExplosionRadius").value_or(0.0f);
    ExplosionDamage = turretFile.Read<float>("ExplosionDamage").value_or(0.0f);
    Tonnage = turretFile.Read<float>("Tonnage").value_or(20.0f);
    read.Value("AttackRadius", AttackRadius);
    read.Value("MaxTurretYawRate", MaxTurretYawRate);
    read.Value("WeaponType", WeaponType);
    read.Value("PilotSkill", PilotSkill);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    LittleExtent = turretFile.Read<float>("LittleExtent").value_or(20.0f);
    BuildingName = turretFile.Read<int32_t>("BuildingName").value_or(0xa4);
    FireOffsetX = turretFile.Read<int32_t>("FireOffsetX").value_or(0);
    FireOffsetY = turretFile.Read<int32_t>("FireOffsetY").value_or(0);
    CenterOffsetX = turretFile.Read<int32_t>("CenterOffsetX").value_or(0);
    CenterOffsetY = turretFile.Read<int32_t>("CenterOffsetY").value_or(0);
    return MCObjectType::Init(turretFile);
}

auto MCTurretType::HandleCollision(MCGameObject& collidee, MCGameObject& collider) -> int
{
    // Only the server picks targets.
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return 1;
    }

    auto& turret = static_cast<MCTurret&>(collidee);
    // The squared distance to the current target, or far.
    float targetDistance = 1e+07f;

    if (LockObject(turret.Target) != nullptr)
    {
        const MCVector3D targetPosition = LockObject(turret.Target)->GetPosition();
        const MCVector3D turretPosition = turret.GetPosition();
        targetDistance = (turretPosition.X - targetPosition.X) * (turretPosition.X - targetPosition.X) +
                         (turretPosition.Y - targetPosition.Y) * (turretPosition.Y - targetPosition.Y);
    }

    const int32_t alignmentGap = std::abs(collider.GetAlignment() - collidee.GetAlignment());

    if (alignmentGap <= 1)
    {
        return 1;
    }

    const MCObjectClass colliderClass = collider.ObjectClass;

    if (colliderClass < MCObjectClass::BattleMech)
    {
        return 1;
    }

    if (colliderClass < MCObjectClass::Explosion)
    {
        // Mechs, vehicles and elementals: only whole ones.
        if (collider.IsDisabled() != 0 || collider.IsDestroyed() != 0)
        {
            return 1;
        }
    }
    else if (colliderClass != MCObjectClass::CameraDrone || collider.IsDestroyed() != 0)
    {
        return 1;
    }

    const MCVector3D colliderPosition = collider.GetPosition();
    const MCVector3D turretPosition = collidee.GetPosition();

    if ((turretPosition.X - colliderPosition.X) * (turretPosition.X - colliderPosition.X) +
            (turretPosition.Y - colliderPosition.Y) * (turretPosition.Y - colliderPosition.Y) <
        targetDistance)
    {
        turret.Target = collider.SharedAs<MCGameObject>();
    }

    return 1;
}

auto MCTurretType::HandleDestruction(MCGameObject& /*collidee*/, uint32_t /*colliderId*/) -> int
{
    return 0;
}
