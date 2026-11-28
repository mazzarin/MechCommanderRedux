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
    using Json = nlohmann::json;

    /// <summary>How long the model gets to hear MCO (each of its polls comes every 20-30 ms here).</summary>
    constexpr int HeardMs = 5000;
    constexpr auto PollLeast = std::chrono::milliseconds(20);
    constexpr auto PollMost = std::chrono::milliseconds(30);
    constexpr auto LookEvery = std::chrono::milliseconds(5);
    /// <summary>How long the host's game is waited for in these tests.</summary>
    constexpr int HostGameMs = 3000;
    using MCFakeMcoService::Content;
    using MCFakeMcoService::MCMemoryKeys;
    using MCFakeMcoService::OtherContent;
    using MCFakeMcoService::OthersRoom;
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

    MCMcoSetup TestSetup(MCService& mco, std::shared_ptr<MCMcoKeys> keys,
                         MCOptionalRef<MCFakeMcoRelay::MCRelay> relay = std::nullopt, std::string content = Content,
                         std::function<uint16_t()> port = {})
    {
        MCMcoSetup setup = MCFakeMcoService::Setup(mco, std::move(keys), {}, std::move(content), PollLeast, PollMost);
        setup.GamePort = std::move(port);
        setup.HostGameMs = HostGameMs;

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
    rooms.CreateAccount("MAVERICK", "new@example.com", "a password");
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

TEST_CASE("mco rooms: INTERNET's lobby plays as MCO's guest under its player name with no sign-in, the same guest each "
          "start, and a new name is the guest's")
{
    MCService mco;
    const std::string roomId(32, '1');
    mco.AddRoom(OthersRoom(roomId, "Solaris Night"));
    mco.AddPlayer(roomId, "BOB", true, true);
    const auto keys = std::make_shared<MCMemoryKeys>();
    std::string clientId;

    {
        MCMcoRooms rooms(TestSetup(mco, keys));
        rooms.OpenLobby("HAWKEYE");
        REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Rooms.size() == 1; }));
        CHECK(rooms.Seen().Account == MCMcoAccount::Guest);
        CHECK_EQ(rooms.Seen().Username, std::string("HAWKEYE"));
        REQUIRE(keys->Now().has_value());
        CHECK(keys->Now()->AccountToken.empty());
        CHECK_EQ(keys->Now()->Username, std::string("HAWKEYE"));
        clientId = keys->Now()->ClientId;
        CHECK_EQ(mco.LastFields("register_peer")["username"], std::string("HAWKEYE"));

        // A name typed in INTERNET before JOIN GAME is the guest's in the room, and kept.
        rooms.Join(roomId, "", "MAVERICK");
        REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Room.has_value(); }));
        CHECK_EQ(mco.LastFields("register_peer")["username"], std::string("MAVERICK"));
        CHECK_EQ(keys->Now()->Username, std::string("MAVERICK"));
        rooms.Leave();
        REQUIRE(Within(HeardMs, [&] { return !rooms.Seen().Room.has_value(); }));
    }

    // The next start: the same guest, under the name INTERNET has now.
    MCMcoRooms again(TestSetup(mco, keys));
    again.OpenLobby("GOOSE");
    REQUIRE(Within(HeardMs, [&] { return again.Seen().Username == "GOOSE" && again.Seen().Rooms.size() == 1; }));
    CHECK_EQ(keys->Now()->ClientId, clientId);
    CHECK_EQ(mco.LastFields("register_peer")["client_id"], clientId);

    // A name MCO won't take: the guest is "Player", as MCO's launcher names its new guests.
    MCMcoRooms unnamed(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    unnamed.OpenLobby(" ");
    REQUIRE(Within(HeardMs, [&] { return unnamed.Seen().Account == MCMcoAccount::Guest; }));
    CHECK_EQ(unnamed.Seen().Username, std::string(MCMcoRooms::GuestNameNone));
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
    CHECK(Within(
        HeardMs, [&]
        { return mco.LastFieldsFrom("room_poll", MCFakeMcoService::AccountName)["redux_process_running"] == "1"; }));
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

namespace
{
    /// <summary>The room's chat as INTERNET shows it: "BOB: gl hf", or MCO's own line.</summary>
    std::vector<std::string> ChatOf(const MCMcoSeen& seen)
    {
        std::vector<std::string> lines;

        if (seen.Room)
        {
            for (const MCMcoChatLine& line : seen.Room->Chat)
            {
                lines.push_back(line.Name.empty() ? line.Text : std::format("{}: {}", line.Name, line.Text));
            }
        }

        return lines;
    }

    bool Has(const std::vector<std::string>& lines, std::string_view line)
    {
        return std::ranges::find(lines, line) != lines.end();
    }
}

TEST_CASE("mco rooms: a joined room's chat: what was said before joining, the player's own line at once and MCO's copy "
          "in its place, others' lines, MCO's own sentences, and a refused line saying why")
{
    MCService mco;
    const std::string roomId(32, '1');
    mco.AddRoom(OthersRoom(roomId, "Solaris Night"));
    mco.AddPlayer(roomId, "BOB", true, true);
    mco.Say(roomId, "BOB", "gl hf");
    MCMcoRooms rooms(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    rooms.OpenLobby("HAWKEYE");
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Rooms.size() == 1; }));

    // Before joining, the room's history: as MCO's launcher's room window, the room's last lines.
    rooms.Join(roomId, "");
    REQUIRE(Within(HeardMs, [&] { return Has(ChatOf(rooms.Seen()), "HAWKEYE joined the room."); }));
    CHECK((ChatOf(rooms.Seen()) == std::vector<std::string>{"BOB: gl hf", "HAWKEYE joined the room."}));

    // The player's own line: shown at once, then MCO's copy in its place (once, with MCO's id).
    rooms.Say("  ready when you are  ");
    CHECK(Has(ChatOf(rooms.Seen()), "HAWKEYE: ready when you are"));
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Room->Chat.back().Id != 0; }));
    CHECK_EQ(std::ranges::count(ChatOf(rooms.Seen()), std::string("HAWKEYE: ready when you are")), 1);
    const std::vector<Json> said = mco.MessagesIn(roomId);
    REQUIRE(!said.empty());
    CHECK_EQ(said.back()["message"].get<std::string>(), std::string("ready when you are"));
    CHECK_EQ(mco.LastFields("send_message")["rich_text_version"], std::string("1"));
    // A blank line isn't sent.
    rooms.Say("   ");
    CHECK_EQ(mco.Asked("send_message"), size_t{1});

    // Others' lines, a GIF as what it was, and MCO's own sentences.
    mco.Say(roomId, "BOB", "launching in 10");
    mco.AddLine(roomId, {{"username", "BOB"}, {"message", ""}, {"type", "gif"}});
    mco.AddPlayer(roomId, "CLANNER", true);
    mco.AddLine(roomId, {{"sender_client_id", ""},
                         {"username", "System"},
                         {"message", ""},
                         {"room_event", "joined"},
                         {"event_player", "CLANNER"}});
    REQUIRE(Within(HeardMs, [&] { return Has(ChatOf(rooms.Seen()), "CLANNER joined the room."); }));
    const std::vector<std::string> chat = ChatOf(rooms.Seen());
    CHECK(Has(chat, "BOB: launching in 10"));
    CHECK(Has(chat, "BOB sent a GIF."));

    // A line MCO refuses gives way to MCO's sentence.
    mco.ChatRefused = true;
    rooms.Say("can anyone hear me");
    REQUIRE(Within(HeardMs, [&] { return Has(ChatOf(rooms.Seen()), "You can't chat in this room."); }));
    CHECK(!Has(ChatOf(rooms.Seen()), "HAWKEYE: can anyone hear me"));
}

TEST_CASE("mco rooms: a player removed from the room hears who removed them, and is back in the lobby")
{
    MCService mco;
    const std::string roomId(32, '1');
    mco.AddRoom(OthersRoom(roomId, "Solaris Night"));
    mco.AddPlayer(roomId, "BOB", true, true);
    MCMcoRooms rooms(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    rooms.OpenLobby("HAWKEYE");
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Rooms.size() == 1; }));
    rooms.Join(roomId, "");
    REQUIRE(Within(HeardMs, [&] { return rooms.Seen().Room.has_value(); }));

    mco.Kick(roomId, "HAWKEYE", "BOB");
    REQUIRE(Within(HeardMs, [&] { return !rooms.Seen().Left.empty(); }));
    CHECK_EQ(rooms.Seen().Left, std::string("BOB removed you from the room."));
    CHECK(!rooms.Seen().Room.has_value());
    // The room's other players heard it in its chat.
    CHECK(Has(
        [&]
        {
            std::vector<std::string> events;

            for (const Json& line : mco.MessagesIn(roomId))
            {
                events.push_back(line.value("room_event", std::string()) + " " +
                                 line.value("event_player", std::string()));
            }

            return events;
        }(),
        "removed HAWKEYE"));

    // Removed without MCO saying by whom: the room's host is named.
    CHECK_EQ(MCMcoRooms::LeftText("kicked", "BOB", false), std::string("BOB removed you from the room."));
    CHECK_EQ(MCMcoRooms::LeftText("kicked", "", true), std::string("The host removed you from the room."));
}

// Port-only (Redux HD; SPEC-mco-hosting.md): an internet game on MCO too.

namespace
{
    /// <summary>The internet game the tests host on both: private, its code the room's password.</summary>
    MCMcoBoth SteelRain()
    {
        return {.Name = "Steel Rain", .MaxPlayers = 6, .Password = "K7QF", .GamePort = 28881};
    }

    /// <summary>The one room on the stand-in (the internet game's), once it's there.</summary>
    std::string OnlyRoom(const MCService& mco)
    {
        const std::vector<std::string> ids = mco.RoomIds();
        return ids.size() == 1 ? ids[0] : std::string();
    }
}

TEST_CASE("mco rooms: an internet game hosted on both is a room there, locked with its code, hosted under the name "
          "typed on INTERNET and described with its mission, with nothing of it on the screens")
{
    MCService mco;
    const auto keys = std::make_shared<MCMemoryKeys>();
    MCMcoRooms host(TestSetup(mco, keys));
    // The lobby opened with the computer's name (INTERNET's first), the game hosted under the one typed after.
    host.OpenLobby("ANN");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Listed; }));

    MCMcoBoth both = SteelRain();
    both.GuestName = "FALCON";
    host.HostBoth(std::move(both));
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room.has_value() && !OnlyRoom(mco).empty(); }));
    const MCMcoSeen hosting = host.Seen();
    CHECK(hosting.Room->Both);
    CHECK(hosting.Room->Hosting);
    CHECK(!hosting.Busy);
    CHECK(hosting.Refusal.empty());
    CHECK(!hosting.ClientId.empty());
    std::map<std::string, std::string> asked = mco.LastFields("register_peer");
    CHECK_EQ(asked["is_host"], std::string("1"));
    CHECK_EQ(asked["password"], std::string("K7QF"));
    CHECK_EQ(asked["room"], std::string("Steel Rain"));
    CHECK_EQ(asked["username"], std::string("FALCON"));
    CHECK_EQ(host.Seen().Username, std::string("FALCON"));
    REQUIRE(keys->Load().has_value());
    CHECK_EQ(keys->Load()->Username, std::string("FALCON"));
    const std::string roomId = OnlyRoom(mco);
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room->RoomId == roomId; }));

    // The mission picked is the room's description; the same again isn't asked twice; fewer seats are told.
    host.Describe("Arena", 6);
    REQUIRE(Within(HeardMs, [&] { return mco.RoomOf(roomId)->Description == "Arena"; }));
    host.Describe("Arena", 6);
    host.Describe("Arena", 4);
    REQUIRE(Within(HeardMs, [&] { return mco.RoomOf(roomId)->MaxPlayers == 4; }));
    CHECK_EQ(mco.Asked("update_room"), size_t{2});
    CHECK_EQ(mco.LastFields("update_room")["new_name"], std::string("Steel Rain"));
    CHECK_EQ(mco.LastFields("update_room")["room_id"], roomId);
    CHECK(!mco.LastFields("update_room").contains("password"));

    host.Leave();
    REQUIRE(Within(HeardMs, [&] { return mco.RoomIds().empty(); }));
    CHECK(!host.Seen().Room.has_value());
    CHECK(host.Seen().Left.empty());
}

TEST_CASE("mco rooms: an internet game's guest sits quietly in its host's room, READY, plays none of its matches, and "
          "goes without a word")
{
    MCService mco;
    MCMcoRooms host(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    MCMcoRooms guest(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    host.OpenLobby("ANN");
    guest.OpenLobby("BOB");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Listed && guest.Seen().Listed; }));
    host.HostBoth(SteelRain());
    REQUIRE(Within(HeardMs, [&] { return !OnlyRoom(mco).empty(); }));
    const std::string roomId = OnlyRoom(mco);
    guest.CloseLobby();

    guest.Sit(roomId, "Steel Rain", 6, "K7QF");
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Seated; }));
    CHECK(!guest.Seen().Room.has_value());
    CHECK(!guest.Seen().ClientId.empty());
    CHECK(mco.SeatOf(roomId, "BOB") == std::optional<bool>(true));
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room && host.Seen().Room->Players.size() == 2; }));

    // The room's match goes on without the quiet seat's game, which reports it running: its seat stays.
    mco.StartMatch(roomId);
    std::this_thread::sleep_for(std::chrono::milliseconds(10 * PollMost.count()));
    CHECK(guest.Seen().Match == MCMcoMatchStage::None);
    CHECK(guest.Seen().Seated);
    CHECK(mco.SeatOf(roomId, "BOB").has_value());

    guest.Unsit();
    REQUIRE(Within(HeardMs, [&] { return !guest.Seen().Seated; }));
    CHECK(!mco.SeatOf(roomId, "BOB").has_value());

    // Taken out of the room, the seat goes without a word on the screens.
    guest.Sit(roomId, "Steel Rain", 6, "K7QF");
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Seated; }));
    mco.Kick(roomId, "BOB", "ANN");
    REQUIRE(Within(HeardMs, [&] { return !guest.Seen().Seated; }));
    CHECK(guest.Seen().Left.empty());
    CHECK(!guest.Seen().Room.has_value());
}

namespace
{
    /// <summary>
    /// The internet game's room hosted on both by <paramref name="host"/> (ANN), and <paramref name="guest"/> (BOB) sat
    /// quietly in it: the room's id; empty when it didn't come.
    /// </summary>
    std::string HostedWithSeat(MCService& mco, MCMcoRooms& host, MCMcoRooms& guest)
    {
        host.OpenLobby("ANN");
        guest.OpenLobby("BOB");

        if (!Within(HeardMs, [&] { return host.Seen().Listed && guest.Seen().Listed; }))
        {
            return {};
        }

        host.HostBoth(SteelRain());

        if (!Within(HeardMs, [&] { return !OnlyRoom(mco).empty(); }))
        {
            return {};
        }

        const std::string roomId = OnlyRoom(mco);
        guest.CloseLobby();
        guest.Sit(roomId, "Steel Rain", 6, "K7QF");
        return Within(HeardMs, [&] { return guest.Seen().Seated; }) ? roomId : std::string();
    }

    /// <summary>Whether <paramref name="rooms"/> hosts the internet game's room on both, another than <paramref name="notRoom"/>.</summary>
    bool HostsBoth(const MCMcoRooms& rooms, std::string_view notRoom = {})
    {
        const MCMcoSeen seen = rooms.Seen();
        return seen.Room.has_value() && seen.Room->Hosting && seen.Room->Both && !seen.Room->RoomId.empty() &&
               seen.Room->RoomId != notRoom;
    }
}

/// <summary>
/// SPEC-mco-hosting.md's "When the host leaves", by LEAVE: the host hands the internet game's MCO room to the game's new
/// host's quiet seat (transfer_host) and leaves; that game, the internet game rolled over to it, takes the same room as
/// its own, hosted on both, with MCO's waiting player still in it, then describes it and closes it as it leaves.
/// </summary>
TEST_CASE("mco rooms: an internet game's room is handed on with the game to its new host's quiet seat, the same room, "
          "its waiting players still in it")
{
    MCService mco;
    MCMcoRooms host(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    MCMcoRooms guest(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    const std::string roomId = HostedWithSeat(mco, host, guest);
    REQUIRE(!roomId.empty());
    mco.AddPlayer(roomId, "CLANNER", true);
    const std::string bob = guest.Seen().ClientId;

    // The new host's game waits to be handed the room; the old host hands it on as it leaves.
    guest.TakeBoth(SteelRain());
    host.HandOnBoth(bob);
    REQUIRE(Within(HeardMs, [&] { return HostsBoth(guest); }));
    const MCMcoSeen taken = guest.Seen();
    CHECK_EQ(taken.Room->RoomId, roomId);
    CHECK(!taken.Seated);
    CHECK_EQ(mco.Asked("transfer_host"), size_t{1});
    CHECK_EQ(mco.LastFields("transfer_host")["target_client_id"], bob);
    REQUIRE(Within(HeardMs, [&] { return !host.Seen().Room.has_value() && !mco.SeatOf(roomId, "ANN").has_value(); }));
    CHECK(mco.RoomIds() == std::vector<std::string>{roomId});
    REQUIRE(mco.RoomOf(roomId).has_value());
    CHECK_EQ(mco.RoomOf(roomId)->HostClient, bob);
    CHECK(mco.SeatOf(roomId, "CLANNER").has_value());
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Room->Players.size() == 2; }));

    // The room is the new host's own: described by it, and closed as it leaves.
    guest.Describe("Arena", 6);
    REQUIRE(Within(HeardMs, [&] { return mco.RoomOf(roomId)->Description == "Arena"; }));
    guest.LeaveBoth();
    REQUIRE(Within(HeardMs, [&] { return mco.RoomIds().empty(); }));
}

/// <summary>
/// SPEC-mco-hosting.md's "When the host leaves", a crash: the game an internet game rolled over to hosts the game's MCO
/// room as a new one when the old host's room is gone (MCO closed it as the host's connection was lost), when it isn't
/// handed on within the wait, or when it had no seat; a hand-on MCO refuses leaves the old host's room to close.
/// </summary>
TEST_CASE(
    "mco rooms: a game an internet game rolled over to hosts a new room when the old host's is gone, isn't handed "
    "on in time, or it had no seat")
{
    const auto keys = [] { return std::make_shared<MCMemoryKeys>(); };

    // The old host crashed: MCO closes its room, and the new host's game hosts its own at once (long before its wait).
    {
        MCService mco;
        MCMcoSetup waiting = TestSetup(mco, keys());
        waiting.HandOnWait = std::chrono::minutes(1);
        MCMcoRooms host(TestSetup(mco, keys()));
        MCMcoRooms guest(std::move(waiting));
        const std::string roomId = HostedWithSeat(mco, host, guest);
        REQUIRE(!roomId.empty());
        guest.TakeBoth(SteelRain());
        mco.HostLeaves(roomId);
        REQUIRE(Within(HeardMs, [&] { return HostsBoth(guest, roomId); }));
        CHECK_EQ(mco.Asked("transfer_host"), size_t{0});
        CHECK_EQ(mco.RoomIds().size(), size_t{1});
        CHECK_EQ(mco.LastFieldsFrom("register_peer", "BOB")["password"], std::string("K7QF"));
    }

    // Not handed on within the wait: the seat let go, and a new room; the old host's hand-on then refused (its new host
    // isn't in its room), its room closes as it leaves.
    {
        MCService mco;
        MCMcoSetup waiting = TestSetup(mco, keys());
        waiting.HandOnWait = std::chrono::milliseconds(200);
        MCMcoRooms host(TestSetup(mco, keys()));
        MCMcoRooms guest(std::move(waiting));
        const std::string roomId = HostedWithSeat(mco, host, guest);
        REQUIRE(!roomId.empty());
        const std::string bob = guest.Seen().ClientId;
        guest.TakeBoth(SteelRain());
        REQUIRE(Within(HeardMs, [&] { return HostsBoth(guest, roomId); }));
        CHECK(!mco.SeatOf(roomId, "BOB").has_value());
        CHECK_EQ(mco.RoomIds().size(), size_t{2});

        host.HandOnBoth(bob);
        REQUIRE(Within(HeardMs, [&] { return mco.RoomIds().size() == 1; }));
        CHECK(!mco.RoomOf(roomId).has_value());
        REQUIRE(Within(HeardMs, [&] { return !host.Seen().Room.has_value(); }));
        CHECK(host.Seen().Left.empty());
        CHECK(HostsBoth(guest, roomId));
    }

    // No seat in the old host's room: a new room at once.
    {
        MCService mco;
        MCMcoRooms solo(TestSetup(mco, keys()));
        solo.OpenLobby("CAT");
        REQUIRE(Within(HeardMs, [&] { return solo.Seen().Listed; }));
        solo.TakeBoth(SteelRain());
        REQUIRE(Within(HeardMs, [&] { return HostsBoth(solo) && !OnlyRoom(mco).empty(); }));
        CHECK_EQ(mco.Asked("transfer_host"), size_t{0});
    }
}

/// <summary>
/// SPEC-mco-hosting.md's "The chat": the internet game's room lines said in MCO's room from each game's own seat, the
/// host's and a guest's quiet one, nothing of it shown; a game with no such seat says nothing there.
/// </summary>
TEST_CASE("mco rooms: an internet game's lines are said in its MCO room from the host's seat and a guest's quiet one, "
          "and nowhere without a seat")
{
    MCService mco;
    MCMcoRooms host(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    MCMcoRooms guest(TestSetup(mco, std::make_shared<MCMemoryKeys>()));
    host.OpenLobby("ANN");
    guest.OpenLobby("BOB");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Listed && guest.Seen().Listed; }));

    // No seat yet: nothing said.
    guest.SayBoth("too early");
    host.HostBoth(SteelRain());
    REQUIRE(Within(HeardMs, [&] { return !OnlyRoom(mco).empty(); }));
    const std::string roomId = OnlyRoom(mco);
    guest.CloseLobby();
    guest.Sit(roomId, "Steel Rain", 6, "K7QF");
    REQUIRE(Within(HeardMs, [&] { return guest.Seen().Seated; }));
    const auto saidBy = [&](std::string_view name, std::string_view text)
    {
        const std::vector<Json> said = mco.MessagesIn(roomId);
        return std::ranges::any_of(said, [&](const Json& line)
                                   { return line.value("username", "") == name && line.value("message", "") == text; });
    };

    host.SayBoth("  gl hf  ");
    guest.SayBoth("on my way");
    REQUIRE(Within(HeardMs, [&] { return saidBy("ANN", "gl hf") && saidBy("BOB", "on my way"); }));
    CHECK(!saidBy("BOB", "too early"));
    CHECK_EQ(mco.Asked("send_message"), size_t{2});
    // Nothing shown as said: the quiet seat has no room, and the host's lines are MCO's copies only.
    CHECK(!guest.Seen().Room.has_value());
    CHECK(std::ranges::none_of(host.Seen().Room->Chat, [](const MCMcoChatLine& line) { return line.Id == 0; }));

    // A blank line isn't sent; nor one MCO refuses said anywhere.
    host.SayBoth("   ");
    mco.ChatRefused = true;
    guest.SayBoth("can anyone hear me");
    REQUIRE(Within(HeardMs, [&] { return mco.Asked("send_message") == 3; }));
    CHECK(!guest.Seen().Room.has_value());
    CHECK(guest.Seen().Seated);
}

TEST_CASE("mco rooms: an internet game's room starts on MCO at LET IN, its bridge open on the game's own port and its "
          "match's name for MCO's players, closes played at the match's end, and leaves MCO quietly for a battle MCO "
          "won't start")
{
    MCService mco;
    MCFakeMcoRelay::MCRelay relay;
    REQUIRE(relay.Port() != 0);
    MCMcoRooms host(TestSetup(mco, std::make_shared<MCMemoryKeys>(), relay));
    MCMcoRooms guest(TestSetup(mco, std::make_shared<MCMemoryKeys>(), relay));
    host.OpenLobby("ANN");
    guest.OpenLobby("BOB");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Listed && guest.Seen().Listed; }));
    MCMcoBoth both = SteelRain();
    size_t carried = 0;
    both.Carry = [&carried]
    {
        carried++;
        return std::shared_ptr<MCCarriedLink>();
    };

    host.HostBoth(std::move(both));
    REQUIRE(Within(HeardMs, [&] { return !OnlyRoom(mco).empty(); }));
    const std::string roomId = OnlyRoom(mco);

    // Alone in MCO's room, LET IN can't start it: said, the room kept.
    host.StartBoth(false);
    REQUIRE(Within(HeardMs, [&] { return !host.Seen().Refusal.empty(); }));
    CHECK_EQ(host.Seen().Refusal, std::string(MCMcoRooms::LetInFailedText));
    CHECK_EQ(mco.Asked("create_session"), size_t{0});
    CHECK(host.Seen().Room.has_value());

    // With the internet game's guest seated, READY: LET IN starts it, the game already hosting (no wait on its port).
    guest.CloseLobby();
    guest.Sit(roomId, "Steel Rain", 6, "K7QF");
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room && host.Seen().Room->Players.size() == 2; }));
    REQUIRE_EQ(host.Seen().Refusal, std::string(MCMcoRooms::LetInFailedText));
    host.StartBoth(false);
    // LET IN asks afresh: the last one's failure goes at once (the room says it until then).
    CHECK(host.Seen().Refusal.empty());
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Match == MCMcoMatchStage::Playing; }));
    CHECK(host.Seen().Refusal.empty());
    CHECK_EQ(host.Seen().GamePort, uint16_t{0});
    CHECK_EQ(mco.Asked("create_session"), size_t{1});
    CHECK_EQ(mco.Asked("publish_ready"), size_t{1});
    // MCO's players join the internet game under the match's name, as MCO's relay helper wants (MCMcoSeats::JoinName).
    CHECK(host.Seen().SessionName.starts_with("MCO "));
    CHECK_EQ(host.Seen().SessionName.size(), std::string_view("MCO 0123456789ab").size());
    // Started already: the battle's start asks nothing more; the quiet seat plays none of it.
    host.StartBoth(true);
    std::this_thread::sleep_for(PollMost * 2);
    CHECK_EQ(mco.Asked("create_session"), size_t{1});
    CHECK(guest.Seen().Seated);
    CHECK_EQ(guest.Seen().Match, MCMcoMatchStage::None);

    // The match over: closed as played, with the room's engine; the room waits again.
    host.BothMatchOver();
    REQUIRE(Within(HeardMs, [&] { return mco.Closed().size() == 1; }));
    CHECK_EQ(mco.Closed()[0].second, std::string(MCFakeMcoService::StockBuild));
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Match == MCMcoMatchStage::None; }));
    CHECK(host.Seen().Room.has_value());
    CHECK(host.Seen().SessionName.empty());

    // The guest gone, the battle's start finds MCO's room alone: it leaves MCO for the battle, without a word.
    guest.Unsit();
    REQUIRE(Within(HeardMs, [&] { return host.Seen().Room && host.Seen().Room->Players.size() == 1; }));
    host.StartBoth(true);
    REQUIRE(Within(HeardMs, [&] { return mco.RoomIds().empty(); }));
    REQUIRE(Within(HeardMs, [&] { return !host.Seen().Room.has_value(); }));
    CHECK(host.Seen().Refusal.empty());
    CHECK(host.Seen().Left.empty());
    CHECK_EQ(mco.Asked("create_session"), size_t{1});
    CHECK_EQ(carried, size_t{0});
}
