#include "stdafx.h"
#include "platform/MCSharpText.h"

#include <limits>

namespace
{
    constexpr int32_t BytesPerPixel = 4;
    /// <summary>A stroke's half width, in glyph pixels (strokes are a pixel wide, as the glyph's are).</summary>
    constexpr float StrokeRadius = 0.5f;
    /// <summary>How far from a redrawn pixel's glyph pixel the strokes that may reach it start, in glyph pixels.</summary>
    constexpr int32_t Reach = 2;
    constexpr uint8_t FullCoverage = 255;

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
}

std::vector<uint8_t> MCSharpText::Redraw(std::span<const uint8_t> glyph, int32_t pitch, int32_t columns, int32_t rows)
{
    if (columns <= 0 || rows <= 0)
    {
        return {};
    }

    const int32_t width = columns * Scale;
    const int32_t height = rows * Scale;
    std::vector<uint8_t> out(static_cast<size_t>(width) * static_cast<size_t>(height) * BytesPerPixel, 0);

    const auto byteAt = [&](int32_t x, int32_t y) -> uint8_t
    {
        if (x < 0 || y < 0 || x >= columns || y >= rows)
        {
            return NoInk;
        }

        const size_t at = static_cast<size_t>(y) * static_cast<size_t>(pitch) + static_cast<size_t>(x);
        return at < glyph.size() ? glyph[at] : NoInk;
    };

    const auto ink = [&](int32_t x, int32_t y) { return byteAt(x, y) != NoInk; };

    // Each ink pixel's strokes: a dot at its centre, and lines to the touching ink pixels to its right and below, and
    // to those diagonally below that no straight pair of ink pixels already joins it to.
    std::vector<std::vector<MCStroke>> strokes(static_cast<size_t>(columns) * static_cast<size_t>(rows));

    for (int32_t y = 0; y < rows; ++y)
    {
        for (int32_t x = 0; x < columns; ++x)
        {
            if (!ink(x, y))
            {
                continue;
            }

            const float cx = static_cast<float>(x) + 0.5f;
            const float cy = static_cast<float>(y) + 0.5f;
            std::vector<MCStroke>& from = strokes[static_cast<size_t>(y) * static_cast<size_t>(columns) + x];
            from.push_back({cx, cy, cx, cy});

            for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}, std::pair{-1, 1}})
            {
                const bool diagonal = dx != 0 && dy != 0;

                if (!ink(x + dx, y + dy) || (diagonal && (ink(x + dx, y) || ink(x, y + dy))))
                {
                    continue;
                }

                from.push_back({cx, cy, cx + static_cast<float>(dx), cy + static_cast<float>(dy)});
            }
        }
    }

    for (int32_t j = 0; j < height; ++j)
    {
        const float py = (static_cast<float>(j) + 0.5f) / static_cast<float>(Scale);
        const auto gy = static_cast<int32_t>(py);

        for (int32_t i = 0; i < width; ++i)
        {
            const float px = (static_cast<float>(i) + 0.5f) / static_cast<float>(Scale);
            const auto gx = static_cast<int32_t>(px);
            float distance = std::numeric_limits<float>::max();
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
                        value = byteAt(x, y);
                    }

                    for (const MCStroke& stroke : strokes[static_cast<size_t>(y) * static_cast<size_t>(columns) + x])
                    {
                        distance = std::min(distance, DistanceTo(stroke, px, py));
                    }
                }
            }

            if (value == NoInk)
            {
                continue;
            }

            // Smoothed over one redrawn pixel at the stroke's edge.
            const float coverage = std::clamp((StrokeRadius - distance) * static_cast<float>(Scale) + 0.5f, 0.0f, 1.0f);
            uint8_t* pixel = &out[(static_cast<size_t>(j) * static_cast<size_t>(width) + i) * BytesPerPixel];
            pixel[0] = value;
            pixel[1] = static_cast<uint8_t>(std::lround(coverage * FullCoverage));
        }
    }

    return out;
}
