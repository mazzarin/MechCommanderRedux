#include "stdafx.h"
#include "MCMcoProbe.h"

#include "MCConsole.h"
#include "MCGameFolder.h"
#include "MCHttpGet.h"
#include "MCMcoMatch.h"
#include "MCVersion.h"
#include "lib/MCRandomId.h"
#include "network/MCMcoApi.h"
#include "update/MCHttpSource.h"
#include "platform/MCFileSystem.h"

#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view GuestWord = "guest";
    constexpr std::string_view SignInWord = "sign-in";
    constexpr std::string_view CreateWord = "create";
    constexpr std::string_view JoinWord = "join";
    constexpr std::string_view HostWord = "host";
    constexpr std::string_view NameOption = "--name";
    constexpr std::string_view SecondsOption = "--seconds";
    constexpr std::string_view UserOption = "--user";
    constexpr std::string_view ServiceOption = "--service";
    constexpr std::string_view RoomOption = "--room";
    constexpr std::string_view PasswordOption = "--password";
    constexpr std::string_view PlayersOption = "--players";
    constexpr std::string_view DataOption = "--data";
    constexpr std::string_view RelayCaOption = "--relay-ca";
    constexpr std::string_view NoLaunchOption = "--no-launch";
    constexpr std::string_view UsageText =
        "Usage: --mco-probe guest | sign-in | create [--name NAME] [--seconds N] [--user FOLDER] [--service ADDRESS/]\n"
        "       --mco-probe join | host [--name NAME] [--room ROOM] [--password WORD] [--players N] [--data FOLDER]\n"
        "                   [--user FOLDER] [--service ADDRESS/] [--relay-ca FILE] [--no-launch]\n"
        "join and host play as a guest: join a Redux room (ROOM, else picked), or host one (its name ROOM, else asked),\n"
        "and play its matches with this game, carried over MCO's relay."
#ifdef _WIN32
        "\nStart it with start /wait, so the terminal waits for it."
#endif
        ;
    /// <summary>The most a relay's trusted authority file may be (a lab's PEM).</summary>
    constexpr uintmax_t AnchorMost = 1 << 20;

    /// <summary>The save folder's file keeping the device's seed, and its line's key.</summary>
    constexpr std::string_view ConfigFile = "mco.cfg";
    constexpr std::string_view SeedKey = "DeviceSeed=";
    /// <summary>The game's save folder, as Main.cpp finds it.</summary>
    constexpr const char* SaveFolderApp = "MechCommander Redux";
    /// <summary>The lobby polled about once a second, as MCO's launcher polls it (850-1150 ms).</summary>
    constexpr auto PollEvery = std::chrono::seconds(1);
    constexpr int MostSeconds = 3600;
    constexpr std::string_view Yes = "y";

    void Say(std::string_view line)
    {
        std::cout << line << '\n' << std::flush;
    }

    std::string Shown(std::string_view text)
    {
        return MCMcoApi::Printable(text);
    }

    std::string Failure(const MCMcoApi::MCError& error)
    {
        return std::format("failed: {}", MCMcoApi::Describe(error));
    }

    struct MCSystem
    {
        std::string Family;
        std::string Release;
        std::string Build;
        std::string SystemCpu;
        std::string AppCpu;
    };

#ifdef __APPLE__
    /// <summary>A text sysctl's value, or empty.</summary>
    std::string SysctlText(const char* name)
    {
        size_t size = 0;

        if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0)
        {
            return {};
        }

        std::string value(size, '\0');

        if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0)
        {
            return {};
        }

        value.resize(std::strlen(value.c_str()));
        return value;
    }
#endif

    /// <summary>This system, named as MCO's launcher names Windows' (its client_os: .NET's CPU names).</summary>
    MCSystem ThisSystem()
    {
#if defined(__aarch64__) || defined(_M_ARM64)
        const std::string appCpu = "Arm64";
#elif defined(__x86_64__) || defined(_M_X64)
        const std::string appCpu = "X64";
#else
        const std::string appCpu = "Unknown";
#endif

#ifdef __APPLE__
        // An Intel build run by Rosetta is on an Arm64 system.
        int translated = 0;
        size_t size = sizeof(translated);
        const bool rosetta =
            sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated == 1;
        return {.Family = "macOS",
                .Release = SysctlText("kern.osproductversion"),
                .Build = SysctlText("kern.osversion"),
                .SystemCpu = rosetta ? "Arm64" : appCpu,
                .AppCpu = appCpu};
#else
        // Windows' version needs Windows.h, which this file (built with the precompiled header) can't include.
        return {.Family = "Windows", .SystemCpu = appCpu, .AppCpu = appCpu};
#endif
    }

    /// <summary>The save folder: --user's, else the game's.</summary>
    std::optional<std::filesystem::path> SaveFolder(std::string_view user)
    {
        if (!user.empty())
        {
            return MCFileSystem::FromUtf8(user);
        }

        const std::unique_ptr<char, decltype(&SDL_free)> prefPath(SDL_GetPrefPath(nullptr, SaveFolderApp), &SDL_free);

        if (prefPath == nullptr)
        {
            return std::nullopt;
        }

        return MCFileSystem::FromUtf8(prefPath.get());
    }

    /// <summary>The device's seed, kept in the save folder's mco.cfg (made there the first time).</summary>
    std::expected<std::string, std::string> DeviceSeed(const std::filesystem::path& folder)
    {
        const std::filesystem::path file = folder / ConfigFile;

        if (std::ifstream in(file); in)
        {
            std::string line;

            while (std::getline(in, line))
            {
                if (line.starts_with(SeedKey) && MCIsRandomId(line.substr(SeedKey.size())))
                {
                    return line.substr(SeedKey.size());
                }
            }
        }

        std::string seed = MCRandomId();
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << SeedKey << seed << '\n';

        if (!out)
        {
            return std::unexpected(std::format("Can't write {}", MCFileSystem::ToUtf8(file)));
        }

        return seed;
    }

    /// <summary>A line typed at the prompt, or none when there's no terminal or it ended.</summary>
    std::optional<std::string> Ask(std::string_view prompt, bool hidden = false)
    {
        return MCConsole::ReadLine(prompt, hidden);
    }

    /// <summary>A player name typed (or --name's), as MCO takes it.</summary>
    std::optional<std::string> PlayerName(std::string_view given)
    {
        std::optional<std::string> typed = given.empty() ? Ask("Player name: ") : std::string(given);

        if (!typed)
        {
            return std::nullopt;
        }

        auto name = MCMcoApi::PlayerName(*typed);

        if (!name)
        {
            Say(name.error());
            return std::nullopt;
        }

        return *name;
    }

    /// <summary>Overwrites a password typed before it's freed.</summary>
    void Wipe(std::string& text)
    {
        std::ranges::fill(text, '\0');
        text.clear();
    }

    /// <summary>The probe's run: what it signed in to and registered, undone as it ends.</summary>
    class MCProbe
    {
    public:
        MCProbe(MCMcoApi::MCService& service, int seconds) : _Service(service), _Seconds(seconds) {}

        MCProbe(const MCProbe&) = delete;
        MCProbe& operator=(const MCProbe&) = delete;

        ~MCProbe() { Leave(); }

        /// <summary>Joins or hosts a room as a guest's profile, made new, and plays its matches.</summary>
        int Play(std::string_view name, bool host, const MCMcoMatch::MCOptions& options)
        {
            const std::optional<std::string> player = PlayerName(name);

            if (!player)
            {
                return 1;
            }

            MCMcoApi::MCPeer guest{.ClientId = MCMcoApi::NewClientId(), .Username = *player};
            std::cout << std::format("Registering {} in the lobby... ", Shown(guest.Username)) << std::flush;

            if (const auto registered = _Service.Register(guest); !registered)
            {
                Say(Failure(registered.error()));
                return 1;
            }

            _Registered = guest;
            Say("registered.");
            return host ? MCMcoMatch::Host(_Service, *_Registered, options)
                        : MCMcoMatch::Join(_Service, *_Registered, options);
        }

        int Guest(std::string_view name)
        {
            const std::optional<std::string> player = PlayerName(name);

            if (!player)
            {
                return 1;
            }

            MCMcoApi::MCPeer guest{.ClientId = MCMcoApi::NewClientId(), .Username = *player};
            return RegisterAndList(std::move(guest));
        }

        int SignIn()
        {
            const std::optional<std::string> email = Ask("Email: ");
            std::optional<std::string> password = email ? Ask("Password: ", true) : std::nullopt;

            if (!password)
            {
                return 1;
            }

            std::cout << "Signing in... " << std::flush;
            const auto signIn = _Service.Login(*email, *password);
            Wipe(*password);

            if (!signIn)
            {
                Say(Failure(signIn.error()));
                return 1;
            }

            Say(std::format("signed in as {} (player {}).", Shown(signIn->Username), signIn->PlayerId));
            return ActivateAndList(*signIn);
        }

        int Create(std::string_view name)
        {
            std::cout << "Asking whether an account can be made here... " << std::flush;
            const auto can = _Service.CanCreate();

            if (!can)
            {
                Say(Failure(can.error()));
                return 1;
            }

            if (!*can)
            {
                Say("no: MCO allows three accounts a computer or address.");
                return 1;
            }

            Say("yes.");
            const std::optional<std::string> player = PlayerName(name);
            const std::optional<std::string> email = player ? Ask("Email: ") : std::nullopt;
            std::optional<std::string> password = email ? Ask("Password: ", true) : std::nullopt;
            std::optional<std::string> again = password ? Ask("Password again: ", true) : std::nullopt;

            if (!again)
            {
                return 1;
            }

            const bool same = *password == *again;
            Wipe(*again);

            if (!same)
            {
                Wipe(*password);
                Say("The passwords differ.");
                return 1;
            }

            // An account is made of a guest's registered profile, as MCO's launcher makes one.
            MCMcoApi::MCPeer guest{.ClientId = MCMcoApi::NewClientId(), .Username = *player};
            std::cout << "Registering a guest's profile... " << std::flush;

            if (const auto registered = _Service.Register(guest); !registered)
            {
                Wipe(*password);
                Say(Failure(registered.error()));
                return 1;
            }

            _Registered = guest;
            Say("registered.");
            std::cout << "Making the account... " << std::flush;
            const auto made = _Service.Create(guest, *player, *email, *password);
            Wipe(*password);

            if (!made)
            {
                Say(Failure(made.error()));
                return 1;
            }

            Say(std::format("made: {} (player {}).", Shown(made->Username), made->PlayerId));
            return ActivateAndList(*made);
        }

    private:
        /// <summary>Makes this device the account's (asking before taking it from another computer), then lists.</summary>
        int ActivateAndList(const MCMcoApi::MCSignIn& signIn)
        {
            _AccountToken = signIn.AccountToken;
            std::cout << "Making this computer the account's... " << std::flush;
            auto activated = _Service.Activate(signIn.AccountToken, false);

            if (!activated && activated.error().Code == MCMcoApi::ActiveDeviceCode)
            {
                Say("it's in use on another computer.");
                const std::optional<std::string> answer =
                    Ask("Move it to this one? Its waiting room session there ends. (y/N) ");

                if (!answer || *answer != Yes)
                {
                    Say("Left on the other computer.");
                    return 1;
                }

                std::cout << "Moving it here... " << std::flush;
                activated = _Service.Activate(signIn.AccountToken, true);
            }

            if (!activated)
            {
                Say(Failure(activated.error()));
                return 1;
            }

            Say("done.");
            MCMcoApi::MCPeer account{.ClientId = signIn.ClientId,
                                     .PeerToken = *activated,
                                     .AccountToken = signIn.AccountToken,
                                     .Username = signIn.Username};

            // A guest's profile made into another player's leaves the lobby first, as MCO's launcher's does.
            if (_Registered && _Registered->ClientId != account.ClientId)
            {
                Unregister();
            }

            return RegisterAndList(std::move(account));
        }

        int RegisterAndList(MCMcoApi::MCPeer peer)
        {
            std::cout << std::format("Registering {} in the lobby... ", Shown(peer.Username)) << std::flush;

            if (const auto registered = _Service.Register(peer); !registered)
            {
                Say(Failure(registered.error()));
                return 1;
            }

            _Registered = peer;
            Say("registered.");
            MCMcoApi::MCPeer& in = *_Registered;
            std::cout << "The build MCO's Redux rooms play: " << std::flush;

            if (const auto build = _Service.StockBuild(in); !build)
            {
                Say(Failure(build.error()));
            }
            else
            {
                Say(build->has_value() ? Shown(**build) : std::string("none named"));
            }

            const auto until = Clock::now() + std::chrono::seconds(_Seconds);
            std::string last;

            for (;;)
            {
                const auto lobby = _Service.Lobby(in);

                if (!lobby)
                {
                    Say(std::format("The lobby: {}", Failure(lobby.error())));
                    return 1;
                }

                if (const std::string listed = Listing(*lobby); listed != last)
                {
                    std::cout << listed << std::flush;
                    last = listed;
                }

                if (Clock::now() >= until)
                {
                    return 0;
                }

                std::this_thread::sleep_for(PollEvery);
            }
        }

        static std::string Listing(const MCMcoApi::MCLobby& lobby)
        {
            std::string listed =
                std::format("The lobby: {} players online, {} rooms.\n", lobby.Players, lobby.Rooms.size());

            for (const MCMcoApi::MCRoom& room : lobby.Rooms)
            {
                const bool redux = room.Engine == MCMcoApi::ReduxEngine;
                listed += std::format("  {:5} \"{}\", host {}, {}/{}, {}{}{}\n", redux ? "REDUX" : "GOLD",
                                      Shown(room.Name), Shown(room.Host), room.Players, room.MaxPlayers,
                                      room.InGame ? "in game" : "waiting", room.Locked ? ", locked" : "",
                                      redux ? std::format(", build {}", Shown(room.EngineBuild)) : std::string());
            }

            return listed;
        }

        void Unregister()
        {
            if (!_Registered)
            {
                return;
            }

            std::cout << std::format("Leaving the lobby as {}... ", Shown(_Registered->Username)) << std::flush;
            const auto left = _Service.Unregister(*_Registered);
            Say(left ? std::string("left.") : Failure(left.error()));
            _Registered.reset();
        }

        void Leave()
        {
            Unregister();

            if (_AccountToken.empty())
            {
                return;
            }

            std::cout << "Signing out... " << std::flush;
            const auto out = _Service.Logout(_AccountToken);
            Say(out ? std::string("signed out.") : Failure(out.error()));
            Wipe(_AccountToken);
        }

        MCMcoApi::MCService& _Service;
        int _Seconds = 0;
        std::optional<MCMcoApi::MCPeer> _Registered;
        std::string _AccountToken;
    };
}

int MCMcoProbe::Run(std::span<const std::string_view> arguments)
{
    if (arguments.empty())
    {
        Say(UsageText);
        return 1;
    }

    const std::string_view mode = arguments[0];
    std::string_view name;
    std::string_view user;
    std::string_view data;
    std::string_view relayCa;
    std::string site(MCMcoApi::Site);
    int seconds = 0;
    MCMcoMatch::MCOptions options;
    const auto number = [](std::string_view value, int least, int most) -> std::optional<int>
    {
        int parsed = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        return error == std::errc() && end == value.data() + value.size() && parsed >= least && parsed <= most
                   ? std::optional<int>(parsed)
                   : std::nullopt;
    };

    for (size_t i = 1; i < arguments.size(); i++)
    {
        const bool hasValue = i + 1 < arguments.size();

        if (arguments[i] == NameOption && hasValue)
        {
            name = arguments[++i];
        }
        else if (arguments[i] == UserOption && hasValue)
        {
            user = arguments[++i];
        }
        else if (arguments[i] == ServiceOption && hasValue)
        {
            // As the launcher's: https, or http to this machine (a stand-in's).
            const auto folder = MCHttpSource::FolderAddress(arguments[++i]);

            if (!folder)
            {
                Say(std::format("--service: {}", Shown(folder.error())));
                return 1;
            }

            site = *folder + "/";
        }
        else if (arguments[i] == SecondsOption && hasValue)
        {
            const std::optional<int> value = number(arguments[++i], 0, MostSeconds);

            if (!value)
            {
                Say(std::format("--seconds takes 0 to {}.", MostSeconds));
                return 1;
            }

            seconds = *value;
        }
        else if (arguments[i] == PlayersOption && hasValue)
        {
            const std::optional<int> value = number(arguments[++i], MCMcoApi::MatchFewest, MCMcoApi::RoomMost);

            if (!value)
            {
                Say(std::format("--players takes {} to {}.", MCMcoApi::MatchFewest, MCMcoApi::RoomMost));
                return 1;
            }

            options.MaxPlayers = *value;
        }
        else if (arguments[i] == RoomOption && hasValue)
        {
            options.Room = arguments[++i];
        }
        else if (arguments[i] == PasswordOption && hasValue)
        {
            options.Password = arguments[++i];
        }
        else if (arguments[i] == DataOption && hasValue)
        {
            data = arguments[++i];
        }
        else if (arguments[i] == RelayCaOption && hasValue)
        {
            relayCa = arguments[++i];
        }
        else if (arguments[i] == NoLaunchOption)
        {
            options.Launch = false;
        }
        else
        {
            Say(std::format("Unknown option {}.\n{}", Shown(arguments[i]), UsageText));
            return 1;
        }
    }

    const bool playing = mode == JoinWord || mode == HostWord;

    if (mode != GuestWord && mode != SignInWord && mode != CreateWord && !playing)
    {
        Say(UsageText);
        return 1;
    }

    const std::optional<std::filesystem::path> folder = SaveFolder(user);

    if (!folder)
    {
        Say(std::format("No save folder: {}", SDL_GetError()));
        return 1;
    }

    if (playing)
    {
        options.UserFolder = *folder;
        options.UserGiven = !user.empty();
        options.DataGiven = !data.empty();
        options.DataFolder = options.DataGiven ? MCFileSystem::FromUtf8(data) : MCGameFolder::Remembered(*folder);

        if (options.DataFolder.empty() || !MCGameFolder::HasGameData(options.DataFolder))
        {
            Say("No game data: start the game once to choose its folder, or name it with --data FOLDER.");
            return 1;
        }

        if (!relayCa.empty())
        {
            const std::filesystem::path anchor = MCFileSystem::FromUtf8(relayCa);
            std::error_code error;
            const uintmax_t size = std::filesystem::file_size(anchor, error);
            std::ifstream in(anchor, std::ios::binary);

            if (error || size > AnchorMost || !in)
            {
                Say(std::format("--relay-ca: {} can't be read.", Shown(relayCa)));
                return 1;
            }

            options.RelayAnchor.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    }

    const auto seed = DeviceSeed(*folder);

    if (!seed)
    {
        Say(seed.error());
        return 1;
    }

    const MCSystem system = ThisSystem();
    MCMcoApi::MCClient client{
        .DeviceId = MCMcoApi::DeviceId(*seed),
        .UserAgent = std::format("MechCommanderReduxHD/{} ({}{}{})", MC_VERSION_SEMVER, system.Family,
                                 system.Release.empty() ? "" : " ", system.Release),
        .ClientOs = MCMcoApi::ClientOs(system.Family, system.Release, system.Build, system.SystemCpu, system.AppCpu),
        .Chat = std::format("{}{}", site, MCMcoApi::ChatPage),
        .Account = std::format("{}{}", site, MCMcoApi::AccountPage)};
    Say(std::format("MechCommander Online probe, as {} (client {}), at {}.", client.UserAgent, client.ClientVersion,
                    Shown(site)));
    const std::unique_ptr<MCHttpSource::MCGet> transfer = MCMakeHttpGet();
    MCMcoApi::MCService service(*transfer, std::move(client));
    MCProbe probe(service, seconds);

    if (playing)
    {
        return probe.Play(name, mode == HostWord, options);
    }

    if (mode == GuestWord)
    {
        return probe.Guest(name);
    }

    if (mode == SignInWord)
    {
        return probe.SignIn();
    }

    return probe.Create(name);
}
