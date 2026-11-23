#include "stdafx.h"
#include "platform/MCUiSoftware.h"

namespace
{
    constexpr size_t PixelBytes = 4;
    constexpr float ByteMax = 255.0f;
    constexpr uint32_t Full = 255;

    /// <summary>The most boxes a redraw keeps apart; past it, a new one joins the box it adds least to.</summary>
    constexpr size_t MostBoxes = 8;

    /// <summary>How many pictures of the window's size the scaled copies may hold before they're all let go of.</summary>
    constexpr size_t ScaledWindowsMost = 4;

    /// <summary>Boost's hash_combine's golden-ratio constant.</summary>
    constexpr size_t HashMix = 0x9e3779b97f4a7c15ULL;
    constexpr int HashLeft = 6;
    constexpr int HashRight = 2;

    std::string SdlError(std::string_view what)
    {
        return std::format("the menus' software drawing, {}: {}", what, SDL_GetError());
    }

    SDL_FRect Float(const SDL_Rect& rect)
    {
        return SDL_FRect{static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w),
                         static_cast<float>(rect.h)};
    }

    bool Empty(const SDL_Rect& rect)
    {
        return rect.w <= 0 || rect.h <= 0;
    }

    int64_t Area(const SDL_Rect& rect)
    {
        return static_cast<int64_t>(rect.w) * static_cast<int64_t>(rect.h);
    }

    SDL_Rect Union(const SDL_Rect& first, const SDL_Rect& second)
    {
        SDL_Rect both{};
        SDL_GetRectUnion(&first, &second, &both);
        return both;
    }

    /// <summary>Where <paramref name="first"/> and <paramref name="second"/> overlap (empty: nowhere).</summary>
    SDL_Rect Overlap(const SDL_Rect& first, const SDL_Rect& second)
    {
        SDL_Rect both{};
        return SDL_GetRectIntersection(&first, &second, &both) ? both : SDL_Rect{};
    }

    /// <summary>SDL's MULT_DIV_255 (SDL_blit.h): <paramref name="a"/> x <paramref name="b"/> / 255, rounded so.</summary>
    constexpr uint32_t MultiplyDivide255(uint32_t a, uint32_t b)
    {
        uint32_t product = a * b + 1;
        product += product >> 8;
        return product >> 8;
    }

    /// <summary>
    /// One channel of a premultiplied colour over <paramref name="back"/>, <paramref name="kept"/> (255 - its alpha) of
    /// it left: SDL_blit_auto.c's SDL_COPY_BLEND_PREMULTIPLIED.
    /// </summary>
    constexpr uint8_t Over(uint32_t front, uint32_t kept, uint8_t back)
    {
        return static_cast<uint8_t>(std::min(front + MultiplyDivide255(kept, back), Full));
    }

    /// <summary>SDL's software triangles' fixed point (SDL_triangle.c's FP_BITS 1): half pixels.</summary>
    constexpr int64_t FixedPerPixel = 2;
    constexpr size_t TriangleCorners = 3;

    /// <summary>A triangle's corner in SDL's fixed point.</summary>
    struct MCFixed
    {
        int64_t X = 0;
        int64_t Y = 0;
    };

    /// <summary>(b - a) x (c - a), as SDL_triangle.c's cross_product.</summary>
    int64_t Cross(const MCFixed& a, const MCFixed& b, int64_t cx, int64_t cy)
    {
        return (b.X - a.X) * (cy - a.Y) - (b.Y - a.Y) * (cx - a.X);
    }

    /// <summary>Whether a to b is a top or a left edge, whose pixels the triangle takes (SDL's is_top_left).</summary>
    bool TopLeft(const MCFixed& a, const MCFixed& b, bool clockwise)
    {
        return clockwise ? (a.Y == b.Y && a.X < b.X) || b.Y < a.Y : (a.Y == b.Y && b.X < a.X) || a.Y < b.Y;
    }

    /// <summary><paramref name="value"/> / <paramref name="divisor"/> (above 0) rounded down.</summary>
    int64_t FloorDivide(int64_t value, int64_t divisor)
    {
        const int64_t quotient = value / divisor;
        return value % divisor != 0 && value < 0 ? quotient - 1 : quotient;
    }

    /// <summary>
    /// A colour channel between a triangle's corners along a row: (w0 c0 + w1 c1 + w2 c2) / area rounded down, as SDL
    /// divides it at each pixel (its weights are never below 0 inside), kept as a quotient and a remainder and stepped.
    /// </summary>
    class MCStepped
    {
    public:
        MCStepped(int64_t value, int64_t step, int64_t area)
            : _Whole(FloorDivide(value, area))
            , _Left(value - _Whole * area)
            , _StepWhole(FloorDivide(step, area))
            , _StepLeft(step - _StepWhole * area)
            , _Area(area)
        {
        }

        int64_t Value() const { return _Whole; }

        void Step()
        {
            _Whole += _StepWhole;
            _Left += _StepLeft;

            if (_Left >= _Area)
            {
                _Left -= _Area;
                ++_Whole;
            }
        }

    private:
        int64_t _Whole;
        int64_t _Left;
        int64_t _StepWhole;
        int64_t _StepLeft;
        int64_t _Area;
    };

    /// <summary>
    /// A straight colour blended over a pixel as SDL_BlitTriangle_Slow blends one through a white texel: the colour
    /// premultiplied by its alpha (rounded down), the pixel kept as much as the alpha leaves (rounded down).
    /// </summary>
    class MCStraight
    {
    public:
        explicit MCStraight(const std::array<int64_t, PixelBytes>& color)
            : _Alpha(static_cast<uint32_t>(color[3])), _Kept(Full - _Alpha)
        {
            for (size_t channel = 0; channel < _Front.size(); ++channel)
            {
                const auto value = static_cast<uint32_t>(color[channel]);
                _Front[channel] = _Alpha < Full ? value * _Alpha / Full : value;
            }
        }

        void Over(uint8_t* pixel) const
        {
            for (size_t channel = 0; channel < _Front.size(); ++channel)
            {
                pixel[channel] = static_cast<uint8_t>(_Front[channel] + _Kept * pixel[channel] / Full);
            }

            pixel[3] = static_cast<uint8_t>(_Alpha + _Kept * pixel[3] / Full);
        }

    private:
        std::array<uint32_t, PixelBytes - 1> _Front{};
        uint32_t _Alpha;
        uint32_t _Kept;
    };

    /// <summary>A vertex's colour channel as SDL's software renderer takes it (SW_QueueGeometry, a colour scale of 1).</summary>
    uint8_t SdlChannel(float channel)
    {
        return static_cast<uint8_t>(std::round(std::clamp(channel, 0.0f, 1.0f) * ByteMax));
    }

    /// <summary>
    /// <paramref name="box"/> added to <paramref name="boxes"/>: joined with every box it overlaps (a pixel is drawn
    /// once), and past <see cref="MostBoxes"/> with the box their union adds least to.
    /// </summary>
    void AddBox(std::vector<SDL_Rect>& boxes, SDL_Rect box)
    {
        if (Empty(box))
        {
            return;
        }

        for (auto overlapping = boxes.begin(); overlapping != boxes.end();)
        {
            if (SDL_HasRectIntersection(&box, &*overlapping))
            {
                box = Union(box, *overlapping);
                boxes.erase(overlapping);
                overlapping = boxes.begin();
            }
            else
            {
                ++overlapping;
            }
        }

        if (boxes.size() < MostBoxes)
        {
            boxes.push_back(box);
            return;
        }

        const auto added = [&box](const SDL_Rect& other) { return Area(Union(box, other)) - Area(box) - Area(other); };
        const auto nearest = std::ranges::min_element(boxes, {}, added);
        const SDL_Rect joined = Union(box, *nearest);
        boxes.erase(nearest);
        AddBox(boxes, joined);
    }

    void Mix(size_t& hash, size_t value)
    {
        hash ^= value + HashMix + (hash << HashLeft) + (hash >> HashRight);
    }

    /// <summary>A draw's hash, alike for draws <see cref="MCUiSameDraw"/> finds the same.</summary>
    struct MCDrawHash
    {
        size_t operator()(const MCUiDraw* draw) const
        {
            size_t hash = std::hash<const void*>{}(draw->Geometry.get());
            Mix(hash, std::hash<const void*>{}(draw->Texture.get()));
            Mix(hash, std::hash<float>{}(draw->X));
            Mix(hash, std::hash<float>{}(draw->Y));

            if (draw->Scissor.has_value())
            {
                for (const int value : {draw->Scissor->x, draw->Scissor->y, draw->Scissor->w, draw->Scissor->h})
                {
                    Mix(hash, std::hash<int>{}(value));
                }
            }

            return hash;
        }
    };

    struct MCDrawSame
    {
        bool operator()(const MCUiDraw* first, const MCUiDraw* second) const { return MCUiSameDraw(*first, *second); }
    };

    /// <summary>Where a draw stood in the last frame's changed run, in order, and the first not yet taken.</summary>
    struct MCPlaces
    {
        std::vector<size_t> At;
        size_t Next = 0;
    };
}

void MCUiFill(SDL_Surface& picture, const SDL_Rect& box, const std::array<uint8_t, 4>& color)
{
    const SDL_Rect inside = Overlap(box, SDL_Rect{0, 0, picture.w, picture.h});
    const uint32_t kept = Full - color[3];

    if (Empty(inside) || (kept == Full && color[0] == 0 && color[1] == 0 && color[2] == 0))
    {
        return;
    }

    uint32_t packed = 0;
    std::memcpy(&packed, color.data(), PixelBytes);

    for (int y = inside.y; y < inside.y + inside.h; ++y)
    {
        uint8_t* row = static_cast<uint8_t*>(picture.pixels) + static_cast<ptrdiff_t>(y) * picture.pitch +
                       static_cast<ptrdiff_t>(inside.x) * static_cast<ptrdiff_t>(PixelBytes);

        // Opaque: the colour itself (nothing of the picture is kept).
        if (kept == 0)
        {
            std::fill_n(reinterpret_cast<uint32_t*>(row), inside.w, packed);
            continue;
        }

        for (int x = 0; x < inside.w; ++x, row += PixelBytes)
        {
            row[0] = Over(color[0], kept, row[0]);
            row[1] = Over(color[1], kept, row[1]);
            row[2] = Over(color[2], kept, row[2]);
            row[3] = Over(color[3], kept, row[3]);
        }
    }
}

void MCUiSdlTriangles(const MCUiDraw& draw, const MCUiPiece& piece, std::vector<SDL_Vertex>& vertices)
{
    const MCUiGeometry& geometry = *draw.Geometry;
    vertices.clear();

    for (size_t i = piece.First; i < piece.First + piece.Count; ++i)
    {
        const MCUiVertex& vertex = geometry.Vertices[static_cast<size_t>(geometry.Indices[i])];
        const float alpha = vertex.Color[3] / ByteMax;
        const auto straight = [alpha](uint8_t channel)
        { return alpha > 0.0f ? std::min(channel / ByteMax / alpha, 1.0f) : 0.0f; };
        vertices.push_back(SDL_Vertex{
            .position = {static_cast<float>(MCUiPlace(vertex.X, draw.X)),
                         static_cast<float>(MCUiPlace(vertex.Y, draw.Y))},
            .color = {straight(vertex.Color[0]), straight(vertex.Color[1]), straight(vertex.Color[2]), alpha},
            .tex_coord = {draw.Texture != nullptr ? vertex.U : 0.0f, draw.Texture != nullptr ? vertex.V : 0.0f},
        });
    }
}

namespace
{
    /// <summary>A triangle's corners, as SDL_SW_RenderGeometryRaw numbers them (none: -1).</summary>
    using MCSdlCorners = std::array<int, TriangleCorners>;
    constexpr int NoCorner = -1;

    /// <summary>
    /// Whether SDL_SW_RenderGeometryRaw takes triangle <paramref name="now"/> with <paramref name="last"/>, which shares
    /// two of its corners, as a rectangle: its test as it stands (it looks only at the corners it names, so the last
    /// triangle's third may lie anywhere).
    /// </summary>
    bool SdlTakesAsRectangle(std::span<const SDL_Vertex> vertices, const MCSdlCorners& last, const MCSdlCorners& now)
    {
        const auto x = [vertices](int corner) { return vertices[static_cast<size_t>(corner)].position.x; };
        const auto y = [vertices](int corner) { return vertices[static_cast<size_t>(corner)].position.y; };
        const auto [k0, k1, k2] = now;
        // The top left (A) and bottom right (B) of the triangle's corners, its third (C), the last's other (C2).
        int a = 0;
        int b = 0;

        if (x(k0) <= x(k1) && y(k0) <= y(k1))
        {
            a = x(k0) <= x(k2) && y(k0) <= y(k2) ? k0 : k2;
        }
        else
        {
            a = x(k1) <= x(k2) && y(k1) <= y(k2) ? k1 : k2;
        }

        if (x(k0) >= x(k1) && y(k0) >= y(k1))
        {
            b = x(k0) >= x(k2) && y(k0) >= y(k2) ? k0 : k2;
        }
        else
        {
            b = x(k1) >= x(k2) && y(k1) >= y(k2) ? k1 : k2;
        }

        const auto other = [a, b](const MCSdlCorners& corners)
        {
            return corners[0] != a && corners[0] != b   ? corners[0]
                   : corners[1] != a && corners[1] != b ? corners[1]
                                                        : corners[2];
        };

        const int c = other(now);
        const int c2 = other(last);
        const auto rightAngle = [&](int third)
        { return (x(a) == x(third) && y(b) == y(third)) || (y(a) == y(third) && x(b) == x(third)); };
        const auto asA = [vertices, a](int corner)
        {
            const SDL_Vertex& vertex = vertices[static_cast<size_t>(corner)];
            return std::memcmp(&vertex.color, &vertices[static_cast<size_t>(a)].color, sizeof(SDL_FColor)) == 0 &&
                   vertex.tex_coord.x >= 0.0f && vertex.tex_coord.x <= 1.0f;
        };

        return rightAngle(c) && rightAngle(c2) && asA(a) && asA(b) && asA(c) && asA(c2);
    }
}

bool MCUiSdlMakesRectangles(std::span<const SDL_Vertex> vertices)
{
    // SDL_SW_RenderGeometryRaw's walk over vertices given without indices (SDL_RenderGeometry's, textured: the white
    // texel): each triangle's corners matched by value with the last triangle's (remap_indices), and the two taken as a
    // rectangle when they share two corners and pass SdlTakesAsRectangle; after one, the next triangle starts afresh.
    // This finds its first rectangle, so stops at it.
    const auto same = [vertices](int first, size_t second)
    {
        const SDL_Vertex& a = vertices[static_cast<size_t>(first)];
        const SDL_Vertex& b = vertices[second];
        return a.position.x == b.position.x && a.position.y == b.position.y && a.tex_coord.x == b.tex_coord.x &&
               a.tex_coord.y == b.tex_coord.y && std::memcmp(&a.color, &b.color, sizeof(SDL_FColor)) == 0;
    };

    MCSdlCorners last = {NoCorner, NoCorner, NoCorner};

    for (size_t first = 0; first + TriangleCorners <= vertices.size(); first += TriangleCorners)
    {
        MCSdlCorners now{};

        for (size_t i = 0; i < TriangleCorners; ++i)
        {
            now[i] = static_cast<int>(first + i);

            if (last[0] != NoCorner)
            {
                if (const auto earlier =
                        std::ranges::find_if(last, [&](int corner) { return same(corner, first + i); });
                    earlier != last.end())
                {
                    now[i] = *earlier;
                }
            }
        }

        if (last[0] != NoCorner &&
            std::ranges::count_if(last, [&now](int corner) { return std::ranges::find(now, corner) != now.end(); }) ==
                2 &&
            SdlTakesAsRectangle(vertices, last, now))
        {
            return true;
        }

        last = now;
    }

    return false;
}

void MCUiTriangle(SDL_Surface& picture, const SDL_Rect& clip, std::span<const SDL_Vertex, 3> corners)
{
    // The corners and colours as SDL's software renderer takes them (SW_QueueGeometry): whole pixels in half pixels,
    // channels rounded to bytes.
    std::array<MCFixed, TriangleCorners> at{};
    std::array<std::array<int64_t, PixelBytes>, TriangleCorners> color{};

    for (size_t i = 0; i < TriangleCorners; ++i)
    {
        at[i] = MCFixed{static_cast<int64_t>(static_cast<int>(corners[i].position.x)) * FixedPerPixel,
                        static_cast<int64_t>(static_cast<int>(corners[i].position.y)) * FixedPerPixel};
        color[i] = {SdlChannel(corners[i].color.r), SdlChannel(corners[i].color.g), SdlChannel(corners[i].color.b),
                    SdlChannel(corners[i].color.a)};
    }

    int64_t area = Cross(at[0], at[1], at[2].X, at[2].Y);

    if (area == 0)
    {
        return;
    }

    // SDL_SW_BlitTriangle: the corners' box cut by the clip, walked from its first pixel's middle.
    const int64_t left = std::min({at[0].X, at[1].X, at[2].X});
    const int64_t top = std::min({at[0].Y, at[1].Y, at[2].Y});
    const SDL_Rect box{static_cast<int>(left / FixedPerPixel), static_cast<int>(top / FixedPerPixel),
                       static_cast<int>((std::max({at[0].X, at[1].X, at[2].X}) - left) / FixedPerPixel),
                       static_cast<int>((std::max({at[0].Y, at[1].Y, at[2].Y}) - top) / FixedPerPixel)};
    const SDL_Rect walked = Overlap(Overlap(box, clip), SDL_Rect{0, 0, picture.w, picture.h});

    if (Empty(walked))
    {
        return;
    }

    const bool clockwise = area > 0;
    const int64_t sign = clockwise ? 1 : -1;
    area *= sign;
    // Each weight's step a pixel across and a row down.
    const std::array<int64_t, TriangleCorners> across = {sign * (at[1].Y - at[2].Y) * FixedPerPixel,
                                                         sign * (at[2].Y - at[0].Y) * FixedPerPixel,
                                                         sign * (at[0].Y - at[1].Y) * FixedPerPixel};
    const std::array<int64_t, TriangleCorners> down = {sign * (at[2].X - at[1].X) * FixedPerPixel,
                                                       sign * (at[0].X - at[2].X) * FixedPerPixel,
                                                       sign * (at[1].X - at[0].X) * FixedPerPixel};
    const int64_t middleX = walked.x * FixedPerPixel + FixedPerPixel / 2;
    const int64_t middleY = walked.y * FixedPerPixel + FixedPerPixel / 2;
    std::array<int64_t, TriangleCorners> row = {sign * Cross(at[1], at[2], middleX, middleY),
                                                sign * Cross(at[2], at[0], middleX, middleY),
                                                sign * Cross(at[0], at[1], middleX, middleY)};
    const std::array<int64_t, TriangleCorners> bias = {TopLeft(at[1], at[2], clockwise) ? 0 : -1,
                                                       TopLeft(at[2], at[0], clockwise) ? 0 : -1,
                                                       TopLeft(at[0], at[1], clockwise) ? 0 : -1};
    const bool uniform = color[0] == color[1] && color[1] == color[2];

    if (uniform && color[0][3] == 0)
    {
        // Blended with nothing, every pixel stays as it is.
        return;
    }

    const MCStraight one(color[0]);
    const auto weighted = [&color](const std::array<int64_t, TriangleCorners>& weights, size_t channel)
    { return weights[0] * color[0][channel] + weights[1] * color[1][channel] + weights[2] * color[2][channel]; };

    for (int y = 0; y < walked.h; ++y)
    {
        // The row's pixels inside every edge (the weights SDL tests at each, w + bias >= 0, rising or falling along
        // the row): from the first to the last, with no test at each.
        int64_t first = 0;
        int64_t last = walked.w - 1;

        for (size_t i = 0; i < TriangleCorners; ++i)
        {
            const int64_t start = row[i] + bias[i];

            if (across[i] > 0)
            {
                first = std::max(first, -FloorDivide(start, across[i]));
            }
            else if (across[i] < 0)
            {
                last = std::min(last, FloorDivide(start, -across[i]));
            }
            else if (start < 0)
            {
                last = first - 1;
            }
        }

        uint8_t* pixel = static_cast<uint8_t*>(picture.pixels) + static_cast<ptrdiff_t>(walked.y + y) * picture.pitch +
                         static_cast<ptrdiff_t>(walked.x + first) * static_cast<ptrdiff_t>(PixelBytes);

        if (uniform)
        {
            for (int64_t x = first; x <= last; ++x, pixel += PixelBytes)
            {
                one.Over(pixel);
            }
        }
        else if (first <= last)
        {
            const std::array<int64_t, TriangleCorners> w = {row[0] + across[0] * first, row[1] + across[1] * first,
                                                            row[2] + across[2] * first};
            std::array<MCStepped, PixelBytes> channels = {MCStepped(weighted(w, 0), weighted(across, 0), area),
                                                          MCStepped(weighted(w, 1), weighted(across, 1), area),
                                                          MCStepped(weighted(w, 2), weighted(across, 2), area),
                                                          MCStepped(weighted(w, 3), weighted(across, 3), area)};

            for (int64_t x = first; x <= last; ++x, pixel += PixelBytes)
            {
                const MCStraight here(std::array<int64_t, PixelBytes>{channels[0].Value(), channels[1].Value(),
                                                                      channels[2].Value(), channels[3].Value()});
                here.Over(pixel);

                for (MCStepped& channel : channels)
                {
                    channel.Step();
                }
            }
        }

        for (size_t i = 0; i < TriangleCorners; ++i)
        {
            row[i] += down[i];
        }
    }
}

SDL_Rect MCUiFootprint(const MCUiDraw& draw, const SDL_Rect& window)
{
    if (draw.Geometry == nullptr || draw.Geometry->Vertices.empty())
    {
        return {};
    }

    int32_t left = std::numeric_limits<int32_t>::max();
    int32_t top = std::numeric_limits<int32_t>::max();
    int32_t right = std::numeric_limits<int32_t>::min();
    int32_t bottom = std::numeric_limits<int32_t>::min();

    // Every corner lands on a pixel edge (MCUiPlace): a rectangle covers the pixels between its edges, a triangle the
    // pixels whose middles it holds, inside its corners' box either way.
    for (const MCUiVertex& vertex : draw.Geometry->Vertices)
    {
        const int32_t x = MCUiPlace(vertex.X, draw.X);
        const int32_t y = MCUiPlace(vertex.Y, draw.Y);
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }

    const SDL_Rect inside = Overlap(SDL_Rect{left, top, right - left, bottom - top}, window);
    return draw.Scissor.has_value() ? Overlap(inside, *draw.Scissor) : inside;
}

std::vector<SDL_Rect> MCUiChangedBoxes(std::span<const MCUiDraw> before, std::span<const MCUiDraw> now,
                                       const SDL_Rect& window)
{
    std::vector<SDL_Rect> boxes;

    // The same draws at the start and the end; between them, each of the new draws matched to the first like it in
    // the old ones after the last matched (so the draws in both stay in order).
    const size_t shorter = std::min(before.size(), now.size());
    size_t same = 0;

    while (same < shorter && MCUiSameDraw(before[same], now[same]))
    {
        ++same;
    }

    size_t sameAtEnd = 0;

    while (sameAtEnd < shorter - same &&
           MCUiSameDraw(before[before.size() - 1 - sameAtEnd], now[now.size() - 1 - sameAtEnd]))
    {
        ++sameAtEnd;
    }

    const std::span<const MCUiDraw> gone = before.subspan(same, before.size() - same - sameAtEnd);
    const std::span<const MCUiDraw> come = now.subspan(same, now.size() - same - sameAtEnd);
    std::unordered_map<const MCUiDraw*, MCPlaces, MCDrawHash, MCDrawSame> places;

    for (size_t i = 0; i < gone.size(); ++i)
    {
        places[&gone[i]].At.push_back(i);
    }

    std::vector<bool> kept(gone.size(), false);
    std::optional<size_t> lastKept;

    for (const MCUiDraw& draw : come)
    {
        const auto found = places.find(&draw);

        if (found != places.end())
        {
            MCPlaces& place = found->second;

            while (place.Next < place.At.size() && lastKept.has_value() && place.At[place.Next] <= *lastKept)
            {
                ++place.Next;
            }

            if (place.Next < place.At.size())
            {
                lastKept = place.At[place.Next++];
                kept[*lastKept] = true;
                continue;
            }
        }

        AddBox(boxes, MCUiFootprint(draw, window));
    }

    for (size_t i = 0; i < gone.size(); ++i)
    {
        if (!kept[i])
        {
            AddBox(boxes, MCUiFootprint(gone[i], window));
        }
    }

    return boxes;
}

MCUiSoftware::MCUiSoftware() = default;

// The textures go before the renderer, the renderer before its surface: the members' order, backwards.
MCUiSoftware::~MCUiSoftware()
{
    _Scaled.clear();
    _Textures.clear();
    _White.reset();
    _Renderer.reset();
}

void MCUiSoftware::Forget()
{
    _Whole = true;
}

std::expected<std::reference_wrapper<SDL_Texture>, std::string> MCUiSoftware::TextureOf(const MCUiTexture& texture)
{
    if (const auto found = _Textures.find(texture.Id); found != _Textures.end())
    {
        return std::ref(*found->second.Texture);
    }

    if (texture.Width <= 0 || texture.Height <= 0 ||
        texture.Pixels.size() < static_cast<size_t>(texture.Width) * static_cast<size_t>(texture.Height) * PixelBytes)
    {
        return std::unexpected(std::format("the menus' picture {} ({}x{}) has {} bytes", texture.Id, texture.Width,
                                           texture.Height, texture.Pixels.size()));
    }

    MCSdlHandle<SDL_Texture> made(SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                                    texture.Width, texture.Height));

    if (made == nullptr ||
        !SDL_UpdateTexture(made.get(), nullptr, texture.Pixels.data(), texture.Width * static_cast<int>(PixelBytes)))
    {
        return std::unexpected(SdlError(std::format("picture {} ({}x{})", texture.Id, texture.Width, texture.Height)));
    }

    SDL_SetTextureBlendMode(made.get(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetTextureScaleMode(made.get(), texture.Smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_Texture& kept = *made;
    _Textures.emplace(texture.Id, MCKept{.Picture = {}, .Texture = std::move(made)});
    return std::ref(kept);
}

size_t MCUiSoftware::MCScaledHash::operator()(const MCScaledKey& key) const
{
    size_t hash = std::hash<uint64_t>{}(key.Picture);

    for (const int value : {key.Source.x, key.Source.y, key.Source.w, key.Source.h, key.Width, key.Height})
    {
        Mix(hash, std::hash<int>{}(value));
    }

    return hash;
}

std::expected<std::reference_wrapper<SDL_Texture>, std::string> MCUiSoftware::ScaledOf(
    const std::shared_ptr<const MCUiTexture>& picture, const SDL_Rect& source, int width, int height)
{
    const MCUiTexture& texture = *picture;
    const MCScaledKey key{.Picture = texture.Id, .Source = source, .Width = width, .Height = height};

    if (const auto found = _Scaled.find(key); found != _Scaled.end())
    {
        found->second.Picture = picture;
        return std::ref(*found->second.Texture);
    }

    const auto failed = [&](std::string_view what)
    {
        return std::unexpected(SdlError(std::format("picture {}'s {},{} {}x{} scaled to {}x{}, {}", texture.Id,
                                                    source.x, source.y, source.w, source.h, width, height, what)));
    };

    // The part scaled, in place (SDL only reads it).
    const size_t pitch = static_cast<size_t>(texture.Width) * PixelBytes;
    MCSdlHandle<SDL_Surface> part(SDL_CreateSurfaceFrom(source.w, source.h, SDL_PIXELFORMAT_RGBA32,
                                                        const_cast<uint8_t*>(texture.Pixels.data()) +
                                                            static_cast<size_t>(source.y) * pitch +
                                                            static_cast<size_t>(source.x) * PixelBytes,
                                                        static_cast<int>(pitch)));

    if (part == nullptr)
    {
        return failed("its part");
    }

    MCSdlHandle<SDL_Surface> scaled(
        SDL_ScaleSurface(part.get(), width, height, texture.Smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));

    if (scaled == nullptr)
    {
        return failed("scaling");
    }

    MCSdlHandle<SDL_Texture> made(
        SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height));

    if (made == nullptr || !SDL_UpdateTexture(made.get(), nullptr, scaled->pixels, scaled->pitch))
    {
        return failed("its copy");
    }

    SDL_SetTextureBlendMode(made.get(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetTextureScaleMode(made.get(), SDL_SCALEMODE_NEAREST);
    SDL_Texture& kept = *made;
    _ScaledBytes += static_cast<size_t>(width) * static_cast<size_t>(height) * PixelBytes;
    _Scaled.emplace(key, MCKept{.Picture = picture, .Texture = std::move(made)});
    return std::ref(kept);
}

void MCUiSoftware::Prune()
{
    std::erase_if(_Textures, [](const auto& entry) { return entry.second.Picture.expired(); });
    std::erase_if(_Scaled,
                  [this](const auto& entry)
                  {
                      if (!entry.second.Picture.expired())
                      {
                          return false;
                      }

                      _ScaledBytes -=
                          static_cast<size_t>(entry.first.Width) * static_cast<size_t>(entry.first.Height) * PixelBytes;
                      return true;
                  });

    // Too many: all let go of (made again as they're drawn).
    if (_Surface != nullptr && _ScaledBytes > ScaledWindowsMost * static_cast<size_t>(_Surface->w) *
                                                  static_cast<size_t>(_Surface->h) * PixelBytes)
    {
        _Scaled.clear();
        _ScaledBytes = 0;
    }
}

std::expected<void, std::string> MCUiSoftware::MakePicture(int width, int height)
{
    _Scaled.clear();
    _ScaledBytes = 0;
    _Textures.clear();
    _White.reset();
    _Renderer.reset();
    _Queued = false;
    _Surface.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));

    if (_Surface == nullptr)
    {
        return std::unexpected(SdlError(std::format("a {}x{} picture", width, height)));
    }

    _Renderer.reset(SDL_CreateSoftwareRenderer(_Surface.get()));
    _White.reset(_Renderer != nullptr
                     ? SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, 1)
                     : nullptr);
    constexpr std::array<uint8_t, PixelBytes> White = {255, 255, 255, 255};

    if (_White == nullptr || !SDL_UpdateTexture(_White.get(), nullptr, White.data(), PixelBytes))
    {
        _Surface.reset();
        return std::unexpected(SdlError(std::format("the {}x{} picture's renderer", width, height)));
    }

    SDL_SetTextureBlendMode(_White.get(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetTextureScaleMode(_White.get(), SDL_SCALEMODE_NEAREST);
    return {};
}

std::expected<void, std::string> MCUiSoftware::RunQueue()
{
    if (_Queued && !SDL_FlushRenderer(_Renderer.get()))
    {
        return std::unexpected(SdlError("drawing"));
    }

    _Queued = false;
    return {};
}

std::expected<void, std::string> MCUiSoftware::Draw(const MCUiDraws& draws)
{
    const int width = std::max(draws.Width, 1);
    const int height = std::max(draws.Height, 1);
    const SDL_Rect window{0, 0, width, height};

    if (_Surface == nullptr || _Surface->w != width || _Surface->h != height)
    {
        _Whole = true;

        if (auto made = MakePicture(width, height); !made)
        {
            return made;
        }
    }

    _Changed = _Whole ? std::vector<SDL_Rect>{window} : MCUiChangedBoxes(_Drawn, draws.Draws, window);
    // Until this picture is whole, the next is drawn whole.
    _Whole = true;
    _Footprints.clear();

    for (const MCUiDraw& draw : draws.Draws)
    {
        _Footprints.push_back(MCUiFootprint(draw, window));
    }

    for (const SDL_Rect& box : _Changed)
    {
        if (auto drawn = DrawBox(draws, box); !drawn)
        {
            return drawn;
        }
    }

    if (auto ran = RunQueue(); !ran)
    {
        return ran;
    }

    // These draws kept (their geometry and pictures held); the copies of pictures nothing holds any more let go of.
    _Drawn = draws.Draws;
    _Whole = false;
    Prune();
    return {};
}

std::expected<void, std::string> MCUiSoftware::DrawBox(const MCUiDraws& draws, const SDL_Rect& box)
{
    // The box cleared to transparent, directly (SDL's queue run first: its draws come before), all of it (the renderer
    // leaves its last clip rectangle on the picture; it sets its own again before each run of its queue).
    if (auto ran = RunQueue(); !ran)
    {
        return ran;
    }

    SDL_SetSurfaceClipRect(_Surface.get(), nullptr);

    if (!SDL_FillSurfaceRect(_Surface.get(), &box, 0))
    {
        return std::unexpected(SdlError(std::format("clearing {},{} {}x{}", box.x, box.y, box.w, box.h)));
    }

    for (size_t i = 0; i < draws.Draws.size(); ++i)
    {
        if (!SDL_HasRectIntersection(&_Footprints[i], &box))
        {
            continue;
        }

        // Inside the footprint, so inside its scissor too: the box is its clip.
        const MCUiDraw& draw = draws.Draws[i];
        const SDL_Rect clip = draw.Scissor.has_value() ? Overlap(*draw.Scissor, box) : box;

        if (auto drawn = DrawOne(draw, clip); !drawn)
        {
            return drawn;
        }
    }

    return {};
}

std::expected<void, std::string> MCUiSoftware::DrawOne(const MCUiDraw& draw, const SDL_Rect& clip)
{
    SDL_Renderer* renderer = _Renderer.get();
    SDL_Texture* texture = _White.get();

    if (draw.Texture != nullptr)
    {
        auto copy = TextureOf(*draw.Texture);

        if (!copy)
        {
            return std::unexpected(copy.error());
        }

        texture = &copy->get();
        _Textures[draw.Texture->Id].Picture = draw.Texture;
    }

    SDL_SetRenderClipRect(renderer, &clip);
    _Pieces = MCUiPieces(draw);

    for (const MCUiPiece& piece : _Pieces)
    {
        // A rectangle, copied (a textured one when it shows whole texels).
        if (piece.Quad.has_value() && (draw.Texture == nullptr || piece.Quad->Source.has_value()))
        {
            const MCUiQuad& quad = *piece.Quad;
            const SDL_Rect cut = Overlap(quad.Target, clip);

            if (Empty(cut))
            {
                continue;
            }

            // A box of one colour: filled here, as SDL blends its white texel scaled over it (its queue run first).
            if (draw.Texture == nullptr)
            {
                if (auto ran = RunQueue(); !ran)
                {
                    return ran;
                }

                MCUiFill(*_Surface, cut, quad.Color);
                continue;
            }

            // A picture at another size, inside its texture: its scaled copy, texel for texel.
            SDL_Texture* copied = texture;
            SDL_Rect from = *quad.Source;
            const bool scaled = from.w != quad.Target.w || from.h != quad.Target.h;

            if (scaled && from.x >= 0 && from.y >= 0 && from.x + from.w <= draw.Texture->Width &&
                from.y + from.h <= draw.Texture->Height)
            {
                auto copy = ScaledOf(draw.Texture, from, quad.Target.w, quad.Target.h);

                if (!copy)
                {
                    return std::unexpected(copy.error());
                }

                copied = &copy->get();
                from = SDL_Rect{0, 0, quad.Target.w, quad.Target.h};
            }

            SDL_SetTextureColorMod(copied, quad.Color[0], quad.Color[1], quad.Color[2]);
            SDL_SetTextureAlphaMod(copied, quad.Color[3]);
            const SDL_FRect target = Float(quad.Target);
            const SDL_FRect source = Float(from);
            SDL_RenderTexture(renderer, copied, &source, &target);
            _Queued = true;
            continue;
        }

        // Triangles as they are, their corners on whole pixels, as the GPU's. SDL's software triangles blend only
        // straight alpha (SDL_triangle.c copies a premultiplied one over what's there), so their corners' colours are
        // given straight and blended so, which premultiplies them as it blends: the same but for the colours between
        // corners (a gradient's), mixed straight. A picture's own texels stay premultiplied: its half transparent ones
        // drawn so come out darker (pictures are rectangles, copied above, all but never this).
        MCUiSdlTriangles(draw, piece, _Vertices);

        // Untextured, drawn here as SDL draws them (its queue run first), unless it would take some as a rectangle.
        if (draw.Texture == nullptr && !MCUiSdlMakesRectangles(_Vertices))
        {
            if (auto ran = RunQueue(); !ran)
            {
                return ran;
            }

            for (size_t first = 0; first + TriangleCorners <= _Vertices.size(); first += TriangleCorners)
            {
                MCUiTriangle(*_Surface, clip,
                             std::span<const SDL_Vertex, TriangleCorners>(&_Vertices[first], TriangleCorners));
            }

            continue;
        }

        SDL_SetTextureColorMod(texture, Full, Full, Full);
        SDL_SetTextureAlphaMod(texture, Full);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(renderer, texture, _Vertices.data(), static_cast<int>(_Vertices.size()), nullptr, 0);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        _Queued = true;
    }

    return {};
}
