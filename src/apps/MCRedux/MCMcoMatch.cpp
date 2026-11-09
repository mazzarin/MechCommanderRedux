#include "stdafx.h"
#include "MCMcoMatch.h"

#include "MCConsole.h"
#include "MCGameCopy.h"
#include "MCSystemTrust.h"
#include "network/MCMcoBridge.h"
#include "network/MCMcoContent.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSocket.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <deque>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>The room polled about once a second, as MCO's launcher polls it (850-1150 ms).</summary>
    constexpr auto PollEvery = std::chrono::seconds(1);
    /// <summary>How long a relay connection and its handshake may take.</summary>
    constexpr int RelayConnectMs = 10000;
    /// <summary>How long the host's game has to answer its session query before the ticket is asked (the launcher's).</summary>
    constexpr int HostGameMs = 45000;
    /// <summary>The match's session as the game names it: "MCO " and the session's first 12 hex digits (the launcher's).</summary>
    constexpr std::string_view SessionPrefix = "MCO ";
    constexpr size_t SessionDigits = 12;
    /// <summary>The game's log, in the save folder, for each match's copy.</summary>
    constexpr std::string_view GameLog = "mco-game.log";
    /// <summary>The words typed in a room.</summary>
    constexpr std::string_view StartWord = "s";
    constexpr std::string_view ReadyWord = "r";
    constexpr std::string_view QuitWord = "q";
    /// <summary>
    /// The words typed in a guest's match: a second ticket for it asked (shown only as given or not), and the match
    /// joined again on a new ticket (the game and its relay link closed first).
    /// </summary>
    constexpr std::string_view TicketWord = "t";
    constexpr std::string_view RejoinWord = "j";
    constexpr std::string_view LoopbackHost = "127.0.0.1";

    /// <summary>How often a guest tries a match the relay or its host wasn't ready for, and how long between.</summary>
    constexpr int MatchTries = 5;
    constexpr auto MatchRetryEvery = std::chrono::seconds(3);
    /// <summary>The room's polls that may fail in a row (MCO unreachable, busy) before the room is given up.</summary>
    constexpr int PollFailuresMost = 10;
    /// <summary>The player's name as the launcher hands it to the game: no controls, at most this long, or this.</summary>
    constexpr size_t GameNameMost = 40;
    constexpr std::string_view GameNameNone = "MCO Player";
    /// <summary>The match's players as the host's game takes them (the launcher's: its capacity, held to 2-6).</summary>
    constexpr int32_t GamePlayersFewest = MCMcoApi::MatchFewest;
    /// <summary>What a relay or MCO says of a match whose session has gone (the launcher reports it, report_session_missing).</summary>
    constexpr std::string_view SessionGoneText = "no longer available";

    /// <summary>Set by Ctrl+C: the room is left as "q" leaves it.</summary>
    std::atomic<bool> Interrupted = false;

    void OnInterrupt(int /*signal*/)
    {
        Interrupted = true;
    }

    /// <summary>Lines of the terminal go to one place, whichever thread writes them.</summary>
    std::mutex SayLock;

    void Say(std::string_view line)
    {
        const std::scoped_lock lock(SayLock);
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

    /// <summary>
    /// The words typed while in a room, read on a thread of their own so the room is polled meanwhile. One reader for
    /// the program's run: it blocks on the terminal, so it's left to end with the program, and what it reads is kept
    /// where it outlives whoever asked.
    /// </summary>
    class MCTyped
    {
    public:
        static MCTyped& Reader()
        {
            static MCTyped reader;
            return reader;
        }

        std::optional<std::string> Take()
        {
            const std::scoped_lock lock(_Lines->Lock);

            if (_Lines->Lines.empty())
            {
                return std::nullopt;
            }

            std::string line = std::move(_Lines->Lines.front());
            _Lines->Lines.pop_front();
            return line;
        }

    private:
        struct MCLines
        {
            std::mutex Lock;
            std::deque<std::string> Lines;
        };

        MCTyped() : _Lines(std::make_shared<MCLines>())
        {
            std::thread(
                [lines = _Lines]()
                {
                    std::string line;

                    while (std::getline(std::cin, line))
                    {
                        const std::scoped_lock lock(lines->Lock);
                        lines->Lines.push_back(line);
                    }
                })
                .detach();
        }

        std::shared_ptr<MCLines> _Lines;
    };

    /// <summary>Why MCO took the player out of a room, in a sentence.</summary>
    std::string GoneText(std::string_view why)
    {
        if (why == "host_connection_lost")
        {
            return "The room closed: its host lost their connection.";
        }

        if (why == "kicked")
        {
            return "The host took you out of the room.";
        }

        if (why == "closed")
        {
            return "The host closed the room.";
        }

        if (why == "moderator_closed")
        {
            return "A moderator closed the room.";
        }

        return why.empty() ? "You're no longer in the room."
                           : std::format("You're no longer in the room ({}).", Shown(why));
    }

    /// <summary>
    /// <paramref name="username"/> as the launcher hands it to the game (`-player`): its control characters (C0, DEL,
    /// C1) dropped, its first <see cref="GameNameMost"/> characters; <see cref="GameNameNone"/> when none are left.
    /// </summary>
    std::string GameName(std::string_view username)
    {
        constexpr unsigned char Delete = 0x7f;
        constexpr unsigned char C1Lead = 0xc2;
        constexpr unsigned char C1Last = 0x9f;
        constexpr unsigned char ContinuationMask = 0xc0;
        constexpr unsigned char Continuation = 0x80;
        std::string name;
        size_t characters = 0;

        for (size_t i = 0; i < username.size() && characters <= GameNameMost; i++)
        {
            const auto byte = static_cast<unsigned char>(username[i]);

            // A C1 control is U+0080-U+009F: 0xC2 then 0x80-0x9F in UTF-8.
            if (byte < ' ' || byte == Delete ||
                (byte == C1Lead && i + 1 < username.size() && static_cast<unsigned char>(username[i + 1]) <= C1Last))
            {
                i += byte == C1Lead ? 1 : 0;
                continue;
            }

            const bool starts = (byte & ContinuationMask) != Continuation;
            characters += starts ? 1 : 0;

            if (starts && characters > GameNameMost)
            {
                break;
            }

            name += static_cast<char>(byte);
        }

        return name.empty() ? std::string(GameNameNone) : name;
    }

    /// <summary>Whether <paramref name="text"/> says a match's session has gone (any case).</summary>
    bool SaysSessionGone(std::string_view text)
    {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        return lower.contains(SessionGoneText);
    }

    /// <summary>A port free for both the host game's TCP and UDP on every interface, or 0.</summary>
    uint16_t FreeGamePort()
    {
        MCSocketTransport sockets;
        const MCSocket::Handle tcp = sockets.ListenTcpOn(0, 0);
        const uint16_t port = tcp == MCSocket::InvalidHandle ? 0 : sockets.LocalPort(tcp);
        const MCSocket::Handle udp = port == 0 ? MCSocket::InvalidHandle : sockets.OpenUdpOn(0, port);
        sockets.Close(tcp);
        sockets.Close(udp);
        return udp == MCSocket::InvalidHandle ? 0 : port;
    }

    /// <summary>A room's matches, played as the options say.</summary>
    class MCRoomRun
    {
    public:
        MCRoomRun(MCMcoApi::MCService& service, MCMcoApi::MCPeer& peer, const MCMcoMatch::MCOptions& options)
            : _Service(service), _Peer(peer), _Options(options), _Trust(MCMakeSystemTrust())
        {
            Interrupted = false;
            std::signal(SIGINT, OnInterrupt);
        }

        MCRoomRun(const MCRoomRun&) = delete;
        MCRoomRun& operator=(const MCRoomRun&) = delete;

        ~MCRoomRun()
        {
            Leave();
            std::signal(SIGINT, SIG_DFL);
        }

        int Join()
        {
            if (!Prepare())
            {
                return 1;
            }

            const std::optional<MCMcoApi::MCRoom> room = ChooseRoom();

            if (!room)
            {
                return 1;
            }

            // A room naming no data is joined, as the launcher joins it; its match is held to the data anyway.
            if (!room->EngineContent.empty() && !MCMcoApi::SameId(room->EngineContent, _Content))
            {
                Say(std::format("This room's game data isn't this computer's: its {} against ours {}. MCO's launcher "
                                "would refuse it too.",
                                Shown(room->EngineContent), _Content));
                return 1;
            }

            std::string password = _Options.Password;

            if (room->Locked && password.empty())
            {
                const std::optional<std::string> typed = MCConsole::ReadLine("The room's password: ", true);

                if (!typed)
                {
                    return 1;
                }

                password = *typed;
            }

            std::cout << std::format("Joining \"{}\"... ", Shown(room->Name)) << std::flush;
            auto seat = _Service.JoinRoom(_Peer, *room, password, _Content, true);
            std::ranges::fill(password, '\0');

            if (!seat)
            {
                Say(Failure(seat.error()));
                return 1;
            }

            _Seat = std::move(*seat);
            Say("joined, READY. Type r and Enter to change READY, q to leave.");
            return InRoom();
        }

        int Host()
        {
            if (!Prepare())
            {
                return 1;
            }

            if (_Stock.empty())
            {
                Say("MCO names no build for its Redux rooms, so none can be hosted.");
                return 1;
            }

            std::string name = _Options.Room;

            if (name.empty())
            {
                const std::optional<std::string> typed = MCConsole::ReadLine("Room name: ", false);

                if (!typed || typed->empty())
                {
                    return 1;
                }

                name = *typed;
            }

            std::cout << std::format("Hosting \"{}\" for {} players... ", Shown(name), _Options.MaxPlayers)
                      << std::flush;
            auto seat = _Service.HostRoom(_Peer, name, _Options.MaxPlayers, _Options.Password, _Stock, _Content);

            if (!seat)
            {
                Say(Failure(seat.error()));
                return 1;
            }

            _Seat = std::move(*seat);
            Say("hosting. Type s and Enter to START once everyone's READY, q to close the room.");
            return InRoom();
        }

    private:
        /// <summary>The game data's fingerprint and the build MCO's Redux rooms play.</summary>
        bool Prepare()
        {
            std::cout << "Fingerprinting the game data as MCO does... " << std::flush;
            const auto content = MCMcoContent::Fingerprint(_Options.DataFolder, _Options.UserFolder);

            if (!content)
            {
                Say(std::format("failed: {}", content.error()));
                return false;
            }

            _Content = *content;
            Say(_Content);
            std::cout << "The build MCO's Redux rooms play: " << std::flush;
            const auto build = _Service.StockBuild(_Peer);

            if (!build)
            {
                Say(Failure(build.error()));
                return false;
            }

            _Stock = build->value_or("");
            Say(_Stock.empty() ? std::string("none named") : Shown(_Stock));
            return true;
        }

        /// <summary>The room to join: the one named, else one picked from the Redux rooms waiting.</summary>
        std::optional<MCMcoApi::MCRoom> ChooseRoom()
        {
            const auto lobby = _Service.Lobby(_Peer);

            if (!lobby)
            {
                Say(std::format("The lobby: {}", Failure(lobby.error())));
                return std::nullopt;
            }

            std::vector<MCMcoApi::MCRoom> rooms;

            for (const MCMcoApi::MCRoom& room : lobby->Rooms)
            {
                if (room.Engine == MCMcoApi::ReduxEngine && (_Stock.empty() || room.EngineBuild == _Stock))
                {
                    rooms.push_back(room);
                }
            }

            if (!_Options.Room.empty())
            {
                const auto named = std::ranges::find_if(rooms, [&](const MCMcoApi::MCRoom& room)
                                                        { return room.Name == _Options.Room; });

                if (named == rooms.end())
                {
                    Say(std::format("No Redux room named \"{}\" is open.", Shown(_Options.Room)));
                    return std::nullopt;
                }

                return *named;
            }

            if (rooms.empty())
            {
                Say("No Redux room is open.");
                return std::nullopt;
            }

            for (size_t i = 0; i < rooms.size(); i++)
            {
                const MCMcoApi::MCRoom& room = rooms[i];
                Say(std::format("  {}. \"{}\", host {}, {}/{}, {}{}", i + 1, Shown(room.Name), Shown(room.Host),
                                room.Players, room.MaxPlayers, room.InGame ? "in battle" : "waiting",
                                room.Locked ? ", locked" : ""));
            }

            const std::optional<std::string> typed = MCConsole::ReadLine("Room number: ", false);
            size_t picked = 0;

            if (!typed || std::from_chars(typed->data(), typed->data() + typed->size(), picked).ec != std::errc() ||
                picked < 1 || picked > rooms.size())
            {
                Say("No room picked.");
                return std::nullopt;
            }

            return rooms[picked - 1];
        }

        /// <summary>The room, polled until the player leaves it or MCO takes them out; each match played.</summary>
        int InRoom()
        {
            MCTyped& typed = MCTyped::Reader();
            std::string shown;
            int tries = 0;
            int failures = 0;
            std::string trying;
            Clock::time_point tried;

            while (!Interrupted)
            {
                const auto state = _Service.RoomPoll(_Peer, *_Seat);

                // MCO unreachable or busy for a moment isn't the room gone: it's asked again (when MCO says).
                if (!state)
                {
                    Say(std::format("The room: {}", Failure(state.error())));

                    if (++failures >= PollFailuresMost)
                    {
                        return 1;
                    }

                    std::this_thread::sleep_for(
                        std::max<std::chrono::seconds>(PollEvery, std::chrono::seconds(state.error().RetryAfter)));
                    continue;
                }

                failures = 0;

                if (state->Gone)
                {
                    Say(GoneText(state->GoneWhy));
                    _Seat.reset();
                    return 0;
                }

                if (const std::string players = PlayersText(*state); players != shown)
                {
                    Say(players);
                    shown = players;
                }

                // A guest plays the host's match once (its start signal stays while the match runs), trying again a few
                // times when the relay or the host wasn't ready for it.
                if (state->Start && state->Start->SessionHex != trying)
                {
                    trying = state->Start->SessionHex;
                    tries = 0;
                }

                if (!_Seat->Host && state->Start && state->Start->SessionHex != _Played &&
                    (tries == 0 || Clock::now() - tried >= MatchRetryEvery))
                {
                    tried = Clock::now();
                    tries++;
                    const bool settled = PlayAsGuest(*state->Start, typed);

                    if (std::exchange(_Rejoin, false))
                    {
                        // j: the same match again at once, on a new ticket.
                        _Again = true;
                        tries = 0;
                        Say("Joining the match again, on a new ticket.");
                    }
                    else if (settled || tries >= MatchTries)
                    {
                        _Played = state->Start->SessionHex;
                        _Again = false;
                        tries = 0;
                    }

                    shown.clear();
                }

                while (const std::optional<std::string> line = typed.Take())
                {
                    if (*line == QuitWord)
                    {
                        return 0;
                    }

                    if (*line == ReadyWord && !_Seat->Host)
                    {
                        const auto set = _Service.SetReady(_Peer, *_Seat, !_Seat->Ready);
                        Say(set ? std::format("{}.", _Seat->Ready ? "READY" : "NOT READY") : Failure(set.error()));
                    }
                    else if (*line == StartWord && _Seat->Host)
                    {
                        if (Startable(*state))
                        {
                            PlayAsHost(typed);
                            shown.clear();
                        }
                    }
                }

                std::this_thread::sleep_for(PollEvery);
            }

            return 0;
        }

        static std::string PlayersText(const MCMcoApi::MCRoomState& state)
        {
            std::string text = std::format("In the room ({}):", state.Players.size());

            for (const MCMcoApi::MCRoomPlayer& player : state.Players)
            {
                text += std::format(" {}{}{}", Shown(player.Username), player.Host ? " (host)" : "",
                                    player.Ready ? " READY" : "");
            }

            return text;
        }

        /// <summary>Whether the host's START may begin the match: two players at least, everyone READY (MCO's rule).</summary>
        static bool Startable(const MCMcoApi::MCRoomState& state)
        {
            if (static_cast<int32_t>(state.Players.size()) < MCMcoApi::MatchFewest)
            {
                Say("At least two players are needed to start.");
                return false;
            }

            const auto waiting = std::ranges::find_if(state.Players, [](const MCMcoApi::MCRoomPlayer& player)
                                                      { return !player.Ready && !player.Host; });

            if (waiting != state.Players.end())
            {
                Say(std::format("{} isn't READY yet.", Shown(waiting->Username)));
                return false;
            }

            return true;
        }

        MCMcoBridge::MCDial Dial(const MCMcoApi::MCMatch& match) const
        {
            MCTlsLink::MCSettings settings{.Host = match.RelayHost,
                                           .Port = match.RelayPort,
                                           .TimeoutMs = RelayConnectMs,
                                           .AnchorPem = _Options.RelayAnchor};

            if (_Options.RelayAnchor.empty())
            {
                settings.Trust = std::cref(*_Trust);
            }

            return MCMcoBridge::TlsDial(std::move(settings));
        }

        /// <summary>The game's words: the match's (host or join), then the player's name and the folders given.</summary>
        std::vector<std::string> GameWords(std::vector<std::string> match) const
        {
            match.insert(match.end(), {"-player", GameName(_Peer.Username)});

            if (_Options.DataGiven)
            {
                match.insert(match.end(), {"--data", MCFileSystem::ToUtf8(_Options.DataFolder)});
            }

            if (_Options.UserGiven)
            {
                match.insert(match.end(), {"--user", MCFileSystem::ToUtf8(_Options.UserFolder)});
            }

            return match;
        }

        /// <summary>Starts the game with <paramref name="words"/>, or says how to (its log beside the save folder's).</summary>
        std::unique_ptr<MCGameCopy> StartGame(const std::vector<std::string>& words)
        {
            if (!_Options.Launch)
            {
                std::string command = "Start the game with:";

                for (const std::string& word : words)
                {
                    command +=
                        word.find(' ') == std::string::npos ? std::format(" {}", word) : std::format(" \"{}\"", word);
                }

                Say(command);
                return nullptr;
            }

            const std::filesystem::path log = _Options.UserFolder / GameLog;
            auto game = MCGameCopy::Start(words, log);

            if (!game)
            {
                Say(std::format("The game: {}", game.error()));
                return nullptr;
            }

            Say(std::format("The game started (its log: {}).", MCFileSystem::ToUtf8(log)));
            return std::move(*game);
        }

        /// <summary>
        /// The match as it runs: the room polled (saying the game is in it), until the game quits or the bridge ends.
        /// </summary>
        template <typename Bridge>
        void WhilePlaying(const MCMcoApi::MCMatch& match, Bridge& bridge, MCOptionalRef<MCGameCopy> game,
                          MCTyped& typed)
        {
            while (!Interrupted && bridge.Running())
            {
                const bool gameOn = !game || game->get().Running();

                if (!gameOn)
                {
                    Say("The game quit.");
                    break;
                }

                const auto state = _Service.RoomPoll(_Peer, *_Seat, match.SessionHex, gameOn && bridge.Running());

                if (state && state->Gone)
                {
                    Say(GoneText(state->GoneWhy));
                    break;
                }

                if (!state)
                {
                    Say(std::format("The room: {}", Failure(state.error())));
                }

                if (const std::optional<std::string> line = typed.Take(); line && *line == QuitWord)
                {
                    Interrupted = true;
                }
                else if (line && *line == TicketWord && !_Seat->Host)
                {
                    SecondTicket(match);
                }
                else if (line && *line == RejoinWord && !_Seat->Host)
                {
                    Say("Leaving the match to join it again.");
                    _Rejoin = true;
                    break;
                }

                std::this_thread::sleep_for(PollEvery);
            }

            if (!bridge.Running() && !bridge.Ended().empty())
            {
                Say(std::format("The relay link ended: {}", bridge.Ended()));
            }

            bridge.Stop();
            const MCMcoBridge::MCCarried& carried = bridge.Carried();
            Say(std::format("Carried: {} datagrams out, {} in; {} bytes out, {} in.", carried.DatagramsOut.load(),
                            carried.DatagramsIn.load(), carried.BytesOut.load(), carried.BytesIn.load()));

            // The game goes with its match, as the launcher's goes with its helper.
            if (game && game->get().Running())
            {
                Say("Closing the game.");
                game->get().Stop();
            }
        }

        static MCOptionalRef<MCGameCopy> GameOf(const std::unique_ptr<MCGameCopy>& game)
        {
            return game ? MCOptionalRef<MCGameCopy>(*game) : std::nullopt;
        }

        /// <summary>
        /// A guest's match that failed saying its session has gone, reported as the launcher reports it (the host's
        /// START is then needed again); whether it was that.
        /// </summary>
        bool ReportIfGone(const MCMcoApi::MCMatch& match, std::string_view failure)
        {
            // A match joined again (j) that MCO or its relay won't take again isn't the host's session gone.
            if (_Again || !SaysSessionGone(failure))
            {
                return false;
            }

            const auto reported = _Service.ReportSessionMissing(_Peer, match.SessionHex);
            Say(reported ? std::string("The match's session had gone: MCO told. The host's START is needed again.")
                         : std::format("Telling MCO the match's session had gone {}", Failure(reported.error())));
            return true;
        }

        /// <returns>
        /// Whether the match is settled: played (the relay took the guest), or one this computer can't play; else it
        /// may be tried again.
        /// </returns>
        bool PlayAsGuest(const MCMcoApi::MCMatch& match, MCTyped& typed)
        {
            Say("The host started the match.");

            // As MCO's launcher holds a match to the player's data before starting its game.
            if (!MCMcoApi::SameId(match.EngineContent, _Content))
            {
                Say(std::format("The match's game data ({}) isn't this computer's ({}): not joining it.",
                                Shown(match.EngineContent), _Content));
                return true;
            }

            const MCMcoBridge::MCDial dial = Dial(match);
            std::cout << std::format("Asking the relay {}:{} for its epoch... ", match.RelayHost, match.RelayPort)
                      << std::flush;
            const auto epoch = MCMcoBridge::Probe(dial);

            if (!epoch)
            {
                Say(std::format("failed: {}", epoch.error()));
                return false;
            }

            Say("done.");
            std::cout << "Asking MCO for the match's ticket... " << std::flush;
            const auto ticket = _Service.Ticket(_Peer, match, *epoch, false);

            if (!ticket)
            {
                Say(Failure(ticket.error()));
                return ReportIfGone(match, ticket.error().Message);
            }

            Say("done.");
            std::cout << "Joining the host through the relay... " << std::flush;
            auto guest = MCMcoBridge::MCGuest::Start(dial, ticket->Ticket, [](std::string_view note)
                                                     { Say(std::format("  relay: {}", note)); });

            if (!guest)
            {
                Say(std::format("failed: {}", guest.error()));
                return ReportIfGone(match, guest.error());
            }

            Say(std::format("done: the game joins at {}:{}.", LoopbackHost, (*guest)->Port()));
            const std::unique_ptr<MCGameCopy> game =
                StartGame(GameWords({"-join", std::format("{}:{}", LoopbackHost, (*guest)->Port())}));

            if (_Options.Launch && game == nullptr)
            {
                (*guest)->Stop();
                return true;
            }

            Say("Type t and Enter for a second ticket for this match, j to join it again, q to leave.");
            _Ticket = ticket->Ticket;
            _Epoch = *epoch;
            WhilePlaying(match, **guest, GameOf(game), typed);
            std::ranges::fill(_Ticket, '\0');
            _Ticket.clear();
            return true;
        }

        /// <summary>
        /// t: asks MCO for another ticket for the match this guest plays, and says whether it gave one, and whether it's
        /// the one it gave first (neither is shown).
        /// </summary>
        void SecondTicket(const MCMcoApi::MCMatch& match)
        {
            std::cout << "Asking MCO for a second ticket for this match... " << std::flush;
            auto ticket = _Service.Ticket(_Peer, match, _Epoch, false);

            if (!ticket)
            {
                Say(Failure(ticket.error()));
                return;
            }

            Say(ticket->Ticket == _Ticket ? "given: the first one again." : "given: a new one.");
            std::ranges::fill(ticket->Ticket, '\0');
        }

        void PlayAsHost(MCTyped& typed)
        {
            std::cout << "Starting the match (create_session)... " << std::flush;
            const auto match = _Service.CreateSession(_Peer, _Stock, _Content);

            if (!match)
            {
                Say(Failure(match.error()));
                return;
            }

            Say(std::format("session {}.", match->SessionHex));
            const MCMcoBridge::MCDial dial = Dial(*match);
            std::cout << std::format("Asking the relay {}:{} for its epoch... ", match->RelayHost, match->RelayPort)
                      << std::flush;
            const auto epoch = MCMcoBridge::Probe(dial);
            const uint16_t port = epoch ? FreeGamePort() : 0;

            if (!epoch || port == 0)
            {
                Say(epoch ? std::string("failed: no free port for the game")
                          : std::format("failed: {}", epoch.error()));
                Close(match->SessionHex);
                return;
            }

            Say("done.");
            const std::string session = std::string(SessionPrefix) + match->SessionHex.substr(0, SessionDigits);
            const std::unique_ptr<MCGameCopy> game = StartGame(
                GameWords({"-host", std::to_string(port), "-session", session, "-maxplayers",
                           std::to_string(std::clamp(match->Capacity, GamePlayersFewest, MCMcoApi::RoomMost))}));

            if (_Options.Launch && game == nullptr)
            {
                Close(match->SessionHex);
                return;
            }

            std::cout << std::format("Waiting for the game to host on port {}... ", port) << std::flush;

            if (!MCMcoBridge::WaitForHostGame(port, HostGameMs, Interrupted))
            {
                Say("it didn't.");
                Close(match->SessionHex);
                return;
            }

            Say("hosting.");
            std::cout << "Asking MCO for the match's ticket... " << std::flush;
            const auto ticket = _Service.Ticket(_Peer, *match, *epoch, true);

            if (!ticket)
            {
                Say(Failure(ticket.error()));
                Close(match->SessionHex);
                return;
            }

            Say("done.");
            std::cout << "Opening the host's connection to the relay... " << std::flush;
            auto host = MCMcoBridge::MCHost::Start(dial, ticket->Ticket, port, [](std::string_view note)
                                                   { Say(std::format("  relay: {}", note)); });

            if (!host)
            {
                Say(std::format("failed: {}", host.error()));
                Close(match->SessionHex);
                return;
            }

            Say("open.");

            // A match MCO wasn't told of can't be joined: it's called off, as the launcher calls it off.
            if (const auto published = _Service.PublishReady(_Peer, match->SessionHex); !published)
            {
                Say(std::format("Telling MCO the match is ready {}", Failure(published.error())));
                (*host)->Stop();
                Close(match->SessionHex);
                return;
            }

            Say("MCO told the match is ready: the guests join now.");
            WhilePlaying(*match, **host, GameOf(game), typed);
            Close(match->SessionHex, *match);
        }

        /// <summary>Ends the match: one its game came back from (<paramref name="played"/>) as such, else called off.</summary>
        void Close(const std::string& session, MCOptionalRef<const MCMcoApi::MCMatch> played = std::nullopt)
        {
            const auto closed =
                played ? _Service.CloseSession(_Peer, session, played->get().EngineBuild, played->get().EngineContent)
                       : _Service.CloseSession(_Peer, session);
            Say(closed ? std::string("The match closed.")
                       : std::format("Closing the match {}", Failure(closed.error())));
        }

        /// <summary>Leaves the room for the lobby (the probe then leaves the lobby).</summary>
        void Leave()
        {
            if (!_Seat)
            {
                return;
            }

            std::cout << "Leaving the room... " << std::flush;
            const auto left = _Service.LeaveRoom(_Peer);
            Say(left ? std::string("left.") : Failure(left.error()));
            _Seat.reset();
        }

        MCMcoApi::MCService& _Service;
        MCMcoApi::MCPeer& _Peer;
        const MCMcoMatch::MCOptions& _Options;
        std::unique_ptr<MCTlsLink::MCTrust> _Trust;
        std::optional<MCMcoApi::MCSeat> _Seat;
        std::string _Content;
        std::string _Stock;
        /// <summary>The match a guest played last (not played again while its signal stays).</summary>
        std::string _Played;
        /// <summary>The ticket a guest's match was joined on, to tell a second one from (never shown).</summary>
        std::string _Ticket;
        /// <summary>The epoch of the relay a guest's match is on, for a second ticket.</summary>
        std::string _Epoch;
        /// <summary>A guest's j: its match is joined again rather than counted played.</summary>
        bool _Rejoin = false;
        /// <summary>The match being joined again: a refusal of it is said, never reported as its session gone.</summary>
        bool _Again = false;
    };
}

namespace MCMcoMatch
{
    int Join(MCMcoApi::MCService& service, MCMcoApi::MCPeer& peer, const MCOptions& options)
    {
        MCRoomRun run(service, peer, options);
        return run.Join();
    }

    int Host(MCMcoApi::MCService& service, MCMcoApi::MCPeer& peer, const MCOptions& options)
    {
        MCRoomRun run(service, peer, options);
        return run.Host();
    }
}
