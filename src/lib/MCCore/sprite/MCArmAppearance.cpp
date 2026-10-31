#include "stdafx.h"
#include "sprite/MCArmAppearance.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBigGameObject.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "terrain/MCTerrain.h"

MCArmAppearance::~MCArmAppearance()
{
    if (AppearType != nullptr)
    {
        AppearanceTypeList().RemoveAppearance(*AppearType);
    }
}

auto MCArmAppearance::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    Owner = std::move(obj);
    AppearType = std::static_pointer_cast<MCArmAppearanceType>(std::move(tree));
    CurrentShape.reset();
    CurrentFrame = -1;
    FadeTableIndex = -1;
    ShapeMinY = -15.0f;
    ShapeMinX = -15.0f;
    Visible = false;
    CurrentTime = 0.0f;
    LastFrame = 0;
    FrameShare = 0.0f;
    Reverse = false;
    InView = false;
    Rotation = 0.0f;
    ShapeMaxY = 15.0f;
    ShapeMaxX = 15.0f;
    return 0;
}

auto MCArmAppearance::RecalcBounds(MCOptionalRef<MCCamera> view) -> int
{
    if (!view.has_value())
    {
        return 0;
    }

    const MCCamera& cam = view->get();
    const MCVector2D pos = Owner.lock()->GetScreenPos(cam.CameraId - 1);
    UpperLeft = pos;
    LowerRight = pos;

    if (const std::shared_ptr<MCShape> shape = CurrentShape.lock(); shape != nullptr && !shape->FrameList().empty())
    {
        // The bounds only ever grow: they cover every frame drawn so far.
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

auto MCArmAppearance::Render(int32_t depthFixup) -> int32_t
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();

    if (obj->Selected != 0)
    {
        RecalcBounds(EyeRef());
        DrawSelectBox(0xfd);
    }

    ScreenPos = obj->GetScreenPos(Eye()->CameraId - 1);
    Rotation = static_cast<float>(MCActorFacing(*obj));
    const std::shared_ptr<MCShape> shape = AppearType->GetShape(static_cast<int32_t>(Rotation), FrameRate, Reverse);
    CurrentShape = shape;

    if (DrawTerrainGrid != 0)
    {
        DrawSelectBox(0xfd);
    }

    std::optional<MCColorTable> fadeTable;

    if (FadeTableIndex >= 0)
    {
        fadeTable = GamePalette().GetFadeTable(FadeTableIndex);
    }

    ElementList().OpenGroup(static_cast<int32_t>(static_cast<float>(depthFixup) - ScreenPos.Y), true);

    if (shape == nullptr || shape->FrameList().empty())
    {
        return 0;
    }

    MCVfxElement& element = ElementList().Make<MCVfxElement>(shape->FrameList(), ScreenPos.X, ScreenPos.Y, CurrentFrame,
                                                             Reverse ? 1 : 0, fadeTable, 0);
    element.InBetween = InBetween();
    ElementList().Add(element);
    return 0;
}

auto MCArmAppearance::InBetween() const -> std::optional<MCInBetween>
{
    // The animation stops at its last frame (Update), which has no in-between.
    const int32_t next = CurrentFrame + 1;

    if (CurrentFrame < 0 || AppearType->ActorData.NumFrames <= static_cast<uint32_t>(next))
    {
        return std::nullopt;
    }

    return InBetweenOf(CurrentFrame, next, FrameShare, AppearType->ActorData.FrameRate);
}

auto MCArmAppearance::Update() -> int32_t
{
    if (CurrentFrame == -1)
    {
        CurrentFrame = 0;
    }

    const int32_t played = LastFrame;
    const MCArmActorData& data = AppearType->ActorData;
    FrameRate = data.FrameRate;
    CurrentTime = FrameLength + CurrentTime;
    const auto wholeFrames = static_cast<int32_t>(std::floor(static_cast<double>(CurrentTime * FrameRate)));
    // Port-only (HD art's in-betweens): how far the animation is through the frame shown.
    FrameShare = CurrentTime * FrameRate - static_cast<float>(wholeFrames);

    if (played < wholeFrames)
    {
        LastFrame = wholeFrames;
        const int32_t advanced = wholeFrames - played;

        if (advanced != 0)
        {
            CurrentFrame += advanced;

            if (data.NumFrames <= static_cast<uint32_t>(CurrentFrame))
            {
                CurrentFrame = static_cast<int32_t>(data.NumFrames - 1);
                return 0;
            }
        }
    }

    return 1;
}
