#include "stdafx.h"
#include "platform/MCDisplay.h"
#include "main/MCGameContext.h"
#include "platform/MCFileSystem.h"
#include "platform/MCRenderer.h"
#include "platform/MCSdlPresenter.h"
#include "platform/MCVulkanPresenter.h"
#include "sprite/MCHdArt.h"

namespace
{
    std::string SdlError(std::string_view what)
    {
        return std::format("{}: {}", what, SDL_GetError());
    }

    /// <summary>The palette through a gamma and brightness, as the GPU gets it.</summary>
    void ApplyGamma(const std::array<MCVfxRgb, 256>& palette, float gamma, float brightness,
                    std::span<SDL_Color, 256> out)
    {
        std::array<uint8_t, 256> curve{};

        for (int i = 0; i < 256; ++i)
        {
            float value = static_cast<float>(i) / 255.0f;

            if (gamma != 1.0f)
            {
                value = std::pow(value, 1.0f / gamma);
            }

            curve[static_cast<size_t>(i)] =
                static_cast<uint8_t>(std::clamp(value * brightness * 255.0f + 0.5f, 0.0f, 255.0f));
        }

        for (size_t i = 0; i < 256; ++i)
        {
            out[i] = {curve[palette[i].R], curve[palette[i].G], curve[palette[i].B], 255};
        }
    }

    /// <summary>The colours shown: <paramref name="colors"/> with <paramref name="cycle"/> applied.</summary>
    void ApplyCycle(std::span<const SDL_Color, 256> colors, const MCColorCycle& cycle, std::span<SDL_Color, 256> out)
    {
        for (size_t i = 0; i < 256; ++i)
        {
            out[i] = colors[cycle.Remap(static_cast<uint8_t>(i))];
        }
    }

    /// <summary>The presenter for <paramref name="kind"/>; Vulkan falls back to software when it can't start.</summary>
    std::expected<std::unique_ptr<MCPresenter>, std::string> CreatePresenter(std::shared_ptr<SDL_Window> window,
                                                                             MCRendererKind kind, bool vsync,
                                                                             const MCPresentation& presentation)
    {
        if (kind == MCRendererKind::Vulkan)
        {
            auto vulkan = MCVulkanPresenter::Create(window, vsync, presentation);

            if (vulkan)
            {
                return std::unique_ptr<MCPresenter>(std::move(*vulkan));
            }

            SDL_Log("MCDisplay: Vulkan can't start (%s); using the software renderer", vulkan.error().c_str());
        }

        auto software = MCSdlPresenter::Create(window, vsync, presentation);

        if (!software)
        {
            return std::unexpected(software.error());
        }

        return std::unique_ptr<MCPresenter>(std::move(*software));
    }
}

std::expected<std::unique_ptr<MCDisplay>, std::string> MCDisplay::Create(const MCDisplayOptions& options)
{
    if (options.Width <= 0 || options.Height <= 0)
    {
        return std::unexpected(std::format("bad screen size {}x{}", options.Width, options.Height));
    }

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0 && !SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return std::unexpected(SdlError("SDL_InitSubSystem(video)"));
    }

    auto display = std::make_unique<MCDisplay>(Key{});
    display->_Width = options.Width;
    display->_Height = options.Height;
    display->_View = SDL_Rect{0, 0, options.Width, options.Height};

    // The windowed size: the logical size times the largest whole scale that fits the usable desktop.
    int scale = options.WindowScale;

    if (scale <= 0)
    {
        scale = 1;
        SDL_Rect usable{};

        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        {
            while ((scale + 1) * options.Width <= usable.w * 9 / 10 &&
                   (scale + 1) * options.Height <= usable.h * 9 / 10)
            {
                ++scale;
            }
        }
    }

    display->_WindowScale = scale;

    SDL_WindowFlags flags = options.HighPixelDensity ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0;

    if (options.Resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    if (options.Fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    if (options.Hidden)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }

    SDL_Window* window = SDL_CreateWindow(options.Title.c_str(), options.Width * scale, options.Height * scale, flags);

    if (window == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateWindow"));
    }

    display->_Window = std::shared_ptr<SDL_Window>(window, &SDL_DestroyWindow);

    MCPresentation presentation;
    presentation.IntegerScale = options.IntegerScale;
    presentation.Stretch = options.Stretch;
    presentation.Linear = options.LinearFilter;
    auto presenter = CreatePresenter(display->_Window, options.Renderer, options.VSync, presentation);

    if (!presenter)
    {
        return std::unexpected(presenter.error());
    }

    display->_Presenter = std::move(*presenter);
    display->_Presenter->FrameLayout = [shown = display.get()] { return shown->LayoutFrame(); };
    SDL_Log("MCDisplay: renderer %s", display->_Presenter->Name().c_str());
    display->SetTitle(options.Title);

    if (options.FollowWindow)
    {
        display->_FollowWindow = true;
        display->_MinWidth = options.Width;
        display->_MinHeight = options.Height;
        display->_MaxHeight = std::max(options.MaxHeight, 0);
        display->_FollowDisplayScale = options.FollowDisplayScale;
        display->SetInterfaceScale(options.InterfaceScale);
        SDL_SetWindowMinimumSize(window, options.Width, options.Height);
        SDL_SyncWindow(window);
        display->WindowScreenSize(display->_Width, display->_Height);
        display->_View = SDL_Rect{0, 0, display->_Width, display->_Height};
    }

    display->MakeScreen();
    return display;
}

MCDisplay::~MCDisplay()
{
    MCRenderer::SetOpPlane(*_Screen, nullptr);
    MCRenderer::RemoveFrameSurface(*_Screen);
    _Presenter.reset();
}

void MCDisplay::SetTitle(std::string_view title)
{
    const std::string_view renderer = _Presenter->Kind() == MCRendererKind::Vulkan ? MCGpuApiName : "Software";
    SDL_SetWindowTitle(_Window.get(), std::format("{} [{}]", title, renderer).c_str());
}

void MCDisplay::MakeScreen()
{
    const size_t size = static_cast<size_t>(_Width) * static_cast<size_t>(_Height);

    if (size > _PixelCapacity)
    {
        _Screen->Pixels = std::make_shared<uint8_t[]>(size);
        _PixelCapacity = size;
    }

    _Screen->XMax = _Width - 1;
    _Screen->YMax = _Height - 1;
    std::ranges::fill(Pixels(), uint8_t{0});
    _Ops->assign(size, 0);
    MCRenderer::SetOpPlane(*_Screen, _Ops);
    MCRenderer::AddFrameSurface(_Screen);
    std::scoped_lock lock(_PaletteLock);
    _PaletteDirty = true;
}

namespace
{
    /// <summary>
    /// The lines a screen in a window <paramref name="pixelHeight"/> pixels tall at <paramref name="displayScale"/>
    /// pixels a point gets within <paramref name="fit"/>'s limits before its interface scale (0: the window's).
    /// </summary>
    int UnscaledMaxHeight(int pixelHeight, float displayScale, const MCScreenFit& fit)
    {
        int maxHeight = fit.MaxHeight;

        // On a high-DPI display (SDL's display scale: a Retina screen's 2, a Windows desktop's 150%), no taller than the
        // window in points, nor shorter than the smallest screen (a short window on a scaled desktop), so the screen
        // keeps the window's aspect.
        if (fit.FollowDisplayScale && displayScale > 1.0f)
        {
            const int pointHeight =
                std::max(static_cast<int>(std::lround(static_cast<double>(pixelHeight) / displayScale)), fit.MinHeight);
            maxHeight = maxHeight > 0 ? std::min(maxHeight, pointHeight) : pointHeight;
        }

        return maxHeight;
    }

    /// <summary>AUTO's steps and largest scale (the interface size choices': 100 to 200 percent by 25).</summary>
    constexpr double AutoScaleStep = 0.25;
    constexpr double AutoScaleMost = 2.0;
}

bool MCDisplay::WindowPixelSize(int& pixelWidth, int& pixelHeight) const
{
    return (SDL_GetWindowFlags(_Window.get()) & SDL_WINDOW_MINIMIZED) == 0 &&
           SDL_GetWindowSizeInPixels(_Window.get(), &pixelWidth, &pixelHeight) && pixelWidth > 0 && pixelHeight > 0;
}

MCScreenFit MCDisplay::ScreenFit() const
{
    return MCScreenFit{.MinWidth = _MinWidth,
                       .MinHeight = _MinHeight,
                       .MaxHeight = _MaxHeight,
                       .FollowDisplayScale = _FollowDisplayScale,
                       .InterfaceScale = _InterfaceScale};
}

void MCDisplay::WindowScreenSize(int& width, int& height) const
{
    width = _Width;
    height = _Height;
    int pixelWidth = 0;
    int pixelHeight = 0;

    if (!WindowPixelSize(pixelWidth, pixelHeight))
    {
        return;
    }

    const SDL_Point size =
        FollowingScreenSize(pixelWidth, pixelHeight, SDL_GetWindowDisplayScale(_Window.get()), ScreenFit());
    width = size.x;
    height = size.y;
}

float MCDisplay::AutoInterfaceScale(int lines)
{
    const double wanted = static_cast<double>(lines) / OriginalLines;
    return static_cast<float>(std::clamp(std::round(wanted / AutoScaleStep) * AutoScaleStep, 1.0, AutoScaleMost));
}

float MCDisplay::AutoInterfaceScaleNow() const
{
    int pixelWidth = 0;
    int pixelHeight = 0;

    if (!WindowPixelSize(pixelWidth, pixelHeight))
    {
        return 1.0f;
    }

    const int maxHeight = UnscaledMaxHeight(pixelHeight, SDL_GetWindowDisplayScale(_Window.get()), ScreenFit());
    return AutoInterfaceScale(maxHeight > 0 ? std::min(maxHeight, pixelHeight) : pixelHeight);
}

SDL_Point MCDisplay::FollowingScreenSize(int pixelWidth, int pixelHeight, float displayScale, const MCScreenFit& fit)
{
    int maxHeight = UnscaledMaxHeight(pixelHeight, displayScale, fit);

    // A bigger interface (AUTO's for the display, or the one asked for): that many times fewer lines than the limits
    // so far leave, but never fewer than the smallest screen's.
    const int lines = maxHeight > 0 ? std::min(maxHeight, pixelHeight) : pixelHeight;
    const float interfaceScale = fit.InterfaceScale == AutoInterface ? AutoInterfaceScale(lines) : fit.InterfaceScale;

    if (interfaceScale > 1.0f)
    {
        maxHeight = std::max(static_cast<int>(std::lround(static_cast<double>(lines) / interfaceScale)), fit.MinHeight);
    }

    // A window taller than the most: that many lines, as wide as the window's aspect makes them.
    if (maxHeight > 0 && pixelHeight > maxHeight)
    {
        pixelWidth = static_cast<int>(
            std::lround(static_cast<double>(pixelWidth) * maxHeight / static_cast<double>(pixelHeight)));
        pixelHeight = maxHeight;
    }

    return SDL_Point{std::max(pixelWidth, fit.MinWidth), std::max(pixelHeight, fit.MinHeight)};
}

void MCDisplay::LargestScreenSize(int& width, int& height) const
{
    width = _Width;
    height = _Height;
    int count = 0;
    const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> displays(SDL_GetDisplays(&count), &SDL_free);

    if (displays == nullptr)
    {
        return;
    }

    for (const SDL_DisplayID display : std::span(displays.get(), static_cast<size_t>(count)))
    {
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display);

        if (mode == nullptr)
        {
            continue;
        }

        const float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
        width = std::max(width, static_cast<int>(std::ceil(static_cast<float>(mode->w) * density)));
        height = std::max(height, static_cast<int>(std::ceil(static_cast<float>(mode->h) * density)));
    }
}

void MCDisplay::ResizeWindowToScale()
{
    if (IsFullscreen() || _FollowWindow)
    {
        return;
    }

    SDL_SetWindowSize(_Window.get(), _Width * _WindowScale, _Height * _WindowScale);
    SDL_SetWindowPosition(_Window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

std::expected<void, std::string> MCDisplay::SetLogicalSize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return std::unexpected(std::format("bad screen size {}x{}", width, height));
    }

    if (width == _Width && height == _Height)
    {
        return {};
    }

    _Width = width;
    _Height = height;
    _View = SDL_Rect{0, 0, width, height};
    MakeScreen();
    ResizeWindowToScale();
    return {};
}

bool MCDisplay::SetView(int x, int y, int width, int height)
{
    SDL_Rect view;
    view.w = std::clamp(width, 1, _Width);
    view.h = std::clamp(height, 1, _Height);
    view.x = std::clamp(x, 0, _Width - view.w);
    view.y = std::clamp(y, 0, _Height - view.h);

    if (view.x == _View.x && view.y == _View.y && view.w == _View.w && view.h == _View.h)
    {
        return false;
    }

    _View = view;
    return true;
}

void MCDisplay::SetPalette(int first, std::span<const MCVfxRgb> entries)
{
    if (entries.empty() || first < 0 || first >= 256)
    {
        return;
    }

    const int count = static_cast<int>(std::min<size_t>(entries.size(), static_cast<size_t>(256 - first)));
    std::scoped_lock lock(_PaletteLock);

    for (int i = 0; i < count; ++i)
    {
        _Palette[static_cast<size_t>(first + i)] = entries[static_cast<size_t>(i)];
    }

    _PaletteDirty = true;
    EndCycleOver(first, count);
}

void MCDisplay::SetPalette(int first, std::span<const uint8_t> rgb)
{
    if (rgb.size() < 3 || first < 0 || first >= 256)
    {
        return;
    }

    const int count = static_cast<int>(std::min<size_t>(rgb.size() / 3, static_cast<size_t>(256 - first)));
    std::scoped_lock lock(_PaletteLock);

    for (int i = 0; i < count; ++i)
    {
        const size_t at = static_cast<size_t>(i) * 3;
        _Palette[static_cast<size_t>(first + i)] = {rgb[at], rgb[at + 1], rgb[at + 2]};
    }

    _PaletteDirty = true;
    EndCycleOver(first, count);
}

void MCDisplay::EndCycleOver(int first, int count)
{
    if (_Cycle.Step >= 0 && first < _Cycle.First + 8 && first + count > _Cycle.First)
    {
        _Cycle.Step = -1;
        _CycleDirty = true;
    }
}

void MCDisplay::GetPalette(int first, std::span<MCVfxRgb> out) const
{
    if (out.empty() || first < 0 || first >= 256)
    {
        return;
    }

    const int count = static_cast<int>(std::min<size_t>(out.size(), static_cast<size_t>(256 - first)));
    std::scoped_lock lock(_PaletteLock);

    for (int i = 0; i < count; ++i)
    {
        out[static_cast<size_t>(i)] = _Palette[static_cast<size_t>(first + i)];
    }
}

void MCDisplay::SetGamma(float gamma, float brightness)
{
    std::scoped_lock lock(_PaletteLock);
    _Gamma = std::clamp(gamma, 0.1f, 10.0f);
    _Brightness = std::clamp(brightness, 0.0f, 4.0f);
    _PaletteDirty = true;
}

void MCDisplay::SetFade(int levels)
{
    _Fade = std::clamp(levels, 0, 255);
}

void MCDisplay::GetShownColors(std::span<SDL_Color, 256> out) const
{
    std::scoped_lock lock(_PaletteLock);
    std::array<SDL_Color, 256> colors{};
    ApplyGamma(_Palette, _Gamma, _Brightness, colors);
    ApplyCycle(colors, _Cycle, out);
}

void MCDisplay::SetColorCycle(const MCColorCycle& cycle)
{
    std::scoped_lock lock(_PaletteLock);

    if (cycle != _Cycle)
    {
        _Cycle = cycle;
        _CycleDirty = true;
    }
}

MCViewport MCDisplay::ShownViewport() const
{
    return _Presenter->Viewport(_View.w, _View.h);
}

bool MCDisplay::SharpScreen() const
{
    return GHdArt != 0 && _Presenter != nullptr && _Presenter->Kind() == MCRendererKind::Vulkan &&
           MCRenderer::GpuDrawing() == MCGpuDrawing::On;
}

namespace
{
    /// <summary>
    /// Whether <paramref name="viewport"/> shows more window pixels than <paramref name="screenWidth"/> x
    /// <paramref name="screenHeight"/> (scaled up), and then how many.
    /// </summary>
    bool ShownScaledUp(const MCViewport& viewport, int screenWidth, int screenHeight, int& width, int& height)
    {
        const auto shownWidth = static_cast<int>(std::lround(viewport.W));
        const auto shownHeight = static_cast<int>(std::lround(viewport.H));

        if (shownWidth <= screenWidth || shownHeight <= screenHeight)
        {
            return false;
        }

        width = shownWidth;
        height = shownHeight;
        return true;
    }
}

bool MCDisplay::ScaledOutput(int& width, int& height) const
{
    const bool wholeScreen = _View.x == 0 && _View.y == 0 && _View.w == _Width && _View.h == _Height;

    if (!_FollowWindow || (!wholeScreen && !SharpScreen()) || _View.w <= 0 || _View.h <= 0)
    {
        return false;
    }

    return ShownScaledUp(ShownViewport(), _View.w, _View.h, width, height);
}

bool MCDisplay::WholeScreenOutput(int& width, int& height) const
{
    return _FollowWindow && ShownScaledUp(_Presenter->Viewport(_Width, _Height), _Width, _Height, width, height);
}

float MCDisplay::UnderlayScale() const
{
    int width = 0;
    int height = 0;
    return ScaledOutput(width, height) ? static_cast<float>(height) / static_cast<float>(_View.h) : 1.0f;
}

float MCDisplay::ScreenScale() const
{
    int width = 0;
    int height = 0;
    return WholeScreenOutput(width, height) ? static_cast<float>(height) / static_cast<float>(_Height) : 1.0f;
}

void MCDisplay::PixelScale(float& scaleX, float& scaleY) const
{
    scaleX = 1.0f;
    scaleY = 1.0f;
    const MCViewport viewport = ShownViewport();

    if (viewport.W <= 0.0f || viewport.H <= 0.0f)
    {
        return;
    }

    scaleX = viewport.W / static_cast<float>(_View.w);
    scaleY = viewport.H / static_cast<float>(_View.h);
}

MCFrame MCDisplay::LayoutFrame() const
{
    MCFrame frame{.Screen = *_Screen};
    frame.Pixels = Pixels();
    frame.Width = _Width;
    frame.Height = _Height;
    frame.View = _View;
    frame.Ops = *_Ops;
    ScaledOutput(frame.OutputWidth, frame.OutputHeight);
    frame.SharpScreen = frame.ScaledOutput() && SharpScreen();
    return frame;
}

MCFrame MCDisplay::BuildFrame(bool allColors)
{
    MCFrame frame = LayoutFrame();
    _FrameUnderlays = ScreenUnderlays();
    frame.Underlays = _FrameUnderlays;

    {
        std::scoped_lock lock(_PaletteLock);

        if (_PaletteDirty || allColors)
        {
            ApplyGamma(_Palette, _Gamma, _Brightness, _PaletteColors);
            frame.PaletteChanged = true;
            _PaletteDirty = false;
        }

        if (frame.PaletteChanged || _CycleDirty)
        {
            ApplyCycle(_PaletteColors, _Cycle, _ShownColors);
            frame.ColorsChanged = true;
            _CycleDirty = false;
        }

        frame.Cycle = _Cycle;
    }

    frame.Colors = _ShownColors;
    frame.PaletteColors = _PaletteColors;
    frame.Fade = _Fade;
    return frame;
}

std::expected<void, std::string> MCDisplay::Present()
{
    MCGameContext::Current().Clock().Presented();

    if (OnPresent)
    {
        OnPresent();
    }

    if ((SDL_GetWindowFlags(_Window.get()) & SDL_WINDOW_MINIMIZED) != 0)
    {
        return _Presenter->Discard(BuildFrame(false));
    }

    return _Presenter->Present(BuildFrame(false));
}

std::expected<std::vector<SDL_Color>, std::string> MCDisplay::ReadFrame()
{
    // At the screen's size, as ComposeScreen gives it.
    MCFrame frame = BuildFrame(true);
    frame.OutputWidth = 0;
    frame.OutputHeight = 0;
    return _Presenter->ReadFrame(frame);
}

std::expected<MCDisplay::MCShownScreen, std::string> MCDisplay::ReadShownScreen()
{
    const MCFrame frame = BuildFrame(true);
    auto read = _Presenter->ReadFrame(frame);

    if (!read)
    {
        return std::unexpected(read.error());
    }

    // A scaled frame is composited at the shown size; else the shown view is a part of the screen.
    const bool scaled = frame.ScaledOutput();
    const int pitch = scaled ? frame.OutputWidth : frame.Width;
    const SDL_Rect shown = scaled ? SDL_Rect{0, 0, frame.OutputWidth, frame.OutputHeight} : frame.View;
    MCShownScreen screen{.Width = shown.w, .Height = shown.h};
    screen.Pixels.resize(static_cast<size_t>(shown.w) * static_cast<size_t>(shown.h));

    for (int y = 0; y < shown.h; ++y)
    {
        std::memcpy(screen.Pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(shown.w),
                    read->data() + static_cast<size_t>(shown.y + y) * pitch + shown.x,
                    static_cast<size_t>(shown.w) * sizeof(SDL_Color));
    }

    return screen;
}

std::expected<void, std::string> MCDisplay::SaveShownScreenshot(const std::filesystem::path& path)
{
    auto screen = ReadShownScreen();

    if (!screen)
    {
        return std::unexpected(screen.error());
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(screen->Width, screen->Height, SDL_PIXELFORMAT_RGBA32,
                                                 screen->Pixels.data(), screen->Width * sizeof(SDL_Color));

    if (surface == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateSurfaceFrom"));
    }

    const bool saved = SDL_SavePNG(surface, MCFileSystem::ToUtf8(path).c_str());
    SDL_DestroySurface(surface);

    if (!saved)
    {
        return std::unexpected(SdlError("SDL_SavePNG"));
    }

    return {};
}

std::vector<uint8_t> MCDisplay::ComposeScreen() const
{
    const std::vector<MCUnderlay> underlays = ScreenUnderlays();
    MCFrame frame{.Screen = *_Screen};
    frame.Pixels = Pixels();
    frame.Width = _Width;
    frame.Height = _Height;
    frame.View = _View;
    frame.Ops = *_Ops;
    frame.Underlays = underlays;
    auto shown = _Presenter->ReadScreen(frame);

    if (shown)
    {
        return std::move(*shown);
    }

    SDL_Log("MCDisplay: the screen can't be read (%s); composing the software renderer's pixels",
            shown.error().c_str());
    std::vector<uint8_t> pixels(Pixels().begin(), Pixels().end());
    MCRenderer::ComposeUnderlays(*_Screen, pixels, MCRect{0, 0, _Width - 1, _Height - 1});
    return pixels;
}

std::vector<MCUnderlay> MCDisplay::ScreenUnderlays() const
{
    std::vector<MCUnderlay> underlays;

    const MCAddressKey screen = _Screen->Pixels != nullptr ? MCAddressKey::Of(_Screen->Pixels[0]) : MCAddressKey();

    for (const MCUnderlay& underlay : MCRenderer::Underlays())
    {
        const std::shared_ptr<const MCWindow> source = underlay.Source.lock();

        if (underlay.TargetPixels && underlay.TargetPixels == screen && source != nullptr && source->Pixels != nullptr)
        {
            underlays.push_back(underlay);
        }
    }

    return underlays;
}

bool MCDisplay::SetFullscreen(bool fullscreen)
{
    if (!SDL_SetWindowFullscreen(_Window.get(), fullscreen))
    {
        return false;
    }

    SDL_SyncWindow(_Window.get());

    if (!fullscreen)
    {
        ResizeWindowToScale();
    }

    return true;
}

bool MCDisplay::IsFullscreen() const
{
    return (SDL_GetWindowFlags(_Window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
}

void MCDisplay::SetVSync(bool on)
{
    _Presenter->SetVSync(on);
}

std::expected<void, std::string> MCDisplay::SaveScreenshot(const std::filesystem::path& path) const
{
    SDL_Surface* surface = SDL_CreateSurface(_View.w, _View.h, SDL_PIXELFORMAT_INDEX8);

    if (surface == nullptr)
    {
        return std::unexpected(SdlError("SDL_CreateSurface"));
    }

    SDL_Palette* palette = SDL_CreateSurfacePalette(surface);

    if (palette != nullptr)
    {
        SDL_Color colors[256];
        {
            std::scoped_lock lock(_PaletteLock);

            for (size_t i = 0; i < 256; ++i)
            {
                colors[i] = {_Palette[i].R, _Palette[i].G, _Palette[i].B, 255};
            }
        }

        SDL_SetPaletteColors(palette, colors, 0, 256);
    }

    // The shown view only, with the underlays.
    const std::vector<uint8_t> shown = ComposeScreen();

    for (int y = 0; y < _View.h; ++y)
    {
        std::memcpy(static_cast<uint8_t*>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch,
                    shown.data() + static_cast<size_t>(_View.y + y) * _Width + _View.x, static_cast<size_t>(_View.w));
    }

    const bool saved = SDL_SavePNG(surface, MCFileSystem::ToUtf8(path).c_str());
    SDL_DestroySurface(surface);

    if (!saved)
    {
        return std::unexpected(SdlError("SDL_SavePNG"));
    }

    return {};
}

namespace
{
    /// <summary>Window pixels per window point (high-density displays have more than one).</summary>
    float PixelDensity(SDL_Window& window)
    {
        const float density = SDL_GetWindowPixelDensity(&window);
        return density > 0.0f ? density : 1.0f;
    }
}

bool MCDisplay::WindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
{
    const float density = PixelDensity(*_Window);
    const bool inside =
        MapToLogical(ShownViewport(), _View.w, _View.h, windowX * density, windowY * density, logicalX, logicalY);
    logicalX += static_cast<float>(_View.x);
    logicalY += static_cast<float>(_View.y);
    return inside;
}

void MCDisplay::LogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
{
    logicalX -= static_cast<float>(_View.x);
    logicalY -= static_cast<float>(_View.y);
    const MCViewport viewport = ShownViewport();

    if (viewport.W <= 0.0f || viewport.H <= 0.0f)
    {
        windowX = logicalX;
        windowY = logicalY;
        return;
    }

    const float density = PixelDensity(*_Window);
    windowX = (viewport.X + logicalX * viewport.W / static_cast<float>(_View.w)) / density;
    windowY = (viewport.Y + logicalY * viewport.H / static_cast<float>(_View.h)) / density;
}

bool MCDisplay::MapToLogical(const MCViewport& viewport, int logicalWidth, int logicalHeight, float x, float y,
                             float& logicalX, float& logicalY)
{
    if (viewport.W <= 0.0f || viewport.H <= 0.0f)
    {
        logicalX = x;
        logicalY = y;
        return false;
    }

    logicalX = (x - viewport.X) * static_cast<float>(logicalWidth) / viewport.W;
    logicalY = (y - viewport.Y) * static_cast<float>(logicalHeight) / viewport.H;
    return logicalX >= 0.0f && logicalY >= 0.0f && logicalX < static_cast<float>(logicalWidth) &&
           logicalY < static_cast<float>(logicalHeight);
}
