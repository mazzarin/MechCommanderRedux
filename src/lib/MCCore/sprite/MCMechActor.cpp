#include "stdafx.h"
#include "sprite/MCMechActor.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCEllipseElement.h"
#include "engine/MCLineElement.h"
#include "engine/MCPolygonElement.h"
#include "engine/MCVfxElement.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sound/MCSoundSystem.h"
#include "platform/MCRenderer.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCWeather.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

int SingleStepMode = 1;

namespace
{
    /// <summary>Per gesture, the gesture outline (jump and fall heights) and hot spot table it uses.</summary>
    constexpr std::array<uint8_t, 28> HotSpotFinderArray = {0,  0,  0,  1,  2,  3,  4,  5,  6,  2,  3,  7, 8,  9,
                                                            10, 11, 12, 13, 14, 15, 16, 17, 18, 11, 10, 0, 19, 19};

    /// <summary>Per gesture, the state gesture it counts as for the transitions (-1: none).</summary>
    constexpr std::array<int32_t, 10> EquivalentGestureArray = {0, -1, 1, -1, 2, -1, -1, 3, -1, 0};

    /// <summary>The gestures (the tree's gesture numbers).</summary>
    constexpr int32_t GestureStand = 2;
    constexpr int32_t GestureJump = 20;
    constexpr int32_t GestureFall = 21;
    constexpr int32_t GestureCombat = 0x1a;
    constexpr int32_t GestureCombatChange = 0x1b;

    /// <summary>The number of gestures in a state's row of the transition table.</summary>
    constexpr int32_t TransitionRow = 10;

    /// <summary>Half a turn: MCX.EXE's constant for pi (a little short of it).</summary>
    constexpr double TurnAngle = 3.1415926535820002;

    /// <summary>Turns <paramref name="mech"/>'s frame half a turn about its up axis (the reversed gestures end
    /// facing the other way) and clears its torso and arm rotations.</summary>
    auto TurnAround(MCBattleMech& mech) -> void
    {
        MCFrameOfRef frame = mech.GetFrame();
        const float s = static_cast<float>(std::sin(TurnAngle));
        const float c = static_cast<float>(std::cos(TurnAngle));
        const MCVector3D oldI = frame.I;
        frame.I.X = frame.J.X * s + oldI.X * c;
        frame.I.Y = frame.J.Y * s + oldI.Y * c;
        frame.I.Z = frame.J.Z * s + oldI.Z * c;
        frame.J.X = frame.J.X * c - oldI.X * s;
        frame.J.Y = frame.J.Y * c - oldI.Y * s;
        frame.J.Z = frame.J.Z * c - oldI.Z * s;
        mech.SetFrame(frame);
    }

    /// <summary>Clears the mech's torso and arm rotations.</summary>
    auto ClearRotations(MCBattleMech& mech) -> void
    {
        mech.LeftArmRotation = 0.0f;
        mech.RightArmRotation = 0.0f;
        mech.TorsoRotation = 0.0f;
    }

    /// <summary>The outline table of <paramref name="mech"/> for gesture <paramref name="gesture"/> (the jump and
    /// fall heights), or empty.</summary>
    auto GestureHeights(MCGameObject& mech, int32_t gesture) -> std::span<const float>
    {
        auto& type = static_cast<MCBattleMechType&>(*mech.GetObjectType());

        if (type.GestureOutlines.empty())
        {
            return {};
        }

        return type.GestureOutline(static_cast<uint32_t>(HotSpotFinderArray[gesture]));
    }

    /// <summary>
    /// Render's stride rule, for part <paramref name="part"/> of <paramref name="frames"/> in stride gesture
    /// <paramref name="gesture"/> (played <paramref name="backwards"/>): a mirrored part runs half a cycle off the part it
    /// follows, so the stride alternates; an unmirrored one keeps in step with it.
    /// </summary>
    auto FollowStride(const MCSpriteTree& tree, MCMechParts<int32_t>& frames, MCMechPart part, int32_t gesture,
                      bool mirrored, bool backwards) -> void
    {
        const MCGestureData& data = tree.Gestures[gesture];

        if (data.ArmSymmetrical != 0 && data.Symmetrical != 0)
        {
            return;
        }

        const int32_t reference = frames[data.ArmSymmetrical == 0 ? MCMechPart::LeftArm : MCMechPart::Torso];

        if (!mirrored)
        {
            frames[part] = reference;
            return;
        }

        if (frames[part] != reference)
        {
            return;
        }

        const int32_t frame = frames[part];
        const uint32_t numFrames = data.NumFrames;

        if (!backwards)
        {
            const uint32_t shifted = (numFrames >> 1) + static_cast<uint32_t>(frame);
            frames[part] = static_cast<int32_t>(numFrames - 1 < shifted ? shifted % numFrames : shifted);
            return;
        }

        int32_t shifted = frame - static_cast<int32_t>(numFrames >> 1);
        frames[part] = shifted;

        if (shifted < 0)
        {
            shifted = -shifted;
            const uint32_t wrapped = static_cast<uint32_t>(shifted) % numFrames;
            frames[part] = static_cast<int32_t>(numFrames - wrapped);
        }
    }

    /// <summary>Port-only (HDStops): the most gestures a stride drawn on goes through in an update.</summary>
    constexpr int32_t MostDrawnPasses = 4;

    /// <summary>
    /// Port-only (HDStops): the stride, or standing, gesture <paramref name="gesture"/> heads for (itself for a stride or
    /// standing); none for a gesture beyond walking, running and standing.
    /// </summary>
    auto HeadsFor(int32_t gesture) -> std::optional<int32_t>
    {
        switch (gesture)
        {
            case MCStopGesture::Stand:
            case MCStopGesture::WalkToStand:
                return MCStopGesture::Stand;
            case MCStopGesture::StandToWalk:
            case MCStopGesture::Walk:
            case MCStopGesture::RunToWalk:
                return MCStopGesture::Walk;
            case MCStopGesture::WalkToRun:
            case MCStopGesture::Run:
                return MCStopGesture::Run;
            default:
                return std::nullopt;
        }
    }

    /// <summary>
    /// Port-only (HDStops): the stride or standing gesture of state <paramref name="state"/> (MCMechActor's state
    /// gestures: 1 standing, 2 walking, 3 running); none for another.
    /// </summary>
    auto StateGesture(int32_t state) -> std::optional<int32_t>
    {
        switch (state)
        {
            case MCStopState::Stand:
                return MCStopGesture::Stand;
            case MCStopState::Walk:
                return MCStopGesture::Walk;
            case MCStopState::Run:
                return MCStopGesture::Run;
            default:
                return std::nullopt;
        }
    }

    /// <summary>Port-only (HDStops): a step of a stride drawn on to the next gesture.</summary>
    struct MCDrawnStep
    {
        /// <summary>The next gesture.</summary>
        int32_t Next = 0;
        /// <summary>The legs' frame the gesture gives way at (its frame count: its end).</summary>
        int32_t At = 0;
        /// <summary>The frame the next starts at.</summary>
        int32_t Start = 0;
    };

    /// <summary>
    /// Port-only (HDStops): the next gesture a stride drawn on in gesture <paramref name="from"/> plays toward
    /// <paramref name="goal"/> (a stride, or standing) through <paramref name="tree"/>'s art, as update moves the mech's
    /// own on (with the stopping art, MCStopTransition); none once there, or from a gesture beyond walking, running and
    /// standing.
    /// </summary>
    auto NextDrawnStep(MCSpriteTree& tree, int32_t from, int32_t goal) -> std::optional<MCDrawnStep>
    {
        if (from == goal)
        {
            return std::nullopt;
        }

        const MCMechSpecialInfo& info = tree.SpecialInfo;
        const auto end = [&](int32_t gesture) { return static_cast<int32_t>(tree.Gestures[gesture].NumFrames); };
        const auto special = [&](uint32_t frame, int32_t gesture)
        { return frame != NoFrame ? static_cast<int32_t>(frame) : end(gesture); };

        switch (from)
        {
            case MCStopGesture::Stand:
                return MCDrawnStep{MCStopGesture::StandToWalk, 0, 0};
            case MCStopGesture::StandToWalk:
                return MCDrawnStep{MCStopGesture::Walk, end(MCStopGesture::StandToWalk),
                                   info.SWToWalkFrame != NoFrame ? static_cast<int32_t>(info.SWToWalkFrame) : 0};
            case MCStopGesture::Walk:
            {
                if (goal == MCStopGesture::Run)
                {
                    return MCHasGestureArt(tree, MCStopGesture::WalkToRun)
                               ? MCDrawnStep{MCStopGesture::WalkToRun, static_cast<int32_t>(info.WalkToWRFrame), 0}
                               : MCDrawnStep{MCStopGesture::Run, end(MCStopGesture::Walk), 0};
                }

                return MCHasGestureArt(tree, MCStopGesture::WalkToStand)
                           ? MCDrawnStep{MCStopGesture::WalkToStand, special(info.WalkToWSFrame, MCStopGesture::Walk),
                                         0}
                           : MCDrawnStep{MCStopGesture::Stand, end(MCStopGesture::Walk), 0};
            }
            case MCStopGesture::Run:
            {
                const std::optional<uint32_t> runToWalk = MCRunToWalkFrame(tree, MCMechBrakingInForce());
                const int32_t at = runToWalk.has_value() ? static_cast<int32_t>(*runToWalk) : end(MCStopGesture::Run);
                return MCHasGestureArt(tree, MCStopGesture::RunToWalk)
                           ? MCDrawnStep{MCStopGesture::RunToWalk, at, 0}
                           : MCDrawnStep{MCStopGesture::Walk, end(MCStopGesture::Run), 0};
            }
            case MCStopGesture::WalkToRun:
                return MCDrawnStep{MCStopGesture::Run, end(MCStopGesture::WalkToRun),
                                   info.SpecialDuaneFlag != 0 ? end(MCStopGesture::Run) / 2 : 0};
            case MCStopGesture::RunToWalk:
                return MCDrawnStep{MCStopGesture::Walk, end(MCStopGesture::RunToWalk), 0};
            case MCStopGesture::WalkToStand:
                return MCDrawnStep{MCStopGesture::Stand, end(MCStopGesture::WalkToStand), 0};
            default:
                return std::nullopt;
        }
    }

    /// <summary>Port-only (HDStops): a move shorter than this, metres, leaves a mech standing still.</summary>
    constexpr float StillMetres = 0.001f;
    /// <summary>Port-only (HDStops): the frames within which a stride drawn on is put on the stride played.</summary>
    constexpr float InStepFrames = 0.5f;

    /// <summary>Port-only (HDStops): whether gesture <paramref name="gesture"/> is walking or running.</summary>
    auto WalkOrRun(int32_t gesture) -> bool
    {
        return gesture == MCStopGesture::Walk || gesture == MCStopGesture::Run;
    }

    /// <summary>
    /// Port-only (HDStops): the legs' frame stride drawn <paramref name="drawn"/> gives way to <paramref name="next"/>
    /// at from gesture <paramref name="gesture"/>: for a walk or run the mech left, where it left it, else where the art
    /// leaves it.
    /// </summary>
    auto LeaveFrame(const MCDrawnStride& drawn, int32_t gesture, const MCDrawnStep& next) -> int32_t
    {
        if (gesture == MCStopGesture::Walk)
        {
            return drawn.WalkLeftAt.value_or(next.At);
        }

        return gesture == MCStopGesture::Run ? drawn.RunLeftAt.value_or(next.At) : next.At;
    }

    /// <summary>
    /// Port-only (HDStops): where a stride drawn on settling to standing goes from gesture <paramref name="gesture"/>
    /// played backwards to its start, the way it came: from a start to standing, from a walk speeding up to the walk (at
    /// the frame it sped up from); none for a gesture that settles forward.
    /// </summary>
    auto SettleBack(const MCSpriteTree& tree, int32_t gesture) -> std::optional<MCDrawnStep>
    {
        if (gesture == MCStopGesture::StandToWalk)
        {
            return MCDrawnStep{MCStopGesture::Stand, 0, 0};
        }

        if (gesture != MCStopGesture::WalkToRun)
        {
            return std::nullopt;
        }

        const auto walkFrames = static_cast<int32_t>(tree.Gestures[MCStopGesture::Walk].NumFrames);
        const auto walkToRun = static_cast<int32_t>(tree.SpecialInfo.WalkToWRFrame);
        return MCDrawnStep{MCStopGesture::Walk, 0, std::clamp(walkToRun, 0, std::max(walkFrames - 1, 0))};
    }

    /// <summary>
    /// Port-only (HDStops): the frames a stride settling in place steps the short way round (backwards, maybe) to where
    /// it gives way at, frame <paramref name="at"/>, from frame <paramref name="frame"/> of its
    /// <paramref name="count"/>: backwards below 0.
    /// </summary>
    auto SettleSteps(float frame, float at, float count) -> float
    {
        return std::remainder(at - frame, count);
    }

    /// <summary>
    /// Port-only (HDStops): whether stride drawn <paramref name="drawn"/> settles to standing through
    /// <paramref name="tree"/>'s art stepping no longer than <see cref="SettleMostSeconds"/> in any stride on the way.
    /// </summary>
    auto SettlesNear(MCSpriteTree& tree, const MCDrawnStride& drawn) -> bool
    {
        int32_t gesture = drawn.Gesture;
        float frame = drawn.Frame[MCMechPart::Legs];

        for (int32_t pass = 0; pass < MostDrawnPasses; ++pass)
        {
            const std::optional<MCDrawnStep> back = SettleBack(tree, gesture);
            const std::optional<MCDrawnStep> next =
                back.has_value() ? back : NextDrawnStep(tree, gesture, MCStopGesture::Stand);

            if (!next.has_value())
            {
                return true;
            }

            if (WalkOrRun(gesture))
            {
                const MCGestureData& data = tree.Gestures[static_cast<size_t>(gesture)];
                const auto count = static_cast<float>(data.NumFrames);
                const auto at = static_cast<float>(LeaveFrame(drawn, gesture, *next));

                if (std::fabs(SettleSteps(frame, at, count)) >
                    SettleMostSeconds * std::fabs(data.FrameRate) * MaxStrideRate)
                {
                    return false;
                }
            }

            gesture = next->Next;
            frame = static_cast<float>(next->Start);
        }

        return true;
    }
}

auto MCMechActor::HitMech(int32_t) -> int
{
    return 0;
}

auto MCMechActor::SetGesture(uint32_t gesture) -> int32_t
{
    if (GestureSet)
    {
        return static_cast<int32_t>(0xeade0004);
    }

    GestureSet = true;
    CurrentGesture = static_cast<int32_t>(gesture);
    CurrentStateGesture = EquivalentGestureArray[gesture];
    return 0;
}

auto MCMechActor::GetNumFramesInGesture(uint32_t gesture) const -> float
{
    return static_cast<float>(MechTree->Gestures[gesture].NumFrames);
}

auto MCMechActor::GetVelocityOfGesture(uint32_t gesture) const -> float
{
    if (static_cast<int32_t>(gesture) < static_cast<int32_t>(MechTree->NumGestures))
    {
        return MechTree->Gestures[gesture].StartVelocity;
    }

    return -1.0f;
}

auto MCMechActor::GetHotSpotIndex(uint32_t location) -> uint32_t
{
    return HotSpotFinderArray[location];
}

auto MCMechActor::PreloadGestures() const -> void
{
    MechTree->PreloadGestures();
}

auto MCMechActor::PrepareHdArt() const -> void
{
    MechTree->PrepareHdArt();
}

auto MCMechActor::SetGestureGoal(int32_t goal) -> int32_t
{
    if (GoalPending)
    {
        return static_cast<int32_t>(0xeada0005);
    }

    const int32_t state = CurrentStateGesture;

    if (state == goal)
    {
        return static_cast<int32_t>(0xeade0001);
    }

    if (InTransition)
    {
        return static_cast<int32_t>(0xeade0002);
    }

    if (goal == 6)
    {
        if (!JumpSetup)
        {
            return static_cast<int32_t>(0xeada0006);
        }
    }
    else if (goal < 0 || goal > 8)
    {
        return static_cast<int32_t>(0xeade0003);
    }

    // Running backwards (the torso turned past the side) is walking backwards for mirrored gestures.
    const auto mech = std::static_pointer_cast<MCBattleMech>(Owner.lock());
    const double facing = MCActorFacing(*mech) + mech->TorsoRotation;

    if (!(facing >= 0.0f) || facing >= 180.0)
    {
        const MCGestureData& data = MechTree->Gestures[goal];

        if ((data.Symmetrical != 0 || data.ArmSymmetrical != 0) && goal == 8)
        {
            goal = 7;
        }
    }

    TransitionStep = 0;

    // Faithful: a leftover test of the transition table index that can't fail.
    if ((goal + state * 9) * 10 == -1)
    {
        return static_cast<int32_t>(0xeade0003);
    }

    GestureGoal = goal;
    GoalPending = true;
    return 0;
}

auto MCMechActor::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    Visible = false;
    Owner = std::move(obj);
    MechTree = std::static_pointer_cast<MCSpriteTree>(std::move(tree));

    RightArmGone = false;
    LeftArmGone = false;
    GestureSet = false;
    FadeTableIndex = -1;
    Wrecked = false;

    for (int32_t i = 0; i < MechPartCount; i++)
    {
        PartShape[i].reset();
        CurrentFrame[i] = -1;
        CurrentTime[i] = 0.0f;
        LastFrame[i] = 0;
        FrameRate[i] = 15.0f;
        PartOrder[i] = MCMechPart::Legs;
    }

    Visible = false;
    ShapeMinY = -25.0f;
    ShapeMinX = -25.0f;
    JumpVelocity = 0.0f;
    GoalPending = false;
    CurrentGesture = 0;
    GestureGoal = -1;
    CurrentStateGesture = 0;
    InTransition = false;
    GestureDone = false;
    NextGesture = -1;
    PlayBackwards = false;
    LyingStill = false;
    FallTurnPending = false;
    StandTurnPending = false;
    HeightsGesture = -1;
    InJump = false;
    JumpSetup = false;
    JumpGoal.Z = 0.0f;
    JumpGoal.Y = 0.0f;
    JumpGoal.X = 0.0f;
    JumpParameter = 0.0f;
    JumpSpeed = 0.0f;
    Airborne = false;
    BodyTurnLocked = false;
    UpperBodyLocked = false;
    ShapeMaxY = 25.0f;
    ShapeMaxX = 25.0f;
    InView = false;
    StopCountdown = 0.0f;
    InCombatMode = false;
    CombatModeRaising = false;
    CombatModeLowering = false;
    return 0;
}

auto MCMechActor::SetCombatMode(bool combatMode) -> void
{
    int32_t gesture;

    if (!combatMode)
    {
        if (!InCombatMode)
        {
            return;
        }

        if (!CombatModeLowering)
        {
            InCombatMode = false;
            CombatModeLowering = true;
            CombatModeChanged = 1;
        }

        gesture = CurrentGesture;

        if (gesture < 2)
        {
            CombatModeLowering = false;
            InCombatMode = false;
            CombatModeChanged = 0;
            return;
        }
    }
    else
    {
        if (InCombatMode)
        {
            return;
        }

        gesture = CurrentGesture;

        if (gesture < 2 || gesture > 11)
        {
            return;
        }

        // Only from the stand, walk and run gestures the mech has a gun pose for.
        const MCMechSpecialInfo& info = MechTree->SpecialInfo;

        if (info.StandToGunPose == 0 && gesture == 2)
        {
            return;
        }

        if (info.WalkToGunPose == 0 && gesture > 2 && gesture < 6)
        {
            return;
        }

        if (gesture > 8)
        {
            return;
        }

        if (info.RunToGunPose == 0 && gesture > 5 && gesture < 9)
        {
            return;
        }

        if (!CombatModeRaising)
        {
            CombatModeRaising = true;
            CombatModeChanged = 1;
        }
    }

    if (gesture < 12)
    {
        return;
    }

    CombatModeLowering = false;
    InCombatMode = false;
    CombatModeChanged = 0;
}

auto MCMechActor::SetJumpParameters(const MCVector3D& goal) -> int32_t
{
    if (InJump)
    {
        return static_cast<int32_t>(0xeada0007);
    }

    JumpParameter = 4.0f;
    JumpSetup = true;
    JumpGoal = goal;
    return 0;
}

auto MCMechActor::GetVelocityMagnitude() -> float
{
    const int32_t gesture = CurrentGesture;

    if (gesture == GestureJump || gesture == 12 || gesture == 13)
    {
        return JumpVelocity;
    }

    // Gestures 14 to 19 have no velocity of their own. Port: the speed eased over the gesture is MCFrameSpeed (with
    // MechBraking, the expansion's stopping gestures slow down: OB-186).
    if (gesture >= 14 && gesture <= 19)
    {
        return 0.0f;
    }

    const bool known = gesture < MechTree->NumGestures;
    const MCGestureVelocities velocities =
        known ? MCStopVelocities(*MechTree, gesture, MCMechBrakingInForce()) : MCGestureVelocities{-1.0f, -1.0f};
    const uint32_t numFrames = known ? MechTree->Gestures[gesture].NumFrames : 0;
    return MCFrameSpeed(velocities, numFrames, CurrentFrame[MCMechPart::Legs]);
}

auto MCMechActor::SetMovePath(const MCMovePath& path) -> int32_t
{
    if (path.NumSteps != 0 && path.CurStep < path.NumSteps)
    {
        StopCountdown = 5.0f;
        return 0;
    }

    StopCountdown = 0.0f;
    return 0;
}

auto MCMechActor::CheckStop() const -> bool
{
    return StopCountdown == 0.0 && CurrentGesture > 2 && CurrentGesture < 12 && GestureGoal < 7 &&
           !(PlannedStop && InTransition);
}

namespace
{
    /// <summary>
    /// Port-only (MechBraking): the legs' frame gesture <paramref name="gesture"/> gives way at in a stop whose next
    /// gesture is <paramref name="next"/>, as update tests it: a walk's frame to stand from, a run's to walk from, else
    /// its end.
    /// </summary>
    auto StopStepFrame(const MCSpriteTree& tree, int32_t gesture, int32_t next) -> int32_t
    {
        const auto frames = static_cast<int32_t>(tree.Gestures[static_cast<size_t>(gesture)].NumFrames);
        const MCMechSpecialInfo& info = tree.SpecialInfo;

        if (gesture == MCStopGesture::Walk && next == MCStopGesture::WalkToStand && info.WalkToWSFrame != NoFrame)
        {
            return std::min(static_cast<int32_t>(info.WalkToWSFrame), frames);
        }

        if (gesture == MCStopGesture::Run && next == MCStopGesture::RunToWalk)
        {
            if (const std::optional<uint32_t> frame = MCRunToWalkFrame(tree, true); frame.has_value())
            {
                return std::min(static_cast<int32_t>(*frame), frames);
            }
        }

        return frames;
    }

    /// <summary>
    /// Port-only (MechBraking): the metres gesture <paramref name="gesture"/> covers from <paramref name="from"/> (its
    /// frame and how far into it) until its frame next reaches <paramref name="until"/> (round from its end to its
    /// start, a stride's, when past it), at its own rate.
    /// </summary>
    auto MetresUntil(const MCSpriteTree& tree, int32_t gesture, float from, int32_t until) -> float
    {
        const float end = MCGestureMetres(tree, gesture, static_cast<float>(until), true);
        const float rest = MCGestureMetres(tree, gesture, from, true) - end;
        return from <= static_cast<float>(until) ? rest : rest + MCGestureMetres(tree, gesture, 0.0f, true);
    }

    /// <summary>
    /// Port-only (MechBraking): the metres a planned stop's gestures from step <paramref name="step"/> of its transition
    /// (from state <paramref name="state"/>) on cover, each played from its start to where it gives way.
    /// </summary>
    auto StopMetresFrom(MCSpriteTree& tree, int32_t state, int32_t step) -> float
    {
        float metres = 0.0f;

        for (int32_t gesture = MCStopTransition(tree, MCStopState::Stand, state, step, true); gesture != -1;)
        {
            const int32_t next = MCStopTransition(tree, MCStopState::Stand, state, ++step, true);
            metres += MetresUntil(tree, gesture, 0.0f, StopStepFrame(tree, gesture, next));
            gesture = next;
        }

        return metres;
    }
} // namespace

auto MCMechActor::FitStop(std::optional<float> metresLeft) -> bool
{
    if (!MCMechBrakingInForce() || CurrentFrame[MCMechPart::Legs] < 0)
    {
        PlannedStop = false;
        StrideRate = 1.0f;
        return false;
    }

    MCSpriteTree& tree = *MechTree;
    const float legsRate = FrameRate[MCMechPart::Legs] * StrideRate;
    const float frame = static_cast<float>(CurrentFrame[MCMechPart::Legs]) +
                        std::clamp(CurrentTime[MCMechPart::Legs] * legsRate, 0.0f, 1.0f);

    if (PlannedStop && InTransition)
    {
        // Under way: fit the gesture playing to the room its part of the stop has left, until the move is done.
        if (!metresLeft.has_value())
        {
            return false;
        }

        const float own = MetresUntil(tree, CurrentGesture, frame, StopStepFrame(tree, CurrentGesture, NextGesture));
        const float room = *metresLeft - StopMetresFrom(tree, CurrentStateGesture, TransitionStep);

        // Still in the stride, with further to go than it can be slowed to fill: walk on (planned again).
        if (TransitionStep == 0 && room > 0.0f && own / room < MinStrideRate)
        {
            CancelPlannedStop();
            return false;
        }

        StrideRate = room > 0.0f ? std::clamp(own / room, MinStrideRate, MaxStrideRate) : MaxStrideRate;
        return false;
    }

    PlannedStop = false;
    StrideRate = 1.0f;
    const bool walking = CurrentGesture == MCStopGesture::Walk && CurrentStateGesture == MCStopState::Walk;
    const bool running = CurrentGesture == MCStopGesture::Run && CurrentStateGesture == MCStopState::Run;

    if (!metresLeft.has_value() || InTransition || !(walking || running))
    {
        return false;
    }

    // The stop starts where the stride gives way to its first gesture (without the art, it stops dead as the original).
    const int32_t next = MCStopTransition(tree, MCStopState::Stand, CurrentStateGesture, 0, true);

    if (next == -1 || next == MCStopGesture::Stand)
    {
        return false;
    }

    const int32_t until = StopStepFrame(tree, CurrentGesture, next);
    const auto strideFrames = static_cast<int32_t>(tree.Gestures[static_cast<size_t>(CurrentGesture)].NumFrames);
    const float framesLeft = frame <= static_cast<float>(until) ? static_cast<float>(until) - frame
                                                                : static_cast<float>(until + strideFrames) - frame;
    const float speed = MCFrameSpeed(MCStopVelocities(tree, CurrentGesture, true), static_cast<uint32_t>(strideFrames),
                                     CurrentFrame[MCMechPart::Legs]);
    const std::optional<MCStridePlan> plan = MCPlanStride(framesLeft, strideFrames, speed / FrameRate[MCMechPart::Legs],
                                                          *metresLeft - StopMetresFrom(tree, CurrentStateGesture, 0));

    if (!plan.has_value())
    {
        return false;
    }

    StrideRate = plan->Rate;

    // The stride that ends in the stop, before the frame it gives way at (a transition doesn't go round past its end).
    if (plan->Strides != 0 || frame > static_cast<float>(until))
    {
        return false;
    }

    PlannedStop = true;
    return true;
}

auto MCMechActor::CancelPlannedStop() -> bool
{
    if (!PlannedStop || !InTransition || TransitionStep != 0)
    {
        return false;
    }

    InTransition = false;
    GestureGoal = -1;
    NextGesture = -1;
    GestureDone = false;
    PlannedStop = false;
    StrideRate = 1.0f;
    return true;
}

int GHdStops = 0;

auto MCMechActor::HoldStride(const MCBattleMech& mech) -> void
{
    if (GHdStops == 0)
    {
        DrawnStride.reset();
        return;
    }

    // Held as last drawn (render keeps a stride's parts' frames in step); one drawn on already goes on as it was.
    const bool stride = WalkOrRun(CurrentGesture) && !PlayBackwards;

    if (DrawnStride.has_value() || !stride || !mech.MoveHeldUp())
    {
        return;
    }

    MCDrawnStride drawn{CurrentGesture, {}, InCombatMode || CombatModeRaising || CombatModeLowering};
    drawn.MechGesture = MCStopGesture::Stand;

    for (int32_t part = 0; part < MechPartCount; ++part)
    {
        drawn.Frame[part] = static_cast<float>(std::max(CurrentFrame[part], 0));
    }

    DrawnStride = drawn;
}

auto MCMechActor::StepDrawnStride(const MCBattleMech& mech) -> void
{
    const MCVector3D position = mech.Position;
    const float moved =
        LastPosition.has_value()
            ? static_cast<float>(std::hypot(position.X - LastPosition->X, position.Y - LastPosition->Y) *
                                 MetersPerWorldUnit)
            : 0.0f;
    LastPosition = position;

    if (!DrawnStride.has_value())
    {
        return;
    }

    MCDrawnStride& drawn = *DrawnStride;
    const std::optional<int32_t> headsFor = PlayBackwards ? std::nullopt : HeadsFor(CurrentGesture);

    if (GHdStops == 0 || !headsFor.has_value() ||
        (drawn.Gesture == MCStopGesture::Stand && CurrentGesture == MCStopGesture::Stand))
    {
        DrawnStride.reset();
        return;
    }

    // Where the mech left its walk or run for the gesture after (the frame it was last on), for the drawn to leave them
    // there too; dropping to standing (as the original stops) leaves it no frame to go from.
    if (CurrentGesture != drawn.MechGesture && CurrentGesture != MCStopGesture::Stand && WalkOrRun(drawn.MechGesture))
    {
        std::optional<int32_t>& left = drawn.MechGesture == MCStopGesture::Run ? drawn.RunLeftAt : drawn.WalkLeftAt;
        left = left.value_or(drawn.MechFrame);
    }

    drawn.MechGesture = CurrentGesture;
    drawn.MechFrame = CurrentFrame[MCMechPart::Legs];

    // Standing, setting off, or on the way from standing to the stride drawn.
    const std::optional<int32_t> going =
        InTransition && CurrentStateGesture == MCStopState::Stand ? StateGesture(GestureGoal) : std::nullopt;
    const bool restart = CurrentGesture == MCStopGesture::Stand || CurrentGesture == MCStopGesture::StandToWalk ||
                         (going == drawn.Gesture && CurrentGesture != drawn.Gesture);
    const bool drawnStride = WalkOrRun(drawn.Gesture);
    drawn.StillSeconds = restart && moved < StillMetres ? drawn.StillSeconds + FrameLength : 0.0f;
    const bool still = CurrentGesture == MCStopGesture::Stand && moved < StillMetres;
    MCDrawnMode mode = MCDrawnMode::Following;

    if (restart && drawn.Gesture != MCStopGesture::Stand)
    {
        // Settles once still too long or with nowhere to go, and keeps on settling while the mech stands still.
        const bool settles = drawn.StillSeconds > HoldMostSeconds ||
                             (CurrentGesture == MCStopGesture::Stand && !InTransition && !mech.MoveHeldUp()) ||
                             (drawn.Mode == MCDrawnMode::Settling && still);
        mode = settles ? MCDrawnMode::Settling : MCDrawnMode::Held;
    }

    // Settling from further than a moment's steps would walk on the spot: it drops to standing as the original does.
    if (mode == MCDrawnMode::Settling && drawn.Mode != MCDrawnMode::Settling && !SettlesNear(*MechTree, drawn))
    {
        DrawnStride.reset();
        return;
    }

    if (mode == MCDrawnMode::Held && drawn.Mode != MCDrawnMode::Held)
    {
        drawn.WalkLeftAt.reset();
        drawn.RunLeftAt.reset();
    }

    drawn.Mode = mode;
    const MCGestureData& data = MechTree->Gestures[static_cast<size_t>(drawn.Gesture)];
    const auto frames = static_cast<float>(data.NumFrames);
    const float timeStep = std::fabs(data.FrameRate) * FrameLength;
    const float legs = drawn.Frame[MCMechPart::Legs];
    const bool combat = InCombatMode || CombatModeRaising || CombatModeLowering;

    // The gun lowered, the upper body takes the legs' frame (as update has it do).
    if (drawn.UpperInCombat && !combat)
    {
        for (int32_t part = 1; part < MechPartCount; ++part)
        {
            drawn.Frame[part] = legs;
        }
    }

    drawn.UpperInCombat = combat;

    if (!(frames > 0.0f))
    {
        DrawnStride.reset();
        return;
    }

    if (mode == MCDrawnMode::Held)
    {
        // A stride by the ground covered, its frames a metre; between strides on to the next at its own rate.
        const bool byGround = drawnStride && data.StartVelocity > 0.0f;
        const int32_t goal = drawnStride ? drawn.Gesture : HeadsFor(drawn.Gesture).value_or(drawn.Gesture);
        PlayDrawnStride(byGround ? moved * std::fabs(data.FrameRate) / data.StartVelocity : timeStep, goal, false);
        return;
    }

    if (mode == MCDrawnMode::Settling)
    {
        PlayDrawnStride(timeStep * MaxStrideRate, MCStopGesture::Stand, true);
        return;
    }

    // Following: faster than the mech's gestures play, closing on its own once in it the shorter way (a stride round
    // either way): faster behind it, slower ahead of it.
    float step = timeStep * MaxStrideRate * (drawn.Gesture == CurrentGesture ? StrideRate : 1.0f);

    if (drawn.Gesture == CurrentGesture)
    {
        const auto mechFrame = static_cast<float>(CurrentFrame[MCMechPart::Legs]);
        const float behind = drawnStride ? std::remainder(mechFrame - legs, frames) : mechFrame - legs;

        if (std::fabs(behind) < InStepFrames)
        {
            // On it, exactly, so that its frame floors to the mech's (render hands back to it).
            const float onto = std::remainder(mechFrame - legs, frames);

            for (int32_t part = 0; part < MechPartCount; ++part)
            {
                drawn.Frame[part] = std::fmod(drawn.Frame[part] + onto + frames, frames);
            }

            drawn.Frame[MCMechPart::Legs] = mechFrame;
            return;
        }

        step *= behind < 0.0f ? MinStrideRate / MaxStrideRate : 1.0f;
    }

    PlayDrawnStride(step, *headsFor, false);
}

auto MCMechActor::PlayDrawnStride(float frames, int32_t goal, bool settling) -> void
{
    MCDrawnStride& drawn = *DrawnStride;

    for (int32_t pass = 0; pass < MostDrawnPasses && frames > 0.0f; ++pass)
    {
        const int32_t gesture = drawn.Gesture;
        const auto count = static_cast<float>(MechTree->Gestures[static_cast<size_t>(gesture)].NumFrames);
        const bool stride = WalkOrRun(gesture);
        const std::optional<MCDrawnStep> next = NextDrawnStep(*MechTree, gesture, goal);
        const float legs = drawn.Frame[MCMechPart::Legs];

        if (!(count > 0.0f))
        {
            return;
        }

        // Each part on by `by` frames: a stride round, anything else up to its end (its last frame, once there).
        const float last = next.has_value() ? std::nextafter(count, 0.0f) : count - 1.0f;
        const auto advance = [&](float by)
        {
            for (int32_t part = 0; part < MechPartCount; ++part)
            {
                const float frame = drawn.Frame[part] + by;
                drawn.Frame[part] =
                    stride ? std::fmod(std::fmod(frame, count) + count, count) : std::clamp(frame, 0.0f, last);
            }
        };

        if (!next.has_value())
        {
            advance(frames);
            return;
        }

        // Where it gives way (LeaveFrame): a stride forward round to there (settling, the short way round, backwards
        // maybe: SettleSteps), anything else on to it. Settling, a start or a walk speeding up goes back the way it came
        // (SettleBack).
        const std::optional<MCDrawnStep> back = settling ? SettleBack(*MechTree, gesture) : std::nullopt;
        const auto at = static_cast<float>(LeaveFrame(drawn, gesture, *next));
        const float steps = settling && stride ? SettleSteps(legs, at, count) : 0.0f;
        const float toward = back.has_value()     ? legs
                             : settling && stride ? std::fabs(steps)
                             : stride             ? std::fmod(at - legs + count, count)
                                                  : std::max(at - legs, 0.0f);
        const float way = back.has_value() || steps < 0.0f ? -1.0f : 1.0f;
        const MCDrawnStep onTo = back.value_or(*next);

        if (frames < toward)
        {
            advance(frames * way);
            return;
        }

        // On to the next gesture, every part at its start (as update has it).
        frames -= toward;
        drawn.Gesture = onTo.Next;

        for (int32_t part = 0; part < MechPartCount; ++part)
        {
            drawn.Frame[part] = static_cast<float>(onTo.Start);
        }

        if (gesture == MCStopGesture::Walk)
        {
            drawn.WalkLeftAt.reset();
        }
        else if (gesture == MCStopGesture::Run)
        {
            drawn.RunLeftAt.reset();
        }
    }
}

auto MCMechActor::DrawnSettlesNear() const -> bool
{
    return DrawnStride.has_value() && SettlesNear(*MechTree, *DrawnStride);
}

auto MCMechActor::DrawnGesture() const -> int32_t
{
    return DrawnStride.has_value() ? DrawnStride->Gesture : CurrentGesture;
}

auto MCMechActor::DrawnLegsFrame() const -> int32_t
{
    return DrawnStride.has_value() ? static_cast<int32_t>(std::floor(DrawnStride->Frame[MCMechPart::Legs]))
                                   : CurrentFrame[MCMechPart::Legs];
}

auto MCMechActor::RecalcBounds(MCOptionalRef<MCCamera> view) -> int
{
    if (!view.has_value())
    {
        return 0;
    }

    const MCCamera& cam = view->get();
    const MCVector2D pos = Owner.lock()->GetScreenPos(cam.CameraId - 1);
    LowerRight.Y = pos.Y;
    UpperLeft.X = pos.X;
    UpperLeft.Y = pos.Y;
    LowerRight.X = pos.X;

    // The bounds are taken once, from every part's first frames seen.
    if (!InView)
    {
        for (int32_t i = 0; i < MechPartCount; i++)
        {
            const MCMechPart part = PartOrder[i];

            const std::shared_ptr<MCShape> shape = PartShape[part].lock();

            if (shape == nullptr || shape->FrameList().empty())
            {
                continue;
            }

            const std::span<const uint8_t> shapeTable = shape->FrameList();
            int32_t frame = CurrentFrame[part];

            if (frame < 0)
            {
                frame = 0;
            }

            const int32_t numShapeFrames = VfxShapeCount(shapeTable);

            if (numShapeFrames <= frame)
            {
                frame = numShapeFrames - 1;
            }

            const int32_t minXY = VfxShapeMinxy(shapeTable, frame);

            if (static_cast<float>(minXY >> 16) < ShapeMinX)
            {
                ShapeMinX = static_cast<float>(minXY >> 16);
            }

            if (static_cast<float>(static_cast<int16_t>(minXY)) < ShapeMinY)
            {
                ShapeMinY = static_cast<float>(static_cast<int16_t>(minXY));
            }

            const int32_t size = VfxShapeResolution(shapeTable, frame);

            if (ShapeMaxX < static_cast<float>(size >> 16))
            {
                ShapeMaxX = static_cast<float>(size >> 16);
            }

            if (ShapeMaxY < static_cast<float>(static_cast<int16_t>(size)))
            {
                ShapeMaxY = static_cast<float>(static_cast<int16_t>(size));
            }
        }

        InView = true;
    }

    // Faithful: the top-left offset is added twice.
    UpperLeft.X = ShapeMinX + ShapeMinX + UpperLeft.X;
    UpperLeft.Y = ShapeMinY + ShapeMinY + UpperLeft.Y;
    LowerRight.X = ShapeMaxX + LowerRight.X;
    LowerRight.Y = ShapeMaxY + LowerRight.Y;

    if (0.0f <= LowerRight.X && 0.0f <= LowerRight.Y &&
        UpperLeft.X <= static_cast<float>(static_cast<int32_t>(std::floor(cam.ViewWidth))) &&
        UpperLeft.Y <= static_cast<float>(static_cast<int32_t>(std::floor(cam.ViewHeight))))
    {
        return 1;
    }

    return 0;
}

auto MCFindJumpFrames(std::span<const float> heights) -> MCJumpFrames
{
    // From the bottom of the crouch to the top of the climb.
    MCJumpFrames frames;
    bool descended = false;
    bool climbing = false;

    for (int32_t i = 0; i < static_cast<int32_t>(heights.size()); i++)
    {
        const float change = i != 0 ? heights[i] - heights[i - 1] : 0.0f;

        if (change < 0.0 && !descended)
        {
            descended = true;
        }

        if (change >= 0.0 && !climbing && descended)
        {
            climbing = true;
            frames.LiftOff = i;
        }

        if (change < 0.0 && climbing)
        {
            frames.TouchDown = i;
            break;
        }
    }

    return frames;
}

auto CalcRotation(float rotation, int32_t numRotations) -> int32_t
{
    double degrees = rotation;

    if (degrees > 180.0)
    {
        degrees -= 360.0;
    }
    else if (degrees < -180.0)
    {
        degrees += 360.0;
    }

    if (degrees < 0.0)
    {
        degrees += 360.0;
    }

    int32_t index = static_cast<int16_t>(
        static_cast<int32_t>(std::floor(degrees * static_cast<double>(numRotations + 1) * (1.0 / 360.0))));

    if (index > 0x1f)
    {
        index = 0x1f;
    }

    return index;
}

auto MCMechActor::FrameHeights() const -> std::span<const float>
{
    if (HeightsGesture < 0)
    {
        return {};
    }

    return GestureHeights(*Owner.lock(), HeightsGesture);
}

auto MCMechActor::FrameHeight(int32_t frame) const -> float
{
    const std::span<const float> heights = FrameHeights();

    if (heights.empty())
    {
        return 0.0f;
    }

    return heights[std::min(static_cast<size_t>(std::max(frame, 0)), heights.size() - 1)];
}

auto MCMechActor::RenderJump() -> void
{
    if (FrameHeights().empty())
    {
        return;
    }

    // The original also turns i and j by 45 degrees here, then drops them.
    const auto mech = std::static_pointer_cast<MCBattleMech>(Owner.lock());
    const MCFrameOfRef frame = mech->GetFrame();

    if (CurrentGesture != GestureJump)
    {
        return;
    }

    const int32_t frameNum = CurrentFrame[MCMechPart::Legs];
    const float height = FrameHeight(frameNum) * 30.0f;

    if (LiftOffFrame <= frameNum)
    {
        JumpVelocity = JumpSpeed;
        Airborne = true;
    }

    if (TouchDownFrame <= frameNum && Airborne)
    {
        JumpVelocity = 0.0f;
        Airborne = false;
    }

    // Project the mech raised along its up axis by the jump height.
    const MCVector3D offset(frame.K.X * height, frame.K.Y * height, frame.K.Z * height);
    const MCVector3D position = mech->GetPosition();
    const MCVector3D raised(position.X + offset.X, position.Y + offset.Y, position.Z + offset.Z);
    const MCCamera& cam = *Eye();
    const MCVector3D camPosition = cam.Position;
    const MCVector3D relative(raised.X - camPosition.X, raised.Y - camPosition.Y, raised.Z - camPosition.Z);
    const float y = ((relative.X * cam.SinAngle + cam.HalfHeight) - relative.Y * cam.SinAngle) - relative.Z;
    ScreenPos.X = relative.Y * cam.CosAngle + relative.X * cam.CosAngle + cam.HalfWidth;
    const float x = ScreenPos.X;
    UpperLeft.X = ScreenPos.X;
    LowerRight.X = x;
    ScreenPos.Y = y;
    UpperLeft.Y = y;
    LowerRight.Y = y;
    UpperLeft.X = ShapeMinX + ShapeMinX + x;
    UpperLeft.Y = ShapeMinY + ShapeMinY + y;
    LowerRight.X = ShapeMaxX + x;
    LowerRight.Y = ShapeMaxY + y;
}

int GHdTurns = 0;

auto MCEaseFacing(float shown, float facing, float maxTurn, float lagMost) -> float
{
    constexpr float Round = 360.0f;
    constexpr float Half = 180.0f;
    const auto wrap = [](float degrees) { return degrees - Round * std::floor((degrees + Half) / Round); };
    const float gap = wrap(facing - shown);
    const float turned = std::clamp(gap, -maxTurn, maxTurn);
    return wrap(facing - std::clamp(gap - turned, -lagMost, lagMost));
}

auto MCMechActor::LegsTurnMiddle(int32_t gesture, int32_t frameNow, float facing, const MCShape& legs)
    -> std::optional<MCInBetween>
{
    DrawnLegs = MechTree->DrawnFacing(gesture, 0, facing);
    DrawnLegsGesture = gesture;
    const std::optional<MCTurnMiddle> middle =
        GHdTurns != 0 && MCHdArt::Active() ? MechTree->TurnMiddle(gesture, facing) : std::nullopt;

    if (!middle.has_value() || !DrawnLegs.has_value())
    {
        return std::nullopt;
    }

    int32_t reverse = 0;
    float frameRate = 0.0f;
    const std::shared_ptr<MCShape> toward =
        MechTree->GetGesture(gesture, 0, middle->TowardRotation, reverse, frameRate, true);

    if (toward == nullptr)
    {
        return std::nullopt;
    }

    const std::span<const uint8_t> table = legs.FrameList();
    const int32_t frame = MCClampShapeFrame(table, frameNow);
    const auto frames = static_cast<int32_t>(MechTree->Gestures[gesture].NumFrames);
    // A mirrored neighbour leads with the other leg: in a stride, its frame half a cycle on leads with this one's (as
    // tools/upscale/make_turn_middles.py pairs them).
    const bool halfOn = middle->TowardMirrored && MCStrideGesture(gesture) && frames > 0;
    const int32_t towardFrame = MCClampShapeFrame(toward->FrameList(), halfOn ? (frame + frames / 2) % frames : frame);
    const MCFrameBounds own = MCShapeFrameBounds(table, frame);
    const MCFrameBounds other = MCShapeFrameBounds(toward->FrameList(), towardFrame);

    if (own.Width <= 0.0f || own.Height <= 0.0f || other.Width <= 0.0f || other.Height <= 0.0f)
    {
        return std::nullopt;
    }

    // Mirrored, a frame spans -xMax..-xMin round the hot spot (AGShapeTransform).
    const float otherMinX = middle->TowardMirrored ? -(other.MinX + other.Width - 1.0f) : other.MinX;
    const float minX = std::min(own.MinX, otherMinX);
    const float minY = std::min(own.MinY, other.MinY);
    const float maxX = std::max(own.MinX + own.Width, otherMinX + other.Width);
    const float maxY = std::max(own.MinY + own.Height, other.MinY + other.Height);
    const MCInBetweenBounds bounds{static_cast<int32_t>(own.MinX - minX), static_cast<int32_t>(own.MinY - minY),
                                   static_cast<int32_t>(maxX - minX), static_cast<int32_t>(maxY - minY)};

    if (MCHdArt::Find(table.data(), frame, bounds.Width, bounds.Height, middle->Step) == nullptr)
    {
        return std::nullopt;
    }

    DrawnLegs->Degrees = middle->Degrees;
    return MCInBetween{frame, frame, middle->Step, bounds};
}

auto MCMechActor::Render(int32_t depthFixup) -> int32_t
{
    const auto mech = std::static_pointer_cast<MCBattleMech>(Owner.lock());
    ScreenPos = mech->GetScreenPos(Eye()->CameraId - 1);
    const float x = ScreenPos.X;
    const float y = ScreenPos.Y;
    // The 90-pixel part PAKs, the full size ones (the small ones were the original's zoomed-out mode's).
    constexpr bool largeSprites = true;
    RenderJump();

    // Port-only (HDTurns): the facing as drawn (eased by update).
    const float facing =
        GHdTurns != 0 && TurnedFacing.has_value() ? *TurnedFacing : static_cast<float>(MCActorFacing(*mech));
    const float torso = mech->TorsoRotation;
    const float rightArm = mech->RightArmRotation;
    const float leftArm = mech->LeftArmRotation;

    // The torso and arms play the combat gestures while the gun is up or moving.
    const bool combatAnim = InCombatMode || CombatModeRaising || CombatModeLowering;
    const int32_t upperGesture = InCombatMode                                ? GestureCombat
                                 : (CombatModeRaising || CombatModeLowering) ? GestureCombatChange
                                                                             : CurrentGesture;
    const float rotation2 = facing + leftArm + torso;
    const float rotation3 = facing + rightArm + torso;

    // Each part's shape, the legs in gesture legsGesture and the upper body in upper, its mirroring and rate set in
    // reverse and rate.
    const auto partShapes =
        [&](int32_t legsGesture, int32_t upper, MCMechParts<int32_t>& reverse, MCMechParts<float>& rate)
    {
        MCMechParts<std::shared_ptr<MCShape>> found;
        found[MCMechPart::Legs] = MechTree->GetGesture(legsGesture, 0, facing, reverse[MCMechPart::Legs],
                                                       rate[MCMechPart::Legs], largeSprites);
        found[MCMechPart::Torso] = MechTree->GetGesture(upper, 1, facing + torso, reverse[MCMechPart::Torso],
                                                        rate[MCMechPart::Torso], largeSprites);
        const bool armSymmetrical = MechTree->Gestures[legsGesture].ArmSymmetrical != 0;

        // A mirrored arm is drawn from the other arm's sprites. OB-179 fixed: the original swapped on the sign of the
        // angle alone, also facing straight ahead or back, where the sprite isn't mirrored, so a lone arm jumped sides.
        const auto mirrored = [&](int32_t part, float rotation)
        {
            const std::optional<MCGesturePacket> packet = MechTree->GesturePacket(upper, part, rotation);
            return armSymmetrical && packet.has_value() && packet->Reverse;
        };

        const bool swapRightArm = mirrored(3, rotation2);
        const bool swapLeftArm = mirrored(2, rotation3);

        if (!RightArmGone)
        {
            found[MCMechPart::RightArm] =
                MechTree->GetGesture(upper, swapRightArm ? 3 : 2, rotation2, reverse[MCMechPart::RightArm],
                                     rate[MCMechPart::RightArm], largeSprites);
        }

        if (!LeftArmGone && swapLeftArm)
        {
            found[MCMechPart::LeftArm] = MechTree->GetGesture(upper, 2, rotation3, reverse[MCMechPart::LeftArm],
                                                              rate[MCMechPart::LeftArm], largeSprites);
        }
        else if (!LeftArmGone)
        {
            // Faithful: asked for twice, first with the legs' gesture.
            found[MCMechPart::LeftArm] = MechTree->GetGesture(legsGesture, 3, rotation3, reverse[MCMechPart::LeftArm],
                                                              rate[MCMechPart::LeftArm], largeSprites);
            found[MCMechPart::LeftArm] = MechTree->GetGesture(upper, 3, rotation3, reverse[MCMechPart::LeftArm],
                                                              rate[MCMechPart::LeftArm], largeSprites);
        }

        if (Wrecked)
        {
            found[MCMechPart::Torso].reset();
        }

        return found;
    };

    const MCMechParts<std::shared_ptr<MCShape>> shapes = partShapes(CurrentGesture, upperGesture, Reverse, FrameRate);

    for (int32_t part = 0; part < MechPartCount; part++)
    {
        PartShape[part] = shapes[part];
    }

    // Port-only (HDStops): the stride drawn on, by the legs and (but in the gun pose) the upper body; each part as drawn
    // in drawnShape, drawnFrame and drawnReverse.
    MCMechParts<std::shared_ptr<MCShape>> drawnShape = shapes;
    MCMechParts<int32_t> drawnFrame{};
    MCMechParts<int32_t> drawnReverse = Reverse;
    MCMechParts<int32_t> heldFrame{};
    const auto held = [&](MCMechPart part)
    { return DrawnStride.has_value() && (part == MCMechPart::Legs || !combatAnim); };

    if (DrawnStride.has_value())
    {
        MCMechParts<float> rate = FrameRate;
        const MCMechParts<std::shared_ptr<MCShape>> stride =
            partShapes(DrawnStride->Gesture, combatAnim ? upperGesture : DrawnStride->Gesture, drawnReverse, rate);

        for (int32_t part = 0; part < MechPartCount; part++)
        {
            heldFrame[part] = static_cast<int32_t>(std::floor(DrawnStride->Frame[part]));

            if (held(static_cast<MCMechPart>(part)))
            {
                drawnShape[part] = stride[part];
            }
        }
    }

    // The shadow, one of 32 facings. Port-only (sun shadows): with HD art drawn by the GPU, the mech casts its parts'
    // own shadow instead, the sun's way (added once the parts' frames are known, below), when the pack has every part (a
    // mech it lacks, a mod's, keeps the blob) and the sun casts any (at night, the blob).
    const bool sunShadow =
        MCSceneLights::SunCasts() && MCHdArt::Active() && MCRenderer::GpuDrawing() == MCGpuDrawing::On &&
        std::ranges::all_of(drawnShape, [](const auto& shape)
                            { return shape == nullptr || MCHdArt::Attached(shape->FrameList().data()); });
    const auto shadowDepth = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(-y))));
    ElementList().OpenGroup(shadowDepth, true);

    if (!sunShadow)
    {
        auto& shadow = ElementList().Make<MCVfxElement>(SpriteManager()->MechShadow(0), x, y,
                                                        CalcRotation(facing, 0x20), 0, std::nullopt, 0);

        ElementList().Add(shadow);
    }

    const auto partsDepth = static_cast<int16_t>(
        static_cast<int32_t>(std::floor(static_cast<double>(static_cast<float>(depthFixup) - ScreenPos.Y))));
    ElementList().OpenGroup(partsDepth, true);
    // Port-only (HDTurns): the legs' turn middle as drawn, which their sun shadow casts too.
    std::optional<MCInBetween> legsMiddle;

    for (int32_t i = 0; i < MechPartCount; i++)
    {
        const MCMechPart part = PartOrder[i];

        if (shapes[part] == nullptr)
        {
            continue;
        }

        std::optional<MCColorTable> fadeTable;

        if (FadeTableIndex != -1 && FadeTableIndex >= 0)
        {
            fadeTable = GamePalette().GetFadeTable(FadeTableIndex);
        }

        // Walking and running: a mirrored part runs half a cycle off the part it follows, so the stride
        // alternates; an unmirrored one keeps in step with it.
        StrideGesture = MCStrideGesture(CurrentGesture) ? 1 : 0;

        if (StrideGesture != 0 && !combatAnim)
        {
            FollowStride(*MechTree, CurrentFrame, part, CurrentGesture, Reverse[part] != 0, PlayBackwards);
        }

        if (CurrentFrame[part] < 0)
        {
            CurrentFrame[part] = 0;
        }

        drawnFrame[part] = CurrentFrame[part];

        // Port-only (HDStops): the stride drawn on keeps its parts' steps by the same rule.
        if (held(part))
        {
            if (!combatAnim && MCStrideGesture(DrawnStride->Gesture))
            {
                FollowStride(*MechTree, heldFrame, part, DrawnStride->Gesture, drawnReverse[part] != 0, false);
            }

            DrawnStride->Frame[part] += static_cast<float>(heldFrame[part]) - std::floor(DrawnStride->Frame[part]);
            drawnFrame[part] = heldFrame[part];

            // Its stride plays again, in step with it: hand back to it.
            if (part == MCMechPart::Legs && CurrentGesture == DrawnStride->Gesture &&
                drawnFrame[part] == CurrentFrame[part])
            {
                DrawnStride.reset();
            }
        }
        else
        {
            drawnShape[part] = shapes[part];
            drawnReverse[part] = Reverse[part];
        }

        if (drawnShape[part] == nullptr)
        {
            continue;
        }

        auto& element = ElementList().Make<MCVfxElement>(drawnShape[part]->FrameList(), ScreenPos.X, ScreenPos.Y,
                                                         drawnFrame[part], drawnReverse[part], fadeTable, 1);

        if (part == MCMechPart::Legs)
        {
            legsMiddle = LegsTurnMiddle(DrawnGesture(), drawnFrame[part], facing, *drawnShape[part]);
            element.InBetween = legsMiddle;
        }
        else if (part == MCMechPart::RightArm || part == MCMechPart::LeftArm)
        {
            (part == MCMechPart::RightArm ? DrawnRightArm : DrawnLeftArm) = {drawnShape[part], drawnFrame[part],
                                                                             drawnReverse[part]};
        }

        ElementList().Add(element);
    }

    // Port-only (sun shadows): the parts' shadow, on the ground before any unit or object is drawn; then on with the
    // parts' group.
    if (sunShadow)
    {
        ElementList().OpenGroup(MCSceneLights::ShadowDepth, false);

        for (int32_t i = 0; i < MechPartCount; i++)
        {
            const MCMechPart part = PartOrder[i];

            if (shapes[part] == nullptr || drawnShape[part] == nullptr)
            {
                continue;
            }

            auto& element = ElementList().Make<MCVfxElement>(drawnShape[part]->FrameList(), ScreenPos.X, ScreenPos.Y,
                                                             drawnFrame[part], drawnReverse[part], std::nullopt, 1);
            element.SunShadow = true;

            if (part == MCMechPart::Legs)
            {
                element.InBetween = legsMiddle;
            }

            ElementList().Add(element);
        }

        ElementList().OpenGroup(partsDepth, true);
    }

    // Port-only (HD weather): the parts as drawn, which the rain lands on and drips off.
    std::array<MCSceneLights::MCSunShadowPart, MechPartCount> wetParts{};
    size_t wetCount = 0;

    for (int32_t i = 0; i < MechPartCount; i++)
    {
        const MCMechPart part = PartOrder[i];

        if (shapes[part] != nullptr && drawnShape[part] != nullptr)
        {
            wetParts[wetCount++] = {
                drawnShape[part]->FrameList(), ScreenPos.X, ScreenPos.Y, drawnFrame[part], drawnReverse[part], 1};
        }
    }

    MCWeather::AddUnit(std::span(wetParts.data(), wetCount), mech->Position, ScreenPos.X, ScreenPos.Y, partsDepth);

    // Selection: -1 and 1 draw the bars; 3 white brackets; anything else the alignment's brackets.
    const int32_t selected = mech->Selected;
    bool attackerBars = true;

    if (selected == -1 || selected == 1)
    {
        DrawBars();
        attackerBars = false;
    }
    else if (selected == 3)
    {
        RecalcBounds(EyeRef());
        DrawSelectBrackets(0xf8);
    }
    else if (selected != 0)
    {
        RecalcBounds(EyeRef());
        const std::shared_ptr<MCBattleMech>& obj = mech;
        const int32_t alignment = obj->GetAlignment();

        if (alignment == -1)
        {
            DrawSelectBrackets(0xfd);
        }
        else if (alignment == 0)
        {
            DrawSelectBrackets(0xfe);
        }
        else if (alignment == 1)
        {
            DrawSelectBrackets(obj->GetAlignment() == HomeTeam()->Alignment ? 0xfc : 0xfb);
        }
    }

    if (attackerBars && mech->GetNumAttackers() > 0)
    {
        RecalcBounds(EyeRef());
        DrawBars();
    }

    // Port-only (HD lighting): the mech casts shadows from the lights by it, and at night, while it runs, shines its
    // headlight from its cockpit, turned with its torso. (HD particles): it wades through water.
    MCSceneLights::AddCaster(*this, mech->Position);

    if (MCSceneLights::HeadlightOn(mech->Status))
    {
        MCSceneLights::AddHeadlight(*this, mech->Position, mech->GetFrame(), mech->TorsoRotation,
                                    mech->CockpitPosition(), depthFixup);
    }

    MCParticles::AddMover(this, mech->Position, ShapeMaxX * 0.5f);

    if (Highlighting)
    {
        if (HighlightTime > 0.0f)
        {
            HighlightTime -= FrameLength;
            DrawSelectBox(0xfc);
            return 0;
        }

        Highlighted = true;
    }

    return 0;
}

auto MCMechActor::Update() -> int32_t
{
    const auto mech = std::static_pointer_cast<MCBattleMech>(Owner.lock());
    const std::shared_ptr<MCSpriteTree> tree = MechTree;
    GoalPending = false;

    // Port-only (HDTurns): the facing drawn follows the mech's.
    const auto ownFacing = static_cast<float>(MCActorFacing(*mech));
    TurnedFacing = GHdTurns != 0 && TurnedFacing.has_value()
                       ? MCEaseFacing(*TurnedFacing, ownFacing, TurnEaseRate * FrameLength, TurnLagMost)
                       : ownFacing;

    // Moving with nowhere to go: stand. Port-only (HDStops): drawn holding its stride while its move is held up.
    if (CheckStop())
    {
        HoldStride(*mech);
        CurrentGesture = GestureStand;
        CurrentStateGesture = 1;

        if (static_cast<uint8_t>(mech->Status) != 4)
        {
            GestureGoal = -1;
            NextGesture = -1;
            InTransition = false;

            if (mech->IsDisabled() == 0)
            {
                mech->Status = 0;
            }
        }
    }

    // Start the transition to the goal: the table lists the gestures from the state to it. Port-only (MechBraking): a
    // planned stop, and a run slowing to a walk, through the stopping art (MCStopTransition).
    int32_t goal = GestureGoal;
    const auto transition = [&](int32_t step)
    {
        const bool braking = goal == MCStopState::Stand ? PlannedStop : MCMechBrakingInForce();
        return MCStopTransition(*tree, goal, CurrentStateGesture, step, braking);
    };

    if (goal != -1 && !InTransition && NextGesture == -1)
    {
        InTransition = true;
        TransitionStep = 0;
        NextGesture = transition(0);

        if (NextGesture == -1)
        {
            GestureGoal = -1;
            InTransition = false;
        }

        goal = GestureGoal;
        GestureDone = false;

        if (goal > 6)
        {
            GestureDone = true;
        }
    }

    if (InTransition && GestureDone)
    {
        // The last gesture played out: on to the next one.
        const int32_t prevGesture = CurrentGesture;
        CurrentGesture = NextGesture;
        TransitionStep++;
        NextGesture = transition(TransitionStep);

        int32_t startFrame = 0;
        bool checkReverse = true;

        if (NextGesture == -1)
        {
            // Arrived.
            CurrentStateGesture = goal;

            if (goal == 6)
            {
                CurrentStateGesture = 1;
            }

            GestureGoal = -1;
            InTransition = false;
            PlannedStop = false;
            StrideRate = 1.0f;

            if (mech->IsDisabled() == 0)
            {
                if (CurrentStateGesture == 1 && static_cast<uint8_t>(mech->Status) != 4)
                {
                    mech->Status = 0;
                }
                else if (CurrentStateGesture == 0)
                {
                    mech->Status = 5;
                }
            }

            if (NextGesture == -1)
            {
                // A gesture with a negative frame rate plays backwards.
                checkReverse = false;

                if (tree->Gestures[CurrentGesture].FrameRate < 0.0)
                {
                    startFrame = static_cast<int32_t>(tree->Gestures[CurrentGesture].NumFrames) - 1;
                    PlayBackwards = true;
                }
                else
                {
                    startFrame = 0;
                    PlayBackwards = false;
                }
            }
        }

        if (checkReverse)
        {
            // The gesture plays backwards when the next one is its reverse result.
            const MCGestureData& data = tree->Gestures[CurrentGesture];

            if (static_cast<uint32_t>(data.ReverseResult) == static_cast<uint32_t>(NextGesture))
            {
                startFrame = static_cast<int32_t>(data.NumFrames) - 1;
                PlayBackwards = true;
            }
            else
            {
                startFrame = 0;
                PlayBackwards = false;
            }
        }

        StrideGesture = 0;
        HeightsGesture = -1;
        uint32_t gesture = static_cast<uint32_t>(CurrentGesture);

        if (Wrecked && gesture != 0x17 && gesture != 0x18)
        {
            InTransition = false;
            GestureDone = false;
            GestureGoal = -1;
            CurrentGesture = static_cast<int32_t>((gesture & 1) + 0x17);
        }

        const MCMechSpecialInfo& info = tree->SpecialInfo;
        // The special frames a gesture starts at, per gesture (-1: none).
        auto setSpecialStart = [this](uint32_t frame)
        {
            if (frame != NoFrame)
            {
                FallStartPending = 1;
                FallStartFrame = static_cast<int32_t>(frame);
            }
        };

        auto clearCombat = [this]()
        {
            CombatModeChanged = 0;
            InCombatMode = false;
            CombatModeRaising = false;
            CombatModeLowering = false;
        };

        switch (CurrentGesture)
        {
            case 0:
            case 1:
            case 2:
            {
                BodyTurnLocked = false;
                UpperBodyLocked = false;

                if (StandTurnPending)
                {
                    TurnAround(*mech);
                    StandTurnPending = false;
                    ClearRotations(*mech);
                }
                break;
            }
            case 4:
            {
                StrideGesture = 1;
                BodyTurnLocked = false;
                UpperBodyLocked = false;

                if (prevGesture == 3 && info.SWToWalkFrame != NoFrame)
                {
                    startFrame = static_cast<int32_t>(info.SWToWalkFrame);
                }
                break;
            }
            case 7:
            {
                StrideGesture = 1;
                BodyTurnLocked = false;
                UpperBodyLocked = false;

                if (info.SpecialDuaneFlag != 0 && prevGesture == 6)
                {
                    startFrame = static_cast<int32_t>(tree->Gestures[7].NumFrames >> 1);
                }
                break;
            }
            case 9:
            {
                BodyTurnLocked = false;
                UpperBodyLocked = false;

                if (info.WalkToWSFrame != NoFrame)
                {
                    startFrame = static_cast<int32_t>(info.WalkToWSFrame);
                }
                break;
            }
            case 11:
            {
                StrideGesture = 0;
                BodyTurnLocked = false;
                UpperBodyLocked = false;
                break;
            }
            case 12:
            case 13:
            case 23:
            case 24:
            {
                BodyTurnLocked = true;
                UpperBodyLocked = true;
                clearCombat();
                break;
            }
            case 14:
            case 15:
            {
                if (FallStartPending != 0)
                {
                    startFrame = FallStartFrame;
                }

                if (info.ReallyStupidJamieReverseFlag != 0)
                {
                    FallTurnPending = true;
                }

                FallStartPending = 0;
                BodyTurnLocked = true;
                UpperBodyLocked = true;
                ClearRotations(*mech);
                clearCombat();
                break;
            }
            case 16:
            case 18:
            {
                setSpecialStart(CurrentGesture == 16 ? info.RFbWFbFrame : info.SFbWFbFrame);

                if (info.ReallyStupidJamieReverseFlag != 0)
                {
                    StandTurnPending = true;
                }

                ClearRotations(*mech);
                BodyTurnLocked = true;
                UpperBodyLocked = true;
                clearCombat();
                break;
            }
            case 17:
            case 19:
            {
                setSpecialStart(CurrentGesture == 17 ? info.RFfWFfFrame : info.SFfWFfFrame);

                if (info.ReallyStupidJamieReverseFlag != 0)
                {
                    FallTurnPending = true;
                }

                ClearRotations(*mech);
                BodyTurnLocked = true;
                UpperBodyLocked = true;
                clearCombat();
                break;
            }
            case GestureJump:
            {
                InJump = true;
                JumpSetup = false;

                if (mech->GetPilot()->CurTacOrder.Code == MCTacticalOrderCode::JumpToPoint)
                {
                    mech->GetPilot()->CurTacOrder.Stage = 2;
                }

                // Head for the goal: the distance, and the direction in jumpDirection.
                const MCVector3D position = mech->GetPosition();
                JumpDirection.X = JumpGoal.X - position.X;
                JumpDirection.Y = JumpGoal.Y - position.Y;
                JumpDirection.Z = JumpGoal.Z - position.Z;
                const double length = std::sqrt((static_cast<double>(JumpDirection.X) * JumpDirection.X +
                                                 static_cast<double>(JumpDirection.Y) * JumpDirection.Y) +
                                                static_cast<double>(JumpDirection.Z) * JumpDirection.Z);
                const float pathLength = static_cast<float>(length);

                if (length < 0.0 || length > 0.0)
                {
                    JumpDirection.X = static_cast<float>(JumpDirection.X / length);
                    JumpDirection.Y = static_cast<float>(JumpDirection.Y / length);
                    JumpDirection.Z = static_cast<float>(JumpDirection.Z / length);
                }

                const float distance = static_cast<float>(pathLength * 0.3 + pathLength);

                HeightsGesture = GestureJump;
                const std::span<const float> heights = FrameHeights();
                const MCGestureData& jump = tree->Gestures[GestureJump];
                // Port fix: the original reads a missing height table.
                const MCJumpFrames air =
                    !heights.empty() ? MCFindJumpFrames(heights.first(std::min<size_t>(heights.size(), jump.NumFrames)))
                                     : MCJumpFrames{};
                LiftOffFrame = air.LiftOff;
                TouchDownFrame = air.TouchDown;

                const int32_t airFrames = (TouchDownFrame - LiftOffFrame) + 5;
                const double speed =
                    (distance / (static_cast<double>(airFrames) / jump.FrameRate)) * MetersPerWorldUnit;
                JumpSpeed = static_cast<float>(speed);
                JumpParameter = static_cast<float>(distance / speed);
                mech->CreateJumpFX();
                BodyTurnLocked = false;
                UpperBodyLocked = false;
                break;
            }

            case GestureFall:
            {
                FallTurnPending = true;
                HeightsGesture = GestureFall;
                BodyTurnLocked = true;
                UpperBodyLocked = true;

                if (info.OtherJamieReverseFlag != 0 || info.ReallyStupidJamieReverseFlag != 0)
                {
                    FallTurnPending = false;
                }

                if (FallTurnPending && info.StupidJamieReverseFlag != 0)
                {
                    TurnAround(*mech);
                    FallTurnPending = false;
                    ClearRotations(*mech);
                }

                clearCombat();
                break;
            }

            case 22:
            {
                if (FallTurnPending && info.StupidJamieReverseFlag == 0)
                {
                    TurnAround(*mech);
                    FallTurnPending = false;
                    ClearRotations(*mech);
                }

                BodyTurnLocked = true;
                UpperBodyLocked = true;
                clearCombat();

                if (tree->SpecialInfo.ReallyStupidJamieReverseFlag != 0)
                {
                    StandTurnPending = true;
                }
                break;
            }
            case 3:
            case 5:
            case 6:
            case 8:
            case 10:
            {
                BodyTurnLocked = false;
                UpperBodyLocked = false;
                break;
            }
            default:
                break;
        }

        GestureDone = false;

        for (int32_t i = 0; i < MechPartCount; i++)
        {
            CurrentFrame[i] = startFrame;
        }
    }

    // The gun going up starts the upper body at the change gesture's start, going down at its end.
    if (CombatModeChanged != 0 && (CombatModeRaising || CombatModeLowering))
    {
        const int32_t frame =
            CombatModeRaising ? 0 : static_cast<int32_t>(tree->Gestures[GestureCombatChange].NumFrames) - 1;
        CurrentFrame[MCMechPart::LeftArm] = frame;
        CurrentFrame[MCMechPart::RightArm] = frame;
        CurrentFrame[MCMechPart::Torso] = frame;
        CombatModeChanged = 0;
    }

    FrameRate[MCMechPart::Legs] = tree->Gestures[CurrentGesture].FrameRate;

    if (FrameRate[MCMechPart::Legs] < 0.0)
    {
        FrameRate[MCMechPart::Legs] = -FrameRate[MCMechPart::Legs];
    }

    // Each part's mirroring and rate at its facing (port-only, HDTurns: as drawn).
    const float facing = *TurnedFacing;
    const float torso = mech->TorsoRotation;
    const float rotation3 = mech->RightArmRotation + facing + torso;
    const float rotation2 = facing + mech->LeftArmRotation + torso;
    tree->SetGesture(CurrentGesture, 0, facing, Reverse[MCMechPart::Legs], FrameRate[MCMechPart::Legs]);
    const int32_t upperGesture = InCombatMode                                ? GestureCombat
                                 : (CombatModeRaising || CombatModeLowering) ? GestureCombatChange
                                                                             : CurrentGesture;
    tree->SetGesture(upperGesture, 1, facing + torso, Reverse[MCMechPart::Torso], FrameRate[MCMechPart::Torso]);

    if (!LeftArmGone)
    {
        tree->SetGesture(upperGesture, 3, rotation3, Reverse[MCMechPart::LeftArm], FrameRate[MCMechPart::LeftArm]);
    }

    if (!RightArmGone)
    {
        tree->SetGesture(upperGesture, 2, rotation2, Reverse[MCMechPart::RightArm], FrameRate[MCMechPart::RightArm]);
    }

    const int32_t gesture = CurrentGesture;

    if (gesture == GestureJump)
    {
        if (LiftOffFrame <= CurrentFrame[MCMechPart::Legs])
        {
            JumpVelocity = JumpSpeed;
            Airborne = true;
        }

        if (TouchDownFrame <= CurrentFrame[MCMechPart::Legs] && Airborne)
        {
            JumpVelocity = 0.0f;
            Airborne = false;
        }
    }

    LyingStill = false;

    if (CurrentFrame[MCMechPart::Legs] == -1)
    {
        CurrentFrame[MCMechPart::Legs] = 0;

        if (!InCombatMode && !CombatModeRaising && !CombatModeLowering)
        {
            CurrentFrame[MCMechPart::Torso] = 0;
            CurrentFrame[MCMechPart::RightArm] = 0;
            CurrentFrame[MCMechPart::LeftArm] = 0;
        }
    }
    else if (!InTransition && (gesture == 0x17 || gesture == 0x18))
    {
        // Lying down: hold the last frame.
        const int32_t last = static_cast<int32_t>(tree->Gestures[gesture].NumFrames) - 1;

        for (int32_t i = 0; i < MechPartCount; i++)
        {
            CurrentFrame[i] = last;
        }

        LyingStill = true;
    }
    else
    {
        // The frames to step: from the legs' frame rate, or one at a time in single-step mode.
        int32_t step = 0;

        if (SingleStepMode == 0)
        {
            // Port-only (MechBraking): at the rate the stride and stop are fitted with (StrideRate, else 1).
            const float legsRate = FrameRate[MCMechPart::Legs] * StrideRate;
            const double interval = 1.0 / legsRate;
            const double time = static_cast<double>(FrameLength) + CurrentTime[MCMechPart::Legs];
            CurrentTime[MCMechPart::Legs] = static_cast<float>(time);

            if (interval <= time)
            {
                step = static_cast<int32_t>(time * legsRate);
                // Original behaviour (OB-055): the time left is time / step - interval, not time - step * interval.
                CurrentTime[MCMechPart::Legs] = static_cast<float>(time / step - interval);
            }

            // Port-only (MechBraking): a planned stop's legs land on the frame their gesture gives way at, not past it.
            if (PlannedStop && InTransition && step > 1)
            {
                const int32_t until = StopStepFrame(*tree, CurrentGesture, NextGesture);
                const int32_t legsFrame = CurrentFrame[MCMechPart::Legs];
                step = legsFrame < until ? std::min(step, until - legsFrame) : step;
            }

            // The footstep of the fall-down gestures.
            if (CurrentFrame[MCMechPart::Legs] == 10 && (gesture == 0xe || gesture == 0xf))
            {
                SoundSystem()->PlayDigitalSample(0x1d, 1, *mech, false, false);
            }
        }
        else if (NextStep != 0)
        {
            step = 1;
        }
        else if (PrevStep != 0)
        {
            step = -1;
        }

        for (int32_t i = 0; i < MechPartCount; i++)
        {
            const MCMechPart part = PartOrder[i];

            // The upper body plays the gun change on its own.
            if (part != MCMechPart::Legs)
            {
                if (CombatModeRaising)
                {
                    CurrentFrame[part] += step;

                    if (static_cast<int32_t>(tree->Gestures[GestureCombatChange].NumFrames) <= CurrentFrame[part])
                    {
                        CurrentFrame[MCMechPart::Torso] = 0;
                        CurrentFrame[MCMechPart::RightArm] = 0;
                        CurrentFrame[MCMechPart::LeftArm] = 0;
                        CombatModeRaising = false;
                        InCombatMode = true;
                    }

                    continue;
                }

                if (CombatModeLowering)
                {
                    const int32_t frame = CurrentFrame[part] - step;
                    CurrentFrame[part] = frame;

                    if (frame < 1)
                    {
                        const int32_t legsFrame = CurrentFrame[MCMechPart::Legs];
                        CombatModeLowering = false;
                        CurrentFrame[MCMechPart::Torso] = legsFrame;
                        CurrentFrame[MCMechPart::RightArm] = legsFrame;
                        CurrentFrame[MCMechPart::LeftArm] = legsFrame;
                        InCombatMode = false;
                    }

                    continue;
                }

                if (InCombatMode)
                {
                    continue;
                }
            }

            if (step == 0)
            {
                continue;
            }

            const int32_t delta = PlayBackwards ? -step : step;
            CurrentFrame[part] += delta;
            const uint32_t numFrames = tree->Gestures[CurrentGesture].NumFrames;
            // Port-only (MechBraking): in a planned stop the legs alone move the gestures on (a part half a stride off
            // them, mirrored, would cut their stride short); the other parts loop.
            const bool leads = !PlannedStop || part == MCMechPart::Legs;

            if (static_cast<int32_t>(numFrames) <= CurrentFrame[part])
            {
                if (!InTransition || !leads)
                {
                    // Loop.
                    CurrentFrame[part] = static_cast<int32_t>(static_cast<uint32_t>(CurrentFrame[part]) % numFrames);

                    if (CurrentGesture == GestureJump)
                    {
                        GestureDone = false;
                    }
                }
                else
                {
                    // Played out: the transition goes on.
                    GestureDone = true;
                    CurrentFrame[part] = static_cast<int32_t>(numFrames) - 1;

                    if (CurrentGesture == GestureJump)
                    {
                        InJump = false;
                        CurrentStateGesture = 1;

                        if (mech->GetPilot()->CurTacOrder.Code == MCTacticalOrderCode::JumpToPoint)
                        {
                            mech->GetPilot()->CurTacOrder.Stage = 3;
                        }
                    }
                }
            }

            // Some transitions go on from a special frame instead of the gesture's end.
            const int32_t inTransition = InTransition && leads;

            if (inTransition != 0)
            {
                const MCMechSpecialInfo& info = tree->SpecialInfo;
                const int32_t frame = CurrentFrame[part];

                if (CurrentGesture == 4 && NextGesture == 6)
                {
                    GestureDone = static_cast<int32_t>(info.WalkToWRFrame) <= frame;
                }

                if (CurrentGesture == 4 && NextGesture == 5 && info.WalkToWSFrame != NoFrame)
                {
                    GestureDone = static_cast<int32_t>(info.WalkToWSFrame) <= frame;
                }

                // Port-only (MechBraking): the expansion's mechs leave the frame 0, and braking, their run plays out.
                const std::optional<uint32_t> runToWalk = MCRunToWalkFrame(*tree, MCMechBrakingInForce());

                if (NextGesture == 8 && CurrentGesture == 7 && runToWalk.has_value())
                {
                    GestureDone = static_cast<int32_t>(*runToWalk) <= frame;
                }
            }

            if (CurrentFrame[part] < 0)
            {
                if (inTransition == 0)
                {
                    // Backwards past the start: wrap to the end.
                    const int32_t frame = CurrentFrame[part];
                    const uint32_t magnitude = static_cast<uint32_t>(frame < 0 ? -frame : frame);
                    const uint32_t count = tree->Gestures[CurrentGesture].NumFrames;
                    CurrentFrame[part] = static_cast<int32_t>(count - magnitude % count);
                }
                else
                {
                    GestureDone = true;
                    CurrentFrame[part] = 0;
                }
            }
        }
    }

    // Legs first, then the arm on the far side, the torso and the near arm.
    PartOrder[0] = MCMechPart::Legs;
    PartOrder[1] = MCMechPart::RightArm;
    PartOrder[2] = MCMechPart::Torso;
    PartOrder[3] = MCMechPart::LeftArm;

    if (rotation3 < 0.0)
    {
        PartOrder[1] = MCMechPart::LeftArm;
        PartOrder[3] = MCMechPart::RightArm;
    }

    if (CurrentGesture == GestureFall)
    {
        PartOrder[1] = MCMechPart::LeftArm;
        PartOrder[3] = MCMechPart::RightArm;
    }

    if (Visible && mech->IsCaptured() != 0 && !Highlighted && !Highlighting)
    {
        Highlighting = true;
        HighlightTime = 3.0f;
    }

    StepDrawnStride(*mech);
    return 1;
}

MCMechActor::~MCMechActor()
{
    if (MechTree != nullptr)
    {
        AppearanceTypeList().RemoveAppearance(*MechTree);
    }
}

auto MCMechActor::DrawBars() -> void
{
    // Port: the bar is an overlay, on the screen over the view: it follows the sprite through the zoom, its size
    // doesn't change.
    constexpr float barWidth = 38.0f;
    constexpr float barHeight = 4.0f;
    const float barY = (MCOverlayY(UpperLeft.Y) - 24.0f) - barHeight;
    const float barX = static_cast<float>(std::floor(static_cast<double>(MCOverlayX(ScreenPos.X) - barWidth * 0.5f)));

    const auto mech = std::static_pointer_cast<MCMover>(Owner.lock());

    if (mech == nullptr || mech->WeaponEffectiveness < 0.0f || mech->MaxWeaponEffectiveness < mech->WeaponEffectiveness)
    {
        return;
    }

    const float health = mech->GetTotalEffectiveness();
    // Green, then yellow below half, red at a fifth.
    int32_t barColor;

    if (health < 0.5)
    {
        barColor = health <= 0.2 ? 0x103 : 0x102;
    }
    else
    {
        barColor = 0x101;
    }

    ElementList().OpenGroup(-50000, true);
    MCPolyElementData data;
    data.NumVertices = 0;
    data.TextureMapOff = false;
    data.Texture = {};
    data.FadeTable = std::nullopt;
    data.Translate = false;
    data.StatusBar = true;
    data.BarColor = barColor;
    auto floorInt = [](float value)
    {
        return static_cast<int32_t>(static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(value)))));
    };

    data.Vertices[0].X = floorInt(barX - 1.0f);
    data.Vertices[0].Y = floorInt(barY - 1.0f);
    data.Vertices[1].X = floorInt(barX + barWidth + 1.0f);
    data.Vertices[1].Y = floorInt(barY + barHeight + 1.0f);
    const int32_t barLength = floorInt(health * barWidth);
    data.BarPercent = barLength;
    data.MeasuredBar = health * barWidth;

    // A mech that isn't quite dead shows at least one pixel.
    if (static_cast<double>(barLength) < 1.0 && health > 0.001)
    {
        data.BarPercent = 1;
    }

    if (mech->IsDisabled() == 0 && mech->IsDestroyed() == 0)
    {
        ElementList().Add(ElementList().Make<MCPolygonElement>(data, -50000));
    }
}
