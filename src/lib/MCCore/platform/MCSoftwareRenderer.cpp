#include "stdafx.h"
#include "platform/MCSoftwareRenderer.h"
#include "vfx/MCVfxClip.h"

// The software renderer's fills, copies, pixels, lines, ellipses, status bars, glyphs and sprites: the pixel loops of
// vfxa.asm, vfx_transform.cpp, vfx_ellipse.cpp, vfx_map_polygon.cpp and the font and image routines.

namespace
{
    /// <summary>Where window pixel (x, y) lies in the window's bytes.</summary>
    size_t IndexOf(const MCWindow& window, int32_t x, int32_t y)
    {
        return static_cast<size_t>(y) * static_cast<size_t>(window.XMax + 1) + static_cast<size_t>(x);
    }

    /// <summary>Window pixel (x, y).</summary>
    uint8_t& At(MCWindow& window, int32_t x, int32_t y)
    {
        return window.Pixels[IndexOf(window, x, y)];
    }

    /// <summary>A blend through the game's alpha table: <paramref name="color"/> over <paramref name="screen"/>.</summary>
    uint8_t Blend(uint8_t color, uint8_t screen)
    {
        return AlphaTable[(static_cast<uint32_t>(color) << 8) | screen];
    }

    /// <summary>
    /// The midpoint ellipse VFX steps through (all 32-bit wrapping arithmetic, as in the asm): calls
    /// <paramref name="plot"/>(x, y) for each step of the first quadrant, from (0, b) to (a, 0).
    /// </summary>
    template <typename Plot> void WalkEllipse(int32_t width, int32_t height, Plot plot)
    {
        const uint32_t b2 = static_cast<uint32_t>(height) * static_cast<uint32_t>(height);
        const uint32_t twoB2 = b2 << 1;
        const uint32_t a2 = static_cast<uint32_t>(width) * static_cast<uint32_t>(width);
        const uint32_t twoA2 = a2 << 1;
        uint32_t px = 0;
        uint32_t py = twoA2 * static_cast<uint32_t>(height);
        uint32_t d = (a2 >> 2) + b2 - a2 * static_cast<uint32_t>(height);
        int32_t x = 0;
        int32_t y = height;
        int32_t rows = height;

        // Region 1: slope above -1, x advances every step.
        while (static_cast<int32_t>(px - py) < 0)
        {
            plot(x, y);

            if (static_cast<int32_t>(d) >= 0)
            {
                --y;
                --rows;
                py -= twoA2;
                d -= py;
            }

            ++x;
            px += twoB2;
            d += px + b2;
        }

        const uint32_t diff = a2 - b2;
        d += static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(diff) >> 1) + diff - px - py) >> 1);

        // Region 2: y descends every step.
        for (;;)
        {
            plot(x, y);

            if (static_cast<int32_t>(d) < 0)
            {
                ++x;
                px += twoB2;
                d += px;
            }

            --y;
            py -= twoA2;
            d -= py - a2;

            if (--rows < 0)
            {
                break;
            }
        }
    }
}

void MCSeeThrough::Map(uint8_t& p, MCColorTable table)
{
    if (const MCAddressKey key = MCAddressKey::OfData(table); key != _LastTable)
    {
        _LastTable = key;
        _LastOp = MCRenderer::OpFor(table);
    }

    uint8_t& op = _Ops[static_cast<size_t>(MCAddressKey::Of(p) - _Base)];
    const uint8_t composed = _LastOp != 0 ? MCRenderer::ComposeOps(op, _LastOp) : 0;

    // A table always has an op row of its own, unless every row is taken.
    if (composed == 0)
    {
        // Every op table is taken: map the key itself, as an opaque pixel.
        p = table[p];
        return;
    }

    op = composed;
}

void MCSeeThrough::Blend(uint8_t& p, uint8_t color)
{
    Map(p, MCAlphaRow(color));
}

MCSoftwareRenderer& MCSoftwareRenderer::Instance()
{
    static MCSoftwareRenderer renderer;
    return renderer;
}

void MCSoftwareRenderer::Clear(MCWindow& target, const MCRect& rect, uint8_t color)
{
    const size_t width = static_cast<size_t>(rect.X1 + 1 - rect.X0);
    const std::span<uint8_t> ops = OpPlane(target);

    for (int32_t y = rect.Y0; y <= rect.Y1; ++y)
    {
        std::memset(&At(target, rect.X0, y), color, width);

        if (!ops.empty())
        {
            std::memset(ops.data() + (&At(target, rect.X0, y) - target.Pixels.get()), 0, width);
        }
    }
}

void MCSoftwareRenderer::Hash(MCWindow& target, const MCRect& rect, uint8_t color)
{
    const int32_t width = rect.X1 - rect.X0 + 1;

    const std::span<uint8_t> pixels = target.Bytes();

    for (int32_t y = rect.Y0; y <= rect.Y1; ++y)
    {
        size_t pixel = IndexOf(target, rect.X0, y);
        int32_t left = width;

        if ((rect.Y1 - y) & 1)
        {
            ++pixel;
            --left;
        }

        // OB-121: the asm wrote at least one pixel per row, one past x1 when an odd row was one pixel wide.
        for (; left > 0; pixel += 2, left -= 2)
        {
            pixels[pixel] = color;
        }
    }
}

void MCSoftwareRenderer::Translate(MCWindow& target, const MCRect& rect, MCColorTable table)
{
    const std::span<uint8_t> pixels = target.Bytes();
    MCSeeThrough seeThrough(target);

    for (int32_t y = rect.Y0; y <= rect.Y1; ++y)
    {
        const size_t row = IndexOf(target, rect.X0, y);

        for (size_t p = row; p <= row + static_cast<size_t>(rect.X1 - rect.X0); ++p)
        {
            seeThrough.Translate(pixels[p], table);
        }
    }
}

void MCSoftwareRenderer::Copy(MCWindow& target, const MCCopyCommand& command)
{
    const MCWindow& source = command.Source.get();
    NoteCpuRead(source, "Copy");
    const MCRect& rect = command.SourceRect;
    const int32_t width = rect.X1 + 1 - rect.X0;
    const int32_t height = rect.Y1 + 1 - rect.Y0;
    const int32_t sourceStride = source.XMax + 1;
    const int32_t targetStride = target.XMax + 1;
    const std::span<const uint8_t> sourcePixels = source.Bytes();
    const std::span<uint8_t> targetPixels = target.Bytes();
    // Where the row being copied starts (its first pixel in the copy's direction) in each window.
    ptrdiff_t src = 0;
    ptrdiff_t dst = 0;
    intptr_t srcRowStep;
    intptr_t dstRowStep;

    if (command.Downwards)
    {
        src += static_cast<intptr_t>(rect.Y0) * sourceStride;
        dst += static_cast<intptr_t>(command.Y) * targetStride;
        srcRowStep = sourceStride;
        dstRowStep = targetStride;
    }
    else
    {
        src += static_cast<intptr_t>(rect.Y1) * sourceStride;
        dst += static_cast<intptr_t>(command.Y + height - 1) * targetStride;
        srcRowStep = -sourceStride;
        dstRowStep = -targetStride;
    }

    intptr_t pixelStep;

    if (command.Rightwards)
    {
        src += rect.X0;
        dst += command.X;
        pixelStep = 1;
    }
    else
    {
        src += rect.X1;
        dst += command.X + width - 1;
        pixelStep = -1;
    }

    if (!command.ColorKey)
    {
        for (int32_t row = 0; row < height; ++row, src += srcRowStep, dst += dstRowStep)
        {
            const ptrdiff_t first = pixelStep > 0 ? 0 : width - 1;
            std::memmove(targetPixels.subspan(static_cast<size_t>(dst - first), static_cast<size_t>(width)).data(),
                         sourcePixels.subspan(static_cast<size_t>(src - first), static_cast<size_t>(width)).data(),
                         static_cast<size_t>(width));
        }

        return;
    }

    for (int32_t row = 0; row < height; ++row, src += srcRowStep, dst += dstRowStep)
    {
        ptrdiff_t s = src;
        ptrdiff_t d = dst;

        for (int32_t i = 0; i < width; ++i, s += pixelStep, d += pixelStep)
        {
            if (sourcePixels[static_cast<size_t>(s)] != command.Key)
            {
                targetPixels[static_cast<size_t>(d)] = sourcePixels[static_cast<size_t>(s)];
            }
        }
    }
}

void MCSoftwareRenderer::AlphaBlit(MCWindow& target, const MCAlphaBlitCommand& command)
{
    const int32_t stride = target.XMax + 1;
    const int32_t pitch = command.Pitch;
    // The sprite's first pixel read is Offset on (negative for a mirrored sprite clipped on the left, whose reads start
    // at the mirrored row end).
    const std::span<const uint8_t> source = command.Sprite;
    const std::span<uint8_t> pixels = target.Bytes();
    size_t destination = IndexOf(target, command.Left, command.Top);
    MCSeeThrough seeThrough(target);
    const auto blend = [&](uint8_t& p, uint8_t color)
    {
        if (seeThrough.At(p))
        {
            seeThrough.Blend(p, color);
        }
        else
        {
            p = Blend(color, p);
        }
    };

    if (command.FullSize)
    {
        ptrdiff_t s = command.Offset + (command.Mirror ? pitch - 1 : 0);
        const int32_t step = command.Mirror ? -1 : 1;

        for (int32_t row = 0; row < command.Rows; ++row)
        {
            for (int32_t column = 0; column < command.Columns; ++column)
            {
                blend(pixels[destination + static_cast<size_t>(column)], source[static_cast<size_t>(s)]);
                s += step;
            }

            s += pitch - step * command.Columns;
            destination += stride;
        }

        return;
    }

    // Half size: every other pixel of every other row.
    const int32_t halfRows = static_cast<int32_t>(static_cast<uint32_t>(command.Rows) >> 1);
    const int32_t halfColumns = static_cast<int32_t>(static_cast<uint32_t>(command.Columns) >> 1);
    ptrdiff_t s = command.Offset + (command.Mirror ? pitch * 2 - 1 : 0);
    const int32_t step = command.Mirror ? -2 : 2;

    for (int32_t row = 0; row < halfRows; ++row)
    {
        for (int32_t column = 0; column < halfColumns; ++column)
        {
            blend(pixels[destination + static_cast<size_t>(column)], source[static_cast<size_t>(s)]);
            s += step;
        }

        s += pitch * 2 - step * halfColumns;
        destination += stride;
    }
}

void MCSoftwareRenderer::ShapeBlit(MCWindow& target, const MCShapeBlitCommand& command)
{
    // Port-only: sun shadows are drawn by the hardware renderer alone.
    if (DrawingSunShadow())
    {
        return;
    }

    // The shape filled into the buffer (as AG_shape_fill draws it at its bounds' corner into a pane of the bounds),
    // then blended.
    const size_t size = static_cast<size_t>(command.Width) * static_cast<size_t>(command.Height);

    if (size > _ShapeScratchCapacity)
    {
        _ShapeScratch.Pixels = std::make_shared<uint8_t[]>(size);
        _ShapeScratchCapacity = size;
    }

    _ShapeScratch.XMax = command.Width - 1;
    _ShapeScratch.YMax = command.Height - 1;
    std::ranges::fill(_ShapeScratch.Bytes(), uint8_t{0});
    MCShapeCommand fill;
    fill.ShapeTable = command.ShapeTable;
    fill.ShapeNum = command.ShapeNum;
    fill.SkipRows = 0;
    fill.Rows = command.Height;
    fill.Top = 0;
    fill.Left = 0;
    fill.Lo = 0;
    fill.Hi = command.Width - 1;
    fill.Op = command.Table.has_value() ? MCShapeOp::XlatFill : MCShapeOp::Fill;
    fill.Table = command.Table;
    Shape(_ShapeScratch, fill);
    MCAlphaBlitCommand blit = command.Blit;
    blit.Sprite = _ShapeScratch.Bytes();
    AlphaBlit(target, blit);
}

void MCSoftwareRenderer::Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels)
{
    std::ranges::copy(pixels, &At(target, x, y));
}

void MCSoftwareRenderer::Pixel(MCWindow& target, int32_t x, int32_t y, uint8_t color)
{
    At(target, x, y) = color;
}

void MCSoftwareRenderer::Line(MCWindow& target, const MCLineCommand& command)
{
    MCSeeThrough seeThrough(target);
    MCLinePixels(command,
                 [&](int32_t x, int32_t y)
                 {
                     uint8_t& pixel = At(target, x, y);

                     if (command.Table.has_value() && seeThrough.At(pixel))
                     {
                         seeThrough.Map(pixel, *command.Table);
                     }
                     else
                     {
                         pixel = command.Table.has_value() ? (*command.Table)[pixel] : command.Color;
                     }
                 });
}

void MCEllipseRuns(const MCEllipseCommand& command, const std::function<void(int32_t y, int32_t x0, int32_t x1)>& emit)
{
    const MCRect& clip = command.Clip;
    const int32_t cx = command.CenterX;
    const int32_t cy = command.CenterY;

    if (!command.Fill)
    {
        // OB-122: the asm plotted the points on the axes twice (blending translucent colours twice).
        const auto put = [&](int32_t x, int32_t y)
        {
            if (x >= clip.X0 && x <= clip.X1 && y >= clip.Y0 && y <= clip.Y1)
            {
                emit(y, x, x);
            }
        };

        WalkEllipse(command.Width, command.Height,
                    [&](int32_t x, int32_t y)
                    {
                        put(cx + x, cy + y);

                        if (y != 0)
                        {
                            put(cx + x, cy - y);
                        }

                        if (x != 0)
                        {
                            put(cx - x, cy + y);

                            if (y != 0)
                            {
                                put(cx - x, cy - y);
                            }
                        }
                    });
        return;
    }

    // OB-122: the asm filled a row again at every step that stayed on it (blending translucent colours repeatedly),
    // and the middle row twice. Each row is filled once, at its widest: the walk's x never shrinks and its y never
    // grows, so that is the last step on the row.
    const auto rows = [&](int32_t x, int32_t y)
    {
        // The span cx - x .. cx + x, clamped; skipped when wholly outside.
        const int32_t right = std::min(cx + x, clip.X1);
        const int32_t left = std::max(cx - x, clip.X0);

        if (right < left)
        {
            return;
        }

        const int32_t below = cy + y;

        if (below >= clip.Y0 && below <= clip.Y1)
        {
            emit(below, left, right);
        }

        const int32_t above = cy - y;

        if (y != 0 && above >= clip.Y0 && above <= clip.Y1)
        {
            emit(above, left, right);
        }
    };

    bool pending = false;
    int32_t pendingX = 0;
    int32_t pendingY = 0;

    WalkEllipse(command.Width, command.Height,
                [&](int32_t x, int32_t y)
                {
                    if (pending && y != pendingY)
                    {
                        rows(pendingX, pendingY);
                    }

                    pending = true;
                    pendingX = x;
                    pendingY = y;
                });

    if (pending)
    {
        rows(pendingX, pendingY);
    }
}

void MCSoftwareRenderer::Ellipse(MCWindow& target, const MCEllipseCommand& command)
{
    MCSeeThrough seeThrough(target);
    const std::span<uint8_t> pixels = target.Bytes();
    MCEllipseRuns(command,
                  [&](int32_t y, int32_t x0, int32_t x1)
                  {
                      size_t p = IndexOf(target, x0, y);

                      for (int32_t count = x1 - x0 + 1; count != 0; --count, ++p)
                      {
                          uint8_t& pixel = pixels[p];

                          if (!command.Alpha)
                          {
                              pixel = command.Color;
                          }
                          else if (seeThrough.At(pixel))
                          {
                              seeThrough.Blend(pixel, command.Color);
                          }
                          else
                          {
                              pixel = Blend(command.Color, pixel);
                          }
                      }
                  });
}

void MCSoftwareRenderer::StatusBar(MCWindow& target, const MCStatusBarCommand& command)
{
    // The AlphaTable row status-bar frames are darkened through (0x008011d0 in MCX.EXE).
    constexpr int32_t StatusFrameAlpha = 0x108;
    const MCColorTable frame = MCAlphaRow(StatusFrameAlpha);
    const MCColorTable fill = MCAlphaRow(static_cast<uint32_t>(command.AlphaColor));
    const MCRect& box = command.Box;
    const int32_t width = box.X1 - box.X0;
    const int32_t stride = target.XMax + 1;
    const std::span<uint8_t> pixels = target.Bytes();
    size_t p = IndexOf(target, box.X0, box.Y0);
    MCSeeThrough seeThrough(target);
    const auto map = [&](uint8_t& pixel, MCColorTable table)
    {
        if (seeThrough.At(pixel))
        {
            seeThrough.Map(pixel, table);
        }
        else
        {
            pixel = table[pixel];
        }
    };

    for (int32_t y = box.Y0; y <= box.Y1; ++y, p += stride)
    {
        if (y == command.FrameTop || y == command.FrameBottom)
        {
            // The frame's top or bottom: the pixels between the corners.
            for (int32_t i = 1; i < width; ++i)
            {
                map(pixels[p + static_cast<size_t>(i)], frame);
            }

            continue;
        }

        map(pixels[p], frame);
        map(pixels[p + static_cast<size_t>(width)], frame);

        if (command.BarLength != 0)
        {
            for (int32_t i = 1; i <= command.BarLength + 1; ++i)
            {
                map(pixels[p + static_cast<size_t>(i)], fill);
            }
        }
    }
}

void MCSoftwareRenderer::Glyph(MCWindow& target, const MCGlyphCommand& command)
{
    // The glyph: its width dword, then its rows (layout in vfx/MCVfxFunctions.h).
    const std::span<const uint8_t> font = command.Font;
    const auto glyphAt = static_cast<uint32_t>(MCRead32(font, 0x10 + static_cast<size_t>(command.Character) * 4));
    const int32_t width = MCRead32(font, glyphAt);
    size_t source = glyphAt + 4 + static_cast<size_t>(command.SourceY) * static_cast<size_t>(width) +
                    static_cast<size_t>(command.SourceX);
    const int32_t stride = target.XMax + 1;
    const std::span<uint8_t> pixels = target.Bytes();
    size_t dest = IndexOf(target, command.X, command.Y);
    const auto columns = static_cast<size_t>(command.Columns);

    for (int32_t rows = command.Rows; rows > 0; --rows)
    {
        if (!command.Table.has_value())
        {
            std::ranges::copy(font.subspan(source, columns), pixels.begin() + static_cast<ptrdiff_t>(dest));
        }
        else
        {
            for (size_t i = 0; i < columns; ++i)
            {
                const uint8_t pixel = (*command.Table)[font[source + i]];

                if (pixel != 0xff)
                {
                    pixels[dest + i] = pixel;
                }
            }
        }

        source += width;
        dest += stride;
    }
}
