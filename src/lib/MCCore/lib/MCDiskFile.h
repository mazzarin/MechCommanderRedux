#pragma once

// Only standard headers: MCDiskFile.cpp is built without the precompiled header on Windows (windows.h).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

/// <summary>How an <see cref="MCDiskFile"/> is opened.</summary>
enum class MCDiskFileMode : uint8_t
{
    /// <summary>Read an existing file.</summary>
    Read,
    /// <summary>Create (truncate) a file, then write and read it.</summary>
    Create
};

/// <summary>
/// An open file on disk, the system's own (Windows' <c>CreateFileW</c>, the Mac's <c>open</c>), closed when destroyed.
/// Every read and write names its offset to the system (<c>ReadFile</c> with an <c>OVERLAPPED</c>'s offset,
/// <c>pread</c>), so a read is one call however large: a C++ stream split each into reads up to its 4 KB boundaries
/// (MSVC's: a 108 KB run was 28 reads), which a slow disk's cache took as that many reads of the disk.
/// </summary>
/// <remarks>
/// A read under <see cref="WindowBytes"/> is served from a window of the file's own, filled by one call at its offset:
/// the original's files read a byte, word or line at a time. The window holds what it read until this file writes or a
/// read falls outside it; another handle's writes meanwhile aren't seen in it. Through the window, the position and the
/// writes, one thread at a time; <see cref="ReadShared"/> from any number at once (on Windows the handle is
/// overlapped, each call waiting for its own end: a handle opened for synchronous I/O makes the threads' reads wait
/// for each other).
/// </remarks>
class MCDiskFile
{
public:
    /// <summary>The window's bytes, and the size of a read from which reads go straight into the caller's bytes.</summary>
    static constexpr size_t WindowBytes = 64u * 1024u;

    /// <summary>Opens <paramref name="path"/>, shared for reading and writing with other handles (as MSVC's streams).</summary>
    /// <returns>The open file, or nothing when it couldn't be opened.</returns>
    static std::optional<MCDiskFile> Open(const std::filesystem::path& path, MCDiskFileMode mode);

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

    /// <summary>
    /// Reads up to <paramref name="buffer"/>'s size at <paramref name="offset"/> straight into it, in one call, without
    /// the window or the position: from any thread, with others reading the file at once (an HD archive's runs and
    /// entries, the game's thread and every reader through the archive's one file).
    /// </summary>
    /// <returns>The number of bytes read (fewer at the file's end).</returns>
    size_t ReadShared(uint32_t offset, std::span<uint8_t> buffer) const;

    /// <summary>The file's length in bytes, asked of the system (the position is kept).</summary>
    uint32_t Length() const;

    /// <summary>Port-only, for the tests: the reads this file has asked of the system.</summary>
    size_t SystemReads() const { return _SystemReads.load(std::memory_order_relaxed); }

private:
    MCDiskFile() = default;

    /// <summary>A read under WindowBytes, from the window, filled at <paramref name="offset"/> when it lacks the bytes.</summary>
    size_t ReadThroughWindow(uint32_t offset, std::span<uint8_t> buffer);

    /// <summary>Closes the file, if open.</summary>
    void Close();

#ifdef _WIN32
    /// <summary>The file's handle (a HANDLE; null: none).</summary>
    void* _Handle = nullptr;
#else
    /// <summary>The file's descriptor (-1: none).</summary>
    int _Descriptor = -1;
#endif
    /// <summary>Where Read and Write go on from.</summary>
    uint32_t _Position = 0;
    /// <summary>The window (made at the first small read), where in the file it starts, and the bytes it holds.</summary>
    std::unique_ptr<uint8_t[]> _Window;
    uint32_t _WindowAt = 0;
    size_t _WindowHeld = 0;
    /// <summary>The reads asked of the system (SystemReads).</summary>
    mutable std::atomic<size_t> _SystemReads{0};
};
