#include "stdafx.h"
#include "sprite/MCMechStops.h"
#include "lib/MCWorkers.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCFrameLog.h"
#include "sprite/MCMechStopsKnown.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "vfx/MCVfxFunctions.h"
#include "vfx/MCVfxShapeRuns.h"
// xxHash, header-only: its functions inline in this file alone.
#define XXH_INLINE_ALL
#include "xxhash.h"

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

auto MCMechBrakingVersion() -> int32_t
{
    if (!MCMechBrakingInForce())
    {
        return 0;
    }

    // Oldest first: the last whose rules the match plays (every one outside a match).
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    int32_t version = 0;

    for (const MCValueVersion& each : MCOptionVersions::BrakingOn)
    {
        version = game == nullptr || (game->MatchRules & each.Rules) == each.Rules ? each.Version : version;
    }

    return version;
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

namespace
{
    /// <summary>The facings the stopping art is compared at: the legs' sixteen, as a mech turns through them.</summary>
    constexpr int32_t CutFacings = 16;
    /// <summary>The parts it is compared by (the tree's numbering): the legs and torso, which step with the stride.</summary>
    constexpr std::array<int32_t, 2> CutParts = {0, 1};
    /// <summary>The bits of a word of a frame's rows.</summary>
    constexpr int32_t RowWordBits = 64;
    /// <summary>The run to walk's place in <see cref="MCTreeStops::Drawn"/> (<see cref="MCStopArt"/>).</summary>
    constexpr size_t RunToWalkArt = 2;
    /// <summary>The gestures compared: the walk, the walk to stand and the run, in <see cref="StopShapes"/>' order.</summary>
    constexpr std::array<int32_t, 3> Compared = {MCStopGesture::Walk, MCStopGesture::WalkToStand, MCStopGesture::Run};
    constexpr size_t ComparedWalk = 0;
    constexpr size_t ComparedStop = 1;
    constexpr size_t ComparedRun = 2;
    /// <summary>The pairs compared, as <see cref="Compared"/>'s: the walk and the run to the stop, the run to the walk.</summary>
    constexpr std::array<std::pair<size_t, size_t>, 3> ComparedPairs = {
        {{ComparedWalk, ComparedStop}, {ComparedRun, ComparedStop}, {ComparedRun, ComparedWalk}}};
    /// <summary>What the stops' art hash is worked out as (a change to it changes every hash, and the table's).</summary>
    constexpr uint32_t StopsHashVersion = 1;

    /// <summary>Facing <paramref name="facing"/> of the <see cref="CutFacings"/>, degrees.</summary>
    auto CutRotation(int32_t facing) -> float
    {
        return FullTurnDegrees * static_cast<float>(facing) / CutFacings;
    }

    /// <summary>Whether gesture <paramref name="gesture"/> of <paramref name="tree"/> has frames to look at.</summary>
    auto HasFrames(const MCSpriteTree& tree, int32_t gesture) -> bool
    {
        return gesture >= 0 && gesture < tree.NumGestures && tree.Gestures[static_cast<size_t>(gesture)].NumFrames != 0;
    }

    /// <summary>
    /// Whether gesture <paramref name="gesture"/> (with frames) of <paramref name="tree"/> has its art drawn, as
    /// <see cref="MCHasGestureArt"/> says, looked at now.
    /// </summary>
    auto GestureArtLooked(MCSpriteTree& tree, int32_t gesture) -> bool
    {
        const auto frames = static_cast<int32_t>(tree.Gestures[static_cast<size_t>(gesture)].NumFrames);
        bool drawn = true;

        for (int32_t part = 0; part < MechPartCount && drawn; ++part)
        {
            int32_t reverse = 0;
            float frameRate = 0.0f;
            const std::shared_ptr<MCShape> shape = tree.GetGesture(gesture, part, ArtFacing, reverse, frameRate, true);
            drawn =
                shape != nullptr && VfxIsShapeTable(shape->FrameList()) && VfxShapeCount(shape->FrameList()) >= frames;

            // Its frames draw something (ShadowCat's and FireFalcon's run to walk draw nothing at all).
            bool draws = false;

            for (int32_t frame = 0; drawn && !draws && frame < frames; ++frame)
            {
                VfxShapeVisit(shape->FrameList(), frame, [&](int32_t, int32_t, uint8_t) { draws = true; });
            }

            drawn = drawn && draws;
        }

        return drawn;
    }

    /// <summary>
    /// The stop's ways in from a stride's frames (<see cref="MCStopEntries"/>), from <paramref name="walk"/>, the walk's
    /// likeness to the walk to stand, and <paramref name="like"/>, the stride's; the walk's frame into the walk to stand
    /// <paramref name="walkToStand"/> (FIT walk_to_w_s_frame). None without both.
    /// </summary>
    auto EntriesFrom(uint32_t walkToStand, const std::optional<MCLikeness>& walk, const std::optional<MCLikeness>& like)
        -> std::vector<int32_t>
    {
        std::vector<int32_t> entries;

        if (!walk.has_value() || !like.has_value())
        {
            return entries;
        }

        const auto likeness = [](const MCLikeness& of, uint32_t first, uint32_t second)
        {
            const size_t at = static_cast<size_t>(first) * of.SecondFrames + second;
            return of.Either[at] > 0 ? static_cast<double>(of.Shared[at]) / static_cast<double>(of.Either[at]) : 0.0;
        };

        // The art's own way in: the walk's frame it gives way at (its last, without one) into the stop's first.
        const uint32_t givesWay =
            walkToStand != NoFrame && walkToStand < walk->FirstFrames ? walkToStand : walk->FirstFrames - 1;
        const double bar = likeness(*walk, givesWay, 0);
        entries.assign(like->FirstFrames, -1);

        for (uint32_t frame = 0; frame < like->FirstFrames; ++frame)
        {
            uint32_t best = 0;

            for (uint32_t stop = 1; stop < like->SecondFrames; ++stop)
            {
                best = likeness(*like, frame, stop) > likeness(*like, frame, best) ? stop : best;
            }

            if (likeness(*like, frame, best) >= bar)
            {
                entries[frame] = static_cast<int32_t>(best);
            }
        }

        return entries;
    }

    /// <summary>The run to walk made from <paramref name="likeness"/>, the run's to the walk (<see cref="MCMakeRunToWalk"/>).</summary>
    auto MadeFrom(const std::optional<MCLikeness>& likeness) -> std::optional<MCMadeRunToWalk>
    {
        if (!likeness.has_value())
        {
            return std::nullopt;
        }

        const std::vector<int64_t>& shared = likeness->Shared;
        const std::vector<int64_t>& either = likeness->Either;
        const uint32_t walkFrames = likeness->SecondFrames;
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

    /// <summary>A frame's pixels as bits, row by row over a box shared by the frames compared with it.</summary>
    using FrameBits = std::vector<uint64_t>;

    /// <summary>A frame's bits (<see cref="FrameBits"/>), the words from its first drawn one to its last, and its pixels.</summary>
    struct DrawnBits
    {
        FrameBits Bits;
        size_t First = 0;
        size_t End = 0;
        int64_t Pixels = 0;
    };

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

    /// <summary>
    /// What <see cref="MCMakeStopsFast"/> works a tree's stops out from, loaded on the game's thread (the shapes' cache is
    /// its), worked out on any (<see cref="StopsFrom"/>).
    /// </summary>
    struct StopShapes
    {
        /// <summary>Whether each of <see cref="MCStopArt"/> has frames, and how many.</summary>
        std::array<bool, 3> ArtAsked{};
        std::array<uint32_t, 3> ArtFrames{};
        /// <summary>Each of <see cref="MCStopArt"/>'s parts' shapes at the art's facing.</summary>
        std::array<std::array<std::shared_ptr<const MCShape>, MechPartCount>, 3> Art;
        /// <summary>
        /// Each of <see cref="Compared"/>'s frames, and whether it is compared (its tree has it, with frames); its
        /// parts' shapes at each of the <see cref="CutFacings"/>.
        /// </summary>
        std::array<uint32_t, 3> Frames{};
        std::array<bool, 3> InTree{};
        std::array<std::array<std::array<std::shared_ptr<const MCShape>, CutParts.size()>, CutFacings>, 3> Strides;
        /// <summary>FIT walk_to_w_s_frame.</summary>
        uint32_t WalkToStand = NoFrame;
    };

    /// <summary>Loads what <paramref name="tree"/>'s stops are worked out from (<see cref="StopShapes"/>).</summary>
    auto LoadStopShapes(MCSpriteTree& tree) -> StopShapes
    {
        StopShapes shapes;
        shapes.WalkToStand = tree.SpecialInfo.WalkToWSFrame;
        int32_t reverse = 0;
        float frameRate = 0.0f;

        for (size_t art = 0; art < MCStopArt.size(); ++art)
        {
            shapes.ArtAsked[art] = HasFrames(tree, MCStopArt[art]);

            if (!shapes.ArtAsked[art])
            {
                continue;
            }

            shapes.ArtFrames[art] = tree.Gestures[static_cast<size_t>(MCStopArt[art])].NumFrames;

            for (int32_t part = 0; part < MechPartCount; ++part)
            {
                shapes.Art[art][static_cast<size_t>(part)] =
                    tree.GetGesture(MCStopArt[art], part, ArtFacing, reverse, frameRate, true);
            }
        }

        for (size_t gesture = 0; gesture < Compared.size(); ++gesture)
        {
            shapes.InTree[gesture] = HasFrames(tree, Compared[gesture]);

            if (!shapes.InTree[gesture])
            {
                continue;
            }

            shapes.Frames[gesture] = tree.Gestures[static_cast<size_t>(Compared[gesture])].NumFrames;

            for (int32_t facing = 0; facing < CutFacings; ++facing)
            {
                for (size_t part = 0; part < CutParts.size(); ++part)
                {
                    shapes.Strides[gesture][static_cast<size_t>(facing)][part] = tree.GetGesture(
                        Compared[gesture], CutParts[part], CutRotation(facing), reverse, frameRate, true);
                }
            }
        }

        return shapes;
    }

    /// <summary>Whether <paramref name="shape"/> holds <paramref name="frames"/> frames (as MCGestureLikeness asks).</summary>
    auto HoldsFrames(const std::shared_ptr<const MCShape>& shape, uint32_t frames) -> bool
    {
        return shape != nullptr && VfxShapeCount(shape->FrameList()) >= static_cast<int32_t>(frames);
    }

    /// <summary>The box a shape's frames draw in (empty: left past right).</summary>
    struct DrawnBox
    {
        int32_t Left = std::numeric_limits<int32_t>::max();
        int32_t Top = std::numeric_limits<int32_t>::max();
        int32_t Right = std::numeric_limits<int32_t>::min();
        int32_t Bottom = std::numeric_limits<int32_t>::min();
    };

    /// <summary>Sets the bits of columns <paramref name="from"/> to <paramref name="to"/> of a row of words.</summary>
    auto SetColumns(std::span<uint64_t> row, int32_t from, int32_t to) -> void
    {
        for (int32_t word = from / RowWordBits; word <= to / RowWordBits; ++word)
        {
            const int32_t first = std::max(from, word * RowWordBits) - word * RowWordBits;
            const int32_t last = std::min(to, word * RowWordBits + RowWordBits - 1) - word * RowWordBits;
            const uint64_t upTo = last == RowWordBits - 1 ? ~uint64_t{0} : (uint64_t{1} << (last + 1)) - 1;
            row[static_cast<size_t>(word)] |= upTo & ~((uint64_t{1} << first) - 1);
        }
    }

    /// <summary>
    /// <paramref name="tree"/>'s stops from <paramref name="shapes"/> (<see cref="MCMakeStopsFast"/>), on any thread: the
    /// likeness of each pair of <see cref="ComparedPairs"/>, each shape's frames set as bits once.
    /// </summary>
    auto StopsFrom(const StopShapes& shapes) -> MCTreeStops
    {
        MCTreeStops stops;

        for (size_t art = 0; art < MCStopArt.size(); ++art)
        {
            bool drawn = shapes.ArtAsked[art];

            for (size_t part = 0; part < MechPartCount && drawn; ++part)
            {
                const std::shared_ptr<const MCShape>& shape = shapes.Art[art][part];
                drawn = shape != nullptr && VfxIsShapeTable(shape->FrameList()) &&
                        VfxShapeCount(shape->FrameList()) >= static_cast<int32_t>(shapes.ArtFrames[art]);
                bool draws = false;

                for (uint32_t frame = 0; drawn && !draws && frame < shapes.ArtFrames[art]; ++frame)
                {
                    VfxShapeRuns(shape->FrameList(), static_cast<int32_t>(frame),
                                 [&](int32_t, int32_t, int32_t, std::span<const uint8_t>) { draws = true; });
                }

                drawn = drawn && draws;
            }

            stops.Drawn[art] = drawn;
        }

        // A gesture is compared when every shape holds its frames (MCGestureLikeness's test).
        std::array<bool, 3> whole{};

        for (size_t gesture = 0; gesture < Compared.size(); ++gesture)
        {
            whole[gesture] = shapes.InTree[gesture];

            for (const auto& facing : shapes.Strides[gesture])
            {
                for (const std::shared_ptr<const MCShape>& shape : facing)
                {
                    whole[gesture] = whole[gesture] && HoldsFrames(shape, shapes.Frames[gesture]);
                }
            }
        }

        std::array<std::optional<MCLikeness>, ComparedPairs.size()> likeness;

        for (size_t pair = 0; pair < ComparedPairs.size(); ++pair)
        {
            const auto [first, second] = ComparedPairs[pair];

            if (whole[first] && whole[second])
            {
                const size_t pairs = static_cast<size_t>(shapes.Frames[first]) * shapes.Frames[second];
                likeness[pair] = MCLikeness{shapes.Frames[first], shapes.Frames[second], std::vector<int64_t>(pairs),
                                            std::vector<int64_t>(pairs)};
            }
        }

        // Each facing and part: the box every compared frame lies in (the counts don't depend on it), each frame's
        // runs set as bits, then each pair's frames' shared and either pixels counted.
        std::array<std::vector<DrawnBits>, 3> bits;

        for (int32_t facing = 0; facing < CutFacings; ++facing)
        {
            for (size_t part = 0; part < CutParts.size(); ++part)
            {
                const auto shapeOf = [&](size_t gesture) -> const MCShape&
                { return *shapes.Strides[gesture][static_cast<size_t>(facing)][part]; };
                DrawnBox box;

                for (size_t gesture = 0; gesture < Compared.size(); ++gesture)
                {
                    for (uint32_t frame = 0; whole[gesture] && frame < shapes.Frames[gesture]; ++frame)
                    {
                        VfxShapeRuns(shapeOf(gesture).FrameList(), static_cast<int32_t>(frame),
                                     [&](int32_t x, int32_t y, int32_t count, std::span<const uint8_t>)
                                     {
                                         box.Left = std::min(box.Left, x);
                                         box.Right = std::max(box.Right, x + count - 1);
                                         box.Top = std::min(box.Top, y);
                                         box.Bottom = std::max(box.Bottom, y);
                                     });
                    }
                }

                if (box.Left > box.Right)
                {
                    continue;
                }

                const int32_t rowWords = (box.Right - box.Left) / RowWordBits + 1;
                const auto words = static_cast<size_t>(rowWords) * static_cast<size_t>(box.Bottom - box.Top + 1);

                for (size_t gesture = 0; gesture < Compared.size(); ++gesture)
                {
                    bits[gesture].resize(whole[gesture] ? shapes.Frames[gesture] : 0);

                    for (uint32_t frame = 0; frame < bits[gesture].size(); ++frame)
                    {
                        DrawnBits& drawn = bits[gesture][frame];
                        drawn.Bits.assign(words, 0);
                        drawn.First = words;
                        drawn.End = 0;
                        VfxShapeRuns(
                            shapeOf(gesture).FrameList(), static_cast<int32_t>(frame),
                            [&](int32_t x, int32_t y, int32_t count, std::span<const uint8_t>)
                            {
                                const size_t row = static_cast<size_t>(y - box.Top) * static_cast<size_t>(rowWords);
                                const int32_t from = x - box.Left;
                                const int32_t to = from + count - 1;
                                SetColumns(std::span(drawn.Bits).subspan(row, static_cast<size_t>(rowWords)), from, to);
                                drawn.First = std::min(drawn.First, row + static_cast<size_t>(from / RowWordBits));
                                drawn.End = std::max(drawn.End, row + static_cast<size_t>(to / RowWordBits) + 1);
                            });
                        drawn.Pixels = 0;

                        for (size_t word = drawn.First; word < drawn.End; ++word)
                        {
                            drawn.Pixels += std::popcount(drawn.Bits[word]);
                        }
                    }
                }

                for (size_t pair = 0; pair < ComparedPairs.size(); ++pair)
                {
                    if (!likeness[pair].has_value())
                    {
                        continue;
                    }

                    const auto [first, second] = ComparedPairs[pair];
                    MCLikeness& like = *likeness[pair];

                    for (uint32_t a = 0; a < like.FirstFrames; ++a)
                    {
                        for (uint32_t b = 0; b < like.SecondFrames; ++b)
                        {
                            // Either's pixels are both frames' less those they share: only where both draw is
                            // counted.
                            const size_t at = static_cast<size_t>(a) * like.SecondFrames + b;
                            const DrawnBits& one = bits[first][a];
                            const DrawnBits& other = bits[second][b];
                            int64_t shared = 0;

                            for (size_t word = std::max(one.First, other.First); word < std::min(one.End, other.End);
                                 ++word)
                            {
                                shared += std::popcount(one.Bits[word] & other.Bits[word]);
                            }

                            like.Shared[at] += shared;
                            like.Either[at] += one.Pixels + other.Pixels - shared;
                        }
                    }
                }
            }
        }

        stops.WalkEntries = EntriesFrom(shapes.WalkToStand, likeness[0], likeness[0]);
        stops.RunEntries = EntriesFrom(shapes.WalkToStand, likeness[0], likeness[1]);
        stops.MadeRunToWalk = stops.Drawn[RunToWalkArt] ? std::nullopt : MadeFrom(likeness[2]);
        return stops;
    }

    /// <summary>Gives <paramref name="tree"/> <paramref name="stops"/>: what MCHasGestureArt, MCStopEntries and
    /// MCRunToWalkMade then read.</summary>
    auto GiveStops(MCSpriteTree& tree, const MCTreeStops& stops) -> void
    {
        for (size_t art = 0; art < MCStopArt.size(); ++art)
        {
            tree.GestureArtDrawn.insert_or_assign(MCStopArt[art], stops.Drawn[art]);
        }

        tree.StopEntries.insert_or_assign(MCStopGesture::Walk, stops.WalkEntries);
        tree.StopEntries.insert_or_assign(MCStopGesture::Run, stops.RunEntries);
        tree.MadeRunToWalk = stops.MadeRunToWalk;
        tree.MadeRunToWalkKnown = true;
    }

    /// <summary>
    /// Looks <paramref name="tree"/>'s stops up in the table by its art's hash <paramref name="hash"/> (once a tree),
    /// giving them to it; false, logged, for art the table hasn't (its stops still to be worked out).
    /// </summary>
    auto LookUpStops(MCSpriteTree& tree, uint64_t hash) -> bool
    {
        tree.StopsPrepared = true;
        tree.StopsLookedUpBy = hash;

        if (const MCOptionalRef<const MCKnownStops> known = MCFindKnownStops(hash))
        {
            GiveStops(tree, known->get().Stops);
            return true;
        }

        SDL_Log("MechBraking: the stops of %s's art (%016llx) aren't in the table: worked out now", tree.Name.c_str(),
                static_cast<unsigned long long>(hash));
        return false;
    }

    /// <summary>The bytes of <paramref name="value"/>, added to <paramref name="hashed"/>.</summary>
    template <typename T> auto AddBytes(XXH3_state_t& hashed, const T& value) -> void
    {
        static_assert(std::is_trivially_copyable_v<T>);
        XXH3_64bits_update(&hashed, &value, sizeof(T));
    }
} // namespace

auto MCHasGestureArt(MCSpriteTree& tree, int32_t gesture) -> bool
{
    if (!HasFrames(tree, gesture))
    {
        return false;
    }

    MCPrepareStops(tree);

    if (const auto known = tree.GestureArtDrawn.find(gesture); known != tree.GestureArtDrawn.end())
    {
        return known->second;
    }

    // Art braking never asks of (the table holds what it does).
    const bool drawn = GestureArtLooked(tree, gesture);
    tree.GestureArtDrawn.emplace(gesture, drawn);
    return drawn;
}

auto MCGestureLikeness(MCSpriteTree& tree, int32_t first, int32_t second) -> std::optional<MCLikeness>
{
    if (std::max(first, second) >= static_cast<int32_t>(tree.NumGestures))
    {
        return std::nullopt;
    }

    const uint32_t runFrames = tree.Gestures[static_cast<size_t>(first)].NumFrames;
    const uint32_t walkFrames = tree.Gestures[static_cast<size_t>(second)].NumFrames;

    if (runFrames == 0 || walkFrames == 0)
    {
        return std::nullopt;
    }

    // Shared and either pixels of each run frame and walk frame, over every facing and part: likeness as their share.
    std::vector<int64_t> shared(static_cast<size_t>(runFrames) * walkFrames);
    std::vector<int64_t> either(shared.size());

    for (int32_t facing = 0; facing < CutFacings; ++facing)
    {
        const float rotation = CutRotation(facing);

        for (const int32_t part : CutParts)
        {
            int32_t reverse = 0;
            float frameRate = 0.0f;
            const std::shared_ptr<MCShape> run = tree.GetGesture(first, part, rotation, reverse, frameRate, true);
            const std::shared_ptr<MCShape> walk = tree.GetGesture(second, part, rotation, reverse, frameRate, true);

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

    return MCLikeness{runFrames, walkFrames, std::move(shared), std::move(either)};
}

auto MCMakeRunToWalk(MCSpriteTree& tree) -> std::optional<MCMadeRunToWalk>
{
    return MadeFrom(MCGestureLikeness(tree, MCStopGesture::Run, MCStopGesture::Walk));
}

auto MCMakeStops(MCSpriteTree& tree) -> MCTreeStops
{
    MCTreeStops stops;

    for (size_t art = 0; art < MCStopArt.size(); ++art)
    {
        stops.Drawn[art] = HasFrames(tree, MCStopArt[art]) && GestureArtLooked(tree, MCStopArt[art]);
    }

    const std::optional<MCLikeness> walk = MCGestureLikeness(tree, MCStopGesture::Walk, MCStopGesture::WalkToStand);
    stops.WalkEntries = EntriesFrom(tree.SpecialInfo.WalkToWSFrame, walk, walk);
    stops.RunEntries = EntriesFrom(tree.SpecialInfo.WalkToWSFrame, walk,
                                   MCGestureLikeness(tree, MCStopGesture::Run, MCStopGesture::WalkToStand));
    stops.MadeRunToWalk = stops.Drawn[RunToWalkArt] ? std::nullopt : MCMakeRunToWalk(tree);
    return stops;
}

auto MCMakeStopsFast(MCSpriteTree& tree) -> MCTreeStops
{
    return StopsFrom(LoadStopShapes(tree));
}

namespace
{
    /// <summary>
    /// Adds to <paramref name="hashed"/> what MCStopsArtHash hashes of <paramref name="tree"/> before its packets' bytes,
    /// and gives the run of each part's packets it then hashes as stored (none: a part it reads none of).
    /// </summary>
    auto HashArtLayout(MCSpriteTree& tree, XXH3_state_t& hashed) -> MCMechParts<std::optional<MCStoredWanted>>
    {
        AddBytes(hashed, StopsHashVersion);
        AddBytes(hashed, tree.NumGestures);
        AddBytes(hashed, tree.SpecialInfo.WalkToWSFrame);

        for (const int32_t gesture : {MCStopGesture::Walk, MCStopGesture::WalkToStand, MCStopGesture::WalkToRun,
                                      MCStopGesture::Run, MCStopGesture::RunToWalk})
        {
            AddBytes(hashed, HasFrames(tree, gesture) ? tree.Gestures[static_cast<size_t>(gesture)].NumFrames : 0u);
        }

        // The packets each facing of the art looked at shows (MCGestureLikeness's and MCHasGestureArt's), and every
        // packet of their gestures (a facing without art borrows another's: MCSpriteTree::GetGesture), by part PAK.
        MCMechParts<std::pair<uint32_t, uint32_t>> spans;
        spans.fill({std::numeric_limits<uint32_t>::max(), 0});
        const auto shows = [&](int32_t gesture, int32_t part, float rotation)
        {
            const std::optional<MCGesturePacket> where = tree.GesturePacket(gesture, part, rotation);
            AddBytes(hashed, where.has_value());

            if (!where.has_value())
            {
                return;
            }

            AddBytes(hashed, where->FilePart);
            AddBytes(hashed, where->Packet);
            AddBytes(hashed, where->FirstPacket);
            AddBytes(hashed, where->Facings);
            AddBytes(hashed, where->FacingStep);
            AddBytes(hashed, where->Mirrored);
            auto& [from, to] = spans[where->FilePart];
            from = std::min(from, where->FirstPacket);
            to = std::max(to, where->FirstPacket + static_cast<uint32_t>(std::max(where->Facings, 0)));
        };

        for (const int32_t gesture : MCStopArt)
        {
            for (int32_t part = 0; part < MechPartCount; ++part)
            {
                shows(gesture, part, ArtFacing);
            }
        }

        for (const int32_t gesture : Compared)
        {
            for (int32_t facing = 0; facing < CutFacings; ++facing)
            {
                for (const int32_t part : CutParts)
                {
                    shows(gesture, part, CutRotation(facing));
                }
            }
        }

        MCMechParts<std::optional<MCStoredWanted>> runs;

        for (const MCMechPart part : {MCMechPart::Legs, MCMechPart::Torso, MCMechPart::RightArm, MCMechPart::LeftArm})
        {
            const auto [from, to] = spans[part];

            if (from < to && tree.FileNumbers[part] != NoFileNumber)
            {
                runs[part] = MCStoredWanted{MCShapeWanted{tree.FileNumbers[part], from, part, {}}, to - from};
            }
        }

        return runs;
    }
} // namespace

auto MCStopsArtHash(MCSpriteTree& tree) -> uint64_t
{
    return MCStopsArtHash(tree, MCStopsArtStored{});
}

auto MCStopsArtPackets(MCSpriteTree& tree) -> MCMechParts<std::optional<MCStoredWanted>>
{
    // The layout's bytes go nowhere: only the runs are wanted.
    XXH3_state_t unused;
    XXH3_64bits_reset(&unused);
    return HashArtLayout(tree, unused);
}

auto MCStopsArtHash(MCSpriteTree& tree, const MCStopsArtStored& stored) -> uint64_t
{
    XXH3_state_t hashed;
    XXH3_64bits_reset(&hashed);
    const MCMechParts<std::optional<MCStoredWanted>> runs = HashArtLayout(tree, hashed);
    const std::shared_ptr<MCSpriteManager> spriteManager = SpriteManager();

    for (const MCMechPart part : {MCMechPart::Legs, MCMechPart::Torso, MCMechPart::RightArm, MCMechPart::LeftArm})
    {
        if (!runs[part].has_value() || spriteManager == nullptr)
        {
            AddBytes(hashed, uint64_t{0});
            continue;
        }

        const MCStoredWanted& run = *runs[part];
        const std::vector<uint8_t> read =
            stored[part].has_value()
                ? std::vector<uint8_t>{}
                : spriteManager->MechPacketsStored(run.Of.FileNum, part, run.Of.Packet, run.Count, true);
        const std::vector<uint8_t>& bytes = stored[part].has_value() ? *stored[part] : read;
        AddBytes(hashed, static_cast<uint64_t>(bytes.size()));
        XXH3_64bits_update(&hashed, bytes.data(), bytes.size());
    }

    return XXH3_64bits_digest(&hashed);
}

auto MCPrepareStops(MCSpriteTree& tree) -> void
{
    if (!tree.StopsPrepared && !LookUpStops(tree, MCStopsArtHash(tree)))
    {
        GiveStops(tree, MCMakeStopsFast(tree));
    }
}

auto MCPrepareStops(std::span<const std::shared_ptr<MCSpriteTree>> trees, std::span<const MCStopsArtStored> stored)
    -> void
{
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<MCSpriteTree>> unknown;
    size_t known = 0;

    for (size_t i = 0; i < trees.size(); ++i)
    {
        const std::shared_ptr<MCSpriteTree>& tree = trees[i];

        if (tree == nullptr || tree->StopsPrepared)
        {
            continue;
        }

        if (LookUpStops(*tree, i < stored.size() ? MCStopsArtHash(*tree, stored[i]) : MCStopsArtHash(*tree)))
        {
            ++known;
        }
        else
        {
            unknown.push_back(tree);
        }
    }

    const auto note = [&]
    {
        if (MCFrameLog::Enabled() && known + unknown.size() > 0)
        {
            MCFrameLog::Note(std::format(
                "mech stops: {} types from the table, {} worked out, in {:.2f} ms", known, unknown.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()));
        }
    };

    if (unknown.empty())
    {
        note();
        return;
    }

    // Art the table hasn't: its shapes loaded here (the shapes' cache is this thread's), worked out together.
    std::vector<StopShapes> shapes;

    for (const std::shared_ptr<MCSpriteTree>& tree : unknown)
    {
        shapes.push_back(LoadStopShapes(*tree));
    }

    std::vector<MCTreeStops> stops(unknown.size());
    MCWorkers workers(std::min(unknown.size() - 1, MCWorkers::HelpersFor(unknown.size())));
    workers.Run(unknown.size(), [&](size_t tree) { stops[tree] = StopsFrom(shapes[tree]); });

    for (size_t tree = 0; tree < unknown.size(); ++tree)
    {
        GiveStops(*unknown[tree], stops[tree]);
    }

    note();
}

auto MCKnownStopsTable() -> std::span<const MCKnownStops>
{
    return MCMechStopsKnown();
}

auto MCFindKnownStops(uint64_t artHash) -> MCOptionalRef<const MCKnownStops>
{
    for (const MCKnownStops& known : MCKnownStopsTable())
    {
        if (known.ArtHash == artHash)
        {
            return known;
        }
    }

    return std::nullopt;
}

auto MCStopEntries(MCSpriteTree& tree, int32_t stride) -> const std::vector<int32_t>&
{
    MCPrepareStops(tree);

    if (const auto found = tree.StopEntries.find(stride); found != tree.StopEntries.end())
    {
        return found->second;
    }

    // A stride braking never asks for (the table holds the walk's and the run's).
    const std::optional<MCLikeness> walk = MCGestureLikeness(tree, MCStopGesture::Walk, MCStopGesture::WalkToStand);
    return tree.StopEntries[stride] = EntriesFrom(tree.SpecialInfo.WalkToWSFrame, walk,
                                                  MCGestureLikeness(tree, stride, MCStopGesture::WalkToStand));
}

auto MCRunToWalkMade(MCSpriteTree& tree, bool braking) -> std::optional<MCMadeRunToWalk>
{
    if (!braking || MCHasGestureArt(tree, MCStopGesture::RunToWalk))
    {
        return std::nullopt;
    }

    MCPrepareStops(tree);
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
