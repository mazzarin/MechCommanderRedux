#pragma once

#include "object/MCObjectType.h"

/// <summary>
/// The type of a <see cref="MCProjectileLaser"/> (a pulse of laser light that travels like a bullet): its speed and
/// shape, its colours for friendly and enemy shooters, and its sound, effects, smoke and light.
/// </summary>
/// <remarks>
/// Original source: <c>object\prjlase.cpp</c>. Read from the "ProjectileLaserData" block; loading it also loads the
/// hit and miss object types.
/// </remarks>
class MCProjectileLaserType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCProjectileLaser"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override
    {
        return MadeTypesAnd({ProjectileHitEffect, ProjectileMissEffect, SmokeObjectId, LightObjectId});
    }

    /// <summary>
    /// Reads the "ProjectileLaserData" block (if present) and the common type data, then loads the hit and miss types.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override { return 0; }
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Sample played when fired (FIT "SoundEffectId"); 0xFFFFFFFF for none.</summary>
    uint32_t SoundEffectId = 0xffffffff;
    /// <summary>Object type created where it hits its target (FIT "ProjectileHitEffect").</summary>
    uint32_t ProjectileHitEffect = 0xffffffff;
    /// <summary>Object type created where a shot without a target lands (FIT "ProjectileMissEffect").</summary>
    uint32_t ProjectileMissEffect = 0xffffffff;
    /// <summary>Object type of the smoke trail (FIT "SmokeObjectId", default -1).</summary>
    uint32_t SmokeObjectId = 0;
    /// <summary>Object type of the light travelling with it (FIT "LightObjectId", default -1).</summary>
    uint32_t LightObjectId = 0;
    /// <summary>Speed in world units per second (FIT "Velocity").</summary>
    float Velocity = 0;
    /// <summary>Distance from the target at which the smoke trail stops (FIT "CloseDistance").</summary>
    float CloseDistance = 0;
    /// <summary>The four colours used when the owner is friendly (FIT "f0Color".."f3Color").</summary>
    std::array<uint8_t, 4> FColor{};
    /// <summary>The four colours used when the owner is an enemy (FIT "e0Color".."e3Color").</summary>
    std::array<uint8_t, 4> EColor{};
    /// <summary>Distance from the head to the tail (FIT "ProjectileLength").</summary>
    float ProjectileLength = 0;
    /// <summary>Distance from the head to the bulge (FIT "BulgeLength").</summary>
    float BulgeLength = 0;
    /// <summary>Half width of the bulge (FIT "BulgeWidth").</summary>
    float BulgeWidth = 0;
};
