#include "stdafx.h"
#include "update/MCContentRemoval.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

#include <thread>

namespace
{
    /// <summary>How often removal tries the game's lock again while copies of the game run.</summary>
    constexpr std::chrono::milliseconds LockRetry{100};

    /// <summary>Takes <paramref name="lock"/> exclusive, trying until <paramref name="wait"/> has passed.</summary>
    bool TakeExclusive(MCUpdateLock& lock, std::chrono::milliseconds wait)
    {
        const auto until = std::chrono::steady_clock::now() + wait;

        while (!lock.TryExclusive())
        {
            if (std::chrono::steady_clock::now() >= until)
            {
                return false;
            }

            std::this_thread::sleep_for(LockRetry);
        }

        return true;
    }

    /// <summary>Whether the file at <paramref name="path"/> is still <paramref name="file"/> as recorded.</summary>
    bool StillAsRecorded(MCUpdateDisk& disk, const std::filesystem::path& path, const MCInstalled::MCFile& file)
    {
        std::error_code error;

        // A link put in a file's place is the player's: removing it could reach outside the folder.
        if (std::filesystem::is_symlink(path, error))
        {
            return false;
        }

        const MCUpdateDisk::MCStat stat = disk.Stat(path);

        if (!stat.Exists || stat.Folder || stat.Size != file.Size)
        {
            return false;
        }

        // Unchanged since it was recorded, it keeps the recorded hash; otherwise it's hashed again.
        if (file.Written != 0 && stat.Written == file.Written)
        {
            return true;
        }

        const auto hash = disk.Hash(path);
        return hash && *hash == file.Sha256;
    }
}

std::expected<MCContentRemoval::MCRemoved, std::string> MCContentRemoval::Remove(
    MCUpdateDisk& disk, const std::filesystem::path& gameFolder, std::chrono::milliseconds wait)
{
    const std::filesystem::path update = gameFolder / MCFileSystem::FromUtf8(MCContentSwap::UpdateFolder);

    if (!disk.Stat(update).Exists)
    {
        return MCRemoved{};
    }

    std::unique_ptr<MCUpdateLock> lock = MCUpdateLock::Open(gameFolder, MCUpdateLock::GameLock);

    if (lock == nullptr)
    {
        return std::unexpected(std::format("the game's lock in {} can't be opened", MCFileSystem::ToUtf8(update)));
    }

    if (!TakeExclusive(*lock, wait))
    {
        return std::unexpected(std::format("a copy of the game is running from {}: close it, then try again",
                                           MCFileSystem::ToUtf8(gameFolder)));
    }

    // A swap a crash stopped midway is finished, or the previous set put back, so the record says what's on disk. One
    // armed and not begun leaves the record as it is: its staged files go with the update folder.
    if (disk.Stat(gameFolder / MCFileSystem::FromUtf8(MCContentSwap::JournalFile)).Exists)
    {
        if (const MCContentSwap::MCResult swap = MCContentSwap::Run(disk, gameFolder);
            swap.Outcome == MCContentSwap::MCOutcome::Stuck)
        {
            return std::unexpected(swap.Error);
        }
    }

    const auto record = MCInstalled::Read(gameFolder);

    if (!record)
    {
        return std::unexpected(record.error());
    }

    MCRemoved removed;
    std::string failed;
    std::set<std::filesystem::path> folders;

    for (const auto& [folded, file] : record->Files)
    {
        const std::filesystem::path listed = MCFileSystem::FromUtf8(file.Path);
        const std::filesystem::path path = gameFolder / listed;

        if (!file.Placed || !StillAsRecorded(disk, path, file))
        {
            removed.Kept += disk.Stat(path).Exists ? 1 : 0;
            continue;
        }

        if (!disk.Remove(path))
        {
            failed = std::format("{} can't be removed", MCFileSystem::ToUtf8(path));
            SDL_Log("Removing the content: %s", failed.c_str());
            continue;
        }

        removed.Removed++;

        for (std::filesystem::path folder = listed.parent_path(); !folder.empty(); folder = folder.parent_path())
        {
            folders.insert(gameFolder / folder);
        }
    }

    // Let go first: its file goes with the update folder.
    lock.reset();

    if (!disk.Remove(update))
    {
        failed = std::format("{} can't be removed", MCFileSystem::ToUtf8(update));
        SDL_Log("Removing the content: %s", failed.c_str());
    }

    // A folder sorts before those in it: backwards, each is emptied before its own folder is tried.
    for (auto folder = folders.rbegin(); folder != folders.rend(); ++folder)
    {
        disk.RemoveIfEmpty(*folder);
    }

    if (!failed.empty())
    {
        return std::unexpected(failed);
    }

    return removed;
}
