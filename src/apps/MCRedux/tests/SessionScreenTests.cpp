#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "MCTestGuest.h"
#include "TestGame.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFile.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "network/MCGameDataHash.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"

using namespace MCScreenInput;
using MCTestGuest::Guest;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;

namespace
{
    /// <summary>The loopback address the game hosts on (not the default port, so a running game is left alone).</summary>
    constexpr std::string_view HostAddress = "127.0.0.1:28877";
    /// <summary>The MATCH OPTIONS test's, so it can run beside the frames test (--jobs).</summary>
    constexpr std::string_view OptionsHostAddress = "127.0.0.1:28879";
    /// <summary>The game data check's test's, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view DataHostAddress = "127.0.0.1:28881";

    /// <summary>Types <paramref name="text"/> into the text object that has the focus.</summary>
    void Type(std::string_view text)
    {
        for (const char c : text)
        {
            SendKey(static_cast<uint8_t>(c));
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }
}

/// <summary>
/// Renderer phase 3 step 4g: the multiplayer session screen, its player names, ready lights, chat window and chat
/// input draw from their state each frame. The game hosts a LAN session on the loopback, a second player joins from
/// the test, and the host moves the players onto teams, changes the points and the tech base and chats. Each step's
/// screen was recorded on the code before step 4 (when these drew into pictures at the time of each event).
/// </summary>
TEST_CASE_ISOLATED("game: the multiplayer session screen matches the pre-renderer frames")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // The LAN screen as the TCP/IP button leaves it, hosting on the loopback.
    ConnectScreen();
    Settle();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(HostAddress);
    MCSplashScreen& lanScreen = *GlobalLog()->LanScreen;
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(0);
    lanScreen.ShowGuiWindow(1);
    lanScreen.ShowBlock(0);
    GlobalLog()->SetCurrentScreen(lanScreen);
    GlobalLog()->LogisticsState = 0xb;
    lanScreen.Element<MCLogTextObject>(4)->SetStringBuffer("Commander");
    lanScreen.Element<MCLogTextObject>(10)->SetStringBuffer("Test game");
    lanScreen.Element<MCLogTextObject>(11)->SetStringBuffer("6");
    CreateSession();
    Settle();
    REQUIRE(manager.CurrentSession() != nullptr);
    REQUIRE(manager.DirectPlay != nullptr);

    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay, HostAddress));
    Settle();

    // Every frame presented from here on is folded into one more hash.
    uint32_t presents = 0x811c9dc5;
    MCTestGame::OnPresent = [&] { presents = (presents ^ ScreenHash()) * 0x01000193; };

    struct Step
    {
        std::string_view Name;
        std::function<void()> Action;
        uint32_t Expected;
    };

    // The cursor hasn't moved yet in the first step: at (0, 0) the original's over test (OB-129, fixed) took it as over
    // every tool button, so its frame was 0x3d56c0c0 before the fix. Port-only: every frame has the MATCH OPTIONS tab
    // behind MULTIPLAYER CHAT (3 Nov 2026; only its column differs from the frames before: 0xa6b1053f, 0x7548a837,
    // 0xf143dbb4, 0x1a793ae4, 0xa6d18671, 0x4adcfb49, 0x74b8c0d0, 0x2d2edef8, 0x983fc7f9).
    const Step steps[] = {
        {"session", [] { Go(); }, 0x0cc42d9au},
        {"first name to team 1", [] { Drag(0x15, 0x16a, 0x100, 0x13a); }, 0x034cda12u},
        {"next name to team 2", [] { Drag(0x15, 0x16a, 0x1c4, 0x13a); }, 0xb3ef257du},
        {"team 1 points up", [] { Click(0x106, 0x186); }, 0x3b17d20du},
        {"team 2 Inner Sphere", [] { Click(0x225, 0x112); }, 0xbcc94190u},
        {"hover a light", [] { SendMouse(7, 0xdc, 5, false); }, 0xec4846b8u},
        {"chat typed",
         []
         {
             Click(0x40, 0x130);
             Type("hello there");
         },
         0x19fd5db9u},
        {"chat sent", [] { SendKey(0xd); }, 0x7342d1b5u},
        {"name back off the team", [] { Drag(0x105, 0x13a, 0x40, 0x1c0); }, 0xdfb618bcu},
    };

    int32_t index = 0;

    for (const Step& step : steps)
    {
        MCTest::Scope scope(step.Name);
        step.Action();
        const uint32_t hash = Settle();
        SaveShot(std::format("session{:02} {}", index, step.Name), hash);
        CHECK_EQ(hash, step.Expected);
        index++;
    }

    MCTestGame::OnPresent = nullptr;
    // 0xa176a052 before the OB-129 fix (the first step's frames), 0x87982e74 before the control audit (2026-10-04: the
    // steps are unchanged; between them presses end at the release, and the ready lights' backing and the clock show
    // at once; 0x8a6bba0e before the MATCH OPTIONS tab).
    CHECK_EQ(presents, 0xc78ee09fu);
}

namespace
{
    /// <summary>The MATCH OPTIONS list's rows (logistics/MCMatchOptionsTab.cpp): its headings and its options.</summary>
    constexpr int32_t WeatherRow = 1;
    constexpr int32_t TimeRow = 2;
    constexpr int32_t BrakingRow = 3;
    constexpr int32_t ArtilleryRow = 4;
    constexpr int32_t ProbesRow = 5;
    constexpr int32_t SensorsRow = 6;
    constexpr int32_t MinesRow = 7;
    constexpr std::array RuleRows = {WeatherRow, TimeRow, BrakingRow, ArtilleryRow, ProbesRow, SensorsRow, MinesRow};
    /// <summary>A point on each tab (MCMatchOptionsTab.cpp's ChatTabArea and OptionsTabArea).</summary>
    constexpr int32_t TabX = 0xca;
    constexpr int32_t ChatTabY = 0x80;
    constexpr int32_t OptionsTabY = 0xf0;

    /// <summary>Clicks row <paramref name="row"/>'s setting box of <paramref name="list"/> <paramref name="times"/> times.</summary>
    void ClickSetting(MCLogOptionList& list, int32_t row, int32_t times)
    {
        const std::optional<std::pair<int32_t, int32_t>> centre = list.SettingCentre(row);
        REQUIRE(centre.has_value());

        for (int32_t click = 0; click < times; click++)
        {
            Click(centre->first, centre->second);
            Settle();
        }
    }
}

/// <summary>
/// Port: why an option is held names every player whose game doesn't have it, then (for a line too narrow for them
/// all) fewer and the rest counted, down to none named.
/// </summary>
TEST_CASE("session screen: a held match option tells whose game doesn't have it to upgrade")
{
    CHECK(MatchTellToUpgrade({}).empty());
    const std::array<std::string, 1> one = {"BOB"};
    CHECK(MatchTellToUpgrade(one) == "TELL BOB TO UPGRADE.");
    const std::array<std::string, 2> two = {"BOB", "ANN"};
    CHECK(MatchTellToUpgrade(two) == "TELL BOB AND ANN TO UPGRADE.");
    const std::array<std::string, 3> three = {"BOB", "ANN", "SALLY"};
    CHECK(MatchTellToUpgrade(three) == "TELL BOB, ANN AND SALLY TO UPGRADE.");
}

/// <summary>
/// Port: the session screen's MATCH OPTIONS tab, under MULTIPLAYER CHAT, brings the match's options in front in the
/// chat's place (its pictures lettered from the art's tabs). The match plays the host's choices as far as every
/// player's build plays them: while the guest's says nothing (as the original's) every option is held at the mission's
/// own (or off), saying whose game hasn't it; a build of the braking's round frees braking; one of the first match
/// options the weather, not the time of day (its sight is a rule of its own); one that plays them all, every option.
/// The host's changes are the match's and go to the guest at once, as do the options when the screen opens; a guest's
/// list is locked; MULTIPLAYER CHAT brings the chat back.
/// </summary>
TEST_CASE_ISOLATED("game: the session screen's MATCH OPTIONS tab sets the match's options and sends them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(OptionsHostAddress);
    MCSplashScreen& lanScreen = *GlobalLog()->LanScreen;
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(0);
    lanScreen.ShowGuiWindow(1);
    lanScreen.ShowBlock(0);
    GlobalLog()->SetCurrentScreen(lanScreen);
    GlobalLog()->LogisticsState = 0xb;
    lanScreen.Element<MCLogTextObject>(4)->SetStringBuffer("Commander");
    lanScreen.Element<MCLogTextObject>(10)->SetStringBuffer("Options game");
    lanScreen.Element<MCLogTextObject>(11)->SetStringBuffer("6");
    CreateSession();
    Settle();
    REQUIRE(manager.DirectPlay != nullptr);

    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay, OptionsHostAddress));
    Settle();
    MCMultiPlayer& game = *MultiPlayer();
    game.MatchOptions = MCMatchOptions{
        .Weather = MCWeatherKind::Rain, .TimeOfDay = MCTimeOfDay::Night, .Rules = MCMatchRule::MechBraking};
    Go();
    Settle();

    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    REQUIRE(screen.MatchTabs[0] != nullptr);
    REQUIRE(screen.MatchTabs[1] != nullptr);
    CHECK(screen.FrontTab == MCSessionTab::Chat);
    SaveShot("match options 0 chat in front", ScreenHash());

    // Opened, the guest's build saying nothing: the mission's own and no rules went to it.
    const auto sentIs = [&guest](int8_t weather, int8_t time, uint32_t rules)
    {
        const std::optional<MCMPMatchOptionsMessage> sent = guest.MatchOptions();
        REQUIRE(sent.has_value());
        CHECK_EQ(sent->Weather, weather);
        CHECK_EQ(sent->TimeOfDay, time);
        CHECK_EQ(sent->Rules, rules);
    };

    constexpr int8_t MissionOwn = MCMPMatchOptionsMessage::MissionOwn;
    sentIs(MissionOwn, MissionOwn, 0);

    Click(TabX, OptionsTabY);
    Settle();
    CHECK(screen.FrontTab == MCSessionTab::Options);
    MCLogOptionList& list = *screen.MatchOptionsList;
    CHECK(list.ShowWindow);
    CHECK(!GlobalLog()->ChatWindow->ShowWindow);
    CHECK(list.ChoiceName(WeatherRow) == "MISSION'S");
    CHECK(list.ChoiceName(TimeRow) == "MISSION'S");
    CHECK(list.ChoiceName(BrakingRow) == "OFF");

    for (const int32_t row : {ArtilleryRow, ProbesRow, SensorsRow, MinesRow})
    {
        CHECK(list.ChoiceName(row) == "ON");
    }

    constexpr std::string_view NotInGuests = "TELL GUEST TO UPGRADE.";

    // Every row's line fits it.
    for (int32_t row = 0; row < list.OptionRows(); row++)
    {
        CHECK(WhiteFont->Width(list.Description(row)) <= list.LineWidth());
    }

    for (const int32_t row : RuleRows)
    {
        CHECK(list.HeldReason(row) == NotInGuests);
    }

    // A reason too long for a line wraps over more, at spaces, each fitting, every name kept: here every other player of
    // a full session with a long name. A word wider than a line is cut where it fills one.
    constexpr std::string_view LongName = "[WOLF]BLACKBEARSRULE";
    const std::vector<std::string> others(static_cast<size_t>(MaxLinkupPlayers - 1), std::string(LongName));
    const std::string longReason = MatchTellToUpgrade(others);
    list.SetHeld(MinesRow, longReason);
    CHECK(list.HeldReason(MinesRow) == longReason);
    CHECK(list.HeldLines(MinesRow).size() > 1);
    std::string rejoined;

    for (const std::string& line : list.HeldLines(MinesRow))
    {
        CHECK(WhiteFont->Width(line) <= list.LineWidth());
        rejoined += rejoined.empty() ? line : " " + line;
    }

    CHECK(rejoined == longReason);
    const std::string unbroken(80, 'W');
    list.SetHeld(MinesRow, unbroken);
    CHECK(list.HeldLines(MinesRow).size() > 1);

    for (const std::string& line : list.HeldLines(MinesRow))
    {
        CHECK(WhiteFont->Width(line) <= list.LineWidth());
    }

    list.SetHeld(MinesRow, NotInGuests);
    CHECK(list.HeldLines(MinesRow).size() == 1);

    // A held option's click changes nothing.
    ClickSetting(list, WeatherRow, 1);
    CHECK(list.ChoiceName(WeatherRow) == "MISSION'S");
    CHECK(game.MatchOptions.Weather == MCWeatherKind::Rain);
    SaveShot("match options 1 held", ScreenHash());

    // A build of the braking's round: braking, not the weather or time of day.
    const uint32_t hostId = manager.MyPlayer()->Id;
    guest.SendRules(hostId, MCMatchRule::MechBraking);
    Settle();
    CHECK(list.HeldReason(BrakingRow).empty());
    CHECK(list.ChoiceName(BrakingRow) == "ON");
    CHECK(list.HeldReason(WeatherRow) == NotInGuests);
    CHECK(list.HeldReason(TimeRow) == NotInGuests);
    sentIs(MissionOwn, MissionOwn, MCMatchRule::MechBraking);

    // A build of the first match options: neither the weather (it hasn't its play) nor the time of day (its sight).
    guest.SendRules(hostId, MCMatchRule::MechBraking | MCMatchRule::MatchWeather | MCMatchRule::MatchTimeOfDay);
    Settle();
    CHECK(list.HeldReason(WeatherRow) == NotInGuests);
    CHECK(list.HeldReason(TimeRow) == NotInGuests);
    sentIs(MissionOwn, MissionOwn, MCMatchRule::MechBraking);

    // A build of the time of day's sight: the time of day, not the weather.
    guest.SendRules(hostId, MCMatchRule::MechBraking | MCMatchRule::MatchWeather | MCMatchRule::TimeOfDayRules);
    Settle();
    CHECK(list.HeldReason(WeatherRow) == NotInGuests);
    CHECK(list.HeldReason(TimeRow).empty());
    CHECK(list.ChoiceName(TimeRow) == "NIGHT");
    sentIs(MissionOwn, static_cast<int8_t>(MCTimeOfDay::Night), MCMatchRule::MechBraking | MCMatchRule::TimeOfDayRules);

    // A build that plays them all: the host's choices, the time of day's sight and the weather's play with them.
    guest.SendRules(hostId, MCMatchRule::Supported);
    Settle();

    for (const int32_t row : RuleRows)
    {
        CHECK(list.HeldReason(row).empty());
    }

    CHECK(list.ChoiceName(WeatherRow) == "RAIN");
    CHECK(list.ChoiceName(TimeRow) == "NIGHT");
    sentIs(static_cast<int8_t>(MCWeatherKind::Rain), static_cast<int8_t>(MCTimeOfDay::Night),
           MCMatchRule::MechBraking | MCMatchRule::TimeOfDayRules | MCMatchRule::WeatherPlay);
    // The guest heard every player's rules from the host (its own, and the host's).
    const std::optional<MCMPPlayersRulesMessage> players = guest.PlayersRules();
    REQUIRE(players.has_value());
    CHECK_EQ(players->Count, 2);

    // RAIN, DOWNPOUR; NIGHT, MISSION'S; braking off; artillery off.
    ClickSetting(list, WeatherRow, 1);
    ClickSetting(list, TimeRow, 1);
    ClickSetting(list, BrakingRow, 1);
    ClickSetting(list, ArtilleryRow, 1);
    CHECK(list.ChoiceName(ArtilleryRow) == "OFF");
    CHECK(list.ChoiceName(ProbesRow) == "ON");
    CHECK(list.ChoiceName(SensorsRow) == "ON");
    CHECK(list.ChoiceName(WeatherRow) == "DOWNPOUR");
    CHECK(list.ChoiceName(TimeRow) == "MISSION'S");
    CHECK(list.ChoiceName(BrakingRow) == "OFF");
    CHECK(game.MatchOptions.Weather == MCWeatherKind::Downpour);
    CHECK(!game.MatchOptions.TimeOfDay.has_value());
    CHECK_EQ(game.WantedRules() & MCMatchRule::MechBraking, 0u);
    sentIs(static_cast<int8_t>(MCWeatherKind::Downpour), MissionOwn,
           MCMatchRule::WeatherPlay | MCMatchRule::NoArtillery);
    SaveShot("match options 2 options in front", ScreenHash());

    // New chat while the options are in front: MULTIPLAYER CHAT blinks until it is in front again.
    const size_t linesSeen = screen.ChatLinesSeen;
    GlobalLog()->ChatWindow->AddLine("%fc4Guest: %fc6ready");
    Settle();
    CHECK(GlobalLog()->ChatWindow->LinesAdded > linesSeen);

    // A guest's list: locked, saying so.
    game.IsHost = false;
    Settle();
    ClickSetting(list, WeatherRow, 1);
    CHECK(list.ChoiceName(WeatherRow) == "DOWNPOUR");
    CHECK(list.Status() == "THE HOST SETS THESE.");
    game.IsHost = true;
    Settle();

    Click(TabX, ChatTabY);
    Settle();
    CHECK(screen.FrontTab == MCSessionTab::Chat);
    CHECK(!list.ShowWindow);
    CHECK(GlobalLog()->ChatWindow->ShowWindow);
}

namespace
{
    /// <summary>
    /// The hash a game's file report gives <paramref name="missionFile"/> when its user folder holds a gamesys.fit of
    /// its own (the install's and a line more), worked out in a context of its own over the game's archives.
    /// </summary>
    uint32_t HashWithOwnGameSystem(std::string_view missionFile)
    {
        const std::string gameSystem = GamePath(MissionPath, "gamesys", ".fit");
        MCFile original;
        REQUIRE_EQ(original.Open(gameSystem), 0);
        std::vector<uint8_t> contents(original.GetLength());
        original.Read(contents);
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "mc_tests_own_gamesys";
        std::filesystem::create_directories(folder / "data" / "missions");
        {
            std::ofstream own(folder / "data" / "missions" / "gamesys.fit", std::ios::binary);
            own.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
            own << "\r\n// the player's own\r\n";
        }

        const std::filesystem::path gameRoot = MCFileSystem::GameRoot();
        uint32_t hash = 0;
        {
            MCTestContextScope scope;
            MCFileSource& files = scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
            files.SetGameRoot(gameRoot);
            files.SetUserRoot(folder);
            hash = MCMissionDataHash(missionFile);
        }

        std::error_code error;
        std::filesystem::remove_all(folder, error);
        return hash;
    }

    /// <summary>The original's "checksum" of <paramref name="missionFile"/> (an older build's report): its first four bytes.</summary>
    uint32_t FirstFourBytes(std::string_view missionFile)
    {
        MCFile file;
        REQUIRE_EQ(file.Open(missionFile), 0);
        uint32_t value = 0;
        file.Read(std::span(reinterpret_cast<uint8_t*>(&value), sizeof(value)));
        return value;
    }

    /// <summary>Clicks <paramref name="dialog"/>'s OK.</summary>
    void ClickOk(MCReusableDialog& dialog)
    {
        MCLogDialogButton& ok = *dialog.OkButton;
        Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
    }
}

/// <summary>
/// Port: the host checks every player's mission and gameplay data against its own (network/MCGameDataHash.h; a build
/// with MCMatchRule::DataHash sends the hash in its file report). A guest with the same data gets the mission as
/// before; one whose data differ (here its own gamesys.fit) is named and the mission is called off, as for a player
/// without the file; one whose build sends no hash (an older one: the original's first four bytes) is named, and plays.
/// </summary>
TEST_CASE_ISOLATED("game: the session screen names a guest whose game data differ, and one whose game can't check them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(DataHostAddress);
    MCSplashScreen& lanScreen = *GlobalLog()->LanScreen;
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(0);
    lanScreen.ShowGuiWindow(1);
    lanScreen.ShowBlock(0);
    GlobalLog()->SetCurrentScreen(lanScreen);
    GlobalLog()->LogisticsState = 0xb;
    lanScreen.Element<MCLogTextObject>(4)->SetStringBuffer("Commander");
    lanScreen.Element<MCLogTextObject>(10)->SetStringBuffer("Data game");
    lanScreen.Element<MCLogTextObject>(11)->SetStringBuffer("6");
    CreateSession();
    Settle();
    REQUIRE(manager.DirectPlay != nullptr);

    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay, DataHostAddress));
    Settle();
    Go();
    Settle();
    // The guest's game joins the group the inquiries go to as the host's setup arrives; the test's guest doesn't.
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, guest.Id()));

    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    const uint32_t hostId = manager.MyPlayer()->Id;
    constexpr std::string_view Mission = "ARENA";
    const std::string missionFile = GamePath(SavePath, Mission, ".mpk");
    const uint32_t same = MCMissionDataHash(missionFile);
    const uint32_t ownGameSystem = HashWithOwnGameSystem(missionFile);
    REQUIRE(same != 0);
    REQUIRE(ownGameSystem != 0);
    REQUIRE(ownGameSystem != same);

    // The same data: the check closes itself and the guest is told to load the mission.
    guest.SendRules(hostId, MCMatchRule::Supported);
    guest.AnswerFiles(same);
    Settle();
    screen.LoadMission(Mission);
    CHECK(SettleUntil([&] { return guest.MissionsLoaded() == 1; }));
    CHECK(!dialog.IsShowing());

    // Its own gamesys.fit: named, as good as without the mission, which is called off.
    guest.AnswerFiles(ownGameSystem);
    screen.LoadMission(Mission);
    CHECK(SettleUntil([&] { return dialog.Text != LoadGameString(0xac, 0xfe); }));
    CHECK(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string("These players' game data differs from yours: Guest"));
    SaveShot("data check 0 differs", ScreenHash());
    ClickOk(dialog);
    Settle();
    CHECK(!dialog.IsShowing());
    CHECK_EQ(guest.MissionsLoaded(), 1);

    // An older build (no hash, the original's first four bytes): named, and the mission loads on the host's OK.
    guest.SendRules(hostId, MCMatchRule::Supported & ~MCMatchRule::DataHash);
    guest.AnswerFiles(FirstFourBytes(missionFile));
    Settle();
    screen.LoadMission(Mission);
    CHECK(SettleUntil([&] { return dialog.Text != LoadGameString(0xac, 0xfe); }));
    CHECK(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string("Can't check these players' game data: Guest. Tell them to upgrade."));
    SaveShot("data check 1 unchecked", ScreenHash());
    CHECK_EQ(guest.MissionsLoaded(), 1);
    ClickOk(dialog);
    CHECK(SettleUntil([&] { return guest.MissionsLoaded() == 2; }));
    CHECK(!dialog.IsShowing());

    // Without the file, as before: the original's text.
    guest.SendRules(hostId, MCMatchRule::Supported);
    guest.AnswerFiles(0);
    Settle();
    screen.LoadMission(Mission);
    CHECK(SettleUntil([&] { return dialog.Text != LoadGameString(0xac, 0xfe); }));
    CHECK_EQ(dialog.Text, LoadGameString(0xab, 0xfe) + "Guest");
    ClickOk(dialog);
    Settle();
    CHECK_EQ(guest.MissionsLoaded(), 2);
}
