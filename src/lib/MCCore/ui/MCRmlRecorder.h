#pragma once

#include "platform/MCUiDraws.h"

#include <RmlUi/Core/RenderInterface.h>

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): RmlUi's render interface, recording a frame's draws
/// (platform/MCUiDraws.h) for the presenter to draw over the window: the GPU renderer's (MCVulkanPresenter) and the
/// software renderer's (MCSdlPresenter) each draw what is recorded here, the GPU's on a thread of its own after the game
/// has gone on. Geometry and pictures are kept as RmlUi compiles and loads them and are shared with the frames that draw
/// them, so letting go of one here never pulls it from under a frame. Only the basic draws: no clip masks, transforms,
/// layers, filters or shaders, which the menus' style sheets never ask for (RmlUi draws nothing for them).
/// </summary>
class MCRmlRecorder final : public Rml::RenderInterface
{
public:
    /// <summary>A picture a source names, and the size RmlUi is told it has (its parts are named in that size).</summary>
    struct MCLoaded
    {
        std::shared_ptr<const MCUiTexture> Texture;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>What <see cref="LoadTexture"/> reads a picture's source with.</summary>
    using MCLoader = std::function<std::expected<MCLoaded, std::string>(std::string_view source)>;

    explicit MCRmlRecorder(MCLoader loader);
    ~MCRmlRecorder() override;

    /// <summary>Starts recording a frame over a window of <paramref name="width"/> x <paramref name="height"/> pixels.</summary>
    void Begin(int32_t width, int32_t height);

    /// <summary>The frame recorded since <see cref="Begin"/>, handed over; recording stops.</summary>
    std::shared_ptr<const MCUiDraws> Finish();

    /// <summary>A picture of <paramref name="pixels"/> (RGBA8, premultiplied), numbered anew.</summary>
    static std::shared_ptr<const MCUiTexture> MakeTexture(int32_t width, int32_t height, std::vector<uint8_t> pixels,
                                                          bool smooth);

    /// <summary>The geometry and pictures kept (tests: none once RmlUi has let go of them).</summary>
    size_t GeometryKept() const { return _Geometry.size(); }
    size_t TexturesKept() const { return _Textures.size(); }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    /// <summary>Keeps <paramref name="texture"/> under a new handle.</summary>
    Rml::TextureHandle Keep(std::shared_ptr<const MCUiTexture> texture);

    MCLoader _Loader;
    std::unordered_map<Rml::CompiledGeometryHandle, std::shared_ptr<const MCUiGeometry>> _Geometry;
    std::unordered_map<Rml::TextureHandle, std::shared_ptr<const MCUiTexture>> _Textures;
    /// <summary>The next handle given out (0 is none, to RmlUi).</summary>
    uintptr_t _NextHandle = 1;
    /// <summary>The frame being recorded; none between frames.</summary>
    std::shared_ptr<MCUiDraws> _Frame;
    bool _ScissorOn = false;
    SDL_Rect _Scissor{};
};
