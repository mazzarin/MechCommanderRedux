#include "stdafx.h"
#include "sprite/MCSpriteTree.h"
#include "main/MCMissionGlobals.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCShape.h"

namespace
{
    /// <summary>Leg rotations per gesture.</summary>
    constexpr int32_t LegRotationCount = 9;

    /// <summary>The default gesture transition table (a tree's FIT "TransitionArray" replaces it).</summary>
    constexpr MCTransitionTable DefaultTransitions = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  2,  -1, -1, -1, -1, -1, -1, -1, -1, 1,  2,  3,  4,  -1, -1, -1, -1,
        -1, -1, 1,  2,  3,  4,  6,  7,  -1, -1, -1, -1, 1,  2,  10, 9,  -1, -1, -1, -1, -1, -1, 1,  2,  3,  11, -1, -1,
        -1, -1, -1, -1, 1,  2,  20, 2,  -1, -1, -1, -1, -1, -1, 19, 15, 23, -1, -1, -1, -1, -1, -1, -1, 18, 14, 24, -1,
        -1, -1, -1, -1, -1, -1, 1,  0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3,  4,
        -1, -1, -1, -1, -1, -1, -1, -1, 3,  4,  6,  7,  -1, -1, -1, -1, -1, -1, 10, 9,  -1, -1, -1, -1, -1, -1, -1, -1,
        3,  11, -1, -1, -1, -1, -1, -1, -1, -1, 20, 2,  -1, -1, -1, -1, -1, -1, -1, -1, 19, 15, 23, -1, -1, -1, -1, -1,
        -1, -1, 18, 14, 24, -1, -1, -1, -1, -1, -1, -1, 2,  1,  0,  -1, -1, -1, -1, -1, -1, -1, 2,  -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 6,  7,  -1, -1, -1, -1, -1, -1, -1, -1, 5,  10, 9,  -1,
        -1, -1, -1, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, 20, 2,  -1, -1, -1, -1, -1, -1, -1, -1, 15, 23,
        -1, -1, -1, -1, -1, -1, -1, -1, 14, 24, -1, -1, -1, -1, -1, -1, -1, -1, 2,  1,  0,  -1, -1, -1, -1, -1, -1, -1,
        2,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 4,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, 10, 9,  -1, -1, -1, -1, -1, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, 20, 2,  -1, -1, -1, -1,
        -1, -1, -1, -1, 17, 15, 23, -1, -1, -1, -1, -1, -1, -1, 16, 14, 24, -1, -1, -1, -1, -1, -1, -1, 2,  1,  0,  -1,
        -1, -1, -1, -1, -1, -1, 2,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 2,  3,  4,  -1, -1, -1, -1, -1, -1, -1, 2,  3,
        4,  6,  7,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3,  11, -1, -1, -1, -1, -1, -1, -1, -1,
        20, 2,  -1, -1, -1, -1, -1, -1, -1, -1, 3,  15, 23, -1, -1, -1, -1, -1, -1, -1, 3,  14, 24, -1, -1, -1, -1, -1,
        -1, -1, 2,  1,  0,  -1, -1, -1, -1, -1, -1, -1, 2,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 4,  -1, -1, -1, -1, -1,
        -1, -1, -1, -1, 6,  7,  -1, -1, -1, -1, -1, -1, -1, -1, 10, 9,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, 20, 2,  -1, -1, -1, -1, -1, -1, -1, -1, 15, 23, -1, -1, -1, -1, -1, -1, -1, -1, 14, 24,
        -1, -1, -1, -1, -1, -1, -1, -1, 1,  0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        3,  4,  -1, -1, -1, -1, -1, -1, -1, -1, 3,  4,  6,  7,  -1, -1, -1, -1, -1, -1, 10, 9,  -1, -1, -1, -1, -1, -1,
        -1, -1, 3,  11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 22, 1,  0,  -1, -1, -1, -1, -1, -1, -1, 22, 2,  -1, -1,
        -1, -1, -1, -1, -1, -1, 22, 3,  4,  -1, -1, -1, -1, -1, -1, -1, 22, 3,  4,  6,  7,  -1, -1, -1, -1, -1, 22, 10,
        9,  -1, -1, -1, -1, -1, -1, -1, 22, 3,  11, -1, -1, -1, -1, -1, -1, -1, 22, 20, 2,  -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 21, 22, 1,  0,  -1, -1, -1, -1,
        -1, -1, 21, 22, 2,  -1, -1, -1, -1, -1, -1, -1, 21, 22, 3,  4,  -1, -1, -1, -1, -1, -1, 21, 22, 3,  4,  6,  7,
        -1, -1, -1, -1, 21, 22, 10, 9,  -1, -1, -1, -1, -1, -1, 21, 22, 3,  11, -1, -1, -1, -1, -1, -1, 21, 22, 20, 2,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    /// <summary>A whole turn and half of one, in degrees.</summary>
    constexpr float FullTurn = 360.0f;
    constexpr float HalfTurn = 180.0f;
    /// <summary>How far a turn middle lies from one facing to the next.</summary>
    constexpr float Halfway = 0.5f;

    /// <summary>Upper-body rotations per gesture: 17 when the part is mirrored, else 32.</summary>
    auto PartRotations(bool symmetrical) -> int32_t
    {
        return symmetrical ? 0x11 : 0x20;
    }

    /// <summary>Whether <paramref name="part"/> (the tree's numbering) is mirrored in the gesture.</summary>
    auto PartSymmetrical(const MCGestureData& data, int32_t part) -> bool
    {
        return (data.Symmetrical && part == 1) || part == 0 || (data.ArmSymmetrical && part > 1);
    }

    /// <summary>
    /// Wraps <paramref name="rotation"/> into -180..180, then mirrors it (returning true) when the part is
    /// <paramref name="symmetrical"/>, or wraps negatives to 0..360 when not.
    /// </summary>
    auto Facing(bool symmetrical, float& rotation) -> bool
    {
        if (rotation > 180.0)
        {
            rotation = (rotation - 180.0f) - 180.0f;
        }
        else if (rotation < -180.0)
        {
            rotation = rotation + 180.0f + 180.0f;
        }

        if (rotation < 0.0 && symmetrical)
        {
            rotation = -rotation;
            return true;
        }

        if (rotation < 0.0)
        {
            rotation = static_cast<float>(rotation + 360.0);
        }

        return false;
    }

    /// <summary>A gesture's frame rate, made positive.</summary>
    auto PositiveRate(const MCGestureData& data) -> float
    {
        return data.FrameRate < 0.0 ? -data.FrameRate : data.FrameRate;
    }

    /// <summary>Whether <paramref name="gesture"/> (falls, deaths and the like: 14..19 and 21..24) has only every fourth facing.</summary>
    auto EveryFourthFacing(int32_t gesture) -> bool
    {
        return (gesture > 0xd && gesture < 0x14) || (gesture > 0x14 && gesture < 0x19);
    }

    /// <summary>Which of a part's facings a rotation shows (its index among the gesture's), and whether mirrored.</summary>
    struct ShownFacing
    {
        int32_t Index = 0;
        bool Reverse = false;
    };

    /// <summary>
    /// The facing part <paramref name="part"/> (the tree's numbering) of gesture <paramref name="gesture"/> (its data
    /// <paramref name="data"/>) shows at <paramref name="rotation"/> degrees.
    /// </summary>
    auto ShowsFacing(const MCGestureData& data, int32_t gesture, int32_t part, float rotation) -> ShownFacing
    {
        const bool symmetrical = PartSymmetrical(data, part);
        bool reverse = Facing(symmetrical, rotation);
        const uint32_t numRotations = data.NumRotations[part];
        int32_t rotationIndex = static_cast<int16_t>(static_cast<int32_t>(
            std::floor(static_cast<double>(static_cast<int32_t>(numRotations + 1)) * rotation * (1.0 / 360.0))));

        if (rotationIndex >= 0x20 && rotation > 180.0 && !symmetrical)
        {
            rotationIndex = 0x1f;

            if (rotationIndex == static_cast<int32_t>(numRotations >> 1))
            {
                reverse = false;
            }
        }
        else if (rotationIndex == 0 || rotationIndex == static_cast<int32_t>(numRotations >> 1))
        {
            // Straight ahead or straight back: no mirroring.
            reverse = false;
        }

        // Gestures 14..19 and 21..24 only have every fourth facing.
        int32_t index = rotationIndex;

        if (EveryFourthFacing(gesture))
        {
            if (rotationIndex < 2)
            {
                index = 0;
            }
            else if (rotationIndex < 6)
            {
                index = 4;
            }
            else if (rotationIndex < 10)
            {
                index = 8;
            }
            else if (rotationIndex < 0xe)
            {
                index = 0xc;
            }
            else if (rotationIndex < 0x12)
            {
                index = 0x10;
            }
            else if (rotationIndex < 0x16)
            {
                index = 0x14;
            }
            else if (rotationIndex > 0x19)
            {
                index = 0x1c;
            }
            else
            {
                index = 0x18;
            }
        }

        return ShownFacing{index, reverse};
    }
}

MCSpriteTree::MCSpriteTree() : ShapeList(TreeShapeCount)
{
}

auto MCSpriteTree::PreloadGestures() -> void
{
    if (GesturesPreloaded)
    {
        return;
    }

    // The shapes load as the large (90-pixel) art, which MCMechActor::Render asks for: the camera scale is pinned
    // to 100. The original loaded the small art here (it started zoomed out), and the cached shape then stood in for
    // the large one, so a preloaded gesture drew at half size.
    auto preloadPart = [this](MCMechPart part, uint32_t gesture, int32_t numRotations)
    {
        const uint32_t fileNumber = FileNumbers[part];

        for (int32_t i = 0; i < numRotations; i++)
        {
            const std::shared_ptr<MCSpriteManager> spriteManager = SpriteManager();

            if (fileNumber == NoFileNumber || spriteManager == nullptr)
            {
                continue;
            }

            const auto packet =
                static_cast<uint32_t>(static_cast<int32_t>(PacketFinderArray[gesture]) * numRotations + i);
            ShapeList[PartShapeStart[part] + packet] =
                spriteManager->GetMechShapeData(fileNumber, packet, part, 1, weak_from_this(), true);
        }
    };

    // Faithful: the torso's "symmetrical" is never cleared once a gesture sets it.
    bool torsoSymmetrical = false;

    // Stand, walk and run.
    for (const uint32_t gesture : {2u, 4u, 7u})
    {
        preloadPart(MCMechPart::Legs, gesture, LegRotationCount);
        const MCGestureData& data = Gestures[gesture];
        torsoSymmetrical = torsoSymmetrical || data.Symmetrical;
        preloadPart(MCMechPart::Torso, gesture, PartRotations(torsoSymmetrical));
        preloadPart(MCMechPart::RightArm, gesture, PartRotations(data.ArmSymmetrical));
        preloadPart(MCMechPart::LeftArm, gesture, PartRotations(data.ArmSymmetrical));
    }

    GesturesPreloaded = true;
    // Port-only (MechBraking): the run to walk made for a tree without one drawn, worked out now the run and walk are
    // in, not as a mech first slows.
    static_cast<void>(MCRunToWalkMade(*this, true));
}

auto MCSpriteTree::PrepareHdArt() const -> void
{
    const std::shared_ptr<MCSpriteManager> spriteManager = SpriteManager();

    if (spriteManager == nullptr)
    {
        return;
    }

    for (const MCMechPart part : {MCMechPart::Legs, MCMechPart::Torso, MCMechPart::RightArm, MCMechPart::LeftArm})
    {
        // The large art's, as PreloadGestures loads and MCMechActor::Render draws.
        if (const uint32_t fileNumber = FileNumbers[part]; fileNumber != NoFileNumber)
        {
            MCHdArt::Prepare(MCHdArt::MechShapeKey{spriteManager->PartFileName(part, true), fileNumber, 0});
        }
    }
}

auto MCSpriteTree::Load(MCFile& apprFile, uint32_t fileSize) -> MCAppearanceLoad
{
    MCAppearanceFit fit(apprFile, fileSize);
    fit.SeekBlock("Main Info");
    fit.Read<std::string>("Name");
    FileNumbers[MCMechPart::Legs] = fit.Read<uint32_t>("legFileNumber");
    FileNumbers[MCMechPart::Torso] = fit.Read<uint32_t>("torsoFileNumber");
    FileNumbers[MCMechPart::RightArm] = fit.Read<uint32_t>("rightArmFileNumber");
    FileNumbers[MCMechPart::LeftArm] = fit.Read<uint32_t>("leftArmFileNumber");
    fit.SeekBlock("Parts");
    NumParts = fit.Read<uint8_t>("NumParts");

    fit.SeekBlock("SpecialInfo");
    MCMechSpecialInfo& info = SpecialInfo;
    info.FbDXlat = fit.Read<float>("fb_d_xlat");
    info.JumpAirborne = fit.Read<uint32_t>("jumpAirborne");
    info.JumpHold = fit.Read<uint32_t>("jumpHold");
    info.JumpStartLandTime = fit.Read<float>("jumpStartLandTime");
    info.JumpMaxDistance = fit.Read<float>("jumpMaxDistance");
    info.JumpGravity = fit.Read<float>("jumpGravity");
    info.JumpStartVel = fit.Read<float>("jumpStartVel");
    info.RFbWFbFrame = fit.Read<uint32_t>("r_fb_w_fb_frame");
    info.RFfWFfFrame = fit.Read<uint32_t>("r_ff_w_ff_frame");
    info.SFbWFbFrame = fit.Read<uint32_t>("s_fb_w_fb_frame");
    info.SFfWFfFrame = fit.Read<uint32_t>("s_ff_w_ff_frame");
    info.WalkToWRFrame = fit.Read<uint32_t>("walk_to_w_r_frame");
    info.RunToRWFrame = fit.Read<uint32_t>("run_to_r_w_frame");
    info.WalkToWSFrame = fit.Read<uint32_t>("walk_to_w_s_frame", NoFrame);
    info.SWToWalkFrame = fit.Read<uint32_t>("s_w_to_walk_frame", NoFrame);
    info.StupidJamieReverseFlag = fit.Read<uint32_t>("stupidJamieReverseFlag", 0);
    info.OtherJamieReverseFlag = fit.Read<uint32_t>("OtherJamieReverseFlag", 0);
    info.ReallyStupidJamieReverseFlag = fit.Read<uint32_t>("reallyStupidJamieReverseFlag", 0);
    info.SpecialDuaneFlag = fit.Read<uint32_t>("specialDuaneFlag", 0);
    info.StandToGunPose = fit.Read<uint32_t>("standToGunPose", 0);
    info.WalkToGunPose = fit.Read<uint32_t>("walkToGunPose", 0);
    info.RunToGunPose = fit.Read<uint32_t>("runToGunPose", 0);

    if (fit.HasBlock("TransitionTable"))
    {
        fit.ReadArray<char>("TransitionArray", std::span(TransitionArray.emplace()));
    }

    fit.SeekBlock("Gestures");
    NumGestures = fit.Read<uint8_t>("NumGestures");

    // Faithful: no gestures fails the load (the original's zero-byte allocation).
    if (!fit.Failed() && NumGestures == 0)
    {
        return std::unexpected("no gestures");
    }

    Gestures.resize(fit.Failed() ? 0 : NumGestures);

    for (size_t i = 0; i < Gestures.size() && !fit.Failed(); i++)
    {
        fit.SeekBlock(std::format("Gestures{}", i));
        MCGestureData& data = Gestures[i];
        data.State = fit.Read<uint8_t>("State");
        data.NumFrames = fit.Read<uint32_t>("NumFrames");
        data.FrameRate = fit.Read<float>("FrameRate");
        data.BasePacketNumber = fit.Read<uint32_t>("BasePacketNumber");
        fit.ReadArray<uint8_t>("NumRotations",
                               std::span(data.NumRotations).first(std::min<size_t>(NumParts, MechPartCount)));
        data.Symmetrical = fit.Read<uint8_t>("Symmetrical") != 0;
        data.ArmSymmetrical = fit.Read<uint8_t>("ArmSymmetrical", 1) != 0;
        data.ForwardResult = fit.Read<uint8_t>("ForwardResult");
        data.ReverseResult = fit.Read<uint8_t>("ReverseResult");
        data.StartVelocity = fit.Read<float>("StartVelocity");
        data.EndVelocity = fit.Read<float>("EndVelocity");
    }

    // Issue #25: the game asks for its gestures by number (0-27) and follows the transition table's numbers, whatever
    // the file holds. A tree with fewer gestures gets empty ones (no frames: nothing drawn), and a table naming a
    // gesture past them refuses the tree.
    if (!fit.Failed() && Gestures.size() < PacketFinderArray.size())
    {
        Gestures.resize(PacketFinderArray.size());
    }

    if (TransitionArray.has_value())
    {
        for (const char next : *TransitionArray)
        {
            if (next < -1 || next >= static_cast<int32_t>(Gestures.size()))
            {
                return std::unexpected(
                    std::format("a transition to gesture {} of {}", static_cast<int32_t>(next), Gestures.size()));
            }
        }
    }

    return fit.Result();
}

auto MCSpriteTree::SetGesture(int32_t gesture, int32_t part, float rotation, int32_t& reverse, float& frameRate) const
    -> void
{
    reverse = 0;
    frameRate = 0.0f;

    if (gesture < 0 || static_cast<size_t>(gesture) >= Gestures.size())
    {
        return;
    }

    const MCGestureData& data = Gestures[gesture];

    if (data.NumFrames == 0)
    {
        return;
    }

    reverse = Facing(PartSymmetrical(data, part), rotation) ? 1 : 0;
    frameRate = PositiveRate(data);
}

auto MCSpriteTree::GesturePacket(int32_t gesture, int32_t part, float rotation) const -> std::optional<MCGesturePacket>
{
    if (gesture < 0 || static_cast<size_t>(gesture) >= std::min(Gestures.size(), PacketFinderArray.size()))
    {
        return std::nullopt;
    }

    const MCGestureData& data = Gestures[gesture];

    if (data.NumFrames == 0 || part < 0 || part >= MechPartCount)
    {
        return std::nullopt;
    }

    const bool symmetrical = PartSymmetrical(data, part);
    const ShownFacing shown = ShowsFacing(data, gesture, part, rotation);
    const int32_t index = shown.Index;
    const bool reverse = shown.Reverse;

    // The tree's parts 2 and 3 are the left and the right arm's sprites.
    constexpr std::array<MCMechPart, MechPartCount> filePart = {MCMechPart::Legs, MCMechPart::Torso,
                                                                MCMechPart::LeftArm, MCMechPart::RightArm};
    const auto finder = static_cast<int32_t>(PacketFinderArray[gesture]);
    const int32_t rotationsStored = part == 0 ? LegRotationCount : PartRotations(symmetrical);

    // Issue #25: the rotation count is the file's; a facing past the part's stored rotations has no shape.
    if (index < 0 || index >= rotationsStored)
    {
        return std::nullopt;
    }

    const auto packet = static_cast<uint32_t>(finder * rotationsStored + index);
    const MCMechPart file = filePart[part];
    return MCGesturePacket{file, packet, PartShapeStart[file] + packet, reverse};
}

auto MCSpriteTree::DrawnFacing(int32_t gesture, int32_t part, float rotation) const -> std::optional<MCDrawnFacing>
{
    if (gesture < 0 || static_cast<size_t>(gesture) >= Gestures.size() || part < 0 || part >= MechPartCount ||
        Gestures[gesture].NumFrames == 0)
    {
        return std::nullopt;
    }

    const MCGestureData& data = Gestures[gesture];
    const ShownFacing shown = ShowsFacing(data, gesture, part, rotation);
    const float degrees = static_cast<float>(shown.Index) * FullTurn /
                          static_cast<float>(static_cast<int32_t>(data.NumRotations[part]) + 1);
    return MCDrawnFacing{shown.Reverse ? -degrees : degrees, shown.Reverse};
}

auto MCSpriteTree::TurnMiddle(int32_t gesture, float rotation) const -> std::optional<MCTurnMiddle>
{
    if (gesture < 0 || static_cast<size_t>(gesture) >= Gestures.size() || Gestures[gesture].NumFrames == 0 ||
        EveryFourthFacing(gesture))
    {
        return std::nullopt;
    }

    const MCGestureData& data = Gestures[gesture];
    const ShownFacing shown = ShowsFacing(data, gesture, 0, rotation);
    // The legs are always mirrored for negative facings: the way off straight ahead, either side.
    float away = rotation;
    const bool negative = Facing(true, away);
    const auto count = static_cast<int32_t>(data.NumRotations[0]) + 1;
    const int32_t back = static_cast<int32_t>(data.NumRotations[0]) >> 1;
    const float step = FullTurn / static_cast<float>(count);
    const auto facingOf = [step](int32_t index) { return (static_cast<float>(index) + Halfway) * step; };

    // The second half of a facing's range: halfway to the next facing, the other side's mirrored, from straight ahead.
    if (shown.Index < back && away - static_cast<float>(shown.Index) * step >= step * Halfway)
    {
        const float degrees = facingOf(shown.Index);

        if (negative && shown.Index == 0)
        {
            return MCTurnMiddle{TurnMirroredStep, 1, true, facingOf(1), -degrees};
        }

        return MCTurnMiddle{TurnStep, shown.Index + 1, false, facingOf(shown.Index + 1), negative ? -degrees : degrees};
    }

    // Straight back's range (never mirrored) reaches round to the other side's: its half there (and straight back
    // itself, on neither side) shows straight back.
    if (shown.Index == back && (negative || away >= HalfTurn))
    {
        return MCTurnMiddle{TurnStep, back, true, facingOf(back), HalfTurn};
    }

    return std::nullopt;
}

auto MCSpriteTree::GetGesture(int32_t gesture, int32_t part, float rotation, int32_t& reverse, float& frameRate,
                              bool zoomedOut) -> std::shared_ptr<MCShape>
{
    reverse = 0;
    frameRate = 0.0f;
    const std::optional<MCGesturePacket> where = GesturePacket(gesture, part, rotation);

    if (!where.has_value())
    {
        return nullptr;
    }

    reverse = where->Reverse ? 1 : 0;
    frameRate = PositiveRate(Gestures[gesture]);
    const uint32_t fileNumber = FileNumbers[where->FilePart];

    const std::shared_ptr<MCSpriteManager> spriteManager = SpriteManager();

    if (fileNumber == NoFileNumber || spriteManager == nullptr)
    {
        return nullptr;
    }

    std::weak_ptr<MCShape>& slot = ShapeList[where->ListIndex];

    if (std::shared_ptr<MCShape> shape = slot.lock())
    {
        shape->LastTurnUsed = Turn;
        return shape;
    }

    std::shared_ptr<MCShape> shape =
        spriteManager->GetMechShapeData(fileNumber, where->Packet, where->FilePart, Turn, weak_from_this(), zoomedOut);
    slot = shape;
    return shape;
}

auto MCSpriteTree::Transition(int32_t index) const -> int32_t
{
    return TransitionArray.has_value() ? (*TransitionArray)[index] : DefaultTransitions[index];
}
