#include "stdafx.h"
#include "platform/MCSdlPresenter.h"
#include "platform/MCFrameLog.h"

namespace
{
    std::string SdlError(std::string_view what)
    {
        return std::format("{}: {}", what, SDL_GetError());
    }

    // Port-only: the menus' picture, SDL_PIXELFORMAT_RGBA32 (bytes R, G, B, A on every machine).
    constexpr int PictureBytes = 4;
    constexpr int AlphaByte = 3;
    constexpr uint32_t WholeAlpha = 255;
    constexpr int FactorBits = 16;
    constexpr uint32_t FactorHalf = 1u << (FactorBits - 1);

    /// <summary>Port-only: for each alpha, colour times this, shifted down FactorBits, undoes premultiplying by it
    /// (colour times 255 / alpha, rounded; at most 255 * 255 * 2^16 + FactorHalf, inside 32 bits).</summary>
    constexpr std::array<uint32_t, WholeAlpha + 1> Unpremultipliers = []
    {
        std::array<uint32_t, WholeAlpha + 1> factors{};

        for (uint32_t alpha = 1; alpha <= WholeAlpha; ++alpha)
        {
            factors[alpha] = ((WholeAlpha << FactorBits) + alpha / 2) / alpha;
        }

        return factors;
    }();

    const uint8_t* PictureRow(const SDL_Surface& picture, int y)
    {
        return static_cast<const uint8_t*>(picture.pixels) + static_cast<ptrdiff_t>(y) * picture.pitch;
    }

    /// <summary>Port-only: the box of <paramref name="picture"/> holding every pixel with any coverage (empty when
    /// none has any).</summary>
    SDL_Rect CoveredBox(const SDL_Surface& picture)
    {
        int left = picture.w;
        int right = -1;
        int top = -1;
        int bottom = -1;

        for (int y = 0; y < picture.h; ++y)
        {
            const uint8_t* row = PictureRow(picture, y);
            int first = 0;

            while (first < picture.w && row[first * PictureBytes + AlphaByte] == 0)
            {
                ++first;
            }

            if (first == picture.w)
            {
                continue;
            }

            int last = picture.w - 1;

            while (row[last * PictureBytes + AlphaByte] == 0)
            {
                --last;
            }

            left = std::min(left, first);
            right = std::max(right, last);
            top = top < 0 ? y : top;
            bottom = y;
        }

        return right < 0 ? SDL_Rect{} : SDL_Rect{left, top, right - left + 1, bottom - top + 1};
    }

    /// <summary>Port-only: whether every pixel of <paramref name="picture"/> in <paramref name="box"/> is whole.</summary>
    bool WholeIn(const SDL_Surface& picture, const SDL_Rect& box)
    {
        for (int y = box.y; y < box.y + box.h; ++y)
        {
            const uint8_t* row = PictureRow(picture, y);

            for (int x = box.x; x < box.x + box.w; ++x)
            {
                if (row[x * PictureBytes + AlphaByte] != WholeAlpha)
                {
                    return false;
                }
            }
        }

        return true;
    }

    /// <summary>Port-only: <paramref name="count"/> premultiplied RGBA pixels written to <paramref name="to"/> in
    /// straight alpha.</summary>
    void Straighten(const uint8_t* from, uint8_t* to, int count)
    {
        for (int i = 0; i < count; ++i, from += PictureBytes, to += PictureBytes)
        {
            const uint32_t factor = Unpremultipliers[from[AlphaByte]];

            for (int channel = 0; channel < AlphaByte; ++channel)
            {
                to[channel] = static_cast<uint8_t>(
                    std::min<uint32_t>((from[channel] * factor + FactorHalf) >> FactorBits, WholeAlpha));
            }

            to[AlphaByte] = from[AlphaByte];
        }
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

    if (frame.ColorsChanged || frame.Fade != _Fade || frame.FadeScales != _FadeScales)
    {
        // The screen fade, on the colours: every pixel is a palette index here.
        std::array<SDL_Color, 256> colors;

        for (size_t i = 0; i < colors.size(); ++i)
        {
            const SDL_Color& color = frame.Colors[i];
            colors[i] = MCFadedColor(color, frame.Fade, frame.FadeScales);
        }

        SDL_SetPaletteColors(_Palette.get(), colors.data(), 0, 256);
        _Fade = frame.Fade;
        _FadeScales = frame.FadeScales;
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
    // The frame log's parts: the frame, the menus over it and the show (with the log open, the renderer's queue is run
    // at each part's end, so each part's draws count in it, not in the show).
    std::optional<MCFrameLog::Scope> part(std::in_place, "present.frame");

    if (auto uploaded = Upload(frame); !uploaded)
    {
        return uploaded;
    }

    // Port-only: the window around the view in the frame's surround (the menus' stone colour), faded with the picture.
    const SDL_Color surround = MCFadedSurround(frame);
    SDL_SetRenderDrawColor(_Renderer.get(), surround.r, surround.g, surround.b, surround.a);
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

    RunQueueForLog();

    if (frame.Overlay != nullptr && !frame.Overlay->Draws.empty())
    {
        part.emplace("present.menus");

        if (auto drawn = DrawOverlay(frame.Overlay, MCUiShown(frame.Fade)); !drawn)
        {
            return drawn;
        }

        RunQueueForLog();
    }

    part.emplace("present.show");

    if (!SDL_RenderPresent(_Renderer.get()))
    {
        return std::unexpected(SdlError("SDL_RenderPresent"));
    }

    part.reset();

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

std::expected<void, std::string> MCSdlPresenter::DrawOverlay(const std::shared_ptr<const MCUiDraws>& overlay,
                                                             float shown)
{
    // The menus' picture is drawn by the CPU and uploaded only when it changes; else the one kept is shown again.
    if (overlay != _OverlayDrawn || _Overlay == nullptr)
    {
        MCFrameLog::Scope part("present.menus.draw");

        if (auto drawn = _Ui.Draw(*overlay); !drawn)
        {
            return drawn;
        }

        if (auto uploaded = UploadOverlay(*_Ui.Picture()); !uploaded)
        {
            return uploaded;
        }

        _OverlayDrawn = overlay;
        ++_OverlayUploads;
    }

    if (_OverlayBox.w <= 0)
    {
        return {};
    }

    // Its covered box over the window, one picture pixel a window pixel, its colours faded with the frame (the
    // colour scaled, the coverage kept).
    constexpr float Full = 255.0f;
    const auto kept = static_cast<uint8_t>(std::lround(std::clamp(shown, 0.0f, 1.0f) * Full));
    SDL_SetTextureColorMod(_Overlay.get(), kept, kept, kept);
    SDL_SetRenderLogicalPresentation(_Renderer.get(), 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    const SDL_FRect box{static_cast<float>(_OverlayBox.x), static_cast<float>(_OverlayBox.y),
                        static_cast<float>(_OverlayBox.w), static_cast<float>(_OverlayBox.h)};
    SDL_RenderTexture(_Renderer.get(), _Overlay.get(), &box, &box);
    ApplyPresentation(_View);
    return {};
}

void MCSdlPresenter::RunQueueForLog()
{
    if (MCFrameLog::Enabled() && !SDL_FlushRenderer(_Renderer.get()))
    {
        SDL_Log("MCSdlPresenter: the frame log's flush of the renderer's queue failed: %s", SDL_GetError());
    }
}

std::expected<void, std::string> MCSdlPresenter::UploadOverlay(const SDL_Surface& picture)
{
    if (_Overlay == nullptr || _Overlay->w != picture.w || _Overlay->h != picture.h)
    {
        _Overlay.reset(SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                         picture.w, picture.h));

        if (_Overlay == nullptr)
        {
            return std::unexpected(SdlError(std::format("SDL_CreateTexture(the menus, {}x{})", picture.w, picture.h)));
        }

        SDL_SetTextureScaleMode(_Overlay.get(), SDL_SCALEMODE_NEAREST);
    }

    _OverlayBox = CoveredBox(picture);

    if (_OverlayBox.w <= 0)
    {
        return {};
    }

    // SDL's software blitter has no fast way for a premultiplied blend: a scalar loop a pixel, about 38 ms a
    // 1920x1080 frame on the HP laptop's i5-4310U, paid every frame a menu shows. A copy, or a blend in straight
    // alpha (its SIMD way), costs a fraction of that, and a whole box's copy is the same picture on every machine.
    const bool whole = WholeIn(picture, _OverlayBox);
    void* pixels = nullptr;
    int pitch = 0;

    if (!SDL_LockTexture(_Overlay.get(), &_OverlayBox, &pixels, &pitch))
    {
        return std::unexpected(SdlError("SDL_LockTexture(the menus)"));
    }

    for (int y = 0; y < _OverlayBox.h; ++y)
    {
        const uint8_t* from = PictureRow(picture, _OverlayBox.y + y) + _OverlayBox.x * PictureBytes;
        auto* to = static_cast<uint8_t*>(pixels) + static_cast<ptrdiff_t>(y) * pitch;

        if (whole)
        {
            std::memcpy(to, from, static_cast<size_t>(_OverlayBox.w) * PictureBytes);
        }
        else
        {
            Straighten(from, to, _OverlayBox.w);
        }
    }

    SDL_UnlockTexture(_Overlay.get());
    SDL_SetTextureBlendMode(_Overlay.get(), whole ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    return {};
}

std::expected<std::vector<SDL_Color>, std::string> MCSdlPresenter::ReadOverlay(const MCUiDraws& overlay)
{
    if (auto drawn = _Ui.Draw(overlay); !drawn)
    {
        return std::unexpected(drawn.error());
    }

    const SDL_Surface& picture = *_Ui.Picture();
    std::vector<SDL_Color> pixels(static_cast<size_t>(picture.w) * static_cast<size_t>(picture.h));

    for (int y = 0; y < picture.h; ++y)
    {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(picture.w),
                    static_cast<const uint8_t*>(picture.pixels) + static_cast<ptrdiff_t>(y) * picture.pitch,
                    static_cast<size_t>(picture.w) * sizeof(SDL_Color));
    }

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
