#include "stdafx.h"
#include "platform/MCServiceSignaling.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <format>
#include <limits>

namespace
{
    using Json = nlohmann::json;

    /// <summary>A game's code: four characters with no lookalikes (no 0 and O, 1 and I), as the service makes them.</summary>
    constexpr std::string_view CodeAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    constexpr size_t CodeLength = 4;
    /// <summary>A guest as the service names it: "g" and its number.</summary>
    constexpr char GuestPrefix = 'g';
    constexpr size_t Uint32Bytes = 4;
    /// <summary>A signal's parts, as the service passes them (and a short field's, a kind's).</summary>
    constexpr size_t MaxSignalText = MCIceLink::Signal::MostText;
    constexpr size_t MaxSignalKind = MCIceLink::Signal::MostKind;
    constexpr size_t MaxMid = MCIceLink::Signal::MostMid;
    /// <summary>The service's STUN and relays: a handful of each, their names and credentials short.</summary>
    constexpr size_t MaxIceServers = 8;
    constexpr size_t MaxIceText = 512;
    constexpr uint16_t LowestPort = 1;
    /// <summary>STUN before the service answers (and the service's own), as host:port.</summary>
    constexpr std::string_view DefaultStun = "stun.cloudflare.com:3478";
    /// <summary>The service's keep-alive, which it answers itself: an idle socket through a router or proxy stays open.</summary>
    constexpr std::string_view PingText = "ping";
    constexpr std::string_view PongText = "pong";
    constexpr auto PingEvery = std::chrono::seconds(30);
    constexpr std::string_view HostPath = "/host";
    constexpr std::string_view JoinPath = "/join/";

    bool IsCode(std::string_view text)
    {
        return text.size() == CodeLength &&
               std::ranges::all_of(text, [](char c) { return CodeAlphabet.find(c) != std::string_view::npos; });
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

    /// <summary>The service's STUN servers and relays (its hosted or joined: "ice"), or none when they don't parse.</summary>
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

    private:
        std::string _Why;
    };
}

MCServiceSignaling::MCServiceSignaling(std::string service, MCWebSocketOpener open, bool relayOnly)
    : _Service(std::move(service)), _Open(std::move(open))
{
    _Settings.Stun.emplace_back(DefaultStun);
    _Settings.RelayOnly = relayOnly;
}

MCServiceSignaling::~MCServiceSignaling()
{
    const std::scoped_lock lock(_Lock);

    if (_Host.has_value())
    {
        _Host->Ws->Close();
    }

    for (auto& [game, socket] : _Games)
    {
        socket.Ws->Close();
    }
}

void MCServiceSignaling::Host()
{
    const std::scoped_lock lock(_Lock);

    if (!_Host.has_value() || _Host->Ws->GetState() == MCWebSocket::State::Closed)
    {
        _HostedCode.reset();
        _Host = Socket{.Ws = Opened(_Service + std::string(HostPath)), .LastPing = std::chrono::steady_clock::now()};
    }
}

std::optional<std::string> MCServiceSignaling::HostedCode()
{
    const std::scoped_lock lock(_Lock);
    return _HostedCode;
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

    if (const auto found = _Games.find(game);
        found != _Games.end() && found->second.Ws->GetState() == MCWebSocket::State::Closed)
    {
        _Games.erase(found);
    }

    GameSocket(game);
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

MCIceLink::Settings MCServiceSignaling::LinkSettings()
{
    const std::scoped_lock lock(_Lock);
    return _Settings;
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
    const auto now = std::chrono::steady_clock::now();
    std::vector<Heard> heard;

    if (_Host.has_value())
    {
        for (const MCWebSocket::Message& message : _Host->Ws->Take())
        {
            HearHost(message, heard);
        }

        // The guests can't be reached through the service now: as far as the links it carries can tell, they're gone.
        if (_Host->Ws->GetState() == MCWebSocket::State::Closed && !_Host->Told)
        {
            _Host->Told = true;
            SDL_Log("MCServiceSignaling: the hosted game's socket closed: %s", _Host->Ws->WhyClosed().c_str());

            for (const std::string& guest : std::exchange(_Guests, {}))
            {
                heard.push_back({.Sender = From::Guest, .Peer = guest});
            }
        }

        Ping(*_Host, now);
    }

    for (auto& [game, socket] : _Games)
    {
        for (const MCWebSocket::Message& message : socket.Ws->Take())
        {
            HearGame(game, socket, message, heard);
        }

        // Its socket gone, the game is too, as far as this game's links can tell.
        if (socket.Ws->GetState() == MCWebSocket::State::Closed && !socket.Told)
        {
            socket.Told = true;
            SDL_Log("MCServiceSignaling: the socket to game \"%s\" closed: %s", game.c_str(),
                    socket.Ws->WhyClosed().c_str());
            heard.push_back({.Sender = From::Host, .Peer = game});
        }

        Ping(socket, now);
    }

    return heard;
}

MCServiceSignaling::Socket& MCServiceSignaling::GameSocket(std::string_view game)
{
    if (const auto found = _Games.find(game); found != _Games.end())
    {
        return found->second;
    }

    // A name that isn't a code is no game on the service: a socket that's closed at once says so.
    Socket socket{.Ws = IsCode(game) ? Opened(_Service + std::string(JoinPath) + std::string(game))
                                     : std::make_unique<MCClosedWebSocket>("not a game's code"),
                  .LastPing = std::chrono::steady_clock::now()};
    return _Games.emplace(std::string(game), std::move(socket)).first->second;
}

std::unique_ptr<MCWebSocket> MCServiceSignaling::Opened(const std::string& url)
{
    std::unique_ptr<MCWebSocket> ws = _Open(url);
    return ws != nullptr ? std::move(ws) : std::make_unique<MCClosedWebSocket>("the system couldn't open the socket");
}

void MCServiceSignaling::Ping(Socket& socket, std::chrono::steady_clock::time_point now)
{
    if (socket.Ws->GetState() == MCWebSocket::State::Open && now - socket.LastPing >= PingEvery)
    {
        socket.Ws->SendText(PingText);
        socket.LastPing = now;
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

    if (type == "hosted")
    {
        const std::optional<std::string> code = TextField(parsed, "game", CodeLength);
        const std::optional<MCIceLink::Settings> ice = IceIn(parsed);

        if (code && IsCode(*code) && ice)
        {
            _HostedCode = *code;
            _Settings.Stun = ice->Stun;
            _Settings.Relays = ice->Relays;
            _Host->In = true;
            SDL_Log("MCServiceSignaling: hosting game \"%s\" (%zu relays)", code->c_str(), ice->Relays.size());
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

    if (type == "joined")
    {
        if (const std::optional<MCIceLink::Settings> ice = IceIn(parsed))
        {
            _Settings.Stun = ice->Stun;
            _Settings.Relays = ice->Relays;
            socket.In = true;
            SDL_Log("MCServiceSignaling: joined game \"%.*s\" (%zu relays)", static_cast<int>(game.size()), game.data(),
                    ice->Relays.size());
            return;
        }
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
    else if (type == "nogame" || type == "full" || type == "gone")
    {
        SDL_Log("MCServiceSignaling: game \"%.*s\": %s", static_cast<int>(game.size()), game.data(),
                type == "nogame" ? "the service has no such game"
                : type == "full" ? "the game is full"
                                 : "its host left");

        if (!socket.Told)
        {
            socket.Told = true;
            heard.push_back({.Sender = From::Host, .Peer = std::string(game)});
        }

        return;
    }

    SDL_Log("MCServiceSignaling: game \"%.*s\"'s socket got a message it doesn't take (%zu bytes)",
            static_cast<int>(game.size()), game.data(), message.Data.size());
}
