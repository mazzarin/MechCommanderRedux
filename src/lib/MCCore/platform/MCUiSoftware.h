#pragma once

#include "platform/MCSdlHandle.h"
#include "platform/MCUiDraws.h"

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): the remade menus' draws (platform/MCUiDraws.h) drawn by the CPU, for
/// the software renderer (MCSdlPresenter): onto a picture of the window's size, transparent where nothing is drawn
/// (premultiplied RGBA8), with SDL's software renderer, which every machine runs alike. Rectangles (nearly every draw:
/// boxes, borders, pictures, letters) are drawn as SDL_RenderTexture copies of whole texels onto whole pixels; other
/// triangles with SDL_RenderGeometry. Colours are premultiplied and blended so
/// (<c>SDL_BLENDMODE_BLEND_PREMULTIPLIED</c>), a scissor is SDL's clip rectangle.
/// </summary>
class MCUiSoftware
{
public:
    MCUiSoftware();
    ~MCUiSoftware();
    MCUiSoftware(const MCUiSoftware&) = delete;
    MCUiSoftware& operator=(const MCUiSoftware&) = delete;

    /// <summary>Draws <paramref name="draws"/> onto a transparent picture of their window's size.</summary>
    std::expected<void, std::string> Draw(const MCUiDraws& draws);

    /// <summary>The picture drawn last (RGBA32, premultiplied; none before the first).</summary>
    const SDL_Surface* Picture() const { return _Surface.get(); }

    /// <summary>The textures kept (tests: let go of with the pictures they copy).</summary>
    size_t TexturesKept() const { return _Textures.size(); }

private:
    /// <summary>The renderer's copy of <paramref name="texture"/>, made the first time it's drawn.</summary>
    std::expected<std::reference_wrapper<SDL_Texture>, std::string> TextureOf(const MCUiTexture& texture);
    /// <summary>Lets go of the copies of pictures nothing holds any more.</summary>
    void Prune();

    struct MCKept
    {
        std::weak_ptr<const MCUiTexture> Picture;
        MCSdlHandle<SDL_Texture> Texture;
    };

    MCSdlHandle<SDL_Surface> _Surface;
    MCSdlHandle<SDL_Renderer> _Renderer;
    /// <summary>A white texel: an untextured rectangle is it, scaled and coloured.</summary>
    MCSdlHandle<SDL_Texture> _White;
    /// <summary>The pictures' copies, by their number; each let go of once its picture is gone.</summary>
    std::unordered_map<uint64_t, MCKept> _Textures;
};
