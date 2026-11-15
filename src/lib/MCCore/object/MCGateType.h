#pragma once

#include "object/MCObjectType.h"

/// <summary>
/// The type of a <see cref="MCGate"/>: the damage that destroys it, its effects and explosion, how close a friendly
/// unit must come for it to open, and its name.
/// </summary>
/// <remarks>Original source: <c>object\gate.cpp</c>, <c>object\gate.h</c>. Read from the "GateData" block of its
/// FIT.</remarks>
class MCGateType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCGate"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({BlownEffectId}); }
    /// <summary>Reads the "GateData" block, then the common type data.</summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>
    /// A mech, vehicle or elemental near the gate: a friendly one (or any, for a neutral gate) within the open radius
    /// asks it to open; one standing right in the gateway is remembered as the object to crush if the gate closes on
    /// it.
    /// </summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Damage that destroys the gate (FIT "DmgLevel").</summary>
    uint32_t DmgLevel = 0;
    /// <summary>
    /// Object type of the fire started when the gate burns or is destroyed; -1 for none (FIT "BlownEffectId", 0 when
    /// missing).
    /// </summary>
    uint32_t BlownEffectId = 0xffffffff;
    /// <summary>FIT "NormalEffectId".</summary>
    uint32_t NormalEffectId = 0xffffffff;
    /// <summary>FIT "DamageEffectId".</summary>
    uint32_t DamageEffectId = 0xffffffff;
    /// <summary>Horizontal pixel offset of the gate's base, replacing the placement's (FIT "BasePixelOffsetX").</summary>
    int32_t BasePixelOffsetX = 0;
    /// <summary>Vertical pixel offset of the gate's base (FIT "BasePixelOffsetY").</summary>
    int32_t BasePixelOffsetY = 0;
    /// <summary>FIT "ExplosionDamage".</summary>
    float ExplosionDamage = 0;
    /// <summary>FIT "ExplosionRadius".</summary>
    float ExplosionRadius = 0;
    /// <summary>How close a friendly unit must be for the gate to open (FIT "OpenRadius"); also its extent radius.</summary>
    float OpenRadius = 0;
    /// <summary>Radius, added to an object's extent, within which a closing gate crushes it (FIT "LittleExtent", default 20).</summary>
    float LittleExtent = 0;
    /// <summary>String resource id of the gate's name (FIT "BuildingName", default 0xa5).</summary>
    int32_t BuildingName = 0;
    /// <summary>Set when the closed gate blocks line of fire (FIT "BlocksLineOfFire").</summary>
    bool BlocksLineOfFire = false;
};
