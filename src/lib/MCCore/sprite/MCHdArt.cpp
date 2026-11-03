#include "stdafx.h"
#include "sprite/MCHdArt.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCInflate.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSdlHandle.h"
#include "sprite/MCBc7.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCShape.h"

#include <unordered_set>

int GHdArt = 0;

namespace
{
    /// <summary>
    /// The pack's folder in the user folder or the game's, its folders of mech part frames and of the other appearances'
    /// frames.
    /// </summary>
    constexpr std::string_view DefaultFolder = "HD";
    constexpr std::string_view MechFolder = "mechs";
    constexpr std::string_view ObjectFolder = "objects";
    constexpr std::string_view CraterFolder = "craters";
    constexpr std::string_view IconFolder = "icons";
    constexpr std::string_view TerrainFolder = "terrain";
    /// <summary>The ground tiles' relief maps (MCHdArt::FindTerrainRelief).</summary>
    constexpr std::string_view ReliefFolder = "relief";
    constexpr std::string_view OverlayFolder = "overlays";
    /// <summary>The pack's folder of shape tables loaded outside the sprite, crater and icon files, each named by its hash.</summary>
    constexpr std::string_view ShapeFolder = "shapes";
    /// <summary>The pack's folder of pictures (the menus' and logistics' art), each named by its hash.</summary>
    constexpr std::string_view PictureFolder = "menus";
    constexpr std::string_view PaletteFile = "palette.rgb";
    /// <summary>The pack's folder of movies' HD frames, and their extension (MCHdArt::FindMovie).</summary>
    constexpr std::string_view MovieFolder = "movies";
    constexpr std::string_view MovieExtension = ".ivf";
    /// <summary>
    /// The pack's settings (MCHdArt::WritePackFile): a FIT file, as prefs.cfg, with a block of the pack format and the
    /// game's sprite files it was made from.
    /// </summary>
    constexpr std::string_view PackFile = "pack.cfg";
    constexpr std::string_view PackBlock = "HDArtPack";
    constexpr std::string_view PackFormatKey = "Format";
    constexpr std::string_view PackSpritesKey = "Sprites";
    /// <summary>The pack format this code reads: a pack of a later one is left out whole.</summary>
    constexpr int32_t PackFormat = 1;
    /// <summary>How the sprite files are written in pack.cfg: <c>name=size</c>, separated by <c>;</c>.</summary>
    constexpr char SpriteFileSeparator = ';';
    constexpr char SpriteSizeSeparator = '=';
    /// <summary>An archive's extension: <c>X.hda</c> holds what folder <c>X</c> would (MCHdArt.h).</summary>
    constexpr std::string_view ArchiveExtension = ".hda";
    constexpr char ArchiveSeparator = '\\';

    /// <summary>An open archive, and the first folders of its entries' names (upper-cased).</summary>
    struct MCHdArchive
    {
        std::unique_ptr<MCFastFile> File;
        std::unordered_set<std::string> Folders;
    };

    std::string Upper(std::string_view text)
    {
        std::string upper(text);
        // Through unsigned char: a name from a pack may hold any byte, and toupper takes only those and EOF.
        std::ranges::transform(upper, upper.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return upper;
    }

    /// <summary>The 64-bit FNV-1a hash's start and prime (TerrainTileName, MCHdArt::PictureHash).</summary>
    constexpr uint64_t FnvOffset = 0xcbf29ce484222325ull;
    constexpr uint64_t FnvPrime = 0x100000001b3ull;
    /// <summary>Where a frame key's folder goes in its hash: above any frame number and in-between step.</summary>
    constexpr int FolderHashShift = 40;
    /// <summary>
    /// Where a numbered frame's key holds the step of its in-between (MCInBetween; 0: the frame itself), above the
    /// frame's number (the bits below).
    /// </summary>
    constexpr int InBetweenNameShift = 32;
    constexpr uint64_t FrameNumberMask = (uint64_t{1} << InBetweenNameShift) - 1;
    /// <summary>What separates an in-between's step from its frame's number in its name (<c>&lt;frame&gt;_&lt;step&gt;</c>).</summary>
    constexpr std::string_view InBetweenSeparator = "_";
    /// <summary>What a turn middle's name has in place of a step (sprite/MCInBetween.h's TurnStep, TurnMirroredStep).</summary>
    constexpr std::string_view TurnName = "turn";
    constexpr std::string_view TurnMirroredName = "turnm";
    /// <summary>What the palette index picture of a frame adds to its name.</summary>
    constexpr std::string_view IndexSuffix = "_idx";
    /// <summary>A frame file's extension (MCHdArt::EncodeHdf), its header, and what it may be at most.</summary>
    constexpr std::string_view HdfExtension = ".hdf";
    constexpr std::array<uint8_t, 4> HdfMagic = {'M', 'C', 'H', 'D'};
    constexpr uint32_t HdfVersion = 1;
    constexpr uint32_t HdfBc7 = 1;
    /// <summary>The colour format of a frame holding its blended pixels (MCHdArt::Frame::Blended).</summary>
    constexpr uint32_t HdfBc7Blended = 2;
    /// <summary>The colour format of a frame of BC5 blocks (MCHdArt::ColorFormat::Bc5: a relief map).</summary>
    constexpr uint32_t HdfBc5 = 3;
    constexpr size_t HdfHeaderBytes = 16;
    constexpr size_t MaxHdfBytes = 64u * 1024u * 1024u;
    using MCHdArt::HasIndex;
    /// <summary>Shapes attached between sweeps of those that are gone.</summary>
    constexpr size_t SweepInterval = 256;
    constexpr int32_t BytesPerPixel = 4;
    /// <summary>
    /// The largest scale a frame may have (packs are made at 3x or 4x), and the largest side its colour texture may have
    /// (SDL GPU guarantees 8192; the indices are a texture of their own, at the original's size).
    /// </summary>
    constexpr int32_t MaxScale = 8;
    constexpr int32_t MaxTextureSide = 8192;
    /// <summary>A PNG file's signature and IHDR chunk header, then the width and height (big-endian).</summary>
    constexpr std::array<uint8_t, 16> PngHeader = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
                                                   0,    0,   0,   13,  'I',  'H',  'D',  'R'};

    /// <summary>How a folder of the pack names its frames: by number (a mech part's), or by a hash in hex (the ground's).</summary>
    enum class Naming : uint8_t
    {
        Number,
        Hash,
    };

    /// <summary>A folder of the pack frames are looked up in, and whether it is a folder of frame files (not looked yet: none).</summary>
    struct FrameFolder
    {
        std::filesystem::path Path;
        Naming Names = Naming::Number;
        std::optional<bool> Loose;
        /// <summary>Whether its frames are objects', whose baked shadows are marked (MCHdArt::MarkBakedShadows).</summary>
        bool BakedShadows = false;
    };

    /// <summary>
    /// A frame: its folder (an index into State::Folders) and its name there (a number or a hash). Numbers, so the lookup
    /// each draw makes hashes two integers rather than comparing paths part by part.
    /// </summary>
    struct FrameKey
    {
        uint32_t Folder = 0;
        uint64_t Name = 0;

        bool operator==(const FrameKey&) const = default;
    };

    struct FrameKeyHash
    {
        size_t operator()(const FrameKey& key) const
        {
            return std::hash<uint64_t>{}(key.Name ^ (static_cast<uint64_t>(key.Folder) << FolderHashShift));
        }
    };

    /// <summary>
    /// A shape table the pack has frames for, what keeps it (its MCShape, or a crater manager's token: the table is that
    /// owner's while it lives), and their folder (an index into State::Folders).
    /// </summary>
    struct Sequence
    {
        std::weak_ptr<const void> Owner;
        uint32_t Folder = 0;
        /// <summary>Whether its frames keep their own edges (MCHdArt::KeepsFrameEdges).</summary>
        bool KeepsEdges = false;
    };

    /// <summary>A frame read (null: the pack has none, or it is unusable), and its place in State::Order.</summary>
    struct Cached
    {
        std::shared_ptr<const MCHdArt::Frame> Frame;
        std::list<FrameKey>::iterator InOrder;
        bool Looked = false;
    };

    struct State
    {
        /// <summary>The folder set (empty: the default), and whether the folder in use exists (unknown: not looked).</summary>
        std::filesystem::path Folder;
        std::optional<bool> FolderExists;
        std::unordered_map<const uint8_t*, Sequence> Sequences;
        size_t Attached = 0;
        /// <summary>The folders frames were looked up in, and each one's index there.</summary>
        std::vector<FrameFolder> Folders;
        std::map<std::filesystem::path, uint32_t> FolderIndices;
        std::unordered_map<FrameKey, Cached, FrameKeyHash> Frames;
        /// <summary>The keys of the frames held (those in Frames with one), least recently used first.</summary>
        std::list<FrameKey> Order;
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
        /// <summary>The folder found for the pack when none is set (the user folder's HD, else the game folder's).</summary>
        std::optional<std::filesystem::path> FoundFolder;
        /// <summary>The pack's pack.cfg, once read: whether its format is one this code reads, and its sprite files.</summary>
        bool PackFileLooked = false;
        bool PackFormatReadable = true;
        std::optional<std::string> PackSprites;
        /// <summary>This game's sprite files (MCHdArt::SetSpriteFiles; empty: not known yet).</summary>
        std::string SpriteFiles;
        /// <summary>The archives looked for, by the folder each stands for (null: none there).</summary>
        std::map<std::filesystem::path, std::shared_ptr<MCHdArchive>> Archives;
        /// <summary>The shape tables drawn with smooth edges (SetSmoothEdgeTables).</summary>
        std::unordered_set<const uint8_t*> SmoothTables;
        /// <summary>The shape frames redrawn as line art (SetLineArtFrames).</summary>
        std::set<std::pair<const uint8_t*, int32_t>> LineArtFrames;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>The pack sequence the shape table starting at <paramref name="table"/> is tied to, if its owner lives.</summary>
    const Sequence* AttachedSequence(const uint8_t* table)
    {
        const State& state = TheState();
        const auto found = state.Sequences.find(table);
        return found == state.Sequences.end() || found->second.Owner.expired() ? nullptr : &found->second;
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

    /// <summary>The archive standing for <paramref name="folder"/> (its <c>.hda</c>), opened once; null when none.</summary>
    MCHdArchive* ArchiveFor(const std::filesystem::path& folder)
    {
        auto [found, fresh] = TheState().Archives.try_emplace(folder);

        if (!fresh)
        {
            return found->second.get();
        }

        const std::filesystem::path path = std::filesystem::path(folder).concat(ArchiveExtension);
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return nullptr;
        }

        auto file = MCFastFile::Create(path.string());

        if (!file)
        {
            LogOnce(path, file.error());
            return nullptr;
        }

        auto archive = std::make_shared<MCHdArchive>();
        archive->File = std::move(*file);

        for (const MCFileEntry& entry : archive->File->GetEntries())
        {
            const std::string_view name = entry.GetName();
            archive->Folders.insert(Upper(name.substr(0, name.find(ArchiveSeparator))));
        }

        found->second = std::move(archive);
        return found->second.get();
    }

    /// <summary>
    /// The frame at <paramref name="key"/> (its path in the pack, without its extension) from the nearest archive standing
    /// for one of its folders (up to the pack's): its entry <c>&lt;the rest of the path&gt;.hdf</c>, inflated when packed;
    /// none when there is no such entry.
    /// </summary>
    std::expected<std::optional<MCHdArt::Frame>, std::string> ReadArchived(const std::filesystem::path& key,
                                                                           int32_t width, int32_t height)
    {
        const std::filesystem::path pack = MCHdArt::PackFolder();

        for (std::filesystem::path folder = key.parent_path(); folder.has_relative_path() && folder != pack;
             folder = folder.parent_path())
        {
            MCHdArchive* archive = ArchiveFor(folder);

            if (archive == nullptr)
            {
                continue;
            }

            std::string name = key.lexically_relative(folder).generic_string();
            std::ranges::replace(name, '/', ArchiveSeparator);
            const std::optional<int32_t> index = archive->File->Find(name + std::string(HdfExtension));

            if (!index.has_value())
            {
                return std::optional<MCHdArt::Frame>();
            }

            const MCFileEntry& entry = archive->File->GetEntries()[static_cast<size_t>(*index)];

            if (entry.RealSize < 0 || static_cast<size_t>(entry.RealSize) > MaxHdfBytes)
            {
                return std::unexpected(std::format("{} is too large", name));
            }

            std::vector<uint8_t> stored = archive->File->ReadStored(*index);

            if (entry.Size != entry.RealSize)
            {
                auto inflated = MCInflate::Inflate(stored, static_cast<size_t>(entry.RealSize));

                if (!inflated)
                {
                    return std::unexpected(std::format("{} doesn't unpack: {}", name, inflated.error()));
                }

                stored = std::move(*inflated);
            }

            auto frame = MCHdArt::DecodeHdf(stored, width, height);

            if (!frame)
            {
                return std::unexpected(frame.error());
            }

            return std::optional<MCHdArt::Frame>(std::move(*frame));
        }

        return std::optional<MCHdArt::Frame>();
    }

    /// <summary>Whether the pack has <paramref name="folder"/>'s frames: the folder, or an archive holding it.</summary>
    bool HasFolder(const std::filesystem::path& folder)
    {
        std::error_code error;

        if (std::filesystem::is_directory(folder, error))
        {
            return true;
        }

        // As ReadArchived looks: the nearest archive standing for one of the folder's (up to the pack's), holding the
        // first folder on the way down.
        const std::filesystem::path pack = MCHdArt::PackFolder();

        for (std::filesystem::path ancestor = folder.parent_path(); ancestor.has_relative_path() && ancestor != pack;
             ancestor = ancestor.parent_path())
        {
            if (const MCHdArchive* archive = ArchiveFor(ancestor); archive != nullptr)
            {
                const std::filesystem::path rest = folder.lexically_relative(ancestor);
                return !rest.empty() && archive->Folders.contains(Upper(rest.begin()->string()));
            }
        }

        return false;
    }

    /// <summary>The key of frame <paramref name="frame"/> of a numbered folder, or of its in-between <paramref name="step"/>.</summary>
    uint64_t NumberedKey(int32_t frame, int32_t step)
    {
        return static_cast<uint64_t>(static_cast<uint32_t>(frame)) | static_cast<uint64_t>(step) << InBetweenNameShift;
    }

    /// <summary>
    /// The name of a numbered folder's frame <paramref name="key"/> (<see cref="NumberedKey"/>): its number, and an
    /// in-between's step (a turn middle's name) after <see cref="InBetweenSeparator"/>.
    /// </summary>
    std::string NumberedName(uint64_t key)
    {
        const uint64_t step = key >> InBetweenNameShift;
        const std::string frame = std::to_string(key & FrameNumberMask);

        if (step == 0)
        {
            return frame;
        }

        const std::string stepName = step == TurnStep           ? std::string(TurnName)
                                     : step == TurnMirroredStep ? std::string(TurnMirroredName)
                                                                : std::to_string(step);
        return std::format("{}{}{}", frame, InBetweenSeparator, stepName);
    }

    /// <summary>The name a hash gives a frame: 16 hex digits.</summary>
    std::string HashName(uint64_t hash)
    {
        return std::format("{:016x}", hash);
    }

    /// <summary>The index of the folder at <paramref name="path"/> in State::Folders (added the first time).</summary>
    uint32_t FolderIndex(const std::filesystem::path& path, Naming names)
    {
        State& state = TheState();
        const auto [found, fresh] = state.FolderIndices.try_emplace(path, static_cast<uint32_t>(state.Folders.size()));

        if (fresh)
        {
            const std::filesystem::path inPack = path.lexically_relative(MCHdArt::PackFolder());
            const bool objects = !inPack.empty() && *inPack.begin() == ObjectFolder;
            state.Folders.push_back(FrameFolder{path, names, std::nullopt, objects});
        }

        return found->second;
    }

    /// <summary>
    /// A frame just read from <paramref name="folder"/>: numbered, its baked shadow marked if it is an object's, else
    /// whether the pack marked one (MCHdArt::Frame::PaintedShadow).
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> Finished(MCHdArt::Frame&& frame, const FrameFolder& folder)
    {
        auto made = std::make_shared<MCHdArt::Frame>(std::move(frame));
        made->Id = MCHdArt::NewFrameId();

        if (folder.BakedShadows)
        {
            MCHdArt::MarkBakedShadows(*made);
        }
        else
        {
            for (size_t at = 1; at < made->Indices.size() && !made->PaintedShadow; at += MCHdArt::IndexBytes)
            {
                made->PaintedShadow = made->Indices[at] == MCHdArt::BakedShadow;
            }
        }

        return made;
    }

    /// <summary>
    /// The frame <paramref name="key"/> for a shape of <paramref name="width"/> x <paramref name="height"/>: its
    /// <c>.hdf</c> file, else its PNG pair, else its entry in an archive; none when the pack has no such frame, or what
    /// is wrong with the one it has.
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> ReadFrame(const FrameKey& key, int32_t width,
                                                                                int32_t height)
    {
        FrameFolder& folder = TheState().Folders.at(key.Folder);
        std::error_code error;

        // Loose frame files lie in a folder; an archive's, in none (a pack of archives costs no file checks a frame).
        if (!folder.Loose.has_value())
        {
            folder.Loose = std::filesystem::is_directory(folder.Path, error);
        }

        const std::string frame = folder.Names == Naming::Hash ? HashName(key.Name) : NumberedName(key.Name);
        const std::filesystem::path path = folder.Path / frame;
        const bool loose = *folder.Loose;
        std::expected<MCHdArt::Frame, std::string> read = std::unexpected(std::string());

        if (const std::filesystem::path hdfPath = std::filesystem::path(path).concat(HdfExtension);
            loose && std::filesystem::is_regular_file(hdfPath, error))
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
            const std::filesystem::path colorPath = std::filesystem::path(path).concat(".png");

            if (!loose || !std::filesystem::is_regular_file(colorPath, error))
            {
                auto archived = ReadArchived(path, width, height);

                if (!archived)
                {
                    return std::unexpected(std::format("frame {}: {}", frame, archived.error()));
                }

                if (!archived->has_value())
                {
                    return nullptr;
                }

                return Finished(std::move(**archived), folder);
            }

            // Sizes are checked before decoding, so a malformed pack can't make the game decode a huge picture.
            const std::filesystem::path indexPath =
                std::filesystem::path(path).concat(std::format("{}.png", IndexSuffix));
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

        return Finished(std::move(*read), folder);
    }

    /// <summary>
    /// Drops the least recently used frames, but never the one used last (the frame just read), until the kept ones fit
    /// the budget.
    /// </summary>
    void Trim()
    {
        State& state = TheState();

        while (state.Bytes > MCHdArt::CacheBudget && state.Order.size() > 1)
        {
            const auto oldest = state.Frames.find(state.Order.front());
            state.Order.pop_front();

            if (oldest != state.Frames.end())
            {
                state.Bytes -= oldest->second.Frame->Bytes();
                state.Frames.erase(oldest);
            }
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
    made.Blended = !index->Pixels.empty() && index->Pixels[2] == HoldsBlended;

    // Premultiplied, so the GPU's blend and filtering need no division.
    for (size_t at = 0; !made.Blended && at < made.Colors.size(); at += BytesPerPixel)
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
            const uint8_t green = index->Pixels[from + 1];
            made.Indices[to + 1] = green == HasIndex || green == BakedShadow ? green : 0;
        }
    }

    return made;
}

namespace
{
    /// <summary>The frame <paramref name="key"/>, from the cache or read the first time it is asked for.</summary>
    std::shared_ptr<const MCHdArt::Frame> Cache(const FrameKey& key, int32_t width, int32_t height)
    {
        State& state = TheState();
        Cached& cached = state.Frames[key];

        if (cached.Frame != nullptr)
        {
            state.Order.splice(state.Order.end(), state.Order, cached.InOrder);
        }
        else if (!cached.Looked)
        {
            cached.Looked = true;
            const auto started = std::chrono::steady_clock::now();
            auto read = ReadFrame(key, width, height);
            state.ReadTime += std::chrono::steady_clock::now() - started;

            if (!read)
            {
                LogOnce(state.Folders.at(key.Folder).Path, read.error());
            }
            else if (*read != nullptr)
            {
                cached.Frame = std::move(*read);
                cached.InOrder = state.Order.insert(state.Order.end(), key);
                state.Bytes += cached.Frame->Bytes();
                ++state.Read;
                std::shared_ptr<const MCHdArt::Frame> kept = cached.Frame;
                Trim();
                return kept;
            }
        }

        return cached.Frame;
    }
}

std::vector<uint8_t> MCHdArt::EncodeHdf(const Frame& frame, const MCBc7::MCBc7Options& options)
{
    const std::vector<uint8_t> blocks = frame.Format != ColorFormat::Rgba8
                                            ? frame.Colors
                                            : MCBc7::Encode(frame.Colors, frame.Width, frame.Height, options);
    std::vector<uint8_t> bytes(HdfMagic.begin(), HdfMagic.end());
    WriteLittle(bytes, HdfVersion, 2);
    WriteLittle(bytes, frame.Format == ColorFormat::Bc5 ? HdfBc5 : (frame.Blended ? HdfBc7Blended : HdfBc7), 1);
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
        ReadLittle(bytes, 4, 2) != HdfVersion ||
        (ReadLittle(bytes, 6, 1) != HdfBc7 && ReadLittle(bytes, 6, 1) != HdfBc7Blended &&
         ReadLittle(bytes, 6, 1) != HdfBc5))
    {
        return std::unexpected(std::string("not a version 1 BC7 or BC5 HD frame"));
    }

    Frame frame;
    frame.Blended = ReadLittle(bytes, 6, 1) == HdfBc7Blended;
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
    // A relief map (BC5) carries no indices: its tile's colour frame has them.
    const bool relief = ReadLittle(bytes, 6, 1) == HdfBc5;
    const size_t indexBytes = relief ? 0 : static_cast<size_t>(originalWidth) * originalHeight * IndexBytes;

    if (!FitsShape(frame.Width, frame.Height, frame.Scale, originalWidth, originalHeight) ||
        colorBytes != blocks * MCBc7::BlockBytes || bytes.size() != HdfHeaderBytes + colorBytes + indexBytes)
    {
        return std::unexpected(std::format("a {}x{} frame at {}x with {} colour bytes in a {}-byte file", originalWidth,
                                           originalHeight, frame.Scale, colorBytes, bytes.size()));
    }

    frame.Format = relief ? ColorFormat::Bc5 : ColorFormat::Bc7;
    frame.Colors.assign(bytes.begin() + HdfHeaderBytes,
                        bytes.begin() + static_cast<ptrdiff_t>(HdfHeaderBytes + colorBytes));
    frame.Indices.assign(bytes.begin() + static_cast<ptrdiff_t>(HdfHeaderBytes + colorBytes), bytes.end());
    return frame;
}

void MCHdArt::MarkBakedShadows(Frame& frame)
{
    const int32_t width = frame.OriginalWidth();
    const int32_t height = frame.OriginalHeight();

    if (frame.Blended || width <= 0 || height <= 0 ||
        frame.Indices.size() != static_cast<size_t>(width) * height * IndexBytes)
    {
        return;
    }

    // Per original pixel, with a one-pixel empty border so every walk below stays inside: what the art draws there.
    enum class Drawn : uint8_t
    {
        Empty,
        Black,
        Body,
    };

    const int32_t paddedWidth = width + 2;
    const int32_t paddedHeight = height + 2;
    const auto at = [paddedWidth](int32_t x, int32_t y) { return static_cast<size_t>(y) * paddedWidth + x; };
    std::vector<Drawn> drawn(static_cast<size_t>(paddedWidth) * paddedHeight, Drawn::Empty);

    for (int32_t y = 0; y < height; ++y)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            const size_t from = (static_cast<size_t>(y) * width + x) * IndexBytes;

            if (frame.Indices[from + 1] == HasIndex)
            {
                drawn[at(x + 1, y + 1)] = frame.Indices[from] == BakedShadowIndex ? Drawn::Black : Drawn::Body;
            }
        }
    }

    // The body's outline, closed over one-pixel gaps (dilated, then eroded, by a 3 x 3 square).
    constexpr int32_t Reach = 1;
    const auto anyAround = [&](const std::vector<uint8_t>& mask, int32_t x, int32_t y, bool all)
    {
        for (int32_t dy = -Reach; dy <= Reach; ++dy)
        {
            for (int32_t dx = -Reach; dx <= Reach; ++dx)
            {
                const int32_t nx = x + dx;
                const int32_t ny = y + dy;
                const bool set = nx >= 0 && ny >= 0 && nx < paddedWidth && ny < paddedHeight && mask[at(nx, ny)] != 0;

                if (set != all)
                {
                    return !all;
                }
            }
        }

        return all;
    };

    std::vector<uint8_t> body(drawn.size());
    std::ranges::transform(drawn, body.begin(), [](Drawn pixel) { return pixel == Drawn::Body ? 1 : 0; });
    std::vector<uint8_t> grown(drawn.size());
    std::vector<uint8_t> closed(drawn.size());

    for (int32_t y = 0; y < paddedHeight; ++y)
    {
        for (int32_t x = 0; x < paddedWidth; ++x)
        {
            grown[at(x, y)] = anyAround(body, x, y, false) ? 1 : 0;
        }
    }

    for (int32_t y = 0; y < paddedHeight; ++y)
    {
        for (int32_t x = 0; x < paddedWidth; ++x)
        {
            closed[at(x, y)] = anyAround(grown, x, y, true) ? 1 : 0;
        }
    }

    // Holes filled: what the outside can't reach (4-connected) past the closed outline is inside it.
    constexpr std::array<std::pair<int32_t, int32_t>, 4> Sides = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    std::vector<uint8_t> outside(drawn.size());
    std::vector<std::pair<int32_t, int32_t>> todo = {{0, 0}};
    outside[at(0, 0)] = 1;

    while (!todo.empty())
    {
        const auto [x, y] = todo.back();
        todo.pop_back();

        for (const auto [dx, dy] : Sides)
        {
            const int32_t nx = x + dx;
            const int32_t ny = y + dy;

            if (nx >= 0 && ny >= 0 && nx < paddedWidth && ny < paddedHeight && outside[at(nx, ny)] == 0 &&
                closed[at(nx, ny)] == 0)
            {
                outside[at(nx, ny)] = 1;
                todo.emplace_back(nx, ny);
            }
        }
    }

    // The black outside the outline, in groups (8-connected): a group touching at least as many empty pixels as drawn
    // ones (the black inside the outline counts as drawn) is shadow.
    std::vector<uint8_t> seen(drawn.size());
    std::vector<std::pair<int32_t, int32_t>> group;
    const auto shadowBlack = [&](int32_t x, int32_t y)
    { return drawn[at(x, y)] == Drawn::Black && outside[at(x, y)] != 0; };

    for (int32_t y = 1; y <= height; ++y)
    {
        for (int32_t x = 1; x <= width; ++x)
        {
            if (seen[at(x, y)] != 0 || !shadowBlack(x, y))
            {
                continue;
            }

            group.assign({{x, y}});
            seen[at(x, y)] = 1;
            int32_t empty = 0;
            int32_t art = 0;

            for (size_t next = 0; next < group.size(); ++next)
            {
                const auto [gx, gy] = group[next];

                for (const auto [dx, dy] : Sides)
                {
                    const Drawn side = drawn[at(gx + dx, gy + dy)];
                    empty += side == Drawn::Empty ? 1 : 0;
                    art += side != Drawn::Empty && !shadowBlack(gx + dx, gy + dy) ? 1 : 0;
                }

                for (int32_t dy = -1; dy <= 1; ++dy)
                {
                    for (int32_t dx = -1; dx <= 1; ++dx)
                    {
                        if (seen[at(gx + dx, gy + dy)] == 0 && shadowBlack(gx + dx, gy + dy))
                        {
                            seen[at(gx + dx, gy + dy)] = 1;
                            group.emplace_back(gx + dx, gy + dy);
                        }
                    }
                }
            }

            if (empty == 0 || empty < art)
            {
                continue;
            }

            for (const auto [gx, gy] : group)
            {
                frame.Indices[(static_cast<size_t>(gy - 1) * width + (gx - 1)) * IndexBytes + 1] = BakedShadow;
            }
        }
    }
}

std::filesystem::path MCHdArt::PackFolder()
{
    State& state = TheState();

    if (!state.Folder.empty())
    {
        return state.Folder;
    }

    // The user folder's, else the game folder's (a pack unzipped into the game's folder, as mods are).
    if (!state.FoundFolder.has_value())
    {
        const std::filesystem::path user = MCFileSystem::UserRoot() / DefaultFolder;
        const std::filesystem::path game = MCFileSystem::GameRoot() / DefaultFolder;
        std::error_code error;
        state.FoundFolder =
            std::filesystem::is_directory(user, error) || !std::filesystem::is_directory(game, error) ? user : game;
    }

    return *state.FoundFolder;
}

void MCHdArt::SetPackFolder(const std::filesystem::path& folder)
{
    State& state = TheState();
    state.Folder = folder;
    state.FoundFolder.reset();
    state.FolderExists.reset();
    state.PackFileLooked = false;
    state.PackFormatReadable = true;
    state.PackSprites.reset();
    state.Sequences.clear();
    state.Frames.clear();
    state.Order.clear();
    state.Folders.clear();
    state.FolderIndices.clear();
    state.Bytes = 0;
    state.Logged.clear();
    state.MadeWith.reset();
    state.PaletteLooked = false;
    state.Archives.clear();
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

void MCHdArt::SetSmoothEdgeTables(std::span<const uint8_t* const> tables)
{
    TheState().SmoothTables = std::unordered_set<const uint8_t*>(tables.begin(), tables.end());
}

bool MCHdArt::SmoothEdges(const uint8_t* table)
{
    return GHdArt != 0 && TheState().SmoothTables.contains(table);
}

void MCHdArt::SetLineArtFrames(std::span<const LineArtFrame> frames)
{
    std::set<std::pair<const uint8_t*, int32_t>>& kept = TheState().LineArtFrames;
    kept.clear();

    for (const LineArtFrame& frame : frames)
    {
        kept.emplace(frame.Table, frame.Frame);
    }
}

bool MCHdArt::LineArt(const uint8_t* table, int32_t frame)
{
    return GHdArt != 0 && TheState().LineArtFrames.contains({table, frame});
}

uint64_t MCHdArt::PackVersion()
{
    return TheState().Version;
}

namespace
{
    /// <summary>The sizes <paramref name="text"/> (MCHdArt::SpriteFiles' form, <c>name=size;...</c>) gives, by name.</summary>
    std::map<std::string, std::string> SpriteSizes(std::string_view text)
    {
        std::map<std::string, std::string> sizes;

        for (const auto part : std::views::split(text, SpriteFileSeparator))
        {
            const std::string_view file(part.begin(), part.end());

            if (const size_t at = file.find(SpriteSizeSeparator); at != std::string_view::npos)
            {
                sizes.emplace(file.substr(0, at), file.substr(at + 1));
            }
        }

        return sizes;
    }

    /// <summary>
    /// Reads the pack's pack.cfg once: a pack without one is used whole; one of a later format, or that can't be read,
    /// not at all.
    /// </summary>
    void ReadPackFile()
    {
        State& state = TheState();

        if (state.PackFileLooked)
        {
            return;
        }

        state.PackFileLooked = true;
        const std::filesystem::path path = MCHdArt::PackFolder() / PackFile;
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return;
        }

        MCFitIniFile file;

        if (file.Open(path.string()) != 0 || file.SeekBlock(PackBlock) != 0)
        {
            state.PackFormatReadable = false;
            LogOnce(path, std::format("{} has no {} block", PackFile, PackBlock));
            return;
        }

        const MCFitResult<int32_t> format = file.Read<int32_t>(PackFormatKey);

        if (!format.has_value() || *format < 1 || *format > PackFormat)
        {
            state.PackFormatReadable = false;
            LogOnce(path, "the pack is for a later MechCommander Redux; it is left out");
            return;
        }

        if (MCFitResult<std::string> sprites = file.Read<std::string>(PackSpritesKey); sprites.has_value())
        {
            state.PackSprites = std::move(*sprites);
        }
    }
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

    if (!*state.FolderExists)
    {
        return false;
    }

    ReadPackFile();
    return state.PackFormatReadable;
}

bool MCHdArt::NumberedFolders()
{
    if (!Active())
    {
        return false;
    }

    State& state = TheState();

    if (!state.PackSprites.has_value() || state.SpriteFiles.empty())
    {
        return true;
    }

    // Only the files both opened count: the 90-pixel parts are opened only when the player's preference asks.
    const std::map<std::string, std::string> made = SpriteSizes(*state.PackSprites);
    const std::map<std::string, std::string> here = SpriteSizes(state.SpriteFiles);
    const bool same = std::ranges::all_of(made,
                                          [&here](const auto& file)
                                          {
                                              const auto found = here.find(file.first);
                                              return found == here.end() || found->second == file.second;
                                          });

    if (!same)
    {
        LogOnce(PackFolder() / PackFile,
                "the pack was made from another release of the game's sprites; its mechs and objects are left out");
    }

    return same;
}

void MCHdArt::SetSpriteFiles(std::span<const std::pair<std::string, uint32_t>> files)
{
    std::string text;

    for (const auto& [name, size] : files)
    {
        text += std::format("{}{}{}{}", text.empty() ? "" : std::string(1, SpriteFileSeparator), Upper(name),
                            SpriteSizeSeparator, size);
    }

    TheState().SpriteFiles = std::move(text);
}

const std::string& MCHdArt::SpriteFiles()
{
    return TheState().SpriteFiles;
}

std::expected<void, std::string> MCHdArt::WritePackFile(const std::filesystem::path& folder)
{
    const std::filesystem::path path = folder / PackFile;
    MCFitIniFile file;

    if (file.Create(path.string()) != 0)
    {
        return std::unexpected(std::format("{} can't be written", path.string()));
    }

    file.WriteBlock(PackBlock);
    file.WriteIdLong(PackFormatKey, PackFormat);
    file.WriteIdString(PackSpritesKey, SpriteFiles());
    file.Close();
    return {};
}

namespace
{
    /// <summary>
    /// Ties shape table <paramref name="table"/>, kept by <paramref name="owner"/>, to the pack's folder
    /// <paramref name="folder"/> (relative to the pack) when the pack has it and its numbered folders are drawn
    /// (MCHdArt::NumberedFolders: HD art is on, and the pack was made from this game's sprite files).
    /// </summary>
    void AttachTo(std::span<const uint8_t> table, const std::shared_ptr<const void>& owner,
                  const std::filesystem::path& folder, bool keepsEdges = false)
    {
        if (owner == nullptr || table.empty() || !MCHdArt::NumberedFolders())
        {
            return;
        }

        const std::filesystem::path path = MCHdArt::PackFolder() / folder;

        if (!HasFolder(path))
        {
            return;
        }

        State& state = TheState();

        // The cache drops shapes; their entries go now and then (a lookup checks that the owner still lives).
        if (++state.Attached % SweepInterval == 0)
        {
            std::erase_if(state.Sequences, [](const auto& entry) { return entry.second.Owner.expired(); });
        }

        state.Sequences[table.data()] = Sequence{owner, FolderIndex(path, Naming::Number), keepsEdges};
    }
}

std::filesystem::path MCHdArt::FolderOf(const MechShapeKey& key)
{
    return std::filesystem::path(MechFolder) / key.PartFile / std::to_string(key.MechNum) / std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::FolderOf(const ObjectShapeKey& key)
{
    return std::filesystem::path(ObjectFolder) / key.ShapeFile / std::to_string(key.Appearance) /
           std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::FolderOf(const CraterShapeKey& key)
{
    return std::filesystem::path(CraterFolder) / key.CraterFile / std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::FolderOf(const IconShapeKey& key)
{
    return std::filesystem::path(IconFolder) / key.File;
}

void MCHdArt::Attach(const std::shared_ptr<MCShape>& shape, const MechShapeKey& key)
{
    if (shape != nullptr)
    {
        AttachTo(shape->FrameList(), shape, FolderOf(key));
    }
}

void MCHdArt::Attach(const std::shared_ptr<MCShape>& shape, const ObjectShapeKey& key)
{
    if (shape != nullptr)
    {
        AttachTo(shape->FrameList(), shape, FolderOf(key));
    }
}

void MCHdArt::Prepare(const MechShapeKey& key)
{
    if (NumberedFolders())
    {
        // Finding the folder opens the archive standing for it, as Attach would.
        static_cast<void>(HasFolder(PackFolder() / FolderOf(key)));
    }
}

void MCHdArt::AttachFolder(const std::shared_ptr<MCShape>& shape)
{
    if (shape != nullptr && !shape->HdFolder.empty())
    {
        AttachTo(shape->FrameList(), shape, shape->HdFolder);
    }
}

void MCHdArt::Attach(std::span<const uint8_t> table, const std::shared_ptr<const void>& owner,
                     const CraterShapeKey& key)
{
    AttachTo(table, owner, FolderOf(key));
}

void MCHdArt::Attach(std::span<const uint8_t> table, const std::shared_ptr<const void>& owner, const IconShapeKey& key)
{
    // The interface's pixel art keeps the crisp edges its frames were made with.
    AttachTo(table, owner, FolderOf(key), true);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::Find(const uint8_t* table, int32_t frame, int32_t width, int32_t height,
                                                    int32_t step)
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

    // The bytes at the address may be another shape's now (its owner gone).
    if (found->second.Owner.expired())
    {
        state.Sequences.erase(found);
        return nullptr;
    }

    return Cache(FrameKey{found->second.Folder, NumberedKey(frame, step)}, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindInFolder(const std::filesystem::path& folder, int32_t frame,
                                                            int32_t width, int32_t height)
{
    if (!Active())
    {
        return nullptr;
    }

    return Cache(FrameKey{FolderIndex(PackFolder() / folder, Naming::Number), static_cast<uint64_t>(frame)}, width,
                 height);
}

bool MCHdArt::KeepsFrameEdges(const uint8_t* table)
{
    const State& state = TheState();
    const auto found = state.Sequences.find(table);
    return found != state.Sequences.end() && found->second.KeepsEdges;
}

bool MCHdArt::Attached(const uint8_t* table)
{
    return AttachedSequence(table) != nullptr;
}

std::optional<std::filesystem::path> MCHdArt::AttachedFolder(const uint8_t* table)
{
    const Sequence* sequence = AttachedSequence(table);

    if (sequence == nullptr)
    {
        return std::nullopt;
    }

    return TheState().Folders[sequence->Folder].Path;
}

uint64_t MCHdArt::PictureHash(std::span<const uint8_t> pixels, int32_t width, int32_t height)
{
    // FNV-1a over the size, then eight bytes at a time (the rest one at a time).
    uint64_t hash = FnvOffset;
    const auto mix = [&hash](uint64_t value)
    {
        hash ^= value;
        hash *= FnvPrime;
    };

    mix(static_cast<uint64_t>(static_cast<uint32_t>(width)));
    mix(static_cast<uint64_t>(static_cast<uint32_t>(height)));
    size_t at = 0;

    for (; at + sizeof(uint64_t) <= pixels.size(); at += sizeof(uint64_t))
    {
        uint64_t word = 0;
        std::memcpy(&word, pixels.data() + at, sizeof(word));
        mix(word);
    }

    for (; at < pixels.size(); ++at)
    {
        mix(pixels[at]);
    }

    return hash;
}

std::string MCHdArt::PictureName(uint64_t hash)
{
    return HashName(hash);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindPicture(std::span<const uint8_t> pixels, int32_t width,
                                                           int32_t height)
{
    if (!Active() || width <= 0 || height <= 0)
    {
        return nullptr;
    }

    return Cache(FrameKey{FolderIndex(PackFolder() / PictureFolder, Naming::Hash), PictureHash(pixels, width, height)},
                 width, height);
}

namespace
{
    /// <summary>The 64-bit FNV-1a hash of a terrain tile's data, which names it in the pack (TerrainTileName).</summary>
    uint64_t TerrainTileHash(std::span<const uint8_t> tile)
    {
        uint64_t hash = FnvOffset;

        for (const uint8_t byte : tile)
        {
            hash = (hash ^ byte) * FnvPrime;
        }

        return hash;
    }
}

std::string MCHdArt::TerrainTileName(std::span<const uint8_t> tile)
{
    return HashName(TerrainTileHash(tile));
}

namespace
{
    /// <summary>
    /// The frame of <paramref name="folder"/> named by its picture's data <paramref name="data"/> (a terrain tile's, an
    /// overlay's: TerrainTileHash), its bounds <paramref name="width"/> x <paramref name="height"/>; null with HD art off.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> FindHashed(std::string_view folder, std::span<const uint8_t> data,
                                                     int32_t width, int32_t height)
    {
        if (!MCHdArt::Active())
        {
            return nullptr;
        }

        return Cache(FrameKey{FolderIndex(MCHdArt::PackFolder() / folder, Naming::Hash), TerrainTileHash(data)}, width,
                     height);
    }
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainTile(std::span<const uint8_t> tile, int32_t width,
                                                               int32_t height)
{
    return FindHashed(TerrainFolder, tile, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainRelief(std::span<const uint8_t> tile, int32_t width,
                                                                 int32_t height)
{
    return FindHashed(ReliefFolder, tile, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindOverlay(std::span<const uint8_t> shape, int32_t width,
                                                           int32_t height)
{
    return FindHashed(OverlayFolder, shape, width, height);
}

uint64_t MCHdArt::ShapeTableHash(std::span<const uint8_t> table)
{
    return TerrainTileHash(table);
}

std::filesystem::path MCHdArt::ShapeTableFolder(uint64_t hash)
{
    return std::filesystem::path(ShapeFolder) / HashName(hash);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindShape(uint64_t hash, int32_t frame, int32_t width, int32_t height,
                                                         int32_t step)
{
    if (!Active())
    {
        return nullptr;
    }

    return Cache(FrameKey{FolderIndex(PackFolder() / ShapeTableFolder(hash), Naming::Number), NumberedKey(frame, step)},
                 width, height);
}

uint64_t MCHdArt::NewFrameId()
{
    return ++TheState().LastId;
}

std::optional<std::filesystem::path> MCHdArt::FindMovie(std::string_view movie)
{
    if (!Active())
    {
        return std::nullopt;
    }

    // The file's name without its folders (a game path may use either separator) or extension.
    const std::string_view file = movie.substr(movie.find_last_of("/\\") + 1);
    const std::string name = Upper(file.substr(0, file.find_last_of('.')));
    const std::filesystem::path path = PackFolder() / MovieFolder / (name + std::string(MovieExtension));
    std::error_code error;

    if (name.empty() || !std::filesystem::is_regular_file(path, error))
    {
        return std::nullopt;
    }

    return path;
}

size_t MCHdArt::FramesRead()
{
    return TheState().Read;
}

size_t MCHdArt::ArchivesOpen()
{
    return static_cast<size_t>(
        std::ranges::count_if(TheState().Archives, [](const auto& archive) { return archive.second != nullptr; }));
}

std::chrono::nanoseconds MCHdArt::ReadTime()
{
    return TheState().ReadTime;
}

size_t MCHdArt::CachedBytes()
{
    return TheState().Bytes;
}
