#include "stdafx.h"
#include "sprite/MCHdArt.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCFirstUse.h"
#include "lib/MCInflate.h"
#include "lib/MCOptionalRef.h"
#include "lib/MCZstd.h"
#include "platform/MCAllocator.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCRenderer.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCThreadPriority.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHoldLists.h"
#include "sprite/MCOpeningLists.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCPackRecords.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <expected>
#include <fstream>
#include <map>
#include <numeric>
#include <stop_token>
#include <unordered_set>

int GHdArt = 0;

namespace
{
    /// <summary>The pack's folders of mech part frames and of the other appearances' frames.</summary>
    constexpr std::string_view MechFolder = "mechs";
    constexpr std::string_view ObjectFolder = "objects";
    constexpr std::string_view CraterFolder = "craters";
    constexpr std::string_view IconFolder = "icons";
    /// <summary>The ground tiles' relief maps (MCHdArt::FindTerrainRelief).</summary>
    constexpr std::string_view ReliefFolder = "relief";
    /// <summary>The pack's folder of shape tables loaded outside the sprite, crater and icon files, each named by its hash.</summary>
    constexpr std::string_view ShapeFolder = "shapes";
    /// <summary>The pack's folder of pictures (the menus' and logistics' art), each named by its hash.</summary>
    constexpr std::string_view PictureFolder = "menus";
    /// <summary>
    /// Port-only: the pack's folder of the briefing maps and their tactical maps, pictures named as the menus' are, kept
    /// apart (<c>maps.hda</c>) so that the menus' archive holds only what the screens show (MCHdArt.h).
    /// </summary>
    constexpr std::string_view MapFolder = "maps";
    /// <summary>Port-only: the pack's archive of the sharp screens' pictures (MCHdArt::SharpArchive).</summary>
    constexpr std::string_view SharpArchiveName = "sharp.hda";
    /// <summary>The folders a picture is looked for in, in order.</summary>
    constexpr std::array<std::string_view, 2> PictureFolders = {PictureFolder, MapFolder};
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
    /// <summary>
    /// The latest pack format this code reads (2: SPEC-pack-v2.md's, its archives' tables found by keys; 1 today's): a
    /// pack of a later one is left out whole.
    /// </summary>
    constexpr int32_t PackFormat = 2;
    /// <summary>
    /// The pack's zstd dictionaries, at its root (SPEC-pack-v2.md's <c>dicts.hdd</c>, about 1 MB), and the longest one
    /// read (a longer file is another's).
    /// </summary>
    constexpr std::string_view DictionariesFile = "dicts.hdd";
    constexpr uintmax_t MostDictionariesFileBytes = 64u << 20;
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
    /// Port-only (ExpandAsks): the frames a reader checks against what's kept, read and asked for at a time under the
    /// readers' lock, which the game's thread takes every frame.
    /// </summary>
    constexpr size_t AskChunk = 256;
    /// <summary>
    /// Port-only (MCHdArt::AheadGeneration): the fewest frames between two times the askers ask again for what was asked
    /// ahead, when some of it was dropped or left out for want of room since (a quarter of a second at 120 Hz).
    /// </summary>
    constexpr uint64_t AskAgainFrames = 30;
    /// <summary>
    /// Port-only (SPEC-memory.md's indices): the frames a frame's record keeps its indices after the GPU took its
    /// picture, so that code on the CPU reading them in the same frame (the weather's measure of a unit just drawn, HD
    /// lighting's glow of an effect) finds them, and its folder keeps its frames' from then on.
    /// </summary>
    constexpr uint64_t IndicesKeptFrames = 2;
    /// <summary>The most reads of the frames drawn next waiting (the oldest left out past it: the game has drawn on).</summary>
    constexpr size_t MaxNextReads = 4096;
    /// <summary>The readers' handles kept open for their next reads, for each reader (MCHdArt::SetReaderSettings).</summary>
    constexpr size_t IdleHandlesPerReader = 2;
    /// <summary>
    /// Port-only: the sets (a screen's pictures, MCHdArt::ReadPicturesAhead; a mech's packets near the view,
    /// MCHdArt::ReadPacketAhead) are read ahead while they and the frames kept would take at most the cache's budget over
    /// this: those read ahead are kept as drawn ones are, and mustn't push those out.
    /// </summary>
    constexpr size_t SetsAheadBudgetShare = 2;
    /// <summary>A shape table's header up to its frame count (VfxShapeCount reads it at byte 4).</summary>
    constexpr size_t ShapeCountEnd = 8;
    /// <summary>The frame log's part for a frame's first use (read then, or taken from the reading thread), and its count.</summary>
    constexpr std::string_view FrameLogRead = "hd.read";
    /// <summary>Port-only: the frame log's part for an archive looked for or opened by the game's thread (ArchiveFor).</summary>
    constexpr std::string_view FrameLogOpen = "hd.open";
    constexpr std::string_view FrameLogReads = "HD frames first used";
    /// <summary>Port-only: the frame log's count of the readers' reads of the archives (MCHdArt::ReaderReads).</summary>
    constexpr std::string_view FrameLogReaderReads = "HD reads by the readers";

    /// <summary>An open archive, and the first folders of its entries' names (upper-cased).</summary>
    struct MCHdArchive
    {
        /// <summary>Shared, so the files kept open (KeepOpen) can tell an archive that's gone.</summary>
        std::shared_ptr<MCFastFile> File;
        std::unordered_set<std::string> Folders;
        /// <summary>Port-only: its file's path (the HD trace names it: MCHdArt::OpenTrace).</summary>
        std::filesystem::path Path;
        /// <summary>
        /// Port-only: the numbered frames each of its folders holds (ArchivedFrames), by the start of their entries'
        /// names (upper-cased, as Find compares them): one past the highest frame number, not an in-between's or a turn
        /// middle's; found as it opens.
        /// </summary>
        std::unordered_map<std::string, int32_t> FrameCounts;
        /// <summary>
        /// Port-only (SPEC-memory.md's fill): the stored bytes the hold keeps of it (HoldsArchive; 0: none), counted as it
        /// opens, since every ask hands the hold its archives again (QueueHold).
        /// </summary>
        uint64_t HoldBytes = 0;
    };

    std::string Upper(std::string_view text)
    {
        std::string upper(text);
        // Through unsigned char: a name from a pack may hold any byte, and toupper takes only those and EOF.
        std::ranges::transform(upper, upper.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return upper;
    }

    /// <summary>
    /// Port-only: a folder's path as the folders and archives found are kept by (State::FolderIndices,
    /// State::Archives): its text, normalized (one separator, the system's), so hashed: a mech coming into view asks for
    /// hundreds of packets' folders in a frame, and a map of paths compares them part by part.
    /// </summary>
    using PathKey = std::filesystem::path::string_type;

    PathKey KeyOfPath(const std::filesystem::path& path)
    {
        return path.lexically_normal().native();
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
    /// <summary>What a loop's wrap in-between's name has before its step (sprite/MCInBetween.h's WrapStep).</summary>
    constexpr std::string_view WrapName = "wrap";
    /// <summary>What the palette index picture of a frame adds to its name.</summary>
    constexpr std::string_view IndexSuffix = "_idx";
    /// <summary>A loose frame's pictures' extension, as the generator writes them.</summary>
    constexpr std::string_view PngExtension = ".png";
    /// <summary>A frame file's extension (MCHdArt::EncodeHdf), its header, and what it may be at most.</summary>
    constexpr std::string_view HdfExtension = ".hdf";
    /// <summary>
    /// The most frames a packet's folder is taken to hold (ArchivedFrames): a number past it is a damaged archive's (the
    /// pack's packets hold at most 151).
    /// </summary>
    constexpr int32_t MostArchivedFrames = 1024;
    /// <summary>
    /// The most packets a type's archive is taken to hold (TypePackets): a number past it is a damaged archive's (the
    /// pack's highest is 735).
    /// </summary>
    constexpr int32_t MostArchivedPackets = 1024;
    constexpr std::array<uint8_t, 4> HdfMagic = {'M', 'C', 'H', 'D'};
    constexpr uint32_t HdfVersion = 1;
    /// <summary>
    /// A version 2 pack's frame (MCHdArt::Frame::Marked): the same layout, its green saying what each pixel is.
    /// </summary>
    constexpr uint32_t HdfMarkedVersion = 2;
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
        /// <summary>
        /// Port-only (SPEC-memory.md's indices): whether code on the CPU read its frames' indices (MCHdArt::IndicesOf:
        /// HD lighting's glows and outlines, the weather's measures, braking's stops, the ground's overlays, a menu
        /// picture's keyed test), so they keep them once the GPU has their pictures; others' go.
        /// </summary>
        bool IndicesRead = false;

        /// <summary>An archive holding a folder's frames, and the start of their entries' names in it.</summary>
        struct InArchive
        {
            std::shared_ptr<MCHdArchive> Archive;
            std::string Prefix;
            /// <summary>
            /// Port-only (ArchivedFrames): its numbered frames, counted once: a version 2 archive's by their keys, a
            /// thousand looks, which each ask again would repeat.
            /// </summary>
            std::optional<int32_t> Frames = std::nullopt;
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
        /// <summary>Port-only: whether the table is a stripped one, without rows (MCShape::Stripped).</summary>
        bool Stripped = false;
    };

    /// <summary>
    /// Port-only (the ledger): a frame the cache let go of while something else held it, and its figures then.
    /// </summary>
    struct LetGoFrame
    {
        std::weak_ptr<const MCHdArt::Frame> Frame;
        MCMemoryLedger::MCFigures Figures;
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
        /// <summary>
        /// Port-only (SPEC-memory.md's one copy of a frame's colours): its colours, which Frame (its record) leaves out,
        /// held until the GPU has its picture (MCHdArt::ColorsUploaded); empty after, until they're read again.
        /// </summary>
        std::vector<uint8_t> Colors;
        /// <summary>
        /// Port-only: the GPU holds its picture (from MCHdArt::ColorsUploaded to MCHdArt::PictureDropped), so its record
        /// is never let go of: a draw of it reads nothing.
        /// </summary>
        bool OnGpu = false;
        /// <summary>
        /// Port-only (SPEC-memory.md's cache budget): kept for the mission's end (MCHdArt::KeepForMission), its record and
        /// colours never let go of, as a frame on the GPU keeps its record, until the hold lets go
        /// (MCHdArt::LetGoHold) or the pack changes.
        /// </summary>
        bool ForMission = false;
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
    /// <summary>
    /// Port-only (SPEC-first-use.md): the kinds of reads, in the order the readers take them: what a load waits for, then
    /// the frames drawn next (a first draw's followers and its neighbours'), then what the camera's surroundings draw
    /// soon (a unit's packets now and next: MCHdArt::ReadAs::Soon), then the sets read ahead (a screen's pictures, the
    /// objects near the view), which go as far as MCHdArt::ReaderSettings::SetReads lets them; then, when nothing else
    /// waits, the hold's (MCHdArt::StartHold), HoldReadsAtOnce at a time. Port-only: beside the hold, the mission's fill on
    /// the GPU (MCHdArt::FillGpu), GpuReadsAtOnce at a time, whose frames go into the upload slots.
    /// </summary>
    enum class AheadKind : uint8_t
    {
        Load,
        Next,
        Soon,
        Set,
        Hold,
        Gpu,
    };

    constexpr size_t AheadKinds = 6;

    /// <summary>
    /// Port-only (MCHdArt::FillGpu): how many of the fill's reads may be in flight at once in play (a reader waits for free
    /// slots as the GPU copies, so the others are free for what's asked meanwhile); under the loading bar half the
    /// readers while the hold reads, all of them once it has read everything.
    /// </summary>
    constexpr size_t GpuReadsAtOnce = 1;
    /// <summary>Port-only: under the loading bar the fill reads with the readers' share of 1 in this many.</summary>
    constexpr size_t LoadGpuReadersDivisor = 2;
    /// <summary>Port-only (StageForGpu): a frame's uploads in the slots, its colours and its indices, each aligned.</summary>
    constexpr size_t UploadsPerPicture = 2;
    /// <summary>Port-only: the bytes a fill's reader takes of the slots at a time (its frames put in them in turn).</summary>
    constexpr size_t FillLeaseBytes = size_t{8} << 20;
    /// <summary>
    /// Port-only (MCHdArt::FillGpu): the longest a fill reader waits for room in the slots at a time before it looks for
    /// other work again. Work queued for the readers ends its wait at once (StopGpuRoomWaits); this bounds it should a
    /// way of queueing work miss that.
    /// </summary>
    constexpr std::chrono::milliseconds FillRoomWaitMost{20};
    /// <summary>
    /// Port-only (SPEC-memory.md's "The GPU from the briefing"): the most of a frame the fill's walk over the folders it
    /// takes in may take outside a load, the rest walked in the frames after. A folder's first look takes about 10 us
    /// (its archive found, its index made), and a briefing takes in thousands at once (mission 1's: 3,115, 31 ms walked
    /// in one frame). Under the loading bar it walks them all.
    /// </summary>
    constexpr std::chrono::microseconds FillWalkMostPerFrame{500};
    /// <summary>
    /// Port-only (SPEC-memory.md: "less only the plan's own error"): the whole mission is put on the GPU when it fits with
    /// a tenth to spare, and left there until it no longer fits at all, so the mode doesn't flip as a budget wavers.
    /// </summary>
    constexpr uint64_t FitsMarginDivisor = 10;
    /// <summary>
    /// Port-only (SPEC-memory.md: "the plan's own error, which the ledger measures"): the pages the HD pictures share take
    /// more than the pictures' own bytes (their places packed in shelves, each kind's last page part full): on the A10's
    /// NVIDIA driver, 20 Nov, 1.25-1.48 times them in the ten missions held whole. A mission's frames are counted at this
    /// many times their bytes until the fill has measured its own (PagesMeasuredFromBytes).
    /// </summary>
    constexpr double PagesShareFirst = 1.3;
    /// <summary>
    /// Port-only: the pictures' bytes on the GPU from which, once a fill has put a mission up whole, what their pages take
    /// is measured (a few pages part full make a small set's share no guide). Never part way: the share moves with the
    /// part of the mission going up (20 Nov, the A10: 2x 19's 1.58 near its load's end, 1.29 whole).
    /// </summary>
    constexpr uint64_t PagesMeasuredFromBytes = uint64_t{256} << 20;
    /// <summary>
    /// Port-only (MCHdArt::TakeStagedPicture): the frames a draw's picture taken from the slots waits for the renderer to
    /// ask for it as it draws the frame, before it goes up as the fill's do (MCHdArt::TakeStagedPictures: a frame read for
    /// the CPU alone, whose colours are in the picture alone).
    /// </summary>
    constexpr uint64_t DrawnStagedFrames = 2;

    /// <summary>
    /// Port-only (MCHdArt::StartHold): how many of the hold's reads may be in flight at once: one, so the other readers
    /// are free for what's asked meanwhile, and a disk serves the hold one run at a time.
    /// </summary>
    constexpr size_t HoldReadsAtOnce = 1;
    /// <summary>Port-only: the bytes in a MB, as the frame log's notes of the hold count them.</summary>
    constexpr double BytesPerMb = 1024.0 * 1024.0;

    struct Follow
    {
        FrameKey Key;
        int32_t Frames = 0;
        std::shared_ptr<MCHdArchive> Archive;
        std::string Prefix;
        bool BakedShadows = false;
        /// <summary>
        /// Port-only: the frames to read, when given (pictures read ahead: MCHdArt::ReadPicturesAhead, Key's folder's),
        /// in place of those drawn after Key's; they go to ReadAhead::Pictures, not Ready.
        /// </summary>
        std::vector<AheadEntry> Entries;
        /// <summary>
        /// Port-only: whether to read Key's own frame (its steps from the first), not those drawn after it: a neighbour's
        /// in a turn (MCHdArt::SetNeighbours).
        /// </summary>
        bool Itself = false;
        /// <summary>Port-only: the kind of reads its frames are (Next, unless its Entries are given).</summary>
        AheadKind Kind = AheadKind::Next;
    };

    /// <summary>
    /// Port-only: a frame for a reader to read: the follow it is of (its folder, archive, kind; shared by its frames'
    /// reads, without their entries) and its entry.
    /// </summary>
    struct AheadRead
    {
        std::shared_ptr<const Follow> From;
        AheadEntry Entry;
    };

    /// <summary>Port-only: a reader's handle on an archive's file, kept for the next read of it (ReadAhead::Idle).</summary>
    struct ReaderHandle
    {
        std::shared_ptr<MCHdArchive> Archive;
        MCDiskFile File;
    };

    /// <summary>
    /// A read waiting (ReadAhead::Waiting): the read, the kind it was asked as, and its place in its archive's index
    /// (ReadAhead::ByOffset).
    /// </summary>
    struct WaitingRead
    {
        AheadRead Read;
        AheadKind Kind = AheadKind::Set;
        std::multimap<int64_t, uint64_t>::iterator Place;
    };

    /// <summary>
    /// A span of an archive a reader reads now (ReadAhead::SpansRead; ReadRun): its archive, its bytes, the pack's
    /// generation it was taken in, and the reads asked for since whose entries lie in it, each with the kind it was asked
    /// as, for it to serve from the same bytes (JoinRead).
    /// </summary>
    struct SpanRead
    {
        const MCHdArchive* Archive = nullptr;
        uint64_t Start = 0;
        uint64_t End = 0;
        uint64_t Generation = 0;
        std::vector<std::pair<AheadKind, AheadRead>> Joined;
    };

    /// <summary>A frame read ahead (numbered when the game's thread takes it), and its place in ReadAhead::Order.</summary>
    struct ReadyFrame
    {
        std::shared_ptr<MCHdArt::Frame> Frame;
        std::list<FrameKey>::iterator InOrder;
    };

    /// <summary>
    /// Port-only (MCHdArt::OpenArchivesAhead): an archive for a reader to open: the folders ArchiveOf looks in for it,
    /// nearest first (each looked in until one has an archive), and the pack's generation it was asked in.
    /// </summary>
    struct ArchiveOpen
    {
        std::vector<std::filesystem::path> Folders;
        uint64_t Generation = 0;
        /// <summary>The pack's dictionaries, as the game's thread read them (PackDictionaries).</summary>
        std::shared_ptr<const MCZstd::Dictionaries> Dictionaries;
    };

    /// <summary>
    /// Port-only (MCHdArt::OpenArchivesAhead): an archive a reader opened for the game's thread to take (or why it
    /// couldn't), and the hold's generation (HoldQueue::Generation) its folder was last asked for in: only that hold's
    /// takes it (QueueHold, MCHdArt::StartHold), never a later mission's (a briefing left with its opens in flight).
    /// </summary>
    struct OpenedArchive
    {
        std::expected<std::shared_ptr<MCHdArchive>, std::string> Archive;
        uint64_t HoldGeneration = 0;
    };

    /// <summary>
    /// Port-only (AskAhead): a frame asked for ahead, for a reader to look for in its folder's archive with its
    /// in-betweens (Steps) and a legs packet's turn middles: its folder's follow in its batch (AskBatch::Follows), its
    /// name, the shape size a picture is asked for at (0 x 0 with Steps: a numbered frame's, any), and whether its
    /// folder's frames are named by a hash.
    /// </summary>
    struct AheadAsk
    {
        uint32_t From = 0;
        uint64_t Name = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        bool Steps = false;
        bool TurnMiddles = false;
        bool Hashed = false;
    };

    /// <summary>
    /// Port-only (AskAhead): frames asked for ahead as reads of one kind, at most Room bytes of them, which a reader makes
    /// reads (ExpandAsks), not the game's thread: a unit's packets are hundreds of frames and in-betweens, each a look in
    /// its archive. Each folder's follow once (its archive, its entries' start, its shadows, the kind), and the pack's
    /// generation they were asked in.
    /// </summary>
    struct AskBatch
    {
        std::vector<std::shared_ptr<const Follow>> Follows;
        std::vector<AheadAsk> Asks;
        AheadKind Kind = AheadKind::Set;
        size_t Room = 0;
        uint64_t Generation = 0;
    };

    /// <summary>
    /// Port-only (MCHdArt::StartHold): an archive for the hold to read, and the pieces of it to keep, in the order they
    /// lie: the ground's frames asked for by name (HoldGround: terrain.hda's, relief.hda's and overlays.hda's, whose own
    /// pieces the hold keeps none of, as they hold every map's); none for the pieces the archive keeps
    /// (MCFastFile::HoldPiece::Kept).
    /// </summary>
    struct HoldItem
    {
        std::shared_ptr<MCHdArchive> Archive;
        std::vector<uint32_t> Pieces;
    };

    /// <summary>
    /// Port-only (MCHdArt::StartHold, SPEC-pack-v2.md's Memory): the hold: whether it holds now (a mission's, until let
    /// go), the archives it reads in turn (each it keeps any of once while it holds, MCHdArchive::HoldBytes: Queued; the
    /// ground's pieces by name once each, PiecesQueued), the first one's next piece to look at (an index into its Pieces
    /// when it names them), its generation (a run read across a let-go keeps nothing), how many reads it made, and when it
    /// began.
    /// </summary>
    struct HoldQueue
    {
        bool On = false;
        std::deque<HoldItem> Archives;
        std::unordered_set<std::shared_ptr<MCHdArchive>> Queued;
        /// <summary>
        /// Port-only (MCHdArt::FillGpu): every archive handed it while it holds, kept or not (Full, none of it kept), and
        /// their frames' bytes unpacked (UnpackedKept): the mission's whole size on the GPU (UpdateGpuFill).
        /// </summary>
        std::unordered_set<std::shared_ptr<MCHdArchive>> Handed;
        uint64_t HandedGpuBytes = 0;
        std::unordered_map<std::shared_ptr<MCHdArchive>, std::unordered_set<uint32_t>> PiecesQueued;
        /// <summary>Port-only (MCHdArt::Manifest): each archive queued, whole or its pieces, once, in turn.</summary>
        std::vector<std::shared_ptr<MCHdArchive>> Taken;
        /// <summary>
        /// Port-only (SPEC-memory.md's ground, HoldGround): the ground's frames asked for by name while their archives
        /// open, by the archive's path (KeyOfPath), queued as it opens (QueueHold).
        /// </summary>
        std::unordered_map<PathKey, std::vector<uint64_t>> GroundAsked;
        /// <summary>
        /// Port-only (SPEC-memory.md's "The GPU from the briefing"): the pieces queued by name (PiecesQueued: the
        /// ground's), unpacked: the ground's share of the GPU until the load puts it up (UpdateGpuFill).
        /// </summary>
        uint64_t GroundGpuBytes = 0;
        uint32_t NextStream = 0;
        uint64_t Generation = 0;
        size_t Reads = 0;
        std::chrono::steady_clock::time_point Began;
        /// <summary>
        /// Port-only (SPEC-memory.md's fill): the bytes of the pieces it keeps of the archives queued, and those read, for
        /// the loading bar (MCHdArt::FillShare).
        /// </summary>
        uint64_t QueuedBytes = 0;
        uint64_t ReadBytes = 0;
        /// <summary>Port-only: the bytes of the hold's runs being read now (HoldRun), which the room counts as held.</summary>
        uint64_t InFlightBytes = 0;
        /// <summary>
        /// Port-only (SPEC-memory.md's fill, as much as fits): the hold has met its room (MCHdArt::HoldRoom): it reads and
        /// queues no more this mission.
        /// </summary>
        bool Full = false;
        /// <summary>
        /// Port-only: a mission's load fills (MCHdArt::StartLoadFill to MCHdArt::FinishLoadFill): the hold's reads go
        /// several at once, as the readers are free, rather than HoldReadsAtOnce.
        /// </summary>
        bool Loading = false;
        /// <summary>
        /// Port-only (SPEC-memory.md's "From the briefing"): the mission the hold holds for since its briefing
        /// (MCHdArt::StartBriefingHold), which that mission's load keeps (MCHdArt::HoldsFor); empty when it holds for a
        /// load alone.
        /// </summary>
        std::string Mission;
        /// <summary>
        /// Port-only: the hold reads behind a briefing's screens (MCHdArt::StartBriefingHold to MCHdArt::StartLoadFill),
        /// in runs of MCHdArt::BriefingRunBytes, so a screen's read waits behind one for little.
        /// </summary>
        bool Briefing = false;
        /// <summary>
        /// Port-only (MCHdArt::LoadBarPercent): the most of the loading bar shown since the load began: the fill's share
        /// falls as the load hands the hold more archives, and the bar never goes back.
        /// </summary>
        int32_t BarShown = 0;
    };

    /// <summary>
    /// Port-only (HD art): what the game's thread and the readers share, under Lock. The game's thread hands them each
    /// numbered frame it draws the first time (Follows) and the sets to read ahead (Reads), and takes what they read when
    /// a frame is first drawn; the readers read the frames drawn after each follow, and the sets, those the game's thread
    /// hasn't looked at (Known), several at once (SPEC-first-use.md).
    /// </summary>
    /// <summary>
    /// Port-only (MCHdArt::FillGpu): a frame of the mission's fill a reader put in the upload slots: its record (its
    /// colours left out), and where its picture's bytes lie.
    /// </summary>
    struct StagedRead
    {
        std::shared_ptr<MCHdArt::Frame> Frame;
        MCHdArt::StagedPicture Picture;
    };

    struct ReadAhead
    {
        std::mutex Lock;
        /// <summary>Told of every frame read ahead, follow ended, handle given back and change of what was found.</summary>
        std::condition_variable_any Changed;
        /// <summary>The first draws whose followers aren't in Reads yet: a reader makes them reads, in turn.</summary>
        std::deque<Follow> Follows;
        /// <summary>
        /// The frames to read, by kind (AheadKind), each kind's in the order they were asked for: their numbers in Waiting
        /// (a number no longer there was read with a neighbour, or dropped).
        /// </summary>
        std::array<std::deque<uint64_t>, AheadKinds> Reads;
        /// <summary>
        /// The reads waiting, by number (the next given ReadNumbers), how many of each kind, and each archive's by its
        /// entries' offsets, so a reader finds a read's neighbours waiting to read with it (TakeRun) without looking
        /// through them all.
        /// </summary>
        std::unordered_map<uint64_t, WaitingRead> Waiting;
        std::array<size_t, AheadKinds> WaitingOfKind{};
        std::unordered_map<std::shared_ptr<MCHdArchive>, std::multimap<int64_t, uint64_t>> ByOffset;
        uint64_t ReadNumbers = 0;
        /// <summary>Port-only, for the tests: the readers take no reads (MCHdArt::HoldReads).</summary>
        bool Held = false;
        /// <summary>
        /// Port-only, for the tests: the readers hold each run they take before reading it (MCHdArt::HoldTakenRuns), and
        /// how many hold one now.
        /// </summary>
        bool RunsHeld = false;
        size_t RunsHolding = 0;
        /// <summary>
        /// The spans of the archives the readers read now, each with the reads asked for meanwhile whose entries lie in it,
        /// which it serves too (JoinRead): an entry asked while its stream is read waits on that read rather than read it
        /// again; and how many of those reads wait on one, by kind.
        /// </summary>
        std::list<SpanRead> SpansRead;
        std::array<size_t, AheadKinds> JoinedOfKind{};
        /// <summary>How many of each kind the readers read now.</summary>
        std::array<size_t, AheadKinds> InFlight{};
        /// <summary>
        /// The frames the game's thread has looked at (kept, missing from the pack, unusable, or being read by it), which
        /// the reading thread leaves alone.
        /// </summary>
        std::unordered_set<FrameKey, FrameKeyHash> Known;
        /// <summary>
        /// The frames the readers read now: the game's thread waits for one rather than read it too; port-only, those of
        /// the fill's among them (MCHdArt::FillGpu: a run of another kind's may read one, and the fill's another's).
        /// </summary>
        std::unordered_set<FrameKey, FrameKeyHash> Reading;
        std::unordered_set<FrameKey, FrameKeyHash> GpuReading;
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
        /// <summary>Port-only: the bytes of Pictures' frames.</summary>
        size_t PictureBytes = 0;
        /// <summary>Port-only: Ready's and Pictures' bytes in the ledger, set each frame (MCHdArt::NextFrame).</summary>
        MCMemoryLedger::MCShare Ledger{MCMemoryLedger::MCHolder::HdReadAhead};
        /// <summary>Counts the times what was found was forgotten (the pack set): a read begun before is dropped.</summary>
        uint64_t Generation = 0;
        /// <summary>How many readers work on a follow or a read now.</summary>
        size_t Working = 0;
        /// <summary>Port-only: how many frames they have read as a neighbour's in a turn (Follow::Itself).</summary>
        size_t NeighbourReads = 0;
        /// <summary>How many reads of the archives they have made (a read of neighbouring entries is one).</summary>
        size_t ArchiveReads = 0;
        /// <summary>
        /// The readers' handles on the archives' files not reading now, the most recently given back first (at most
        /// IdleHandlesPerReader for each reader), and how many are reading: ForgetAhead closes the first and waits for the
        /// rest, as the pack's files may go once another is set.
        /// </summary>
        std::list<ReaderHandle> Idle;
        size_t Borrowed = 0;
        /// <summary>How the readers read (MCHdArt::SetReaderSettings), and the game's thread's priority as they started.</summary>
        MCHdArt::ReaderSettings Settings;
        MCThreadPriority GamePriority;
        /// <summary>
        /// Port-only (MCHdArt::OpenArchivesAhead): the archives to open, ahead of everything else (a read needs its
        /// archive's directory); the folders the game's thread waits on while their archives are opened (each open's, not
        /// in Opened yet), each with the hold's generation it was last asked for in (OpenedArchive's); and those opened,
        /// until it takes them (ArchiveFor), by the folder each stands for.
        /// </summary>
        std::deque<ArchiveOpen> Opens;
        std::map<std::filesystem::path, uint64_t> Opening;
        std::map<std::filesystem::path, OpenedArchive> Opened;
        /// <summary>Port-only, for the tests: the readers take no opens (MCHdArt::HoldOpens).</summary>
        bool OpensHeld = false;
        /// <summary>
        /// Port-only (AskAhead): the frames asked for ahead, not made reads yet, in turn; and how many of those batches,
        /// with one a reader is making reads of, are a load's (MCHdArt::WaitForLoadReads waits for them too).
        /// </summary>
        std::deque<AskBatch> Asks;
        size_t LoadAsks = 0;
        /// <summary>Port-only: the hold (MCHdArt::StartHold).</summary>
        HoldQueue Holding;
        /// <summary>
        /// Port-only (MCHdArt::FillGpu): the upload slots the mission's fill puts its pictures in (none: no fill), and
        /// whether the GPU takes BC7; the pictures put there, by frame, the soonest first in StagedOrder (a key no longer
        /// in Staged a draw took), for the game's thread to take (MCHdArt::TakeStagedPictures); how many of the fill's
        /// asks aren't made reads yet; the reads it made, and the frames put in the slots.
        /// </summary>
        std::shared_ptr<MCUploadSlots> Slots;
        bool Bc7 = false;
        std::unordered_map<FrameKey, StagedRead, FrameKeyHash> Staged;
        std::deque<FrameKey> StagedOrder;
        size_t GpuAsks = 0;
        uint64_t GpuReads = 0;
        uint64_t GpuStaged = 0;
        /// <summary>
        /// Port-only (DropGpuReads): how many times the fill was left. A reader making reads of the fill's asks
        /// (ExpandAsks) or reading its frames (ReadRun) since before the last leave stops, its pictures dropped (20 Nov, the
        /// A10: 2x missions 22 and 24, left at the reading after they entered, went up whole in play from the ask a reader
        /// was making reads of).
        /// </summary>
        uint64_t GpuLeaves = 0;
        /// <summary>
        /// Port-only: a fill reader's room, at least this (a frame that found the slots full needed it), and what stops
        /// its wait for it (StopGpuRoomWaits): the fill dropped, the read-ahead forgotten, or other work for the readers.
        /// </summary>
        size_t GpuRoomNeed = 0;
        std::stop_source GpuRoomStop;
        /// <summary>The readers, started with the first follow or set; last, so they stop before the rest goes.</summary>
        std::vector<std::jthread> Readers;
    };

    /// <summary>Port-only (MCHdArt::ReadMenuSetAhead): an entry of the menus' archive, its name and unpacked bytes.</summary>
    struct MenuSetEntry
    {
        std::string Name;
        size_t Bytes = 0;
    };

    /// <summary>Port-only (MCHdArt::GroundOnGpu): where a load's ground is, for the fill on the GPU.</summary>
    enum class GroundState : uint8_t
    {
        /// <summary>From a load's start until its ground goes up.</summary>
        Waiting,
        /// <summary>Up, the room noted before it went up (the card is read again for the next frame's).</summary>
        ReadingCard,
        /// <summary>In the room the card gives the pictures (or no load is under way).</summary>
        InRoom,
    };

    struct State
    {
        /// <summary>The folder set (empty: the default), and whether the folder in use exists (unknown: not looked).</summary>
        std::filesystem::path Folder;
        std::optional<bool> FolderExists;
        /// <summary>The pack's sharp.hda, once looked for (MCHdArt::SharpArchive).</summary>
        std::optional<std::optional<std::filesystem::path>> Sharp;
        std::unordered_map<MCAddressKey, Sequence> Sequences;
        size_t Attached = 0;
        /// <summary>The folders frames were looked up in, and each one's index there.</summary>
        std::vector<FrameFolder> Folders;
        std::unordered_map<PathKey, uint32_t> FolderIndices;
        /// <summary>
        /// Port-only (FindKeyed): the ground's folders' indices (TerrainFolder's, ReliefFolder's, OverlayFolder's) by
        /// their names: making a folder's path and finding it among the rest for each of hundreds of draws a frame cost a
        /// pan up to 0.9 ms a frame with the overlays' hashing (21 Nov).
        /// </summary>
        std::unordered_map<std::string_view, uint32_t> KeyedFolders;
        /// <summary>
        /// FindShape's folders' indices by their shape table's hash: making a folder's path and finding it among the
        /// folders' cost more, every draw, than finding its frame.
        /// </summary>
        std::unordered_map<uint64_t, uint32_t> ShapeTableFolders;
        /// <summary>
        /// The hashes of the tables MCShapeFile read (NoteShapeLoaded): the guard noted each read from the game's
        /// files where it was read, and those from the pack read nothing of the original's, so ShapeTableFolderIndex
        /// notes neither. Kept across packs: the bytes stay the loaders' whichever pack is drawn.
        /// </summary>
        std::unordered_set<uint64_t> LoadedShapeHashes;
        /// <summary>
        /// Port-only (SPEC-first-use.md: a mission's effects at its load): the hashes NoteShapeLoaded took since the last
        /// MCHdArt::ReadLoadedShapesAhead, in the order read, for it to read their frames ahead.
        /// </summary>
        std::vector<uint64_t> ShapeHashesToRead;
        /// <summary>
        /// Port-only (SPEC-memory.md's "Under the loading bar": the opening view): whether an MCHdArt::MCOpeningViewAsks
        /// lives, and the folders asked for under it as the load's reads (MCHdArt::KeepOpeningView gives their frames).
        /// </summary>
        bool OpeningView = false;
        std::unordered_set<uint32_t> OpeningFolders;
        /// <summary>Port-only, for test runs (-hdbriefinghold off): no hold from the briefing, so it starts at the load.</summary>
        bool BriefingHoldOff = false;
        /// <summary>
        /// Port-only (SPEC-memory.md's "The GPU from the briefing"): no fill on the GPU from the briefing, for test runs
        /// (-hdbriefingfill off); whether the briefing's fill runs (from MCHdArt::StartBriefingHold to the load); the
        /// folders the briefing's hold was handed, each once, in turn (its list's but the ground's, then each mech's as
        /// placed: MCHdArt::HoldMechAhead), their keys in BriefingFillKeys; and how far through them the fill has asked.
        /// </summary>
        bool BriefingFillOff = false;
        bool BriefingFill = false;
        std::vector<std::filesystem::path> BriefingFillFolders;
        std::unordered_set<std::string> BriefingFillKeys;
        size_t BriefingFillAsked = 0;
        /// <summary>
        /// Port-only (SPEC-memory.md's "From the briefing": a mission's list): whether a load's fill runs
        /// (MCHdArt::StartLoadFill to MCHdArt::FinishLoadFill), and the pack's folders it handed the hold meanwhile, each
        /// once, in the order handed (NoteLoadFolder: MCHdArt::LoadFolders), its keys in LoadFolderKeys.
        /// </summary>
        bool RecordingLoad = false;
        std::vector<std::filesystem::path> LoadFolders;
        std::unordered_set<std::string> LoadFolderKeys;
        /// <summary>
        /// Port-only (MCHdArt::Manifest): the players' mechs' folders the hold was handed for its mission
        /// (MCHdArt::HoldMechAhead), each once; whether the players' part is final (the load began); and the manifest's
        /// changes. The game's thread's.
        /// </summary>
        std::vector<std::filesystem::path> ManifestPlayers;
        std::unordered_set<std::string> ManifestPlayerKeys;
        /// <summary>Port-only (MCHdArt::Manifest): the mission's own folders its briefing's hold was handed.</summary>
        std::vector<std::string> ManifestFolders;
        bool ManifestFinal = false;
        uint64_t ManifestGeneration = 0;
        /// <summary>
        /// Port-only (MCHdArt::MechTakenOff): each of ManifestPlayers' folders (by its key there) and how many of the
        /// mechs placed now it's one of (none: every mech it's one of was taken off), and the changes to them; the bytes
        /// of the archives handed that only mechs taken off need (TakenOffBytes), as counted at PlacedCounted's change
        /// with HoldQueue::Handed that size.
        /// </summary>
        struct PlacedFolder
        {
            std::filesystem::path Folder;
            uint32_t Mechs = 0;
        };

        std::unordered_map<std::string, PlacedFolder> PlacedFolders;
        uint64_t PlacedGeneration = 0;
        uint64_t PlacedCounted = 0;
        size_t PlacedHanded = 0;
        uint64_t TakenOffGpuBytes = 0;
        std::unordered_map<FrameKey, Cached, FrameKeyHash> Frames;
        /// <summary>The keys of the frames held (those in Frames with one), least recently used first.</summary>
        std::list<FrameKey> Order;
        uint64_t LastId = 0;
        size_t Bytes = 0;
        /// <summary>Port-only (the ledger): what Bytes' frames take, as the allocator rounds their blocks up.</summary>
        size_t AllocatedBytes = 0;
        /// <summary>Port-only: how much of Bytes and AllocatedBytes is the frames' colours (Cached::Colors).</summary>
        size_t ColorBytes = 0;
        size_t ColorAllocatedBytes = 0;
        /// <summary>
        /// Port-only: Bytes and AllocatedBytes in the ledger, set each frame (MCHdArt::NextFrame): the records' and the
        /// colours' apart.
        /// </summary>
        MCMemoryLedger::MCShare Ledger{MCMemoryLedger::MCHolder::HdFrames};
        MCMemoryLedger::MCShare ColorsLedger{MCMemoryLedger::MCHolder::HdColors};
        /// <summary>Port-only: the key of each frame numbered (Cached::Id), for MCHdArt::ColorsOf and the rest.</summary>
        std::unordered_map<uint64_t, FrameKey> ById;
        /// <summary>Port-only (MCHdArt::ColorsReadAgain): the times a frame was read again for its colours.</summary>
        size_t ColorsReadAgain = 0;
        /// <summary>
        /// Port-only (SPEC-memory.md's indices): the frames whose pictures the GPU took (MCHdArt::ColorsUploaded) and the
        /// frame it took each in; IndicesKeptFrames after, each record lets go of its indices unless its folder's are
        /// read on the CPU (FrameFolder::IndicesRead). The times a frame was read again for them (MCHdArt::IndicesOf).
        /// </summary>
        std::vector<std::pair<FrameKey, uint64_t>> Uploaded;
        size_t IndicesReadAgain = 0;
        /// <summary>
        /// Port-only (MCHdArt::FillGpu; SPEC-memory.md's plan, item 2): the mission's fill on the GPU: whether it may run
        /// (from a load's start to its mission's end) and whether the whole mission fits (it fills), the mission's bytes
        /// there (est.: HoldQueue::HandedGpuBytes), the room a card gives the
        /// pictures (MCHdArt::NoteGpuRoom; none on shared memory, planned here); the folders asked for (State::Folders'
        /// indices), those handed in order (FillGpu, then the load's), and how far through each it has asked.
        /// </summary>
        bool GpuFillOn = false;
        bool GpuFits = false;
        /// <summary>
        /// Port-only: whether the mission's whole size is known: from a load's start when a briefing held for it (its
        /// list, the lance as placed), else once the load has made its units (MCHdArt::LoadUnitsMade) or ends.
        /// </summary>
        bool GpuMissionKnown = false;
        /// <summary>
        /// Port-only (MCHdArt::GroundOnGpu): where a load's ground is: not yet up (from the load's start), up with the card
        /// not yet read again since, or in the room the card gives the pictures; the fill enters only once it is.
        /// </summary>
        GroundState GpuGround = GroundState::InRoom;
        /// <summary>
        /// Port-only (PagesShare): how many times their own bytes the HD pictures' pages took, as last measured once a
        /// fill had put a mission up whole (none yet: PagesShareFirst), and whether this load's fill has measured it.
        /// </summary>
        std::optional<double> GpuPagesShare;
        bool GpuPagesMeasured = false;
        uint64_t GpuMissionBytes = 0;
        std::optional<uint64_t> GpuCardRoom;
        std::unordered_set<uint32_t> GpuAsked;
        std::vector<MCHdArt::PacketAhead> GpuOrder;
        size_t GpuOrderAsked = 0;
        size_t LoadFoldersAsked = 0;
        /// <summary>
        /// Port-only (SPEC-memory.md's "The GPU from the briefing": the opening): the held mission's (its list's,
        /// sprite/MCOpeningLists.h, or MCHdArt::SetOpening's), with OpeningGeneration counting its changes; the
        /// mission's own mechs' archives (<c>mechs/legs90/7</c>: SetMissionMechs), which tell the player's from them;
        /// the player's mechs' archives, those placed (in the order placed, as of PlacedGeneration OpeningPlacedAt) and
        /// those the load handed that aren't the mission's (LoadFolders, OpeningLoadScanned of them looked at); the order
        /// built from them (BuildOpening, for OpeningBuiltAt's generation), how far the walk is through it, the bytes
        /// of the folders walked (asked now or before), whether it reached its end, whether it stopped at the opening's
        /// room this frame, and where it last stopped as the frame log noted it; those it asked for (State::Folders'
        /// indices, in GpuAsked too) and each folder's bytes unpacked as counted (FolderGpuBytes).
        /// </summary>
        std::vector<MCHdArt::OpeningPacket> Opening;
        std::string OpeningMission;
        uint64_t OpeningGeneration = 0;
        std::unordered_set<std::string> OpeningMissionMechs;
        std::vector<std::string> OpeningLancePlaced;
        uint64_t OpeningPlacedAt = std::numeric_limits<uint64_t>::max();
        std::vector<std::string> OpeningLanceLoaded;
        size_t OpeningLoadScanned = 0;
        std::vector<std::filesystem::path> OpeningOrder;
        uint64_t OpeningBuiltAt = std::numeric_limits<uint64_t>::max();
        size_t OpeningWalked = 0;
        uint64_t OpeningWalkedBytes = 0;
        bool OpeningDone = false;
        bool OpeningAtRoom = false;
        size_t OpeningNotedAt = std::numeric_limits<size_t>::max();
        std::unordered_set<uint32_t> OpeningAsked;
        std::unordered_map<uint32_t, uint64_t> OpeningFolderBytes;
        /// <summary>
        /// Port-only (MCHdArt::FillGpu): the pictures taken up (by the renderer: TakeStagedPictures, TakeStagedPicture)
        /// and their bytes; a draw's taken from the slots (TakeAhead, kept here until the renderer asks for it as it draws
        /// it: MCHdArt::TakeStagedPicture, or DrawnStagedFrames have gone and it goes up as the fill's do), by the frame's
        /// number, with the frame it was taken in; and when the last went up (FinishLoadFill's stall).
        /// </summary>
        uint64_t GpuUp = 0;
        uint64_t GpuUpBytes = 0;
        /// <summary>Port-only: GpuUp and ReadAhead::GpuReads as the last load began (the bar's and its note's share).</summary>
        uint64_t GpuUpAtLoad = 0;
        uint64_t GpuReadsAtLoad = 0;
        std::optional<MCHdArt::StagedPicture> TakenStaged;
        std::unordered_map<uint64_t, std::pair<MCHdArt::StagedPicture, uint64_t>> DrawnStaged;
        std::chrono::steady_clock::time_point GpuUpAt;
        /// <summary>
        /// Port-only (MCHdArt::KeepIndices): whether version 2 frames keep their indices whatever their folder (the
        /// weather shown, which measures every unit's frames as they're drawn).
        /// </summary>
        bool KeepMarkedIndices = false;
        /// <summary>
        /// Port-only (the ledger): the frames the cache let go of while something else held them (a texture's picture,
        /// a draw's), until they go, and their bytes in the ledger, set each frame (MCHdArt::NextFrame).
        /// </summary>
        std::vector<LetGoFrame> LetGo;
        MCMemoryLedger::MCShare LetGoLedger{MCMemoryLedger::MCHolder::HdFramesLetGo};
        /// <summary>The budget set (0: none, the plan's share: MCHdArt::CacheBudget).</summary>
        size_t Budget = 0;
        /// <summary>Port-only: the frames begun (MCHdArt::NextFrame).</summary>
        uint64_t Frame = 0;
        size_t Read = 0;
        /// <summary>How many of those the reading thread read (ReadAhead).</summary>
        size_t TakenAhead = 0;
        /// <summary>Port-only: the readers' reads (ReadAhead::ArchiveReads) the frame log has counted.</summary>
        size_t ReaderReadsLogged = 0;
        /// <summary>Port-only: the archives taken open from the readers (MCHdArt::OpenArchivesAhead).</summary>
        size_t OpenedAhead = 0;
        /// <summary>
        /// Port-only: the times the readers forgot what they read (ReadAhead::Generation's count, the game's thread's
        /// own: MCHdArt::AheadGeneration reads it without their lock).
        /// </summary>
        uint64_t Forgets = 0;
        /// <summary>
        /// Port-only: whether some of what was asked for ahead was dropped (from the cache or the readers' own) or left
        /// out for want of room (set by the readers too), and the times and frame the askers were last told to ask
        /// again for it all (MCHdArt::NextFrame, at most every AskAgainFrames).
        /// </summary>
        std::atomic<bool> AskAgain = false;
        uint64_t ReAsks = 0;
        uint64_t AskAgainAt = 0;
        std::chrono::nanoseconds ReadTime{0};
        /// <summary>The folders whose problems were logged (once each).</summary>
        std::set<std::filesystem::path> Logged;
        /// <summary>The pack's palette.rgb, once read (Looked), and the pack's version.</summary>
        std::optional<std::array<uint8_t, MCHdArt::PaletteBytes>> MadeWith;
        bool PaletteLooked = false;
        uint64_t Version = 1;
        /// <summary>The folder found for the pack when none is set (the user folder's HD, else the game folder's).</summary>
        std::optional<std::filesystem::path> FoundFolder;
        /// <summary>
        /// The pack's pack.cfg, once read: whether its format is one this code reads, the format (1 without the file),
        /// and its sprite files.
        /// </summary>
        bool PackFileLooked = false;
        bool PackFormatReadable = true;
        int32_t PackFormatRead = 1;
        std::optional<std::string> PackSprites;
        /// <summary>The pack's dicts.hdd, once read (DictionariesLooked; null: none, or it can't be read).</summary>
        bool DictionariesLooked = false;
        std::shared_ptr<const MCZstd::Dictionaries> Dictionaries;
        /// <summary>
        /// Port-only (MenuSetOf, the game's thread's): the menus' archive whose entries MenuSet holds, in the order they
        /// lie in it (a version 2 archive's read from its names once, SPEC-pack-v2.md).
        /// </summary>
        std::weak_ptr<const MCHdArchive> MenuSetFrom;
        std::vector<MenuSetEntry> MenuSet;
        /// <summary>This game's sprite files (MCHdArt::SetSpriteFiles; empty: not known yet).</summary>
        std::string SpriteFiles;
        /// <summary>
        /// Whether the pack's sprite files are this game's (MCHdArt::NumberedFolders), once worked out: asked each frame
        /// for every object near the view (MCAppearance::ReadAheadNear). Forgotten when either changes.
        /// </summary>
        std::optional<bool> SpritesMatch;
        /// <summary>
        /// Port-only (ListPack): the pack's folders and archives as found once, the first time the game's thread asks
        /// (IsDirectory, IsArchive), by ListedName: it holds a few dozen folders and about a thousand files, so it is
        /// listed in milliseconds, and nothing the game's thread asks of the pack's layout looks at the disk after.
        /// </summary>
        std::optional<std::unordered_set<std::string>> Listed;
        /// <summary>The archives looked for, by the folder each stands for (null: none there).</summary>
        std::unordered_map<PathKey, std::shared_ptr<MCHdArchive>> Archives;
        /// <summary>
        /// Guards the archives' files kept open (MCFastFile's reads, Release and IsOpen) and the four below: the reading
        /// thread reads through a handle of its own (a file's stream can't be read from two threads), so it holds this
        /// only to count that one, which may close one of these. Never held with ReadAhead::Lock.
        /// </summary>
        std::mutex ArchiveFiles;
        /// <summary>The readers' own handles open (ReadAhead's, reading or kept), counted against MaxOpenArchiveFiles.</summary>
        size_t ReaderFiles = 0;
        /// <summary>How many times the readers have opened handles of their own (MCHdArt::ReaderFilesOpened).</summary>
        size_t ReaderOpens = 0;
        /// <summary>
        /// The archives' files kept open, the most recently read first (at most MaxOpenArchiveFiles), and each one's
        /// place there.
        /// </summary>
        std::list<std::weak_ptr<MCFastFile>> OpenFiles;
        std::map<std::weak_ptr<MCFastFile>, std::list<std::weak_ptr<MCFastFile>>::iterator, std::owner_less<>> OpenAt;
        /// <summary>The shape tables drawn with smooth edges (SetSmoothEdgeTables).</summary>
        std::unordered_set<MCAddressKey> SmoothTables;
        /// <summary>
        /// The shape frames redrawn as line art (SetLineArtFrames): which set each is in, and its parts drawn by hand.
        /// </summary>
        std::map<std::pair<MCAddressKey, int32_t>,
                 std::pair<MCHdArt::LineArtSet, std::vector<MCSharpLines::MCDrawnLoop>>>
            LineArtFrames;
        /// <summary>The pictures the game made of the pack's (MCHdArt::KeepPicture), by their pixels' hash.</summary>
        std::unordered_map<uint64_t, std::shared_ptr<const MCHdArt::Frame>> KeptPictures;
        /// <summary>Port-only: the frames kept for the mission's end (Cached::ForMission, MCHdArt::KeepForMission).</summary>
        std::vector<FrameKey> ForMission;
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
    /// Closes the least recently read of the archives' files kept open past MaxOpenArchiveFiles, the readers' own
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

    /// <summary>Whether <paramref name="rest"/> is a frame file's extension (.hdf), in either case as Find compares.</summary>
    bool IsHdfExtension(std::string_view rest)
    {
        return rest.size() == HdfExtension.size() && Upper(rest) == Upper(HdfExtension);
    }

    /// <summary>
    /// Port-only: <paramref name="path"/> as the pack's listing names it (State::Listed): in the generic form, upper-cased,
    /// a folder's with a trailing separator, so that every way of writing a path finds it (the systems compare names
    /// without regard to case).
    /// </summary>
    std::string ListedName(const std::filesystem::path& path, bool folder)
    {
        std::string name = Upper(MCFileSystem::ToUtf8(path.lexically_normal()));
        std::ranges::replace(name, '\\', '/');

        while (name.size() > 1 && name.back() == '/')
        {
            name.pop_back();
        }

        return folder ? name + '/' : name;
    }

    /// <summary>
    /// Port-only: the pack's listing (State::Listed), made the first time it's asked for: the pack's folder, each folder
    /// in it and each file. A pack set again is listed again (MCHdArt::SetPackFolder).
    /// </summary>
    const std::unordered_set<std::string>& ListPack()
    {
        State& state = TheState();

        if (state.Listed.has_value())
        {
            return *state.Listed;
        }

        std::unordered_set<std::string>& listed = state.Listed.emplace();
        const std::filesystem::path pack = MCHdArt::PackFolder();
        std::error_code error;

        if (!std::filesystem::is_directory(pack, error))
        {
            return listed;
        }

        listed.insert(ListedName(pack, true));

        for (auto at = std::filesystem::recursive_directory_iterator(
                 pack, std::filesystem::directory_options::skip_permission_denied, error);
             !error && at != std::filesystem::recursive_directory_iterator(); at.increment(error))
        {
            std::error_code kind;
            listed.insert(ListedName(at->path(), at->is_directory(kind)));
        }

        if (error)
        {
            LogOnce(pack, std::format("listing the pack stopped: {}", error.message()));
        }

        return listed;
    }

    /// <summary>
    /// Port-only: whether <paramref name="path"/>, in the pack, is a folder: from the pack's listing (ListPack), so
    /// without a look at the disk (a look is tens of microseconds, more with Windows' file scanning, and the game's
    /// thread asks of each folder it finds frames in). The game's thread's.
    /// </summary>
    bool IsDirectory(const std::filesystem::path& path)
    {
        return ListPack().contains(ListedName(path, true));
    }

    /// <summary>Port-only: whether the pack holds <paramref name="folder"/>'s archive (its <c>.hda</c>), as IsDirectory.</summary>
    bool IsArchive(const std::filesystem::path& folder)
    {
        return ListPack().contains(ListedName(std::filesystem::path(folder).concat(ArchiveExtension), false));
    }

    /// <summary>
    /// The pack's dictionaries (its dicts.hdd), read the first time the game's thread asks after the pack is set and
    /// held while it's in use: null when it has none, or they can't be read (logged once), so that its archives packed
    /// against one are refused and their art drawn from the original.
    /// </summary>
    std::shared_ptr<const MCZstd::Dictionaries> PackDictionaries()
    {
        State& state = TheState();

        if (state.DictionariesLooked)
        {
            return state.Dictionaries;
        }

        state.DictionariesLooked = true;
        const std::filesystem::path path = MCHdArt::PackFolder() / DictionariesFile;
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return nullptr;
        }

        const uintmax_t length = std::filesystem::file_size(path, error);

        if (error || length > MostDictionariesFileBytes)
        {
            LogOnce(path,
                    std::format("{} can't be sized, or is over {} bytes ({}): the archives packed against it are "
                                "left out",
                                DictionariesFile, MostDictionariesFileBytes, error ? error.message() : "too long"));
            return nullptr;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        std::ifstream in(path, std::ios::binary);

        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            LogOnce(path,
                    std::format("{} couldn't be read: the archives packed against it are left out", DictionariesFile));
            return nullptr;
        }

        std::expected<std::shared_ptr<const MCZstd::Dictionaries>, std::string> read =
            MCZstd::Dictionaries::Read(bytes);

        if (!read)
        {
            LogOnce(path,
                    std::format("{}: {}; the archives packed against it are left out", DictionariesFile, read.error()));
            return nullptr;
        }

        state.Dictionaries = std::move(*read);
        return state.Dictionaries;
    }

    /// <summary>
    /// The archive standing for <paramref name="folder"/> (its <c>.hda</c>) opened, its directory read, its streams
    /// unpacking against <paramref name="dictionaries"/> (PackDictionaries, which the game's thread reads): null when
    /// there's none, what's wrong when it can't be read. Touches nothing shared, so a reader opens archives ahead with it
    /// (MCHdArt::OpenArchivesAhead).
    /// </summary>
    std::expected<std::shared_ptr<MCHdArchive>, std::string> OpenArchive(
        const std::filesystem::path& folder, std::shared_ptr<const MCZstd::Dictionaries> dictionaries)
    {
        const std::filesystem::path path = std::filesystem::path(folder).concat(ArchiveExtension);
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return nullptr;
        }

        auto file = MCFastFile::Create(MCFileSystem::ToUtf8(path), std::move(dictionaries));

        if (!file)
        {
            return std::unexpected(std::move(file.error()));
        }

        auto archive = std::make_shared<MCHdArchive>();
        archive->File = std::move(*file);
        archive->Path = path;
        // The last numbered entry's folder and its count: a folder's entries come together.
        std::string_view lastFolder;
        int32_t* lastFrames = nullptr;

        for (const MCFileEntry& entry : archive->File->GetEntries())
        {
            const std::string_view name = entry.GetName();
            archive->Folders.insert(Upper(name.substr(0, name.find(ArchiveSeparator))));

            // A numbered frame's entry, <folder>\<frame>.hdf: its folder holds at least the frames up to it.
            const size_t split = name.rfind(ArchiveSeparator);
            const std::string_view folder =
                split == std::string_view::npos ? std::string_view() : name.substr(0, split + 1);
            const std::string_view leaf = name.substr(folder.size());
            int32_t frame = 0;
            const auto [end, error] = std::from_chars(leaf.data(), leaf.data() + leaf.size(), frame);

            if (error == std::errc() && end != leaf.data() && frame >= 0 && frame < MostArchivedFrames &&
                IsHdfExtension(leaf.substr(static_cast<size_t>(end - leaf.data()))))
            {
                if (lastFrames == nullptr || folder != lastFolder)
                {
                    lastFolder = folder;
                    lastFrames = &archive->FrameCounts[Upper(folder)];
                }

                *lastFrames = std::max(*lastFrames, frame + 1);
            }
        }

        // Port-only (QueueHold): a version 2 archive's packets' or heads' streams; a version 1 archive's entries when
        // they're numbered frames (a mech part's or an object's), not the ground's, the pictures' or the shapes' named by
        // their hashes.
        if (const MCFastFile& opened = *archive->File; opened.IsVersion2() || !archive->FrameCounts.empty())
        {
            for (uint32_t piece = 0; piece < opened.HoldPieceCount(); ++piece)
            {
                const MCFastFile::HoldPiece facts = opened.HoldPieceAt(piece);
                archive->HoldBytes += facts.Kept ? facts.Stored : 0;
            }
        }

        return archive;
    }

    /// <summary>
    /// Port-only (MCHdArt::OpenArchivesAhead): the archive the readers opened for <paramref name="folder"/>, taken from
    /// them, waiting while they open it; none when they weren't asked to. Never with ReadAhead::Lock held.
    /// </summary>
    std::optional<std::expected<std::shared_ptr<MCHdArchive>, std::string>> TakeOpened(
        const std::filesystem::path& folder)
    {
        ReadAhead& ahead = TheState().Ahead;
        std::unique_lock lock(ahead.Lock);

        // Port-only (lib/MCFirstUse.h): an archive the game's thread waits on the readers to open in play.
        if (ahead.Opening.contains(folder))
        {
            const MCFirstUseScope use(
                MCFirstUseKind::Wait,
                [&] { return std::format("an archive being opened: {}", MCFileSystem::ToUtf8(folder)); }, 0);
            ahead.Changed.wait(lock, [&] { return !ahead.Opening.contains(folder); });
        }

        auto opened = ahead.Opened.extract(folder);

        if (opened.empty())
        {
            return std::nullopt;
        }

        return std::move(opened.mapped().Archive);
    }

    void QueueHold(ReadAhead& ahead, const std::shared_ptr<MCHdArchive>& archive);
    void ClearOpening(State& state);
    bool HoldsArchive(const MCHdArchive& archive);
    uint64_t UnpackedKept(const MCFastFile& file);
    bool IsHashName(std::string_view name);

    /// <summary>
    /// Port-only (SPEC-memory.md's "From the briefing": a mission's list): notes <paramref name="folder"/> (one of the
    /// pack's, relative to it) among those the load handed the hold (MCHdArt::LoadFolders), while a load's fill runs.
    /// </summary>
    void NoteLoadFolder(const std::filesystem::path& folder)
    {
        State& state = TheState();

        if (state.RecordingLoad && state.LoadFolderKeys.insert(folder.generic_string()).second)
        {
            state.LoadFolders.push_back(folder);
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "The GPU from the briefing"): notes <paramref name="folders"/> (the pack's, relative to
    /// it) among those the briefing's hold was handed, each once, for the fill on the GPU (State::BriefingFillFolders).
    /// </summary>
    void NoteBriefingFillFolders(State& state, std::span<const std::filesystem::path> folders)
    {
        for (const std::filesystem::path& folder : folders)
        {
            if (state.BriefingFillKeys.insert(folder.generic_string()).second)
            {
                state.BriefingFillFolders.push_back(folder);
            }
        }
    }

    /// <summary>Whether <paramref name="folder"/> is <paramref name="archive"/>'s folder or lies under it.</summary>
    bool Under(std::string_view folder, std::string_view archive)
    {
        return folder == archive ||
               (folder.size() > archive.size() && folder.starts_with(archive) && folder[archive.size()] == '/');
    }

    /// <summary>
    /// Port-only (MCHdArt::MechTakenOff): whether <paramref name="folder"/> (the pack's) is only the mechs' taken off: one
    /// of a mech's placed, none placed now, and none of the mission's own.
    /// </summary>
    bool TakenOff(const State& state, const std::filesystem::path& folder)
    {
        if (state.PlacedFolders.empty())
        {
            return false;
        }

        const std::string key = folder.generic_string();
        const auto placed = state.PlacedFolders.find(key);
        return placed != state.PlacedFolders.end() && placed->second.Mechs == 0 &&
               !std::ranges::contains(state.ManifestFolders, key);
    }

    /// <summary>
    /// Port-only (MCHdArt::MechTakenOff): the bytes unpacked (UnpackedKept) of the archives handed the hold that only
    /// mechs taken off need: an archive standing for a folder of theirs (the nearest of its folders that has one, as
    /// OpenArchivesAhead finds it) that no folder of a mech placed or of the mission's own lies under. Counted again only
    /// when the mechs placed or the archives handed change; a mech's archives found in a few lookups each, so swapping
    /// mechs costs a frame little. The game's thread's; call with ReadAhead::Lock held.
    /// </summary>
    uint64_t TakenOffBytes(State& state, const std::filesystem::path& pack)
    {
        const HoldQueue& holding = state.Ahead.Holding;

        if (state.PlacedCounted == state.PlacedGeneration && state.PlacedHanded == holding.Handed.size())
        {
            return state.TakenOffGpuBytes;
        }

        state.PlacedCounted = state.PlacedGeneration;
        state.PlacedHanded = holding.Handed.size();
        state.TakenOffGpuBytes = 0;
        std::vector<const std::filesystem::path*> off;
        std::vector<std::string_view> needed(state.ManifestFolders.begin(), state.ManifestFolders.end());

        for (const auto& [key, placed] : state.PlacedFolders)
        {
            if (placed.Mechs == 0)
            {
                off.push_back(&placed.Folder);
            }
            else
            {
                needed.push_back(key);
            }
        }

        std::unordered_set<const MCHdArchive*> counted;

        for (const std::filesystem::path* folder : off)
        {
            for (std::filesystem::path at = pack / *folder; at.has_relative_path() && at != pack; at = at.parent_path())
            {
                // The game's thread's archives, or one the readers opened that it hasn't taken yet; a folder without
                // one of its own passed for those above it.
                std::shared_ptr<MCHdArchive> archive;

                if (const auto known = state.Archives.find(KeyOfPath(at)); known != state.Archives.end())
                {
                    archive = known->second;
                }
                else if (const auto opened = state.Ahead.Opened.find(at); opened != state.Ahead.Opened.end())
                {
                    if (!opened->second.Archive.has_value())
                    {
                        break;
                    }

                    archive = *opened->second.Archive;
                }

                if (archive == nullptr)
                {
                    continue;
                }

                const std::string name = at.lexically_relative(pack).generic_string();

                if (holding.Handed.contains(archive) && counted.insert(archive.get()).second &&
                    std::ranges::none_of(needed, [&name](std::string_view listed) { return Under(listed, name); }))
                {
                    state.TakenOffGpuBytes += UnpackedKept(*archive->File);
                }

                break;
            }
        }

        return state.TakenOffGpuBytes;
    }

    /// <summary>
    /// The archive standing for <paramref name="folder"/> (its <c>.hda</c>), opened once (by the readers when asked
    /// ahead: TakeOpened); null when none. Never with ReadAhead::Lock held.
    /// </summary>
    std::shared_ptr<MCHdArchive> ArchiveFor(const std::filesystem::path& folder)
    {
        auto [found, fresh] = TheState().Archives.try_emplace(KeyOfPath(folder));

        if (!fresh)
        {
            return found->second;
        }

        // Port-only: the game's thread's wait for a reader's open, or its own (the frame log's).
        const MCFrameLog::Scope part(FrameLogOpen);
        // None when the pack's listing has none (a packet's folder, its frames in its part's archive).
        std::optional<std::expected<std::shared_ptr<MCHdArchive>, std::string>> opened = TakeOpened(folder);
        std::expected<std::shared_ptr<MCHdArchive>, std::string> archive =
            opened.has_value()  ? std::move(*opened)
            : IsArchive(folder) ? OpenArchive(folder, PackDictionaries())
                                : std::expected<std::shared_ptr<MCHdArchive>, std::string>(nullptr);

        if (!archive)
        {
            LogOnce(std::filesystem::path(folder).concat(ArchiveExtension), archive.error());
            return nullptr;
        }

        // One the readers opened is among the files kept open since (OpenAhead), or closed since as read longest ago.
        if (*archive != nullptr && opened.has_value())
        {
            ++TheState().OpenedAhead;
        }
        else if (*archive != nullptr)
        {
            std::scoped_lock lock(TheState().ArchiveFiles);
            KeepOpen((*archive)->File);
        }

        found->second = std::move(*archive);

        // Port-only: one opened while a mission's art is held is held too (MCHdArt::StartHold), and is the manifest's.
        if (found->second != nullptr)
        {
            ++TheState().ManifestGeneration;
            ReadAhead& ahead = TheState().Ahead;
            std::scoped_lock lock(ahead.Lock);
            QueueHold(ahead, found->second);
        }

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
                // Port-only (MCHdArt::LoadFolders): a folder of frames the load draws or reads ahead, its archive held; one
                // named by a hash as its archive's folder, which any of them opens (each shape's hash names one of
                // shapes').
                if (HoldsArchive(*archive))
                {
                    const bool hashed = at != folder.Path && IsHashName(folder.Path.filename().generic_string());
                    NoteLoadFolder((hashed ? at : folder.Path).lexically_relative(pack));
                }

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
    /// Port-only (SPEC-pack-v2.md's facts): the shape table record (record 1) of the folder <paramref name="archived"/>
    /// stands for in its version 2 archive; none when it holds none (or the archive is version 1's).
    /// </summary>
    std::optional<std::span<const uint8_t>> ShapeRecordOf(const FrameFolder::InArchive& archived)
    {
        // The folder in its archive: the start of its frames' names without the separator after it.
        const std::string_view inArchive =
            std::string_view(archived.Prefix).substr(0, archived.Prefix.empty() ? 0 : archived.Prefix.size() - 1);

        for (const auto& [type, data] : archived.Archive->File->Facts(inArchive))
        {
            if (type == static_cast<uint16_t>(MCPackRecords::Type::ShapeTable))
            {
                return data;
            }
        }

        return std::nullopt;
    }

    /// <summary>
    /// The frames <paramref name="folder"/>'s archive holds: one past the highest numbered frame (not an in-between or
    /// turn middle) among its entries (a packet may skip a number: an empty frame of the original's), as the archive
    /// found them when it opened (MCHdArchive::FrameCounts); a version 2 archive's, its shape table's count, which none
    /// lies past. Counted once a folder (FrameFolder::InArchive::Frames). 0 outside an archive.
    /// </summary>
    int32_t ArchivedFrames(FrameFolder& folder)
    {
        ArchiveOf(folder);
        FrameFolder::InArchive& archived = *folder.Archived;

        if (archived.Archive == nullptr)
        {
            return 0;
        }

        if (archived.Frames.has_value())
        {
            return *archived.Frames;
        }

        // A version 2 archive keeps no names. Port-only: a packet's folder holds its shape table (record 1), whose count
        // none of its frames lies past (every folder of pack v2's, 21 Nov), so the count is read: trying each name took
        // 58 us a folder, on the game's thread as the fill asks (mission 1's briefing: 3,115 folders, 180 ms in one
        // frame). A folder without one (a mod's pack's) is found by its keys.
        if (archived.Archive->File->IsVersion2())
        {
            if (const std::optional<std::span<const uint8_t>> record = ShapeRecordOf(archived); record.has_value())
            {
                const std::span<const uint8_t> table =
                    record->subspan(std::min(MCShape::TableStart(*record), record->size()));
                archived.Frames = std::clamp(VfxShapeCount(table), 0, MostArchivedFrames);
                return *archived.Frames;
            }

            int32_t count = 0;

            for (int32_t frame = 0; frame < MostArchivedFrames; ++frame)
            {
                if (archived.Archive->File->Find(std::format("{}{}{}", archived.Prefix, frame, HdfExtension)))
                {
                    count = frame + 1;
                }
            }

            archived.Frames = count;
            return count;
        }

        const auto found = archived.Archive->FrameCounts.find(Upper(archived.Prefix));
        archived.Frames = found == archived.Archive->FrameCounts.end() ? 0 : found->second;
        return *archived.Frames;
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "The GPU from the briefing"): the folders of the packets of <paramref name="folder"/>'s
    /// archive but its own, each named from <paramref name="name"/> (its folder, relative to the pack), when the folder
    /// names a type as the lists do (MCScenario::HoldList: its packet 0's, which opens its archive whether or not the type
    /// has a packet 0) and its archive is opened already and holds the type's packets at its top (a mech's part's, an
    /// object's); none otherwise. A type's packets may skip numbers: they are looked for by number, up to as many as the
    /// archive has folders.
    /// </summary>
    std::vector<std::filesystem::path> TypePackets(const FrameFolder& folder, const std::filesystem::path& name)
    {
        constexpr std::string_view FirstPacket = "0";
        std::vector<std::filesystem::path> packets;

        if (!folder.Archived.has_value() || folder.Archived->Archive == nullptr || name.filename() != FirstPacket ||
            folder.Archived->Prefix != std::format("{}{}", FirstPacket, ArchiveSeparator))
        {
            return packets;
        }

        const MCHdArchive& archive = *folder.Archived->Archive;
        const bool version2 = archive.File->IsVersion2();
        // Version 2's rows: each packet's and the archive's own folder's (the empty path); version 1's first folders: each
        // packet's.
        const size_t rows = archive.File->FolderCount();
        const size_t count = version2 ? rows - (rows > 0 && archive.File->HasFolder(std::string_view()) ? 1 : 0)
                                      : archive.Folders.size();
        size_t found = 0;

        for (int32_t packet = 0; packet < MostArchivedPackets && found < count; ++packet)
        {
            const std::string number = std::to_string(packet);

            if (version2 ? archive.File->HasFolder(number) : archive.Folders.contains(number))
            {
                ++found;

                if (number != FirstPacket)
                {
                    packets.push_back(name.parent_path() / number);
                }
            }
        }

        return packets;
    }

    /// <summary>Whether <paramref name="entry"/> unpacks to no more than a frame file may be (MaxHdfBytes).</summary>
    bool FitsFrameFile(const MCFileEntry& entry)
    {
        return entry.Size >= 0 && entry.RealSize >= 0 && static_cast<size_t>(entry.RealSize) <= MaxHdfBytes;
    }

    /// <summary>
    /// Entry <paramref name="index"/> of <paramref name="file"/> (named <paramref name="name"/>, for what is wrong) from
    /// its <paramref name="stored"/> bytes, a <c>.hdf</c> file, unpacked (MCFastFile::Unpack: inflated when packed, or
    /// cut from its version 2 stream), as a frame for a shape of <paramref name="width"/> x <paramref name="height"/>
    /// (0 x 0: any).
    /// </summary>
    std::expected<MCHdArt::Frame, std::string> DecodeEntry(const MCFastFile& file, int32_t index,
                                                           std::span<const uint8_t> stored, std::string_view name,
                                                           int32_t width, int32_t height)
    {
        if (!FitsFrameFile(file.EntryAt(index)))
        {
            return std::unexpected(std::format("{} is too large", name));
        }

        std::expected<std::vector<uint8_t>, std::string> unpacked = file.Unpack(index, stored);

        if (!unpacked)
        {
            return std::unexpected(std::format("{} doesn't unpack: {}", name, unpacked.error()));
        }

        return MCHdArt::DecodeHdf(*unpacked, width, height);
    }

    /// <summary>
    /// Entry <paramref name="index"/> of <paramref name="archive"/> (named <paramref name="name"/>, for what is wrong),
    /// a <c>.hdf</c> file, inflated when packed, as a frame for a shape of <paramref name="width"/> x
    /// <paramref name="height"/> (0 x 0: any): read through a reader's handle <paramref name="own"/>, else the archive's
    /// own, under State::ArchiveFiles.
    /// </summary>
    std::expected<MCHdArt::Frame, std::string> ReadEntry(const MCHdArchive& archive, int32_t index,
                                                         std::string_view name, int32_t width, int32_t height,
                                                         MCOptionalRef<MCDiskFile> own = std::nullopt)
    {
        const MCFileEntry& entry = archive.File->EntryAt(index);
        // Port-only (lib/MCFirstUse.h): an HD frame the game's thread reads in play (or unpacks, below), not a reader.
        MCFirstUseScope use(
            MCFirstUseKind::Read,
            [&]
            {
                return std::format("{} {}",
                                   MCFileSystem::ToUtf8(archive.Path.lexically_relative(MCHdArt::PackFolder())), name);
            },
            static_cast<uint64_t>(std::max(entry.RealSize, 0)));

        // Checked before the read, so a malformed pack can't make the game read a huge entry.
        if (!FitsFrameFile(entry))
        {
            return std::unexpected(std::format("{} is too large", name));
        }

        // A version 2 entry whose stream is kept unpacked from an earlier read (the readers', its packet's) needs no read.
        if (const std::shared_ptr<const std::vector<uint8_t>> kept = archive.File->KeptStreamOf(index); kept != nullptr)
        {
            use.Kind = MCFirstUseKind::Unpack;
            return MCHdArt::DecodeHdf(archive.File->EntryIn(index, *kept), width, height);
        }

        // Port-only: nor one whose stored bytes are held (MCHdArt::StartHold): it's unpacked from memory.
        if (const std::optional<MCFastFile::Held> held = archive.File->HeldOf(index); held.has_value())
        {
            use.Kind = MCFirstUseKind::Unpack;
            return DecodeEntry(*archive.File, index, held->Stored, name, width, height);
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

        return DecodeEntry(*archive.File, index, stored, name, width, height);
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

    /// <summary>
    /// Port-only: whether <paramref name="folder"/> holds frame <paramref name="frame"/> (its frame file or PNG pair, else
    /// its archive's entry), looked for by name, without a read.
    /// </summary>
    bool HoldsFrame(FrameFolder& folder, std::string_view frame)
    {
        std::error_code error;

        if (!folder.Loose.has_value())
        {
            folder.Loose = std::filesystem::is_directory(folder.Path, error);
        }

        if (const std::filesystem::path path = folder.Path / frame;
            *folder.Loose &&
            (std::filesystem::is_regular_file(std::filesystem::path(path).concat(HdfExtension), error) ||
             std::filesystem::is_regular_file(std::filesystem::path(path).concat(PngExtension), error)))
        {
            return true;
        }

        const FrameFolder::InArchive& archived = ArchiveOf(folder);
        return archived.Archive != nullptr &&
               archived.Archive->File->Find(archived.Prefix + std::string(frame) + std::string(HdfExtension))
                   .has_value();
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
                // A version 2 archive keeps no names: its folders are found by their keys.
                const std::string first = rest.empty() ? std::string() : MCFileSystem::ToUtf8(*rest.begin());
                return !rest.empty() && (archive->File->IsVersion2() ? archive->File->HasFolder(first)
                                                                     : archive->Folders.contains(Upper(first)));
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
    /// the next frame (an effect's, a walk's), or (HDTurns) a legs frame's turn middle and its in-betweens; or a walk's
    /// in-betweens round its loop's wrap (WrapStep), its turn middle's too. Its members are numbered from the frame's own
    /// (or the turn middle's), 0.
    /// </summary>
    struct StepFamily
    {
        /// <summary>The turn middle's step (TurnStep or TurnMirroredStep); 0: the frame's own.</summary>
        int32_t Turn = 0;
        /// <summary>The member a step is, and how many members there may be.</summary>
        int32_t Member = 0;
        int32_t Members = MaxInBetweenSteps;
        /// <summary>Whether its in-betweens are toward the loop's first frame (WrapStep), not the art's next.</summary>
        bool Wrap = false;

        /// <summary>
        /// The step that names member <paramref name="member"/>: a wrap's member 0 is the frame's own (or its turn
        /// middle), which has no wrap of its own.
        /// </summary>
        int32_t StepOf(int32_t member) const
        {
            const int32_t step = Turn == 0 ? member : TurnBetweenStep(Turn, member);
            return Wrap && member > 0 ? WrapBetweenStep(step) : step;
        }

        /// <summary>The family of the frames after: the art's next frames have their own in-betweens, not a wrap's.</summary>
        StepFamily Unwrapped() const { return {Turn, 0, Members, false}; }
    };

    /// <summary>The family <paramref name="step"/> (a numbered frame key's, NumberedKey) is of, and its member.</summary>
    StepFamily FamilyOf(uint64_t step)
    {
        const bool wrap = (step & static_cast<uint64_t>(WrapStep)) != 0;
        step &= ~static_cast<uint64_t>(WrapStep);

        if (step < static_cast<uint64_t>(TurnStep))
        {
            return {0, static_cast<int32_t>(step), wrap ? LegsInBetweenSteps : MaxInBetweenSteps, wrap};
        }

        constexpr uint64_t BetweenMask = (uint64_t{1} << TurnBetweenShift) - 1;
        return {static_cast<int32_t>(step & ~(BetweenMask << TurnBetweenShift)),
                static_cast<int32_t>(step >> TurnBetweenShift & BetweenMask), LegsInBetweenSteps, wrap};
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

        // A turn middle, or (HD art's walk in-betweens) a turn middle's in-between toward the next frame's, or round
        // the loop's wrap (<frame>_turn_wrap_<step>).
        const StepFamily family = FamilyOf(step);
        const std::string wrap = family.Wrap ? std::format("{}{}", WrapName, InBetweenSeparator) : std::string();

        if (family.Turn != 0)
        {
            const std::string_view turnName = family.Turn == TurnMirroredStep ? TurnMirroredName : TurnName;
            return family.Member == 0 ? std::format("{}{}{}", frame, InBetweenSeparator, turnName)
                                      : std::format("{}{}{}{}{}{}", frame, InBetweenSeparator, turnName,
                                                    InBetweenSeparator, wrap, family.Member);
        }

        return std::format("{}{}{}{}", frame, InBetweenSeparator, wrap, family.Member);
    }

    /// <summary>The digits of the name a hash gives a frame (HashName), and their base.</summary>
    constexpr size_t HashNameDigits = 16;
    constexpr int HashNameBase = 16;

    /// <summary>The name a hash gives a frame: 16 hex digits.</summary>
    std::string HashName(uint64_t hash)
    {
        return std::format("{:016x}", hash);
    }

    /// <summary>Port-only: <paramref name="name"/>'s hash when it's one a hash gives (HashName); none otherwise.</summary>
    std::optional<uint64_t> HashOfName(std::string_view name)
    {
        uint64_t hash = 0;

        if (name.size() != HashNameDigits)
        {
            return std::nullopt;
        }

        const std::from_chars_result parsed =
            std::from_chars(name.data(), name.data() + name.size(), hash, HashNameBase);
        return parsed.ec == std::errc() && parsed.ptr == name.data() + name.size() ? std::optional(hash) : std::nullopt;
    }

    /// <summary>Port-only: whether <paramref name="name"/> is one a hash gives (HashName).</summary>
    bool IsHashName(std::string_view name)
    {
        return HashOfName(name).has_value();
    }

    /// <summary>The index of the folder at <paramref name="path"/> in State::Folders (added the first time).</summary>
    uint32_t FolderIndex(const std::filesystem::path& path, Naming names)
    {
        State& state = TheState();
        const auto [found, fresh] =
            state.FolderIndices.try_emplace(KeyOfPath(path), static_cast<uint32_t>(state.Folders.size()));

        if (fresh)
        {
            const std::filesystem::path inPack = path.lexically_relative(MCHdArt::PackFolder());
            const bool objects = !inPack.empty() && *inPack.begin() == ObjectFolder;
            state.Folders.push_back(FrameFolder{path, names, std::nullopt, objects});
        }

        return found->second;
    }

    /// <summary>
    /// Port-only: the folder (State::Folders' index) of PictureFolders that holds picture <paramref name="hash"/>, else the
    /// menus', where its lack is noted as any frame's.
    /// </summary>
    uint32_t PictureFolderOf(uint64_t hash)
    {
        const std::string name = HashName(hash);

        for (const std::string_view folder : PictureFolders)
        {
            const uint32_t index = FolderIndex(MCHdArt::PackFolder() / folder, Naming::Hash);

            if (HoldsFrame(TheState().Folders.at(index), name))
            {
                return index;
            }
        }

        return FolderIndex(MCHdArt::PackFolder() / PictureFolder, Naming::Hash);
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
            // A version 2 frame's shadow is marked in the pack, by the same rule.
            if (!made->Marked)
            {
                MCHdArt::MarkBakedShadows(*made);
            }
        }
        else
        {
            for (size_t at = 1; at < made->Indices.size() && !made->PaintedShadow; at += MCHdArt::IndexBytes)
            {
                made->PaintedShadow = MCHdArt::InShadowMark(made->Indices[at]);
            }
        }

        return made;
    }

    /// <summary>
    /// The frame <paramref name="key"/> for a shape of <paramref name="width"/> x <paramref name="height"/>, numbered
    /// <paramref name="id"/>: its <c>.hdf</c> file, else its PNG pair, else its entry in an archive; none when the pack
    /// has no such frame, or what is wrong with the one it has.
    /// </summary>
    std::expected<std::shared_ptr<MCHdArt::Frame>, std::string> ReadFrame(const FrameKey& key, int32_t width,
                                                                          int32_t height, uint64_t id)
    {
        FrameFolder& folder = TheState().Folders.at(key.Folder);
        std::error_code error;

        // Loose frame files lie in a folder; an archive's, in none (a pack of archives costs no file checks a frame).
        if (!folder.Loose.has_value())
        {
            folder.Loose = IsDirectory(folder.Path);
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
            const std::filesystem::path colorPath = std::filesystem::path(path).concat(PngExtension);

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
                std::filesystem::path(path).concat(std::format("{}{}", IndexSuffix, PngExtension));
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

    /// <summary>A frame's figures in the ledger: the bytes it keeps, and what they take.</summary>
    MCMemoryLedger::MCFigures FiguresOf(const MCHdArt::Frame& frame)
    {
        return {.Payload = static_cast<int64_t>(frame.Bytes()),
                .Allocated = static_cast<int64_t>(frame.AllocatedBytes())};
    }

    /// <summary>
    /// Port-only (the ledger): counts <paramref name="frame"/>, which the cache is letting go of, among the frames let go
    /// while something else holds it (its memory stays until that lets go too).
    /// </summary>
    void CountLetGo(State& state, const std::shared_ptr<const MCHdArt::Frame>& frame)
    {
        if (frame != nullptr && frame.use_count() > 1)
        {
            state.LetGo.push_back({.Frame = frame, .Figures = FiguresOf(*frame)});
        }
    }

    /// <summary>
    /// Drops the kept frame <paramref name="key"/> (<paramref name="cached"/>); its entry stays, to be read again (or read
    /// ahead again) with its Id.
    /// </summary>
    /// <summary>Port-only: <paramref name="cached"/>'s colours let go of, out of the cache's bytes.</summary>
    void LetGoColors(State& state, Cached& cached)
    {
        const size_t allocated = MCAllocatedBytes(cached.Colors.data());
        state.Bytes -= cached.Colors.size();
        state.ColorBytes -= cached.Colors.size();
        state.AllocatedBytes -= allocated;
        state.ColorAllocatedBytes -= allocated;
        std::vector<uint8_t>().swap(cached.Colors);
    }

    /// <summary>Port-only: <paramref name="colors"/> held as <paramref name="cached"/>'s, in the cache's bytes.</summary>
    void HoldColors(State& state, Cached& cached, std::vector<uint8_t>&& colors)
    {
        LetGoColors(state, cached);
        cached.Colors = std::move(colors);
        const size_t allocated = MCAllocatedBytes(cached.Colors.data());
        state.Bytes += cached.Colors.size();
        state.ColorBytes += cached.Colors.size();
        state.AllocatedBytes += allocated;
        state.ColorAllocatedBytes += allocated;
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): <paramref name="cached"/>'s record in place of the one it holds, out of the
    /// cache's bytes and into them; the one it held counted as let go while something else holds it.
    /// </summary>
    void ReplaceRecord(State& state, Cached& cached, std::shared_ptr<const MCHdArt::Frame> record)
    {
        state.Bytes -= cached.Frame->Bytes();
        state.AllocatedBytes -= cached.Frame->AllocatedBytes();
        CountLetGo(state, cached.Frame);
        cached.Frame = std::move(record);
        state.Bytes += cached.Frame->Bytes();
        state.AllocatedBytes += cached.Frame->AllocatedBytes();
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): a record as <paramref name="record"/> is, with <paramref name="indices"/>
    /// as its indices: its own never copied (a record has no colours: they're kept beside it).
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> RecordWith(const MCHdArt::Frame& record, std::vector<uint8_t> indices)
    {
        // Every field but the pixels: a field added to MCHdArt::Frame is copied here too.
        return std::make_shared<const MCHdArt::Frame>(MCHdArt::Frame{.Id = record.Id,
                                                                     .Width = record.Width,
                                                                     .Height = record.Height,
                                                                     .Scale = record.Scale,
                                                                     .Format = record.Format,
                                                                     .Indices = std::move(indices),
                                                                     .Blended = record.Blended,
                                                                     .PaintedShadow = record.PaintedShadow,
                                                                     .Marked = record.Marked,
                                                                     .Colorspace = record.Colorspace,
                                                                     .Movie = record.Movie});
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): <paramref name="cached"/>'s record without its indices, which the GPU's
    /// picture holds.
    /// </summary>
    void LetGoIndices(State& state, Cached& cached)
    {
        if (!cached.Frame->Indices.empty())
        {
            ReplaceRecord(state, cached, RecordWith(*cached.Frame, {}));
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): <paramref name="indices"/>, read again, back in <paramref name="cached"/>'s
    /// record; let go again as after an upload while the GPU has its picture.
    /// </summary>
    void IndicesBack(State& state, const FrameKey& key, Cached& cached, std::vector<uint8_t> indices)
    {
        ReplaceRecord(state, cached, RecordWith(*cached.Frame, std::move(indices)));

        if (cached.OnGpu)
        {
            state.Uploaded.emplace_back(key, state.Frame);
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's frame record): <paramref name="frame"/>, just read, kept as <paramref name="key"/>'s,
    /// the most recently used: its record (the frame without its colours), and its colours beside it.
    /// </summary>
    void KeepRead(State& state, const FrameKey& key, Cached& cached, std::shared_ptr<MCHdArt::Frame> frame)
    {
        std::vector<uint8_t> colors = std::move(frame->Colors);
        frame->Colors = {};
        cached.Frame = std::move(frame);
        cached.InOrder = state.Order.insert(state.Order.end(), key);
        state.Bytes += cached.Frame->Bytes();
        state.AllocatedBytes += cached.Frame->AllocatedBytes();
        HoldColors(state, cached, std::move(colors));
    }

    /// <summary>Port-only: numbers <paramref name="cached"/>, <paramref name="key"/>'s, the first time it's read.</summary>
    void Number(State& state, const FrameKey& key, Cached& cached)
    {
        if (cached.Id == 0)
        {
            cached.Id = MCHdArt::NewFrameId();
            state.ById.emplace(cached.Id, key);
        }
    }

    void Drop(State& state, const FrameKey& key, Cached& cached)
    {
        cached.Undrawn = false;
        state.Order.erase(cached.InOrder);
        state.Bytes -= cached.Frame->Bytes();
        state.AllocatedBytes -= cached.Frame->AllocatedBytes();
        LetGoColors(state, cached);
        CountLetGo(state, cached.Frame);
        cached.Frame.reset();
        cached.Looked = false;
        cached.OnGpu = false;

        // Port-only: a unit's packet near the view may hold it (MCHdArt::MCAskedPackets).
        if (state.Folders.at(key.Folder).Names == Naming::Number)
        {
            state.AskAgain = true;
        }

        std::scoped_lock lock(state.Ahead.Lock);
        state.Ahead.Known.erase(key);
    }

    /// <summary>
    /// Drops the least recently used frames, but never the one used last (the frame just read) nor one whose picture the
    /// GPU holds, until the kept ones fit the budget set (MCHdArt::SetCacheBudget). Port-only (SPEC-memory.md's plan):
    /// with none set, until they fit the art's room whole (MCMemoryRoom::ArtRoom), a bound for a frame's reads; the
    /// frames' share of that room is kept to frame by frame (MCHdArt::NextFrame), MCMemoryRoom::TrimmedBytesPerFrame at
    /// a time and never a frame being drawn, so no read drops a pile at once. Nor one kept for the mission's end
    /// (Cached::ForMission): the loop passes them, so it ends at the last frame even when they alone overfill the budget.
    /// </summary>
    void Trim()
    {
        State& state = TheState();
        const size_t budget = state.Budget != 0 ? state.Budget : static_cast<size_t>(MCMemoryRoom::ArtRoom().Bytes);
        auto next = state.Order.begin();

        // Port-only: past a frame whose picture the GPU holds (its record stays while the picture does), letting go of
        // any colours read again for it (MCHdArt::ColorsOf), which the GPU has; past one kept for the mission's end.
        while (state.Bytes > budget && next != state.Order.end() && std::next(next) != state.Order.end())
        {
            const auto oldest = state.Frames.find(*next);

            if (oldest == state.Frames.end())
            {
                next = state.Order.erase(next);
            }
            else if (oldest->second.OnGpu)
            {
                LetGoColors(state, oldest->second);
                ++next;
            }
            else if (oldest->second.ForMission)
            {
                ++next;
            }
            else
            {
                ++next;
                Drop(state, oldest->first, oldest->second);
            }
        }
    }

    /// <summary>The frames in the shape table starting at <paramref name="table"/>; 0 when it isn't one.</summary>
    int32_t FramesIn(std::span<const uint8_t> table)
    {
        return table.size() >= ShapeCountEnd && VfxIsShapeTable(table) ? VfxShapeCount(table) : 0;
    }

    /// <summary>
    /// Counts <paramref name="opened"/> more of the readers' own handles on the archives' files open (fewer: negative),
    /// making room for them among the files kept open (MaxOpenArchiveFiles). Takes State::ArchiveFiles, so never with
    /// ReadAhead::Lock held.
    /// </summary>
    void CountReaderFiles(ptrdiff_t opened)
    {
        State& state = TheState();
        std::scoped_lock lock(state.ArchiveFiles);
        state.ReaderFiles = static_cast<size_t>(static_cast<ptrdiff_t>(state.ReaderFiles) + opened);
        state.ReaderOpens += opened > 0 ? static_cast<size_t>(opened) : 0;
        TrimOpenFiles(state);
    }

    /// <summary>
    /// Closes the readers' <paramref name="handles"/>, letting go of ReadAhead::Lock (<paramref name="lock"/>)
    /// meanwhile: their count takes State::ArchiveFiles.
    /// </summary>
    void CloseHandles(std::unique_lock<std::mutex>& lock, std::list<ReaderHandle> handles)
    {
        if (handles.empty())
        {
            return;
        }

        const auto closed = static_cast<ptrdiff_t>(handles.size());
        lock.unlock();
        handles.clear();
        CountReaderFiles(-closed);
        lock.lock();
    }

    /// <summary>
    /// A handle of a reader's own on <paramref name="archive"/>'s file (a file's stream can't be read from two threads):
    /// one kept from an earlier read of it, else opened now, letting go of ReadAhead::Lock meanwhile; none when the file
    /// can't be opened or changed since its directory was read (the game's thread then reads, and logs, as it would have).
    /// Counted as reading (ReadAhead::Borrowed) until given back (GiveBack).
    /// </summary>
    std::optional<ReaderHandle> BorrowHandle(ReadAhead& ahead, std::unique_lock<std::mutex>& lock,
                                             const std::shared_ptr<MCHdArchive>& archive)
    {
        ++ahead.Borrowed;

        if (const auto kept = std::ranges::find(ahead.Idle, archive, &ReaderHandle::Archive); kept != ahead.Idle.end())
        {
            std::optional<ReaderHandle> handle(std::move(*kept));
            ahead.Idle.erase(kept);
            return handle;
        }

        lock.unlock();
        // Counted first, so that another file kept open closes to make room for it.
        CountReaderFiles(1);
        std::optional<MCDiskFile> file = archive->File->OpenAnother();

        if (!file.has_value())
        {
            CountReaderFiles(-1);
        }

        lock.lock();
        return file.has_value() ? std::optional<ReaderHandle>(ReaderHandle{archive, std::move(*file)}) : std::nullopt;
    }

    /// <summary>
    /// Gives back a handle BorrowHandle gave: kept for the next read of its archive (<paramref name="keep"/>: its read
    /// didn't fail, and the pack wasn't set since <paramref name="generation"/>), the least recently given back closed
    /// past IdleHandlesPerReader for each reader; else closed.
    /// </summary>
    /// <remarks>
    /// Kept across reads, as a pan's mostly read the same archives: a handle opened for each follow had one archive opened
    /// 136 times in 2 s on Windows (13 Nov).
    /// </remarks>
    void GiveBack(ReadAhead& ahead, std::unique_lock<std::mutex>& lock, std::optional<ReaderHandle>&& handle, bool keep,
                  uint64_t generation)
    {
        --ahead.Borrowed;
        std::list<ReaderHandle> closing;

        if (handle.has_value() && keep && generation == ahead.Generation)
        {
            ahead.Idle.push_front(std::move(*handle));
            const size_t most = IdleHandlesPerReader * ahead.Settings.Readers;

            while (ahead.Idle.size() > most)
            {
                closing.splice(closing.end(), ahead.Idle, std::prev(ahead.Idle.end()));
            }
        }
        else if (handle.has_value())
        {
            closing.push_back(std::move(*handle));
        }

        handle.reset();
        CloseHandles(lock, std::move(closing));
        ahead.Changed.notify_all();
    }

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

        // Frame at's family of steps (of) from member from on, up to the first member the archive lacks.
        const auto addFamily = [&](int32_t at, const StepFamily& of, int32_t from)
        {
            for (int32_t member = from; member < of.Members; ++member)
            {
                const uint64_t name = NumberedKey(at, of.StepOf(member));
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
            addFamily(frame, family, 0);
            return next;
        }

        addFamily(frame, family, family.Member + 1);

        for (int32_t on = 1; on <= FramesAhead && on < follow.Frames; ++on)
        {
            addFamily((frame + on) % follow.Frames, family.Unwrapped(), 0);
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
                TheState().AskAgain = true;
            }

            ahead.Order.pop_front();
        }
    }

    /// <summary>The key a read's frame is kept under.</summary>
    FrameKey KeyOf(const AheadRead& read)
    {
        return FrameKey{read.From->Key.Folder, read.Entry.Name};
    }

    /// <summary>The entry <paramref name="read"/> reads, in its archive's directory.</summary>
    MCFileEntry EntryOf(const AheadRead& read)
    {
        return read.From->Archive->File->EntryAt(read.Entry.Index);
    }

    /// <summary>The bytes of its archive <paramref name="run"/>'s entries lie in, from its first to its last.</summary>
    std::pair<uint32_t, uint32_t> RunSpan(const std::vector<AheadRead>& run)
    {
        uint32_t start = std::numeric_limits<uint32_t>::max();
        uint32_t end = 0;

        for (const AheadRead& read : run)
        {
            const MCFileEntry& entry = EntryOf(read);
            start = std::min(start, static_cast<uint32_t>(entry.Offset));
            end = std::max(end, static_cast<uint32_t>(entry.Offset) + static_cast<uint32_t>(entry.Size));
        }

        return {start, end};
    }

    /// <summary>
    /// Port-only (MCHdArt::OpenTrace): the HD trace: its file, its clock's start, whether it's open (tested without the
    /// lock), and the archives it has named, each by a number.
    /// </summary>
    struct Trace
    {
        std::mutex Lock;
        std::ofstream Out;
        std::chrono::steady_clock::time_point Start;
        std::atomic<bool> On = false;
        std::unordered_map<const MCHdArchive*, size_t> Archives;
        /// <summary>
        /// The pack's folder, which the archives' paths are given from: set by the game's thread (OpenTrace,
        /// SetPackFolder), read by the readers' (TraceArchive), so never MCHdArt::PackFolder's state there.
        /// </summary>
        std::filesystem::path Pack;
    };

    Trace& TheTrace()
    {
        static Trace trace;
        return trace;
    }

    /// <summary>Whether the HD trace is open: its lines cost a test each while it isn't.</summary>
    bool Tracing()
    {
        return TheTrace().On.load(std::memory_order_relaxed);
    }

    /// <summary>The microseconds since the trace opened. With Trace::Lock held.</summary>
    int64_t TraceNow(const Trace& trace)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - trace.Start)
            .count();
    }

    /// <summary>
    /// The trace's number for <paramref name="archive"/>, its path (from the pack's folder) written the first time:
    /// <c>P number path</c>. With Trace::Lock held.
    /// </summary>
    size_t TraceArchive(Trace& trace, const MCHdArchive& archive)
    {
        const auto [found, fresh] = trace.Archives.try_emplace(&archive, trace.Archives.size());

        if (fresh)
        {
            trace.Out << std::format("P\t{}\t{}\n", found->second,
                                     MCFileSystem::ToUtf8(archive.Path.lexically_relative(trace.Pack)));
        }

        return found->second;
    }

    /// <summary>The name the trace gives reads of <paramref name="kind"/>.</summary>
    std::string_view TraceKind(AheadKind kind)
    {
        constexpr std::array<std::string_view, AheadKinds> names{"load", "next", "soon", "set", "hold", "gpu"};
        return names[std::to_underlying(kind)];
    }

    /// <summary>
    /// Traces <paramref name="read"/> asked for as a read of <paramref name="kind"/>: <c>A time kind archive offset
    /// stored entry</c>.
    /// </summary>
    void TraceAsked(const AheadRead& read, AheadKind kind)
    {
        if (!Tracing())
        {
            return;
        }

        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        const MCFileEntry& entry = read.From->Archive->File->EntryAt(read.Entry.Index);
        trace.Out << std::format("A\t{}\t{}\t{}\t{}\t{}\t{}\n", TraceNow(trace), TraceKind(kind),
                                 TraceArchive(trace, *read.From->Archive), entry.Offset, entry.Size, read.Entry.Entry);
    }

    /// <summary>
    /// Traces a reader's read of <paramref name="run"/> and the <paramref name="joined"/> reads it served that were asked
    /// for while it read (JoinRead), begun <paramref name="begun"/> (TraceNow's), its bytes in by <paramref name="read"/>,
    /// its frames unpacked by now: <c>R begun read unpacked kind archive offset bytes entries</c>, the entries the run's
    /// and the joined; or, none read (its streams kept unpacked from an earlier read), <c>K begun unpacked kind archive
    /// offset entries</c>.
    /// </summary>
    void TraceRun(const std::vector<AheadRead>& run, size_t joined, int64_t begun, std::optional<int64_t> read)
    {
        if (!Tracing() || run.empty())
        {
            return;
        }

        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        const auto [start, end] = RunSpan(run);
        const std::string_view kind = TraceKind(run.front().From->Kind);
        const size_t archive = TraceArchive(trace, *run.front().From->Archive);

        if (!read.has_value())
        {
            trace.Out << std::format("K\t{}\t{}\t{}\t{}\t{}\t{}\n", begun, TraceNow(trace), kind, archive, start,
                                     run.size() + joined);
            return;
        }

        trace.Out << std::format("R\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", begun, *read, TraceNow(trace), kind, archive,
                                 start, end - start, run.size() + joined);
    }

    /// <summary>
    /// Port-only: traces one of the hold's reads (HoldRun) of <paramref name="archive"/>, begun <paramref name="begun"/>
    /// (TraceNow's), its bytes in by <paramref name="read"/>: <c>H begun read archive offset bytes streams</c>, the
    /// streams those it holds.
    /// </summary>
    void TraceHold(const MCHdArchive& archive, int64_t begun, int64_t read, uint64_t offset, uint64_t bytes,
                   size_t streams)
    {
        if (!Tracing())
        {
            return;
        }

        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        trace.Out << std::format("H\t{}\t{}\t{}\t{}\t{}\t{}\n", begun, read, TraceArchive(trace, archive), offset,
                                 bytes, streams);
    }

    /// <summary>TraceNow's time, when tracing (0 when not).</summary>
    int64_t TraceTime()
    {
        if (!Tracing())
        {
            return 0;
        }

        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        return TraceNow(trace);
    }

    /// <summary>
    /// Port-only: lets a set's or a load's frame (ReadAhead::PicturesAsked) be asked for again once its read is done or
    /// left out: read, it is found among the Pictures; not read (failed, the game's thread has it, the pack set), it may be
    /// asked for again.
    /// </summary>
    void Unasked(ReadAhead& ahead, const AheadRead& read)
    {
        // Port-only: the fill's reads (MCHdArt::FillGpu) aren't marked asked: another ask of the frame reads it sooner.
        if (read.From->Kind != AheadKind::Next && read.From->Kind != AheadKind::Gpu)
        {
            ahead.PicturesAsked.erase(KeyOf(read));
        }
    }

    /// <summary>
    /// Whether <paramref name="entry"/> may be read with its neighbours: one a frame file can hold, in its place (one too
    /// large, or misplaced, is read alone, and ReadEntry says why).
    /// </summary>
    bool JoinsRun(const MCFileEntry& entry)
    {
        return FitsFrameFile(entry) && entry.Offset >= 0;
    }

    /// <summary>
    /// Puts <paramref name="read"/> among those waiting (ReadAhead::Waiting) as a read of <paramref name="kind"/>, after
    /// those of its kind asked before it, and in its archive's index; or, its entry lying in a span a reader reads now,
    /// has that read serve it (JoinRead). Call with ReadAhead::Lock held.
    /// </summary>
    bool StillToRead(const ReadAhead& ahead, const AheadRead& read);

    /// <summary>
    /// Marks <paramref name="read"/>'s frame as being read (ReadAhead::Reading, and GpuReading for the fill's). Call with
    /// ReadAhead::Lock held.
    /// </summary>
    void MarkReading(ReadAhead& ahead, const AheadRead& read)
    {
        const FrameKey key = KeyOf(read);

        if (ahead.Reading.insert(key).second && read.From->Kind == AheadKind::Gpu)
        {
            ahead.GpuReading.insert(key);
        }
    }

    /// <summary>
    /// Port-only (MCHdArt::FillGpu): stops the fill readers' waits for room, if any wait: the fill dropped, the read-ahead
    /// forgotten, or work for the readers that isn't the fill's queued (an archive to open, an ask, a follow, an archive to
    /// hold, reads). Every reader may be waiting for room in the slots, which only the game's thread frees, while the
    /// game's thread waits for that work (an archive being opened, the load's reads). With ReadAhead::Lock held.
    /// </summary>
    void StopGpuRoomWaits(ReadAhead& ahead)
    {
        ahead.GpuRoomStop.request_stop();
        ahead.GpuRoomStop = std::stop_source();
    }

    /// <summary>
    /// Port-only (UpdateGpuFill, the briefing's fill): whether a reader opens the archive standing for
    /// <paramref name="path"/>'s folders now (any of them up to <paramref name="pack"/>, as ArchiveOf looks). Call with
    /// ReadAhead::Lock held.
    /// </summary>
    bool OpensFor(const ReadAhead& ahead, const std::filesystem::path& path, const std::filesystem::path& pack)
    {
        for (std::filesystem::path at = path; at.has_relative_path() && at != pack; at = at.parent_path())
        {
            if (ahead.Opening.contains(at))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// Has the read of the span <paramref name="read"/>'s entry lies in, if a reader reads one now
    /// (ReadAhead::SpansRead), serve <paramref name="read"/> (a read of <paramref name="kind"/>) too, counted as being read:
    /// an entry asked while its stream is read waits on that read, not a read of its own. Whether it does (else
    /// <paramref name="read"/> is left as it was). Call with ReadAhead::Lock held.
    /// </summary>
    bool JoinRead(ReadAhead& ahead, AheadKind kind, AheadRead& read)
    {
        if (ahead.SpansRead.empty())
        {
            return false;
        }

        const MCFileEntry& entry = EntryOf(read);

        if (!JoinsRun(entry) || !StillToRead(ahead, read))
        {
            return false;
        }

        const auto start = static_cast<uint64_t>(entry.Offset);
        const uint64_t end = start + static_cast<uint64_t>(entry.Size);
        const auto span = std::ranges::find_if(ahead.SpansRead,
                                               [&](const SpanRead& one)
                                               {
                                                   return one.Archive == read.From->Archive.get() &&
                                                          one.Generation == ahead.Generation && one.Start <= start &&
                                                          end <= one.End;
                                               });

        if (span == ahead.SpansRead.end())
        {
            return false;
        }

        MarkReading(ahead, read);
        ++ahead.JoinedOfKind[std::to_underlying(kind)];
        span->Joined.emplace_back(kind, std::move(read));
        return true;
    }

    void AddWaiting(ReadAhead& ahead, AheadKind kind, AheadRead&& read)
    {
        TraceAsked(read, kind);

        if (JoinRead(ahead, kind, read))
        {
            return;
        }

        const uint64_t number = ahead.ReadNumbers++;
        std::multimap<int64_t, uint64_t>& index = ahead.ByOffset[read.From->Archive];
        const auto place = index.emplace(EntryOf(read).Offset, number);
        ahead.Waiting.emplace(number, WaitingRead{std::move(read), kind, place});
        ahead.Reads[std::to_underlying(kind)].push_back(number);
        ++ahead.WaitingOfKind[std::to_underlying(kind)];
    }

    /// <summary>
    /// Takes read <paramref name="number"/> from those waiting, and from its archive's index (the archive's left in
    /// ReadAhead::ByOffset, empty, for <see cref="DropEmptyIndex"/>); its number stays in its kind's queue, skipped.
    /// None when it isn't waiting (read with a neighbour already). Call with ReadAhead::Lock held.
    /// </summary>
    std::optional<WaitingRead> TakeWaiting(ReadAhead& ahead, uint64_t number)
    {
        const auto found = ahead.Waiting.find(number);

        if (found == ahead.Waiting.end())
        {
            return std::nullopt;
        }

        WaitingRead waiting = std::move(found->second);
        ahead.Waiting.erase(found);
        ahead.ByOffset.at(waiting.Read.From->Archive).erase(waiting.Place);
        --ahead.WaitingOfKind[std::to_underlying(waiting.Kind)];
        return waiting;
    }

    /// <summary>Drops <paramref name="archive"/>'s index from ReadAhead::ByOffset once none of its reads wait.</summary>
    void DropEmptyIndex(ReadAhead& ahead, const std::shared_ptr<MCHdArchive>& archive)
    {
        if (const auto found = ahead.ByOffset.find(archive); found != ahead.ByOffset.end() && found->second.empty())
        {
            ahead.ByOffset.erase(found);
        }
    }

    /// <summary>Forgets every read waiting. Call with ReadAhead::Lock held.</summary>
    void ForgetWaiting(ReadAhead& ahead)
    {
        for (std::deque<uint64_t>& reads : ahead.Reads)
        {
            reads.clear();
        }

        ahead.Waiting.clear();
        ahead.WaitingOfKind.fill(0);
        ahead.ByOffset.clear();
    }

    /// <summary>
    /// Whether <paramref name="read"/> is to be read still: not looked at by the game's thread, read ahead or being read
    /// (else it is let go of: Unasked). Call with ReadAhead::Lock held.
    /// </summary>
    bool StillToRead(const ReadAhead& ahead, const AheadRead& read)
    {
        const FrameKey key = KeyOf(read);
        return !ahead.Known.contains(key) && !ahead.Ready.contains(key) && !ahead.Pictures.contains(key) &&
               !ahead.Reading.contains(key) && !ahead.Staged.contains(key);
    }

    /// <summary>
    /// How many of the sets' reads may be in flight at once (MCHdArt::ReaderSettings::SetReads; 0: as many as there are
    /// readers).
    /// </summary>
    size_t SetReadsAllowed(const ReadAhead& ahead)
    {
        return ahead.Settings.SetReads == 0 ? ahead.Settings.Readers : ahead.Settings.SetReads;
    }

    /// <summary>Port-only: whether the hold (MCHdArt::StartHold) holds and has archives still to read.</summary>
    bool HoldPending(const ReadAhead& ahead)
    {
        return ahead.Holding.On && !ahead.Holding.Archives.empty();
    }

    /// <summary>
    /// The kind of read a reader takes next (SPEC-first-use.md: what a load waits for, the frames drawn next with the
    /// follows still to make reads of, what the camera's surroundings draw soon, then the sets, as many at once as
    /// SetReadsAllowed; port-only, then the hold's, when nothing else waits, HoldReadsAtOnce at a time); none while there
    /// is none it may take.
    /// </summary>
    std::optional<AheadKind> NextKind(const ReadAhead& ahead)
    {
        const auto waiting = [&ahead](AheadKind kind) { return ahead.WaitingOfKind[std::to_underlying(kind)] > 0; };

        if (ahead.Held)
        {
            return std::nullopt;
        }

        if (waiting(AheadKind::Load))
        {
            return AheadKind::Load;
        }

        if (waiting(AheadKind::Next) || !ahead.Follows.empty())
        {
            return AheadKind::Next;
        }

        if (waiting(AheadKind::Soon))
        {
            return AheadKind::Soon;
        }

        if (waiting(AheadKind::Set) && ahead.InFlight[std::to_underlying(AheadKind::Set)] < SetReadsAllowed(ahead))
        {
            return AheadKind::Set;
        }

        // Port-only (MCHdArt::FillGpu): the mission's fill on the GPU, beside the hold (from its memory once held): under
        // the bar half the readers while the hold reads, all once it's read everything.
        const bool holding = HoldPending(ahead) || ahead.InFlight[std::to_underlying(AheadKind::Hold)] > 0;
        const size_t gpuReads = !ahead.Holding.Loading ? GpuReadsAtOnce
                                : holding ? std::max(ahead.Settings.Readers / LoadGpuReadersDivisor, GpuReadsAtOnce)
                                          : ahead.Settings.Readers;

        if (!waiting(AheadKind::Set) && waiting(AheadKind::Gpu) && ahead.Slots != nullptr &&
            ahead.InFlight[std::to_underlying(AheadKind::Gpu)] < gpuReads)
        {
            return AheadKind::Gpu;
        }

        // Port-only (SPEC-memory.md's fill): under the loading bar, as many as there are readers.
        const size_t holdReads = ahead.Holding.Loading ? ahead.Settings.Readers : HoldReadsAtOnce;

        if (!waiting(AheadKind::Set) && HoldPending(ahead) &&
            ahead.InFlight[std::to_underlying(AheadKind::Hold)] < holdReads)
        {
            return AheadKind::Hold;
        }

        return std::nullopt;
    }

    /// <summary>
    /// Makes <paramref name="follow"/>'s frames drawn next (NextFrames) reads, the soonest drawn first, after those
    /// waiting (the oldest left out past MaxNextReads). Call with ReadAhead::Lock held: the archive's directory never
    /// changes once read, and looking in it is a few lookups.
    /// </summary>
    void MakeReads(ReadAhead& ahead, Follow&& follow)
    {
        std::vector<AheadEntry> next = NextFrames(follow);
        const auto from = std::make_shared<const Follow>(std::move(follow));
        constexpr auto kind = std::to_underlying(AheadKind::Next);

        for (AheadEntry& entry : next)
        {
            AddWaiting(ahead, AheadKind::Next, AheadRead{from, std::move(entry)});
        }

        while (ahead.WaitingOfKind[kind] > MaxNextReads)
        {
            if (std::optional<WaitingRead> dropped = TakeWaiting(ahead, ahead.Reads[kind].front()))
            {
                DropEmptyIndex(ahead, dropped->Read.From->Archive);
            }

            ahead.Reads[kind].pop_front();
        }
    }

    /// <summary>
    /// Adds to <paramref name="taken"/> (its first, an entry that joins a run: JoinsRun, being read) every read waiting
    /// of the archive's, of any kind and asked for in any order, whose entry lies in the span the read takes (a version 2
    /// archive's entries of one stream, however large it is: the stream is what is read), or within
    /// MCHdArt::ReaderRunGap of it, before it or after it, while the span takes at most MCHdArt::ReaderRunBytes: found by
    /// offset in the archive's index (ReadAhead::ByOffset), each counted as being read. Those no longer to read are let go
    /// of on the way. Call with ReadAhead::Lock held.
    /// </summary>
    void GatherNeighbours(ReadAhead& ahead, std::vector<std::pair<AheadKind, AheadRead>>& taken)
    {
        const auto index = ahead.ByOffset.find(taken.front().second.From->Archive);

        if (index == ahead.ByOffset.end())
        {
            return;
        }

        std::multimap<int64_t, uint64_t>& byOffset = index->second;
        const MCFileEntry& first = EntryOf(taken.front().second);
        auto start = static_cast<uint64_t>(first.Offset);
        uint64_t end = start + static_cast<uint64_t>(first.Size);

        for (bool grew = true; grew;)
        {
            grew = false;
            // An entry that starts further back than the most bytes a read takes before the span's end can't join,
            // unless it lies in the span.
            const uint64_t from = std::min(start, end > MCHdArt::ReaderRunBytes ? end - MCHdArt::ReaderRunBytes : 0);

            for (auto at = byOffset.lower_bound(static_cast<int64_t>(from));
                 at != byOffset.end() && static_cast<uint64_t>(at->first) <= end + MCHdArt::ReaderRunGap;)
            {
                // On before it may be taken (which takes it from the index).
                const uint64_t number = (at++)->second;
                const MCFileEntry& entry = EntryOf(ahead.Waiting.at(number).Read);

                if (!JoinsRun(entry))
                {
                    continue;
                }

                const auto entryStart = static_cast<uint64_t>(entry.Offset);
                const uint64_t entryEnd = entryStart + static_cast<uint64_t>(entry.Size);
                const bool near = entryStart >= end   ? entryStart - end <= MCHdArt::ReaderRunGap
                                  : entryEnd <= start ? start - entryEnd <= MCHdArt::ReaderRunGap
                                                      : true;
                const bool inside = entryStart >= start && entryEnd <= end;
                const uint64_t spanStart = std::min(start, entryStart);
                const uint64_t spanEnd = std::max(end, entryEnd);

                if (!inside && (!near || spanEnd - spanStart > MCHdArt::ReaderRunBytes))
                {
                    continue;
                }

                std::optional<WaitingRead> read = TakeWaiting(ahead, number);

                if (!StillToRead(ahead, read->Read))
                {
                    Unasked(ahead, read->Read);
                    continue;
                }

                MarkReading(ahead, read->Read);
                taken.emplace_back(read->Kind, std::move(read->Read));
                // Another look only once the span grows: an entry passed over may be near it then.
                grew = grew || spanStart != start || spanEnd != end;
                start = spanStart;
                end = spanEnd;
            }
        }
    }

    /// <summary>
    /// Takes the next reads for one read of their archive: the first of <paramref name="kind"/>'s asked for that is still
    /// to read (those before it that aren't are left out), and with it its archive's reads waiting near it
    /// (GatherNeighbours); the first first, then the rest by kind, the more urgent first, then by offset, as ReadRun
    /// hands them over. Each counted as being read (ReadAhead::Reading). Call with ReadAhead::Lock held.
    /// </summary>
    std::vector<AheadRead> TakeRun(ReadAhead& ahead, AheadKind kind)
    {
        std::deque<uint64_t>& queue = ahead.Reads[std::to_underlying(kind)];
        std::vector<std::pair<AheadKind, AheadRead>> taken;

        while (!queue.empty() && taken.empty())
        {
            std::optional<WaitingRead> first = TakeWaiting(ahead, queue.front());
            queue.pop_front();

            if (!first.has_value())
            {
                continue;
            }

            if (!StillToRead(ahead, first->Read))
            {
                Unasked(ahead, first->Read);
                DropEmptyIndex(ahead, first->Read.From->Archive);
                continue;
            }

            MarkReading(ahead, first->Read);
            taken.emplace_back(first->Kind, std::move(first->Read));
        }

        if (taken.empty())
        {
            return {};
        }

        const std::shared_ptr<MCHdArchive> archive = taken.front().second.From->Archive;

        if (JoinsRun(EntryOf(taken.front().second)))
        {
            GatherNeighbours(ahead, taken);
        }

        DropEmptyIndex(ahead, archive);
        std::stable_sort(
            taken.begin() + 1, taken.end(), [](const auto& a, const auto& b)
            { return a.first != b.first ? a.first < b.first : EntryOf(a.second).Offset < EntryOf(b.second).Offset; });
        std::vector<AheadRead> run;
        run.reserve(taken.size());

        for (auto& [readKind, read] : taken)
        {
            run.push_back(std::move(read));
        }

        return run;
    }

    /// <summary>
    /// Port-only, for the tests (MCHdArt::HoldTakenRuns): holds the run a reader took until they're let go. Call with
    /// ReadAhead::Lock held (<paramref name="lock"/>), which the wait lets go of.
    /// </summary>
    void WaitWhileRunsHeld(ReadAhead& ahead, std::unique_lock<std::mutex>& lock)
    {
        if (!ahead.RunsHeld)
        {
            return;
        }

        ++ahead.RunsHolding;
        ahead.Changed.notify_all();
        ahead.Changed.wait(lock, [&ahead] { return !ahead.RunsHeld; });
        --ahead.RunsHolding;
    }

    /// <summary>
    /// Port-only (StageForGpu): why a frame of the fill's isn't put in the upload slots: it never can be, or the slots are
    /// full now.
    /// </summary>
    enum class StageMiss
    {
        Never,
        Full,
    };

    /// <summary>Port-only (StageForGpu): why a frame wasn't put in the slots, and the room it needed there.</summary>
    struct StageMissed
    {
        StageMiss Why = StageMiss::Never;
        size_t Need = 0;
    };

    /// <summary>
    /// Port-only (MCHdArt::FillGpu): the most of <paramref name="slots"/> the fill may hold at once, in bytes.
    /// </summary>
    size_t FillRoomBytes(const MCUploadSlots& slots)
    {
        return slots.MostFor(MCUploadSlots::MCKind::Fill) * MCUploadSlots::SlotBytes;
    }

    /// <summary>
    /// Port-only (MCHdArt::FillGpu; SPEC-memory.md's "A frame's path": the reader unpacks into its own memory, then copies
    /// the frame into an upload slot): <paramref name="frame"/>'s picture, its colours (decoded for a GPU without BC7)
    /// and indices, put in the slots, in <paramref name="lease"/> while they fit, else a new one if the slots have one
    /// free (never waiting: a reader waits for room before it takes a run, ReadFramesAhead); its colours let go of then.
    /// Why not, the frame left as it is, when it can't be: never (a movie's, or more than the fill may hold), or not now
    /// (the slots full).
    /// </summary>
    std::expected<MCHdArt::StagedPicture, StageMissed> StageForGpu(MCUploadSlots& slots,
                                                                   std::shared_ptr<MCUploadSlots::MCLease>& lease,
                                                                   MCHdArt::Frame& frame, bool bc7)
    {
        if (frame.Format == MCHdArt::ColorFormat::Yuv420)
        {
            return std::unexpected(StageMissed{});
        }

        const bool isBc7 = frame.Format == MCHdArt::ColorFormat::Bc7;
        std::vector<uint8_t> decoded;

        if (isBc7 && !bc7)
        {
            decoded = MCBc7::Decode(frame.Colors, frame.Width, frame.Height);
        }

        const std::span<const uint8_t> colors =
            isBc7 && !bc7 ? std::span<const uint8_t>(decoded) : std::span<const uint8_t>(frame.Colors);
        const std::span<const uint8_t> indices = frame.Indices;
        std::optional<size_t> colorsAt = lease != nullptr ? lease->Place(colors.size()) : std::nullopt;
        std::optional<size_t> indicesAt = colorsAt.has_value() ? lease->Place(indices.size()) : std::nullopt;

        if (!indicesAt.has_value())
        {
            const size_t need = colors.size() + indices.size() + UploadsPerPicture * MCUploadSlots::Alignment;
            const size_t most = FillRoomBytes(slots);

            if (need > most)
            {
                return std::unexpected(StageMissed{StageMiss::Never, need});
            }

            // The lease it doesn't fit let go of first (freed unless pictures in it hold it), its room the new one's.
            lease.reset();
            std::optional<MCUploadSlots::MCLease> taken =
                slots.TryTake(MCUploadSlots::MCKind::Fill, std::max(need, std::min(FillLeaseBytes, most)));

            if (!taken.has_value())
            {
                return std::unexpected(StageMissed{StageMiss::Full, need});
            }

            lease = std::make_shared<MCUploadSlots::MCLease>(std::move(*taken));
            colorsAt = lease->Place(colors.size());
            indicesAt = lease->Place(indices.size());
        }

        std::ranges::copy(colors, lease->At(*colorsAt, colors.size()).begin());
        std::ranges::copy(indices, lease->At(*indicesAt, indices.size()).begin());
        MCHdArt::StagedPicture picture;
        picture.Lease = lease;
        picture.ColorsAt = static_cast<uint32_t>(*colorsAt);
        picture.ColorBytes = static_cast<uint32_t>(colors.size());
        picture.IndicesAt = static_cast<uint32_t>(*indicesAt);
        picture.IndexBytes = static_cast<uint32_t>(indices.size());
        picture.Bc7 = isBc7 && bc7;
        picture.DrawnColors = frame.Marked ? MCHdArt::DrawnColorsOf(indices) : std::bitset<256>{};
        // Its picture is in the slots now: the record keeps no colours (SPEC-memory.md's one copy of a frame's colours).
        frame.Colors = {};
        return picture;
    }

    void QueueReads(ReadAhead& ahead, AheadKind kind, std::vector<AheadRead>&& reads);

    /// <summary>
    /// Reads <paramref name="run"/> (TakeRun's) through a handle of a reader's own, in one read of the archive, without
    /// ReadAhead::Lock (<paramref name="lock"/>), and keeps the frames read for the game's thread (Ready for the frames
    /// drawn next; Pictures for the sets and what a load waits for, bounded where they're asked for) unless the pack was
    /// set meanwhile. A version 2 archive's stream is unpacked once for all the run's entries in it, and one kept unpacked
    /// from an earlier read, or held as stored (MCHdArt::StartHold) and unpacked from memory, is read again only when
    /// another of the run's streams is neither (none read: no read at all). The reads asked for while the span is read
    /// whose entries lie in it (JoinRead) are served from the same bytes. A frame whose read fails is left to the game's
    /// thread, which reads it again and logs why. Port-only (MCHdArt::FillGpu): a frame of the mission's fill goes into
    /// the upload slots (StageForGpu), in <paramref name="lease"/> (a run of the fill's) or more room the slots have
    /// free, never waiting; once they're full, the run's frames of the fill go back to its queue unread.
    /// </summary>
    void ReadRun(ReadAhead& ahead, std::unique_lock<std::mutex>& lock, const std::vector<AheadRead>& run,
                 std::shared_ptr<MCUploadSlots::MCLease> lease = nullptr)
    {
        const uint64_t generation = ahead.Generation;
        const uint64_t leaves = ahead.GpuLeaves;
        const std::shared_ptr<MCUploadSlots> slots = ahead.Slots;
        const bool bc7 = ahead.Bc7;
        const std::shared_ptr<MCHdArchive> archive = run.front().From->Archive;
        const MCFastFile& file = *archive->File;
        const auto [start, end] = RunSpan(run);
        // An entry that can't join a run is read as the game's thread reads it, which says what is wrong with it.
        const bool alone = run.size() == 1 && !JoinsRun(EntryOf(run.front()));
        // The run's streams unpacked, by where they lie (a version 2 stream's entries share its place): those kept from
        // an earlier read; the rest unpacked from the bytes read, once each.
        std::vector<std::pair<int32_t, std::shared_ptr<const std::vector<uint8_t>>>> streams;
        bool toRead = true;

        if (!alone && file.IsVersion2())
        {
            toRead = false;

            for (const AheadRead& read : run)
            {
                const int32_t at = EntryOf(read).Offset;

                if (std::ranges::find(streams, at, &decltype(streams)::value_type::first) == streams.end())
                {
                    std::shared_ptr<const std::vector<uint8_t>> kept = file.KeptStreamOf(read.Entry.Index);
                    toRead = toRead || (kept == nullptr && !file.HeldOf(read.Entry.Index).has_value());
                    streams.emplace_back(at, std::move(kept));
                }
            }
        }

        // Port-only: a version 1 run whose entries are all held (MCHdArt::StartHold) needs no read either.
        else if (!alone)
        {
            toRead = !std::ranges::all_of(run, [&file](const AheadRead& read)
                                          { return file.HeldOf(read.Entry.Index).has_value(); });
        }

        // The span read, for the reads asked for meanwhile to join (JoinRead) until its last is served.
        std::optional<std::list<SpanRead>::iterator> span;

        if (!alone && toRead)
        {
            span = ahead.SpansRead.insert(ahead.SpansRead.end(), SpanRead{archive.get(), start, end, generation, {}});
        }

        WaitWhileRunsHeld(ahead, lock);
        std::optional<ReaderHandle> handle;

        if (toRead)
        {
            handle = BorrowHandle(ahead, lock, archive);
        }

        // Borrowing may have let go of the lock: the pack may have been set meanwhile.
        const bool current = (handle.has_value() || !toRead) && ahead.Generation == generation;
        ahead.ArchiveReads += current && toRead ? 1 : 0;
        lock.unlock();
        const int64_t begun = TraceTime();
        int64_t readBy = begun;
        std::vector<std::shared_ptr<MCHdArt::Frame>> frames(run.size());
        // Port-only (MCHdArt::FillGpu): the fill's frames' pictures put in the slots, in the run's lease while they fit;
        // those that find the slots full, left for a later run (requeue), and none after them unpacked.
        std::vector<std::optional<MCHdArt::StagedPicture>> staged(run.size());
        std::vector<AheadRead> requeue;
        bool slotsFull = false;
        size_t roomNeed = 0;
        const auto fills = [&slots](const AheadRead& read)
        { return slots != nullptr && read.From->Kind == AheadKind::Gpu; };
        // The frame's picture in the slots, for a read of the fill's (none for another's, or one whose read failed). One the
        // fill can't take is dropped: read again later (requeue) when the slots are full, else for good.
        const auto stage = [&](const AheadRead& read, std::shared_ptr<MCHdArt::Frame>& frame)
        {
            std::optional<MCHdArt::StagedPicture> staged;

            if (!fills(read) || (frame == nullptr && !slotsFull))
            {
                return staged;
            }

            std::expected<MCHdArt::StagedPicture, StageMissed> picture =
                slotsFull ? std::unexpected(StageMissed{StageMiss::Full}) : StageForGpu(*slots, lease, *frame, bc7);

            if (picture.has_value())
            {
                staged = std::move(*picture);
                return staged;
            }

            frame.reset();

            if (picture.error().Why == StageMiss::Full)
            {
                slotsFull = true;
                roomNeed = std::max(roomNeed, picture.error().Need);
                requeue.push_back(read);
            }

            return staged;
        };

        std::vector<uint8_t> bytes;
        bool readWhole = current;
        // The reads handed over (publish): those before it.
        size_t handed = 0;

        // Hands the game's thread a read's frame: kept for it, or left to it when its read failed or the pack was set
        // meanwhile. With ReadAhead::Lock held. Port-only: one of the fill's put in the slots (<picture>) waits for the
        // game's thread to take it up (MCHdArt::TakeStagedPictures).
        const auto handOver = [&](const AheadRead& read, std::shared_ptr<MCHdArt::Frame>&& frame,
                                  std::optional<MCHdArt::StagedPicture>&& picture)
        {
            const FrameKey key = KeyOf(read);
            ahead.Reading.erase(key);
            ahead.GpuReading.erase(key);

            // Port-only: a picture put in slots the renderer has let go of since (made again: another pool), or of a fill
            // left since, is dropped, its frame with it (its colours are in the picture alone); the game's thread reads it
            // again when drawn.
            if (picture.has_value() && (ahead.Slots != slots || ahead.GpuLeaves != leaves))
            {
                picture.reset();
                frame.reset();
            }

            if (frame != nullptr && ahead.Generation == generation)
            {
                ahead.NeighbourReads += read.From->Itself ? 1 : 0;

                if (picture.has_value())
                {
                    ahead.Staged.insert_or_assign(key, StagedRead{std::move(frame), std::move(*picture)});
                    ahead.StagedOrder.push_back(key);
                    ++ahead.GpuStaged;
                }
                else if (read.From->Kind == AheadKind::Next)
                {
                    Hold(ahead, key, std::move(frame));
                }
                else
                {
                    const size_t bytes = frame->Bytes();

                    if (ahead.Pictures.emplace(key, std::move(frame)).second)
                    {
                        ahead.PictureBytes += bytes;
                    }
                }
            }

            Unasked(ahead, read);
        };

        // The run's frames up to at (not including it), handed over. With ReadAhead::Lock held.
        const auto publish = [&](size_t at)
        {
            for (; handed < at; ++handed)
            {
                handOver(run[handed], std::move(frames[handed]), std::move(staged[handed]));
            }
        };

        // An entry in the span as a frame: cut from its stream, unpacked once (version 2) from the bytes read or, held
        // (MCHdArt::StartHold), from memory, or from its own bytes; null when it doesn't decode (the game's thread reads it
        // again and logs why). Without ReadAhead::Lock.
        const auto decode = [&](const AheadRead& read) -> std::shared_ptr<MCHdArt::Frame>
        {
            const AheadEntry& asked = read.Entry;
            const MCFileEntry& entry = EntryOf(read);
            const auto kept = std::ranges::find(streams, entry.Offset, &decltype(streams)::value_type::first);
            std::shared_ptr<const std::vector<uint8_t>> stream = kept != streams.end() ? kept->second : nullptr;

            if (stream == nullptr)
            {
                const std::optional<MCFastFile::Held> held = file.HeldOf(asked.Index);
                std::span<const uint8_t> stored;

                if (held.has_value())
                {
                    stored = held->Stored;
                }
                else if (bytes.size() == end - start)
                {
                    const auto from = bytes.begin() + (static_cast<ptrdiff_t>(entry.Offset) - start);
                    stored = std::span<const uint8_t>(from, from + entry.Size);
                }
                else
                {
                    return nullptr;
                }

                auto unpacked = file.StreamOf(asked.Index, stored);

                if (!unpacked.has_value())
                {
                    return nullptr;
                }

                stream = std::move(*unpacked);

                if (kept != streams.end())
                {
                    kept->second = stream;
                }
                else if (file.IsVersion2())
                {
                    streams.emplace_back(entry.Offset, stream);
                }
            }

            auto decoded = MCHdArt::DecodeHdf(file.EntryIn(asked.Index, *stream), asked.Width, asked.Height);
            return decoded.has_value() ? Finished(std::move(*decoded), read.From->BakedShadows, 0) : nullptr;
        };

        if (current && alone)
        {
            const AheadEntry& entry = run.front().Entry;

            if (auto read = ReadEntry(*archive, entry.Index, entry.Entry, entry.Width, entry.Height,
                                      MCOptionalRef<MCDiskFile>(handle->File));
                read.has_value())
            {
                frames.front() = Finished(std::move(*read), run.front().From->BakedShadows, 0);
                staged.front() = stage(run.front(), frames.front());
            }
        }
        else if (current)
        {
            if (toRead)
            {
                bytes.resize(end - start);
                readWhole = handle->File.ReadAt(start, bytes) == bytes.size();
                readBy = TraceTime();
            }

            for (size_t at = 0; readWhole && at < run.size(); ++at)
            {
                frames[at] = slotsFull && fills(run[at]) ? nullptr : decode(run[at]);
                staged[at] = stage(run[at], frames[at]);

                // Each handed over as it's unpacked, so a draw waiting for one (TakeAhead) waits for it alone, not the run;
                // the rest dropped once the pack is set.
                lock.lock();
                publish(at + 1);
                ahead.Changed.notify_all();
                const bool forgotten = ahead.Generation != generation;
                lock.unlock();

                if (forgotten)
                {
                    break;
                }
            }
        }

        lock.lock();
        publish(run.size());
        size_t joined = 0;

        // The reads asked for while the span was read (JoinRead), served from its bytes in turn until none is left; then
        // the span is no longer read, so none joins it after.
        while (span.has_value())
        {
            std::vector<std::pair<AheadKind, AheadRead>> late = std::move((*span)->Joined);
            (*span)->Joined.clear();

            if (late.empty())
            {
                ahead.SpansRead.erase(*span);
                span.reset();
                break;
            }

            joined += late.size();

            for (auto& [kind, read] : late)
            {
                const bool usable = current && readWhole && ahead.Generation == generation;
                lock.unlock();
                std::shared_ptr<MCHdArt::Frame> frame = usable && !(slotsFull && fills(read)) ? decode(read) : nullptr;
                std::optional<MCHdArt::StagedPicture> picture = stage(read, frame);
                lock.lock();
                handOver(read, std::move(frame), std::move(picture));
                --ahead.JoinedOfKind[std::to_underlying(kind)];
                ahead.Changed.notify_all();
            }
        }

        if (current)
        {
            TraceRun(run, joined, begun, toRead ? std::optional<int64_t>(alone ? TraceTime() : readBy) : std::nullopt);
        }

        // Port-only (MCHdArt::FillGpu): the fill's frames the full slots left, read again in a later run of its.
        if (!requeue.empty() && ahead.Generation == generation && ahead.Slots == slots && ahead.GpuLeaves == leaves)
        {
            // The next reader's room takes the largest of them (it waits for that much rather than miss it again).
            ahead.GpuRoomNeed = std::max(ahead.GpuRoomNeed, roomNeed);
            QueueReads(ahead, AheadKind::Gpu, std::move(requeue));
        }

        // A handle is borrowed only for a read.
        if (toRead)
        {
            GiveBack(ahead, lock, std::move(handle), readWhole, generation);
        }
    }

    /// <summary>
    /// Port-only (HoldRun): a run of an archive's pieces (MCFastFile::HoldPiece) for the hold to read: the first, one past
    /// the last, the bytes they span, and the stored bytes of those it keeps (the bar counts them: MCHdArt::LoadFillShare).
    /// </summary>
    struct HoldSpan
    {
        std::shared_ptr<MCHdArchive> Archive;
        uint32_t First = 0;
        uint32_t End = 0;
        uint64_t Start = 0;
        uint64_t Stop = 0;
        uint64_t Kept = 0;
    };

    /// <summary>
    /// Port-only (TakeHoldRun, QueueHold): the first of <paramref name="file"/>'s pieces from <paramref name="from"/> on
    /// that the hold keeps (MCFastFile::HoldPiece::Kept); its piece count when none.
    /// </summary>
    uint32_t NextHeldPiece(const MCFastFile& file, uint32_t from)
    {
        const uint32_t count = file.HoldPieceCount();

        while (from < count && !file.HoldPieceAt(from).Kept)
        {
            ++from;
        }

        return from;
    }

    /// <summary>Port-only (QueueHold): whether the hold keeps any of <paramref name="archive"/> (MCHdArchive::HoldBytes).</summary>
    bool HoldsArchive(const MCHdArchive& archive)
    {
        return archive.HoldBytes > 0;
    }

    /// <summary>
    /// Port-only (MCHdArt::Manifest, MCHdArt::FillGpu): the bytes <paramref name="file"/>'s pieces the hold keeps take
    /// unpacked: its frames' pictures as the GPU takes them (BC7 colours and indices).
    /// </summary>
    uint64_t UnpackedKept(const MCFastFile& file)
    {
        uint64_t bytes = 0;

        for (uint32_t piece = 0; piece < file.HoldPieceCount(); ++piece)
        {
            const MCFastFile::HoldPiece facts = file.HoldPieceAt(piece);
            bytes += facts.Kept ? facts.Unpacked : 0;
        }

        return bytes;
    }

    /// <summary>Port-only (MCHdArt::StartHold): the stored bytes of the pieces the hold keeps from first to end.</summary>
    uint64_t KeptBytes(const MCFastFile& file, uint32_t first, uint32_t end)
    {
        uint64_t bytes = 0;

        for (uint32_t piece = first; piece < end; ++piece)
        {
            const MCFastFile::HoldPiece facts = file.HoldPieceAt(piece);
            bytes += facts.Kept ? facts.Stored : 0;
        }

        return bytes;
    }

    /// <summary>
    /// Port-only (TakeHoldRun, SPEC-memory.md's ground): takes the hold's next run of the first archive's pieces named
    /// (HoldItem::Pieces), from its next one on: those lying no further than MCHdArt::ReaderRunGap past the bytes
    /// before, while they lie in <paramref name="most"/> bytes (a larger one alone), and what lies between them; the
    /// archive leaves the queue with its last one. Call with ReadAhead::Lock held, the first archive's pieces named.
    /// </summary>
    HoldSpan TakeNamedRun(HoldQueue& holding, uint64_t most)
    {
        const HoldItem& item = holding.Archives.front();
        const MCFastFile& file = *item.Archive->File;
        size_t at = holding.NextStream;
        const MCFastFile::HoldPiece head = file.HoldPieceAt(item.Pieces[at]);
        HoldSpan span{item.Archive, item.Pieces[at], item.Pieces[at] + 1, head.At, head.At + head.Stored, head.Stored};

        for (++at; at < item.Pieces.size(); ++at)
        {
            const MCFastFile::HoldPiece next = file.HoldPieceAt(item.Pieces[at]);
            const uint64_t nextEnd = next.At + next.Stored;

            if (next.At > span.Stop + MCHdArt::ReaderRunGap || std::max(span.Stop, nextEnd) - span.Start > most)
            {
                break;
            }

            span.End = item.Pieces[at] + 1;
            span.Stop = std::max(span.Stop, nextEnd);
            span.Kept += next.Stored;
        }

        holding.NextStream = static_cast<uint32_t>(at);

        if (at == item.Pieces.size())
        {
            holding.Archives.pop_front();
            holding.NextStream = 0;
        }

        return span;
    }

    /// <summary>
    /// Port-only (HoldRun): takes the hold's next run: the first archive's pieces it keeps from its next one on, in the
    /// order they lie, none further than MCHdArt::ReaderRunGap past the bytes before, while they lie in
    /// MCHdArt::HoldRunBytes, or MCHdArt::BriefingRunBytes behind a briefing (a larger one alone; a version 1 entry stored
    /// once for two names within the run); an archive leaves the queue with its last one. None when none is left. Call
    /// with ReadAhead::Lock held: an archive's table never changes once read.
    /// </summary>
    std::optional<HoldSpan> TakeHoldRun(HoldQueue& holding)
    {
        // Port-only (SPEC-memory.md's "From the briefing"): behind a briefing's screens, shorter runs.
        const uint64_t most = holding.Briefing ? MCHdArt::BriefingRunBytes : MCHdArt::HoldRunBytes;

        while (!holding.Archives.empty())
        {
            // Port-only (SPEC-memory.md's ground): an archive's pieces named (HoldItem::Pieces), read as its own are.
            if (!holding.Archives.front().Pieces.empty())
            {
                return TakeNamedRun(holding, most);
            }

            const std::shared_ptr<MCHdArchive> archive = holding.Archives.front().Archive;
            const MCFastFile& file = *archive->File;
            const uint32_t count = file.HoldPieceCount();
            const uint32_t first = NextHeldPiece(file, holding.NextStream);
            uint32_t last = first + 1;
            uint64_t start = 0;
            uint64_t end = 0;

            if (first < count)
            {
                const MCFastFile::HoldPiece head = file.HoldPieceAt(first);
                start = head.At;
                end = head.At + head.Stored;

                for (; last < count; ++last)
                {
                    const MCFastFile::HoldPiece next = file.HoldPieceAt(last);
                    const uint64_t nextEnd = next.At + next.Stored;

                    // In the order they lie, so none starts before the run.
                    if (!next.Kept || next.At > end + MCHdArt::ReaderRunGap || std::max(end, nextEnd) - start > most)
                    {
                        break;
                    }

                    end = std::max(end, nextEnd);
                }
            }

            // The archive leaves the queue with its last piece to keep, not at a later take, so the queue empties as
            // the hold's last run is read (NoteHeld then) whatever pieces it doesn't keep lie after.
            holding.NextStream = first < count ? NextHeldPiece(file, last) : count;

            if (holding.NextStream == count)
            {
                holding.Archives.pop_front();
                holding.NextStream = 0;
            }

            if (first < count)
            {
                return HoldSpan{archive, first, last, start, end, KeptBytes(file, first, last)};
            }
        }

        return std::nullopt;
    }

    /// <summary>
    /// Port-only (MCHdArt::LoadFillShare): what the hold has read of the bytes it was handed (HoldRun counts a run's
    /// pieces read or not, so it reaches them): none while a load has handed it nothing yet, all once it met its room or
    /// was let go of (it reads no more), or outside a load when it was handed none. Call with ReadAhead::Lock held.
    /// </summary>
    double FillShareOf(const HoldQueue& holding)
    {
        if (holding.Full || !holding.On)
        {
            return 1.0;
        }

        if (holding.QueuedBytes == 0)
        {
            return holding.Loading ? 0.0 : 1.0;
        }

        return std::min(1.0, static_cast<double>(holding.ReadBytes) / static_cast<double>(holding.QueuedBytes));
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's plan): the colours the frames kept may always hold past their records: those read
    /// ahead for the next few draws.
    /// </summary>
    constexpr uint64_t ColourFloorBytes = uint64_t{64} << 20;

    /// <summary><paramref name="from"/> less <paramref name="take"/>, 0 when it takes it all.</summary>
    constexpr uint64_t Less(uint64_t from, uint64_t take)
    {
        return from > take ? from - take : 0;
    }

    /// <summary>A ledger holder's bytes as allocated (and retired, not yet freed).</summary>
    uint64_t AllocatedOf(MCMemoryLedger::MCHolder holder)
    {
        const MCMemoryLedger::MCFigures figures = MCMemoryLedger::Of(holder);
        return static_cast<uint64_t>(std::max<int64_t>(figures.Allocated + figures.Retired, 0));
    }

    /// <summary>Port-only (SPEC-memory.md's plan): the art's room and its parts now, as the ledger counts them.</summary>
    struct ArtParts
    {
        MCMemoryRoom::MCArtRoom Room;
        /// <summary>The hold's stored bytes (MCFastFile::HeldBytes).</summary>
        uint64_t Held = 0;
        /// <summary>What none of the shares takes back: the kept unpacked streams, what's read ahead, frames let go that live on.</summary>
        uint64_t Rest = 0;
        /// <summary>The kept unpacked streams, of Rest (MCFastFile::KeepStreamsUpTo).</summary>
        uint64_t Kept = 0;
        /// <summary>The frames' records (never let go of while the GPU holds their pictures) and ColourFloorBytes.</summary>
        uint64_t Floor = 0;
        /// <summary>The GPU's HD pictures when they're in the same room (shared memory), else 0.</summary>
        uint64_t Pictures = 0;
    };

    ArtParts ArtPartsNow()
    {
        using MCMemoryLedger::MCHolder;
        ArtParts parts{.Room = MCMemoryRoom::ArtRoom(), .Held = MCFastFile::HeldBytes()};
        parts.Kept = AllocatedOf(MCHolder::HdKeptStreams);
        parts.Rest = parts.Kept + AllocatedOf(MCHolder::HdReadAhead) + AllocatedOf(MCHolder::HdFramesLetGo);
        parts.Floor = AllocatedOf(MCHolder::HdFrames) + ColourFloorBytes;
        parts.Pictures = parts.Room.Shared ? AllocatedOf(MCHolder::GpuHdPictures) : 0;
        return parts;
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "Filled in this order"): the hold's share of the room: what the rest and the floor
    /// leave, less the pictures' working set (MCMemoryRoom::WorkingSetBytes) on shared memory, of which
    /// <paramref name="pictures"/> is counted (the hold reads into all of it, and gives back for what's drawn).
    /// </summary>
    uint64_t HoldShareOf(const ArtParts& parts, uint64_t pictures)
    {
        const uint64_t workingSet = parts.Room.Shared ? std::min(pictures, MCMemoryRoom::WorkingSetBytes) : 0;
        return Less(parts.Room.Bytes, parts.Rest + parts.Floor + workingSet);
    }

    /// <summary>
    /// Port-only (MCHdArt::HoldRoom): the most bytes the hold may keep, by the readers' <paramref name="settings"/>: none
    /// (no limit) when <c>-hdhold on</c> forces it, else its share with the whole working set kept for the pictures.
    /// </summary>
    std::optional<uint64_t> HoldRoomOf(const MCHdArt::ReaderSettings& settings)
    {
        if (settings.Hold.value_or(false))
        {
            return std::nullopt;
        }

        return HoldShareOf(ArtPartsNow(), MCMemoryRoom::WorkingSetBytes);
    }

    /// <summary>
    /// Port-only (HoldRun): the hold has met its room (<paramref name="room"/> bytes): it reads and queues no more this
    /// mission, what it holds kept, and says so in the frame log. Call with ReadAhead::Lock held.
    /// </summary>
    void FillHold(HoldQueue& holding, uint64_t room)
    {
        holding.Full = true;
        holding.Archives.clear();
        holding.NextStream = 0;

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("HD hold: full at {:.1f} MB, its room {:.1f} MB; the rest read as drawn",
                                         static_cast<double>(MCFastFile::HeldBytes()) / BytesPerMb,
                                         static_cast<double>(room) / BytesPerMb));
        }
    }

    /// <summary>
    /// Port-only (MCHdArt::StartHold): notes in the frame log what the hold keeps once it has read all it was handed:
    /// its bytes, its archives and the time since it began. Call with ReadAhead::Lock held.
    /// </summary>
    void NoteHeld(const HoldQueue& holding)
    {
        if (!MCFrameLog::Enabled())
        {
            return;
        }

        MCFrameLog::Note(
            std::format("HD hold: {:.1f} MB held of {} archives, {:.2f} s after it began",
                        static_cast<double>(MCFastFile::HeldBytes()) / BytesPerMb, holding.Queued.size(),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - holding.Began).count()));
    }

    /// <summary>
    /// Port-only (MCHdArt::StartHold): reads the hold's next run (TakeHoldRun) through a handle of a reader's own, in one
    /// read, without ReadAhead::Lock (<paramref name="lock"/>), and keeps each of its streams' stored bytes
    /// (MCFastFile::HoldStream), none unpacked, unless the hold was let go of or the pack set meanwhile. A read that fails
    /// is logged and its streams left to be read as they're drawn.
    /// </summary>
    void HoldRun(ReadAhead& ahead, std::unique_lock<std::mutex>& lock)
    {
        HoldQueue& holding = ahead.Holding;
        const std::optional<HoldSpan> run = TakeHoldRun(holding);

        if (!run.has_value())
        {
            return;
        }

        // Port-only (SPEC-memory.md's fill, as much as fits): a run past the room ends the hold's reads this mission.
        const uint64_t runBytes = run->Stop - run->Start;

        if (const std::optional<uint64_t> room = HoldRoomOf(ahead.Settings);
            room.has_value() && MCFastFile::HeldBytes() + holding.InFlightBytes + runBytes > *room)
        {
            FillHold(holding, *room);
            return;
        }

        holding.InFlightBytes += runBytes;
        // The run belongs to the generations it was taken in: after a let-go, memory short's stop or a pack set, none kept.
        const uint64_t generation = ahead.Generation;
        const uint64_t holdGeneration = holding.Generation;
        WaitWhileRunsHeld(ahead, lock);
        std::optional<ReaderHandle> handle = BorrowHandle(ahead, lock, run->Archive);
        // Borrowing may have let go of the lock: the hold may have been let go of, or the pack set, meanwhile.
        const bool current =
            handle.has_value() && ahead.Generation == generation && holding.Generation == holdGeneration;
        lock.unlock();
        const MCFastFile& file = *run->Archive->File;
        const uint64_t start = run->Start;
        const uint64_t end = run->Stop;
        const int64_t begun = TraceTime();
        std::vector<uint8_t> bytes;
        bool readWhole = false;

        if (current)
        {
            bytes.resize(end - start);
            readWhole = handle->File.ReadAt(static_cast<uint32_t>(start), bytes) == bytes.size();
        }

        const int64_t readBy = TraceTime();

        if (current && !readWhole)
        {
            SDL_Log("HD art: the hold's read of %s, %llu bytes at %llu, came short; its streams are read as drawn",
                    MCFileSystem::ToUtf8(run->Archive->Path).c_str(), static_cast<unsigned long long>(end - start),
                    static_cast<unsigned long long>(start));
        }

        lock.lock();
        holding.InFlightBytes -= runBytes;

        // The bar's share (MCHdArt::LoadFillShare) counts the run's pieces as read, held or not, so it ends.
        if (holding.Generation == holdGeneration)
        {
            holding.ReadBytes += run->Kept;
        }

        if (readWhole && ahead.Generation == generation && holding.On && holding.Generation == holdGeneration)
        {
            // Kept under the lock, so a let-go (StopHolding, then MCFastFile::LetGoHeld) can't miss them.
            file.HoldPieces(run->First, run->End, start, std::move(bytes));
            ++holding.Reads;
            TraceHold(*run->Archive, begun, readBy, start, end - start, run->End - run->First);

            if (holding.Archives.empty())
            {
                NoteHeld(holding);
            }
        }

        // Kept only for the hold's next run of the same archive: the hold reads every archive once, so its handles kept
        // among the idle would close the readers' (GiveBack's least recently given back), which they read again.
        const bool again = !holding.Archives.empty() && holding.Archives.front().Archive == run->Archive;
        GiveBack(ahead, lock, std::move(handle), readWhole && again, generation);
    }

    void StartReaders(ReadAhead& ahead);

    void QueueHoldPieces(ReadAhead& ahead, const std::shared_ptr<MCHdArchive>& archive, std::span<const uint64_t> keys);

    /// <summary>
    /// Port-only (MCHdArt::StartHold): has the hold read <paramref name="archive"/>, while it holds, once, if it keeps
    /// any of it (HoldsArchive), and the ground's frames asked for of it by name (HoldGround). Call with ReadAhead::Lock
    /// held.
    /// </summary>
    void QueueHold(ReadAhead& ahead, const std::shared_ptr<MCHdArchive>& archive)
    {
        HoldQueue& holding = ahead.Holding;

        if (!holding.On || archive == nullptr)
        {
            return;
        }

        if (holding.Handed.insert(archive).second)
        {
            holding.HandedGpuBytes += UnpackedKept(*archive->File);
        }

        if (holding.Full)
        {
            return;
        }

        // Port-only (SPEC-memory.md's ground): the ground's frames asked for by name while it opened (HoldGround).
        if (auto asked = holding.GroundAsked.extract(KeyOfPath(archive->Path)); !asked.empty())
        {
            QueueHoldPieces(ahead, archive, asked.mapped());
        }

        if (!HoldsArchive(*archive) || !holding.Queued.insert(archive).second)
        {
            return;
        }

        // Port-only (SPEC-memory.md's fill): its bytes counted to read, for the loading bar (MCHdArt::LoadFillShare).
        holding.QueuedBytes += archive->HoldBytes;
        holding.Archives.push_back(HoldItem{archive, {}});
        holding.Taken.push_back(archive);
        StopGpuRoomWaits(ahead);
        StartReaders(ahead);
        ahead.Changed.notify_all();
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's ground, HoldGround): has the hold read the pieces of <paramref name="archive"/> that
    /// hold the frames <paramref name="keys"/> names (a hashed folder's, MCHdArt::TerrainTileKey's), those it hasn't
    /// queued already while it holds, while it holds; a name the archive lacks is passed by. Call with ReadAhead::Lock
    /// held: an archive's table never changes once read.
    /// </summary>
    void QueueHoldPieces(ReadAhead& ahead, const std::shared_ptr<MCHdArchive>& archive, std::span<const uint64_t> keys)
    {
        HoldQueue& holding = ahead.Holding;
        const MCFastFile& file = *archive->File;
        const auto [at, fresh] = holding.PiecesQueued.try_emplace(archive);
        std::unordered_set<uint32_t>& queued = at->second;
        HoldItem item{archive, {}};

        if (fresh)
        {
            holding.Taken.push_back(archive);
        }

        for (const uint64_t key : keys)
        {
            const std::optional<int32_t> index = file.Find(HashName(key) + std::string(HdfExtension));
            const std::optional<uint32_t> piece = index.has_value() ? file.PieceOf(*index) : std::nullopt;

            if (piece.has_value() && queued.insert(*piece).second)
            {
                item.Pieces.push_back(*piece);
            }
        }

        if (item.Pieces.empty())
        {
            return;
        }

        // In the order they lie, so a run reads neighbours together (TakeHoldRun).
        std::ranges::sort(item.Pieces);

        for (const uint32_t piece : item.Pieces)
        {
            const MCFastFile::HoldPiece facts = file.HoldPieceAt(piece);
            holding.QueuedBytes += facts.Stored;
            holding.GroundGpuBytes += facts.Unpacked;
        }

        holding.Archives.push_back(std::move(item));
        StartReaders(ahead);
        ahead.Changed.notify_all();
    }

    /// <summary>
    /// Port-only (BeginHolding, StopHolding): the hold forgets what it was to read and what it was handed: its archives
    /// to read, those queued, the ground asked for, and the bytes counted for the bar. Call with ReadAhead::Lock held.
    /// </summary>
    void ForgetHoldQueue(HoldQueue& holding)
    {
        holding.Archives.clear();
        holding.Queued.clear();
        holding.PiecesQueued.clear();
        holding.Taken.clear();
        holding.GroundAsked.clear();
        holding.GroundGpuBytes = 0;
        holding.NextStream = 0;
        holding.QueuedBytes = 0;
        holding.ReadBytes = 0;
    }

    /// <summary>
    /// Port-only (MCHdArt::StartHold, MCHdArt::StartLoadFill): the hold begins a mission's when it's off, or when it met
    /// its room or gave some back (Full): what it was handed is handed again (an archive given back is read again), what
    /// it holds kept. One on and not full holds what it holds and takes what's handed next. Call with ReadAhead::Lock held.
    /// </summary>
    void BeginHolding(HoldQueue& holding)
    {
        if (holding.On && !holding.Full)
        {
            return;
        }

        holding.On = true;
        holding.Full = false;
        ForgetHoldQueue(holding);
        holding.Began = std::chrono::steady_clock::now();
    }

    /// <summary>
    /// Port-only (MCHdArt::LetGoHold): the hold holds no more until it's started again, its archives to read forgotten and
    /// a run being read keeping nothing; the caller lets go of what it keeps (MCFastFile::LetGoHeld) once none can be
    /// added. Call with ReadAhead::Lock held.
    /// </summary>
    void StopHolding(ReadAhead& ahead)
    {
        HoldQueue& holding = ahead.Holding;
        holding.On = false;
        ForgetHoldQueue(holding);
        holding.Handed.clear();
        holding.HandedGpuBytes = 0;
        holding.Loading = false;
        holding.Full = false;
        holding.Mission.clear();
        holding.Briefing = false;
        ++holding.Generation;
        ahead.Changed.notify_all();
    }

    /// <summary>
    /// Port-only (MCHdArt::OpenArchivesAhead): opens <paramref name="open"/>'s archive, without ReadAhead::Lock
    /// (<paramref name="lock"/>): looks in its folders in turn until one has an archive (or one can't be read), and hands
    /// the game's thread what each held (Opened) unless the pack was set meanwhile.
    /// </summary>
    void OpenAhead(ReadAhead& ahead, std::unique_lock<std::mutex>& lock, ArchiveOpen open)
    {
        lock.unlock();
        std::vector<std::pair<std::filesystem::path, std::expected<std::shared_ptr<MCHdArchive>, std::string>>> opened;

        for (const std::filesystem::path& folder : open.Folders)
        {
            std::expected<std::shared_ptr<MCHdArchive>, std::string> archive = OpenArchive(folder, open.Dictionaries);
            const bool ends = !archive.has_value() || *archive != nullptr;

            // Its file among those kept open from now (MaxOpenArchiveFiles), not only once the game takes it: a
            // mission's types ask for more archives than it draws.
            if (archive.has_value() && *archive != nullptr)
            {
                std::scoped_lock files(TheState().ArchiveFiles);
                KeepOpen((*archive)->File);
            }

            opened.emplace_back(folder, std::move(archive));

            if (ends)
            {
                break;
            }
        }

        lock.lock();

        if (open.Generation == ahead.Generation)
        {
            for (auto& [folder, archive] : opened)
            {
                // Port-only: one opened while a mission's art is held is held too (MCHdArt::StartHold), when it was asked
                // for that hold (a later mission's ask takes it over): one asked for a mission let go of since is the game's
                // thread's to take, never another's hold.
                const auto wanted = ahead.Opening.find(folder);
                const uint64_t holdGeneration =
                    wanted != ahead.Opening.end() ? wanted->second : ahead.Holding.Generation;

                if (archive.has_value() && holdGeneration == ahead.Holding.Generation)
                {
                    QueueHold(ahead, *archive);
                }

                ahead.Opened.insert_or_assign(folder, OpenedArchive{std::move(archive), holdGeneration});
            }
        }

        for (const std::filesystem::path& folder : open.Folders)
        {
            ahead.Opening.erase(folder);
        }
    }

    /// <summary>
    /// Whether the read-ahead has frame <paramref name="key"/> already: looked at by the game's thread, read ahead, read
    /// now or asked for (under ReadAhead::Lock).
    /// </summary>
    bool AheadHas(const ReadAhead& ahead, const FrameKey& key)
    {
        return ahead.Known.contains(key) || ahead.Ready.contains(key) || ahead.Pictures.contains(key) ||
               ahead.PicturesAsked.contains(key) || ahead.Reading.contains(key) || ahead.Staged.contains(key);
    }

    void ExpandAsks(ReadAhead& ahead, std::unique_lock<std::mutex>& lock, AskBatch&& batch);

    /// <summary>
    /// A reader (SPEC-first-use.md; several read at once): takes the kind of read due next (NextKind), makes a first
    /// draw's follow reads of the frames drawn after it as it comes to one (MakeReads), and reads each run of reads
    /// (TakeRun, ReadRun), as the game's thread would for a shape of any size (taking one, it checks the size it asks
    /// for). At the game's thread's priority, or a step below it (MCHdArt::ReaderSettings::Below), as it starts.
    /// </summary>
    void ReadFramesAhead(std::stop_token stop)
    {
        ReadAhead& ahead = TheState().Ahead;
        std::unique_lock lock(ahead.Lock);
        MCSetCurrentThreadPriority(ahead.GamePriority, ahead.Settings.Below);
        std::optional<AheadKind> kind;

        while (ahead.Changed.wait(lock, stop,
                                  [&]
                                  {
                                      return (kind = NextKind(ahead)).has_value() ||
                                             (!ahead.Opens.empty() && !ahead.OpensHeld) || !ahead.Asks.empty();
                                  }) &&
               !stop.stop_requested())
        {
            ++ahead.Working;

            if (!ahead.Opens.empty() && !ahead.OpensHeld)
            {
                ArchiveOpen open = std::move(ahead.Opens.front());
                ahead.Opens.pop_front();
                OpenAhead(ahead, lock, std::move(open));
            }
            else if (!ahead.Asks.empty())
            {
                AskBatch batch = std::move(ahead.Asks.front());
                ahead.Asks.pop_front();
                ExpandAsks(ahead, lock, std::move(batch));
            }
            else if (*kind == AheadKind::Next && ahead.WaitingOfKind[std::to_underlying(AheadKind::Next)] == 0)
            {
                Follow follow = std::move(ahead.Follows.front());
                ahead.Follows.pop_front();
                MakeReads(ahead, std::move(follow));
            }
            else if (*kind == AheadKind::Hold)
            {
                ++ahead.InFlight[std::to_underlying(AheadKind::Hold)];
                HoldRun(ahead, lock);
                --ahead.InFlight[std::to_underlying(AheadKind::Hold)];
            }
            else if (*kind == AheadKind::Gpu)
            {
                // Port-only (MCHdArt::FillGpu): room in the slots first, waited for without the lock and before any read
                // is taken (the fill's backpressure), so a draw never waits on a read the full slots hold up.
                // Its wait ends when other work comes (StopGpuRoomWaits), or after FillRoomWaitMost, to look for it.
                ++ahead.InFlight[std::to_underlying(AheadKind::Gpu)];
                const std::shared_ptr<MCUploadSlots> slots = ahead.Slots;
                const size_t most = FillRoomBytes(*slots);
                const size_t bytes = std::min(std::max(std::min(FillLeaseBytes, most), ahead.GpuRoomNeed), most);
                ahead.GpuRoomNeed = 0;
                const std::stop_token dropped = ahead.GpuRoomStop.get_token();
                lock.unlock();
                std::optional<MCUploadSlots::MCLease> room;

                {
                    std::stop_source giveUp;
                    const std::stop_callback readerStopped(stop, [&giveUp] { giveUp.request_stop(); });
                    const std::stop_callback fillDropped(dropped, [&giveUp] { giveUp.request_stop(); });
                    room = slots->Take(MCUploadSlots::MCKind::Fill, bytes, giveUp.get_token(),
                                       MCUploadSlots::MCWait::Free, FillRoomWaitMost);
                }

                lock.lock();

                if (room.has_value() && !stop.stop_requested() && !dropped.stop_requested() && ahead.Slots == slots)
                {
                    if (std::vector<AheadRead> run = TakeRun(ahead, AheadKind::Gpu); !run.empty())
                    {
                        ReadRun(ahead, lock, run, std::make_shared<MCUploadSlots::MCLease>(std::move(*room)));
                    }
                }

                --ahead.InFlight[std::to_underlying(AheadKind::Gpu)];
            }
            else if (std::vector<AheadRead> run = TakeRun(ahead, *kind); !run.empty())
            {
                ++ahead.InFlight[std::to_underlying(*kind)];
                ReadRun(ahead, lock, run);
                --ahead.InFlight[std::to_underlying(*kind)];
            }

            --ahead.Working;
            ahead.Changed.notify_all();
        }
    }

    /// <summary>
    /// Starts the readers (as many as ReadAhead::Settings says) the first time there is something to read, taking the
    /// game's thread's priority for them (this is it). Call with ReadAhead::Lock held.
    /// </summary>
    void StartReaders(ReadAhead& ahead)
    {
        if (!ahead.Readers.empty())
        {
            return;
        }

        ahead.GamePriority = MCCurrentThreadPriority();

        for (size_t reader = 0; reader < ahead.Settings.Readers; ++reader)
        {
            ahead.Readers.emplace_back(ReadFramesAhead);
        }
    }

    /// <summary>
    /// Port-only: hands the readers <paramref name="reads"/> as reads of <paramref name="kind"/>, each archive's in its
    /// offset order, so that its neighbouring entries are read in one read (TakeRun), starting the readers the first
    /// time. Call with ReadAhead::Lock held.
    /// </summary>
    void QueueReads(ReadAhead& ahead, AheadKind kind, std::vector<AheadRead>&& reads)
    {
        const auto offset = [](const AheadRead& read)
        { return read.From->Archive->File->EntryAt(read.Entry.Index).Offset; };
        std::ranges::stable_sort(reads,
                                 [&offset](const AheadRead& a, const AheadRead& b)
                                 {
                                     const MCHdArchive* const first = a.From->Archive.get();
                                     const MCHdArchive* const second = b.From->Archive.get();
                                     return first != second ? std::less<>{}(first, second) : offset(a) < offset(b);
                                 });

        for (AheadRead& read : reads)
        {
            AddWaiting(ahead, kind, std::move(read));
        }

        if (kind != AheadKind::Gpu)
        {
            StopGpuRoomWaits(ahead);
        }

        StartReaders(ahead);
        ahead.Changed.notify_all();
    }

    /// <summary>
    /// Port-only: hands the readers <paramref name="follow"/>'s given entries (Follow::Entries) as reads of its kind
    /// (QueueReads). Call with ReadAhead::Lock held.
    /// </summary>
    void QueueEntries(ReadAhead& ahead, Follow&& follow)
    {
        std::vector<AheadEntry> entries = std::move(follow.Entries);
        follow.Entries.clear();
        const AheadKind kind = follow.Kind;
        const auto from = std::make_shared<const Follow>(std::move(follow));
        std::vector<AheadRead> reads;
        reads.reserve(entries.size());

        for (AheadEntry& entry : entries)
        {
            reads.push_back(AheadRead{from, std::move(entry)});
        }

        QueueReads(ahead, kind, std::move(reads));
    }

    /// <summary>
    /// Port-only: calls <paramref name="look"/> with the names an ask of numbered <paramref name="frame"/> reads, in order:
    /// the frame and its in-betweens, its loop's wrap's in-betweens, and with <paramref name="turnMiddles"/> each turn
    /// middle with its in-betweens and its wrap's, each run up to the first name <paramref name="look"/> says the archive
    /// lacks (it returns false). ExpandAsks' reads and the opening's bytes (FolderGpuBytes).
    /// </summary>
    template <typename Look> void ForEachStep(int32_t frame, bool turnMiddles, const Look& look)
    {
        for (int32_t step = 0; step < MaxInBetweenSteps && look(NumberedKey(frame, step)); ++step)
        {
        }

        // A walk's loop's wrap (the last frame a tree plays, where its art draws more): its in-betweens toward the first
        // frame, up to the first the archive lacks.
        for (int32_t step = 1; step < LegsInBetweenSteps && look(NumberedKey(frame, WrapBetweenStep(step))); ++step)
        {
        }

        // Each turn middle, then its in-betweens (and its wrap's), up to the first the archive lacks.
        for (const int32_t turn : {TurnStep, TurnMirroredStep})
        {
            for (int32_t step = 0; turnMiddles && step < LegsInBetweenSteps &&
                                   look(NumberedKey(frame, step == 0 ? turn : TurnBetweenStep(turn, step)));
                 ++step)
            {
            }

            for (int32_t step = 1; turnMiddles && step < LegsInBetweenSteps &&
                                   look(NumberedKey(frame, WrapBetweenStep(TurnBetweenStep(turn, step))));
                 ++step)
            {
            }
        }
    }

    /// <summary>
    /// Port-only: makes <paramref name="batch"/>'s frames (AskAhead's) reads of its kind (QueueReads: each archive's in
    /// its order): each frame with its in-betweens up to the first its archive lacks, and a legs packet's turn middles
    /// with theirs, those not kept, read, looked at or asked for already, in order, while they take at most the batch's
    /// room (an entry's unpacked bytes are about its frame's). Looks in the archives without ReadAhead::Lock
    /// (<paramref name="lock"/>; an archive's directory never changes once read), then takes it AskChunk frames at a
    /// time. Some left out for want of room are asked for again later (State::AskAgain).
    /// </summary>
    void ExpandAsks(ReadAhead& ahead, std::unique_lock<std::mutex>& lock, AskBatch&& batch)
    {
        const uint64_t leaves = ahead.GpuLeaves;
        lock.unlock();

        struct Found
        {
            uint32_t From = 0;
            AheadEntry Entry;
            size_t Bytes = 0;
        };

        std::vector<Found> found;
        const auto look = [&](const AheadAsk& ask, uint64_t name)
        {
            const Follow& from = *batch.Follows[ask.From];
            std::string entry =
                from.Prefix + (ask.Hashed ? HashName(name) : NumberedName(name)) + std::string(HdfExtension);
            const std::optional<int32_t> index = from.Archive->File->Find(entry);

            if (!index.has_value())
            {
                return false;
            }

            const auto bytes = static_cast<size_t>(std::max<int64_t>(from.Archive->File->EntryAt(*index).RealSize, 0));
            found.push_back(Found{ask.From, AheadEntry{name, *index, std::move(entry), ask.Width, ask.Height}, bytes});
            return true;
        };

        for (const AheadAsk& ask : batch.Asks)
        {
            if (!ask.Steps)
            {
                if ((ask.Width > 0 && ask.Height > 0) || (ask.Width == 0 && ask.Height == 0))
                {
                    look(ask, ask.Name);
                }

                continue;
            }

            ForEachStep(static_cast<int32_t>(ask.Name & FrameNumberMask), ask.TurnMiddles,
                        [&](uint64_t name) { return look(ask, name); });
        }

        std::unordered_set<FrameKey, FrameKeyHash> named;
        size_t asked = 0;
        bool full = false;

        for (size_t start = 0;;)
        {
            lock.lock();

            // The pack set meanwhile: what was looked up is the old one's. Port-only: the fill left meanwhile, its asks
            // dropped.
            if (ahead.Generation != batch.Generation || (batch.Kind == AheadKind::Gpu && ahead.GpuLeaves != leaves))
            {
                break;
            }

            std::vector<AheadRead> reads;
            const size_t end = std::min(found.size(), start + AskChunk);

            for (size_t at = start; at < end && !full; ++at)
            {
                Found& next = found[at];
                const std::shared_ptr<const Follow>& from = batch.Follows[next.From];
                const FrameKey key{from->Key.Folder, next.Entry.Name};

                if (!named.insert(key).second || AheadHas(ahead, key))
                {
                    continue;
                }

                if (asked + next.Bytes > batch.Room)
                {
                    full = true;
                    break;
                }

                asked += next.Bytes;

                // Port-only (MCHdArt::FillGpu): the fill's, the lowest of all, leave the frame to be asked for sooner.
                if (batch.Kind != AheadKind::Gpu)
                {
                    ahead.PicturesAsked.insert(key);
                }

                reads.push_back(AheadRead{from, std::move(next.Entry)});
            }

            if (!reads.empty())
            {
                ahead.GpuReads += batch.Kind == AheadKind::Gpu ? reads.size() : 0;
                QueueReads(ahead, batch.Kind, std::move(reads));
            }

            start = end;

            if (full || start >= found.size())
            {
                break;
            }

            lock.unlock();
        }

        if (full)
        {
            TheState().AskAgain = true;
        }

        ahead.LoadAsks -= batch.Kind == AheadKind::Load ? 1 : 0;
        ahead.GpuAsks -= batch.Kind == AheadKind::Gpu ? 1 : 0;
        ahead.Changed.notify_all();
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
        StopGpuRoomWaits(ahead);
        StartReaders(ahead);
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

    /// <summary>Port-only (lib/MCFirstUse.h): frame <paramref name="key"/>'s name, its folder's path in the pack's.</summary>
    std::string KeyName(const FrameKey& key)
    {
        const FrameFolder& folder = TheState().Folders.at(key.Folder);
        const std::string name = folder.Names == Naming::Hash ? HashName(key.Name) : NumberedName(key.Name);
        return std::format("{}/{}", MCFileSystem::ToUtf8(folder.Path.lexically_relative(MCHdArt::PackFolder())), name);
    }

    /// <summary>
    /// The frame <paramref name="key"/> if the reading thread read it ahead (waiting while it reads it), taken from it;
    /// null when it didn't, and then it never will: the game's thread looks at it now.
    /// </summary>
    std::shared_ptr<MCHdArt::Frame> TakeAhead(const FrameKey& key)
    {
        ReadAhead& ahead = TheState().Ahead;
        std::unique_lock lock(ahead.Lock);

        // Port-only (lib/MCFirstUse.h): a frame the game's thread waits on a reader for in play is late.
        if (ahead.Reading.contains(key))
        {
            const MCFirstUseScope use(
                MCFirstUseKind::Wait, [&] { return std::format("a frame being read: {}", KeyName(key)); }, 0);
            ahead.Changed.wait(lock, [&] { return !ahead.Reading.contains(key); });
        }

        ahead.Known.insert(key);

        // Port-only: a picture read ahead, not kept yet (MCHdArt::KeepReadAhead).
        if (const auto picture = ahead.Pictures.find(key); picture != ahead.Pictures.end())
        {
            std::shared_ptr<MCHdArt::Frame> frame = std::move(picture->second);
            ahead.Pictures.erase(picture);
            ahead.PictureBytes -= frame->Bytes();
            return frame;
        }

        // Port-only (MCHdArt::FillGpu): one the fill put in the upload slots, its colours there: the renderer copies its
        // picture up as the draw asks for it (MCHdArt::TakeStagedPicture), never capped.
        if (const auto staged = ahead.Staged.find(key); staged != ahead.Staged.end())
        {
            std::shared_ptr<MCHdArt::Frame> frame = std::move(staged->second.Frame);
            TheState().TakenStaged = std::move(staged->second.Picture);
            ahead.Staged.erase(staged);
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
    /// readers' handles on the old pack's archives are closed and their reads have ended.
    /// </summary>
    void ForgetAhead()
    {
        ReadAhead& ahead = TheState().Ahead;
        std::unique_lock lock(ahead.Lock);
        ahead.Follows.clear();
        ForgetWaiting(ahead);
        // Port-only (MCHdArt::FillGpu): a fill reader waiting for room in the slots, which this thread may hold until its
        // frame ends, stops waiting: the readers are waited for below.
        ahead.GpuRoomNeed = 0;
        StopGpuRoomWaits(ahead);

        // The frames being read stay counted as read (Reading) until their reads end, dropped then: the game's thread
        // waits for one it asks for rather than read it beside them.
        ahead.Known.clear();
        ahead.Ready.clear();
        ahead.Order.clear();
        ahead.Bytes = 0;
        ahead.Pictures.clear();
        ahead.PictureBytes = 0;
        ahead.PicturesAsked.clear();
        ahead.Staged.clear();
        ahead.StagedOrder.clear();
        TheState().DrawnStaged.clear();

        // The archives to open; those being opened stay waited on (Opening) until their opens end, dropped then.
        for (const ArchiveOpen& open : ahead.Opens)
        {
            for (const std::filesystem::path& folder : open.Folders)
            {
                ahead.Opening.erase(folder);
            }
        }

        ahead.Opens.clear();
        ahead.Opened.clear();

        // The frames asked for and not made reads yet; a batch a reader makes reads of is dropped as it relocks.
        for (const AskBatch& batch : ahead.Asks)
        {
            ahead.LoadAsks -= batch.Kind == AheadKind::Load ? 1 : 0;
            ahead.GpuAsks -= batch.Kind == AheadKind::Gpu ? 1 : 0;
        }

        ahead.Asks.clear();
        // Port-only: the hold's archives are the old pack's (MCHdArt::StartHold); held under the lock, so none is added.
        StopHolding(ahead);
        MCFastFile::LetGoHeld();
        ++ahead.Generation;
        ++TheState().Forgets;
        ahead.Changed.notify_all();
        // The readers' handles on the old pack's archives: those kept closed now, those reading as their reads end; and
        // their reads ended, each holding its archive (a run's frames are handed over as they're unpacked).
        std::list<ReaderHandle> idle;
        idle.splice(idle.end(), ahead.Idle);
        CloseHandles(lock, std::move(idle));
        ahead.Changed.wait(lock, [&ahead] { return ahead.Borrowed == 0 && ahead.Working == 0; });
    }

    /// <summary>
    /// Traces frame <paramref name="key"/>'s first use, <paramref name="how"/> it came (kept: read ahead and kept; taken
    /// from the readers as drawn; read by the game's thread; lacked: not in the pack), the game's thread's
    /// <paramref name="micros"/> on it: <c>U time how folder name micros</c>, the folder from the pack's.
    /// </summary>
    void TraceUse(const FrameKey& key, std::string_view how, int64_t micros)
    {
        if (!Tracing())
        {
            return;
        }

        const FrameFolder& folder = TheState().Folders.at(key.Folder);
        const std::string name = folder.Names == Naming::Hash ? HashName(key.Name) : NumberedName(key.Name);
        const std::string path = MCFileSystem::ToUtf8(folder.Path.lexically_relative(MCHdArt::PackFolder()));
        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        trace.Out << std::format("U\t{}\t{}\t{}\t{}\t{}\n", TraceNow(trace), how, path, name, micros);
    }

    /// <summary>
    /// The frame <paramref name="key"/> as ReadFrame reads it, numbered <paramref name="id"/>: the reading thread's when
    /// it read it ahead and it fits the shape of <paramref name="width"/> x <paramref name="height"/> asked for (0 x 0:
    /// any, as MCHdArt::DecodeHdf takes them), else read now.
    /// </summary>
    std::expected<std::shared_ptr<MCHdArt::Frame>, std::string> ReadOrTake(const FrameKey& key, int32_t width,
                                                                           int32_t height, uint64_t id)
    {
        State& state = TheState();
        state.TakenStaged.reset();

        if (std::shared_ptr<MCHdArt::Frame> ahead = TakeAhead(key);
            ahead != nullptr &&
            ((width == 0 && height == 0) || (ahead->OriginalWidth() == width && ahead->OriginalHeight() == height)))
        {
            ahead->Id = id;
            ++state.TakenAhead;

            // Port-only (MCHdArt::FillGpu): its picture in the slots, for the renderer to take as it draws it.
            if (state.TakenStaged.has_value())
            {
                state.TakenStaged->Record = ahead;
                state.DrawnStaged.insert_or_assign(id, std::pair(std::move(*state.TakenStaged), state.Frame));
                state.TakenStaged.reset();
            }

            return ahead;
        }

        state.TakenStaged.reset();
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
            TraceUse(key, "kept", 0);

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
            Number(state, key, cached);
            const auto started = std::chrono::steady_clock::now();
            // Port-only: a frame's first use reads it here, as the frame draws, unless read ahead (the frame log's).
            MCFrameLog::Count(FrameLogReads);
            const size_t taken = state.TakenAhead;
            auto read = [&]
            {
                const MCFrameLog::Scope part(FrameLogRead);
                return ReadOrTake(key, width, height, cached.Id);
            }();
            const std::chrono::nanoseconds spent = std::chrono::steady_clock::now() - started;
            state.ReadTime += spent;
            TraceUse(key,
                     state.TakenAhead > taken               ? "taken"
                     : read.has_value() && *read == nullptr ? "lacked"
                                                            : "read",
                     std::chrono::duration_cast<std::chrono::microseconds>(spent).count());

            if (!read)
            {
                LogOnce(state.Folders.at(key.Folder).Path, read.error());
            }
            else if (*read != nullptr)
            {
                KeepRead(state, key, cached, std::move(*read));
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

    /// <summary>
    /// Port-only (SPEC-memory.md's one copy of a frame's colours): <paramref name="cached"/>'s colours read again, its
    /// record kept: the GPU let go of its picture, or never took it, since its colours went. As a first draw reads a
    /// frame, on this thread, and counted with the frame log's reads.
    /// </summary>
    void ReadColorsAgain(State& state, const FrameKey& key, Cached& cached)
    {
        const auto started = std::chrono::steady_clock::now();
        MCFrameLog::Count(FrameLogReads);
        auto read = [&]
        {
            const MCFrameLog::Scope part(FrameLogRead);
            return ReadOrTake(key, cached.Frame->OriginalWidth(), cached.Frame->OriginalHeight(), cached.Id);
        }();
        const std::chrono::nanoseconds spent = std::chrono::steady_clock::now() - started;
        state.ReadTime += spent;
        TraceUse(key, "colours", std::chrono::duration_cast<std::chrono::microseconds>(spent).count());

        if (!read)
        {
            LogOnce(state.Folders.at(key.Folder).Path, read.error());
        }
        else if (*read != nullptr)
        {
            HoldColors(state, cached, std::move((*read)->Colors));
            ++state.ColorsReadAgain;

            // Port-only (SPEC-memory.md's indices): the indices too, if its record let go of them.
            if (cached.Frame->Indices.empty() && !(*read)->Indices.empty())
            {
                IndicesBack(state, key, cached, std::move((*read)->Indices));
            }
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): <paramref name="cached"/>'s indices read again, its record kept with them:
    /// it let go of them once the GPU had its picture, before code on the CPU read its folder's. As a first draw reads a
    /// frame, on this thread, and counted with the frame log's reads.
    /// </summary>
    void ReadIndicesAgain(State& state, const FrameKey& key, Cached& cached)
    {
        const auto started = std::chrono::steady_clock::now();
        MCFrameLog::Count(FrameLogReads);
        auto read = [&]
        {
            const MCFrameLog::Scope part(FrameLogRead);
            return ReadOrTake(key, cached.Frame->OriginalWidth(), cached.Frame->OriginalHeight(), cached.Id);
        }();
        const std::chrono::nanoseconds spent = std::chrono::steady_clock::now() - started;
        state.ReadTime += spent;
        TraceUse(key, "indices", std::chrono::duration_cast<std::chrono::microseconds>(spent).count());

        if (!read)
        {
            LogOnce(state.Folders.at(key.Folder).Path, read.error());
        }
        else if (*read != nullptr && !(*read)->Indices.empty())
        {
            IndicesBack(state, key, cached, std::move((*read)->Indices));
            ++state.IndicesReadAgain;
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's indices): the cache's record holding <paramref name="frame"/>'s indices, read again
    /// if it let go of them, for a caller to hold while it reads them; null when <paramref name="frame"/>'s own are
    /// its indices. Read (MCHdArt::MCIndicesUse) marks its folder's as read on the CPU.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> IndexedRecord(const MCHdArt::Frame& frame, MCHdArt::MCIndicesUse use)
    {
        State& state = TheState();
        const auto numbered = state.ById.find(frame.Id);

        if (numbered == state.ById.end())
        {
            return nullptr;
        }

        const FrameKey key = numbered->second;

        if (use == MCHdArt::MCIndicesUse::Read)
        {
            state.Folders.at(key.Folder).IndicesRead = true;
        }

        const auto found = frame.Indices.empty() ? state.Frames.find(key) : state.Frames.end();

        if (found == state.Frames.end())
        {
            return nullptr;
        }

        Cached& cached = found->second;

        // Let go of whole since: read again, as a first draw reads it.
        if (cached.Frame == nullptr)
        {
            return Cache(key, frame.OriginalWidth(), frame.OriginalHeight());
        }

        if (cached.Frame->Indices.empty())
        {
            ReadIndicesAgain(state, key, cached);
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
    WriteLittle(bytes, frame.Marked ? HdfMarkedVersion : HdfVersion, 2);
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
        (ReadLittle(bytes, 4, 2) != HdfVersion && ReadLittle(bytes, 4, 2) != HdfMarkedVersion) ||
        (ReadLittle(bytes, 6, 1) != HdfBc7 && ReadLittle(bytes, 6, 1) != HdfBc7Blended &&
         ReadLittle(bytes, 6, 1) != HdfBc5))
    {
        return std::unexpected(std::string("not a version 1 or 2 BC7 or BC5 HD frame"));
    }

    Frame frame;
    frame.Marked = ReadLittle(bytes, 4, 2) == HdfMarkedVersion;
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

    if (frame.Blended || frame.Marked || width <= 0 || height <= 0 ||
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
    state.Sharp.reset();
    state.PackFileLooked = false;
    state.PackFormatReadable = true;
    state.PackFormatRead = 1;
    state.PackSprites.reset();
    state.DictionariesLooked = false;
    state.Dictionaries.reset();
    state.SpritesMatch.reset();
    state.Listed.reset();
    state.Sequences.clear();

    for (const auto& kept : state.Frames)
    {
        CountLetGo(state, kept.second.Frame);
    }

    state.Frames.clear();
    state.Order.clear();
    state.Uploaded.clear();
    state.Folders.clear();
    state.FolderIndices.clear();
    state.KeyedFolders.clear();
    ClearOpening(state);
    state.ShapeTableFolders.clear();
    state.Bytes = 0;
    state.AllocatedBytes = 0;
    state.ColorBytes = 0;
    state.ColorAllocatedBytes = 0;
    state.ById.clear();
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
    // Port-only: their frames went with the cache's (MCHdArt::KeepForMission).
    state.ForMission.clear();
    state.Neighbours.clear();
    ++state.Version;

    Trace& trace = TheTrace();
    std::scoped_lock lock(trace.Lock);
    trace.Pack = PackFolder();
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

void MCHdArt::SetLineArtFrames(std::span<const LineArtFrame> frames, LineArtSet set)
{
    auto& kept = TheState().LineArtFrames;
    std::erase_if(kept, [set](const auto& entry) { return entry.second.first == set; });

    for (const LineArtFrame& frame : frames)
    {
        kept.insert_or_assign({frame.Table, frame.Frame}, std::pair{set, frame.Drawn});
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
    return found == kept.end() ? std::span<const MCSharpLines::MCDrawnLoop>{} : found->second.second;
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

        state.PackFormatRead = *format;

        if (MCFitResult<std::string> sprites = file.Read<std::string>(PackSpritesKey); sprites.has_value())
        {
            state.PackSprites = std::move(*sprites);
            state.SpritesMatch.reset();
        }
    }
}

namespace
{
    /// <summary>Whether the pack folder exists and its pack.cfg (if it has one) is of a format this code reads.</summary>
    bool PackReadable()
    {
        State& state = TheState();

        if (!state.FolderExists.has_value())
        {
            std::error_code error;
            state.FolderExists = std::filesystem::is_directory(MCHdArt::PackFolder(), error);
        }

        if (!*state.FolderExists)
        {
            return false;
        }

        ReadPackFile();
        return state.PackFormatReadable;
    }
}

bool MCHdArt::Active()
{
    return GHdArt != 0 && PackReadable();
}

std::optional<std::filesystem::path> MCHdArt::SharpArchive()
{
    State& state = TheState();

    if (!state.Sharp.has_value())
    {
        const std::filesystem::path path = PackFolder() / SharpArchiveName;
        std::error_code error;
        state.Sharp = PackReadable() && std::filesystem::is_regular_file(path, error)
                          ? std::optional<std::filesystem::path>(path)
                          : std::nullopt;
    }

    return *state.Sharp;
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

    if (state.SpritesMatch.has_value())
    {
        return *state.SpritesMatch;
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

    state.SpritesMatch = same;
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
    TheState().SpritesMatch.reset();
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
                  const std::filesystem::path& folder, bool keepsEdges = false, bool stripped = false)
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

        state.Sequences[MCAddressKey::OfData(table)] =
            Sequence{owner, FolderIndex(path, Naming::Number), keepsEdges, stripped};
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
    return MechArchiveFolder(key.PartFile, key.MechNum) / std::to_string(key.Packet);
}

std::filesystem::path MCHdArt::MechArchiveFolder(std::string_view partFile, uint32_t mechNum)
{
    return std::filesystem::path(MechFolder) / MCFileSystem::FromUtf8(partFile) / std::to_string(mechNum);
}

auto MCHdArt::ArchiveFacts(const std::filesystem::path& folder, uint16_t type) -> std::optional<std::vector<uint8_t>>
{
    if (!Active())
    {
        return std::nullopt;
    }

    const std::shared_ptr<MCHdArchive> archive = ArchiveFor(PackFolder() / folder);

    if (archive == nullptr || !archive->File->IsVersion2())
    {
        return std::nullopt;
    }

    for (const auto& [recordType, data] : archive->File->Facts({}))
    {
        if (recordType == type)
        {
            return std::vector<uint8_t>(data.begin(), data.end());
        }
    }

    return std::nullopt;
}

bool MCHdArt::MarksOriginalPixels(const std::filesystem::path& folder)
{
    if (!Active())
    {
        return false;
    }

    const std::shared_ptr<MCHdArchive> archive = ArchiveFor(PackFolder() / folder);
    return archive != nullptr && archive->File->IsVersion2();
}

bool MCHdArt::DrawsStrippedTables()
{
    return NumberedFolders() && MCRenderer::GpuDrawing() == MCGpuDrawing::On;
}

namespace
{
    /// <summary>Why a folder's shape table record isn't found (FolderShapeRecord): its archive isn't of version 2.</summary>
    constexpr std::string_view NotVersion2 = "its folder isn't in a version 2 archive";
    /// <summary>Why a folder's shape table record isn't found: its folder holds none.</summary>
    constexpr std::string_view NoRecord = "its folder has no shape table record";

    /// <summary>A folder of the pack's shape table record (record 1), with its archive's view of the folder.</summary>
    struct FolderShapeTable
    {
        std::span<const uint8_t> Record;
        std::reference_wrapper<const FrameFolder::InArchive> Archived;
    };

    /// <summary>
    /// The shape table record of the pack's <paramref name="folder"/> (relative to the pack), held by its version 2
    /// archive; else why not (NotVersion2, NoRecord).
    /// </summary>
    auto FolderShapeRecord(const std::filesystem::path& folder) -> std::expected<FolderShapeTable, std::string_view>
    {
        State& state = TheState();
        const FrameFolder::InArchive& archived =
            ArchiveOf(state.Folders[FolderIndex(MCHdArt::PackFolder() / folder, Naming::Number)]);

        if (archived.Archive == nullptr || !archived.Archive->File->IsVersion2())
        {
            return std::unexpected(NotVersion2);
        }

        if (const std::optional<std::span<const uint8_t>> record = ShapeRecordOf(archived); record.has_value())
        {
            return FolderShapeTable{.Record = *record, .Archived = std::cref(archived)};
        }

        return std::unexpected(NoRecord);
    }
}

auto MCHdArt::StrippedShapeTable(const std::filesystem::path& folder) -> std::expected<PackShapeTable, std::string_view>
{
    constexpr std::string_view NotDrawn = "HD art doesn't draw stripped tables now";
    constexpr std::string_view LacksFrame = "its archive lacks a frame the table holds with a box";

    // Each refusal reads the packet from its PAK (the guard's, with why).
    const auto refuse = [&folder](std::string_view why)
    {
        NoteOriginalRead(std::format("a shape table read from its PAK: {}", why), MCFileSystem::ToUtf8(folder));
        return std::unexpected(why);
    };

    if (!DrawsStrippedTables())
    {
        return refuse(NotDrawn);
    }

    const std::expected<FolderShapeTable, std::string_view> found = FolderShapeRecord(folder);

    if (!found.has_value())
    {
        return refuse(found.error());
    }

    const std::span<const uint8_t> data = found->Record;
    const FrameFolder::InArchive& archived = found->Archived.get();
    const std::span<const uint8_t> table = data.subspan(std::min(MCShape::TableStart(data), data.size()));

    // The original's packet whole (stripping it would take its rows): its frames drawn from its rows where the
    // pack lacks them, as without a pack.
    if (const std::expected<std::vector<uint8_t>, std::string> rowless = MCPackRecords::ShapeTable(data);
        rowless.has_value() && rowless->size() < data.size())
    {
        return PackShapeTable{.Bytes = std::vector<uint8_t>(data.begin(), data.end()), .Stripped = false};
    }

    // Each frame it holds with a box is drawn from the pack, which must hold it: the table has no rows to draw.
    for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
    {
        const MCFrameBounds bounds = MCShapeFrameBounds(table, frame);

        if (MCVfxHasShape(table, frame) && bounds.Width > 0.0f && bounds.Height > 0.0f &&
            !archived.Archive->File->Find(std::format("{}{}{}", archived.Prefix, frame, HdfExtension)))
        {
            return refuse(LacksFrame);
        }
    }

    return PackShapeTable{.Bytes = std::vector<uint8_t>(data.begin(), data.end())};
}

auto MCHdArt::PackShapeFile(std::string_view gamePath, std::optional<int32_t> packet, uint32_t size)
    -> std::optional<std::vector<uint8_t>>
{
    if (!NumberedFolders())
    {
        return std::nullopt;
    }

    const std::expected<FolderShapeTable, std::string_view> found =
        FolderShapeRecord(ShapeFileFolder(gamePath, packet));

    // Another size is another file than the pack was made from (a mod's): the loader reads it.
    if (!found.has_value() || found->Record.size() != size)
    {
        return std::nullopt;
    }

    return std::vector<uint8_t>(found->Record.begin(), found->Record.end());
}

std::filesystem::path MCHdArt::ShapeFileFolder(std::string_view gamePath, std::optional<int32_t> packet)
{
    std::string name(gamePath);
    std::ranges::replace(name, '\\', '/');
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::filesystem::path folder = std::filesystem::path(ShapeFolder) / MCFileSystem::FromUtf8(name);
    return packet.has_value() ? folder / std::to_string(*packet) : folder;
}

void MCHdArt::NoteShapeLoaded(std::span<const uint8_t> bytes)
{
    // The renderer names a table by its bytes from where its draws point to the end of its block: the file's start,
    // or its table's past a shape file's version header.
    State& state = TheState();

    for (const uint64_t hash :
         {ShapeTableHash(bytes), ShapeTableHash(bytes.subspan(std::min(MCShape::TableStart(bytes), bytes.size())))})
    {
        state.LoadedShapeHashes.insert(hash);
        state.ShapeHashesToRead.push_back(hash);
    }
}

void MCHdArt::ReadLoadedShapesAhead()
{
    State& state = TheState();
    std::vector<uint64_t> hashes = std::exchange(state.ShapeHashesToRead, {});

    if (!Active())
    {
        return;
    }

    // A table read twice (a type's, again for the next mission) is asked once; a hash the pack has no folder for (the
    // other of each table's two) asks nothing.
    std::unordered_set<uint64_t> asked;
    std::vector<PacketAhead> tables;

    for (const uint64_t hash : hashes)
    {
        if (asked.insert(hash).second)
        {
            tables.push_back(PacketAhead{ShapeTableFolder(hash), std::nullopt, false});
        }
    }

    ReadPacketsAhead(tables, ReadAs::Load);
}

bool MCHdArt::Stripped(std::span<const uint8_t> table)
{
    const MCOptionalRef<const Sequence> sequence = AttachedSequence(table);
    return sequence.has_value() && sequence->get().Stripped;
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::StrippedFrame(std::span<const uint8_t> table, int32_t frame)
{
    if (!Stripped(table) || !MCVfxHasShape(table, frame))
    {
        return nullptr;
    }

    const MCFrameBounds bounds = MCShapeFrameBounds(table, frame);
    std::shared_ptr<const Frame> hd =
        Find(table, frame, static_cast<int32_t>(bounds.Width), static_cast<int32_t>(bounds.Height));
    return hd != nullptr && hd->Marked ? hd : nullptr;
}

void MCHdArt::VisitShape(std::span<const uint8_t> table, int32_t frame,
                         const std::function<void(int32_t x, int32_t y, uint8_t color)>& pixel)
{
    if (!Stripped(table))
    {
        VfxShapeVisit(table, frame, pixel);
        return;
    }

    const std::shared_ptr<const Frame> hd = StrippedFrame(table, frame);

    if (hd == nullptr)
    {
        return;
    }

    const MCFrameBounds bounds = MCShapeFrameBounds(table, frame);
    const auto width = static_cast<int32_t>(bounds.Width);
    const auto height = static_cast<int32_t>(bounds.Height);
    const auto minX = static_cast<int32_t>(bounds.MinX);
    const auto minY = static_cast<int32_t>(bounds.MinY);
    // Port-only (SPEC-memory.md's indices): its folder's frames keep their indices from now on; the record held while
    // the visits run, whatever they read.
    const std::shared_ptr<const Frame> record = IndexedRecord(*hd, MCIndicesUse::Read);
    const std::span<const uint8_t> indices = record != nullptr ? record->Indices : hd->Indices;

    for (int32_t y = 0; y < height; ++y)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            const size_t at =
                (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * IndexBytes;

            if (at + 1 < indices.size() && OriginalDrawsMark(indices[at + 1]))
            {
                pixel(minX + x, minY + y, indices[at]);
            }
        }
    }
}

void MCHdArt::Detach(std::span<const uint8_t> table)
{
    TheState().Sequences.erase(MCAddressKey::OfData(table));
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
        AttachTo(shape->FrameList(), shape, FolderOf(key), false, shape->Stripped);
    }
}

void MCHdArt::Attach(const std::shared_ptr<MCShape>& shape, const ObjectShapeKey& key)
{
    if (shape != nullptr)
    {
        AttachTo(shape->FrameList(), shape, FolderOf(key), false, shape->Stripped);
    }
}

void MCHdArt::Prepare(const MechShapeKey& key)
{
    // The archive standing for the folder opened by the readers, before Attach looks in it.
    const std::array<std::filesystem::path, 1> folder{FolderOf(key)};
    OpenArchivesAhead(folder);
}

void MCHdArt::OpenArchivesAhead(std::span<const std::filesystem::path> folders)
{
    if (!NumberedFolders() || folders.empty())
    {
        return;
    }

    State& state = TheState();

    // Port-only (MCHdArt::LoadFolders): the folders of the types a load makes, among those it hands the hold; a hashed
    // one (a shape table's) as its parent, whose archive any of them opens.
    for (const std::filesystem::path& folder : folders)
    {
        NoteLoadFolder(IsHashName(folder.filename().generic_string()) ? folder.parent_path() : folder);
    }

    const std::filesystem::path pack = PackFolder();
    // Read before the readers' lock: the first time, it reads the file.
    const std::shared_ptr<const MCZstd::Dictionaries> dictionaries = PackDictionaries();
    ReadAhead& ahead = state.Ahead;
    std::scoped_lock lock(ahead.Lock);
    bool asked = false;

    for (const std::filesystem::path& folder : folders)
    {
        // The folders ArchiveOf looks in, nearest first, up to one whose archive is found or asked for already.
        ArchiveOpen open{{}, ahead.Generation, dictionaries};

        for (std::filesystem::path at = pack / folder; at.has_relative_path() && at != pack; at = at.parent_path())
        {
            if (const auto known = state.Archives.find(KeyOfPath(at)); known != state.Archives.end())
            {
                if (known->second != nullptr)
                {
                    // Port-only (MCHdArt::StartLoadFill): one open from before is this mission's too, and held with it.
                    QueueHold(ahead, known->second);
                    break;
                }

                continue;
            }

            // Port-only: one opened for the game's thread to take is held too; one being opened is as it opens. Either is
            // this hold's from now (a briefing left for another mission's asks for some of the first's). A folder
            // found to have no archive of its own is passed for those above it, as the game's thread's walk passes it.
            if (const auto opened = ahead.Opened.find(at); opened != ahead.Opened.end())
            {
                opened->second.HoldGeneration = ahead.Holding.Generation;

                if (opened->second.Archive.has_value() && *opened->second.Archive == nullptr)
                {
                    continue;
                }

                if (opened->second.Archive.has_value())
                {
                    QueueHold(ahead, *opened->second.Archive);
                }

                break;
            }

            if (const auto opening = ahead.Opening.find(at); opening != ahead.Opening.end())
            {
                opening->second = ahead.Holding.Generation;
                break;
            }

            open.Folders.push_back(at);
        }

        if (!open.Folders.empty())
        {
            for (const std::filesystem::path& at : open.Folders)
            {
                ahead.Opening.insert_or_assign(at, ahead.Holding.Generation);
            }

            ahead.Opens.push_back(std::move(open));
            asked = true;
        }
    }

    if (asked)
    {
        StopGpuRoomWaits(ahead);
        StartReaders(ahead);
        ahead.Changed.notify_all();
    }
}

void MCHdArt::AttachFolder(const std::shared_ptr<MCShape>& shape)
{
    if (shape != nullptr && !shape->HdFolder.empty())
    {
        AttachTo(shape->FrameList(), shape, shape->HdFolder, false, shape->Stripped);
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

    return Cache(FrameKey{PictureFolderOf(hash), hash}, width, height);
}

namespace
{
    /// <summary>
    /// A frame to read ahead (AskAhead): its folder (FolderIndex), its key there and its draw's size (0 x 0: any, as
    /// DecodeHdf takes it); or (Steps) a numbered folder's frame with its in-betweens, from the first up to the first its
    /// archive lacks (and with TurnMiddles its turn middles'), of any size (each checked at its first draw, as a
    /// follow's is).
    /// </summary>
    struct FrameAsk
    {
        uint32_t Folder = 0;
        uint64_t Name = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        bool Steps = false;
        /// <summary>With Steps: the frame's turn middles too (a legs packet's: MCInBetween.h's TurnStep), each with its
        /// in-betweens.</summary>
        bool TurnMiddles = false;
    };

    /// <summary>
    /// Has the readers read <paramref name="asks"/> ahead as reads of <paramref name="kind"/>, at most
    /// <paramref name="room"/> bytes of them: a reader makes them reads (ExpandAsks: each frame with its steps, those
    /// not kept, read, looked at or asked for already, in order), so the game's thread only finds each folder's archive
    /// (once: ArchiveOf, which may take it from the readers, so before their lock). Only archives' frames, as the frames
    /// followed: loose files are a generator's, not a player's.
    /// </summary>
    void AskAhead(std::span<const FrameAsk> asks, AheadKind kind, size_t room)
    {
        State& state = TheState();
        AskBatch batch;
        batch.Kind = kind;
        batch.Room = room;
        batch.Asks.reserve(asks.size());

        // Each folder's follow in the batch, and whether its frames are named by a hash; none outside an archive.
        struct InBatch
        {
            std::optional<uint32_t> From;
            bool Hashed = false;
        };

        std::unordered_map<uint32_t, InBatch> folders;

        for (const FrameAsk& ask : asks)
        {
            const auto [at, fresh] = folders.try_emplace(ask.Folder);

            if (fresh)
            {
                FrameFolder& folder = state.Folders.at(ask.Folder);
                folder.Loose = folder.Loose.has_value() ? folder.Loose : IsDirectory(folder.Path);
                at->second.Hashed = folder.Names == Naming::Hash;

                if (const FrameFolder::InArchive* const archived = *folder.Loose ? nullptr : &ArchiveOf(folder);
                    archived != nullptr && archived->Archive != nullptr)
                {
                    Follow follow{FrameKey{ask.Folder, 0}, 0, archived->Archive, archived->Prefix,
                                  folder.BakedShadows,     {}};
                    follow.Kind = kind;
                    at->second.From = static_cast<uint32_t>(batch.Follows.size());
                    batch.Follows.push_back(std::make_shared<const Follow>(std::move(follow)));
                }
            }

            if (at->second.From.has_value())
            {
                batch.Asks.push_back(AheadAsk{*at->second.From, ask.Name, ask.Width, ask.Height, ask.Steps,
                                              ask.TurnMiddles, at->second.Hashed});
            }
        }

        if (batch.Asks.empty())
        {
            return;
        }

        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);
        batch.Generation = ahead.Generation;
        ahead.LoadAsks += kind == AheadKind::Load ? 1 : 0;
        ahead.GpuAsks += kind == AheadKind::Gpu ? 1 : 0;
        ahead.Asks.push_back(std::move(batch));

        if (kind != AheadKind::Gpu)
        {
            StopGpuRoomWaits(ahead);
        }

        StartReaders(ahead);
        ahead.Changed.notify_all();
    }

    /// <summary>The bytes the cache has room for past those it holds, with <paramref name="budget"/> its budget.</summary>
    size_t RoomIn(size_t budget)
    {
        const size_t held = TheState().Bytes;
        return budget > held ? budget - held : 0;
    }

    /// <summary>The kind of reads asked for as <paramref name="as"/>.</summary>
    AheadKind KindOf(MCHdArt::ReadAs as)
    {
        switch (as)
        {
            case MCHdArt::ReadAs::Load:
                return AheadKind::Load;
            case MCHdArt::ReadAs::Soon:
                return AheadKind::Soon;
            case MCHdArt::ReadAs::Set:
                return AheadKind::Set;
        }

        return AheadKind::Set;
    }

    /// <summary>
    /// The cache's budget the reads asked for as <paramref name="as"/> may fill: all of it for a load's, else the sets'
    /// share (they're kept as drawn ones are, and mustn't push those out).
    /// </summary>
    size_t RoomFor(MCHdArt::ReadAs as)
    {
        return as == MCHdArt::ReadAs::Load ? MCHdArt::CacheBudget() : MCHdArt::CacheBudget() / SetsAheadBudgetShare;
    }
}

void MCHdArt::ReadPicturesAhead(std::span<const PictureAhead> pictures, ReadAs as)
{
    if (!Active() || pictures.empty())
    {
        return;
    }

    // As <as> asks: a load's within the whole budget, else while they and the frames kept fit the sets' share (they're
    // kept as drawn ones are, and mustn't push those out); each from the first of PictureFolders holding it
    // (PictureFolderOf, as its draw finds it).
    std::vector<FrameAsk> asks;
    asks.reserve(pictures.size());

    for (const PictureAhead& picture : pictures)
    {
        if (picture.Width > 0 && picture.Height > 0)
        {
            const uint64_t hash = PictureHash(picture.Pixels, picture.Width, picture.Height);

            if (!TheState().KeptPictures.contains(hash))
            {
                asks.push_back(FrameAsk{PictureFolderOf(hash), hash, picture.Width, picture.Height});
            }
        }
    }

    AskAhead(asks, KindOf(as), RoomIn(RoomFor(as)));
}

namespace
{
    /// <summary>The asks for <paramref name="packets"/>' frames, each with its in-betweens (MCHdArt::ReadPacketsAhead).</summary>
    std::vector<FrameAsk> PacketAsks(std::span<const MCHdArt::PacketAhead> packets)
    {
        std::vector<FrameAsk> asks;

        for (const MCHdArt::PacketAhead& packet : packets)
        {
            const uint32_t index = FolderIndex(MCHdArt::PackFolder() / packet.Folder, Naming::Number);
            const int32_t count =
                packet.Frames.has_value() ? *packet.Frames : ArchivedFrames(TheState().Folders.at(index));

            for (int32_t frame = packet.First; frame < packet.First + count; ++frame)
            {
                asks.push_back(FrameAsk{index, NumberedKey(frame, 0), 0, 0, true, packet.TurnMiddles});
            }
        }

        return asks;
    }
}

void MCHdArt::ReadPacketsAhead(std::span<const PacketAhead> packets, ReadAs as)
{
    if (!NumberedFolders() || packets.empty())
    {
        return;
    }

    State& state = TheState();
    // Port-only (the opening view, MCOpeningViewAsks): what's drawn now is the load's, its folders noted.
    const ReadAs asked = state.OpeningView && as == ReadAs::Soon ? ReadAs::Load : as;

    if (state.OpeningView && asked == ReadAs::Load)
    {
        for (const PacketAhead& packet : packets)
        {
            state.OpeningFolders.insert(FolderIndex(PackFolder() / packet.Folder, Naming::Number));
        }
    }

    AskAhead(PacketAsks(packets), KindOf(asked), RoomIn(RoomFor(asked)));
}

void MCHdArt::ReadPacketAhead(const std::filesystem::path& folder, std::optional<int32_t> frames)
{
    const std::array<PacketAhead, 1> packet{{{folder, frames, false}}};
    // Port-only (the opening view, MCOpeningViewAsks): an object's packet the first frames draw is the load's.
    ReadPacketsAhead(packet, TheState().OpeningView ? ReadAs::Load : ReadAs::Set);
}

namespace
{
    /// <summary>
    /// Port-only (MCHdArt::ReadMenuSetAhead): the menus' archive <paramref name="set"/>'s entries, in the order they lie
    /// in it: a version 1 archive's directory's, a version 2 archive's from its names (SPEC-pack-v2.md: it has no
    /// directory), in its streams' order, read once and kept in <paramref name="state"/> until the archive changes;
    /// none (logged) when it keeps no names.
    /// </summary>
    const std::vector<MenuSetEntry>& MenuSetOf(State& state, const std::shared_ptr<MCHdArchive>& set)
    {
        if (state.MenuSetFrom.lock() == set)
        {
            return state.MenuSet;
        }

        state.MenuSetFrom = set;
        state.MenuSet.clear();
        const MCFastFile& file = *set->File;

        if (!file.IsVersion2())
        {
            for (const MCFileEntry& entry : file.GetEntries())
            {
                state.MenuSet.push_back(
                    {std::string(entry.GetName()), static_cast<size_t>(std::max(entry.RealSize, 0))});
            }

            return state.MenuSet;
        }

        std::expected<std::vector<std::string>, std::string> names = file.Names();

        if (!names.has_value())
        {
            LogOnce(set->Path, std::format("the screens' set isn't read ahead: {}", names.error()));
            return state.MenuSet;
        }

        // Each with its stream's place, so the readers take them as they lie.
        std::vector<std::pair<int32_t, MenuSetEntry>> placed;

        for (std::string& name : *names)
        {
            if (const std::optional<int32_t> index = file.Find(name); index.has_value())
            {
                const MCFileEntry entry = file.EntryAt(*index);
                placed.push_back({entry.Offset, {std::move(name), static_cast<size_t>(std::max(entry.RealSize, 0))}});
            }
        }

        std::ranges::stable_sort(placed, {}, &std::pair<int32_t, MenuSetEntry>::first);

        for (auto& [offset, entry] : placed)
        {
            state.MenuSet.push_back(std::move(entry));
        }

        return state.MenuSet;
    }
}

size_t MCHdArt::ReadMenuSetAhead()
{
    if (!Active())
    {
        return 0;
    }

    State& state = TheState();
    // Both indices first: finding a folder may add one, and moves the others.
    const uint32_t menus = FolderIndex(PackFolder() / PictureFolder, Naming::Hash);
    const uint32_t maps = FolderIndex(PackFolder() / MapFolder, Naming::Hash);
    const std::shared_ptr<MCHdArchive> set = ArchiveOf(state.Folders.at(menus)).Archive;
    const std::shared_ptr<MCHdArchive> apart = ArchiveOf(state.Folders.at(maps)).Archive;

    if (set == nullptr || apart == nullptr || apart == set)
    {
        return 0;
    }

    const std::string& prefix = ArchiveOf(state.Folders.at(menus)).Prefix;
    const size_t room = RoomIn(RoomFor(ReadAs::Set));
    std::vector<FrameAsk> asks;
    size_t bytes = 0;
    ReadAhead& ahead = state.Ahead;
    std::unique_lock lock(ahead.Lock);

    for (const MenuSetEntry& entry : MenuSetOf(state, set))
    {
        // "<prefix><16 hex digits>.hdf" (the archive's names compared without regard to case): the picture's hash
        // (HashName), its size any, checked at its first draw.
        const std::string_view name = entry.Name;

        if (name.size() != prefix.size() + HashNameDigits + HdfExtension.size() ||
            MCPort::StrICmp(name.substr(0, prefix.size()), prefix) != 0 ||
            MCPort::StrICmp(name.substr(name.size() - HdfExtension.size()), HdfExtension) != 0)
        {
            continue;
        }

        if (const std::optional<uint64_t> hash = HashOfName(name.substr(prefix.size(), HashNameDigits));
            hash.has_value() && !AheadHas(ahead, FrameKey{menus, *hash}))
        {
            // The readers take them in this order while they fit the room (ExpandAsks): so many bytes are asked for.
            const size_t size = entry.Bytes;

            if (bytes + size > room)
            {
                break;
            }

            bytes += size;
            asks.push_back(FrameAsk{menus, *hash, 0, 0});
        }
    }

    lock.unlock();
    AskAhead(asks, AheadKind::Set, room);
    return bytes;
}

void MCHdArt::KeepReadAhead()
{
    State& state = TheState();
    std::vector<std::pair<FrameKey, std::shared_ptr<Frame>>> read;

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);

        // Port-only: the readers' reads since the last keep (a frame's, from the display), in the frame log's count.
        if (ahead.ArchiveReads > state.ReaderReadsLogged)
        {
            MCFrameLog::Count(FrameLogReaderReads, static_cast<int64_t>(ahead.ArchiveReads - state.ReaderReadsLogged));
        }

        state.ReaderReadsLogged = ahead.ArchiveReads;

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
        ahead.PictureBytes = 0;
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
        Number(state, key, cached);
        frame->Id = cached.Id;
        KeepRead(state, key, cached, std::move(frame));
        ++state.Read;
        ++state.TakenAhead;
    }

    if (!read.empty())
    {
        Trim();
    }
}

MCHdArt::MCOpeningViewAsks::MCOpeningViewAsks()
{
    // A pass that never reached KeepOpeningView (an earlier mission's, cut short) leaves no folders to this one.
    State& state = TheState();
    state.OpeningFolders.clear();
    state.OpeningView = true;
}

MCHdArt::MCOpeningViewAsks::~MCOpeningViewAsks()
{
    TheState().OpeningView = false;
}

std::vector<std::shared_ptr<const MCHdArt::Frame>> MCHdArt::KeepOpeningView()
{
    KeepReadAhead();
    State& state = TheState();
    std::vector<std::shared_ptr<const Frame>> frames;

    for (const auto& [key, cached] : state.Frames)
    {
        if (cached.Frame != nullptr && cached.Undrawn && state.OpeningFolders.contains(key.Folder))
        {
            frames.push_back(cached.Frame);
        }
    }

    state.OpeningFolders.clear();
    return frames;
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
    /// Port-only (MCHdArt::KeepForMission, MCHdArt::KeepShapesForMission): marks the cached frame
    /// <paramref name="key"/> kept for the mission (Cached::ForMission), once.
    /// </summary>
    void KeepCachedForMission(State& state, const FrameKey& key)
    {
        if (Cached& cached = state.Frames.at(key); !cached.ForMission)
        {
            cached.ForMission = true;
            state.ForMission.push_back(key);
        }
    }
}

size_t MCHdArt::KeepForMission(std::span<const PictureAhead> pictures)
{
    if (!Active())
    {
        return 0;
    }

    State& state = TheState();

    for (const PictureAhead& picture : pictures)
    {
        if (picture.Width <= 0 || picture.Height <= 0)
        {
            continue;
        }

        // One the game made (KeptPictures) is kept apart from the cache already.
        const uint64_t hash = PictureHash(picture.Pixels, picture.Width, picture.Height);

        if (state.KeptPictures.contains(hash))
        {
            continue;
        }

        // Found as its draw finds it (checked against its size, as a first draw checks one kept as read ahead), read
        // now if the load's reads left it out; none when the pack lacks it.
        const FrameKey key{PictureFolderOf(hash), hash};

        if (Cache(key, picture.Width, picture.Height) == nullptr)
        {
            continue;
        }

        KeepCachedForMission(state, key);
    }

    return KeptForMissionBytes();
}

size_t MCHdArt::KeepShapesForMission(std::span<const std::vector<uint8_t>> tables)
{
    if (!Active())
    {
        return 0;
    }

    State& state = TheState();

    for (const std::vector<uint8_t>& table : tables)
    {
        const MCOptionalRef<const Sequence> sequence = AttachedSequence(table);

        if (!sequence.has_value())
        {
            continue;
        }

        const uint32_t folder = sequence->get().Folder;
        const int32_t frames = FramesIn(table);

        // Each frame found as its draw finds it (Find: checked against its bounds), none when the pack lacks it.
        for (int32_t frame = 0; frame < frames; ++frame)
        {
            if (!MCVfxHasShape(table, frame))
            {
                continue;
            }

            const MCFrameBounds bounds = MCShapeFrameBounds(table, frame);
            const FrameKey key{folder, NumberedKey(frame, 0)};

            if (Cache(key, static_cast<int32_t>(bounds.Width), static_cast<int32_t>(bounds.Height), frames) != nullptr)
            {
                KeepCachedForMission(state, key);
            }
        }
    }

    return KeptForMissionBytes();
}

size_t MCHdArt::KeptForMissionBytes()
{
    const State& state = TheState();
    size_t bytes = 0;

    for (const FrameKey& key : state.ForMission)
    {
        if (const auto found = state.Frames.find(key); found != state.Frames.end() && found->second.Frame != nullptr)
        {
            bytes += found->second.Frame->Bytes() + found->second.Colors.size();
        }
    }

    return bytes;
}

void MCHdArt::LetGoKeptForMission()
{
    State& state = TheState();

    for (const FrameKey& key : state.ForMission)
    {
        if (const auto found = state.Frames.find(key); found != state.Frames.end())
        {
            found->second.ForMission = false;
        }
    }

    // Trimmed from the next frame on as any others are (NextFrame), not all at once here.
    state.ForMission.clear();
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

uint64_t MCHdArt::TerrainTileKey(std::span<const uint8_t> tile)
{
    return TerrainTileHash(tile);
}

namespace
{
    /// <summary>A ground tile's or overlay's key: the one it carries, else its bytes' hash.</summary>
    uint64_t GroundKeyOf(const MCHdArt::GroundAhead& tile)
    {
        return tile.Key.has_value() ? *tile.Key : TerrainTileHash(tile.Tile);
    }
}

namespace
{
    /// <summary>
    /// The frame of <paramref name="folder"/> (TerrainFolder, ReliefFolder or OverlayFolder: State::KeyedFolders keeps
    /// its index) named <paramref name="key"/> (a terrain tile's, an overlay's: TerrainTileHash of its data), its bounds
    /// <paramref name="width"/> x <paramref name="height"/>; null with HD art off.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> FindKeyed(std::string_view folder, uint64_t key, int32_t width,
                                                    int32_t height)
    {
        if (!MCHdArt::Active())
        {
            return nullptr;
        }

        State& state = TheState();
        auto [index, fresh] = state.KeyedFolders.try_emplace(folder, 0);

        if (fresh)
        {
            index->second = FolderIndex(MCHdArt::PackFolder() / folder, Naming::Hash);
        }

        return Cache(FrameKey{index->second, key}, width, height);
    }

    /// <summary>As FindKeyed, for the frame named by its picture's data <paramref name="data"/>.</summary>
    std::shared_ptr<const MCHdArt::Frame> FindHashed(std::string_view folder, std::span<const uint8_t> data,
                                                     int32_t width, int32_t height)
    {
        return MCHdArt::Active() ? FindKeyed(folder, TerrainTileHash(data), width, height) : nullptr;
    }
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainTile(std::span<const uint8_t> tile, int32_t width,
                                                               int32_t height)
{
    return FindHashed(TerrainFolder, tile, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainTileByKey(uint64_t key, int32_t width, int32_t height)
{
    return FindKeyed(TerrainFolder, key, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainRelief(std::span<const uint8_t> tile, int32_t width,
                                                                 int32_t height)
{
    return FindHashed(ReliefFolder, tile, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindTerrainReliefByKey(uint64_t key, int32_t width, int32_t height)
{
    return FindKeyed(ReliefFolder, key, width, height);
}

namespace
{
    /// <summary>
    /// Port-only (SPEC-memory.md's ground by name): notes the ground's frames <paramref name="frames"/> of
    /// <paramref name="folder"/> (TerrainFolder's, OverlayFolder's) among the pack's folders the load handed the hold
    /// (MCHdArt::LoadFolders), each as its folder and name (<c>terrain/&lt;hash&gt;</c>), which a briefing holds by
    /// name (MCHdArt::StartBriefingHold).
    /// </summary>
    void NoteLoadGround(std::string_view folder, std::span<const MCHdArt::GroundAhead> frames)
    {
        if (!TheState().RecordingLoad)
        {
            return;
        }

        for (const MCHdArt::GroundAhead& frame : frames)
        {
            NoteLoadFolder(std::filesystem::path(folder) / HashName(GroundKeyOf(frame)));
        }
    }
}

void MCHdArt::ReadGroundAhead(std::span<const GroundAhead> tiles, bool relief)
{
    if (!Active() || tiles.empty())
    {
        return;
    }

    NoteLoadGround(TerrainFolder, tiles);

    // Each tile's relief map after the tiles (another archive), within the cache's budget: a load reads the ground
    // whole, and the frames past it are read as the load finds them.
    const uint32_t tileFolder = FolderIndex(PackFolder() / TerrainFolder, Naming::Hash);
    const uint32_t reliefFolder = FolderIndex(PackFolder() / ReliefFolder, Naming::Hash);
    std::vector<FrameAsk> asks;
    asks.reserve(tiles.size() * 2);

    for (const uint32_t folder : {tileFolder, reliefFolder})
    {
        for (const GroundAhead& tile : tiles)
        {
            if (folder == tileFolder || relief)
            {
                asks.push_back(FrameAsk{folder, GroundKeyOf(tile), tile.Width, tile.Height});
            }
        }
    }

    AskAhead(asks, AheadKind::Load, RoomIn(CacheBudget()));
}

void MCHdArt::ReadOverlaysAhead(std::span<const GroundAhead> overlays)
{
    if (!Active() || overlays.empty())
    {
        return;
    }

    NoteLoadGround(OverlayFolder, overlays);

    const uint32_t folder = FolderIndex(PackFolder() / OverlayFolder, Naming::Hash);
    std::vector<FrameAsk> asks;
    asks.reserve(overlays.size());

    for (const GroundAhead& overlay : overlays)
    {
        asks.push_back(FrameAsk{folder, GroundKeyOf(overlay), overlay.Width, overlay.Height});
    }

    AskAhead(asks, AheadKind::Load, RoomIn(CacheBudget()));
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindOverlay(std::span<const uint8_t> shape, int32_t width,
                                                           int32_t height)
{
    return FindHashed(OverlayFolder, shape, width, height);
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindOverlayByKey(uint64_t key, int32_t width, int32_t height)
{
    return FindKeyed(OverlayFolder, key, width, height);
}

uint64_t MCHdArt::ShapeTableHash(std::span<const uint8_t> table)
{
    return TerrainTileHash(table);
}

std::filesystem::path MCHdArt::ShapeTableFolder(uint64_t hash)
{
    return std::filesystem::path(ShapeFolder) / HashName(hash);
}

namespace
{
    /// <summary>The folder (FolderIndex) of the shape table whose bytes hash to <paramref name="hash"/>, found once.</summary>
    uint32_t ShapeTableFolderIndex(uint64_t hash)
    {
        const auto [folder, fresh] = TheState().ShapeTableFolders.try_emplace(hash, 0);

        if (fresh)
        {
            folder->second = FolderIndex(MCHdArt::PackFolder() / MCHdArt::ShapeTableFolder(hash), Naming::Number);

            // Each table named by its bytes was read from the original's files to be named (the guard's), but those
            // MCShapeFile read: noted where it read them, or the pack's copies.
            if (!TheState().LoadedShapeHashes.contains(hash))
            {
                MCHdArt::NoteOriginalRead(MCHdArt::ShapeHashRead, HashName(hash));
            }
        }

        return folder->second;
    }
}

std::shared_ptr<const MCHdArt::Frame> MCHdArt::FindShape(uint64_t hash, int32_t frame, int32_t width, int32_t height,
                                                         int32_t step, int32_t frames)
{
    if (!Active())
    {
        return nullptr;
    }

    return Cache(FrameKey{ShapeTableFolderIndex(hash), NumberedKey(frame, step)}, width, height, frames);
}

void MCHdArt::ReadShapesAhead(std::span<const ShapeAhead> shapes, ReadAs as)
{
    if (!Active() || shapes.empty())
    {
        return;
    }

    std::vector<FrameAsk> asks;
    asks.reserve(shapes.size());

    for (const ShapeAhead& shape : shapes)
    {
        asks.push_back(FrameAsk{ShapeTableFolderIndex(ShapeTableHash(shape.Table)), NumberedKey(shape.Frame, 0),
                                shape.Width, shape.Height});
    }

    AskAhead(asks, KindOf(as), RoomIn(RoomFor(as)));
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

bool MCHdArt::OpenTrace(const std::filesystem::path& path)
{
    CloseTrace();
    Trace& trace = TheTrace();
    std::scoped_lock lock(trace.Lock);
    trace.Out.open(path, std::ios::binary | std::ios::trunc);

    if (!trace.Out)
    {
        return false;
    }

    trace.Start = std::chrono::steady_clock::now();
    trace.Archives.clear();
    trace.Pack = PackFolder();
    trace.On = true;
    return true;
}

void MCHdArt::CloseTrace()
{
    Trace& trace = TheTrace();
    std::scoped_lock lock(trace.Lock);
    trace.On = false;
    trace.Out.close();
}

void MCHdArt::TraceNote(std::string_view text)
{
    if (Tracing())
    {
        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        trace.Out << std::format("N\t{}\t{}\n", TraceNow(trace), text);
    }
}

namespace
{
    /// <summary>
    /// Port-only (MCHdArt::GuardOriginalReads): the reads of the original's art noted while the guard is on, by kind,
    /// each with its first few; whether it's on (tested without the lock).
    /// </summary>
    struct Guard
    {
        std::mutex Lock;
        std::atomic<bool> On = false;
        std::map<std::string, MCHdArt::GuardedRead, std::less<>> Reads;
        size_t Examples = MCHdArt::GuardExamples;
    };

    Guard& TheGuard()
    {
        static Guard guard;
        return guard;
    }
} // namespace

void MCHdArt::GuardOriginalReads(bool on, size_t examples)
{
    Guard& guard = TheGuard();
    std::scoped_lock lock(guard.Lock);
    guard.Reads.clear();
    guard.Examples = examples;
    guard.On = on;
}

bool MCHdArt::GuardingOriginalReads()
{
    return TheGuard().On.load(std::memory_order_relaxed) && Active() && TheState().PackFormatRead >= PackFormat;
}

void MCHdArt::NoteOriginalRead(std::string_view kind, std::string_view what)
{
    if (!GuardingOriginalReads())
    {
        return;
    }

    Guard& guard = TheGuard();
    std::scoped_lock lock(guard.Lock);
    auto found = guard.Reads.find(kind);

    if (found == guard.Reads.end())
    {
        found = guard.Reads.emplace(std::string(kind), GuardedRead{.Kind = std::string(kind)}).first;
    }

    if (found->second.Examples.size() < guard.Examples)
    {
        found->second.Examples.emplace_back(what);
    }

    found->second.Count++;
}

auto MCHdArt::GuardedReads() -> std::optional<std::vector<GuardedRead>>
{
    Guard& guard = TheGuard();
    std::scoped_lock lock(guard.Lock);

    if (!guard.On)
    {
        return std::nullopt;
    }

    std::vector<GuardedRead> reads;
    std::ranges::transform(guard.Reads, std::back_inserter(reads), [](const auto& entry) { return entry.second; });
    return reads;
}

size_t MCHdArt::ArchivesOpenedAhead()
{
    return TheState().OpenedAhead;
}

uint64_t MCHdArt::AheadGeneration()
{
    const State& state = TheState();
    return state.Forgets + state.ReAsks;
}

bool MCHdArt::MCAskedPackets::Ask(uint64_t packet)
{
    const uint64_t generation = AheadGeneration() + 1;
    const auto [asked, added] = _Asked.try_emplace(packet, generation);

    if (!added && asked->second == generation)
    {
        return false;
    }

    asked->second = generation;
    return true;
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
    // Port-only (MCHdArt::FillGpu): but the fill's, which go on as the frames drawn take its pictures up (its readers'
    // runs serve the others' reads joined to them, JoinRead).
    constexpr size_t gpu = std::to_underlying(AheadKind::Gpu);
    const auto fills = [](const AskBatch& batch) { return batch.Kind == AheadKind::Gpu; };
    ahead.Changed.wait(lock,
                       [&]
                       {
                           const size_t joined = std::reduce(ahead.JoinedOfKind.begin(), ahead.JoinedOfKind.end());
                           return ahead.Follows.empty() && ahead.Working == ahead.InFlight[gpu] &&
                                  joined == ahead.JoinedOfKind[gpu] &&
                                  ahead.Reading.size() == ahead.GpuReading.size() && ahead.Opens.empty() &&
                                  std::ranges::all_of(ahead.Asks, fills) &&
                                  ahead.Waiting.size() == ahead.WaitingOfKind[gpu];
                       });
}

void MCHdArt::WaitForGpuReads()
{
    ReadAhead& ahead = TheState().Ahead;
    std::unique_lock lock(ahead.Lock);
    constexpr size_t gpu = std::to_underlying(AheadKind::Gpu);
    ahead.Changed.wait_for(lock, GpuReadsWaitMost,
                           [&]
                           {
                               return ahead.GpuAsks == 0 &&
                                      (!ahead.Staged.empty() || (ahead.WaitingOfKind[gpu] == 0 &&
                                                                 ahead.InFlight[gpu] == 0 && ahead.GpuReading.empty()));
                           });
}

void MCHdArt::SetReaderSettings(const ReaderSettings& settings)
{
    ReadAhead& ahead = TheState().Ahead;
    std::vector<std::jthread> stopping;

    {
        std::scoped_lock lock(ahead.Lock);
        ahead.Settings = settings;
        ahead.Settings.Readers = std::clamp<size_t>(settings.Readers, 1, MCHdArt::MostReaders);
        stopping.swap(ahead.Readers);

        for (std::jthread& reader : stopping)
        {
            reader.request_stop();
        }

        ahead.Changed.notify_all();
    }

    // Each ends once the read or open in its hands is done; what's left to read or open waits for the new ones.
    stopping.clear();
    std::scoped_lock lock(ahead.Lock);

    if (!ahead.Follows.empty() || !ahead.Opens.empty() || !ahead.Asks.empty() || !ahead.Waiting.empty() ||
        HoldPending(ahead))
    {
        StartReaders(ahead);
        ahead.Changed.notify_all();
    }
}

void MCHdArt::WaitForLoadReads()
{
    ReadAhead& ahead = TheState().Ahead;
    std::unique_lock lock(ahead.Lock);
    constexpr auto load = std::to_underlying(AheadKind::Load);
    ahead.Changed.wait(lock,
                       [&ahead]
                       {
                           return ahead.WaitingOfKind[load] == 0 && ahead.InFlight[load] == 0 &&
                                  ahead.JoinedOfKind[load] == 0 && ahead.Opens.empty() && ahead.Opening.empty() &&
                                  ahead.LoadAsks == 0;
                       });
}

void MCHdArt::HoldReads(bool hold)
{
    ReadAhead& ahead = TheState().Ahead;
    std::unique_lock lock(ahead.Lock);

    if (!hold)
    {
        // What was asked for made reads first, so the readers find them all waiting.
        ahead.Changed.wait(lock, [&ahead] { return ahead.Asks.empty() && ahead.Opens.empty() && ahead.Working == 0; });
    }

    ahead.Held = hold;
    ahead.Changed.notify_all();
}

void MCHdArt::HoldOpens(bool hold)
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    ahead.OpensHeld = hold;
    ahead.Changed.notify_all();
}

void MCHdArt::HoldTakenRuns(bool hold)
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    ahead.RunsHeld = hold;
    ahead.Changed.notify_all();
}

void MCHdArt::WaitForHeldRun()
{
    ReadAhead& ahead = TheState().Ahead;
    std::unique_lock lock(ahead.Lock);
    ahead.Changed.wait(lock,
                       [&ahead]
                       {
                           return ahead.RunsHolding > 0 && ahead.Asks.empty() && ahead.Opens.empty() &&
                                  ahead.Working == ahead.RunsHolding;
                       });
}

MCHdArt::ReaderSettings MCHdArt::GetReaderSettings()
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return ahead.Settings;
}

size_t MCHdArt::ReaderReads()
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return ahead.ArchiveReads;
}

bool MCHdArt::HoldWanted()
{
    return GetReaderSettings().Hold.value_or(true);
}

std::optional<uint64_t> MCHdArt::HoldRoom()
{
    return HoldRoomOf(GetReaderSettings());
}

void MCHdArt::StartHold()
{
    if (!Active() || !HoldWanted())
    {
        return;
    }

    State& state = TheState();
    std::vector<std::shared_ptr<MCHdArchive>> open;

    for (const auto& [folder, archive] : state.Archives)
    {
        if (archive != nullptr)
        {
            open.push_back(archive);
        }
    }

    ReadAhead& ahead = state.Ahead;
    std::scoped_lock lock(ahead.Lock);

    // Those the readers opened for the mission's types and the game's thread hasn't taken yet too: this hold's, not a
    // mission's let go of since (a briefing left before its opens were taken).
    for (const auto& [folder, opened] : ahead.Opened)
    {
        if (opened.HoldGeneration == ahead.Holding.Generation && opened.Archive.has_value() &&
            *opened.Archive != nullptr)
        {
            open.push_back(*opened.Archive);
        }
    }

    // In the pack's order, so a trace's reads are the same run to run.
    std::ranges::sort(open, [](const auto& a, const auto& b) { return a->Path < b->Path; });
    BeginHolding(ahead.Holding);

    for (const std::shared_ptr<MCHdArchive>& archive : open)
    {
        QueueHold(ahead, archive);
    }
}

namespace
{
    /// <summary>
    /// Port-only: drops the fill's reads waiting, its asks not made reads yet and the pictures it put in the slots that
    /// the game's thread hasn't taken (their slots free as they go). Call with ReadAhead::Lock held.
    /// </summary>
    void DropGpuReads(ReadAhead& ahead)
    {
        std::deque<uint64_t>& queue = ahead.Reads[std::to_underlying(AheadKind::Gpu)];

        for (const uint64_t number : queue)
        {
            if (std::optional<WaitingRead> read = TakeWaiting(ahead, number); read.has_value())
            {
                Unasked(ahead, read->Read);
                DropEmptyIndex(ahead, read->Read.From->Archive);
            }
        }

        queue.clear();
        ++ahead.GpuLeaves;
        const size_t asks = static_cast<size_t>(
            std::erase_if(ahead.Asks, [](const AskBatch& batch) { return batch.Kind == AheadKind::Gpu; }));
        ahead.GpuAsks -= std::min(ahead.GpuAsks, asks);
        ahead.Staged.clear();
        ahead.StagedOrder.clear();
        ahead.GpuRoomNeed = 0;
        StopGpuRoomWaits(ahead);
    }

    /// <summary>
    /// Port-only (MCHdArt::FinishLoadFill): whether the fill on the GPU has put up all it asked for: no ask or read left,
    /// nothing in the slots the game's thread hasn't taken. Call with ReadAhead::Lock held.
    /// </summary>
    bool GpuReadsDone(const ReadAhead& ahead)
    {
        return ahead.GpuAsks == 0 && ahead.WaitingOfKind[std::to_underlying(AheadKind::Gpu)] == 0 &&
               ahead.InFlight[std::to_underlying(AheadKind::Gpu)] == 0 &&
               ahead.JoinedOfKind[std::to_underlying(AheadKind::Gpu)] == 0 && ahead.GpuReading.empty() &&
               ahead.Staged.empty();
    }

    /// <summary>
    /// Port-only (MCHdArt::FinishLoadFill): whether the fill on the GPU has put up all it was handed: every folder asked
    /// for, and all it asked for up (GpuReadsDone). Call with ReadAhead::Lock held.
    /// </summary>
    bool GpuFillDone(const State& state)
    {
        return state.GpuOrderAsked == state.GpuOrder.size() && state.LoadFoldersAsked == state.LoadFolders.size() &&
               GpuReadsDone(state.Ahead);
    }

    /// <summary>
    /// Port-only (MCHdArt::FinishLoadFill, a mission that doesn't fit whole): whether its opening is up: walked to its end
    /// or its room, and all it asked for up; with no fill on the GPU, none to wait for. Call with ReadAhead::Lock held.
    /// </summary>
    bool OpeningUp(const State& state)
    {
        return !state.GpuFillOn || state.Ahead.Slots == nullptr ||
               ((state.OpeningDone || state.OpeningAtRoom) && GpuReadsDone(state.Ahead));
    }

    /// <summary>
    /// Port-only (SPEC-memory.md: "the plan's own error, which the ledger measures"): how many times their own bytes the HD
    /// pictures take on the GPU, in the pages they share: as last measured (MeasurePagesShare); until the first,
    /// PagesShareFirst.
    /// </summary>
    double PagesShare(const State& state)
    {
        return state.GpuPagesShare.value_or(PagesShareFirst);
    }

    /// <summary>
    /// Port-only (PagesShare): once this load's fill has put its mission up whole, what the pictures' pages take: the
    /// ledger's allocations over the pictures' bytes, once PagesMeasuredFromBytes of them are there, kept for the next
    /// mission's. Never under 1; once a load. Call with ReadAhead::Lock held.
    /// </summary>
    void MeasurePagesShare(State& state)
    {
        if (!state.GpuFits || state.GpuPagesMeasured || state.GpuUp == state.GpuUpAtLoad || !GpuFillDone(state))
        {
            return;
        }

        state.GpuPagesMeasured = true;
        const MCMemoryLedger::MCFigures pictures = MCMemoryLedger::Of(MCMemoryLedger::MCHolder::GpuHdPictures);

        if (pictures.Payload >= static_cast<int64_t>(PagesMeasuredFromBytes))
        {
            state.GpuPagesShare =
                std::max(1.0, static_cast<double>(pictures.Allocated) / static_cast<double>(pictures.Payload));
        }
    }

    /// <summary>
    /// Port-only (the opening): <paramref name="mission"/>'s opening as sprite/MCOpeningLists.h lists it, else (a match's
    /// map, a mod's mission) the player's mechs' packets most missions draw first (MCOpeningLists::AnyMission).
    /// </summary>
    std::vector<MCHdArt::OpeningPacket> OpeningOf(std::string_view mission)
    {
        const auto found =
            std::ranges::find_if(MCOpeningLists::Missions, [mission](const MCOpeningLists::Mission& listed)
                                 { return MCIEquals(listed.Name, mission); });
        const std::span<const MCOpeningLists::Entry> entries =
            found != MCOpeningLists::Missions.end() ? found->Entries : std::span(MCOpeningLists::AnyMission);
        std::vector<MCHdArt::OpeningPacket> packets;
        packets.reserve(entries.size());

        for (const MCOpeningLists::Entry& entry : entries)
        {
            if (entry.Folder >= MCOpeningLists::Folders.size())
            {
                continue;
            }

            packets.push_back(MCHdArt::OpeningPacket{
                MCFileSystem::FromUtf8(MCOpeningLists::Folders[entry.Folder]),
                entry.Packet == MCOpeningLists::NoPacket ? std::nullopt : std::optional<uint16_t>(entry.Packet),
                entry.Place == MCOpeningLists::AnyPlace ? std::nullopt : std::optional<uint8_t>(entry.Place)});
        }

        return packets;
    }

    /// <summary>
    /// Port-only (the opening): its walk from the start, nothing of it asked: what it asked goes back to the fill's
    /// (GpuAsked), to be asked again (the fill's reads were dropped: DropGpuReads); a frame up already isn't read again.
    /// </summary>
    void RewindOpening(State& state)
    {
        for (const uint32_t index : state.OpeningAsked)
        {
            state.GpuAsked.erase(index);
        }

        state.OpeningAsked.clear();
        state.OpeningWalked = 0;
        state.OpeningWalkedBytes = 0;
        state.OpeningDone = false;
        state.OpeningAtRoom = false;
        state.OpeningNotedAt = std::numeric_limits<size_t>::max();
    }

    /// <summary>
    /// Port-only (the opening): the held mission's: <paramref name="packets"/> and its own mechs' archives, those of
    /// <paramref name="folders"/> (its list's), which tell the player's from them; the order built again.
    /// </summary>
    void SetMissionOpening(State& state, std::string_view mission, std::vector<MCHdArt::OpeningPacket> packets,
                           std::span<const std::filesystem::path> folders)
    {
        state.OpeningMission = std::string(mission);
        state.Opening = std::move(packets);
        state.OpeningMissionMechs.clear();

        for (const std::filesystem::path& folder : folders)
        {
            if (std::optional<std::string> archive = MCHdArt::MechPartArchive(folder.generic_string());
                archive.has_value())
            {
                state.OpeningMissionMechs.insert(std::move(*archive));
            }
        }

        ++state.OpeningGeneration;
    }

    /// <summary>
    /// Port-only (the opening): its folders of the mission's own (whole: no packet), whose archives are opened and held
    /// before the list's (MCHdArt::StartBriefingHold, MCHdArt::StartLoadFill), so it goes up first, and none it names
    /// that the list leaves out is opened on the game's thread.
    /// </summary>
    std::vector<std::filesystem::path> OpeningFolders(const State& state)
    {
        std::vector<std::filesystem::path> folders;

        for (const MCHdArt::OpeningPacket& packet : state.Opening)
        {
            if (!packet.Packet.has_value())
            {
                folders.push_back(packet.Folder);
            }
        }

        return folders;
    }

    /// <summary>Port-only (MCHdArt::LetGoHold, a pack set): no opening, nothing of it asked or counted.</summary>
    void ClearOpening(State& state)
    {
        RewindOpening(state);
        state.Opening.clear();
        state.OpeningMission.clear();
        state.OpeningMissionMechs.clear();
        state.OpeningLancePlaced.clear();
        state.OpeningLanceLoaded.clear();
        state.OpeningLoadScanned = 0;
        state.OpeningPlacedAt = std::numeric_limits<uint64_t>::max();
        state.OpeningOrder.clear();
        state.OpeningFolderBytes.clear();
        ++state.OpeningGeneration;
    }

    /// <summary>
    /// Port-only (the opening): the player's mechs' part archives brought up to date: those placed, in the order placed
    /// (as of PlacedGeneration), then those the load handed that aren't the mission's (LoadFolders); a change has the
    /// order built again (BuildOpening).
    /// </summary>
    void UpdateOpeningLance(State& state)
    {
        bool changed = false;
        const auto addTo = [&state](std::vector<std::string>& lance, std::string_view folder)
        {
            std::optional<std::string> archive = MCHdArt::MechPartArchive(folder);

            if (!archive.has_value() || state.OpeningMissionMechs.contains(*archive) ||
                std::ranges::contains(state.OpeningLancePlaced, *archive) ||
                std::ranges::contains(state.OpeningLanceLoaded, *archive))
            {
                return false;
            }

            lance.push_back(std::move(*archive));
            return true;
        };

        if (state.OpeningPlacedAt != state.PlacedGeneration)
        {
            state.OpeningPlacedAt = state.PlacedGeneration;
            std::vector<std::string> placed;
            std::swap(placed, state.OpeningLancePlaced);

            for (const std::filesystem::path& folder : state.ManifestPlayers)
            {
                const std::string key = folder.generic_string();

                if (const auto found = state.PlacedFolders.find(key);
                    found != state.PlacedFolders.end() && found->second.Mechs > 0)
                {
                    addTo(state.OpeningLancePlaced, key);
                }
            }

            changed = changed || placed != state.OpeningLancePlaced;
        }

        for (; state.OpeningLoadScanned < state.LoadFolders.size(); ++state.OpeningLoadScanned)
        {
            changed = addTo(state.OpeningLanceLoaded, state.LoadFolders[state.OpeningLoadScanned].generic_string()) ||
                      changed;
        }

        if (changed)
        {
            state.OpeningBuiltAt = std::numeric_limits<uint64_t>::max();
        }
    }

    /// <summary>
    /// Port-only (the opening): the order it goes up in, built again when the opening or the player's mechs changed
    /// (UpdateOpeningLance): each packet in turn, a folder of the mission's whole, a packet of the player's mechs' as
    /// that of each mech whose part is that PAK's and whose place among them (MCHdArt::OpeningLance's order), counted
    /// round the most places the list's play had of it, is the packet's (every one's, with no place), each folder once;
    /// the walk then from the start (what it asked stays asked).
    /// </summary>
    void BuildOpening(State& state)
    {
        UpdateOpeningLance(state);

        if (state.OpeningBuiltAt == state.OpeningGeneration)
        {
            return;
        }

        state.OpeningBuiltAt = state.OpeningGeneration;
        state.OpeningOrder.clear();
        std::unordered_set<std::string> seen;
        const auto add = [&state, &seen](std::string folder)
        {
            if (seen.insert(folder).second)
            {
                state.OpeningOrder.push_back(MCFileSystem::FromUtf8(folder));
            }
        };

        // Each PAK's places in the list's play: the most its packets name, plus one.
        std::unordered_map<std::string, uint32_t> places;

        for (const MCHdArt::OpeningPacket& packet : state.Opening)
        {
            if (packet.Packet.has_value() && packet.Place.has_value())
            {
                uint32_t& most = places[packet.Folder.generic_string()];
                most = std::max<uint32_t>(most, uint32_t{*packet.Place} + 1);
            }
        }

        for (const MCHdArt::OpeningPacket& packet : state.Opening)
        {
            std::string folder = packet.Folder.generic_string();

            if (!packet.Packet.has_value())
            {
                add(std::move(folder));
                continue;
            }

            const uint32_t most = packet.Place.has_value() ? places.at(folder) : 0;
            uint32_t place = 0;

            for (const std::vector<std::string>* lance : {&state.OpeningLancePlaced, &state.OpeningLanceLoaded})
            {
                for (const std::string& archive : *lance)
                {
                    if (!Under(archive, folder))
                    {
                        continue;
                    }

                    if (!packet.Place.has_value() || place % most == *packet.Place)
                    {
                        add(std::format("{}/{}", archive, *packet.Packet));
                    }

                    ++place;
                }
            }
        }

        state.OpeningWalked = 0;
        state.OpeningWalkedBytes = 0;
        state.OpeningDone = false;
        state.OpeningAtRoom = false;
        state.OpeningNotedAt = std::numeric_limits<size_t>::max();
    }

    /// <summary>
    /// Port-only (the opening): the bytes the frames of the folder <paramref name="index"/> take unpacked, as an ask of it
    /// whole reads them (each frame's entries: ForEachStep, with its turn middles), counted once
    /// (State::OpeningFolderBytes). Its archive found already (the fill's look).
    /// </summary>
    uint64_t FolderGpuBytes(State& state, uint32_t index)
    {
        if (const auto counted = state.OpeningFolderBytes.find(index); counted != state.OpeningFolderBytes.end())
        {
            return counted->second;
        }

        FrameFolder& folder = state.Folders.at(index);
        const int32_t frames = ArchivedFrames(folder);
        const FrameFolder::InArchive& archived = ArchiveOf(folder);
        uint64_t bytes = 0;

        if (archived.Archive != nullptr)
        {
            const MCFastFile& file = *archived.Archive->File;

            for (int32_t frame = 0; frame < frames; ++frame)
            {
                ForEachStep(frame, true,
                            [&](uint64_t name)
                            {
                                const std::optional<int32_t> entry =
                                    file.Find(archived.Prefix + NumberedName(name) + std::string(HdfExtension));

                                if (entry.has_value())
                                {
                                    bytes += static_cast<uint64_t>(std::max<int64_t>(file.EntryAt(*entry).RealSize, 0));
                                }

                                return entry.has_value();
                            });
            }
        }

        state.OpeningFolderBytes.emplace(index, bytes);
        return bytes;
    }

    /// <summary>
    /// Port-only (the opening): the most of it on the GPU where the whole mission doesn't fit, at its pages' share: a
    /// quarter of <paramref name="room"/> (the pictures' room) up to MCHdArt::OpeningMostBytes.
    /// </summary>
    uint64_t OpeningRoom(uint64_t room)
    {
        return std::min(MCHdArt::OpeningMostBytes, room / MCHdArt::OpeningRoomShare);
    }

    /// <summary>
    /// Port-only (MCHdArt::FillGpu; SPEC-memory.md's plan, item 2): whether the whole mission fits on the GPU now (the
    /// frames of every archive handed the hold unpacked, at what their pages take: PagesShare; with the hold beside them on
    /// shared memory), the mode entered with a tenth to spare, never while archives are being opened for the hold (the
    /// mission's size isn't known whole until they're open: its list's, from the briefing or the load's start) nor before
    /// the load's ground is in the card's room (MCHdArt::GroundOnGpu), and left only once it no longer fits (its
    /// reads waiting dropped then: the pictures up stay as the plan's room lets them); while it fits, the folders handed (FillGpu's, then the load's) asked for in order, each once its
    /// archive is held whole (its frames unpacked from memory, not read from the disk again), at once when the hold
    /// keeps no more or doesn't take it. In play no archive is opened for it. The game's thread's (MCHdArt::NextFrame,
    /// MCHdArt::FinishLoadFill).
    /// </summary>
    void UpdateGpuFill(State& state)
    {
        ReadAhead& ahead = state.Ahead;
        const std::filesystem::path pack = MCHdArt::PackFolder();
        uint64_t mission = 0;
        uint64_t handed = 0;
        uint64_t ground = 0;
        bool loading = false;
        bool opening = false;
        bool fillDone = false;

        {
            std::scoped_lock lock(ahead.Lock);
            MeasurePagesShare(state);

            if (!state.GpuFillOn || ahead.Slots == nullptr)
            {
                state.GpuFits = false;
                return;
            }

            handed = ahead.Holding.QueuedBytes;
            loading = ahead.Holding.Loading;
            opening = std::ranges::any_of(ahead.Opening, [&ahead](const auto& open)
                                          { return open.second == ahead.Holding.Generation; });
            // A mech taken off in the mech bay is the mission's no more (MCHdArt::MechTakenOff).
            mission = Less(ahead.Holding.HandedGpuBytes, TakenOffBytes(state, pack));
            ground = ahead.Holding.GroundGpuBytes;
            fillDone = GpuFillDone(state);
        }

        state.GpuMissionBytes = mission;

        // Not decided on part of the mission (one that doesn't fit whole puts only its opening up: below), nor under the
        // load before the room holds its ground. From the briefing the ground is counted ahead (below).
        const bool decide = state.GpuFits ||
                            !(opening || !state.GpuMissionKnown || (loading && state.GpuGround != GroundState::InRoom));

        // The mission's frames at what their pages take. A card's room counts the pictures up at what the card holds for
        // them (MCMemoryRoom::CardRoom), so there only the frames still to go up are counted at the pages' share; the
        // art's room on shared memory leaves the pictures out, so there all of them are.
        const MCMemoryLedger::MCFigures pictures = MCMemoryLedger::Of(MCMemoryLedger::MCHolder::GpuHdPictures);
        const auto picturesUp = static_cast<uint64_t>(std::max<int64_t>(pictures.Payload, 0));
        const double share = PagesShare(state);
        const auto atShare = [share](uint64_t bytes)
        { return static_cast<uint64_t>(static_cast<double>(bytes) * share); };
        // A mission up whole once the load's fill is done: nothing more of it goes up, so what's up is all of it (the
        // estimate counts bytes of its archives the GPU never takes: on the A10 a whole mission's ended 66-175 MB over
        // what went up, and two were counted out in play). Entering counts it all.
        const uint64_t up = std::min(mission, picturesUp);
        // Not a briefing's (SPEC-memory.md's "The GPU from the briefing"): its ground and its load are still to come.
        const bool done = state.GpuFits && !loading && state.GpuGround == GroundState::InRoom && fillDone;
        const uint64_t onCard = done ? up : up + atShare(mission - up);
        const uint64_t inPages = done ? atShare(up) : atShare(mission);
        // Port-only (SPEC-memory.md's "The GPU from the briefing"): the ground before the room holds it (the briefing's,
        // and a load's until it's up), as its pieces held take unpacked, at the pages' share (est.).
        const uint64_t groundAhead = state.GpuGround == GroundState::InRoom ? 0 : atShare(ground);

        // On shared memory the mission and the hold beside it within the art's room past the rest and the floor; on any
        // GPU the mission within the room the renderer's card gives its pictures (MCHdArt::NoteGpuRoom).
        const ArtParts parts = ArtPartsNow();
        constexpr uint64_t Unbounded = std::numeric_limits<uint64_t>::max();
        const uint64_t space =
            parts.Room.Shared ? Less(parts.Room.Bytes, parts.Rest + parts.Floor + groundAhead) : Unbounded;
        // The card's room as last read, never more than what the ledger has on it now: a read before the load's ground
        // was made left it out (the A10: 2478 MB of room at every decision, about 450 MB less at the next read).
        const uint64_t cardRoom =
            std::min(state.GpuCardRoom.value_or(Unbounded),
                     MCMemoryRoom::CardRoomNow(static_cast<uint64_t>(std::max<int64_t>(pictures.Allocated, 0)))
                         .value_or(Unbounded));
        const uint64_t card = cardRoom == Unbounded ? Unbounded : Less(cardRoom, groundAhead);
        const uint64_t held = parts.Room.Shared ? std::max<uint64_t>(parts.Held, handed) : 0;
        const auto within = [&state](uint64_t need, uint64_t room)
        { return state.GpuFits ? need <= room : need + need / FitsMarginDivisor <= room; };

        if (decide)
        {
            const bool fits = mission > 0 && within(inPages + held, space) && within(onCard, card);

            if (fits != state.GpuFits && MCFrameLog::Enabled())
            {
                MCFrameLog::Note(std::format(
                    "HD fill on the GPU: {} ({:.1f} MB for the mission, {:.1f} MB as its pages take it at {:.2f} times "
                    "its bytes, {:.1f} MB held, {:.1f} MB for the ground ahead, {:.1f} MB of room{})",
                    fits ? "the whole mission fits" : "the mission no longer fits",
                    static_cast<double>(mission) / BytesPerMb,
                    static_cast<double>(parts.Room.Shared ? inPages : onCard) / BytesPerMb, share,
                    static_cast<double>(held) / BytesPerMb, static_cast<double>(groundAhead) / BytesPerMb,
                    static_cast<double>(std::min(space - std::min(space, held), card)) / BytesPerMb,
                    state.BriefingFill ? ", from the briefing" : ""));
            }

            // Left, its reads dropped; the opening then walked again within its room, its folders asked again whoever asked
            // them (what's up stays, and isn't read again).
            if (!fits && state.GpuFits)
            {
                {
                    std::scoped_lock lock(ahead.Lock);
                    DropGpuReads(ahead);
                }

                state.GpuAsked.clear();
                RewindOpening(state);
            }

            state.GpuFits = fits;
        }

        enum class Next : uint8_t
        {
            Ask,
            Wait,
            Skip,
        };

        const auto look = [&](const std::filesystem::path& folder, uint32_t& index)
        {
            index = FolderIndex(pack / folder, Naming::Number);
            FrameFolder& frames = state.Folders.at(index);

            // A mech's taken off since waits until it's placed again (MCHdArt::MechTakenOff), noted again then.
            if (state.GpuAsked.contains(index) || TakenOff(state, folder))
            {
                return Next::Skip;
            }

            // In play no archive is opened for it. From the briefing (SPEC-memory.md's "The GPU from the briefing") its
            // folders' archives are opened ahead: one a reader opens still is waited for, never opened here.
            if (!frames.Archived.has_value() && !loading)
            {
                if (!state.BriefingFill)
                {
                    return Next::Skip;
                }

                std::scoped_lock lock(ahead.Lock);

                if (OpensFor(ahead, frames.Path, pack))
                {
                    return Next::Wait;
                }
            }

            const std::shared_ptr<MCHdArchive> archive = ArchiveOf(frames).Archive;

            if (archive == nullptr)
            {
                return Next::Skip;
            }

            std::scoped_lock lock(ahead.Lock);
            const HoldQueue& holding = ahead.Holding;
            const bool held =
                !holding.On || holding.Full || !holding.Queued.contains(archive) ||
                std::ranges::find(holding.Archives, archive, &HoldItem::Archive) == holding.Archives.end();
            return held ? Next::Ask : Next::Wait;
        };

        std::vector<MCHdArt::PacketAhead> packets;
        std::vector<std::filesystem::path> typePackets;
        // Each in its order from where it was left (packetAt: its packet at an index), up to the first whose archive the
        // hold reads still, or outside a load to the frame's share (FillWalkMostPerFrame; one folder at least). With
        // types, a folder naming a type (TypePackets) brings its archive's other packets after.
        const auto walkUntil = std::chrono::steady_clock::now() + FillWalkMostPerFrame;
        bool walked = false;
        const auto walk = [&](size_t count, size_t& asked, const auto& packetAt, bool types)
        {
            for (; asked < count; ++asked)
            {
                if (walked && !loading && std::chrono::steady_clock::now() >= walkUntil)
                {
                    return false;
                }

                walked = true;
                const MCHdArt::PacketAhead packet = packetAt(asked);
                uint32_t index = 0;
                const Next next = look(packet.Folder, index);

                if (next == Next::Wait)
                {
                    return false;
                }

                if (types)
                {
                    std::ranges::move(TypePackets(state.Folders.at(index), packet.Folder),
                                      std::back_inserter(typePackets));
                }

                if (next == Next::Ask && state.GpuAsked.insert(index).second)
                {
                    packets.push_back(packet);
                }
            }

            return true;
        };

        // A list of folders, each one's frames whole.
        const auto folderAt = [](std::span<const std::filesystem::path> folders)
        { return [folders](size_t at) { return MCHdArt::PacketAhead{folders[at], std::nullopt, true, 0}; }; };

        // Port-only (SPEC-memory.md's "The GPU from the briefing": the opening, on every card): its folders first, in its
        // order from where it was left, each one's bytes counted as walked, those asked before too, until they'd pass its
        // room at their pages' share (none where the mission fits: all of it goes up), each asked for once; up to the first
        // whose archive the hold reads still, or outside a load to the frame's share. Stopped at its room, it looks again
        // each frame: a room grown (the mission fitting) takes it further.
        const auto walkOpening = [&](uint64_t room)
        {
            state.OpeningAtRoom = false;

            // No pack, no opening to put up. Built again first: a change since it was done walks it again.
            if (pack.empty())
            {
                state.OpeningDone = true;
                return;
            }

            BuildOpening(state);

            if (state.OpeningDone)
            {
                return;
            }

            // Where its walk stops (its end, or its room), noted once there.
            const auto note = [&state, room]
            {
                if (std::exchange(state.OpeningNotedAt, state.OpeningWalked) != state.OpeningWalked &&
                    MCFrameLog::Enabled() && !state.OpeningOrder.empty())
                {
                    MCFrameLog::Note(std::format(
                        "HD fill on the GPU: the opening, {} of its {} folders walked, {:.1f} MB, {} asked ({})",
                        state.OpeningWalked, state.OpeningOrder.size(),
                        static_cast<double>(state.OpeningWalkedBytes) / BytesPerMb, state.OpeningAsked.size(),
                        room == Unbounded ? std::string("the whole mission fits")
                                          : std::format("{:.1f} MB of room", static_cast<double>(room) / BytesPerMb)));
                }
            };

            for (; state.OpeningWalked < state.OpeningOrder.size(); ++state.OpeningWalked)
            {
                if (walked && !loading && std::chrono::steady_clock::now() >= walkUntil)
                {
                    return;
                }

                walked = true;
                const std::filesystem::path& folder = state.OpeningOrder[state.OpeningWalked];
                uint32_t index = FolderIndex(pack / folder, Naming::Number);
                const bool before = state.OpeningAsked.contains(index) || state.GpuAsked.contains(index);
                const Next next = before ? Next::Skip : look(folder, index);

                // An archive the hold reads still waits; one not there, or a mech's taken off, isn't counted.
                if (next == Next::Wait)
                {
                    return;
                }

                if (!before && next == Next::Skip)
                {
                    continue;
                }

                const uint64_t bytes = FolderGpuBytes(state, index);

                if (room != Unbounded && atShare(state.OpeningWalkedBytes + bytes) > room)
                {
                    state.OpeningAtRoom = true;
                    note();
                    return;
                }

                state.OpeningWalkedBytes += bytes;

                if (next == Next::Ask)
                {
                    state.GpuAsked.insert(index);
                    state.OpeningAsked.insert(index);
                    packets.push_back(MCHdArt::PacketAhead{folder, std::nullopt, true, 0});
                }
            }

            state.OpeningDone = true;
            note();
        };

        walkOpening(state.GpuFits ? Unbounded : OpeningRoom(std::min(space, card)));

        // FillGpu's order, then the briefing's folders (SPEC-memory.md's "The GPU from the briefing"), then the load's:
        // the whole mission's, the opening's asked before them each frame (one whose archive is held later doesn't hold
        // them back).
        const auto orderAt = [&state](size_t at) { return state.GpuOrder[at]; };

        if (state.GpuFits && walk(state.GpuOrder.size(), state.GpuOrderAsked, orderAt, false) &&
            walk(state.BriefingFillFolders.size(), state.BriefingFillAsked, folderAt(state.BriefingFillFolders), true))
        {
            walk(state.LoadFolders.size(), state.LoadFoldersAsked, folderAt(state.LoadFolders), false);
        }

        // Walked from the next frame on: the walk's list stays as it was while walked.
        NoteBriefingFillFolders(state, typePackets);

        if (!packets.empty())
        {
            AskAhead(PacketAsks(packets), AheadKind::Gpu, std::numeric_limits<size_t>::max());
        }
    }
}

void MCHdArt::StartLoadFill(std::string_view mission)
{
    if (!Active() || !HoldWanted())
    {
        return;
    }

    // Port-only (MCHdArt::FillGpu): a load no briefing held for opens its mission's list now, as the briefing would have.
    const bool briefed = mission.empty() || HoldsFor(mission);
    State& state = TheState();
    state.LoadFolders.clear();
    state.LoadFolderKeys.clear();
    state.LoadFoldersAsked = 0;

    // Port-only (the opening): the player's mechs the load hands looked for again in this load's folders, its order built
    // again if it had any from the last.
    if (!state.OpeningLanceLoaded.empty())
    {
        state.OpeningLanceLoaded.clear();
        state.OpeningBuiltAt = std::numeric_limits<uint64_t>::max();
    }

    state.OpeningLoadScanned = 0;
    // Port-only (SPEC-memory.md's "The GPU from the briefing"): a fill begun at the briefing goes on, what it put up
    // counted from there.
    const bool fromBriefing = state.BriefingFill;
    state.BriefingFill = false;
    state.GpuFillOn = true;
    state.GpuMissionKnown = briefed;
    state.GpuGround = GroundState::Waiting;

    if (!fromBriefing)
    {
        state.GpuUpAtLoad = state.GpuUp;
        state.GpuPagesMeasured = false;
    }

    // Every player's mechs are in once the load begins (MCHdArt::Manifest).
    state.ManifestFinal = true;
    ++state.ManifestGeneration;

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);
        HoldQueue& holding = ahead.Holding;

        // A hold already on (its briefing's, MCHdArt::StartBriefingHold; a load begun without the last mission's let go)
        // holds what it holds and takes this load's too, its reads now as many at once as the readers are free.
        BeginHolding(holding);
        holding.Loading = true;
        holding.Briefing = false;
        holding.BarShown = 0;

        if (!fromBriefing)
        {
            state.GpuReadsAtLoad = ahead.GpuReads;
        }

        ahead.Changed.notify_all();
    }

    // Before the load's folders are noted (MCHdArt::LoadFolders, which the lists are worked out from). Port-only (the
    // opening): its list's too, as a briefing's would be (a saved game, a load no briefing held for).
    if (!briefed)
    {
        const std::vector<std::filesystem::path> folders = BriefingFolders(mission);

        if (!mission.empty() && !MCIEquals(state.OpeningMission, mission))
        {
            SetMissionOpening(state, mission, OpeningOf(mission), folders);
        }

        OpenArchivesAhead(OpeningFolders(state));
        OpenArchivesAhead(folders);
    }

    state.RecordingLoad = true;
}

void MCHdArt::FinishLoadFill(const std::function<void()>& shown, bool waitForGpu)
{
    State& state = TheState();
    state.RecordingLoad = false;
    // Port-only (SPEC-memory.md's "Under the loading bar"): when the whole mission fits on the GPU, the bar ends once it's
    // there too, and when it doesn't once its opening is, unless none of it went up for GpuFillStall. The load has handed
    // the hold all it will, and its ground has gone up (MCHdArt::GroundOnGpu) if it has one.
    state.GpuMissionKnown = true;
    state.GpuGround = GroundState::InRoom;
    UpdateGpuFill(state);
    state.GpuUpAt = std::chrono::steady_clock::now();
    ReadAhead& ahead = state.Ahead;
    std::unique_lock lock(ahead.Lock);
    const auto done = [&]
    {
        return !HoldPending(ahead) && ahead.InFlight[std::to_underlying(AheadKind::Hold)] == 0 &&
               (!waitForGpu || (state.GpuFits ? GpuFillDone(state) : OpeningUp(state)) ||
                std::chrono::steady_clock::now() - state.GpuUpAt > GpuFillStall);
    };

    while (ahead.Holding.Loading && !ahead.Changed.wait_for(lock, LoadFillShowEvery, done))
    {
        lock.unlock();
        // The fill's folders asked for as the hold has them (each frame does too: MCHdArt::NextFrame).
        UpdateGpuFill(state);
        shown();
        lock.lock();
    }

    if (ahead.Holding.Loading && state.GpuFits && MCFrameLog::Enabled())
    {
        MCFrameLog::Note(std::format("HD fill on the GPU: the load's end, {} of {} frames up ({:.1f} MB), {:.1f} MB "
                                     "for the mission (est.){}",
                                     state.GpuUp - state.GpuUpAtLoad, ahead.GpuReads - state.GpuReadsAtLoad,
                                     static_cast<double>(state.GpuUpBytes) / BytesPerMb,
                                     static_cast<double>(state.GpuMissionBytes) / BytesPerMb,
                                     GpuFillDone(state) ? "" : ", stalled"));
    }

    if (ahead.Holding.Loading && MCFrameLog::Enabled())
    {
        MCFrameLog::Note(
            std::format("HD fill: the load's end, {:.1f} MB held of {:.1f} MB handed, {:.2f} s after the load began",
                        static_cast<double>(MCFastFile::HeldBytes()) / BytesPerMb,
                        static_cast<double>(ahead.Holding.QueuedBytes) / BytesPerMb,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - ahead.Holding.Began).count()));
    }

    // Port-only (SPEC-memory.md's "The GPU from the briefing", a match): what's left asked for while the load's archives
    // are its to look up, all held now, or where the mission doesn't fit its opening; in play none is opened for it
    // (UpdateGpuFill).
    if (!waitForGpu && ahead.Holding.Loading &&
        (state.GpuFits ? !GpuFillDone(state) : !(state.OpeningDone || state.OpeningAtRoom)))
    {
        lock.unlock();
        UpdateGpuFill(state);
        lock.lock();
    }

    const bool filled = ahead.Holding.Loading;
    ahead.Holding.Loading = false;
    lock.unlock();

    // The bar shown whole once the fill is done: its last showing while it waited fell short of it.
    if (filled)
    {
        shown();
    }
}

void MCHdArt::LoadUnitsMade()
{
    TheState().GpuMissionKnown = true;
}

void MCHdArt::GroundOnGpu()
{
    State& state = TheState();

    if (state.GpuGround == GroundState::Waiting)
    {
        state.GpuGround = GroundState::ReadingCard;
        // The card read again for the next frame's room (MCMemoryRoom's reads are CardReadInterval apart).
        MCMemoryRoom::AskAgain();
    }
}

double MCHdArt::LoadFillShare()
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return FillShareOf(ahead.Holding);
}

int32_t MCHdArt::LoadBarPercent(int32_t progress)
{
    constexpr int32_t Whole = 100;
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    HoldQueue& holding = ahead.Holding;

    if (!holding.Loading)
    {
        return progress;
    }

    const int32_t steps = std::clamp(progress, 0, Whole) * (Whole - LoadFillBarPercent) / Whole;
    // Port-only (MCHdArt::FillGpu): with the whole mission going on the GPU, or its opening, half the stretch is its
    // frames going up.
    const State& state = TheState();
    const uint64_t asked = ahead.GpuReads - state.GpuReadsAtLoad;
    const double gpu =
        asked == 0 ? 0.0
                   : std::min(1.0, static_cast<double>(state.GpuUp - state.GpuUpAtLoad) / static_cast<double>(asked));
    constexpr double GpuFillParts = 2.0;
    const double share =
        state.GpuFits || asked > 0 ? (FillShareOf(holding) + gpu) / GpuFillParts : FillShareOf(holding);
    const int32_t percent = steps + static_cast<int32_t>(share * LoadFillBarPercent);
    holding.BarShown = std::max(holding.BarShown, percent);
    return holding.BarShown;
}

void MCHdArt::LetGoHold()
{
    LetGoKeptForMission();
    State& state = TheState();
    ReadAhead& ahead = state.Ahead;

    // Port-only (MCHdArt::FillGpu): the mission's fill on the GPU ends with it, the briefing's too.
    state.GpuFillOn = false;
    state.GpuFits = false;
    state.GpuGround = GroundState::InRoom;
    state.BriefingFill = false;
    state.BriefingFillFolders.clear();
    state.BriefingFillKeys.clear();
    state.BriefingFillAsked = 0;
    state.GpuOrder.clear();
    state.GpuOrderAsked = 0;
    state.LoadFoldersAsked = 0;
    state.GpuAsked.clear();
    state.DrawnStaged.clear();

    {
        std::scoped_lock lock(ahead.Lock);
        DropGpuReads(ahead);
    }

    // The manifest goes with the hold (MCHdArt::Manifest), the mechs placed with it.
    state.ManifestPlayers.clear();
    state.ManifestPlayerKeys.clear();
    state.ManifestFolders.clear();
    state.PlacedFolders.clear();
    ++state.PlacedGeneration;
    state.ManifestFinal = false;
    ++state.ManifestGeneration;
    // Port-only (the opening): it goes with the hold too.
    ClearOpening(state);

    {
        std::scoped_lock lock(ahead.Lock);

        if (!ahead.Holding.On && ahead.Holding.Queued.empty() && MCFastFile::HeldBytes() == 0)
        {
            return;
        }

        StopHolding(ahead);
    }

    // Let go of without the lock (a couple of GB at most): nothing is held once the hold stopped under it.
    const size_t held = MCFastFile::HeldBytes();
    MCFastFile::LetGoHeld();

    if (held > 0 && MCFrameLog::Enabled())
    {
        MCFrameLog::Note(std::format("HD hold: let go of {:.1f} MB", static_cast<double>(held) / BytesPerMb));
    }
}

size_t MCHdArt::HeldBytes()
{
    return MCFastFile::HeldBytes();
}

void MCHdArt::SetBriefingHold(bool on)
{
    TheState().BriefingHoldOff = !on;
}

bool MCHdArt::BriefingHoldWanted()
{
    return HoldWanted() && !TheState().BriefingHoldOff;
}

void MCHdArt::SetBriefingFill(bool on)
{
    TheState().BriefingFillOff = !on;
}

bool MCHdArt::BriefingFillWanted()
{
    return BriefingHoldWanted() && !TheState().BriefingFillOff;
}

bool MCHdArt::BriefingFilling()
{
    const State& state = TheState();
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return state.BriefingFill && state.GpuFits && !ahead.Holding.Loading;
}

namespace
{
    /// <summary>
    /// Port-only (SPEC-memory.md's ground): the ground's frame a list's line names (a hold list's
    /// <c>terrain/&lt;hash&gt;</c> or <c>overlays/&lt;hash&gt;</c>: NoteLoadGround), its archive's folder and its key;
    /// none for any other folder.
    /// </summary>
    std::optional<std::pair<std::string_view, uint64_t>> GroundNameOf(const std::filesystem::path& folder)
    {
        constexpr size_t GroundNameParts = 2;
        const std::vector<std::filesystem::path> parts(folder.begin(), folder.end());

        if (parts.size() != GroundNameParts)
        {
            return std::nullopt;
        }

        const std::string kind = parts[0].generic_string();
        const std::optional<uint64_t> key = HashOfName(parts[1].generic_string());

        if ((kind != MCHdArt::TerrainFolder && kind != MCHdArt::OverlayFolder) || !key.has_value())
        {
            return std::nullopt;
        }

        return std::pair{kind == MCHdArt::TerrainFolder ? MCHdArt::TerrainFolder : MCHdArt::OverlayFolder, *key};
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's ground): has the hold read the ground's frames <paramref name="names"/> (keys by the
    /// folder of their archive: terrain.hda's, relief.hda's, overlays.hda's), the pieces of those archives that hold
    /// them and no others: at once for an archive open already, else as the readers open it (QueueHold).
    /// </summary>
    void HoldGround(const std::map<std::string_view, std::vector<uint64_t>>& names)
    {
        const std::filesystem::path pack = MCHdArt::PackFolder();
        State& state = TheState();
        std::vector<std::filesystem::path> toOpen;

        {
            ReadAhead& ahead = state.Ahead;
            std::scoped_lock lock(ahead.Lock);

            for (const auto& [folder, keys] : names)
            {
                const std::filesystem::path at = pack / folder;

                if (const auto known = state.Archives.find(KeyOfPath(at)); known != state.Archives.end())
                {
                    if (known->second != nullptr)
                    {
                        QueueHoldPieces(ahead, known->second, keys);
                    }

                    continue;
                }

                std::vector<uint64_t>& asked =
                    ahead.Holding.GroundAsked[KeyOfPath(std::filesystem::path(at).concat(ArchiveExtension))];
                asked.insert(asked.end(), keys.begin(), keys.end());
                toOpen.emplace_back(folder);
            }
        }

        MCHdArt::OpenArchivesAhead(toOpen);
    }
}

void MCHdArt::StartBriefingHold(std::string_view mission, std::span<const std::filesystem::path> folders, bool relief)
{
    if (!Active() || !BriefingHoldWanted() || mission.empty())
    {
        return;
    }

    // Another mission's (a save loaded at a briefing, the match changed) let go of first, as its load would.
    if (!HoldsFor(mission))
    {
        LetGoHold();
    }

    // Port-only (SPEC-memory.md's "The GPU from the briefing"): the fill on the GPU from here, not under a load (its own
    // fill runs then).
    State& state = TheState();
    const bool fillWanted = BriefingFillWanted();
    bool fill = false;

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);
        HoldQueue& holding = ahead.Holding;
        BeginHolding(holding);
        holding.Mission = mission;
        holding.Briefing = !holding.Loading;
        fill = holding.Briefing && fillWanted;

        if (fill && !state.BriefingFill)
        {
            state.GpuReadsAtLoad = ahead.GpuReads;
        }
    }

    // The manifest's mission part (MCHdArt::Manifest): the players' kept for the same mission, gone with another's.
    ++state.ManifestGeneration;
    state.ManifestFolders.clear();

    for (const std::filesystem::path& folder : folders)
    {
        state.ManifestFolders.push_back(folder.generic_string());
    }

    // Port-only (SPEC-memory.md's "The GPU from the briefing": the opening): its list's, kept while the mission's held.
    if (!MCIEquals(state.OpeningMission, mission))
    {
        SetMissionOpening(state, mission, OpeningOf(mission), folders);
    }

    // Port-only (SPEC-memory.md's ground): the ground's frames by name, only their pieces of the ground's archives, and
    // with relief maps each tile's beside it; the other folders' archives whole.
    std::map<std::string_view, std::vector<uint64_t>> ground;
    std::vector<std::filesystem::path> archives;

    for (const std::filesystem::path& folder : folders)
    {
        if (const auto name = GroundNameOf(folder); name.has_value())
        {
            ground[name->first].push_back(name->second);

            if (relief && name->first == TerrainFolder)
            {
                ground[ReliefFolder].push_back(name->second);
            }
        }
        else
        {
            archives.push_back(folder);
        }
    }

    // Port-only (SPEC-memory.md's "The GPU from the briefing"): the mission goes up at play's pace (one reader, the
    // renderer's share of each frame) while it fits, its archives once held, its ground counted ahead until the load puts
    // it up; each mech's after it as placed (MCHdArt::HoldMechAhead).
    if (fill)
    {
        if (!state.BriefingFill)
        {
            state.GpuUpAtLoad = state.GpuUp;
            state.GpuPagesMeasured = false;
        }

        NoteBriefingFillFolders(state, archives);
        state.BriefingFill = true;
        state.GpuFillOn = true;
        state.GpuMissionKnown = true;
        state.GpuGround = GroundState::Waiting;
    }

    // Their archives held as the readers open them (QueueHold), or at once when open already: the opening's first.
    OpenArchivesAhead(OpeningFolders(state));
    OpenArchivesAhead(archives);
    HoldGround(ground);
}

bool MCHdArt::HoldsFor(std::string_view mission)
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    const HoldQueue& holding = ahead.Holding;
    return holding.On && !holding.Mission.empty() && MCIEquals(holding.Mission, mission);
}

std::vector<std::filesystem::path> MCHdArt::LoadFolders()
{
    return TheState().LoadFolders;
}

namespace
{
    /// <summary>
    /// Port-only (sprite/MCHoldLists.h, SPEC-memory.md's ground by name): adds to <paramref name="folders"/> the ground's
    /// frames <paramref name="listed"/>'s load asked for, as a learned list names them (<c>terrain/&lt;hash&gt;</c>,
    /// <c>overlays/&lt;hash&gt;</c>: NoteLoadGround), from the slots of its tile set the table marks; none when the set
    /// isn't among those in use (MCGroundNamesInUse), whose ground the load reads.
    /// </summary>
    void HoldListGround(const MCHoldLists::Mission& listed, std::vector<std::filesystem::path>& folders)
    {
        const std::span<const MCKnownGroundNames> sets = MCGroundNamesInUse();
        const auto set = std::ranges::find(sets, listed.GroundSet, &MCKnownGroundNames::FilesHash);

        if (listed.GroundSet == 0 || set == sets.end())
        {
            return;
        }

        constexpr int32_t BitsAWord = 64;
        const auto add = [&](std::span<const uint64_t> marked, std::string_view folder,
                             std::optional<MCGroundName> MCGroundSlotNames::* name)
        {
            for (int32_t slot = 0; slot < set->Slots && slot / BitsAWord < std::ssize(marked); ++slot)
            {
                if (((marked[static_cast<size_t>(slot / BitsAWord)] >> (slot % BitsAWord)) & 1) == 0)
                {
                    continue;
                }

                if (const std::optional<MCGroundName> named = set->At(slot).*name; named.has_value())
                {
                    folders.push_back(std::filesystem::path(folder) / HashName(named->Key));
                }
            }
        };

        add(listed.Tiles, MCHdArt::TerrainFolder, &MCGroundSlotNames::Tile);
        add(listed.Overlays, MCHdArt::OverlayFolder, &MCGroundSlotNames::Overlay);
    }

    /// <summary>Port-only (sprite/MCHoldLists.h): the pack's folders a list of the program's names.</summary>
    std::vector<std::filesystem::path> HoldListFolders(std::span<const uint16_t> indices)
    {
        std::vector<std::filesystem::path> folders;
        folders.reserve(indices.size());

        for (const uint16_t index : indices)
        {
            folders.push_back(MCFileSystem::FromUtf8(MCHoldLists::Folders[index]));
        }

        return folders;
    }
}

namespace
{
    /// <summary>
    /// Port-only (SPEC-memory.md's "The mission's manifest"): the user folder's folder of the hold lists the missions'
    /// loads learned (MCHdArt::KeepHoldList), a file each, named by the mission in lower case.
    /// </summary>
    constexpr std::string_view LearnedListsFolder = "hold-lists";
    constexpr std::string_view LearnedListSuffix = ".txt";
    /// <summary>The pieces of the game folder's files the load read (MCHdArt::KeepFilePieces), beside its list.</summary>
    constexpr std::string_view LearnedPiecesSuffix = ".files.txt";

    std::filesystem::path LearnedListPath(std::string_view mission, std::string_view suffix = LearnedListSuffix)
    {
        std::string name(mission);
        std::ranges::transform(name, name.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return MCFileSystem::UserRoot() / LearnedListsFolder / MCFileSystem::FromUtf8(name + std::string(suffix));
    }

    /// <summary>
    /// Writes <paramref name="text"/> as <paramref name="mission"/>'s learned <paramref name="what"/> at
    /// <paramref name="path"/>: written whole, then put in place, so a list is never read half written.
    /// </summary>
    void KeepLearned(std::string_view mission, std::string_view what, const std::filesystem::path& path,
                     std::string_view text)
    {
        std::filesystem::path written = path;
        written += ".new";
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);

        if (error)
        {
            SDL_Log("HD art: the %s of %s not kept: its folder %s couldn't be made (%s)", std::string(what).c_str(),
                    std::string(mission).c_str(), MCFileSystem::ToUtf8(path.parent_path()).c_str(),
                    error.message().c_str());
            return;
        }

        {
            std::ofstream out(written, std::ios::binary | std::ios::trunc);
            out << text;

            if (!out.flush())
            {
                SDL_Log("HD art: the %s of %s not kept: %s couldn't be written", std::string(what).c_str(),
                        std::string(mission).c_str(), MCFileSystem::ToUtf8(written).c_str());
                return;
            }
        }

        std::filesystem::rename(written, path, error);

        if (error)
        {
            SDL_Log("HD art: the %s of %s not kept: %s couldn't replace %s (%s)", std::string(what).c_str(),
                    std::string(mission).c_str(), MCFileSystem::ToUtf8(written).c_str(),
                    MCFileSystem::ToUtf8(path).c_str(), error.message().c_str());
        }
    }

    /// <summary>The list <paramref name="mission"/>'s last load learned, a folder a line; none when it has none.</summary>
    std::vector<std::filesystem::path> LearnedFolders(std::string_view mission)
    {
        std::vector<std::filesystem::path> folders;
        std::ifstream file(LearnedListPath(mission), std::ios::binary);

        for (std::string line; std::getline(file, line);)
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            // A list from another program's file (a user's edit) names only folders under the pack.
            if (const std::filesystem::path folder = MCFileSystem::FromUtf8(line);
                !line.empty() && folder.is_relative() && !line.contains(".."))
            {
                folders.push_back(folder);
            }
        }

        return folders;
    }
}

std::vector<std::filesystem::path> MCHdArt::BriefingFolders(std::string_view mission)
{
    // The mission's last load's list first: a mod's or another mission without a list in the program's table, or a
    // stock one a mod changed.
    if (std::vector<std::filesystem::path> learned = LearnedFolders(mission); !learned.empty())
    {
        return learned;
    }

    const auto found = std::ranges::find_if(MCHoldLists::Missions, [mission](const MCHoldLists::Mission& listed)
                                            { return MCIEquals(listed.Name, mission); });

    if (found == MCHoldLists::Missions.end())
    {
        return {};
    }

    std::vector<std::filesystem::path> folders = HoldListFolders(found->Folders);
    HoldListGround(*found, folders);
    return folders;
}

void MCHdArt::KeepHoldList(std::string_view mission, std::span<const std::string> folders)
{
    if (mission.empty() || folders.empty())
    {
        return;
    }

    std::string text;

    for (const std::string& folder : folders)
    {
        text += folder + '\n';
    }

    KeepLearned(mission, "hold list", LearnedListPath(mission), text);
}

std::vector<MCFilesAhead::MCFilePiece> MCHdArt::MissionFilePieces(std::string_view mission)
{
    // The mission's last load's first, as its list's (BriefingFolders).
    if (std::ifstream file(LearnedListPath(mission, LearnedPiecesSuffix), std::ios::binary); file)
    {
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

        if (std::vector<MCFilesAhead::MCFilePiece> learned = MCFilesAhead::PiecesOfText(text); !learned.empty())
        {
            return learned;
        }
    }

    const auto found = std::ranges::find_if(MCHoldLists::Missions, [mission](const MCHoldLists::Mission& listed)
                                            { return MCIEquals(listed.Name, mission); });
    std::vector<MCFilesAhead::MCFilePiece> pieces;

    if (found == MCHoldLists::Missions.end())
    {
        return pieces;
    }

    for (const MCHoldLists::Piece& piece : found->Pieces)
    {
        pieces.push_back(MCFilesAhead::MCFilePiece{
            .File = std::string(MCHoldLists::Files[piece.File]), .Offset = piece.Offset, .Bytes = piece.Bytes});
    }

    return pieces;
}

void MCHdArt::KeepFilePieces(std::string_view mission, const std::vector<MCFilesAhead::MCFilePiece>& pieces)
{
    if (!mission.empty() && !pieces.empty())
    {
        KeepLearned(mission, "files' pieces", LearnedListPath(mission, LearnedPiecesSuffix),
                    MCFilesAhead::PiecesText(pieces));
    }
}

void MCHdArt::HoldMechAhead(std::span<const std::filesystem::path> folders)
{
    if (!Active() || !HoldWanted())
    {
        return;
    }

    {
        ReadAhead& ahead = TheState().Ahead;
        std::scoped_lock lock(ahead.Lock);

        // Only while a briefing's mission is held (StartBriefingHold): a lance changed otherwise reads nothing.
        if (!ahead.Holding.On || ahead.Holding.Mission.empty())
        {
            return;
        }
    }

    // The manifest's players' part (MCHdArt::Manifest), each folder's mechs placed counted (MCHdArt::MechTakenOff).
    State& state = TheState();

    for (const std::filesystem::path& folder : folders)
    {
        std::string key = folder.generic_string();
        State::PlacedFolder& placed = state.PlacedFolders[key];
        placed.Folder = folder;
        ++placed.Mechs;

        if (state.ManifestPlayerKeys.insert(std::move(key)).second)
        {
            state.ManifestPlayers.push_back(folder);
            ++state.ManifestGeneration;
        }
    }

    ++state.PlacedGeneration;

    // Port-only (SPEC-memory.md's "The GPU from the briefing"): after the mission's, as placed.
    if (state.BriefingFill)
    {
        NoteBriefingFillFolders(state, folders);
    }

    OpenArchivesAhead(folders);
}

void MCHdArt::MechTakenOff(std::span<const std::filesystem::path> folders)
{
    State& state = TheState();

    for (const std::filesystem::path& folder : folders)
    {
        const std::string key = folder.generic_string();

        // Only a mech placed while a mission was held counts (HoldMechAhead).
        if (const auto placed = state.PlacedFolders.find(key);
            placed != state.PlacedFolders.end() && placed->second.Mechs > 0 && --placed->second.Mechs == 0)
        {
            // Placed again, it goes up after the mission's again (NoteBriefingFillFolders).
            state.BriefingFillKeys.erase(key);
            ++state.PlacedGeneration;
        }
    }
}

MCHdArt::MissionManifest MCHdArt::Manifest()
{
    State& state = TheState();
    MissionManifest manifest{.Generation = state.ManifestGeneration};
    // Each archive the hold took: whole (no pieces), or the pieces its list names.
    std::vector<std::pair<std::shared_ptr<MCHdArchive>, std::vector<uint32_t>>> taken;
    bool opening = false;

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);
        const HoldQueue& holding = ahead.Holding;

        if (!holding.On)
        {
            return manifest;
        }

        manifest.Mission = holding.Mission;
        opening = !ahead.Opens.empty() || !ahead.Opening.empty() || !holding.GroundAsked.empty();

        for (const std::shared_ptr<MCHdArchive>& archive : holding.Taken)
        {
            std::vector<uint32_t> pieces;

            if (const auto named = holding.PiecesQueued.find(archive);
                named != holding.PiecesQueued.end() && !holding.Queued.contains(archive))
            {
                pieces.assign(named->second.begin(), named->second.end());
            }

            taken.emplace_back(archive, std::move(pieces));
        }
    }

    const std::filesystem::path pack = PackFolder();

    for (const auto& [archive, pieces] : taken)
    {
        const MCFastFile& file = *archive->File;
        std::filesystem::path folder = archive->Path.lexically_relative(pack);
        folder.replace_extension();
        ManifestEntry entry{.Folder = folder};

        if (pieces.empty())
        {
            entry.Bytes = archive->HoldBytes;
            entry.GpuBytes = UnpackedKept(file);
            entry.Frames = static_cast<uint32_t>(file.GetEntries().size());
        }
        else
        {
            for (const uint32_t piece : pieces)
            {
                const MCFastFile::HoldPiece facts = file.HoldPieceAt(piece);
                entry.Bytes += facts.Stored;
                entry.GpuBytes += facts.Unpacked;
            }

            entry.Frames = static_cast<uint32_t>(pieces.size());
        }

        // The players' part: an archive that only their mechs' folders lie under.
        const std::string name = folder.generic_string();
        const auto named = [&name](std::string_view listed) { return Under(listed, name); };
        const bool players = std::ranges::any_of(state.ManifestPlayers, [&named](const std::filesystem::path& listed)
                                                 { return named(listed.generic_string()); }) &&
                             !std::ranges::any_of(state.ManifestFolders, named);
        (players ? manifest.Players : manifest.Entries).push_back(std::move(entry));
    }

    manifest.Complete = state.ManifestFinal && !opening;
    return manifest;
}

size_t MCHdArt::HeldReads()
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return ahead.Holding.Reads;
}

void MCHdArt::WaitForHold()
{
    ReadAhead& ahead = TheState().Ahead;
    std::unique_lock lock(ahead.Lock);
    ahead.Changed.wait(lock, [&ahead]
                       { return !HoldPending(ahead) && ahead.InFlight[std::to_underlying(AheadKind::Hold)] == 0; });
}

void MCHdArt::DropGround()
{
    State& state = TheState();
    const std::filesystem::path pack = PackFolder();
    std::vector<uint32_t> folders;

    for (const std::string_view name : {TerrainFolder, ReliefFolder})
    {
        if (const auto found = state.FolderIndices.find(KeyOfPath(pack / name)); found != state.FolderIndices.end())
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

namespace
{
    /// <summary>Port-only (SPEC-memory.md's plan): the art's room shared out for a frame (PlanArt).</summary>
    struct ArtPlan
    {
        /// <summary>
        /// The frames kept in memory (records and colours): what the room leaves past the rest, never below their records
        /// and ColourFloorBytes (the records of frames on the GPU are never let go of).
        /// </summary>
        size_t FrameBytes = 0;
        /// <summary>
        /// On shared memory, the GPU's HD pictures: what the room leaves past the rest, the hold and the frames at their
        /// floor, never below the working set (MCMemoryRoom::WorkingSetBytes; the hold gives way to it instead); none on
        /// a card of its own, whose room the renderer reads (MCMemoryRoom::CardRoom).
        /// </summary>
        std::optional<uint64_t> PictureBytes;
        /// <summary>
        /// What the hold holds past its share (HoldShareOf, the pictures drawn counted), given back the least recently
        /// used first; 0 when -hdhold on forces it.
        /// </summary>
        uint64_t HoldOver = 0;
        /// <summary>
        /// The most of the unpacked streams kept (MCFastFile::KeepStreamsUpTo): what the room leaves past the rest less
        /// them, the hold, the pictures and the frames at their floor, from KeptStreamsLeast to KeptStreamsMost.
        /// </summary>
        size_t KeptStreams = MCFastFile::KeptStreamsLeast;
    };

    /// <summary>
    /// Port-only (SPEC-memory.md's "Filled in this order"): the art's room (MCMemoryRoom::ArtRoom) shared out now, by
    /// what the ledger counts: the working set first (the pictures drawn, the frames' records and a floor of colours),
    /// then the hold, then the pictures past the working set, then the frames' colours read ahead. Taken back in the
    /// reverse: the colours read ahead first (FrameBytes), then the pictures past the working set (PictureBytes), then
    /// the hold (HoldOver). The unpacked streams kept take what's left past all but the colours, within their bounds
    /// (KeptStreams).
    /// </summary>
    ArtPlan PlanArt(const MCHdArt::ReaderSettings& settings)
    {
        const ArtParts parts = ArtPartsNow();
        const uint64_t room = parts.Room.Bytes;
        ArtPlan plan;
        plan.FrameBytes =
            static_cast<size_t>(std::max(Less(room, parts.Rest + parts.Held + parts.Pictures), parts.Floor));

        if (parts.Room.Shared)
        {
            plan.PictureBytes = std::max(Less(room, parts.Rest + parts.Held + parts.Floor),
                                         std::min(Less(room, parts.Rest + parts.Floor), MCMemoryRoom::WorkingSetBytes));
        }

        if (!settings.Hold.value_or(false))
        {
            plan.HoldOver = Less(parts.Held, HoldShareOf(parts, parts.Pictures));
        }

        const uint64_t spare = Less(room, Less(parts.Rest, parts.Kept) + parts.Held + parts.Pictures + parts.Floor);
        plan.KeptStreams =
            static_cast<size_t>(std::clamp<uint64_t>(spare, MCFastFile::KeptStreamsLeast, MCFastFile::KeptStreamsMost));
        return plan;
    }

    /// <summary>
    /// Port-only: lets go of the frames kept, the least recently used first, until <paramref name="budget"/> fits them or
    /// MCMemoryRoom::TrimmedBytesPerFrame have gone or the next was asked for in the last frame (it and those after it
    /// are drawn, and would only be read again). A frame something else still holds (a texture's picture, asked for once)
    /// leaves the cache too, so its memory goes once that lets go of it, but it frees nothing now: it isn't counted, or
    /// the frames only the cache holds would wait for later frames. A frame whose picture the GPU holds keeps its record
    /// (MCHdArt::ColorsUploaded), and lets go of any colours read again for it; one kept for the mission's end
    /// (Cached::ForMission) keeps both.
    /// </summary>
    /// <returns>The bytes freed.</returns>
    size_t TrimFrames(State& state, size_t budget)
    {
        size_t freed = 0;
        auto next = state.Order.begin();

        while (next != state.Order.end() && freed < MCMemoryRoom::TrimmedBytesPerFrame && state.Bytes > budget)
        {
            const auto oldest = state.Frames.find(*next);

            if (oldest == state.Frames.end())
            {
                next = state.Order.erase(next);
                continue;
            }

            if (oldest->second.LastFrame + 1 >= state.Frame)
            {
                break;
            }

            ++next;

            if (oldest->second.OnGpu)
            {
                freed += oldest->second.Colors.size();
                LetGoColors(state, oldest->second);
                continue;
            }

            if (oldest->second.ForMission)
            {
                continue;
            }

            // The colours are the cache's alone; the record's bytes go with it when the cache's reference is its only one.
            freed += oldest->second.Colors.size();

            if (oldest->second.Frame.use_count() == 1)
            {
                freed += oldest->second.Frame->Bytes();
            }

            Drop(state, oldest->first, oldest->second);
        }

        return freed;
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's give-back): the hold lets go of up to <paramref name="bytes"/>, its runs asked for
    /// least recently first, and reads no more this mission (its room is short), saying so in the frame log.
    /// </summary>
    void GiveBackHold(size_t bytes)
    {
        const size_t freed = MCFastFile::GiveBackHeld(bytes);

        if (freed == 0)
        {
            return;
        }

        {
            ReadAhead& ahead = TheState().Ahead;
            std::scoped_lock lock(ahead.Lock);
            ahead.Holding.Full = true;
            ahead.Holding.Archives.clear();
        }

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("HD hold: gave back {:.1f} MB, {:.1f} MB held",
                                         static_cast<double>(freed) / BytesPerMb,
                                         static_cast<double>(MCFastFile::HeldBytes()) / BytesPerMb));
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "When the system says memory is short", its first step): the hold's fill stops before
    /// anything is given back: it reads and queues no more this mission, and a run being read keeps nothing (its
    /// generation moved on).
    /// </summary>
    void StopFilling()
    {
        ReadAhead& ahead = TheState().Ahead;
        std::scoped_lock lock(ahead.Lock);
        HoldQueue& holding = ahead.Holding;

        if (!holding.On || holding.Full)
        {
            return;
        }

        holding.Full = true;
        holding.Archives.clear();
        holding.NextStream = 0;
        ++holding.Generation;
        ahead.Changed.notify_all();

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("HD hold: memory short, the fill stopped at {:.1f} MB held",
                                         static_cast<double>(MCFastFile::HeldBytes()) / BytesPerMb));
        }
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "When the system says memory is short", its first step): the mission's fill on the GPU
    /// stops with the hold's: its reads waiting dropped, and nothing more asked for until the next briefing or load starts
    /// it (MCHdArt::StartBriefingHold, MCHdArt::StartLoadFill); the pictures up stay. Without it the fill went on asking
    /// for the rest of the mission, read from the disk once the hold kept no more.
    /// </summary>
    void StopGpuFill(State& state)
    {
        if (!state.GpuFillOn)
        {
            return;
        }

        state.GpuFillOn = false;

        if (std::exchange(state.GpuFits, false))
        {
            std::scoped_lock lock(state.Ahead.Lock);
            DropGpuReads(state.Ahead);
        }

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("HD fill on the GPU: memory short, the fill stopped at {:.1f} MB up",
                                         static_cast<double>(state.GpuUpBytes) / BytesPerMb));
        }
    }
}

size_t MCHdArt::CacheBudget()
{
    const State& state = TheState();

    if (state.Budget != 0)
    {
        return state.Budget;
    }

    return PlanArt(GetReaderSettings()).FrameBytes;
}

size_t MCHdArt::Frame::AllocatedBytes() const
{
    return MCAllocatedBytes(Colors.data()) + MCAllocatedBytes(Indices.data());
}

void MCHdArt::SetCacheBudget(size_t bytes)
{
    TheState().Budget = bytes;
    Trim();
}

std::optional<uint64_t> MCHdArt::NextFrame(bool memoryShort)
{
    State& state = TheState();
    ++state.Frame;
    UpdateGpuFill(state);

    // Port-only (SPEC-memory.md's indices): the frames whose pictures the GPU took a while ago let go of their indices,
    // but those of folders code on the CPU reads.
    std::erase_if(state.Uploaded,
                  [&state](const std::pair<FrameKey, uint64_t>& uploaded)
                  {
                      if (uploaded.second + IndicesKeptFrames > state.Frame)
                      {
                          return false;
                      }

                      const auto found = state.Frames.find(uploaded.first);

                      if (found != state.Frames.end() && found->second.OnGpu && found->second.Frame != nullptr &&
                          !state.Folders.at(uploaded.first.Folder).IndicesRead &&
                          !(state.KeepMarkedIndices && found->second.Frame->Marked))
                      {
                          LetGoIndices(state, found->second);
                      }

                      return true;
                  });

    // Port-only (SPEC-memory.md's ledger): the frames kept, those let go that live on, and those read ahead, as the
    // frame starts.
    state.Ledger.Set({.Payload = static_cast<int64_t>(state.Bytes - state.ColorBytes),
                      .Allocated = static_cast<int64_t>(state.AllocatedBytes - state.ColorAllocatedBytes)});
    state.ColorsLedger.Set({.Payload = static_cast<int64_t>(state.ColorBytes),
                            .Allocated = static_cast<int64_t>(state.ColorAllocatedBytes)});
    MCMemoryLedger::MCFigures letGo;
    std::erase_if(state.LetGo,
                  [&letGo](const LetGoFrame& frame)
                  {
                      if (frame.Frame.expired())
                      {
                          return true;
                      }

                      letGo.Payload += frame.Figures.Payload;
                      letGo.Allocated += frame.Figures.Allocated;
                      return false;
                  });
    state.LetGoLedger.Set(letGo);

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);
        ahead.Ledger.SetBytes(ahead.Bytes + ahead.PictureBytes);
    }

    // Port-only: what was asked for ahead and dropped since, or left out for want of room, asked for again, at most
    // every AskAgainFrames (MCHdArt::MCAskedPackets asks again in a new generation).
    if (state.Frame >= state.AskAgainAt && state.AskAgain.exchange(false))
    {
        ++state.ReAsks;
        state.AskAgainAt = state.Frame + AskAgainFrames;
    }

    if (Tracing())
    {
        Trace& trace = TheTrace();
        std::scoped_lock lock(trace.Lock);
        trace.Out << std::format("F\t{}\t{}\n", TraceNow(trace), state.Frame);
    }

    // Port-only (SPEC-memory.md's plan): the art's room shared out for this frame, each holder past its share giving back
    // MCMemoryRoom::TrimmedBytesPerFrame at most: the frames' colours read ahead first, then (the renderer) the pictures
    // past the working set, then the hold.
    //
    // While the system says memory is short (SPEC-memory.md's "When the system says memory is short"), the hold's fill
    // stops first, the GPU's with it, then the frames kept go whatever the budget, then, when they've no more to give this
    // frame, the hold, whatever -hdhold says.
    if (memoryShort)
    {
        StopFilling();
        StopGpuFill(state);
    }

    const ArtPlan plan = PlanArt(GetReaderSettings());
    MCFastFile::KeepStreamsUpTo(memoryShort ? MCFastFile::KeptStreamsLeast : plan.KeptStreams);
    const size_t budget = memoryShort ? 0 : (state.Budget != 0 ? state.Budget : plan.FrameBytes);
    const size_t freed = state.Bytes > budget ? TrimFrames(state, budget) : 0;
    const size_t left = MCMemoryRoom::TrimmedBytesPerFrame - std::min(freed, MCMemoryRoom::TrimmedBytesPerFrame);
    const size_t holdOver = memoryShort ? left : std::min(static_cast<size_t>(plan.HoldOver), left);

    if (holdOver > 0)
    {
        GiveBackHold(holdOver);
    }

    return plan.PictureBytes;
}

size_t MCHdArt::ArchivesOpen()
{
    State& state = TheState();
    const auto open = [](const auto& archive) { return archive.second != nullptr; };
    ReadAhead& ahead = state.Ahead;
    std::scoped_lock lock(ahead.Lock);
    const auto openedAhead = [](const auto& archive)
    { return archive.second.Archive.has_value() && *archive.second.Archive != nullptr; };
    return static_cast<size_t>(std::ranges::count_if(state.Archives, open) +
                               std::ranges::count_if(ahead.Opened, openedAhead));
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

std::span<const uint8_t> MCHdArt::ColorsOf(const Frame& frame)
{
    State& state = TheState();
    const auto numbered = frame.Colors.empty() ? state.ById.find(frame.Id) : state.ById.end();

    if (numbered == state.ById.end())
    {
        return frame.Colors;
    }

    const FrameKey key = numbered->second;
    Cached& cached = state.Frames.at(key);

    // Let go of whole since (and the GPU's picture with it): read again, as a first draw reads it.
    if (cached.Frame == nullptr)
    {
        Cache(key, frame.OriginalWidth(), frame.OriginalHeight());
    }
    else if (cached.Colors.empty())
    {
        // The most recently used, so the trim after keeps it.
        state.Order.splice(state.Order.end(), state.Order, cached.InOrder);
        ReadColorsAgain(state, key, cached);
        Trim();
    }

    return cached.Colors;
}

void MCHdArt::ColorsUploaded(const Frame& frame)
{
    State& state = TheState();
    const auto numbered = frame.Colors.empty() ? state.ById.find(frame.Id) : state.ById.end();

    if (numbered == state.ById.end())
    {
        return;
    }

    if (const auto found = state.Frames.find(numbered->second);
        found != state.Frames.end() && found->second.Frame != nullptr)
    {
        found->second.OnGpu = true;
        LetGoColors(state, found->second);
        state.Uploaded.emplace_back(numbered->second, state.Frame);
    }
}

void MCHdArt::PictureDropped(uint64_t id)
{
    State& state = TheState();

    if (const auto numbered = state.ById.find(id); numbered != state.ById.end())
    {
        if (const auto found = state.Frames.find(numbered->second); found != state.Frames.end())
        {
            Cached& cached = found->second;
            cached.OnGpu = false;

            // Port-only (MCHdArt::FillGpu): a picture the fill put up and the GPU let go of before its frame was drawn held
            // the frame's only colours: the frame goes whole, back in the hold as before the fill, so the readers read it
            // ahead as they read any frame to come (kept as a record, it would be read again on the game's thread).
            if (cached.Undrawn && cached.Frame != nullptr && cached.Colors.empty())
            {
                Drop(state, numbered->second, cached);
            }
        }
    }
}

size_t MCHdArt::ColorsReadAgain()
{
    return TheState().ColorsReadAgain;
}

std::string MCHdArt::OpeningMission()
{
    return TheState().OpeningMission;
}

std::bitset<256> MCHdArt::DrawnColorsOf(std::span<const uint8_t> indices)
{
    std::bitset<256> colors;

    for (size_t at = 0; at + IndexBytes <= indices.size(); at += IndexBytes)
    {
        if (OriginalDrawsMark(indices[at + 1]))
        {
            colors.set(indices[at]);
        }
    }

    return colors;
}

namespace
{
    /// <summary>The frame log's counts of the fill's pictures taken up, and their KB.</summary>
    constexpr std::string_view FrameLogGpuUp = "HD fill up";
    constexpr std::string_view FrameLogGpuUpKb = "HD fill up KB";
    constexpr int64_t BytesPerKb = 1024;

    /// <summary>Counts <paramref name="picture"/> taken up (MCHdArt::GpuFillNow, the frame log).</summary>
    void CountUp(State& state, const MCHdArt::StagedPicture& picture)
    {
        const uint64_t bytes = uint64_t{picture.ColorBytes} + picture.IndexBytes;
        ++state.GpuUp;
        state.GpuUpBytes += bytes;
        state.GpuUpAt = std::chrono::steady_clock::now();
        MCFrameLog::Count(FrameLogGpuUp);
        MCFrameLog::Count(FrameLogGpuUpKb, static_cast<int64_t>(bytes) / BytesPerKb);
    }
}

void MCHdArt::SetUploadSlots(std::shared_ptr<MCUploadSlots> slots, bool bc7)
{
    State& state = TheState();
    ReadAhead& ahead = state.Ahead;
    std::scoped_lock lock(ahead.Lock);

    // The renderer's slots gone, nothing of the fill's holds them.
    if (slots == nullptr)
    {
        DropGpuReads(ahead);
        state.DrawnStaged.clear();
    }

    ahead.Slots = std::move(slots);
    ahead.Bc7 = bc7;
}

void MCHdArt::NoteGpuRoom(std::optional<uint64_t> cardPictureBytes)
{
    State& state = TheState();
    state.GpuCardRoom = cardPictureBytes;

    // The first room noted after the ground went up was read after it (MCHdArt::GroundOnGpu asked for it).
    if (state.GpuGround == GroundState::ReadingCard)
    {
        state.GpuGround = GroundState::InRoom;
    }
}

void MCHdArt::FillGpu(std::span<const PacketAhead> packets)
{
    State& state = TheState();
    state.GpuOrder.insert(state.GpuOrder.end(), packets.begin(), packets.end());
}

MCHdArt::GpuFill MCHdArt::GpuFillNow()
{
    const State& state = TheState();
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return GpuFill{.Fits = state.GpuFits,
                   .MissionBytes = state.GpuMissionBytes,
                   .PagesShare = state.GpuPagesShare.value_or(PagesShareFirst),
                   .Asked = ahead.GpuReads,
                   .Staged = ahead.GpuStaged,
                   .Up = state.GpuUp,
                   .UpBytes = state.GpuUpBytes,
                   .Waiting = ahead.Staged.size() + state.DrawnStaged.size(),
                   .Reading = !GpuFillDone(state),
                   .OpeningBytes = state.OpeningWalkedBytes,
                   .OpeningAsked = state.OpeningAsked.size(),
                   .OpeningDone = state.OpeningDone || state.OpeningAtRoom};
}

std::optional<std::string> MCHdArt::MechPartArchive(std::string_view folder)
{
    // The start of a mech part's folders in the pack (mechs/legs90/7/0).
    constexpr std::string_view MechsFolderStart = "mechs/";

    if (!folder.starts_with(MechsFolderStart))
    {
        return std::nullopt;
    }

    const size_t part = folder.find('/', MechsFolderStart.size());

    if (part == std::string_view::npos || part + 1 == folder.size())
    {
        return std::nullopt;
    }

    return std::string(folder.substr(0, folder.find('/', part + 1)));
}

void MCHdArt::SetOpening(std::span<const OpeningPacket> packets)
{
    State& state = TheState();
    state.Opening.assign(packets.begin(), packets.end());
    ++state.OpeningGeneration;
    OpenArchivesAhead(OpeningFolders(state));
}

std::vector<std::string> MCHdArt::OpeningLance()
{
    State& state = TheState();
    UpdateOpeningLance(state);
    std::vector<std::string> lance = state.OpeningLancePlaced;
    lance.insert(lance.end(), state.OpeningLanceLoaded.begin(), state.OpeningLanceLoaded.end());
    return lance;
}

std::vector<MCHdArt::StagedPicture> MCHdArt::TakeStagedPictures(size_t mostCount, size_t mostBytes)
{
    State& state = TheState();
    std::vector<std::pair<FrameKey, StagedRead>> taken;
    std::vector<StagedPicture> pictures;
    size_t bytes = 0;
    const auto sizeOf = [](const StagedPicture& picture) { return size_t{picture.ColorBytes} + picture.IndexBytes; };

    // A draw's the renderer didn't ask for (a frame read for the CPU alone) goes up first, its colours in its picture
    // alone: else they'd be read again when it's drawn.
    for (auto drawn = state.DrawnStaged.begin(); drawn != state.DrawnStaged.end() && pictures.size() < mostCount;)
    {
        if (drawn->second.second + DrawnStagedFrames >= state.Frame)
        {
            ++drawn;
            continue;
        }

        const size_t size = sizeOf(drawn->second.first);

        if (!pictures.empty() && bytes + size > mostBytes)
        {
            break;
        }

        bytes += size;
        CountUp(state, drawn->second.first);
        pictures.push_back(std::move(drawn->second.first));
        drawn = state.DrawnStaged.erase(drawn);
    }

    {
        ReadAhead& ahead = state.Ahead;
        std::scoped_lock lock(ahead.Lock);

        while (!ahead.StagedOrder.empty() && pictures.size() + taken.size() < mostCount)
        {
            const FrameKey key = ahead.StagedOrder.front();
            const auto found = ahead.Staged.find(key);

            // One a draw took (TakeAhead) is the draw's.
            if (found == ahead.Staged.end())
            {
                ahead.StagedOrder.pop_front();
                continue;
            }

            const size_t size = sizeOf(found->second.Picture);

            if ((!taken.empty() || !pictures.empty()) && bytes + size > mostBytes)
            {
                break;
            }

            bytes += size;
            ahead.Known.insert(key);
            taken.emplace_back(key, std::move(found->second));
            ahead.Staged.erase(found);
            ahead.StagedOrder.pop_front();
        }
    }

    // Kept as KeepReadAhead keeps a frame read ahead, numbered as a draw would number it.
    pictures.reserve(pictures.size() + taken.size());

    for (auto& [key, read] : taken)
    {
        Cached& cached = state.Frames[key];

        // Read meanwhile by the game's thread itself: its own picture goes up as it's drawn.
        if (cached.Frame != nullptr)
        {
            continue;
        }

        cached.Looked = true;
        cached.Undrawn = true;
        cached.LastFrame = state.Frame;
        Number(state, key, cached);
        read.Frame->Id = cached.Id;
        read.Picture.Record = read.Frame;
        KeepRead(state, key, cached, std::move(read.Frame));
        ++state.Read;
        ++state.TakenAhead;
        CountUp(state, read.Picture);
        pictures.push_back(std::move(read.Picture));
    }

    return pictures;
}

std::optional<MCHdArt::StagedPicture> MCHdArt::TakeStagedPicture(uint64_t id)
{
    State& state = TheState();
    const auto found = state.DrawnStaged.find(id);

    if (found == state.DrawnStaged.end())
    {
        return std::nullopt;
    }

    StagedPicture picture = std::move(found->second.first);
    state.DrawnStaged.erase(found);
    CountUp(state, picture);
    return picture;
}

bool MCHdArt::LoadFilling()
{
    ReadAhead& ahead = TheState().Ahead;
    std::scoped_lock lock(ahead.Lock);
    return ahead.Holding.Loading;
}

std::span<const uint8_t> MCHdArt::IndicesOf(const Frame& frame, MCIndicesUse use)
{
    // The cache holds the record until its next trim.
    const std::shared_ptr<const Frame> record = IndexedRecord(frame, use);
    return record != nullptr ? std::span<const uint8_t>(record->Indices) : frame.Indices;
}

size_t MCHdArt::IndicesReadAgain()
{
    return TheState().IndicesReadAgain;
}

void MCHdArt::KeepIndices(bool marked)
{
    TheState().KeepMarkedIndices = marked;
}
