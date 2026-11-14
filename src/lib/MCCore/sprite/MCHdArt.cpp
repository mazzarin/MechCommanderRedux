#include "stdafx.h"
#include "sprite/MCHdArt.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCInflate.h"
#include "lib/MCOptionalRef.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCSdlHandle.h"
#include "sprite/MCBc7.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCShape.h"
#include "vfx/MCVfxFunctions.h"

#include <condition_variable>
#include <unordered_set>

int GHdArt = 0;

namespace
{
    /// <summary>The pack's folders of mech part frames and of the other appearances' frames.</summary>
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
    /// <summary>
    /// The most archive files kept open at once, the others keeping their directories and opening again when read: the
    /// pack has over a thousand archives (one for each appearance it draws) and none closes until the pack changes,
    /// while the Mac gives an app started from the Finder 256 open files and Windows' C runtime 512.
    /// </summary>
    constexpr size_t MaxOpenArchiveFiles = 128;
    /// <summary>
    /// Port-only (HD art): once one of a numbered folder's frames is drawn the first time, the reading thread reads the
    /// rest of its steps (its in-betweens, or its turn middle's), then this many frames on with theirs.
    /// </summary>
    constexpr int32_t FramesAhead = 1;
    /// <summary>
    /// The most first draws waiting for the reading thread to read after (the oldest left out past it); each hands it its
    /// own frame's follow and its neighbours' in a turn (up to four).
    /// </summary>
    constexpr size_t MaxFollows = 1024;
    /// <summary>The most bytes of frames read ahead and not taken yet (the oldest dropped past it).</summary>
    constexpr size_t MaxAheadBytes = 32u * 1024u * 1024u;
    /// <summary>
    /// Port-only: pictures are read ahead (MCHdArt::ReadPicturesAhead) while they and the frames kept would take at most
    /// the cache's budget over this: those read ahead are kept as drawn ones are, and mustn't push those out.
    /// </summary>
    constexpr size_t PicturesAheadBudgetShare = 2;
    /// <summary>A shape table's header up to its frame count (VfxShapeCount reads it at byte 4).</summary>
    constexpr size_t ShapeCountEnd = 8;
    /// <summary>The frame log's part for a frame's first use (read then, or taken from the reading thread), and its count.</summary>
    constexpr std::string_view FrameLogRead = "hd.read";
    constexpr std::string_view FrameLogReads = "HD frames first used";

    /// <summary>An open archive, and the first folders of its entries' names (upper-cased).</summary>
    struct MCHdArchive
    {
        /// <summary>Shared, so the files kept open (KeepOpen) can tell an archive that's gone.</summary>
        std::shared_ptr<MCFastFile> File;
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

        /// <summary>An archive holding a folder's frames, and the start of their entries' names in it.</summary>
        struct InArchive
        {
            std::shared_ptr<MCHdArchive> Archive;
            std::string Prefix;
        };

        /// <summary>The archive its frames are in (found once: ArchiveOf; none there: null).</summary>
        std::optional<InArchive> Archived;
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

    /// <summary>
    /// A frame read (null: the pack has none, it is unusable, or it was dropped), its place in State::Order, and its Id,
    /// kept when it is dropped so that it is read again with the same one (the GPU's copy stays good for it). Port-only:
    /// the frame (State::Frame) it was last asked for in, which MCHdArt::NextFrame never lets it go in or after.
    /// </summary>
    struct Cached
    {
        std::shared_ptr<const MCHdArt::Frame> Frame;
        std::list<FrameKey>::iterator InOrder;
        bool Looked = false;
        uint64_t Id = 0;
        /// <summary>
        /// Port-only: kept as read ahead (MCHdArt::KeepReadAhead) and not drawn yet: its first draw checks it against the
        /// size it asks for and hands the reading thread the frames drawn after it, as a first draw that reads it does.
        /// </summary>
        bool Undrawn = false;
        uint64_t LastFrame = 0;
    };

    /// <summary>
    /// A frame for the reading thread to read: its name, its entry in the follow's archive and that's name, and the size
    /// in original pixels it must have (0 x 0: any; port-only, a picture read ahead's).
    /// </summary>
    struct AheadEntry
    {
        uint64_t Name = 0;
        int32_t Index = 0;
        std::string Entry;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// Port-only (HD art): a numbered folder's frame drawn the first time, whose next ones the reading thread reads: its
    /// key, its shape table's frame count, and copies of what its folder says (the folder may go while it reads): its
    /// archive, its entries' start there, and whether its frames' baked shadows are marked.
    /// </summary>
    struct Follow
    {
        FrameKey Key;
        int32_t Frames = 0;
        std::shared_ptr<MCHdArchive> Archive;
        std::string Prefix;
        bool BakedShadows = false;
        /// <summary>
        /// Port-only: the frames to read, when given (pictures read ahead: MCHdArt::ReadPicturesAhead, Key's folder's),
        /// in place of those drawn after Key's.
        /// </summary>
        std::vector<AheadEntry> Entries;
        /// <summary>
        /// Port-only: whether to read Key's own frame (its steps from the first), not those drawn after it: a neighbour's
        /// in a turn (MCHdArt::SetNeighbours).
        /// </summary>
        bool Itself = false;
    };

    /// <summary>A frame read ahead (numbered when the game's thread takes it), and its place in ReadAhead::Order.</summary>
    struct ReadyFrame
    {
        std::shared_ptr<MCHdArt::Frame> Frame;
        std::list<FrameKey>::iterator InOrder;
    };

    /// <summary>
    /// Port-only (HD art): what the game's thread and the reading thread share, under Lock. The game's thread hands it
    /// each numbered frame it draws the first time (Follows) and takes what it read when a frame is first drawn; the
    /// reading thread reads the frames drawn after each, those the game's thread hasn't looked at (Known).
    /// </summary>
    struct ReadAhead
    {
        std::mutex Lock;
        /// <summary>Told of every frame read ahead, follow ended and change of what was found.</summary>
        std::condition_variable_any Changed;
        std::deque<Follow> Follows;
        /// <summary>
        /// The frames the game's thread has looked at (kept, missing from the pack, unusable, or being read by it), which
        /// the reading thread leaves alone.
        /// </summary>
        std::unordered_set<FrameKey, FrameKeyHash> Known;
        /// <summary>The frame the reading thread reads now: the game's thread waits for it rather than read it too.</summary>
        std::optional<FrameKey> Reading;
        /// <summary>The frames read ahead, the oldest first in Order, and their bytes.</summary>
        std::unordered_map<FrameKey, ReadyFrame, FrameKeyHash> Ready;
        std::list<FrameKey> Order;
        size_t Bytes = 0;
        /// <summary>
        /// Port-only: the pictures read ahead (Follow::Entries), kept apart from Ready (MCHdArt::ReadPicturesAhead
        /// bounds them, not MaxAheadBytes) until the game's thread keeps them (MCHdArt::KeepReadAhead) or a draw
        /// takes one; and those asked for and not read yet.
        /// </summary>
        std::unordered_map<FrameKey, std::shared_ptr<MCHdArt::Frame>, FrameKeyHash> Pictures;
        std::unordered_set<FrameKey, FrameKeyHash> PicturesAsked;
        /// <summary>Counts the times what was found was forgotten (the pack set): a read begun before is dropped.</summary>
        uint64_t Generation = 0;
        /// <summary>Set while the reading thread works on a follow.</summary>
        bool Working = false;
        /// <summary>Port-only: how many frames it has read as a neighbour's in a turn (Follow::Itself).</summary>
        size_t NeighbourReads = 0;
        /// <summary>
        /// Set while the reading thread keeps its handle on the archive it read last for the follows after (ReaderFile);
        /// ForgetAhead waits until it's closed, as the pack's files may go once another is set.
        /// </summary>
        bool ReaderKeeps = false;
        /// <summary>The reading thread, started with the first follow; last, so it stops before the rest goes.</summary>
        std::jthread Reader;
    };

    struct State
    {
        /// <summary>The folder set (empty: the default), and whether the folder in use exists (unknown: not looked).</summary>
        std::filesystem::path Folder;
        std::optional<bool> FolderExists;
        std::unordered_map<MCAddressKey, Sequence> Sequences;
        size_t Attached = 0;
        /// <summary>The folders frames were looked up in, and each one's index there.</summary>
        std::vector<FrameFolder> Folders;
        std::map<std::filesystem::path, uint32_t> FolderIndices;
        /// <summary>
        /// FindShape's folders' indices by their shape table's hash: making a folder's path and finding it among the
        /// folders' cost more, every draw, than finding its frame.
        /// </summary>
        std::unordered_map<uint64_t, uint32_t> ShapeTableFolders;
        std::unordered_map<FrameKey, Cached, FrameKeyHash> Frames;
        /// <summary>The keys of the frames held (those in Frames with one), least recently used first.</summary>
        std::list<FrameKey> Order;
        uint64_t LastId = 0;
        size_t Bytes = 0;
        /// <summary>The budget set (0: none, MCMemoryRoom::MostHdBytes).</summary>
        size_t Budget = 0;
        /// <summary>Port-only: the frames begun (MCHdArt::NextFrame).</summary>
        uint64_t Frame = 0;
        size_t Read = 0;
        /// <summary>How many of those the reading thread read (ReadAhead).</summary>
        size_t TakenAhead = 0;
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
        /// <summary>
        /// Guards the archives' files kept open (MCFastFile's reads, Release and IsOpen) and the four below: the reading
        /// thread reads through a handle of its own (a file's stream can't be read from two threads), so it holds this
        /// only to count that one, which may close one of these. Never held with ReadAhead::Lock.
        /// </summary>
        std::mutex ArchiveFiles;
        /// <summary>The reading thread's own handles open (0 or 1: ReaderFile), counted against MaxOpenArchiveFiles.</summary>
        size_t ReaderFiles = 0;
        /// <summary>How many times the reading thread has opened its own handle (MCHdArt::ReaderFilesOpened).</summary>
        size_t ReaderOpens = 0;
        /// <summary>
        /// The archives' files kept open, the most recently read first (at most MaxOpenArchiveFiles), and each one's
        /// place there.
        /// </summary>
        std::list<std::weak_ptr<MCFastFile>> OpenFiles;
        std::map<std::weak_ptr<MCFastFile>, std::list<std::weak_ptr<MCFastFile>>::iterator, std::owner_less<>> OpenAt;
        /// <summary>The shape tables drawn with smooth edges (SetSmoothEdgeTables).</summary>
        std::unordered_set<MCAddressKey> SmoothTables;
        /// <summary>The shape frames redrawn as line art (SetLineArtFrames), and their parts drawn by hand.</summary>
        std::map<std::pair<MCAddressKey, int32_t>, std::vector<MCSharpLines::MCDrawnLoop>> LineArtFrames;
        /// <summary>The pictures the game made of the pack's (MCHdArt::KeepPicture), by their pixels' hash.</summary>
        std::unordered_map<uint64_t, std::shared_ptr<const MCHdArt::Frame>> KeptPictures;
        /// <summary>
        /// Port-only: a folder's neighbours in a turn (MCHdArt::SetNeighbours), nearest first, by their indices in
        /// Folders, whose frames the reading thread reads when one of its own is drawn the first time.
        /// </summary>
        std::unordered_map<uint32_t, std::vector<uint32_t>> Neighbours;
        /// <summary>The frames read ahead; last, so its thread stops before the rest it reads goes.</summary>
        ReadAhead Ahead;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>The pack sequence the shape table starting at <paramref name="table"/> is tied to, if its owner lives.</summary>
    MCOptionalRef<const Sequence> AttachedSequence(std::span<const uint8_t> table)
    {
        const State& state = TheState();
        const auto found = state.Sequences.find(MCAddressKey::OfData(table));
        return found == state.Sequences.end() || found->second.Owner.expired()
                   ? std::nullopt
                   : MCOptionalRef<const Sequence>(found->second);
    }

    /// <summary>Logs <paramref name="problem"/> with frames from <paramref name="folder"/>, once per folder.</summary>
    void LogOnce(const std::filesystem::path& folder, const std::string& problem)
    {
        if (TheState().Logged.insert(folder).second)
        {
            SDL_Log("HD art: %s (%s)", problem.c_str(), MCFileSystem::ToUtf8(folder).c_str());
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
        const MCSdlHandle<SDL_Surface> loaded(SDL_LoadPNG(MCFileSystem::ToUtf8(path).c_str()));

        if (loaded == nullptr)
        {
            return std::unexpected(
                std::format("{} can't be read: {}", MCFileSystem::ToUtf8(path.filename()), SDL_GetError()));
        }

        const MCSdlHandle<SDL_Surface> rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));

        if (rgba == nullptr)
        {
            return std::unexpected(
                std::format("{} can't be converted: {}", MCFileSystem::ToUtf8(path.filename()), SDL_GetError()));
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
            return std::unexpected(
                std::format("{} can't be read or is too large", MCFileSystem::ToUtf8(path.filename())));
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        std::ifstream in(path, std::ios::binary);

        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            return std::unexpected(std::format("{} can't be read", MCFileSystem::ToUtf8(path.filename())));
        }

        return bytes;
    }

    /// <summary>
    /// Closes the least recently read of the archives' files kept open past MaxOpenArchiveFiles, the reading thread's own
    /// counted among them. Call with State::ArchiveFiles held.
    /// </summary>
    void TrimOpenFiles(State& state)
    {
        while (!state.OpenFiles.empty() && state.OpenFiles.size() + state.ReaderFiles > MaxOpenArchiveFiles)
        {
            const std::weak_ptr<MCFastFile> oldest = state.OpenFiles.back();

            if (const std::shared_ptr<MCFastFile> kept = oldest.lock())
            {
                kept->Release();
            }

            state.OpenAt.erase(oldest);
            state.OpenFiles.pop_back();
        }
    }

    /// <summary>
    /// Notes that <paramref name="file"/> was just opened or read, closing the least recently read of the archives' files
    /// past MaxOpenArchiveFiles. Call with State::ArchiveFiles held.
    /// </summary>
    void KeepOpen(const std::shared_ptr<MCFastFile>& file)
    {
        State& state = TheState();

        if (const auto found = state.OpenAt.find(file); found != state.OpenAt.end())
        {
            state.OpenFiles.splice(state.OpenFiles.begin(), state.OpenFiles, found->second);
        }
        else
        {
            state.OpenFiles.push_front(file);
            state.OpenAt.emplace(file, state.OpenFiles.begin());
        }

        TrimOpenFiles(state);
    }

    /// <summary>The archive standing for <paramref name="folder"/> (its <c>.hda</c>), opened once; null when none.</summary>
    std::shared_ptr<MCHdArchive> ArchiveFor(const std::filesystem::path& folder)
    {
        auto [found, fresh] = TheState().Archives.try_emplace(folder);

        if (!fresh)
        {
            return found->second;
        }

        const std::filesystem::path path = std::filesystem::path(folder).concat(ArchiveExtension);
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return nullptr;
        }

        auto file = MCFastFile::Create(MCFileSystem::ToUtf8(path));

        if (!file)
        {
            LogOnce(path, file.error());
            return nullptr;
        }

        auto archive = std::make_shared<MCHdArchive>();
        archive->File = std::move(*file);

        {
            std::scoped_lock lock(TheState().ArchiveFiles);
            KeepOpen(archive->File);
        }

        for (const MCFileEntry& entry : archive->File->GetEntries())
        {
            const std::string_view name = entry.GetName();
            archive->Folders.insert(Upper(name.substr(0, name.find(ArchiveSeparator))));
        }

        found->second = std::move(archive);
        return found->second;
    }

    /// <summary>
    /// The archive standing for the nearest of <paramref name="folder"/>'s folders (up to the pack's) that has one, and
    /// the start of its frames' entries' names there (the rest of the folder's path), found the first time: frames are
    /// read without walking the folders' paths.
    /// </summary>
    const FrameFolder::InArchive& ArchiveOf(FrameFolder& folder)
    {
        if (folder.Archived.has_value())
        {
            return *folder.Archived;
        }

        FrameFolder::InArchive& found = folder.Archived.emplace();
        const std::filesystem::path pack = MCHdArt::PackFolder();

        for (std::filesystem::path at = folder.Path; at.has_relative_path() && at != pack; at = at.parent_path())
        {
            if (std::shared_ptr<MCHdArchive> archive = ArchiveFor(at); archive != nullptr)
            {
                found.Archive = std::move(archive);

                if (at != folder.Path)
                {
                    // In UTF-8 with the system's separators ('\' on Windows already), then the archive's.
                    found.Prefix = MCFileSystem::ToUtf8(folder.Path.lexically_relative(at));
                    std::ranges::replace(found.Prefix, '/', ArchiveSeparator);
                    found.Prefix += ArchiveSeparator;
                }

                break;
            }
        }

        return found;
    }

    /// <summary>
    /// Entry <paramref name="index"/> of <paramref name="archive"/> (named <paramref name="name"/>, for what is wrong),
    /// a <c>.hdf</c> file, inflated when packed, as a frame for a shape of <paramref name="width"/> x
    /// <paramref name="height"/> (0 x 0: any): read through the reading thread's handle <paramref name="own"/>, else the
    /// archive's own, under State::ArchiveFiles.
    /// </summary>
    std::expected<MCHdArt::Frame, std::string> ReadEntry(const MCHdArchive& archive, int32_t index,
                                                         std::string_view name, int32_t width, int32_t height,
                                                         MCOptionalRef<MCDiskFile> own = std::nullopt)
    {
        const MCFileEntry& entry = archive.File->GetEntries()[static_cast<size_t>(index)];

        if (entry.RealSize < 0 || static_cast<size_t>(entry.RealSize) > MaxHdfBytes)
        {
            return std::unexpected(std::format("{} is too large", name));
        }

        std::vector<uint8_t> stored;

        if (own)
        {
            stored = archive.File->ReadStored(index, *own);
        }
        else
        {
            std::scoped_lock lock(TheState().ArchiveFiles);
            stored = archive.File->ReadStored(index);
            KeepOpen(archive.File);
        }

        if (stored.size() != static_cast<size_t>(entry.Size))
        {
            return std::unexpected(std::format("{} can't be read", name));
        }

        if (entry.Size != entry.RealSize)
        {
            auto inflated = MCInflate::Inflate(stored, static_cast<size_t>(entry.RealSize));

            if (!inflated)
            {
                return std::unexpected(std::format("{} doesn't unpack: {}", name, inflated.error()));
            }

            stored = std::move(*inflated);
        }

        return MCHdArt::DecodeHdf(stored, width, height);
    }

    /// <summary>
    /// The frame <paramref name="frame"/> of <paramref name="folder"/> from its archive (ArchiveOf): its entry
    /// <c>&lt;the rest of the folder's path&gt;\&lt;frame&gt;.hdf</c>, inflated when packed; none when there is no such
    /// entry.
    /// </summary>
    std::expected<std::optional<MCHdArt::Frame>, std::string> ReadArchived(FrameFolder& folder, std::string_view frame,
                                                                           int32_t width, int32_t height)
    {
        const FrameFolder::InArchive& archived = ArchiveOf(folder);
        const std::shared_ptr<MCHdArchive>& archive = archived.Archive;

        if (archive != nullptr)
        {
            const std::string name = archived.Prefix + std::string(frame);
            const std::optional<int32_t> index = archive->File->Find(name + std::string(HdfExtension));

            if (!index.has_value())
            {
                return std::optional<MCHdArt::Frame>();
            }

            auto decoded = ReadEntry(*archive, *index, name, width, height);

            if (!decoded)
            {
                return std::unexpected(decoded.error());
            }

            return std::optional<MCHdArt::Frame>(std::move(*decoded));
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
            if (const std::shared_ptr<MCHdArchive> archive = ArchiveFor(ancestor); archive != nullptr)
            {
                const std::filesystem::path rest = folder.lexically_relative(ancestor);
                return !rest.empty() && archive->Folders.contains(Upper(MCFileSystem::ToUtf8(*rest.begin())));
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
    /// The steps a numbered frame's step goes with (sprite/MCInBetween.h): the frame's own and its in-betweens toward
    /// the next frame (an effect's, a walk's), or (HDTurns) a legs frame's turn middle and its in-betweens. Its members
    /// are numbered from the frame's own (or the turn middle's), 0.
    /// </summary>
    struct StepFamily
    {
        /// <summary>The turn middle's step (TurnStep or TurnMirroredStep); 0: the frame's own.</summary>
        int32_t Turn = 0;
        /// <summary>The member a step is, and how many members there may be.</summary>
        int32_t Member = 0;
        int32_t Members = MaxInBetweenSteps;

        /// <summary>The step that names member <paramref name="member"/>.</summary>
        int32_t StepOf(int32_t member) const { return Turn == 0 ? member : TurnBetweenStep(Turn, member); }
    };

    /// <summary>The family <paramref name="step"/> (a numbered frame key's, NumberedKey) is of, and its member.</summary>
    StepFamily FamilyOf(uint64_t step)
    {
        if (step < static_cast<uint64_t>(TurnStep))
        {
            return {0, static_cast<int32_t>(step), MaxInBetweenSteps};
        }

        constexpr uint64_t BetweenMask = (uint64_t{1} << TurnBetweenShift) - 1;
        return {static_cast<int32_t>(step & ~(BetweenMask << TurnBetweenShift)),
                static_cast<int32_t>(step >> TurnBetweenShift & BetweenMask), LegsInBetweenSteps};
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

        // A turn middle, or (HD art's walk in-betweens) a turn middle's in-between toward the next frame's.
        if (const StepFamily family = FamilyOf(step); family.Turn != 0)
        {
            const std::string_view turnName = family.Turn == TurnMirroredStep ? TurnMirroredName : TurnName;
            return family.Member == 0 ? std::format("{}{}{}", frame, InBetweenSeparator, turnName)
                                      : std::format("{}{}{}{}{}", frame, InBetweenSeparator, turnName,
                                                    InBetweenSeparator, family.Member);
        }

        return std::format("{}{}{}", frame, InBetweenSeparator, step);
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
    /// A frame just read: numbered <paramref name="id"/>, its baked shadow marked if it is an object's (its folder's
    /// <paramref name="bakedShadows"/>), else whether the pack marked one (MCHdArt::Frame::PaintedShadow).
    /// </summary>
    std::shared_ptr<MCHdArt::Frame> Finished(MCHdArt::Frame&& frame, bool bakedShadows, uint64_t id)
    {
        auto made = std::make_shared<MCHdArt::Frame>(std::move(frame));
        made->Id = id;

        if (bakedShadows)
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
    /// The frame <paramref name="key"/> for a shape of <paramref name="width"/> x <paramref name="height"/>, numbered
    /// <paramref name="id"/>: its <c>.hdf</c> file, else its PNG pair, else its entry in an archive; none when the pack
    /// has no such frame, or what is wrong with the one it has.
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> ReadFrame(const FrameKey& key, int32_t width,
                                                                                int32_t height, uint64_t id)
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
                auto archived = ReadArchived(folder, frame, width, height);

                if (!archived)
                {
                    return std::unexpected(std::format("frame {}: {}", frame, archived.error()));
                }

                if (!archived->has_value())
                {
                    return nullptr;
                }

                return Finished(std::move(**archived), folder.BakedShadows, id);
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

        return Finished(std::move(*read), folder.BakedShadows, id);
    }

    /// <summary>
    /// Drops the kept frame <paramref name="key"/> (<paramref name="cached"/>); its entry stays, to be read again (or read
    /// ahead again) with its Id.
    /// </summary>
    void Drop(State& state, const FrameKey& key, Cached& cached)
    {
        state.Order.erase(cached.InOrder);
        state.Bytes -= cached.Frame->Bytes();
        cached.Frame.reset();
        cached.Looked = false;
        std::scoped_lock lock(state.Ahead.Lock);
        state.Ahead.Known.erase(key);
    }

    /// <summary>
    /// Drops the least recently used frames, but never the one used last (the frame just read), until the kept ones fit
    /// the budget.
    /// </summary>
    void Trim()
    {
        State& state = TheState();
        const size_t budget = MCHdArt::CacheBudget();

        while (state.Bytes > budget && state.Order.size() > 1)
        {
            if (const auto oldest = state.Frames.find(state.Order.front()); oldest != state.Frames.end())
            {
                Drop(state, oldest->first, oldest->second);
            }
            else
            {
                state.Order.pop_front();
            }
        }
    }

    /// <summary>The frames in the shape table starting at <paramref name="table"/>; 0 when it isn't one.</summary>
    int32_t FramesIn(std::span<const uint8_t> table)
    {
        return table.size() >= ShapeCountEnd && VfxIsShapeTable(table) ? VfxShapeCount(table) : 0;
    }

    /// <summary>
    /// The reading thread's own handle on an archive's file, opened for its first read and closed with it, counted among
    /// the archives' files kept open (State::ReaderFiles) so that no more than MaxOpenArchiveFiles are open at once.
    /// </summary>
    class ReaderFile
    {
    public:
        explicit ReaderFile(const MCHdArchive& archive) : _Archive(archive) {}
        ReaderFile(const ReaderFile&) = delete;
        ReaderFile& operator=(const ReaderFile&) = delete;

        ~ReaderFile() { Close(); }

        /// <summary>
        /// The handle, opened the first time; none when the file can't be opened or changed since its directory was read
        /// (the game's thread then reads, and logs, as it would have).
        /// </summary>
        MCOptionalRef<MCDiskFile> Get()
        {
            if (!_Counted)
            {
                _Counted = true;
                Count(true);
                _File = _Archive.File->OpenAnother();
            }

            return _File.has_value() ? MCOptionalRef<MCDiskFile>(*_File) : std::nullopt;
        }

        /// <summary>Closes the handle, if opened (Get opens it again).</summary>
        void Close()
        {
            if (_Counted)
            {
                _Counted = false;
                _File.reset();
                Count(false);
            }
        }

    private:
        /// <summary>Counts the handle as open (making room for it) or closed.</summary>
        static void Count(bool open)
        {
            State& state = TheState();
            std::scoped_lock lock(state.ArchiveFiles);
            state.ReaderFiles = open ? 1 : 0;
            state.ReaderOpens += open ? 1 : 0;
            TrimOpenFiles(state);
        }

        const MCHdArchive& _Archive;
        std::optional<MCDiskFile> _File;
        bool _Counted = false;
    };

    /// <summary>
    /// The frames drawn after <paramref name="follow"/>'s that its archive holds, the soonest drawn first: the rest of its
    /// step's family (its in-betweens, or its turn middle's), then each of the next FramesAhead frames' (looping round to
    /// the first), each family up to the first member the archive lacks.
    /// </summary>
    std::vector<AheadEntry> NextFrames(const Follow& follow)
    {
        const auto frame = static_cast<int32_t>(follow.Key.Name & FrameNumberMask);
        const StepFamily family = FamilyOf(follow.Key.Name >> InBetweenNameShift);
        std::vector<AheadEntry> next;

        // Frame at's family from member from on, up to the first member the archive lacks.
        const auto addFamily = [&](int32_t at, int32_t from)
        {
            for (int32_t member = from; member < family.Members; ++member)
            {
                const uint64_t name = NumberedKey(at, family.StepOf(member));
                std::string entry = follow.Prefix + NumberedName(name) + std::string(HdfExtension);
                const std::optional<int32_t> index = follow.Archive->File->Find(entry);

                if (!index.has_value())
                {
                    return;
                }

                next.push_back(AheadEntry{name, *index, std::move(entry)});
            }
        };

        // Port-only: a neighbour's in a turn, its own frame.
        if (follow.Itself)
        {
            addFamily(frame, 0);
            return next;
        }

        addFamily(frame, family.Member + 1);

        for (int32_t on = 1; on <= FramesAhead && on < follow.Frames; ++on)
        {
            addFamily((frame + on) % follow.Frames, 0);
        }

        return next;
    }

    /// <summary>
    /// Keeps <paramref name="frame"/>, read ahead as <paramref name="key"/>, for the game's thread, dropping the oldest
    /// kept past MaxAheadBytes. Call with ReadAhead::Lock held.
    /// </summary>
    void Hold(ReadAhead& ahead, const FrameKey& key, std::shared_ptr<MCHdArt::Frame>&& frame)
    {
        const size_t bytes = frame->Bytes();

        if (!ahead.Ready.try_emplace(key, ReadyFrame{std::move(frame), ahead.Order.end()}).second)
        {
            return;
        }

        ahead.Bytes += bytes;
        ahead.Ready.at(key).InOrder = ahead.Order.insert(ahead.Order.end(), key);

        while (ahead.Bytes > MaxAheadBytes && ahead.Order.size() > 1)
        {
            if (const auto oldest = ahead.Ready.find(ahead.Order.front()); oldest != ahead.Ready.end())
            {
                ahead.Bytes -= oldest->second.Frame->Bytes();
                ahead.Ready.erase(oldest);
            }

            ahead.Order.pop_front();
        }
    }

    /// <summary>
    /// The reading thread: takes the follows in turn and reads the frames drawn after each (NextFrames) that the game's
    /// thread hasn't looked at and that aren't read already, one at a time, without the lock and through a handle of
    /// its own on the archive's file (ReaderFile), as the game's thread would for a shape of any size (taking one, it
    /// checks the size it asks for). A read that fails is left to the game's thread, which reads it again and logs why;
    /// a read begun before what was found was forgotten is dropped.
    /// </summary>
    /// <remarks>
    /// The handle is kept for the follows after while they read the same archive, as a pan's mostly do: opened for each
    /// follow, one archive was opened 136 times in 2 s on Windows (13 Nov). It closes for another archive's follow,
    /// after a read through it fails (the next follow opens it again), and when the pack is set (ForgetAhead waits).
    /// </remarks>
    void ReadFramesAhead(std::stop_token stop)
    {
        ReadAhead& ahead = TheState().Ahead;
        // The archive is held here as long as the handle on it (ReaderFile keeps a reference).
        std::shared_ptr<MCHdArchive> keptArchive;
        std::optional<ReaderFile> kept;
        uint64_t keptGeneration = 0;
        std::unique_lock lock(ahead.Lock);
        // Without the lock: the handle's count takes State::ArchiveFiles.
        const auto closeKept = [&]
        {
            lock.unlock();
            kept.reset();
            keptArchive.reset();
            lock.lock();
            ahead.ReaderKeeps = false;
            ahead.Changed.notify_all();
        };

        const auto keptStale = [&] { return ahead.ReaderKeeps && keptGeneration != ahead.Generation; };

        while (ahead.Changed.wait(lock, stop, [&] { return !ahead.Follows.empty() || keptStale(); }))
        {
            if (keptStale())
            {
                closeKept();
                continue;
            }

            const Follow follow = std::move(ahead.Follows.front());
            ahead.Follows.pop_front();
            const uint64_t generation = ahead.Generation;
            ahead.Working = true;

            if (ahead.ReaderKeeps && keptArchive != follow.Archive)
            {
                closeKept();
            }

            // Kept is set with ReaderKeeps, both under the lock. The pack may have been set while the lock was let go.
            if (!ahead.ReaderKeeps && ahead.Generation == generation)
            {
                keptArchive = follow.Archive;
                kept.emplace(*keptArchive);
                keptGeneration = generation;
                ahead.ReaderKeeps = true;
            }

            if (!kept.has_value())
            {
                ahead.Working = false;
                ahead.Changed.notify_all();
                continue;
            }

            ReaderFile& file = *kept;
            lock.unlock();
            // The archive's directory never changes once read, so it is looked in without a lock.
            const std::vector<AheadEntry> next = follow.Entries.empty() ? NextFrames(follow) : follow.Entries;
            lock.lock();
            bool failed = false;

            for (const AheadEntry& entry : next)
            {
                const FrameKey key{follow.Key.Folder, entry.Name};

                if (stop.stop_requested() || ahead.Generation != generation)
                {
                    break;
                }

                if (ahead.Known.contains(key) || ahead.Ready.contains(key) || ahead.Pictures.contains(key))
                {
                    continue;
                }

                ahead.Reading = key;
                lock.unlock();
                const MCOptionalRef<MCDiskFile> handle = file.Get();
                std::shared_ptr<MCHdArt::Frame> frame;

                if (handle)
                {
                    if (auto read =
                            ReadEntry(*follow.Archive, entry.Index, entry.Entry, entry.Width, entry.Height, handle);
                        read.has_value())
                    {
                        frame = Finished(std::move(*read), follow.BakedShadows, 0);
                    }
                }

                lock.lock();
                ahead.Reading.reset();

                if (frame != nullptr && ahead.Generation == generation)
                {
                    ahead.NeighbourReads += follow.Itself ? 1 : 0;

                    if (follow.Entries.empty())
                    {
                        Hold(ahead, key, std::move(frame));
                    }
                    else
                    {
                        ahead.Pictures.emplace(key, std::move(frame));
                    }
                }

                ahead.Changed.notify_all();

                if (!handle)
                {
                    failed = true;
                    break;
                }
            }

            if (failed)
            {
                closeKept();
            }

            // Port-only: pictures not read (failed, or the pack set) may be asked for again.
            for (const AheadEntry& entry : follow.Entries)
            {
                ahead.PicturesAsked.erase(FrameKey{follow.Key.Folder, entry.Name});
            }

            ahead.Working = false;
            ahead.Changed.notify_all();
        }

        if (ahead.ReaderKeeps)
        {
            closeKept();
        }
    }

    /// <summary>
    /// Hands the reading thread <paramref name="key"/>, a frame just drawn the first time, to read the frames drawn after
    /// it (starting the thread the first time), or (<paramref name="itself"/>) a neighbour's frame, to read it: only a
    /// numbered folder's frame in an archive (not loose files, which are a generator's, not a player's), and of a shape
    /// table of <paramref name="frames"/> frames.
    /// </summary>
    void FollowAhead(const FrameKey& key, int32_t frames, bool itself = false)
    {
        State& state = TheState();
        const FrameFolder& folder = state.Folders.at(key.Folder);

        if (frames <= 0 || (key.Name & FrameNumberMask) >= static_cast<uint64_t>(frames) ||
            folder.Names != Naming::Number || folder.Loose != std::optional(false) || !folder.Archived.has_value() ||
            folder.Archived->Archive == nullptr)
        {
            return;
        }

        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);

        // Past the most, the oldest is left out: the game has drawn on since.
        if (ahead.Follows.size() >= MaxFollows)
        {
            ahead.Follows.pop_front();
        }

        ahead.Follows.push_back(
            Follow{key, frames, folder.Archived->Archive, folder.Archived->Prefix, folder.BakedShadows, {}, itself});

        if (!ahead.Reader.joinable())
        {
            ahead.Reader = std::jthread(ReadFramesAhead);
        }

        ahead.Changed.notify_all();
    }

    /// <summary>
    /// Port-only: whether neighbour folder <paramref name="neighbour"/>'s frames are in the archive holding
    /// <paramref name="drawn"/>'s, which stands for both folders' parent (as a mech part's archive holds every packet of
    /// the mech's): then it is taken as the neighbour's, its entries' names starting with the neighbour's own folder, and
    /// no file is looked at.
    /// </summary>
    bool SharesArchive(const FrameFolder& drawn, FrameFolder& neighbour)
    {
        if (neighbour.Archived.has_value())
        {
            return neighbour.Loose == std::optional(false) && neighbour.Archived->Archive != nullptr;
        }

        if (drawn.Loose != std::optional(false) || !drawn.Archived.has_value() || drawn.Archived->Archive == nullptr ||
            drawn.Path.parent_path() != neighbour.Path.parent_path())
        {
            return false;
        }

        const std::string own = MCFileSystem::ToUtf8(drawn.Path.filename()) + ArchiveSeparator;
        const std::string& prefix = drawn.Archived->Prefix;

        // Not when the archive stands for the drawn folder itself (no prefix).
        if (!prefix.ends_with(own) ||
            (prefix.size() > own.size() && prefix[prefix.size() - own.size() - 1] != ArchiveSeparator))
        {
            return false;
        }

        neighbour.Loose = false;
        neighbour.Archived = FrameFolder::InArchive{
            drawn.Archived->Archive, prefix.substr(0, prefix.size() - own.size()) +
                                         MCFileSystem::ToUtf8(neighbour.Path.filename()) + ArchiveSeparator};
        return true;
    }

    /// <summary>
    /// Port-only: hands the reading thread the same frame as <paramref name="key"/> (just drawn the first time, of a shape
    /// table of <paramref name="frames"/> frames) in each of its folder's neighbours in a turn (MCHdArt::SetNeighbours)
    /// whose frames its archive holds.
    /// </summary>
    void FollowNeighbours(const FrameKey& key, int32_t frames)
    {
        State& state = TheState();
        const auto found = state.Neighbours.find(key.Folder);

        if (found == state.Neighbours.end())
        {
            return;
        }

        for (const uint32_t neighbour : found->second)
        {
            if (SharesArchive(state.Folders.at(key.Folder), state.Folders.at(neighbour)))
            {
                FollowAhead(FrameKey{neighbour, key.Name & FrameNumberMask}, frames, true);
            }
        }
    }

    /// <summary>
    /// The frame <paramref name="key"/> if the reading thread read it ahead (waiting while it reads it), taken from it;
    /// null when it didn't, and then it never will: the game's thread looks at it now.
    /// </summary>
    std::shared_ptr<MCHdArt::Frame> TakeAhead(const FrameKey& key)
    {
        ReadAhead& ahead = TheState().Ahead;
        std::unique_lock lock(ahead.Lock);
        ahead.Changed.wait(lock, [&] { return ahead.Reading != key; });
        ahead.Known.insert(key);

        // Port-only: a picture read ahead, not kept yet (MCHdArt::KeepReadAhead).
        if (const auto picture = ahead.Pictures.find(key); picture != ahead.Pictures.end())
        {
            std::shared_ptr<MCHdArt::Frame> frame = std::move(picture->second);
            ahead.Pictures.erase(picture);
            return frame;
        }

        const auto found = ahead.Ready.find(key);

        if (found == ahead.Ready.end())
        {
            return nullptr;
        }

        std::shared_ptr<MCHdArt::Frame> frame = std::move(found->second.Frame);
        ahead.Bytes -= frame->Bytes();
        ahead.Order.erase(found->second.InOrder);
        ahead.Ready.erase(found);
        return frame;
    }

    /// <summary>
    /// Forgets the frames read ahead and those to read, and drops any read begun (the pack is set); returns once the
    /// reading thread's handle on the old pack's archive is closed.
    /// </summary>
    void ForgetAhead()
    {
        ReadAhead& ahead = TheState().Ahead;
        std::unique_lock lock(ahead.Lock);
        ahead.Follows.clear();
        ahead.Known.clear();
        ahead.Reading.reset();
        ahead.Ready.clear();
        ahead.Order.clear();
        ahead.Bytes = 0;
        ahead.Pictures.clear();
        ahead.PicturesAsked.clear();
        ++ahead.Generation;
        ahead.Changed.notify_all();
        ahead.Changed.wait(lock, [&ahead] { return !ahead.ReaderKeeps; });
    }

    /// <summary>
    /// The frame <paramref name="key"/> as ReadFrame reads it, numbered <paramref name="id"/>: the reading thread's when
    /// it read it ahead and it fits the shape of <paramref name="width"/> x <paramref name="height"/> asked for (0 x 0:
    /// any, as MCHdArt::DecodeHdf takes them), else read now.
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> ReadOrTake(const FrameKey& key, int32_t width,
                                                                                 int32_t height, uint64_t id)
    {
        if (std::shared_ptr<MCHdArt::Frame> ahead = TakeAhead(key);
            ahead != nullptr &&
            ((width == 0 && height == 0) || (ahead->OriginalWidth() == width && ahead->OriginalHeight() == height)))
        {
            ahead->Id = id;
            ++TheState().TakenAhead;
            return ahead;
        }

        return ReadFrame(key, width, height, id);
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
                                           MCFileSystem::ToUtf8(colorPath.filename()),
                                           MCFileSystem::ToUtf8(indexPath.filename()), color->Width, color->Height,
                                           index->Width, index->Height, scale));
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
    /// <summary>
    /// The frame <paramref name="key"/>, from the cache or read the first time it is asked for (or taken from the reading
    /// thread). A numbered frame of a shape table of <paramref name="frames"/> frames (none: 0) has the frames drawn after
    /// it read ahead then.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> Cache(const FrameKey& key, int32_t width, int32_t height, int32_t frames = 0)
    {
        State& state = TheState();
        Cached& cached = state.Frames[key];
        cached.LastFrame = state.Frame;

        // Port-only: a frame kept as read ahead (MCHdArt::KeepReadAhead) is checked at its first draw, as one taken from
        // the reading thread is (ReadOrTake): one not of the size asked for is read again, as drawn, which refuses it;
        // one that is has the frames drawn after it read ahead, as a first draw that reads one does.
        if (cached.Frame != nullptr && cached.Undrawn)
        {
            cached.Undrawn = false;

            if ((width != 0 || height != 0) &&
                (cached.Frame->OriginalWidth() != width || cached.Frame->OriginalHeight() != height))
            {
                Drop(state, key, cached);
            }
            else
            {
                FollowAhead(key, frames);
                FollowNeighbours(key, frames);
            }
        }

        if (cached.Frame != nullptr)
        {
            state.Order.splice(state.Order.end(), state.Order, cached.InOrder);
        }
        else if (!cached.Looked)
        {
            cached.Looked = true;

            if (cached.Id == 0)
            {
                cached.Id = MCHdArt::NewFrameId();
            }

            const auto started = std::chrono::steady_clock::now();
            // Port-only: a frame's first use reads it here, as the frame draws, unless read ahead (the frame log's).
            MCFrameLog::Count(FrameLogReads);
            auto read = [&]
            {
                const MCFrameLog::Scope part(FrameLogRead);
                return ReadOrTake(key, width, height, cached.Id);
            }();
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
                FollowAhead(key, frames);
                FollowNeighbours(key, frames);
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
        const std::filesystem::path user = MCFileSystem::UserRoot() / MCHdArt::DefaultFolder;
        const std::filesystem::path game = MCFileSystem::GameRoot() / MCHdArt::DefaultFolder;
        std::error_code error;
        state.FoundFolder =
            std::filesystem::is_directory(user, error) || !std::filesystem::is_directory(game, error) ? user : game;
    }

    return *state.FoundFolder;
}

void MCHdArt::SetPackFolder(const std::filesystem::path& folder)
{
    // First, so nothing read ahead from the old pack is taken for the new one's frames of the same keys.
    ForgetAhead();
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
    state.ShapeTableFolders.clear();
    state.Bytes = 0;
    state.Logged.clear();
    state.MadeWith.reset();
    state.PaletteLooked = false;

    {
        std::scoped_lock lock(state.ArchiveFiles);
        state.OpenFiles.clear();
        state.OpenAt.clear();
    }

    state.Archives.clear();
    state.KeptPictures.clear();
    state.Neighbours.clear();
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

void MCHdArt::SetSmoothEdgeTables(std::span<const MCAddressKey> tables)
{
    TheState().SmoothTables = std::unordered_set<MCAddressKey>(tables.begin(), tables.end());
}

bool MCHdArt::SmoothEdges(std::span<const uint8_t> table)
{
    return GHdArt != 0 && TheState().SmoothTables.contains(MCAddressKey::OfData(table));
}

void MCHdArt::SetLineArtFrames(std::span<const LineArtFrame> frames)
{
    auto& kept = TheState().LineArtFrames;
    kept.clear();

    for (const LineArtFrame& frame : frames)
    {
        kept.insert_or_assign({frame.Table, frame.Frame}, frame.Drawn);
    }
}

bool MCHdArt::LineArt(std::span<const uint8_t> table, int32_t frame)
{
    return GHdArt != 0 && TheState().LineArtFrames.contains({MCAddressKey::OfData(table), frame});
}

std::span<const MCSharpLines::MCDrawnLoop> MCHdArt::LineArtDrawn(std::span<const uint8_t> table, int32_t frame)
{
    const auto& kept = TheState().LineArtFrames;
    const auto found = kept.find({MCAddressKey::OfData(table), frame});
    return found == kept.end() ? std::span<const MCSharpLines::MCDrawnLoop>{} : found->second;
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

        if (file.Open(MCFileSystem::ToUtf8(path)) != 0 || file.SeekBlock(PackBlock) != 0)
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

    if (file.Create(MCFileSystem::ToUtf8(path)) != 0)
    {
        return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(path)));
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

        state.Sequences[MCAddressKey::OfData(table)] = Sequence{owner, FolderIndex(path, Naming::Number), keepsEdges};
    }

    /// <summary>
    /// Port-only: notes <paramref name="shape"/>'s neighbours in a turn (its HdNeighbours) for its folder, once it is
    /// attached (MCHdArt::SetNeighbours).
    /// </summary>
    void NoteNeighbours(const MCShape& shape)
    {
        if (shape.HdNeighbours.empty())
        {
            return;
        }

        const MCOptionalRef<const Sequence> sequence = AttachedSequence(shape.FrameList());

        if (!sequence)
        {
            return;
        }

        const uint32_t folder = sequence->get().Folder;
        std::vector<uint32_t> neighbours;
        neighbours.reserve(shape.HdNeighbours.size());

        for (const std::filesystem::path& neighbour : shape.HdNeighbours)
        {
            neighbours.push_back(FolderIndex(MCHdArt::PackFolder() / neighbour, Naming::Number));
        }

        TheState().Neighbours[folder] = std::move(neighbours);
    }
}

std::filesystem::path MCHdArt::FolderOf(const MechShapeKey& key)
{
    return std::filesystem::path(MechFolder) / MCFileSystem::FromUtf8(key.PartFile) / std::to_string(key.MechNum) /
           std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::FolderOf(const ObjectShapeKey& key)
{
    return std::filesystem::path(ObjectFolder) / MCFileSystem::FromUtf8(key.ShapeFile) /
           std::to_string(key.Appearance) / std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::FolderOf(const CraterShapeKey& key)
{
    return std::filesystem::path(CraterFolder) / MCFileSystem::FromUtf8(key.CraterFile) / std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::FolderOf(const IconShapeKey& key)
{
    return std::filesystem::path(IconFolder) / MCFileSystem::FromUtf8(key.File);
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
        NoteNeighbours(*shape);
    }
}

void MCHdArt::SetNeighbours(const std::shared_ptr<MCShape>& shape, std::vector<std::filesystem::path> folders)
{
    if (shape != nullptr)
    {
        shape->HdNeighbours = std::move(folders);
        NoteNeighbours(*shape);
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

std::shared_ptr<const MCHdArt::Frame> MCHdArt::Find(std::span<const uint8_t> table, int32_t frame, int32_t width,
                                                    int32_t height, int32_t step)
{
    if (!Active())
    {
        return nullptr;
    }

    State& state = TheState();
    const auto found = state.Sequences.find(MCAddressKey::OfData(table));

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

    return Cache(FrameKey{found->second.Folder, NumberedKey(frame, step)}, width, height, FramesIn(table));
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

bool MCHdArt::KeepsFrameEdges(std::span<const uint8_t> table)
{
    const State& state = TheState();
    const auto found = state.Sequences.find(MCAddressKey::OfData(table));
    return found != state.Sequences.end() && found->second.KeepsEdges;
}

bool MCHdArt::Attached(std::span<const uint8_t> table)
{
    return AttachedSequence(table).has_value();
}

std::optional<std::filesystem::path> MCHdArt::AttachedFolder(std::span<const uint8_t> table)
{
    const MCOptionalRef<const Sequence> sequence = AttachedSequence(table);

    if (!sequence)
    {
        return std::nullopt;
    }

    return TheState().Folders[sequence->get().Folder].Path;
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

    const uint64_t hash = PictureHash(pixels, width, height);

    if (const auto kept = TheState().KeptPictures.find(hash); kept != TheState().KeptPictures.end())
    {
        return kept->second;
    }

    return Cache(FrameKey{FolderIndex(PackFolder() / PictureFolder, Naming::Hash), hash}, width, height);
}

void MCHdArt::ReadPicturesAhead(std::span<const PictureAhead> pictures)
{
    if (!Active() || pictures.empty())
    {
        return;
    }

    State& state = TheState();
    const uint32_t folderIndex = FolderIndex(PackFolder() / PictureFolder, Naming::Hash);
    FrameFolder& folder = state.Folders.at(folderIndex);
    std::error_code error;

    // Only an archive's (as the frames followed): loose files are a generator's, not a player's.
    if (!folder.Loose.has_value())
    {
        folder.Loose = std::filesystem::is_directory(folder.Path, error);
    }

    const FrameFolder::InArchive& archived = ArchiveOf(folder);

    if (*folder.Loose || archived.Archive == nullptr)
    {
        return;
    }

    // Those the pack has, not kept, looked at or asked for already, while they and the frames kept fit the share of the
    // budget (an entry's unpacked bytes are about its frame's).
    const size_t room = CacheBudget() / PicturesAheadBudgetShare;
    Follow follow{FrameKey{folderIndex, 0}, 0, archived.Archive, archived.Prefix, folder.BakedShadows, {}};
    std::unordered_set<uint64_t> named;
    size_t asked = state.Bytes;
    ReadAhead& ahead = state.Ahead;
    std::scoped_lock lock(ahead.Lock);

    for (const PictureAhead& picture : pictures)
    {
        if (picture.Width <= 0 || picture.Height <= 0)
        {
            continue;
        }

        const uint64_t hash = PictureHash(picture.Pixels, picture.Width, picture.Height);
        const FrameKey key{folderIndex, hash};

        if (!named.insert(hash).second || state.KeptPictures.contains(hash) || ahead.Known.contains(key) ||
            ahead.Ready.contains(key) || ahead.Pictures.contains(key) || ahead.PicturesAsked.contains(key))
        {
            continue;
        }

        if (const auto cached = state.Frames.find(key);
            cached != state.Frames.end() && (cached->second.Frame != nullptr || cached->second.Looked))
        {
            continue;
        }

        std::string entry = archived.Prefix + HashName(hash) + std::string(HdfExtension);
        const std::optional<int32_t> index = archived.Archive->File->Find(entry);

        if (!index.has_value())
        {
            continue;
        }

        const auto bytes =
            static_cast<size_t>(std::max<int64_t>(archived.Archive->File->GetEntries()[*index].RealSize, 0));

        if (asked + bytes > room)
        {
            break;
        }

        asked += bytes;
        ahead.PicturesAsked.insert(key);
        follow.Entries.push_back(AheadEntry{hash, *index, std::move(entry), picture.Width, picture.Height});
    }

    if (follow.Entries.empty())
    {
        return;
    }

    ahead.Follows.push_back(std::move(follow));

    if (!ahead.Reader.joinable())
    {
        ahead.Reader = std::jthread(ReadFramesAhead);
    }

    ahead.Changed.notify_all();
}

void MCHdArt::KeepReadAhead()
{
    State& state = TheState();
    std::vector<std::pair<FrameKey, std::shared_ptr<Frame>>> read;

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);

        if (ahead.Pictures.empty() && ahead.Ready.empty())
        {
            return;
        }

        for (auto& [key, frame] : ahead.Pictures)
        {
            read.emplace_back(key, std::move(frame));
            ahead.Known.insert(key);
        }

        // The oldest read first, so that kept in the cache's order they go first.
        for (const FrameKey& key : ahead.Order)
        {
            if (const auto ready = ahead.Ready.find(key); ready != ahead.Ready.end())
            {
                read.emplace_back(key, std::move(ready->second.Frame));
                ahead.Known.insert(key);
            }
        }

        ahead.Pictures.clear();
        ahead.Ready.clear();
        ahead.Order.clear();
        ahead.Bytes = 0;
    }

    // Kept as a first draw keeps a frame read (Cache), numbered as the draw would number it.
    for (auto& [key, frame] : read)
    {
        Cached& cached = state.Frames[key];

        if (cached.Frame != nullptr)
        {
            continue;
        }

        cached.Looked = true;
        cached.Undrawn = true;
        cached.LastFrame = state.Frame;

        if (cached.Id == 0)
        {
            cached.Id = NewFrameId();
        }

        frame->Id = cached.Id;
        cached.Frame = std::move(frame);
        cached.InOrder = state.Order.insert(state.Order.end(), key);
        state.Bytes += cached.Frame->Bytes();
        ++state.Read;
        ++state.TakenAhead;
    }

    if (!read.empty())
    {
        Trim();
    }
}

void MCHdArt::KeepPicture(std::span<const uint8_t> pixels, int32_t width, int32_t height, Frame&& frame)
{
    auto kept = std::make_shared<Frame>(std::move(frame));
    kept->Id = NewFrameId();
    TheState().KeptPictures.insert_or_assign(PictureHash(pixels, width, height), std::move(kept));
}

namespace
{
    /// <summary>
    /// How a frame's colours are laid out, for copying a part of them: the bytes of a unit (a pixel, or a block of
    /// them), its side in HD pixels, and the units in a row; none for a format whose units can't be copied apart.
    /// </summary>
    struct ColorUnits
    {
        size_t Bytes = 0;
        int32_t Side = 1;
        size_t Wide = 0;
        size_t High = 0;
    };

    std::optional<ColorUnits> UnitsOf(const MCHdArt::Frame& frame)
    {
        ColorUnits units;

        switch (frame.Format)
        {
            case MCHdArt::ColorFormat::Rgba8:
                units.Bytes = static_cast<size_t>(BytesPerPixel);
                break;
            case MCHdArt::ColorFormat::Bc7:
            {
                units.Bytes = MCBc7::BlockBytes;
                units.Side = MCBc7::BlockSide;
                break;
            }
            case MCHdArt::ColorFormat::Bc5:
            default:
                return std::nullopt;
        }

        units.Wide = static_cast<size_t>((frame.Width + units.Side - 1) / units.Side);
        units.High = static_cast<size_t>((frame.Height + units.Side - 1) / units.Side);
        return units;
    }
}

MCHdArt::Frame MCHdArt::BlankFrame(const Frame& like, int32_t width, int32_t height)
{
    Frame blank;
    blank.Scale = like.Scale;
    blank.Format = like.Format;
    blank.Blended = like.Blended;
    blank.Width = width * like.Scale;
    blank.Height = height * like.Scale;
    const std::optional<ColorUnits> units = UnitsOf(blank);
    blank.Colors.assign(units.has_value() ? units->Wide * units->High * units->Bytes : 0, 0);
    blank.Indices.assign(static_cast<size_t>(width) * height * IndexBytes, 0);
    return blank;
}

bool MCHdArt::CopyArea(const Frame& source, int32_t x, int32_t y, int32_t width, int32_t height, Frame& target,
                       int32_t toX, int32_t toY)
{
    const auto inside = [width, height](const Frame& frame, int32_t left, int32_t top)
    {
        return width > 0 && height > 0 && left >= 0 && top >= 0 && left + width <= frame.OriginalWidth() &&
               top + height <= frame.OriginalHeight() &&
               frame.Indices.size() == static_cast<size_t>(frame.OriginalWidth()) * frame.OriginalHeight() * IndexBytes;
    };

    const std::optional<ColorUnits> from = UnitsOf(source);
    const std::optional<ColorUnits> to = UnitsOf(target);

    if (source.Scale != target.Scale || source.Format != target.Format || !from.has_value() || !to.has_value() ||
        source.Scale % from->Side != 0 || !inside(source, x, y) || !inside(target, toX, toY) ||
        source.Colors.size() != from->Wide * from->High * from->Bytes ||
        target.Colors.size() != to->Wide * to->High * to->Bytes)
    {
        return false;
    }

    // The colours, a row of units at a time: each original pixel's HD pixels are whole units.
    const auto perPixel = static_cast<size_t>(source.Scale / from->Side);
    const size_t rowBytes = static_cast<size_t>(width) * perPixel * from->Bytes;

    for (size_t row = 0; row < static_cast<size_t>(height) * perPixel; ++row)
    {
        const size_t read =
            ((static_cast<size_t>(y) * perPixel + row) * from->Wide + static_cast<size_t>(x) * perPixel) * from->Bytes;
        const size_t write =
            ((static_cast<size_t>(toY) * perPixel + row) * to->Wide + static_cast<size_t>(toX) * perPixel) * to->Bytes;
        std::copy_n(source.Colors.begin() + static_cast<ptrdiff_t>(read), rowBytes,
                    target.Colors.begin() + static_cast<ptrdiff_t>(write));
    }

    // The indices, an original pixel each.
    for (int32_t row = 0; row < height; ++row)
    {
        const size_t read =
            (static_cast<size_t>(y + row) * static_cast<size_t>(source.OriginalWidth()) + static_cast<size_t>(x)) *
            IndexBytes;
        const size_t write =
            (static_cast<size_t>(toY + row) * static_cast<size_t>(target.OriginalWidth()) + static_cast<size_t>(toX)) *
            IndexBytes;
        std::copy_n(source.Indices.begin() + static_cast<ptrdiff_t>(read), static_cast<size_t>(width) * IndexBytes,
                    target.Indices.begin() + static_cast<ptrdiff_t>(write));
    }

    return true;
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
                                                         int32_t step, int32_t frames)
{
    if (!Active())
    {
        return nullptr;
    }

    const auto [folder, fresh] = TheState().ShapeTableFolders.try_emplace(hash, 0);

    if (fresh)
    {
        folder->second = FolderIndex(PackFolder() / ShapeTableFolder(hash), Naming::Number);
    }

    return Cache(FrameKey{folder->second, NumberedKey(frame, step)}, width, height, frames);
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
    const std::filesystem::path path =
        PackFolder() / MovieFolder / MCFileSystem::FromUtf8(name + std::string(MovieExtension));
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

size_t MCHdArt::FramesReadAhead()
{
    return TheState().TakenAhead;
}

size_t MCHdArt::NeighboursReadAhead()
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return ahead.NeighbourReads;
}

void MCHdArt::WaitForReadAhead()
{
    ReadAhead& ahead = TheState().Ahead;
    std::unique_lock lock(ahead.Lock);
    ahead.Changed.wait(lock, [&ahead] { return ahead.Follows.empty() && !ahead.Working; });
}

void MCHdArt::DropGround()
{
    State& state = TheState();
    const std::filesystem::path pack = PackFolder();
    std::vector<uint32_t> folders;

    for (const std::string_view name : {TerrainFolder, ReliefFolder})
    {
        if (const auto found = state.FolderIndices.find(pack / name); found != state.FolderIndices.end())
        {
            folders.push_back(found->second);
        }
    }

    for (auto& [key, cached] : state.Frames)
    {
        if (cached.Frame != nullptr && std::ranges::contains(folders, key.Folder))
        {
            Drop(state, key, cached);
        }
    }
}

size_t MCHdArt::CacheBudget()
{
    const size_t set = TheState().Budget;
    return set != 0 ? set : static_cast<size_t>(MCMemoryRoom::MostHdBytes);
}

void MCHdArt::SetCacheBudget(size_t bytes)
{
    TheState().Budget = bytes;
    Trim();
}

void MCHdArt::NextFrame(bool memoryShort)
{
    State& state = TheState();
    ++state.Frame;

    if (!memoryShort)
    {
        return;
    }

    // The least recently used first, until TrimmedBytesPerFrame have gone or the next was asked for in the last frame
    // (it and those after it are drawn, and would only be read again). A frame something else still holds (a texture's
    // picture, asked for once) leaves the cache too, so its memory goes once that lets go of it, but it frees nothing
    // now: it isn't counted, or the frames only the cache holds would wait for later frames.
    size_t freed = 0;

    while (!state.Order.empty() && freed < MCMemoryRoom::TrimmedBytesPerFrame)
    {
        const auto oldest = state.Frames.find(state.Order.front());

        if (oldest == state.Frames.end())
        {
            state.Order.pop_front();
            continue;
        }

        if (oldest->second.LastFrame + 1 >= state.Frame)
        {
            break;
        }

        // The cache's own reference alone: its bytes go with it.
        if (oldest->second.Frame.use_count() == 1)
        {
            freed += oldest->second.Frame->Bytes();
        }

        Drop(state, oldest->first, oldest->second);
    }
}

size_t MCHdArt::ArchivesOpen()
{
    return static_cast<size_t>(
        std::ranges::count_if(TheState().Archives, [](const auto& archive) { return archive.second != nullptr; }));
}

size_t MCHdArt::ReaderFilesOpened()
{
    State& state = TheState();
    std::scoped_lock lock(state.ArchiveFiles);
    return state.ReaderOpens;
}

size_t MCHdArt::ArchiveFilesOpen()
{
    State& state = TheState();
    std::scoped_lock lock(state.ArchiveFiles);
    return state.ReaderFiles + static_cast<size_t>(std::ranges::count_if(state.OpenFiles,
                                                                         [](const std::weak_ptr<MCFastFile>& kept)
                                                                         {
                                                                             const std::shared_ptr<MCFastFile> file =
                                                                                 kept.lock();
                                                                             return file != nullptr && file->IsOpen();
                                                                         }));
}

std::chrono::nanoseconds MCHdArt::ReadTime()
{
    return TheState().ReadTime;
}

size_t MCHdArt::CachedBytes()
{
    return TheState().Bytes;
}
