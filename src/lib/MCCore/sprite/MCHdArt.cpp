#include "stdafx.h"
#include "sprite/MCHdArt.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSdlHandle.h"
#include "sprite/MCBc7.h"
#include "sprite/MCShape.h"

int GHdArt = 0;

namespace
{
    /// <summary>The pack's folder in the user folder, and its folder of mech part frames.</summary>
    constexpr std::string_view DefaultFolder = "HD";
    constexpr std::string_view MechFolder = "mechs";
    constexpr std::string_view TerrainFolder = "terrain";
    constexpr std::string_view PaletteFile = "palette.rgb";
    /// <summary>The 64-bit FNV-1a hash's start and prime (TerrainTileName).</summary>
    constexpr uint64_t FnvOffset = 0xcbf29ce484222325ull;
    constexpr uint64_t FnvPrime = 0x100000001b3ull;
    /// <summary>What the palette index picture of a frame adds to its name.</summary>
    constexpr std::string_view IndexSuffix = "_idx";
    /// <summary>A frame file's extension (MCHdArt::EncodeHdf), its header, and what it may be at most.</summary>
    constexpr std::string_view HdfExtension = ".hdf";
    constexpr std::array<uint8_t, 4> HdfMagic = {'M', 'C', 'H', 'D'};
    constexpr uint32_t HdfVersion = 1;
    constexpr uint32_t HdfBc7 = 1;
    constexpr size_t HdfHeaderBytes = 16;
    constexpr size_t MaxHdfBytes = 64u * 1024u * 1024u;
    using MCHdArt::HasIndex;
    /// <summary>Shapes attached between sweeps of those that are gone.</summary>
    constexpr size_t SweepInterval = 256;
    constexpr int32_t BytesPerPixel = 4;
    /// <summary>
    /// The largest scale a frame may have (packs are made at 3x or 4x), and the largest side its GPU texture (colours
    /// and indices side by side, so twice as wide) may have: SDL GPU guarantees 8192.
    /// </summary>
    constexpr int32_t MaxScale = 8;
    constexpr int32_t MaxTextureSide = 8192;
    /// <summary>A PNG file's signature and IHDR chunk header, then the width and height (big-endian).</summary>
    constexpr std::array<uint8_t, 16> PngHeader = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
                                                   0,    0,   0,   13,  'I',  'H',  'D',  'R'};

    /// <summary>A shape the pack has frames for, and their folder.</summary>
    struct Sequence
    {
        std::weak_ptr<MCShape> Shape;
        std::filesystem::path Folder;
    };

    /// <summary>A frame read (null: the pack has none, or it is unusable), and when it was last asked for.</summary>
    struct Cached
    {
        std::shared_ptr<const MCHdArt::Frame> Frame;
        uint64_t LastUse = 0;
        bool Looked = false;
    };

    /// <summary>A frame's file in the pack, without its extension.</summary>
    using FrameKey = std::filesystem::path;

    struct State
    {
        /// <summary>The folder set (empty: the default), and whether the folder in use exists (unknown: not looked).</summary>
        std::filesystem::path Folder;
        std::optional<bool> FolderExists;
        std::unordered_map<const uint8_t*, Sequence> Sequences;
        size_t Attached = 0;
        std::map<FrameKey, Cached> Frames;
        uint64_t Uses = 0;
        uint64_t LastId = 0;
        size_t Bytes = 0;
        size_t Read = 0;
        std::chrono::nanoseconds ReadTime{0};
        /// <summary>The folders whose problems were logged (once each).</summary>
        std::set<std::filesystem::path> Logged;
        /// <summary>The pack's palette.rgb, once read (Looked), and the pack's version.</summary>
        std::optional<std::array<uint8_t, MCHdArt::PaletteBytes>> MadeWith;
        bool PaletteLooked = false;
        uint64_t Version = 1;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>Logs <paramref name="problem"/> with frames from <paramref name="folder"/>, once per folder.</summary>
    void LogOnce(const std::filesystem::path& folder, const std::string& problem)
    {
        if (TheState().Logged.insert(folder).second)
        {
            SDL_Log("HD art: %s (%s)", problem.c_str(), folder.string().c_str());
        }
    }

    /// <summary>An RGBA picture.</summary>
    struct Rgba
    {
        int32_t Width = 0;
        int32_t Height = 0;
        std::vector<uint8_t> Pixels;
    };

    /// <summary>The PNG at <paramref name="path"/> as RGBA (rows <c>4 * width</c> bytes), or what went wrong.</summary>
    std::expected<Rgba, std::string> LoadRgba(const std::filesystem::path& path)
    {
        const MCSdlHandle<SDL_Surface> loaded(SDL_LoadPNG(path.string().c_str()));

        if (loaded == nullptr)
        {
            return std::unexpected(std::format("{} can't be read: {}", path.filename().string(), SDL_GetError()));
        }

        const MCSdlHandle<SDL_Surface> rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));

        if (rgba == nullptr)
        {
            return std::unexpected(std::format("{} can't be converted: {}", path.filename().string(), SDL_GetError()));
        }

        Rgba image{rgba->w, rgba->h, {}};
        const size_t row = static_cast<size_t>(rgba->w) * BytesPerPixel;
        image.Pixels.resize(row * static_cast<size_t>(rgba->h));

        for (int32_t y = 0; y < rgba->h; ++y)
        {
            std::memcpy(image.Pixels.data() + row * static_cast<size_t>(y),
                        static_cast<const uint8_t*>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch, row);
        }

        return image;
    }

    /// <summary>The size a PNG file declares in its header, read before decoding it; none when it isn't a PNG.</summary>
    std::optional<std::pair<int32_t, int32_t>> PngSize(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::array<uint8_t, PngHeader.size() + 8> bytes{};

        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
            !std::equal(PngHeader.begin(), PngHeader.end(), bytes.begin()))
        {
            return std::nullopt;
        }

        const auto bigEndian = [&](size_t at)
        {
            return (static_cast<uint32_t>(bytes[at]) << 24) | (static_cast<uint32_t>(bytes[at + 1]) << 16) |
                   (static_cast<uint32_t>(bytes[at + 2]) << 8) | static_cast<uint32_t>(bytes[at + 3]);
        };

        const uint32_t width = bigEndian(PngHeader.size());
        const uint32_t height = bigEndian(PngHeader.size() + 4);

        if (width > static_cast<uint32_t>(INT32_MAX) || height > static_cast<uint32_t>(INT32_MAX))
        {
            return std::nullopt;
        }

        return std::pair{static_cast<int32_t>(width), static_cast<int32_t>(height)};
    }

    /// <summary>
    /// Whether an HD frame of <paramref name="hdWidth"/> x <paramref name="hdHeight"/> at <paramref name="scale"/> fits a
    /// shape of <paramref name="width"/> x <paramref name="height"/> and the GPU's textures.
    /// </summary>
    bool FitsShape(int32_t hdWidth, int32_t hdHeight, int32_t scale, int32_t width, int32_t height)
    {
        return width > 0 && height > 0 && scale >= 1 && scale <= MaxScale && hdWidth == scale * width &&
               hdHeight == scale * height && MCBc7::Padded(hdWidth) <= MaxTextureSide &&
               MCBc7::Padded(hdHeight) <= MaxTextureSide;
    }

    /// <summary>The little-endian value of <paramref name="size"/> bytes at <paramref name="at"/>.</summary>
    uint32_t ReadLittle(std::span<const uint8_t> bytes, size_t at, size_t size)
    {
        uint32_t value = 0;

        for (size_t i = 0; i < size; i++)
        {
            value |= static_cast<uint32_t>(bytes[at + i]) << (8 * i);
        }

        return value;
    }

    void WriteLittle(std::vector<uint8_t>& bytes, uint32_t value, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
    }

    /// <summary>The whole file at <paramref name="path"/>, up to <paramref name="limit"/> bytes, or what went wrong.</summary>
    std::expected<std::vector<uint8_t>, std::string> ReadFile(const std::filesystem::path& path, size_t limit)
    {
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);

        if (error || size > limit)
        {
            return std::unexpected(std::format("{} can't be read or is too large", path.filename().string()));
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        std::ifstream in(path, std::ios::binary);

        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            return std::unexpected(std::format("{} can't be read", path.filename().string()));
        }

        return bytes;
    }

    /// <summary>
    /// The frame at <paramref name="key"/> for a shape of <paramref name="width"/> x <paramref name="height"/>: its
    /// <c>.hdf</c> file, else its PNG pair; none when the pack has no such frame, or what is wrong with the one it has.
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> ReadFrame(const FrameKey& key, int32_t width,
                                                                                int32_t height)
    {
        std::error_code error;
        std::expected<MCHdArt::Frame, std::string> read = std::unexpected(std::string());
        const std::string frame = key.filename().string();

        if (const std::filesystem::path hdfPath = std::filesystem::path(key).concat(HdfExtension);
            std::filesystem::is_regular_file(hdfPath, error))
        {
            auto bytes = ReadFile(hdfPath, MaxHdfBytes);

            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }

            read = MCHdArt::DecodeHdf(*bytes, width, height);
        }
        else
        {
            const std::filesystem::path colorPath = std::filesystem::path(key).concat(".png");

            if (!std::filesystem::is_regular_file(colorPath, error))
            {
                return nullptr;
            }

            // Sizes are checked before decoding, so a malformed pack can't make the game decode a huge picture.
            const std::filesystem::path indexPath =
                std::filesystem::path(key).concat(std::format("{}.png", IndexSuffix));
            const std::optional<std::pair<int32_t, int32_t>> declared = PngSize(colorPath);

            if (!declared.has_value() || PngSize(indexPath) != declared)
            {
                return std::unexpected(std::format("frame {}'s pictures aren't two PNGs of one size", frame));
            }

            const auto [declaredWidth, declaredHeight] = *declared;
            const int32_t scale = declaredWidth / std::max(width, 1);

            if (!FitsShape(declaredWidth, declaredHeight, scale, width, height))
            {
                return std::unexpected(
                    std::format("frame {} is {}x{}: not 1 to {} times the shape's {}x{}, or too large", frame,
                                declaredWidth, declaredHeight, MaxScale, width, height));
            }

            read = MCHdArt::FrameFromPngs(colorPath, indexPath, scale);
        }

        if (!read)
        {
            return std::unexpected(std::format("frame {}: {}", frame, read.error()));
        }

        auto made = std::make_shared<MCHdArt::Frame>(std::move(*read));
        made->Id = ++TheState().LastId;
        return made;
    }

    /// <summary>
    /// Drops the least recently used frames, but never <paramref name="keep"/>, until the kept ones fit the budget.
    /// </summary>
    void Trim(const FrameKey& keep)
    {
        State& state = TheState();

        while (state.Bytes > MCHdArt::CacheBudget)
        {
            auto oldest = state.Frames.end();

            for (auto it = state.Frames.begin(); it != state.Frames.end(); ++it)
            {
                if (it->second.Frame != nullptr && it->first != keep &&
                    (oldest == state.Frames.end() || it->second.LastUse < oldest->second.LastUse))
                {
                    oldest = it;
                }
            }

            if (oldest == state.Frames.end())
            {
                return;
            }

            state.Bytes -= oldest->second.Frame->Bytes();
            state.Frames.erase(oldest);
        }
    }
}

auto MCHdArt::FrameFromPngs(const std::filesystem::path& colorPath, const std::filesystem::path& indexPath,
                            int32_t scale) -> std::expected<Frame, std::string>
{
    auto color = LoadRgba(colorPath);

    if (!color)
    {
        return std::unexpected(color.error());
    }

    auto index = LoadRgba(indexPath);

    if (!index)
    {
        return std::unexpected(index.error());
    }

    if (scale < 1 || scale > MaxScale || color->Width % scale != 0 || color->Height % scale != 0 ||
        index->Width != color->Width || index->Height != color->Height)
    {
        return std::unexpected(std::format("{} and {} are {}x{} and {}x{}: not one size, a whole {} times the original",
                                           colorPath.filename().string(), indexPath.filename().string(), color->Width,
                                           color->Height, index->Width, index->Height, scale));
    }

    Frame made;
    made.Width = color->Width;
    made.Height = color->Height;
    made.Scale = scale;
    made.Format = ColorFormat::Rgba8;
    made.Colors = std::move(color->Pixels);

    // Premultiplied, so the GPU's blend and filtering need no division.
    for (size_t at = 0; at < made.Colors.size(); at += BytesPerPixel)
    {
        const uint32_t alpha = made.Colors[at + 3];

        for (size_t channel = 0; channel < 3; ++channel)
        {
            made.Colors[at + channel] = static_cast<uint8_t>((made.Colors[at + channel] * alpha + 127) / 255);
        }
    }

    const int32_t width = made.OriginalWidth();
    const int32_t height = made.OriginalHeight();
    made.Indices.resize(static_cast<size_t>(width) * height * IndexBytes);

    for (int32_t y = 0; y < height; ++y)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            const size_t from =
                (static_cast<size_t>(y) * scale * index->Width + static_cast<size_t>(x) * scale) * BytesPerPixel;
            const size_t to = (static_cast<size_t>(y) * width + x) * IndexBytes;
            made.Indices[to] = index->Pixels[from];
            made.Indices[to + 1] = index->Pixels[from + 1] == HasIndex ? HasIndex : 0;
        }
    }

    return made;
}

namespace
{
    /// <summary>The frame at <paramref name="key"/>, from the cache or read the first time it is asked for.</summary>
    std::shared_ptr<const MCHdArt::Frame> Cache(const FrameKey& key, int32_t width, int32_t height)
    {
        State& state = TheState();
        Cached& cached = state.Frames[key];
        cached.LastUse = ++state.Uses;

        if (!cached.Looked)
        {
            cached.Looked = true;
            const auto started = std::chrono::steady_clock::now();
            auto read = ReadFrame(key, width, height);
            state.ReadTime += std::chrono::steady_clock::now() - started;

            if (!read)
            {
                LogOnce(key.parent_path(), read.error());
            }
            else if (*read != nullptr)
            {
                cached.Frame = std::move(*read);
                state.Bytes += cached.Frame->Bytes();
                ++state.Read;
                std::shared_ptr<const MCHdArt::Frame> kept = cached.Frame;
                Trim(key);
                return kept;
            }
        }

        return cached.Frame;
    }
}

std::vector<uint8_t> MCHdArt::EncodeHdf(const Frame& frame)
{
    const std::vector<uint8_t> blocks =
        frame.Format == ColorFormat::Bc7 ? frame.Colors : MCBc7::Encode(frame.Colors, frame.Width, frame.Height);
    std::vector<uint8_t> bytes(HdfMagic.begin(), HdfMagic.end());
    WriteLittle(bytes, HdfVersion, 2);
    WriteLittle(bytes, HdfBc7, 1);
    WriteLittle(bytes, static_cast<uint32_t>(frame.Scale), 1);
    WriteLittle(bytes, static_cast<uint32_t>(frame.OriginalWidth()), 2);
    WriteLittle(bytes, static_cast<uint32_t>(frame.OriginalHeight()), 2);
    WriteLittle(bytes, static_cast<uint32_t>(blocks.size()), 4);
    bytes.insert(bytes.end(), blocks.begin(), blocks.end());
    bytes.insert(bytes.end(), frame.Indices.begin(), frame.Indices.end());
    return bytes;
}

auto MCHdArt::DecodeHdf(std::span<const uint8_t> bytes, int32_t width, int32_t height)
    -> std::expected<Frame, std::string>
{
    if (bytes.size() < HdfHeaderBytes || !std::equal(HdfMagic.begin(), HdfMagic.end(), bytes.begin()) ||
        ReadLittle(bytes, 4, 2) != HdfVersion || ReadLittle(bytes, 6, 1) != HdfBc7)
    {
        return std::unexpected(std::string("not a version 1 BC7 HD frame"));
    }

    Frame frame;
    frame.Scale = static_cast<int32_t>(ReadLittle(bytes, 7, 1));
    const auto originalWidth = static_cast<int32_t>(ReadLittle(bytes, 8, 2));
    const auto originalHeight = static_cast<int32_t>(ReadLittle(bytes, 10, 2));
    const size_t colorBytes = ReadLittle(bytes, 12, 4);
    frame.Width = originalWidth * frame.Scale;
    frame.Height = originalHeight * frame.Scale;

    if ((width != 0 || height != 0) && (originalWidth != width || originalHeight != height))
    {
        return std::unexpected(
            std::format("made for a {}x{} shape, not {}x{}", originalWidth, originalHeight, width, height));
    }

    const size_t blocks = static_cast<size_t>(MCBc7::Padded(frame.Width) / MCBc7::BlockSide) *
                          static_cast<size_t>(MCBc7::Padded(frame.Height) / MCBc7::BlockSide);
    const size_t indexBytes = static_cast<size_t>(originalWidth) * originalHeight * IndexBytes;

    if (!FitsShape(frame.Width, frame.Height, frame.Scale, originalWidth, originalHeight) ||
        colorBytes != blocks * MCBc7::BlockBytes || bytes.size() != HdfHeaderBytes + colorBytes + indexBytes)
    {
        return std::unexpected(std::format("a {}x{} frame at {}x with {} colour bytes in a {}-byte file", originalWidth,
                                           originalHeight, frame.Scale, colorBytes, bytes.size()));
    }

    frame.Format = ColorFormat::Bc7;
    frame.Colors.assign(bytes.begin() + HdfHeaderBytes,
                        bytes.begin() + static_cast<ptrdiff_t>(HdfHeaderBytes + colorBytes));
    frame.Indices.assign(bytes.begin() + static_cast<ptrdiff_t>(HdfHeaderBytes + colorBytes), bytes.end());
    return frame;
}

std::filesystem::path MCHdArt::PackFolder()
{
    const State& state = TheState();
    return state.Folder.empty() ? MCFileSystem::UserRoot() / DefaultFolder : state.Folder;
}

void MCHdArt::SetPackFolder(const std::filesystem::path& folder)
{
    State& state = TheState();
    state.Folder = folder;
    state.FolderExists.reset();
    state.Sequences.clear();
    state.Frames.clear();
    state.Bytes = 0;
    state.Logged.clear();
    state.MadeWith.reset();
    state.PaletteLooked = false;
    ++state.Version;
}

const std::optional<std::array<uint8_t, MCHdArt::PaletteBytes>>& MCHdArt::MadeWith()
{
    State& state = TheState();

    if (!state.PaletteLooked)
    {
        state.PaletteLooked = true;
        const std::filesystem::path path = PackFolder() / PaletteFile;
        std::error_code error;

        if (std::filesystem::is_regular_file(path, error))
        {
            std::array<uint8_t, PaletteBytes> colors{};
            std::ifstream in(path, std::ios::binary);

            if (in.read(reinterpret_cast<char*>(colors.data()), static_cast<std::streamsize>(colors.size())) &&
                in.peek() == std::char_traits<char>::eof())
            {
                state.MadeWith = colors;
            }
            else
            {
                LogOnce(path, std::format("{} isn't {} bytes of colours", PaletteFile, PaletteBytes));
            }
        }
    }

    return state.MadeWith;
}

uint64_t MCHdArt::PackVersion()
{
    return TheState().Version;
}

bool MCHdArt::Active()
{
    if (GHdArt == 0)
    {
        return false;
    }

    State& state = TheState();

    if (!state.FolderExists.has_value())
    {
        std::error_code error;
        state.FolderExists = std::filesystem::is_directory(PackFolder(), error);
    }

    return *state.FolderExists;
}

void MCHdArt::Attach(const std::shared_ptr<MCShape>& shape, const MechShapeKey& key)
{
    if (shape == nullptr || !Active() || shape->FrameList().empty())
    {
        return;
    }

    const std::filesystem::path folder =
        PackFolder() / MechFolder / key.PartFile / std::to_string(key.MechNum) / std::to_string(key.Packet);
    std::error_code error;

    if (!std::filesystem::is_directory(folder, error))
    {
        return;
    }

    State& state = TheState();

    // The cache drops shapes; their entries go now and then (a lookup checks that the shape still lives).
    if (++state.Attached % SweepInterval == 0)
    {
        std::erase_if(state.Sequences, [](const auto& entry) { return entry.second.Shape.expired(); });
    }

    state.Sequences[shape->FrameList().data()] = Sequence{shape, folder};
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::Find(const uint8_t* table, int32_t frame, int32_t width, int32_t height)
{
    if (!Active())
    {
        return nullptr;
    }

    State& state = TheState();
    const auto found = state.Sequences.find(table);

    if (found == state.Sequences.end())
    {
        return nullptr;
    }

    // The bytes at the address may be another shape's now.
    const std::shared_ptr<MCShape> shape = found->second.Shape.lock();

    if (shape == nullptr || shape->FrameList().data() != table)
    {
        state.Sequences.erase(found);
        return nullptr;
    }

    return Cache(found->second.Folder / std::to_string(frame), width, height);
}

std::string MCHdArt::TerrainTileName(std::span<const uint8_t> tile)
{
    uint64_t hash = FnvOffset;

    for (const uint8_t byte : tile)
    {
        hash = (hash ^ byte) * FnvPrime;
    }

    return std::format("{:016x}", hash);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainTile(std::span<const uint8_t> tile, int32_t width,
                                                               int32_t height)
{
    if (!Active())
    {
        return nullptr;
    }

    return Cache(PackFolder() / TerrainFolder / TerrainTileName(tile), width, height);
}

size_t MCHdArt::FramesRead()
{
    return TheState().Read;
}

std::chrono::nanoseconds MCHdArt::ReadTime()
{
    return TheState().ReadTime;
}

size_t MCHdArt::CachedBytes()
{
    return TheState().Bytes;
}
