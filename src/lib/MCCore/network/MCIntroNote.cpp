#include "stdafx.h"
#include "network/MCIntroNote.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>A note bigger than this isn't one (it holds one number).</summary>
    constexpr std::uintmax_t MaxNoteBytes = 64;
}

bool MCIntroNote::SkipIntro = false;

auto MCIntroNote::Write(const std::filesystem::path& userFolder, int64_t now) -> std::expected<void, std::string>
{
    const std::filesystem::path path = userFolder / FileName;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << now << "\n";
    file.close();

    if (!file)
    {
        return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(path)));
    }

    return {};
}

auto MCIntroNote::TakeAtStart(const std::filesystem::path& userFolder, int64_t now) -> bool
{
    const std::filesystem::path path = userFolder / FileName;
    std::error_code error;

    if (!std::filesystem::is_regular_file(path, error))
    {
        return false;
    }

    int64_t written = 0;
    bool read = false;

    if (std::filesystem::file_size(path, error) <= MaxNoteBytes && !error)
    {
        std::ifstream file(path, std::ios::binary);
        read = static_cast<bool>(file >> written);
    }

    if (!std::filesystem::remove(path, error))
    {
        SDL_Log("The intro note %s couldn't be removed: %s", MCFileSystem::ToUtf8(path).c_str(),
                error.message().c_str());
    }

    const int64_t age = now - written;
    return read && age >= 0 && age <= Life.count();
}
