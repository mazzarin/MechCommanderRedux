#include "stdafx.h"
#include "MCTest.h"
#include "MCInternetSpots.h"
#include "MCMenusPictures.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCFakeMcoRelay.h"
#include "fakes/MCFakeMcoService.h"
#include "fakes/MCServiceScript.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRoomScreen.h"
#include "network/MCMcoBridge.h"
#include "network/MCMcoRooms.h"
#include "network/MCMcoSeats.h"
#include "network/MCMcoSwitch.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlDialog.h"

#include <format>

// Port-only (Redux HD; SPEC-mco-rooms.md): MechCommander Online's rooms in INTERNET's list like any other game, its
// player a guest under INTERNET's name with no sign-in: the rooms beside the service's games, JOIN GAME on an open room
// and a locked one (its password's window), the room's players and whom it waits for, its chat, LEAVE, the room closing
// under the player or its host removing them, and a match joined through the stand-in relay (tests/fakes/MCFakeMcoRelay.h) into the session screen and
// back to the room. Against a stand-in MCO (tests/fakes/MCFakeMcoService.h). --shots saves each step's screen.

using MCTestGuest::DialogText;
using MCTestGuest::InRoom;
using MCTestGuest::OnInternet;
using MCTestGuest::PressDialogOk;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;
using Json = nlohmann::json;
using Rows = std::vector<std::string>;

namespace
{
    constexpr std::string_view Service = MCFakeGames::ServiceUrl;
    constexpr auto PollLeast = std::chrono::milliseconds(20);
    constexpr auto PollMost = std::chrono::milliseconds(30);

    const std::string SolarisId(MCMcoApi::ClientIdDigits, '1');
    const std::string BunkerId(MCMcoApi::ClientIdDigits, '2');
    const std::string RaidId(MCMcoApi::ClientIdDigits, '3');
    constexpr std::string_view BunkerPassword = "rook";

    /// <summary>The window the shots are drawn at, as a player's 16:9 one.</summary>
    constexpr MCMenusPictures::MCWindowSize ShotWindow{1280, 720};

    /// <summary>What the menus show now, drawn on the software path; --shots saves it as <paramref name="name"/>.</summary>
    void Shot(std::string_view name)
    {
        // The hash isn't checked: these frames are for the eye.
        static_cast<void>(MCMenusPictures::MenusDrawnAt(ShotWindow, name));
    }

    /// <summary>MCO in the test's game for the scope's time (its thread stopped as it ends).</summary>
    struct MCMcoScope
    {
        explicit MCMcoScope(MCMcoSetup setup) { MCMcoRooms::Setup(std::move(setup)); }
        ~MCMcoScope() { MCMcoRooms::Release(); }
        MCMcoScope(const MCMcoScope&) = delete;
        MCMcoScope& operator=(const MCMcoScope&) = delete;
    };

    /// <summary>
    /// MCO's switch as a game that has heard nothing, however the test ends (the user folder is the next test's).
    /// </summary>
    struct MCSwitchRestore
    {
        MCSwitchRestore() = default;
        MCSwitchRestore(const MCSwitchRestore&) = delete;
        MCSwitchRestore& operator=(const MCSwitchRestore&) = delete;
        ~MCSwitchRestore() { MCMcoSwitch::Heard({.Rooms = true, .Hosts = false}); }
    };

    /// <summary>
    /// MCO's lobby as these tests have it: BOB's Solaris Night (BOB READY, CLANNER not), ANN's Bunker (locked), and
    /// CAT's Night raid, in battle.
    /// </summary>
    void ThreeRooms(MCFakeMcoService::MCService& mco)
    {
        mco.AddRoom(MCFakeMcoService::OthersRoom(SolarisId, "Solaris Night"));
        mco.AddPlayer(SolarisId, "BOB", true, true);
        mco.AddPlayer(SolarisId, "CLANNER", false);
        MCFakeMcoService::MCService::MCRoom bunker = MCFakeMcoService::OthersRoom(BunkerId, "Bunker");
        bunker.MaxPlayers = 4;
        bunker.Password = std::string(BunkerPassword);
        mco.AddRoom(std::move(bunker));
        mco.AddPlayer(BunkerId, "ANN", true, true);
        mco.AddRoom(MCFakeMcoService::OthersRoom(RaidId, "Night raid"));
        mco.AddPlayer(RaidId, "CAT", true, true);
        mco.AddPlayer(RaidId, "DAN", true);
        mco.AddPlayer(RaidId, "EVE", true);
        mco.StartMatch(RaidId);
    }

    /// <summary>
    /// INTERNET's stand-ins for the scope's time: the service's scripted list (its count three online), and MCO's lobby
    /// with <see cref="ThreeRooms"/>, its player MCO's guest.
    /// </summary>
    struct MCStandIns
    {
        MCServiceScript::ListScript List;
        std::string Asked;
        MCServiceScript::SetupScope Scope{MCServiceScript::ListOpener(List),
                                          MCServiceScript::Answering(R"({"online":3})", Asked), Service,
                                          MCInternetSpots::Version};
        MCFakeMcoService::MCService Mco;
        std::optional<MCMcoScope> McoScope;

        MCStandIns()
        {
            ThreeRooms(Mco);
            McoScope.emplace(MCFakeMcoService::Setup(Mco, std::make_shared<MCFakeMcoService::MCMemoryKeys>(), {},
                                                     MCFakeMcoService::Content, PollLeast, PollMost));
        }

        MCStandIns(const MCStandIns&) = delete;
        MCStandIns& operator=(const MCStandIns&) = delete;
    };

    /// <summary>INTERNET's screen (made by its first opening).</summary>
    MCGamesScreen& Internet()
    {
        const MCGuiHandle<MCGamesScreen> screen = MCTestGuest::Games(MCGamesWhere::Internet);
        REQUIRE(screen);
        return *screen;
    }

    /// <summary>The service's list with its one game, Bob's.</summary>
    Json BobsListing()
    {
        return MCInternetSpots::Listing(
            {MCServiceScript::ListedGame("K7QF", "Bob's game", {"Bob", "Ann"}, 6, nullptr, MCInternetSpots::Version)},
            3, 0);
    }

    /// <summary>INTERNET opened from MULTIPLAYER, the service's one game listed and MCO's three rooms with it.</summary>
    MCGamesScreen& OpenWithRooms(MCServiceScript::ListScript& list, size_t rows)
    {
        MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
        Settle();
        MCTestGuest::PressInternet();
        REQUIRE(SettleUntil([] { return OnInternet(); }));
        list.Send(BobsListing());
        MCGamesScreen& screen = Internet();
        REQUIRE(SettleUntil([&] { return screen.Seen().Rows.size() == rows; }));
        return screen;
    }

    /// <summary>BOB's game, on another computer: it hosts the match's session on its port, its MCO helper's bridge on
    /// the relay, ready for BOB's START.</summary>
    struct MCBobsGame
    {
        MCTestGuest::Host Game;
        std::unique_ptr<MCMcoBridge::MCHost> Bridge;

        explicit MCBobsGame(MCFakeMcoRelay::MCRelay& relay)
        {
            const uint16_t port = MCMcoBridge::FreeGamePort();
            REQUIRE(port != 0);
            REQUIRE(Game.Open(std::to_string(port), "BOB", "MCO 0123456789ab", 0, 3));
            auto bridge = MCMcoBridge::MCHost::Start(relay.Dial(), "host-ticket", port, {});
            REQUIRE(bridge.has_value());
            Bridge = std::move(*bridge);
        }

        ~MCBobsGame() { Bridge->Stop(); }
        MCBobsGame(const MCBobsGame&) = delete;
        MCBobsGame& operator=(const MCBobsGame&) = delete;
    };

    /// <summary>MCO with the relay's dial, in the test's game.</summary>
    MCMcoSetup RelaySetup(MCFakeMcoService::MCService& mco, MCFakeMcoRelay::MCRelay& relay)
    {
        return MCFakeMcoService::Setup(
            mco, std::make_shared<MCFakeMcoService::MCMemoryKeys>(), [&relay](const MCMcoApi::MCMatch&)
            { return relay.Dial(); }, MCFakeMcoService::Content, PollLeast, PollMost);
    }

    bool Has(const Rows& rows, std::string_view row)
    {
        return std::ranges::find(rows, row) != rows.end();
    }

    /// <summary>The row of the game named <paramref name="name"/>, picked.</summary>
    void PickNamed(MCGamesScreen& screen, std::string_view name)
    {
        const auto found =
            std::ranges::find_if(screen.List().Games(), [&](const MCListedGame& game) { return game.Name == name; });
        REQUIRE(found != screen.List().Games().end());
        screen.PickKey(found->Key);
        Settle();
    }
}

TEST_CASE_ISOLATED("game: INTERNET lists MechCommander Online's rooms with the service's games, its player a guest "
                   "under INTERNET's name, and joins one: its players and whom it waits for, then LEAVE")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":3})", asked), Service,
                                      MCInternetSpots::Version);
    MCFakeMcoService::MCService mco;
    ThreeRooms(mco);
    const auto keys = std::make_shared<MCFakeMcoService::MCMemoryKeys>();
    const MCMcoScope mcoScope(MCFakeMcoService::Setup(mco, keys, {}, MCFakeMcoService::Content, PollLeast, PollMost));
    REQUIRE(MCTestGame::StartLogistics());

    // One list: the service's game and MCO's rooms, open ones first, a locked one so, the one in battle last. No
    // sign-in: the player is MCO's guest under INTERNET's name, kept for the next start.
    MCGamesScreen& screen = OpenWithRooms(list, 4);
    CHECK((screen.Seen().Rows == Rows{"Bob's game | 2/6 | Europe | Open", "Solaris Night | 2/6 |  | Open",
                                      "Bunker | 1/4 |  | Locked", "Night raid | 3/6 |  | In battle"}));
    REQUIRE(keys->Now().has_value());
    CHECK(keys->Now()->AccountToken.empty());
    CHECK_EQ(keys->Now()->Username, screen.PlayerName());
    CHECK_EQ(mco.LastFields("register_peer")["username"], screen.PlayerName());
    CHECK(DialogText().empty());
    Shot("mco internet 1 list");

    // A room picked: its host, and how many play.
    PickNamed(screen, "Solaris Night");
    CHECK_EQ(screen.Seen().PickName, "Solaris Night");
    CHECK((screen.Seen().PickPlayers == Rows{"BOB"}));
    CHECK((screen.Seen().PickLines == Rows{"Public"}));
    CHECK(screen.Seen().CanJoin);
    PickNamed(screen, "Bunker");
    CHECK((screen.Seen().PickLines == Rows{"Needs a password"}));
    Shot("mco internet 2 locked picked");
    PickNamed(screen, "Night raid");
    CHECK(!screen.Seen().CanJoin);

    // JOIN GAME on the open room, under the name typed: the list becomes its players, READY or not (the player READY
    // throughout, as MCO's launcher is), the line says whom it waits for, and LEAVE takes the buttons' place.
    screen.SetPlayerName("Rahman");
    PickNamed(screen, "Solaris Night");
    screen.PressJoin();
    REQUIRE(SettleUntil([&] { return screen.Seen().InRoom && screen.Seen().RoomPlayers.size() == 3; }));
    CHECK_EQ(screen.Seen().RoomName, "Solaris Night");
    CHECK_EQ(screen.Seen().RoomLine, "Waiting for BOB to start.");
    CHECK((screen.Seen().RoomPlayers == Rows{"BOB | HOST | Ready", "CLANNER | Not ready", "Rahman | Ready"}));
    CHECK_EQ(mco.LastFields("register_peer")["username"], "Rahman");
    CHECK_EQ(keys->Now()->Username, "Rahman");
    CHECK(DialogText().empty());
    CHECK(OnInternet());

    // The room's chat under whom it waits for: MCO's own lines and the players', the keys in its box from the start.
    mco.Say(SolarisId, "BOB", "gl hf");
    REQUIRE(SettleUntil([&] { return Has(screen.Seen().RoomChat, "BOB: gl hf"); }));
    CHECK((screen.Seen().RoomChat == Rows{"Rahman joined the room.", "BOB: gl hf"}));
    Rml::Element* say = MCTestGuest::ElementOf(screen, "room-say");
    REQUIRE(say != nullptr);
    REQUIRE(say->GetContext() != nullptr);
    CHECK(say->GetContext()->GetFocusElement() == say);
    Rows hints;

    for (const MCRmlKeyHint& hint : screen.Keys)
    {
        hints.push_back(std::format("{} {}", hint.Key, hint.Does));
    }

    CHECK((hints == Rows{"ENTER Say", "ESC Leave"}));

    // Typed and Enter: said in MCO's room, the box emptied, the keys still in it.
    MCTestGuest::RealClickOn(*say);
    MCTestGuest::RealType("On my way");
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(SettleUntil([&] { return Has(screen.Seen().RoomChat, "Rahman: On my way"); }));
    CHECK(SettleUntil(
        [&]
        {
            const std::vector<Json> lines = mco.MessagesIn(SolarisId);
            return !lines.empty() && lines.back().value("message", std::string()) == "On my way";
        }));
    CHECK_EQ(say->GetAttribute<Rml::String>("value", "unread"), std::string());
    CHECK(say->GetContext()->GetFocusElement() == say);
    Shot("mco internet 3 room");

    // LEAVE: the list again, out of MCO's room.
    screen.PressLeave();
    REQUIRE(SettleUntil([&] { return !screen.Seen().InRoom; }));
    CHECK(!MCMcoRooms::Now().Seen().Room.has_value());
    CHECK_EQ(screen.Seen().Rows.size(), size_t{4});
    CHECK(OnInternet());
}

TEST_CASE_ISOLATED("game: a locked MechCommander Online room asks for its password, a wrong one is refused, and the "
                   "room closing under the player says so over the list")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":3})", asked), Service,
                                      MCInternetSpots::Version);
    MCFakeMcoService::MCService mco;
    ThreeRooms(mco);
    const MCMcoScope mcoScope(MCFakeMcoService::Setup(mco, std::make_shared<MCFakeMcoService::MCMemoryKeys>(), {},
                                                      MCFakeMcoService::Content, PollLeast, PollMost));
    REQUIRE(MCTestGame::StartLogistics());
    MCGamesScreen& screen = OpenWithRooms(list, 4);

    // JOIN GAME on the locked room: its window, the room named over PASSWORD.
    PickNamed(screen, "Bunker");
    screen.PressJoin();
    Settle();
    REQUIRE(screen.PasswordWindow());
    CHECK(screen.PasswordWindow()->Document().has_value());
    Shot("mco internet 4 password");

    // A wrong password: MCO's refusal said over the list, the player not in the room.
    screen.PasswordWindow()->SetPassword("knight");
    screen.PasswordWindow()->PressJoin();
    REQUIRE(SettleUntil([] { return DialogText() == "Wrong room password."; }));
    CHECK(!screen.Seen().InRoom);
    Shot("mco internet 5 wrong password");
    PressDialogOk();

    // The right one: in the room.
    screen.PressJoin();
    Settle();
    screen.PasswordWindow()->SetPassword(BunkerPassword);
    screen.PasswordWindow()->PressJoin();
    REQUIRE(SettleUntil([&] { return screen.Seen().InRoom; }));
    CHECK_EQ(screen.Seen().RoomLine, "Waiting for ANN to start.");

    // Its host leaves: the room closes, said over the list, which is back.
    mco.HostLeaves(BunkerId);
    REQUIRE(SettleUntil([] { return DialogText() == "ANN left. The room has closed."; }));
    CHECK(!screen.Seen().InRoom);
    Shot("mco internet 6 room closed");
    PressDialogOk();
    REQUIRE(SettleUntil([&] { return DialogText().empty(); }));
    CHECK(OnInternet());
}

TEST_CASE_ISOLATED("game: the host's START in a MechCommander Online room takes the game through the relay into the "
                   "match's session screen, and its host's game leaving comes back to the room")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":3})", asked), Service,
                                      MCInternetSpots::Version);
    MCFakeMcoService::MCService mco;
    ThreeRooms(mco);
    MCFakeMcoRelay::MCRelay relay;
    REQUIRE(relay.Port() != 0);
    const MCMcoScope mcoScope(RelaySetup(mco, relay));
    REQUIRE(MCTestGame::StartLogistics());
    MCGamesScreen& screen = OpenWithRooms(list, 4);
    screen.SetPlayerName("Rahman");
    PickNamed(screen, "Solaris Night");
    screen.PressJoin();
    REQUIRE(SettleUntil([&] { return screen.Seen().InRoom; }));
    MCBobsGame bobs(relay);

    // BOB's START: the game connects through the relay and opens the match's session screen, in the match as BOB's
    // room's players see it (its polls say its game is running).
    mco.StartMatch(SolarisId);
    REQUIRE(SettleUntil([] { return InRoom(); }));
    REQUIRE(MultiPlayer() != nullptr);
    CHECK(MultiPlayer()->SessionManager->CurrentSession() != nullptr);
    CHECK_EQ(MCMcoRooms::Now().Seen().Match, MCMcoMatchStage::Playing);
    CHECK(SettleUntil(
        [&] { return mco.LastFieldsFrom("room_poll", screen.PlayerName())["redux_process_running"] == "1"; }));
    CHECK_EQ(bobs.Game.Joined().size(), size_t{1});
    CHECK_EQ(RoomPlace(), "Internet");
    Shot("mco internet 7 match");

    // BOB's game leaving: back to INTERNET, in BOB's room, the match closed played.
    bobs.Game.Close();
    REQUIRE(SettleUntil([&] { return OnInternet() && screen.Seen().InRoom; }));
    CHECK(SettleUntil([&] { return MCMcoRooms::Now().Seen().Match == MCMcoMatchStage::None; }));
    CHECK_EQ(screen.Seen().RoomName, "Solaris Night");
    Shot("mco internet 8 back in room");
}

TEST_CASE_ISOLATED("game: a player removed from a MechCommander Online room is told who removed them over INTERNET's "
                   "list, in the room or in its match, in one dialog")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":3})", asked), Service,
                                      MCInternetSpots::Version);
    MCFakeMcoService::MCService mco;
    ThreeRooms(mco);
    MCFakeMcoRelay::MCRelay relay;
    REQUIRE(relay.Port() != 0);
    const MCMcoScope mcoScope(RelaySetup(mco, relay));
    REQUIRE(MCTestGame::StartLogistics());
    MCGamesScreen& screen = OpenWithRooms(list, 4);
    screen.SetPlayerName("Rahman");

    // In the room: BOB removes the player, said over the list, which is back.
    PickNamed(screen, "Solaris Night");
    screen.PressJoin();
    REQUIRE(SettleUntil([&] { return screen.Seen().InRoom; }));
    mco.Kick(SolarisId, "Rahman", "BOB");
    REQUIRE(SettleUntil([] { return DialogText() == "BOB removed you from the room."; }));
    CHECK(!screen.Seen().InRoom);
    Shot("mco internet 9 removed");
    PressDialogOk();
    REQUIRE(SettleUntil([&] { return DialogText().empty(); }));
    CHECK(OnInternet());
    CHECK_EQ(screen.Seen().Rows.size(), size_t{4});

    // Back in, and in BOB's match: removed there, the match ends and INTERNET's list is back, with that one line.
    screen.PressJoin();
    REQUIRE(SettleUntil([&] { return screen.Seen().InRoom; }));
    MCBobsGame bobs(relay);
    mco.StartMatch(SolarisId);
    REQUIRE(SettleUntil([] { return InRoom(); }));
    REQUIRE(SettleUntil([] { return MCMcoRooms::Now().Seen().Match == MCMcoMatchStage::Playing; }));
    mco.Kick(SolarisId, "Rahman", "BOB");
    REQUIRE(SettleUntil([] { return OnInternet() && !DialogText().empty(); }));
    CHECK_EQ(DialogText(), "BOB removed you from the room.");
    CHECK(!screen.Seen().InRoom);
    CHECK(MultiPlayer() == nullptr || MultiPlayer()->SessionManager->CurrentSession() == nullptr);
    PressDialogOk();
    REQUIRE(SettleUntil([&] { return DialogText().empty(); }));
    // No second dialog (the match's own ending) after it.
    Settle();
    CHECK(DialogText().empty());
    CHECK(OnInternet());
}

/// <summary>
/// SPEC-mco-hosting.md's "The switch": its hosts part is the host's MCO room's (off until the list says), its rooms part
/// INTERNET's, kept in the user folder for the next start.
/// </summary>
TEST_CASE("mco internet: the switch's hosts part turns hosting on MCO on or off, and its rooms part is kept for the "
          "next start")
{
    const MCSwitchRestore restore;
    MCMcoSwitch::Heard({.Rooms = false, .Hosts = true});
    CHECK(!MCMcoSwitch::RoomsOn());
    CHECK(MCMcoSeats::HostsOn());
    MCMcoSwitch::ReadAgainForTests();
    CHECK(!MCMcoSwitch::RoomsOn());

    MCMcoSwitch::Heard({.Rooms = true, .Hosts = false});
    CHECK(MCMcoSwitch::RoomsOn());
    CHECK(!MCMcoSeats::HostsOn());
    MCMcoSwitch::ReadAgainForTests();
    CHECK(MCMcoSwitch::RoomsOn());
}

/// <summary>
/// SPEC-mco-hosting.md's "The switch", in INTERNET: MCO's rooms show while its rooms part is on (on until the service has
/// said), hidden and out of MCO's lobby while it's off, the last said kept for the next start; INTERNET's games are hosted
/// on MCO only once the service has said its hosts part is on.
/// </summary>
TEST_CASE_ISOLATED(
    "game: INTERNET shows MechCommander Online's rooms only while the service's switch says so, and hosts "
    "there only once it has said")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCSwitchRestore restore;
    MCStandIns standIns;
    REQUIRE(MCTestGame::StartLogistics());

    // A list that says nothing of the switch: MCO's rooms show, and nothing is hosted there.
    MCGamesScreen& screen = OpenWithRooms(standIns.List, 4);
    CHECK(!MCMcoSeats::HostsOn());
    const auto withSwitch = [](bool rooms, bool hosts)
    {
        Json listing = BobsListing();
        listing["mco"] = {{"rooms", rooms}, {"hosts", hosts}};
        return listing;
    };

    // MCO's rooms off: the service's game alone; hosting on MCO on. Kept for the next start.
    standIns.List.Send(withSwitch(false, true));
    REQUIRE(SettleUntil([&] { return screen.Seen().Rows.size() == 1; }));
    CHECK((screen.Seen().Rows == Rows{"Bob's game | 2/6 | Europe | Open"}));
    CHECK(MCMcoSeats::HostsOn());
    MCMcoSwitch::ReadAgainForTests();
    CHECK(!MCMcoSwitch::RoomsOn());

    // On again: MCO's rooms back; hosting there off.
    standIns.List.Send(withSwitch(true, false));
    REQUIRE(SettleUntil([&] { return screen.Seen().Rows.size() == 4; }));
    CHECK(!MCMcoSeats::HostsOn());
}

/// <summary>
/// SPEC-mco-hosting.md's "On MCO": a game on both shows once, MCO's room of it left out as the service's list passes on
/// its id and name (a private game's too, unlisted); the same id under another name shows, so a game naming another's
/// room hides nothing.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET leaves out MechCommander Online's room of a game on the service, by its id and name "
                   "together")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCStandIns standIns;
    REQUIRE(MCTestGame::StartLogistics());
    MCGamesScreen& screen = OpenWithRooms(standIns.List, 4);

    // Solaris Night's id and name: a game of the service's (private, so not listed); Bunker's id under another name.
    Json listing = BobsListing();
    listing["mcoRooms"] = Json::array(
        {Json{{"id", SolarisId}, {"name", "Solaris Night"}}, Json{{"id", BunkerId}, {"name", "Not the bunker"}}});
    standIns.List.Send(listing);
    REQUIRE(SettleUntil([&] { return screen.Seen().Rows.size() == 3; }));
    CHECK((screen.Seen().Rows ==
           Rows{"Bob's game | 2/6 | Europe | Open", "Bunker | 1/4 |  | Locked", "Night raid | 3/6 |  | In battle"}));

    // That game gone from the service: MCO's room shows again.
    standIns.List.Send(BobsListing());
    REQUIRE(SettleUntil([&] { return screen.Seen().Rows.size() == 4; }));
}
