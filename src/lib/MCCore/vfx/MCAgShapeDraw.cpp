#include "stdafx.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCAgShape.h"

// The game's own shape drawers (mcx\vfx\vfx_translatedraw.cpp): VFX_shape_draw and VFX_shape_translate_draw
// rewritten by the MechCommander team in inline asm, adding translucent shapes blended through AlphaTable. Unlike
// VFX's routines they take window coordinates: the pane only clips.
//
// A shape is translucent when its data begins with the token pair 03 00 (a one-pixel literal of colour 0, which
// blends to the screen pixel unchanged); its pixels then become AlphaTable[shape << 8 | screen].
//
// Each routine repeated the same asm body with its own off-by-ones in the clipping (OB-112); the port clips every one
// to the pane's rectangle within the window.

void MCAgDrawShape(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                   int32_t hotY, MCShapeOp op, std::optional<MCColorTable> xlat)
{
    MCVfxClip clip;

    if (MCVfxClipPane(pane, clip) != 0 || shapeTable.empty())
    {
        return;
    }

    const std::span<const uint8_t> header = MCVfxShape(shapeTable, shapeNum);
    const int32_t left = MCVfxRead32(header, 0x08) + hotX;
    int32_t top = MCVfxRead32(header, 0x0c) + hotY;
    const int32_t right = MCVfxRead32(header, 0x10) + hotX;
    const int32_t bottom = MCVfxRead32(header, 0x14) + hotY;

    if (right < clip.X0 || left > clip.X1 || bottom < clip.Y0 || top > clip.Y1 || right < left || bottom < top)
    {
        return;
    }

    int32_t skip = 0;

    if (top < clip.Y0)
    {
        skip = clip.Y0 - top;
        top = clip.Y0;
    }

    MCShapeCommand command;
    command.ShapeTable = shapeTable;
    command.ShapeNum = shapeNum;
    command.SkipRows = skip;
    command.Rows = std::min(bottom, clip.Y1) - top + 1;
    command.Top = top;
    command.Left = left;
    command.Lo = clip.X0;
    command.Hi = clip.X1;
    command.ClipTop = clip.Y0;
    command.ClipBottom = clip.Y1;
    command.Op = op;
    command.Table = xlat;
    MCRenderer::For(clip.Target()).Shape(clip.Target(), command);
}

void AGShapeDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX, int32_t hotY)
{
    if (MCAgShapeIsAlpha(shapeTable, shapeNum))
    {
        MCAgDrawShape(pane, shapeTable, shapeNum, hotX, hotY, MCShapeOp::Alpha, std::nullopt);
    }
    else
    {
        MCAgDrawShape(pane, shapeTable, shapeNum, hotX, hotY, MCShapeOp::Draw, std::nullopt);
    }
}

void AGShapeTranslateDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                          int32_t hotY, MCColorTable lookaside)
{
    // The clipped loop also checked, after every skip, that the destination pointer's top nibble was 8 (the Win9x
    // shared arena DirectDraw surfaces were mapped in) and gave up on the shape otherwise. The port always
    // continues, as on the video surfaces the check was written for.
    if (MCAgShapeIsAlpha(shapeTable, shapeNum))
    {
        MCAgDrawShape(pane, shapeTable, shapeNum, hotX, hotY, MCShapeOp::XlatAlpha, lookaside);
    }
    else
    {
        MCAgDrawShape(pane, shapeTable, shapeNum, hotX, hotY, MCShapeOp::Xlat, lookaside);
    }
}
