#include "stdafx.h"
#include "sprite/MCMechStops.h"
#include "network/MCMultiPlayer.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteTree.h"
#include "vfx/MCVfxFunctions.h"

int GMechBraking = 0;

namespace
{
    /// <summary>The number of gestures in a state's row of the transition table (MCMechActor's TransitionRow).</summary>
    constexpr int32_t TransitionRow = 10;
    /// <summary>The goals in a state's span of the table (MCMechActor's).</summary>
    constexpr int32_t GoalsPerState = 9;
    /// <summary>A speed below this is none (GetVelocityMagnitude's test).</summary>
    constexpr float NoVelocity = -1999.0f;
    /// <summary>The facing a tree's art is looked up at, degrees.</summary>
    constexpr float ArtFacing = 0.0f;

    /// <summary>A transition's gestures, -1 ending them.</summary>
    using StopSequence = std::array<int8_t, 5>;
    constexpr StopSequence WalkToStand = {MCStopGesture::WalkToStand, MCStopGesture::Stand, -1, -1, -1};
    constexpr StopSequence RunToStand = {MCStopGesture::RunToWalk, MCStopGesture::Walk, MCStopGesture::WalkToStand,
                                         MCStopGesture::Stand, -1};
    constexpr StopSequence RunToStandWithoutArt = {MCStopGesture::Walk, MCStopGesture::WalkToStand,
                                                   MCStopGesture::Stand, -1, -1};
    constexpr StopSequence RunToWalk = {MCStopGesture::RunToWalk, MCStopGesture::Walk, -1, -1, -1};

} // namespace

auto MCFrameSpeed(MCGestureVelocities velocities, uint32_t numFrames, int32_t frame) -> float
{
    if (velocities.Start == velocities.End)
    {
        return velocities.Start >= NoVelocity ? velocities.Start : 0.0f;
    }

    if (velocities.Start < NoVelocity || velocities.End < NoVelocity)
    {
        return 0.0f;
    }

    // From the start speed to the end one over the gesture.
    const double framesLeft = static_cast<double>(numFrames - static_cast<uint32_t>(frame));
    const double t = 1.0 - framesLeft / static_cast<double>(static_cast<int32_t>(numFrames));
    return static_cast<float>((static_cast<double>(velocities.End) - static_cast<double>(velocities.Start)) * t +
                              velocities.Start);
}

auto MCMechBrakingInForce() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr ? (game->MatchRules & MCMatchRule::MechBraking) != 0 : GMechBraking != 0;
}

auto MCPlanStride(float framesLeft, int32_t strideFrames, float metresPerFrame, float metres)
    -> std::optional<MCStridePlan>
{
    if (!(metresPerFrame > 0.0f) || !(metres > 0.0f) || strideFrames <= 0 || !(framesLeft >= 0.0f))
    {
        return std::nullopt;
    }

    // At rate scale r a frame covers metresPerFrame / r, so (framesLeft + strides * strideFrames) / r frames' worth
    // at its own rate covers metres.
    const double framesThere = static_cast<double>(metres) / metresPerFrame;
    const double strides = (framesThere - framesLeft) / strideFrames;
    std::optional<MCStridePlan> best;

    for (const double whole : {std::floor(strides), std::ceil(strides)})
    {
        if (whole < 0.0)
        {
            continue;
        }

        const double rate = (framesLeft + whole * strideFrames) / framesThere;

        if (rate < MinStrideRate || rate > MaxStrideRate)
        {
            continue;
        }

        if (!best.has_value() || std::fabs(rate - 1.0) < std::fabs(best->Rate - 1.0))
        {
            best = MCStridePlan{static_cast<float>(rate), static_cast<int32_t>(whole)};
        }
    }

    return best;
}

auto MCStopVelocities(const MCSpriteTree& tree, int32_t gesture, bool braking) -> MCGestureVelocities
{
    const MCGestureData& data = tree.Gestures[static_cast<size_t>(gesture)];
    MCGestureVelocities velocities{data.StartVelocity, data.EndVelocity};

    if (!braking || velocities.Start != velocities.End)
    {
        return velocities;
    }

    if (gesture == MCStopGesture::WalkToStand)
    {
        velocities.End = 0.0f;
    }
    else if (gesture == MCStopGesture::RunToWalk && MCStopGesture::Walk < tree.NumGestures)
    {
        velocities.End = tree.Gestures[MCStopGesture::Walk].StartVelocity;
    }

    return velocities;
}

auto MCGestureMetres(const MCSpriteTree& tree, int32_t gesture, float fromFrame, bool braking) -> float
{
    if (gesture < 0 || gesture >= tree.NumGestures)
    {
        return 0.0f;
    }

    const MCGestureData& data = tree.Gestures[static_cast<size_t>(gesture)];
    const float rate = std::fabs(data.FrameRate);

    if (data.NumFrames == 0 || !(rate > 0.0f))
    {
        return 0.0f;
    }

    const MCGestureVelocities velocities = MCStopVelocities(tree, gesture, braking);
    const float from = std::clamp(fromFrame, 0.0f, static_cast<float>(data.NumFrames));
    const auto first = static_cast<int32_t>(std::floor(from));
    double metres = 0.0;

    for (auto frame = first; frame < static_cast<int32_t>(data.NumFrames); ++frame)
    {
        const double part = frame == first ? 1.0 - (from - static_cast<float>(first)) : 1.0;
        metres += part * MCFrameSpeed(velocities, data.NumFrames, frame) / rate;
    }

    return static_cast<float>(metres);
}

auto MCHasGestureArt(MCSpriteTree& tree, int32_t gesture) -> bool
{
    if (gesture < 0 || gesture >= tree.NumGestures || tree.Gestures[static_cast<size_t>(gesture)].NumFrames == 0)
    {
        return false;
    }

    const auto frames = static_cast<int32_t>(tree.Gestures[static_cast<size_t>(gesture)].NumFrames);

    for (int32_t part = 0; part < MechPartCount; ++part)
    {
        int32_t reverse = 0;
        float frameRate = 0.0f;
        const std::shared_ptr<MCShape> shape = tree.GetGesture(gesture, part, ArtFacing, reverse, frameRate, true);

        if (shape == nullptr || !VfxIsShapeTable(shape->FrameList()) || VfxShapeCount(shape->FrameList()) < frames)
        {
            return false;
        }
    }

    return true;
}

auto MCStopTransition(MCSpriteTree& tree, int32_t goal, int32_t state, int32_t step, bool braking) -> int32_t
{
    MCOptionalRef<const StopSequence> sequence;

    if (braking && goal == MCStopState::Stand && state == MCStopState::Walk &&
        MCHasGestureArt(tree, MCStopGesture::WalkToStand))
    {
        sequence = WalkToStand;
    }
    else if (braking && goal == MCStopState::Stand && state == MCStopState::Run &&
             MCHasGestureArt(tree, MCStopGesture::WalkToStand))
    {
        sequence =
            MCHasGestureArt(tree, MCStopGesture::RunToWalk) ? std::cref(RunToStand) : std::cref(RunToStandWithoutArt);
    }
    else if (braking && goal == MCStopState::Walk && state == MCStopState::Run &&
             MCHasGestureArt(tree, MCStopGesture::RunToWalk))
    {
        sequence = RunToWalk;
    }

    if (!sequence)
    {
        return tree.Transition(step + (goal + state * GoalsPerState) * TransitionRow);
    }

    return step >= 0 && step < std::ssize(sequence->get()) ? sequence->get()[static_cast<size_t>(step)] : -1;
}

auto MCRunToWalkFrame(const MCSpriteTree& tree, bool braking) -> std::optional<uint32_t>
{
    const uint32_t frame = tree.SpecialInfo.RunToRWFrame;

    if (braking && frame == 0)
    {
        return std::nullopt;
    }

    return frame;
}
