#pragma once

#include "object/MCObjectType.h"

/// <summary>The type of a <see cref="MCJet"/>: its sound and the objects it trails.</summary>
/// <remarks>Original source: <c>object\jet.cpp</c>. Read from the "JetData" block of its FIT, when it has one.</remarks>
class MCJetType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCJet"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({SmokeObjectId}); }
    /// <summary>Reads SoundEffectId and SmokeObjectId from the "JetData" block (if present), then the common data.</summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override { return 0; }
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Sample played when the jet starts (FIT "SoundEffectId"); 0xFFFFFFFF for none.</summary>
    uint32_t SoundEffectId = 0xffffffff;
    /// <summary>Object type of the <see cref="MCSmoke"/> trail the jet creates (FIT "SmokeObjectId"); -1 for none.</summary>
    uint32_t SmokeObjectId = 0xffffffff;
    /// <summary>
    /// Object type of a second object the jet creates and keeps on the terrain under itself; -1 for none. Never read
    /// from the FIT, so always -1 in practice.
    /// </summary>
    uint32_t GroundObjectId = 0xffffffff;
};
