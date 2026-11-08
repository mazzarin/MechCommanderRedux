#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCServiceScript.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"

// The multiplayer menu's internet screens (logistics/MCInternetMenu) in the test game, through a scripted games service
// (tests/fakes/MCServiceScript.h): MULTIPLAYER ACCESS's two buttons and its count, and INTERNET CONNECTION's list, picks,
// CODE box and host block. --shots saves each step's screen.

using namespace MCScreenInput;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;
using MCTestGuest::Step;
using Json = nlohmann::json;

namespace
{
    constexpr std::string_view Service = "wss://games.test";
    constexpr std::string_view Version = "0.6.0 (812)";
    constexpr std::string_view NewerVersion = "0.7.0 (900)";
    constexpr std::string_view CountUrl = "https://games.test/count";

    /// <summary>Where to click: the middles of MULTIPLAYER ACCESS's two buttons, and of INTERNET CONNECTION's.</summary>
    constexpr int32_t InternetX = 477;
    constexpr int32_t InternetY = 166;
    constexpr int32_t LanX = 477;
    constexpr int32_t LanY = 228;
    constexpr int32_t HostX = 558;
    constexpr int32_t HostY = 325;
    constexpr int32_t CancelX = 580;
    constexpr int32_t CancelY = 116;
    constexpr int32_t PrivacyX = 390;
    constexpr int32_t PrivacyY = 335;
    constexpr int32_t CodeX = 494;
    constexpr int32_t CodeY = 315;
    constexpr size_t CodeLength = 4;
    constexpr uint8_t Backspace = 8;

    /// <summary>INTERNET CONNECTION's elements (the LAN screen's).</summary>
    constexpr int32_t GameList = 2;
    constexpr int32_t PlayerList = 3;
    constexpr int32_t JoinButton = 6;

    /// <summary>A list's line <paramref name="line"/> (from 0), its text as the screen draws it.</summary>
    std::string LineOf(MCLogScrollTextObject& list, int32_t line)
    {
        return list.GetTextLine(line + 1).value_or("");
    }

    Json Listing(Json games, int online, int inGames)
    {
        return Json{{"t", "list"}, {"online", online}, {"inGames", inGames}, {"games", std::move(games)}};
    }
}

TEST_CASE_ISOLATED("game: MULTIPLAYER ACCESS offers INTERNET and LAN, and INTERNET lists the service's public games")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":37})", asked), Service, Version);
    REQUIRE(MCTestGame::StartLogistics());

    // MULTIPLAYER: INTERNET and LAN, the count under INTERNET once the service answers.
    ConnectScreen();
    REQUIRE(SettleUntil([] { return AccessCountShown() == "37 ONLINE"; }));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MultiplayerScreen);
    CHECK(asked == CountUrl);
    SaveShot("internet 0 access", ScreenHash());

    // LAN opens the LAN games, and their CANCEL comes back.
    Click(LanX, LanY);
    Settle();
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->LanScreen);
    CancelToConnect();
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->MultiplayerScreen);

    // INTERNET opens the service's list: looking for games until it answers.
    Click(InternetX, InternetY);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    REQUIRE(list.Opened.size() == 1);
    MCSplashScreen& screen = *GlobalLog()->InternetScreen;
    auto games = screen.Element<MCInternetGameList>(GameList);
    auto players = screen.Element<MCLogScrollTextObject>(PlayerList);
    auto join = screen.Element<MCLogButton>(JoinButton);
    CHECK(LineOf(*games, 0) == "LOOKING FOR GAMES...");
    CHECK(join->Disabled);

    // Its games, open ones first: one with room, a full one; then one in battle; and one on a newer version.
    list.Send(Listing(
        {MCServiceScript::ListedGame("K7QF", "Bob's game", {"Bob", "Ann"}, 6, nullptr, Version),
         MCServiceScript::ListedGame("PXYL", "Full house", {"A", "B", "C", "D"}, 4, nullptr, Version, "NA"),
         MCServiceScript::ListedGame("Q4RT", "Night raid", {"Cat", "Dan"}, 4, 720, Version, "AS", "Lost Outpost"),
         MCServiceScript::ListedGame("W8ZE", "Next version", {"Eve"}, 6, nullptr, NewerVersion, "OC")},
        37, 12));
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "37 ONLINE, 12 IN GAMES"; }));
    CHECK(LineOf(*games, 0) == "BOB'S GAME\t2/6");
    CHECK(LineOf(*games, 1) == "FULL HOUSE\tFULL");
    CHECK(LineOf(*games, 2) == "NIGHT RAID\t2/4");
    CHECK(LineOf(*games, 3) == "NEXT VERSION\t1/6");
    SaveShot("internet 1 list", ScreenHash());

    // A game picked: its players and where it is, and JOIN GAME takes it.
    games->Pick("K7QF");
    Settle();
    CHECK(LineOf(*players, 0) == "BOB");
    CHECK(LineOf(*players, 1) == "ANN");
    CHECK(LineOf(*players, 3) == "EUROPE");
    CHECK(!join->Disabled);
    SaveShot("internet 2 picked", ScreenHash());

    // One that can't be joined says why: full, in battle (its mission), a newer version.
    games->Pick("PXYL");
    Settle();
    CHECK(join->Disabled);
    games->Pick("Q4RT");
    Settle();
    CHECK(join->Disabled);
    CHECK(LineOf(*players, 4) == "IN BATTLE");
    CHECK(LineOf(*players, 5) == "12 MIN");
    // The mission, wrapped to the narrow list.
    CHECK(LineOf(*players, 6) + " " + LineOf(*players, 7) == "LOST OUTPOST");
    games->Pick("W8ZE");
    Settle();
    CHECK(join->Disabled);
    CHECK(LineOf(*players, 2) == "OCEANIA");
    CHECK(LineOf(*players, 3) == "NEEDS 0.7.0");

    // A code typed (either case): JOIN GAME takes it, whatever is picked.
    games->ClearPick();
    Settle();
    CHECK(join->Disabled);
    Click(CodeX, CodeY);

    for (const char c : std::string_view("k7qf"))
    {
        SendKey(static_cast<uint8_t>(c));
        Step();
    }

    Settle();
    CHECK(!join->Disabled);

    for (size_t i = 0; i < CodeLength; i++)
    {
        SendKey(Backspace);
        Step();
    }

    Settle();
    CHECK(join->Disabled);

    // HOST GAME: the game's name and players, PUBLIC (or PRIVATE, a click away).
    Click(HostX, HostY);
    Settle();
    CHECK(InternetHostsPublic());
    SaveShot("internet 3 host", ScreenHash());
    Click(PrivacyX, PrivacyY);
    Settle();
    CHECK(!InternetHostsPublic());
    SaveShot("internet 4 host private", ScreenHash());
    Click(PrivacyX, PrivacyY);
    Settle();
    CHECK(InternetHostsPublic());

    // CANCEL: back to the games, then to MULTIPLAYER ACCESS, the list's socket closed.
    Click(CancelX, CancelY);
    Settle();
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    Click(CancelX, CancelY);
    Settle();
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MultiplayerScreen);
    CHECK(list.ClosedByGame);

    // No open games: it says so, and when players are usually on.
    Click(InternetX, InternetY);
    Settle();
    REQUIRE(list.Opened.size() == 2);
    Json hours = Json::array();

    for (size_t hour = 0; hour < MCWeekHours; hour++)
    {
        constexpr size_t SaturdayEvening = 6 * 24 + 20;
        hours.push_back(hour == SaturdayEvening ? 30.0 : 1.0);
    }

    Json empty = Listing(Json::array(), 3, 0);
    empty["busiest"] = hours;
    list.Send(empty);
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "3 ONLINE, 0 IN GAMES"; }));
    CHECK(LineOf(*games, 0).starts_with("NO OPEN GAMES."));
    SaveShot("internet 5 empty", ScreenHash());
}
