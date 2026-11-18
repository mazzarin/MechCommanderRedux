#pragma once

#include "object/MCObjectType.h"

/// <summary>One flame of a fire type: where it stands from the fire and when it starts, each with a random spread.</summary>
struct MCFireShape
{
    /// <summary>X offset from the fire's position (FIT "FireOffsetX%d").</summary>
    float OffsetX = 0;
    /// <summary>Y offset (FIT "FireOffsetY%d").</summary>
    float OffsetY = 0;
    /// <summary>Tenths of a second before it starts (FIT "FireDelay%d").</summary>
    float Delay = 0;
    /// <summary>Random spread of the X offset (FIT "FireRandomOffsetX%d").</summary>
    int32_t RandomOffsetX = 0;
    /// <summary>Random spread of the Y offset (FIT "FireRandomOffsetY%d").</summary>
    int32_t RandomOffsetY = 0;
    /// <summary>Random extra delay, in tenths of a second (FIT "FireRandomDelay%d").</summary>
    int32_t RandomDelay = 0;
};

/// <summary>
/// The type of a <see cref="MCFire"/>: its damage, sound and light, how its animation loops, and the flames that make
/// it up.
/// </summary>
/// <remarks>Original source: <c>object\fire.cpp</c>, <c>object\fire.h</c>. Read from the "FireData" block.</remarks>
class MCFireType : public MCObjectType
{
public:
    /// <summary>Makes a <see cref="MCFire"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own.</summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({LightObjectId}); }
    /// <summary>
    /// Reads the "FireData" block, each flame's FireOffsetX/Y, FireDelay and their random spreads, then the common
    /// type data.
    /// </summary>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>
    /// A fire touching a building, tree, misc terrain object or tree building sets it alight one time in ten (host
    /// only in multiplayer, which then sends a light-on-fire chunk).
    /// </summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override { return 0; }

    /// <summary>Damage level (FIT "DmgLevel").</summary>
    uint32_t DmgLevel = 0;
    /// <summary>FIT "SoundEffectId"; 0xFFFFFFFF for none.</summary>
    uint32_t SoundEffectId = 0xffffffff;
    /// <summary>Object type of the fire's light (FIT "LightObjectId"); -1 for none.</summary>
    uint32_t LightObjectId = 0;
    /// <summary>First frame of the looped part of the animation (FIT "startLoopFrame").</summary>
    uint32_t StartLoopFrame = 0;
    /// <summary>Last frame of the looped part (FIT "endLoopFrame").</summary>
    uint32_t EndLoopFrame = 0;
    /// <summary>How many times each flame loops (FIT "numLoops"); the start of each flame's loop count.</summary>
    uint32_t NumLoops = 0;
    /// <summary>The fire's extent radius once it burns out and turns on collision (FIT "maxExtentRadius", default 0).</summary>
    float MaxExtentRadius = 0;
    /// <summary>FIT "TimeToMaxExtent" (default 0); not used by the fire.</summary>
    float TimeToMaxExtent = 0;
    /// <summary>The flames (FIT "TotalFireShapes" of them, default 1).</summary>
    std::vector<MCFireShape> Shapes = std::vector<MCFireShape>(1);
};
