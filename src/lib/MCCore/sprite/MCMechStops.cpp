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
    /// <summary>A whole turn, degrees.</summary>
    constexpr float FullTurnDegrees = 360.0f;

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

    if (const auto known = tree.GestureArtDrawn.find(gesture); known != tree.GestureArtDrawn.end())
    {
        return known->second;
    }

    const auto frames = static_cast<int32_t>(tree.Gestures[static_cast<size_t>(gesture)].NumFrames);
    bool drawn = true;

    for (int32_t part = 0; part < MechPartCount && drawn; ++part)
    {
        int32_t reverse = 0;
        float frameRate = 0.0f;
        const std::shared_ptr<MCShape> shape = tree.GetGesture(gesture, part, ArtFacing, reverse, frameRate, true);
        drawn = shape != nullptr && VfxIsShapeTable(shape->FrameList()) && VfxShapeCount(shape->FrameList()) >= frames;

        // Its frames draw something (ShadowCat's and FireFalcon's run to walk draw nothing at all).
        bool draws = false;

        for (int32_t frame = 0; drawn && !draws && frame < frames; ++frame)
        {
            VfxShapeVisit(shape->FrameList(), frame, [&](int32_t, int32_t, uint8_t) { draws = true; });
        }

        drawn = drawn && draws;
    }

    tree.GestureArtDrawn.emplace(gesture, drawn);
    return drawn;
}

namespace
{
    /// <summary>The facings a made run to walk is compared at: the legs' sixteen, as a mech turns through them.</summary>
    constexpr int32_t CutFacings = 16;
    /// <summary>The parts it is compared by (the tree's numbering): the legs and torso, which step with the stride.</summary>
    constexpr std::array<int32_t, 2> CutParts = {0, 1};
    /// <summary>The bits of a word of a frame's rows.</summary>
    constexpr int32_t RowWordBits = 64;

    /// <summary>A frame's pixels as bits, row by row over a box shared by the frames compared with it.</summary>
    using FrameBits = std::vector<uint64_t>;

    /// <summary>A gesture's frames' drawn pixels, as offsets from the hot spot.</summary>
    using FramePixels = std::vector<std::vector<std::pair<int32_t, int32_t>>>;

    /// <summary>The pixels each of the <paramref name="frames"/> frames of <paramref name="shape"/> draws.</summary>
    auto PixelsOf(const MCShape& shape, uint32_t frames) -> FramePixels
    {
        FramePixels pixels(frames);

        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            VfxShapeVisit(shape.FrameList(), static_cast<int32_t>(frame),
                          [&](int32_t x, int32_t y, uint8_t) { pixels[frame].emplace_back(x, y); });
        }

        return pixels;
    }
} // namespace

auto MCMakeRunToWalk(MCSpriteTree& tree) -> std::optional<MCMadeRunToWalk>
{
    if (MCStopGesture::Run >= tree.NumGestures)
    {
        return std::nullopt;
    }

    const uint32_t runFrames = tree.Gestures[MCStopGesture::Run].NumFrames;
    const uint32_t walkFrames = tree.Gestures[MCStopGesture::Walk].NumFrames;

    if (runFrames == 0 || walkFrames == 0)
    {
        return std::nullopt;
    }

    // Shared and either pixels of each run frame and walk frame, over every facing and part: likeness as their share.
    std::vector<int64_t> shared(static_cast<size_t>(runFrames) * walkFrames);
    std::vector<int64_t> either(shared.size());

    for (int32_t facing = 0; facing < CutFacings; ++facing)
    {
        const float rotation = FullTurnDegrees * static_cast<float>(facing) / CutFacings;

        for (const int32_t part : CutParts)
        {
            int32_t reverse = 0;
            float frameRate = 0.0f;
            const std::shared_ptr<MCShape> run =
                tree.GetGesture(MCStopGesture::Run, part, rotation, reverse, frameRate, true);
            const std::shared_ptr<MCShape> walk =
                tree.GetGesture(MCStopGesture::Walk, part, rotation, reverse, frameRate, true);

            if (run == nullptr || walk == nullptr ||
                VfxShapeCount(run->FrameList()) < static_cast<int32_t>(runFrames) ||
                VfxShapeCount(walk->FrameList()) < static_cast<int32_t>(walkFrames))
            {
                return std::nullopt;
            }

            const FramePixels runPixels = PixelsOf(*run, runFrames);
            const FramePixels walkPixels = PixelsOf(*walk, walkFrames);

            // The box every frame of both lies in.
            int32_t left = std::numeric_limits<int32_t>::max();
            int32_t top = std::numeric_limits<int32_t>::max();
            int32_t right = std::numeric_limits<int32_t>::min();
            int32_t bottom = std::numeric_limits<int32_t>::min();

            for (const FramePixels* gesture : {&runPixels, &walkPixels})
            {
                for (const auto& frame : *gesture)
                {
                    for (const auto& [x, y] : frame)
                    {
                        left = std::min(left, x);
                        top = std::min(top, y);
                        right = std::max(right, x);
                        bottom = std::max(bottom, y);
                    }
                }
            }

            if (left > right)
            {
                continue;
            }

            const int32_t rowWords = (right - left) / RowWordBits + 1;
            const auto words = static_cast<size_t>(rowWords) * static_cast<size_t>(bottom - top + 1);
            const auto bitsOf = [&](const FramePixels& gesture)
            {
                std::vector<FrameBits> bits(gesture.size(), FrameBits(words));

                for (size_t frame = 0; frame < gesture.size(); ++frame)
                {
                    for (const auto& [x, y] : gesture[frame])
                    {
                        const int32_t column = x - left;
                        bits[frame][static_cast<size_t>((y - top) * rowWords + column / RowWordBits)] |=
                            uint64_t{1} << (column % RowWordBits);
                    }
                }

                return bits;
            };

            const std::vector<FrameBits> runBits = bitsOf(runPixels);
            const std::vector<FrameBits> walkBits = bitsOf(walkPixels);

            for (uint32_t r = 0; r < runFrames; ++r)
            {
                for (uint32_t w = 0; w < walkFrames; ++w)
                {
                    const size_t at = static_cast<size_t>(r) * walkFrames + w;

                    for (size_t word = 0; word < words; ++word)
                    {
                        shared[at] += std::popcount(runBits[r][word] & walkBits[w][word]);
                        either[at] += std::popcount(runBits[r][word] | walkBits[w][word]);
                    }
                }
            }
        }
    }

    // The most alike, the first of equals (the counts compared exactly, so every machine picks the same).
    std::optional<size_t> best;

    for (size_t at = 0; at < shared.size(); ++at)
    {
        if (either[at] > 0 && (!best.has_value() || shared[at] * either[*best] > shared[*best] * either[at]))
        {
            best = at;
        }
    }

    if (!best.has_value())
    {
        return std::nullopt;
    }

    return MCMadeRunToWalk{static_cast<uint32_t>(*best / walkFrames),
                           static_cast<uint32_t>((*best % walkFrames + 1) % walkFrames)};
}

auto MCRunToWalkMade(MCSpriteTree& tree, bool braking) -> std::optional<MCMadeRunToWalk>
{
    if (!braking || MCHasGestureArt(tree, MCStopGesture::RunToWalk))
    {
        return std::nullopt;
    }

    if (!tree.MadeRunToWalkKnown)
    {
        tree.MadeRunToWalk = MCMakeRunToWalk(tree);
        tree.MadeRunToWalkKnown = true;
    }

    return tree.MadeRunToWalk;
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
