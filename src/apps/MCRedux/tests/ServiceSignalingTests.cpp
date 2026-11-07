#include "stdafx.h"
#include "MCTest.h"
#include "platform/MCServiceSignaling.h"

// The service's signals over its WebSockets (platform/MCServiceSignaling), each socket a scripted one: what the game
// sends the service, and what it makes of what the service sends, the malformed included. The real sockets and the
// trial service are the internet games' trial's (MCIceTrial: --ice-trial).

namespace
{
    constexpr std::string_view Service = "wss://service.test";
    constexpr std::string_view GameCode = "K7QF";
    constexpr std::string_view Guest = "g3";
    constexpr uint32_t GuestNumber = 3;
    constexpr uint32_t LinkNumber = 1;
    /// <summary>Longer than any signal the service passes (32 KB).</summary>
    constexpr size_t TooLongText = 33 * 1024;

    constexpr std::string_view HostedMessage =
        R"({"t":"hosted","game":"K7QF","ice":{"stun":["stun.cloudflare.com:3478"],)"
        R"("relays":[{"host":"turn.cloudflare.com","port":3478,"user":"u","password":"p"}]}})";
    constexpr std::string_view JoinedMessage =
        R"({"t":"joined","game":"K7QF","guest":"g1","ice":{"stun":["stun.cloudflare.com:3478"],)"
        R"("relays":[{"host":"turn.cloudflare.com","port":443,"user":"u2","password":"p2"}]}})";

    /// <summary>A socket the test plays the service's end of.</summary>
    class MCScriptedSocket final : public MCWebSocket
    {
    public:
        void SendText(std::string_view text) override { Sent.push_back({false, std::string(text)}); }
        void SendBinary(std::span<const uint8_t> bytes) override
        {
            Sent.push_back({true, {bytes.begin(), bytes.end()}});
        }

        std::vector<Message> Take() override { return std::exchange(Arrived, {}); }
        State GetState() const override { return Now; }
        std::string WhyClosed() const override { return "closed (1000)"; }
        size_t Unsent() const override { return Waiting; }
        void Close() override { Now = State::Closed; }

        std::vector<Message> Sent;
        std::vector<Message> Arrived;
        State Now = State::Open;
        size_t Waiting = 0;
    };

    /// <summary>Opens scripted sockets, keeping each by its address for the test.</summary>
    struct MCScriptedService
    {
        /// <summary>Each socket opened, by its address; the signaling owns them.</summary>
        std::vector<std::pair<std::string, std::reference_wrapper<MCScriptedSocket>>> Opened;

        MCWebSocketOpener Opener()
        {
            return [this](const std::string& url)
            {
                auto socket = std::make_unique<MCScriptedSocket>();
                Opened.emplace_back(url, std::ref(*socket));
                return socket;
            };
        }

        MCScriptedSocket& At(std::string_view url)
        {
            const auto found = std::ranges::find_if(Opened, [&](const auto& entry) { return entry.first == url; });
            REQUIRE(found != Opened.end());
            return found->second.get();
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
}

TEST_CASE("service signaling: a host's game and its code, its guests' signals and bytes, and a guest leaving")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), true);
    signaling.Host();
    MCScriptedSocket& host = service.At("wss://service.test/host");
    CHECK(!signaling.HostedCode().has_value());

    // The service's answer: the game's code, and the relays its links start with.
    host.Arrived.push_back(Text(HostedMessage));
    CHECK(signaling.Take().empty());
    CHECK(signaling.HostedCode() == std::optional<std::string>(GameCode));
    const MCIceLink::Settings settings = signaling.LinkSettings();
    CHECK(settings.RelayOnly);
    CHECK(settings.Stun == std::vector<std::string>{"stun.cloudflare.com:3478"});
    REQUIRE_EQ(settings.Relays.size(), size_t{1});
    CHECK_EQ(settings.Relays[0].Port, uint16_t{3478});
    CHECK_EQ(settings.Relays[0].User, std::string("u"));

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
}

TEST_CASE("service signaling: a guest joins by the code, and what isn't the service's protocol is left alone")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), false);
    signaling.Join(GameCode);
    MCScriptedSocket& game = service.At("wss://service.test/join/K7QF");
    CHECK(!signaling.Joined(GameCode));
    game.Arrived.push_back(Text(JoinedMessage));
    CHECK(signaling.Take().empty());
    CHECK(signaling.Joined(GameCode));
    CHECK(!signaling.LinkSettings().RelayOnly);
    REQUIRE_EQ(signaling.LinkSettings().Relays.size(), size_t{1});
    CHECK_EQ(signaling.LinkSettings().Relays[0].Port, uint16_t{443});

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
          Text(R"({"t":"joined","ice":{"stun":[],"relays":[{"host":"x","port":0,"user":"u","password":"p"}]}})"),
          Text(R"({"t":"joined","ice":{"stun":[7],"relays":[]}})"), Text(R"({"t":"other"})"), Binary({}, "abc"),
          Binary({LinkNumber}, "")})
    {
        game.Arrived.push_back(message);
    }

    CHECK(signaling.Take().empty());
    CHECK_EQ(signaling.LinkSettings().Relays.size(), size_t{1});

    // A name that isn't a code is no game, said once; the service's "gone" says the game ended, once, however its
    // socket closes after.
    signaling.ToHost("not a code", LinkNumber, Offer);
    heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK_EQ(heard[0].Peer, std::string("not a code"));
    CHECK(!heard[0].Signal.has_value());
    CHECK(heard[0].Bytes.empty());
    CHECK(signaling.Take().empty());
    game.Arrived.push_back(Text(R"({"t":"gone"})"));
    game.Now = MCWebSocket::State::Closed;
    heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK_EQ(heard[0].Peer, std::string(GameCode));
    CHECK(!heard[0].Signal.has_value());
    CHECK(signaling.Take().empty());
    CHECK(signaling.WhyClosed(GameCode) == std::optional<std::string>("closed (1000)"));
}

TEST_CASE("service signaling: a socket the service closes says its game is gone, once")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), false);
    signaling.Join(GameCode);
    MCScriptedSocket& game = service.At("wss://service.test/join/K7QF");
    game.Arrived.push_back(Text(JoinedMessage));
    CHECK(signaling.Take().empty());
    game.Now = MCWebSocket::State::Closed;
    const std::vector<MCIceSignaling::Heard> heard = signaling.Take();
    REQUIRE_EQ(heard.size(), size_t{1});
    CHECK(heard[0].Sender == MCIceSignaling::From::Host);
    CHECK_EQ(heard[0].Peer, std::string(GameCode));
    CHECK(signaling.Take().empty());

    // Joining again opens a new socket.
    signaling.Join(GameCode);
    CHECK_EQ(service.Opened.size(), size_t{2});
}

TEST_CASE("service signaling: the hosted game's socket closing says each guest heard from is gone, and unsent adds up")
{
    MCScriptedService service;
    MCServiceSignaling signaling(std::string(Service), service.Opener(), false);
    signaling.Host();
    MCScriptedSocket& host = service.At("wss://service.test/host");
    host.Arrived.push_back(Text(HostedMessage));
    host.Arrived.push_back(Text(R"({"t":"signal","guest":"g3","link":1,"kind":"offer","text":"v=0","mid":""})"));
    host.Arrived.push_back(Binary({5, LinkNumber}, "abc"));
    CHECK_EQ(signaling.Take().size(), size_t{2});

    // What the sockets hold unsent, together: the transport's window for the links the service carries.
    signaling.Join(GameCode);
    host.Waiting = 100;
    service.At("wss://service.test/join/K7QF").Waiting = 20;
    CHECK_EQ(signaling.Unsent(), size_t{120});

    // The guests can't be reached through the service now: each is heard as gone, once.
    host.Now = MCWebSocket::State::Closed;
    std::vector<MCIceSignaling::Heard> heard = signaling.Take();
    std::erase_if(heard, [](const MCIceSignaling::Heard& one) { return one.Sender != MCIceSignaling::From::Guest; });
    REQUIRE_EQ(heard.size(), size_t{2});
    CHECK_EQ(heard[0].Peer, std::string(Guest));
    CHECK_EQ(heard[1].Peer, std::string("g5"));
    CHECK(std::ranges::none_of(heard, [](const MCIceSignaling::Heard& one)
                               { return one.Signal.has_value() || !one.Bytes.empty(); }));
    CHECK(std::ranges::none_of(signaling.Take(), [](const MCIceSignaling::Heard& one)
                               { return one.Sender == MCIceSignaling::From::Guest; }));
}
