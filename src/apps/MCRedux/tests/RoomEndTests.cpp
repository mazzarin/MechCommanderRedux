#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiEvent.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
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

    /// <summary>A clock the tests move on by hand.</summary>
    class ManualClock
    {
    public:
        ManualClock()
        {
            MCRoomEnd::SetClock([this] { return _Now; });
        }

        ~ManualClock() { MCRoomEnd::SetClock(nullptr); }

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
/// The host's EXIT from a LAN room: it tells the guests whose builds take its word that the room closes, and leaves once
/// they have (an older build's guest is told nothing, and loses the session as before); a guest that doesn't leave is
/// waited for only a moment.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN host's EXIT tells its guests the room closes, and leaves once they have")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28952";
    const std::string address = std::format("127.0.0.1:{}", Port);
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    HostFromLan(Port);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    Guest ann("ANN", MCMatchRule::Supported);
    Guest cal("CAL", MCMatchRule::Supported & ~MCMatchRule::Rematch);
    REQUIRE(ann.Join(*manager.DirectPlay, address));
    REQUIRE(cal.Join(*manager.DirectPlay, address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && MCTestGuest::RoomShows(cal.Id()); }));
    MCSessionScreen& room = *GlobalLog()->SessionScreen;

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
    ClickButton(*GlobalLog()->SessionScreen->ExitButton);
    REQUIRE(SettleUntil([&] { return !dan.RoomEnds().empty(); }));
    Settle();
    CHECK(OnScreen(GlobalLog()->SessionScreen));
    clock.Advance(MCRoomEnd::LeaveWait);
    REQUIRE(SettleUntil([] { return OnScreen(GlobalLog()->LanScreen); }));
    CHECK(!InSession());
}
