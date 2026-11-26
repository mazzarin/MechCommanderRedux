#include "stdafx.h"
#include "ui/MCRmlArt.h"
#include "logistics/MCLogPort.h"
#include "main/MCLogMech.h"
#include "main/MCLogVehicle.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "platform/MCSdlHandle.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCShapeFile.h"
#include "ui/MCRmlSystem.h"
#include "vfx/MCVfxFunctions.h"

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

    /// <summary>The briefing map a mission's name stands for (its mission file's MapFile's <c>.log.tga</c>).</summary>
    std::expected<std::string, std::string> MapFile(std::string_view scenario)
    {
        if (scenario.empty() || scenario.find_first_of(":/\\.") != std::string_view::npos)
        {
            return std::unexpected(std::format("'{}' isn't the name of a mission", scenario));
        }

        if (std::optional<std::string> path = MCLogistics::BriefingMapFile(scenario); path.has_value())
        {
            return std::move(*path);
        }

        return std::unexpected(std::format("the mission {} names no map", scenario));
    }

    /// <summary>
    /// The briefing map a map file's name stands for (<c>terrain:</c>): its <c>.log.tga</c> under TerrainPath, as the
    /// room's 1998 screen read it.
    /// </summary>
    std::expected<std::string, std::string> TerrainFile(std::string_view map)
    {
        if (map.empty() || map.find("..") != std::string_view::npos ||
            map.find_first_of(":/\\") != std::string_view::npos)
        {
            return std::unexpected(std::format("'{}' isn't the name of a map", map));
        }

        return GamePath(TerrainPath, map, ".log.tga");
    }

    /// <summary>The picture a source names: <c>art:</c>'s, <c>map:</c>'s or <c>terrain:</c>'s file.</summary>
    std::expected<std::string, std::string> GameFile(std::string_view source)
    {
        if (source.starts_with(MCRmlArt::MapScheme))
        {
            return MapFile(source.substr(MCRmlArt::MapScheme.size()));
        }

        if (source.starts_with(MCRmlArt::TerrainScheme))
        {
            return TerrainFile(source.substr(MCRmlArt::TerrainScheme.size()));
        }

        return ArtFile(source.substr(MCRmlArt::Scheme.size()));
    }

    /// <summary>The game's picture <paramref name="source"/> names (<c>art:</c>'s, <c>map:</c>'s or <c>terrain:</c>'s), with its colours.</summary>
    std::expected<MCArtPicture, std::string> ReadArt(std::string_view source)
    {
        const std::expected<std::string, std::string> path = GameFile(source);

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
        // The stream is the load's to close (SDL's closeio).
        SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
        MCSdlHandle<SDL_Surface> loaded(stream != nullptr ? SDL_LoadPNG_IO(stream, true) : nullptr);
        MCSdlHandle<SDL_Surface> rgba(loaded != nullptr ? SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32)
                                                        : nullptr);
        loaded.reset();

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
        rgba.reset();
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

namespace
{
    /// <summary>
    /// 1998's tile's outline: its size (a diagram's own block, a mech's or a vehicle's), a mech's locations and a
    /// vehicle's. 1998's tile drew a vehicle's block 11 pixels further right than a mech's, in a tile wider than the
    /// block; the outline is the block alone, so both are drawn at its left (at 11 a vehicle lost its right 11 columns).
    /// </summary>
    constexpr int32_t OutlineWidth = 0x19;
    constexpr int32_t OutlineHeight = 0x1e;
    constexpr int32_t MechLocations = 8;
    constexpr int32_t VehicleLocations = 5;
    /// <summary>The outline's ground (left transparent), and the colours the logistics screens show it in.</summary>
    constexpr uint8_t OutlineGround = 0xff;
    constexpr std::string_view LogisticsColors = "logart/lsrupm05";
    constexpr std::string_view MechKind = "mech";
    constexpr std::string_view VehicleKind = "vehicle";
    constexpr char Unhurt = '-';
    constexpr char NoLocation = 'x';
    constexpr char FirstFade = '0';

    char StateMark(std::optional<int32_t> state)
    {
        if (!state.has_value())
        {
            return NoLocation;
        }

        return *state < 0 ? Unhurt : static_cast<char>(FirstFade + *state);
    }

    /// <summary>
    /// The icon shapes the outlines are drawn from: logistics', or in a mission the art it kept from logistics
    /// (MCMission::KeptLogisticsArt: the results' salvage and best pilot); none without either.
    /// </summary>
    std::shared_ptr<const MCLogisticsArtFiles> IconShapes()
    {
        if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
        {
            return {logistics, logistics.get()};
        }

        if (const std::shared_ptr<MCMission> mission = Mission();
            mission != nullptr && mission->KeptLogisticsArt != nullptr)
        {
            return {mission->KeptLogisticsArt, &mission->KeptLogisticsArt->Files};
        }

        return nullptr;
    }

    /// <summary>The outline a <c>unit:</c> source names, drawn as 1998's tile draws it, in the logistics' colours.</summary>
    std::expected<MCRmlRecorder::MCLoaded, std::string> LoadUnit(std::string_view source)
    {
        const std::string_view rest = source.substr(MCRmlArt::UnitScheme.size());
        const size_t kindEnd = rest.find('/');
        const size_t indexEnd = kindEnd == std::string_view::npos ? kindEnd : rest.find('/', kindEnd + 1);
        const std::shared_ptr<const MCLogisticsArtFiles> logistics = IconShapes();

        if (indexEnd == std::string_view::npos || logistics == nullptr)
        {
            return std::unexpected(std::format("'{}' isn't a unit's outline the logistics can draw", source));
        }

        const std::string_view kind = rest.substr(0, kindEnd);
        const std::string_view number = rest.substr(kindEnd + 1, indexEnd - kindEnd - 1);
        const std::string_view states = rest.substr(indexEnd + 1);
        size_t index = 0;
        const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), index);
        const bool mech = kind == MechKind;
        const size_t shapes = mech ? logistics->MechIconShapes.size() : logistics->VehicleIconShapes.size();

        if (error != std::errc() || end != number.data() + number.size() || (!mech && kind != VehicleKind) ||
            index >= shapes || states.size() != static_cast<size_t>(mech ? MechLocations : VehicleLocations))
        {
            return std::unexpected(std::format("'{}' isn't a unit's outline the logistics can draw", source));
        }

        const std::span<const uint8_t> table =
            mech ? logistics->MechIconShapes[index].Bytes() : logistics->VehicleIconShapes[index].Bytes();
        const std::expected<std::array<SDL_Color, 256>, std::string> colors = MCRmlArt::Colors(LogisticsColors);

        if (table.empty() || !colors)
        {
            return std::unexpected(std::format("the outline {} can't be drawn: {}", source,
                                               colors ? std::string("its shapes aren't loaded") : colors.error()));
        }

        MCLogPort port;
        port.Init(OutlineWidth, OutlineHeight);
        VfxPaneWipe(port.Frame(), OutlineGround);

        for (size_t location = 0; location < states.size(); ++location)
        {
            const char mark = states[location];

            if (mark == NoLocation)
            {
                continue;
            }

            const int32_t state = mark == Unhurt ? -1 : mark - FirstFade;
            MCLogistics::DrawBodyLoc(table, static_cast<int32_t>(location), state, port, 0, 0);
        }

        const std::span<const uint8_t> pixels = port.Pixels();
        std::vector<uint8_t> rgba(pixels.size() * PixelBytes);

        for (size_t i = 0; i < pixels.size(); ++i)
        {
            if (pixels[i] != OutlineGround)
            {
                const SDL_Color& color = (*colors)[pixels[i]];
                rgba[i * PixelBytes] = color.r;
                rgba[i * PixelBytes + 1] = color.g;
                rgba[i * PixelBytes + 2] = color.b;
                rgba[i * PixelBytes + 3] = Opaque;
            }
        }

        return MCRmlRecorder::MCLoaded{
            .Texture = MCRmlRecorder::MakeTexture(OutlineWidth, OutlineHeight, std::move(rgba), false),
            .Width = OutlineWidth,
            .Height = OutlineHeight,
        };
    }
}

namespace MCRmlArt
{
    std::string UnitSource(const MCLogPart& part)
    {
        std::string states;

        if (part.PartType == 1)
        {
            const auto& mech = static_cast<const MCLogMech&>(part);

            for (int32_t location = 0; location < MechLocations; ++location)
            {
                states += StateMark(MCLogistics::MechBodyLocState(mech, location));
            }

            return std::format("{}{}/{}/{}", UnitScheme, MechKind, mech.NameIndex, states);
        }

        const auto& vehicle = static_cast<const MCLogVehicle&>(part);

        for (int32_t location = 0; location < VehicleLocations; ++location)
        {
            states += StateMark(MCLogistics::VehicleBodyLocState(vehicle, location));
        }

        return std::format("{}{}/{}/{}", UnitScheme, VehicleKind, vehicle.NameIndex, states);
    }

    std::string WholeUnitSource(bool mech, int32_t nameIndex)
    {
        const std::string states(static_cast<size_t>(mech ? MechLocations : VehicleLocations), StateMark(-1));
        return std::format("{}{}/{}/{}", UnitScheme, mech ? MechKind : VehicleKind, nameIndex, states);
    }

    std::string HdPath(std::string_view path)
    {
        const std::filesystem::path file(path);
        return (file.parent_path() / HdFolder / file.filename()).generic_string();
    }

    std::expected<MCRmlRecorder::MCLoaded, std::string> Load(std::string_view source, bool hd, const MCRmlFiles& files)
    {
        if (source.starts_with(UnitScheme))
        {
            return LoadUnit(source);
        }

        if (!source.starts_with(Scheme) && !source.starts_with(MapScheme) && !source.starts_with(TerrainScheme))
        {
            return LoadAppArt(source, hd, files);
        }

        const std::expected<MCArtPicture, std::string> picture = ReadArt(source);

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
        const std::expected<MCArtPicture, std::string> picture = ReadArt(std::string(Scheme) + std::string(name));

        if (!picture)
        {
            return std::unexpected(picture.error());
        }

        return picture->Colors;
    }
}
