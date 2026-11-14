#pragma once

#include "appear/MCAppearanceType.h"

/// <summary>
/// The animation states of a VFX appearance (the FIT's "State%d" blocks). The names follow MechCommander 2's
/// <c>actor.h</c>, which descends from this code; the values match MCX.EXE's use.
/// </summary>
enum class MCActorState : int32_t
{
    Invalid = -1,
    Normal = 0,
    /// <summary>From normal to damaged (the frame is the damage level).</summary>
    BlowingUp1 = 1,
    Damaged = 2,
    /// <summary>From damaged to destroyed (the frame is the damage level past state 1's frames).</summary>
    BlowingUp2 = 3,
    Destroyed = 4,
    FallenDamaged = 5
};

/// <summary>The number of states a VFX type always has room for.</summary>
inline constexpr int32_t ActorStateCount = 6;

/// <summary>No sub-state (<see cref="MCVfxAppearance::CurrentSubState"/>).</summary>
inline constexpr uint8_t NoSubState = 0xff;

/// <summary>One animation state (or sub-state) of a VFX appearance type, from its FIT.</summary>
struct MCActorData
{
    /// <summary>FIT "State".</summary>
    MCActorState State = MCActorState::Normal;
    /// <summary>FIT "Symmetrical": the negative facings are the positive ones mirrored.</summary>
    bool Symmetrical = false;
    /// <summary>FIT "NumRotations".</summary>
    uint8_t NumRotations = 0;
    /// <summary>FIT "Sub" (sub-states only).</summary>
    uint8_t SubState = 0;
    /// <summary>FIT "Loop" (sub-states only; states always loop).</summary>
    bool Loop = false;
    /// <summary>FIT "NumFrames" (0: the state doesn't exist).</summary>
    uint32_t NumFrames = 0;
    /// <summary>FIT "BasePacketNumber": the state's first packet in the appearance's PAK.</summary>
    uint32_t BasePacketNumber = 0;
    /// <summary>FIT "FrameRate", frames per second.</summary>
    float FrameRate = 0.0f;
};

/// <summary>
/// The type of a <see cref="MCVfxAppearance"/>: an object drawn from one VFX shape per state and facing (effects,
/// terrain objects, trees...).
/// </summary>
/// <remarks>
/// Original source: <c>sprite\actor.cpp</c> (class 2 of the sprite PAK). Its FIT: "Main Info" (delta), "States"
/// (NumStates, Scaled), "State%d" and "Sub%dState%d" blocks.
/// </remarks>
class MCVfxAppearanceType : public MCAppearanceType
{
public:
    MCVfxAppearanceType() = default;

    /// <summary>Reads the FIT and makes the shape list (one entry per packet of the type's PAK).</summary>
    MCAppearanceLoad Load(MCFile& apprFile, uint32_t fileSize) override;

    /// <summary>The data of <paramref name="state"/>.</summary>
    const MCActorData& StateData(MCActorState state) const { return States[static_cast<size_t>(state)]; }

    /// <summary>
    /// The shape for <paramref name="state"/> (sub-state <paramref name="subState"/>, <see cref="NoSubState"/> for
    /// none) facing <paramref name="rotation"/> degrees, loading it if needed. <paramref name="frameRate"/> gets the
    /// state's frame rate, <paramref name="reverse"/> whether the shape is drawn mirrored.
    /// </summary>
    std::shared_ptr<MCShape> GetShape(MCActorState state, uint8_t subState, int32_t rotation, float& frameRate,
                                      bool& reverse);

    /// <summary>
    /// Port-only (HD particles, MCParticles::MeasureExplosions): every shape GetShape gives, each state's and sub-state's
    /// at each of its facings (a scaled type's full size ones), loading them if needed.
    /// </summary>
    std::vector<std::shared_ptr<MCShape>> DrawnShapes();

    /// <summary>The states (at least <see cref="ActorStateCount"/>; those past NumStates are empty).</summary>
    std::vector<MCActorData> States;
    /// <summary>The sub-states of the first state that has them (state 0 in practice); empty when none.</summary>
    std::vector<MCActorData> SubStates;
    /// <summary>The loaded shape of each packet of the type's PAK (expired once the cache drops it).</summary>
    std::vector<std::weak_ptr<MCShape>> ShapeList;
    /// <summary>FIT "NumStates".</summary>
    uint8_t NumStates = 0;
    /// <summary>FIT "Scaled": the packets alternate full size and zoomed out.</summary>
    bool Scaled = false;
    /// <summary>FIT "delta": the frames are delta-compressed, and the animation starts at frame 0.</summary>
    bool Delta = false;
};
