#pragma once

#include "object/MCObjectType.h"
#include "platform/MCRegisteredBlock.h"

/// <summary>
/// The type shared by the terrain features that can be damaged in place: bridges, forest tiles and walls (heavy,
/// medium and light). Holds each kind's damage threshold and effect ids, and the forest-edge shapes.
/// </summary>
/// <remarks>Original source: <c>object\bridge.cpp</c>, <c>object\bridge.h</c>. Read from the "BridgeData" block of
/// its FIT.</remarks>
class MCMiscTerrainObjectType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCMiscTerrainObject"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({ForestFireFX}); }
    /// <summary>
    /// Reads the damage levels and effect ids from the "BridgeData" block, loads the ForestEdges shape file, then the
    /// common type data.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>A mech or vehicle running into a light wall crushes it: a 250-point hit on the wall.</summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Damage that destroys a heavy wall (FIT "WallDmgLevel").</summary>
    uint32_t WallDmgLevel = 0;
    /// <summary>Damage that destroys a medium wall (FIT "MediumWallDmgLevel", default half the wall's).</summary>
    uint32_t MediumWallDmgLevel = 0;
    /// <summary>Damage that destroys a light wall (FIT "LightWallDmgLevel"; OB-021).</summary>
    uint32_t LightWallDmgLevel = 0;
    /// <summary>Damage that destroys a bridge (FIT "BridgeDmgLevel").</summary>
    uint32_t BridgeDmgLevel = 0;
    /// <summary>Damage that burns down a forest tile (FIT "ForestDmgLevel").</summary>
    uint32_t ForestDmgLevel = 0;
    /// <summary>FIT "BlownEffectId".</summary>
    uint32_t BlownEffectId = 0xffffffff;
    /// <summary>FIT "NormalEffectId".</summary>
    uint32_t NormalEffectId = 0xffffffff;
    /// <summary>FIT "DamageEffectId".</summary>
    uint32_t DamageEffectId = 0xffffffff;
    /// <summary>Object type of a wall's fire (FIT "WallFireFX").</summary>
    uint32_t WallFireFX = 0xffffffff;
    /// <summary>Object type of a bridge's fire (FIT "BridgeFireFX").</summary>
    uint32_t BridgeFireFX = 0xffffffff;
    /// <summary>Object type of the fire started on a forest tile (FIT "ForestFireFX").</summary>
    uint32_t ForestFireFX = 0xffffffff;
    /// <summary>The ForestEdges shape file: drawn over burnt forest edges.</summary>
    MCRegisteredBlock ForestEdgeShapes;
};
