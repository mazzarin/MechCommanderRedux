#include "stdafx.h"
#include "vfx/MCVfxClip.h"

// VFX's primitives (vfxa.asm in MCX.EXE): pixels, lines, hashed rectangles, pane wipes, copies and scrolls, and
// ellipses. (The asm's display-driver hooks, VFX_driver_name and VFX_register_driver, have no use in the port.)

int32_t VfxPixelWrite(const MCPane& pane, int32_t x, int32_t y, uint8_t color)
{
    MCVfxClip clip;
    const int32_t status = MCVfxClipPane(pane, clip);

    if (status != 0)
    {
        return status;
    }

    x += clip.PaneX;
    y += clip.PaneY;

    if (x < clip.X0 || x > clip.X1 || y < clip.Y0 || y > clip.Y1)
    {
        return VfxErrClipped;
    }

    // The pixel it replaces is returned (no caller uses it; a view has no pixels to read).
    const int32_t previous = clip.HasPixels() ? clip.Row(x, y)[0] : 0;
    MCRenderer::For(clip.Target()).Pixel(clip.Target(), x, y, color);
    return previous;
}

namespace
{
    /// <summary>
    /// How VFX_line_draw paints a line's pixels (its mode and parm). LD_EXECUTE, a callback per pixel, had no callers
    /// in the game and went with the port's VfxLineExecute.
    /// </summary>
    struct MCLinePaint
    {
        /// <summary>The colour (LD_DRAW).</summary>
        uint8_t Color = 0;
        /// <summary>The table each pixel is mapped through (LD_TRANSLATE), if any.</summary>
        std::optional<MCColorTable> Table;
        /// <summary>Port-only: the line's width where it is drawn smooth (MCLineCommand::Width).</summary>
        float Width = 1.0f;
    };

    /// <summary>
    /// <c>round(a * slope / 2^32)</c>: the asm's <c>mul</c> then <c>add eax, 0x80000000 / adc edx, 0</c>, the minor-axis
    /// offset at major-axis distance <paramref name="a"/>.
    /// </summary>
    int32_t MinorFromMajor(uint32_t a, uint32_t slope)
    {
        return static_cast<int32_t>((static_cast<uint64_t>(a) * slope + 0x80000000u) >> 32);
    }

    /// <summary>
    /// The major-axis distance at which the line reaches minor-axis distance <paramref name="a"/>, rounded as the asm
    /// does when clipping the line's start: <c>ceil((a - 1/2) * 2^32 / slope)</c>.
    /// </summary>
    int32_t MajorFromMinorStart(uint32_t a, uint32_t slope)
    {
        const uint64_t dividend = (static_cast<uint64_t>(a - 1) << 32) | 0x80000000u;
        // Port fix: the asm's DIV faults when the quotient exceeds 32 bits (never for a line the clip loop keeps).
        const uint64_t quotient = dividend / slope;
        const uint64_t remainder = dividend % slope;
        return static_cast<int32_t>(static_cast<uint32_t>(quotient + (remainder != 0 ? 1 : 0)));
    }

    /// <summary>
    /// The same for the line's end: <c>ceil((a + 1/2) * 2^32 / slope) - 1</c>, the last major-axis step still at or
    /// before minor-axis distance <paramref name="a"/>.
    /// </summary>
    int32_t MajorFromMinorEnd(uint32_t a, uint32_t slope)
    {
        const uint64_t dividend = (static_cast<uint64_t>(a) << 32) | 0x80000000u;
        const uint64_t quotient = dividend / slope;
        const uint64_t remainder = dividend % slope;
        return static_cast<int32_t>(static_cast<uint32_t>(quotient - (remainder == 0 ? 1 : 0)));
    }

    /// <summary><paramref name="value"/> negated when <paramref name="mask"/> is -1, unchanged when it is 0.</summary>
    int32_t Negate(int32_t value, int32_t mask)
    {
        return static_cast<int32_t>((static_cast<uint32_t>(value) ^ static_cast<uint32_t>(mask)) -
                                    static_cast<uint32_t>(mask));
    }

    /// <summary>The outcode of a point: 8 left of, 4 right of, 2 above, 1 below the clip rectangle.</summary>
    uint32_t OutCode(const MCVfxClip& clip, int32_t x, int32_t y)
    {
        return (x < clip.X0 ? 8u : 0u) | (x > clip.X1 ? 4u : 0u) | (y < clip.Y0 ? 2u : 0u) | (y > clip.Y1 ? 1u : 0u);
    }

    /// <summary>
    /// Draws the resolved line in one colour or through a table: <paramref name="count"/> pixels from (x, y), moving
    /// by the major step each pixel and also by the minor step when the fraction carries.
    /// </summary>
    void DrawLine(MCWindow& window, int32_t x, int32_t y, int32_t count, int32_t majorX, int32_t majorY, int32_t minorX,
                  int32_t minorY, uint32_t slope, uint32_t fraction, const MCLinePaint& paint)
    {
        MCLineCommand command;
        command.X = x;
        command.Y = y;
        command.Count = count;
        command.MajorX = majorX;
        command.MajorY = majorY;
        command.MinorX = minorX;
        command.MinorY = minorY;
        command.Slope = slope;
        command.Fraction = fraction;
        command.Table = paint.Table;
        command.Color = paint.Color;
        command.Width = paint.Width;
        MCRenderer::For(window).Line(window, command);
    }
}

namespace
{
    /// <summary>
    /// VFX_line_draw: the line from (x0, y0) to (x1, y1) clipped to the pane, each pixel painted as
    /// <paramref name="paint"/> says.
    /// </summary>
    int32_t LineDraw(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const MCLinePaint& paint)
    {
        MCVfxClip clip;
        const int32_t status = MCVfxClipPane(pane, clip);

        if (status != 0)
        {
            return status;
        }

        // Window coordinates: (P, Q) is the start as given, the reference every clipped coordinate is measured from.
        x0 += clip.PaneX;
        x1 += clip.PaneX;
        y0 += clip.PaneY;
        y1 += clip.PaneY;
        const int32_t p = x0;
        const int32_t q = y0;

        const int32_t dx = x1 - x0;
        const int32_t signX = dx < 0 ? -1 : 0;
        const int32_t adx = dx < 0 ? -dx : dx;
        const int32_t dy = y1 - y0;
        const int32_t signY = dy < 0 ? -1 : 0;
        const int32_t ady = dy < 0 ? -dy : dy;

        int32_t startX = x0;
        int32_t startY = y0;
        int32_t endX = x1;
        int32_t endY = y1;
        int32_t count;
        int32_t stepX = 0;
        int32_t stepY = 0;

        if (dx != 0 && dy != 0)
        {
            const int32_t sameSign = signX ^ signY;
            const int32_t notSame = ~sameSign;
            // The minor axis's advance per major-axis pixel as a 0.32 fraction (all ones for a 45-degree line).
            uint32_t slope = 0xffffffffu;

            if (adx < ady)
            {
                slope = static_cast<uint32_t>((static_cast<uint64_t>(adx) << 32) / static_cast<uint32_t>(ady));
            }
            else if (adx > ady)
            {
                slope = static_cast<uint32_t>((static_cast<uint64_t>(ady) << 32) / static_cast<uint32_t>(adx));
            }

            const bool xMajor = adx >= ady;

            // Clip one end against one edge at a time until both ends are inside or both lie beyond one edge.
            uint32_t everClipped = 0;
            int guard = 0;

            for (;;)
            {
                const uint32_t codeStart = OutCode(clip, startX, startY);
                const uint32_t codeEnd = OutCode(clip, endX, endY);
                everClipped |= codeStart | codeEnd;

                if ((codeStart | codeEnd) == 0)
                {
                    break;
                }

                // Port fix: the asm loops for as long as rounding keeps an end outside; give up as clipped instead.
                if ((codeStart & codeEnd) != 0 || ++guard > 16)
                {
                    return 2;
                }

                if (xMajor)
                {
                    if (codeStart & 8)
                    {
                        startX = clip.X0;
                        startY = q + Negate(MinorFromMajor(static_cast<uint32_t>(clip.X0 - p), slope), sameSign);
                    }
                    else if (codeStart & 4)
                    {
                        startX = clip.X1;
                        startY = q + Negate(MinorFromMajor(static_cast<uint32_t>(p - clip.X1), slope), notSame);
                    }
                    else if (codeStart & 2)
                    {
                        startY = clip.Y0;
                        startX = p + Negate(MajorFromMinorStart(static_cast<uint32_t>(clip.Y0 - q), slope), sameSign);
                    }
                    else if (codeStart & 1)
                    {
                        startY = clip.Y1;
                        startX = p + Negate(MajorFromMinorStart(static_cast<uint32_t>(q - clip.Y1), slope), notSame);
                    }
                    else if (codeEnd & 8)
                    {
                        endX = clip.X0;
                        endY = q + Negate(MinorFromMajor(static_cast<uint32_t>(p - clip.X0), slope), notSame);
                    }
                    else if (codeEnd & 4)
                    {
                        endX = clip.X1;
                        endY = q + Negate(MinorFromMajor(static_cast<uint32_t>(clip.X1 - p), slope), sameSign);
                    }
                    else if (codeEnd & 2)
                    {
                        endY = clip.Y0;
                        endX = p + Negate(MajorFromMinorEnd(static_cast<uint32_t>(q - clip.Y0), slope), notSame);
                    }
                    else
                    {
                        endY = clip.Y1;
                        endX = p + Negate(MajorFromMinorEnd(static_cast<uint32_t>(clip.Y1 - q), slope), sameSign);
                    }
                }
                else
                {
                    if (codeStart & 8)
                    {
                        startX = clip.X0;
                        startY = q + Negate(MajorFromMinorStart(static_cast<uint32_t>(clip.X0 - p), slope), sameSign);
                    }
                    else if (codeStart & 4)
                    {
                        startX = clip.X1;
                        startY = q + Negate(MajorFromMinorStart(static_cast<uint32_t>(p - clip.X1), slope), notSame);
                    }
                    else if (codeStart & 2)
                    {
                        startY = clip.Y0;
                        startX = p + Negate(MinorFromMajor(static_cast<uint32_t>(clip.Y0 - q), slope), sameSign);
                    }
                    else if (codeStart & 1)
                    {
                        startY = clip.Y1;
                        startX = p + Negate(MinorFromMajor(static_cast<uint32_t>(q - clip.Y1), slope), notSame);
                    }
                    else if (codeEnd & 8)
                    {
                        endX = clip.X0;
                        endY = q + Negate(MajorFromMinorEnd(static_cast<uint32_t>(p - clip.X0), slope), notSame);
                    }
                    else if (codeEnd & 4)
                    {
                        endX = clip.X1;
                        endY = q + Negate(MajorFromMinorEnd(static_cast<uint32_t>(clip.X1 - p), slope), sameSign);
                    }
                    else if (codeEnd & 2)
                    {
                        endY = clip.Y0;
                        endX = p + Negate(MinorFromMajor(static_cast<uint32_t>(q - clip.Y0), slope), notSame);
                    }
                    else
                    {
                        endY = clip.Y1;
                        endX = p + Negate(MinorFromMajor(static_cast<uint32_t>(clip.Y1 - q), slope), sameSign);
                    }
                }
            }

            const int32_t result = everClipped != 0 ? 1 : 0;

            // The row each pixel moves to: up or down.
            const int32_t rowStep = signY != 0 ? -1 : 1;

            if (adx != ady)
            {
                if (!xMajor)
                {
                    // y-major: one row per pixel, the column advancing when the fraction carries.
                    count = std::abs(endY - startY) + 1;
                    const uint32_t fraction = static_cast<uint32_t>(std::abs(startY - q)) * slope + 0x80000000u;
                    DrawLine(clip.Target(), startX, startY, count, 0, rowStep, signX != 0 ? -1 : 1, 0, slope, fraction,
                             paint);
                    return result;
                }

                // x-major: one column per pixel, the row advancing when the fraction carries.
                count = std::abs(endX - startX) + 1;
                const uint32_t fraction = static_cast<uint32_t>(std::abs(startX - p)) * slope + 0x80000000u;
                DrawLine(clip.Target(), startX, startY, count, signX != 0 ? -1 : 1, 0, 0, rowStep, slope, fraction,
                         paint);
                return result;
            }

            // 45 degrees: one row and one column per pixel.
            count = std::abs(endX - startX) + 1;
            DrawLine(clip.Target(), startX, startY, count, signX != 0 ? -1 : 1, rowStep, 0, 0, 0, 0, paint);
            return result;
        }

        // Vertical and horizontal lines (and single points) are clamped rather than clipped, and report 0 even when
        // clamped (original behaviour).
        if (dx == 0)
        {
            if (x0 < clip.X0 || x0 > clip.X1)
            {
                return 2;
            }

            if (std::max(y0, y1) < clip.Y0)
            {
                return 2;
            }

            if (std::min(y0, y1) > clip.Y1)
            {
                return 2;
            }

            startY = std::clamp(y0, clip.Y0, clip.Y1);
            endY = std::clamp(y1, clip.Y0, clip.Y1);
            startX = x0;
            stepY = signY != 0 ? -1 : 1;
            count = std::abs(endY - startY) + 1;
        }
        else
        {
            if (y0 < clip.Y0 || y0 > clip.Y1)
            {
                return 2;
            }

            if (std::max(x0, x1) < clip.X0)
            {
                return 2;
            }

            if (std::min(x0, x1) > clip.X1)
            {
                return 2;
            }

            startX = std::clamp(x0, clip.X0, clip.X1);
            endX = std::clamp(x1, clip.X0, clip.X1);
            startY = y0;
            stepX = signX != 0 ? -1 : 1;
            count = std::abs(endX - startX) + 1;
        }

        DrawLine(clip.Target(), startX, startY, count, stepX, stepY, 0, 0, 0, 0, paint);
        return 0;
    }
}

int32_t VfxLineDraw(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
{
    return LineDraw(pane, x0, y0, x1, y1, MCLinePaint{static_cast<uint8_t>(color), std::nullopt});
}

int32_t VfxLineDrawWide(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color, float width)
{
    return LineDraw(pane, x0, y0, x1, y1, MCLinePaint{static_cast<uint8_t>(color), std::nullopt, width});
}

int32_t VfxLineTranslate(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, MCColorTable table)
{
    return LineDraw(pane, x0, y0, x1, y1, MCLinePaint{0, table});
}

int32_t VfxPaneWipe(const MCPane& pane, int32_t color)
{
    MCVfxClip clip;
    const int32_t status = MCVfxClipPane(pane, clip);

    if (status != 0)
    {
        return status;
    }

    MCRenderer::For(clip.Target())
        .Clear(clip.Target(), MCRect{clip.X0, clip.Y0, clip.X1, clip.Y1}, static_cast<uint8_t>(color));
    return 0;
}

int32_t VfxTranslateRect(const MCPane& pane, const MCRect& rect, MCColorTable table)
{
    MCVfxClip clip;
    const int32_t status = MCVfxClipPane(pane, clip);

    if (status != 0)
    {
        return status;
    }

    const MCRect at{std::max(clip.X0, clip.PaneX + rect.X0), std::max(clip.Y0, clip.PaneY + rect.Y0),
                    std::min(clip.X1, clip.PaneX + rect.X1), std::min(clip.Y1, clip.PaneY + rect.Y1)};

    if (at.X0 <= at.X1 && at.Y0 <= at.Y1)
    {
        MCRenderer::For(clip.Target()).Translate(clip.Target(), at, table);
    }

    return 0;
}

int32_t VfxPaneCopy(const MCPane& source, int32_t sx, int32_t sy, const MCPane& target, int32_t tx, int32_t ty,
                    int32_t fill)
{
    MCVfxClip from;
    int32_t status = MCVfxClipPane(source, from);

    if (status != 0)
    {
        return status;
    }

    MCVfxClip to;
    status = MCVfxClipPane(target, to);

    if (status != 0)
    {
        return status;
    }

    // Both clip rectangles relative to their pane's origin.
    const int32_t sLeft = from.X0 - from.PaneX;
    const int32_t sTop = from.Y0 - from.PaneY;
    const int32_t sRight = from.X1 - from.PaneX;
    const int32_t sBottom = from.Y1 - from.PaneY;
    const int32_t tLeft = to.X0 - to.PaneX;
    const int32_t tTop = to.Y0 - to.PaneY;
    const int32_t tRight = to.X1 - to.PaneX;
    const int32_t tBottom = to.Y1 - to.PaneY;

    // The area both cover, in source coordinates, then in target coordinates.
    const int32_t offsetX = sx - tx;
    const int32_t offsetY = sy - ty;
    const int32_t left = std::max(sLeft, tLeft + offsetX);
    const int32_t top = std::max(sTop, tTop + offsetY);
    const int32_t right = std::min(sRight, tRight + offsetX);
    const int32_t bottom = std::min(sBottom, tBottom + offsetY);

    if (right < left || bottom < top)
    {
        return VfxErrClipped;
    }

    const int32_t toLeft = std::max(tLeft, sLeft - offsetX);
    const int32_t toTop = std::max(tTop, sTop - offsetY);
    const int32_t toRight = std::min(tRight, sRight - offsetX);
    const int32_t toBottom = std::min(tBottom, sBottom - offsetY);
    // The two areas in window coordinates.
    const MCRect sourceRect{left + from.PaneX, top + from.PaneY, right + from.PaneX, bottom + from.PaneY};
    const MCRect targetRect{toLeft + to.PaneX, toTop + to.PaneY, toRight + to.PaneX, toBottom + to.PaneY};
    MCRenderer& renderer = MCRenderer::For(to.Target());

    if (fill >= 0 && (fill & 0xffffff00) == 0)
    {
        // A colour: the target area is filled with it instead of copied.
        renderer.Clear(to.Target(), targetRect, static_cast<uint8_t>(fill));
        return 0;
    }

    // A plain copy (VfxNoColor and every negative value), or above 255 one that leaves the pixels of colour
    // (fill & 0xff) alone. Rows are copied downwards when the source row is below the target row (in pane
    // coordinates), else upwards; columns likewise, so an overlapping copy within a window survives.
    MCCopyCommand command{.Source = from.Target()};
    command.SourceRect = sourceRect;
    command.X = targetRect.X0;
    command.Y = targetRect.Y0;
    command.ColorKey = fill >= 0;
    command.Key = static_cast<uint8_t>(fill);
    command.Downwards = top > toTop;
    command.Rightwards = left > toLeft;
    renderer.Copy(to.Target(), command);
    return 0;
}

int32_t VfxPaneScroll(const MCPane& pane, int32_t dx, int32_t dy, int32_t mode, int32_t parm)
{
    const int32_t width = pane.X1 + 1 - pane.X0;
    const int32_t height = pane.Y1 + 1 - pane.Y0;

    if (width <= 0 || height <= 0)
    {
        return VfxErrEmptyPane;
    }

    if (mode != 1)
    {
        if (std::abs(dx) >= width || std::abs(dy) >= height)
        {
            return VfxPaneWipe(pane, parm & 0xff);
        }
    }
    else
    {
        // PS_WRAP: the pane is copied to a scratch window first. parm was that window's buffer (a pointer passed as
        // a long; a call with 0 returned the size it needs); the port allocates it.
        if (parm == 0)
        {
            return width * height;
        }

        const MCPane wrapPane = MCPane::Over(MCWindow::Make(width, height));
        VfxPaneCopy(pane, 0, 0, wrapPane, 0, 0, VfxNoColor);
        dx %= width;
        dy %= height;

        if ((dx | dy) != 0)
        {
            VfxPaneCopy(wrapPane, 0, 0, pane, dx, dy, VfxNoColor);
            const int32_t offsets[8][2] = {{width, height}, {width, 0},       {width, -height}, {0, height},
                                           {0, -height},    {-width, height}, {-width, 0},      {-width, -height}};

            for (const auto& offset : offsets)
            {
                VfxPaneCopy(wrapPane, offset[0], offset[1], pane, dx, dy, VfxNoColor);
            }
        }

        return 0;
    }

    if ((dx | dy) == 0)
    {
        return 0;
    }

    const MCPane& source = pane;
    const int32_t fill = parm;
    VfxPaneCopy(source, 0, 0, pane, dx, dy, VfxNoColor);
    // The eight neighbours of the moved image: with a colour they fill the uncovered strips (VFX_pane_copy fills
    // with a colour of 0..255); with VfxNoColor they copy the pane's already-moved pixels (original behaviour).
    const int32_t offsets[8][2] = {{width, height}, {width, 0},       {width, -height}, {0, height},
                                   {0, -height},    {-width, height}, {-width, 0},      {-width, -height}};

    for (const auto& offset : offsets)
    {
        VfxPaneCopy(source, offset[0], offset[1], pane, dx, dy, fill);
    }

    return 0;
}

namespace
{
    /// <summary>VFX_ellipse_draw and VFX_ellipse_fill: the midpoint ellipse, outlined or filled, clipped to the pane.</summary>
    int32_t DrawEllipse(const MCPane& pane, int32_t xc, int32_t yc, int32_t width, int32_t height, int32_t color,
                        bool fill)
    {
        if (width == 0 || height == 0)
        {
            return VfxLineDraw(pane, xc - width, yc - height, xc + width, yc + height, color);
        }

        MCVfxClip clip;
        const int32_t status = MCVfxClipPane(pane, clip);

        if (status != 0)
        {
            return status;
        }

        MCEllipseCommand command;
        command.CenterX = xc + clip.PaneX;
        command.CenterY = yc + clip.PaneY;
        command.Width = width;
        command.Height = height;
        command.Clip = MCRect{clip.X0, clip.Y0, clip.X1, clip.Y1};
        command.Fill = fill;
        command.Color = static_cast<uint8_t>(color);
        command.Alpha = false;
        MCRenderer::For(clip.Target()).Ellipse(clip.Target(), command);
        // Original behaviour: the outline's asm returned whatever EAX held (a leftover of the stepping); 0 here.
        return 0;
    }
}

int32_t VfxEllipseFill(const MCPane& pane, int32_t xc, int32_t yc, int32_t width, int32_t height, int32_t color)
{
    return DrawEllipse(pane, xc, yc, width, height, color, true);
}
