#include "stdafx.h"
#include "sprite/MCHdArt.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSdlHandle.h"
#include "sprite/MCShape.h"

int GHdArt = 0;

namespace
{
    /// <summary>The pack's folder in the user folder, and its folder of mech part frames.</summary>
    constexpr std::string_view DefaultFolder = "HD";
    constexpr std::string_view MechFolder = "mechs";
    /// <summary>What the palette index picture of a frame adds to its name.</summary>
    constexpr std::string_view IndexSuffix = "_idx";
    /// <summary>The index picture's green where a pixel has an index.</summary>
    constexpr uint8_t HasIndex = 255;
    /// <summary>Shapes attached between sweeps of those that are gone.</summary>
    constexpr size_t SweepInterval = 256;
    constexpr int32_t BytesPerPixel = 4;

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

    using FrameKey = std::pair<std::filesystem::path, int32_t>;

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

    /// <summary>
    /// Frame <paramref name="frame"/> of <paramref name="folder"/> for a shape of <paramref name="width"/> x
    /// <paramref name="height"/>; none when the pack has no such frame, or what is wrong with the one it has.
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> ReadFrame(const std::filesystem::path& folder,
                                                                                int32_t frame, int32_t width,
                                                                                int32_t height)
    {
        const std::filesystem::path colorPath = folder / std::format("{}.png", frame);
        std::error_code error;

        if (!std::filesystem::is_regular_file(colorPath, error))
        {
            return nullptr;
        }

        auto color = LoadRgba(colorPath);

        if (!color)
        {
            return std::unexpected(color.error());
        }

        auto index = LoadRgba(folder / std::format("{}{}.png", frame, IndexSuffix));

        if (!index)
        {
            return std::unexpected(index.error());
        }

        const int32_t scale = color->Width / std::max(width, 1);

        if (width <= 0 || height <= 0 || scale < 1 || color->Width != scale * width || color->Height != scale * height)
        {
            return std::unexpected(std::format("frame {} is {}x{}, not a whole multiple of the shape's {}x{}", frame,
                                               color->Width, color->Height, width, height));
        }

        if (index->Width != color->Width || index->Height != color->Height)
        {
            return std::unexpected(std::format("frame {}'s index picture is {}x{}, its colours {}x{}", frame,
                                               index->Width, index->Height, color->Width, color->Height));
        }

        auto made = std::make_shared<MCHdArt::Frame>();
        made->Id = ++TheState().LastId;
        made->Width = color->Width;
        made->Height = color->Height;
        made->Scale = scale;
        made->Pixels.resize(static_cast<size_t>(made->Width) * 2 * static_cast<size_t>(made->Height) * BytesPerPixel);
        const size_t row = static_cast<size_t>(made->Width) * BytesPerPixel;

        for (int32_t y = 0; y < made->Height; ++y)
        {
            const uint8_t* colors = color->Pixels.data() + row * static_cast<size_t>(y);
            const uint8_t* indices = index->Pixels.data() + row * static_cast<size_t>(y);
            uint8_t* out = made->Pixels.data() + row * 2 * static_cast<size_t>(y);

            for (int32_t x = 0; x < made->Width; ++x)
            {
                // Premultiplied, so the GPU's blend and filtering need no division.
                const uint32_t alpha = colors[x * BytesPerPixel + 3];

                for (int32_t channel = 0; channel < 3; ++channel)
                {
                    out[x * BytesPerPixel + channel] =
                        static_cast<uint8_t>((colors[x * BytesPerPixel + channel] * alpha + 127) / 255);
                }

                out[x * BytesPerPixel + 3] = static_cast<uint8_t>(alpha);
                uint8_t* indexOut = out + row + static_cast<size_t>(x) * BytesPerPixel;
                indexOut[0] = indices[x * BytesPerPixel];
                indexOut[1] = indices[x * BytesPerPixel + 1] == HasIndex ? HasIndex : 0;
                indexOut[2] = 0;
                indexOut[3] = HasIndex;
            }
        }

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

            state.Bytes -= oldest->second.Frame->Pixels.size();
            state.Frames.erase(oldest);
        }
    }
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

    const FrameKey key{found->second.Folder, frame};
    Cached& cached = state.Frames[key];
    cached.LastUse = ++state.Uses;

    if (!cached.Looked)
    {
        cached.Looked = true;
        const auto started = std::chrono::steady_clock::now();
        auto read = ReadFrame(key.first, frame, width, height);
        state.ReadTime += std::chrono::steady_clock::now() - started;

        if (!read)
        {
            LogOnce(key.first, read.error());
        }
        else if (*read != nullptr)
        {
            cached.Frame = std::move(*read);
            state.Bytes += cached.Frame->Pixels.size();
            ++state.Read;
            std::shared_ptr<const Frame> kept = cached.Frame;
            Trim(key);
            return kept;
        }
    }

    return cached.Frame;
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
