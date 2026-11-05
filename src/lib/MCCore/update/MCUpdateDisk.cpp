// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "update/MCUpdateDisk.h"

#include <SDL3/SDL_log.h>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <chrono>
#include <thread>
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    constexpr std::string_view TemporarySuffix = ".writing";
#ifndef _WIN32
    constexpr int64_t NanosecondsPerSecond = 1'000'000'000;
#endif

#ifdef _WIN32
    /// <summary>A move of a file another program holds: tried this many times, this far apart (a second in all).</summary>
    constexpr int HeldTries = 10;
    constexpr std::chrono::milliseconds HeldWait{100};
#endif

    std::string Shown(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

#ifdef _WIN32
    /// <summary>Flushes the file's bytes to the disk.</summary>
    bool FlushPath(const std::filesystem::path& path)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            SDL_Log("Updates: %s can't be opened to flush (%lu)", Shown(path).c_str(), GetLastError());
            return false;
        }

        const bool flushed = FlushFileBuffers(file) != 0;

        if (!flushed)
        {
            SDL_Log("Updates: %s can't be flushed (%lu)", Shown(path).c_str(), GetLastError());
        }

        CloseHandle(file);
        return flushed;
    }
#else
    /// <summary>
    /// Flushes a file's bytes, or a folder's entries, to the disk itself: on the Mac F_FULLFSYNC, as fsync leaves them
    /// in the drive's cache.
    /// </summary>
    bool FlushPath(const std::filesystem::path& path)
    {
        const int descriptor = open(path.c_str(), O_RDONLY);

        if (descriptor < 0)
        {
            SDL_Log("Updates: %s can't be opened to flush (%s)", Shown(path).c_str(), std::strerror(errno));
            return false;
        }

#ifdef F_FULLFSYNC
        bool flushed = fcntl(descriptor, F_FULLFSYNC) == 0 || fsync(descriptor) == 0;
#else
        bool flushed = fsync(descriptor) == 0;
#endif

        if (!flushed)
        {
            SDL_Log("Updates: %s can't be flushed (%s)", Shown(path).c_str(), std::strerror(errno));
        }

        close(descriptor);
        return flushed;
    }
#endif

#ifdef _WIN32
    /// <summary>
    /// Moves a file with MoveFileExW's <paramref name="flags"/>, written through: the move is on the disk when it
    /// returns, as NTFS offers no flush of a folder's entries. A file another program has open can't be moved; a virus
    /// scanner holds one briefly, so it's tried again.
    /// </summary>
    bool MoveWithFlags(const std::filesystem::path& from, const std::filesystem::path& to, DWORD flags)
    {
        for (int attempt = 1;; ++attempt)
        {
            if (MoveFileExW(from.c_str(), to.c_str(), flags | MOVEFILE_WRITE_THROUGH) != 0)
            {
                return true;
            }

            const DWORD error = GetLastError();
            const bool held =
                error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_ACCESS_DENIED;

            if (!held || attempt >= HeldTries)
            {
                SDL_Log("Updates: %s can't be moved to %s (%lu, try %d)", Shown(from).c_str(), Shown(to).c_str(), error,
                        attempt);
                return false;
            }

            std::this_thread::sleep_for(HeldWait);
        }
    }
#endif

    /// <summary>A stat that couldn't be made: why, naming <paramref name="path"/>.</summary>
    MCUpdateDisk::MCStat Unseen(const std::filesystem::path& path, const std::error_code& error)
    {
        return {.Error = std::format("{} can't be looked at ({})", Shown(path), error.message())};
    }

    class MCDisk final : public MCUpdateDisk
    {
    public:
        MCStat Stat(const std::filesystem::path& path) override
        {
#ifndef _WIN32
            // One stat call, not three (the swap looks at each file several times): its time as
            // std::filesystem::last_write_time gives it, nanoseconds since 1970.
            struct stat info{};

            if (::stat(path.c_str(), &info) != 0)
            {
                // Missing only when the system says so: any other error (no access, a failing drive) is told as one.
                if (errno == ENOENT || errno == ENOTDIR)
                {
                    return {};
                }

                return Unseen(path, std::error_code(errno, std::generic_category()));
            }

#ifdef __APPLE__
            const timespec& written = info.st_mtimespec;
#else
            const timespec& written = info.st_mtim;
#endif
            const bool folder = S_ISDIR(info.st_mode);
            return {.Exists = true,
                    .Folder = folder,
                    .Size = folder ? 0 : static_cast<uint64_t>(info.st_size),
                    .Written = static_cast<int64_t>(written.tv_sec) * NanosecondsPerSecond + written.tv_nsec};
#else
            std::error_code error;
            const std::filesystem::file_status status = std::filesystem::status(path, error);

            // Missing only when the system says so: any other error (no access, a failing drive) is told as one.
            if (status.type() == std::filesystem::file_type::not_found)
            {
                return {};
            }

            if (error || !std::filesystem::exists(status))
            {
                return Unseen(path, error);
            }

            MCStat stat{.Exists = true, .Folder = std::filesystem::is_directory(status)};

            if (!stat.Folder)
            {
                stat.Size = std::filesystem::file_size(path, error);

                if (error)
                {
                    return Unseen(path, error);
                }
            }

            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);

            if (error)
            {
                return Unseen(path, error);
            }

            stat.Written = static_cast<int64_t>(written.time_since_epoch().count());
            return stat;
#endif
        }

        std::expected<MCSha256::MCDigest, std::string> Hash(const std::filesystem::path& path) override
        {
            return MCSha256::OfFile(path);
        }

        std::expected<std::string, std::string> ReadText(const std::filesystem::path& path) override
        {
            std::ifstream file(path, std::ios::binary);

            if (!file)
            {
                return std::unexpected(Shown(path) + " can't be opened");
            }

            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            if (file.bad())
            {
                return std::unexpected(Shown(path) + " can't be read");
            }

            return text;
        }

        std::expected<std::vector<std::string>, std::string> Files(const std::filesystem::path& folder) override
        {
            std::vector<std::string> files;
            std::error_code error;

            if (!std::filesystem::is_directory(std::filesystem::symlink_status(folder, error)))
            {
                return files;
            }

            // Links aren't followed: one is listed as a file is.
            for (std::filesystem::recursive_directory_iterator entry(folder, error), end; !error && entry != end;
                 entry.increment(error))
            {
                const std::filesystem::file_status status = entry->symlink_status(error);

                if (!error && !std::filesystem::is_directory(status))
                {
                    const std::u8string relative = entry->path().lexically_relative(folder).generic_u8string();
                    files.emplace_back(relative.begin(), relative.end());
                }
            }

            if (error)
            {
                return std::unexpected(Shown(folder) + " can't be listed (" + error.message() + ")");
            }

            return files;
        }

        bool MakeFolders(const std::filesystem::path& path) override
        {
            std::error_code error;
            std::filesystem::create_directories(path, error);

            if (error)
            {
                SDL_Log("Updates: %s can't be made (%s)", Shown(path).c_str(), error.message().c_str());
            }

            return !error;
        }

        bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
#ifdef _WIN32
            return MoveWithFlags(from, to, MOVEFILE_REPLACE_EXISTING);
#else
            std::error_code error;
            std::filesystem::rename(from, to, error);

            if (error)
            {
                SDL_Log("Updates: %s can't be moved to %s (%s)", Shown(from).c_str(), Shown(to).c_str(),
                        error.message().c_str());
            }

            return !error;
#endif
        }

        bool Move(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
#ifdef _WIN32
            // Without MOVEFILE_REPLACE_EXISTING: a file at to fails it (ERROR_ALREADY_EXISTS), never tried again.
            constexpr DWORD replacingNothing = 0;
            return MoveWithFlags(from, to, replacingNothing);
#else
            // RENAME_EXCL: one step, failing with EEXIST when something is at to.
            if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) != 0)
            {
                SDL_Log("Updates: %s can't be moved to %s (%s)", Shown(from).c_str(), Shown(to).c_str(),
                        std::strerror(errno));
                return false;
            }

            return true;
#endif
        }

        bool Remove(const std::filesystem::path& path) override
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);

            if (error)
            {
                SDL_Log("Updates: %s can't be deleted (%s)", Shown(path).c_str(), error.message().c_str());
            }

            return !error;
        }

        bool RemoveIfEmpty(const std::filesystem::path& path) override
        {
            std::error_code error;

            // remove deletes an empty folder only, but a file too: a file or a link is never taken for one.
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(path, error)))
            {
                return false;
            }

            return std::filesystem::remove(path, error);
        }

        bool WriteFlushed(const std::filesystem::path& path, std::string_view bytes) override
        {
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                file.close();

                if (!file)
                {
                    SDL_Log("Updates: %s can't be written", Shown(path).c_str());
                    return false;
                }
            }

            return FlushPath(path);
        }

        bool FlushFile(const std::filesystem::path& path) override { return FlushPath(path); }

        bool FlushFolder([[maybe_unused]] const std::filesystem::path& path) override
        {
#ifdef _WIN32
            // Moves are written through (Rename), and NTFS logs a folder's entries as it changes them.
            return true;
#else
            return FlushPath(path);
#endif
        }
    };
}

std::unique_ptr<MCUpdateDisk> MakeDisk()
{
    return std::make_unique<MCDisk>();
}

std::expected<bool, std::string> IsThere(MCUpdateDisk& disk, const std::filesystem::path& path)
{
    const MCUpdateDisk::MCStat stat = disk.Stat(path);

    if (!stat.Error.empty())
    {
        return std::unexpected(stat.Error);
    }

    return stat.Exists;
}

bool WriteDurably(MCUpdateDisk& disk, const std::filesystem::path& path, std::string_view bytes)
{
    std::filesystem::path temporary = path;
    temporary += std::string(TemporarySuffix);
    return disk.MakeFolders(path.parent_path()) && disk.WriteFlushed(temporary, bytes) &&
           disk.Rename(temporary, path) && disk.FlushFolder(path.parent_path());
}
