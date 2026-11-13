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
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
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
using MCTestGuest::Guest;
using MCTestGuest::Host;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;

namespace
{
    /// <summary>The LAN screen's player name entry and its game list (lanscreen.fit's elements).</summary>
    constexpr int32_t LanPlayerName = 4;
    constexpr int32_t LanGames = 2;
    constexpr int32_t LanGameName = 10;
    constexpr int32_t LanPlayers = 11;

    /// <summary>Clicks the middle of <paramref name="button"/>, as a player does, and lets the game settle.</summary>
    void ClickButton(MCGuiObject& button)
    {
        Click(button.GlobalX() + button.Width() / 2, button.GlobalY() + button.Height() / 2);
        Settle();
    }

    /// <summary>The message dialog's text, when it shows.</summary>
    std::string DialogText()
    {
        const MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        return dialog.ShowWindow ? dialog.Text : std::string();
    }

    /// <summary>Whether <paramref name="screen"/> is logistics' screen now.</summary>
    template <class Screen> bool OnScreen(const Screen& screen)
    {
        return GlobalLog()->CurrentScreen() == screen;
    }

    /// <summary>Whether this game is in a session.</summary>
    bool InSession()
    {
        return MultiPlayer() != nullptr && MultiPlayer()->SessionManager->CurrentSession() != nullptr;
    }

    /// <summary>
    /// From the LAN games, joins the room the test's host holds on <paramref name="port"/> as <paramref name="name"/>,
    /// as a player picking it from the list does: the room (the session screen) opens.
    /// </summary>
    void JoinFromLan(std::string_view port, std::string_view name)
    {
        REQUIRE(OnScreen(GlobalLog()->LanScreen));
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        manager.ConnectTcp(std::format("127.0.0.1:{}", port));
        MCSplashScreen& lan = *GlobalLog()->LanScreen;
        lan.Element<MCLogTextObject>(LanPlayerName)->SetStringBuffer(name);
        const auto games = lan.Element<MCGameList>(LanGames);

        // The list's refresh (its timer's), until the room is listed.
        REQUIRE(SettleUntil(
            [&]
            {
                MCGuiEvent refresh;
                refresh.Clear();
                refresh.Type = MCGuiEventType::Timer;
                games->HandleEvent(refresh);
                return games->NumSessions() > 0;
            }));
        games->SelectedSession = 0;
        games->SelectedGuid = games->Sessions.front();
        JoinGame();
        REQUIRE(SettleUntil([] { return OnScreen(GlobalLog()->SessionScreen) && InSession(); }));
        Settle();
    }

    /// <summary>From the LAN games, hosts a room on <paramref name="port"/> as COMMANDER: the room opens.</summary>
    void HostFromLan(std::string_view port)
    {
        REQUIRE(OnScreen(GlobalLog()->LanScreen));
        MultiPlayer()->SessionManager->ConnectTcp(std::format("127.0.0.1:{}", port));
        MCSplashScreen& lan = *GlobalLog()->LanScreen;
        lan.Element<MCLogTextObject>(LanPlayerName)->SetStringBuffer("COMMANDER");
        lan.Element<MCLogTextObject>(LanGameName)->SetStringBuffer("COMMANDER'S GAME");
        lan.Element<MCLogTextObject>(LanPlayers)->SetStringBuffer("4");
        CreateSession();
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
/// LAN games, never the main menu, saying why: the host's game lost ("LOST THE CONNECTION TO BOB'S GAME."), or closed
/// by its host, who said so as it left ("BOB CLOSED THE GAME.").
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
    REQUIRE(SettleUntil([] { return OnScreen(GlobalLog()->LanScreen); }));
    Settle();
    CHECK(!InSession());
    CHECK_EQ(DialogText(), std::string("LOST THE CONNECTION TO BOB'S GAME."));
    SaveShot("room end 0 host lost", ScreenHash());
    ClickButton(*GlobalLog()->MessageDialog->OkButton);
    Settle();
    CHECK(DialogText().empty());
    CHECK(OnScreen(GlobalLog()->LanScreen));

    // BOB hosts again, and closes his room, saying so first: this game leaves on his word.
    Host again;
    REQUIRE(again.Open(Port, "BOB", "BOB'S GAME"));
    JoinFromLan(Port, "ANN");
    again.SendRoomEnds(MultiPlayer()->SessionManager->MyPlayerID, std::to_underlying(MCRoomEnd::MCHow::Closed));
    REQUIRE(SettleUntil([] { return !InSession(); }));
    Settle();
    CHECK(OnScreen(GlobalLog()->LanScreen));
    CHECK_EQ(DialogText(), std::string("BOB CLOSED THE GAME."));
    SaveShot("room end 1 host closed", ScreenHash());
    again.Close();
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
    CHECK(OnScreen(GlobalLog()->LanScreen));
    CHECK_EQ(DialogText(), std::string("COULDN'T LOAD THE HOST'S MISSION."));
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
    ClickButton(*GlobalLog()->SessionScreen->ExitButton);
    REQUIRE(SettleUntil([] { return OnScreen(GlobalLog()->LanScreen); }));
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
    ClickButton(*room.ExitButton);
    REQUIRE(SettleUntil([&] { return !ann.RoomEnds().empty(); }));
    CHECK(ann.RoomEnds() == std::vector<int32_t>{std::to_underlying(MCRoomEnd::MCHow::Closed)});
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    CHECK(InSession());
    CHECK(room.ExitButton->Disabled);

    // She leaves: the host goes back to the LAN games.
    ann.Leave();
    REQUIRE(SettleUntil([] { return OnScreen(GlobalLog()->LanScreen); }));
    CHECK(!InSession());
    CHECK(DialogText().empty());

    // Hosted again with a guest who never leaves: the host goes after its moment's wait.
    ManualClock clock;
    HostFromLan(Port);
    Guest dan("DAN", MCMatchRule::Supported);
    REQUIRE(dan.Join(*MultiPlayer()->SessionManager->DirectPlay, address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(dan.Id()); }));
    MultiPlayer()->SessionManager->HandsOn = false;
    ClickButton(*GlobalLog()->SessionScreen->ExitButton);
    REQUIRE(SettleUntil([&] { return !dan.RoomEnds().empty(); }));
    Settle();
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    clock.Advance(MCRoomEnd::LeaveWait);
    REQUIRE(SettleUntil([] { return OnScreen(GlobalLog()->LanScreen); }));
    CHECK(!InSession());
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
    REQUIRE(SettleUntil([&] { return ann.MissionsLoaded() == 1 && !GlobalLog()->MessageDialog->ShowWindow; }));
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
    CHECK(again.Team1Players[0] == manager.MyPlayerID);
    CHECK(!again.MissionFile.empty());
    CHECK_EQ(again.Team2TechBase, 1);
    CHECK_EQ(std::string(again.Team1RPText->Text()), std::to_string(Team1Points));
    CHECK_EQ(std::string(again.Team2RPText->Text()), std::to_string(Team2Points));
    CHECK_EQ(again.StartWaits, std::string("WAITING FOR ANN TO REJOIN."));
    CHECK_EQ(MCLobbyUpdate::StartQuestion(), std::string("ANN ISN'T BACK YET. START WITHOUT ANN?"));
    SaveShot("rematch 0 room again", ScreenHash());

    // ANN's game joins again: shown the room, back on team 2's second slot, her place no longer kept.
    Guest annAgain("ANN", MCMatchRule::Supported);
    REQUIRE(annAgain.Join(*manager.DirectPlay, address));
    REQUIRE(
        SettleUntil([&] { return MCTestGuest::RoomShows(annAgain.Id()) && again.Team2Players[1] == annAgain.Id(); }));
    Settle();
    CHECK(again.StartWaits.empty());
    CHECK(MCLobbyUpdate::StartQuestion().empty());
    CHECK(again.Team1Players[0] == manager.MyPlayerID);
    SaveShot("rematch 1 guest back", ScreenHash());
}

/// <summary>
/// A guest whose host has a rematch: its game leaves the session at once, waits over the LAN games ("REJOINING BOB'S GAME
/// FOR A REMATCH...") and joins the room the host hosts again; a room that doesn't come back in time is said over the
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
    CHECK(OnScreen(GlobalLog()->LanScreen));
    CHECK_EQ(DialogText(), std::string("REJOINING BOB'S GAME FOR A REMATCH..."));
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
    CHECK_EQ(DialogText(), std::string("REJOINING BOB'S GAME FOR A REMATCH..."));
    clock.Advance(MCLobbyUpdate::RejoinWait);
    REQUIRE(SettleUntil([] { return DialogText() == "Couldn't join the room again: it has closed."; }));
    CHECK(OnScreen(GlobalLog()->LanScreen));
    CHECK(!InSession());
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
