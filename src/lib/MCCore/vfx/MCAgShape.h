#pragma once

// Port-only: the body behind the game's AG_shape_* routines (vfx\vfx_translatedraw.cpp and vfx\vfx_transform.cpp in
// MCX.EXE). The original repeated the same inline-asm body in every routine; the port has it once. Defined in
// vfx/MCAgShapeDraw.cpp. Each routine's op (MCShapeOp): AG_shape_draw Draw, or Alpha for a marked shape;
// AG_shape_translate_draw Xlat, or XlatAlpha for a marked shape; AG_shape_fill Fill; AG_shape_translate_fill XlatFill.
// The translating ops map through the caller's lookaside table.

#include "vfx/MCVfxClip.h"

/// <summary>
/// Draws shape <paramref name="shapeNum"/> of <paramref name="shapeTable"/> with its hot spot at window coordinates
/// (hotX, hotY) (the AG routines don't offset by the pane's origin), clipped to the pane, applying
/// <paramref name="op"/> to each pixel with the table <paramref name="xlat"/> where the op uses one.
/// </summary>
void MCAgDrawShape(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                   int32_t hotY, MCShapeOp op, std::optional<MCColorTable> xlat);

/// <summary>Whether the game's shape routines treat a shape as translucent: its data starts with the token pair 03 00.</summary>
inline bool MCAgShapeIsAlpha(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    // OB-190 (fixed): a frame the table lists but doesn't hold (MCVfxHasShape) draws nothing.
    if (!MCVfxHasShape(shapeTable, shapeNum))
    {
        return false;
    }

    const std::span<const uint8_t> data = MCVfxShape(shapeTable, shapeNum).subspan(0x18);

    // OB-171: an empty shape last in its table (the encoder's all-transparent header, no rows) has no data; the
    // original read the two bytes past the table. It draws nothing either way.
    if (data.size() < 2)
    {
        return false;
    }

    return data[0] == 3 && data[1] == 0;
}
