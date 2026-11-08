#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCFakeGamesService.h"
#include "fakes/MCLoopbackTransport.h"
#include "network/MCInternetGame.h"
#include "platform/MCNetSwitch.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <thread>

// An internet game on this machine (network/MCInternetGame): its network switched in under the game's sockets
// (platform/MCNetSwitch), a host's game made and a guest's link to it opened by its code, through a stand-in for the
// games service in the process (tests/fakes/MCFakeGamesService.h), the links real ICE links on the loopback. The real
// service is checked by its own checks and the trials; the service's signals, malformed ones too, by the
// ServiceSignaling tests.

namespace
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    using MCFakeGames::MCFakeGamesService;
    constexpr std::string_view Service = MCFakeGames::ServiceUrl;
    constexpr std::string_view Code = MCFakeGames::GameCode;
    /// <summary>The copy's ID and version the games say: the test's own, so nothing is kept in a user folder.</summary>
    constexpr std::string_view Copy = "a1b2c3d4e5f60718293a4b5c6d7e8f90";
    constexpr std::string_view Version = "0.6.0 (812)";
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
        void CloseAway() override { _Closed = true; }

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

/// <summary>
/// A join whose host's socket goes away before the guest's link opens (the host's game restarting into an update) fails
/// at once, as the service's own answer to a join then: the service never says when the host is back, so the guest
/// tries again rather than wait out its link's open deadline. Seen live (7 Nov): the service's joined and away came
/// together, before the guest's link was made.
/// </summary>
TEST_CASE("internet game: a join whose host goes away before its link opens fails at once, to be tried again")
{
    MCFakeGamesService service;
    const MCInternetSetupScope setup(service);
    MCNetSwitch hostNet(std::make_unique<MCLoopbackTransport>());
    MCNetSwitch guestNet(std::make_unique<MCLoopbackTransport>());
    std::unique_ptr<MCInternetGame> host = MCInternetGame::Host(hostNet, false);
    REQUIRE(Until(*host, *host, [&] { return host->Update() == MCInternetGame::Stage::Ready; }));

    // The service takes the guest in, then the host's game quits keeping its place, before the guest hears either.
    std::unique_ptr<MCInternetGame> guest = MCInternetGame::Join(guestNet, Code);
    REQUIRE(guest != nullptr);
    host->KeepPlace(true);
    host->End();
    REQUIRE(service.HostPlaceKept());
    const auto away = Clock::now();
    REQUIRE(Until(*guest, *guest, [&] { return guest->Update() == MCInternetGame::Stage::Failed; }));
    CHECK(guest->Refusal() == std::optional(MCServiceRefusal::HostAway));
    CHECK(Clock::now() - away < OpenDeadline);
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
