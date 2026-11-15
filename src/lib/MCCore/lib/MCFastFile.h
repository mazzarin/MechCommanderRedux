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
    /// <summary>Opens the archive at <paramref name="fileName"/> and reads its directory.</summary>
    /// <returns>The archive, or why it couldn't be opened.</returns>
    static std::expected<std::unique_ptr<MCFastFile>, std::string> Create(std::string_view fileName);

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

    /// <summary>Port-only (version 2): a stream's place, sizes and codec.</summary>
    struct Stream
    {
        uint64_t At = 0;
        uint32_t Stored = 0;
        uint32_t Unpacked = 0;
        uint8_t Codec = 0;
    };

    /// <summary>Port-only (version 2): an entry's stream, and where it lies in the stream unpacked.</summary>
    struct Placed
    {
        uint32_t Stream = 0;
        uint32_t At = 0;
        uint32_t Bytes = 0;
    };

    bool _Version2 = false;
    /// <summary>Port-only (version 2): this archive's own number, keying its unpacked streams kept a while.</summary>
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
