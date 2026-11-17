#include "stdafx.h"
#include "lib/MCReleaseData.h"
#include "platform/MCFileSystem.h"
#include "update/MCInstalled.h"
#include "update/MCReleaseCopy.h"
#include "update/MCUpdateDisk.h"

#include <atomic>
#include <map>
#include <set>

namespace
{
    constexpr std::string_view ArchiveKind = ".FST";

    struct State
    {
        /// <summary>The copy's loose files, by their game paths keyed (<see cref="Key"/>).</summary>
        std::map<std::string, std::filesystem::path, std::less<>> Loose;
        /// <summary>The copy's archives.</summary>
        MCFastFileSet Archives;
        std::vector<std::string> Lacking;
        bool Has = false;
        std::atomic<bool> Playing = false;
        std::atomic<uint32_t> Switches = 0;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary><paramref name="gamePath"/> as the copy's index keys it: upper case, <c>\</c> apart (as an archive's).</summary>
    std::string Key(std::string_view gamePath)
    {
        std::string key(gamePath);

        for (char& c : key)
        {
            if (c == '/')
            {
                c = '\\';
            }
            else if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }

        return key;
    }
}

void MCReleaseData::Open(const std::filesystem::path& gameFolder)
{
    Close();
    State& state = TheState();
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const auto record = MCReleaseCopy::Read(*disk, gameFolder);

    if (!record)
    {
        SDL_Log("Release copy: %s: a match reads the game folder", record.error().c_str());
        return;
    }

    if (!record->has_value())
    {
        return;
    }

    const std::filesystem::path folder = MCReleaseCopy::FolderIn(gameFolder);
    state.Lacking = (*record)->Lacking;

    for (const auto& [folded, held] : (*record)->Files)
    {
        const std::filesystem::path path = folder / MCFileSystem::FromUtf8(held.Path);

        // Changed since it was filled: not the release's, so a match reads the game folder's (the next start fills it).
        if (const MCUpdateDisk::MCStat stat = disk->Stat(path);
            !stat.Exists || stat.Size != held.Size || stat.Written != held.Written)
        {
            SDL_Log("Release copy: %s changed since it was filled: a match reads the game folder's", held.Path.c_str());
            state.Lacking.push_back(held.Path);
            continue;
        }

        if (held.Path.find('/') == std::string::npos && Key(held.Path).ends_with(ArchiveKind))
        {
            if (const auto opened = state.Archives.Open(MCFileSystem::ToUtf8(path)); !opened)
            {
                SDL_Log("Release copy: %s left out: %s", held.Path.c_str(), opened.error().c_str());
                state.Lacking.push_back(held.Path);
            }

            continue;
        }

        state.Loose.insert_or_assign(Key(held.Path), path);
    }

    state.Has = !state.Loose.empty() || !state.Archives.Files().empty();
    SDL_Log("Release copy: %zu files and %zu archives, %zu lacking", state.Loose.size(), state.Archives.Files().size(),
            state.Lacking.size());
}

void MCReleaseData::Close()
{
    State& state = TheState();
    state.Playing = false;
    state.Has = false;
    state.Loose.clear();
    state.Archives.Clear();
    state.Lacking.clear();
}

bool MCReleaseData::Has()
{
    return TheState().Has;
}

bool MCReleaseData::Whole()
{
    return TheState().Lacking.empty();
}

const std::vector<std::string>& MCReleaseData::Lacking()
{
    return TheState().Lacking;
}

bool MCReleaseData::SetPlaying(bool playing)
{
    State& state = TheState();
    const bool now = playing && state.Has;

    if (state.Playing.exchange(now) == now)
    {
        return false;
    }

    ++state.Switches;
    SDL_Log("Release copy: %s", now ? "a match reads the release's data" : "the game folder's data is read again");
    return true;
}

bool MCReleaseData::Playing()
{
    return TheState().Playing;
}

uint32_t MCReleaseData::Switches()
{
    return TheState().Switches;
}

MCReleaseData::MCHeld MCReleaseData::Find(std::string_view gamePath)
{
    State& state = TheState();

    if (!state.Playing)
    {
        return {};
    }

    const std::string key = Key(gamePath);

    if (const auto loose = state.Loose.find(key); loose != state.Loose.end())
    {
        return {.Loose = loose->second};
    }

    for (const std::unique_ptr<MCFastFile>& archive : state.Archives.Files())
    {
        if (archive->Find(key).has_value())
        {
            return {.InArchive = true};
        }
    }

    return {};
}

std::optional<std::vector<uint8_t>> MCReleaseData::ReadArchived(std::string_view gamePath)
{
    return TheState().Archives.Read(Key(gamePath));
}

std::vector<std::string> MCReleaseData::NamesIn(std::string_view folder)
{
    State& state = TheState();
    std::string prefix = Key(folder);

    if (!prefix.empty() && !prefix.ends_with('\\'))
    {
        prefix += '\\';
    }

    std::set<std::string> keys;
    std::vector<std::string> names;
    const auto add = [&](std::string_view name)
    {
        const std::string key = Key(name);

        if (key.size() > prefix.size() && key.starts_with(prefix) &&
            key.find('\\', prefix.size()) == std::string::npos && keys.insert(key).second)
        {
            names.emplace_back(name.substr(prefix.size()));
        }
    };

    for (const auto& [key, path] : state.Loose)
    {
        // In the folder itself: named as the game folder spells it.
        if (key.size() > prefix.size() && key.starts_with(prefix) && key.find('\\', prefix.size()) == std::string::npos)
        {
            add(prefix + MCFileSystem::ToUtf8(path.filename()));
        }
    }

    for (const std::unique_ptr<MCFastFile>& archive : state.Archives.Files())
    {
        for (const MCFileEntry& entry : archive->GetEntries())
        {
            add(entry.GetName());
        }
    }

    return names;
}
