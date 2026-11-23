#include "stdafx.h"
#include "platform/MCUiSoftware.h"

namespace
{
    constexpr size_t PixelBytes = 4;
    constexpr float ByteMax = 255.0f;

    std::string SdlError(std::string_view what)
    {
        return std::format("the menus' software drawing, {}: {}", what, SDL_GetError());
    }

    SDL_FRect Float(const SDL_Rect& rect)
    {
        return SDL_FRect{static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w),
                         static_cast<float>(rect.h)};
    }
}

MCUiSoftware::MCUiSoftware() = default;

// The textures go before the renderer, the renderer before its surface: the members' order, backwards.
MCUiSoftware::~MCUiSoftware()
{
    _Textures.clear();
    _White.reset();
    _Renderer.reset();
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

void MCUiSoftware::Prune()
{
    std::erase_if(_Textures, [](const auto& entry) { return entry.second.Picture.expired(); });
}

std::expected<void, std::string> MCUiSoftware::Draw(const MCUiDraws& draws)
{
    const int width = std::max(draws.Width, 1);
    const int height = std::max(draws.Height, 1);

    if (_Surface == nullptr || _Surface->w != width || _Surface->h != height)
    {
        _Textures.clear();
        _White.reset();
        _Renderer.reset();
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
            return std::unexpected(SdlError("its renderer"));
        }

        SDL_SetTextureBlendMode(_White.get(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(_White.get(), SDL_SCALEMODE_NEAREST);
    }

    SDL_Renderer* renderer = _Renderer.get();
    SDL_SetRenderClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    std::vector<SDL_Vertex> vertices;

    for (const MCUiDraw& draw : draws.Draws)
    {
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

        SDL_SetRenderClipRect(renderer, draw.Scissor.has_value() ? &*draw.Scissor : nullptr);

        for (const MCUiPiece& piece : MCUiPieces(draw))
        {
            // A rectangle, copied (a textured one when it shows whole texels).
            if (piece.Quad.has_value() && (draw.Texture == nullptr || piece.Quad->Source.has_value()))
            {
                const MCUiQuad& quad = *piece.Quad;
                SDL_SetTextureColorMod(texture, quad.Color[0], quad.Color[1], quad.Color[2]);
                SDL_SetTextureAlphaMod(texture, quad.Color[3]);
                const SDL_FRect target = Float(quad.Target);
                const std::optional<SDL_FRect> source =
                    quad.Source.has_value() ? std::optional(Float(*quad.Source)) : std::nullopt;
                SDL_RenderTexture(renderer, texture, source.has_value() ? &*source : nullptr, &target);
                continue;
            }

            // Triangles as they are, their corners on whole pixels, as the GPU's. SDL's software triangles blend only
            // straight alpha (SDL_triangle.c copies a premultiplied one over what's there), so their corners' colours
            // are given straight and blended so, which premultiplies them as it blends: the same but for the colours
            // between corners (a gradient's), mixed straight. A picture's own texels stay premultiplied: its half
            // transparent ones drawn so come out darker (pictures are rectangles, copied above, all but never this).
            SDL_SetTextureColorMod(texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(texture, 255);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
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

            SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        }
    }

    if (!SDL_FlushRenderer(renderer))
    {
        return std::unexpected(SdlError("drawing"));
    }

    // The copies of pictures no frame or recorder holds any more are let go of.
    Prune();
    return {};
}
