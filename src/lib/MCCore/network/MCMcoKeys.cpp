#include "stdafx.h"
#include "network/MCMcoKeys.h"

#include "platform/MCUtf8Path.h"
#include "update/MCUpdateDisk.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <system_error>

namespace
{
    /// <summary>The file a save is written to before it's moved over the sign-in's.</summary>
    constexpr std::string_view WritingSuffix = ".writing";
    /// <summary>Far more than a sign-in's line: a file this long isn't one, and isn't read.</summary>
    constexpr std::uintmax_t KeptMostBytes = 4096;

    /// <summary>Overwrites the sign-in's text before it's let go.</summary>
    void Wipe(std::string& text)
    {
        std::ranges::fill(text, '\0');
        text.clear();
    }

    /// <summary>Writes <paramref name="text"/> as a new file only its owner may read; whether it could (logged).</summary>
    bool WriteOwnerOnly(const std::filesystem::path& path, std::string_view text)
    {
        auto file = MCNewFile::Create(path, MCNewFile::MCReaders::Owner);

        if (!file)
        {
            SDL_Log("MechCommander Online: the sign-in can't be written (%s)", file.error().c_str());
            return false;
        }

        return file->WriteAt(0, text) && file->Close();
    }
}

std::optional<MCMcoKept> MCMcoKeysFile::Load()
{
    const std::scoped_lock lock(_Lock);
    const std::string shown = MCFileSystem::ToUtf8(_File);
    std::error_code error;

    if (!std::filesystem::exists(_File, error))
    {
        if (error)
        {
            SDL_Log("MechCommander Online: %s can't be looked at (%s)", shown.c_str(), error.message().c_str());
        }

        return std::nullopt;
    }

    if (const std::uintmax_t size = std::filesystem::file_size(_File, error); error || size > KeptMostBytes)
    {
        SDL_Log("MechCommander Online: %s isn't read: %s", shown.c_str(),
                error ? error.message().c_str() : "it's too long to be a sign-in");
        return std::nullopt;
    }

    auto text = MakeDisk()->ReadText(_File);

    if (!text)
    {
        SDL_Log("MechCommander Online: the sign-in isn't read (%s)", text.error().c_str());
        return std::nullopt;
    }

    std::optional<MCMcoKept> kept = MCMcoKeptFrom(*text);
    Wipe(*text);

    if (!kept)
    {
        SDL_Log("MechCommander Online: %s isn't a sign-in; the next one kept replaces it", shown.c_str());
    }

    return kept;
}

bool MCMcoKeysFile::Save(const MCMcoKept& kept)
{
    const std::scoped_lock lock(_Lock);
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const std::filesystem::path folder = _File.parent_path();
    std::filesystem::path writing = _File;
    writing += WritingSuffix;

    std::string text = MCMcoKeptText(kept);
    const bool saved = disk->MakeFolders(folder) && WriteOwnerOnly(writing, text) && disk->FlushFile(writing) &&
                       disk->Rename(writing, _File) && disk->FlushFolder(folder);
    Wipe(text);

    if (!saved)
    {
        SDL_Log("MechCommander Online: the sign-in couldn't be kept in %s", MCFileSystem::ToUtf8(_File).c_str());
        // A half-written save holds tokens too. Only its owner can read it, and the next save replaces it, so one
        // that can't be removed now is left.
        std::error_code error;
        std::filesystem::remove(writing, error);
    }

    return saved;
}

void MCMcoKeysFile::Erase()
{
    const std::scoped_lock lock(_Lock);
    std::error_code error;
    std::filesystem::remove(_File, error);

    if (error)
    {
        SDL_Log("MechCommander Online: the sign-in in %s couldn't be removed (%s)", MCFileSystem::ToUtf8(_File).c_str(),
                error.message().c_str());
    }
}
