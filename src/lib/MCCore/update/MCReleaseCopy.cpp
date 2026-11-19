#include "stdafx.h"
#include "update/MCReleaseCopy.h"
#include "platform/MCFileSystem.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"

#if defined(__APPLE__)
#include <cstring>
#include <sys/clonefile.h>
#endif

using namespace MCUpdateJson;

namespace
{
    constexpr std::string_view ReleaseKey = "release";
    constexpr std::string_view FilesKey = "files";
    constexpr std::string_view LackingKey = "lacking";
    constexpr std::string_view PathKey = "path";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr std::string_view SizeKey = "size";
    constexpr std::string_view WrittenKey = "written";
    /// <summary>A file being filled is written here first, beside its place, and renamed in once checked.</summary>
    constexpr std::string_view FillingSuffix = ".filling";

    constexpr std::string_view ArchiveKind = ".FST";
    constexpr std::string_view ObjectsFolder = "DATA/OBJECTS/";
    constexpr std::string_view SavesFolder = "DATA/SAVEGAME/";
    constexpr std::string_view MissionKind = ".MPK";

    std::filesystem::path RecordIn(const std::filesystem::path& gameFolder)
    {
        return MCReleaseCopy::FolderIn(gameFolder) / MCFileSystem::FromUtf8(MCReleaseCopy::RecordName);
    }

    MCReleaseCopy::MCRecord ReadRecord(const MCJson& json)
    {
        MCReleaseCopy::MCRecord record{.Release = Count(json, ReleaseKey)};

        for (const MCJson& file : Array(json, FilesKey))
        {
            MCReleaseCopy::MCHeld held{.Path = Text(file, PathKey),
                                       .Sha256 = Digest(file, Sha256Key),
                                       .Size = Count(file, SizeKey),
                                       .Written = Integer(file, WrittenKey)};

            if (!MCReleaseCopy::Holds(held.Path) || !MCContentList::PathProblem(held.Path).empty())
            {
                Fail("holds {}, which isn't one of its files", held.Path);
            }

            std::string folded = MCContentList::FoldedPath(held.Path);
            record.Files.insert_or_assign(std::move(folded), std::move(held));
        }

        record.Lacking = Strings(json, LackingKey);
        return record;
    }

    std::string RecordText(const MCReleaseCopy::MCRecord& record)
    {
        MCJson files = MCJson::array();

        for (const auto& [folded, held] : record.Files)
        {
            files.push_back({{PathKey, held.Path},
                             {Sha256Key, MCSha256::Hex(held.Sha256)},
                             {SizeKey, held.Size},
                             {WrittenKey, held.Written}});
        }

        return MCJson{{ReleaseKey, record.Release}, {FilesKey, std::move(files)}, {LackingKey, record.Lacking}}.dump(1);
    }

    /// <summary>
    /// Whether <paramref name="path"/> is as <paramref name="sha256"/> says: its size and time as recorded
    /// (<paramref name="size"/>, <paramref name="written"/>: the hash recorded with them holds), else its bytes hashed.
    /// </summary>
    std::expected<bool, std::string> IsAsListed(MCUpdateDisk& disk, const std::filesystem::path& path,
                                                const MCSha256::MCDigest& sha256, uint64_t size, int64_t written,
                                                const MCSha256::MCDigest& recorded, MCReleaseCopy::MCSynced& synced)
    {
        const MCUpdateDisk::MCStat stat = disk.Stat(path);

        if (!stat.Error.empty())
        {
            return std::unexpected(std::format("{} can't be looked at: {}", MCFileSystem::ToUtf8(path), stat.Error));
        }

        if (!stat.Exists || stat.Folder)
        {
            return false;
        }

        if (stat.Size == size && stat.Written == written)
        {
            return recorded == sha256;
        }

        ++synced.Hashed;
        const auto hash = disk.Hash(path);

        if (!hash)
        {
            return std::unexpected(hash.error());
        }

        return *hash == sha256;
    }
}

bool MCReleaseCopy::Holds(std::string_view listedPath)
{
    const std::string folded = MCContentList::FoldedPath(listedPath);

    if (folded.find('/') == std::string::npos)
    {
        return folded.ends_with(ArchiveKind);
    }

    if (folded.starts_with(ObjectsFolder))
    {
        return folded.size() > ObjectsFolder.size();
    }

    return folded.starts_with(SavesFolder) && folded.find('/', SavesFolder.size()) == std::string::npos &&
           folded.ends_with(MissionKind);
}

std::filesystem::path MCReleaseCopy::FolderIn(const std::filesystem::path& gameFolder)
{
    return gameFolder / MCFileSystem::FromUtf8(Folder);
}

std::expected<std::optional<MCReleaseCopy::MCRecord>, std::string> MCReleaseCopy::Read(
    MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    return ReadFile(disk, RecordIn(gameFolder), [](std::string_view text)
                    { return MCUpdateJson::Read(RecordName, [&]() { return ReadRecord(Object(text)); }); });
}

std::expected<void, std::string> MCReleaseCopy::CloneOrCopy(const std::filesystem::path& from,
                                                            const std::filesystem::path& to)
{
#if defined(__APPLE__)
    // A clone shares the original's blocks until one side is written; another volume, or a file system without
    // clones, copies instead.
    if (clonefile(from.c_str(), to.c_str(), 0) == 0)
    {
        return {};
    }

    SDL_Log("Release copy: %s can't be cloned (%s): copied", MCFileSystem::ToUtf8(from).c_str(), std::strerror(errno));
#endif

    std::error_code error;

    if (!std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error))
    {
        return std::unexpected(
            std::format("{} can't be copied: {}", MCFileSystem::ToUtf8(from), error ? error.message() : "it's there"));
    }

    return {};
}

std::expected<MCReleaseCopy::MCSynced, std::string> MCReleaseCopy::Sync(MCUpdateDisk& disk,
                                                                        const std::filesystem::path& gameFolder,
                                                                        const MCInstalled::MCRecord& installed)
{
    MCSynced synced;

    if (installed.Release == 0)
    {
        return synced;
    }

    const std::filesystem::path folder = FolderIn(gameFolder);
    const auto read = Read(disk, gameFolder);

    // A record that can't be read is filled again: the copy's files are checked against the release's anyway.
    if (!read)
    {
        SDL_Log("Release copy: %s: filled again", read.error().c_str());
    }

    const MCRecord had = read && read->has_value() ? **read : MCRecord{};
    MCRecord now{.Release = installed.Release};

    for (const auto& [folded, listed] : installed.Files)
    {
        if (!Holds(listed.Path))
        {
            continue;
        }

        const std::filesystem::path place = folder / MCFileSystem::FromUtf8(listed.Path);

        // Held already as the release lists it.
        if (const auto kept = had.Files.find(folded); kept != had.Files.end() && kept->second.Sha256 == listed.Sha256)
        {
            const auto same = IsAsListed(disk, place, listed.Sha256, kept->second.Size, kept->second.Written,
                                         kept->second.Sha256, synced);

            if (!same)
            {
                return std::unexpected(same.error());
            }

            if (*same)
            {
                // As it is now: hashed because its size or time changed (a restore, a copied install), it's recorded
                // again so the start that opens it takes it as filled.
                const MCUpdateDisk::MCStat stat = disk.Stat(place);
                now.Files.insert_or_assign(
                    folded,
                    MCHeld{.Path = listed.Path, .Sha256 = listed.Sha256, .Size = stat.Size, .Written = stat.Written});
                continue;
            }
        }

        // From the game folder's copy, when it's the release's.
        const std::filesystem::path source = gameFolder / MCFileSystem::FromUtf8(listed.Path);
        const auto fromRelease =
            IsAsListed(disk, source, listed.Sha256, listed.Size, listed.Written, listed.Sha256, synced);

        if (!fromRelease)
        {
            return std::unexpected(fromRelease.error());
        }

        if (!*fromRelease)
        {
            SDL_Log("Release copy: %s isn't the release's in the game folder: a match reads it there",
                    listed.Path.c_str());
            now.Lacking.push_back(listed.Path);

            if (!disk.Remove(place))
            {
                return std::unexpected(std::format("{} can't be taken out of the copy", listed.Path));
            }

            continue;
        }

        std::filesystem::path filling = place;
        filling += MCFileSystem::FromUtf8(FillingSuffix);

        if (!disk.MakeFolders(place.parent_path()) || !disk.Remove(filling))
        {
            return std::unexpected(std::format("{} can't be made", MCFileSystem::ToUtf8(place.parent_path())));
        }

        if (const auto copied = CloneOrCopy(source, filling); !copied)
        {
            return std::unexpected(copied.error());
        }

        // Checked as it landed: a file changed while it was copied isn't the release's.
        ++synced.Hashed;
        const auto landed = disk.Hash(filling);

        if (!landed || *landed != listed.Sha256 || !disk.FlushFile(filling) || !disk.Rename(filling, place))
        {
            disk.Remove(filling);
            return std::unexpected(std::format("{} can't be put in the copy{}", listed.Path,
                                               landed ? "" : std::format(": {}", landed.error())));
        }

        const MCUpdateDisk::MCStat stat = disk.Stat(place);

        if (!stat.Exists || !stat.Error.empty())
        {
            return std::unexpected(std::format("{} can't be looked at once copied", listed.Path));
        }

        now.Files.insert_or_assign(
            folded, MCHeld{.Path = listed.Path, .Sha256 = listed.Sha256, .Size = stat.Size, .Written = stat.Written});
        ++synced.Filled;
    }

    // What the release no longer lists goes, the record itself kept.
    if (const auto files = disk.Files(folder); files)
    {
        for (const std::string& file : *files)
        {
            if (file != RecordName && !now.Files.contains(MCContentList::FoldedPath(file)))
            {
                if (!disk.Remove(folder / MCFileSystem::FromUtf8(file)))
                {
                    SDL_Log("Release copy: %s, no longer the release's, can't be deleted", file.c_str());
                }
            }
        }
    }
    else
    {
        return std::unexpected(files.error());
    }

    if (!WriteDurably(disk, gameFolder, RecordIn(gameFolder), RecordText(now)))
    {
        return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(RecordIn(gameFolder))));
    }

    synced.Held = now.Files.size();
    synced.Lacking = std::move(now.Lacking);
    return synced;
}
