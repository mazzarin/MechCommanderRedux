#include "stdafx.h"
#include "MCTest.h"
#include "MCInternetSpots.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "MCFakeUpdaterBackend.h"
#include "fakes/MCFakeGamesService.h"
#include "fakes/MCLoopbackTransport.h"
#include "fakes/MCServiceScript.h"
#include "linkup/MCFidpSession.h"
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
#include "network/MCRoomEnd.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCNetSwitch.h"

#include <thread>

// The multiplayer menu's internet screens (logistics/MCInternetMenu) in the test game, through a scripted games service
// (tests/fakes/MCServiceScript.h) and a stand-in for its games (tests/fakes/MCFakeGamesService.h): MULTIPLAYER ACCESS's
// two buttons and its count, and INTERNET CONNECTION's list, picks, CODE box and host block, a game hosted into its room
// and a code no game has. --shots saves each step's screen.

using namespace MCScreenInput;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;
using MCTestGuest::Step;
using namespace MCInternetSpots;
using Json = nlohmann::json;

namespace
{
    constexpr std::string_view Service = MCFakeGames::ServiceUrl;
    constexpr std::string_view ListPath = "/list";
    constexpr std::string_view CountUrl = "https://games.test/count";
    constexpr size_t CodeLength = 4;
    constexpr uint8_t Backspace = 8;

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

    /// <summary>How long a link on the loopback gets to open: one that can't (relays only, and none) fails soon.</summary>
    constexpr auto LoopbackOpen = std::chrono::milliseconds(1500);
    constexpr auto HostStep = std::chrono::milliseconds(5);

    /// <summary>The internet games' links on the loopback, the service never carrying one (the stand-in has no relays).</summary>
    void LinksOnLoopback()
    {
        MCInternetSetup setup = MCInternetGame::SetupNow();
        setup.BindAddress = "127.0.0.1";
        setup.Deadlines = {.Open = LoopbackOpen, .Service = std::nullopt};
        MCInternetGame::Setup(std::move(setup));
    }

    /// <summary>The game's port on its host (MCDirectPlay's), where a host's game listens.</summary>
    constexpr uint16_t GamePort = 28800;

    /// <summary>
    /// Bob's game, on another machine (its own network, here in the process), hosted on the stand-in service, private:
    /// none of its links relayed. It listens on the game's port, as its DirectPlay does (a host answers links then).
    /// </summary>
    struct MCBobsGame
    {
        MCNetSwitch Net{std::make_unique<MCLoopbackTransport>()};
        std::unique_ptr<MCInternetGame> Game = MCInternetGame::Host(Net, false);

        MCBobsGame()
        {
            REQUIRE(Game != nullptr);
            REQUIRE(Net.ListenTcp(GamePort) != MCSocket::InvalidHandle);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

            while (Game->Update() != MCInternetGame::Stage::Ready && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(HostStep);
            }

            REQUIRE(Game->Update() == MCInternetGame::Stage::Ready);
        }
    };

    /// <summary>Clicks the middle of <paramref name="button"/>, the game not yet run on.</summary>
    void Press(MCGuiObject& button)
    {
        Click(button.GlobalX() + button.Width() / 2, button.GlobalY() + button.Height() / 2);
    }

    /// <summary>
    /// JOIN GAME, run on until the join fails with <paramref name="failure"/>: whether this game's internet game was
    /// public (its links relayed) every frame it was there, and there at all.
    /// </summary>
    bool JoinsRelayedOnly(MCGuiObject& join, std::string_view failure)
    {
        Press(join);
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        bool there = false;
        bool relayed = true;
        REQUIRE(SettleUntil(
            [&]
            {
                if (manager.Internet != nullptr)
                {
                    there = true;
                    relayed = relayed && manager.Internet->Public();
                }

                return DialogText() == failure;
            }));
        ClickButton(*GlobalLog()->MessageDialog->OkButton);
        return there && relayed;
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
    list.Send(EveryKindOfGame(37, 12));
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
/// INTERNET CONNECTION's list longer than its view: scrolled to the bottom, a game picked there by a click on its row,
/// and the list's updates, keep it where it's scrolled (a pick only lights its row); a shorter list holds the scroll to
/// its new length, and one that fits shows its top with no scroll bar.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET CONNECTION's list stays where it's scrolled through a pick and the list's updates")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    std::string asked;
    MCServiceScript::SetupScope scope(MCServiceScript::ListOpener(list),
                                      MCServiceScript::Answering(R"({"online":1})", asked), Service, Version);
    REQUIRE(MCTestGame::StartLogistics());
    MCSplashScreen& screen = OpenInternetScreen(list);
    auto games = screen.Element<MCInternetGameList>(GameList);

    // Open games, each its own code ("GAM" and a letter of the codes' own).
    constexpr std::string_view CodeLetters = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    const auto codeOf = [&](int32_t game) { return std::format("GAM{}", CodeLetters[static_cast<size_t>(game)]); };
    const auto listing = [&](int32_t count, int online)
    {
        Json listed = Json::array();

        for (int32_t game = 0; game < count; game++)
        {
            listed.push_back(
                MCServiceScript::ListedGame(codeOf(game), std::format("Game {}", game), {"Bob"}, 6, nullptr, Version));
        }

        return Listing(std::move(listed), online, 0);
    };

    const auto countsAre = [](int online)
    { return [online] { return InternetCountsShown() == std::format("{} ONLINE, 0 IN GAMES", online); }; };

    // More games than the list shows at once.
    constexpr int32_t ManyGames = 30;
    list.Send(listing(ManyGames, ManyGames));
    REQUIRE(SettleUntil(countsAre(ManyGames)));
    REQUIRE(games->NumLines == ManyGames);
    REQUIRE(games->Lport()->Height() > games->Height());
    const int32_t rowHeight = games->Lport()->Height() / games->NumLines;
    CHECK(games->ScrollTab->ShowWindow);
    SaveShot("internet scroll 0 top", ScreenHash());

    // Scrolled to the bottom, a notch of the wheel a row.
    CHECK(games->MouseWheel(ManyGames, 0, 0));
    Settle();
    const int32_t bottom = games->Lport()->Height() - games->Height();
    REQUIRE(bottom > 0);
    REQUIRE(games->FirstPixel == bottom);
    const int32_t tabAtBottom = games->ScrollTab->Y();

    // The last game, below the fold until scrolled: a click on its row picks it, and the list stays put.
    const int32_t last = ManyGames - 1;
    Click(games->GlobalX() + games->Width() / 2,
          games->GlobalY() + last * rowHeight - games->FirstPixel + rowHeight / 2);
    Settle();
    REQUIRE(games->Picked().has_value());
    CHECK(games->Picked()->Code == codeOf(last));
    CHECK(games->HighlightLine[0] == last);
    CHECK(games->FirstPixel == bottom);
    CHECK(games->ScrollTab->Y() == tabAtBottom);
    CHECK(LineOf(*games, last) == std::format("GAME {}\t1/6", last));
    SaveShot("internet scroll 1 picked at the bottom", ScreenHash());

    // The list's updates keep it there, and the pick.
    list.Send(listing(ManyGames, ManyGames + 1));
    REQUIRE(SettleUntil(countsAre(ManyGames + 1)));
    CHECK(games->FirstPixel == bottom);
    CHECK(games->ScrollTab->Y() == tabAtBottom);
    CHECK(games->HighlightLine[0] == last);

    // Scrolled back a few rows, a pick there and an update keep that too.
    constexpr int32_t RowsBack = 4;
    CHECK(games->MouseWheel(-RowsBack, 0, 0));
    Settle();
    const int32_t between = bottom - RowsBack * rowHeight;
    REQUIRE(games->FirstPixel == between);
    games->Pick(codeOf(last - 1));
    Settle();
    CHECK(games->FirstPixel == between);
    CHECK(games->HighlightLine[0] == last - 1);
    list.Send(listing(ManyGames, ManyGames + 2));
    REQUIRE(SettleUntil(countsAre(ManyGames + 2)));
    CHECK(games->FirstPixel == between);

    // A shorter list: the scroll held to its new length (the picked game gone with it).
    constexpr int32_t FewerGames = ManyGames - 2 * RowsBack;
    list.Send(listing(FewerGames, FewerGames));
    REQUIRE(SettleUntil(countsAre(FewerGames)));
    REQUIRE(games->NumLines == FewerGames);
    CHECK(games->Lport()->Height() == std::max(FewerGames * rowHeight, games->Height()));
    CHECK(games->FirstPixel == std::max(games->Lport()->Height() - games->Height(), 0));
    CHECK(!games->Picked().has_value());
    CHECK(games->HighlightLine[0] == -1);

    // One that fits: its top, and no scroll bar.
    constexpr int32_t FewGames = 3;
    list.Send(listing(FewGames, FewGames));
    REQUIRE(SettleUntil(countsAre(FewGames)));
    CHECK(games->Lport()->Height() == games->Height());
    CHECK(games->FirstPixel == 0);
    CHECK(!games->ScrollTab->ShowWindow);
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

    // The host's network lists enough public games: the room's chat says this one isn't listed; it stays public.
    service.TellHost(R"({"t":"unlisted"})");
    const auto unlisted = [](const MCLogChat::HistoryLine& line) { return line.Text.contains("ISN'T LISTED"); };
    REQUIRE(SettleUntil([&] { return std::ranges::any_of(GlobalLog()->Chat->Lines, unlisted); }));
    CHECK(std::ranges::any_of(GlobalLog()->Chat->Lines,
                              [](const MCLogChat::HistoryLine& line)
                              {
                                  return line.Text.contains(
                                      "YOUR NETWORK ALREADY HAS 3 PUBLIC GAMES, SO THIS ONE ISN'T "
                                      "LISTED. PLAYERS CAN JOIN WITH ITS CODE.");
                              }));
    Settle();
    CHECK_EQ(std::ranges::count_if(GlobalLog()->Chat->Lines, unlisted), 1);
    CHECK(service.Room()->at("public") == true);

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

    // The player's network holds too many relays in live games: the join is refused, and the screen says why.
    service.RefuseJoinsWith = "crowded";
    ClickButton(*join);
    REQUIRE(SettleUntil([] { return DialogText() == "TOO MANY PLAYERS ON YOUR NETWORK ARE IN GAMES RIGHT NOW."; }));
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    ClickButton(*GlobalLog()->MessageDialog->OkButton);
    CHECK(DialogText().empty());
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
    // Picked from the service's list: public, so the new version's links take relays only.
    CHECK_EQ(*note, std::string("-join W8ZE -internet -public -player Commander -menus"));
}

/// <summary>
/// Port-only (Redux HD): an internet host's REMATCH (network/MCRoomEnd.h): its game keeps its place on the service
/// through the match's session's end and comes back to it (the same code, no game hosted afresh), and its room is
/// hosted again, a new session with the same name and the match's mission.
/// </summary>
TEST_CASE_ISOLATED("game: an internet host's REMATCH hosts its room again on the same code")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    const MCServicesScope scope(list, service);
    REQUIRE(MCTestGame::StartLogistics());
    MCSplashScreen& screen = OpenInternetScreen(list);
    const std::string gameName = HostFrom(screen);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(SettleUntil([&] { return service.Room().has_value(); }));
    GlobalLog()->SessionScreen->LoadMission("ARENA");
    REQUIRE(SettleUntil([] { return DialogText().empty(); }));
    // The match's session, kept: the rematch's room is another (a host's session has no instance of its own here).
    const std::shared_ptr<MCFidpSession> firstRoom = manager.CurrentSession();
    const auto hosted = [&service]
    {
        return std::ranges::count_if(service.Paths, [](const std::string& path) { return path.starts_with("/host?"); });
    };

    REQUIRE_EQ(hosted(), 1);

    // The match starts (its hook alone: START needs a player on each team), and the host's REMATCH at its results.
    MCRoomEnd::MatchStarts();
    MCRoomEnd::Rematch();
    REQUIRE(SettleUntil(
        [&]
        {
            return GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen && manager.CurrentSession() != nullptr &&
                   manager.CurrentSession() != firstRoom;
        }));
    Settle();
    CHECK(manager.HostedInternetCode() == std::optional<std::string>(MCFakeGames::GameCode));
    CHECK_EQ(hosted(), 1);
    CHECK(std::ranges::any_of(service.Paths, [](const std::string& path)
                              { return path.starts_with(std::format("/game/{}?", MCFakeGames::GameCode)); }));
    CHECK(!service.HostPlaceKept());
    CHECK_EQ(manager.CurrentSession()->Name, gameName);
    CHECK(!GlobalLog()->SessionScreen->MissionFile.empty());
    CHECK(DialogText().empty());
    SaveShot("internet 14 rematch", ScreenHash());
}

/// <summary>
/// A game joined from INTERNET CONNECTION's list is public: picked, or its code typed while the list shows it, its
/// links take relays only though the service says its host made it private (Bob's here, which isn't), so its host never
/// learns this player's address; the stand-in has no relays, so such a link can't open on the loopback. A code typed for
/// a game the list doesn't show joins as the service says, its links direct.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a game joined from INTERNET CONNECTION's list takes relays only, though its host says it's private")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    service.JoinedPublic = false;
    const MCServicesScope scope(list, service);
    LinksOnLoopback();
    REQUIRE(MCTestGame::StartLogistics());
    MCSplashScreen& screen = OpenInternetScreen(list);
    MCBobsGame bob;
    list.Send(EveryKindOfGame(37, 12));
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "37 ONLINE, 12 IN GAMES"; }));
    auto games = screen.Element<MCInternetGameList>(GameList);
    auto join = screen.Element<MCLogButton>(JoinButton);
    constexpr std::string_view NotReached = "COULDN'T CONNECT TO BOB'S GAME. TRY AGAIN.";

    // Picked from the list.
    games->Pick("K7QF");
    Settle();
    REQUIRE(!join->Disabled);
    CHECK(JoinsRelayedOnly(*join, NotReached));

    // Its code typed while the list shows it.
    Click(CodeX, CodeY);
    Type("k7qf");
    REQUIRE(!join->Disabled);
    CHECK(JoinsRelayedOnly(*join, NotReached));
    CHECK(!service.GuestSignals().empty());
    CHECK(!service.GuestOfferedOwnAddress());

    // The list no longer shows it: the typed code joins as the service says (private), its link direct and open.
    list.Send(Listing(Json::array(), 1, 0));
    REQUIRE(SettleUntil([] { return InternetCountsShown() == "1 ONLINE, 0 IN GAMES"; }));
    REQUIRE(!join->Disabled);
    Press(*join);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(SettleUntil(
        [&]
        {
            bob.Game->Update();
            return manager.Internet != nullptr && manager.Internet->Update() == MCInternetGame::Stage::Ready;
        }));
    CHECK(!manager.Internet->Public());
    CHECK(service.GuestOfferedOwnAddress());
    ClickButton(*GlobalLog()->MessageDialog->OkButton);
    CHECK(manager.Internet == nullptr);
}

/// <summary>
/// A public game's guest restarting into an update leaves the rejoin note with -public, and its new version, started
/// from the note, joins the game again through relays only, though the service now says the game is private.
/// </summary>
TEST_CASE_ISOLATED("game: a public game's guest, started again from the rejoin note, joins it through relays only")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    service.JoinedPublic = false;
    const MCServicesScope scope(list, service);
    LinksOnLoopback();
    const MCBobsGame bob;
    const std::string note = MCRejoinNote::InternetRoomCommandLine(
        {.Code = std::string(MCFakeGames::GameCode), .Public = true, .PlayerName = "Commander"});
    CHECK_EQ(note, std::format("-join {} -internet -public -player Commander -menus -rejoin", MCFakeGames::GameCode));
    REQUIRE(StartFromNote(note));
    CHECK(!LobbyLaunchArgs.IsHost);
    CHECK(LobbyLaunchArgs.Public);
    CHECK(std::ranges::any_of(service.Paths, [](const std::string& path)
                              { return path.starts_with(std::format("/join/{}?", MCFakeGames::GameCode)); }));
    CHECK(!service.GuestSignals().empty());
    CHECK(!service.GuestOfferedOwnAddress());
}
