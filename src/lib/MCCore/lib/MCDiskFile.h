#pragma once

// Only standard headers: MCDiskFile.cpp is built without the precompiled header on Windows (windows.h).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class MCHeldFile;

/// <summary>How an <see cref="MCDiskFile"/> is opened.</summary>
enum class MCDiskFileMode : uint8_t
{
    /// <summary>Read an existing file.</summary>
    Read,
    /// <summary>Create (truncate) a file, then write and read it.</summary>
    Create
};

/// <summary>How an <see cref="MCDiskFile"/> reads, chosen as it opens.</summary>
enum class MCDiskFileReads : uint8_t
{
    /// <summary>
    /// Each read one call of exactly the bytes asked for: an archive's (an HD archive's runs and entries, the original's
    /// FastFiles' entries), already whole at known places, which a buffer would only copy and read past.
    /// </summary>
    Direct,
    /// <summary>
    /// Through a buffer of the file's own that grows while the file is read in order: a file read a little at a time
    /// (the original's loose files, a byte, word or line at a time; the music and movies, a frame at a time).
    /// </summary>
    Buffered
};

/// <summary>
/// An open file on disk, the system's own (Windows' <c>CreateFileW</c>, the Mac's <c>open</c>), closed when destroyed.
/// Every read and write names its offset to the system (<c>ReadFile</c> with an <c>OVERLAPPED</c>'s offset,
/// <c>pread</c>), so a read is one call however large: a C++ stream split each into reads up to its 4 KB boundaries
/// (MSVC's: a 108 KB run was 28 reads), which a slow disk's cache took as that many reads of the disk.
/// </summary>
/// <remarks>
/// A buffered file's buffer starts at <see cref="FirstFillBytes"/>, doubles at each fill that carries on where the last
/// read of the file ended, up to <see cref="MostFillBytes"/>, and goes back to the first size after a jump; a file of
/// <see cref="MostFillBytes"/> or less is read whole at its first read; a read of the next fill's size or more goes
/// straight into the caller's bytes; a read across the buffer's end takes what it holds and carries on from there; a
/// write empties it. It holds what it read until this file writes or a read falls outside
/// it: another handle's writes meanwhile aren't seen in it. One thread at a time.
/// </remarks>
class MCDiskFile
{
public:
    /// <summary>A buffered file's first fill, and its fill after a jump.</summary>
    static constexpr size_t FirstFillBytes = 16u * 1024u;
    /// <summary>A buffered file's largest fill, and the largest file read whole at its first.</summary>
    static constexpr size_t MostFillBytes = 256u * 1024u;

    /// <summary>
    /// Port-only (SPEC-first-use.md): the most bytes one read of a file's neighbouring pieces takes (HD art's readers'
    /// reads of an archive's entries, a packet file's batch of packets), and the widest gap between two pieces one read
    /// takes both across (read and thrown away: a read saved costs a slow disk more than this many bytes do).
    /// </summary>
    static constexpr uint64_t RunMostBytes = 1024u * 1024u;
    static constexpr uint64_t RunGapBytes = 64u * 1024u;

    /// <summary>
    /// Opens <paramref name="path"/>, reading as <paramref name="reads"/> says, shared for reading and writing with other
    /// handles (as MSVC's streams).
    /// </summary>
    /// <returns>The open file, or nothing when it couldn't be opened.</returns>
    static std::optional<MCDiskFile> Open(const std::filesystem::path& path, MCDiskFileMode mode,
                                          MCDiskFileReads reads);

    MCDiskFile(MCDiskFile&& other) noexcept;
    MCDiskFile& operator=(MCDiskFile&& other) noexcept;
    MCDiskFile(const MCDiskFile&) = delete;
    MCDiskFile& operator=(const MCDiskFile&) = delete;
    ~MCDiskFile();

    /// <summary>Reads up to <paramref name="buffer"/>'s size from the current position, advancing it.</summary>
    /// <returns>The number of bytes read (fewer at the file's end).</returns>
    size_t Read(std::span<uint8_t> buffer);

    /// <summary>Reads up to <paramref name="buffer"/>'s size at <paramref name="offset"/>; the position follows it.</summary>
    /// <returns>The number of bytes read (fewer at the file's end).</returns>
    size_t ReadAt(uint32_t offset, std::span<uint8_t> buffer);

    /// <summary>Writes <paramref name="data"/> at the current position, advancing it.</summary>
    /// <returns>The number of bytes written.</returns>
    size_t Write(std::span<const uint8_t> data);

    /// <summary>Writes <paramref name="data"/> at <paramref name="offset"/>; the position follows it.</summary>
    /// <returns>The number of bytes written.</returns>
    size_t WriteAt(uint32_t offset, std::span<const uint8_t> data);

    /// <summary>Where <see cref="Read"/> and <see cref="Write"/> go on from (past the end, a read gets nothing).</summary>
    uint32_t Position() const { return _Position; }
    void SetPosition(uint32_t position) { _Position = position; }

    /// <summary>The file's length in bytes, asked of the system (the position is kept).</summary>
    uint32_t Length() const;

    /// <summary>Port-only, for the tests: the reads this file has asked of the system.</summary>
    size_t SystemReads() const { return _SystemReads.load(std::memory_order_relaxed); }

    /// <summary>
    /// Port-only (SPEC-first-use.md): reads from now on take their bytes from <paramref name="held"/> (this file's
    /// bytes read whole into memory) rather than the system, until <see cref="LetGo"/>, a write, or the hold failing,
    /// when they read the disk as ever.
    /// </summary>
    void Hold(std::shared_ptr<MCHeldFile> held) { _Held = std::move(held); }
    void LetGo() { _Held.reset(); }

    /// <summary>Port-only: the hold reads take their bytes from (<see cref="Hold"/>), if any.</summary>
    const std::shared_ptr<MCHeldFile>& Held() const { return _Held; }

private:
    MCDiskFile() = default;

    /// <summary>One call (more only past a call's most) reading <paramref name="buffer"/>'s size at <paramref name="offset"/>.</summary>
    size_t ReadDirect(uint32_t offset, std::span<uint8_t> buffer) const;

    /// <summary>A buffered file's read: from the buffer, filled as the remarks say when it lacks the bytes.</summary>
    size_t ReadBuffered(uint32_t offset, std::span<uint8_t> buffer);

    /// <summary>Fills the buffer with up to <paramref name="bytes"/> at <paramref name="offset"/>.</summary>
    void Fill(uint32_t offset, size_t bytes);

    /// <summary>Empties the buffer, its next fill the first size and a check for a file read whole (a write).</summary>
    void EmptyBuffer();

    /// <summary>Closes the file, if open.</summary>
    void Close();

#ifdef _WIN32
    /// <summary>The file's handle (a HANDLE; null: none).</summary>
    void* _Handle = nullptr;
#else
    /// <summary>The file's descriptor (-1: none).</summary>
    int _Descriptor = -1;
#endif
    MCDiskFileReads _Reads = MCDiskFileReads::Direct;
    /// <summary>Where Read and Write go on from.</summary>
    uint32_t _Position = 0;
    /// <summary>
    /// The buffer (its size the largest fill's), where in the file it starts, the bytes it holds, the last fill's size,
    /// whether the file has been read since it opened or last wrote and where that read ended (a fill's or a read
    /// straight in), and whether the buffer holds the whole file.
    /// </summary>
    std::vector<uint8_t> _Buffer;
    uint32_t _BufferAt = 0;
    size_t _BufferHeld = 0;
    size_t _FillBytes = FirstFillBytes;
    bool _Filled = false;
    uint64_t _ReadEnd = 0;
    bool _Whole = false;
    /// <summary>The reads asked of the system (SystemReads).</summary>
    mutable std::atomic<size_t> _SystemReads{0};
    /// <summary>Port-only: the file's bytes held in memory that reads take (Hold).</summary>
    std::shared_ptr<MCHeldFile> _Held;
};
