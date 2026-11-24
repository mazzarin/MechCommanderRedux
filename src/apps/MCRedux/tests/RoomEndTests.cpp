#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiPort.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "mission/MCRematchLetters.h"
#include "mission/MCScenario.h"
#include "network/MCGameDataHash.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"

// Port-only (Redux HD): where a game the menus took to its room goes when the room ends (network/MCRoomEnd.h): back to
// the games screen it came from, saying why. The game's side is the test game; the others' are the tests' players
// (tests/MCTestGuest.h) on the loopback. --shots saves each step's screen.

using namespace MCScreenInput;
using MCTestGuest::DialogText;
using MCTestGuest::Guest;
using MCTestGuest::Host;
using MCTestGuest::InSession;
using MCTestGuest::JoinFromLan;
using MCTestGuest::OnLan;
using MCTestGuest::OnScreen;
using MCTestGuest::PressDialogOk;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;

namespace
{
    /// <summary>The LAN screen's game name and players entries (lanscreen.fit's elements).</summary>
    constexpr int32_t LanGameName = 10;
    constexpr int32_t LanPlayers = 11;

    /// <summary>Clicks the middle of <paramref name="button"/>, as a player does, and lets the game settle.</summary>
    void ClickButton(MCGuiObject& button)
    {
        Click(button.GlobalX() + button.Width() / 2, button.GlobalY() + button.Height() / 2);
        Settle();
    }

    /// <summary>From the LAN games, hosts a room on <paramref name="port"/> as COMMANDER: the room opens.</summary>
    void HostFromLan(std::string_view port)
    {
        MCTestGuest::HostFromLan(std::format("127.0.0.1:{}", port), "COMMANDER", "COMMANDER'S GAME", 4);
        Settle();
        REQUIRE(OnScreen(GlobalLog()->SessionScreen));
    }

    /// <summary>A clock the tests move on by hand: the room's end's waits, and the lobby updates' (a guest's rejoin).</summary>
    class ManualClock
    {
    public:
        ManualClock()
        {
            MCRoomEnd::SetClock([this] { return _Now; });
            MCLobbyUpdate::SetClock([this] { return _Now; });
        }

        ~ManualClock()
        {
            MCRoomEnd::SetClock(nullptr);
            MCLobbyUpdate::SetClock(nullptr);
        }

        ManualClock(const ManualClock&) = delete;
        ManualClock& operator=(const ManualClock&) = delete;

        void Advance(std::chrono::milliseconds by) { _Now += by; }

    private:
        std::chrono::steady_clock::time_point _Now = std::chrono::steady_clock::now();
    };
}

/// <summary>
/// A guest the LAN games took to its room, whose host's game goes (whatever the cause, nobody taking over): back to the
/// LAN games, never the main menu, saying why over them: the host's game lost ("Lost the connection to BOB's game."),
/// or closed by its host, who said so as it left ("BOB closed the game.").
/// </summary>
TEST_CASE_ISOLATED("game: a LAN guest whose room ends goes back to the LAN games, saying why")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28951";
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();

    // BOB's game goes without a word: its connection lost.
    auto bob = std::make_unique<Host>();
    REQUIRE(bob->Open(Port, "BOB", "BOB'S GAME"));
    JoinFromLan(Port, "ANN");
    bob->Close();
    bob.reset();
    REQUIRE(SettleUntil([] { return OnLan(); }));
    Settle();
    CHECK(!InSession());
    CHECK_EQ(DialogText(), std::string("Lost the connection to BOB's game."));
    SaveShot("room end 0 host lost", ScreenHash());
    PressDialogOk();
    Settle();
    CHECK(DialogText().empty());
    CHECK(OnLan());

    // BOB hosts again, and closes his room, saying so first: this game leaves on his word.
    Host again;
    REQUIRE(again.Open(Port, "BOB", "BOB'S GAME"));
    JoinFromLan(Port, "ANN");
    again.SendRoomEnds(MultiPlayer()->SessionManager->MyPlayerID, std::to_underlying(MCRoomEnd::MCHow::Closed));
    REQUIRE(SettleUntil([] { return !InSession(); }));
    Settle();
    CHECK(OnLan());
    CHECK_EQ(DialogText(), std::string("BOB closed the game."));
    SaveShot("room end 1 host closed", ScreenHash());
    again.Close();
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 2): a launcher's guest, whose room is the stack's end, whose host closes the room
/// saying so: the game's last message says why ("BOB closed the game.", over the remade room), its OK ending the game
/// as the original's did.
/// </summary>
TEST_CASE_ISOLATED("game: a launcher's guest whose host closes the room says so as the game ends")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28958";
    Host bob;
    REQUIRE(bob.Open(Port, "BOB", "BOB'S GAME"));
    REQUIRE(MCTestGame::StartLobby(std::format("-join 127.0.0.1 -port {} -player ANN", Port)));
    REQUIRE(SettleUntil([] { return InSession() && OnScreen(GlobalLog()->SessionScreen); }));

    bob.SendRoomEnds(MultiPlayer()->SessionManager->MyPlayerID, std::to_underlying(MCRoomEnd::MCHow::Closed));
    REQUIRE(SettleUntil([] { return !InSession(); }));
    Settle();
    CHECK_EQ(DialogText(), std::string("BOB closed the game."));
    bob.Close();
}

/// <summary>
/// A LAN guest whose host starts a mission its game can't load (a hostile host, or a start without the file check):
/// the guest leaves the room for the LAN games, saying so ("COULDN'T LOAD THE HOST'S MISSION."), where it went on into
/// the mech bay with nothing loaded and the scenario's start quit the game (an empty mission's name, a Fatal); and a
/// scenario's start with no mission loaded is refused, the guest staying in the room.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN guest whose host starts a mission it can't load leaves the room, saying so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28956";
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    Host bob;
    REQUIRE(bob.Open(Port, "BOB", "BOB'S GAME"));
    JoinFromLan(Port, "ANN");
    const uint32_t ann = MultiPlayer()->SessionManager->MyPlayerID;

    bob.SendPrepareScenario(ann);
    Settle();
    CHECK(InSession());
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    CHECK(Scenario() == nullptr);

    // BOB puts ANN on the first team.
    bob.SendJoinTeam(ann, ann, 1, 0);
    Settle();

    {
        // The game logs the file it can't open as it refuses it.
        const MCTest::ExpectAssert unopened("campaign file NOT Valid", 1);
        bob.SendStart(ann, "NOSUCHMISSION");
        REQUIRE(SettleUntil([] { return !InSession(); }));
    }

    Settle();
    CHECK(OnLan());
    CHECK_EQ(DialogText(), std::string("Couldn't load the host's mission."));
    SaveShot("room end 2 mission unloaded", ScreenHash());
    bob.Close();
}

/// <summary>
/// The host's EXIT from a LAN room: a guest's game takes the room over (MCDirectPlay::HasSuccessor), so the host leaves
/// at once, telling no one. Where no game can take it over (here the host's leave set not to hand on), it tells the
/// guests whose builds take its word that the room closes, and leaves once they have (an older build's guest is told
/// nothing, and loses the session as before); a guest that doesn't leave is waited for only a moment.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN host's EXIT tells its guests the room closes, and leaves once they have")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view RollingPort = "28955";
    constexpr std::string_view Port = "28952";
    const std::string address = std::format("127.0.0.1:{}", Port);
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();

    // A room another game can take over: EXIT leaves it to them at once, the room told nothing.
    HostFromLan(RollingPort);
    Guest eve("EVE", MCMatchRule::Supported);
    REQUIRE(eve.Join(*MultiPlayer()->SessionManager->DirectPlay, std::format("127.0.0.1:{}", RollingPort)));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(eve.Id()); }));
    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil([] { return OnLan(); }));
    CHECK(!InSession());
    CHECK(eve.RoomEnds().empty());
    eve.Leave();

    // A room no game can take over.
    HostFromLan(Port);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    Guest ann("ANN", MCMatchRule::Supported);
    Guest cal("CAL", MCMatchRule::Supported & ~MCMatchRule::Rematch);
    REQUIRE(ann.Join(*manager.DirectPlay, address));
    REQUIRE(cal.Join(*manager.DirectPlay, address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && MCTestGuest::RoomShows(cal.Id()); }));
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    manager.HandsOn = false;

    // EXIT: the room is told (CAL's older game ignores the word); the room waits for ANN to go, not for CAL.
    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil([&] { return !ann.RoomEnds().empty(); }));
    CHECK(ann.RoomEnds() == std::vector<int32_t>{std::to_underlying(MCRoomEnd::MCHow::Closed)});
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    CHECK(InSession());
    CHECK(room.ExitButton->Disabled);

    // She leaves: the host goes back to the LAN games.
    ann.Leave();
    REQUIRE(SettleUntil([] { return OnLan(); }));
    CHECK(!InSession());
    CHECK(DialogText().empty());

    // Hosted again with a guest who never leaves: the host goes after its moment's wait.
    ManualClock clock;
    HostFromLan(Port);
    Guest dan("DAN", MCMatchRule::Supported);
    REQUIRE(dan.Join(*MultiPlayer()->SessionManager->DirectPlay, address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(dan.Id()); }));
    MultiPlayer()->SessionManager->HandsOn = false;
    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil([&] { return !dan.RoomEnds().empty(); }));
    Settle();
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    clock.Advance(MCRoomEnd::LeaveWait);
    REQUIRE(SettleUntil([] { return OnLan(); }));
    CHECK(!InSession());
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 4): while the host's remade room closes and waits for its guests to leave, LEAVE,
/// Escape and BEGIN PLANNING are held, as the 1998 room's EXIT and START were. A second LEAVE would leave at once, its
/// guests losing the link instead of being told; BEGIN PLANNING would start a match in a closing room.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a LAN host's remade room holds LEAVE, Escape and BEGIN PLANNING while it waits for its guests")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28959";
    constexpr std::string_view Mission = "ARENA";
    const std::string address = std::format("127.0.0.1:{}", Port);
    // The wait's clock held, so the room waits for ANN however long the presses take.
    ManualClock clock;
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    HostFromLan(Port);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.HandsOn = false;
    const uint32_t sameData = MCMissionDataHash(GamePath(SavePath, Mission, ".mpk"));
    REQUIRE(sameData != 0);

    // ANN on team 2 with the mission's file: BEGIN PLANNING lit.
    Guest ann("ANN", MCMatchRule::Supported);
    REQUIRE(ann.Join(*manager.DirectPlay, address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && ann.SessionScreens() == 1; }));
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, ann.Id()));
    ann.AnswerFiles(sameData);
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    room.AssignPlayer(manager.MyPlayerID, 1, 0, false);
    room.AssignPlayer(ann.Id(), 2, 0, false);
    room.LoadMission(Mission);
    REQUIRE(SettleUntil([&] { return ann.MissionsLoaded() == 1 && DialogText().empty(); }));
    Settle();
    const MCGuiHandle<MCRoomScreen> remade = RoomScreen();
    REQUIRE(remade);
    REQUIRE(remade->Seen().CanBegin);
    CHECK(!remade->Seen().Closing);

    // LEAVE: ANN is told and the room waits for her, LEAVE and BEGIN PLANNING greyed.
    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil([&] { return !ann.RoomEnds().empty(); }));
    Settle();
    CHECK(MCRoomEnd::WaitsForGuests());
    CHECK(remade->Seen().Closing);
    CHECK(!remade->Seen().CanBegin);
    SaveShot("room end 3 closing", ScreenHash());

    // LEAVE again, Escape and BEGIN PLANNING, clicked and pressed: still the room, waiting for ANN, no match.
    MCTestGuest::PressInRoom("leave");
    RealKey(SDL_SCANCODE_ESCAPE);
    Settle();
    MCTestGuest::PressInRoom("begin");
    remade->PressLeave();
    remade->PressBegin();
    Settle();
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    CHECK(InSession());
    CHECK(!manager.SessionLocked());
    CHECK(MCRoomEnd::WaitsForGuests());
    CHECK(ann.RoomEnds() == std::vector<int32_t>{std::to_underlying(MCRoomEnd::MCHow::Closed)});

    // She leaves: the host goes back to the LAN games, nothing said.
    ann.Leave();
    REQUIRE(SettleUntil([] { return OnLan(); }));
    CHECK(!InSession());
    CHECK(DialogText().empty());
}

/// <summary>
/// The host's REMATCH (its results' button, here once the match's start has been taken): its guests are told, and once they have
/// left the same room is hosted again, a new session on the same port with the same name and size, the host alone in it
/// on its team, with the match's mission, points and tech bases, keeping the guests' places ("WAITING FOR ANN TO
/// REJOIN.", START asking); a guest returning goes back to its team and slot.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN host's REMATCH hosts its room again with the match's picks, and its guests return to "
                   "their teams")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28953";
    constexpr std::string_view Mission = "ARENA";
    constexpr int32_t Team1Points = 12000;
    constexpr int32_t Team2Points = 9000;
    const std::string address = std::format("127.0.0.1:{}", Port);
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    HostFromLan(Port);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    // The match's session, kept: the rematch's room is another (a host's session has no instance of its own here).
    const std::shared_ptr<MCFidpSession> firstRoom = manager.CurrentSession();
    const uint32_t sameData = MCMissionDataHash(GamePath(SavePath, Mission, ".mpk"));
    REQUIRE(sameData != 0);

    // ANN joins and is picked for team 2's second slot; the host takes team 1, Inner Sphere for team 2, a mission and
    // the points.
    Guest ann("ANN", MCMatchRule::Supported);
    REQUIRE(ann.Join(*manager.DirectPlay, address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && ann.SessionScreens() == 1; }));
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, ann.Id()));
    ann.AnswerFiles(sameData);
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    room.AssignPlayer(manager.MyPlayerID, 1, 0, false);
    room.AssignPlayer(ann.Id(), 2, 1, false);
    room.SetTeamTechBase(2, 1);
    room.LoadMission(Mission);
    REQUIRE(SettleUntil([&] { return ann.MissionsLoaded() == 1 && DialogText().empty(); }));
    room.SetTeam1RP(Team1Points);
    room.SetTeam2RP(Team2Points);
    Settle();
    REQUIRE(!room.StartButton->Disabled);

    // The match starts (its hook alone: the test's guest plays no match) and ends; the results' REMATCH: ANN's game is
    // told, and the host waits for it to leave.
    MCRoomEnd::MatchStarts();
    CHECK(MCRoomEnd::OffersRematch());
    MCRoomEnd::Rematch();
    REQUIRE(SettleUntil([&] { return !ann.RoomEnds().empty(); }));
    CHECK(ann.RoomEnds() == std::vector<int32_t>{std::to_underlying(MCRoomEnd::MCHow::Rematch)});
    CHECK(MCRoomEnd::Rematching());
    ann.Leave();

    // The room again: a new session, the host alone on team 1 with the match's picks, ANN's place kept.
    REQUIRE(SettleUntil(
        [&] { return OnScreen(GlobalLog()->SessionScreen) && InSession() && manager.CurrentSession() != firstRoom; }));
    Settle();
    MCSessionScreen& again = *GlobalLog()->SessionScreen;
    CHECK(!MCRoomEnd::Rematching());
    CHECK_EQ(manager.CurrentSession()->Name, std::string("COMMANDER'S GAME"));
    CHECK_EQ(manager.CurrentSession()->SessionDesc.dwMaxPlayers, 4u);
    CHECK_EQ(manager.GetPlayers(nullptr).size(), 1u);
    CHECK(MultiPlayer()->IsHost);
    CHECK(MCSessionScreen::Room().Team1Players[0] == manager.MyPlayerID);
    CHECK(!MCSessionScreen::Room().MissionFile.empty());
    CHECK_EQ(MCSessionScreen::Room().Team2TechBase, 1);
    CHECK_EQ(std::string(again.Team1RPText->Text()), std::to_string(Team1Points));
    CHECK_EQ(std::string(again.Team2RPText->Text()), std::to_string(Team2Points));
    CHECK_EQ(MCSessionScreen::Room().StartWaits, std::string("WAITING FOR ANN TO REJOIN."));
    CHECK_EQ(MCLobbyUpdate::StartQuestion(), std::string("ANN isn't back yet. Start without ANN?"));
    SaveShot("rematch 0 room again", ScreenHash());

    // ANN's game joins again: shown the room, back on team 2's second slot, her place no longer kept.
    Guest annAgain("ANN", MCMatchRule::Supported);
    REQUIRE(annAgain.Join(*manager.DirectPlay, address));
    REQUIRE(SettleUntil(
        [&]
        { return MCTestGuest::RoomShows(annAgain.Id()) && MCSessionScreen::Room().Team2Players[1] == annAgain.Id(); }));
    Settle();
    CHECK(MCSessionScreen::Room().StartWaits.empty());
    CHECK(MCLobbyUpdate::StartQuestion().empty());
    CHECK(MCSessionScreen::Room().Team1Players[0] == manager.MyPlayerID);
    SaveShot("rematch 1 guest back", ScreenHash());
}

/// <summary>
/// The host's REMATCH from the match's own results, where the mission has no logistics (it dropped it as the scenario
/// loaded): once its guests have left, the results close and the room is hosted again, keeping their places. The wait's
/// end asked for the logistics first and, finding none, dropped the rematch: the results stayed up with NEXT MISSION
/// alone and the room never came back (an internet game's kept place listed as a greyed room meanwhile; the 20 Nov
/// playtest, 2 of 2). A campaign mission's results, both sides lost, stand in for the match's.
/// </summary>
TEST_CASE_ISOLATED("game: a host's REMATCH from its match's results hosts the room again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28957";
    constexpr uint32_t BothSidesLost = 3;
    const std::string address = std::format("127.0.0.1:{}", Port);
    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::ReachFirstResults(BothSidesLost));
    REQUIRE(GlobalLog() == nullptr);

    // The match's room, as the LAN games host it (here once the results are up, with no screens to host from), and ANN
    // in it; the match under way since the room's start.
    auto host = std::make_shared<MCMultiPlayer>();
    MCGameContext::Current().SetMultiPlayer(host);
    REQUIRE_EQ(host->Start(), 0);
    MCSessionManager& manager = *host->SessionManager;
    manager.ConnectTcp(address);
    REQUIRE_EQ(host->CreateSession("COMMANDER'S GAME", "COMMANDER", 4), 0);
    REQUIRE(MCRoomEnd::GamesOf() == MCRoomEnd::MCGames::Lan);
    const std::shared_ptr<MCFidpSession> matchRoom = manager.CurrentSession();
    Guest ann("ANN", MCMatchRule::Supported);
    REQUIRE(ann.Join(*manager.DirectPlay, address));
    REQUIRE(SettleUntil([&] { return host->PlayerRules.contains(ann.Id()); }));
    MCRoomEnd::MatchStarts();
    host->InMission = true;
    IsMPlayerGame = true;

    // The results' REMATCH: ANN is told and leaves.
    REQUIRE(MCRoomEnd::OffersRematch());
    MCRoomEnd::Rematch();
    REQUIRE(SettleUntil([&] { return !ann.RoomEnds().empty(); }));
    CHECK(ann.RoomEnds() == std::vector<int32_t>{std::to_underlying(MCRoomEnd::MCHow::Rematch)});
    ann.Leave();

    // The results close, and the room is hosted again: a new session, the host alone in it, ANN's place kept.
    CHECK(SettleUntil([&] { return Mission()->State == MCMissionState::Logistics && GlobalLog() != nullptr; }));
    REQUIRE(SettleUntil(
        [&]
        {
            return GlobalLog() != nullptr && OnScreen(GlobalLog()->SessionScreen) && InSession() &&
                   manager.CurrentSession() != matchRoom;
        }));
    Settle();
    CHECK(!MCRoomEnd::Rematching());
    CHECK(Scenario() == nullptr);
    CHECK(MultiPlayer()->IsHost);
    CHECK_EQ(manager.CurrentSession()->Name, std::string("COMMANDER'S GAME"));
    CHECK_EQ(manager.GetPlayers(nullptr).size(), 1u);
    CHECK_EQ(GlobalLog()->Room.StartWaits, std::string("WAITING FOR ANN TO REJOIN."));
    SaveShot("rematch 3 room again from the results", ScreenHash());
    IsMPlayerGame = false;
}

/// <summary>
/// A guest whose host has a rematch: its game leaves the session at once, waits over the LAN games ("Rejoining BOB's game
/// for a rematch...") and joins the room the host hosts again; a room that doesn't come back in time is said over the
/// games.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN guest whose host has a rematch joins its room again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28954";
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    auto bob = std::make_unique<Host>();
    REQUIRE(bob->Open(Port, "BOB", "BOB'S GAME"));
    JoinFromLan(Port, "ANN");

    // BOB's REMATCH at the results (his room locked since the match started): this game leaves, and waits over the LAN
    // games.
    bob->Lock();
    bob->SendRoomEnds(MultiPlayer()->SessionManager->MyPlayerID, std::to_underlying(MCRoomEnd::MCHow::Rematch));
    REQUIRE(SettleUntil([] { return !InSession(); }));
    Settle();
    CHECK(OnLan());
    CHECK_EQ(DialogText(), std::string("Rejoining BOB's game for a rematch..."));
    SaveShot("rematch 2 guest waiting", ScreenHash());

    // BOB's game hosts the room again: this game joins it, straight into the room.
    bob->Close();
    bob.reset();
    Host again;
    REQUIRE(again.Open(Port, "BOB", "BOB'S GAME"));
    REQUIRE(SettleUntil([] { return InSession() && OnScreen(GlobalLog()->SessionScreen); }));
    CHECK(DialogText().empty());
    REQUIRE(SettleUntil([&] { return !again.Joined().empty(); }));
    CHECK_EQ(MultiPlayer()->SessionManager->MyPlayer()->Name, std::string("ANN"));
    CHECK(!MultiPlayer()->IsHost);

    // Another rematch, whose room never comes back: said over the LAN games once the wait is over.
    ManualClock clock;
    again.Lock();
    again.SendRoomEnds(MultiPlayer()->SessionManager->MyPlayerID, std::to_underlying(MCRoomEnd::MCHow::Rematch));
    REQUIRE(SettleUntil([] { return !InSession(); }));
    again.Close();
    Settle();
    CHECK_EQ(DialogText(), std::string("Rejoining BOB's game for a rematch..."));
    clock.Advance(MCLobbyUpdate::RejoinWait);
    REQUIRE(SettleUntil([] { return DialogText() == "Couldn't join the room again: it has closed."; }));
    CHECK(OnLan());
    CHECK(!InSession());

    // Its OK, which the waiting message had hidden, closes it with a click.
    PressDialogOk();
    CHECK(DialogText().empty());
}

/// <summary>
/// The results' REMATCH (mission/MCMissionResultsScreen.h) is NEXT MISSION's faces (mr_ms01 up, mr_ms02 held) lettered
/// REMATCH in the art's font (mission/MCRematchLetters.h): outside the lettering each face as it is; in it, where the
/// word doesn't reach, each row the face's own colour (nothing of NEXT MISSION or of another font left), and where it
/// does, the face's colour blended with the art's ink (its word's darkest pixel up, its brightest held) by the word's
/// coverage, to within half the grey ramp's widest step; the word on the art's word's rows, centred where it is.
/// </summary>
TEST_CASE_ISOLATED("game: REMATCH is NEXT MISSION's faces lettered REMATCH in the art's font, up and held")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The faces' lettering (inclusive: inside their bevels) and the column whose colour is each row's face.
    constexpr int32_t FaceColumn = 3;
    constexpr int32_t LetteringLeft = 4;
    constexpr int32_t LetteringRight = 0x67;
    constexpr int32_t LetteringTop = 2;
    constexpr int32_t LetteringBottom = 0xf;
    // The palette's grey ramp steps up to 20 at a time, and the faces' rows are tinted a little (4): a blend's nearest
    // grey is this near it on each channel.
    constexpr int32_t BlendTolerance = 14;
    REQUIRE(MCTestGame::StartLogistics());
    const MCRematchFaces faces = RematchFaces();

    for (const auto& [made, file, darkInk] :
         {std::tuple{faces.Up, "mr_ms01.tga", true}, std::tuple{faces.Down, "mr_ms02.tga", false}})
    {
        MCTest::Scope scope(file);
        REQUIRE(made != nullptr);
        MCTgaColors colors{};
        MCGuiPort art;
        REQUIRE(art.Init(file, colors) == 0);
        REQUIRE_EQ(made->Width(), art.Width());
        REQUIRE_EQ(made->Height(), art.Height());
        const int32_t width = art.Width();
        const auto pixel = [&](const MCGuiPort& port, int32_t x, int32_t y)
        { return port.Pixels()[static_cast<size_t>(y * width + x)]; };
        const auto brightness = [&](uint8_t index) { return colors[index].r + colors[index].g + colors[index].b; };
        const auto lettering = [&](int32_t x, int32_t y)
        { return x >= LetteringLeft && x <= LetteringRight && y >= LetteringTop && y <= LetteringBottom; };

        // The art's ink: its word's darkest or brightest pixel.
        uint8_t ink = pixel(art, FaceColumn, LetteringTop);

        for (int32_t y = LetteringTop; y <= LetteringBottom; ++y)
        {
            for (int32_t x = LetteringLeft; x <= LetteringRight; ++x)
            {
                const uint8_t at = pixel(art, x, y);
                ink = (darkInk ? brightness(at) < brightness(ink) : brightness(at) > brightness(ink)) ? at : ink;
            }
        }

        // Every pixel against the face and the word's coverage there, and where each word is inked over half (nearer
        // the ink than its row's face): its rows and its columns' ends.
        int32_t faceChanged = 0;
        int32_t notWiped = 0;
        int32_t offBlend = 0;
        std::array<MCRect, 2> inked{};
        inked.fill(MCRect{width, art.Height(), -1, -1});
        const auto markInked = [&](const MCGuiPort& port, int32_t x, int32_t y, MCRect& box)
        {
            const int32_t at = brightness(pixel(port, x, y));

            if (std::abs(at - brightness(ink)) < std::abs(at - brightness(pixel(art, FaceColumn, y))))
            {
                box = MCRect{std::min(box.X0, x), std::min(box.Y0, y), std::max(box.X1, x), std::max(box.Y1, y)};
            }
        };

        for (int32_t y = 0; y < art.Height(); ++y)
        {
            const SDL_Color face = colors[pixel(art, FaceColumn, y)];

            for (int32_t x = 0; x < width; ++x)
            {
                const uint8_t shown = pixel(*made, x, y);

                if (!lettering(x, y))
                {
                    faceChanged += shown == pixel(art, x, y) ? 0 : 1;
                    continue;
                }

                markInked(art, x, y, inked[0]);
                markInked(*made, x, y, inked[1]);

                const int32_t column = x - MCRematchLetters::Left;
                const int32_t row = y - MCRematchLetters::Top;
                const int32_t coverage =
                    column >= 0 && column < MCRematchLetters::Width && row >= 0 && row < MCRematchLetters::Rows
                        ? MCRematchLetters::Coverage[static_cast<size_t>(row)][static_cast<size_t>(column)]
                        : 0;

                if (coverage == 0)
                {
                    notWiped += shown == pixel(art, FaceColumn, y) ? 0 : 1;
                    continue;
                }

                const SDL_Color& inkColor = colors[ink];
                const SDL_Color& seen = colors[shown];
                const auto off = [&](uint8_t faceChannel, uint8_t inkChannel, uint8_t seenChannel)
                {
                    const int32_t blend = (faceChannel * (MCRematchLetters::Full - coverage) + inkChannel * coverage +
                                           MCRematchLetters::Full / 2) /
                                          MCRematchLetters::Full;
                    return std::abs(seenChannel - blend) > BlendTolerance;
                };

                offBlend += off(face.r, inkColor.r, seen.r) || off(face.g, inkColor.g, seen.g) ||
                                    off(face.b, inkColor.b, seen.b)
                                ? 1
                                : 0;
            }
        }

        std::cout << std::format("  {}: the art's word inked over columns {}-{}, rows {}-{}; REMATCH's {}-{}, {}-{}\n",
                                 file, inked[0].X0, inked[0].X1, inked[0].Y0, inked[0].Y1, inked[1].X0, inked[1].X1,
                                 inked[1].Y0, inked[1].Y1);
        CHECK_EQ(faceChanged, 0);
        CHECK_EQ(notWiped, 0);
        CHECK_EQ(offBlend, 0);
        CHECK_EQ(inked[1].Y0, inked[0].Y0);
        CHECK_EQ(inked[1].Y1, inked[0].Y1);
        // Centred: the columns' ends' sums (twice the centres) a column apart at most.
        CHECK(std::abs((inked[1].X0 + inked[1].X1) - (inked[0].X0 + inked[0].X1)) <= 1);
    }
}
