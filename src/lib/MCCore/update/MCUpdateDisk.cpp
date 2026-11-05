// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "update/MCUpdateDisk.h"

#include <SDL3/SDL_log.h>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    constexpr std::string_view TemporarySuffix = ".writing";

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

    class MCDisk final : public MCUpdateDisk
    {
    public:
        MCStat Stat(const std::filesystem::path& path) override
        {
            std::error_code error;
            const std::filesystem::file_status status = std::filesystem::status(path, error);

            if (error || !std::filesystem::exists(status))
            {
                return {};
            }

            MCStat stat{.Exists = true, .Folder = std::filesystem::is_directory(status)};

            if (!stat.Folder)
            {
                stat.Size = std::filesystem::file_size(path, error);
            }

            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);
            stat.Written = error ? 0 : static_cast<int64_t>(written.time_since_epoch().count());
            return stat;
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
            // Write-through: the move is on the disk when it returns, as NTFS offers no flush of a folder's entries.
            if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
            {
                SDL_Log("Updates: %s can't be moved to %s (%lu)", Shown(from).c_str(), Shown(to).c_str(),
                        GetLastError());
                return false;
            }

            return true;
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

bool WriteDurably(MCUpdateDisk& disk, const std::filesystem::path& path, std::string_view bytes)
{
    std::filesystem::path temporary = path;
    temporary += std::string(TemporarySuffix);
    return disk.MakeFolders(path.parent_path()) && disk.WriteFlushed(temporary, bytes) &&
           disk.Rename(temporary, path) && disk.FlushFolder(path.parent_path());
}
