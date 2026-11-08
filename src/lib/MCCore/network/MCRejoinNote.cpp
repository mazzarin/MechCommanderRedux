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
    // The lobby's own words alone: the note starts nothing else (a file written, a mission loaded). A rejoin's start
    // has the switch already.
    const std::string words = LobbyWords(args.CommandLine);

    if (args.Rejoin)
    {
        return words;
    }

    return words.empty() ? std::string(RejoinSwitch) : std::format("{} {}", words, RejoinSwitch);
}

auto MCRejoinNote::InternetRoomCommandLine(const MCInternetRoom& room) -> std::string
{
    std::vector<std::string> words;

    words = room.Hosting ? std::vector<std::string>{"-host", "-internet"}
                         : std::vector<std::string>{"-join", room.Code, "-internet"};

    // A public game's guest comes back to it through relays only, as it played it.
    if (room.Public)
    {
        words.emplace_back(PublicSwitch);
    }

    if (room.Hosting)
    {
        words.insert(words.end(), {"-player", room.PlayerName, "-session", room.SessionName, "-maxplayers",
                                   std::to_string(room.MaxPlayers), "-place", room.Code, room.Token});
    }
    else
    {
        words.insert(words.end(), {"-player", room.PlayerName});
    }

    words.emplace_back("-menus");

    if (room.Rejoin)
    {
        words.emplace_back(RejoinSwitch);
    }

    return LobbyCommandWords(words);
}

auto MCRejoinNote::Write(const std::filesystem::path& userFolder, const MCNote& note)
    -> std::expected<void, std::string>
{
    const std::filesystem::path path = userFolder / FileName;
    MCUpdateJson::MCJson object = MCUpdateJson::MCJson::object();
    object[std::string(CommandLineKey)] = note.CommandLine;
    object[std::string(BuildKey)] = note.Build;
    object[std::string(TimeKey)] = note.Time;
    const std::string text = object.dump() + "\n";

    // One its reader would take as no note.
    if (text.size() > MaxNoteBytes)
    {
        return std::unexpected(std::format("{} would be too big ({} bytes)", MCFileSystem::ToUtf8(path), text.size()));
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
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
    // Only the lobby's own words, again: anything that can write to the user folder can write a note.
    const std::string words = note ? LobbyWords(note->CommandLine) : std::string();

    if (!note || !StartsInLobby(words))
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
    // What it starts, not its words: a host's place's token is among them.
    SDL_Log("Rejoin: starting as the note says: %s", LobbyStartLogged(words).c_str());
    return words;
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
