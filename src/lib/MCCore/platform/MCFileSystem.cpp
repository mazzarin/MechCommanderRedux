#include "stdafx.h"
#include "platform/MCFileSystem.h"
#include "main/MCGameContext.h"

namespace
{
    /// <summary>The folder that keeps the player's own settings when it isn't the user folder (SetSettingsRoot).</summary>
    std::filesystem::path& SettingsFolder()
    {
        static std::filesystem::path folder;
        return folder;
    }

    /// <summary><paramref name="c"/>'s byte, its ASCII letter upper-cased (no locale's: the same everywhere).</summary>
    unsigned char AsciiUpper(char c)
    {
        const auto byte = static_cast<unsigned char>(c);
        return byte >= 'a' && byte <= 'z' ? static_cast<unsigned char>(byte - 'a' + 'A') : byte;
    }

    /// <summary><paramref name="c"/>'s byte, so names compare as unsigned bytes (UTF-8 after ASCII).</summary>
    unsigned char Byte(char c)
    {
        return static_cast<unsigned char>(c);
    }

    std::vector<std::string> SplitGamePath(std::string_view path)
    {
        std::vector<std::string> parts;
        std::string current;

        for (char c : path)
        {
            if (c == '\\' || c == '/')
            {
                if (!current.empty())
                {
                    parts.push_back(std::move(current));
                }

                current.clear();
            }
            else
            {
                current.push_back(c);
            }
        }

        if (!current.empty())
        {
            parts.push_back(std::move(current));
        }

        return parts;
    }

    bool IsAbsoluteGamePath(std::string_view path)
    {
        if (path.size() >= 2 && path[1] == ':')
        {
            return true;
        }

        return !path.empty() && (path[0] == '\\' || path[0] == '/');
    }

    /// <summary>
    /// Whether <paramref name="part"/> names a drive ("C:"), told by its spelling rather than has_root_name, which only
    /// knows drives on Windows: elsewhere the part would become a folder of that name.
    /// </summary>
    bool IsDrivePart(std::string_view part)
    {
        return part.size() >= 2 && part[1] == ':' && std::isalpha(static_cast<unsigned char>(part[0]));
    }

    /// <summary>Resolves <paramref name="gamePath"/> against <paramref name="root"/> (for a relative path).</summary>
    /// <remarks>
    /// A relative path never leaves <paramref name="root"/>: a ".." at the root is ignored, as at a drive's root, and a
    /// part naming a drive ("C:") is skipped, since appending it would replace the path. Paths reach here from saves,
    /// mission data and the network (issue #25).
    ///
    /// A path whose parts are all names is looked for whole first, and taken as spelled when it's there, as the walk
    /// would take it. Windows opens each folder on a path to look in it: walking an HD archive's path folder by folder
    /// opened ~8 folders an archive, each 86 times a minute in a pan, ~1.3 ms of the game's thread an archive (13 Nov).
    /// </remarks>
    std::filesystem::path ResolveFrom(const std::filesystem::path& root, std::string_view gamePath)
    {
        std::filesystem::path result;
        std::vector<std::string> parts = SplitGamePath(gamePath);
        size_t first = 0;
        size_t depth = 0;

        if (IsAbsoluteGamePath(gamePath))
        {
            if (gamePath.size() >= 2 && gamePath[1] == ':')
            {
                result = MCFileSystem::FromUtf8(std::string(gamePath.substr(0, 2)) + "/");
                first = 1;
            }
            else
            {
                result = "/";
            }
        }
        else
        {
            result = root;
        }

        const std::span<const std::string> named = std::span(parts).subspan(first);

        if (std::ranges::none_of(named, [](const std::string& part)
                                 { return part == "." || part == ".." || IsDrivePart(part); }))
        {
            std::filesystem::path whole = result;

            for (const std::string& part : named)
            {
                whole /= MCFileSystem::FromUtf8(part);
            }

            std::error_code error;

            if (std::filesystem::exists(whole, error))
            {
                return whole;
            }
        }

        for (size_t i = first; i < parts.size(); ++i)
        {
            const std::string& part = parts[i];

            if (part == ".")
            {
                continue;
            }

            if (part == "..")
            {
                if (depth > 0)
                {
                    result = result.parent_path();
                    --depth;
                }

                continue;
            }

            // A drive: skipped, not made a folder of that name.
            if (IsDrivePart(part))
            {
                continue;
            }

            ++depth;
            std::filesystem::path exact = result / MCFileSystem::FromUtf8(part);
            std::error_code error;

            if (std::filesystem::exists(exact, error))
            {
                result = std::move(exact);
                continue;
            }

            // Not there as spelled: look for the entry that matches without regard to case.
            bool found = false;

            if (std::filesystem::is_directory(result, error))
            {
                for (const auto& entry : std::filesystem::directory_iterator(result, error))
                {
                    if (MCPort::StrICmp(MCFileSystem::ToUtf8(entry.path().filename()), part) == 0)
                    {
                        result = entry.path();
                        found = true;
                        break;
                    }
                }
            }

            if (!found)
            {
                result = std::move(exact);
            }
        }

        return result;
    }

    /// <summary>The original's five archives, which a launcher's folder never gives (MCFileSystem::IsLauncherContent).</summary>
    constexpr std::array<std::string_view, 5> BaseArchives = {"ART.FST", "MISC.FST", "MISSION.FST", "SHAPES.FST",
                                                              "TERRAIN.FST"};

    /// <summary>A launcher's maps' and mods' kinds, and the folder its missions are in.</summary>
    constexpr std::string_view ArchiveKind = ".fst";
    constexpr std::string_view MissionKind = ".mpk";
    constexpr std::array<std::string_view, 2> MissionFolder = {"data", "savegame"};

    /// <summary>Whether <paramref name="name"/> ends with <paramref name="kind"/>, case ignored.</summary>
    bool IsKind(std::string_view name, std::string_view kind)
    {
        return name.size() > kind.size() && MCIEquals(name.substr(name.size() - kind.size()), kind);
    }

    /// <summary>
    /// Adds the files in <paramref name="folder"/> that match <paramref name="pattern"/> (and <paramref name="wanted"/>,
    /// when given) to <paramref name="files"/> (once each, case ignored), marked as found by <paramref name="found"/>.
    /// </summary>
    void AddMatches(const std::filesystem::path& folder, const std::string& pattern, std::vector<MCFoundFile>& files,
                    bool MCFoundFile::* found, const std::function<bool(std::string_view)>& wanted = {})
    {
        std::error_code error;

        if (!std::filesystem::is_directory(folder, error))
        {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        {
            if (!entry.is_regular_file(error))
            {
                continue;
            }

            const std::string name = MCFileSystem::ToUtf8(entry.path().filename());

            if (!MCFileSystem::WildcardMatch(pattern, name) || (wanted && !wanted(name)))
            {
                continue;
            }

            const auto listed = std::ranges::find_if(files, [&](const MCFoundFile& other)
                                                     { return MCPort::StrICmp(other.Name, name) == 0; });
            MCFoundFile& file = listed != files.end() ? *listed : files.emplace_back(MCFoundFile{.Name = name});
            file.*found = true;
        }
    }
}

MCDiskFileSource::MCDiskFileSource() : _Root(std::filesystem::current_path())
{
}

bool MCDiskFileSource::HasOverlay(std::string_view gamePath) const
{
    return !_UserRoot.empty() && !IsAbsoluteGamePath(gamePath);
}

void MCDiskFileSource::SetGameRoot(const std::filesystem::path& root)
{
    _Root = root;
}

const std::filesystem::path& MCDiskFileSource::GameRoot() const
{
    return _Root;
}

void MCDiskFileSource::SetUserRoot(const std::filesystem::path& root)
{
    _UserRoot = root;

    if (!root.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(root, error);
    }
}

const std::filesystem::path& MCDiskFileSource::UserRoot() const
{
    return _UserRoot.empty() ? _Root : _UserRoot;
}

void MCDiskFileSource::SetLauncherRoot(const std::filesystem::path& root)
{
    _LauncherRoot = root;
}

const std::filesystem::path& MCDiskFileSource::LauncherRoot() const
{
    return _LauncherRoot;
}

std::optional<std::filesystem::path> MCDiskFileSource::InLauncherFolder(std::string_view gamePath) const
{
    if (_LauncherRoot.empty() || IsAbsoluteGamePath(gamePath) || !MCFileSystem::IsLauncherContent(gamePath))
    {
        return std::nullopt;
    }

    std::filesystem::path launcher = ResolveFrom(_LauncherRoot, gamePath);
    std::error_code error;
    return std::filesystem::is_regular_file(launcher, error) ? std::optional(std::move(launcher)) : std::nullopt;
}

std::filesystem::path MCDiskFileSource::Resolve(std::string_view gamePath)
{
    if (HasOverlay(gamePath))
    {
        std::filesystem::path user = ResolveFrom(_UserRoot, gamePath);
        std::error_code error;

        if (std::filesystem::exists(user, error))
        {
            return user;
        }
    }

    if (std::optional<std::filesystem::path> launcher = InLauncherFolder(gamePath))
    {
        return std::move(*launcher);
    }

    return ResolveFrom(_Root, gamePath);
}

std::filesystem::path MCDiskFileSource::ResolveWrite(std::string_view gamePath, bool copyExisting)
{
    if (!HasOverlay(gamePath))
    {
        return ResolveFrom(_Root, gamePath);
    }

    std::filesystem::path user = ResolveFrom(_UserRoot, gamePath);
    std::error_code error;
    std::filesystem::create_directories(user.parent_path(), error);

    if (copyExisting && !std::filesystem::exists(user, error))
    {
        // The copy the game reads: a launcher's map or mod, else the install's (the launcher's folder is never written).
        const std::filesystem::path installed = InLauncherFolder(gamePath).value_or(ResolveFrom(_Root, gamePath));

        if (std::filesystem::is_regular_file(installed, error))
        {
            std::filesystem::copy_file(installed, user, error);
        }
    }

    return user;
}

bool MCDiskFileSource::MakeDirectory(std::string_view gamePath)
{
    const std::filesystem::path folder = ResolveWrite(gamePath, false);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return std::filesystem::is_directory(folder, error);
}

bool MCDiskFileSource::RemoveDirectory(std::string_view gamePath)
{
    const std::filesystem::path folder = ResolveWrite(gamePath, false);
    std::error_code error;

    if (!std::filesystem::is_directory(folder, error) || !std::filesystem::is_empty(folder, error))
    {
        return false;
    }

    return std::filesystem::remove(folder, error);
}

std::vector<std::string> MCDiskFileSource::FindFiles(std::string_view gamePattern)
{
    std::vector<std::string> names;

    for (MCFoundFile& file : FindFileSources(gamePattern))
    {
        names.push_back(std::move(file.Name));
    }

    return names;
}

std::vector<MCFoundFile> MCDiskFileSource::FindFileSources(std::string_view gamePattern)
{
    std::string folder(gamePattern);
    std::string pattern = folder;
    const size_t slash = folder.find_last_of("\\/");

    if (slash == std::string::npos)
    {
        folder.clear();
    }
    else
    {
        pattern = folder.substr(slash + 1);
        folder.resize(slash);
    }

    std::vector<MCFoundFile> files;

    if (HasOverlay(gamePattern))
    {
        AddMatches(ResolveFrom(_UserRoot, folder), pattern, files, &MCFoundFile::InUserFolder);
    }

    // A launcher's folder lists only its maps and mods.
    if (!_LauncherRoot.empty() && !IsAbsoluteGamePath(gamePattern))
    {
        AddMatches(ResolveFrom(_LauncherRoot, folder), pattern, files, &MCFoundFile::InLauncherFolder,
                   [&folder](std::string_view name)
                   {
                       return MCFileSystem::IsLauncherContent(folder.empty() ? std::string(name)
                                                                             : std::format("{}\\{}", folder, name));
                   });
    }

    AddMatches(ResolveFrom(_Root, folder), pattern, files, &MCFoundFile::InGameFolder);
    std::ranges::sort(files, MCFileSystem::NameLess, &MCFoundFile::Name);
    return files;
}

bool MCDiskFileSource::Exists(std::string_view gamePath)
{
    std::error_code error;
    return std::filesystem::is_regular_file(Resolve(gamePath), error);
}

bool MCDiskFileSource::RemoveFile(std::string_view gamePath)
{
    std::error_code error;
    return std::filesystem::remove(ResolveWrite(gamePath, false), error);
}

bool MCDiskFileSource::RemoveSave(std::string_view gamePath)
{
    std::error_code error;

    if (HasOverlay(gamePath))
    {
        std::filesystem::remove(ResolveFrom(_UserRoot, gamePath), error);
    }

    std::filesystem::remove(ResolveFrom(_Root, gamePath), error);
    return !Exists(gamePath);
}

bool MCDiskFileSource::RenameFile(std::string_view fromGamePath, std::string_view toGamePath)
{
    std::error_code error;
    std::filesystem::rename(ResolveWrite(fromGamePath, true), ResolveWrite(toGamePath, false), error);
    return !error;
}

bool MCDiskFileSource::CopyFile(std::string_view fromGamePath, std::string_view toGamePath)
{
    std::error_code error;
    return std::filesystem::copy_file(Resolve(fromGamePath), ResolveWrite(toGamePath, false),
                                      std::filesystem::copy_options::overwrite_existing, error);
}

namespace MCFileSystem
{
    namespace
    {
        MCFileSource& Files()
        {
            return MCGameContext::Current().Files();
        }
    }

    /// <summary>Case-insensitive DOS wildcard match (<c>*</c> and <c>?</c>).</summary>
    bool WildcardMatch(std::string_view pattern, std::string_view name)
    {
        if (pattern.empty())
        {
            return name.empty();
        }

        if (pattern.front() == '*')
        {
            for (size_t skipped = 0;; ++skipped)
            {
                if (WildcardMatch(pattern.substr(1), name.substr(skipped)))
                {
                    return true;
                }

                if (skipped == name.size())
                {
                    return false;
                }
            }
        }

        if (name.empty())
        {
            return false;
        }

        if (pattern.front() != '?' && std::tolower(static_cast<unsigned char>(pattern.front())) !=
                                          std::tolower(static_cast<unsigned char>(name.front())))
        {
            return false;
        }

        return WildcardMatch(pattern.substr(1), name.substr(1));
    }

    bool NameLess(std::string_view a, std::string_view b)
    {
        if (std::ranges::lexicographical_compare(a, b, {}, AsciiUpper, AsciiUpper))
        {
            return true;
        }

        if (std::ranges::lexicographical_compare(b, a, {}, AsciiUpper, AsciiUpper))
        {
            return false;
        }

        return std::ranges::lexicographical_compare(a, b, {}, Byte, Byte);
    }

    void SetGameRoot(const std::filesystem::path& root)
    {
        Files().SetGameRoot(root);
    }

    const std::filesystem::path& GameRoot()
    {
        return Files().GameRoot();
    }

    void SetUserRoot(const std::filesystem::path& root)
    {
        Files().SetUserRoot(root);
    }

    const std::filesystem::path& UserRoot()
    {
        return Files().UserRoot();
    }

    void SetLauncherRoot(const std::filesystem::path& root)
    {
        Files().SetLauncherRoot(root);
    }

    const std::filesystem::path& LauncherRoot()
    {
        return Files().LauncherRoot();
    }

    bool IsLauncherContent(std::string_view gamePath)
    {
        if (IsAbsoluteGamePath(gamePath))
        {
            return false;
        }

        std::vector<std::string> parts = SplitGamePath(gamePath);
        std::erase(parts, ".");

        // An archive in the folder's root, but the original's five.
        if (parts.size() == 1)
        {
            return IsKind(parts[0], ArchiveKind) &&
                   std::ranges::none_of(BaseArchives, [&](std::string_view base) { return MCIEquals(parts[0], base); });
        }

        // A mission in data\savegame.
        return parts.size() == MissionFolder.size() + 1 && IsKind(parts.back(), MissionKind) &&
               std::ranges::equal(std::span(parts).first(MissionFolder.size()), MissionFolder, MCIEquals);
    }

    bool SetSettingsRoot(const std::filesystem::path& root)
    {
        SettingsFolder().clear();

        if (root.empty())
        {
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(root, error);

        // The settings are opened by the game's own paths, which reach a drive's root and never a network share's
        // (\\server\share): a folder they don't reach, or one that can't be made, leaves them in the user folder.
        if (!std::filesystem::equivalent(Resolve(ToUtf8(root)), root, error))
        {
            SDL_Log("Settings: the player's folder (%s) can't be made, or the game's paths don't reach it: the user "
                    "folder keeps them",
                    ToUtf8(root).c_str());
            return false;
        }

        SettingsFolder() = root;
        return true;
    }

    std::string SettingsToRead(std::string_view name)
    {
        const std::filesystem::path& root = SettingsFolder();

        if (root.empty())
        {
            return std::string(name);
        }

        // Found without regard to case, as the save finds the file it replaces.
        const std::filesystem::path path = root / FromUtf8(name);
        std::error_code error;
        return std::filesystem::is_regular_file(Resolve(ToUtf8(path)), error) ? ToUtf8(path) : std::string(name);
    }

    std::string SettingsToWrite(std::string_view name)
    {
        const std::filesystem::path& root = SettingsFolder();
        return root.empty() ? std::string(name) : ToUtf8(root / FromUtf8(name));
    }

    std::filesystem::path Resolve(std::string_view gamePath)
    {
        return Files().Resolve(gamePath);
    }

    bool IsContainedPath(std::string_view gamePath)
    {
        if (gamePath.empty() || IsAbsoluteGamePath(gamePath) || gamePath.find(':') != std::string_view::npos)
        {
            return false;
        }

        return std::ranges::none_of(SplitGamePath(gamePath), [](const std::string& part) { return part == ".."; });
    }

    std::filesystem::path ResolveWrite(std::string_view gamePath, bool copyExisting)
    {
        return Files().ResolveWrite(gamePath, copyExisting);
    }

    bool MakeDirectory(std::string_view gamePath)
    {
        return Files().MakeDirectory(gamePath);
    }

    bool RemoveDirectory(std::string_view gamePath)
    {
        return Files().RemoveDirectory(gamePath);
    }

    std::vector<std::string> FindFiles(std::string_view gamePattern)
    {
        return Files().FindFiles(gamePattern);
    }

    std::vector<MCFoundFile> FindFileSources(std::string_view gamePattern)
    {
        return Files().FindFileSources(gamePattern);
    }

    std::optional<std::span<const uint8_t>> FindImage(std::string_view gamePath)
    {
        return Files().FindImage(gamePath);
    }

    bool Exists(std::string_view gamePath)
    {
        return Files().Exists(gamePath);
    }

    bool RemoveFile(std::string_view gamePath)
    {
        return Files().RemoveFile(gamePath);
    }

    bool RemoveSave(std::string_view gamePath)
    {
        return Files().RemoveSave(gamePath);
    }

    bool RenameFile(std::string_view fromGamePath, std::string_view toGamePath)
    {
        return Files().RenameFile(fromGamePath, toGamePath);
    }

    bool CopyFile(std::string_view fromGamePath, std::string_view toGamePath)
    {
        return Files().CopyFile(fromGamePath, toGamePath);
    }
}
