#pragma once

// Port-only: mechs coming to a halt through the stopping art the original drew for every mech: gesture 5 (walk to
// stand), braking from walking speed to a standstill on the stride's last foot, and gesture 8 (run to walk). The
// original played them only to turn a walk round into a backward walk, and three of its trees (madcat, masakari,
// vulture) to halt; a mech whose move ended dropped its legs from mid-stride to standing in a frame
// (MCMechActor::CheckStop). With MechBraking a mech brakes there instead, its last strides fitted so that the stop ends
// on the spot.

#include <cstdint>
#include <optional>

class MCSpriteTree;

/// <summary>
/// Port-only: PREFS "MechBraking" (gameplay): 1 has a mech whose move ends brake to a halt through its stopping art,
/// its last strides fitted (<see cref="MCPlanStride"/>) to end the stop where the original stops it; 0 as the original,
/// which stops it dead. Off by default. In multiplayer the match's rules apply (<see cref="MCMechBrakingInForce"/>).
/// </summary>
extern int GMechBraking;

/// <summary>
/// Port-only: whether mechs brake in this mission: the player's MechBraking, or in multiplayer the match's rules (the
/// server's MechBraking when every player's build brakes too; MCMultiPlayer::MatchRules).
/// </summary>
[[nodiscard]] bool MCMechBrakingInForce();

/// <summary>Port-only (MechBraking, HDStops): the tree's gestures standing, walking, running and between them.</summary>
namespace MCStopGesture
{
    inline constexpr int32_t Stand = 2;
    inline constexpr int32_t StandToWalk = 3;
    inline constexpr int32_t Walk = 4;
    inline constexpr int32_t WalkToStand = 5;
    inline constexpr int32_t WalkToRun = 6;
    inline constexpr int32_t Run = 7;
    inline constexpr int32_t RunToWalk = 8;
} // namespace MCStopGesture

/// <summary>Port-only (MechBraking): the actor's state gestures (MCMechActor::CurrentStateGesture) a stop leaves.</summary>
namespace MCStopState
{
    inline constexpr int32_t Stand = 1;
    inline constexpr int32_t Walk = 2;
    inline constexpr int32_t Run = 3;
} // namespace MCStopState

/// <summary>Port-only (MechBraking): the scales on a stride's frame rate a fitted stride or stop keeps within.</summary>
inline constexpr float MinStrideRate = 0.8f;
inline constexpr float MaxStrideRate = 1.25f;

/// <summary>
/// Port-only (MechBraking): how much sooner than the original a braking mech counts as at the end of its move, metres
/// (the original's, the last step's margin of error, 4 m from its end): its stop ends there, within this.
/// </summary>
inline constexpr float BrakeArrivalSlack = 1.5f;

/// <summary>Port-only (MechBraking): a stride fitted to start a stop where the stop then ends on the spot.</summary>
struct MCStridePlan
{
    /// <summary>The scale on the stride's frame rate (its speed stays its gesture's: the feet slide a little).</summary>
    float Rate = 1.0f;
    /// <summary>The whole strides before the one whose end starts the stop.</summary>
    int32_t Strides = 0;
};

/// <summary>
/// Port-only (MechBraking): the fitted stride that plays <paramref name="framesLeft"/> frames (to where the stop can
/// start), then any whole strides of <paramref name="strideFrames"/> frames, over <paramref name="metres"/>, a frame
/// covering <paramref name="metresPerFrame"/> at its own rate: its rate scale the nearest 1 within
/// <see cref="MinStrideRate"/> and <see cref="MaxStrideRate"/>. None when no number of strides fits.
/// </summary>
[[nodiscard]] std::optional<MCStridePlan> MCPlanStride(float framesLeft, int32_t strideFrames, float metresPerFrame,
                                                       float metres);

/// <summary>Port-only (MechBraking): the speeds a gesture eases between over its frames, metres a second.</summary>
struct MCGestureVelocities
{
    float Start = 0.0f;
    float End = 0.0f;
};

/// <summary>
/// Port-only: the speeds gesture <paramref name="gesture"/> of <paramref name="tree"/> eases between
/// (MCMechActor::GetVelocityMagnitude): its data's. OB-186 (fixed with <paramref name="braking"/>): the expansion's
/// mechs' stopping gestures keep their start speed to the end (a walk to stand that never slows, a run to walk at
/// running speed throughout); braking, a walk to stand ends at a standstill and a run to walk at walking speed.
/// </summary>
[[nodiscard]] MCGestureVelocities MCStopVelocities(const MCSpriteTree& tree, int32_t gesture, bool braking);

/// <summary>
/// Port-only: the speed at frame <paramref name="frame"/> of a gesture of <paramref name="numFrames"/> frames easing
/// between <paramref name="velocities"/> (MCMechActor::GetVelocityMagnitude's ease, which this is); 0 for a speed of
/// none (below -1999).
/// </summary>
[[nodiscard]] float MCFrameSpeed(MCGestureVelocities velocities, uint32_t numFrames, int32_t frame);

/// <summary>
/// Port-only (MechBraking): the metres gesture <paramref name="gesture"/> of <paramref name="tree"/> covers from
/// <paramref name="fromFrame"/> (its frame and how far into it) to its end at its own rate, its speed eased by frame as
/// MCMechActor::GetVelocityMagnitude eases it.
/// </summary>
[[nodiscard]] float MCGestureMetres(const MCSpriteTree& tree, int32_t gesture, float fromFrame, bool braking);

/// <summary>
/// Port-only (MechBraking): the gesture at <paramref name="step"/> of the transition from state
/// <paramref name="state"/> to goal <paramref name="goal"/> while a mech brakes to a halt (<paramref name="braking"/>):
/// a walk to a stand plays out its stride, then the stop (5, 2); a run, the run to walk, a stride, then the stop (8, 4,
/// 5, 2); a run to a walk, the run to walk (8, 4); as madcat's tree has them, for every tree with that art. Without a
/// run to walk drawn, the run goes into the walk by the one made (4, 5, 2; 4: <see cref="MCRunToWalkMade"/>). Else the
/// tree's own (MCSpriteTree::Transition).
/// </summary>
[[nodiscard]] int32_t MCStopTransition(MCSpriteTree& tree, int32_t goal, int32_t state, int32_t step, bool braking);

/// <summary>
/// Port-only (MechBraking): whether <paramref name="tree"/> has gesture <paramref name="gesture"/> drawn: every part's
/// shape has its frames, and draws (the expansion's Wendigo, Turkina, BushWhacker and Mauler list a run to walk with
/// no shapes; ShadowCat and FireFalcon one whose shapes are blank). Looked at once a tree.
/// </summary>
[[nodiscard]] bool MCHasGestureArt(MCSpriteTree& tree, int32_t gesture);

/// <summary>
/// Port-only (MechBraking): a run to walk made from a tree's own run and walk, for a tree with none drawn: the run plays
/// on to <see cref="RunFrame"/>, then the walk goes on from <see cref="WalkFrame"/>, the frame after the walk's frame
/// most like the run's there, so the cut goes on as the walk does.
/// </summary>
struct MCMadeRunToWalk
{
    /// <summary>The run's last frame shown.</summary>
    uint32_t RunFrame = 0;
    /// <summary>The walk's first frame shown.</summary>
    uint32_t WalkFrame = 0;

    bool operator==(const MCMadeRunToWalk&) const = default;
};

/// <summary>
/// Port-only (MechBraking): the run to walk made for <paramref name="tree"/> (<see cref="MCMakeRunToWalk"/>, worked
/// out once a tree, as its gestures preload); none when it has its own drawn, or without <paramref name="braking"/>.
/// </summary>
[[nodiscard]] std::optional<MCMadeRunToWalk> MCRunToWalkMade(MCSpriteTree& tree, bool braking);

/// <summary>
/// Port-only (MechBraking): the run frame and walk frame of <paramref name="tree"/> most alike, the legs' and torso's
/// pixels at every facing taken together (the pixels both draw over those either draws), as its run to walk's cut;
/// none without a run and a walk drawn.
/// </summary>
[[nodiscard]] std::optional<MCMadeRunToWalk> MCMakeRunToWalk(MCSpriteTree& tree);

/// <summary>
/// Port-only (MechBraking): the run's frame a run to walk starts after (FIT RunToRWFrame). The expansion's mechs leave
/// it 0, which would start the run to walk from any frame of the run; braking, their run plays out first. None: the
/// run plays out.
/// </summary>
[[nodiscard]] std::optional<uint32_t> MCRunToWalkFrame(const MCSpriteTree& tree, bool braking);
