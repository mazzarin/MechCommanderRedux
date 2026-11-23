#pragma once

#include "MCTest.h"

// Port-only (SPEC-menus.md's "Testing"): what the remade screens' tests share: the windows they're drawn and checked
// at, a picture's hash, and where --shots saves one.

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
}
