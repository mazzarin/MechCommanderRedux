// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "update/MCUpdateDisk.h"

#include "platform/MCUtf8Path.h"

#include <SDL3/SDL_log.h>
#include <atomic>
#include <format>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <system_error>
#include <utility>

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
    /// <summary>An offset's high half, as OVERLAPPED takes it.</summary>
    constexpr int DwordBits = 32;
#endif

#ifdef _WIN32
    /// <summary>Flushes the file's bytes to the disk.</summary>
    bool FlushPath(const std::filesystem::path& path)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            SDL_Log("Updates: %s can't be opened to flush (%lu)", MCFileSystem::ToUtf8(path).c_str(), GetLastError());
            return false;
        }

        const bool flushed = FlushFileBuffers(file) != 0;

        if (!flushed)
        {
            SDL_Log("Updates: %s can't be flushed (%lu)", MCFileSystem::ToUtf8(path).c_str(), GetLastError());
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
            SDL_Log("Updates: %s can't be opened to flush (%s)", MCFileSystem::ToUtf8(path).c_str(),
                    std::strerror(errno));
            return false;
        }

#ifdef F_FULLFSYNC
        bool flushed = fcntl(descriptor, F_FULLFSYNC) == 0 || fsync(descriptor) == 0;
#else
        bool flushed = fsync(descriptor) == 0;
#endif

        if (!flushed)
        {
            SDL_Log("Updates: %s can't be flushed (%s)", MCFileSystem::ToUtf8(path).c_str(), std::strerror(errno));
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
                SDL_Log("Updates: %s can't be moved to %s (%lu, try %d)", MCFileSystem::ToUtf8(from).c_str(),
                        MCFileSystem::ToUtf8(to).c_str(), error, attempt);
                return false;
            }

            std::this_thread::sleep_for(HeldWait);
        }
    }
#endif

    /// <summary>Moves a file with the system's rename, which replaces what's at <paramref name="to"/>.</summary>
    bool RenameReplacing(const std::filesystem::path& from, const std::filesystem::path& to)
    {
#ifdef _WIN32
        return MoveWithFlags(from, to, MOVEFILE_REPLACE_EXISTING);
#else
        std::error_code error;
        std::filesystem::rename(from, to, error);

        if (error)
        {
            SDL_Log("Updates: %s can't be moved to %s (%s)", MCFileSystem::ToUtf8(from).c_str(),
                    MCFileSystem::ToUtf8(to).c_str(), error.message().c_str());
        }

        return !error;
#endif
    }

    /// <summary>
    /// A move where nothing may be, for a volume whose system can't refuse one where something is (on the Mac, SMB, NFS
    /// and FAT volumes lack RENAME_EXCL): <paramref name="to"/> looked at first, then the rename. The two steps aren't
    /// one, but the swap holds the game's lock exclusive while it moves, so no copy of the game puts a file there in
    /// between; anything else must have been there already.
    /// </summary>
    bool MoveLookingFirst(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(to, error);

        if (status.type() != std::filesystem::file_type::not_found)
        {
            SDL_Log("Updates: %s can't be moved to %s (%s)", MCFileSystem::ToUtf8(from).c_str(),
                    MCFileSystem::ToUtf8(to).c_str(), error ? error.message().c_str() : "something is there already");
            return false;
        }

        return RenameReplacing(from, to);
    }

    /// <summary>A stat that couldn't be made: why, naming <paramref name="path"/>.</summary>
    MCUpdateDisk::MCStat Unseen(const std::filesystem::path& path, const std::error_code& error)
    {
        return {.Error = std::format("{} can't be looked at ({})", MCFileSystem::ToUtf8(path), error.message())};
    }

    class MCDisk final : public MCUpdateDisk
    {
    public:
        explicit MCDisk(MCMoves moves) : _Moves(moves) {}

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

        std::expected<bool, std::string> IsLink(const std::filesystem::path& path) override
        {
#ifdef _WIN32
            const DWORD attributes = GetFileAttributesW(path.c_str());

            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = GetLastError();

                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return false;
                }

                return std::unexpected(std::format("{} can't be looked at ({})", MCFileSystem::ToUtf8(path), error));
            }

            return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
            struct stat info{};

            if (::lstat(path.c_str(), &info) != 0)
            {
                if (errno == ENOENT || errno == ENOTDIR)
                {
                    return false;
                }

                return std::unexpected(
                    std::format("{} can't be looked at ({})", MCFileSystem::ToUtf8(path), std::strerror(errno)));
            }

            return S_ISLNK(info.st_mode);
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
                return std::unexpected(MCFileSystem::ToUtf8(path) + " can't be opened");
            }

            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            if (file.bad())
            {
                return std::unexpected(MCFileSystem::ToUtf8(path) + " can't be read");
            }

            return text;
        }

        std::expected<std::vector<std::string>, std::string> Files(const std::filesystem::path& folder) override
        {
            std::vector<std::string> files;
            std::error_code error;
            const std::filesystem::file_status status = std::filesystem::symlink_status(folder, error);

            // None only when the system says it isn't there: a folder that can't be looked at isn't an empty one.
            if (status.type() == std::filesystem::file_type::not_found)
            {
                return files;
            }

            if (error)
            {
                return std::unexpected(MCFileSystem::ToUtf8(folder) + " can't be looked at (" + error.message() + ")");
            }

            if (!std::filesystem::is_directory(status))
            {
                return std::unexpected(MCFileSystem::ToUtf8(folder) + " isn't a folder");
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
                return std::unexpected(MCFileSystem::ToUtf8(folder) + " can't be listed (" + error.message() + ")");
            }

            return files;
        }

        bool MakeFolders(const std::filesystem::path& path) override
        {
            std::error_code error;
            std::filesystem::create_directories(path, error);

            if (error)
            {
                SDL_Log("Updates: %s can't be made (%s)", MCFileSystem::ToUtf8(path).c_str(), error.message().c_str());
            }

            return !error;
        }

        bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            return RenameReplacing(from, to);
        }

        bool Move(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            if (_Moves == MCMoves::LookingFirst)
            {
                return MoveLookingFirst(from, to);
            }

#ifdef _WIN32
            // Without MOVEFILE_REPLACE_EXISTING: a file at to fails it (ERROR_ALREADY_EXISTS), never tried again.
            constexpr DWORD replacingNothing = 0;
            return MoveWithFlags(from, to, replacingNothing);
#else
            // RENAME_EXCL: one step, failing with EEXIST when something is at to.
            if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0)
            {
                return true;
            }

            const int error = errno;

            // A volume without it (SMB, NFS, FAT): every move there looks first.
            if (error == ENOTSUP || error == EINVAL)
            {
                if (!ToldLookingFirst.exchange(true))
                {
                    SDL_Log("Updates: %s's volume can't move a file only where nothing is (%s): moves there look first",
                            MCFileSystem::ToUtf8(to.parent_path()).c_str(), std::strerror(error));
                }

                return MoveLookingFirst(from, to);
            }

            SDL_Log("Updates: %s can't be moved to %s (%s)", MCFileSystem::ToUtf8(from).c_str(),
                    MCFileSystem::ToUtf8(to).c_str(), std::strerror(error));
            return false;
#endif
        }

        bool Remove(const std::filesystem::path& path) override
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);

            if (error)
            {
                SDL_Log("Updates: %s can't be deleted (%s)", MCFileSystem::ToUtf8(path).c_str(),
                        error.message().c_str());
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
            // Made anew, never through a link planted at its name (MCNewFile).
            auto file = MCNewFile::Create(path);

            if (!file)
            {
                SDL_Log("Updates: %s can't be written (%s)", MCFileSystem::ToUtf8(path).c_str(), file.error().c_str());
                return false;
            }

            if (!file->WriteAt(0, bytes) || !file->Close())
            {
                return false;
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

        std::expected<uint64_t, std::string> FreeBytes(const std::filesystem::path& path) override
        {
            std::error_code error;
            const std::filesystem::space_info space = std::filesystem::space(path, error);

            if (error)
            {
                return std::unexpected("the free space on " + MCFileSystem::ToUtf8(path) + "'s disk can't be read (" +
                                       error.message() + ")");
            }

            return space.available;
        }

    private:
        MCMoves _Moves;
#ifndef _WIN32
        /// <summary>Whether a volume without RENAME_EXCL was logged (once a run).</summary>
        static inline std::atomic<bool> ToldLookingFirst = false;
#endif
    };
}

std::unique_ptr<MCUpdateDisk> MakeDisk(MCMoves moves)
{
    return std::make_unique<MCDisk>(moves);
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

std::expected<void, std::string> MCNoLinks::Check(const std::filesystem::path& path)
{
    const std::filesystem::path relative = path.lexically_normal().lexically_relative(_Game.lexically_normal());

    if (relative.empty() || *relative.begin() == "..")
    {
        return std::unexpected(std::format("{} isn't in the game's folder", MCFileSystem::ToUtf8(path)));
    }

    std::filesystem::path at = _Game;

    for (const std::filesystem::path& part : relative)
    {
        at /= part;
        const auto link = _Disk.IsLink(at);

        if (!link)
        {
            return std::unexpected(link.error());
        }

        if (*link)
        {
            return std::unexpected(std::format("{} is a link, which updates don't follow", MCFileSystem::ToUtf8(at)));
        }
    }

    return {};
}

std::expected<MCNewFile, std::string> MCNewFile::Create(const std::filesystem::path& path)
{
    const std::string shown = MCFileSystem::ToUtf8(path);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);

    if (status.type() != std::filesystem::file_type::not_found)
    {
        if (error)
        {
            return std::unexpected(
                std::format("{} can't be written: it can't be looked at ({})", shown, error.message()));
        }

        if (std::filesystem::is_directory(status))
        {
            return std::unexpected(std::format("{} can't be written: a folder is there", shown));
        }

        // The file or link itself goes, never what a link points to.
        if (!std::filesystem::remove(path, error))
        {
            return std::unexpected(
                std::format("{} can't be written: what's there can't be deleted ({})", shown, error.message()));
        }
    }

#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(std::format("{} can't be written ({})", shown, GetLastError()));
    }

    return MCNewFile(path, reinterpret_cast<intptr_t>(file));
#else
    constexpr mode_t ReadableByAll = 0644;
    const int descriptor = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, ReadableByAll);

    if (descriptor < 0)
    {
        return std::unexpected(std::format("{} can't be written ({})", shown, std::strerror(errno)));
    }

    return MCNewFile(path, descriptor);
#endif
}

MCNewFile::MCNewFile(MCNewFile&& other) noexcept
    : _Path(std::move(other._Path)), _Handle(std::exchange(other._Handle, Closed))
{
}

MCNewFile::~MCNewFile()
{
    Close();
}

bool MCNewFile::WriteAt(uint64_t at, std::span<const char> bytes)
{
    while (!bytes.empty())
    {
#ifdef _WIN32
        OVERLAPPED where{};
        where.Offset = static_cast<DWORD>(at);
        where.OffsetHigh = static_cast<DWORD>(at >> DwordBits);
        const auto count = static_cast<DWORD>(std::min<size_t>(bytes.size(), MAXDWORD));
        DWORD written = 0;
        const bool done = WriteFile(reinterpret_cast<HANDLE>(_Handle), bytes.data(), count, &written, &where) != 0;
#else
        const ssize_t wrote = pwrite(static_cast<int>(_Handle), bytes.data(), bytes.size(), static_cast<off_t>(at));

        if (wrote < 0 && errno == EINTR)
        {
            continue;
        }

        const bool done = wrote > 0;
        const auto written = static_cast<size_t>(std::max<ssize_t>(wrote, 0));
#endif

        if (!done || written == 0)
        {
            SDL_Log("Updates: %s can't be written at %llu", MCFileSystem::ToUtf8(_Path).c_str(),
                    static_cast<unsigned long long>(at));
            return false;
        }

        at += written;
        bytes = bytes.subspan(written);
    }

    return true;
}

bool MCNewFile::ReadAt(uint64_t at, std::span<char> bytes)
{
    while (!bytes.empty())
    {
#ifdef _WIN32
        OVERLAPPED where{};
        where.Offset = static_cast<DWORD>(at);
        where.OffsetHigh = static_cast<DWORD>(at >> DwordBits);
        const auto count = static_cast<DWORD>(std::min<size_t>(bytes.size(), MAXDWORD));
        DWORD read = 0;
        const bool done = ReadFile(reinterpret_cast<HANDLE>(_Handle), bytes.data(), count, &read, &where) != 0;
#else
        const ssize_t got = pread(static_cast<int>(_Handle), bytes.data(), bytes.size(), static_cast<off_t>(at));

        if (got < 0 && errno == EINTR)
        {
            continue;
        }

        const bool done = got > 0;
        const auto read = static_cast<size_t>(std::max<ssize_t>(got, 0));
#endif

        // Past what's written there's nothing to read: the caller asked for what it wrote.
        if (!done || read == 0)
        {
            return false;
        }

        at += read;
        bytes = bytes.subspan(read);
    }

    return true;
}

bool MCNewFile::Close()
{
    if (_Handle == Closed)
    {
        return true;
    }

#ifdef _WIN32
    const bool closed = CloseHandle(reinterpret_cast<HANDLE>(std::exchange(_Handle, Closed))) != 0;
#else
    const bool closed = close(static_cast<int>(std::exchange(_Handle, Closed))) == 0;
#endif

    if (!closed)
    {
        SDL_Log("Updates: %s can't be closed: what was written may not all be there",
                MCFileSystem::ToUtf8(_Path).c_str());
    }

    return closed;
}

bool WriteDurably(MCUpdateDisk& disk, const std::filesystem::path& within, const std::filesystem::path& path,
                  std::string_view bytes)
{
    std::filesystem::path temporary = path;
    temporary += std::string(TemporarySuffix);
    MCNoLinks noLinks(disk, within);

    // No link on its way below the folder (Redux update put in as one, say) takes the write elsewhere: looked at before
    // its folders are made, so none is made through one, and again after. The file itself is made anew, never through a
    // link (WriteFlushed), and the rename replaces a link at its name rather than follow it.
    const auto clear = [&]
    {
        const auto checked = noLinks.Check(path.parent_path());

        if (!checked)
        {
            SDL_Log("Updates: %s isn't written: %s", MCFileSystem::ToUtf8(path).c_str(), checked.error().c_str());
        }

        return checked.has_value();
    };

    return clear() && disk.MakeFolders(path.parent_path()) && clear() && disk.WriteFlushed(temporary, bytes) &&
           disk.Rename(temporary, path) && disk.FlushFolder(path.parent_path());
}
