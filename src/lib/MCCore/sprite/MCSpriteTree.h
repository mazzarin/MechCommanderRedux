#pragma once

#include "appear/MCAppearanceType.h"
#include "sprite/MCSpriteManager.h"

/// <summary>A mech gesture (FIT "Gestures%d").</summary>
struct MCGestureData
{
    /// <summary>FIT "State".</summary>
    uint8_t State = 0;
    /// <summary>FIT "ForwardResult": the gesture this one leads to.</summary>
    uint8_t ForwardResult = 0;
    /// <summary>FIT "ReverseResult": the gesture it leads to played backwards.</summary>
    uint8_t ReverseResult = 0;
    /// <summary>FIT "Symmetrical": the torso's negative facings are the positive ones mirrored.</summary>
    bool Symmetrical = false;
    /// <summary>FIT "ArmSymmetrical" (true when absent): the arms' likewise.</summary>
    bool ArmSymmetrical = false;
    /// <summary>FIT "NumRotations", per part of the tree's numbering.</summary>
    std::array<uint8_t, MechPartCount> NumRotations{};
    /// <summary>FIT "NumFrames" (0: no such gesture).</summary>
    uint32_t NumFrames = 0;
    /// <summary>FIT "BasePacketNumber".</summary>
    uint32_t BasePacketNumber = 0;
    /// <summary>FIT "FrameRate" (negative plays backwards).</summary>
    float FrameRate = 0.0f;
    /// <summary>FIT "StartVelocity".</summary>
    float StartVelocity = 0.0f;
    /// <summary>FIT "EndVelocity".</summary>
    float EndVelocity = 0.0f;
};

/// <summary>A frame number a mech's FIT may leave out.</summary>
inline constexpr uint32_t NoFrame = 0xffffffff;

/// <summary>A part's file number when the mech has no such part.</summary>
inline constexpr uint32_t NoFileNumber = 0xffffffff;

/// <summary>A mech's special animation data (FIT "SpecialInfo").</summary>
struct MCMechSpecialInfo
{
    float FbDXlat = 0.0f;
    uint32_t JumpAirborne = 0;
    uint32_t JumpHold = 0;
    float JumpStartLandTime = 0.0f;
    float JumpMaxDistance = 0.0f;
    float JumpGravity = 0.0f;
    float JumpStartVel = 0.0f;
    /// <summary>The frames the fall-down gestures 16..19 start at, after running (R) or standing (S).</summary>
    uint32_t RFbWFbFrame = 0;
    uint32_t RFfWFfFrame = 0;
    uint32_t SFbWFbFrame = 0;
    uint32_t SFfWFfFrame = 0;
    /// <summary>The frames a walk or run moves on to the next gesture of a transition at.</summary>
    uint32_t WalkToWRFrame = 0;
    uint32_t RunToRWFrame = 0;
    /// <summary><see cref="NoFrame"/> when absent.</summary>
    uint32_t WalkToWSFrame = NoFrame;
    /// <summary><see cref="NoFrame"/> when absent.</summary>
    uint32_t SWToWalkFrame = NoFrame;
    /// <summary>The fall turns the mech round in the fall gesture rather than in gesture 22.</summary>
    uint32_t StupidJamieReverseFlag = 0;
    /// <summary>The fall doesn't turn the mech round.</summary>
    uint32_t OtherJamieReverseFlag = 0;
    /// <summary>The run after a jump starts half way through.</summary>
    uint32_t SpecialDuaneFlag = 0;
    /// <summary>The mech has a gun pose from standing, walking, running.</summary>
    uint32_t StandToGunPose = 0;
    uint32_t WalkToGunPose = 0;
    uint32_t RunToGunPose = 0;
    /// <summary>The fall-down gestures turn the mech round, at the fall or when it next stands.</summary>
    uint32_t ReallyStupidJamieReverseFlag = 0;
};

/// <summary>The size of a gesture transition table: from 9 states to 9 goals, 10 steps each.</summary>
inline constexpr size_t TransitionTableSize = 0x32a;

/// <summary>A gesture transition table (-1 ends a row).</summary>
using MCTransitionTable = std::array<char, TransitionTableSize>;

/// <summary>Where a mech part's shapes go in a tree's shape list, and which part PAK holds them.</summary>
struct MCGesturePacket
{
    /// <summary>The part PAK.</summary>
    MCMechPart FilePart = MCMechPart::Legs;
    /// <summary>The packet in the mech's PAK of that part.</summary>
    uint32_t Packet = 0;
    /// <summary>The entry in the tree's shape list.</summary>
    uint32_t ListIndex = 0;
    /// <summary>Whether the shape is drawn mirrored.</summary>
    bool Reverse = false;
};

/// <summary>Port-only (HDFootprints): which way a mech part is drawn facing (<see cref="MCSpriteTree::DrawnFacing"/>).</summary>
struct MCDrawnFacing
{
    /// <summary>The way, in degrees: the start of the rotations that show the facing, the other way when mirrored.</summary>
    float Degrees = 0.0f;
    /// <summary>Whether the shape is drawn mirrored (straight ahead and straight back never are).</summary>
    bool Mirrored = false;
};

/// <summary>
/// Port-only (HDTurns): a mech's legs drawn as the HD art pack's turn middle, halfway to a neighbouring stored facing
/// (<see cref="MCSpriteTree::TurnMiddle"/>).
/// </summary>
struct MCTurnMiddle
{
    /// <summary>The middle's step in the pack (sprite/MCInBetween.h's TurnStep or TurnMirroredStep).</summary>
    int32_t Step = 0;
    /// <summary>The stored facing it is halfway to, whether that one is mirrored, and a rotation showing it unmirrored.</summary>
    int32_t Toward = 0;
    bool TowardMirrored = false;
    float TowardRotation = 0.0f;
    /// <summary>The way it is drawn facing, in degrees as a rotation (<see cref="MCDrawnFacing::Degrees"/>).</summary>
    float Degrees = 0.0f;
};

/// <summary>
/// Whether gesture <paramref name="gesture"/> strides (walking, running, limping, walking backward): a mirrored part
/// leads with the other leg, so it may run half a cycle off (MCMechActor::Render).
/// </summary>
inline bool MCStrideGesture(int32_t gesture)
{
    return gesture == 4 || gesture == 7 || gesture == 9 || gesture == 11;
}

/// <summary>
/// The type of a <see cref="MCMechActor"/>: a mech's gestures, its special animation data, the transition table and
/// the files of its four parts in the part PAKs.
/// </summary>
/// <remarks>
/// Original source: <c>sprite\spritree.cpp</c> (class 1 of the sprite PAK). FIT: "Main Info" (Name,
/// leg/torso/rightArm/leftArmFileNumber), "Parts", "SpecialInfo", optional "TransitionTable", "Gestures" and
/// "Gestures%d". The tree numbers the parts its own way: 0 legs, 1 torso, 2 the left arm's sprites, 3 the right
/// arm's.
/// </remarks>
class MCSpriteTree : public MCAppearanceType
{
public:
    MCSpriteTree();

    /// <summary>Reads the FIT.</summary>
    MCAppearanceLoad Load(MCFile& apprFile, uint32_t fileSize) override;

    /// <summary>Loads (once) the shapes of the stand, walk and run gestures, every facing of every part.</summary>
    void PreloadGestures();

    /// <summary>Port-only: opens the HD art pack's archives of the mech's parts (MCHdArt::Prepare).</summary>
    void PrepareHdArt() const;

    /// <summary>
    /// The frame rate of gesture <paramref name="gesture"/> (made positive), and whether part
    /// <paramref name="part"/> (the tree's numbering) is drawn mirrored at <paramref name="rotation"/> degrees.
    /// Both 0 when the gesture has no frames.
    /// </summary>
    void SetGesture(int32_t gesture, int32_t part, float rotation, int32_t& reverse, float& frameRate) const;

    /// <summary>
    /// Where the shape of part <paramref name="part"/> (the tree's numbering) in gesture <paramref name="gesture"/>
    /// at <paramref name="rotation"/> degrees is; nothing when the gesture has no frames.
    /// </summary>
    std::optional<MCGesturePacket> GesturePacket(int32_t gesture, int32_t part, float rotation) const;

    /// <summary>
    /// Port-only (HDFootprints): the way part <paramref name="part"/> (the tree's numbering) of gesture
    /// <paramref name="gesture"/> is drawn facing at <paramref name="rotation"/> degrees, as <see cref="GetGesture"/>
    /// draws it. Nothing when the gesture has no frames.
    /// </summary>
    std::optional<MCDrawnFacing> DrawnFacing(int32_t gesture, int32_t part, float rotation) const;

    /// <summary>
    /// Port-only (HDTurns): the turn middle the legs of gesture <paramref name="gesture"/> show at
    /// <paramref name="rotation"/> degrees, drawn over the facing it shows (whose indices stay): in the second half of a
    /// facing's range (~10.6 degrees of each ~21.2), the facing halfway to the next one (the next mirrored, from
    /// straight ahead on the mirrored side), and in straight back's half on the mirrored side, straight back. None in a
    /// facing's first half, or for a gesture with every fourth facing only (falls and the like) or without frames.
    /// </summary>
    std::optional<MCTurnMiddle> TurnMiddle(int32_t gesture, float rotation) const;

    /// <summary>
    /// The shape of part <paramref name="part"/> (the tree's numbering) in gesture <paramref name="gesture"/> at
    /// <paramref name="rotation"/> degrees (from the 90-pixel PAKs when <paramref name="zoomedOut"/>), loading it if
    /// needed; <paramref name="reverse"/> and <paramref name="frameRate"/> as <see cref="SetGesture"/>.
    /// </summary>
    std::shared_ptr<MCShape> GetGesture(int32_t gesture, int32_t part, float rotation, int32_t& reverse,
                                        float& frameRate, bool zoomedOut);

    /// <summary>The next gesture of row <paramref name="index"/> of the tree's transition table, else the default one.</summary>
    int32_t Transition(int32_t index) const;

    /// <summary>FIT "NumGestures".</summary>
    uint8_t NumGestures = 0;
    /// <summary>FIT "NumParts": legs, torso, arms.</summary>
    uint8_t NumParts = 0;
    /// <summary>The gestures.</summary>
    std::vector<MCGestureData> Gestures;
    /// <summary>The special animation data.</summary>
    MCMechSpecialInfo SpecialInfo;
    /// <summary>
    /// The loaded shapes: each part's packets of each gesture, at <see cref="PartShapeStart"/> (expired once the cache
    /// drops them).
    /// </summary>
    std::vector<std::weak_ptr<MCShape>> ShapeList;
    /// <summary>FIT "leg/torso/rightArm/leftArmFileNumber": the mech's packet in each part PAK (-1 for none).</summary>
    MCMechParts<uint32_t> FileNumbers{{NoFileNumber, NoFileNumber, NoFileNumber, NoFileNumber}};
    /// <summary>FIT "TransitionArray": the tree's own transition table, when it has one.</summary>
    std::optional<MCTransitionTable> TransitionArray;
    /// <summary>Whether <see cref="PreloadGestures"/> has run.</summary>
    bool GesturesPreloaded = false;
};

/// <summary>The number of entries of a tree's shape list (all four parts).</summary>
inline constexpr size_t TreeShapeCount = 0x96f;

/// <summary>Where each part PAK's shapes start in a tree's shape list.</summary>
inline constexpr MCMechParts<uint32_t> PartShapeStart{{0, 0xcf, 0x3af, 0x68f}};

/// <summary>Per gesture, its index among the gestures stored in a part PAK (28 entries).</summary>
inline constexpr std::array<uint32_t, 28> PacketFinderArray = {0,  1,  2,  3,  4,  5,  6,  7,  8,  4,  5,  9, 10, 11,
                                                               12, 13, 14, 15, 16, 17, 18, 19, 20, 13, 12, 0, 21, 22};
