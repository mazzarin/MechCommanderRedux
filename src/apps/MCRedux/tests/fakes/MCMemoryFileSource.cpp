#include "stdafx.h"
#include "MCMemoryFileSource.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>Tells the scratch folders of the sources of one run apart.</summary>
    std::atomic<uint32_t> ScratchCount = 0;

    /// <summary>The file name of a game path (after its last <c>\</c> or <c>/</c>).</summary>
    std::string_view LastPart(std::string_view gamePath)
    {
        const size_t slash = gamePath.find_last_of("\\/");
        return slash == std::string_view::npos ? gamePath : gamePath.substr(slash + 1);
    }

    void WriteBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

MCMemoryFileSource::MCMemoryFileSource()
    : _Scratch(std::filesystem::temp_directory_path() / "mc_tests_memfs" /
               std::format("{}_{}", MCPort::ProcessId(), ScratchCount++))
{
    std::error_code error;
    std::filesystem::remove_all(_Scratch, error);
    std::filesystem::create_directories(_Scratch, error);
}

MCMemoryFileSource::~MCMemoryFileSource()
{
    std::error_code error;
    std::filesystem::remove_all(_Scratch, error);
}

std::string MCMemoryFileSource::Key(std::string_view gamePath)
{
    std::string key;
    key.reserve(gamePath.size());

    for (const char c : gamePath)
    {
        const char normal = c == '/' ? '\\' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (normal == '\\' && (key.empty() || key.back() == '\\'))
        {
            continue;
        }

        key.push_back(normal);
    }

    while (key.starts_with(".\\"))
    {
        key.erase(0, 2);
    }

    return key;
}

void MCMemoryFileSource::AddFile(std::string_view gamePath, std::vector<uint8_t> bytes)
{
    _Install[Key(gamePath)] = Entry{std::string(LastPart(gamePath)), std::move(bytes)};
}

void MCMemoryFileSource::AddFile(std::string_view gamePath, std::string_view text)
{
    AddFile(gamePath, std::vector<uint8_t>(text.begin(), text.end()));
}

void MCMemoryFileSource::AddUserFile(std::string_view gamePath, std::vector<uint8_t> bytes)
{
    _User[Key(gamePath)] = Entry{std::string(LastPart(gamePath)), std::move(bytes)};
}

MCOptionalRef<const MCMemoryFileSource::Entry> MCMemoryFileSource::Find(const std::string& key) const
{
    if (const auto user = _User.find(key); user != _User.end())
    {
        return user->second;
    }

    if (const auto install = _Install.find(key); install != _Install.end())
    {
        return install->second;
    }

    return std::nullopt;
}

std::filesystem::path MCMemoryFileSource::ScratchPath(std::string_view gamePath) const
{
    std::filesystem::path path = _Scratch;

    for (const auto part : std::views::split(Key(gamePath), '\\'))
    {
        path /= std::string_view(part.begin(), part.end());
    }

    return path;
}

std::optional<std::vector<uint8_t>> MCMemoryFileSource::ReadAny(std::string_view gamePath)
{
    const std::filesystem::path scratch = ScratchPath(gamePath);
    std::error_code error;

    if (std::filesystem::is_regular_file(scratch, error))
    {
        std::ifstream file(scratch, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    if (const MCOptionalRef<const Entry> entry = Find(Key(gamePath)))
    {
        return entry->get().Bytes;
    }

    return std::nullopt;
}

void MCMemoryFileSource::SetGameRoot(const std::filesystem::path& root)
{
    _GameRoot = root;
}

const std::filesystem::path& MCMemoryFileSource::GameRoot() const
{
    return _GameRoot;
}

void MCMemoryFileSource::SetUserRoot(const std::filesystem::path& root)
{
    static_cast<void>(root);
}

const std::filesystem::path& MCMemoryFileSource::UserRoot() const
{
    return _Scratch;
}

std::filesystem::path MCMemoryFileSource::Resolve(std::string_view gamePath)
{
    return ScratchPath(gamePath);
}

std::filesystem::path MCMemoryFileSource::ResolveWrite(std::string_view gamePath, bool copyExisting)
{
    const std::filesystem::path scratch = ScratchPath(gamePath);
    std::error_code error;
    std::filesystem::create_directories(scratch.parent_path(), error);

    if (copyExisting && !std::filesystem::exists(scratch, error))
    {
        if (const MCOptionalRef<const Entry> entry = Find(Key(gamePath)))
        {
            WriteBytes(scratch, entry->get().Bytes);
        }
    }

    return scratch;
}

bool MCMemoryFileSource::MakeDirectory(std::string_view gamePath)
{
    const std::filesystem::path folder = ScratchPath(gamePath);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return std::filesystem::is_directory(folder, error);
}

bool MCMemoryFileSource::RemoveDirectory(std::string_view gamePath)
{
    const std::filesystem::path folder = ScratchPath(gamePath);
    std::error_code error;

    if (!std::filesystem::is_directory(folder, error) || !std::filesystem::is_empty(folder, error))
    {
        return false;
    }

    return std::filesystem::remove(folder, error);
}

std::vector<std::string> MCMemoryFileSource::FindFiles(std::string_view gamePattern)
{
    std::vector<std::string> names;

    for (MCFoundFile& file : FindFileSources(gamePattern))
    {
        names.push_back(std::move(file.Name));
    }

    return names;
}

std::vector<MCFoundFile> MCMemoryFileSource::FindFileSources(std::string_view gamePattern)
{
    const std::string key = Key(gamePattern);
    const size_t slash = key.find_last_of('\\');
    const std::string folder = slash == std::string::npos ? std::string() : key.substr(0, slash + 1);
    const std::string pattern = slash == std::string::npos ? key : key.substr(slash + 1);
    std::vector<MCFoundFile> files;

    // The scratch folder and the user layer are the user folder; the install layer is the game's folder.
    const auto add = [&](const std::string& name, bool userFolder)
    {
        if (!MCFileSystem::WildcardMatch(pattern, name))
        {
            return;
        }

        const auto listed = std::ranges::find_if(files, [&](const MCFoundFile& other)
                                                 { return MCPort::StrICmp(other.Name, name) == 0; });
        MCFoundFile& file = listed != files.end() ? *listed : files.emplace_back(MCFoundFile{.Name = name});
        (userFolder ? file.InUserFolder : file.InGameFolder) = true;
    };

    std::error_code error;
    const std::filesystem::path scratchFolder = ScratchPath(folder);

    if (std::filesystem::is_directory(scratchFolder, error))
    {
        for (const auto& entry : std::filesystem::directory_iterator(scratchFolder, error))
        {
            if (entry.is_regular_file(error))
            {
                add(MCFileSystem::ToUtf8(entry.path().filename()), true);
            }
        }
    }

    const auto addLayer = [&](const std::map<std::string, Entry>& layer, bool userFolder)
    {
        for (const auto& [fileKey, entry] : layer)
        {
            if (fileKey.starts_with(folder) && fileKey.find('\\', folder.size()) == std::string::npos)
            {
                add(entry.Name, userFolder);
            }
        }
    };

    addLayer(_User, true);
    addLayer(_Install, false);
    return files;
}

bool MCMemoryFileSource::Exists(std::string_view gamePath)
{
    std::error_code error;
    return std::filesystem::is_regular_file(ScratchPath(gamePath), error) || Find(Key(gamePath)).has_value();
}

bool MCMemoryFileSource::RemoveFile(std::string_view gamePath)
{
    std::error_code error;
    const bool removedScratch = std::filesystem::remove(ScratchPath(gamePath), error);
    const bool removedUser = _User.erase(Key(gamePath)) != 0;
    return removedScratch || removedUser;
}

bool MCMemoryFileSource::RemoveSave(std::string_view gamePath)
{
    RemoveFile(gamePath);
    _Install.erase(Key(gamePath));
    return !Exists(gamePath);
}

bool MCMemoryFileSource::RenameFile(std::string_view fromGamePath, std::string_view toGamePath)
{
    std::optional<std::vector<uint8_t>> bytes = ReadAny(fromGamePath);

    if (!bytes)
    {
        return false;
    }

    WriteBytes(ScratchPath(toGamePath), *bytes);
    std::error_code error;
    std::filesystem::remove(ScratchPath(fromGamePath), error);
    _User.erase(Key(fromGamePath));
    return true;
}

bool MCMemoryFileSource::CopyFile(std::string_view fromGamePath, std::string_view toGamePath)
{
    std::optional<std::vector<uint8_t>> bytes = ReadAny(fromGamePath);

    if (!bytes)
    {
        return false;
    }

    WriteBytes(ScratchPath(toGamePath), *bytes);
    return true;
}

std::optional<std::span<const uint8_t>> MCMemoryFileSource::FindImage(std::string_view gamePath)
{
    std::error_code error;

    if (std::filesystem::is_regular_file(ScratchPath(gamePath), error))
    {
        return std::nullopt;
    }

    if (const MCOptionalRef<const Entry> entry = Find(Key(gamePath)))
    {
        return std::span<const uint8_t>(entry->get().Bytes);
    }

    return std::nullopt;
}
