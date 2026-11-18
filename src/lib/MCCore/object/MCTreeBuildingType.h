#pragma once

#include "object/MCBuildingMarines.h"
#include "object/MCObjectType.h"
#include "platform/MCRegisteredBlock.h"

/// <summary>
/// The type of a <see cref="MCTreeBuilding"/>: a building drawn as a sprite with shadows, which can be a refit point
/// or mech bay.
/// </summary>
/// <remarks>Original source: <c>object\tbldng.cpp</c>, <c>object\tbldng.h</c>. Read from the "TreeData" block of its
/// FIT.</remarks>
class MCTreeBuildingType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCTreeBuilding"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>
    /// Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own: its blown effect and, with marines, the
    /// marine it lets out (DefaultPilotId).
    /// </summary>
    std::vector<int32_t> MadeTypes() const override
    {
        return NumMarines > 0 ? MadeTypesAnd({BlownEffectId, static_cast<uint32_t>(DefaultPilotId)})
                              : MadeTypesAnd({BlownEffectId});
    }

    /// <summary>
    /// Reads the "TreeData" block (loading the NormalShadow and DestroyedShadow shapes), then the common type data.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>Always reports a collision and does nothing else.</summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override { return 1; }
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>The damage that destroys the building; 0 means it starts destroyed (FIT "DmgLevel").</summary>
    uint32_t DmgLevel = 0;
    /// <summary>
    /// The object type made when the building is set burning or destroyed (a fire), or -1 (FIT "BlownEffectId", 0 when
    /// missing).
    /// </summary>
    uint32_t BlownEffectId = 0xffffffff;
    /// <summary>The looping sound played while the building is visible, or 0xffffffff (FIT "NormalEffectId").</summary>
    uint32_t NormalEffectId = 0xffffffff;
    /// <summary>The sound played when the building is destroyed, or 0xffffffff (FIT "DamageEffectId").</summary>
    uint32_t DamageEffectId = 0xffffffff;
    /// <summary>The shadow shape of the standing building (FIT "NormalShadow").</summary>
    MCRegisteredBlock NormalShadow;
    /// <summary>The shadow shape of the destroyed building (FIT "DestroyedShadow").</summary>
    MCRegisteredBlock DestroyedShadow;
    /// <summary>The range of the building's sensor, or -1 for none (FIT "SensorRange").</summary>
    float SensorRange = -1.0f;
    /// <summary>The team (0 Inner Sphere, 1 Clan, 2 allies) the building belongs to, or -1 (FIT "TeamID").</summary>
    int32_t TeamId = -1;
    /// <summary>FIT "Tonnage" (default 20).</summary>
    float BaseTonnage = 0;
    /// <summary>FIT "ExplosionDamage".</summary>
    float ExplDmg = 0;
    /// <summary>FIT "ExplosionRadius".</summary>
    float ExplRad = 0;
    /// <summary>Seconds between burn damage while on fire (FIT "TimeToBurnDamage", default 5).</summary>
    float TimeToBurnDamage = 0;
    /// <summary>The damage burning deals each time (FIT "BurnDamagePerTime", default 1).</summary>
    float BurnDamagePerTime = 0;
    /// <summary>FIT "DamageLvlForBurn" (default the damage level).</summary>
    float DamageLvlForBurn = 0;
    /// <summary>The string resource id of the building's name (FIT "BuildingName", default 0xa3).</summary>
    int32_t BuildingName = 0;
    /// <summary>The building's combat value (FIT "BattleRating", default 20).</summary>
    int32_t BattleRating = 0;
    /// <summary>How many marines come out when it is destroyed (FIT "NumMarines").</summary>
    int32_t NumMarines = 0;
    /// <summary>Whether the building repairs units (FIT "CanRefit").</summary>
    bool CanRefit = false;
    /// <summary>Whether a refit building is a mech bay (FIT "MechBay", read only when CanRefit).</summary>
    bool MechBay = false;
};
