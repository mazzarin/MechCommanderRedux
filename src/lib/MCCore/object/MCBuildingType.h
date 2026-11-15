#pragma once

#include "object/MCObjectType.h"

/// <summary>
/// The type of a <see cref="MCBuilding"/>: damage level, effects, placement offsets, tonnage, explosion, burning,
/// sensor and team, name and marines.
/// </summary>
/// <remarks>Original source: <c>object\bldng.cpp</c>, <c>object\bldng.h</c>. Read from the "BuildingData" block of its
/// FIT.</remarks>
class MCBuildingType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCBuilding"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({BlownEffectId}); }
    /// <summary>
    /// Reads the "BuildingData" block, then the common type data; the building's own ExtentRadius (-1 when missing:
    /// measured from the appearance) wins.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>
    /// A mover (object class below 8, not artillery) running into the building deals it 10 points of damage, but
    /// only while the scenario time hasn't passed the mover's collision-free time (server only).
    /// </summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>The damage that destroys the building (FIT "DmgLevel").</summary>
    uint32_t DmgLevel = 0;
    /// <summary>The object type made when the building is destroyed or set burning (a fire; FIT "BlownEffectId").</summary>
    uint32_t BlownEffectId = 0xffffffff;
    /// <summary>The looping sound played while the building is visible, or 0xffffffff (FIT "NormalEffectId").</summary>
    uint32_t NormalEffectId = 0xffffffff;
    /// <summary>FIT "DamageEffectId".</summary>
    uint32_t DamageEffectId = 0xffffffff;
    /// <summary>Replaces the placement's pixel offset X when nonzero (FIT "BasePixelOffsetX").</summary>
    int32_t BasePixelOffsetX = 0;
    /// <summary>Replaces the placement's pixel offset Y when nonzero (FIT "BasePixelOffsetY").</summary>
    int32_t BasePixelOffsetY = 0;
    /// <summary>Replaces the pixel offset X when placing the building in the world (FIT "CollisionOffsetX").</summary>
    int32_t CollisionOffsetX = 0;
    /// <summary>Replaces the pixel offset Y when placing the building in the world (FIT "CollisionOffsetY").</summary>
    int32_t CollisionOffsetY = 0;
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
};
