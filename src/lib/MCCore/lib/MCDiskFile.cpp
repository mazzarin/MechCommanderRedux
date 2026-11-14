// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "lib/MCDiskFile.h"

#include <algorithm>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    /// <summary>The most bytes asked of the system in one call (its counts are 32-bit on Windows).</summary>
    constexpr size_t MostInOneCall = size_t{1} << 30;

#ifdef _WIN32
    /// <summary>An OVERLAPPED naming <paramref name="offset"/>, for a call at it.</summary>
    OVERLAPPED At(uint64_t offset)
    {
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        return at;
    }
#else
    /// <summary>The permissions a new file gets before the process's mask, as a stream's.</summary>
    constexpr mode_t NewFilePermissions = 0666;
#endif
}

std::optional<MCDiskFile> MCDiskFile::Open(const std::filesystem::path& path, MCDiskFileMode mode)
{
    const bool create = mode == MCDiskFileMode::Create;
    MCDiskFile file;
#ifdef _WIN32
    // The path as the system spells it (UTF-16), so any name opens; shared as MSVC's streams are (_SH_DENYNO).
    const HANDLE handle = CreateFileW(path.c_str(), create ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      create ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }

    file._Handle = handle;
#else
    const int flags = (create ? O_RDWR | O_CREAT | O_TRUNC : O_RDONLY) | O_CLOEXEC;
    int descriptor = -1;

    do
    {
        descriptor = ::open(path.c_str(), flags, NewFilePermissions);
    } while (descriptor < 0 && errno == EINTR);

    if (descriptor < 0)
    {
        return std::nullopt;
    }

    file._Descriptor = descriptor;
#endif
    return file;
}

MCDiskFile::MCDiskFile(MCDiskFile&& other) noexcept
{
    *this = std::move(other);
}

MCDiskFile& MCDiskFile::operator=(MCDiskFile&& other) noexcept
{
    if (this != &other)
    {
        Close();
#ifdef _WIN32
        _Handle = std::exchange(other._Handle, nullptr);
#else
        _Descriptor = std::exchange(other._Descriptor, -1);
#endif
        _Position = std::exchange(other._Position, 0);
        _Window = std::move(other._Window);
        _WindowAt = std::exchange(other._WindowAt, 0);
        _WindowHeld = std::exchange(other._WindowHeld, 0);
        _SystemReads.store(other._SystemReads.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }

    return *this;
}

MCDiskFile::~MCDiskFile()
{
    Close();
}

void MCDiskFile::Close()
{
#ifdef _WIN32
    if (_Handle != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(_Handle));
        _Handle = nullptr;
    }
#else
    if (_Descriptor >= 0)
    {
        // Not retried on EINTR: the descriptor is gone either way.
        ::close(_Descriptor);
        _Descriptor = -1;
    }
#endif
}

size_t MCDiskFile::ReadDirect(uint64_t offset, std::span<uint8_t> buffer) const
{
    size_t done = 0;

    // One call for a whole read (more only past MostInOneCall): the system returns fewer bytes only at the file's end.
    while (done < buffer.size())
    {
        const size_t ask = std::min(buffer.size() - done, MostInOneCall);
        _SystemReads.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
        OVERLAPPED at = At(offset + done);
        DWORD got = 0;

        // Past the end it fails with ERROR_HANDLE_EOF: a short read, as any other failure is.
        if (!ReadFile(static_cast<HANDLE>(_Handle), buffer.data() + done, static_cast<DWORD>(ask), &got, &at) ||
            got == 0)
        {
            break;
        }
#else
        const ssize_t got = ::pread(_Descriptor, buffer.data() + done, ask, static_cast<off_t>(offset + done));

        if (got < 0 && errno == EINTR)
        {
            continue;
        }

        if (got <= 0)
        {
            break;
        }
#endif
        done += static_cast<size_t>(got);

        // Fewer than asked: the file ends there.
        if (static_cast<size_t>(got) < ask)
        {
            break;
        }
    }

    return done;
}

size_t MCDiskFile::ReadThroughWindow(uint32_t offset, std::span<uint8_t> buffer)
{
    const uint64_t end = uint64_t{offset} + buffer.size();

    if (_WindowHeld == 0 || offset < _WindowAt || end > uint64_t{_WindowAt} + _WindowHeld)
    {
        if (_Window == nullptr)
        {
            _Window = std::make_unique<uint8_t[]>(WindowBytes);
        }

        // Filled from the read's own offset: the original's files read on from there.
        _WindowAt = offset;
        _WindowHeld = ReadDirect(offset, std::span(_Window.get(), WindowBytes));
    }

    const size_t skip = offset - _WindowAt;
    const size_t count = std::min(buffer.size(), _WindowHeld - std::min(_WindowHeld, skip));
    std::memcpy(buffer.data(), _Window.get() + skip, count);
    return count;
}

size_t MCDiskFile::Read(std::span<uint8_t> buffer)
{
    return ReadAt(_Position, buffer);
}

size_t MCDiskFile::ReadAt(uint32_t offset, std::span<uint8_t> buffer)
{
    const size_t read = buffer.size() >= WindowBytes ? ReadDirect(offset, buffer) : ReadThroughWindow(offset, buffer);
    _Position = static_cast<uint32_t>(offset + read);
    return read;
}

size_t MCDiskFile::Write(std::span<const uint8_t> data)
{
    return WriteAt(_Position, data);
}

size_t MCDiskFile::WriteAt(uint32_t offset, std::span<const uint8_t> data)
{
    // What the window held may be what this writes over.
    _WindowHeld = 0;
    size_t done = 0;

    while (done < data.size())
    {
        const size_t give = std::min(data.size() - done, MostInOneCall);
#ifdef _WIN32
        OVERLAPPED at = At(uint64_t{offset} + done);
        DWORD put = 0;

        if (!WriteFile(static_cast<HANDLE>(_Handle), data.data() + done, static_cast<DWORD>(give), &put, &at) ||
            put == 0)
        {
            break;
        }
#else
        const ssize_t put =
            ::pwrite(_Descriptor, data.data() + done, give, static_cast<off_t>(uint64_t{offset} + done));

        if (put < 0 && errno == EINTR)
        {
            continue;
        }

        if (put <= 0)
        {
            break;
        }
#endif
        done += static_cast<size_t>(put);
    }

    _Position = static_cast<uint32_t>(offset + done);
    return done;
}

uint32_t MCDiskFile::Length() const
{
#ifdef _WIN32
    LARGE_INTEGER size{};
    return GetFileSizeEx(static_cast<HANDLE>(_Handle), &size) ? static_cast<uint32_t>(size.QuadPart) : 0;
#else
    struct stat status{};
    return ::fstat(_Descriptor, &status) == 0 ? static_cast<uint32_t>(status.st_size) : 0;
#endif
}
