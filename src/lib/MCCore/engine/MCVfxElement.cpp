#include "stdafx.h"
#include "engine/MCVfxElement.h"
#include "camera/MCCamera.h"
#include "engine/MCScaleDraw.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"

MCVfxElement::MCVfxElement(std::span<const uint8_t> shape, int32_t x, int32_t y, int32_t frame, int32_t reverse,
                           std::optional<MCColorTable> fadeTable, bool noScaleDraw)
    : MCElement(-y)
    , ShapeTable(shape)
    , FrameNum(frame)
    , X(x)
    , Y(y)
    , Reverse(reverse)
    , FadeTable(fadeTable)
    , NoScaleDraw(noScaleDraw)
{
    ClampFrame();
}

MCVfxElement::MCVfxElement(std::span<const uint8_t> shape, float x, float y, int32_t frame, int32_t reverse,
                           std::optional<MCColorTable> fadeTable, bool noScaleDraw)
    : MCElement(-y)
    , ShapeTable(shape)
    , FrameNum(frame)
    , Reverse(reverse)
    , FadeTable(fadeTable)
    , NoScaleDraw(noScaleDraw)
{
    // Rounded down and cut to 16 bits, as the original's __ftol into a short.
    X = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(x))));
    Y = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(y))));
    ClampFrame();
}

auto MCVfxElement::ClampFrame() -> void
{
    const int32_t count = VfxShapeCount(ShapeTable);

    if (count <= FrameNum)
    {
        FrameNum = count - 1;
    }
}

auto MCVfxElement::Draw() -> void
{
    const MCRenderer::GroundFillScope fill(GroundFill);

    if (!NoScaleDraw)
    {
        ScaleDraw(ShapeTable, static_cast<uint32_t>(FrameNum), X, Y, Reverse, FadeTable);
        return;
    }

    if (!VfxIsShapeTable(ShapeTable))
    {
        return;
    }

    if (Reverse == 0)
    {
        if (!FadeTable.has_value())
        {
            AGShapeDraw(*GlobalPane, ShapeTable, FrameNum, X, Y);
            return;
        }

        AGShapeTranslateDraw(*GlobalPane, ShapeTable, FrameNum, X, Y, *FadeTable);
        return;
    }

    if (FadeTable.has_value())
    {
        AGShapeTranslateTransform(*GlobalPane, ShapeTable, FrameNum, X, Y, Reverse, 1, *FadeTable);
        return;
    }

    AGShapeTransform(*GlobalPane, ShapeTable, FrameNum, X, Y, Reverse, 1);
}
