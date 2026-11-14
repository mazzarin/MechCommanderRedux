#pragma once

// Port-only: mechs coming to a halt through the stopping art the original drew for every mech: gesture 5 (walk to
// stand), braking from walking speed to a standstill on the stride's last foot, and gesture 8 (run to walk). The
// original played them only to turn a walk round into a backward walk, and three of its trees (madcat, masakari,
// vulture) to halt; a mech whose move ended dropped its legs from mid-stride to standing in a frame
// (MCMechActor::CheckStop). With MechBraking a mech brakes there instead, its last strides fitted so that the stop ends
// on the spot.

#include <cstdint>
#include <optional>
#include <vector>

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

/// <summary>
/// Port-only: the version of braking in force, 0 for none. To the player it is BRAKING whatever its version; a version
/// is how a match whose builds brake differently plays the same. Version 2 keeps a group move's mechs to where the
/// original halts them: a stop fitted to where crash avoidance ends the move short of a unit on its end, the cell a mech
/// will come to rest in claimed so that a mech headed there too ends short of it, a run's stop playing walk strides
/// where its run's strides can't reach, a running mech walking behind a slower unit on its path, a planned stop whose
/// move ends early halting where the original halts it, and outside a match a STOP braked through the stopping art.
/// The latest outside a match; in one, the latest every player's build plays (MCOptionVersions::Braking, by the
/// match's rules, MCMultiPlayer::MatchRules).
/// </summary>
[[nodiscard]] int32_t MCMechBrakingVersion();

/// <summary>Port-only: braking's version 2 (<see cref="MCMechBrakingVersion"/>).</summary>
inline constexpr int32_t BrakingVersion2 = 2;

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
/// <summary>
/// Port-only (MechBraking): the strides, beyond its stop's metres, within which a striding mech claims the cell
/// its move ends at (MCBattleMech::ClaimedCell): early enough that a mech behind it, headed there too, still fits its stop
/// to the cell before.
/// </summary>
inline constexpr float ClaimStrides = 2.0f;
/// <summary>
/// Port-only (MechBraking): the least metres, beyond its stop's, within which a striding mech claims its move's
/// end (a walk's two strides are too few: a mech behind, a cell short, can't fit its stop).
/// </summary>
inline constexpr float ClaimLeastMetres = 60.0f;
/// <summary>
/// Port-only (MechBraking): the most walk strides a run's stop plays beyond its one (run to walk, walk strides,
/// walk to stand), so that it reaches distances its run's strides alone can't: a run's stop starts only at one frame of
/// its stride, the stride paced <see cref="MinStrideRate"/> to <see cref="MaxStrideRate"/>, so some fall between.
/// </summary>
inline constexpr int32_t StopWalksMost = 3;
/// <summary>
/// Port-only (MechBraking): the strides, beyond a run's stop, a running mech keeps behind a slower unit on its
/// path (MCBattleMech::PlanStop: it walks within them, and runs again a stride further off).
/// </summary>
inline constexpr float FollowStrides = 1.0f;
/// <summary>
/// Port-only (MechBraking): the share of its running speed below which a unit ahead counts as slower, so a
/// running mech walks behind it (one running too keeps its distance by itself).
/// </summary>
inline constexpr float FollowSlower = 0.9f;
/// <summary>
/// Port-only (MechBraking): the strides, beyond a run's stop, its move's end must be off for a mech not yet
/// running to start a run (nearer, it couldn't stop from it there).
/// </summary>
inline constexpr float RunStartStrides = 1.5f;
/// <summary>
/// Port-only (MechBraking): the least share of the stride's speed the stopping art has where a STOP's stop
/// (MCMechActor::QuickStop) goes into it (further in, it halts in a frame or two: no pose jumps, but no braking either).
/// </summary>
inline constexpr float QuickEntryShare = 0.5f;
/// <summary>
/// Port-only (MechBraking): the metres past its next position a STOP's stop keeps clear of cells other units
/// hold.
/// </summary>
inline constexpr float QuickStopClearance = 2.5f;

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
/// Port-only (MechBraking): how alike each frame of one gesture is to each of another's, as drawn: the pixels the legs
/// and torso draw in both and in either, over the legs' sixteen facings, frame by frame (first's frame times second's
/// frames plus second's). Likeness is shared over either.
/// </summary>
struct MCLikeness
{
    uint32_t FirstFrames = 0;
    uint32_t SecondFrames = 0;
    std::vector<int64_t> Shared;
    std::vector<int64_t> Either;
};

/// <summary>Port-only (MechBraking): <see cref="MCLikeness"/> of <paramref name="first"/>'s frames to
/// <paramref name="second"/>'s in <paramref name="tree"/>; none without both drawn.</summary>
[[nodiscard]] std::optional<MCLikeness> MCGestureLikeness(MCSpriteTree& tree, int32_t first, int32_t second);

/// <summary>
/// Port-only (MechBraking): for each frame of stride <paramref name="stride"/> (walk or run), the walk to
/// stand's frame it goes into as drawn alike (MCGestureLikeness) as the art's own way in (the walk's frame it gives way
/// at into the stop's first), or -1 where none is: the frames a stop goes in at without a jump. Worked out once a tree.
/// </summary>
[[nodiscard]] const std::vector<int32_t>& MCStopEntries(MCSpriteTree& tree, int32_t stride);

/// <summary>
/// Port-only (MechBraking): the run's frame a run to walk starts after (FIT RunToRWFrame). The expansion's mechs leave
/// it 0, which would start the run to walk from any frame of the run; braking, their run plays out first. None: the
/// run plays out.
/// </summary>
[[nodiscard]] std::optional<uint32_t> MCRunToWalkFrame(const MCSpriteTree& tree, bool braking);
