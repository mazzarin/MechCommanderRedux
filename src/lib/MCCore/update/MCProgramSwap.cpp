#include "stdafx.h"
#include "update/MCProgramSwap.h"
#include "platform/MCFileSystem.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"

#include <thread>

namespace
{
    using MCReleaseFile::MCProgram;
    using MCReleaseFile::MCProgramFile;

    /// <summary>The journal's keys, and the setup record's.</summary>
    constexpr std::string_view BuildKey = "build";
    constexpr std::string_view VersionKey = "version";
    constexpr std::string_view FilesKey = "files";
    constexpr std::string_view NameKey = "name";
    constexpr std::string_view SizeKey = "size";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr std::string_view OldKey = "old";
    constexpr std::string_view NamesKey = "names";
    constexpr std::string_view LevelKey = "level";

    /// <summary>A file held (an antivirus reading it, say) is tried this many times, this far apart.</summary>
    constexpr int MoveTries = 5;
    constexpr std::chrono::milliseconds MoveRetryWait{200};

    /// <summary>
    /// The game's other names, folded: the original's, and its resolutions' (MCX&lt;width&gt;x&lt;height&gt;.EXE), as the
    /// setup makes them (src/installer/windows/MechCommanderRedux.iss's MakeGameName).
    /// </summary>
    constexpr std::string_view OriginalName = "MCX.EXE";
    constexpr std::string_view ResolutionPrefix = "MCX";
    constexpr char ResolutionSign = 'X';
    constexpr std::string_view ResolutionExtension = ".EXE";
    /// <summary>A name being made, in the old files' folder, before it moves into place.</summary>
    constexpr std::string_view MadeSuffix = ".new";
    /// <summary>A new file put back that can't be deleted (a copy runs it) is renamed aside with this, beside the old ones.</summary>
    constexpr std::string_view GoneSuffix = ".gone";
    /// <summary>The zip's download, in the swap folder: its build between these.</summary>
    constexpr std::string_view ZipPrefix = "program-";
    constexpr std::string_view ZipSuffix = ".zip";

    /// <summary>A file's size and SHA-256.</summary>
    struct MCFileId
    {
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
    };

    /// <summary>A file the swap puts in: its name, the new one's size and hash, and the one it replaces.</summary>
    struct MCJournalFile
    {
        std::string Name;
        uint64_t Size = 0;
        MCSha256::MCDigest Sha256{};
        /// <summary>The file the folder had there before, as it was; none when it had none.</summary>
        std::optional<MCFileId> Old;
    };

    /// <summary>The swap under way: the build and version going in, its files (the game last), the game's other names.</summary>
    struct MCJournal
    {
        uint64_t Build = 0;
        std::string Version;
        std::vector<MCJournalFile> Files;
        std::vector<std::string> Names;
    };

    std::filesystem::path In(const std::filesystem::path& gameFolder, std::string_view relative)
    {
        return gameFolder / MCFileSystem::FromUtf8(relative);
    }

    /// <summary>Build <paramref name="build"/>'s folder in <paramref name="folder"/> (the staged files', the old ones').</summary>
    std::filesystem::path ForBuild(const std::filesystem::path& gameFolder, std::string_view folder, uint64_t build)
    {
        return In(gameFolder, folder) / MCFileSystem::FromUtf8(std::to_string(build));
    }

    std::filesystem::path ZipPath(const std::filesystem::path& gameFolder, uint64_t build)
    {
        return In(gameFolder, MCProgramSwap::SwapFolder) /
               MCFileSystem::FromUtf8(std::format("{}{}{}", ZipPrefix, build, ZipSuffix));
    }

    /// <summary>The build a name of the swap's spells (a staged folder's, a zip's between its prefix and suffix), or none.</summary>
    std::optional<uint64_t> BuildIn(std::string_view name, std::string_view prefix, std::string_view suffix)
    {
        if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() == prefix.size() + suffix.size())
        {
            return std::nullopt;
        }

        const std::string_view digits = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        uint64_t build = 0;
        const auto [end, failed] = std::from_chars(digits.data(), digits.data() + digits.size(), build);
        return failed == std::errc() && end == digits.data() + digits.size() ? std::optional(build) : std::nullopt;
    }

    /// <summary>Whether the file at <paramref name="path"/> is <paramref name="size"/> bytes of SHA-256 <paramref name="sha256"/>.</summary>
    std::expected<bool, std::string> IsNamed(MCUpdateDisk& disk, const std::filesystem::path& path, uint64_t size,
                                             const MCSha256::MCDigest& sha256)
    {
        const MCUpdateDisk::MCStat stat = disk.Stat(path);

        if (!stat.Error.empty())
        {
            return std::unexpected(stat.Error);
        }

        if (!stat.Exists || stat.Folder || stat.Size != size)
        {
            return false;
        }

        const auto hash = disk.Hash(path);

        if (!hash)
        {
            return std::unexpected(hash.error());
        }

        return *hash == sha256;
    }

    /// <summary>Whether the folded <paramref name="name"/> is one of the game's other names: MCX.EXE, or a resolution's.</summary>
    bool IsGameName(std::string_view name)
    {
        if (name == OriginalName)
        {
            return true;
        }

        if (!name.starts_with(ResolutionPrefix) || !name.ends_with(ResolutionExtension))
        {
            return false;
        }

        const std::string_view size =
            name.substr(ResolutionPrefix.size(), name.size() - ResolutionPrefix.size() - ResolutionExtension.size());
        const size_t sign = size.find(ResolutionSign);
        const auto digits = [](std::string_view part)
        { return !part.empty() && std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; }); };
        return sign != std::string_view::npos && digits(size.substr(0, sign)) && digits(size.substr(sign + 1));
    }

    /// <summary>
    /// The game's other names in <paramref name="gameFolder"/> that are the game now: a link to its <c>MCRedux.exe</c>, or
    /// a copy of it (a drive without links). Others (the original's own program, kept by a player) aren't touched.
    /// </summary>
    std::expected<std::vector<std::string>, std::string> GameNames(MCUpdateDisk& disk,
                                                                   const std::filesystem::path& gameFolder)
    {
        const std::filesystem::path game = gameFolder / MCFileSystem::FromUtf8(MCProgramSwap::GameName);
        const MCUpdateDisk::MCStat gameStat = disk.Stat(game);

        if (!gameStat.Exists)
        {
            return std::vector<std::string>();
        }

        std::optional<MCSha256::MCDigest> gameHash;
        std::vector<std::string> names;
        std::error_code error;

        for (std::filesystem::directory_iterator entry(gameFolder, error), end; !error && entry != end;
             entry.increment(error))
        {
            const std::string name = MCFileSystem::ToUtf8(entry->path().filename());

            if (!IsGameName(MCContentList::FoldedPath(name)) || !entry->is_regular_file(error))
            {
                continue;
            }

            std::error_code same;

            if (std::filesystem::equivalent(game, entry->path(), same))
            {
                names.push_back(name);
                continue;
            }

            if (disk.Stat(entry->path()).Size != gameStat.Size)
            {
                continue;
            }

            if (!gameHash)
            {
                const auto hash = disk.Hash(game);

                if (!hash)
                {
                    return std::unexpected(hash.error());
                }

                gameHash = *hash;
            }

            if (const auto hash = disk.Hash(entry->path()); hash && *hash == *gameHash)
            {
                names.push_back(name);
            }
        }

        if (error)
        {
            return std::unexpected(
                std::format("{} can't be listed: {}", MCFileSystem::ToUtf8(gameFolder), error.message()));
        }

        return names;
    }

    /// <summary>Moves a file where nothing is, tried again while something holds it.</summary>
    bool MoveTrying(MCUpdateDisk& disk, const std::filesystem::path& from, const std::filesystem::path& to)
    {
        for (int attempt = 1;; ++attempt)
        {
            if (disk.Move(from, to))
            {
                return true;
            }

            if (attempt == MoveTries)
            {
                return false;
            }

            std::this_thread::sleep_for(MoveRetryWait);
        }
    }

    std::string JournalText(const MCJournal& journal)
    {
        MCUpdateJson::MCJson files = MCUpdateJson::MCJson::array();

        for (const MCJournalFile& file : journal.Files)
        {
            files.push_back({{NameKey, file.Name},
                             {SizeKey, file.Size},
                             {Sha256Key, MCSha256::Hex(file.Sha256)},
                             {OldKey, file.Old ? MCUpdateJson::MCJson{{SizeKey, file.Old->Size},
                                                                      {Sha256Key, MCSha256::Hex(file.Old->Sha256)}}
                                               : MCUpdateJson::MCJson()}});
        }

        return MCUpdateJson::MCJson{
            {BuildKey, journal.Build}, {VersionKey, journal.Version}, {FilesKey, files}, {NamesKey, journal.Names}}
            .dump(1);
    }

    /// <summary>The journal, none, or why it can't be read.</summary>
    std::expected<std::optional<MCJournal>, std::string> ReadJournal(MCUpdateDisk& disk,
                                                                     const std::filesystem::path& gameFolder)
    {
        return MCUpdateJson::ReadFile(
            disk, In(gameFolder, MCProgramSwap::JournalFile),
            [](std::string_view text)
            {
                return MCUpdateJson::Read(
                    MCProgramSwap::JournalFile,
                    [&]
                    {
                        const MCUpdateJson::MCJson json = MCUpdateJson::Object(text);
                        MCJournal journal{.Build = MCUpdateJson::Count(json, BuildKey),
                                          .Version = MCUpdateJson::Text(json, VersionKey),
                                          .Names = MCUpdateJson::Strings(json, NamesKey)};

                        for (const MCUpdateJson::MCJson& file : MCUpdateJson::Array(json, FilesKey))
                        {
                            const MCUpdateJson::MCJson& old = MCUpdateJson::Member(file, OldKey);

                            if (!old.is_null() && !old.is_object())
                            {
                                MCUpdateJson::Fail("\"{}\" isn't a file or null", OldKey);
                            }

                            journal.Files.push_back(
                                {.Name = MCUpdateJson::Text(file, NameKey),
                                 .Size = MCUpdateJson::Count(file, SizeKey),
                                 .Sha256 = MCUpdateJson::Digest(file, Sha256Key),
                                 .Old = old.is_null()
                                            ? std::nullopt
                                            : std::optional(MCFileId{.Size = MCUpdateJson::Count(old, SizeKey),
                                                                     .Sha256 = MCUpdateJson::Digest(old, Sha256Key)})});
                        }

                        // Only the program's plain names, in the game folder itself (MCReleaseFile's rule), and as
                        // the game's other names only MCX.EXE and the resolutions', none a file's.
                        const auto plain = [](std::string_view name)
                        {
                            return MCContentList::PathProblem(name).empty() && name.find('/') == std::string_view::npos;
                        };

                        const auto otherName = [&](const std::string& name)
                        {
                            const std::string folded = MCContentList::FoldedPath(name);
                            return IsGameName(folded) &&
                                   std::ranges::none_of(journal.Files, [&](const MCJournalFile& file)
                                                        { return MCContentList::FoldedPath(file.Name) == folded; });
                        };

                        if (!std::ranges::all_of(journal.Files, plain, &MCJournalFile::Name) ||
                            !std::ranges::all_of(journal.Names, plain))
                        {
                            MCUpdateJson::Fail("it names a file outside the game folder's top");
                        }

                        if (!std::ranges::all_of(journal.Names, otherName))
                        {
                            MCUpdateJson::Fail("it names a file as the game's other name that isn't one");
                        }

                        return journal;
                    });
            });
    }

    /// <summary>Nothing, or why no program update may begin: one under way (its journal), which the next start ends.</summary>
    std::expected<void, std::string> NoneUnderWay(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
    {
        const auto underWay = IsThere(disk, In(gameFolder, MCProgramSwap::JournalFile));

        if (!underWay)
        {
            return std::unexpected(underWay.error());
        }

        if (*underWay)
        {
            return std::unexpected("an earlier program update didn't finish: the next start ends it, or run the setup");
        }

        return {};
    }

    /// <summary>
    /// Whether every one of <paramref name="program"/>'s files is in its place already: its swap committed (the running
    /// copy still the old program), and the update tried again, its content having failed to arm after it.
    /// </summary>
    std::expected<bool, std::string> AllIn(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                           const MCProgram& program)
    {
        for (const MCProgramFile& file : program.Files)
        {
            const auto in = IsNamed(disk, In(gameFolder, file.Name), file.Size, file.Sha256);

            if (!in || !*in)
            {
                return in;
            }
        }

        return true;
    }

    /// <summary>The swap's paths for one file: where it goes, where its old copy is set aside, and where it's staged.</summary>
    struct MCPlaces
    {
        std::filesystem::path At;
        std::filesystem::path Old;
        std::filesystem::path Staged;
    };

    MCPlaces PlacesOf(const std::filesystem::path& gameFolder, uint64_t build, std::string_view name)
    {
        const std::filesystem::path file = MCFileSystem::FromUtf8(name);
        return {.At = gameFolder / file,
                .Old = ForBuild(gameFolder, MCProgramSwap::OldFolder, build) / file,
                .Staged = ForBuild(gameFolder, MCProgramSwap::StagedFolder, build) / file};
    }

    /// <summary>Nothing, or why <paramref name="path"/> isn't a file the swap may move: a folder (the player's own).</summary>
    std::expected<void, std::string> NotAFolder(MCUpdateDisk& disk, const std::filesystem::path& path,
                                                std::string_view name)
    {
        const MCUpdateDisk::MCStat stat = disk.Stat(path);

        if (!stat.Error.empty())
        {
            return std::unexpected(stat.Error);
        }

        if (stat.Folder)
        {
            return std::unexpected(std::format("{} in the game folder is a folder: move it, then try again", name));
        }

        return {};
    }

    /// <summary>
    /// A new file at <paramref name="path"/> taken out of its place: deleted, or, where a copy runs it (Windows deletes
    /// no running program), renamed to <paramref name="gone"/>, which the next start's clearing deletes.
    /// </summary>
    bool TakeOut(MCUpdateDisk& disk, const std::filesystem::path& path, const std::filesystem::path& gone)
    {
        if (disk.Remove(path))
        {
            return true;
        }

        disk.Remove(gone);
        return MoveTrying(disk, path, gone);
    }

    std::filesystem::path GonePath(const std::filesystem::path& old, std::string_view name)
    {
        return old.parent_path() / MCFileSystem::FromUtf8(std::string(name) + std::string(GoneSuffix));
    }

    /// <summary>
    /// <paramref name="file"/> in: the new one at its place (left as it is: unchanged since the last release, or moved
    /// already), or its old one set aside and the staged one moved in. The staged one is known to be right.
    /// </summary>
    std::expected<void, std::string> FileIn(MCUpdateDisk& disk, MCNoLinks& noLinks,
                                            const std::filesystem::path& gameFolder, uint64_t build,
                                            const MCJournalFile& file)
    {
        const MCPlaces places = PlacesOf(gameFolder, build, file.Name);

        for (const std::filesystem::path& path : {places.At, places.Old, places.Staged})
        {
            if (auto checked = noLinks.Check(path); !checked)
            {
                return std::unexpected(checked.error());
            }
        }

        const auto isNew = IsNamed(disk, places.At, file.Size, file.Sha256);

        if (!isNew)
        {
            return std::unexpected(isNew.error());
        }

        if (*isNew)
        {
            return {};
        }

        const auto atThere = IsThere(disk, places.At);

        if (!atThere)
        {
            return std::unexpected(atThere.error());
        }

        if (auto notFolder = NotAFolder(disk, places.At, file.Name); !notFolder)
        {
            return notFolder;
        }

        if (*atThere && !MoveTrying(disk, places.At, places.Old))
        {
            return std::unexpected(std::format("{} is in use", file.Name));
        }

        if (!MoveTrying(disk, places.Staged, places.At))
        {
            return std::unexpected(std::format("the new {} can't be moved into place", file.Name));
        }

        return {};
    }

    /// <summary>
    /// <paramref name="file"/> as it was: its old one back from where it was set aside (the new one back to staged/,
    /// or deleted when staged/ holds it again), a file the folder didn't have taken out again; one never moved stays.
    /// </summary>
    std::expected<void, std::string> FileBack(MCUpdateDisk& disk, MCNoLinks& noLinks,
                                              const std::filesystem::path& gameFolder, uint64_t build,
                                              const MCJournalFile& file)
    {
        const MCPlaces places = PlacesOf(gameFolder, build, file.Name);
        const std::filesystem::path gone = GonePath(places.Old, file.Name);

        for (const std::filesystem::path& path : {places.At, places.Old, places.Staged, gone})
        {
            if (auto checked = noLinks.Check(path); !checked)
            {
                return std::unexpected(checked.error());
            }
        }

        const auto oldThere = IsThere(disk, places.Old);
        const auto atThere = IsThere(disk, places.At);
        const auto stagedThere = IsThere(disk, places.Staged);

        if (!oldThere || !atThere || !stagedThere)
        {
            return std::unexpected(!oldThere ? oldThere.error() : !atThere ? atThere.error() : stagedThere.error());
        }

        // Its old one never moved (or it had none, and the new one never came): nothing to put back.
        if (!*oldThere && (file.Old || !*atThere))
        {
            return {};
        }

        if (*atThere)
        {
            const bool aside =
                *stagedThere ? TakeOut(disk, places.At, gone) : MoveTrying(disk, places.At, places.Staged);

            if (!aside)
            {
                return std::unexpected(std::format("the new {} is in use", file.Name));
            }
        }

        if (*oldThere && !MoveTrying(disk, places.Old, places.At))
        {
            return std::unexpected(std::format("the old {} can't be moved back", file.Name));
        }

        return {};
    }

    /// <summary>
    /// The game's other name <paramref name="name"/> made the game again: the old one set aside (a link to the old
    /// program, or a copy of it), then a new link to the new <c>MCRedux.exe</c> (a copy on a drive without links) made
    /// beside it and moved into place.
    /// </summary>
    std::expected<void, std::string> NameIn(MCUpdateDisk& disk, MCNoLinks& noLinks,
                                            const std::filesystem::path& gameFolder, uint64_t build,
                                            const std::string& name)
    {
        const MCPlaces places = PlacesOf(gameFolder, build, name);
        const std::filesystem::path made =
            places.Old.parent_path() / MCFileSystem::FromUtf8(name + std::string(MadeSuffix));
        const std::filesystem::path game = gameFolder / MCFileSystem::FromUtf8(MCProgramSwap::GameName);

        for (const std::filesystem::path& path : {places.At, places.Old, made})
        {
            if (auto checked = noLinks.Check(path); !checked)
            {
                return std::unexpected(checked.error());
            }
        }

        const auto oldThere = IsThere(disk, places.Old);
        const auto atThere = IsThere(disk, places.At);

        if (!oldThere || !atThere)
        {
            return std::unexpected(!oldThere ? oldThere.error() : atThere.error());
        }

        // Set aside and made again already.
        if (*oldThere && *atThere)
        {
            return {};
        }

        if (auto notFolder = NotAFolder(disk, places.At, name); !notFolder)
        {
            return notFolder;
        }

        if (*atThere && !MoveTrying(disk, places.At, places.Old))
        {
            return std::unexpected(std::format("{} is in use", name));
        }

        disk.Remove(made);
        std::error_code linked;
        std::filesystem::create_hard_link(game, made, linked);

        if (linked)
        {
            std::error_code copied;
            std::filesystem::copy_file(game, made, copied);

            if (copied || !disk.FlushFile(made))
            {
                return std::unexpected(std::format("{} can't be made the game: no link ({}), and no copy ({})", name,
                                                   linked.message(), copied ? copied.message() : "not flushed"));
            }

            SDL_Log("Updater: %s is a copy of the new %s (no link: %s)", name.c_str(),
                    std::string(MCProgramSwap::GameName).c_str(), linked.message().c_str());
        }

        if (!MoveTrying(disk, made, places.At))
        {
            return std::unexpected(std::format("{} can't be moved into place", name));
        }

        return {};
    }

    /// <summary>The game's other name <paramref name="name"/> as it was: the new one gone, the old one back.</summary>
    std::expected<void, std::string> NameBack(MCUpdateDisk& disk, MCNoLinks& noLinks,
                                              const std::filesystem::path& gameFolder, uint64_t build,
                                              const std::string& name)
    {
        const MCPlaces places = PlacesOf(gameFolder, build, name);
        const std::filesystem::path made =
            places.Old.parent_path() / MCFileSystem::FromUtf8(name + std::string(MadeSuffix));
        const std::filesystem::path gone = GonePath(places.Old, name);

        for (const std::filesystem::path& path : {places.At, places.Old, made, gone})
        {
            if (auto checked = noLinks.Check(path); !checked)
            {
                return std::unexpected(checked.error());
            }
        }

        disk.Remove(made);
        const auto oldThere = IsThere(disk, places.Old);

        if (!oldThere)
        {
            return std::unexpected(oldThere.error());
        }

        if (!*oldThere)
        {
            return {};
        }

        if (const auto atThere = IsThere(disk, places.At); !atThere || (*atThere && !TakeOut(disk, places.At, gone)))
        {
            return std::unexpected(!atThere ? atThere.error() : std::format("the new {} is in use", name));
        }

        if (!MoveTrying(disk, places.Old, places.At))
        {
            return std::unexpected(std::format("the old {} can't be moved back", name));
        }

        return {};
    }

    /// <summary>The folders the swap changed flushed: the game folder's and the old files'.</summary>
    bool FlushSwapped(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, uint64_t build)
    {
        const std::filesystem::path old = ForBuild(gameFolder, MCProgramSwap::OldFolder, build);
        return disk.FlushFolder(gameFolder) && (!disk.Stat(old).Exists || disk.FlushFolder(old)) &&
               (!disk.Stat(ForBuild(gameFolder, MCProgramSwap::StagedFolder, build)).Exists ||
                disk.FlushFolder(ForBuild(gameFolder, MCProgramSwap::StagedFolder, build)));
    }

    /// <summary>The journal deleted, its folder flushed: what it recorded is done (or undone).</summary>
    bool EndJournal(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
    {
        return disk.Remove(In(gameFolder, MCProgramSwap::JournalFile)) &&
               disk.FlushFolder(In(gameFolder, MCProgramSwap::SwapFolder));
    }

    /// <summary>
    /// Puts <paramref name="journal"/>'s files back as they were, the reverse of their order in, then ends it. It stops
    /// at the first that can't go back, so a new program is never left beside its old files (the game's other names
    /// first, then the game, then its files); the journal stays, so the next start tries again.
    /// </summary>
    /// <returns>Empty once back, or why not.</returns>
    std::string PutBack(MCUpdateDisk& disk, MCNoLinks& noLinks, const std::filesystem::path& gameFolder,
                        const MCJournal& journal)
    {
        for (const std::string& name : journal.Names | std::views::reverse)
        {
            if (const auto back = NameBack(disk, noLinks, gameFolder, journal.Build, name); !back)
            {
                return back.error();
            }
        }

        for (const MCJournalFile& file : journal.Files | std::views::reverse)
        {
            if (const auto back = FileBack(disk, noLinks, gameFolder, journal.Build, file); !back)
            {
                return back.error();
            }
        }

        if (!FlushSwapped(disk, gameFolder, journal.Build) || !EndJournal(disk, gameFolder))
        {
            return "the old files can't be flushed";
        }

        return {};
    }

    /// <summary>
    /// Every file of <paramref name="journal"/> in, its other names made, the folders flushed, the Apps list's version
    /// set, and the journal ended: the commit. The staged files are known to be right.
    /// </summary>
    std::expected<void, std::string> PutIn(MCUpdateDisk& disk, MCNoLinks& noLinks,
                                           const std::filesystem::path& gameFolder, const MCJournal& journal,
                                           const MCProgramSwap::MCPlatform& platform)
    {
        const std::filesystem::path old = ForBuild(gameFolder, MCProgramSwap::OldFolder, journal.Build);

        if (auto checked = noLinks.Check(old); !checked)
        {
            return std::unexpected(checked.error());
        }

        if (!disk.MakeFolders(old))
        {
            return std::unexpected("the old files' folder can't be made");
        }

        for (const MCJournalFile& file : journal.Files)
        {
            if (const auto in = FileIn(disk, noLinks, gameFolder, journal.Build, file); !in)
            {
                return in;
            }
        }

        for (const std::string& name : journal.Names)
        {
            if (const auto in = NameIn(disk, noLinks, gameFolder, journal.Build, name); !in)
            {
                return in;
            }
        }

        if (!FlushSwapped(disk, gameFolder, journal.Build))
        {
            return std::unexpected("the new files can't be flushed");
        }

        if (const std::string shown = platform.ShowVersion ? platform.ShowVersion(gameFolder, journal.Version) : "";
            !shown.empty())
        {
            SDL_Log("Updater: the Apps list still shows the earlier version: %s", shown.c_str());
        }

        if (!EndJournal(disk, gameFolder))
        {
            // In place: the next start finds every file new, and ends it.
            SDL_Log("Updater: build %llu is in, but its journal can't be ended: the next start does",
                    static_cast<unsigned long long>(journal.Build));
        }

        // What staged/ still holds: the files unchanged since the last release, never moved.
        disk.Remove(ForBuild(gameFolder, MCProgramSwap::StagedFolder, journal.Build));
        return {};
    }

    /// <summary>The swap failed (<paramref name="reason"/>): the old files back, or the next start's to put back.</summary>
    std::string Failed(MCUpdateDisk& disk, MCNoLinks& noLinks, const std::filesystem::path& gameFolder,
                       const MCJournal& journal, const std::string& reason)
    {
        if (const std::string notBack = PutBack(disk, noLinks, gameFolder, journal); !notBack.empty())
        {
            SDL_Log("Updater: build %llu's program didn't go in (%s), and isn't all back yet (%s): the next start puts "
                    "it back",
                    static_cast<unsigned long long>(journal.Build), reason.c_str(), notBack.c_str());
            return reason;
        }

        SDL_Log("Updater: build %llu's program didn't go in (%s): the old one is back",
                static_cast<unsigned long long>(journal.Build), reason.c_str());
        return reason;
    }

    /// <summary>
    /// With no swap under way: the files earlier swaps set aside go, and those staged (or downloading) for build
    /// <paramref name="runningBuild"/> or older; a newer build's stay for its INSTALL to go on with.
    /// </summary>
    void ClearDone(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, uint64_t runningBuild)
    {
        const std::filesystem::path old = In(gameFolder, MCProgramSwap::OldFolder);
        MCNoLinks noLinks(disk, gameFolder);

        for (const std::string_view folder :
             {MCProgramSwap::SwapFolder, MCProgramSwap::OldFolder, MCProgramSwap::StagedFolder})
        {
            if (auto checked = noLinks.Check(In(gameFolder, folder)); !checked)
            {
                SDL_Log("Updater: earlier program updates' files aren't cleared: %s", checked.error().c_str());
                return;
            }
        }

        const std::filesystem::path staged = In(gameFolder, MCProgramSwap::StagedFolder);
        const std::filesystem::path swap = In(gameFolder, MCProgramSwap::SwapFolder);
        std::vector<std::filesystem::path> done;
        std::error_code error;

        for (const std::filesystem::path& folder : {old, staged, swap})
        {
            for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end;
                 entry.increment(error))
            {
                const std::string name = MCFileSystem::ToUtf8(entry->path().filename());
                const std::optional<uint64_t> build =
                    folder == swap ? BuildIn(name, ZipPrefix, ZipSuffix) : BuildIn(name, "", "");

                // Every earlier swap's old files; staged files (and downloads) for this build or older.
                if (build && (folder == old || *build <= runningBuild))
                {
                    done.push_back(entry->path());
                }
            }

            error.clear();
        }

        // Each looked at first (a link there, the game's writes would follow it). A copy still running an old program
        // holds its files: a later start tries again.
        for (const std::filesystem::path& path : done)
        {
            if (auto checked = noLinks.Check(path); !checked)
            {
                SDL_Log("Updater: an earlier program update's files aren't cleared: %s", checked.error().c_str());
            }
            else if (!disk.Remove(path))
            {
                SDL_Log("Updater: what an earlier program update set aside can't all be deleted yet");
            }
        }

        disk.RemoveIfEmpty(old);
        disk.RemoveIfEmpty(staged);
        disk.RemoveIfEmpty(swap);
    }
}

uint64_t MCProgramSwap::SetupLevel(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    const auto level = MCUpdateJson::ReadFile(
        disk, In(gameFolder, SetupFile),
        [](std::string_view text)
        {
            return MCUpdateJson::Read(SetupFile,
                                      [&] { return MCUpdateJson::CountOr(MCUpdateJson::Object(text), LevelKey, 0); });
        });

    if (!level)
    {
        SDL_Log("Updater: the setup's level can't be read (%s): its setup runs", level.error().c_str());
        return 0;
    }

    return level->value_or(0);
}

bool MCProgramSwap::Swaps(uint64_t copyLevel, const MCReleaseFile::MCProgram& program)
{
    return program.Zip.has_value() && program.SetupLevel != 0 && copyLevel == program.SetupLevel &&
           std::ranges::contains(program.Files, GameName, &MCProgramFile::Name);
}

std::expected<void, std::string> MCProgramSwap::Stage(MCUpdateDisk& disk, MCContentStage::MCSource& source,
                                                      std::string_view zipName, const std::filesystem::path& gameFolder,
                                                      const MCReleaseFile::MCProgram& program,
                                                      const std::function<bool(uint64_t)>& received)
{
    if (!program.Zip)
    {
        return std::unexpected("release.json gives the program no zip");
    }

    // A swap stopped midway needs its staged files: nothing is staged until a start has ended it.
    if (auto none = NoneUnderWay(disk, gameFolder); !none)
    {
        return none;
    }

    // In already: nothing to fetch.
    if (const auto in = AllIn(disk, gameFolder, program); !in || *in)
    {
        return in ? std::expected<void, std::string>() : std::unexpected(in.error());
    }

    MCNoLinks noLinks(disk, gameFolder);
    const std::filesystem::path staged = ForBuild(gameFolder, StagedFolder, program.Build);

    if (auto checked = noLinks.Check(staged); !checked)
    {
        return std::unexpected(checked.error());
    }

    // Another build's staged files (an update given up, or superseded) go.
    std::error_code error;

    for (std::filesystem::directory_iterator entry(In(gameFolder, StagedFolder), error), end; !error && entry != end;
         entry.increment(error))
    {
        if (entry->path() != staged && noLinks.Check(entry->path()))
        {
            disk.Remove(entry->path());
        }
    }

    std::vector<MCContentStage::MCEntry> wanted;

    for (const MCProgramFile& file : program.Files)
    {
        const std::filesystem::path path = staged / MCFileSystem::FromUtf8(file.Name);
        const auto right = IsNamed(disk, path, file.Size, file.Sha256);

        if (!right)
        {
            return std::unexpected(right.error());
        }

        if (!*right)
        {
            wanted.push_back({.Path = file.Name, .To = path, .Size = file.Size, .Sha256 = file.Sha256});
        }
    }

    if (!wanted.empty())
    {
        const std::filesystem::path zip = ZipPath(gameFolder, program.Build);

        if (auto checked = noLinks.Check(zip); !checked)
        {
            return std::unexpected(checked.error());
        }

        if (!disk.MakeFolders(zip.parent_path()))
        {
            return std::unexpected(std::format("{} can't be made", MCFileSystem::ToUtf8(zip.parent_path())));
        }

        const auto fetched =
            MCContentStage::FetchChecked(disk, source, zipName, zip, program.Zip->Size, program.Zip->Sha256, received);

        if (!fetched)
        {
            using MCKind = MCContentStage::MCFetchError::MCKind;
            return std::unexpected(fetched.error().Kind == MCKind::NotAsNamed
                                       ? std::string("the program's download isn't the one release.json names")
                                       : fetched.error().Reason);
        }

        const auto unpacked =
            MCContentStage::Unpack(disk, noLinks, zip, program.Zip->Size, zipName, wanted, [&] { return received(0); });

        if (unpacked || unpacked.error().Kind == MCContentStage::MCUnpackError::MCKind::NotAsNamed)
        {
            disk.Remove(zip);
        }

        if (!unpacked)
        {
            return std::unexpected(unpacked.error().Reason);
        }
    }

    // Every file, those staged already too: a start stopped before their flush left them only in the system's cache.
    for (const MCProgramFile& file : program.Files)
    {
        if (const std::filesystem::path path = staged / MCFileSystem::FromUtf8(file.Name); !disk.FlushFile(path))
        {
            return std::unexpected(std::format("{} can't be flushed", MCFileSystem::ToUtf8(path)));
        }
    }

    if (!disk.FlushFolder(staged))
    {
        return std::unexpected(std::format("{} can't be flushed", MCFileSystem::ToUtf8(staged)));
    }

    return {};
}

std::expected<void, std::string> MCProgramSwap::Swap(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                     const MCReleaseFile::MCProgram& program, std::string_view version,
                                                     const MCPlatform& platform)
{
    MCNoLinks noLinks(disk, gameFolder);
    const std::filesystem::path journalPath = In(gameFolder, JournalFile);

    if (auto none = NoneUnderWay(disk, gameFolder); !none)
    {
        return none;
    }

    // In already (its swap committed, this copy still running the old program): its old files, the running one among
    // them, stay set aside for the next start to clear.
    if (const auto in = AllIn(disk, gameFolder, program); !in || *in)
    {
        if (in)
        {
            SDL_Log("Updater: build %llu's program is in already", static_cast<unsigned long long>(program.Build));
        }

        return in ? std::expected<void, std::string>() : std::unexpected(in.error());
    }

    MCJournal journal{.Build = program.Build, .Version = std::string(version)};

    // The game's program last: until it moves, a start by its shortcut runs the old one, which finishes the swap.
    for (const bool game : {false, true})
    {
        for (const MCProgramFile& file : program.Files)
        {
            if ((file.Name == GameName) == game)
            {
                journal.Files.push_back({.Name = file.Name, .Size = file.Size, .Sha256 = file.Sha256});
            }
        }
    }

    // 1. Each staged file checked again, and the system asked whether it runs the new programs.
    for (MCJournalFile& file : journal.Files)
    {
        const MCPlaces places = PlacesOf(gameFolder, program.Build, file.Name);
        const auto staged = IsNamed(disk, places.Staged, file.Size, file.Sha256);

        if (!staged || !*staged)
        {
            return std::unexpected(
                !staged ? staged.error() : std::format("the new {} isn't staged as release.json names it", file.Name));
        }

        // The file it replaces, as it is now (a folder there is the player's, never moved): a start finding neither
        // this nor the new one there knows something else (the setup) has installed since.
        if (auto notFolder = NotAFolder(disk, places.At, file.Name); !notFolder)
        {
            return notFolder;
        }

        if (const MCUpdateDisk::MCStat at = disk.Stat(places.At); at.Exists)
        {
            const auto hash = disk.Hash(places.At);

            if (!hash)
            {
                return std::unexpected(hash.error());
            }

            file.Old = MCFileId{.Size = at.Size, .Sha256 = *hash};
        }

        if ((file.Name == GameName || file.Name == EditorName) && platform.Runnable)
        {
            if (std::string refused = platform.Runnable(places.Staged); !refused.empty())
            {
                return std::unexpected(std::move(refused));
            }
        }
    }

    auto names = GameNames(disk, gameFolder);

    if (!names)
    {
        return std::unexpected(names.error());
    }

    journal.Names = std::move(*names);

    // An earlier attempt at this build, put back, may have left its folder: what's left there (a new file a copy ran,
    // renamed aside) has none of the names the swap moves, and goes at a later start.
    const std::filesystem::path old = ForBuild(gameFolder, OldFolder, program.Build);

    if (auto checked = noLinks.Check(old); !checked)
    {
        return std::unexpected(checked.error());
    }

    if (disk.Stat(old).Exists && !disk.Remove(old))
    {
        SDL_Log("Updater: %s can't all be deleted yet", MCFileSystem::ToUtf8(old).c_str());
    }

    // 2. The journal. One written but not flushed is taken out again: the swap failed, so no start may finish it.
    if (!WriteDurably(disk, gameFolder, journalPath, JournalText(journal)))
    {
        disk.Remove(journalPath);
        return std::unexpected(std::format("{} can't be written", JournalFile));
    }

    // 3 to 5: the files, the game's other names, the flush, the version shown, the commit.
    if (const auto in = PutIn(disk, noLinks, gameFolder, journal, platform); !in)
    {
        return std::unexpected(Failed(disk, noLinks, gameFolder, journal, in.error()));
    }

    SDL_Log("Updater: build %llu's program is in: the next start runs it",
            static_cast<unsigned long long>(program.Build));
    return {};
}

MCProgramSwap::MCResult MCProgramSwap::Recover(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                               uint64_t runningBuild, const MCPlatform& platform)
{
    const auto read = ReadJournal(disk, gameFolder);

    if (!read)
    {
        SDL_Log("Updater: a program update was stopped midway, and its journal can't be read: %s",
                read.error().c_str());
        return {.Outcome = MCOutcome::Stuck, .Error = read.error()};
    }

    if (!*read)
    {
        ClearDone(disk, gameFolder, runningBuild);
        return {};
    }

    const MCJournal& journal = **read;
    MCNoLinks noLinks(disk, gameFolder);

    // Something else installed since (the setup, run over the swap stopped midway): a file at its place that is neither
    // the one the swap replaces nor its new one. The journal is moot and goes; nothing moves. (A folder there is
    // looked at no further: the moves below refuse it.)
    for (const MCJournalFile& file : journal.Files)
    {
        const std::filesystem::path at = In(gameFolder, file.Name);
        const MCUpdateDisk::MCStat stat = disk.Stat(at);

        if (!stat.Error.empty())
        {
            return {.Outcome = MCOutcome::Stuck, .Error = stat.Error};
        }

        if (!stat.Exists || stat.Folder)
        {
            continue;
        }

        const auto isNew = IsNamed(disk, at, file.Size, file.Sha256);
        const auto isOld = isNew && !*isNew && file.Old ? IsNamed(disk, at, file.Old->Size, file.Old->Sha256)
                                                        : std::expected<bool, std::string>(false);

        if (!isNew || !isOld)
        {
            return {.Outcome = MCOutcome::Stuck, .Error = !isNew ? isNew.error() : isOld.error()};
        }

        if (!*isNew && !*isOld)
        {
            const std::string reason = std::format("{} is neither its old one nor its new one", file.Name);

            if (!EndJournal(disk, gameFolder))
            {
                return {.Outcome = MCOutcome::Stuck,
                        .Error = std::format("{}, and its journal can't be deleted", reason)};
            }

            SDL_Log("Updater: build %llu's program update, stopped midway, was overtaken (%s): its journal is gone",
                    static_cast<unsigned long long>(journal.Build), reason.c_str());
            ClearDone(disk, gameFolder, runningBuild);
            return {.Outcome = MCOutcome::Overtaken, .Error = reason};
        }
    }

    // Forward when every file is in or staged as it was checked; otherwise back.
    std::string gone;

    for (const MCJournalFile& file : journal.Files)
    {
        const MCPlaces places = PlacesOf(gameFolder, journal.Build, file.Name);
        const auto in = IsNamed(disk, places.At, file.Size, file.Sha256);
        const auto staged = in && !*in ? IsNamed(disk, places.Staged, file.Size, file.Sha256) : in;

        if (!in || !staged)
        {
            const std::string error = !in ? in.error() : staged.error();
            SDL_Log("Updater: the program update stopped midway can't be looked at: %s", error.c_str());
            return {.Outcome = MCOutcome::Stuck, .Error = error};
        }

        if (!*in && !*staged)
        {
            gone = std::format("the staged {} is gone or damaged", file.Name);
            break;
        }
    }

    if (gone.empty())
    {
        if (const auto in = PutIn(disk, noLinks, gameFolder, journal, platform); in)
        {
            SDL_Log("Updater: build %llu's program update, stopped midway, is finished",
                    static_cast<unsigned long long>(journal.Build));
            return {.Outcome = MCOutcome::Finished};
        }
        else
        {
            gone = in.error();
        }
    }

    if (const std::string notBack = PutBack(disk, noLinks, gameFolder, journal); !notBack.empty())
    {
        SDL_Log("Updater: build %llu's program update, stopped midway, can neither finish (%s) nor go back (%s)",
                static_cast<unsigned long long>(journal.Build), gone.c_str(), notBack.c_str());
        return {.Outcome = MCOutcome::Stuck, .Error = notBack};
    }

    SDL_Log("Updater: build %llu's program update, stopped midway, is put back: %s",
            static_cast<unsigned long long>(journal.Build), gone.c_str());
    return {.Outcome = MCOutcome::PutBack, .Error = gone};
}
