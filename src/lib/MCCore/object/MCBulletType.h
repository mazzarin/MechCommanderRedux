#pragma once

#include "object/MCObjectType.h"

/// <summary>The type of a <see cref="MCBullet"/>: its speed, sound, and the effects it makes on hit and miss.</summary>
/// <remarks>
/// Original source: <c>object\bullet.cpp</c>. Read from the "BulletData" block of its FIT; loading it also loads the
/// hit, miss and smoke object types.
/// </remarks>
class MCBulletType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCBullet"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override
    {
        return MadeTypesAnd({BulletHitEffect, BulletMissEffect, SmokeObjectId, LightObjectId});
    }

    /// <summary>
    /// Reads the "BulletData" block (if present) and the common type data, then loads the hit, miss and smoke types.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override { return 0; }
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Sample played when the bullet is fired (FIT "SoundEffectId"); 0xFFFFFFFF for none.</summary>
    uint32_t SoundEffectId = 0xffffffff;
    /// <summary>Object type created where the bullet hits its target (FIT "BulletHitEffect").</summary>
    uint32_t BulletHitEffect = 0xffffffff;
    /// <summary>Object type created where a bullet without a target lands (FIT "BulletMissEffect").</summary>
    uint32_t BulletMissEffect = 0xffffffff;
    /// <summary>Object type of the smoke trail (FIT "SmokeObjectId"); -1 for none.</summary>
    uint32_t SmokeObjectId = 0xffffffff;
    /// <summary>Object type of the light that travels with the bullet (FIT "LightObjectId"); -1 for none.</summary>
    uint32_t LightObjectId = 0;
    /// <summary>Speed in world units per second (FIT "Velocity").</summary>
    float Velocity = 0;
    /// <summary>FIT "CloseDistance"; not used by the bullet.</summary>
    float CloseDistance = 0;
};
