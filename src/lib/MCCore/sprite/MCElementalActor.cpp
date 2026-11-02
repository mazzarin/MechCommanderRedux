#include "stdafx.h"
#include "sprite/MCElementalActor.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBigGameObject.h"
#include "object/MCForces.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCWeather.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The jump gesture.</summary>
    constexpr int32_t GestureJump = 2;
}

MCElementalActor::~MCElementalActor()
{
    if (AppearType != nullptr)
    {
        AppearanceTypeList().RemoveAppearance(*AppearType);
    }
}

auto MCElementalActor::SetGestureGoal(int32_t goal) -> int32_t
{
    if (GoalPending)
    {
        return static_cast<int32_t>(0xeadd0005);
    }

    if (goal == GestureJump)
    {
        if (!JumpSetup)
        {
            return static_cast<int32_t>(0xeadd0006);
        }
    }
    else if (goal < 0)
    {
        return static_cast<int32_t>(0xeadd0003);
    }

    if (static_cast<int32_t>(AppearType->Gestures.size()) < goal)
    {
        return static_cast<int32_t>(0xeadd0003);
    }

    GestureGoal = goal;
    GoalPending = true;
    return 0;
}

auto MCElementalActor::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    Owner = std::move(obj);
    AppearType = std::static_pointer_cast<MCElementalTree>(std::move(tree));

    Visible = false;
    FadeTableIndex = -1;
    CurrentFrame = -1;
    ShapeMinY = -15.0f;
    ShapeMinX = -15.0f;
    CurrentShape.reset();
    CurrentTime = 0.0f;
    LastFrame = 0;
    Velocity = 0.0f;
    GoalPending = false;
    CurrentGesture = 0;
    OldGesture = 0;
    Jumping = false;
    JumpSetup = false;
    InView = false;
    FrameRate = 15.0f;
    VelocityPercentage = 1.0f;
    ShapeMaxY = 30.0f;
    ShapeMaxX = 30.0f;
    return 0;
}

auto MCElementalActor::SetJumpParameters(float jumpDistance) -> int32_t
{
    if (Jumping)
    {
        return static_cast<int32_t>(0xeadd0007);
    }

    GestureGoal = GestureJump;
    JumpSetup = true;
    // Cover the distance in the jump gesture's time.
    const MCElementalGestureData& jump = AppearType->Gestures[GestureJump];
    Velocity = jumpDistance / (static_cast<float>(jump.NumFrames) / jump.FrameRate);
    return 0;
}

auto MCElementalActor::GetVelocityMagnitude() -> float
{
    if (!Jumping && !JumpSetup)
    {
        Velocity = AppearType->Gestures[CurrentGesture].Velocity;
    }

    return Velocity;
}

auto MCElementalActor::SetVelocityPercentage(float percent) -> void
{
    // Keep the animation at the same frame when the speed changes.
    if (percent != VelocityPercentage)
    {
        CurrentTime = (VelocityPercentage * CurrentTime * FrameRate) / (percent * FrameRate);
    }

    VelocityPercentage = percent;
}

auto MCElementalActor::RecalcBounds(MCOptionalRef<MCCamera> view) -> int
{
    if (!view.has_value())
    {
        return 0;
    }

    const MCCamera& cam = view->get();
    const MCVector2D pos = Owner.lock()->GetScreenPos(cam.CameraId - 1);
    UpperLeft = pos;
    LowerRight = pos;

    // The shape's bounds are taken once, from the first shape seen.
    if (const std::shared_ptr<MCShape> shape = CurrentShape.lock();
        shape != nullptr && !shape->FrameList().empty() && !InView)
    {
        const std::span<const uint8_t> shapeTable = shape->FrameList();
        MCGrowBounds(MCShapeFrameBounds(shapeTable, MCClampShapeFrame(shapeTable, CurrentFrame)), ShapeMinX, ShapeMinY,
                     ShapeMaxX, ShapeMaxY);
        InView = true;
    }

    UpperLeft.X = ShapeMinX + pos.X;
    UpperLeft.Y = ShapeMinY + pos.Y;
    LowerRight.X = ShapeMaxX + UpperLeft.X;
    LowerRight.Y = ShapeMaxY + UpperLeft.Y;

    if (0.0f <= LowerRight.X && 0.0f <= LowerRight.Y &&
        UpperLeft.X <= static_cast<float>(static_cast<int32_t>(std::floor(cam.ViewWidth))) &&
        UpperLeft.Y <= static_cast<float>(static_cast<int32_t>(std::floor(cam.ViewHeight))))
    {
        return 1;
    }

    return 0;
}

auto MCElementalActor::Render(int32_t depthFixup) -> int32_t
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    ScreenPos = obj->GetScreenPos(Eye()->CameraId - 1);
    FrameRate = AppearType->GestureFrameRate(CurrentGesture);
    const std::shared_ptr<MCShape> shape =
        AppearType->GetGesture(CurrentGesture, static_cast<float>(MCActorFacing(*obj)));
    CurrentShape = shape;
    RecalcBounds(EyeRef());
    const auto depth = static_cast<int32_t>(static_cast<float>(depthFixup) - ScreenPos.Y);
    ElementList().OpenGroup(depth, true);

    if (DrawTerrainGrid != 0)
    {
        DrawSelectBox(0xff);
    }

    if (shape == nullptr || shape->FrameList().empty())
    {
        return 0;
    }

    std::optional<MCColorTable> fadeTable;

    if (FadeTableIndex >= 0)
    {
        fadeTable = GamePalette().GetFadeTable(FadeTableIndex);
    }

    if (CurrentFrame < 0)
    {
        CurrentFrame = 0;
    }

    ElementList().Add(
        ElementList().Make<MCVfxElement>(shape->FrameList(), ScreenPos.X, ScreenPos.Y, CurrentFrame, 0, fadeTable, 1));
    // Port-only (sun shadows): its own shadow at dusk.
    const MCSceneLights::MCSunShadowPart part{shape->FrameList(), ScreenPos.X, ScreenPos.Y, CurrentFrame};
    MCSceneLights::AddRecastShadows(std::span(&part, 1), depth, true);
    // Port-only (HD weather): it takes the rain.
    MCWeather::AddUnit(std::span(&part, 1), obj->Position, ScreenPos.X, ScreenPos.Y, depth);

    // Selection: -1 and 1 draw the bars (none for elementals), 2 the brackets in the owner's alignment colour.
    const int32_t selected = obj->Selected;

    if (selected == -1 || selected == 1)
    {
        RecalcBounds(EyeRef());
        DrawBars();
    }
    else if (selected == 2)
    {
        RecalcBounds(EyeRef());
        const std::shared_ptr<MCGameObject>& selectedObj = obj;
        const int32_t alignment = selectedObj->GetAlignment();

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
            DrawSelectBrackets(selectedObj->GetAlignment() == HomeTeam()->Alignment ? 0xfc : 0xfb);
        }
    }

    return 0;
}

auto MCElementalActor::Update() -> int32_t
{
    int32_t gesture = CurrentGesture;
    GoalPending = false;

    if (GestureGoal != gesture)
    {
        gesture = GestureGoal;
        CurrentGesture = gesture;
        OldGesture = gesture;
        JumpSetup = false;
        Jumping = gesture == GestureJump;

        if (const std::shared_ptr<MCGameObject> owner = Owner.lock(); owner->IsDisabled() == 0)
        {
            owner->Status = 0;
        }

        CurrentFrame = 0;
        // Keep the animation time consistent with the new gesture's rate.
        const float newRate = std::fabs(AppearType->Gestures[gesture].FrameRate);

        if (newRate != FrameRate || VelocityPercentage != 1.0)
        {
            CurrentTime = (FrameRate * CurrentTime) / (VelocityPercentage * newRate);
        }
    }

    FrameRate = AppearType->GestureFrameRate(gesture);

    if (CurrentFrame == -1)
    {
        CurrentFrame = 0;
        return 1;
    }

    FrameRate = VelocityPercentage * FrameRate;
    CurrentTime = FrameLength + CurrentTime;
    const auto wholeFrames = static_cast<int32_t>(std::floor(static_cast<double>(CurrentTime * FrameRate)));

    if (LastFrame < wholeFrames)
    {
        const int32_t played = LastFrame;
        LastFrame = wholeFrames;
        const int32_t advanced = wholeFrames - played;
        // The original also let a flag (always 1) stop the jump gesture from animating.
        const int32_t current = CurrentGesture;

        if (advanced != 0)
        {
            const auto frame = static_cast<uint32_t>(CurrentFrame + advanced);
            CurrentFrame = static_cast<int32_t>(frame);
            const uint32_t numFrames = AppearType->Gestures[current].NumFrames;

            if (static_cast<int32_t>(numFrames) <= static_cast<int32_t>(frame))
            {
                CurrentFrame = static_cast<int32_t>(frame % numFrames);

                if (current == GestureJump)
                {
                    // The jump is over: back to gesture 0.
                    SetGestureGoal(0);
                    const int32_t goal = GestureGoal;
                    CurrentGesture = goal;
                    OldGesture = goal;
                    Jumping = false;
                    const float newRate = std::fabs(AppearType->Gestures[goal].FrameRate);

                    if (newRate != FrameRate || VelocityPercentage != 1.0)
                    {
                        CurrentTime = (FrameRate * CurrentTime) / (VelocityPercentage * newRate);
                    }

                    CurrentFrame = 0;
                }
            }
        }
    }

    return 1;
}
