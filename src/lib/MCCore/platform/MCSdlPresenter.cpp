#include "stdafx.h"
#include "platform/MCSdlPresenter.h"
#include "platform/MCFrameLog.h"

namespace
{
    std::string SdlError(std::string_view what)
    {
        return std::format("{}: {}", what, SDL_GetError());
    }
}

std::expected<std::unique_ptr<MCSdlPresenter>, std::string> MCSdlPresenter::Create(std::shared_ptr<SDL_Window> window,
                                                                                   bool vsync,
                                                                                   const MCPresentation& presentation)
{
    auto presenter = std::make_unique<MCSdlPresenter>(Key{});
    presenter->_Window = std::move(window);
    presenter->_Presentation = presentation;
    presenter->_Renderer.reset(SDL_CreateRenderer(presenter->_Window.get(), nullptr));

    if (presenter->_Renderer == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateRenderer"));
    }

    SDL_SetRenderVSync(presenter->_Renderer.get(), vsync ? 1 : 0);
    presenter->_Palette.reset(SDL_CreatePalette(256));

    if (presenter->_Palette == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreatePalette"));
    }

    return presenter;
}

// The textures go first, then the palette, the renderer and (if the display let it go) the window: the members'
// order.
MCSdlPresenter::~MCSdlPresenter() = default;

std::string MCSdlPresenter::Name() const
{
    return std::format("software ({})", SDL_GetRendererName(_Renderer.get()));
}

void MCSdlPresenter::SetPresentation(const MCPresentation& presentation)
{
    const bool linearChanged = presentation.Linear != _Presentation.Linear;
    _Presentation = presentation;

    if (linearChanged)
    {
        // The RGBA step is made (or dropped) with the textures.
        _Width = 0;
        _Height = 0;
    }

    if (_View.w > 0 && _View.h > 0)
    {
        ApplyPresentation(_View);
    }
}

void MCSdlPresenter::SetVSync(bool on)
{
    SDL_SetRenderVSync(_Renderer.get(), on ? 1 : 0);
}

std::expected<void, std::string> MCSdlPresenter::EnsureTextures(int width, int height)
{
    if (width == _Width && height == _Height && _Texture != nullptr)
    {
        return {};
    }

    _Target.reset();
    _Texture.reset();

    _Width = 0;
    _Height = 0;
    _Texture.reset(
        SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_INDEX8, SDL_TEXTUREACCESS_STREAMING, width, height));

    if (_Texture == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateTexture(INDEX8)"));
    }

    if (!SDL_SetTexturePalette(_Texture.get(), _Palette.get()))
    {
        return std::unexpected(SdlError("SDL_SetTexturePalette"));
    }

    SDL_SetTextureBlendMode(_Texture.get(), SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(_Texture.get(), SDL_SCALEMODE_NEAREST);

    if (_Presentation.Linear)
    {
        // A palette lookup can't be filtered, so the screen is expanded to RGBA at its own size first.
        _Target.reset(
            SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height));

        if (_Target != nullptr)
        {
            SDL_SetTextureBlendMode(_Target.get(), SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(_Target.get(), SDL_SCALEMODE_LINEAR);
        }
    }

    _Width = width;
    _Height = height;
    return {};
}

void MCSdlPresenter::ApplyPresentation(const SDL_Rect& view)
{
    _View = view;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    if (_Presentation.Stretch)
    {
        mode = SDL_LOGICAL_PRESENTATION_STRETCH;
    }
    else if (_Presentation.IntegerScale)
    {
        mode = SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
    }

    SDL_SetRenderLogicalPresentation(_Renderer.get(), view.w, view.h, mode);
}

SDL_Rect MCSdlPresenter::ShownRect(const MCFrame& frame)
{
    return frame.ScaledOutput() ? SDL_Rect{0, 0, frame.OutputWidth, frame.OutputHeight} : frame.View;
}

std::expected<void, std::string> MCSdlPresenter::Upload(const MCFrame& frame)
{
    const int width = frame.ScaledOutput() ? frame.OutputWidth : frame.Width;
    const int height = frame.ScaledOutput() ? frame.OutputHeight : frame.Height;

    if (auto made = EnsureTextures(width, height); !made)
    {
        return made;
    }

    if (frame.ColorsChanged || frame.Fade != _Fade)
    {
        // The screen fade, on the colours: every pixel is a palette index here.
        std::array<SDL_Color, 256> colors;

        for (size_t i = 0; i < colors.size(); ++i)
        {
            const SDL_Color& color = frame.Colors[i];
            colors[i] = {static_cast<uint8_t>(std::max(color.r - frame.Fade, 0)),
                         static_cast<uint8_t>(std::max(color.g - frame.Fade, 0)),
                         static_cast<uint8_t>(std::max(color.b - frame.Fade, 0)), color.a};
        }

        SDL_SetPaletteColors(_Palette.get(), colors.data(), 0, 256);
        _Fade = frame.Fade;
    }

    std::span<const uint8_t> pixels = frame.Pixels;

    if (frame.ScaledOutput())
    {
        // The view at the window's pixels: the UI taken nearest, the underlays at their own resolution.
        MCComposeScaled(frame, _Composed);
        pixels = _Composed;
    }
    else if (!frame.Underlays.empty())
    {
        _Composed.assign(frame.Pixels.begin(),
                         frame.Pixels.begin() + static_cast<ptrdiff_t>(frame.Width) * frame.Height);
        MCRenderer::ComposeUnderlays(frame.Screen.get(), _Composed, MCRect{0, 0, frame.Width - 1, frame.Height - 1});
        pixels = _Composed;
    }

    if (!SDL_UpdateTexture(_Texture.get(), nullptr, pixels.data(), width))
    {
        return std::unexpected(SdlError("SDL_UpdateTexture"));
    }

    if (const SDL_Rect shown = ShownRect(frame);
        shown.x != _View.x || shown.y != _View.y || shown.w != _View.w || shown.h != _View.h)
    {
        ApplyPresentation(shown);
    }

    return {};
}

std::expected<void, std::string> MCSdlPresenter::Present(const MCFrame& frame)
{
    if (auto uploaded = Upload(frame); !uploaded)
    {
        return uploaded;
    }

    SDL_SetRenderDrawColor(_Renderer.get(), 0, 0, 0, 255);
    SDL_RenderClear(_Renderer.get());
    const SDL_Rect shown = ShownRect(frame);
    const SDL_FRect view{static_cast<float>(shown.x), static_cast<float>(shown.y), static_cast<float>(shown.w),
                         static_cast<float>(shown.h)};

    if (_Target != nullptr)
    {
        // Converted at the screen's size, then filtered up.
        SDL_SetRenderLogicalPresentation(_Renderer.get(), 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderTarget(_Renderer.get(), _Target.get());
        SDL_RenderTexture(_Renderer.get(), _Texture.get(), nullptr, nullptr);
        SDL_SetRenderTarget(_Renderer.get(), nullptr);
        ApplyPresentation(shown);
        SDL_RenderClear(_Renderer.get());
        SDL_RenderTexture(_Renderer.get(), _Target.get(), &view, nullptr);
    }
    else
    {
        SDL_RenderTexture(_Renderer.get(), _Texture.get(), &view, nullptr);
    }

    if (!SDL_RenderPresent(_Renderer.get()))
    {
        return std::unexpected(SdlError("SDL_RenderPresent"));
    }

    // The frame handed to the screen: the frame log's time between these is the screen's (a benchmark's hitches).
    MCFrameLog::Presented();
    return {};
}

std::expected<std::vector<SDL_Color>, std::string> MCSdlPresenter::ReadFrame(const MCFrame& frame)
{
    if (auto uploaded = Upload(frame); !uploaded)
    {
        return std::unexpected(uploaded.error());
    }

    // The upload's size: the window pixels of a scaled frame, else the screen's.
    const int width = frame.ScaledOutput() ? frame.OutputWidth : frame.Width;
    const int height = frame.ScaledOutput() ? frame.OutputHeight : frame.Height;
    SDL_Texture* target =
        SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);

    if (target == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateTexture(read frame)"));
    }

    SDL_SetRenderLogicalPresentation(_Renderer.get(), 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderTarget(_Renderer.get(), target);
    SDL_RenderTexture(_Renderer.get(), _Texture.get(), nullptr, nullptr);
    SDL_Surface* read = SDL_RenderReadPixels(_Renderer.get(), nullptr);
    SDL_SetRenderTarget(_Renderer.get(), nullptr);
    SDL_DestroyTexture(target);
    ApplyPresentation(ShownRect(frame));

    if (read == nullptr)
    {
        return std::unexpected(SdlError("SDL_RenderReadPixels"));
    }

    SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);

    if (rgba == nullptr)
    {
        return std::unexpected(SdlError("SDL_ConvertSurface"));
    }

    std::vector<SDL_Color> pixels(static_cast<size_t>(width) * height);

    for (int y = 0; y < height && y < rgba->h; ++y)
    {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * width,
                    static_cast<const uint8_t*>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch,
                    static_cast<size_t>(std::min(width, rgba->w)) * sizeof(SDL_Color));
    }

    SDL_DestroySurface(rgba);
    return pixels;
}

MCViewport MCSdlPresenter::Viewport(int viewWidth, int viewHeight) const
{
    SDL_FRect rect{};

    if (viewWidth == _View.w && viewHeight == _View.h && SDL_GetRenderLogicalPresentationRect(_Renderer.get(), &rect) &&
        rect.w > 0.0f && rect.h > 0.0f)
    {
        return MCViewport{rect.x, rect.y, rect.w, rect.h};
    }

    int width = 0;
    int height = 0;
    SDL_GetRenderOutputSize(_Renderer.get(), &width, &height);
    return MCPresentViewport(width, height, viewWidth, viewHeight, _Presentation);
}
