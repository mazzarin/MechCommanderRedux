#include "stdafx.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCAgShape.h"

// The game's shape "transforms" (mcx\vfx\vfx_transform.cpp): a shape is rendered opaque into a scratch buffer
// (AG_shape_fill / AG_shape_translate_fill, skipped pixels as colour 0), then blended onto the pane at full or half
// size, optionally mirrored, through AlphaTable (CopySprite). Colour 0 blends to the screen pixel unchanged, so the
// shape's skipped pixels stay transparent. The port hands both steps to the renderer as one command (ShapeBlit): the
// software renderer keeps its own scratch picture, and a hardware renderer draws the shape's picture without one.

namespace
{
    /// <summary>
    /// CopySprite's clipping: the blit of a <paramref name="width"/> x <paramref name="height"/> sprite at
    /// (<paramref name="x"/>, <paramref name="y"/>) of the pane into <paramref name="command"/>; false when nothing
    /// is drawn.
    /// </summary>
    bool ClipSprite(const MCPane& pane, const MCWindow& window, int x, int y, int width, int height, int mirror,
                    int fullSize, MCAlphaBlitCommand& command)
    {
        const int32_t stride = window.XMax + 1;
        int32_t cx0 = pane.X0 < 0 ? 0 : pane.X0;
        int32_t cy0 = pane.Y0 < 0 ? 0 : pane.Y0;
        int32_t cx1 = pane.X1 < stride ? pane.X1 : window.XMax;
        int32_t cy1 = pane.Y1 < window.YMax + 1 ? pane.Y1 : window.YMax;
        MCClipToView(window, cx0, cy0, cx1, cy1);

        // OB-115: (x, y) are relative to the pane's origin; the asm offset by its corner clipped to the window.
        int32_t left = x + pane.X0;
        int32_t top = y + pane.Y0;
        const int32_t pitch = width;
        int32_t columns = width;
        int32_t rows = height;
        intptr_t sourceOffset = 0;

        if (left < cx0)
        {
            const int32_t over = left - cx0; // negative
            if (fullSize != 0)
            {
                columns += over;
                sourceOffset += mirror != 0 ? over : -over;
            }
            else
            {
                columns += over * 2;
                sourceOffset += mirror != 0 ? over * 2 : -over * 2;
            }

            left = cx0;
        }

        if (top < cy0)
        {
            // OB-119: the asm stepped the source down by the (possibly already clipped) column count, not the sprite's
            // pitch, so a sprite clipped on the left and the top read its rows skewed.
            const int32_t over = top - cy0; // negative
            if (fullSize != 0)
            {
                rows += over;
                sourceOffset += static_cast<intptr_t>(-over) * pitch;
            }
            else
            {
                rows += over * 2;
                sourceOffset += static_cast<intptr_t>(-over) * pitch * 2;
            }

            top = cy0;
        }

        if (fullSize != 0)
        {
            if (left + columns > cx1 + 1)
            {
                columns = cx1 - left + 1;
            }

            if (rows + top > cy1 + 1)
            {
                rows = cy1 - top + 1;
            }
        }
        else
        {
            if ((columns >> 1) + left > cx1 + 1)
            {
                columns = (cx1 - left) * 2 + 2;
            }

            if ((rows >> 1) + top > cy1 + 1)
            {
                rows = (cy1 - top) * 2 + 2;
            }
        }

        // OB-116: the asm counted a sprite starting on the last column or row as outside.
        if (cx1 < cx0 || cy1 < cy0 || left > cx1 || top > cy1 || left <= cx0 - columns || top <= cy0 - rows ||
            columns <= 0 || rows <= 0)
        {
            return false;
        }

        // Half size: every other pixel of every other row; nothing when that leaves none.
        if (fullSize == 0 && ((static_cast<uint32_t>(rows) >> 1) == 0 || (static_cast<uint32_t>(columns) >> 1) == 0))
        {
            return false;
        }

        command.Sprite = {};
        command.Offset = sourceOffset;
        command.Pitch = pitch;
        command.Left = left;
        command.Top = top;
        command.Columns = columns;
        command.Rows = rows;
        command.Mirror = mirror != 0;
        command.FullSize = fullSize != 0;
        command.Clip = {cx0, cy0, cx1, cy1};
        return true;
    }

    /// <summary>
    /// The body of AG_shape_transform and AG_shape_translate_transform: renders the shape (through
    /// <paramref name="table"/> when there is one), then copies it to the pane.
    /// </summary>
    void TransformShape(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                        int32_t hotY, int32_t mirror, int32_t fullSize, std::optional<MCColorTable> table)
    {
        const std::shared_ptr<MCWindow> window = pane.Window.lock();

        if (window == nullptr || shapeTable.empty())
        {
            return;
        }

        const std::span<const uint8_t> header = MCVfxShape(shapeTable, shapeNum);
        const int32_t xMin = MCVfxRead32(header, 0x08);
        const int32_t yMin = MCVfxRead32(header, 0x0c);
        const int32_t xMax = MCVfxRead32(header, 0x10);
        const int32_t yMax = MCVfxRead32(header, 0x14);
        const int32_t width = xMax - xMin + 1;
        const int32_t height = yMax - yMin + 1;
        // The original filled its callers' buffer (through a static scratch window and pane) before clipping the copy,
        // and skipped shapes of 0x1fa40 pixels or more, which the buffer couldn't hold; the renderer fills its own
        // picture, only for a copy that draws.
        int32_t x = 0;
        int32_t y = 0;

        if (fullSize != 0)
        {
            x = mirror != 0 ? hotX - xMax : xMin + hotX;
            y = yMin + hotY;
        }
        else
        {
            x = mirror != 0 ? hotX - (xMax >> 1) : (xMin >> 1) + hotX;
            y = (yMin >> 1) + hotY;
        }

        MCShapeBlitCommand command;

        if (!ClipSprite(pane, *window, x, y, width, height, mirror, fullSize, command.Blit))
        {
            return;
        }

        command.ShapeTable = shapeTable;
        command.ShapeNum = shapeNum;
        command.Table = table;
        command.Width = width;
        command.Height = height;
        MCRenderer::For(*window).ShapeBlit(*window, command);
    }
}

void AGShapeTransform(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                      int32_t hotY, int32_t mirror, int32_t fullSize)
{
    TransformShape(pane, shapeTable, shapeNum, hotX, hotY, mirror, fullSize, std::nullopt);
}

void AGShapeTranslateTransform(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                               int32_t hotY, int32_t mirror, int32_t fullSize, MCColorTable lookaside)
{
    TransformShape(pane, shapeTable, shapeNum, hotX, hotY, mirror, fullSize, lookaside);
}

void CopySprite(const MCPane& pane, std::span<const uint8_t> sprite, int x, int y, int width, int height, int mirror,
                int fullSize)
{
    const std::shared_ptr<MCWindow> window = pane.Window.lock();
    MCAlphaBlitCommand command;

    if (window == nullptr || !ClipSprite(pane, *window, x, y, width, height, mirror, fullSize, command))
    {
        return;
    }

    command.Sprite = sprite.first(static_cast<size_t>(width) * static_cast<size_t>(height));
    MCRenderer::For(*window).AlphaBlit(*window, command);
}

void CopySprite(const MCPane& pane, const MCWindow& sprite, int x, int y, int width, int height, int mirror,
                int fullSize)
{
    const std::shared_ptr<MCWindow> window = pane.Window.lock();
    MCAlphaBlitCommand command;

    if (window == nullptr || !ClipSprite(pane, *window, x, y, width, height, mirror, fullSize, command))
    {
        return;
    }

    if (const std::shared_ptr<MCTexture> texture = sprite.Texture.lock(); texture != nullptr)
    {
        command.Texture = *texture;
    }

    command.Sprite = sprite.Bytes();
    MCRenderer::For(*window).AlphaBlit(*window, command);
}
