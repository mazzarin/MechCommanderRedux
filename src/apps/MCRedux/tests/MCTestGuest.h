#pragma once

#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "linkup/MCLinkupMessages.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "platform/MCDirectPlay.h"

// The tests' side of a multiplayer session the game hosts: a second player joining over the loopback with DirectPlay's
// stand-in (platform/MCDirectPlay.h), on a thread of its own, answering as a game's build would as far as a test asks.

namespace MCTestGuest
{
    /// <summary>
    /// What release 1's game (0.5.0 (724), 16543520) says it plays by, in its rules message once in the room: its join
    /// says nothing (it came before the join card).
    /// </summary>
    inline constexpr uint32_t ReleaseOneRules = 0x18001ffu;

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
    /// A player on the test's side of a session: its DirectPlay, which a listener thread of its own owns once it is in
    /// the session (taking in what comes, and sending what the test leaves for it), and the Updating and RestartOff
    /// messages it was sent, counted. A class made from it stops the listener in its own destructor
    /// (<see cref="StopListening"/>), as the listener calls its <see cref="Take"/>.
    /// </summary>
    class Peer
    {
    public:
        Peer(const Peer&) = delete;
        Peer& operator=(const Peer&) = delete;

        /// <summary>The player's id.</summary>
        uint32_t Id() const { return _Id; }

        /// <summary>Says to player <paramref name="to"/> that this player's game restarts into an update.</summary>
        void SendUpdating(uint32_t to) { SendNotice(to, MCMPMessageType::Updating); }

        /// <summary>Says to player <paramref name="to"/> that this player's restart into an update is off.</summary>
        void SendRestartOff(uint32_t to) { SendNotice(to, MCMPMessageType::RestartOff); }

        /// <summary>The messages saying another game restarts into an update, so far.</summary>
        int32_t Updatings() const { return _Updatings; }

        /// <summary>The messages saying another game's restart into an update is off, so far.</summary>
        int32_t RestartOffs() const { return _RestartOffs; }

        /// <summary>
        /// Says to player <paramref name="to"/> that this player's room ends, <paramref name="how"/> (an
        /// MCRoomEnd::MCHow's value), as a host's game does (network/MCRoomEnd.h).
        /// </summary>
        void SendRoomEnds(uint32_t to, int32_t how)
        {
            MCMPLongMessage message{};
            message.Header = PlainHeader(MCMPMessageType::RoomEnds);
            message.Value = how;
            Send(to, message);
        }

        /// <summary>What each host's word that its room ends said (an MCRoomEnd::MCHow's value), so far.</summary>
        std::vector<int32_t> RoomEnds()
        {
            const std::lock_guard lock(_Mutex);
            return _RoomEnds;
        }

    protected:
        Peer() = default;
        virtual ~Peer() { StopListening(); }

        /// <summary>Takes in <paramref name="message"/> from player <paramref name="from"/> (on the listener).</summary>
        virtual void Take(uint32_t from, std::span<const uint8_t> message) = 0;

        /// <summary>Anything else the listener does each pass, after sending what waits.</summary>
        virtual void EachPass() {}

        /// <summary>Starts the listener, once in the session as player <paramref name="id"/>.</summary>
        void StartListening(uint32_t id)
        {
            _Id = id;
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
                        EachPass();
                        _DirectPlay.Pump();
                        SDL_Delay(1);
                    }
                });
        }

        /// <summary>Stops the listener, if it runs.</summary>
        void StopListening()
        {
            _Stop = true;

            if (_Listener.joinable())
            {
                _Listener.join();
            }
        }

        /// <summary>Leaves the session once what waits to be sent is out, the link flushed before it closes.</summary>
        void Disconnect()
        {
            constexpr int32_t MostWaits = 200;
            constexpr int32_t Flushes = 20;

            for (int32_t wait = 0; wait < MostWaits && Sending(); wait++)
            {
                SDL_Delay(1);
            }

            StopListening();

            for (int32_t flush = 0; flush < Flushes; flush++)
            {
                _DirectPlay.Pump();
                SDL_Delay(1);
            }

            _DirectPlay.Close();
        }

        /// <summary>Leaves <paramref name="message"/> for the listener, which owns the connection, to send to <paramref name="to"/>.</summary>
        template <typename Message> void Send(uint32_t to, Message message)
        {
            const std::span<uint8_t> bytes = LinkupBytes(message);
            const std::lock_guard lock(_Mutex);
            _ToSend.emplace_back(to, std::vector<uint8_t>(bytes.begin(), bytes.end()));
        }

        /// <summary>
        /// Counts <paramref name="message"/> of <paramref name="type"/> if it is an Updating or a RestartOff, and keeps
        /// what a RoomEnds says; whether it was one of them.
        /// </summary>
        bool Counted(uint16_t type, std::span<const uint8_t> message)
        {
            if (type == static_cast<uint16_t>(MCMPMessageType::RoomEnds) && message.size() >= sizeof(MCMPLongMessage))
            {
                const std::lock_guard lock(_Mutex);
                _RoomEnds.push_back(ReadLinkupMessage<MCMPLongMessage>(message).Value);
                return true;
            }

            if (type == static_cast<uint16_t>(MCMPMessageType::Updating))
            {
                _Updatings++;
                return true;
            }

            if (type == static_cast<uint16_t>(MCMPMessageType::RestartOff))
            {
                _RestartOffs++;
                return true;
            }

            return false;
        }

        MCDirectPlay _DirectPlay;
        /// <summary>Guards the messages to send, and what the class made from it keeps of the messages taken in.</summary>
        std::mutex _Mutex;

    private:
        /// <summary>Sends <paramref name="type"/>, a header alone, to player <paramref name="to"/>.</summary>
        void SendNotice(uint32_t to, MCMPMessageType type)
        {
            MCMPLongMessage message{};
            message.Header = PlainHeader(type);
            Send(to, message);
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

        uint32_t _Id = 0;
        std::thread _Listener;
        std::atomic<bool> _Stop = false;
        /// <summary>The messages to send and to whom, until the listener sends them.</summary>
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> _ToSend;
        std::atomic<int32_t> _Updatings = 0;
        std::atomic<int32_t> _RestartOffs = 0;
        /// <summary>What each RoomEnds said (guarded by <see cref="_Mutex"/>).</summary>
        std::vector<int32_t> _RoomEnds;
    };

    /// <summary>
    /// A second player in the game's session, on the test's side: it joins over the loopback and then only takes in
    /// (and drops) what it is sent, on its own thread, so the host keeps it.
    /// </summary>
    class Guest final : public Peer
    {
    public:
        /// <summary>
        /// A guest who joins as <paramref name="name"/>, its join card saying its game plays by <paramref name="card"/>,
        /// as this build's game's does (none: an older build's join, which says nothing).
        /// </summary>
        explicit Guest(std::string name = "Guest", std::optional<uint32_t> card = std::nullopt)
            : _Name(std::move(name)), _Card(card)
        {
        }

        ~Guest() override { StopListening(); }

        Guest(const Guest&) = delete;
        Guest& operator=(const Guest&) = delete;

        /// <summary>
        /// Joins the session at <paramref name="address"/> (the game's <paramref name="host"/> pumped meanwhile), and
        /// pings as a game's guest does once its room's screen is up (<see cref="Ping"/>), unless
        /// <paramref name="pings"/> is false: a game still on its way to the room. A host that refuses it at the door
        /// says why (<see cref="Refusal"/>), and the link is closed.
        /// </summary>
        bool Join(MCDirectPlay& host, std::string_view address, bool pings = true)
        {
            constexpr int32_t MostLooks = 40;

            if (Connect(_DirectPlay, address) != DP_OK)
            {
                return false;
            }

            DPSESSIONDESC2 found{};
            bool foundOne = false;

            for (int32_t attempt = 0; attempt < MostLooks && !foundOne; attempt++)
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
            const std::vector<uint8_t> card = _Card.has_value() ? MCJoinCard(*_Card) : std::vector<uint8_t>();
            uint32_t id = 0;
            _Refusal.clear();

            if (WhilePumping(host, [&] { return _DirectPlay.CreatePlayer(id, name, card, 0); }) != DP_OK)
            {
                _Refusal = _DirectPlay.LastRefusal();
                _DirectPlay.Close();
                return false;
            }

            StartListening(id);

            if (pings)
            {
                Ping();
            }

            return true;
        }

        /// <summary>
        /// Pings every player for a while, as a game's guest does from its room's screen as it opens: the host then shows
        /// it the room. Often, as the game's frames go by faster than the clock here: the first pings may reach the host
        /// before it has taken in this guest's arrival, and go unheard.
        /// </summary>
        void Ping() { _PingUntil = std::chrono::steady_clock::now() + PingTime; }

        /// <summary>What the host said its refusal at the door was for (the options' names), if it refused one.</summary>
        const std::string& Refusal() const { return _Refusal; }

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

        /// <summary>The host's messages taking every player to the session screen (its room shown again).</summary>
        int32_t SessionScreens() const { return _SessionScreens; }

        /// <summary>Leaves the session: the listener stops once what waits to be sent is out, and the link closes.</summary>
        void Leave() { Disconnect(); }

        /// <summary>
        /// Answers the host's file inquiries from now on with <paramref name="value"/>, as a game's file report does
        /// (0: no file).
        /// </summary>
        void AnswerFiles(uint32_t value) { _FileAnswer = value; }

        /// <summary>The host's messages telling this guest to load a mission, so far.</summary>
        int32_t MissionsLoaded() const { return _MissionsLoaded; }

        /// <summary>
        /// Keeps every message of <paramref name="type"/> the host sends from now on, each once (the host sends a
        /// guaranteed message again, byte for byte, until it is acknowledged): <see cref="Watched"/>.
        /// </summary>
        void Watch(MCMPMessageType type)
        {
            const std::lock_guard lock(_Mutex);
            _WatchedMessages.try_emplace(static_cast<uint16_t>(type));
        }

        /// <summary>The messages of <paramref name="type"/> (one <see cref="Watch"/> named), in the order they came.</summary>
        std::vector<std::vector<uint8_t>> Watched(MCMPMessageType type)
        {
            const std::lock_guard lock(_Mutex);
            const auto watched = _WatchedMessages.find(static_cast<uint16_t>(type));
            return watched != _WatchedMessages.end() ? watched->second : std::vector<std::vector<uint8_t>>();
        }

        /// <summary>
        /// Sends <paramref name="message"/> to player <paramref name="to"/> as a game's would (from the listener, which
        /// owns the connection).
        /// </summary>
        template <typename Message> void SendTo(uint32_t to, Message message) { Send(to, message); }

    private:
        /// <summary>How long a guest pings (<see cref="Ping"/>), and how often.</summary>
        static constexpr std::chrono::seconds PingTime{1};
        static constexpr std::chrono::milliseconds PingEvery{5};

        void Take(uint32_t from, std::span<const uint8_t> message) override
        {
            Answer(from, message);
            Keep(message);
            KeepWatched(message);
        }

        /// <summary>Keeps <paramref name="message"/> if it is of a watched type, the first time it comes.</summary>
        void KeepWatched(std::span<const uint8_t> message)
        {
            if (message.size() < sizeof(MCFIGuaranteedMessageHeader))
            {
                return;
            }

            const std::lock_guard lock(_Mutex);
            const auto watched = _WatchedMessages.find(ReadLinkupMessage<MCFIGuaranteedMessageHeader>(message).Type());

            if (watched != _WatchedMessages.end() && _WatchedTaken.emplace(message.begin(), message.end()).second)
            {
                watched->second.emplace_back(message.begin(), message.end());
            }
        }

        /// <summary>A ping, while <see cref="Ping"/>'s time lasts.</summary>
        void EachPass() override
        {
            const auto now = std::chrono::steady_clock::now();

            if (now >= _PingUntil.load() || now < _NextPing)
            {
                return;
            }

            _NextPing = now + PingEvery;
            MCFIPingMessage ping;
            ping.Header = LinkupHeader(MCLinkupMessageType::Ping);
            Send(DPID_ALLPLAYERS, ping);
        }

        /// <summary>
        /// Answers <paramref name="message"/> from player <paramref name="from"/> if it is a file inquiry (the file
        /// report: <see cref="AnswerFiles"/>'s value and the name asked about), and counts a load mission. The host sends
        /// a guaranteed message again until it is acknowledged, which this guest never does: each is taken once from
        /// its sender (a room rolled over to another host may send the same bytes again).
        /// </summary>
        void Answer(uint32_t from, std::span<const uint8_t> message)
        {
            constexpr size_t NameOffset = sizeof(MCFIGuaranteedMessageHeader);

            if (message.size() <= NameOffset)
            {
                return;
            }

            const uint16_t type = ReadLinkupMessage<MCFIGuaranteedMessageHeader>(message).Type();

            if (Counted(type, message))
            {
                return;
            }

            std::vector<uint8_t> taken(sizeof(from));
            std::memcpy(taken.data(), &from, sizeof(from));
            taken.insert(taken.end(), message.begin(), message.end());

            if (type == static_cast<uint16_t>(MCMPMessageType::SwitchScreen) && _Taken.insert(taken).second)
            {
                _SessionScreens++;
                return;
            }

            if ((type != static_cast<uint16_t>(MCMPMessageType::LoadMission) &&
                 type != static_cast<uint16_t>(MCMPMessageType::FileInquiry)) ||
                !_Taken.insert(taken).second)
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
            _DirectPlay.Send(Id(), from, 0, bytes);
        }

        /// <summary>
        /// Keeps <paramref name="message"/> if it is the host's match options or players' rules, the first time it comes:
        /// the host sends a guaranteed message again, byte for byte, until it is acknowledged, so an older one's
        /// re-send would stand for the newest.
        /// </summary>
        void Keep(std::span<const uint8_t> message)
        {
            if (!_Kept.emplace(message.begin(), message.end()).second)
            {
                return;
            }

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

        std::string _Name;
        /// <summary>The rules the guest's join card says, if it sends one.</summary>
        std::optional<uint32_t> _Card;
        /// <summary>The host's refusal at the door (the test's thread's own, as is the join).</summary>
        std::string _Refusal;
        std::optional<MCMPMatchOptionsMessage> _MatchOptions;
        std::optional<MCMPPlayersRulesMessage> _PlayersRules;
        /// <summary>The host's messages taking every player to the session screen.</summary>
        std::atomic<int32_t> _SessionScreens = 0;
        /// <summary>What the guest's file reports say (<see cref="AnswerFiles"/>).</summary>
        std::atomic<uint32_t> _FileAnswer = 0;
        /// <summary>The load missions the host sent.</summary>
        std::atomic<int32_t> _MissionsLoaded = 0;
        /// <summary>The inquiries and load missions taken, and the messages kept (the listener's own).</summary>
        std::set<std::vector<uint8_t>> _Taken;
        std::set<std::vector<uint8_t>> _Kept;
        /// <summary>The messages of the watched types taken, and kept by type.</summary>
        std::set<std::vector<uint8_t>> _WatchedTaken;
        std::map<uint16_t, std::vector<std::vector<uint8_t>>> _WatchedMessages;
        /// <summary>Until when the guest pings (<see cref="Ping"/>), and its next ping (the listener's own).</summary>
        std::atomic<std::chrono::steady_clock::time_point> _PingUntil{};
        std::chrono::steady_clock::time_point _NextPing{};
    };

    /// <summary>
    /// A room's host on the test's side, for the game to join as a lobby's guest: it hosts a session on the loopback
    /// and takes in what it is sent on a thread of its own, counting the guests' Updating messages.
    /// </summary>
    class Host final : public Peer
    {
    public:
        /// <summary>The most players a room holds.</summary>
        static constexpr uint32_t MaxPlayers = 6;

        Host() = default;
        ~Host() override { StopListening(); }

        Host(const Host&) = delete;
        Host& operator=(const Host&) = delete;

        /// <summary>
        /// Hosts session <paramref name="session"/> on <paramref name="port"/> as player <paramref name="name"/>, with
        /// <paramref name="flags"/> (DPSESSION_*: a started room's), for <paramref name="places"/> players.
        /// </summary>
        bool Open(std::string_view port, std::string name, std::string session, uint32_t flags = 0,
                  uint32_t places = MaxPlayers)
        {
            if (Connect(_DirectPlay, std::format(":{}", port)) != DP_OK)
            {
                return false;
            }

            // The admission runs on the listener, which owns the connection once it is open.
            _DirectPlay.SetAdmission(
                [this](std::string_view, std::span<const uint8_t> data) -> std::optional<std::string>
                {
                    const std::lock_guard lock(_Mutex);
                    _Cards.push_back(MCJoinCardRules(data));
                    return _RefuseWith;
                });
            DPSESSIONDESC2& desc = _Desc;
            desc.dwSize = sizeof(desc);
            desc.dwFlags = flags;
            desc.guidApplication = MultiPlayerAppGuid;
            desc.dwMaxPlayers = places;
            desc.SessionName = std::move(session);
            uint32_t id = 0;

            if (_DirectPlay.Open(desc, DPOPEN_CREATE | DPOPEN_RETURNSTATUS) != DP_OK ||
                _DirectPlay.CreatePlayer(id, DPNAME{std::move(name), std::nullopt}, {}, 0) != DP_OK)
            {
                return false;
            }

            StartListening(id);
            return true;
        }

        /// <summary>Closes the room to new players, as a host's GO does (on the listener, which owns the connection).</summary>
        void Lock() { _AdmitWanted = Admit::Close; }

        /// <summary>Opens the room to new players again, as a host's restart called off does (on the listener).</summary>
        void Unlock() { _AdmitWanted = Admit::Open; }

        /// <summary>
        /// Refuses every joiner at the door from now on, saying it lacks <paramref name="options"/> (the options' names),
        /// as a host whose match its game can't play does.
        /// </summary>
        void RefuseWith(std::string options)
        {
            const std::lock_guard lock(_Mutex);
            _RefuseWith = std::move(options);
        }

        /// <summary>
        /// The rules each joiner's join card said, in the order they came to the door (none for a join without one).
        /// </summary>
        std::vector<std::optional<uint32_t>> Cards()
        {
            const std::lock_guard lock(_Mutex);
            return _Cards;
        }

        /// <summary>The players who joined, by id, as DirectPlay announced them.</summary>
        std::vector<uint32_t> Joined()
        {
            const std::lock_guard lock(_Mutex);
            return _Joined;
        }

        /// <summary>Ends the session once what waits to be sent is out, as the host's game quitting does.</summary>
        void Close() { Disconnect(); }

    private:
        /// <summary>Counts <paramref name="message"/> from <paramref name="from"/> if it is a guest's Updating or a player joining.</summary>
        void Take(uint32_t from, std::span<const uint8_t> message) override
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

            if (message.size() >= sizeof(MCFIGuaranteedMessageHeader))
            {
                Counted(ReadLinkupMessage<MCFIGuaranteedMessageHeader>(message).Type(), message);
            }
        }

        /// <summary>The room closed to new players, or opened to them again, when the test asked.</summary>
        void EachPass() override
        {
            const Admit wanted = _AdmitWanted.exchange(Admit::AsIs);

            if (wanted == Admit::AsIs)
            {
                return;
            }

            if (wanted == Admit::Close)
            {
                _Desc.dwFlags |= DPSESSION_NEWPLAYERSDISABLED;
            }
            else
            {
                _Desc.dwFlags &= ~DPSESSION_NEWPLAYERSDISABLED;
            }

            _DirectPlay.SetSessionDesc(_Desc, 0);
        }

        /// <summary>What the test wants of the room's new players: as it is, closed out, or let in.</summary>
        enum class Admit : uint8_t
        {
            AsIs,
            Close,
            Open
        };

        std::vector<uint32_t> _Joined;
        /// <summary>The joiners' cards' rules, and the refusal every joiner gets (none: each is let in).</summary>
        std::vector<std::optional<uint32_t>> _Cards;
        std::optional<std::string> _RefuseWith;
        /// <summary>The session as hosted, and what the test wants of its new players.</summary>
        DPSESSIONDESC2 _Desc{};
        std::atomic<Admit> _AdmitWanted = Admit::AsIs;
    };

    /// <summary>Runs one frame of a fifteenth of a second, giving the network a moment first.</summary>
    inline void Step()
    {
        constexpr uint32_t NetworkMs = 3;
        SDL_Delay(NetworkMs);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    /// <summary>Runs frames for two seconds of game time, giving the network a moment each; the last frame's hash.</summary>
    inline uint32_t Settle()
    {
        constexpr int32_t Frames = 30;

        for (int32_t frame = 0; frame < Frames; frame++)
        {
            Step();
        }

        return MCScreenInput::ScreenHash();
    }

    /// <summary>
    /// Runs frames until <paramref name="done"/> says so, for ten seconds at most (the network's answers come in real
    /// time, slower on a loaded machine); whether it did.
    /// </summary>
    inline bool SettleUntil(const std::function<bool()>& done)
    {
        constexpr std::chrono::seconds MostWait{10};
        const auto deadline = std::chrono::steady_clock::now() + MostWait;

        while (!done())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            Step();
        }

        return true;
    }

    /// <summary>Whether the game's session screen shows player <paramref name="id"/>.</summary>
    inline bool RoomShows(uint32_t id)
    {
        return std::ranges::any_of(GlobalLog()->SessionScreen->PlayerNames, [id](const auto& name)
                                   { return name != nullptr && name->ShowWindow && name->PlayerId == id; });
    }

    /// <summary>
    /// Boots the game as a lobby's host on <paramref name="port"/> (straight into its room, the session screen) and has
    /// <paramref name="guests"/> join it; whether the room came to show them all (the host shows it again for each).
    /// </summary>
    inline bool HostLobbyRoom(std::string_view port, std::initializer_list<std::reference_wrapper<Guest>> guests)
    {
        if (!MCTestGame::StartLobby(std::format("-host -port {} -player BOB", port)) || MultiPlayer() == nullptr ||
            MultiPlayer()->SessionManager->DirectPlay == nullptr ||
            GlobalLog()->CurrentScreen() != GlobalLog()->SessionScreen)
        {
            return false;
        }

        for (Guest& guest : guests)
        {
            if (!guest.Join(*MultiPlayer()->SessionManager->DirectPlay, std::format("127.0.0.1:{}", port)))
            {
                return false;
            }
        }

        const bool shown = SettleUntil(
            [&guests]
            {
                return std::ranges::all_of(guests, [](const Guest& guest) { return RoomShows(guest.Id()); }) &&
                       std::ranges::all_of(guests, [](const Guest& guest) { return guest.SessionScreens() > 0; });
            });
        Settle();
        return shown && GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen;
    }
}
