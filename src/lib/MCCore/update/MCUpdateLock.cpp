// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins. The editor builds it too, without SDL (MC_LOCK_NO_SDL, MCEditor.vcxproj).

#include "update/MCUpdateLock.h"

#include "platform/MCUtf8Path.h"

#include <algorithm>
#include <cstdarg>
#include <system_error>
#include <thread>

#ifdef MC_LOCK_NO_SDL
#include <cstdio>
#else
#include <SDL3/SDL_log.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{
    /// <summary>Notes a lock's trouble, printf-style: in the game's log, or on stderr in the editor.</summary>
#if defined(__clang__) || defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    void Note(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
#ifdef MC_LOCK_NO_SDL
        std::vfprintf(stderr, format, arguments);
        std::fputc('\n', stderr);
#else
        SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, arguments);
#endif
        va_end(arguments);
    }

#ifdef _WIN32
    /// <summary>The byte LockFileEx locks: the file holds nothing, and a lock may lie past its end.</summary>
    constexpr DWORD LockedBytes = 1;

    HANDLE HandleOf(intptr_t handle)
    {
        return reinterpret_cast<HANDLE>(handle);
    }

    bool Lock(intptr_t handle, DWORD flags)
    {
        OVERLAPPED at{};
        return LockFileEx(HandleOf(handle), flags, 0, LockedBytes, 0, &at) != FALSE;
    }

    void Unlock(intptr_t handle)
    {
        OVERLAPPED at{};

        if (!UnlockFileEx(HandleOf(handle), 0, LockedBytes, 0, &at))
        {
            Note("Updates: a lock can't be let go (error %lu); it goes as the game ends", GetLastError());
        }
    }
#else
    /// <summary>The lock file's permissions: the owner reads and writes it, others only read (and so lock) it.</summary>
    constexpr mode_t LockFileMode = 0644;

    /// <summary>flock, tried again when a signal cuts it short.</summary>
    bool Flock(intptr_t handle, int operation)
    {
        int result = 0;

        do
        {
            result = flock(static_cast<int>(handle), operation);
        } while (result != 0 && errno == EINTR);

        return result == 0;
    }
#endif

    /// <summary>Tries <paramref name="attempt"/> until it succeeds or <paramref name="until"/> has passed (once at least).</summary>
    template <typename Attempt> bool TryUntil(std::chrono::steady_clock::time_point until, Attempt&& attempt)
    {
        for (;;)
        {
            if (attempt())
            {
                return true;
            }

            const auto now = std::chrono::steady_clock::now();

            if (now >= until)
            {
                return false;
            }

            std::this_thread::sleep_for(
                std::min<std::chrono::steady_clock::duration>(MCUpdateLock::RetryEvery, until - now));
        }
    }
}

std::unique_ptr<MCUpdateLock> MCUpdateLock::Open(const std::filesystem::path& gameFolder, std::string_view name)
{
    const std::filesystem::path path = gameFolder / MCFileSystem::FromUtf8(name);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

#ifdef _WIN32
    // Shared every way, so another copy opens it too; it's never deleted while open, nor at all.
    constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, sharing, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle == INVALID_HANDLE_VALUE)
    {
        // A folder this account can't write: the lock made by the account that can still locks for reading.
        handle =
            CreateFileW(path.c_str(), GENERIC_READ, sharing, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (handle == INVALID_HANDLE_VALUE)
    {
        Note("Updates: the lock %s can't be opened (error %lu)", MCFileSystem::ToUtf8(path).c_str(), GetLastError());
        return nullptr;
    }

    return std::unique_ptr<MCUpdateLock>(new MCUpdateLock(reinterpret_cast<intptr_t>(handle)));
#else
    // Not inherited: a game started again by this one (a restart) locks its own.
    int handle = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, LockFileMode);

    if (handle < 0)
    {
        handle = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    }

    if (handle < 0)
    {
        Note("Updates: the lock %s can't be opened (%s)", path.c_str(), std::strerror(errno));
        return nullptr;
    }

    return std::unique_ptr<MCUpdateLock>(new MCUpdateLock(handle));
#endif
}

MCUpdateLock::~MCUpdateLock()
{
    Release();
#ifdef _WIN32
    CloseHandle(HandleOf(_Handle));
#else
    close(static_cast<int>(_Handle));
#endif
}

bool MCUpdateLock::TryExclusive()
{
    if (_Held == MCHeld::Exclusive)
    {
        return true;
    }

#ifdef _WIN32
    // Windows locks a range once per hold: a shared hold goes before the exclusive one is tried.
    Release();

    if (!Lock(_Handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY))
    {
        return false;
    }
#else
    if (!Flock(_Handle, LOCK_EX | LOCK_NB))
    {
        return false;
    }
#endif

    _Held = MCHeld::Exclusive;
    return true;
}

bool MCUpdateLock::TryShared()
{
    if (_Held == MCHeld::Shared)
    {
        return true;
    }

#ifdef _WIN32
    Release();

    if (!Lock(_Handle, LOCKFILE_FAIL_IMMEDIATELY))
    {
        return false;
    }
#else
    // flock turns an exclusive hold shared in place.
    if (!Flock(_Handle, LOCK_SH | LOCK_NB))
    {
        return false;
    }
#endif

    _Held = MCHeld::Shared;
    return true;
}

bool MCUpdateLock::ExclusiveBy(std::chrono::steady_clock::time_point until)
{
    return TryUntil(until, [this] { return TryExclusive(); });
}

bool MCUpdateLock::SharedBy(std::chrono::steady_clock::time_point until)
{
    return TryUntil(until, [this] { return TryShared(); });
}

void MCUpdateLock::Release()
{
    if (_Held == MCHeld::None)
    {
        return;
    }

#ifdef _WIN32
    Unlock(_Handle);
#else
    if (!Flock(_Handle, LOCK_UN))
    {
        Note("Updates: a lock can't be let go (%s); it goes as the game ends", std::strerror(errno));
    }
#endif
    _Held = MCHeld::None;
}
