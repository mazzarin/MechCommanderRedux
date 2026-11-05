#include "stdafx.h"
#include "network/MCRejoinNote.h"
#include "gui/MCGuiStartup.h"
#include "linkup/MCSessionManager.h"
#include "platform/MCFileSystem.h"
#include "update/MCUpdateJson.h"

namespace
{
    /// <summary>The note's keys.</summary>
    constexpr std::string_view CommandLineKey = "commandLine";
    constexpr std::string_view BuildKey = "build";
    constexpr std::string_view TimeKey = "time";

    /// <summary>The most a note may hold: a few words, so anything bigger isn't one.</summary>
    constexpr uintmax_t MaxNoteBytes = 4096;

    /// <summary><paramref name="word"/> as one word of a command line: its quotes dropped, in quotes when it has a space.</summary>
    std::string Word(std::string_view word)
    {
        std::string kept;
        std::ranges::copy_if(word, std::back_inserter(kept), [](char c) { return c != '"'; });
        return kept.find_first_of(" \t") == std::string::npos ? kept : std::format("\"{}\"", kept);
    }

    /// <summary>Deletes the note at <paramref name="path"/>, saying why (a failure only logged: it ages out).</summary>
    void Delete(const std::filesystem::path& path, std::string_view why)
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        SDL_Log("Rejoin: the note %s (%.*s)%s", MCFileSystem::ToUtf8(path).c_str(), static_cast<int>(why.size()),
                why.data(), error ? std::format(" can't be deleted: {}", error.message()).c_str() : " is deleted");
    }

    /// <summary>Deletes the note in <paramref name="userFolder"/>, if there is one, saying why.</summary>
    void DeleteAny(const std::filesystem::path& userFolder, std::string_view why)
    {
        const std::filesystem::path path = userFolder / MCRejoinNote::FileName;
        std::error_code error;

        if (std::filesystem::exists(path, error))
        {
            Delete(path, why);
        }
    }

    /// <summary>The note at <paramref name="path"/>, or why it isn't one.</summary>
    std::expected<MCRejoinNote::MCNote, std::string> Read(const std::filesystem::path& path)
    {
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);

        if (error || size > MaxNoteBytes)
        {
            return std::unexpected(error ? error.message() : std::string("too big"));
        }

        std::ifstream file(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (!file.good() && !file.eof())
        {
            return std::unexpected("can't be read");
        }

        return MCUpdateJson::Read(MCFileSystem::ToUtf8(path.filename()),
                                  [&]
                                  {
                                      const MCUpdateJson::MCJson object = MCUpdateJson::Object(text);
                                      const uint64_t time = MCUpdateJson::Count(object, TimeKey);

                                      if (time > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                                      {
                                          MCUpdateJson::Fail("its time is out of range");
                                      }

                                      return MCRejoinNote::MCNote{.CommandLine =
                                                                      MCUpdateJson::Text(object, CommandLineKey),
                                                                  .Build = MCUpdateJson::Count(object, BuildKey),
                                                                  .Time = static_cast<int64_t>(time)};
                                  });
    }
}

auto MCRejoinNote::LobbyCommandLine(const MCLobbyLaunchArgs& args) -> std::string
{
    std::vector<std::string> words;

    if (args.IsHost)
    {
        words.emplace_back("-host");

        // A host's address is its port after a colon.
        if (args.Address.size() > 1 && args.Address.front() == ':')
        {
            words.emplace_back("-port");
            words.push_back(Word(std::string_view(args.Address).substr(1)));
        }
    }
    else
    {
        words.emplace_back("-join");

        if (!args.Address.empty())
        {
            words.push_back(Word(args.Address));
        }
    }

    if (!args.PlayerName.empty())
    {
        words.emplace_back("-player");
        words.push_back(Word(args.PlayerName));
    }

    if (!args.SessionName.empty())
    {
        words.emplace_back("-session");
        words.push_back(Word(args.SessionName));
    }

    words.emplace_back("-maxplayers");
    words.push_back(std::to_string(args.MaxPlayers));
    words.emplace_back(RejoinSwitch);
    std::string line;

    for (const std::string& word : words)
    {
        line += line.empty() ? word : " " + word;
    }

    return line;
}

auto MCRejoinNote::Write(const std::filesystem::path& userFolder, const MCNote& note)
    -> std::expected<void, std::string>
{
    const std::filesystem::path path = userFolder / FileName;
    MCUpdateJson::MCJson object = MCUpdateJson::MCJson::object();
    object[std::string(CommandLineKey)] = note.CommandLine;
    object[std::string(BuildKey)] = note.Build;
    object[std::string(TimeKey)] = note.Time;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << object.dump() << "\n";
    file.close();

    if (!file)
    {
        return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(path)));
    }

    return {};
}

auto MCRejoinNote::TakeAtStart(const std::filesystem::path& userFolder, uint64_t build, int64_t now)
    -> std::optional<std::string>
{
    const std::filesystem::path path = userFolder / FileName;
    std::error_code error;

    if (!std::filesystem::exists(path, error))
    {
        return std::nullopt;
    }

    const auto note = Read(path);

    if (!note || !StartsInLobby(note->CommandLine))
    {
        Delete(path, note ? std::string("not a lobby's words") : note.error());
        return std::nullopt;
    }

    const int64_t age = now - note->Time;

    if (age < -Life.count() || age >= Life.count())
    {
        Delete(path, std::format("{} seconds old", age));
        return std::nullopt;
    }

    if (note->Build != build)
    {
        SDL_Log("Rejoin: the note is for build %llu, not this one (%llu): left for it",
                static_cast<unsigned long long>(note->Build), static_cast<unsigned long long>(build));
        return std::nullopt;
    }

    Delete(path, "taken");
    SDL_Log("Rejoin: starting as the note says: %s", note->CommandLine.c_str());
    return note->CommandLine;
}

auto MCRejoinNote::Discard(const std::filesystem::path& userFolder) -> void
{
    DeleteAny(userFolder, "a lobby's start with its own words");
}

auto MCRejoinNote::Withdraw(const std::filesystem::path& userFolder) -> void
{
    DeleteAny(userFolder, "the restart is off");
}

auto MCRejoinNote::Now() -> int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
