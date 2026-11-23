#include "stdafx.h"
#include "platform/MCUiDraws.h"

namespace
{
    /// <summary>The indices of two triangles.</summary>
    constexpr size_t PairIndices = 6;
    constexpr size_t Corners = 4;
    constexpr size_t TriangleIndices = 3;

    /// <summary>How far a texture place times its size may lie from a whole texel and still be taken as one.</summary>
    constexpr double WholeTexel = 1.0 / 1024.0;

    /// <summary><paramref name="place"/> texels into a texture <paramref name="size"/> wide, when it's whole.</summary>
    std::optional<int32_t> WholeTexels(float place, int32_t size)
    {
        const double texels = static_cast<double>(place) * size;
        const double whole = std::floor(texels + 0.5);
        return std::abs(texels - whole) <= WholeTexel ? std::optional(static_cast<int32_t>(whole)) : std::nullopt;
    }

    /// <summary>The rectangle triangles <paramref name="first"/> and the next make, when they make one.</summary>
    std::optional<MCUiQuad> QuadAt(const MCUiDraw& draw, size_t first)
    {
        const MCUiGeometry& geometry = *draw.Geometry;
        std::array<int32_t, Corners> corners{};
        size_t found = 0;

        for (size_t i = 0; i < PairIndices; ++i)
        {
            const int32_t index = geometry.Indices[first + i];

            if (index < 0 || static_cast<size_t>(index) >= geometry.Vertices.size())
            {
                return std::nullopt;
            }

            if (std::find(corners.begin(), corners.begin() + static_cast<ptrdiff_t>(found), index) ==
                corners.begin() + static_cast<ptrdiff_t>(found))
            {
                if (found == Corners)
                {
                    return std::nullopt;
                }

                corners[found++] = index;
            }
        }

        if (found != Corners)
        {
            return std::nullopt;
        }

        // The two triangles share a diagonal: two corners each has, apart on both axes.
        std::array<int32_t, 2> shared{};
        size_t sharedCount = 0;

        for (size_t i = 0; i < TriangleIndices; ++i)
        {
            const int32_t index = geometry.Indices[first + i];
            const auto second = geometry.Indices.begin() + static_cast<ptrdiff_t>(first + TriangleIndices);

            if (std::find(second, second + TriangleIndices, index) != second + TriangleIndices && sharedCount < 2)
            {
                shared[sharedCount++] = index;
            }
        }

        if (sharedCount != 2)
        {
            return std::nullopt;
        }

        const MCUiVertex& a = geometry.Vertices[static_cast<size_t>(shared[0])];
        const MCUiVertex& b = geometry.Vertices[static_cast<size_t>(shared[1])];

        if (MCUiPlace(a.X, draw.X) == MCUiPlace(b.X, draw.X) || MCUiPlace(a.Y, draw.Y) == MCUiPlace(b.Y, draw.Y))
        {
            return std::nullopt;
        }

        // Two columns and two rows, a corner at each, all in one colour.
        const MCUiVertex& origin = geometry.Vertices[static_cast<size_t>(corners[0])];
        int32_t left = MCUiPlace(origin.X, draw.X);
        int32_t top = MCUiPlace(origin.Y, draw.Y);
        int32_t right = left;
        int32_t bottom = top;

        for (const int32_t index : corners)
        {
            const MCUiVertex& vertex = geometry.Vertices[static_cast<size_t>(index)];
            left = std::min(left, MCUiPlace(vertex.X, draw.X));
            right = std::max(right, MCUiPlace(vertex.X, draw.X));
            top = std::min(top, MCUiPlace(vertex.Y, draw.Y));
            bottom = std::max(bottom, MCUiPlace(vertex.Y, draw.Y));

            if (vertex.Color != origin.Color)
            {
                return std::nullopt;
            }
        }

        // Each corner's texture place: the same along each edge, rising to the right and down.
        std::array<float, 2> u{};
        std::array<float, 2> v{};
        std::array<bool, Corners> seen{};

        for (const int32_t index : corners)
        {
            const MCUiVertex& vertex = geometry.Vertices[static_cast<size_t>(index)];
            const int32_t x = MCUiPlace(vertex.X, draw.X);
            const int32_t y = MCUiPlace(vertex.Y, draw.Y);

            if ((x != left && x != right) || (y != top && y != bottom))
            {
                return std::nullopt;
            }

            const size_t column = x == right ? 1 : 0;
            const size_t row = y == bottom ? 1 : 0;
            const size_t corner = row * 2 + column;

            if (seen[corner])
            {
                return std::nullopt;
            }

            seen[corner] = true;

            if (corner == 0)
            {
                u[0] = vertex.U;
                v[0] = vertex.V;
            }
            else if (corner == Corners - 1)
            {
                u[1] = vertex.U;
                v[1] = vertex.V;
            }
        }

        MCUiQuad quad{.Target = SDL_Rect{left, top, right - left, bottom - top}, .Color = origin.Color};

        if (draw.Texture == nullptr)
        {
            return quad;
        }

        for (const int32_t index : corners)
        {
            const MCUiVertex& vertex = geometry.Vertices[static_cast<size_t>(index)];
            const size_t column = MCUiPlace(vertex.X, draw.X) == right ? 1 : 0;
            const size_t row = MCUiPlace(vertex.Y, draw.Y) == bottom ? 1 : 0;

            if (vertex.U != u[column] || vertex.V != v[row])
            {
                return std::nullopt;
            }
        }

        const std::optional<int32_t> sourceLeft = WholeTexels(u[0], draw.Texture->Width);
        const std::optional<int32_t> sourceRight = WholeTexels(u[1], draw.Texture->Width);
        const std::optional<int32_t> sourceTop = WholeTexels(v[0], draw.Texture->Height);
        const std::optional<int32_t> sourceBottom = WholeTexels(v[1], draw.Texture->Height);

        if (!sourceLeft || !sourceRight || !sourceTop || !sourceBottom || *sourceRight <= *sourceLeft ||
            *sourceBottom <= *sourceTop)
        {
            return std::nullopt;
        }

        quad.Source = SDL_Rect{*sourceLeft, *sourceTop, *sourceRight - *sourceLeft, *sourceBottom - *sourceTop};
        return quad;
    }
}

int32_t MCUiPlace(float at, float by)
{
    return static_cast<int32_t>(std::floor(at + by + 0.5f));
}

std::vector<MCUiPiece> MCUiPieces(const MCUiDraw& draw)
{
    std::vector<MCUiPiece> pieces;

    if (draw.Geometry == nullptr)
    {
        return pieces;
    }

    const size_t count = draw.Geometry->Indices.size() - draw.Geometry->Indices.size() % TriangleIndices;

    const auto addTriangles = [&](size_t first, size_t indices)
    {
        if (!pieces.empty() && !pieces.back().Quad.has_value() && pieces.back().First + pieces.back().Count == first)
        {
            pieces.back().Count += indices;
        }
        else
        {
            pieces.push_back(MCUiPiece{.First = first, .Count = indices});
        }
    };

    size_t at = 0;

    for (; at + PairIndices <= count; at += PairIndices)
    {
        if (std::optional<MCUiQuad> quad = QuadAt(draw, at); quad.has_value())
        {
            pieces.push_back(MCUiPiece{.Quad = *quad, .First = at, .Count = PairIndices});
        }
        else
        {
            addTriangles(at, PairIndices);
        }
    }

    if (at < count)
    {
        addTriangles(at, count - at);
    }

    return pieces;
}

bool MCUiSameDraw(const MCUiDraw& first, const MCUiDraw& second)
{
    const auto sameScissor = [](const std::optional<SDL_Rect>& a, const std::optional<SDL_Rect>& b)
    {
        return a.has_value() == b.has_value() &&
               (!a.has_value() || (a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h));
    };

    return first.Geometry == second.Geometry && first.Texture == second.Texture && first.X == second.X &&
           first.Y == second.Y && sameScissor(first.Scissor, second.Scissor);
}

bool MCUiSameDraws(const MCUiDraws& first, const MCUiDraws& second)
{
    return first.Width == second.Width && first.Height == second.Height &&
           std::ranges::equal(first.Draws, second.Draws, MCUiSameDraw);
}
