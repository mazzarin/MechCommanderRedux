#include "stdafx.h"
#include "MCTest.h"
#include "MCInternetSpots.h"
#include "MCMenusPictures.h"
#include "MCTestGuest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "MCFakeUpdaterBackend.h"
#include "fakes/MCFakeGamesService.h"
#include "fakes/MCLoopbackTransport.h"
#include "fakes/MCServiceScript.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCIdleCheck.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCUpdateModel.h"
#include "logistics/MCUpdateScreen.h"
#include "gui/MCGuiStartup.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCRejoinNote.h"
#include "network/MCRoomEnd.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCNetSwitch.h"
#include "platform/MCUiSoftware.h"
#include "platform/MCUtf8Path.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

#include <thread>

// The multiplayer menu's internet games (SPEC-menus.md's stage 3: logistics/MCGamesScreen over network/MCGamesList) in
// the test game, through a scripted games service (tests/fakes/MCServiceScript.h) and a stand-in for its games
// (tests/fakes/MCFakeGamesService.h): MULTIPLAYER ACCESS's two buttons and its count (the 1998 screen's), and INTERNET's
// list, picks, CODE box and HOST GAME's window, played from the mouse and the keys; the list's states; a game hosted into
// its room, a join's wait called off, and the joins' refusals said over the list. --shots saves each step's screen.

using namespace MCScreenInput;
using MCTestGuest::DialogText;
using MCTestGuest::ElementOf;
using MCTestGuest::HostFrom;
using MCTestGuest::InRoom;
using MCTestGuest::OnAccess;
using MCTestGuest::OnInternet;
using MCTestGuest::OnLan;
using MCTestGuest::PressDialogCancel;
using MCTestGuest::PressDialogOk;
using MCTestGuest::RealClickOn;
using MCTestGuest::RealType;
using MCTestGuest::RowsOf;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;
using MCTestGuest::Step;
using namespace MCInternetSpots;
using namespace MCMenusPictures;
using Json = nlohmann::json;
using Rows = std::vector<std::string>;

namespace
{
    constexpr std::string_view Service = MCFakeGames::ServiceUrl;
    constexpr std::string_view CountUrl = "https://games.test/count";

    /// <summary>The rows of <see cref="MCInternetSpots::EveryKindOfGame"/> as INTERNET shows them.</summary>
    const Rows EveryKindOfRow = {
        "Bob's game | 2/6 | Europe | Open",
        "Full house | 4/4 | N. America | Full",
        "Night raid | 2/4 | Asia | In battle for 12 min",
        "Next version | 1/6 | Oceania | Needs version 0.7.0",
    };

    /// <summary>INTERNET's screen (made by its first opening).</summary>
    MCGamesScreen& Internet()
    {
        const MCGuiHandle<MCGamesScreen> screen = MCTestGuest::Games(MCGamesWhere::Internet);
        REQUIRE(screen);
        return *screen;
    }

    /// <summary>Whether INTERNET's counts' line says <paramref name="counts"/>.</summary>
    std::function<bool()> CountsAre(std::string counts)
    {
        return [counts = std::move(counts)] { return Internet().Seen().Counts == counts; };
    }

    /// <summary>INTERNET opened from MULTIPLAYER ACCESS, its list answered with no games, the player's name set.</summary>
    MCGamesScreen& OpenInternetScreen(MCServiceScript::ListScript& list)
    {
        MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
        Settle();
        MCTestGuest::PressInternet();
        REQUIRE(SettleUntil([] { return OnInternet(); }));
        list.Send(Listing(Json::array(), 1, 0));
        REQUIRE(SettleUntil(CountsAre("1 online, 0 in games")));
        MCGamesScreen& screen = Internet();
        screen.SetPlayerName("Commander");
        return screen;
    }

    /// <summary>No games, 3 players online, and the week's hours busiest on Saturday evening (the service's time).</summary>
    Json EmptyListingBusySaturday()
    {
        constexpr size_t SaturdayEvening = 6 * 24 + 20;
        constexpr double Busy = 30.0;
        constexpr double Quiet = 1.0;
        Json hours = Json::array();

        for (size_t hour = 0; hour < MCWeekHours; hour++)
        {
            hours.push_back(hour == SaturdayEvening ? Busy : Quiet);
        }

        Json empty = Listing(Json::array(), 3, 0);
        empty["busiest"] = hours;
        return empty;
    }

    /// <summary>The remade dialog's title, when it shows.</summary>
    std::string DialogHeading()
    {
        return MCRmlDialogs::Shown() ? MCRmlDialogs::Dialog()->Said().Heading : std::string();
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

    /// <summary>How long a link on the loopback gets to open: one that can't (relays only, and none) fails soon.</summary>
    constexpr auto LoopbackOpen = std::chrono::milliseconds(1500);
    /// <summary>A link's open long enough for a test to call its join off first.</summary>
    constexpr auto SlowOpen = std::chrono::milliseconds(20000);
    constexpr auto HostStep = std::chrono::milliseconds(5);

    /// <summary>
    /// The internet games' links on the loopback, the service never carrying one (the stand-in has no relays); one that
    /// can't open fails after <paramref name="open"/>.
    /// </summary>
    void LinksOnLoopback(std::chrono::milliseconds open = LoopbackOpen)
    {
        MCInternetSetup setup = MCInternetGame::SetupNow();
        setup.BindAddress = "127.0.0.1";
        setup.Deadlines = {.Open = open, .Service = std::nullopt};
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

    /// <summary>
    /// JOIN GAME, run on until the join fails with <paramref name="failure"/> (its RETRY's dialog, called off): whether
    /// this game's internet game was public (its links relayed) every frame it was there, and there at all.
    /// </summary>
    bool JoinsRelayedOnly(MCGamesScreen& screen, std::string_view failure)
    {
        screen.PressJoin();
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
        PressDialogCancel();
        return there && relayed;
    }
}

TEST_CASE_ISOLATED("game: MULTIPLAYER offers INTERNET, with how many are online, and LAN, and INTERNET lists the "
                   "service's public games")
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

    // MULTIPLAYER (the main menu's panel): INTERNET and LAN, the count under INTERNET once the service answers.
    MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
    REQUIRE(SettleUntil(
        [] { return OnAccess() && MCTestGuest::Panel(MCMainPanel::Multiplayer).OnlineLine() == "37 players online"; }));
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
    CHECK(asked == CountUrl);
    SaveShownShot("internet 0 access");

    // LAN opens the LAN games, and their BACK (a click) comes back.
    MCTestGuest::PressLan();
    REQUIRE(SettleUntil([] { return OnLan(); }));
    RealClickOn(*ElementOf(*MCTestGuest::Games(MCGamesWhere::Lan), "back"));
    REQUIRE(SettleUntil(OnAccess));

    // INTERNET opens the service's list: looking for games until it answers, JOIN GAME off.
    MCTestGuest::PressInternet();
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    REQUIRE(list.Opened.size() == 1);
    MCGamesScreen& screen = Internet();
    CHECK_EQ(screen.Seen().Empty, "Looking for games...");
    CHECK(!screen.Seen().CanJoin);
    CHECK(screen.Seen().CanHost);
    CHECK(screen.Seen().Counts.empty());

    // Its games in the service's order, open ones first: one with room, a full one; then one in battle; and one on a
    // newer version.
    list.Send(EveryKindOfGame(37, 12));
    REQUIRE(SettleUntil(CountsAre("37 online, 12 in games")));
    CHECK(screen.Seen().Rows == EveryKindOfRow);
    CHECK(screen.Seen().Empty.empty());
    Settle();
    CHECK_EQ(RowsOf(screen).size(), EveryKindOfRow.size());
    SaveShownShot("internet 1 list");

    // A click on a game's row picks it: its players (the host first) and where it is; JOIN GAME takes it.
    RealClickOn(*RowsOf(screen).at(1));
    CHECK_EQ(screen.Seen().Picked, 1);
    RealClickOn(*RowsOf(screen).at(0));
    CHECK_EQ(screen.Seen().Picked, 0);
    CHECK_EQ(screen.Seen().PickName, "Bob's game");
    CHECK((screen.Seen().PickPlayers == Rows{"Bob", "Ann"}));
    CHECK((screen.Seen().PickLines == Rows{"Public, hosted in Europe"}));
    CHECK(screen.Seen().CanJoin);
    SaveShownShot("internet 2 picked");

    // One that can't be joined says why: full, in battle (its mission); a newer version's JOIN GAME offers the update.
    screen.PickKey("PXYL");
    Settle();
    CHECK(!screen.Seen().CanJoin);
    screen.PickKey("Q4RT");
    Settle();
    CHECK(!screen.Seen().CanJoin);
    CHECK((screen.Seen().PickLines == Rows{"Public, hosted in Asia", "In battle for 12 min, on Lost Outpost"}));
    screen.PickKey("W8ZE");
    Settle();
    CHECK(screen.Seen().CanJoin);
    CHECK((screen.Seen().PickLines == Rows{"Public, hosted in Oceania", "Needs version 0.7.0"}));

    // A code typed (either case): JOIN GAME takes it, whatever is picked; JOIN GAME off without it.
    screen.PickKey("PXYL");
    Settle();
    CHECK(!screen.Seen().CanJoin);
    RealClickOn(*ElementOf(screen, "code"));
    RealType("k7qf");
    Settle();
    CHECK(screen.Seen().CanJoin);
    screen.SetCode("");
    Settle();
    CHECK(!screen.Seen().CanJoin);

    // HOST GAME (a click): its window over the list, the game named for the player, PUBLIC; PRIVATE kept for the next.
    RealClickOn(*ElementOf(screen, "host"));
    REQUIRE(screen.HostWindow());
    MCHostWindow& host = *screen.HostWindow();
    CHECK(host.Shown());
    CHECK(host.Choices().Public);
    CHECK_EQ(host.Choices().GameName, std::format("{}'s game", screen.PlayerName()));
    SaveShownShot("internet 3 host");
    MCHostWindow::MCHost choices = host.Choices();
    choices.Public = false;
    host.SetChoices(choices);
    Settle();
    CHECK(!host.Choices().Public);
    SaveShownShot("internet 4 host private");

    // Its CANCEL (a click): the list again; HOST GAME again keeps PRIVATE.
    RealClickOn(*ElementOf(host, "cancel"));
    CHECK(!host.Shown());
    CHECK(OnInternet());
    screen.PressHost();
    Settle();
    CHECK(host.Shown());
    CHECK(!host.Choices().Public);
    host.Back();
    Settle();

    // BACK (a click): MULTIPLAYER ACCESS, the list's socket closed.
    RealClickOn(*ElementOf(screen, "back"));
    REQUIRE(SettleUntil(OnAccess));
    CHECK(list.ClosedByGame);

    // No open games: it says so, alone (the week's busiest hours heard, not shown).
    MCTestGuest::PressInternet();
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    REQUIRE(list.Opened.size() == 2);
    list.Send(EmptyListingBusySaturday());
    REQUIRE(SettleUntil(CountsAre("3 online, 0 in games")));
    CHECK_EQ(screen.Seen().Empty, "No open games.");
    CHECK(!screen.Seen().CanJoin);
    SaveShownShot("internet 5 empty");
}

/// <summary>
/// INTERNET never starts with the computer's account name, the original's default (LAN keeps it), so the account name
/// never goes online: with no name entered before, its box is empty and has the focus, and HOST GAME and JOIN GAME wait
/// for a name (spaces aren't one); a name entered before is the one it starts with.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET starts with the name entered before, never the computer's account name, and waits "
                   "for one to host or join")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    const MCServicesScope scope(list, service);
    REQUIRE(MCTestGame::StartLogistics());
    // No name entered before (the worker's user folder may hold an earlier test's).
    SaveUserName("");
    const auto openInternet = [&list]() -> MCGamesScreen&
    {
        MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
        Settle();
        MCTestGuest::PressInternet();
        REQUIRE(SettleUntil([] { return OnInternet(); }));
        list.Send(Listing(Json::array(), 1, 0));
        REQUIRE(SettleUntil(CountsAre("1 online, 0 in games")));
        return Internet();
    };

    MCGamesScreen& screen = openInternet();
    CHECK(screen.PlayerName().empty());
    CHECK(!screen.Seen().CanHost);
    const Rml::Element* focused = screen.Layer().Context().GetFocusElement();
    REQUIRE(focused != nullptr);
    CHECK_EQ(focused->GetId(), std::string("name"));
    // The empty box asks for the name, in dim, until one is typed.
    CHECK_EQ(focused->GetAttribute<Rml::String>("placeholder", ""), std::string("Type your name"));
    CHECK(focused->IsPseudoClassSet("placeholder-shown"));
    SaveWindowShot("internet no name");

    // A code typed: JOIN GAME still waits for the name; spaces aren't one.
    screen.SetCode(std::string(MCFakeGames::GameCode));
    screen.SetPlayerName("   ");
    Settle();
    CHECK(!screen.Seen().CanHost);
    CHECK(!screen.Seen().CanJoin);
    screen.SetPlayerName("Commander");
    Settle();
    CHECK(screen.Seen().CanHost);
    CHECK(screen.Seen().CanJoin);
    CHECK(!focused->IsPseudoClassSet("placeholder-shown"));

    // A name entered before: INTERNET starts with it.
    SaveUserName("Falcon");
    CHECK_EQ(openInternet().PlayerName(), std::string("Falcon"));
}

/// <summary>
/// INTERNET from the keys alone: the arrows through the games (each picked as it's reached), Enter on one joins it (its
/// wait over the list, CANCEL on Escape), Tab to the player's name and the code, Enter on HOST GAME opens its window, and
/// Escape closes it, then goes back to MULTIPLAYER ACCESS.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET plays from the keys")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCServiceScript::ListScript list;
    MCFakeGames::MCFakeGamesService service;
    const MCServicesScope scope(list, service);
    LinksOnLoopback(SlowOpen);
    REQUIRE(MCTestGame::StartLogistics());
    // A player who entered a name before (the worker's user folder may hold none, or an earlier test's).
    SaveUserName("Commander");
    MCGamesScreen& screen = OpenInternetScreen(list);
    list.Send(EveryKindOfGame(37, 12));
    REQUIRE(SettleUntil(CountsAre("37 online, 12 in games")));
    Rml::Context& context = screen.Layer().Context();

    // The first key shows the focus on the first game; Down picks the next.
    RealKey(SDL_SCANCODE_DOWN);
    Settle();
    const std::vector<Rml::Element*> rows = RowsOf(screen);
    REQUIRE(rows.size() == EveryKindOfRow.size());
    const auto focused = [&context] { return context.GetFocusElement(); };
    CHECK(std::ranges::find(rows, focused()) != rows.end());
    const int32_t first = screen.Seen().Picked;
    REQUIRE(first >= 0);
    RealKey(SDL_SCANCODE_DOWN);
    Settle();
    CHECK_EQ(screen.Seen().Picked, first + 1);
    RealKey(SDL_SCANCODE_UP);
    Settle();
    CHECK_EQ(screen.Seen().Picked, first);
    SaveShownShot("internet keys 0 picked");

    // Enter on Bob's game: joining, the list still live under the wait, which Escape calls off.
    while (screen.Seen().Picked > 0)
    {
        RealKey(SDL_SCANCODE_UP);
    }

    Settle();
    REQUIRE_EQ(screen.Seen().PickName, "Bob's game");
    MCBobsGame bob;
    RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(SettleUntil([] { return DialogText() == "Joining Bob's game..."; }));
    CHECK(MCScreenChange::Running());
    list.Send(EveryKindOfGame(38, 12));
    REQUIRE(SettleUntil(CountsAre("38 online, 12 in games")));
    CHECK_EQ(DialogText(), "Joining Bob's game...");
    SaveShownShot("internet keys 1 joining");
    RealKey(SDL_SCANCODE_ESCAPE);
    Settle();
    CHECK(DialogText().empty());
    CHECK(OnInternet());
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);

    // Tab to HOST GAME; Enter opens its window, with the focus in it; Escape closes it.
    for (int32_t tabs = 0; tabs < 8 && focused() != ElementOf(screen, "host"); ++tabs)
    {
        RealKey(SDL_SCANCODE_TAB);
    }

    REQUIRE(focused() == ElementOf(screen, "host"));
    RealKey(SDL_SCANCODE_RETURN);
    Settle();
    REQUIRE(screen.HostWindow());
    CHECK(screen.HostWindow()->Shown());
    SaveShownShot("internet keys 2 host");
    RealKey(SDL_SCANCODE_ESCAPE);
    Settle();
    CHECK(!screen.HostWindow()->Shown());
    CHECK(OnInternet());

    // Escape: back to MULTIPLAYER ACCESS.
    RealKey(SDL_SCANCODE_ESCAPE);
    REQUIRE(SettleUntil(OnAccess));
}

namespace
{
    /// <summary>Where LAN's search asks in the frames' test: an address of this machine's no game uses.</summary>
    constexpr std::string_view NoLanGameAddress = "127.0.0.1:28983";
}

/// <summary>
/// SPEC-menus.md's "Testing" for stage 3's screens: INTERNET (no connection, its games with one picked, from the keys,
/// HOST GAME's window, a newer game's JOIN GAME, a message, no games), LAN (no games, HOST GAME's window), each drawn on the
/// software path at 640x480, 1280x720 and 1280x1024 (the vendored FreeType's letters on whole pixels: the same on every
/// machine; --shots saves them), and at every window the players' reports name, every element inside the window, no two
/// siblings overlapping, no text spilling from its box, every text legible.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET and LAN draw their frames, and lay out and read at every players' window")
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
    // A player who entered a name before, so the box doesn't open with the focus (the worker's user folder may hold
    // none, or an earlier test's).
    SaveUserName("Commander");
    MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
    Settle();
    MCTestGuest::PressInternet();
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    MCGamesScreen& screen = Internet();
    screen.SetPlayerName("Commander");

    // No internet (before the list answers), then RETRY: the games, one picked.
    list.Drop("The Internet connection appears to be offline.");
    REQUIRE(SettleUntil([&] { return screen.Seen().Empty == "No internet connection."; }));
    CheckMenusFrames("internet offline", {0x80c9b96d, 0x01e8cae0, 0xbd3a01ed});
    screen.PressRetry();
    REQUIRE(SettleUntil([&] { return list.Opened.size() == 2; }));
    list.Send(EveryKindOfGame(37, 12));
    REQUIRE(SettleUntil(CountsAre("37 online, 12 in games")));
    screen.PickKey("K7QF");
    Settle();
    CheckMenusFrames("internet", {0xe9164f52, 0xbd3f1a7b, 0x8ebf1472});

    // From the keys: the focus's ring on the game picked.
    RealKey(SDL_SCANCODE_DOWN);
    RealKey(SDL_SCANCODE_UP);
    Settle();
    CheckMenusFrames("internet keys", {0x08409c52, 0x2a2e551b, 0xa556f6f2});

    screen.PressHost();
    Settle();
    REQUIRE(screen.HostWindow());
    CheckMenusFrames("internet host", {0x578ddcc4, 0xc7a0069a, 0x523b2ef6});
    screen.HostWindow()->Hide();
    Settle();

    screen.PickKey("W8ZE");
    Settle();
    screen.PressJoin();
    Settle();
    REQUIRE_EQ(DialogHeading(), "Needs version 0.7.0");
    CheckMenusFrames("internet join update", {0x9a42b31d, 0x94d929b6, 0x83dbffe7});
    PressDialogCancel();

    MCRmlDialogs::Message("No game has the code M3VD.");
    Settle();
    CheckMenusFrames("internet message", {0xd1941330, 0xe38dedea, 0xda03b9e5});
    PressDialogOk();

    // No games: "No open games." alone, the week's busiest hours heard but not drawn.
    list.Send(EmptyListingBusySaturday());
    REQUIRE(SettleUntil(CountsAre("3 online, 0 in games")));
    CheckMenusFrames("internet empty", {0x9170ef65, 0x5ae0e67f, 0xb5d7b31e});
    CHECK_EQ(MenusProblems(), "");

    // LAN: no games on this network, then its HOST GAME's window.
    screen.Back();
    REQUIRE(SettleUntil(OnAccess));
    MCTestGuest::PressLan();
    REQUIRE(SettleUntil([] { return OnLan(); }));
    // Its search asks only where no game is (another test's game on this machine stays out of its list).
    MultiPlayer()->SessionManager->ConnectTcp(NoLanGameAddress);
    MCGamesScreen& lan = *MCTestGuest::Games(MCGamesWhere::Lan);
    lan.SetPlayerName("Commander");
    REQUIRE(SettleUntil([&] { return lan.Seen().Empty == "No games on this network."; }));
    CheckMenusFrames("lan", {0xe32d17ea, 0x6791dcba, 0xbcbfe80b});
    lan.PressHost();
    Settle();
    REQUIRE(lan.HostWindow());
    CheckMenusFrames("lan host", {0x6ac200ab, 0x3a4b05c4, 0xdca239ba});
}

/// <summary>
/// INTERNET's list when it can't list: the service not reached ("No internet connection.", Offline, RETRY), closed
/// before it listed ("The games list isn't answering.", RETRY), and turned away for good ("Update to play online." with
/// UPDATE; "This copy is blocked from online games."). RETRY tries again; HOST GAME is off while it can't list.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET says why it can't list games, and RETRY tries again")
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
    // A player who entered a name before (the worker's user folder may hold none, or an earlier test's).
    SaveUserName("Commander");
    MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
    Settle();
    MCTestGuest::PressInternet();
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    MCGamesScreen& screen = Internet();

    // No internet: the Mac's socket's words for it.
    list.Drop("The Internet connection appears to be offline.");
    REQUIRE(SettleUntil([&] { return screen.Seen().Empty == "No internet connection."; }));
    CHECK_EQ(screen.Seen().Counts, "Offline");
    CHECK(screen.Seen().Retry);
    CHECK(!screen.Seen().CanHost);
    SaveShownShot("internet offline");

    // RETRY: the list opened again, and listing.
    screen.PressRetry();
    REQUIRE(SettleUntil([&] { return list.Opened.size() == 2; }));
    list.Send(Listing(Json::array(), 2, 0));
    REQUIRE(SettleUntil(CountsAre("2 online, 0 in games")));
    CHECK(!screen.Seen().Retry);
    CHECK(screen.Seen().CanHost);

    // Its socket closed before it listed again: not answering, RETRY (Enter).
    screen.Back();
    REQUIRE(SettleUntil(OnAccess));
    MCTestGuest::PressInternet();
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    list.Drop("closed (1006)");
    REQUIRE(SettleUntil([&] { return screen.Seen().Empty == "The games list isn't answering."; }));
    CHECK(screen.Seen().Retry);
    const size_t opened = list.Opened.size();
    RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(SettleUntil([&] { return list.Opened.size() == opened + 1; }));

    // Turned away for an update: UPDATE, no RETRY.
    list.Send(Json{{"t", "update"}});
    REQUIRE(SettleUntil([&] { return screen.Seen().Empty == "Update to play online."; }));
    CHECK(screen.Seen().Update);
    CHECK(!screen.Seen().Retry);
    SaveShownShot("internet update");

    // Blocked.
    screen.Back();
    REQUIRE(SettleUntil(OnAccess));
    MCTestGuest::PressInternet();
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    list.Send(Json{{"t", "banned"}});
    REQUIRE(SettleUntil([&] { return screen.Seen().Empty == "This copy is blocked from online games."; }));
    CHECK(!screen.Seen().Retry);
    CHECK(!screen.Seen().Update);
}

/// <summary>
/// INTERNET's list longer than its view: scrolled to the bottom, a game picked there by a click on its row, and the
/// list's updates, keep it where it's scrolled and the pick; a shorter list without the picked game picks nothing.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET's list stays where it's scrolled through a pick and the list's updates")
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
    MCGamesScreen& screen = OpenInternetScreen(list);

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

    const auto countsAre = [](int online) { return CountsAre(std::format("{} online, 0 in games", online)); };

    // More games than the list shows at once.
    constexpr int32_t ManyGames = 30;
    list.Send(listing(ManyGames, ManyGames));
    REQUIRE(SettleUntil(countsAre(ManyGames)));
    Rml::Element& scroller = *ElementOf(screen, "list");
    REQUIRE(scroller.GetScrollHeight() > scroller.GetClientHeight());

    // Scrolled to the bottom: the last game, a click on its row, picks it, and the list stays put.
    const float bottom = scroller.GetScrollHeight() - scroller.GetClientHeight();
    scroller.SetScrollTop(bottom);
    Settle();
    REQUIRE(scroller.GetScrollTop() == bottom);
    const int32_t last = ManyGames - 1;
    RealClickOn(*RowsOf(screen).at(static_cast<size_t>(last)));
    CHECK_EQ(screen.Seen().Picked, last);
    CHECK_EQ(screen.Seen().PickName, std::format("Game {}", last));
    CHECK(scroller.GetScrollTop() == bottom);
    SaveShownShot("internet scroll 0 picked at the bottom");

    // The list's updates keep it there, and the pick.
    list.Send(listing(ManyGames, ManyGames + 1));
    REQUIRE(SettleUntil(countsAre(ManyGames + 1)));
    CHECK(scroller.GetScrollTop() == bottom);
    CHECK_EQ(screen.Seen().Picked, last);

    // A shorter list: the picked game gone with it, nothing picked.
    constexpr int32_t FewGames = 3;
    list.Send(listing(FewGames, FewGames));
    REQUIRE(SettleUntil(countsAre(FewGames)));
    CHECK_EQ(screen.Seen().Rows.size(), static_cast<size_t>(FewGames));
    CHECK_EQ(screen.Seen().Picked, -1);
    CHECK(screen.Seen().PickName.empty());
    CHECK(scroller.GetScrollTop() == 0.0f);
}

/// <summary>
/// Hosting and joining from INTERNET, through a stand-in for the games service: a game hosted goes into its room (the
/// session screen), the service told the room and the players its code; the room's EXIT comes back to the games; a code
/// no game has is refused over the list, and nothing is left open.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET hosts a game into its room, its EXIT comes back, and a wrong code is refused")
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
    MCGamesScreen& screen = OpenInternetScreen(list);

    // HOST GAME, then its window's HOST GAME: the room, the host alone in it, and the game's code in the chat.
    const std::string gameName = HostFrom(screen);
    CHECK_EQ(gameName, "Commander's game");
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(manager.Internet != nullptr);
    CHECK(manager.HostedInternetCode() == std::optional<std::string>(MCFakeGames::GameCode));
    CHECK(DialogText().empty());
    CHECK(!OnInternet());
    const std::string codeLine = std::format("FRIENDS CAN JOIN WITH CODE {}.", MCFakeGames::GameCode);
    CHECK(std::ranges::any_of(GlobalLog()->Chat->Lines,
                              [&](const MCLogChat::HistoryLine& line) { return line.Text.contains(codeLine); }));

    // The service has the room as the list will show it: public (the window's default), its name, its players.
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
    // (The host is alone, so the file check is done at once, its message closing itself.)
    GlobalLog()->Room.LoadMission("ARENA");
    REQUIRE(SettleUntil([] { return DialogText().empty(); }));
    REQUIRE(!GlobalLog()->Room.MissionName.empty());
    REQUIRE(SettleUntil([&] { return service.Room()->at("mission") != ""; }));
    CHECK(service.Room()->at("mission") ==
          MCServiceName(GlobalLog()->Room.MissionName, MCServiceMissionMost, std::string_view()));
    SaveShot("internet 6 room", ScreenHash());

    // The room's EXIT: the internet games again, the game and its socket gone, the player's name kept.
    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    CHECK(!RoomScreenShown());
    REQUIRE(MultiPlayer() != nullptr);
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK(MultiPlayer()->SessionManager->CurrentSession() == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    CHECK_EQ(screen.PlayerName(), "Commander");

    // A code no game has: JOIN GAME tries it, and the service's answer is said over the list.
    constexpr std::string_view NoSuchCode = "M3VD";
    screen.SetCode(std::string(NoSuchCode));
    Settle();
    REQUIRE(screen.Seen().CanJoin);
    RealClickOn(*ElementOf(screen, "join"));
    REQUIRE(SettleUntil([] { return DialogText() == "No game has the code M3VD."; }));
    CHECK(service.Paths.back().starts_with(std::format("/join/{}", NoSuchCode)));
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    CHECK(OnInternet());
    SaveShownShot("internet 7 no game");
    PressDialogOk();
    CHECK(DialogText().empty());
    CHECK(OnInternet());

    // The player's network holds too many relays in live games: the join is refused, and the list says why.
    service.RefuseJoinsWith = "crowded";
    screen.PressJoin();
    REQUIRE(SettleUntil([] { return DialogText() == "Too many players on your network are in games."; }));
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    PressDialogOk();
    CHECK(DialogText().empty());

    // Port-only (SPEC-mco-hosting.md's "REMOVE"): the game's host removed this copy a while ago: when to try again.
    service.RefuseJoinsWith = "removed";
    service.RefusalSeconds = 421;
    screen.PressJoin();
    constexpr std::string_view KeptOut = "You were removed from the game M3VD. Try again in 8 minutes.";
    CHECK(SettleUntil([&] { return DialogText() == KeptOut; }));
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    PressDialogOk();
    CHECK(DialogText().empty());
}

/// <summary>
/// A player away from an internet game: INTERNET lets its list go (not counted online) until the next input; the room
/// asks "STILL THERE?" after 10 minutes without input, any input answers it, and a minute without one has the player
/// leave the room, as its EXIT does (the host's game ends), and says why over the games.
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
    MCGamesScreen& screen = OpenInternetScreen(list);

    // Away on the games: the list's socket goes, and comes back with the player.
    now += MCIdleCheck::AwayTime;
    REQUIRE(SettleUntil([&] { return list.ClosedByGame; }));
    CHECK_EQ(screen.Seen().Empty, "Looking for games...");
    Nudge();
    REQUIRE(SettleUntil([&] { return list.Opened.size() == 2; }));
    list.Send(Listing(Json::array(), 1, 0));
    REQUIRE(SettleUntil(CountsAre("1 online, 0 in games")));

    // In the room: asked after 10 minutes, and an answer keeps the player in.
    HostFrom(screen);
    now += MCIdleCheck::AwayTime - std::chrono::seconds(1);
    Settle();
    CHECK(DialogText().empty());
    now += std::chrono::seconds(1);
    REQUIRE(SettleUntil([] { return DialogText() == "Are you still there?"; }));
    CHECK(MCIdleCheck::Asking());
    SaveShot("internet 8 still there", ScreenHash());
    now += MCIdleCheck::AnswerTime - std::chrono::seconds(1);
    Nudge();
    REQUIRE(SettleUntil([] { return DialogText().empty(); }));
    CHECK(!MCIdleCheck::Asking());
    now += MCIdleCheck::AnswerTime;
    Settle();
    CHECK(InRoom());

    // Asked again, and no answer: the player leaves, the game ends on the service, and the games say why.
    now += MCIdleCheck::AwayTime;
    REQUIRE(SettleUntil([] { return DialogText() == "Are you still there?"; }));
    now += MCIdleCheck::AnswerTime;
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    REQUIRE(SettleUntil([] { return !DialogText().empty(); }));
    CHECK_EQ(DialogText(), "You were away too long and were taken out of the game.");
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);
    CHECK_EQ(service.OpenLines(), 0u);
    MCIdleCheck::SetClock({});
}

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
    MCGamesScreen& screen = OpenInternetScreen(list);
    const std::string gameName = HostFrom(screen);
    REQUIRE(SettleUntil([&] { return service.Room().has_value(); }));

    // The update ready: UPDATE, opened by the room's line, offers RESTART, which brings the room back.
    UpdateReady();
    CHECK(MCLobbyUpdate::InRoom());
    MCTestGuest::PressInRoom("update");
    REQUIRE(SettleUntil([] { return MCUpdateScreen::Shows() && !MCScreenChange::Running(); }));
    const std::shared_ptr<MCUpdateScreen> update = MCRemadeTest::OnTop<MCUpdateScreen>();
    REQUIRE(update != nullptr);
    Settle();
    CHECK(MCRemadeTest::Visible(*update, "act"));
    CHECK_EQ(MCRemadeTest::Words(*update, "act"), "Restart");
    CHECK_EQ(MCRemadeTest::Words(*update, "status"),
             MCRmlDialogs::AsSentence("DOWNLOADED. RESTART TO INSTALL IT; YOUR ROOM WILL REOPEN."));

    // RESTART: the note to come back by, the update installing, the game's place kept.
    REQUIRE(MCRemadeTest::Press(*update, "act"));
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
    REQUIRE(SettleUntil([] { return MCMenuScreens::Top() == MCMenuScreens::Room(); }));
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

    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil([] { return OnInternet(); }));
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
    CHECK(OnInternet());
    CHECK(!LaunchedFromLobby);
    SaveShownShot("internet 11 place gone");
    PressDialogOk();
    CHECK(DialogText().empty());
    CHECK(OnInternet());
}

/// <summary>
/// A game on a newer version, picked: "Needs version 0.7.0", and JOIN GAME says so ("You have 0.6.0.") with UPDATE, which
/// opens UPDATE (the 1998 page over the main menu until stage 6); once this game has the update that brings it, the page
/// says the new version joins the game, and RESTART leaves the rejoin note that has it join. CANCEL keeps the list.
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
    MCGamesScreen& screen = OpenInternetScreen(list);
    // An array of one game said as one: braces round a single json make that json itself with MSVC (a copy of the
    // game, so no list), an array of it with Clang.
    list.Send(Listing(
        Json::array({MCServiceScript::ListedGame("W8ZE", "Next version", {"Eve"}, 6, nullptr, NewerVersion, "OC")}), 2,
        1));
    REQUIRE(SettleUntil(CountsAre("2 online, 1 in games")));

    // Picked: it needs a newer version, which JOIN GAME says, offering UPDATE; CANCEL stays on the list.
    screen.PickKey("W8ZE");
    Settle();
    CHECK(screen.Seen().CanJoin);
    CHECK((screen.Seen().PickPlayers == Rows{"Eve"}));
    CHECK((screen.Seen().PickLines == Rows{"Public, hosted in Oceania", "Needs version 0.7.0"}));
    screen.PressJoin();
    Settle();
    CHECK_EQ(DialogHeading(), "Needs version 0.7.0");
    CHECK_EQ(DialogText(), "You have 0.6.0.");
    SaveShownShot("internet 12 newer picked");
    PressDialogCancel();
    CHECK(OnInternet());
    CHECK(MultiPlayer()->SessionManager->Internet == nullptr);

    // The update that brings it found; JOIN GAME's UPDATE: the page says the new version joins the game.
    MCUpdater::Found(std::string(NewerVersion), "Rooms come back after an update.");
    Settle();
    screen.PressJoin();
    Settle();
    REQUIRE_EQ(DialogHeading(), "Needs version 0.7.0");
    PressDialogOk();
    // Port-only (SPEC-menus.md's stage 6): the remade UPDATE over INTERNET, through the screens' flow.
    REQUIRE(SettleUntil([] { return MCUpdateScreen::Shows() && !MCScreenChange::Running(); }));
    CHECK(!OnInternet());
    const std::shared_ptr<MCUpdateScreen> update = MCRemadeTest::OnTop<MCUpdateScreen>();
    REQUIRE(update != nullptr);
    MCUpdater::Answer(MCUpdater::MCAnswer::Install);
    MCUpdater::Ready();
    Settle();
    CHECK_EQ(MCRemadeTest::Words(*update, "status"),
             std::string("Downloaded. Restart to install it and join Next version."));
    REQUIRE(MCRemadeTest::Visible(*update, "act"));
    CHECK_EQ(MCRemadeTest::Words(*update, "act"), std::string("Restart"));
    SaveShownShot("internet 13 update to join");

    // RESTART: the update installs, and the new version joins the game, the menus' again once in.
    REQUIRE(MCRemadeTest::Press(*update, "act"));
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
/// INTERNET's UPDATE (its list turned away for an update) opens UPDATE, whose way back returns to INTERNET.
/// </summary>
TEST_CASE_ISOLATED("game: INTERNET's UPDATE comes back to INTERNET")
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
    MCGamesScreen& screen = OpenInternetScreen(list);
    screen.PressUpdate();
    REQUIRE(SettleUntil([] { return MCUpdateScreen::Shows() && !MCScreenChange::Running(); }));
    CHECK(MCScreenFlow::Shown() == MCMenuScreens::Update());
    // DONE: the way back.
    CHECK(MCScreenFlow::Back());
    REQUIRE(SettleUntil([] { return OnInternet(); }));
    CHECK(!MCUpdateScreen::Shows());
}

/// <summary>
/// Port-only (Redux HD): an internet host's REMATCH (network/MCRoomEnd.h): its game keeps its place on the service
/// through the match's session's end and comes back to it (the same code, no game hosted afresh), and its room is
/// hosted again, a new session with the same name and the match's mission, listed out of battle (the match's start
/// stayed on it, so the service turned its guests away: the 21 Nov playtest).
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
    MCGamesScreen& screen = OpenInternetScreen(list);
    const std::string gameName = HostFrom(screen);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(SettleUntil([&] { return service.Room().has_value(); }));
    GlobalLog()->Room.LoadMission("ARENA");
    REQUIRE(SettleUntil([] { return DialogText().empty(); }));
    // The match's session, kept: the rematch's room is another (a host's session has no instance of its own here).
    const std::shared_ptr<MCFidpSession> firstRoom = manager.CurrentSession();
    const auto hosted = [&service]
    {
        return std::ranges::count_if(service.Paths, [](const std::string& path) { return path.starts_with("/host?"); });
    };

    REQUIRE_EQ(hosted(), 1);

    // The match starts (its lock and its hook: START needs a player on each team), its room in battle on the service's
    // list, and the host's REMATCH at its results.
    REQUIRE(manager.LockSession());
    MCRoomEnd::MatchStarts();
    REQUIRE(SettleUntil([&] { return service.Room()->at("battle") == true; }));
    MCRoomEnd::Rematch();
    REQUIRE(SettleUntil(
        [&]
        {
            return MCMenuScreens::Top() == MCMenuScreens::Room() && manager.CurrentSession() != nullptr &&
                   manager.CurrentSession() != firstRoom;
        }));
    Settle();
    CHECK(manager.HostedInternetCode() == std::optional<std::string>(MCFakeGames::GameCode));
    CHECK_EQ(hosted(), 1);
    CHECK(std::ranges::any_of(service.Paths, [](const std::string& path)
                              { return path.starts_with(std::format("/game/{}?", MCFakeGames::GameCode)); }));
    CHECK(!service.HostPlaceKept());
    CHECK_EQ(manager.CurrentSession()->Name, gameName);
    // The room back on the list out of battle, as its guests can join it (the service turns joins to one in battle away).
    CHECK(service.Room()->at("battle") == false);
    CHECK(!GlobalLog()->Room.MissionFile.empty());
    CHECK(DialogText().empty());
    SaveShot("internet 14 rematch", ScreenHash());
}

/// <summary>
/// A game joined from INTERNET's list is public: picked, or its code typed while the list shows it, its links take
/// relays only though the service says its host made it private (Bob's here, which isn't), so its host never learns this
/// player's address; the stand-in has no relays, so such a link can't open on the loopback ("Couldn't connect to Bob's
/// game.", RETRY). A code typed for a game the list doesn't show joins as the service says, its links direct.
/// </summary>
TEST_CASE_ISOLATED("game: a game joined from INTERNET's list takes relays only, though its host says it's private")
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
    MCGamesScreen& screen = OpenInternetScreen(list);
    MCBobsGame bob;
    list.Send(EveryKindOfGame(37, 12));
    REQUIRE(SettleUntil(CountsAre("37 online, 12 in games")));
    constexpr std::string_view NotReached = "Couldn't connect to Bob's game.";

    // Picked from the list.
    screen.PickKey("K7QF");
    Settle();
    REQUIRE(screen.Seen().CanJoin);
    CHECK(JoinsRelayedOnly(screen, NotReached));

    // Its code typed while the list shows it.
    screen.SetCode("k7qf");
    Settle();
    REQUIRE(screen.Seen().CanJoin);
    CHECK(JoinsRelayedOnly(screen, NotReached));
    CHECK(!service.GuestSignals().empty());
    CHECK(!service.GuestOfferedOwnAddress());

    // The list no longer shows it: the typed code joins as the service says (private), its link direct and open.
    list.Send(Listing(Json::array(), 1, 0));
    REQUIRE(SettleUntil(CountsAre("1 online, 0 in games")));
    REQUIRE(screen.Seen().CanJoin);
    screen.PressJoin();
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    REQUIRE(SettleUntil(
        [&]
        {
            bob.Game->Update();
            return manager.Internet != nullptr && manager.Internet->Update() == MCInternetGame::Stage::Ready;
        }));
    CHECK(!manager.Internet->Public());
    CHECK(service.GuestOfferedOwnAddress());

    // The join called off (its CANCEL) lets the link go.
    CHECK_EQ(DialogText(), "Joining the game K7QF...");
    PressDialogCancel();
    CHECK(manager.Internet == nullptr);
    CHECK(OnInternet());
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
