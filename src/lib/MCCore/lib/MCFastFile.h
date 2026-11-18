#pragma once

#include "lib/MCDiskFile.h"
#include "lib/MCOptionalRef.h"

/// <summary>
/// One entry of a FastFile's directory, as stored in the file: where the data is, its stored and unpacked sizes and
/// its game path (<c>data\art\ACCESS00.tga</c>). <c>Size == RealSize</c> means stored raw, otherwise LZ-packed.
/// </summary>
#pragma pack(push, 1)
struct MCFileEntry
{
    /// <summary>Offset of the data from the start of the FastFile.</summary>
    int32_t Offset;
    /// <summary>Stored size in bytes.</summary>
    int32_t Size;
    /// <summary>Unpacked size in bytes.</summary>
    int32_t RealSize;
    /// <summary>The entry's game path, zero-terminated.</summary>
    std::array<char, 250> Name; // Fixed layout: FastFile directory entry

    /// <summary>The game path, up to its terminator.</summary>
    std::string_view GetName() const
    {
        return std::string_view(Name.data(), std::ranges::find(Name, '\0') - Name.begin());
    }
};
#pragma pack(pop)
static_assert(sizeof(MCFileEntry) == 0x106);

namespace MCZstd
{
    class Dictionaries;
}

/// <summary>
/// A FastFile (<c>.FST</c>): an archive of game files, each stored raw or LZ-packed, that <c>MCFile::Open</c> falls
/// back to when a path isn't found on disk. The game opens every <c>*.fst</c> beside SYSTEM.CFG at startup
/// (<see cref="MCFastFileSet"/>).
/// </summary>
/// <remarks>
/// Original source: <c>lib\ffile.cpp</c>. The original opened an entry by name into a handle, then sought and read
/// through it; the game only ever read entries whole, which is what the port's interface does.
/// </remarks>
class MCFastFile
{
public:
    /// <summary>
    /// Opens the archive at <paramref name="fileName"/> and reads its directory. A version 2 archive's streams packed
    /// against a dictionary unpack against <paramref name="dictionaries"/> (its pack's <c>dicts.hdd</c>); one naming a
    /// dictionary they lack, or naming one when none are given, refuses the archive.
    /// </summary>
    /// <returns>The archive, or why it couldn't be opened.</returns>
    static std::expected<std::unique_ptr<MCFastFile>, std::string> Create(
        std::string_view fileName, std::shared_ptr<const MCZstd::Dictionaries> dictionaries = nullptr);

    /// <summary>
    /// Opens the archive at the file system's <paramref name="path"/> (not a game path: a network share's too), named
    /// <paramref name="fileName"/> in what's said of it, and reads its directory, its streams unpacking against
    /// <paramref name="dictionaries"/> as <see cref="Create"/>'s do.
    /// </summary>
    /// <returns>The archive, or why it couldn't be opened.</returns>
    static std::expected<std::unique_ptr<MCFastFile>, std::string> CreateAt(
        const std::filesystem::path& path, std::string_view fileName,
        std::shared_ptr<const MCZstd::Dictionaries> dictionaries = nullptr);

    MCFastFile(const MCFastFile&) = delete;
    MCFastFile& operator=(const MCFastFile&) = delete;

    /// <summary>The index of the entry named <paramref name="gamePath"/> (case ignored; the first of equal names).</summary>
    std::optional<int32_t> Find(std::string_view gamePath) const;

    /// <summary>Reads entry <paramref name="index"/>'s data, unpacked, into <paramref name="data"/> (RealSize bytes).</summary>
    /// <returns>The bytes read, or 0 when a packed entry didn't unpack to its full size.</returns>
    int32_t ReadEntry(int32_t index, std::span<uint8_t> data);

    /// <summary>
    /// Port-only: entry <paramref name="index"/>'s stored bytes as they lie in the archive (Size of them; packed or not),
    /// for an archive whose entries another packer packed (the HD art pack's).
    /// </summary>
    std::vector<uint8_t> ReadStored(int32_t index);

    /// <summary>
    /// Port-only: a handle of its own on the archive's file, for a reader on another thread (a file's stream can't be read
    /// from two at once); none when it can't be opened, or the file changed since its directory was read (as a released
    /// archive's reads fail).
    /// </summary>
    std::optional<MCDiskFile> OpenAnother() const;

    /// <summary>
    /// Port-only: entry <paramref name="index"/>'s stored bytes as <see cref="ReadStored(int32_t)"/> reads them, through
    /// <paramref name="file"/> (a handle <see cref="OpenAnother"/> gave), from any thread.
    /// </summary>
    std::vector<uint8_t> ReadStored(int32_t index, MCDiskFile& file) const;

    /// <summary>The number of entries in the archive.</summary>
    int32_t GetNumFiles() const { return static_cast<int32_t>(_Entries.size()); }

    /// <summary>The directory records, in the archive's order (for the tools; the game goes by name).</summary>
    std::span<const MCFileEntry> GetEntries() const { return _Entries; }

    /// <summary>
    /// Port-only: closes the archive's file and keeps its directory, for a reader holding more archives than a process
    /// may keep open (the HD art pack's); the next read opens the file again, unless it changed since its directory was
    /// read (its size or time): then reads fail, rather than reading another file by this one's offsets.
    /// </summary>
    void Release() { _Handle.reset(); }

    /// <summary>Port-only: whether the archive's file is open (<see cref="Release"/>).</summary>
    bool IsOpen() const { return _Handle.has_value(); }

    /// <summary>
    /// Port-only: whether this is a version 2 HD archive (SPEC-pack-v2.md): its entries found by their paths' keys, with
    /// no names, and packed together in streams. Its <see cref="GetEntries"/> give each entry its stream's place
    /// (Offset and Size, so a read of neighbouring entries reads a stream once) and its own bytes (RealSize).
    /// </summary>
    bool IsVersion2() const { return _Version2; }

    /// <summary>
    /// Port-only (the HD art pack's archives): entry <paramref name="index"/>'s bytes from its stored bytes
    /// <paramref name="stored"/> as <see cref="ReadStored(int32_t)"/> gives them: a version 1 entry inflated when packed
    /// (DEFLATE), a version 2 entry cut from its stream, unpacked once and kept a while for its neighbours; or what is
    /// wrong with them.
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> Unpack(int32_t index, std::span<const uint8_t> stored) const;

    /// <summary>
    /// Port-only (version 2): entry <paramref name="index"/>'s stream unpacked from its stored bytes
    /// <paramref name="stored"/> (<see cref="ReadStored(int32_t)"/>'s), or found kept from an earlier unpacking, and kept
    /// a while for its neighbours, for a reader cutting several of its entries (<see cref="EntryIn"/>) from one unpacking;
    /// or what is wrong with them. Unpacked for its entries of a version 1 archive too: its stored bytes as
    /// <see cref="Unpack"/> gives them.
    /// </summary>
    std::expected<std::shared_ptr<const std::vector<uint8_t>>, std::string> StreamOf(
        int32_t index, std::span<const uint8_t> stored) const;

    /// <summary>
    /// Port-only (version 2): entry <paramref name="index"/>'s stream unpacked if it is kept from an earlier unpacking
    /// (<see cref="Unpack"/>, <see cref="StreamOf"/>), so its entry needs no read; none when it isn't, and for an archive of
    /// version 1.
    /// </summary>
    std::shared_ptr<const std::vector<uint8_t>> KeptStreamOf(int32_t index) const;

    /// <summary>
    /// Port-only: entry <paramref name="index"/>'s bytes in <paramref name="stream"/>, its stream unpacked
    /// (<see cref="StreamOf"/>'s or <see cref="KeptStreamOf"/>'s): cut from it in a version 2 archive, all of it in one of
    /// version 1.
    /// </summary>
    std::span<const uint8_t> EntryIn(int32_t index, const std::vector<uint8_t>& stream) const;

    /// <summary>
    /// Port-only (version 2): whether the archive has folder <paramref name="path"/> (below the archive, <c>\</c>
    /// between names, case ignored; the empty path its own), found by its key with nothing read.
    /// </summary>
    bool HasFolder(std::string_view path) const;

    /// <summary>
    /// Port-only (version 2): folder <paramref name="path"/>'s facts records, each its type and data, in order; none for
    /// a folder without facts or an archive of version 1.
    /// </summary>
    std::vector<std::pair<uint16_t, std::span<const uint8_t>>> Facts(std::string_view path) const;

    /// <summary>Port-only: the key a version 2 archive finds <paramref name="path"/> by (XXH3-64 of it lower-cased).</summary>
    static uint64_t PathKey(std::string_view path);

    /// <summary>Port-only (version 2): what a stream holds (SPEC-pack-v2.md's streams).</summary>
    enum class StreamKind : uint8_t
    {
        /// <summary>A packet's frames with their in-betweens and turn middles.</summary>
        Packet = 0,
        /// <summary>Every packet's first frames, together.</summary>
        Heads = 1,
        Tiles = 2,
        Pictures = 3,
    };

    /// <summary>Port-only (version 2): a stream's place in the file, its stored bytes and its kind.</summary>
    struct StreamFacts
    {
        uint64_t At = 0;
        uint32_t Stored = 0;
        StreamKind Kind = StreamKind::Packet;
    };

    /// <summary>Port-only: the archive's streams (none in an archive of version 1), in the data's order.</summary>
    uint32_t StreamCount() const { return static_cast<uint32_t>(_Streams.size()); }

    /// <summary>Port-only (version 2): stream <paramref name="stream"/>'s place, stored bytes and kind.</summary>
    StreamFacts StreamAt(uint32_t stream) const;

    /// <summary>
    /// Port-only (version 2; the hold, SPEC-pack-v2.md's Memory): keeps stream <paramref name="stream"/>'s stored bytes
    /// <paramref name="stored"/>, as they lie in the archive, in memory, where <see cref="ReadStored(int32_t)"/> and HD
    /// art's readers take them in place of the disk, until <see cref="LetGoHeld"/>. False, keeping nothing, when they
    /// aren't its stored size.
    /// </summary>
    bool HoldStream(uint32_t stream, std::vector<uint8_t>&& stored) const;

    /// <summary>
    /// Port-only (version 2): entry <paramref name="index"/>'s stream's stored bytes when they are held
    /// (<see cref="HoldStream"/>); none when they aren't, and for an archive of version 1.
    /// </summary>
    std::shared_ptr<const std::vector<uint8_t>> HeldStreamOf(int32_t index) const;

    /// <summary>Port-only: lets go of every stream held (<see cref="HoldStream"/>), every archive's.</summary>
    static void LetGoHeld();

    /// <summary>Port-only: the stored bytes of the streams held now, every archive's.</summary>
    static size_t HeldBytes();

    /// <summary>Lets only <see cref="Create"/> construct an archive.</summary>
    class Key
    {
        friend class MCFastFile;
        Key() = default;
    };

    /// <summary>An archive with no directory yet (see <see cref="Create"/>).</summary>
    explicit MCFastFile(Key) {}

private:
    /// <summary>The open archive, opened again after <see cref="Release"/>; none when it can't be.</summary>
    MCOptionalRef<MCDiskFile> File();

    /// <summary>The archive's path, as opened, and its length and time then (<see cref="File"/>).</summary>
    std::filesystem::path _Path;
    uint32_t _Length = 0;
    std::filesystem::file_time_type _Written{};
    /// <summary>The directory.</summary>
    std::vector<MCFileEntry> _Entries;
    /// <summary>Entry index by upper-cased name (the first entry of a name wins, as the original's search).</summary>
    std::unordered_map<std::string, int32_t> _Index;
    /// <summary>The open archive.</summary>
    std::optional<MCDiskFile> _Handle;

    /// <summary>Port-only: reads a version 2 archive's header and table (<see cref="IsVersion2"/>) into this one.</summary>
    std::expected<void, std::string> ReadVersion2(std::string_view fileName);

    /// <summary>
    /// Port-only (version 2): a stream's place, sizes, codec and kind, and the dictionary it was packed against (its
    /// number in <see cref="_Dictionaries"/>; 0: none).
    /// </summary>
    struct Stream
    {
        uint64_t At = 0;
        uint32_t Stored = 0;
        uint32_t Unpacked = 0;
        uint8_t Codec = 0;
        StreamKind Kind = StreamKind::Packet;
        uint16_t Dictionary = 0;
    };

    /// <summary>Port-only (version 2): the pack's dictionaries its streams name, as <see cref="Create"/> was given.</summary>
    std::shared_ptr<const MCZstd::Dictionaries> _Dictionaries;

    /// <summary>Port-only (version 2): an entry's stream, and where it lies in the stream unpacked.</summary>
    struct Placed
    {
        uint32_t Stream = 0;
        uint32_t At = 0;
        uint32_t Bytes = 0;
    };

    bool _Version2 = false;
    /// <summary>
    /// Port-only (version 2): this archive's own number, keying its unpacked streams kept a while and its streams held.
    /// </summary>
    uint64_t _Serial = 0;
    std::vector<Stream> _Streams;
    /// <summary>Port-only (version 2): the entries' keys, sorted, and each one's place (by the same index).</summary>
    std::vector<uint64_t> _EntryKeys;
    std::vector<Placed> _Placed;
    /// <summary>Port-only (version 2): the folders' keys, sorted, each one's facts (start and bytes), and the facts.</summary>
    std::vector<uint64_t> _FolderKeys;
    std::vector<std::pair<uint32_t, uint32_t>> _FolderFacts;
    std::vector<uint8_t> _Facts;
};
