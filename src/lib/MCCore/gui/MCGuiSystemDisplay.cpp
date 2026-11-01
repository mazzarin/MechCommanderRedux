#include "stdafx.h"
#include "gui/MCGuiSystem.h"
#include "camera/MCMainWindow.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "vfx/MCVfxFunctions.h"
#include "platform/MCCursor.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"

namespace
{
    /// <summary>The gamma translation table (initialised data in MCX.EXE): one step of brightening.</summary>
    constexpr std::array<uint8_t, 256> GammaColorTranslation = {
        0,   6,   10,  13,  16,  19,  21,  23,  25,  27,  29,  31,  33,  35,  37,  39,  40,  42,  44,  45,  47,  48,
        50,  51,  53,  54,  56,  57,  58,  60,  61,  63,  64,  65,  67,  68,  69,  70,  72,  73,  74,  75,  77,  78,
        79,  80,  81,  83,  84,  85,  86,  87,  88,  89,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 103,
        104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
        125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 138, 139, 140, 141, 142, 143, 144, 145,
        146, 146, 147, 148, 149, 150, 151, 152, 153, 153, 154, 155, 156, 157, 158, 159, 159, 160, 161, 162, 163, 164,
        164, 165, 166, 167, 168, 169, 169, 170, 171, 172, 173, 173, 174, 175, 176, 177, 177, 178, 179, 180, 181, 181,
        182, 183, 184, 185, 185, 186, 187, 188, 188, 189, 190, 191, 192, 192, 193, 194, 195, 195, 196, 197, 198, 198,
        199, 200, 201, 201, 202, 203, 204, 204, 205, 206, 207, 207, 208, 209, 210, 210, 211, 212, 213, 213, 214, 215,
        215, 216, 217, 218, 218, 219, 220, 220, 221, 222, 223, 223, 224, 225, 225, 226, 227, 228, 228, 229, 230, 230,
        231, 232, 232, 233, 234, 235, 235, 236, 237, 237, 238, 239, 239, 240, 241, 241, 242, 243, 244, 244, 245, 246,
        246, 247, 248, 248, 249, 250, 250, 251, 252, 252, 253, 254, 254, 255};

    /// <summary>The palette entries Windows kept for itself (0..9 and 246..255) unless a movie played.</summary>
    constexpr int32_t FirstGameColor = 10;
    constexpr int32_t EndGameColor = 0xf6;

    /// <summary>The tallest the screen gets: PREFS "UIResolution", native (0) and every choice no taller than 1080.</summary>
    int MaxScreenHeight()
    {
        return GUiResolution > 0 ? std::min(GUiResolution, MaxUiResolution) : MaxUiResolution;
    }

    /// <summary>One gamma step over <paramref name="color"/>.</summary>
    MCVfxRgb Brighten(MCVfxRgb color)
    {
        return {GammaColorTranslation[color.R], GammaColorTranslation[color.G], GammaColorTranslation[color.B]};
    }

    /// <summary>The palette entries of <paramref name="bytes"/> (red, green, blue each).</summary>
    std::vector<MCVfxRgb> RgbEntries(std::span<const uint8_t> bytes)
    {
        std::vector<MCVfxRgb> entries(bytes.size() / 3);

        for (size_t i = 0; i < entries.size(); i++)
        {
            entries[i] = {bytes[i * 3], bytes[i * 3 + 1], bytes[i * 3 + 2]};
        }

        return entries;
    }
}

auto MCGuiSystem::ShowPalette(int32_t first, int32_t count) -> void
{
    if (_Display != nullptr && first >= 0 && first < 256 && count > 0)
    {
        _Display->SetPalette(
            first, std::span<const MCVfxRgb>(_LogicalPalette)
                       .subspan(static_cast<size_t>(first), static_cast<size_t>(std::min(count, 256 - first))));
    }
}

auto MCGuiSystem::OpenDisplay() -> void
{
    // The original made the DirectDraw object (exclusive full screen, a primary surface and its palette) or, in a
    // window, a GDI palette and an 8-bit DIB section. The port opens the SDL display, whose 8-bit buffer the game
    // draws into. The screen is the window's size in pixels (at least 640x480), not the mode PREFS "Resolution" asks
    // for; a scenario draws on all of it and follows the window (FollowWindowSize), the 640x480 screens use its
    // top-left corner. A window taller than PREFS "UIResolution" (1080 at most) gets a screen that many lines tall at
    // its aspect, shown scaled up, with the world view composited at the window's pixels.
    MCDisplayOptions options;
    options.Title = AppName;
    options.Width = 640;
    options.Height = 480;
    options.FollowWindow = true;
    options.MaxHeight = MaxScreenHeight();
    options.FollowDisplayScale = GUiResolution == 0;
    options.Fullscreen = GFullScreen != 0;
    options.Stretch = GStretchToFit != 0;
    options.Hidden = GHiddenWindow;

    // A hidden window (the tests') is 640x480 pixels on any desktop. A window-following screen is the window's size
    // in pixels, which the desktop's size (the window scale) and pixel density would otherwise set, and with it what
    // the game draws and updates: the tests' recorded frames and runs would hold on one kind of machine only.
    if (GHiddenWindow)
    {
        options.WindowScale = 1;
        options.HighPixelDensity = false;
    }

    options.Renderer = static_cast<MCRendererKind>(GRenderer);
    options.VSync = GVSync;
    auto display = MCDisplay::Create(options);

    if (!display)
    {
        Fatal(0, "Cannot initialize DirectDraw.", display.error());
    }

    _Display = std::move(*display);
    GWidth = _Display->Width();
    GHeight = _Display->Height();
    DisplayWidth = GWidth;
    DisplayHeight = GHeight;
    ScreenWidth = GWidth;
    ScreenHeight = GHeight;
    MCInput::Attach(_Display);
    ShowPalette(0, 256);
    _DisplayReady = true;
}

auto MCGuiSystem::ResetDisplay(int32_t width, int32_t height, int32_t bitDepth) -> void
{
    // The original released and remade the surfaces in the new mode (full screen or windowed). The port switches the
    // display and, for another size, its buffer.
    GBitDepth = bitDepth;
    GWidth = width;
    GHeight = height;

    if (_Display == nullptr)
    {
        OpenDisplay();
        return;
    }

    _Display->SetFullscreen(GFullScreen != 0);

    if (width != _Display->Width() || height != _Display->Height())
    {
        auto resized = _Display->SetLogicalSize(width, height);

        if (!resized)
        {
            Fatal(0, " Unable to Set Display Mode ", resized.error());
        }

        // The screen port keeps pointing at the display's buffer, which the resize moved.
        if (const std::shared_ptr<MCGuiPort> port = ScreenPort();
            port != nullptr && port->SharedBitmap() != nullptr && port->Bitmap().Pixels != nullptr)
        {
            port->Bitmap().Pixels = _Display->Screen().Pixels;
        }
    }

    MCInput::RefreshMouseArea();
    ShowPalette(0, 256);
    _DisplayReady = true;
}

auto MCGuiSystem::FollowWindowSize() -> bool
{
    if (_Display == nullptr || !_Display->FollowsWindow())
    {
        return false;
    }

    int32_t width = 0;
    int32_t height = 0;
    _Display->SetMaxHeight(MaxScreenHeight());
    _Display->SetFollowDisplayScale(GUiResolution == 0);
    _Display->WindowScreenSize(width, height);

    if (width == _Display->Width() && height == _Display->Height())
    {
        return false;
    }

    auto resized = _Display->SetLogicalSize(width, height);

    if (!resized)
    {
        Fatal(0, " Unable to Set Display Mode ", resized.error());
    }

    GWidth = width;
    GHeight = height;
    DisplayWidth = width;
    DisplayHeight = height;
    ScreenWidth = width;
    ScreenHeight = height;

    // Edge scrolling follows the new screen's edges (Start set them for the first size only).
    SetScrollRect();

    if (ScreenPixels != nullptr && ScreenPixels->SharedBitmap() != nullptr)
    {
        ScreenPixels->Resize(width, height);
        ScreenPixels->Bitmap().Pixels = _Display->Screen().Pixels;
    }

    MCInput::RefreshMouseArea();

    if (Screen != nullptr)
    {
        Screen->Resize(width, height);

        if (MainHolder() != nullptr)
        {
            // The original's resolution-change broadcast (nothing in MCX.EXE sends it): the main window takes the
            // screen's size and re-tiles its panes, the mech bar goes back to the bottom.
            MCGuiEvent event;
            event.Type = MCGuiEventType::ScreenResized;
            Screen->HandleEvent(event);
        }
        else if (TacticalInterface() != nullptr && TacticalInterface()->MechBar != nullptr)
        {
            // Before the scenario's windows exist (StartScenario after the window was resized in the menus), the
            // mech bar's handler would pass the event to the main window's active pane: just move the bar down.
            MCMechBar& bar = *TacticalInterface()->MechBar;
            bar.MoveTo(1, Height() - bar.Height() - 1);
        }
    }

    return true;
}

auto MCFollowWindowSize() -> bool
{
    return HasGuiSystem() && GuiSystem().FollowWindowSize();
}

auto MCGuiSystem::CloseDisplay() -> void
{
    MCCursor::Shutdown();
    MCInput::Attach({});
    _Display.reset();
    _DisplayReady = false;
}

auto MCGuiSystem::TweakPalette(int32_t first, int32_t count, std::span<const MCVfxRgb> colors, bool sixBit) -> void
{
    if (!_DisplayReady)
    {
        return;
    }

    if (SmackerWindow == nullptr)
    {
        // Entries 0..9 and 246..255 are Windows' own unless a movie plays.
        if (first < FirstGameColor)
        {
            const int32_t skipped = FirstGameColor - first;
            first = FirstGameColor;
            colors =
                colors.subspan(static_cast<size_t>(std::min<int32_t>(skipped, static_cast<int32_t>(colors.size()))));
            count -= skipped;
        }

        if (count + first > EndGameColor)
        {
            count = EndGameColor - first;
        }
    }

    const int32_t end = count + first;

    for (int32_t i = first; i < end; i++)
    {
        MCVfxRgb color = colors[static_cast<size_t>(i - first)];

        if (sixBit)
        {
            color.R = static_cast<uint8_t>(color.R << 2);
            color.G = static_cast<uint8_t>(color.G << 2);
            color.B = static_cast<uint8_t>(color.B << 2);
        }

        CurrentPalette[static_cast<size_t>(i)] = color;
        _LogicalPalette[static_cast<size_t>(i)] = color;
    }

    // Original bug (OB-159): level 2 brightens twice, any other level but 0 once (GammaCorrectCurrentPalette gives
    // level 3 three).
    const int32_t passes = (GammaLevel != 0 ? 1 : 0) + (GammaLevel == 2 ? 1 : 0);

    for (int32_t pass = 0; pass < passes; pass++)
    {
        for (int32_t i = first; i < end; i++)
        {
            _LogicalPalette[static_cast<size_t>(i)] = Brighten(_LogicalPalette[static_cast<size_t>(i)]);
        }
    }

    ShowPalette(first, count);
}

auto MCGuiSystem::GammaCorrectCurrentPalette() -> void
{
    if (_DisplayReady)
    {
        GammaCorrectCurrentPalette(GammaLevel >= 3 ? 0 : GammaLevel + 1);
    }
}

auto MCGuiSystem::GammaCorrectCurrentPalette(int32_t level) -> void
{
    if (!_DisplayReady)
    {
        return;
    }

    GammaLevel = level;

    // Entries 10..245, through the gamma table once per level (three at most).
    for (int32_t i = FirstGameColor; i < EndGameColor; i++)
    {
        _LogicalPalette[static_cast<size_t>(i)] = CurrentPalette[static_cast<size_t>(i)];
    }

    for (int32_t pass = 0; pass < level && pass < 3; pass++)
    {
        for (int32_t i = FirstGameColor; i < EndGameColor; i++)
        {
            _LogicalPalette[static_cast<size_t>(i)] = Brighten(_LogicalPalette[static_cast<size_t>(i)]);
        }
    }

    ShowPalette(FirstGameColor, EndGameColor - FirstGameColor);
}

auto MCGuiSystem::FadeDownCurrentPalette() -> void
{
    if (!_DisplayReady || CurrentPalette[FirstGameColor].R == 0)
    {
        return;
    }

    // Darkens the screen by a step that follows the time each step took (256 levels a second), waiting at least
    // 1/256 s a step, until 256 levels are gone. The original took each step off palette entries 10..245; the port
    // fades the shown picture (MCDisplay::SetFade): the GPU's surfaces hold colours, so a palette change alone doesn't
    // reach what's already drawn, and the frame isn't redrawn during the fade.
    int32_t step = 1;
    int32_t faded = 0;
    const double frequency = static_cast<double>(CountsPerSecond);

    do
    {
        const int64_t stepStart = MCPort::PerformanceCounter();
        faded += step;
        _Display->SetFade(faded);
        std::ignore = _Display->Present();
        float elapsed;

        do
        {
            elapsed = static_cast<float>(static_cast<double>(MCPort::PerformanceCounter() - stepStart) / frequency);
        } while (elapsed < 0.00390625f);

        step = static_cast<int32_t>(elapsed * 256.0f);
    } while (faded < 0x100);

    // Where the original ends: entries 10..245 black until the next palette is set. The fade comes off with them, so
    // the screen stays black.
    for (int32_t i = FirstGameColor; i < EndGameColor; i++)
    {
        CurrentPalette[static_cast<size_t>(i)] = {};
        _LogicalPalette[static_cast<size_t>(i)] = {};
    }

    ShowPalette(FirstGameColor, EndGameColor - FirstGameColor);
    _Display->SetFade(0);
}

auto MCGuiSystem::ActivatePalette(std::span<const uint8_t> colors, int32_t first, int32_t count) -> void
{
    if (first != 0)
    {
        TweakPalette(first, count, RgbEntries(colors.subspan(static_cast<size_t>(first) * 3)), true);
        return;
    }

    // From entry 0 the palette waits for the next frame, which fades to it.
    PendingPalette.resize(256);
    PendingPaletteCount = count;
    PendingPaletteFirst = 0;
    const std::vector<MCVfxRgb> entries = RgbEntries(colors.first(static_cast<size_t>(count) * 3));
    std::ranges::copy(entries, PendingPalette.begin());
}

auto MCGuiSystem::ActivatePaletteFromTga(std::string_view fileName) -> void
{
    std::string path = std::format("{}{}", std::string_view(ArtPath), fileName);
    MCFile tgaFile;

    if (tgaFile.Open(path) != 0)
    {
        path = fileName;

        if (tgaFile.Open(path) != 0)
        {
            GeneralMsg(std::format("Error reading '{}'", path));
        }
    }

    const uint32_t size = tgaFile.FileSize();

    if (size == 0)
    {
        GeneralMsg(std::format("Error reading '{}'", path));
    }

    std::vector<uint8_t> tga(size);
    tgaFile.Read(tga);
    tgaFile.Close();

    // The colour map at byte 0x12: blue-green-red, 8 bits, made 6-bit.
    std::array<MCVfxRgb, 256> palette = {};
    const std::span<const uint8_t> map = std::span<const uint8_t>(tga).subspan(0x12, palette.size() * 3);

    for (size_t i = 0; i < palette.size(); i++)
    {
        palette[i].R = static_cast<uint8_t>(map[i * 3 + 2] >> 2);
        palette[i].G = static_cast<uint8_t>(map[i * 3 + 1] >> 2);
        palette[i].B = static_cast<uint8_t>(map[i * 3] >> 2);
    }

    std::array<uint8_t, 768> bytes = {};

    for (size_t i = 0; i < palette.size(); i++)
    {
        bytes[i * 3] = palette[i].R;
        bytes[i * 3 + 1] = palette[i].G;
        bytes[i * 3 + 2] = palette[i].B;
    }

    ActivatePalette(bytes, 0, 0x100);
    InitAlphaLookup(palette);
}

auto MCGuiSystem::ActivateSmackerPalette(std::span<const uint8_t> colors) -> void
{
    TweakPalette(0, 0x100, RgbEntries(colors), false);
}
