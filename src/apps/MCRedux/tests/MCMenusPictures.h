#pragma once

#include "MCTest.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCUiSoftware.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Context.h>

// Port-only (SPEC-menus.md's "Testing"): what the remade screens' tests share: the windows they're drawn and checked
// at, a picture's hash (an SDL surface's or the game window's), where --shots saves one, the game's window set to a
// size, and the frames' check (a screen drawn on the software path at the frames' windows, and laid out and read at the
// players').

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

    /// <summary>Frames the menus take to lay out a new window and show it (the layout, then the drawn frame).</summary>
    inline constexpr int32_t MenusSettleFrames = 3;

    /// <summary>The game's menus (made by the first remade screen).</summary>
    inline MCRmlMenus& GameMenus()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        REQUIRE(menus.has_value());
        return menus->get();
    }

    /// <summary>The menus laid out for <paramref name="window"/>, between the game's frames: their last frame's draws.</summary>
    inline std::shared_ptr<const MCUiDraws> MenusAt(MCWindowSize window)
    {
        MCRmlMenus& menus = GameMenus();
        menus.SetWindow(window.Width, window.Height);
        std::shared_ptr<const MCUiDraws> draws;

        for (int32_t frame = 0; frame < MenusSettleFrames; ++frame)
        {
            draws = menus.Frame();
        }

        return draws;
    }

    /// <summary>
    /// What the menus show now drawn on the software path at <paramref name="window"/> (the game's next frame takes
    /// them back to its window): the picture's hash, saved by --shots as <paramref name="name"/>.
    /// </summary>
    inline uint32_t MenusDrawnAt(MCWindowSize window, std::string_view name)
    {
        const std::shared_ptr<const MCUiDraws> draws = MenusAt(window);
        REQUIRE(draws != nullptr);
        MCUiSoftware software;

        if (const auto drawn = software.Draw(*draws); !drawn)
        {
            FAIL_CHECK(std::format("{} can't be drawn: {}", name, drawn.error()));
            return 0;
        }

        const SDL_Surface& picture = *software.Picture();

        // SDL's save takes the surface as changeable; it only reads it.
        if (const std::optional<std::filesystem::path> path = ShotPath(name);
            path.has_value() && !SDL_SavePNG(const_cast<SDL_Surface*>(&picture), MCFileSystem::ToUtf8(*path).c_str()))
        {
            FAIL_CHECK(std::format("{} can't be saved to {}: {}", name, path->string(), SDL_GetError()));
        }

        return PictureHash(picture);
    }

    /// <summary>
    /// What the menus show now at every window the players' reports name: what the layout and legibility checks find,
    /// each line headed by its window (nothing: none).
    /// </summary>
    inline std::string MenusProblems()
    {
        std::string problems;

        for (const MCWindowSize& window : PlayersWindows)
        {
            MenusAt(window);
            Rml::Context& context = GameMenus().Menus().Context();
            const std::string found =
                MCRmlChecks::BoxProblems(context) +
                MCRmlChecks::Illegible(context, MCRmlMenus::MenusUnit(window.Width, window.Height));

            if (!found.empty())
            {
                problems += std::format("{}x{}: {}\n", window.Width, window.Height, found);
            }
        }

        return problems;
    }

    /// <summary>
    /// What the menus show now as <paramref name="name"/>: its frames at 4:3, 16:9 and 5:4 (<paramref name="hashes"/>),
    /// and the layout and legibility checks at every players' window.
    /// </summary>
    inline void CheckMenusFrames(std::string_view name, const std::array<uint32_t, FrameWindows.size()>& hashes)
    {
        const MCTest::Scope scope{std::string(name)};

        for (size_t i = 0; i < FrameWindows.size(); ++i)
        {
            const MCWindowSize window = FrameWindows[i];
            const uint32_t hash =
                MenusDrawnAt(window, std::format("menus {} {}x{}", name, window.Width, window.Height));
            CHECK_EQ(std::format("{}x{}: {:#010x}", window.Width, window.Height, hash),
                     std::format("{}x{}: {:#010x}", window.Width, window.Height, hashes[i]));
        }

        CHECK_EQ(MenusProblems(), "");
    }
}
