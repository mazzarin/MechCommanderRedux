// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "lib/MCDiskFile.h"
#include "lib/MCHeldFile.h"

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

#ifndef _WIN32
    /// <summary>The permissions a new file gets before the process's mask, as a stream's.</summary>
    constexpr mode_t NewFilePermissions = 0666;
#endif
}

std::optional<MCDiskFile> MCDiskFile::Open(const std::filesystem::path& path, MCDiskFileMode mode,
                                           MCDiskFileReads reads)
{
    const bool create = mode == MCDiskFileMode::Create;
    MCDiskFile file;
    file._Reads = reads;
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
        _Reads = other._Reads;
        _Position = std::exchange(other._Position, 0);
        _Buffer = std::move(other._Buffer);
        _BufferAt = std::exchange(other._BufferAt, 0);
        _BufferHeld = std::exchange(other._BufferHeld, 0);
        _FillBytes = std::exchange(other._FillBytes, FirstFillBytes);
        _Filled = std::exchange(other._Filled, false);
        _ReadEnd = std::exchange(other._ReadEnd, 0);
        _Whole = std::exchange(other._Whole, false);
        _SystemReads.store(other._SystemReads.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        _Held = std::move(other._Held);
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

size_t MCDiskFile::ReadDirect(uint32_t offset, std::span<uint8_t> buffer) const
{
    // Port-only: the file's bytes held in memory, unless the hold failed.
    if (_Held != nullptr)
    {
        if (const std::optional<size_t> held = _Held->ReadAt(offset, buffer); held.has_value())
        {
            return *held;
        }
    }

    size_t done = 0;

    // One call for a whole read (more only past MostInOneCall): the system returns fewer bytes only at the file's end.
    while (done < buffer.size())
    {
        const size_t ask = std::min(buffer.size() - done, MostInOneCall);
        _SystemReads.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(uint64_t{offset} + done);
        at.OffsetHigh = static_cast<DWORD>((uint64_t{offset} + done) >> 32);
        DWORD got = 0;

        // Past the end it fails with ERROR_HANDLE_EOF: a short read, as any other failure is.
        if (!ReadFile(static_cast<HANDLE>(_Handle), buffer.data() + done, static_cast<DWORD>(ask), &got, &at) ||
            got == 0)
        {
            break;
        }
#else
        const ssize_t got =
            ::pread(_Descriptor, buffer.data() + done, ask, static_cast<off_t>(uint64_t{offset} + done));

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

void MCDiskFile::Fill(uint32_t offset, size_t bytes)
{
    if (_Buffer.size() < bytes)
    {
        _Buffer.resize(bytes);
    }

    _BufferAt = offset;
    _BufferHeld = ReadDirect(offset, std::span(_Buffer.data(), bytes));
    _Filled = true;
    _ReadEnd = uint64_t{offset} + _BufferHeld;
}

void MCDiskFile::EmptyBuffer()
{
    _BufferHeld = 0;
    _FillBytes = FirstFillBytes;
    _Filled = false;
    _Whole = false;
}

size_t MCDiskFile::ReadBuffered(uint32_t offset, std::span<uint8_t> buffer)
{
    const uint64_t heldEnd = uint64_t{_BufferAt} + _BufferHeld;
    size_t copied = 0;

    // From the buffer: all of it, or its start, the rest carrying on from the buffer's end.
    if (_BufferHeld > 0 && offset >= _BufferAt && offset < heldEnd)
    {
        const size_t skip = offset - _BufferAt;
        copied = std::min(buffer.size(), _BufferHeld - skip);

        if (copied > 0)
        {
            std::memcpy(buffer.data(), _Buffer.data() + skip, copied);
        }
    }

    const auto from = static_cast<uint32_t>(offset + copied);

    // All of it; or a file held whole ends there.
    if (copied == buffer.size() || (_Whole && from >= heldEnd))
    {
        return copied;
    }

    const std::span<uint8_t> rest = buffer.subspan(copied);

    if (!_Filled)
    {
        // A small file read whole at its first fill.
        if (const uint32_t length = Length(); length <= MostFillBytes)
        {
            Fill(0, length);
            _Whole = true;
            const size_t count = from < _BufferHeld ? std::min(rest.size(), _BufferHeld - from) : 0;

            if (count > 0)
            {
                std::memcpy(rest.data(), _Buffer.data() + from, count);
            }

            return copied + count;
        }
    }

    // A fill that carries on where the last read ended doubles; a jump goes back to the first size.
    const size_t fill = _Filled && from == _ReadEnd ? std::min(_FillBytes * 2, MostFillBytes) : FirstFillBytes;

    if (rest.size() >= fill)
    {
        const size_t got = ReadDirect(from, rest);
        _Filled = true;
        _ReadEnd = uint64_t{from} + got;
        return copied + got;
    }

    _FillBytes = fill;
    Fill(from, fill);
    const size_t count = std::min(rest.size(), _BufferHeld);

    if (count > 0)
    {
        std::memcpy(rest.data(), _Buffer.data(), count);
    }

    return copied + count;
}

size_t MCDiskFile::Read(std::span<uint8_t> buffer)
{
    return ReadAt(_Position, buffer);
}

size_t MCDiskFile::ReadAt(uint32_t offset, std::span<uint8_t> buffer)
{
    const size_t read = _Reads == MCDiskFileReads::Buffered ? ReadBuffered(offset, buffer) : ReadDirect(offset, buffer);
    _Position = static_cast<uint32_t>(offset + read);
    return read;
}

size_t MCDiskFile::Write(std::span<const uint8_t> data)
{
    return WriteAt(_Position, data);
}

size_t MCDiskFile::WriteAt(uint32_t offset, std::span<const uint8_t> data)
{
    // What the buffer held, or the hold, may be what this writes over.
    EmptyBuffer();
    _Held.reset();
    size_t done = 0;

    while (done < data.size())
    {
        const size_t give = std::min(data.size() - done, MostInOneCall);
#ifdef _WIN32
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(uint64_t{offset} + done);
        at.OffsetHigh = static_cast<DWORD>((uint64_t{offset} + done) >> 32);
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
