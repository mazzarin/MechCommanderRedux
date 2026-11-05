#pragma once

#include "MCTest.h"
#include "linkup/MCLinkupMessages.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "platform/MCDirectPlay.h"

// The tests' side of a multiplayer session the game hosts: a second player joining over the loopback with DirectPlay's
// stand-in (platform/MCDirectPlay.h), on a thread of its own, answering as a game's build would as far as a test asks.

namespace MCTestGuest
{
    /// <summary>Selects the TCP/IP connection to <paramref name="address"/> ("host:port").</summary>
    inline uint32_t Connect(MCDirectPlay& directPlay, std::string_view address)
    {
        const auto provider = std::bit_cast<std::array<uint8_t, sizeof(_GUID)>>(MCDirectPlayGuids::DPSPGUID_TCPIP);
        std::vector<uint8_t> host(address.begin(), address.end());
        host.push_back(0);
        const std::array<DPCOMPOUNDADDRESSELEMENT, 2> elements{
            DPCOMPOUNDADDRESSELEMENT{MCDirectPlayGuids::DPAID_ServiceProvider,
                                     std::vector<uint8_t>(provider.begin(), provider.end())},
            DPCOMPOUNDADDRESSELEMENT{MCDirectPlayGuids::DPAID_INet, host}};
        return directPlay.InitializeConnection(MCDirectPlay::CreateCompoundAddress(elements), 0);
    }

    /// <summary>Runs <paramref name="call"/> while another thread pumps <paramref name="other"/> (the game is not running).</summary>
    template <class Call> auto WhilePumping(MCDirectPlay& other, Call&& call)
    {
        std::atomic<bool> stop = false;
        std::thread pumper(
            [&]
            {
                while (!stop)
                {
                    other.Pump();
                    SDL_Delay(1);
                }
            });
        auto result = call();
        stop = true;
        pumper.join();
        return result;
    }

    /// <summary>
    /// A second player in the game's session, on the test's side: it joins over the loopback and then only takes in
    /// (and drops) what it is sent, on its own thread, so the host keeps it.
    /// </summary>
    class Guest
    {
    public:
        /// <summary>A guest who joins as <paramref name="name"/>.</summary>
        explicit Guest(std::string name = "Guest") : _Name(std::move(name)) {}

        bool Join(MCDirectPlay& host, std::string_view address)
        {
            if (Connect(_DirectPlay, address) != DP_OK)
            {
                return false;
            }

            DPSESSIONDESC2 found{};
            bool foundOne = false;

            for (int attempt = 0; attempt < 40 && !foundOne; attempt++)
            {
                DPSESSIONDESC2 desc{};
                desc.dwSize = sizeof(desc);
                desc.guidApplication = MultiPlayerAppGuid;
                _DirectPlay.EnumSessions(
                    desc, 0,
                    [&](const DPSESSIONDESC2& session, uint32_t flags)
                    {
                        if ((flags & DPESC_TIMEDOUT) == 0)
                        {
                            found = session;
                            foundOne = true;
                        }

                        return false;
                    },
                    DPENUMSESSIONS_AVAILABLE);
                host.Pump();
                SDL_Delay(5);
            }

            if (!foundOne)
            {
                return false;
            }

            if (WhilePumping(host, [&] { return _DirectPlay.Open(found, DPOPEN_JOIN | DPOPEN_RETURNSTATUS); }) != DP_OK)
            {
                return false;
            }

            const DPNAME name{_Name, std::nullopt};
            uint32_t id = 0;

            if (WhilePumping(host, [&] { return _DirectPlay.CreatePlayer(id, name, {}, 0); }) != DP_OK)
            {
                return false;
            }

            _Id = id;
            _Listener = std::thread(
                [this]
                {
                    while (!_Stop)
                    {
                        std::array<uint8_t, 4096> buffer{};
                        uint32_t size = 0;
                        uint32_t from = 0;
                        uint32_t to = 0;

                        while (_DirectPlay.Receive(from, to, DPRECEIVE_ALL, buffer, size) == DP_OK)
                        {
                            const auto message =
                                std::span<const uint8_t>(buffer).first(std::min<size_t>(size, buffer.size()));
                            Answer(from, message);
                            Keep(message);
                        }

                        SendWaiting();
                        _DirectPlay.Pump();
                        SDL_Delay(1);
                    }
                });
            return true;
        }

        /// <summary>The last match options message the host sent, if any.</summary>
        std::optional<MCMPMatchOptionsMessage> MatchOptions()
        {
            const std::lock_guard lock(_Mutex);
            return _MatchOptions;
        }

        /// <summary>The last players' rules message the host sent, if any.</summary>
        std::optional<MCMPPlayersRulesMessage> PlayersRules()
        {
            const std::lock_guard lock(_Mutex);
            return _PlayersRules;
        }

        /// <summary>
        /// Says to the host (player <paramref name="hostId"/>) that this guest's build plays by
        /// <paramref name="rules"/>, as a game's rules message does (sent from the listener, which owns the connection).
        /// </summary>
        void SendRules(uint32_t hostId, uint32_t rules)
        {
            MCMPLongMessage message{};
            message.Header = PlainHeader(MCMPMessageType::PlayerRules);
            message.Value = static_cast<int32_t>(rules);
            Send(hostId, message);
        }

        /// <summary>Says to the host (player <paramref name="hostId"/>) that this guest's game restarts into an update.</summary>
        void SendUpdating(uint32_t hostId)
        {
            MCMPLongMessage message{};
            message.Header = PlainHeader(MCMPMessageType::Updating);
            Send(hostId, message);
        }

        /// <summary>The host's messages saying its game restarts into an update, so far.</summary>
        int32_t Updatings() const { return _Updatings; }

        /// <summary>The host's messages taking every player to the session screen (its GO, or its room shown again).</summary>
        int32_t SessionScreens() const { return _SessionScreens; }

        /// <summary>Leaves the session: the listener stops once what waits to be sent is out, and the link closes.</summary>
        void Leave()
        {
            constexpr int32_t MostWaits = 200;

            for (int32_t wait = 0; wait < MostWaits && Sending(); wait++)
            {
                SDL_Delay(1);
            }

            _Stop = true;

            if (_Listener.joinable())
            {
                _Listener.join();
            }

            // What the link holds goes out before it closes.
            constexpr int32_t Flushes = 20;

            for (int32_t flush = 0; flush < Flushes; flush++)
            {
                _DirectPlay.Pump();
                SDL_Delay(1);
            }

            _DirectPlay.Close();
        }

        /// <summary>The guest's player id.</summary>
        uint32_t Id() const { return _Id; }

        /// <summary>
        /// Answers the host's file inquiries from now on with <paramref name="value"/>, as a game's file report does
        /// (0: no file).
        /// </summary>
        void AnswerFiles(uint32_t value) { _FileAnswer = value; }

        /// <summary>The host's messages telling this guest to load a mission, so far.</summary>
        int32_t MissionsLoaded() const { return _MissionsLoaded; }

        ~Guest()
        {
            _Stop = true;

            if (_Listener.joinable())
            {
                _Listener.join();
            }
        }

    private:
        /// <summary>
        /// Answers <paramref name="message"/> from player <paramref name="from"/> if it is a file inquiry (the file
        /// report: <see cref="AnswerFiles"/>'s value and the name asked about), and counts a load mission. The host sends
        /// a guaranteed message again until it is acknowledged, which this guest never does: each is taken once.
        /// </summary>
        void Answer(uint32_t from, std::span<const uint8_t> message)
        {
            constexpr size_t NameOffset = sizeof(MCFIGuaranteedMessageHeader);

            if (message.size() <= NameOffset)
            {
                return;
            }

            const uint16_t type = ReadLinkupMessage<MCFIGuaranteedMessageHeader>(message).Type();

            if (type == static_cast<uint16_t>(MCMPMessageType::Updating))
            {
                _Updatings++;
                return;
            }

            if (type == static_cast<uint16_t>(MCMPMessageType::SwitchScreen) &&
                _Taken.emplace(message.begin(), message.end()).second)
            {
                _SessionScreens++;
                return;
            }

            if ((type != static_cast<uint16_t>(MCMPMessageType::LoadMission) &&
                 type != static_cast<uint16_t>(MCMPMessageType::FileInquiry)) ||
                !_Taken.emplace(message.begin(), message.end()).second)
            {
                return;
            }

            if (type == static_cast<uint16_t>(MCMPMessageType::LoadMission))
            {
                _MissionsLoaded++;
                return;
            }

            const std::span<const uint8_t> rest = message.subspan(NameOffset);
            const std::string name(rest.begin(), std::ranges::find(rest, uint8_t{0}));
            MCMPFileNameMessage report{};
            report.Header = PlainHeader(MCMPMessageType::FileReport);
            report.Value = static_cast<int32_t>(_FileAnswer.load());
            std::vector<uint8_t> bytes(sizeof(report));
            std::memcpy(bytes.data(), &report, sizeof(report));
            bytes.insert(bytes.end(), name.begin(), name.end());
            bytes.push_back(0);
            _DirectPlay.Send(_Id, from, 0, bytes);
        }

        /// <summary>Keeps <paramref name="message"/> if it is the host's match options or players' rules.</summary>
        void Keep(std::span<const uint8_t> message)
        {
            if (message.size() >= sizeof(MCMPPlayersRulesMessage))
            {
                const auto players = ReadLinkupMessage<MCMPPlayersRulesMessage>(message);

                if (players.Type() == static_cast<uint16_t>(MCMPMessageType::PlayersRules))
                {
                    const std::lock_guard lock(_Mutex);
                    _PlayersRules = players;
                    return;
                }
            }

            if (message.size() < sizeof(MCMPMatchOptionsMessage))
            {
                return;
            }

            const auto options = ReadLinkupMessage<MCMPMatchOptionsMessage>(message);

            if (options.Type() == static_cast<uint16_t>(MCMPMessageType::MatchOptions))
            {
                const std::lock_guard lock(_Mutex);
                _MatchOptions = options;
            }
        }

        /// <summary>Leaves <paramref name="message"/> for the listener, which owns the connection, to send to <paramref name="to"/>.</summary>
        template <typename Message> void Send(uint32_t to, Message message)
        {
            const std::span<uint8_t> bytes = LinkupBytes(message);
            const std::lock_guard lock(_Mutex);
            _ToSend.emplace_back(to, std::vector<uint8_t>(bytes.begin(), bytes.end()));
        }

        /// <summary>Whether messages wait to be sent.</summary>
        bool Sending()
        {
            const std::lock_guard lock(_Mutex);
            return !_ToSend.empty();
        }

        /// <summary>Sends the messages <see cref="Send"/> left.</summary>
        void SendWaiting()
        {
            std::vector<std::pair<uint32_t, std::vector<uint8_t>>> messages;

            {
                const std::lock_guard lock(_Mutex);
                messages.swap(_ToSend);
            }

            for (const auto& [to, bytes] : messages)
            {
                _DirectPlay.Send(_Id, to, 0, bytes);
            }
        }

        std::string _Name;
        MCDirectPlay _DirectPlay;
        uint32_t _Id = 0;
        std::thread _Listener;
        std::atomic<bool> _Stop = false;
        std::mutex _Mutex;
        std::optional<MCMPMatchOptionsMessage> _MatchOptions;
        std::optional<MCMPPlayersRulesMessage> _PlayersRules;
        /// <summary>The messages to send and to whom, until the listener sends them.</summary>
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> _ToSend;
        /// <summary>The host's messages saying it restarts into an update.</summary>
        std::atomic<int32_t> _Updatings = 0;
        /// <summary>The host's messages taking every player to the session screen.</summary>
        std::atomic<int32_t> _SessionScreens = 0;
        /// <summary>What the guest's file reports say (<see cref="AnswerFiles"/>).</summary>
        std::atomic<uint32_t> _FileAnswer = 0;
        /// <summary>The load missions the host sent.</summary>
        std::atomic<int32_t> _MissionsLoaded = 0;
        /// <summary>The inquiries and load missions taken (the listener's own).</summary>
        std::set<std::vector<uint8_t>> _Taken;
    };

    /// <summary>
    /// A room's host on the test's side, for the game to join as a lobby's guest: it hosts a session on the loopback
    /// and takes in what it is sent on a thread of its own, counting the guests' Updating messages.
    /// </summary>
    class Host
    {
    public:
        /// <summary>
        /// Hosts session <paramref name="session"/> on <paramref name="port"/> as player <paramref name="name"/>, with
        /// <paramref name="flags"/> (DPSESSION_*: a started room's).
        /// </summary>
        bool Open(std::string_view port, std::string name, std::string session, uint32_t flags = 0)
        {
            constexpr uint32_t MaxPlayers = 6;

            if (Connect(_DirectPlay, std::format(":{}", port)) != DP_OK)
            {
                return false;
            }

            DPSESSIONDESC2 desc{};
            desc.dwSize = sizeof(desc);
            desc.dwFlags = flags;
            desc.guidApplication = MultiPlayerAppGuid;
            desc.dwMaxPlayers = MaxPlayers;
            desc.SessionName = std::move(session);

            if (_DirectPlay.Open(desc, DPOPEN_CREATE | DPOPEN_RETURNSTATUS) != DP_OK ||
                _DirectPlay.CreatePlayer(_Id, DPNAME{std::move(name), std::nullopt}, {}, 0) != DP_OK)
            {
                return false;
            }

            _Stop = false;
            _Listener = std::thread(
                [this]
                {
                    while (!_Stop)
                    {
                        std::array<uint8_t, 4096> buffer{};
                        uint32_t size = 0;
                        uint32_t from = 0;
                        uint32_t to = 0;

                        while (_DirectPlay.Receive(from, to, DPRECEIVE_ALL, buffer, size) == DP_OK)
                        {
                            Take(from, std::span<const uint8_t>(buffer).first(std::min<size_t>(size, buffer.size())));
                        }

                        SendWaiting();
                        _DirectPlay.Pump();
                        SDL_Delay(1);
                    }
                });
            return true;
        }

        /// <summary>The host's player id.</summary>
        uint32_t Id() const { return _Id; }

        /// <summary>The players who joined, by id, as DirectPlay announced them.</summary>
        std::vector<uint32_t> Joined()
        {
            const std::lock_guard lock(_Mutex);
            return _Joined;
        }

        /// <summary>The guests' messages saying their games restart into an update, so far.</summary>
        int32_t Updatings() const { return _Updatings; }

        /// <summary>Says to player <paramref name="to"/> that the host's game restarts into an update.</summary>
        void SendUpdating(uint32_t to)
        {
            MCMPLongMessage message{};
            message.Header = PlainHeader(MCMPMessageType::Updating);
            const std::span<uint8_t> bytes = LinkupBytes(message);
            const std::lock_guard lock(_Mutex);
            _ToSend.emplace_back(to, std::vector<uint8_t>(bytes.begin(), bytes.end()));
        }

        /// <summary>Ends the session once what waits to be sent is out, as the host's game quitting does.</summary>
        void Close()
        {
            constexpr int32_t MostWaits = 200;
            constexpr int32_t Flushes = 20;

            for (int32_t wait = 0; wait < MostWaits && Sending(); wait++)
            {
                SDL_Delay(1);
            }

            _Stop = true;

            if (_Listener.joinable())
            {
                _Listener.join();
            }

            for (int32_t flush = 0; flush < Flushes; flush++)
            {
                _DirectPlay.Pump();
                SDL_Delay(1);
            }

            _DirectPlay.Close();
        }

        ~Host()
        {
            _Stop = true;

            if (_Listener.joinable())
            {
                _Listener.join();
            }
        }

    private:
        /// <summary>Counts <paramref name="message"/> from <paramref name="from"/> if it is a guest's Updating or a player joining.</summary>
        void Take(uint32_t from, std::span<const uint8_t> message)
        {
            if (from == DPID_SYSMSG && message.size() >= sizeof(DPMSG_CREATEPLAYERORGROUP))
            {
                const auto created = ReadLinkupMessage<DPMSG_GENERIC>(message);

                if (created.dwType == DPSYS_CREATEPLAYERORGROUP)
                {
                    const std::lock_guard lock(_Mutex);
                    _Joined.push_back(ReadLinkupMessage<DPMSG_CREATEPLAYERORGROUP>(message).dpId);
                }

                return;
            }

            if (message.size() >= sizeof(MCFIGuaranteedMessageHeader) &&
                ReadLinkupMessage<MCFIGuaranteedMessageHeader>(message).Type() ==
                    static_cast<uint16_t>(MCMPMessageType::Updating))
            {
                _Updatings++;
            }
        }

        /// <summary>Whether messages wait to be sent.</summary>
        bool Sending()
        {
            const std::lock_guard lock(_Mutex);
            return !_ToSend.empty();
        }

        /// <summary>Sends the messages left for the listener.</summary>
        void SendWaiting()
        {
            std::vector<std::pair<uint32_t, std::vector<uint8_t>>> messages;

            {
                const std::lock_guard lock(_Mutex);
                messages.swap(_ToSend);
            }

            for (const auto& [to, bytes] : messages)
            {
                _DirectPlay.Send(_Id, to, 0, bytes);
            }
        }

        MCDirectPlay _DirectPlay;
        uint32_t _Id = 0;
        std::thread _Listener;
        std::atomic<bool> _Stop = false;
        std::mutex _Mutex;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> _ToSend;
        std::vector<uint32_t> _Joined;
        std::atomic<int32_t> _Updatings = 0;
    };
}
