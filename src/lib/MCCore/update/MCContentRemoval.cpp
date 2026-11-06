#include "stdafx.h"
#include "update/MCContentRemoval.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

namespace
{
    /// <summary>
    /// Under the game's lock held exclusive, finishes what an update left under way, so the record says what's on disk
    /// and nothing the player had is left in <c>Redux update/replaced/</c>: a swap a crash stopped midway is finished,
    /// or the previous set put back, its clearing done; a repair's giving up (MCContentSwap::Abandon) is finished, which
    /// also records the new files the swap given up had put in place (one that only added files has no replaced/). One
    /// armed and not begun leaves the record as it is: its staged files go with the update folder.
    /// </summary>
    /// <returns>Empty, or what's left undone: then nothing may go, as the player's originals may still be set aside.</returns>
    std::string FinishUnderWay(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
    {
        const auto begun = IsThere(disk, gameFolder / MCFileSystem::FromUtf8(MCContentSwap::JournalFile));
        const auto repairing = IsThere(disk, gameFolder / MCFileSystem::FromUtf8(MCContentSwap::RepairFile));

        for (const std::expected<bool, std::string>& seen : {std::cref(begun), std::cref(repairing)})
        {
            if (!seen)
            {
                return seen.error();
            }
        }

        if (*begun)
        {
            // A committed swap whose clearing failed says so too: what it set aside is still in replaced/.
            if (const MCContentSwap::MCResult swap = MCContentSwap::Run(disk, gameFolder);
                swap.Outcome == MCContentSwap::MCOutcome::Stuck || !swap.Error.empty())
            {
                return swap.Error;
            }

            return {};
        }

        // The journal becomes repair.json before the files set aside are sorted and the new ones recorded: done again,
        // it ends as it does whole.
        if (*repairing)
        {
            if (const auto given = MCContentSwap::Abandon(disk, gameFolder); !given)
            {
                return given.error();
            }
        }

        return {};
    }

    /// <summary>
    /// Whether the file at <paramref name="path"/> is still <paramref name="file"/> as recorded, or why it can't be told
    /// (it can't be looked at or read: never taken for the player's, nor for the update's).
    /// </summary>
    std::expected<bool, std::string> StillAsRecorded(MCUpdateDisk& disk, const std::filesystem::path& path,
                                                     const MCInstalled::MCFile& file)
    {
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);

        // A link put in a file's place is the player's: removing it could reach outside the folder.
        if (status.type() != std::filesystem::file_type::not_found)
        {
            if (error)
            {
                return std::unexpected(
                    std::format("{} can't be looked at ({})", MCFileSystem::ToUtf8(path), error.message()));
            }

            if (std::filesystem::is_symlink(status))
            {
                return false;
            }
        }

        const MCUpdateDisk::MCStat stat = disk.Stat(path);

        if (!stat.Error.empty())
        {
            return std::unexpected(stat.Error);
        }

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

        if (!hash)
        {
            return std::unexpected(hash.error());
        }

        return *hash == file.Sha256;
    }
}

std::expected<MCContentRemoval::MCRemoved, std::string> MCContentRemoval::Remove(
    MCUpdateDisk& disk, const std::filesystem::path& gameFolder, std::chrono::milliseconds wait)
{
    const std::filesystem::path update = gameFolder / MCFileSystem::FromUtf8(MCContentSwap::UpdateFolder);
    const auto hasUpdate = IsThere(disk, update);

    if (!hasUpdate)
    {
        return std::unexpected(hasUpdate.error());
    }

    if (!*hasUpdate)
    {
        return MCRemoved{};
    }

    std::unique_ptr<MCUpdateLock> lock = MCUpdateLock::Open(gameFolder, MCUpdateLock::GameLock);

    if (lock == nullptr)
    {
        return std::unexpected(std::format("the game's lock in {} can't be opened", MCFileSystem::ToUtf8(update)));
    }

    if (!lock->ExclusiveBy(std::chrono::steady_clock::now() + wait))
    {
        return std::unexpected(std::format("a copy of the game is running from {}: close it, then try again",
                                           MCFileSystem::ToUtf8(gameFolder)));
    }

    if (const std::string unfinished = FinishUnderWay(disk, gameFolder); !unfinished.empty())
    {
        return std::unexpected(unfinished);
    }

    const auto record = MCInstalled::Read(disk, gameFolder);

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

        const auto still = file.Placed ? StillAsRecorded(disk, path, file) : std::expected<bool, std::string>(false);

        // One that can't be told keeps the record (and the update folder), for another try.
        if (!still)
        {
            failed = still.error();
            SDL_Log("Removing the content: %s", failed.c_str());
            continue;
        }

        if (!*still)
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

    // Let go first: its file goes with the update folder. A file that couldn't go keeps the record, for another try.
    lock.reset();

    if (failed.empty() && !disk.Remove(update))
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
