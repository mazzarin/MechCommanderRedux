#pragma once

#include "appear/MCAppearance.h"
#include "main/MCOriginalBugFixes.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCSpriteTree.h"

#include <bit>

class MCBattleMech;
class MCMovePath;

/// <summary>Port-only (HDStops): how a stride drawn on (<see cref="MCDrawnStride"/>) moves on.</summary>
enum class MCDrawnMode : uint8_t
{
    /// <summary>Held while the mech stands or sets off with its move held up: moved on by the ground it covers.</summary>
    Held,
    /// <summary>
    /// After the mech's gestures, through the tree's art between them, behind them and faster than they play until in
    /// step with them.
    /// </summary>
    Following,
    /// <summary>Settling where the mech stands: stepped the short way round to where its stop starts, then the stop.</summary>
    Settling,
};

/// <summary>
/// Port-only (HD art's arms): an arm as its mech last drew it, for MCDebris to throw off as it was: its shape (none until
/// drawn), frame and mirroring.
/// </summary>
struct MCDrawnArm
{
    std::shared_ptr<MCShape> Shape;
    int32_t Frame = 0;
    int32_t Reverse = 0;
};

/// <summary>
/// Port-only (HDStops): a stride drawn on while its mech stands with its move held up: each part's frame as last drawn,
/// moved on by the ground the mech covers, so that the mech stops mid-stride and picks its stride up where it stopped;
/// then drawn after the mech's own gestures until in step with them, or settled to standing in place.
/// </summary>
struct MCDrawnStride
{
    /// <summary>The gesture drawn: the walk or the run, or the tree's art between them and standing.</summary>
    int32_t Gesture = 0;
    /// <summary>Each part's frame, and how far into it.</summary>
    MCMechParts<float> Frame{};
    /// <summary>Whether the upper body was last in the gun pose (drawn as it plays; the stride's again once lowered).</summary>
    bool UpperInCombat = false;
    /// <summary>The seconds the mech has stood still, held.</summary>
    float StillSeconds = 0.0f;
    /// <summary>How it moved on at the last update.</summary>
    MCDrawnMode Mode = MCDrawnMode::Held;
    /// <summary>The mech's gesture and legs' frame at the last update.</summary>
    int32_t MechGesture = 0;
    int32_t MechFrame = 0;
    /// <summary>The legs' frame the mech last left its walk and run at since held (where the drawn leaves them).</summary>
    std::optional<int32_t> WalkLeftAt;
    std::optional<int32_t> RunLeftAt;
};

/// <summary>The value of the mech actor's fields the original left uninitialised (0 with the port's fix).</summary>
inline constexpr int32_t ActorUnset = FixUninitializedMechActor ? 0 : -1;
/// <summary>The same for float fields.</summary>
inline constexpr float ActorUnsetF = FixUninitializedMechActor ? 0.0f : std::bit_cast<float>(0xffffffffu);

/// <summary>
/// A BattleMech's appearance: legs, torso and arms drawn from the part PAKs through its <see cref="MCSpriteTree"/>,
/// animated by gestures (stand, walk, run, jump, fall...) with transitions between them, plus its shadow, jump
/// arc and damage marks.
/// </summary>
/// <remarks>Original source: <c>sprite\mactor.cpp</c>, <c>sprite\mactor.h</c>.</remarks>
class MCMechActor : public MCAppearance
{
public:
    MCMechActor() = default;
    /// <summary>Drops the actor's use of its tree.</summary>
    ~MCMechActor() override;

    /// <summary>Binds to the tree and resets every part.</summary>
    int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {}) override;

    /// <summary>Advances the parts' animations, the gesture transitions, jumps and falls.</summary>
    int32_t Update() override;

    /// <summary>Adds the shadow, the parts and the selection marks to the draw list.</summary>
    int32_t Render(int32_t depthFixup = 0) override;

    std::shared_ptr<MCAppearanceType> GetAppearanceType() override { return MechTree; }

    /// <summary>Draws the mech's damage bar.</summary>
    void DrawBars() override;

    int RecalcBounds(MCOptionalRef<MCCamera> view) override;

    /// <summary>Jumps to gesture <paramref name="gesture"/> (once until the next update).</summary>
    /// <returns>0, or 0xEADE0004 (negative) when a gesture was already set this update.</returns>
    int32_t SetGesture(uint32_t gesture) override;

    /// <summary>
    /// Sets the gesture to move to through the transition table (6, the jump, only after
    /// <see cref="SetJumpParameters"/>); fails while one is pending or in transition.
    /// </summary>
    int32_t SetGestureGoal(int32_t goal) override;

    /// <summary>The legs' frame.</summary>
    int32_t GetFrameNumber() override { return CurrentFrame[MCMechPart::Legs]; }

    /// <summary>Always 0.</summary>
    static int HitMech(int32_t hitLocation);

    /// <summary>The frames of gesture <paramref name="gesture"/>.</summary>
    float GetNumFramesInGesture(uint32_t gesture) const;

    /// <summary>The start velocity of gesture <paramref name="gesture"/> (-1 for a gesture the tree hasn't).</summary>
    float GetVelocityOfGesture(uint32_t gesture) const;

    /// <summary>The hot spot (weapon mount) table index for <paramref name="location"/>.</summary>
    static uint32_t GetHotSpotIndex(uint32_t location);

    /// <summary>Loads the tree's stand, walk and run shapes.</summary>
    void PreloadGestures() const;

    /// <summary>Port-only: opens the tree's HD art archives (MCSpriteTree::PrepareHdArt).</summary>
    void PrepareHdArt() const;

    /// <summary>Raises (<paramref name="combatMode"/>) or lowers the arms into the gun pose.</summary>
    void SetCombatMode(bool combatMode);

    /// <summary>Prepares a jump to <paramref name="jumpGoal"/>.</summary>
    /// <returns>0, or 0xEADA0007 (negative) while already jumping.</returns>
    int32_t SetJumpParameters(const MCVector3D& jumpGoal);

    /// <summary>The mech's ground speed from its gesture and frame.</summary>
    float GetVelocityMagnitude();

    /// <summary>Starts the stop countdown when <paramref name="path"/> has steps left, else clears it.</summary>
    int32_t SetMovePath(const MCMovePath& path);

    /// <summary>
    /// Whether the mech is walking or running with nowhere to go, and stands. Port-only (MechBraking): not while it
    /// brakes to a halt (<see cref="PlannedStop"/>).
    /// </summary>
    bool CheckStop() const;

    /// <summary>
    /// Port-only (MechBraking): fits the stride and stop to end where the mech's move does, <paramref name="metresLeft"/>
    /// on (none when the move doesn't end there, or has ended): the stride's frame rate (<see cref="StrideRate"/>) so
    /// that a stride's end starts the stop at the spot the stop then ends on, and once the stop is under way each of its
    /// gestures'. True when the stop should start now: the mover asks for the stand (<see cref="PlannedStop"/> set).
    /// Gives a stop up while the stride still plays out when the move goes on further.
    /// </summary>
    bool FitStop(std::optional<float> metresLeft);

    /// <summary>
    /// Port-only (MechBraking): gives up the stop planned (<see cref="PlannedStop"/>) while the stride is still being
    /// played out, back to walking or running on; true when it did.
    /// </summary>
    bool CancelPlannedStop();

    /// <summary>
    /// Port-only (HDStops): as the mech stands from a stride (<see cref="CheckStop"/>) with its move held up
    /// (MCBattleMech::MoveHeldUp), holds the stride as drawn (<see cref="DrawnStride"/>); one drawn on already goes on as
    /// it was (<see cref="StepDrawnStride"/> holds or settles it). A mech standing at its move's end is drawn standing, as
    /// the original.
    /// </summary>
    void HoldStride(const MCBattleMech& mech);

    /// <summary>
    /// Port-only (HDStops): moves the stride drawn on: held (<see cref="MCDrawnMode::Held"/>) by the ground the mech
    /// covers while it stands or sets off with its move held up; after its gestures (Following) once it goes on, through
    /// the tree's art between them, played faster or slower (<see cref="MaxStrideRate"/>, <see cref="MinStrideRate"/>)
    /// until in step (render hands back to them); settled to standing in place (Settling), briskly, once the mech has
    /// stood still <see cref="HoldMostSeconds"/> or stands with nowhere to go, if it's near where its art stops (else
    /// dropped to standing, as the original stops). Dropped on a gesture beyond walking, running and standing.
    /// </summary>
    void StepDrawnStride(const MCBattleMech& mech);

    /// <summary>
    /// Port-only (HDStops): plays the stride drawn on <paramref name="frames"/> of its gestures' frames toward gesture
    /// <paramref name="goal"/> (a stride, or standing), through the tree's art between them, leaving a stride where the
    /// mech left it; <paramref name="settling"/>, a start or a walk speeding up goes back the way it came.
    /// </summary>
    void PlayDrawnStride(float frames, int32_t goal, bool settling);

    /// <summary>
    /// Port-only (HDStops): whether the stride drawn on would settle to standing in place from where it is, its steps on
    /// the way no longer than <see cref="SettleMostSeconds"/> (else it drops to standing); false with none.
    /// </summary>
    bool DrawnSettlesNear() const;

    /// <summary>Port-only (HDStops): the gesture the legs are drawn in (the stride drawn on, else the playing one).</summary>
    int32_t DrawnGesture() const;

    /// <summary>Port-only (HDStops): the legs' frame as drawn.</summary>
    int32_t DrawnLegsFrame() const;

    /// <summary>Draws the jump's arc and landing point.</summary>
    void RenderJump();

    /// <summary>
    /// Port-only (HDTurns): notes how the legs (shape <paramref name="legs"/>, in gesture <paramref name="gesture"/> at
    /// frame <paramref name="frame"/>) are drawn at <paramref name="facing"/> (<see cref="DrawnLegs"/>), and with
    /// HDTurns, the turn middle to draw them as (MCSpriteTree::TurnMiddle) when the HD art pack has it, where it lies
    /// round their frame (its bounds and the frame's neighbour's together).
    /// </summary>
    std::optional<MCInBetween> LegsTurnMiddle(int32_t gesture, int32_t frame, float facing, const MCShape& legs);

    /// <summary>
    /// The jump's or fall's height per frame of the legs: the mech type's gesture outline of
    /// <see cref="HeightsGesture"/>; empty when there is none.
    /// </summary>
    std::span<const float> FrameHeights() const;

    /// <summary>The height of <paramref name="frame"/> in <see cref="FrameHeights"/>; frames past the table hold its
    /// last height (OB-175: every mech's jump gesture has one frame more than its outline). 0 without a table.</summary>
    float FrameHeight(int32_t frame) const;

    /// <summary>The tree.</summary>
    std::shared_ptr<MCSpriteTree> MechTree;
    /// <summary>Each part's shape (expired once the cache drops it).</summary>
    MCMechParts<std::weak_ptr<MCShape>> PartShape{};
    /// <summary>Each part's frame (-1: not started).</summary>
    MCMechParts<int32_t> CurrentFrame{{-1, -1, -1, -1}};
    /// <summary>The order the parts are drawn in (update sets it: legs, then the arm nearer the back first).</summary>
    std::array<MCMechPart, MechPartCount> PartOrder{};
    /// <summary>Each part's frame rate (15 after init).</summary>
    MCMechParts<float> FrameRate{{15.0f, 15.0f, 15.0f, 15.0f}};
    /// <summary>Each part's time into its gesture.</summary>
    MCMechParts<float> CurrentTime{};
    /// <summary>Each part's frames played so far.</summary>
    MCMechParts<int32_t> LastFrame{};
    /// <summary>Each part's mirroring (set by the tree's GetGesture/SetGesture); read by BattleMech::update.</summary>
    MCMechParts<int32_t> Reverse{{ActorUnset, ActorUnset, ActorUnset, ActorUnset}};
    /// <summary>
    /// Nonzero while the gesture is a walk or run cycle (4, 7, 9 or 11), whose mirrored parts render keeps half a
    /// cycle off the part they follow.
    /// </summary>
    int32_t StrideGesture = ActorUnset;
    /// <summary>The gesture playing.</summary>
    int32_t CurrentGesture = 0;
    /// <summary>The gesture to move to (-1: none).</summary>
    int32_t GestureGoal = -1;
    /// <summary>The playing gesture's state gesture, used by the transitions.</summary>
    int32_t CurrentStateGesture = 0;
    /// <summary>The transition's step: the column of the transition table row that gave the playing gesture.</summary>
    int32_t TransitionStep = ActorUnset;
    /// <summary>The transition's next gesture (-1: the playing one is its last, or no transition).</summary>
    int32_t NextGesture = -1;
    /// <summary>
    /// The speed GetVelocityMagnitude gives for the jump and gestures 12 and 13: <see cref="JumpSpeed"/> while
    /// airborne, else 0.
    /// </summary>
    float JumpVelocity = 0.0f;
    /// <summary>The gesture whose outline gives <see cref="FrameHeights"/> (the jump or the fall), -1 for none.</summary>
    int32_t HeightsGesture = -1;
    /// <summary>Whether the mech is jumping (read by BattleMech::setControlSettings).</summary>
    bool InJump = false;
    /// <summary>Set to 4 by <see cref="SetJumpParameters"/>.</summary>
    float JumpParameter = 0.0f;
    /// <summary>Where the jump lands.</summary>
    MCVector3D JumpGoal;
    /// <summary>The unit vector from where the jump started towards <see cref="JumpGoal"/>.</summary>
    MCVector3D JumpDirection{ActorUnsetF, ActorUnsetF, ActorUnsetF};
    /// <summary>Set once <see cref="SetJumpParameters"/> has set a jump up.</summary>
    bool JumpSetup = false;
    /// <summary>The jump's ground speed (set when the jump gesture starts; JumpVelocity takes it once airborne).</summary>
    float JumpSpeed = 0.0f;
    /// <summary>Set from <see cref="LiftOffFrame"/> to <see cref="TouchDownFrame"/> of the jump.</summary>
    bool Airborne = false;
    /// <summary>The jump's first climbing frame after the crouch.</summary>
    int32_t LiftOffFrame = ActorUnset;
    /// <summary>The jump's first frame going down again after the climb.</summary>
    int32_t TouchDownFrame = ActorUnset;
    /// <summary>Set when the right arm is blown off: it is no longer drawn or animated.</summary>
    bool RightArmGone = false;
    /// <summary>Set when the left arm is blown off: it is no longer drawn or animated.</summary>
    bool LeftArmGone = false;
    /// <summary>Port-only (HD art's arms): the right and left arms as last drawn.</summary>
    MCDrawnArm DrawnRightArm;
    MCDrawnArm DrawnLeftArm;
    /// <summary>Whether a transition plays (SetGestureGoal fails).</summary>
    bool InTransition = false;
    /// <summary>
    /// Set when the playing gesture has played out (or reached the special frame its transition leaves at): the next
    /// update moves on to <see cref="NextGesture"/>.
    /// </summary>
    bool GestureDone = false;
    /// <summary>Whether the gesture plays from its last frame to its first.</summary>
    bool PlayBackwards = false;
    /// <summary>Set by <see cref="SetGesture"/> until the next update.</summary>
    bool GestureSet = false;
    /// <summary>Whether a gesture goal is pending.</summary>
    bool GoalPending = false;
    /// <summary>
    /// The paint scheme: which fade table render draws the 'Mech through (-1 = none). Set from the part's or the
    /// pilot's <c>PaintScheme</c> by <c>Scenario::createPartObject</c>.
    /// </summary>
    int32_t FadeTableIndex = -1;
    /// <summary>Set while the mech lies on the ground holding the last frame of gesture 0x17 or 0x18.</summary>
    bool LyingStill = false;
    /// <summary>
    /// The fall (or a fall-down gesture of a mech with a reverse flag) still has to turn the mech round: done by the
    /// fall gesture or gesture 22, depending on <c>StupidJamieReverseFlag</c>.
    /// </summary>
    bool FallTurnPending = false;
    /// <summary>The mech has to turn round when it next reaches a standing gesture (0 to 2).</summary>
    bool StandTurnPending = false;
    /// <summary>Set when a gesture leads into a fall-down one that starts at <see cref="FallStartFrame"/>.</summary>
    int32_t FallStartPending = ActorUnset;
    /// <summary>The frame the next fall-down gesture starts at (valid while FallStartPending is set).</summary>
    int32_t FallStartFrame = ActorUnset;
    /// <summary>Set by the falling and getting-up gestures: the body can't turn (MechDynamics).</summary>
    bool BodyTurnLocked = false;
    /// <summary>Set with <see cref="BodyTurnLocked"/>: the torso and arms can't turn either.</summary>
    bool UpperBodyLocked = false;
    /// <summary>The shape's top-left offset from its hotspot (-25 before a shape).</summary>
    float ShapeMinX = -25.0f;
    float ShapeMinY = -25.0f;
    /// <summary>The shape's size (25 before a shape).</summary>
    float ShapeMaxX = 25.0f;
    float ShapeMaxY = 25.0f;
    /// <summary>
    /// Set by BattleMech::update when the dead mech blows up: the torso is no longer drawn and the mech stays in a
    /// lying gesture (0x17 or 0x18).
    /// </summary>
    bool Wrecked = false;
    /// <summary>The stop countdown (5 when a path has steps left; 0 stops).</summary>
    float StopCountdown = 0.0f;
    /// <summary>Whether the arms are in the gun pose.</summary>
    bool InCombatMode = false;
    /// <summary>The arms are going up into the gun pose.</summary>
    bool CombatModeRaising = false;
    /// <summary>The arms are coming down from the gun pose.</summary>
    bool CombatModeLowering = false;
    /// <summary>Set when the gun pose starts changing: the next update starts the upper body's change gesture.</summary>
    int32_t CombatModeChanged = ActorUnset;
    /// <summary>
    /// Port-only (HDTurns): the facing the mech is drawn at, following its own (MCActorFacing) at
    /// <see cref="TurnEaseRate"/>, never more than <see cref="TurnLagMost"/> behind; its own while HDTurns is off. Set
    /// by each update.
    /// </summary>
    std::optional<float> TurnedFacing;
    /// <summary>
    /// Port-only (HDFootprints): how the legs were last drawn, in gesture <see cref="DrawnLegsGesture"/>: the way their
    /// facing (or turn middle) points and whether mirrored (MCSpriteTree::DrawnFacing). Set by each render.
    /// </summary>
    std::optional<MCDrawnFacing> DrawnLegs;
    int32_t DrawnLegsGesture = -1;
    /// <summary>
    /// Port-only (MechBraking): set by the mover (MCBattleMech::PlanStop) while the mech brakes to a halt where its move
    /// ends: the transition to a stand plays out the stride, then the stopping art (MCStopTransition), and the move
    /// ending doesn't stop it dead (<see cref="CheckStop"/>). Cleared once it stands.
    /// </summary>
    bool PlannedStop = false;
    /// <summary>
    /// Port-only (MechBraking): the scale on the legs' frame rate the mover fits the stride and stop with (1 as the
    /// original).
    /// </summary>
    float StrideRate = 1.0f;
    /// <summary>
    /// Port-only (HDStops): the stride drawn on while the mech stands with its move held up, and until its stride is in
    /// step with it again (<see cref="StepDrawnStride"/>); none while the mech is drawn as it plays.
    /// </summary>
    std::optional<MCDrawnStride> DrawnStride;
    /// <summary>Port-only (HDStops): where the mech was at the last update.</summary>
    std::optional<MCVector3D> LastPosition;
};

/// <summary>
/// Port-only: PREFS "HDStops": 1 has a mech that stops dead mid-stride with its move held up (yielding to a unit in its
/// way, or waiting on its next path) hold its stride as drawn, and walk on from it, its legs following the ground it
/// covers, rather than drop to standing and start a stride afresh; 0 as the original. Off by default.
/// </summary>
extern int GHdStops;

/// <summary>
/// Port-only (HDStops): the longest a mech stands still with its stride held, seconds (a mech yields to a unit in its way
/// for its CrashYieldTime, 2): then it settles to standing in place.
/// </summary>
inline constexpr float HoldMostSeconds = 3.0f;

/// <summary>
/// Port-only (HDStops): the longest a stride settling in place steps on (or back) to where it gives way to stopping,
/// seconds (it steps faster than it plays, MaxStrideRate); from further it would walk on the spot, so it drops to standing
/// as the original does.
/// </summary>
inline constexpr float SettleMostSeconds = 0.4f;

/// <summary>
/// Port-only: PREFS "HDTurns": 1 eases a mech's turns as drawn (<see cref="TurnEaseRate"/>; it turns as the original
/// does, and its torso and arms are drawn turned from the eased facing as from its own) and draws its legs halfway
/// between their stored facings where the HD art pack has those middles (MCSpriteTree::TurnMiddle); 0 as the original.
/// Off by default.
/// </summary>
extern int GHdTurns;

/// <summary>
/// Port-only (HDTurns): how fast a mech's drawn facing follows its facing, degrees a second (its pivot rate's floor; it
/// turns walking at 720 or more, a corner in a frame or two).
/// </summary>
inline constexpr float TurnEaseRate = 180.0f;

/// <summary>Port-only (HDTurns): how far a mech's drawn facing may lag its facing, in degrees.</summary>
inline constexpr float TurnLagMost = 90.0f;

/// <summary>
/// Port-only (HDTurns): <paramref name="shown"/> turned the short way toward <paramref name="facing"/> by at most
/// <paramref name="maxTurn"/> degrees, then brought within <paramref name="lagMost"/> of it; -180 to 180.
/// </summary>
float MCEaseFacing(float shown, float facing, float maxTurn, float lagMost);

/// <summary>The frames of a jump in the air.</summary>
struct MCJumpFrames
{
    /// <summary>The first frame climbing after the crouch (0 when there is none).</summary>
    int32_t LiftOff = 0;
    /// <summary>The first frame going down again after the climb (0 when there is none).</summary>
    int32_t TouchDown = 0;
};

/// <summary>Where a jump leaves the ground and comes down, from the height of each of its frames.</summary>
MCJumpFrames MCFindJumpFrames(std::span<const float> heights);

/// <summary>The index of <paramref name="rotation"/> degrees among <paramref name="numRotations"/> + 1 facings (at most 31).</summary>
int32_t CalcRotation(float rotation, int32_t numRotations);

/// <summary>Debug single-stepping of mech animation (the scenario's "SingleStep").</summary>
extern int SingleStepMode;
