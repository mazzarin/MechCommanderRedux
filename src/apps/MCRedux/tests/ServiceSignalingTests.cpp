#include "stdafx.h"
#include "MCTest.h"
#include "lib/MCRandomId.h"
#include "network/MCInternetCopy.h"
#include "platform/MCFileSystem.h"
#include "platform/MCServiceSignaling.h"

#include <filesystem>
#include <fstream>

// The service's signals over its WebSockets (platform/MCServiceSignaling), each socket a scripted one: what the game
// sends the service, and what it makes of what the service sends, the malformed included; a dropped socket coming back
// to its place. The real sockets and service are the internet games' trial's (MCIceTrial: --ice-trial), and the
// service's own checks (tools/cloudflare/games in the workspace).

namespace
{
    constexpr std::string_view Service = "wss://service.test";
    constexpr std::string_view GameCode = "K7QF";
    constexpr std::string_view Guest = "g3";
    constexpr uint32_t GuestNumber = 3;
    constexpr uint32_t LinkNumber = 1;
    /// <summary>Longer than any signal the service passes (32 KB).</summary>
    constexpr size_t TooLongText = 33 * 1024;

    constexpr std::string_view Token = "0123456789abcdef0123456789abcdef";
    const MCServiceIdentity Identity{.Copy = "a1b2c3d4e5f60718293a4b5c6d7e8f90", .Version = "0.6.0 (812)"};
    /// <summary>Each socket's address ends with the protocol, the copy and its version (a space and brackets escaped).</summary>
    constexpr std::string_view Query = "?proto=1&copy=a1b2c3d4e5f60718293a4b5c6d7e8f90&v=0.6.0%20%28812%29";
    const std::string HostUrl = std::format("{}/host{}", Service, Query);
    const std::string JoinUrl = std::format("{}/join/K7QF{}", Service, Query);
    const std::string BackUrl = std::format("{}/game/K7QF{}&token={}", Service, Query, Token);

    constexpr std::string_view HostedMessage =
        R"({"t":"hosted","game":"K7QF","token":"0123456789abcdef0123456789abcdef",)"
        R"("ice":{"stun":["stun.cloudflare.com:3478"],"relays":[]}})";
    constexpr std::string_view GuestMessage =
        R"({"t":"guest","guest":"g3","ice":{"stun":["stun.cloudflare.com:3478"],)"
        R"("relays":[{"host":"turn.cloudflare.com","port":3478,"user":"u","password":"p"}]}})";
    constexpr std::string_view JoinedMessage =
        R"({"t":"joined","game":"K7QF","guest":"g1","token":"0123456789abcdef0123456789abcdef",)"
        R"("ice":{"stun":["stun.cloudflare.com:3478"],)"
        R"("relays":[{"host":"turn.cloudflare.com","port":443,"user":"u2","password":"p2"}]}})";
    /// <summary>A dropped socket tries to come back at once, and its place waits long past the test.</summary>
    const MCServiceComeback AtOnce{.PlaceKept = std::chrono::hours(1), .Tries = {}};

    /// <summary>
    /// The service's end of a scripted socket, which the test plays: what the game sent, what arrives for it, its state.
    /// The socket and the test share it, so it outlives a socket the signaling lets go.
    /// </summary>
    struct MCScriptedEnd
    {
        std::vector<MCWebSocket::Message> Sent;
        std::vector<MCWebSocket::Message> Arrived;
        MCWebSocket::State Now = MCWebSocket::State::Open;
        size_t Waiting = 0;
        /// <summary>Closed as a drop (CloseAway), its place kept.</summary>
        bool Away = false;
    };

    /// <summary>A socket whose far end the test plays (<see cref="MCScriptedEnd"/>).</summary>
    class MCScriptedSocket final : public MCWebSocket
    {
    public:
        explicit MCScriptedSocket(std::shared_ptr<MCScriptedEnd> end) : _End(std::move(end)) {}

        void SendText(std::string_view text) override { _End->Sent.push_back({false, std::string(text)}); }
        void SendBinary(std::span<const uint8_t> bytes) override
        {
            _End->Sent.push_back({true, {bytes.begin(), bytes.end()}});
        }

        std::vector<Message> Take() override { return std::exchange(_End->Arrived, {}); }
        State GetState() const override { return _End->Now; }
        std::string WhyClosed() const override { return "closed (1000)"; }
        size_t Unsent() const override { return _End->Waiting; }
        void Close() override { _End->Now = State::Closed; }

        void CloseAway() override
        {
            _End->Now = State::Closed;
            _End->Away = true;
        }

    private:
        std::shared_ptr<MCScriptedEnd> _End;
    };

    /// <summary>Opens scripted sockets, keeping each one's far end by its address for the test.</summary>
    struct MCScriptedService
    {
        /// <summary>Each socket opened, by its address: the signaling owns the sockets, the test shares their ends.</summary>
        std::vector<std::pair<std::string, std::shared_ptr<MCScriptedEnd>>> Opened;

        MCWebSocketOpener Opener()
        {
            return [this](const std::string& url)
            {
                auto end = std::make_shared<MCScriptedEnd>();
                Opened.emplace_back(url, end);
                return std::make_unique<MCScriptedSocket>(std::move(end));
            };
        }

        /// <summary>The far end of the latest socket opened to <paramref name="url"/>.</summary>
        MCScriptedEnd& At(std::string_view url)
        {
            const auto found = std::ranges::find_if(Opened.rbegin(), Opened.rend(),
                                                    [&](const auto& entry) { return entry.first == url; });
            REQUIRE(found != Opened.rend());
            return *found->second;
        }

        size_t OpenedTo(std::string_view url) const
        {
            return static_cast<size_t>(
                std::ranges::count_if(Opened, [&](const auto& entry) { return entry.first == url; }));
        }
    };

    MCWebSocket::Message Text(std::string_view text)
    {
        return {false, std::string(text)};
    }

    MCWebSocket::Message Binary(std::initializer_list<uint32_t> numbers, std::string_view bytes)
    {
        MCWebSocket::Message message{true, {}};

        for (const uint32_t number : numbers)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                message.Data.push_back(static_cast<char>(number >> shift));
            }
        }

        message.Data += bytes;
        return message;
    }

    std::span<const uint8_t> Bytes(std::string_view text)
    {
        return std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    const MCIceLink::Signal Offer{.Kind = "offer", .Text = "v=0", .Mid = ""};

    bool AllGone(const std::vector<MCIceSignaling::Heard>& heard)
    {
        return std::ranges::all_of(heard, [](const MCIceSignaling::Heard& one)
                                   { return !one.Signal.has_value() && one.Bytes.empty(); });
    }
}

TEST_CASE(
    "service signaling: a host's game and its code, the relays for each guest, its signals and bytes, and a leaving")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, true);
    signaling.Host();
    MCScriptedEnd& host = service.At(HostUrl);
    CHECK(!signaling.HostedCode().has_value());

    // The room set before the service answered goes once the game is made.
    const MCServiceRoom room{
        .Name = "BOB'S GAME", .Public = true, .Max = 4, .Players = {"BOB"}, .Mission = "", .Battle = false};
    signaling.SetRoom(room);
    CHECK(host.Sent.empty());
    host.Arrived.push_back(Text(HostedMessage));
    CHECK(signaling.Take().empty());
    CHECK(signaling.HostedCode() == std::optional<std::string>(GameCode));
    REQUIRE_EQ(host.Sent.size(), size_t{1});
    CHECK_EQ(host.Sent[0].Data, std::string(R"({"battle":false,"max":4,"mission":"","name":"BOB'S GAME",)"
                                            R"("players":["BOB"],"public":true,"t":"room"})"));
    signaling.SetRoom(room);
    CHECK_EQ(host.Sent.size(), size_t{1});
    host.Sent.clear();

    // Each guest's links start with the relays the service made for that guest; others with STUN alone.
    host.Arrived.push_back(Text(GuestMessage));
    CHECK(signaling.Take().empty());
    const MCIceLink::Settings settings = signaling.LinkSettings(Guest);
    CHECK(settings.RelayOnly);
    CHECK(settings.Stun == std::vector<std::string>{"stun.cloudflare.com:3478"});
    REQUIRE_EQ(settings.Relays.size(), size_t{1});
    CHECK_EQ(settings.Relays[0].Port, uint16_t{3478});
    CHECK_EQ(settings.Relays[0].User, std::string("u"));
    CHECK(signaling.LinkSettings("g4").Relays.empty());
    CHECK(signaling.LinkSettings("g4").RelayOnly);

    // A guest's signal, then its bytes on a link the service carries, then its leaving.
    host.Arrived.push_back(Text(R"({"t":"signal","guest":"g3","link":1,"kind":"offer","text":"v=0","mid":""})"));
    host.Arrived.push_back(Binary({GuestNumber, LinkNumber}, "abc"));
    host.Arrived.push_back(Text(R"({"t":"left","guest":"g3"})"));
    const std::vector<MCIceSignaling::Heard> heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{3});
    CHECK(heard[0].Sender == MCIceSignaling::From::Guest);
    CHECK_EQ(heard[0].Peer, std::string(Guest));
    CHECK_EQ(heard[0].Link, LinkNumber);
    CHECK(heard[0].Signal == std::optional(Offer));
    CHECK(!heard[1].Signal.has_value());
    CHECK_EQ(std::string(heard[1].Bytes.begin(), heard[1].Bytes.end()), std::string("abc"));
    CHECK_EQ(heard[1].Link, LinkNumber);
    CHECK(!heard[2].Signal.has_value());
    CHECK(heard[2].Bytes.empty());
    CHECK_EQ(heard[2].Peer, std::string(Guest));

    // What the host says goes to that guest, by its name, and its bytes by its number.
    signaling.ToGuest(Guest, LinkNumber, {.Kind = "answer", .Text = "v=0 a", .Mid = "0"});
    signaling.BytesToGuest(Guest, LinkNumber, Bytes("xyz"));
    signaling.BytesToGuest("not a guest", LinkNumber, Bytes("xyz"));
    REQUIRE_EQ(host.Sent.size(), size_t{2});
    CHECK(!host.Sent[0].Binary);
    CHECK_EQ(host.Sent[0].Data,
             std::string(R"({"guest":"g3","kind":"answer","link":1,"mid":"0","t":"signal","text":"v=0 a"})"));
    CHECK(host.Sent[1].Binary);
    CHECK_EQ(host.Sent[1].Data, Binary({GuestNumber, LinkNumber}, "xyz").Data);

    // A name the service won't list, until the room's name changes.
    host.Arrived.push_back(Text(R"({"t":"name"})"));
    CHECK(signaling.Take().empty());
    CHECK(signaling.NameRefused());
    signaling.SetRoom({.Name = "ANOTHER", .Public = true, .Max = 4, .Players = {"BOB"}});
    CHECK(!signaling.NameRefused());
}

TEST_CASE("service signaling: a joined game's links take relays only, unless the service says the game is private")
{
    // Before the service answers, and once it says the game is public, or says nothing of it: relays only.
    for (const std::string_view said : {std::string_view(R"(,"public":true)"), std::string_view()})
    {
        MCScriptedService service;
        MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false);
        signaling.Join(GameCode);
        CHECK(signaling.LinkSettings(GameCode).RelayOnly);
        std::string joined(JoinedMessage);
        joined.insert(joined.size() - 1, said);
        service.At(JoinUrl).Arrived.push_back(Text(joined));
        CHECK(signaling.Take().empty());
        REQUIRE(signaling.Joined(GameCode));
        CHECK(signaling.LinkSettings(GameCode).RelayOnly);
    }

    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false);
    signaling.Join(GameCode);
    std::string joined(JoinedMessage);
    joined.insert(joined.size() - 1, R"(,"public":false)");
    service.At(JoinUrl).Arrived.push_back(Text(joined));
    CHECK(signaling.Take().empty());
    REQUIRE(signaling.Joined(GameCode));
    CHECK(!signaling.LinkSettings(GameCode).RelayOnly);

    // A game that relays every link takes them through relays only, whatever the service says.
    MCServiceSignaling always(std::string(Service), service.Opener(), Identity, true);
    always.Join(GameCode);
    service.At(JoinUrl).Arrived.push_back(Text(joined));
    CHECK(always.Take().empty());
    REQUIRE(always.Joined(GameCode));
    CHECK(always.LinkSettings(GameCode).RelayOnly);
}

TEST_CASE("service signaling: a guest joins by the code, and what isn't the service's protocol is left alone")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false);
    signaling.Join(GameCode);
    MCScriptedEnd& game = service.At(JoinUrl);
    CHECK(!signaling.Joined(GameCode));

    // Nothing without its token is a join.
    game.Arrived.push_back(Text(R"({"t":"joined","game":"K7QF","guest":"g1","ice":{"stun":[],"relays":[]}})"));
    CHECK(signaling.Take().empty());
    CHECK(!signaling.Joined(GameCode));
    game.Arrived.push_back(Text(JoinedMessage));
    CHECK(signaling.Take().empty());
    CHECK(signaling.Joined(GameCode));
    // A game the service doesn't say is private is public: its links take relays only.
    CHECK(signaling.LinkSettings(GameCode).RelayOnly);
    REQUIRE_EQ(signaling.LinkSettings(GameCode).Relays.size(), size_t{1});
    CHECK_EQ(signaling.LinkSettings(GameCode).Relays[0].Port, uint16_t{443});

    // The guest's signal and bytes go to the game's socket, without a guest's name.
    signaling.ToHost(GameCode, LinkNumber, Offer);
    signaling.BytesToHost(GameCode, LinkNumber, Bytes("abc"));
    REQUIRE_EQ(game.Sent.size(), size_t{2});
    CHECK_EQ(game.Sent[0].Data, std::string(R"({"kind":"offer","link":1,"mid":"","t":"signal","text":"v=0"})"));
    CHECK_EQ(game.Sent[1].Data, Binary({LinkNumber}, "abc").Data);

    // The host's bytes come back by the link's number.
    game.Arrived.push_back(Binary({LinkNumber}, "def"));
    std::vector<MCIceSignaling::Heard> heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK(heard[0].Sender == MCIceSignaling::From::Host);
    CHECK_EQ(heard[0].Peer, std::string(GameCode));
    CHECK_EQ(std::string(heard[0].Bytes.begin(), heard[0].Bytes.end()), std::string("def"));

    // Nothing malformed is heard, and none of it changes the relays.
    const std::string tooLong(TooLongText, 'x');

    for (const MCWebSocket::Message& message :
         {Text("not json"), Text("[]"), Text(R"({"t":"signal","link":-1,"kind":"offer","text":"v=0","mid":""})"),
          Text(R"({"t":"signal","link":4294967296,"kind":"offer","text":"v=0","mid":""})"),
          Text(R"({"t":"signal","link":1,"kind":"offer","mid":""})"),
          Text(std::format(R"({{"t":"signal","link":1,"kind":"offer","text":"{}","mid":""}})", tooLong)),
          Text(R"({"t":"joined","token":"0123456789abcdef0123456789abcdef","ice":{"stun":[],"relays":[]}})"),
          Text(R"({"t":"guest","guest":"g2","ice":{"stun":[],"relays":[]}})"), Text(R"({"t":"other"})"),
          Binary({}, "abc"), Binary({LinkNumber}, "")})
    {
        game.Arrived.push_back(message);
    }

    CHECK(signaling.Take().empty());
    CHECK_EQ(signaling.LinkSettings(GameCode).Relays.size(), size_t{1});

    // A name that isn't a code is no game, said once; the service's "gone" says the game ended, once, however its
    // socket closes after.
    signaling.ToHost("not a code", LinkNumber, Offer);
    heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK_EQ(heard[0].Peer, std::string("not a code"));
    CHECK(AllGone(heard));
    CHECK(signaling.Refusal("not a code") == std::optional(MCServiceRefusal::NoGame));
    CHECK(signaling.Take().empty());
    game.Arrived.push_back(Text(R"({"t":"gone"})"));
    game.Now = MCWebSocket::State::Closed;
    heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK_EQ(heard[0].Peer, std::string(GameCode));
    CHECK(AllGone(heard));
    CHECK(signaling.Take().empty());
    CHECK(signaling.Refusal(GameCode) == std::optional(MCServiceRefusal::GameOver));
    CHECK(signaling.WhyClosed(GameCode) == std::optional<std::string>("closed (1000)"));
    CHECK_EQ(service.OpenedTo(BackUrl), size_t{0});
}

TEST_CASE("service signaling: a guest's socket dropping says its game is gone once, then comes back to its place")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false, AtOnce);
    signaling.Join(GameCode);
    service.At(JoinUrl).Arrived.push_back(Text(JoinedMessage));
    CHECK(signaling.Take().empty());

    // The host's socket away: the links through the service are gone, this socket stays.
    service.At(JoinUrl).Arrived.push_back(Text(R"({"t":"away"})"));
    std::vector<MCIceSignaling::Heard> heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK(AllGone(heard));
    CHECK(signaling.Joined(GameCode));

    service.At(JoinUrl).Now = MCWebSocket::State::Closed;
    heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK(heard[0].Sender == MCIceSignaling::From::Host);
    CHECK_EQ(heard[0].Peer, std::string(GameCode));
    CHECK(AllGone(heard));
    CHECK(!signaling.Refusal(GameCode).has_value());

    // Its next try goes back to its place with its token; it's in once the service says so, its relays kept.
    CHECK(signaling.Take().empty());
    MCScriptedEnd& back = service.At(BackUrl);
    CHECK(!signaling.Joined(GameCode));
    back.Arrived.push_back(Text(R"({"t":"back","guest":"g1"})"));
    CHECK(signaling.Take().empty());
    CHECK(signaling.Joined(GameCode));
    CHECK_EQ(signaling.LinkSettings(GameCode).Relays.size(), size_t{1});
    signaling.ToHost(GameCode, LinkNumber, Offer);
    CHECK_EQ(back.Sent.size(), size_t{1});

    // Too late, its place is gone: no more tries, and joining again opens a new socket.
    back.Now = MCWebSocket::State::Closed;
    CHECK_EQ(signaling.Take().size(), size_t{1});
    CHECK(signaling.Take().empty());
    MCScriptedEnd& again = service.At(BackUrl);
    again.Arrived.push_back(Text(R"({"t":"expired"})"));
    again.Now = MCWebSocket::State::Closed;
    CHECK_EQ(signaling.Take().size(), size_t{1});
    CHECK(signaling.Refusal(GameCode) == std::optional(MCServiceRefusal::PlaceGone));
    CHECK(signaling.Take().empty());
    CHECK_EQ(service.OpenedTo(BackUrl), size_t{2});
    signaling.Join(GameCode);
    CHECK_EQ(service.OpenedTo(JoinUrl), size_t{2});
}

TEST_CASE("service signaling: a dropped socket that can't come back while its place waits is given up")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false,
                                 {.PlaceKept = std::chrono::milliseconds(0)});
    signaling.Join(GameCode);
    service.At(JoinUrl).Arrived.push_back(Text(JoinedMessage));
    CHECK(signaling.Take().empty());
    service.At(JoinUrl).Now = MCWebSocket::State::Closed;
    CHECK_EQ(signaling.Take().size(), size_t{1});
    CHECK(signaling.Take().empty());
    CHECK(signaling.Refusal(GameCode) == std::optional(MCServiceRefusal::Unreachable));
    CHECK_EQ(service.OpenedTo(BackUrl), size_t{0});
}

TEST_CASE("service signaling: the service's refusals, each its reason, none tried again")
{
    const std::array<std::pair<std::string_view, MCServiceRefusal>, 7> refusals{{
        {"nogame", MCServiceRefusal::NoGame},
        {"hostaway", MCServiceRefusal::HostAway},
        {"battle", MCServiceRefusal::InBattle},
        {"full", MCServiceRefusal::Full},
        {"banned", MCServiceRefusal::Banned},
        {"slow", MCServiceRefusal::TooManyTries},
        {"update", MCServiceRefusal::Update},
    }};

    for (const auto& [type, refusal] : refusals)
    {
        MCScriptedService service;
        MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false, AtOnce);
        signaling.Join(GameCode);
        service.At(JoinUrl).Arrived.push_back(Text(std::format(R"({{"t":"{}"}})", type)));
        service.At(JoinUrl).Now = MCWebSocket::State::Closed;
        const std::vector<MCIceSignaling::Heard> heard = signaling.Take();
        REQUIRE_EQ(heard.size(), size_t{1});
        CHECK(AllGone(heard));
        CHECK(signaling.Take().empty());

        if (signaling.Refusal(GameCode) != std::optional(refusal))
        {
            FAIL_CHECK(std::format("the service's \"{}\" isn't its refusal", type));
        }

        CHECK_EQ(service.Opened.size(), size_t{1});
    }

    // A socket that never got in has no place to come back to.
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false, AtOnce);
    signaling.Host();
    service.At(HostUrl).Now = MCWebSocket::State::Closed;
    CHECK(signaling.Take().empty());
    CHECK(signaling.Refusal() == std::optional(MCServiceRefusal::Unreachable));
    CHECK(signaling.Take().empty());
    CHECK_EQ(service.Opened.size(), size_t{1});
    signaling.Host();
    CHECK_EQ(service.Opened.size(), size_t{2});
}

TEST_CASE(
    "service signaling: the hosted game's socket dropping says each guest is gone, comes back, and says its room again")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, false, AtOnce);
    signaling.Host();
    MCScriptedEnd& host = service.At(HostUrl);
    host.Arrived.push_back(Text(HostedMessage));
    host.Arrived.push_back(Text(GuestMessage));
    host.Arrived.push_back(Text(R"({"t":"signal","guest":"g3","link":1,"kind":"offer","text":"v=0","mid":""})"));
    host.Arrived.push_back(Binary({5, LinkNumber}, "abc"));
    CHECK_EQ(signaling.Take().size(), size_t{2});
    const MCServiceRoom room{.Name = "BOB'S GAME", .Public = false, .Max = 2, .Players = {"BOB", "ANN"}};
    signaling.SetRoom(room);
    REQUIRE_EQ(host.Sent.size(), size_t{1});
    const std::string roomSaid = host.Sent[0].Data;

    // What the sockets hold unsent, together: the transport's window for the links the service carries.
    signaling.Join("X2Y3");
    host.Waiting = 100;
    service.At(std::format("{}/join/X2Y3{}", Service, Query)).Waiting = 20;
    CHECK_EQ(signaling.Unsent(), size_t{120});

    // The guests can't be reached through the service now: each is heard as gone, once. (The signaling's next try
    // opens the host's socket again, and the old one goes.)
    host.Now = MCWebSocket::State::Closed;
    std::vector<MCIceSignaling::Heard> heard = signaling.Take();
    std::erase_if(heard, [](const MCIceSignaling::Heard& one) { return one.Sender != MCIceSignaling::From::Guest; });
    REQUIRE_EQ(heard.size(), size_t{2});
    CHECK_EQ(heard[0].Peer, std::string(Guest));
    CHECK_EQ(heard[1].Peer, std::string("g5"));
    CHECK(AllGone(heard));
    CHECK(std::ranges::none_of(signaling.Take(), [](const MCIceSignaling::Heard& one)
                               { return one.Sender == MCIceSignaling::From::Guest; }));

    // Back in its place: the same code, the guests' relays kept, the room said again.
    MCScriptedEnd& back = service.At(BackUrl);
    back.Arrived.push_back(Text(R"({"t":"back"})"));
    CHECK(signaling.Take().empty());
    CHECK(signaling.HostedCode() == std::optional<std::string>(GameCode));
    CHECK_EQ(signaling.LinkSettings(Guest).Relays.size(), size_t{1});
    REQUIRE_EQ(back.Sent.size(), size_t{1});
    CHECK_EQ(back.Sent[0].Data, roomSaid);
}

TEST_CASE("service signaling: a host restarting leaves its place as a drop, and comes back to it with its token")
{
    // Hosting: the game's place, its code and token, to come back to.
    {
        MCScriptedService service;
        MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, true);
        signaling.Host();
        MCScriptedEnd& host = service.At(HostUrl);
        CHECK(!signaling.HostedToken().has_value());
        host.Arrived.push_back(Text(HostedMessage));
        CHECK(signaling.Take().empty());
        CHECK(signaling.HostedToken() == std::optional<std::string>(Token));

        // Leaving away: its socket closes as a drop, which the service keeps the place for.
        signaling.LeaveAway();
        CHECK(host.Away);
        CHECK(signaling.AllClosed());
    }

    // The new version comes back to the place: the same code once the service takes it back, and its room said again.
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, true, AtOnce);
    signaling.HostAgain(GameCode, Token);
    CHECK_EQ(service.OpenedTo(HostUrl), size_t{0});
    MCScriptedEnd& back = service.At(BackUrl);
    signaling.SetRoom({.Name = "BOB'S GAME", .Public = true, .Max = 4, .Players = {"BOB"}});
    CHECK(!signaling.HostedCode().has_value());
    CHECK(back.Sent.empty());
    back.Arrived.push_back(Text(R"({"t":"back"})"));
    CHECK(signaling.Take().empty());
    CHECK(signaling.HostedCode() == std::optional<std::string>(GameCode));
    CHECK(signaling.HostedToken() == std::optional<std::string>(Token));
    REQUIRE_EQ(back.Sent.size(), size_t{1});
    CHECK(back.Sent[0].Data.contains(R"("t":"room")"));

    // A place gone (past its 5 minutes, or the game over) is refused: no other game is made in its stead.
    MCServiceSignaling late(std::string(Service), service.Opener(), Identity, true, AtOnce);
    late.HostAgain(GameCode, Token);
    MCScriptedEnd& expired = service.At(BackUrl);
    expired.Arrived.push_back(Text(R"({"t":"expired"})"));
    expired.Now = MCWebSocket::State::Closed;
    late.Take();
    CHECK(late.Refusal() == std::optional<MCServiceRefusal>(MCServiceRefusal::PlaceGone));
    CHECK(!late.HostedCode().has_value());
    CHECK_EQ(service.OpenedTo(HostUrl), size_t{0});
}

TEST_CASE("service signaling: a room's names go as the service takes them, plain ASCII within its lengths")
{
    // The game's Windows-1252: accented letters as plain ones, what has none left out, spaces run together and trimmed,
    // cut to the service's length; nothing left, the fallback.
    CHECK_EQ(MCServiceName("Pil\xf3to", MCServicePlayerNameMost, "PLAYER"), std::string("Piloto"));
    CHECK_EQ(MCServiceName("\xc6sir \xdf\x9c \xbfy\xbd?", 24, {}), std::string("AEsir ssoe y?"));
    CHECK_EQ(MCServiceName("  two   spaces\t ", 24, {}), std::string("two spaces"));
    CHECK_EQ(MCServiceName("\xa0\xa0"
                           "Edge\xa0",
                           24, {}),
             std::string("Edge"));
    CHECK_EQ(MCServiceName("ABCDEFGHIJKLMNO PQRSTUVWX", MCServicePlayerNameMost, {}), std::string("ABCDEFGHIJKLMNO"));
    CHECK_EQ(MCServiceName("\xd7\xf7", 24, "GAME"), std::string("x"));
    CHECK_EQ(MCServiceName("\xf7\x81", 24, "GAME"), std::string("GAME"));
    CHECK_EQ(MCServiceName("", MCServiceMissionMost, {}), std::string());

    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), Identity, true);
    signaling.Host();
    MCScriptedEnd& host = service.At(HostUrl);
    host.Arrived.push_back(Text(HostedMessage));
    CHECK(signaling.Take().empty());
    signaling.SetRoom({.Name = "  ",
                       .Public = true,
                       .Max = 4,
                       .Players = {"Ren\xe9"
                                   "e",
                                   "\xbf\xbf"},
                       .Mission = "Escape from  \xc9lan"});
    REQUIRE_EQ(host.Sent.size(), size_t{1});
    CHECK_EQ(host.Sent[0].Data, std::string(R"({"battle":false,"max":4,"mission":"Escape from Elan","name":"GAME",)"
                                            R"("players":["Renee","PLAYER"],"public":true,"t":"room"})"));
}

TEST_CASE("internet copy: an ID made once and kept in internet.cfg, and one that isn't an ID made again")
{
    const std::filesystem::path file = MCFileSystem::UserRoot() / "internet.cfg";
    std::error_code error;
    std::filesystem::remove(file, error);

    const std::string id = MCInternetCopyId();
    CHECK(MCIsRandomId(id));
    CHECK(std::filesystem::exists(file));
    CHECK_EQ(MCInternetCopyId(), id);

    std::ofstream(file, std::ios::binary | std::ios::trunc) << "not the game's file";
    const std::string again = MCInternetCopyId();
    CHECK(MCIsRandomId(again));
    CHECK(again != id);
    CHECK_EQ(MCInternetCopyId(), again);
    std::filesystem::remove(file, error);
}
