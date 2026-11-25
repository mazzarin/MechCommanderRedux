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

    constexpr size_t PixelBytes = 4;

    /// <summary>The pictures' numbers, across the run (<see cref="MCUiMakeTexture"/>).</summary>
    std::atomic<uint64_t> NextTextureId{1};

    /// <summary>A premultiplied colour that keeps what it multiplies (a picture's own colours).</summary>
    constexpr std::array<uint8_t, 4> White{255, 255, 255, 255};

    /// <summary>
    /// <paramref name="place"/> texels into a texture <paramref name="size"/> wide: a whole texel when it lies that near
    /// one.
    /// </summary>
    float TexelsAt(float place, int32_t size)
    {
        const double texels = static_cast<double>(place) * size;
        const double whole = std::floor(texels + 0.5);
        return static_cast<float>(std::abs(texels - whole) <= WholeTexel ? whole : texels);
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

        const float texelLeft = TexelsAt(u[0], draw.Texture->Width);
        const float texelRight = TexelsAt(u[1], draw.Texture->Width);
        const float texelTop = TexelsAt(v[0], draw.Texture->Height);
        const float texelBottom = TexelsAt(v[1], draw.Texture->Height);

        // Flipped, or past the texture (a picture repeated): triangles, as SDL samples them.
        if (!(texelLeft >= 0.0f && texelRight > texelLeft && texelRight <= static_cast<float>(draw.Texture->Width) &&
              texelTop >= 0.0f && texelBottom > texelTop && texelBottom <= static_cast<float>(draw.Texture->Height)))
        {
            return std::nullopt;
        }

        quad.Texels = SDL_FRect{texelLeft, texelTop, texelRight - texelLeft, texelBottom - texelTop};
        return quad;
    }
}

std::shared_ptr<const MCUiTexture> MCUiMakeTexture(int32_t width, int32_t height, std::vector<uint8_t> pixels,
                                                   bool smooth)
{
    auto texture = std::make_shared<MCUiTexture>();
    texture->Id = NextTextureId.fetch_add(1, std::memory_order_relaxed);
    texture->Width = width;
    texture->Height = height;
    texture->Pixels = std::move(pixels);
    texture->Smooth = smooth;
    return texture;
}

std::shared_ptr<const MCUiGeometry> MCUiPictureRectangle(int32_t width, int32_t height)
{
    const auto right = static_cast<float>(width);
    const auto bottom = static_cast<float>(height);
    auto geometry = std::make_shared<MCUiGeometry>();
    geometry->Vertices = {
        MCUiVertex{.X = 0.0f, .Y = 0.0f, .Color = White, .U = 0.0f, .V = 0.0f},
        MCUiVertex{.X = right, .Y = 0.0f, .Color = White, .U = 1.0f, .V = 0.0f},
        MCUiVertex{.X = right, .Y = bottom, .Color = White, .U = 1.0f, .V = 1.0f},
        MCUiVertex{.X = 0.0f, .Y = bottom, .Color = White, .U = 0.0f, .V = 1.0f},
    };

    // Two triangles sharing the diagonal from the top left to the bottom right (MCUiPieces takes them as one rectangle).
    geometry->Indices = {0, 1, 2, 0, 2, 3};
    return geometry;
}

std::expected<MCSdlHandle<SDL_Surface>, std::string> MCUiScaled(const MCUiTexture& picture, const SDL_Rect& part,
                                                                int width, int height)
{
    const auto failed = [&](std::string_view what)
    {
        return std::unexpected(std::format("picture {}'s {},{} {}x{} scaled to {}x{}, {}: {}", picture.Id, part.x,
                                           part.y, part.w, part.h, width, height, what, SDL_GetError()));
    };

    const size_t pitch = static_cast<size_t>(picture.Width) * PixelBytes;

    if (part.x < 0 || part.y < 0 || part.w <= 0 || part.h <= 0 || part.x + part.w > picture.Width ||
        part.y + part.h > picture.Height || picture.Pixels.size() < pitch * static_cast<size_t>(picture.Height))
    {
        return failed("not inside its pixels");
    }

    // The part scaled, in place (SDL only reads it).
    MCSdlHandle<SDL_Surface> from(SDL_CreateSurfaceFrom(part.w, part.h, SDL_PIXELFORMAT_RGBA32,
                                                        const_cast<uint8_t*>(picture.Pixels.data()) +
                                                            static_cast<size_t>(part.y) * pitch +
                                                            static_cast<size_t>(part.x) * PixelBytes,
                                                        static_cast<int>(pitch)));

    if (from == nullptr)
    {
        return failed("its part");
    }

    MCSdlHandle<SDL_Surface> scaled(
        SDL_ScaleSurface(from.get(), width, height, picture.Smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));

    if (scaled == nullptr)
    {
        return failed("scaling");
    }

    return scaled;
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
