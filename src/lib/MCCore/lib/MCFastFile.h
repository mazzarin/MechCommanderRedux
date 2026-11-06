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
};
