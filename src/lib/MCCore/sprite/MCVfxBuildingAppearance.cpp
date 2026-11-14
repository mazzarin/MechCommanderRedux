#include "stdafx.h"
#include "sprite/MCVfxBuildingAppearance.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCForces.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Adds a VFX element of frame <paramref name="frame"/> of <paramref name="shapeTable"/>, unscaled.</summary>
    auto AddShape(std::span<const uint8_t> shapeTable, float x, float y, int32_t frame,
                  std::optional<MCColorTable> fadeTable) -> void
    {
        ElementList().Add(ElementList().Make<MCVfxElement>(shapeTable, x, y, frame, 0, fadeTable, 1));
    }

    /// <summary>The building <paramref name="obj"/> is, or null for any other class.</summary>
    auto AsBuilding(std::shared_ptr<MCGameObject> obj) -> std::shared_ptr<MCBuilding>
    {
        return obj != nullptr && obj->ObjectClass == MCObjectClass::Building ? std::static_pointer_cast<MCBuilding>(obj)
                                                                             : nullptr;
    }
}

auto MCVfxBuildingAppearance::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    HazeLevel = 0;
    DamageSet = false;
    DamageLevel = 0;
    InView = false;
    AnimState = -1;
    BuildType = std::static_pointer_cast<MCVfxBuildingAppearanceType>(tree);
    const int32_t result = MCVfxAppearance::Init(std::move(tree), std::move(obj));
    return result;
}

auto MCVfxBuildingAppearance::Update() -> int32_t
{
    if (Visible && Owner.lock()->IsCaptured() != 0 && !Highlighted && !Highlighting)
    {
        Highlighting = true;
        HighlightTime = 3.0f;
    }

    return 1;
}

auto MCVfxBuildingAppearance::SetDamageLvl(uint32_t newDamageLevel) -> void
{
    DamageSet = true;
    const uint32_t level = newDamageLevel & 0xf;
    DamageLevel = level;

    if (level != 0 && BuildType->NumFrames <= level)
    {
        DamageLevel = BuildType->NumFrames - 1;
    }

    if (TileNum > 9)
    {
        TileNum = 0;
    }
}

auto MCVfxBuildingAppearance::RecalcBounds(MCOptionalRef<MCCamera>) -> int
{
    // Faithful: without a tile shape the visibility test takes in the previous shape's bounds.
    float tileMinX = ShapeMinX;
    float tileMinY = ShapeMinY;
    float tileMaxX = ShapeMaxX;
    float tileMaxY = ShapeMaxY;
    int result = 0;

    if (const std::shared_ptr<MCShape> shape = CurrentShape.lock(); shape != nullptr && !shape->FrameList().empty())
    {
        const MCFrameBounds bounds = MCShapeFrameBounds(shape->FrameList(), 0);
        ShapeMinX = bounds.MinX;
        ShapeMinY = bounds.MinY;
        ShapeMaxX = bounds.Width;
        ShapeMaxY = bounds.Height;
        // Faithful: the last shape size read is what an off-screen building returns.
        result = VfxShapeResolution(shape->FrameList(), 0);

        if (const std::shared_ptr<MCShape> tileShape = TileShape.lock();
            tileShape != nullptr && !tileShape->FrameList().empty())
        {
            const MCFrameBounds tile = MCShapeFrameBounds(tileShape->FrameList(), 0);
            tileMinX = tile.MinX;
            tileMinY = tile.MinY;
            tileMaxX = tile.Width;
            tileMaxY = tile.Height;
            result = VfxShapeResolution(tileShape->FrameList(), 0);
        }

        InView = true;
    }

    if (Eye() == nullptr)
    {
        return result;
    }

    // Faithful: the camera passed in is ignored; the eye's screen and screen position 0 are used.
    const std::shared_ptr<MCGameObject> owner = Owner.lock();
    const std::shared_ptr<MCBuilding> building = AsBuilding(owner);
    const MCVector2D pos = owner->GetScreenPos(0);
    float x = pos.X;
    float y = pos.Y;

    if (building != nullptr)
    {
        x = static_cast<float>(building->PixelOffsetX) + x;
        y = static_cast<float>(building->PixelOffsetY) + y;
    }

    UpperLeft.X = ShapeMinX + x;
    UpperLeft.Y = ShapeMinY + y;
    LowerRight.X = ShapeMaxX + UpperLeft.X;
    LowerRight.Y = ShapeMaxY + UpperLeft.Y;

    // On screen when the building or its tile is.
    const float minX = ShapeMinX > tileMinX ? tileMinX : ShapeMinX;
    const float minY = ShapeMinY > tileMinY ? tileMinY : ShapeMinY;
    const float maxX = ShapeMaxX < tileMaxX ? tileMaxX : ShapeMaxX;
    const float maxY = ShapeMaxY < tileMaxY ? tileMaxY : ShapeMaxY;
    const float left = minX + x;
    const float top = minY + y;
    const float bottom = maxY + top;

    if (0.0f <= maxX + left && 0.0f <= bottom &&
        left <= static_cast<float>(static_cast<int32_t>(std::floor(Eye()->ViewWidth))) &&
        top <= static_cast<float>(static_cast<int32_t>(std::floor(Eye()->ViewHeight))))
    {
        return 1;
    }

    return result;
}

auto MCVfxBuildingAppearance::CalcCollideBounds() -> void
{
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    if (const std::shared_ptr<MCBuilding> building = AsBuilding(Owner.lock()))
    {
        offsetX = static_cast<float>(building->PixelOffsetX);
        offsetY = static_cast<float>(building->PixelOffsetY);
    }

    UpperLeft.X = offsetX;
    UpperLeft.Y = offsetY;
    LowerRight.Y = offsetY;
    LowerRight.X = offsetX;

    const std::shared_ptr<MCShape> shape = CurrentShape.lock();

    if (shape == nullptr || shape->FrameList().empty())
    {
        return;
    }

    const std::span<const uint8_t> shapeTable = shape->FrameList();
    int32_t frame = CurrentFrame;
    const int32_t numShapeFrames = VfxShapeCount(shapeTable);

    if (frame == -1)
    {
        frame = 0;
    }

    if (numShapeFrames <= frame)
    {
        frame = numShapeFrames - 1;
    }

    const MCFrameBounds bounds = MCShapeFrameBounds(shapeTable, frame);
    UpperLeft.X = bounds.MinX + offsetX;
    UpperLeft.Y = bounds.MinY + offsetY;
    LowerRight.X = bounds.Width + UpperLeft.X;
    LowerRight.Y = bounds.Height + UpperLeft.Y;
}

auto MCVfxBuildingAppearance::ReadAhead() -> void
{
    if (BuildType == nullptr)
    {
        return;
    }

    const MCAppearanceType::MCAskingAhead asking;
    BuildType->GetShape(DamageLevel);
    BuildType->GetShape(DamageLevel + 1);
    BuildType->GetTileShape(TileNum);
}

auto MCVfxBuildingAppearance::Render(int32_t) -> int32_t
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    const std::shared_ptr<MCBuilding> building = AsBuilding(obj);

    if (building != nullptr)
    {
        ScreenPos = building->GetScreenPos(Eye()->CameraId - 1);
    }

    MCVfxBuildingAppearanceType& type = *BuildType;
    const std::shared_ptr<MCShape> shape = type.GetShape(DamageLevel);
    CurrentShape = shape;
    const std::shared_ptr<MCShape> tile = type.GetTileShape(TileNum);
    TileShape = tile;
    const std::optional<MCColorTable> hazeTable = GamePalette().GetHazePalette(HazeLevel);

    // The tile goes under everything. Its table's second (small) frame was the original's zoomed-out mode's.
    ElementList().OpenGroup(20000000, true);

    if (tile != nullptr && !tile->FrameList().empty())
    {
        AddShape(tile->FrameList(), ScreenPos.X, ScreenPos.Y, 0, hazeTable);
    }

    if (building != nullptr)
    {
        ScreenPos.X = static_cast<float>(building->PixelOffsetX) + ScreenPos.X;
        ScreenPos.Y = static_cast<float>(building->PixelOffsetY) + ScreenPos.Y;
    }

    if (shape != nullptr && !shape->FrameList().empty())
    {
        int32_t frame = 0;

        if (AnimState != -1)
        {
            const int32_t numShapeFrames = VfxShapeCount(shape->FrameList());

            if (numShapeFrames <= CurrentFrame)
            {
                CurrentFrame = numShapeFrames - 1;
            }

            frame = CurrentFrame;
        }

        const auto depth = static_cast<int32_t>(-ScreenPos.Y);
        ElementList().OpenGroup(depth, true);
        AddShape(shape->FrameList(), ScreenPos.X, ScreenPos.Y, frame, hazeTable);
        // Port-only (sun shadows): the building (not its tile) casts its own shadow at dusk.
        const MCSceneLights::MCSunShadowPart part{shape->FrameList(), ScreenPos.X, ScreenPos.Y, frame};
        MCSceneLights::AddRecastShadows(std::span(&part, 1), depth, true);
    }

    if (obj->Selected == 1 || obj->GetNumAttackers() > 0)
    {
        DrawBars();
    }

    if (obj != nullptr && obj->Selected != 0)
    {
        const int32_t homeAlignment = HomeTeam()->Alignment;
        RecalcBounds(EyeRef());

        if (obj->GetAlignment() != homeAlignment && obj->GetAlignment() != 0 && static_cast<uint8_t>(obj->Status) != 2)
        {
            DrawSelectBrackets(0xcf);
        }
        else
        {
            DrawSelectBox(0xe1);
        }
    }

    if (DrawTerrainGrid != 0)
    {
        DrawSelectBox(0xe1);
    }

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

auto MCVfxBuildingAppearance::DrawBars() -> void
{
    MCDrawDamageBar(*this, GetAppearanceType(), Owner.lock());
}
