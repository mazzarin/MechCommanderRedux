#include "stdafx.h"
#include "ui/MCRmlRecorder.h"

#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Vertex.h>

namespace
{
    /// <summary>The bytes of an RGBA8 pixel.</summary>
    constexpr size_t PixelBytes = 4;

    /// <summary>The pictures' numbers, across every recorder of the run.</summary>
    std::atomic<uint64_t> NextTextureId{1};
}

MCRmlRecorder::MCRmlRecorder(MCLoader loader) : _Loader(std::move(loader))
{
}

MCRmlRecorder::~MCRmlRecorder() = default;

void MCRmlRecorder::Begin(int32_t width, int32_t height)
{
    _Frame = std::make_shared<MCUiDraws>();
    _Frame->Width = width;
    _Frame->Height = height;
    _ScissorOn = false;
}

std::shared_ptr<const MCUiDraws> MCRmlRecorder::Finish()
{
    return std::exchange(_Frame, nullptr);
}

std::shared_ptr<const MCUiTexture> MCRmlRecorder::MakeTexture(int32_t width, int32_t height,
                                                              std::vector<uint8_t> pixels, bool smooth)
{
    auto texture = std::make_shared<MCUiTexture>();
    texture->Id = NextTextureId.fetch_add(1, std::memory_order_relaxed);
    texture->Width = width;
    texture->Height = height;
    texture->Pixels = std::move(pixels);
    texture->Smooth = smooth;
    return texture;
}

Rml::CompiledGeometryHandle MCRmlRecorder::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                           Rml::Span<const int> indices)
{
    auto geometry = std::make_shared<MCUiGeometry>();
    geometry->Vertices.reserve(vertices.size());

    for (const Rml::Vertex& vertex : vertices)
    {
        geometry->Vertices.push_back(MCUiVertex{
            .X = vertex.position.x,
            .Y = vertex.position.y,
            .Color = {vertex.colour.red, vertex.colour.green, vertex.colour.blue, vertex.colour.alpha},
            .U = vertex.tex_coord.x,
            .V = vertex.tex_coord.y,
        });
    }

    geometry->Indices.assign(indices.begin(), indices.end());
    const Rml::CompiledGeometryHandle handle = _NextHandle++;
    _Geometry.emplace(handle, std::move(geometry));
    return handle;
}

void MCRmlRecorder::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                   Rml::TextureHandle texture)
{
    const auto found = _Geometry.find(geometry);

    if (_Frame == nullptr || found == _Geometry.end())
    {
        return;
    }

    MCUiDraw draw{.Geometry = found->second, .X = translation.x, .Y = translation.y};

    if (const auto live = _LiveOf.find(texture); live != _LiveOf.end())
    {
        const auto shown = _Live.find(live->second);

        // A live picture not given one yet draws nothing.
        if (shown == _Live.end() || shown->second == nullptr)
        {
            return;
        }

        draw.Texture = shown->second;
    }
    else if (texture != 0)
    {
        const auto picture = _Textures.find(texture);

        // A picture that couldn't be read (LoadTexture logged why) draws nothing, as RmlUi's own back ends do.
        if (picture == _Textures.end())
        {
            return;
        }

        draw.Texture = picture->second;
    }

    if (_ScissorOn)
    {
        draw.Scissor = _Scissor;
    }

    _Frame->Draws.push_back(std::move(draw));
}

void MCRmlRecorder::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    _Geometry.erase(geometry);
}

Rml::TextureHandle MCRmlRecorder::Keep(std::shared_ptr<const MCUiTexture> texture)
{
    const Rml::TextureHandle handle = _NextHandle++;
    _Textures.emplace(handle, std::move(texture));
    return handle;
}

Rml::TextureHandle MCRmlRecorder::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
{
    if (source.starts_with(LiveScheme))
    {
        // Its size as it shows now, else a pixel's (its box is sized by the style sheet).
        const auto shown = _Live.find(source);
        const bool has = shown != _Live.end() && shown->second != nullptr;
        textureDimensions = has ? Rml::Vector2i(shown->second->Width, shown->second->Height) : Rml::Vector2i(1, 1);
        const Rml::TextureHandle handle = _NextHandle++;
        _LiveOf.emplace(handle, source);
        return handle;
    }

    std::expected<MCLoaded, std::string> loaded = _Loader ? _Loader(source) : std::unexpected("no picture loader");

    if (!loaded)
    {
        Rml::Log::Message(Rml::Log::LT_WARNING, "Can't load the picture '%s': %s", source.c_str(),
                          loaded.error().c_str());
        return 0;
    }

    textureDimensions = Rml::Vector2i(loaded->Width, loaded->Height);
    return Keep(std::move(loaded->Texture));
}

Rml::TextureHandle MCRmlRecorder::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions)
{
    const size_t bytes = static_cast<size_t>(std::max(sourceDimensions.x, 0)) *
                         static_cast<size_t>(std::max(sourceDimensions.y, 0)) * PixelBytes;

    if (bytes == 0 || source.size() < bytes)
    {
        Rml::Log::Message(Rml::Log::LT_WARNING, "A %dx%d picture made of %zu bytes", sourceDimensions.x,
                          sourceDimensions.y, source.size());
        return 0;
    }

    // RmlUi's pictures are made premultiplied (a font's letters, here): drawn 1:1, so their nearest pixel.
    return Keep(MakeTexture(sourceDimensions.x, sourceDimensions.y,
                            std::vector<uint8_t>(source.begin(), source.begin() + bytes), false));
}

void MCRmlRecorder::ReleaseTexture(Rml::TextureHandle texture)
{
    _Textures.erase(texture);
    _LiveOf.erase(texture);
}

void MCRmlRecorder::ShowLive(const std::string& source, std::shared_ptr<const MCUiTexture> picture)
{
    if (picture == nullptr)
    {
        _Live.erase(source);
        return;
    }

    _Live.insert_or_assign(source, std::move(picture));
}

void MCRmlRecorder::EnableScissorRegion(bool enable)
{
    _ScissorOn = enable;
}

void MCRmlRecorder::SetScissorRegion(Rml::Rectanglei region)
{
    _Scissor = SDL_Rect{region.Left(), region.Top(), region.Width(), region.Height()};
}
