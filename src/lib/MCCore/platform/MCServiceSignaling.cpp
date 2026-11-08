#include "stdafx.h"
#include "platform/MCServiceSignaling.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <limits>

namespace
{
    using Json = nlohmann::json;

    constexpr int ProtocolVersion = MCServiceProtocol;
    /// <summary>A game's code: four characters with no lookalikes (no 0 and O, 1 and I), as the service makes them.</summary>
    constexpr std::string_view CodeAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    constexpr size_t CodeLength = 4;
    /// <summary>A place's token: 128 random bits as hex.</summary>
    constexpr size_t TokenLength = 32;
    /// <summary>A guest as the service names it: "g" and its number.</summary>
    constexpr char GuestPrefix = 'g';
    constexpr size_t Uint32Bytes = 4;
    /// <summary>A signal's parts, as the service passes them (and a short field's, a kind's).</summary>
    constexpr size_t MaxSignalText = MCIceLink::Signal::MostText;
    constexpr size_t MaxSignalKind = MCIceLink::Signal::MostKind;
    constexpr size_t MaxMid = MCIceLink::Signal::MostMid;
    /// <summary>A room's texts the service would refuse empty: what stands in for them.</summary>
    constexpr std::string_view GameNameFallback = "GAME";
    constexpr std::string_view PlayerNameFallback = "PLAYER";
    /// <summary>Printable ASCII, and Windows-1252's letters from À on as their plain ones (× x, ÷ nothing).</summary>
    constexpr char FirstPrintable = 0x20;
    constexpr char LastPrintable = 0x7e;
    constexpr uint8_t FirstLatinLetter = 0xc0;
    constexpr std::array<std::string_view, 64> LatinLetters{
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I",  "I",
        "D", "N", "O", "O", "O", "O", "O",  "x", "O", "U", "U", "U", "U", "Y", "TH", "ss",
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i",  "i",
        "d", "n", "o", "o", "o", "o", "o",  "",  "o", "u", "u", "u", "u", "y", "th", "y"};
    /// <summary>Windows-1252's other letters (0x80 to 0xBF) with plain ones, and its no-break space.</summary>
    constexpr std::array<std::pair<uint8_t, std::string_view>, 8> OtherLetters{{
        {0x8a, "S"},
        {0x8c, "OE"},
        {0x8e, "Z"},
        {0x9a, "s"},
        {0x9c, "oe"},
        {0x9e, "z"},
        {0x9f, "Y"},
        {0xa0, " "},
    }};

    /// <summary>The plain ASCII for Windows-1252's <paramref name="c"/>: itself, its letter's, or nothing.</summary>
    std::string_view PlainOf(const char& c)
    {
        if (c >= FirstPrintable && c <= LastPrintable)
        {
            return {&c, 1};
        }

        const auto code = static_cast<uint8_t>(c);

        if (code >= FirstLatinLetter)
        {
            return LatinLetters[code - FirstLatinLetter];
        }

        const auto other = std::ranges::find(OtherLetters, code, &std::pair<uint8_t, std::string_view>::first);
        return other != OtherLetters.end() ? other->second : std::string_view();
    }

    /// <summary>The service's STUN and relays: a handful of each, their names and credentials short.</summary>
    constexpr size_t MaxIceServers = 8;
    constexpr size_t MaxIceText = 512;
    constexpr uint16_t LowestPort = 1;
    /// <summary>The guests whose relays are kept: far past a room's, over a long evening of guests coming and going.</summary>
    constexpr size_t MaxGuestSettings = 64;
    /// <summary>STUN before the service answers (and the service's own), as host:port.</summary>
    constexpr std::string_view DefaultStun = "stun.cloudflare.com:3478";
    /// <summary>The service's answer to its keep-alive (MCServicePingIfDue).</summary>
    constexpr std::string_view PongText = MCServicePong;
    constexpr std::string_view HostPath = "/host";
    constexpr std::string_view JoinPath = "/join/";
    constexpr std::string_view BackPath = "/game/";
    /// <summary>Port-only (Redux HD): a guest's socket taking its game over as its host (with its place's token).</summary>
    constexpr std::string_view TakeoverPath = "/takeover/";

    /// <summary>What the service's refusals and endings are called in its messages.</summary>
    constexpr std::array<std::pair<std::string_view, MCServiceRefusal>, 10> Refusals{{
        {"nogame", MCServiceRefusal::NoGame},
        {"hostaway", MCServiceRefusal::HostAway},
        {"battle", MCServiceRefusal::InBattle},
        {"full", MCServiceRefusal::Full},
        {"expired", MCServiceRefusal::PlaceGone},
        {"gone", MCServiceRefusal::GameOver},
        {"banned", MCServiceRefusal::Banned},
        {"slow", MCServiceRefusal::TooManyTries},
        {"update", MCServiceRefusal::Update},
        {"refused", MCServiceRefusal::Refused},
    }};

    std::optional<MCServiceRefusal> RefusalNamed(std::string_view type)
    {
        const auto found = std::ranges::find(Refusals, type, &std::pair<std::string_view, MCServiceRefusal>::first);
        return found != Refusals.end() ? std::optional(found->second) : std::nullopt;
    }

    bool IsCode(std::string_view text)
    {
        return text.size() == CodeLength &&
               std::ranges::all_of(text, [](char c) { return CodeAlphabet.find(c) != std::string_view::npos; });
    }

    bool IsToken(std::string_view text)
    {
        return text.size() == TokenLength &&
               std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    /// <summary>A guest's number from its name ("g3": 3), or none.</summary>
    std::optional<uint32_t> GuestNumber(std::string_view guest)
    {
        if (guest.size() < 2 || guest.front() != GuestPrefix || guest[1] == '0')
        {
            return std::nullopt;
        }

        uint32_t number = 0;
        const char* end = guest.data() + guest.size();
        const auto [at, error] = std::from_chars(guest.data() + 1, end, number);
        return error == std::errc() && at == end ? std::optional(number) : std::nullopt;
    }

    /// <summary>A URL's query value: unreserved characters as they are, every other byte %XX.</summary>
    std::string Percent(std::string_view text)
    {
        std::string out;

        for (const char c : text)
        {
            const auto byte = static_cast<unsigned char>(c);

            if (std::isalnum(byte) != 0 || c == '-' || c == '.' || c == '_' || c == '~')
            {
                out += c;
            }
            else
            {
                out += std::format("%{:02X}", byte);
            }
        }

        return out;
    }

    void PutU32(std::vector<uint8_t>& out, uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out.push_back(static_cast<uint8_t>(value >> shift));
        }
    }

    uint32_t U32(std::string_view bytes, size_t at)
    {
        uint32_t value = 0;

        for (size_t i = 0; i < Uint32Bytes; i++)
        {
            value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + i])) << (8 * i);
        }

        return value;
    }

    /// <summary>A short string field, or none when it's missing, not a string or longer than <paramref name="most"/>.</summary>
    std::optional<std::string> TextField(const Json& message, const char* name, size_t most)
    {
        const auto found = message.find(name);

        if (found == message.end() || !found->is_string() || found->get_ref<const std::string&>().size() > most)
        {
            return std::nullopt;
        }

        return found->get<std::string>();
    }

    std::optional<uint32_t> Uint32Field(const Json& message, const char* name)
    {
        const auto found = message.find(name);

        if (found == message.end() || !found->is_number_unsigned() ||
            found->get<uint64_t>() > std::numeric_limits<uint32_t>::max())
        {
            return std::nullopt;
        }

        return static_cast<uint32_t>(found->get<uint64_t>());
    }

    /// <summary>A link's signal in a message from the service, or none when it isn't one.</summary>
    std::optional<MCIceLink::Signal> SignalIn(const Json& message)
    {
        std::optional<std::string> kind = TextField(message, "kind", MaxSignalKind);
        std::optional<std::string> text = TextField(message, "text", MaxSignalText);
        std::optional<std::string> mid = TextField(message, "mid", MaxMid);

        if (!kind || !text || !mid)
        {
            return std::nullopt;
        }

        return MCIceLink::Signal{.Kind = std::move(*kind), .Text = std::move(*text), .Mid = std::move(*mid)};
    }

    /// <summary>The service's STUN servers and relays (a message's "ice"), or none when they don't parse.</summary>
    std::optional<MCIceLink::Settings> IceIn(const Json& message)
    {
        const auto ice = message.find("ice");

        if (ice == message.end() || !ice->is_object())
        {
            return std::nullopt;
        }

        const auto stun = ice->find("stun");
        const auto relays = ice->find("relays");

        if (stun == ice->end() || !stun->is_array() || stun->size() > MaxIceServers || relays == ice->end() ||
            !relays->is_array() || relays->size() > MaxIceServers)
        {
            return std::nullopt;
        }

        MCIceLink::Settings settings;

        for (const Json& server : *stun)
        {
            if (!server.is_string() || server.get_ref<const std::string&>().size() > MaxIceText)
            {
                return std::nullopt;
            }

            settings.Stun.push_back(server.get<std::string>());
        }

        for (const Json& relay : *relays)
        {
            if (!relay.is_object())
            {
                return std::nullopt;
            }

            std::optional<std::string> host = TextField(relay, "host", MaxIceText);
            std::optional<std::string> user = TextField(relay, "user", MaxIceText);
            std::optional<std::string> password = TextField(relay, "password", MaxIceText);
            const std::optional<uint32_t> port = Uint32Field(relay, "port");

            if (!host || !user || !password || !port || *port < LowestPort || *port > UINT16_MAX)
            {
                return std::nullopt;
            }

            settings.Relays.push_back({.Host = std::move(*host),
                                       .Port = static_cast<uint16_t>(*port),
                                       .User = std::move(*user),
                                       .Password = std::move(*password)});
        }

        return settings;
    }

    /// <summary>A JSON message as text (a description's text is ASCII; anything else is replaced, never thrown on).</summary>
    std::string Dumped(const Json& message)
    {
        return message.dump(-1, ' ', false, Json::error_handler_t::replace);
    }

    Json SignalOut(uint32_t link, const MCIceLink::Signal& signal)
    {
        return {{"t", "signal"}, {"link", link}, {"kind", signal.Kind}, {"text", signal.Text}, {"mid", signal.Mid}};
    }

    /// <summary>A socket that never opened: to a name that isn't a game's code, or one the system couldn't open.</summary>
    class MCClosedWebSocket final : public MCWebSocket
    {
    public:
        explicit MCClosedWebSocket(std::string why) : _Why(std::move(why)) {}

        void SendText(std::string_view) override {}
        void SendBinary(std::span<const uint8_t>) override {}
        std::vector<Message> Take() override { return {}; }
        State GetState() const override { return State::Closed; }
        std::string WhyClosed() const override { return _Why; }
        size_t Unsent() const override { return 0; }
        void Close() override {}
        void CloseAway() override {}

    private:
        std::string _Why;
    };

    /// <summary>A game's socket a code can't reach: closed at once, and refused as no game.</summary>
    constexpr std::string_view NotACode = "not a game's code";
}

MCServiceSignaling::MCServiceSignaling(std::string service, MCWebSocketOpener open, MCServiceIdentity identity,
                                       bool relayOnly, MCServiceComeback comeback)
    : _Service(std::move(service))
    , _Open(std::move(open))
    , _Identity(std::move(identity))
    , _RelayOnly(relayOnly)
    , _Comeback(comeback)
{
}

MCServiceSignaling::~MCServiceSignaling()
{
    const std::scoped_lock lock(_Lock);
    CloseAll();
}

void MCServiceSignaling::Leave()
{
    const std::scoped_lock lock(_Lock);
    _Left = true;
    CloseAll();
}

void MCServiceSignaling::LeaveAway()
{
    const std::scoped_lock lock(_Lock);
    _Left = true;

    if (_Host.has_value())
    {
        _Host->Ws->CloseAway();
    }

    for (auto& [game, socket] : _Games)
    {
        socket.Ws->CloseAway();
    }
}

void MCServiceSignaling::CloseAll()
{
    // A socket's own close is its player's leaving: the service frees its place (the host's ends its game).
    if (_Host.has_value())
    {
        _Host->Ws->Close();
    }

    for (auto& [game, socket] : _Games)
    {
        socket.Ws->Close();
    }
}

namespace
{
    /// <summary>Whether a socket is done with: ended by the service, or closed before it was ever in.</summary>
    template <typename TSocket> bool Over(const TSocket& socket)
    {
        return socket.Refusal.has_value() ||
               (socket.Ws->GetState() == MCWebSocket::State::Closed && socket.Token.empty());
    }
}

void MCServiceSignaling::Host()
{
    const std::scoped_lock lock(_Lock);

    if (!_Host.has_value() || Over(*_Host))
    {
        _Host = Socket{.Ws = Opened(HostPath), .LastPing = Clock::now()};
        _Guests.clear();
        _GuestSettings.clear();
        _NameRefused = false;
    }
}

void MCServiceSignaling::HostAgain(std::string_view code, std::string_view token)
{
    const std::scoped_lock lock(_Lock);
    const Clock::time_point now = Clock::now();
    // A dropped host's socket, coming back to its place: its first try made now, Tend's the others while the place
    // waits.
    constexpr int32_t TriesMade = 1;
    _Host = Socket{.Ws = Opened(std::format("{}{}", BackPath, code), token),
                   .LastPing = now,
                   .Code = std::string(code),
                   .Token = std::string(token),
                   .DroppedAt = now,
                   .NextTry = now + _Comeback.Tries.Wait(TriesMade),
                   .Tries = TriesMade};
    _Guests.clear();
    _GuestSettings.clear();
    _NameRefused = false;
    SDL_Log("MCServiceSignaling: hosting game \"%.*s\" again, back in its place", static_cast<int>(code.size()),
            code.data());
}

std::optional<std::string> MCServiceSignaling::HostedCode()
{
    const std::scoped_lock lock(_Lock);
    return _Host.has_value() && _Host->Hosted && !_Host->Refusal.has_value() ? std::optional(_Host->Code)
                                                                             : std::nullopt;
}

std::optional<std::string> MCServiceSignaling::HostedToken()
{
    const std::scoped_lock lock(_Lock);
    return _Host.has_value() && _Host->Hosted && !_Host->Refusal.has_value() ? std::optional(_Host->Token)
                                                                             : std::nullopt;
}

void MCServiceSignaling::SetRoom(const MCServiceRoom& room)
{
    const std::scoped_lock lock(_Lock);

    if (_Room == room)
    {
        return;
    }

    if (!_Room.has_value() || _Room->Name != room.Name)
    {
        _NameRefused = false;
    }

    _Room = room;
    SendRoom();
}

bool MCServiceSignaling::NameRefused()
{
    const std::scoped_lock lock(_Lock);
    return _NameRefused;
}

void MCServiceSignaling::SendRoom()
{
    if (!_Room.has_value() || !_Host.has_value() || !_Host->In)
    {
        return;
    }

    // The game's names as the service takes them (it closes the host's socket on a room it can't read).
    std::vector<std::string> players;
    std::ranges::transform(_Room->Players, std::back_inserter(players), [](const std::string& player)
                           { return MCServiceName(player, MCServicePlayerNameMost, PlayerNameFallback); });
    _Host->Ws->SendText(Dumped({{"t", "room"},
                                {"name", MCServiceName(_Room->Name, MCServiceGameNameMost, GameNameFallback)},
                                {"public", _Room->Public},
                                {"max", _Room->Max},
                                {"players", players},
                                {"mission", MCServiceName(_Room->Mission, MCServiceMissionMost, {})},
                                {"battle", _Room->Battle}}));
}

bool MCServiceSignaling::Joined(std::string_view game)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Games.find(game);
    return found != _Games.end() && found->second.In && found->second.Ws->GetState() == MCWebSocket::State::Open;
}

void MCServiceSignaling::Join(std::string_view game)
{
    const std::scoped_lock lock(_Lock);

    if (const auto found = _Games.find(game); found != _Games.end() && Over(found->second))
    {
        _Games.erase(found);
    }

    GameSocket(game);
}

std::optional<MCServiceRefusal> MCServiceSignaling::Refusal(std::string_view game)
{
    const std::scoped_lock lock(_Lock);

    if (game.empty())
    {
        return _Host.has_value() ? _Host->Refusal : std::nullopt;
    }

    const auto found = _Games.find(game);
    return found != _Games.end() ? found->second.Refusal : std::nullopt;
}

bool MCServiceSignaling::HostAway(std::string_view game)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Games.find(game);
    return found != _Games.end() && found->second.HostAway;
}

bool MCServiceSignaling::TakeOver(std::string_view game)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Games.find(game);

    if (found == _Games.end() || !found->second.In || found->second.Token.empty() || _Left)
    {
        SDL_Log("MCServiceSignaling: can't take game \"%.*s\" over: this game isn't in it",
                static_cast<int>(game.size()), game.data());
        return false;
    }

    Socket& joined = found->second;
    const Clock::time_point now = Clock::now();
    // A public game's links take relays only, the host's too.
    _RelayOnly = _RelayOnly || joined.Public;
    _Host = Socket{.Ws = Opened(std::format("{}{}", TakeoverPath, game), joined.Token),
                   .LastPing = now,
                   .Code = std::string(game),
                   .Token = joined.Token};
    _Guests.clear();
    _GuestSettings.clear();
    _NameRefused = false;
    // Its place is the host's now: the socket to the game goes, never coming back to it.
    joined.TakenOver = true;
    joined.Ws->Close();
    SDL_Log("MCServiceSignaling: taking game \"%.*s\" over as its host", static_cast<int>(game.size()), game.data());
    return true;
}

int32_t MCServiceSignaling::HostMoves(std::string_view game)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Games.find(game);
    return found != _Games.end() ? found->second.HostMoves : 0;
}

bool MCServiceSignaling::GamePublic(std::string_view game)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Games.find(game);
    return found == _Games.end() || found->second.Public;
}

std::optional<std::string> MCServiceSignaling::WhyClosed(std::string_view game)
{
    const std::scoped_lock lock(_Lock);
    const auto why = [](const Socket& socket)
    {
        return socket.Ws->GetState() == MCWebSocket::State::Closed ? std::optional(socket.Ws->WhyClosed())
                                                                   : std::nullopt;
    };

    if (game.empty())
    {
        return _Host.has_value() ? why(*_Host) : std::nullopt;
    }

    const auto found = _Games.find(game);
    return found != _Games.end() ? why(found->second) : std::nullopt;
}

bool MCServiceSignaling::AllClosed()
{
    const std::scoped_lock lock(_Lock);
    const auto closed = [](const Socket& socket) { return socket.Ws->GetState() == MCWebSocket::State::Closed; };
    return (!_Host.has_value() || closed(*_Host)) &&
           std::ranges::all_of(_Games, [&](const auto& game) { return closed(game.second); });
}

void MCServiceSignaling::BindLinksTo(std::string address)
{
    const std::scoped_lock lock(_Lock);
    _BindAddress = std::move(address);
}

MCIceLink::Settings MCServiceSignaling::WithPolicy(MCIceLink::Settings settings, bool relayOnly) const
{
    settings.RelayOnly = _RelayOnly || relayOnly;

    if (!_BindAddress.empty())
    {
        settings.BindAddress = _BindAddress;
    }

    return settings;
}

MCIceLink::Settings MCServiceSignaling::LinkSettings(std::string_view peer)
{
    const std::scoped_lock lock(_Lock);

    if (const std::optional<uint32_t> guest = GuestNumber(peer))
    {
        if (const auto found = _GuestSettings.find(*guest); found != _GuestSettings.end())
        {
            return WithPolicy(found->second, false);
        }
    }
    else if (const auto found = _Games.find(peer); found != _Games.end() && found->second.Settings.has_value())
    {
        return WithPolicy(*found->second.Settings, found->second.Public);
    }

    // No relays from the service yet: STUN alone, and a game reached taken as public until the service says.
    MCIceLink::Settings settings;
    settings.Stun.emplace_back(DefaultStun);
    return WithPolicy(std::move(settings), !GuestNumber(peer).has_value());
}

void MCServiceSignaling::ToHost(std::string_view game, uint32_t link, const MCIceLink::Signal& signal)
{
    const std::scoped_lock lock(_Lock);
    GameSocket(game).Ws->SendText(Dumped(SignalOut(link, signal)));
}

void MCServiceSignaling::ToGuest(std::string_view guest, uint32_t link, const MCIceLink::Signal& signal)
{
    const std::scoped_lock lock(_Lock);

    if (_Host.has_value())
    {
        Json message = SignalOut(link, signal);
        message["guest"] = guest;
        _Host->Ws->SendText(Dumped(message));
    }
}

void MCServiceSignaling::BytesToHost(std::string_view game, uint32_t link, std::span<const uint8_t> bytes)
{
    const std::scoped_lock lock(_Lock);
    std::vector<uint8_t> message;
    PutU32(message, link);
    message.insert(message.end(), bytes.begin(), bytes.end());
    GameSocket(game).Ws->SendBinary(message);
}

void MCServiceSignaling::BytesToGuest(std::string_view guest, uint32_t link, std::span<const uint8_t> bytes)
{
    const std::scoped_lock lock(_Lock);
    const std::optional<uint32_t> number = GuestNumber(guest);

    if (!_Host.has_value() || !number.has_value())
    {
        return;
    }

    std::vector<uint8_t> message;
    PutU32(message, *number);
    PutU32(message, link);
    message.insert(message.end(), bytes.begin(), bytes.end());
    _Host->Ws->SendBinary(message);
}

size_t MCServiceSignaling::Unsent()
{
    const std::scoped_lock lock(_Lock);
    size_t unsent = _Host.has_value() ? _Host->Ws->Unsent() : 0;

    for (const auto& [game, socket] : _Games)
    {
        unsent += socket.Ws->Unsent();
    }

    return unsent;
}

std::vector<MCIceSignaling::Heard> MCServiceSignaling::Take()
{
    const std::scoped_lock lock(_Lock);
    const auto now = Clock::now();
    std::vector<Heard> heard;

    // Left: no socket comes back.
    if (_Left)
    {
        return heard;
    }

    if (_Host.has_value())
    {
        for (const MCWebSocket::Message& message : _Host->Ws->Take())
        {
            HearHost(message, heard);
        }

        Tend(*_Host, {}, now, heard);
        MCServicePingIfDue(*_Host->Ws, _Host->LastPing, now);
    }

    for (auto& [game, socket] : _Games)
    {
        for (const MCWebSocket::Message& message : socket.Ws->Take())
        {
            HearGame(game, socket, message, heard);
        }

        Tend(socket, game, now, heard);
        MCServicePingIfDue(*socket.Ws, socket.LastPing, now);
    }

    return heard;
}

void MCServiceSignaling::TellGone(std::string_view game, std::vector<Heard>& heard)
{
    if (game.empty())
    {
        // The guests can't be reached through the service now: as far as the links it carries can tell, they're gone.
        for (const std::string& guest : std::exchange(_Guests, {}))
        {
            heard.push_back({.Sender = From::Guest, .Peer = guest});
        }
    }
    else
    {
        heard.push_back({.Sender = From::Host, .Peer = std::string(game)});
    }
}

void MCServiceSignaling::Tend(Socket& socket, std::string_view game, Clock::time_point now, std::vector<Heard>& heard)
{
    if (socket.Ws->GetState() != MCWebSocket::State::Closed)
    {
        return;
    }

    const std::string what =
        game.empty() ? std::string("the hosted game's socket") : std::format("the socket to game \"{}\"", game);

    if (!socket.Told)
    {
        socket.Told = true;
        SDL_Log("MCServiceSignaling: %s closed: %s", what.c_str(), socket.Ws->WhyClosed().c_str());
        TellGone(game, heard);
    }

    if (socket.Refusal.has_value() || socket.TakenOver)
    {
        return;
    }

    if (socket.Token.empty())
    {
        // Never in: there's no place to come back to.
        socket.Refusal = socket.Ws->WhyClosed() == NotACode ? MCServiceRefusal::NoGame : MCServiceRefusal::Unreachable;
        return;
    }

    if (!socket.DroppedAt.has_value())
    {
        socket.DroppedAt = now;
        socket.Tries = 0;
        socket.NextTry = now + _Comeback.Tries.Wait(socket.Tries);
        return;
    }

    if (now - *socket.DroppedAt >= _Comeback.PlaceKept)
    {
        SDL_Log("MCServiceSignaling: %s couldn't come back while its place waited", what.c_str());
        socket.Refusal = MCServiceRefusal::Unreachable;
        return;
    }

    if (now < socket.NextTry)
    {
        return;
    }

    // Back to its place: each try waits up to twice the last, up to the longest wait (MCServiceComeback::Tries).
    socket.Ws = Opened(std::format("{}{}", BackPath, socket.Code), socket.Token);
    socket.In = false;
    socket.Told = false;
    socket.Tries++;
    socket.NextTry = now + _Comeback.Tries.Wait(socket.Tries);
    SDL_Log("MCServiceSignaling: %s coming back to its place (try %d)", what.c_str(), socket.Tries);
}

MCServiceSignaling::Socket& MCServiceSignaling::GameSocket(std::string_view game)
{
    if (const auto found = _Games.find(game); found != _Games.end())
    {
        return found->second;
    }

    // A name that isn't a code is no game on the service: a socket that's closed at once says so.
    Socket socket{.Ws = IsCode(game) ? Opened(std::format("{}{}", JoinPath, game))
                                     : std::make_unique<MCClosedWebSocket>(std::string(NotACode)),
                  .LastPing = Clock::now()};
    return _Games.emplace(std::string(game), std::move(socket)).first->second;
}

std::unique_ptr<MCWebSocket> MCServiceSignaling::Opened(std::string_view path, std::string_view token)
{
    std::string url = std::format("{}{}?proto={}&copy={}&v={}", _Service, path, ProtocolVersion,
                                  Percent(_Identity.Copy), Percent(_Identity.Version));

    if (!token.empty())
    {
        url += std::format("&token={}", Percent(token));
    }

    std::unique_ptr<MCWebSocket> ws = _Open(url);
    return ws != nullptr ? std::move(ws) : std::make_unique<MCClosedWebSocket>("the system couldn't open the socket");
}

void MCServicePingIfDue(MCWebSocket& ws, std::chrono::steady_clock::time_point& lastPing,
                        std::chrono::steady_clock::time_point now)
{
    // The service answers it itself: an idle socket through a router or proxy stays open.
    if (ws.GetState() == MCWebSocket::State::Open && now - lastPing >= MCServicePingEvery)
    {
        ws.SendText(MCServicePing);
        lastPing = now;
    }
}

void MCServiceSignaling::HearHost(const MCWebSocket::Message& message, std::vector<Heard>& heard)
{
    if (message.Binary)
    {
        // A guest's link's bytes: [guest][link][bytes].
        if (message.Data.size() <= 2 * Uint32Bytes)
        {
            SDL_Log("MCServiceSignaling: the service sent the hosted game %zu bytes, too few for a link's",
                    message.Data.size());
            return;
        }

        const std::string_view data(message.Data);
        const std::string_view bytes = data.substr(2 * Uint32Bytes);
        std::string guest = std::format("{}{}", GuestPrefix, U32(data, 0));
        _Guests.insert(guest);
        heard.push_back({.Sender = From::Guest,
                         .Peer = std::move(guest),
                         .Link = U32(data, Uint32Bytes),
                         .Bytes = {bytes.begin(), bytes.end()}});
        return;
    }

    if (message.Data == PongText)
    {
        return;
    }

    const Json parsed = Json::parse(message.Data, nullptr, false);
    const std::optional<std::string> type = parsed.is_object() ? TextField(parsed, "t", MaxSignalKind) : std::nullopt;

    if (type == "hosted" && !_Host->In)
    {
        const std::optional<std::string> code = TextField(parsed, "game", CodeLength);
        const std::optional<std::string> token = TextField(parsed, "token", TokenLength);

        if (code && IsCode(*code) && token && IsToken(*token) && IceIn(parsed))
        {
            _Host->Code = *code;
            _Host->Token = *token;
            _Host->In = true;
            _Host->Hosted = true;
            SDL_Log("MCServiceSignaling: hosting game \"%s\"", code->c_str());
            SendRoom();
            return;
        }
    }
    else if (type == "back" && !_Host->In && !_Host->Token.empty())
    {
        _Host->In = true;
        _Host->Hosted = true;
        _Host->DroppedAt.reset();
        _Host->Tries = 0;
        SDL_Log("MCServiceSignaling: the hosted game's socket is back in its place");
        SendRoom();
        return;
    }
    else if (type == "guest")
    {
        const std::optional<std::string> guest = TextField(parsed, "guest", MaxSignalKind);
        const std::optional<uint32_t> number = guest ? GuestNumber(*guest) : std::nullopt;
        std::optional<MCIceLink::Settings> ice = IceIn(parsed);

        if (number && ice)
        {
            _GuestSettings[*number] = std::move(*ice);

            // The earliest guests' go first: they're long gone by now.
            while (_GuestSettings.size() > MaxGuestSettings)
            {
                _GuestSettings.erase(_GuestSettings.begin());
            }

            return;
        }
    }
    else if (type == "signal")
    {
        const std::optional<std::string> guest = TextField(parsed, "guest", MaxSignalKind);
        const std::optional<uint32_t> link = Uint32Field(parsed, "link");
        std::optional<MCIceLink::Signal> signal = SignalIn(parsed);

        if (guest && GuestNumber(*guest) && link && signal)
        {
            _Guests.insert(*guest);
            heard.push_back({.Sender = From::Guest, .Peer = *guest, .Link = *link, .Signal = std::move(signal)});
            return;
        }
    }
    else if (type == "left")
    {
        const std::optional<std::string> guest = TextField(parsed, "guest", MaxSignalKind);

        if (guest && GuestNumber(*guest))
        {
            _Guests.erase(*guest);
            heard.push_back({.Sender = From::Guest, .Peer = *guest});
            return;
        }
    }
    else if (type == "name")
    {
        SDL_Log("MCServiceSignaling: the service won't list the room by its name");
        _NameRefused = true;
        return;
    }
    else if (const std::optional<MCServiceRefusal> refusal = type ? RefusalNamed(*type) : std::nullopt)
    {
        SDL_Log("MCServiceSignaling: the service refused the hosted game: %s", type->c_str());
        _Host->Refusal = refusal;

        if (!_Host->Told)
        {
            _Host->Told = true;
            TellGone({}, heard);
        }

        return;
    }

    SDL_Log("MCServiceSignaling: the hosted game's socket got a message it doesn't take (%zu bytes)",
            message.Data.size());
}

void MCServiceSignaling::HearGame(std::string_view game, Socket& socket, const MCWebSocket::Message& message,
                                  std::vector<Heard>& heard)
{
    if (message.Binary)
    {
        // The host's bytes on a link the service carries: [link][bytes].
        if (message.Data.size() <= Uint32Bytes)
        {
            SDL_Log("MCServiceSignaling: game \"%.*s\"'s socket got %zu bytes, too few for a link's",
                    static_cast<int>(game.size()), game.data(), message.Data.size());
            return;
        }

        const std::string_view data(message.Data);
        const std::string_view bytes = data.substr(Uint32Bytes);
        heard.push_back({.Sender = From::Host,
                         .Peer = std::string(game),
                         .Link = U32(data, 0),
                         .Bytes = {bytes.begin(), bytes.end()}});
        return;
    }

    if (message.Data == PongText)
    {
        return;
    }

    const Json parsed = Json::parse(message.Data, nullptr, false);
    const std::optional<std::string> type = parsed.is_object() ? TextField(parsed, "t", MaxSignalKind) : std::nullopt;

    if (type == "joined" && !socket.In)
    {
        const std::optional<std::string> token = TextField(parsed, "token", TokenLength);
        std::optional<MCIceLink::Settings> ice = IceIn(parsed);

        if (token && IsToken(*token) && ice)
        {
            socket.Code = std::string(game);
            socket.Token = *token;
            socket.Settings = std::move(*ice);
            // A game the service doesn't say is private is public: its links take relays only.
            const auto isPublic = parsed.find("public");
            socket.Public = isPublic == parsed.end() || !isPublic->is_boolean() || isPublic->get<bool>();
            socket.In = true;
            SDL_Log("MCServiceSignaling: joined game \"%.*s\" (%zu relays, %s)", static_cast<int>(game.size()),
                    game.data(), socket.Settings->Relays.size(), socket.Public ? "public" : "private");
            return;
        }
    }
    else if (type == "back" && !socket.In && !socket.Token.empty())
    {
        socket.In = true;
        socket.DroppedAt.reset();
        socket.Tries = 0;
        SDL_Log("MCServiceSignaling: the socket to game \"%.*s\" is back in its place", static_cast<int>(game.size()),
                game.data());
        return;
    }
    else if (type == "signal")
    {
        const std::optional<uint32_t> link = Uint32Field(parsed, "link");
        std::optional<MCIceLink::Signal> signal = SignalIn(parsed);

        if (link && signal)
        {
            heard.push_back(
                {.Sender = From::Host, .Peer = std::string(game), .Link = *link, .Signal = std::move(signal)});
            return;
        }
    }
    else if (type == "host" && socket.In)
    {
        // Port-only (Redux HD): another guest took the game over as its host (SPEC-host-rollover.md): links to the old
        // one are gone, and a new one reaches the new host.
        SDL_Log("MCServiceSignaling: game \"%.*s\" has a new host", static_cast<int>(game.size()), game.data());
        socket.HostAway = false;
        socket.HostMoves++;
        return;
    }
    else if (type == "away" && socket.In)
    {
        // The host's socket dropped: the links the service carried are gone, the others go on.
        SDL_Log("MCServiceSignaling: game \"%.*s\"'s host is away from the service", static_cast<int>(game.size()),
                game.data());
        socket.HostAway = true;
        TellGone(game, heard);
        return;
    }
    else if (const std::optional<MCServiceRefusal> refusal = type ? RefusalNamed(*type) : std::nullopt)
    {
        SDL_Log("MCServiceSignaling: game \"%.*s\": the service said %s", static_cast<int>(game.size()), game.data(),
                type->c_str());
        socket.Refusal = refusal;

        if (!socket.Told)
        {
            socket.Told = true;
            TellGone(game, heard);
        }

        return;
    }

    SDL_Log("MCServiceSignaling: game \"%.*s\"'s socket got a message it doesn't take (%zu bytes)",
            static_cast<int>(game.size()), game.data(), message.Data.size());
}

std::string MCServiceName(std::string_view text, size_t most, std::string_view fallback)
{
    std::string name;

    for (const char& c : text)
    {
        for (const char plain : PlainOf(c))
        {
            // Runs of spaces as one, none leading.
            if (plain != ' ' || (!name.empty() && name.back() != ' '))
            {
                name.push_back(plain);
            }
        }
    }

    name.resize(std::min(name.size(), most));

    while (!name.empty() && name.back() == ' ')
    {
        name.pop_back();
    }

    return name.empty() ? std::string(fallback) : name;
}

bool MCIsGameCode(std::string_view text)
{
    return IsCode(text);
}

std::optional<MCServiceRefusal> MCServiceRefusalNamed(std::string_view type)
{
    return RefusalNamed(type);
}
