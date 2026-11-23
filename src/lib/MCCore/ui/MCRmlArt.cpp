#include "stdafx.h"
#include "ui/MCRmlArt.h"
#include "logistics/MCLogPort.h"
#include "main/MCGamePaths.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCShapeFile.h"
#include "ui/MCRmlSystem.h"

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

namespace
{
    /// <summary>A PNG's width and height, from its header (its IHDR chunk, first after the signature).</summary>
    constexpr size_t PngWidthAt = 16;
    constexpr size_t PngHeaderBytes = 24;
    constexpr std::array<uint8_t, 8> PngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    /// <summary>The folder beside an app picture its HD copy is in.</summary>
    constexpr std::string_view HdFolder = "hd";

    uint32_t BigEndian(const std::vector<char>& bytes, size_t at)
    {
        uint32_t value = 0;

        for (size_t i = 0; i < sizeof(uint32_t); ++i)
        {
            value = (value << CHAR_BIT) | static_cast<uint8_t>(bytes[at + i]);
        }

        return value;
    }

    std::expected<SDL_Point, std::string> PngSize(const std::vector<char>& bytes, std::string_view path)
    {
        if (bytes.size() < PngHeaderBytes ||
            !std::equal(PngSignature.begin(), PngSignature.end(), bytes.begin(),
                        [](uint8_t want, char have) { return static_cast<uint8_t>(have) == want; }))
        {
            return std::unexpected(std::format("the menus' picture {} isn't a PNG", path));
        }

        return SDL_Point{static_cast<int>(BigEndian(bytes, PngWidthAt)),
                         static_cast<int>(BigEndian(bytes, PngWidthAt + sizeof(uint32_t)))};
    }

    /// <summary>A PNG's pixels in RGBA8, premultiplied, smoothly filtered when drawn at another size.</summary>
    std::expected<std::shared_ptr<const MCUiTexture>, std::string> DecodePng(const std::vector<char>& bytes,
                                                                             std::string_view path)
    {
        SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
        SDL_Surface* loaded = stream != nullptr ? SDL_LoadPNG_IO(stream, true) : nullptr;
        SDL_Surface* rgba = loaded != nullptr ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
        SDL_DestroySurface(loaded);

        if (rgba == nullptr)
        {
            return std::unexpected(std::format("the menus' picture {} can't be read: {}", path, SDL_GetError()));
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(rgba->w) * static_cast<size_t>(rgba->h) * PixelBytes);

        for (int y = 0; y < rgba->h; ++y)
        {
            const auto* row = static_cast<const uint8_t*>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch;

            for (int x = 0; x < rgba->w; ++x)
            {
                const uint8_t* from = row + static_cast<size_t>(x) * PixelBytes;
                uint8_t* to = pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(rgba->w) + x) * PixelBytes;
                const uint32_t alpha = from[3];

                for (size_t c = 0; c < 3; ++c)
                {
                    to[c] = static_cast<uint8_t>((from[c] * alpha + Opaque / 2) / Opaque);
                }

                to[3] = from[3];
            }
        }

        const int width = rgba->w;
        const int height = rgba->h;
        SDL_DestroySurface(rgba);
        return MCRmlRecorder::MakeTexture(width, height, std::move(pixels), true);
    }

    /// <summary>The app's picture <paramref name="path"/>, its HD copy in its place when asked and there.</summary>
    std::expected<MCRmlRecorder::MCLoaded, std::string> LoadAppArt(std::string_view path, bool hd,
                                                                   const MCRmlFiles& files)
    {
        const std::expected<std::vector<char>, std::string> bytes = files.ReadFile(path);

        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }

        const std::expected<SDL_Point, std::string> size = PngSize(*bytes, path);

        if (!size)
        {
            return std::unexpected(size.error());
        }

        const std::string hdPath = MCRmlArt::HdPath(path);

        if (hd && files.Has(hdPath))
        {
            if (const std::expected<std::vector<char>, std::string> hdBytes = files.ReadFile(hdPath); hdBytes)
            {
                if (auto texture = DecodePng(*hdBytes, hdPath); texture)
                {
                    return MCRmlRecorder::MCLoaded{.Texture = std::move(*texture), .Width = size->x, .Height = size->y};
                }
                else
                {
                    SDL_Log("Menus: %s; the 1998 size drawn instead", texture.error().c_str());
                }
            }
        }

        auto texture = DecodePng(*bytes, path);

        if (!texture)
        {
            return std::unexpected(texture.error());
        }

        return MCRmlRecorder::MCLoaded{.Texture = std::move(*texture), .Width = size->x, .Height = size->y};
    }
}

namespace MCRmlArt
{
    std::string HdPath(std::string_view path)
    {
        const std::filesystem::path file(path);
        return (file.parent_path() / HdFolder / file.filename()).generic_string();
    }

    std::expected<MCRmlRecorder::MCLoaded, std::string> Load(std::string_view source, bool hd, const MCRmlFiles& files)
    {
        if (!source.starts_with(Scheme))
        {
            return LoadAppArt(source, hd, files);
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
