#pragma once

#include "MCTest.h"
#include "platform/MCDisplay.h"

// Port-only (SPEC-menus.md's "Testing"): what the remade screens' tests share: the windows they're drawn and checked
// at, a picture's hash (an SDL surface's or the game window's), where --shots saves one, and the game's window set to a
// size.

namespace MCMenusPictures
{
    struct MCWindowSize
    {
        int32_t Width;
        int32_t Height;
    };

    /// <summary>
    /// The windows the players' reports name (90 days to 19 Nov, HISTORY.md: 16:9, 16:10, 21:9 and 4:3, from 640x480
    /// to 3840x2410).
    /// </summary>
    inline constexpr std::array<MCWindowSize, 16> PlayersWindows = {{
        {640, 480},
        {1280, 960},
        {1366, 768},
        {1440, 900},
        {1600, 900},
        {1920, 1080},
        {1920, 1200},
        {2560, 1440},
        {2560, 1920},
        {3440, 1440},
        {3456, 2168},
        {3600, 2024},
        {3600, 2260},
        {3840, 1600},
        {3840, 2160},
        {3840, 2410},
    }};

    /// <summary>A remade screen's recorded frames' windows (SPEC-menus.md, "Testing"): 4:3, 16:9 and 5:4.</summary>
    inline constexpr std::array<MCWindowSize, 3> FrameWindows = {{{640, 480}, {1280, 720}, {1280, 1024}}};

    inline constexpr uint32_t FnvBasis = 0x811c9dc5;
    inline constexpr uint32_t FnvPrime = 0x01000193;

    /// <summary>The FNV-1a hash of a picture's size and its RGBA pixels, row by row.</summary>
    inline uint32_t PictureHash(const SDL_Surface& picture)
    {
        constexpr size_t PixelBytes = 4;
        uint32_t hash = FnvBasis;
        hash = (hash ^ static_cast<uint32_t>(picture.w)) * FnvPrime;
        hash = (hash ^ static_cast<uint32_t>(picture.h)) * FnvPrime;

        for (int y = 0; y < picture.h; ++y)
        {
            const auto* row = static_cast<const uint8_t*>(picture.pixels) + static_cast<ptrdiff_t>(y) * picture.pitch;

            for (size_t i = 0; i < static_cast<size_t>(picture.w) * PixelBytes; ++i)
            {
                hash = (hash ^ row[i]) * FnvPrime;
            }
        }

        return hash;
    }

    /// <summary>The FNV-1a hash of a window's picture as the display reads it.</summary>
    inline uint32_t PictureHash(const MCDisplay::MCShownScreen& picture)
    {
        uint32_t hash = FnvBasis;
        hash = (hash ^ static_cast<uint32_t>(picture.Width)) * FnvPrime;
        hash = (hash ^ static_cast<uint32_t>(picture.Height)) * FnvPrime;

        for (const SDL_Color& pixel : picture.Pixels)
        {
            for (const uint8_t channel : {pixel.r, pixel.g, pixel.b, pixel.a})
            {
                hash = (hash ^ channel) * FnvPrime;
            }
        }

        return hash;
    }

    /// <summary>The window made <paramref name="width"/> x <paramref name="height"/> pixels (in points on a dense screen).</summary>
    inline bool SetWindowPixels(MCDisplay& display, int width, int height)
    {
        SDL_Window& window = display.Window();
        const float density = std::max(SDL_GetWindowPixelDensity(&window), 1.0f);
        SDL_SetWindowSize(&window, static_cast<int>(std::lround(static_cast<float>(width) / density)),
                          static_cast<int>(std::lround(static_cast<float>(height) / density)));
        SDL_SyncWindow(&window);
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(&window, &pixelWidth, &pixelHeight);
        return pixelWidth == width && pixelHeight == height;
    }

    /// <summary>Where --shots saves the picture <paramref name="name"/> (a .png); none without --shots.</summary>
    inline std::optional<std::filesystem::path> ShotPath(std::string_view name)
    {
        const std::optional<std::string_view> shots = MCTest::Option("shots");

        if (!shots.has_value())
        {
            return std::nullopt;
        }

        std::filesystem::create_directories(std::filesystem::path(*shots));
        return std::filesystem::path(*shots) / std::format("{}.png", name);
    }

    /// <summary>
    /// The game window's picture as the display reads it now, as "WxH: 0x&lt;its hash&gt;"; saved by --shots as
    /// <c>&lt;name&gt;_WxH</c>.
    /// </summary>
    inline std::string WindowNow(MCDisplay& display, std::string_view name)
    {
        const auto window = display.ReadWindow();
        REQUIRE(window.has_value());

        if (const std::optional<std::filesystem::path> path =
                ShotPath(std::format("{}_{}x{}", name, window->Width, window->Height));
            path.has_value())
        {
            REQUIRE(display.SaveWindowScreenshot(*path).has_value());
        }

        return std::format("{}x{}: {:#010x}", window->Width, window->Height, PictureHash(*window));
    }

    /// <summary>
    /// The game window drawn as <paramref name="name"/> at each recorded frame's window (4:3, 16:9, 5:4), each picture's
    /// hash checked against <paramref name="hashes"/> (--shots saves them), <paramref name="settle"/> run after each
    /// change of size (the menus laid out again, a frame drawn); the window's size put back after.
    /// </summary>
    inline void CheckWindowFrames(MCDisplay& display, std::string_view name,
                                  const std::array<uint32_t, FrameWindows.size()>& hashes,
                                  const std::function<void()>& settle)
    {
        const MCTest::Scope scope{std::string(name)};
        int width = 0;
        int height = 0;
        REQUIRE(SDL_GetWindowSizeInPixels(&display.Window(), &width, &height));

        for (size_t i = 0; i < FrameWindows.size(); ++i)
        {
            const MCWindowSize window = FrameWindows[i];
            REQUIRE(SetWindowPixels(display, window.Width, window.Height));
            settle();
            CHECK_EQ(WindowNow(display, name), std::format("{}x{}: {:#010x}", window.Width, window.Height, hashes[i]));
        }

        REQUIRE(SetWindowPixels(display, width, height));
        settle();
    }
}
