#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCFakeGamesService.h"
#include "fakes/MCServiceScript.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"

// The multiplayer menu's internet screens (logistics/MCInternetMenu) in the test game, through a scripted games service
// (tests/fakes/MCServiceScript.h) and a stand-in for its games (tests/fakes/MCFakeGamesService.h): MULTIPLAYER ACCESS's
// two buttons and its count, and INTERNET CONNECTION's list, picks, CODE box and host block, a game hosted into its room
// and a code no game has. --shots saves each step's screen.

using namespace MCScreenInput;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;
using MCTestGuest::Step;
using Json = nlohmann::json;

namespace
{
    constexpr std::string_view Service = MCFakeGames::ServiceUrl;
    constexpr std::string_view ListPath = "/list";
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
    constexpr int32_t PlayerNameEntry = 4;
    constexpr int32_t JoinButton = 6;
    constexpr int32_t CreateButton = 9;
    constexpr int32_t GameNameEntry = 10;
    constexpr int32_t PlayersEntry = 11;

    /// <summary>A list's line <paramref name="line"/> (from 0), its text as the screen draws it.</summary>
    std::string LineOf(MCLogScrollTextObject& list, int32_t line)
    {
        return list.GetTextLine(line + 1).value_or("");
    }

    Json Listing(Json games, int online, int inGames)
    {
        return Json{{"t", "list"}, {"online", online}, {"inGames", inGames}, {"games", std::move(games)}};
    }

    /// <summary>Clicks the middle of <paramref name="button"/>, as a player does, and lets the game settle.</summary>
    void ClickButton(MCGuiObject& button)
    {
        Click(button.GlobalX() + button.Width() / 2, button.GlobalY() + button.Height() / 2);
        Settle();
    }

    /// <summary>Types <paramref name="text"/> into the entry that has the keys.</summary>
    void Type(std::string_view text)
    {
        for (const char c : text)
        {
            SendKey(static_cast<uint8_t>(c));
            Step();
        }

        Settle();
    }

    /// <summary>The message dialog's text, when it shows.</summary>
    std::string DialogText()
    {
        const MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        return dialog.ShowWindow ? dialog.Text : std::string();
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
    Type("k7qf");
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

/// <summary>
/// Hosting and joining from INTERNET CONNECTION, through a stand-in for the games service: a game hosted goes into its
/// room (the session screen), the service told the room and the players its code; the room's EXIT comes back to the
/// games; a code no game has is refused with a message, and nothing is left open.
/// </summary>
TEST_CASE_ISOLATED(
    "game: INTERNET CONNECTION hosts a game into its room, its EXIT comes back, and a wrong code is refused")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The list's socket scripted, the games' sockets to the stand-in service.
    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    MCWebSocketOpener toList = MCServiceScript::ListOpener(list);
    MCWebSocketOpener toGames = service.Opener();
    const std::string listUrl = std::format("{}{}", Service, ListPath);
    std::string asked;
    MCServiceScript::SetupScope scope([&](const std::string& url)
                                      { return url.starts_with(listUrl) ? toList(url) : toGames(url); },
                                      MCServiceScript::Answering(R"({"online":1})", asked), Service, Version);
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    Click(InternetX, InternetY);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    list.Send(Listing(Json::array(), 1, 0));
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "1 ONLINE, 0 IN GAMES"; }));
    MCSplashScreen& screen = *GlobalLog()->InternetScreen;
    screen.Element<MCLogTextObject>(PlayerNameEntry)->SetStringBuffer("Commander");

    // HOST GAME, then CREATE GAME: the room, the host alone in it, and the game's code in the chat.
    Click(HostX, HostY);
    Settle();
    const std::string gameName = ElementText(screen, GameNameEntry);
    CHECK(!gameName.empty());
    screen.Element<MCLogTextObject>(PlayersEntry)->SetStringBuffer("4");
    ClickButton(*screen.Element<MCLogButton>(CreateButton));
    REQUIRE(SettleUntil([] { return GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen; }));
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(manager.Internet != nullptr);
    CHECK(manager.HostedInternetCode() == std::optional<std::string>(MCFakeGames::GameCode));
    CHECK(DialogText().empty());
    const std::string codeLine = std::format("THIS GAME'S CODE: {}", MCFakeGames::GameCode);
    CHECK(std::ranges::any_of(GlobalLog()->Chat->Lines,
                              [&](const MCLogChat::HistoryLine& line) { return line.Text.contains(codeLine); }));

    // The service has the room as the list will show it: public (the host block's default), its name, its players.
    REQUIRE(SettleUntil([&] { return service.Room().has_value(); }));
    const Json room = *service.Room();
    CHECK(room.at("public") == true);
    CHECK(room.at("name") == gameName);
    CHECK(room.at("max") == 4);
    CHECK(room.at("players") == Json::array({"Commander"}));
    CHECK(room.at("battle") == false);
    SaveShot("internet 6 room", ScreenHash());

    // The room's EXIT: the internet games' screen again, the game and its socket gone.
    ClickButton(*GlobalLog()->SessionScreen->ExitButton);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    CHECK(screen.IsShowing());
    REQUIRE(MultiPlayer() != nullptr);
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK(MultiPlayer()->SessionManager->CurrentSession() == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    CHECK_EQ(ElementText(screen, PlayerNameEntry), "Commander");

    // A code no game has: JOIN GAME tries it, and the service's answer is said.
    constexpr std::string_view NoSuchCode = "M3VD";
    Click(CodeX, CodeY);
    Type(NoSuchCode);
    auto join = screen.Element<MCLogButton>(JoinButton);
    REQUIRE(!join->Disabled);
    ClickButton(*join);
    REQUIRE(SettleUntil([] { return DialogText() == "NO GAME WITH THAT CODE."; }));
    CHECK(service.Paths.back().starts_with(std::format("/join/{}", NoSuchCode)));
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    SaveShot("internet 7 no game", ScreenHash());
    ClickButton(*GlobalLog()->MessageDialog->OkButton);
    CHECK(DialogText().empty());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
}
