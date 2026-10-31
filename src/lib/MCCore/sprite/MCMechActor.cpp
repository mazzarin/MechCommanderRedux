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
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
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

    const int32_t numGestures = MechTree->NumGestures;
    const float startVelocity = gesture < numGestures ? MechTree->Gestures[gesture].StartVelocity : -1.0f;
    const float endVelocity = gesture < numGestures ? MechTree->Gestures[gesture].EndVelocity : -1.0f;
    // Gestures 14 to 19 have no velocity of their own.
    const bool hasVelocity = gesture < 14 || gesture > 19;
    const float velocity = hasVelocity ? startVelocity : 0.0f;

    if (hasVelocity && startVelocity != endVelocity)
    {
        // Ease from the start velocity to the end one over the gesture.
        if (startVelocity >= -1999.0 && endVelocity >= -1999.0)
        {
            const uint32_t numFrames = MechTree->Gestures[gesture].NumFrames;
            const double framesLeft =
                static_cast<double>(numFrames - static_cast<uint32_t>(CurrentFrame[MCMechPart::Legs]));
            const double t = 1.0 - framesLeft / static_cast<double>(static_cast<int32_t>(numFrames));
            return static_cast<float>((static_cast<double>(endVelocity) - static_cast<double>(startVelocity)) * t +
                                      startVelocity);
        }
    }
    else if (velocity >= -1999.0)
    {
        return velocity;
    }

    return 0.0f;
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
    return StopCountdown == 0.0 && CurrentGesture > 2 && CurrentGesture < 12 && GestureGoal < 7;
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

auto MCMechActor::Render(int32_t depthFixup) -> int32_t
{
    const auto mech = std::static_pointer_cast<MCBattleMech>(Owner.lock());
    ScreenPos = mech->GetScreenPos(Eye()->CameraId - 1);
    const float x = ScreenPos.X;
    const float y = ScreenPos.Y;
    // The 90-pixel part PAKs, the full size ones (the small ones were the original's zoomed-out mode's).
    constexpr bool largeSprites = true;
    RenderJump();

    const float facing = static_cast<float>(MCActorFacing(*mech));
    const float torso = mech->TorsoRotation;
    const float rightArm = mech->RightArmRotation;
    const float leftArm = mech->LeftArmRotation;

    // The torso and arms play the combat gestures while the gun is up or moving.
    const int32_t upperGesture = InCombatMode                                ? GestureCombat
                                 : (CombatModeRaising || CombatModeLowering) ? GestureCombatChange
                                                                             : CurrentGesture;
    MCMechParts<std::shared_ptr<MCShape>> shapes;
    shapes[MCMechPart::Legs] = MechTree->GetGesture(CurrentGesture, 0, facing, Reverse[MCMechPart::Legs],
                                                    FrameRate[MCMechPart::Legs], largeSprites);
    shapes[MCMechPart::Torso] = MechTree->GetGesture(upperGesture, 1, facing + torso, Reverse[MCMechPart::Torso],
                                                     FrameRate[MCMechPart::Torso], largeSprites);

    const float rotation2 = facing + leftArm + torso;
    const float rotation3 = facing + rightArm + torso;
    const bool armSymmetrical = MechTree->Gestures[CurrentGesture].ArmSymmetrical != 0;

    // A mirrored arm is drawn from the other arm's sprites. OB-179 fixed: the original swapped on the sign of the
    // angle alone, also facing straight ahead or back, where the sprite isn't mirrored, so a lone arm jumped sides.
    const auto mirrored = [&](int32_t part, float rotation)
    {
        const std::optional<MCGesturePacket> packet = MechTree->GesturePacket(upperGesture, part, rotation);
        return armSymmetrical && packet.has_value() && packet->Reverse;
    };

    const bool swapRightArm = mirrored(3, rotation2);
    const bool swapLeftArm = mirrored(2, rotation3);

    if (!RightArmGone)
    {
        shapes[MCMechPart::RightArm] =
            MechTree->GetGesture(upperGesture, swapRightArm ? 3 : 2, rotation2, Reverse[MCMechPart::RightArm],
                                 FrameRate[MCMechPart::RightArm], largeSprites);
    }

    if (!LeftArmGone && swapLeftArm)
    {
        shapes[MCMechPart::LeftArm] = MechTree->GetGesture(upperGesture, 2, rotation3, Reverse[MCMechPart::LeftArm],
                                                           FrameRate[MCMechPart::LeftArm], largeSprites);
    }
    else if (!LeftArmGone)
    {
        // Faithful: asked for twice, first with the legs' gesture.
        shapes[MCMechPart::LeftArm] = MechTree->GetGesture(CurrentGesture, 3, rotation3, Reverse[MCMechPart::LeftArm],
                                                           FrameRate[MCMechPart::LeftArm], largeSprites);
        shapes[MCMechPart::LeftArm] = MechTree->GetGesture(upperGesture, 3, rotation3, Reverse[MCMechPart::LeftArm],
                                                           FrameRate[MCMechPart::LeftArm], largeSprites);
    }

    if (Wrecked)
    {
        shapes[MCMechPart::Torso].reset();
    }

    for (int32_t part = 0; part < MechPartCount; part++)
    {
        PartShape[part] = shapes[part];
    }

    // The shadow, one of 32 facings. Port-only (sun shadows): with HD art drawn by the GPU, the mech casts its parts'
    // own shadow instead, the way the objects' baked shadows fall (added once the parts' frames are known, below).
    const bool sunShadow = GHdSunShadows != 0 && MCHdArt::Active() && MCRenderer::GpuDrawing() == MCGpuDrawing::On;
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
        const int32_t gesture = CurrentGesture;
        const bool combatAnim = InCombatMode || CombatModeRaising || CombatModeLowering;
        StrideGesture = (gesture == 4 || gesture == 7 || gesture == 9 || gesture == 11) ? 1 : 0;

        if (StrideGesture != 0 && Reverse[part] != 0)
        {
            const MCGestureData& data = MechTree->Gestures[gesture];
            int32_t reference = -1;
            bool follows = false;

            if (data.ArmSymmetrical == 0)
            {
                reference = CurrentFrame[MCMechPart::LeftArm];
                follows = true;
            }
            else if (data.Symmetrical == 0)
            {
                reference = CurrentFrame[MCMechPart::Torso];
                follows = true;
            }

            if (follows && CurrentFrame[part] == reference && !combatAnim)
            {
                const int32_t frame = CurrentFrame[part];
                const uint32_t numFrames = data.NumFrames;

                if (!PlayBackwards)
                {
                    const uint32_t shifted = (numFrames >> 1) + static_cast<uint32_t>(frame);
                    CurrentFrame[part] = static_cast<int32_t>(shifted);
                    const uint32_t count = MechTree->Gestures[CurrentGesture].NumFrames;

                    if (count - 1 < shifted)
                    {
                        CurrentFrame[part] = static_cast<int32_t>(shifted % count);
                    }
                }
                else
                {
                    int32_t shifted = frame - static_cast<int32_t>(numFrames >> 1);
                    CurrentFrame[part] = shifted;

                    if (shifted < 0)
                    {
                        shifted = shifted < 0 ? -shifted : shifted;
                        const uint32_t count = MechTree->Gestures[CurrentGesture].NumFrames;
                        const uint32_t wrapped = static_cast<uint32_t>(shifted) % count;
                        CurrentFrame[part] = static_cast<int32_t>(count - wrapped);
                    }
                }
            }
        }

        StrideGesture =
            (CurrentGesture == 4 || CurrentGesture == 7 || CurrentGesture == 9 || CurrentGesture == 11) ? 1 : 0;

        if (StrideGesture != 0 && Reverse[part] == 0)
        {
            const MCGestureData& data = MechTree->Gestures[CurrentGesture];
            int32_t reference = 0;
            bool follows = false;

            if (data.ArmSymmetrical == 0)
            {
                reference = CurrentFrame[MCMechPart::LeftArm];
                follows = true;
            }
            else if (data.Symmetrical == 0)
            {
                reference = CurrentFrame[MCMechPart::Torso];
                follows = true;
            }

            if (follows && CurrentFrame[part] != reference && !combatAnim)
            {
                CurrentFrame[part] = reference;
            }
        }

        if (CurrentFrame[part] < 0)
        {
            CurrentFrame[part] = 0;
        }

        auto& element = ElementList().Make<MCVfxElement>(shapes[part]->FrameList(), ScreenPos.X, ScreenPos.Y,
                                                         CurrentFrame[part], Reverse[part], fadeTable, 1);

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

            if (shapes[part] == nullptr)
            {
                continue;
            }

            auto& element = ElementList().Make<MCVfxElement>(shapes[part]->FrameList(), ScreenPos.X, ScreenPos.Y,
                                                             CurrentFrame[part], Reverse[part], std::nullopt, 1);
            element.SunShadow = true;
            ElementList().Add(element);
        }

        ElementList().OpenGroup(partsDepth, true);
    }

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

    // Port-only (HD lighting): the mech casts shadows from the lights by it.
    MCSceneLights::AddCaster(*this, mech->Position);

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

    // Moving with nowhere to go: stand.
    if (CheckStop())
    {
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

    // Start the transition to the goal: the table lists the gestures from the state to it.
    int32_t goal = GestureGoal;

    if (goal != -1 && !InTransition && NextGesture == -1)
    {
        InTransition = true;
        TransitionStep = 0;
        const int32_t index = (goal + CurrentStateGesture * 9) * TransitionRow;
        NextGesture = tree->Transition(index);

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
        const int32_t index = TransitionStep + (goal + CurrentStateGesture * 9) * TransitionRow;
        NextGesture = tree->Transition(index);

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

    // Each part's mirroring and rate at its facing.
    const float facing = static_cast<float>(MCActorFacing(*mech));
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
            const double interval = 1.0 / FrameRate[MCMechPart::Legs];
            const double time = static_cast<double>(FrameLength) + CurrentTime[MCMechPart::Legs];
            CurrentTime[MCMechPart::Legs] = static_cast<float>(time);

            if (interval <= time)
            {
                step = static_cast<int32_t>(time * FrameRate[MCMechPart::Legs]);
                // Original behaviour (OB-055): the time left is time / step - interval, not time - step * interval.
                CurrentTime[MCMechPart::Legs] = static_cast<float>(time / step - interval);
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

            if (static_cast<int32_t>(numFrames) <= CurrentFrame[part])
            {
                if (!InTransition)
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
            const int32_t transition = InTransition;

            if (transition != 0)
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

                if (NextGesture == 8 && CurrentGesture == 7)
                {
                    GestureDone = static_cast<int32_t>(info.RunToRWFrame) <= frame;
                }
            }

            if (CurrentFrame[part] < 0)
            {
                if (transition == 0)
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
