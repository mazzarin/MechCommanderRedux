#pragma once

#include "object/MCObjectType.h"
#include "platform/MCRegisteredBlock.h"

/// <summary>One colour stage of a laser beam: how long it lasts, and its outer (cool) and core (hot) colours.</summary>
struct MCLaserStage
{
    /// <summary>Seconds (FIT "StageDuration").</summary>
    float Duration = 0;
    /// <summary>The outer palette colour (FIT "StageCool").</summary>
    uint8_t Cool = 0;
    /// <summary>The core palette colour (FIT "StageHot").</summary>
    uint8_t Hot = 0;
};

/// <summary>
/// The type of a <see cref="MCLaser"/> beam: its width and colour stages (one set for friendly, one for enemy
/// shooters), damage, sound and hit effects, and for a PPC the effect shape played along the beam.
/// </summary>
/// <remarks>
/// Original source: <c>object\laser.cpp</c>, <c>object\laser.h</c>. Read from the "LaserData" block, the
/// "FLaser%d"/"ELaser%d" stage blocks and, with a LaserEffectShape, the "PPCData" block.
/// </remarks>
class MCLaserType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCLaser"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({LaserHitEffect, LaserMissEffect}); }
    /// <summary>
    /// Reads the laser data, loads the effect shape (from the sprite path) and its PPC data, reads numStages friendly
    /// then numStages enemy stages, the common type data, and loads the hit and miss types.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override { return 0; }
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Beam width in pixels (FIT "PixelWidth").</summary>
    uint8_t PixelWidth = 0;
    /// <summary>Stages per beam (FIT "NumStages").</summary>
    uint8_t NumStages = 0;
    /// <summary>The stages: <see cref="NumStages"/> friendly ones, then as many enemy ones.</summary>
    std::vector<MCLaserStage> Stages;
    /// <summary>FIT "DmgLevel".</summary>
    uint32_t DmgLevel = 0;
    /// <summary>Sample played when the laser fires (FIT "SoundEffectId").</summary>
    uint32_t SoundEffectId = 0;
    /// <summary>Object type created where the beam hits its target (FIT "LaserHitEffect").</summary>
    uint32_t LaserHitEffect = 0xffffffff;
    /// <summary>Object type created where a beam without a target lands (FIT "LaserMissEffect").</summary>
    uint32_t LaserMissEffect = 0xffffffff;
    /// <summary>The PPC effect shape file (FIT "LaserEffectShape"), or empty for a plain beam.</summary>
    MCRegisteredBlock LaserEffectShape;
    /// <summary>Frames of the PPC effect (FIT "numPPCFrames"); the beam ends after the last.</summary>
    uint32_t NumPpcFrames = 0;
    /// <summary>Left edge of the effect in its shape (FIT "lPPC").</summary>
    uint32_t Lppc = 0;
    /// <summary>Top edge (FIT "tPPC").</summary>
    uint32_t Tppc = 0;
    /// <summary>Right edge (FIT "rPPC").</summary>
    uint32_t Rppc = 0;
    /// <summary>Bottom edge (FIT "bPPC").</summary>
    uint32_t Bppc = 0;
    /// <summary>The frame at which the PPC hits and deals its damage (FIT "hitPPC").</summary>
    uint32_t HitPpc = 0;
    /// <summary>Seconds per PPC frame (FIT "lengthPPC").</summary>
    float LengthPpc = 0;
    /// <summary>Animation period of the PPC effect (FIT "animPPC").</summary>
    float AnimPpc = 0;
};
