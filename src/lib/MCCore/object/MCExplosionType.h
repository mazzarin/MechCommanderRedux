#pragma once

#include "object/MCObjectType.h"

/// <summary>The type of an <see cref="MCExplosion"/>: the damage it deals, over what radius, and its sound and light.</summary>
/// <remarks>Original source: <c>object\explode.cpp</c>. Read from the "ExplosionData" block of its FIT.</remarks>
class MCExplosionType : public MCObjectType
{
public:
    /// <summary>Makes an <see cref="MCExplosion"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({LightObjectId}); }
    /// <summary>
    /// Reads DmgLevel, SoundEffectId, ExplosionRadius (default 0), LightObjectId (default -1) and DamageChunkSize
    /// (default 5) from the "ExplosionData" block, then the common type data.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>
    /// Damages what the explosion touches with all its damage: movers and turrets and gates in hits of at most
    /// damageChunkSize (movers on random hit locations; turrets and gates only when the blast reaches their little
    /// extent), anything else in one hit. The damage doesn't fall off with distance. Only the host applies it in
    /// multiplayer.
    /// </summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>The damage the explosion deals (FIT "DmgLevel").</summary>
    uint32_t DmgLevel = 0;
    /// <summary>Sample played when the explosion starts (FIT "SoundEffectId"); 0xFFFFFFFF for none.</summary>
    uint32_t SoundEffectId = 0xffffffff;
    /// <summary>Object type of the light the explosion creates (FIT "LightObjectId"); -1 for none.</summary>
    uint32_t LightObjectId = 0;
    /// <summary>The blast radius (FIT "ExplosionRadius"); 0 means the explosion damages nothing around it.</summary>
    int32_t ExplosionRadius = 0;
    /// <summary>The largest single hit the damage is split into (FIT "DamageChunkSize", default 5).</summary>
    float DamageChunkSize = 0;
};
