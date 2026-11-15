#pragma once

#include "object/MCObjectType.h"

class MCFile;

/// <summary>
/// The type of a <see cref="MCTurret"/>: its damage levels and effects, its single weapon, attack radius, yaw rate and
/// pilot skill, and the pixel offsets of its base, muzzle and centre.
/// </summary>
/// <remarks>Original source: <c>object\turret.cpp</c>, <c>object\turret.h</c>. Read from the "TurretData" block of its
/// FIT.</remarks>
class MCTurretType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCTurret"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({BlownEffectId}); }
    /// <summary>Reads the "TurretData" block (with defaults for the optional keys), then the common type data.</summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>
    /// An object entering the turret's range: an enemy mover (or object class 0x1c) that is alive and closer than the
    /// current target becomes the target. Always 1 (the turret isn't blocked).
    /// </summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    /// <summary>Does nothing (the turret's own hit handling destroys it).</summary>
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override;

    /// <summary>Damage that destroys the turret (FIT "DmgLevel").</summary>
    uint32_t DmgLevel = 0;
    /// <summary>Damage level while closed (FIT "DmgLevelClosed"; defaults to dmgLevel).</summary>
    uint32_t DmgLevelClosed = 0;
    /// <summary>Object type made when the turret is destroyed, e.g. a fire (FIT "BlownEffectId"; -1 = none).</summary>
    uint32_t BlownEffectId = 0xffffffff;
    /// <summary>FIT "NormalEffectId" (-1 = none).</summary>
    uint32_t NormalEffectId = 0xffffffff;
    /// <summary>FIT "DamageEffectId" (-1 = none).</summary>
    uint32_t DamageEffectId = 0xffffffff;
    /// <summary>FIT "Tonnage" (default 20).</summary>
    float Tonnage = 0;
    /// <summary>FIT "BasePixelOffsetX".</summary>
    int32_t BasePixelOffsetX = 0;
    /// <summary>FIT "BasePixelOffsetY".</summary>
    int32_t BasePixelOffsetY = 0;
    /// <summary>FIT "ExplosionDamage".</summary>
    float ExplosionDamage = 0;
    /// <summary>FIT "ExplosionRadius".</summary>
    float ExplosionRadius = 0;
    /// <summary>FIT "LittleExtent" (default 20).</summary>
    float LittleExtent = 0;
    /// <summary>FIT "AttackRadius"; when nonzero it becomes the type's extent radius.</summary>
    float AttackRadius = 0;
    /// <summary>Degrees per second the turret turns (FIT "MaxTurretYawRate").</summary>
    float MaxTurretYawRate = 0;
    /// <summary>The weapon's master component id (FIT "WeaponType"; -1 before it is read).</summary>
    int32_t WeaponType = -1;
    /// <summary>Added to the attack chance (FIT "PilotSkill").</summary>
    int32_t PilotSkill = 0;
    /// <summary>String resource id of the turret's name (FIT "BuildingName", default 0xa4).</summary>
    int32_t BuildingName = 0;
    /// <summary>FIT "FireOffsetX".</summary>
    int32_t FireOffsetX = 0;
    /// <summary>FIT "FireOffsetY".</summary>
    int32_t FireOffsetY = 0;
    /// <summary>FIT "CenterOffsetX".</summary>
    int32_t CenterOffsetX = 0;
    /// <summary>FIT "CenterOffsetY".</summary>
    int32_t CenterOffsetY = 0;
};
