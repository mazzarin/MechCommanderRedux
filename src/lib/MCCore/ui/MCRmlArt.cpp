#include "stdafx.h"
#include "ui/MCRmlArt.h"
#include "logistics/MCLogPort.h"
#include "main/MCGamePaths.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCShapeFile.h"

namespace
{
    /// <summary>A TGA's colour map: at +0x12, 256 colours of blue, green and red (MCGuiPort::Init reads it so).</summary>
    constexpr size_t ColorMapAt = 0x12;
    constexpr size_t ColorBytes = 3;
    constexpr size_t ColorCount = 256;

    /// <summary>The key colour (the palette's 63,0,63): its red and blue at least this, without green.</summary>
    constexpr uint8_t KeyLeast = 0xf8;

    constexpr size_t PixelBytes = 4;
    constexpr uint8_t Opaque = 255;

    /// <summary>A TGA as the logistics screens read it, with its colour map.</summary>
    struct MCArtPicture
    {
        int32_t Width = 0;
        int32_t Height = 0;
        std::vector<uint8_t> Pixels;
        std::array<SDL_Color, ColorCount> Colors{};
    };

    bool IsKey(const SDL_Color& color)
    {
        return color.r >= KeyLeast && color.b >= KeyLeast && color.g == 0;
    }

    /// <summary>The art file a picture's name stands for (under ArtPath, its folders apart by backslashes).</summary>
    std::expected<std::string, std::string> ArtFile(std::string_view name)
    {
        if (name.empty() || name.find("..") != std::string_view::npos || name.find(':') != std::string_view::npos ||
            name.front() == '/' || name.front() == '\\')
        {
            return std::unexpected(std::format("'{}' isn't the name of a picture in the game's art", name));
        }

        std::string file = std::format("{}{}.tga", std::string_view(ArtPath), name);
        std::ranges::replace(file, '/', '\\');
        return file;
    }

    std::expected<MCArtPicture, std::string> ReadArt(std::string_view name)
    {
        const std::expected<std::string, std::string> path = ArtFile(name);

        if (!path)
        {
            return std::unexpected(path.error());
        }

        MCShapeFile file;

        if (file.Open(*path) != 0)
        {
            return std::unexpected(std::format("{} isn't in the game's files", *path));
        }

        std::vector<uint8_t> data(file.FileSize());
        file.Read(data);
        file.Close();
        const MCLogTga tga = ReadLogTga(data);

        if (tga.Width <= 0 || tga.Height <= 0 || tga.Pixels.empty() ||
            data.size() < ColorMapAt + ColorCount * ColorBytes)
        {
            return std::unexpected(std::format("{} is too short for its picture", *path));
        }

        MCArtPicture picture{.Width = tga.Width, .Height = tga.Height};
        picture.Pixels.assign(tga.Pixels.begin(), tga.Pixels.end());

        for (size_t entry = 0; entry < ColorCount; ++entry)
        {
            const uint8_t* color = data.data() + ColorMapAt + entry * ColorBytes;
            picture.Colors[entry] = SDL_Color{color[2], color[1], color[0], Opaque};
        }

        return picture;
    }

    /// <summary>The 1998 picture in its colours, premultiplied (its key transparent).</summary>
    std::vector<uint8_t> Coloured(const MCArtPicture& picture)
    {
        std::vector<uint8_t> rgba(picture.Pixels.size() * PixelBytes);

        for (size_t i = 0; i < picture.Pixels.size(); ++i)
        {
            const SDL_Color& color = picture.Colors[picture.Pixels[i]];

            if (!IsKey(color))
            {
                rgba[i * PixelBytes] = color.r;
                rgba[i * PixelBytes + 1] = color.g;
                rgba[i * PixelBytes + 2] = color.b;
                rgba[i * PixelBytes + 3] = Opaque;
            }
        }

        return rgba;
    }

    /// <summary>The pack's HD copy of <paramref name="picture"/> in RGBA8, its key pixels left out; none without one.</summary>
    std::optional<MCRmlRecorder::MCLoaded> HdCopy(const MCArtPicture& picture)
    {
        const std::shared_ptr<const MCHdArt::Frame> frame =
            MCHdArt::FindPicture(picture.Pixels, picture.Width, picture.Height);

        if (frame == nullptr || frame->OriginalWidth() != picture.Width || frame->OriginalHeight() != picture.Height)
        {
            return std::nullopt;
        }

        const std::span<const uint8_t> colors = MCHdArt::ColorsOf(*frame);
        const size_t bytes = static_cast<size_t>(frame->Width) * static_cast<size_t>(frame->Height) * PixelBytes;
        std::vector<uint8_t> rgba;

        if (frame->Format == MCHdArt::ColorFormat::Rgba8 && colors.size() >= bytes)
        {
            rgba.assign(colors.begin(), colors.begin() + static_cast<ptrdiff_t>(bytes));
        }
        else if (frame->Format == MCHdArt::ColorFormat::Bc7 && !colors.empty())
        {
            rgba = MCBc7::Decode(colors, frame->Width, frame->Height);
        }

        if (rgba.size() != bytes)
        {
            return std::nullopt;
        }

        const int32_t scale = std::max(frame->Scale, 1);

        for (int32_t y = 0; y < frame->Height; ++y)
        {
            for (int32_t x = 0; x < frame->Width; ++x)
            {
                const size_t original = static_cast<size_t>(y / scale) * static_cast<size_t>(picture.Width) +
                                        static_cast<size_t>(x / scale);

                if (IsKey(picture.Colors[picture.Pixels[original]]))
                {
                    const size_t at =
                        (static_cast<size_t>(y) * static_cast<size_t>(frame->Width) + static_cast<size_t>(x)) *
                        PixelBytes;
                    std::fill_n(rgba.begin() + static_cast<ptrdiff_t>(at), PixelBytes, uint8_t{0});
                }
            }
        }

        return MCRmlRecorder::MCLoaded{
            .Texture = MCRmlRecorder::MakeTexture(frame->Width, frame->Height, std::move(rgba), true),
            .Width = picture.Width,
            .Height = picture.Height,
        };
    }
}

namespace MCRmlArt
{
    std::expected<MCRmlRecorder::MCLoaded, std::string> Load(std::string_view source, bool hd)
    {
        if (!source.starts_with(Scheme))
        {
            return std::unexpected(std::format("'{}' doesn't name a picture as {}<name>", source, Scheme));
        }

        const std::expected<MCArtPicture, std::string> picture = ReadArt(source.substr(Scheme.size()));

        if (!picture)
        {
            return std::unexpected(picture.error());
        }

        if (hd)
        {
            if (std::optional<MCRmlRecorder::MCLoaded> copy = HdCopy(*picture); copy.has_value())
            {
                return std::move(*copy);
            }
        }

        return MCRmlRecorder::MCLoaded{
            .Texture = MCRmlRecorder::MakeTexture(picture->Width, picture->Height, Coloured(*picture), false),
            .Width = picture->Width,
            .Height = picture->Height,
        };
    }

    std::expected<std::array<SDL_Color, 256>, std::string> Colors(std::string_view name)
    {
        const std::expected<MCArtPicture, std::string> picture = ReadArt(name);

        if (!picture)
        {
            return std::unexpected(picture.error());
        }

        return picture->Colors;
    }
}
