#include "stdafx.h"
#include "platform/MCSharpText.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"

#include <limits>
#include <numeric>

namespace
{
    constexpr int32_t BytesPerPixel = 4;
    /// <summary>
    /// The characters the original's fonts leave blank (Windows-1252 0x80-0xBF), some of which the port makes at load
    /// (gui/MCFontGlyphs: a ¿ turned round, its shadow on the wrong side): not the art's own, so not read for its layers.
    /// </summary>
    constexpr int32_t MadeFirst = 0x80;
    constexpr int32_t MadeLast = 0xbf;
    /// <summary>A stroke's half width, in glyph pixels (strokes are a pixel wide, as the glyph's are).</summary>
    constexpr float StrokeRadius = 0.5f;
    /// <summary>How far from a redrawn pixel's glyph pixel the strokes that may reach it start, in glyph pixels.</summary>
    constexpr int32_t Reach = 2;
    constexpr uint8_t FullCoverage = 255;
    /// <summary>A VFX font's glyph count (vfx/MCVfxFunctions.h's fonts), and its glyph offsets' table.</summary>
    constexpr size_t FontCountAt = 4;
    constexpr size_t FontTableAt = 0x10;
    constexpr size_t FontOffsetBytes = 4;
    /// <summary>A glyph's pixels follow its width.</summary>
    constexpr size_t GlyphPixelsAt = 4;

    /// <summary>
    /// The drop shadows a font's two-tone glyphs are tried as, most likely first: the game's (yelldrp.fnt and
    /// bluedrp.fnt: one pixel down and one down and right), and one diagonal pixel. Only the bytes are left out.
    /// </summary>
    constexpr std::array<MCSharpText::MCDropShadow, 2> ShadowShapes = {{
        {MCSharpText::NoInk, MCSharpText::NoInk, 0, 1, 1, 1},
        {MCSharpText::NoInk, MCSharpText::NoInk, 1, 1, 1, 1},
    }};

    /// <summary>A stroke from one ink pixel's centre to another's (or a dot, both ends the same), in glyph pixels.</summary>
    struct MCStroke
    {
        float X0 = 0.0f;
        float Y0 = 0.0f;
        float X1 = 0.0f;
        float Y1 = 0.0f;
    };

    float DistanceTo(const MCStroke& stroke, float x, float y)
    {
        const float dx = stroke.X1 - stroke.X0;
        const float dy = stroke.Y1 - stroke.Y0;
        const float lengthSquared = dx * dx + dy * dy;
        const float along = lengthSquared > 0.0f
                                ? std::clamp(((x - stroke.X0) * dx + (y - stroke.Y0) * dy) / lengthSquared, 0.0f, 1.0f)
                                : 0.0f;
        const float ex = stroke.X0 + along * dx - x;
        const float ey = stroke.Y0 + along * dy - y;
        return std::sqrt(ex * ex + ey * ey);
    }

    /// <summary>
    /// The distance from (<paramref name="x"/>, <paramref name="y"/>) to <paramref name="stroke"/> moved by every offset
    /// of <paramref name="shadow"/>'s: a parallelogram, the stroke along one side and the offsets along the other.
    /// </summary>
    float SweptDistanceTo(const MCStroke& stroke, const MCSharpText::MCDropShadow& shadow, float x, float y)
    {
        const float ox = stroke.X0 + static_cast<float>(shadow.FromX);
        const float oy = stroke.Y0 + static_cast<float>(shadow.FromY);
        const float ax = stroke.X1 - stroke.X0;
        const float ay = stroke.Y1 - stroke.Y0;
        const auto bx = static_cast<float>(shadow.ToX - shadow.FromX);
        const auto by = static_cast<float>(shadow.ToY - shadow.FromY);

        // Inside: (x, y) is the corner plus a part of each side.
        if (const float determinant = ax * by - ay * bx; determinant != 0.0f)
        {
            const float px = x - ox;
            const float py = y - oy;
            const float u = (px * by - py * bx) / determinant;
            const float v = (ax * py - ay * px) / determinant;

            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)
            {
                return 0.0f;
            }
        }

        // Outside, or flat (a dot's sweep, or a stroke along it): the nearest side.
        float distance = std::numeric_limits<float>::max();

        for (const MCStroke& side :
             {MCStroke{ox, oy, ox + ax, oy + ay}, MCStroke{ox + bx, oy + by, ox + ax + bx, oy + ay + by},
              MCStroke{ox, oy, ox + bx, oy + by}, MCStroke{ox + ax, oy + ay, ox + ax + bx, oy + ay + by}})
        {
            distance = std::min(distance, DistanceTo(side, x, y));
        }

        return distance;
    }

    /// <summary>A glyph's bytes: <c>Columns</c> x <c>Rows</c>, its rows <c>Pitch</c> apart.</summary>
    struct MCGlyph
    {
        std::span<const uint8_t> Bytes;
        int32_t Pitch = 0;
        int32_t Columns = 0;
        int32_t Rows = 0;

        /// <summary>The byte at (x, y); no ink outside the glyph.</summary>
        uint8_t At(int32_t x, int32_t y) const
        {
            if (x < 0 || y < 0 || x >= Columns || y >= Rows)
            {
                return MCSharpText::NoInk;
            }

            const size_t at = static_cast<size_t>(y) * static_cast<size_t>(Pitch) + static_cast<size_t>(x);
            return at < Bytes.size() ? Bytes[at] : MCSharpText::NoInk;
        }

        /// <summary>The byte at (x, y), or outside the glyph its nearest pixel's (its border carried on).</summary>
        uint8_t Clamped(int32_t x, int32_t y) const
        {
            return At(std::clamp(x, 0, Columns - 1), std::clamp(y, 0, Rows - 1));
        }
    };

    /// <summary>The strokes of each pixel of a glyph and of the pixels <c>Margin</c> around it.</summary>
    struct MCStrokeGrid
    {
        int32_t Columns = 0;
        int32_t Rows = 0;
        int32_t Margin = 0;
        std::vector<std::vector<MCStroke>> Cells;

        size_t IndexOf(int32_t x, int32_t y) const
        {
            return static_cast<size_t>(y + Margin) * static_cast<size_t>(Columns + 2 * Margin) +
                   static_cast<size_t>(x + Margin);
        }
    };

    /// <summary>
    /// The strokes of the pixels <paramref name="ink"/> takes as ink, from <paramref name="margin"/> pixels before the
    /// glyph to as far past it: each one's dot at its centre, and lines to the touching ink pixels to its right and
    /// below, and to those diagonally below that no straight pair of ink pixels already joins it to; those
    /// <paramref name="keep"/> keeps (from one pixel to another, a dot from a pixel to itself).
    /// </summary>
    template <typename Ink, typename Keep>
    MCStrokeGrid StrokesOf(int32_t columns, int32_t rows, int32_t margin, const Ink& ink, const Keep& keep)
    {
        MCStrokeGrid grid{columns, rows, margin, {}};
        grid.Cells.resize(static_cast<size_t>(columns + 2 * margin) * static_cast<size_t>(rows + 2 * margin));

        for (int32_t y = -margin; y < rows + margin; ++y)
        {
            for (int32_t x = -margin; x < columns + margin; ++x)
            {
                if (!ink(x, y))
                {
                    continue;
                }

                const float cx = static_cast<float>(x) + 0.5f;
                const float cy = static_cast<float>(y) + 0.5f;
                std::vector<MCStroke>& from = grid.Cells[grid.IndexOf(x, y)];

                if (keep(x, y, x, y))
                {
                    from.push_back({cx, cy, cx, cy});
                }

                for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}, std::pair{-1, 1}})
                {
                    const bool diagonal = dx != 0 && dy != 0;

                    if (!ink(x + dx, y + dy) || (diagonal && (ink(x + dx, y) || ink(x, y + dy))) ||
                        !keep(x, y, x + dx, y + dy))
                    {
                        continue;
                    }

                    from.push_back({cx, cy, cx + static_cast<float>(dx), cy + static_cast<float>(dy)});
                }
            }
        }

        return grid;
    }

    /// <summary>
    /// <paramref name="grid"/>'s strokes painted at <see cref="MCSharpText::Scale"/> (RGBA8 as
    /// <see cref="MCSharpText::Redraw"/>'s): each redrawn pixel's coverage by the strokes of the pixels within
    /// <paramref name="reach"/> of its own (<paramref name="distanceTo"/> a stroke), and its byte
    /// (<paramref name="byteAt"/> the redrawn pixel's centre and its glyph pixel; no ink leaves it empty).
    /// </summary>
    template <typename Distance, typename ByteAt>
    std::vector<uint8_t> Paint(const MCStrokeGrid& grid, int32_t reach, const Distance& distanceTo,
                               const ByteAt& byteAt)
    {
        constexpr int32_t Scale = MCSharpText::Scale;
        const int32_t width = grid.Columns * Scale;
        const int32_t height = grid.Rows * Scale;
        std::vector<uint8_t> out(static_cast<size_t>(width) * static_cast<size_t>(height) * BytesPerPixel, 0);

        for (int32_t j = 0; j < height; ++j)
        {
            const float py = (static_cast<float>(j) + 0.5f) / static_cast<float>(Scale);
            const auto gy = static_cast<int32_t>(py);

            for (int32_t i = 0; i < width; ++i)
            {
                const float px = (static_cast<float>(i) + 0.5f) / static_cast<float>(Scale);
                const auto gx = static_cast<int32_t>(px);
                const uint8_t value = byteAt(px, py, gx, gy);

                if (value == MCSharpText::NoInk)
                {
                    continue;
                }

                float distance = std::numeric_limits<float>::max();

                for (int32_t y = std::max(gy - reach, -grid.Margin);
                     y <= std::min(gy + reach, grid.Rows + grid.Margin - 1); ++y)
                {
                    for (int32_t x = std::max(gx - reach, -grid.Margin);
                         x <= std::min(gx + reach, grid.Columns + grid.Margin - 1); ++x)
                    {
                        for (const MCStroke& stroke : grid.Cells[grid.IndexOf(x, y)])
                        {
                            distance = std::min(distance, distanceTo(stroke, px, py));
                        }
                    }
                }

                // Smoothed over one redrawn pixel at the stroke's edge.
                const float coverage =
                    std::clamp((StrokeRadius - distance) * static_cast<float>(Scale) + 0.5f, 0.0f, 1.0f);
                uint8_t* pixel = &out[(static_cast<size_t>(j) * static_cast<size_t>(width) + i) * BytesPerPixel];
                pixel[0] = value;
                pixel[1] = static_cast<uint8_t>(std::lround(coverage * FullCoverage));
            }
        }

        return out;
    }

    constexpr auto KeepAll = [](int32_t, int32_t, int32_t, int32_t) { return true; };

    /// <summary>A layer's byte, the same throughout.</summary>
    auto Throughout(uint8_t value)
    {
        return [value](float, float, int32_t, int32_t) { return value; };
    }

    /// <summary>The glyph's ink bytes, each once, in no order.</summary>
    std::vector<uint8_t> InkBytes(const MCGlyph& glyph)
    {
        std::array<bool, 256> seen{};
        std::vector<uint8_t> bytes;

        for (int32_t y = 0; y < glyph.Rows; ++y)
        {
            for (int32_t x = 0; x < glyph.Columns; ++x)
            {
                if (const uint8_t byte = glyph.At(x, y); byte != MCSharpText::NoInk && !seen[byte])
                {
                    seen[byte] = true;
                    bytes.push_back(byte);
                }
            }
        }

        return bytes;
    }

    bool LayeredGlyph(const MCGlyph& glyph)
    {
        for (int32_t y = 0; y < glyph.Rows; ++y)
        {
            for (int32_t x = 0; x < glyph.Columns; ++x)
            {
                const uint8_t byte = glyph.At(x, y);

                if (byte == MCSharpText::NoInk)
                {
                    continue;
                }

                for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}, std::pair{-1, 1}})
                {
                    if (const uint8_t next = glyph.At(x + dx, y + dy); next != MCSharpText::NoInk && next != byte)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    /// <summary>
    /// Whether the glyph is <paramref name="shadow"/>'s body and its shadow alone: each pixel of the shadow's byte is
    /// the body's moved by one of the shadow's offsets, where no body pixel is, and every such place is one.
    /// </summary>
    bool IsDropShadow(const MCGlyph& glyph, const MCSharpText::MCDropShadow& shadow)
    {
        for (int32_t y = 0; y < glyph.Rows; ++y)
        {
            for (int32_t x = 0; x < glyph.Columns; ++x)
            {
                const uint8_t byte = glyph.At(x, y);
                const bool shaded =
                    byte != shadow.Body && (glyph.At(x - shadow.FromX, y - shadow.FromY) == shadow.Body ||
                                            glyph.At(x - shadow.ToX, y - shadow.ToY) == shadow.Body);

                if (byte != shadow.Body && (byte == shadow.Shadow) != shaded)
                {
                    return false;
                }

                if (byte != shadow.Body && byte != shadow.Shadow && byte != MCSharpText::NoInk)
                {
                    return false;
                }
            }
        }

        return true;
    }
}

std::vector<uint8_t> MCSharpText::Redraw(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns, int32_t rows)
{
    if (columns <= 0 || rows <= 0)
    {
        return {};
    }

    const MCGlyph bytes{glyph, pitch, columns, rows};
    const auto ink = [&](int32_t x, int32_t y) { return bytes.At(x, y) != NoInk; };

    // Each redrawn pixel takes the byte of the ink pixel whose centre is nearest.
    const auto nearestByte = [&](float px, float py, int32_t gx, int32_t gy)
    {
        float nearest = std::numeric_limits<float>::max();
        uint8_t value = NoInk;

        for (int32_t y = std::max(gy - Reach, 0); y <= std::min(gy + Reach, rows - 1); ++y)
        {
            for (int32_t x = std::max(gx - Reach, 0); x <= std::min(gx + Reach, columns - 1); ++x)
            {
                if (!ink(x, y))
                {
                    continue;
                }

                const float ex = static_cast<float>(x) + 0.5f - px;
                const float ey = static_cast<float>(y) + 0.5f - py;

                if (const float centre = ex * ex + ey * ey; centre < nearest)
                {
                    nearest = centre;
                    value = bytes.At(x, y);
                }
            }
        }

        return value;
    };

    return Paint(StrokesOf(columns, rows, 0, ink, KeepAll), Reach, DistanceTo, nearestByte);
}

MCSharpText::MCFontLayers MCSharpText::LayersOf(std::span<const uint8_t> font)
{
    MCFontLayers layers;

    if (!VfxFontFits(font))
    {
        return layers;
    }

    const int32_t count = MCVfxRead32(font, FontCountAt);
    const int32_t height = VfxFontHeight(font);
    std::array<int64_t, 256> pixels{};
    std::vector<MCGlyph> glyphs;

    for (int32_t character = 0; character < count; ++character)
    {
        if (character >= MadeFirst && character <= MadeLast)
        {
            continue;
        }

        const auto offset =
            static_cast<uint32_t>(MCVfxRead32(font, FontTableAt + static_cast<size_t>(character) * FontOffsetBytes));
        const int32_t width = MCVfxRead32(font, offset);
        const MCGlyph glyph{
            font.subspan(offset + GlyphPixelsAt, static_cast<size_t>(width) * static_cast<size_t>(height)), width,
            width, height};

        for (const uint8_t byte : glyph.Bytes)
        {
            pixels[byte] += byte != NoInk ? 1 : 0;
        }

        if (LayeredGlyph(glyph))
        {
            glyphs.push_back(glyph);
        }
    }

    std::array<uint8_t, 256> order{};
    std::iota(order.begin(), order.end(), uint8_t{0});
    std::ranges::stable_sort(order, [&](uint8_t a, uint8_t b) { return pixels[a] > pixels[b]; });

    for (size_t place = 0; place < order.size(); ++place)
    {
        layers.Depth[order[place]] = static_cast<uint8_t>(place);
    }

    if (glyphs.empty())
    {
        return layers;
    }

    // A drop shadow when every layered glyph is the same two bytes, the front one's shadow the other.
    for (const MCDropShadow& shape : ShadowShapes)
    {
        std::optional<MCDropShadow> found;

        for (const MCGlyph& glyph : glyphs)
        {
            std::vector<uint8_t> bytes = InkBytes(glyph);

            if (bytes.size() != 2)
            {
                found.reset();
                break;
            }

            std::ranges::sort(bytes, [&](uint8_t a, uint8_t b) { return layers.Depth[a] < layers.Depth[b]; });
            MCDropShadow shadow = shape;
            shadow.Shadow = bytes[0];
            shadow.Body = bytes[1];

            if ((found.has_value() && (found->Body != shadow.Body || found->Shadow != shadow.Shadow)) ||
                !IsDropShadow(glyph, shadow))
            {
                found.reset();
                break;
            }

            found = shadow;
        }

        if (found.has_value())
        {
            layers.Shadow = found;
            break;
        }
    }

    return layers;
}

bool MCSharpText::Layered(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns, int32_t rows)
{
    return LayeredGlyph(MCGlyph{glyph, pitch, columns, rows});
}

MCSharpText::MCGlyphLayers MCSharpText::LayersOf(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns,
                                                 int32_t rows, const MCFontLayers& font)
{
    MCGlyphLayers layers;
    layers.Bytes = InkBytes(MCGlyph{glyph, pitch, columns, rows});
    std::ranges::sort(layers.Bytes, [&](uint8_t a, uint8_t b) { return font.Depth[a] < font.Depth[b]; });

    if (font.Shadow.has_value() && layers.Bytes == std::vector<uint8_t>{font.Shadow->Shadow, font.Shadow->Body})
    {
        layers.Shadow = font.Shadow;
    }

    return layers;
}

std::vector<std::vector<uint8_t>> MCSharpText::RedrawLayers(std::span<const uint8_t> glyph, int32_t pitch,
                                                            int32_t columns, int32_t rows, const MCGlyphLayers& layers)
{
    if (columns <= 0 || rows <= 0 || layers.Bytes.empty())
    {
        return {};
    }

    const MCGlyph bytes{glyph, pitch, columns, rows};
    const uint8_t front = layers.Bytes.back();
    const MCStrokeGrid frontStrokes =
        StrokesOf(columns, rows, 0, [&](int32_t x, int32_t y) { return bytes.At(x, y) == front; }, KeepAll);
    std::vector<std::vector<uint8_t>> redrawn;

    if (layers.Shadow.has_value())
    {
        const MCDropShadow& shadow = *layers.Shadow;
        const int32_t offset =
            std::max({std::abs(shadow.FromX), std::abs(shadow.FromY), std::abs(shadow.ToX), std::abs(shadow.ToY)});
        redrawn.push_back(Paint(
            frontStrokes, Reach + offset, [&](const MCStroke& stroke, float x, float y)
            { return SweptDistanceTo(stroke, shadow, x, y); }, Throughout(shadow.Shadow)));
    }
    else
    {
        // Each byte's place among the glyph's, back to front.
        std::array<int32_t, 256> place{};
        place.fill(-1);

        for (size_t i = 0; i < layers.Bytes.size(); ++i)
        {
            place[layers.Bytes[i]] = static_cast<int32_t>(i);
        }

        for (size_t i = 0; i + 1 < layers.Bytes.size(); ++i)
        {
            const uint8_t byte = layers.Bytes[i];
            const auto inkHere = [&](int32_t x, int32_t y)
            {
                const uint8_t at = bytes.Clamped(x, y);
                return at != NoInk && place[at] >= static_cast<int32_t>(i);
            };

            const auto touchesByte = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1)
            { return bytes.Clamped(x0, y0) == byte || bytes.Clamped(x1, y1) == byte; };
            redrawn.push_back(
                Paint(StrokesOf(columns, rows, Reach, inkHere, touchesByte), Reach, DistanceTo, Throughout(byte)));
        }
    }

    redrawn.push_back(Paint(frontStrokes, Reach, DistanceTo, Throughout(front)));
    return redrawn;
}
