#pragma once

#include "appear/MCAppearanceType.h"

/// <summary>The single animation state of an arm (weapon effect) appearance type.</summary>
struct MCArmActorData
{
    /// <summary>FIT "NumRotations".</summary>
    uint8_t NumRotations = 0;
    /// <summary>FIT "NumFrames".</summary>
    uint32_t NumFrames = 0;
    /// <summary>FIT "BasePacketNumber".</summary>
    uint32_t BasePacketNumber = 0;
    /// <summary>FIT "FrameRate".</summary>
    float FrameRate = 0.0f;
    /// <summary>FIT "Symmetrical".</summary>
    bool Symmetrical = false;
};

/// <summary>
/// The type of an <see cref="MCArmAppearance"/>: a weapon effect (missile, bullet, jet...) drawn from one shape per
/// facing.
/// </summary>
/// <remarks>
/// Original source: <c>sprite\armactor.cpp</c> (class 6 of the sprite PAK). FIT: "State" (NumFrames, FrameRate,
/// BasePacketNumber, NumRotations, Symmetrical, CheckForHeader).
/// </remarks>
class MCArmAppearanceType : public MCAppearanceType
{
public:
    MCArmAppearanceType() = default;

    /// <summary>Reads the FIT and makes the shape list (one entry per packet).</summary>
    MCAppearanceLoad Load(MCFile& apprFile, uint32_t fileSize) override;

    /// <summary>
    /// The shape facing <paramref name="rotation"/> degrees; <paramref name="frameRate"/> gets the frame rate, and
    /// <paramref name="reverse"/> is set when the shape is to be mirrored.
    /// </summary>
    std::shared_ptr<MCShape> GetShape(int32_t rotation, float& frameRate, bool& reverse);

    /// <summary>
    /// Port-only: a frameless arm draws none of its shapes (GetShape gives none), so none is read at the load.
    /// </summary>
    bool DrawsShapes() const override { return ActorData.NumFrames != 0; }

    /// <summary>The state.</summary>
    MCArmActorData ActorData;
    /// <summary>The loaded shape of each packet (expired once the cache drops it).</summary>
    std::vector<std::weak_ptr<MCShape>> ShapeList;
    /// <summary>FIT "CheckForHeader" (1 when absent); never read.</summary>
    uint8_t CheckForHeader = 1;
};
