#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "MCFakeUpdaterBackend.h"
#include "fakes/MCFakeGamesService.h"
#include "fakes/MCServiceScript.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCIdleCheck.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "gui/MCGuiStartup.h"
#include "logistics/MCLogTextButton.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCRejoinNote.h"
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

    /// <summary>The list's lines from <paramref name="first"/> on, one space between: its text however it wrapped.</summary>
    std::string TextFrom(MCLogScrollTextObject& list, int32_t first)
    {
        std::string text;

        for (int32_t line = first; line < list.NumLines; line++)
        {
            text += (text.empty() ? "" : " ") + LineOf(list, line);
        }

        return text;
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

    /// <summary>The list's socket scripted and the games' sockets to <paramref name="service"/>, for the test's time.</summary>
    class MCServicesScope
    {
    public:
        MCServicesScope(MCServiceScript::ListScript& list, MCFakeGames::MCFakeGamesService& service)
            : _Scope([toList = MCServiceScript::ListOpener(list), toGames = service.Opener(),
                      listUrl = std::format("{}{}", Service, ListPath)](const std::string& url)
                     { return url.starts_with(listUrl) ? toList(url) : toGames(url); },
                     MCServiceScript::Answering(R"({"online":1})", _Asked), Service, Version)
        {
        }

    private:
        std::string _Asked;
        MCServiceScript::SetupScope _Scope;
    };

    /// <summary>Opens INTERNET CONNECTION, its list answered with no games, the player's name set.</summary>
    MCSplashScreen& OpenInternetScreen(MCServiceScript::ListScript& list)
    {
        ConnectScreen();
        Settle();
        Click(InternetX, InternetY);
        Settle();
        REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
        list.Send(Listing(Json::array(), 1, 0));
        REQUIRE(SettleUntil([] { return InternetCountsShown() == "1 ONLINE, 0 IN GAMES"; }));
        MCSplashScreen& screen = *GlobalLog()->InternetScreen;
        screen.Element<MCLogTextObject>(PlayerNameEntry)->SetStringBuffer("Commander");
        return screen;
    }

    /// <summary>HOST GAME, then CREATE GAME: the room. The game's name.</summary>
    std::string HostFrom(MCSplashScreen& screen)
    {
        Click(HostX, HostY);
        Settle();
        std::string gameName = ElementText(screen, GameNameEntry);
        screen.Element<MCLogTextObject>(PlayersEntry)->SetStringBuffer("4");
        ClickButton(*screen.Element<MCLogButton>(CreateButton));
        REQUIRE(SettleUntil([] { return GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen; }));
        return gameName;
    }

    /// <summary>The update the restart tests find, and the build it brings.</summary>
    constexpr std::string_view NewVersion = "0.6.1 (830)";
    constexpr uint64_t NewBuild = 830;

    /// <summary>An update found, downloaded and ready to install.</summary>
    void UpdateReady()
    {
        MCUpdater::Found(std::string(NewVersion), "Rooms come back after an update.");
        MCUpdater::Answer(MCUpdater::MCAnswer::Install);
        MCUpdater::Ready();
        Settle();
    }

    /// <summary>The rejoin note a host of "Commander's game" (Commander, 4 players, public) leaves as it restarts.</summary>
    std::string HostNote(std::string_view gameName)
    {
        return MCRejoinNote::InternetRoomCommandLine({.Hosting = true,
                                                      .Code = std::string(MCFakeGames::GameCode),
                                                      .Token = std::string(MCFakeGames::GameToken),
                                                      .Public = true,
                                                      .PlayerName = "Commander",
                                                      .SessionName = std::string(gameName),
                                                      .MaxPlayers = 4});
    }

    /// <summary>Boots the game as its new version does after a restart: the note's words, as the app takes them.</summary>
    bool StartFromNote(std::string_view note)
    {
        const std::filesystem::path user = MCTestGame::UserFolder();
        std::filesystem::create_directories(user);
        REQUIRE(MCRejoinNote::Write(
            user, {.CommandLine = std::string(note), .Build = NewBuild, .Time = MCRejoinNote::Now()}));
        const std::optional<std::string> line = MCRejoinNote::TakeAtStart(user, NewBuild, MCRejoinNote::Now());
        REQUIRE(line.has_value());
        return MCTestGame::StartLobby(*line);
    }

    /// <summary>The player moves the mouse: input, which the idle check sees.</summary>
    void Nudge()
    {
        constexpr int32_t SpotX = 100;
        constexpr int32_t SpotY = 100;
        SendMouse(MCGuiEventType::MouseMove, SpotX, SpotY, false);
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
    CHECK(TextFrom(*players, 3) == "HOSTED IN EUROPE");
    CHECK(!join->Disabled);
    SaveShot("internet 2 picked", ScreenHash());

    // One that can't be joined says why: full, in battle (its mission), a newer version.
    games->Pick("PXYL");
    Settle();
    CHECK(join->Disabled);
    games->Pick("Q4RT");
    Settle();
    CHECK(join->Disabled);
    CHECK(TextFrom(*players, 3) == "IN BATTLE FOR 12 MIN, ON LOST OUTPOST HOSTED IN ASIA");
    games->Pick("W8ZE");
    Settle();
    CHECK(join->Disabled);
    CHECK(TextFrom(*players, 2) == "NEEDS VERSION 0.7.0 HOSTED IN OCEANIA");

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
    CHECK(LineOf(*games, 0).starts_with("NO GAMES ARE OPEN"));
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
    const MCServicesScope scope(list, service);
    REQUIRE(MCTestGame::StartLogistics());
    MCSplashScreen& screen = OpenInternetScreen(list);

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
    const std::string codeLine = std::format("FRIENDS CAN JOIN WITH CODE {}.", MCFakeGames::GameCode);
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
    CHECK(room.at("mission") == "");

    // A mission picked: the list shows it.
    MCSessionScreen& roomScreen = *GlobalLog()->SessionScreen;
    // (The host is alone, so the file check is done at once, its message closing itself.)
    roomScreen.LoadMission("ARENA");
    REQUIRE(SettleUntil([] { return DialogText().empty(); }));
    REQUIRE(!roomScreen.MissionName.empty());
    REQUIRE(SettleUntil([&] { return service.Room()->at("mission") != ""; }));
    CHECK(service.Room()->at("mission") ==
          MCServiceName(roomScreen.MissionName, MCServiceMissionMost, std::string_view()));
    SaveShot("internet 6 room", ScreenHash());

    // The room's EXIT: the internet games' screen again, the game and its socket gone.
    ClickButton(*roomScreen.ExitButton);
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
    REQUIRE(SettleUntil([] { return DialogText() == "THERE'S NO GAME WITH THAT CODE."; }));
    CHECK(service.Paths.back().starts_with(std::format("/join/{}", NoSuchCode)));
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    SaveShot("internet 7 no game", ScreenHash());
    ClickButton(*GlobalLog()->MessageDialog->OkButton);
    CHECK(DialogText().empty());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
}

/// <summary>
/// A player away from an internet game: INTERNET CONNECTION lets its list go (not counted online) until the next input;
/// the room asks "STILL THERE?" after 10 minutes without input, any input answers it, and a minute without one has the
/// player leave the room, as its EXIT does (the host's game ends), and says why.
/// </summary>
TEST_CASE_ISOLATED("game: a player away from an internet game is asked STILL THERE?, and leaves without an answer")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    const MCServicesScope scope(list, service);
    REQUIRE(MCTestGame::StartLogistics());
    auto now = std::chrono::steady_clock::now();
    MCIdleCheck::SetClock([&now] { return now; });
    MCSplashScreen& screen = OpenInternetScreen(list);
    auto games = screen.Element<MCInternetGameList>(GameList);

    // Away on the games: the list's socket goes, and comes back with the player.
    now += MCIdleCheck::AwayTime;
    REQUIRE(SettleUntil([&] { return list.ClosedByGame; }));
    CHECK(LineOf(*games, 0) == "LOOKING FOR GAMES...");
    CHECK(InternetCountsShown().empty());
    Nudge();
    REQUIRE(SettleUntil([&] { return list.Opened.size() == 2; }));
    list.Send(Listing(Json::array(), 1, 0));
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "1 ONLINE, 0 IN GAMES"; }));

    // In the room: asked after 10 minutes, and an answer keeps the player in.
    HostFrom(screen);
    now += MCIdleCheck::AwayTime - std::chrono::seconds(1);
    Settle();
    CHECK(DialogText().empty());
    now += std::chrono::seconds(1);
    REQUIRE(SettleUntil([] { return DialogText() == "ARE YOU STILL THERE?"; }));
    CHECK(MCIdleCheck::Asking());
    SaveShot("internet 8 still there", ScreenHash());
    now += MCIdleCheck::AnswerTime - std::chrono::seconds(1);
    Nudge();
    REQUIRE(SettleUntil([] { return DialogText().empty(); }));
    CHECK(!MCIdleCheck::Asking());
    now += MCIdleCheck::AnswerTime;
    Settle();
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);

    // Asked again, and no answer: the player leaves, the game ends on the service, and the games say why.
    now += MCIdleCheck::AwayTime;
    REQUIRE(SettleUntil([] { return DialogText() == "ARE YOU STILL THERE?"; }));
    now += MCIdleCheck::AnswerTime;
    REQUIRE(SettleUntil([] { return GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen; }));
    CHECK(DialogText() == "YOU WERE AWAY TOO LONG AND WERE TAKEN OUT OF THE GAME.");
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    MCIdleCheck::SetClock({});
}

/// <summary>
/// RESTART in an internet game's room the INTERNET screen opened: the host's update page offers it ("THE ROOM OPENS
/// AGAIN"), and taking it leaves the rejoin note to come back to the same game (its place's code and token, the menus'
/// again) and keeps the game's place on the service: its socket closes as a drop.
/// </summary>
TEST_CASE_ISOLATED("game: an internet room's host restarts into an update, keeping its game's place on the service")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestUpdater::MCFakeUpdater updater;
    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    const MCServicesScope scope(list, service);
    REQUIRE(MCTestGame::StartLogistics());
    MCSplashScreen& screen = OpenInternetScreen(list);
    const std::string gameName = HostFrom(screen);
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    REQUIRE(SettleUntil([&] { return service.Room().has_value(); }));

    // The update ready: the room's page offers RESTART, which brings the room back.
    UpdateReady();
    CHECK(MCLobbyUpdate::InRoom());
    ShowUpdatePageOver(room);
    Settle();
    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(room, "RESTART");
    REQUIRE(restart != nullptr);
    CHECK(restart->IsShowing());
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. RESTART TO INSTALL IT; YOUR ROOM WILL REOPEN."));
    SaveShot("internet 9 restart", ScreenHash());

    // RESTART: the note to come back by, the update installing, the game's place kept.
    ClickButton(*restart);
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
    CHECK(updater.Log->Answers.back() == MCUpdater::MCAnswer::Install);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(manager.Internet != nullptr);
    CHECK(manager.Internet->KeepsPlace());
    const std::optional<std::string> note =
        MCRejoinNote::TakeAtStart(MCTestGame::UserFolder(), NewBuild, MCRejoinNote::Now());
    REQUIRE(note.has_value());
    CHECK_EQ(*note, HostNote(gameName));

    // The game quits: its socket goes as a drop, and the service keeps the game for it.
    manager.EndInternet();
    CHECK(service.HostPlaceKept());
    CHECK_EQ(service.OpenLines(), 0u);
}

/// <summary>
/// A host's new version, started from the rejoin note: it comes back to its game's place on the service (the same code,
/// none hosted afresh), straight into the room, and is the menus' again: the room's EXIT goes back to the INTERNET
/// screen rather than ending the game.
/// </summary>
TEST_CASE_ISOLATED("game: a host's new version comes back to its internet game's place, and is the menus' again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    service.KeepHostPlace();
    const MCServicesScope scope(list, service);
    constexpr std::string_view GameName = "Commander's game";
    REQUIRE(StartFromNote(HostNote(GameName)));
    REQUIRE(SettleUntil([] { return GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen; }));
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    CHECK(manager.HostedInternetCode() == std::optional<std::string>(MCFakeGames::GameCode));
    CHECK(!service.HostPlaceKept());
    CHECK(std::ranges::none_of(service.Paths, [](const std::string& path) { return path.starts_with("/host?"); }));
    REQUIRE(SettleUntil([&] { return service.Room().has_value(); }));
    CHECK(service.Room()->at("name").get<std::string>() == GameName);
    CHECK(service.Room()->at("public") == true);
    CHECK(!LaunchedFromLobby);
    CHECK(!LobbyLaunchArgs.Active);
    SaveShot("internet 10 back", ScreenHash());

    ClickButton(*GlobalLog()->SessionScreen->ExitButton);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    CHECK(manager.Internet == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
}

/// <summary>A host's new version whose game's place has gone says so over the INTERNET screen, and the game goes on.</summary>
TEST_CASE_ISOLATED("game: a host's new version whose internet game's place has gone says so over the INTERNET screen")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    const MCServicesScope scope(list, service);
    REQUIRE(StartFromNote(HostNote("Commander's game")));
    REQUIRE(SettleUntil([] { return DialogText() == "Couldn't reopen your room."; }));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    CHECK(!LaunchedFromLobby);
    SaveShot("internet 11 place gone", ScreenHash());
    ClickButton(*GlobalLog()->MessageDialog->OkButton);
    CHECK(DialogText().empty());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
}

/// <summary>
/// A game on a newer version, picked: its players and "NEEDS 0.7.0", and once this game has found the update that
/// brings it, JOIN GAME updates first: the UPDATE page over the screen says the new version joins the game, and RESTART
/// leaves the rejoin note that has it join.
/// </summary>
TEST_CASE_ISOLATED("game: a newer game picked offers the update with JOIN GAME, and the new version joins it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestUpdater::MCFakeUpdater updater;
    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":2})", asked), Service, Version);
    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    Click(InternetX, InternetY);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->InternetScreen);
    MCSplashScreen& screen = *GlobalLog()->InternetScreen;
    screen.Element<MCLogTextObject>(PlayerNameEntry)->SetStringBuffer("Commander");
    list.Send(
        Listing({MCServiceScript::ListedGame("W8ZE", "Next version", {"Eve"}, 6, nullptr, NewerVersion, "OC")}, 2, 1));
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "2 ONLINE, 1 IN GAMES"; }));
    auto games = screen.Element<MCInternetGameList>(GameList);
    auto players = screen.Element<MCLogScrollTextObject>(PlayerList);
    auto join = screen.Element<MCLogButton>(JoinButton);

    // No update known: it needs one, and can't be joined.
    games->Pick("W8ZE");
    Settle();
    CHECK(join->Disabled);
    CHECK_EQ(LineOf(*players, 0), std::string("EVE"));
    CHECK_EQ(TextFrom(*players, 2), std::string("NEEDS VERSION 0.7.0 HOSTED IN OCEANIA"));

    // The update that brings it found: JOIN GAME updates first.
    MCUpdater::Found(std::string(NewerVersion), "Rooms come back after an update.");
    REQUIRE(SettleUntil([&] { return !join->Disabled; }));
    CHECK_EQ(TextFrom(*players, 2), std::string("PRESS JOIN GAME TO UPDATE AND JOIN. HOSTED IN OCEANIA"));
    SaveShot("internet 12 newer picked", ScreenHash());
    ClickButton(*join);
    REQUIRE(UpdatePageShown());
    MCUpdater::Answer(MCUpdater::MCAnswer::Install);
    MCUpdater::Ready();
    Settle();
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. RESTART TO INSTALL IT AND JOIN NEXT VERSION."));
    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(screen, "RESTART");
    REQUIRE(restart != nullptr);
    REQUIRE(restart->IsShowing());
    SaveShot("internet 13 update to join", ScreenHash());

    // RESTART: the update installs, and the new version joins the game, the menus' again once in.
    ClickButton(*restart);
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
    CHECK(updater.Log->Answers.back() == MCUpdater::MCAnswer::Install);
    constexpr uint64_t NextBuild = 900;
    const std::optional<std::string> note =
        MCRejoinNote::TakeAtStart(MCTestGame::UserFolder(), NextBuild, MCRejoinNote::Now());
    REQUIRE(note.has_value());
    CHECK_EQ(*note, std::string("-join W8ZE -internet -player Commander -menus"));
}
