#include "stdafx.h"
#include "platform/MCSharpLines.h"
#include "platform/MCSharpText.h"

#include <limits>

namespace
{
    constexpr int32_t BytesPerPixel = 4;
    constexpr uint8_t FullCoverage = 255;
    /// <summary>A stroke's half width, in art pixels (strokes are a pixel wide, as the art's are).</summary>
    constexpr float StrokeRadius = 0.5f;
    /// <summary>
    /// How far a run's pixels may lie from its straightened line, in art pixels: a one-pixel step is straightened, a
    /// bend of more is kept as a point of the line.
    /// </summary>
    constexpr float StepTolerance = 0.75f;
    /// <summary>A bend turning further than this (its cosine: 60 degrees) is a corner, and stays sharp.</summary>
    constexpr float CornerCosine = 0.5f;
    /// <summary>A bend turning less than this (its cosine) is no bend: the point is kept as it is.</summary>
    constexpr float StraightCosine = 0.9999f;
    /// <summary>The rounding passes over a run's gentle bends; each cuts a bend's corner a quarter along its sides.</summary>
    constexpr int32_t RoundingPasses = 3;
    constexpr float CornerCut = 0.25f;
    /// <summary>How far from a redrawn pixel the art pixel whose byte it keeps is looked for, in art pixels.</summary>
    constexpr int32_t ValueReach = 3;
    /// <summary>A pixel's centre, from its corner.</summary>
    constexpr float Centre = 0.5f;

    /// <summary>The eight directions to a pixel's neighbours: the straight ones, then the diagonals.</summary>
    constexpr std::array<std::pair<int32_t, int32_t>, 8> Directions = {
        {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    constexpr int32_t StraightDirections = 4;

    /// <summary>The direction back along <paramref name="direction"/>.</summary>
    constexpr int32_t Opposite(int32_t direction)
    {
        constexpr int32_t Half = 2;
        return direction < StraightDirections
                   ? (direction + Half) % StraightDirections
                   : StraightDirections + (direction - StraightDirections + Half) % StraightDirections;
    }

    struct MCPoint
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    /// <summary>
    /// A run of joined pixels' centres between ends or forks, or round a loop (<c>Closed</c>); a loop from a fork
    /// (<c>Anchored</c>) keeps its first point where it is.
    /// </summary>
    struct MCRun
    {
        std::vector<MCPoint> Points;
        bool Closed = false;
        bool Anchored = false;
    };

    float DistanceTo(MCPoint a, MCPoint b, MCPoint p)
    {
        const float dx = b.X - a.X;
        const float dy = b.Y - a.Y;
        const float lengthSquared = dx * dx + dy * dy;
        const float along =
            lengthSquared > 0.0f ? std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        return std::hypot(a.X + along * dx - p.X, a.Y + along * dy - p.Y);
    }

    /// <summary>The art: its bytes, and how its pixels join (as a glyph's do, MCSharpText).</summary>
    class MCArt
    {
    public:
        MCArt(std::span<const uint8_t> pixels, int32_t pitch, int32_t columns, int32_t rows)
            : _Pixels(pixels), _Pitch(pitch), Columns(columns), Rows(rows)
        {
        }

        uint8_t ByteAt(int32_t x, int32_t y) const
        {
            if (x < 0 || y < 0 || x >= Columns || y >= Rows)
            {
                return MCSharpText::NoInk;
            }

            const size_t at = static_cast<size_t>(y) * static_cast<size_t>(_Pitch) + static_cast<size_t>(x);
            return at < _Pixels.size() ? _Pixels[at] : MCSharpText::NoInk;
        }

        bool Ink(int32_t x, int32_t y) const { return ByteAt(x, y) != MCSharpText::NoInk; }

        /// <summary>
        /// Whether ink pixel (x, y) joins its neighbour in <paramref name="direction"/>: a straight neighbour always, a
        /// diagonal one only where no straight pair of ink pixels already joins them.
        /// </summary>
        bool Joined(int32_t x, int32_t y, int32_t direction) const
        {
            const auto [dx, dy] = Directions[static_cast<size_t>(direction)];
            return Ink(x + dx, y + dy) && (direction < StraightDirections || (!Ink(x + dx, y) && !Ink(x, y + dy)));
        }

        int32_t Degree(int32_t x, int32_t y) const
        {
            int32_t degree = 0;

            for (int32_t direction = 0; direction < static_cast<int32_t>(Directions.size()); ++direction)
            {
                degree += Joined(x, y, direction) ? 1 : 0;
            }

            return degree;
        }

        size_t Cell(int32_t x, int32_t y) const
        {
            return static_cast<size_t>(y) * static_cast<size_t>(Columns) + static_cast<size_t>(x);
        }

        const int32_t Columns;
        const int32_t Rows;

    private:
        std::span<const uint8_t> _Pixels;
        int32_t _Pitch;
    };

    MCPoint CentreOf(int32_t x, int32_t y)
    {
        return MCPoint{static_cast<float>(x) + Centre, static_cast<float>(y) + Centre};
    }

    /// <summary>The art's runs: from every end and fork along each of its joins to the next, then the loops left.</summary>
    std::vector<MCRun> Runs(const MCArt& art)
    {
        std::vector<MCRun> runs;
        // The joins followed, a bit a direction for each pixel.
        std::vector<uint8_t> followed(static_cast<size_t>(art.Columns) * static_cast<size_t>(art.Rows), 0);
        const auto isFollowed = [&](int32_t x, int32_t y, int32_t direction)
        { return (followed[art.Cell(x, y)] & (1u << direction)) != 0; };

        const auto follow = [&](int32_t x, int32_t y, int32_t direction)
        {
            MCRun run;
            run.Points.push_back(CentreOf(x, y));
            run.Anchored = art.Degree(x, y) != 2;
            const int32_t startX = x;
            const int32_t startY = y;

            for (;;)
            {
                const auto [dx, dy] = Directions[static_cast<size_t>(direction)];
                followed[art.Cell(x, y)] |= static_cast<uint8_t>(1u << direction);
                x += dx;
                y += dy;
                followed[art.Cell(x, y)] |= static_cast<uint8_t>(1u << Opposite(direction));

                if (x == startX && y == startY)
                {
                    run.Closed = true;
                    break;
                }

                run.Points.push_back(CentreOf(x, y));

                if (art.Degree(x, y) != 2)
                {
                    break;
                }

                int32_t next = -1;

                for (int32_t on = 0; on < static_cast<int32_t>(Directions.size()) && next < 0; ++on)
                {
                    if (art.Joined(x, y, on) && !isFollowed(x, y, on))
                    {
                        next = on;
                    }
                }

                if (next < 0)
                {
                    break;
                }

                direction = next;
            }

            runs.push_back(std::move(run));
        };

        const auto followAll = [&](bool ends)
        {
            for (int32_t y = 0; y < art.Rows; ++y)
            {
                for (int32_t x = 0; x < art.Columns; ++x)
                {
                    if (!art.Ink(x, y) || (art.Degree(x, y) != 2) != ends)
                    {
                        continue;
                    }

                    if (ends && art.Degree(x, y) == 0)
                    {
                        // A lone pixel: a dot.
                        runs.push_back(MCRun{{CentreOf(x, y)}, false, true});
                        continue;
                    }

                    for (int32_t direction = 0; direction < static_cast<int32_t>(Directions.size()); ++direction)
                    {
                        if (art.Joined(x, y, direction) && !isFollowed(x, y, direction))
                        {
                            follow(x, y, direction);
                        }
                    }
                }
            }
        };

        followAll(true);
        followAll(false);
        return runs;
    }

    /// <summary>
    /// The points of open run <paramref name="points"/> its line keeps (Douglas-Peucker): its ends, and every point
    /// further than StepTolerance from the line through those kept either side of it.
    /// </summary>
    std::vector<MCPoint> Straightened(std::span<const MCPoint> points)
    {
        if (points.size() <= 2)
        {
            return {points.begin(), points.end()};
        }

        std::vector<bool> kept(points.size(), false);
        kept.front() = true;
        kept.back() = true;
        std::vector<std::pair<size_t, size_t>> open = {{0, points.size() - 1}};

        while (!open.empty())
        {
            const auto [first, last] = open.back();
            open.pop_back();
            float furthest = 0.0f;
            size_t at = first;

            for (size_t i = first + 1; i < last; ++i)
            {
                if (const float distance = DistanceTo(points[first], points[last], points[i]); distance > furthest)
                {
                    furthest = distance;
                    at = i;
                }
            }

            if (furthest > StepTolerance)
            {
                kept[at] = true;
                open.emplace_back(first, at);
                open.emplace_back(at, last);
            }
        }

        std::vector<MCPoint> line;

        for (size_t i = 0; i < points.size(); ++i)
        {
            if (kept[i])
            {
                line.push_back(points[i]);
            }
        }

        return line;
    }

    /// <summary>A run's line straightened: a loop is cut in two at the point furthest from its first, each half alone.</summary>
    std::vector<MCPoint> Straightened(const MCRun& run)
    {
        constexpr size_t SmallestLoop = 4;

        if (!run.Closed || run.Points.size() < SmallestLoop)
        {
            return run.Closed ? run.Points : Straightened(std::span(run.Points));
        }

        const std::vector<MCPoint>& points = run.Points;
        size_t far = 0;
        float furthest = 0.0f;

        for (size_t i = 1; i < points.size(); ++i)
        {
            if (const float distance = DistanceTo(points[0], points[0], points[i]); distance > furthest)
            {
                furthest = distance;
                far = i;
            }
        }

        std::vector<MCPoint> line = Straightened(std::span(points).subspan(0, far + 1));
        std::vector<MCPoint> back(points.begin() + static_cast<std::ptrdiff_t>(far), points.end());
        back.push_back(points[0]);
        const std::vector<MCPoint> second = Straightened(std::span(back));
        // The halves share the cut point and the first point; the loop closes on its own.
        line.insert(line.end(), second.begin() + 1, second.end() - 1);
        return line;
    }

    /// <summary>
    /// The line of <paramref name="run"/> (<paramref name="line"/>) with its gentle bends rounded (corner cutting,
    /// Chaikin's), its corners, ends and anchor kept.
    /// </summary>
    std::vector<MCPoint> Rounded(std::vector<MCPoint> line, const MCRun& run)
    {
        const bool closed = run.Closed;

        constexpr size_t SmallestBend = 3;

        for (int32_t pass = 0; pass < RoundingPasses && line.size() >= SmallestBend; ++pass)
        {
            const size_t count = line.size();
            std::vector<MCPoint> rounded;
            rounded.reserve(count * 2);

            for (size_t i = 0; i < count; ++i)
            {
                const MCPoint point = line[i];

                if ((!closed && (i == 0 || i == count - 1)) || (run.Anchored && i == 0))
                {
                    rounded.push_back(point);
                    continue;
                }

                const MCPoint before = line[(i + count - 1) % count];
                const MCPoint after = line[(i + 1) % count];
                const float inX = point.X - before.X;
                const float inY = point.Y - before.Y;
                const float outX = after.X - point.X;
                const float outY = after.Y - point.Y;
                const float lengths = std::hypot(inX, inY) * std::hypot(outX, outY);
                const float turn = lengths > 0.0f ? (inX * outX + inY * outY) / lengths : -1.0f;

                if (turn < CornerCosine || turn > StraightCosine)
                {
                    rounded.push_back(point);
                    continue;
                }

                rounded.push_back({point.X - inX * CornerCut, point.Y - inY * CornerCut});
                rounded.push_back({point.X + outX * CornerCut, point.Y + outY * CornerCut});
            }

            line = std::move(rounded);
        }

        return line;
    }
}

std::vector<uint8_t> MCSharpLines::Redraw(std::span<const uint8_t> pixels, int32_t pitch, int32_t columns, int32_t rows)
{
    if (columns <= 0 || rows <= 0)
    {
        return {};
    }

    const MCArt art(pixels, pitch, columns, rows);
    constexpr int32_t Scale = MCSharpText::Scale;
    const int32_t width = columns * Scale;
    const int32_t height = rows * Scale;
    // The distance from each redrawn pixel's centre to the nearest stroke, in art pixels.
    std::vector<float> distance(static_cast<size_t>(width) * static_cast<size_t>(height),
                                std::numeric_limits<float>::max());
    // A redrawn pixel's centre in art pixels, and how far from a stroke one may be touched by it.
    const auto artPoint = [](int32_t i, int32_t j)
    {
        return MCPoint{(static_cast<float>(i) + Centre) / static_cast<float>(Scale),
                       (static_cast<float>(j) + Centre) / static_cast<float>(Scale)};
    };

    constexpr float RedrawnPixel = 1.0f / static_cast<float>(Scale);
    const float reach = StrokeRadius + RedrawnPixel;

    const auto stroke = [&](MCPoint a, MCPoint b)
    {
        const auto redrawn = [](float at) { return static_cast<int32_t>(std::floor(at * static_cast<float>(Scale))); };
        const int32_t left = std::max(redrawn(std::min(a.X, b.X) - reach), 0);
        const int32_t right = std::min(redrawn(std::max(a.X, b.X) + reach), width - 1);
        const int32_t top = std::max(redrawn(std::min(a.Y, b.Y) - reach), 0);
        const int32_t bottom = std::min(redrawn(std::max(a.Y, b.Y) + reach), height - 1);

        for (int32_t j = top; j <= bottom; ++j)
        {
            for (int32_t i = left; i <= right; ++i)
            {
                const MCPoint centre = artPoint(i, j);
                float& nearest = distance[static_cast<size_t>(j) * static_cast<size_t>(width) + static_cast<size_t>(i)];
                nearest = std::min(nearest, DistanceTo(a, b, centre));
            }
        }
    };

    for (const MCRun& run : Runs(art))
    {
        const std::vector<MCPoint> line = Rounded(Straightened(run), run);

        if (line.size() == 1)
        {
            stroke(line[0], line[0]);
            continue;
        }

        for (size_t i = 0; i + 1 < line.size(); ++i)
        {
            stroke(line[i], line[i + 1]);
        }

        if (run.Closed)
        {
            stroke(line.back(), line.front());
        }
    }

    std::vector<uint8_t> out(static_cast<size_t>(width) * static_cast<size_t>(height) * BytesPerPixel, 0);

    for (int32_t j = 0; j < height; ++j)
    {
        for (int32_t i = 0; i < width; ++i)
        {
            const size_t at = static_cast<size_t>(j) * static_cast<size_t>(width) + static_cast<size_t>(i);
            // Smoothed over one redrawn pixel at the stroke's edge.
            const float coverage =
                std::clamp((StrokeRadius - distance[at]) * static_cast<float>(Scale) + Centre, 0.0f, 1.0f);

            if (coverage <= 0.0f)
            {
                continue;
            }

            // The byte of the art pixel nearest the redrawn one.
            const auto [px, py] = artPoint(i, j);
            const auto gx = static_cast<int32_t>(px);
            const auto gy = static_cast<int32_t>(py);
            float nearest = std::numeric_limits<float>::max();
            uint8_t value = MCSharpText::NoInk;

            for (int32_t y = gy - ValueReach; y <= gy + ValueReach; ++y)
            {
                for (int32_t x = gx - ValueReach; x <= gx + ValueReach; ++x)
                {
                    if (!art.Ink(x, y))
                    {
                        continue;
                    }

                    const float ex = static_cast<float>(x) + Centre - px;
                    const float ey = static_cast<float>(y) + Centre - py;

                    if (const float centre = ex * ex + ey * ey; centre < nearest)
                    {
                        nearest = centre;
                        value = art.ByteAt(x, y);
                    }
                }
            }

            if (value == MCSharpText::NoInk)
            {
                continue;
            }

            uint8_t* pixel = &out[at * BytesPerPixel];
            pixel[0] = value;
            pixel[1] = static_cast<uint8_t>(std::lround(coverage * FullCoverage));
        }
    }

    return out;
}
