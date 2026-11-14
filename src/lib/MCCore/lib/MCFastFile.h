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
    /// Port-only: the archive's file, shared by every thread's reads (they read at an offset, MCDiskFile::ReadShared,
    /// moving nothing shared), opened again after <see cref="Release"/> when it is the file the directory was read from;
    /// none when it can't be. A read holding it keeps it open through a Release. Call where Release is called: one
    /// thread, or under one lock.
    /// </summary>
    std::shared_ptr<MCDiskFile> File();

    /// <summary>Port-only: the archive's file as open now, without opening it again; none after <see cref="Release"/>.</summary>
    const std::shared_ptr<MCDiskFile>& Handle() const { return _Handle; }

    /// <summary>
    /// Port-only: a new handle on the archive's file, from any thread (it reads only the path, length and time kept):
    /// none when it can't be opened, or the file changed since its directory was read (then reads fail, rather than
    /// reading another file by this one's offsets). For a caller that opens it again without the lock its
    /// <see cref="File"/> takes (an open may take milliseconds), handing it to <see cref="Reopened"/>.
    /// </summary>
    std::shared_ptr<MCDiskFile> OpenAgain() const;

    /// <summary>
    /// Port-only: keeps <paramref name="opened"/> (<see cref="OpenAgain"/>'s) as the archive's file, unless it was opened
    /// again meanwhile (then <paramref name="opened"/> closes).
    /// </summary>
    /// <returns>The archive's file now.</returns>
    std::shared_ptr<MCDiskFile> Reopened(std::shared_ptr<MCDiskFile> opened);

    /// <summary>
    /// Port-only: entry <paramref name="index"/>'s stored bytes as they lie in the archive (Size of them; packed or not),
    /// for an archive whose entries another packer packed (the HD art pack's), through <paramref name="file"/> (its
    /// <see cref="File"/>), from any thread.
    /// </summary>
    std::vector<uint8_t> ReadStored(int32_t index, const MCDiskFile& file) const;

    /// <summary>The number of entries in the archive.</summary>
    int32_t GetNumFiles() const { return static_cast<int32_t>(_Entries.size()); }

    /// <summary>The directory records, in the archive's order (for the tools; the game goes by name).</summary>
    std::span<const MCFileEntry> GetEntries() const { return _Entries; }

    /// <summary>
    /// Port-only: closes the archive's file and keeps its directory, for a reader holding more archives than a process
    /// may keep open (the HD art pack's), once the reads in hand (holding <see cref="File"/>) end; the next read opens
    /// the file again, unless it changed since its directory was read (its size or time): then reads fail, rather than
    /// reading another file by this one's offsets.
    /// </summary>
    void Release() { _Handle.reset(); }

    /// <summary>Port-only: whether the archive's file is open (<see cref="Release"/>).</summary>
    bool IsOpen() const { return _Handle != nullptr; }

    /// <summary>Lets only <see cref="Create"/> construct an archive.</summary>
    class Key
    {
        friend class MCFastFile;
        Key() = default;
    };

    /// <summary>An archive with no directory yet (see <see cref="Create"/>).</summary>
    explicit MCFastFile(Key) {}

private:
    /// <summary>The archive's path, as opened, and its length and time then (<see cref="File"/>).</summary>
    std::filesystem::path _Path;
    uint32_t _Length = 0;
    std::filesystem::file_time_type _Written{};
    /// <summary>The directory.</summary>
    std::vector<MCFileEntry> _Entries;
    /// <summary>Entry index by upper-cased name (the first entry of a name wins, as the original's search).</summary>
    std::unordered_map<std::string, int32_t> _Index;
    /// <summary>The open archive, shared with the reads in hand.</summary>
    std::shared_ptr<MCDiskFile> _Handle;
};
