#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCLoopbackTransport.h"
#include "network/MCInternetGame.h"
#include "platform/MCNetSwitch.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <thread>

// An internet game on this machine (network/MCInternetGame): its network switched in under the game's sockets
// (platform/MCNetSwitch), a host's game made and a guest's link to it opened by its code, through a stand-in for the
// games service in the process (its protocol as tools/cloudflare/games/README.md in the workspace has it), the links
// real ICE links on the loopback. The real service is checked by its own checks and the trials; the service's
// signals, malformed ones too, by the ServiceSignaling tests.

namespace
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view Service = "wss://games.test";
    constexpr std::string_view Code = "K7QF";
    constexpr std::string_view Token = "0123456789abcdef0123456789abcdef";
    /// <summary>The copy's ID and version the games say: the test's own, so nothing is kept in a user folder.</summary>
    constexpr std::string_view Copy = "a1b2c3d4e5f60718293a4b5c6d7e8f90";
    constexpr std::string_view Version = "0.6.0 (812)";
    constexpr std::string_view PingText = "ping";
    constexpr std::string_view PongText = "pong";
    /// <summary>The game's port on its host (MCDirectPlay's).</summary>
    constexpr uint16_t GamePort = 28800;
    /// <summary>A link on the loopback opens in well under a second; a failing one waits out the open deadline.</summary>
    constexpr auto OpenDeadline = std::chrono::milliseconds(3000);
    constexpr auto StepWait = std::chrono::milliseconds(5);
    constexpr auto Patience = std::chrono::seconds(10);
    constexpr int ConnectMs = 5000;
    constexpr size_t ReadBytes = 64;
    /// <summary>How long a join waits for a service that never answers.</summary>
    constexpr auto SilentServiceWait = std::chrono::milliseconds(200);

    class MCFakeGamesService;

    /// <summary>A socket to the stand-in service: what it sends goes to the service at once, what it hears waits.</summary>
    class MCServiceLine final : public MCWebSocket
    {
    public:
        MCServiceLine(MCFakeGamesService& service, int id) : _Service(service), _Id(id) {}

        void SendText(std::string_view text) override;
        void SendBinary(std::span<const uint8_t>) override {}
        std::vector<Message> Take() override;
        State GetState() const override;
        std::string WhyClosed() const override { return "closed (1000)"; }
        size_t Unsent() const override { return 0; }
        void Close() override;

    private:
        MCFakeGamesService& _Service;
        int _Id;
    };

    /// <summary>
    /// The games service's protocol for one game, in the process: hosting, joining by the code, the signals passed
    /// with each guest's tag, pings, the room kept, a guest's leaving told to its host and a host's to its guests.
    /// No relays (the links are on the loopback) and no WebSocket path.
    /// </summary>
    class MCFakeGamesService
    {
    public:
        /// <summary>What joined says of the game's being public: none, and it says nothing (an older service).</summary>
        std::optional<bool> JoinedPublic = false;

        MCWebSocketOpener Opener()
        {
            return [this](const std::string& url) -> std::unique_ptr<MCWebSocket>
            {
                const std::scoped_lock lock(_Lock);
                const int id = _NextId++;
                Line& line = _Lines[id];
                Open(id, line, std::string_view(url).substr(Service.size()));
                return std::make_unique<MCServiceLine>(*this, id);
            };
        }

        /// <summary>The host's room as it last said it.</summary>
        std::optional<Json> Room()
        {
            const std::scoped_lock lock(_Lock);
            return _Room;
        }

        /// <summary>Sockets open now.</summary>
        size_t OpenLines()
        {
            const std::scoped_lock lock(_Lock);
            return static_cast<size_t>(
                std::ranges::count_if(_Lines, [](const auto& line) { return line.second.Open; }));
        }

        void Heard(int id, std::string_view text)
        {
            const std::scoped_lock lock(_Lock);
            Line& from = _Lines.at(id);

            if (!from.Open)
            {
                return;
            }

            if (text == PingText)
            {
                Say(from, PongText);
                return;
            }

            const Json message = Json::parse(text);

            if (from.Host && message.at("t") == "room")
            {
                _Room = message;
            }
            else if (from.Host && message.at("t") == "signal")
            {
                Json signal = message;
                const std::string guest = signal.at("guest");
                signal.erase("guest");

                for (auto& [other, line] : _Lines)
                {
                    if (line.Open && line.Guest == guest)
                    {
                        Say(line, signal.dump());
                    }
                }
            }
            else if (!from.Host && message.at("t") == "signal")
            {
                Json signal = message;
                signal["guest"] = from.Guest;

                if (Line* const host = HostLine())
                {
                    Say(*host, signal.dump());
                }
            }
        }

        std::vector<MCWebSocket::Message> Take(int id)
        {
            const std::scoped_lock lock(_Lock);
            return std::exchange(_Lines.at(id).Inbox, {});
        }

        MCWebSocket::State StateOf(int id)
        {
            const std::scoped_lock lock(_Lock);
            return _Lines.at(id).Open ? MCWebSocket::State::Open : MCWebSocket::State::Closed;
        }

        void Closed(int id)
        {
            const std::scoped_lock lock(_Lock);
            Line& line = _Lines.at(id);

            if (!line.Open)
            {
                return;
            }

            line.Open = false;

            for (auto& [other, peer] : _Lines)
            {
                if (!peer.Open)
                {
                    continue;
                }

                if (line.Host)
                {
                    Say(peer, R"({"t":"gone"})");
                    peer.Open = false;
                }
                else if (peer.Host)
                {
                    Say(peer, Json{{"t", "left"}, {"guest", line.Guest}}.dump());
                }
            }
        }

    private:
        struct Line
        {
            bool Open = true;
            bool Host = false;
            std::string Guest;
            std::vector<MCWebSocket::Message> Inbox;
        };

        static void Say(Line& line, std::string_view text) { line.Inbox.push_back({false, std::string(text)}); }

        static Json NoRelays() { return Json{{"stun", Json::array()}, {"relays", Json::array()}}; }

        Line* HostLine()
        {
            const auto host =
                std::ranges::find_if(_Lines, [](const auto& line) { return line.second.Open && line.second.Host; });
            return host != _Lines.end() ? &host->second : nullptr;
        }

        void Open(int id, Line& line, std::string_view path)
        {
            if (path.starts_with("/host?"))
            {
                line.Host = true;
                Say(line, Json{{"t", "hosted"}, {"game", Code}, {"token", Token}, {"ice", NoRelays()}}.dump());
                return;
            }

            const std::string join = std::format("/join/{}?", Code);
            Line* const host = HostLine();

            if (!path.starts_with(join) || host == nullptr)
            {
                Say(line, R"({"t":"nogame"})");
                line.Open = false;
                return;
            }

            line.Guest = std::format("g{}", id);
            Say(*host, Json{{"t", "guest"}, {"guest", line.Guest}, {"ice", NoRelays()}}.dump());
            Json joined{{"t", "joined"}, {"game", Code}, {"guest", line.Guest}, {"token", Token}, {"ice", NoRelays()}};

            if (JoinedPublic.has_value())
            {
                joined["public"] = *JoinedPublic;
            }

            Say(line, joined.dump());
        }

        std::mutex _Lock;
        std::map<int, Line> _Lines;
        std::optional<Json> _Room;
        int _NextId = 1;
    };

    void MCServiceLine::SendText(std::string_view text)
    {
        _Service.Heard(_Id, text);
    }

    std::vector<MCWebSocket::Message> MCServiceLine::Take()
    {
        return _Service.Take(_Id);
    }

    MCWebSocket::State MCServiceLine::GetState() const
    {
        return _Service.StateOf(_Id);
    }

    void MCServiceLine::Close()
    {
        _Service.Closed(_Id);
    }

    /// <summary>A socket to a service that never answers.</summary>
    class MCSilentSocket final : public MCWebSocket
    {
    public:
        void SendText(std::string_view) override {}
        void SendBinary(std::span<const uint8_t>) override {}
        std::vector<Message> Take() override { return {}; }
        State GetState() const override { return _Closed ? State::Closed : State::Open; }
        std::string WhyClosed() const override { return "closed (1000)"; }
        size_t Unsent() const override { return 0; }
        void Close() override { _Closed = true; }

    private:
        std::atomic<bool> _Closed = false;
    };

    /// <summary>Internet games through <paramref name="service"/>, their links on the loopback, for the test's time.</summary>
    class MCInternetSetupScope
    {
    public:
        explicit MCInternetSetupScope(MCFakeGamesService& service)
        {
            MCInternetGame::Setup({.Open = service.Opener(),
                                   .Version = std::string(Version),
                                   .Copy = std::string(Copy),
                                   .Service = std::string(Service),
                                   .Deadlines = {.Open = OpenDeadline, .Service = std::nullopt},
                                   .BindAddress = "127.0.0.1"});
        }

        ~MCInternetSetupScope() { MCInternetGame::Setup({}); }
        MCInternetSetupScope(const MCInternetSetupScope&) = delete;
        MCInternetSetupScope& operator=(const MCInternetSetupScope&) = delete;
    };

    /// <summary>Moves both games along until <paramref name="done"/> holds, or the test's patience runs out.</summary>
    template <typename Done> bool Until(MCInternetGame& host, MCInternetGame& guest, Done done)
    {
        const auto deadline = Clock::now() + Patience;

        while (!done() && Clock::now() < deadline)
        {
            host.Update();
            guest.Update();
            std::this_thread::sleep_for(StepWait);
        }

        return done();
    }

    std::span<const uint8_t> Bytes(std::string_view text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }

    /// <summary>What arrives on <paramref name="socket"/> within the test's patience, read whole as text.</summary>
    std::string Read(MCNetTransport& net, MCSocket::Handle socket, size_t wanted)
    {
        std::string got;
        std::array<uint8_t, ReadBytes> buffer{};
        const auto deadline = Clock::now() + Patience;

        while (got.size() < wanted && Clock::now() < deadline)
        {
            const int read = net.Receive(socket, buffer);

            if (read < 0)
            {
                break;
            }

            got.append(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(read));
            std::this_thread::sleep_for(StepWait);
        }

        return got;
    }
}

TEST_CASE("net switch: sockets opened before an internet game stay on the system's, and the game's go as it ends")
{
    auto system = std::make_unique<MCLoopbackTransport>();
    MCLoopbackTransport& systemNet = *system;
    MCNetSwitch net(std::move(system));
    const MCSocket::Handle lan = net.OpenUdp(GamePort);
    REQUIRE(lan != MCSocket::InvalidHandle);
    const MCSocket::Handle lanListener = net.ListenTcp(GamePort);
    REQUIRE(lanListener != MCSocket::InvalidHandle);

    auto internet = std::make_shared<MCLoopbackTransport>();
    net.BeginInternet(internet);
    CHECK(net.InternetOn());
    const MCSocket::Handle game = net.OpenUdp(GamePort);
    REQUIRE(game != MCSocket::InvalidHandle);
    CHECK(game != lan);
    CHECK_EQ(systemNet.OpenSockets(), size_t{2});
    CHECK_EQ(internet->OpenSockets(), size_t{1});

    // A LAN connection taken while the game is on is the system's.
    const MCSocket::Handle caller = systemNet.ConnectTcp({MCSocket::LoopbackIp, GamePort}, ConnectMs);
    REQUIRE(caller != MCSocket::InvalidHandle);
    MCSocket::Address callerAt;
    const MCSocket::Handle taken = net.AcceptTcp(lanListener, callerAt);
    REQUIRE(taken != MCSocket::InvalidHandle);
    REQUIRE_EQ(systemNet.Send(caller, Bytes("lan call")), 8);
    CHECK_EQ(Read(net, taken, 8), "lan call");
    net.Close(taken);
    systemNet.Close(caller);

    // Each socket's datagrams go on its own network: the same port on each, and nothing crosses.
    const MCSocket::Address to{MCSocket::LoopbackIp, GamePort};
    REQUIRE(net.SendTo(game, to, Bytes("internet")));
    REQUIRE(net.SendTo(lan, to, Bytes("lan")));
    std::array<uint8_t, ReadBytes> buffer{};
    MCSocket::Address from;
    CHECK_EQ(net.ReceiveFrom(game, buffer, from), static_cast<int>(std::string_view("internet").size()));
    CHECK_EQ(net.ReceiveFrom(lan, buffer, from), static_cast<int>(std::string_view("lan").size()));
    CHECK_EQ(net.ReceiveFrom(game, buffer, from), 0);

    // The game over: its socket reads as gone and closes nothing of the system's; the LAN's goes on.
    net.EndInternet();
    CHECK(!net.InternetOn());
    CHECK_EQ(net.ReceiveFrom(game, buffer, from), -1);
    CHECK(!net.SendTo(game, to, Bytes("late")));
    net.Close(game);
    CHECK_EQ(systemNet.OpenSockets(), size_t{2});
    REQUIRE(net.SendTo(lan, to, Bytes("still")));
    CHECK_EQ(net.ReceiveFrom(lan, buffer, from), static_cast<int>(std::string_view("still").size()));

    // Ending a network that isn't on ends nothing.
    net.BeginInternet(internet);
    net.EndInternet(MCLoopbackTransport());
    CHECK(net.InternetOn());
    net.EndInternet(*internet);
    CHECK(!net.InternetOn());
    net.Close(lan);
    net.Close(lanListener);
    CHECK_EQ(systemNet.OpenSockets(), size_t{0});
}

TEST_CASE("internet game: a host's game made on the service, a guest's link opened by its code, a connection over it")
{
    MCFakeGamesService service;
    const MCInternetSetupScope setup(service);
    MCNetSwitch hostNet(std::make_unique<MCLoopbackTransport>());
    MCNetSwitch guestNet(std::make_unique<MCLoopbackTransport>());

    std::unique_ptr<MCInternetGame> host = MCInternetGame::Host(hostNet, false);
    REQUIRE(host != nullptr);
    CHECK(hostNet.InternetOn());
    const auto hosted = Clock::now() + Patience;

    while (host->Update() == MCInternetGame::Stage::Service && Clock::now() < hosted)
    {
        std::this_thread::sleep_for(StepWait);
    }

    REQUIRE(host->Update() == MCInternetGame::Stage::Ready);
    CHECK_EQ(host->Code(), std::string(Code));
    host->SetRoom({.Name = "BOB'S GAME", .Public = true, .Max = 4, .Players = {"BOB"}});
    const std::optional<Json> room = service.Room();
    REQUIRE(room.has_value());
    CHECK_EQ(room->at("name"), "BOB'S GAME");
    CHECK_EQ(room->at("public"), false);
    const MCSocket::Handle listener = hostNet.ListenTcp(GamePort);
    REQUIRE(listener != MCSocket::InvalidHandle);

    // The code in either case; the link open before the game's own search or connection asks for it.
    std::unique_ptr<MCInternetGame> guest = MCInternetGame::Join(guestNet, "k7qf");
    REQUIRE(guest != nullptr);
    CHECK_EQ(guest->Code(), std::string(Code));
    REQUIRE(Until(*host, *guest,
                  [&]
                  {
                      return guest->Update() != MCInternetGame::Stage::Service &&
                             guest->Update() != MCInternetGame::Stage::Reaching;
                  }));
    REQUIRE(guest->Update() == MCInternetGame::Stage::Ready);

    // A connection to the code's address, the host taking it on its own thread as a game does.
    std::atomic<MCSocket::Handle> accepted = MCSocket::InvalidHandle;
    std::jthread taker(
        [&](std::stop_token stop)
        {
            while (!stop.stop_requested() && accepted == MCSocket::InvalidHandle)
            {
                MCSocket::Address from;
                accepted = hostNet.AcceptTcp(listener, from);
                std::this_thread::sleep_for(StepWait);
            }
        });
    const std::optional<uint32_t> address = guestNet.Resolve(Code);
    REQUIRE(address.has_value());
    const MCSocket::Handle stream = guestNet.ConnectTcp({*address, GamePort}, ConnectMs);
    taker.request_stop();
    taker.join();
    REQUIRE(stream != MCSocket::InvalidHandle);
    REQUIRE(accepted != MCSocket::InvalidHandle);

    REQUIRE_EQ(guestNet.Send(stream, Bytes("hello")), 5);
    CHECK_EQ(Read(hostNet, accepted, 5), "hello");
    REQUIRE_EQ(hostNet.Send(accepted, Bytes("there")), 5);
    CHECK_EQ(Read(guestNet, stream, 5), "there");

    // The host's game ending: its sockets close as its leaving, the guest told the game is over.
    host->End();
    CHECK(!hostNet.InternetOn());
    CHECK(host->Update() == MCInternetGame::Stage::Failed);
    CHECK(Until(*guest, *guest, [&] { return guest->Update() == MCInternetGame::Stage::Failed; }));
    CHECK(guest->Refusal() == std::optional(MCServiceRefusal::GameOver));
    guest.reset();
    CHECK(!guestNet.InternetOn());
    CHECK_EQ(service.OpenLines(), size_t{0});
}

TEST_CASE("internet game: a guest of a public game, or one the service doesn't say is private, offers only its relays")
{
    // No relays from the service: a guest that offers only its relays has no path at all, so its link can't open.
    for (const std::optional<bool> saidPublic : {std::optional<bool>(true), std::optional<bool>()})
    {
        MCFakeGamesService service;
        service.JoinedPublic = saidPublic;
        const MCInternetSetupScope setup(service);
        MCNetSwitch hostNet(std::make_unique<MCLoopbackTransport>());
        MCNetSwitch guestNet(std::make_unique<MCLoopbackTransport>());
        std::unique_ptr<MCInternetGame> host = MCInternetGame::Host(hostNet, false);
        std::unique_ptr<MCInternetGame> guest;
        REQUIRE(Until(*host, *host, [&] { return host->Update() == MCInternetGame::Stage::Ready; }));
        REQUIRE(hostNet.ListenTcp(GamePort) != MCSocket::InvalidHandle);
        guest = MCInternetGame::Join(guestNet, Code);
        CHECK(Until(*host, *guest, [&] { return guest->Update() == MCInternetGame::Stage::Failed; }));
        CHECK(guest->Refusal() == std::optional(MCServiceRefusal::Unreachable));
    }
}

TEST_CASE("internet game: a code no game has is refused, and a service that never answers fails the join")
{
    MCFakeGamesService service;
    const MCInternetSetupScope setup(service);
    MCNetSwitch net(std::make_unique<MCLoopbackTransport>());
    std::unique_ptr<MCInternetGame> lost = MCInternetGame::Join(net, "ZZZZ");
    REQUIRE(lost != nullptr);
    CHECK(Until(*lost, *lost, [&] { return lost->Update() == MCInternetGame::Stage::Failed; }));
    CHECK(lost->Refusal() == std::optional(MCServiceRefusal::NoGame));

    // A socket that opens and hears nothing: the join gives up once the service's time is up.
    MCInternetGame::Setup({.Open = [](const std::string&) -> std::unique_ptr<MCWebSocket>
                           { return std::make_unique<MCSilentSocket>(); },
                           .Version = std::string(Version),
                           .Copy = std::string(Copy),
                           .ServiceWait = SilentServiceWait});
    std::unique_ptr<MCInternetGame> unanswered = MCInternetGame::Join(net, Code);
    REQUIRE(unanswered != nullptr);
    CHECK(Until(*unanswered, *unanswered, [&] { return unanswered->Update() == MCInternetGame::Stage::Failed; }));
    CHECK(unanswered->Refusal() == std::optional(MCServiceRefusal::Unreachable));
}
