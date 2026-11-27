#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCFakeMcoRelay.h"
#include "fakes/MCFakeMcoService.h"
#include "network/MCMcoRooms.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

// Port-only (Redux HD; SPEC-mco-rooms.md): MechCommander Online in the game (network/MCMcoRooms.h) against a stand-in
// for MCO's services (tests/fakes/MCFakeMcoService.h) and its relay (tests/fakes/MCFakeMcoRelay.h): the account, the
// lobby's rooms, a room joined and hosted, and a match from the host's START to a guest's game answered through the
// relay.

namespace
{
    using Clock = std::chrono::steady_clock;
    using MCFakeMcoService::MCService;

    /// <summary>How long the model gets to hear MCO (each of its polls comes every 20-30 ms here).</summary>
    constexpr int HeardMs = 5000;
    constexpr auto PollLeast = std::chrono::milliseconds(20);
    constexpr auto PollMost = std::chrono::milliseconds(30);
    constexpr auto LookEvery = std::chrono::milliseconds(5);
    /// <summary>How long the host's game is waited for in these tests.</summary>
    constexpr int HostGameMs = 3000;
    const std::string Content(64, '9');
    const std::string OtherContent(64, '8');
    constexpr _GUID TestGame{0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};

    template <class Done> bool Within(int ms, Done&& done)
    {
        const Clock::time_point until = Clock::now() + std::chrono::milliseconds(ms);

        while (!done())
        {
            if (Clock::now() >= until)
            {
                return false;
            }

            std::this_thread::sleep_for(LookEvery);
        }

        return true;
    }

    /// <summary>The system's store as these tests keep it: in memory.</summary>
    class MCMemoryKeys final : public MCMcoKeys
    {
    public:
        std::optional<MCMcoKept> Load() override
        {
            const std::scoped_lock lock(_Lock);
            return Kept;
        }

        bool Save(const MCMcoKept& kept) override
        {
            const std::scoped_lock lock(_Lock);
            Kept = kept;
            return true;
        }

        void Erase() override
        {
            const std::scoped_lock lock(_Lock);
            Kept.reset();
        }

        std::optional<MCMcoKept> Now() const
        {
            const std::scoped_lock lock(_Lock);
            return Kept;
        }

    private:
        mutable std::mutex _Lock;
        std::optional<MCMcoKept> Kept;
    };

    MCMcoSetup TestSetup(MCService& mco, std::shared_ptr<MCMcoKeys> keys,
                         MCOptionalRef<MCFakeMcoRelay::MCRelay> relay = std::nullopt, std::string content = Content,
                         std::function<uint16_t()> port = {})
    {
        MCMcoSetup setup{.Get = [&mco] { return std::make_unique<MCService::MCTransfer>(mco); },
                         .Client = {.DeviceId = std::string(MCMcoApi::TokenDigits, 'e'),
                                    .UserAgent = "MechCommanderReduxHD/test (macOS 26.5)",
                                    .ClientOs = "{}",
                                    .Chat = std::string(MCFakeMcoService::ChatUrl),
                                    .Account = std::string(MCFakeMcoService::AccountUrl)},
                         .Keys = std::move(keys),
                         .Content = [content] { return std::expected<std::string, std::string>(content); },
                         .GamePort = std::move(port),
                         .PollLeast = PollLeast,
                         .PollMost = PollMost,
                         .HostGameMs = HostGameMs};

        if (relay)
        {
            setup.Dial = [&relay = relay->get()](const MCMcoApi::MCMatch&) { return relay.Dial(); };
        }

        return setup;
    }

    /// <summary>Signed in to the stand-in's account, the lobby open; whether the rooms came.</summary>
    bool SignedInWithLobby(MCMcoRooms& rooms)
    {
        rooms.OpenLobby();
        rooms.SignIn(std::string(MCFakeMcoService::Email), std::string(MCFakeMcoService::Password));
        return Within(HeardMs,
                      [&]
                      {
                          const MCMcoSeen seen = rooms.Seen();
                          return seen.Account == MCMcoAccount::SignedIn && seen.Listed;
                      });
    }

    MCService::MCRoom OthersRoom(std::string id, std::string name, std::string content = Content)
    {
        return {.RoomId = std::move(id),
                .Name = std::move(name),
                .Build = std::string(MCFakeMcoService::StockBuild),
                .Content = std::move(content)};
    }
}

TEST_CASE("mco rooms: signing in keeps the account's tokens in the system's store, never its password, and while "
          "INTERNET is open the lobby lists MCO's Redux rooms on its stock build")
{
    MCService mco;
    mco.AddRoom(OthersRoom(std::string(32, '1'), "Solaris Night"));
    mco.AddRoom(
        {.RoomId = std::string(32, '2'), .Name = "Old build", .Build = "official-0.2.1.50", .Content = Content});
    mco.AddPlayer(std::string(32, '1'), "BOB", true, true);
    const auto keys = std::make_shared<MCMemoryKeys>();
    MCMcoRooms rooms(TestSetup(mco, keys));

    REQUIRE(SignedInWithLobby(rooms));
    const MCMcoSeen seen = rooms.Seen();
    CHECK_EQ(seen.Username, std::string(MCFakeMcoService::AccountName));
    CHECK(!seen.Busy);
    REQUIRE_EQ(seen.Rooms.size(), size_t{1});
    CHECK_EQ(seen.Rooms[0].Name, std::string("Solaris Night"));
    CHECK_EQ(seen.Rooms[0].Host, std::string("BOB"));
    CHECK_EQ(seen.Rooms[0].Players, 1);

    // The tokens kept; the password nowhere in them.
    const std::optional<MCMcoKept> kept = keys->Now();
    REQUIRE(kept.has_value());
    CHECK(MCMcoApi::IsHex(kept->AccountToken, MCMcoApi::TokenDigits));
    CHECK(MCMcoApi::IsHex(kept->PeerToken, MCMcoApi::TokenDigits));
    CHECK(!MCMcoKeptText(*kept).contains(MCFakeMcoService::Password));
    CHECK(MCMcoKeptFrom(MCMcoKeptText(*kept)) == kept);
    CHECK(mco.Asked("activate") == 1);
    CHECK(mco.LastFields("activate")["take_over"] == "false");
    CHECK(mco.Asked("register_peer") >= 1);
    CHECK(mco.Asked("redux_stock_status") == 1);

    // INTERNET closed: out of the lobby, its polls stopped.
    rooms.CloseLobby();
    REQUIRE(Within(HeardMs, [&] { return mco.Asked("unregister_peer") == 1; }));
    const size_t polls = mco.Asked("lobby_poll");
    std::this_thread::sleep_for(PollMost * 4);
    CHECK_EQ(mco.Asked("lobby_poll"), polls);
    CHECK(!rooms.Seen().Listed);
}

TEST_CASE("mco rooms: a wrong password, MCO unreachable and the three-account limit are each said in a sentence")
{
    MCService mco;
    MCMcoRooms rooms(TestSetup(mco, std::make_shared<MCMemoryKeys>()));

    rooms.SignIn(std::string(MCFakeMcoService::Email), "wrong");
    REQUIRE(Within(HeardMs, [&] { return !rooms.Seen().Refusal.empty(); }));
    CHECK_EQ(rooms.Seen().Refusal, std::string(MCMcoRooms::WrongSignInText));
    CHECK_EQ(rooms.Seen().Account, MCMcoAccount::SignedOut);

    mco.CanCreate = false;
    rooms.CreateAccount("RAHMAN", "new@example.com", "a password");
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Refusal == MCMcoRooms::ThreeAccountsText; }));
    CHECK_EQ(mco.Asked("create"), size_t{0});

    mco.Unreachable = true;
    rooms.SignIn(std::string(MCFakeMcoService::Email), std::string(MCFakeMcoService::Password));
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Refusal == MCMcoRooms::UnreachableText; }));
    CHECK(!rooms.Seen().Busy);

    // MCO's own words otherwise; a reply MCO's launcher couldn't read is MCO having changed.
    CHECK_EQ(MCMcoRooms::RefusalText({.Message = "That room is gone.", .Code = "room_not_found", .Status = 403}),
             std::string("That room is gone."));
    CHECK_EQ(MCMcoRooms::RefusalText({.Code = std::string(MCMcoApi::InvalidResponseCode)}),
             std::string(MCMcoRooms::ChangedText));
    CHECK_EQ(MCMcoRooms::RefusalText({.Status = 429}), std::string(MCMcoRooms::TooManyText));
    CHECK_EQ(MCMcoRooms::RefusalText({.Status = 503}), std::string(MCMcoRooms::UnreachableText));
}

TEST_CASE("mco rooms: an account signed in on another computer asks first, and moves here only when told")
{
    MCService mco;
    mco.ActiveElsewhere = true;
    const auto keys = std::make_shared<MCMemoryKeys>();
    MCMcoRooms rooms(TestSetup(mco, keys));

    rooms.SignIn(std::string(MCFakeMcoService::Email), std::string(MCFakeMcoService::Password));
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Account == MCMcoAccount::OtherComputer; }));
    CHECK(!keys->Now().has_value());

    // CANCEL: signed out, nothing taken.
    rooms.TakeOver(false);
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Account == MCMcoAccount::SignedOut; }));
    CHECK_EQ(mco.Asked("activate"), size_t{1});

    // SIGN IN HERE: activated taking it over.
    rooms.SignIn(std::string(MCFakeMcoService::Email), std::string(MCFakeMcoService::Password));
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Account == MCMcoAccount::OtherComputer; }));
    rooms.TakeOver(true);
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Account == MCMcoAccount::SignedIn; }));
    CHECK(mco.LastFields("activate")["take_over"] == "true");
    CHECK(keys->Now().has_value());
}

TEST_CASE("mco rooms: a later start resumes the kept sign-in, an account's through MCO and a guest's as it was; SIGN "
          "OUT forgets it")
{
    MCService mco;
    const auto keys = std::make_shared<MCMemoryKeys>();

    {
        MCMcoRooms first(TestSetup(mco, keys));
        REQUIRE(SignedInWithLobby(first));
    }

    {
        MCMcoRooms later(TestSetup(mco, keys));
        later.Resume();
        REQUIRE(Within(HeardMs, [&] { return later.Seen().Account == MCMcoAccount::SignedIn; }));
        CHECK_EQ(mco.Asked("login"), size_t{1});
        CHECK_EQ(mco.Asked("session"), size_t{1});
        CHECK_EQ(later.Seen().Username, std::string(MCFakeMcoService::AccountName));

        later.SignOut();
        REQUIRE(Within(HeardMs, [&] { return mco.Asked("logout") == 1; }));
        REQUIRE(Within(HeardMs, [&] { return !keys->Now().has_value(); }));
        CHECK_EQ(later.Seen().Account, MCMcoAccount::SignedOut);
    }

    // A guest's profile: kept with its own client id and peer token, back at the next start without asking MCO.
    {
        MCMcoRooms guest(TestSetup(mco, keys));
        guest.OpenLobby();
        guest.PlayAsGuest("HAWKEYE");
        REQUIRE(Within(HeardMs, [&] { return guest.Seen().Account == MCMcoAccount::Guest && guest.Seen().Listed; }));
        const std::optional<MCMcoKept> kept = keys->Now();
        REQUIRE(kept.has_value());
        CHECK(kept->AccountToken.empty());
        CHECK_EQ(kept->Username, std::string("HAWKEYE"));
        CHECK(MCMcoApi::IsHex(kept->ClientId, MCMcoApi::ClientIdDigits));
    }

    const size_t asked = mco.Actions().size();
    MCMcoRooms again(TestSetup(mco, keys));
    again.Resume();
    REQUIRE(Within(HeardMs, [&] { return again.Seen().Account == MCMcoAccount::Guest; }));
    CHECK_EQ(again.Seen().Username, std::string("HAWKEYE"));
    CHECK_EQ(mco.Actions().size(), asked);

    // A name MCO wouldn't take is said, and nothing asked.
    MCMcoRooms named(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    named.PlayAsGuest(std::string(MCMcoApi::NameMost + 1, 'X'));
    REQUIRE(Within(HeardMs, [&] { return !named.Seen().Refusal.empty(); }));
    CHECK_EQ(named.Seen().Account, MCMcoAccount::SignedOut);
}

TEST_CASE("mco rooms: a room on other game data is refused; a joined room's players are polled, and its host leaving "
          "closes it with a sentence naming them")
{
    MCService mco;
    const std::string roomId(32, '1');
    const std::string otherId(32, '2');
    mco.AddRoom(OthersRoom(roomId, "Solaris Night"));
    mco.AddRoom(OthersRoom(otherId, "Other data", OtherContent));
    mco.AddPlayer(roomId, "BOB", true, true);
    MCMcoRooms rooms(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    REQUIRE(SignedInWithLobby(rooms));
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Rooms.size() == 2; }));

    rooms.Join(otherId, "");
    REQUIRE(Within(HeardMs, [&] { return !rooms.Seen().Refusal.empty(); }));
    CHECK_EQ(rooms.Seen().Refusal, std::string(MCMcoRooms::OtherDataText));
    CHECK(!rooms.Seen().Room.has_value());

    rooms.Join(roomId, "");
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Room && rooms.Seen().Room->Players.size() == 2; }));
    MCMcoRoomSeen room = *rooms.Seen().Room;
    CHECK_EQ(room.Name, std::string("Solaris Night"));
    CHECK_EQ(room.Host, std::string("BOB"));
    CHECK(!room.Hosting);
    // Redux HD's player is READY throughout, as MCO's launcher is by default.
    CHECK(mco.LastFields("register_peer")["is_ready"] == "1");
    CHECK(mco.LastFields("register_peer")["engine_content"] == Content);

    mco.HostLeaves(roomId);
    REQUIRE(Within(HeardMs, [&] { return !rooms.Seen().Left.empty(); }));
    CHECK_EQ(rooms.Seen().Left, std::string("BOB left. The room has closed."));
    CHECK(!rooms.Seen().Room.has_value());
    rooms.ClearLeft();
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Left.empty(); }));
}

TEST_CASE("mco rooms: the host's START with every guest READY carries the match: the host's game hosts on its port, "
          "the host's bridge opens, and the guest's game is answered through the relay; the match closes played")
{
    MCService mco;
    MCFakeMcoRelay::MCRelay relay;
    REQUIRE(relay.Port() != 0);
    const MCFakeMcoRelay::MCAnsweringGame hostGame;
    const auto port = [&] { return hostGame.Port(); };
    MCMcoRooms host(TestSetup(mco, std::make_shared<MCMemoryKeys>(), relay, Content, port));
    MCMcoRooms guest(TestSetup(mco, std::make_shared<MCMemoryKeys>(), relay));

    REQUIRE(SignedInWithLobby(host));
    host.Host("Night Raid", 4, "");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room.has_value(); }));
    CHECK(host.Seen().Room->Hosting);
    CHECK(mco.LastFields("register_peer")["engine_build"] == MCFakeMcoService::StockBuild);
    // One player alone can't START.
    CHECK(!MCMcoRooms::Startable(*host.Seen().Room));

    guest.OpenLobby();
    guest.PlayAsGuest("SHINOBI");
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Rooms.size() == 1; }));
    guest.Join(guest.Seen().Rooms[0].RoomId, "");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room && host.Seen().Room->Players.size() == 2; }));
    REQUIRE(MCMcoRooms::Startable(*host.Seen().Room));

    host.Start();
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Match == MCMcoMatchStage::Playing; }));
    const MCMcoSeen hosting = host.Seen();
    CHECK_EQ(hosting.GamePort, hostGame.Port());
    CHECK(hosting.SessionName.starts_with("MCO "));
    CHECK_EQ(hosting.Capacity, 2);
    CHECK_EQ(hosting.MatchName, std::string(MCFakeMcoService::AccountName));
    CHECK_EQ(mco.Asked("publish_ready"), size_t{1});

    // The guest hears the start: its bridge listens for its game.
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Match == MCMcoMatchStage::JoinGame; }));
    const uint16_t guestPort = guest.Seen().GamePort;
    CHECK(guestPort != 0);
    CHECK_EQ(guest.Seen().MatchName, std::string("SHINOBI"));

    // The guest's game asks for the session at its bridge: the host's game answers through the relay.
    MCSocketTransport sockets;
    const MCSocket::Handle guestGame = sockets.OpenUdpOn(MCSocket::LoopbackIp, 0);
    const MCSocket::Address bridge{MCSocket::LoopbackIp, guestPort};
    sockets.SendTo(guestGame, bridge, MCMcoRelay::Query(TestGame));
    std::array<uint8_t, MCMcoRelay::DatagramMost> datagram{};
    MCSocket::Address from;
    int size = 0;
    REQUIRE(Within(HeardMs, [&] { return (size = sockets.ReceiveFrom(guestGame, datagram, from)) > 0; }));
    CHECK(MCMcoRelay::IsAnswer(std::span(datagram).first(static_cast<size_t>(size))));
    CHECK_EQ(MCMcoRelay::AnswerPort(std::span(datagram).first(static_cast<size_t>(size))), guestPort);
    sockets.Close(guestGame);
    CHECK(relay.Hellos().size() >= 4);

    guest.GameInMatch();
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Match == MCMcoMatchStage::Playing; }));
    // Each polls its room saying its game is in the match, as MCO's launcher does (or the room drops the player).
    CHECK(Within(HeardMs, [&] { return mco.LastFieldsFrom("room_poll", "SHINOBI")["redux_process_running"] == "1"; }));
    CHECK(mco.LastFieldsFrom("room_poll", MCFakeMcoService::AccountName)["redux_process_running"] == "1");
    CHECK(!mco.LastFieldsFrom("room_poll", "SHINOBI")["redux_process_session"].empty());

    // The match over: each back in the room, the host's closed as played (with the room's engine).
    guest.GameEnded();
    host.GameEnded();
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Match == MCMcoMatchStage::None; }));
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Match == MCMcoMatchStage::None; }));
    REQUIRE(Within(HeardMs, [&] { return mco.Closed().size() == 1; }));
    CHECK_EQ(mco.Closed()[0].second, std::string(MCFakeMcoService::StockBuild));
    CHECK(host.Seen().Room.has_value());
    CHECK(guest.Seen().Room.has_value());
    // The match isn't played again while its signal stays.
    std::this_thread::sleep_for(PollMost * 4);
    CHECK_EQ(guest.Seen().Match, MCMcoMatchStage::None);

    // LEAVE: the guest is back in the lobby; the host's leaving closes the room.
    guest.Leave();
    REQUIRE(Within(HeardMs, [&] { return !guest.Seen().Room.has_value(); }));
    host.Leave();
    REQUIRE(Within(HeardMs, [&] { return !host.Seen().Room.has_value(); }));
}

TEST_CASE("mco rooms: a match the host's game couldn't host is called off, and the room stays")
{
    MCService mco;
    MCFakeMcoRelay::MCRelay relay;
    // A port nothing answers on: the game never hosts.
    MCMcoRooms host(TestSetup(mco, std::make_shared<MCMemoryKeys>(), relay, Content, [] { return uint16_t{1}; }));
    REQUIRE(SignedInWithLobby(host));
    host.Host("Night Raid", 2, "secret");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room.has_value(); }));
    CHECK(mco.LastFields("register_peer")["password"] == "secret");
    mco.AddPlayer(mco.RoomIds().at(0), "BOB", true);
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room->Players.size() == 2; }));

    host.Start();
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Match == MCMcoMatchStage::HostGame; }));
    host.GameFailed();
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Match == MCMcoMatchStage::None; }));
    CHECK_EQ(host.Seen().Refusal, std::string(MCMcoRooms::NoMatchText));
    REQUIRE(Within(HeardMs, [&] { return mco.Closed().size() == 1; }));
    CHECK(mco.Closed()[0].second.empty());
    CHECK(host.Seen().Room.has_value());
}
