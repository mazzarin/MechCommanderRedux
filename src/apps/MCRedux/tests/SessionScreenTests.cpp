#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "MCScratchFolder.h"
#include "MCTestGuest.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCChooseMapWindow.h"
#include "MCMenusPictures.h"
#include "gui/MCScreenFlow.h"
#include "gui/MCScreenChange.h"
#include "logistics/MCGamesScreen.h"
#include "TestGame.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFile.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCMatchOptionsWindow.h"
#include "logistics/MCRoomScreen.h"
#include "main/MCWeaponBans.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCMenuDialog.h"
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

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

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
    /// <summary>The room's chat test's, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view ChatHostAddress = "127.0.0.1:28884";

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
/// SPEC-menus.md's "Testing" for stage 4's screens: the room (the host's alone with a guest on no team, then the teams
/// picked, points and tech bases set and a chat; with a map; a guest's), CHOOSE MAP's window (from the keys too) and
/// MATCH OPTIONS' (FORCES, PLAY and WEAPONS, the host's and a guest's), each drawn on the software path at 640x480,
/// 1280x720 and 1280x1024 (--shots saves them), and laid out and read at every window the players' reports name. CHOOSE
/// MAP lists the game's room packets by name, each with its file, points and time limit, and CHOOSE loads the one picked
/// into the room as 1998's LOAD MAP did.
/// </summary>
TEST_CASE_ISOLATED(
    "game: the room, CHOOSE MAP and MATCH OPTIONS draw their frames, and lay out and read at every players' window")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    using MCMenusPictures::CheckMenusFrames;
    REQUIRE(MCTestGame::StartLogistics());

    // The LAN games from MULTIPLAYER, hosting on the loopback.
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(HostAddress);
    MCTestGuest::HostOnLan("Bob", "Steel Rain", 6);
    Settle();
    REQUIRE(manager.CurrentSession() != nullptr);
    REQUIRE(manager.DirectPlay != nullptr);
    Guest guest("Ann");
    REQUIRE(guest.Join(*manager.DirectPlay, HostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(guest.Id()); }));
    Settle();
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, guest.Id()));
    const uint32_t hostId = manager.MyPlayer()->Id;
    guest.SendRules(hostId, MCMatchRule::Supported);
    Settle();
    REQUIRE(RoomScreenShown());
    MCRoomScreen& room = *RoomScreen();
    CheckMenusFrames("room host", {0x7750c44d, 0x72d01dac, 0x76fb0409});

    // The teams picked, points and tech bases set, a line each way in the chat.
    room.PressJoin(hostId, MCRoomModel::Team1);
    room.PressJoin(guest.Id(), MCRoomModel::Team2);
    room.TypePoints(MCRoomModel::Team1, "300,000");
    room.TypePoints(MCRoomModel::Team2, "300000");
    room.PickTechBase(MCRoomModel::Team2, MCRoomModel::Clan);
    room.Say("Evening all. Map in a minute.", false);
    // A guest's line as MCLogChat::ProcessChatString writes it: the name in the guest's colour, the text to all.
    const char annColour = static_cast<char>('0' + GlobalLog()->PlayerColors[1]);
    GlobalLog()->Chat->AddLine(std::format("%fc{}Ann: %fc6Clan for me, if nobody minds.", annColour));
    Settle();
    const MCRoomScreen::MCSeen teams = room.Seen();
    CHECK(teams.Team1.front() == "Bob (host)");
    CHECK(teams.Team2.front() == "Ann");
    CHECK_EQ(teams.Points1, std::string("300,000"));
    CHECK_EQ(teams.Tech2, std::string("Clan"));
    CheckMenusFrames("room teams", {0x35271144, 0x79fdecce, 0x92749424});

    // CHOOSE MAP: the room packets by name, the first picked; from the keys, the next.
    MCTestGuest::PressInRoom("choose-map");
    REQUIRE(ChooseMapWindowShown());
    MCChooseMapWindow& choose = *ChooseMapWindow();
    REQUIRE(!choose.Maps().empty());
    CHECK(std::ranges::is_sorted(choose.Maps(), {},
                                 [](const MCMatchMap& map)
                                 {
                                     std::string name = map.Name;
                                     std::ranges::transform(name, name.begin(), [](unsigned char c)
                                                            { return static_cast<char>(std::tolower(c)); });
                                     return name;
                                 }));
    CHECK_EQ(choose.Picked(), 0);
    CheckMenusFrames("room choose map", {0x0836541b, 0x8563f464, 0xe368ce56});
    // The keys' focus is on the picked map: Down picks the next.
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    Settle();
    CHECK_EQ(choose.Picked(), 1);
    CheckMenusFrames("room choose map keys", {0x6b13d73c, 0xfb449dde, 0xd0b511ec});

    // Capture Base: its points and no time limit, CHOOSE loads it (the guest has the same file).
    const auto capture = std::ranges::find(choose.Maps(), std::string("CPTRBASE"), &MCMatchMap::File);
    REQUIRE(capture != choose.Maps().end());
    CHECK_EQ(capture->Name, std::string("Capture Base"));
    CHECK_EQ(MCChooseMapWindow::FactsOf(*capture), std::string("300,000 RP a team. No time limit."));
    choose.Pick(static_cast<int>(capture - choose.Maps().begin()));
    Settle();
    CheckMenusFrames("room choose map picked", {0x25fe5524, 0x6129ac85, 0xc03036c1});
    guest.AnswerFiles(MCMissionDataHash(GamePath(SavePath, "CPTRBASE", ".mpk")));
    choose.PressChoose();
    REQUIRE(SettleUntil([&] { return guest.MissionsLoaded() == 1; }));
    Settle();
    CHECK(!ChooseMapWindowShown());
    CHECK_EQ(room.Seen().Map, std::string("Capture Base"));
    CHECK(!room.Seen().MapText.empty());
    CheckMenusFrames("room map", {0x61e18703, 0x764def3b, 0xe7ff55d0});

    // CHANGE opens it again, the room's map picked; ENTER on it (the keys' focus there) chooses it again, as its row's
    // ENTER does in every list.
    MCTestGuest::PressInRoom("change-map");
    REQUIRE(ChooseMapWindowShown());
    CHECK_EQ(choose.Maps()[static_cast<size_t>(choose.Picked())].File, std::string("CPTRBASE"));
    Settle();
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(SettleUntil([&] { return guest.MissionsLoaded() == 2; }));
    Settle();
    CHECK(!ChooseMapWindowShown());
    CHECK_EQ(room.Seen().Map, std::string("Capture Base"));

    // Escape cancels, the room's map kept.
    MCTestGuest::PressInRoom("change-map");
    REQUIRE(ChooseMapWindowShown());
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    Settle();
    CHECK(!ChooseMapWindowShown());
    CHECK_EQ(room.Seen().Map, std::string("Capture Base"));

    // MATCH OPTIONS, the host's: FORCES, PLAY, WEAPONS.
    MCTestGuest::PressInRoom("view-options");
    REQUIRE(MatchOptionsWindowShown());
    MCMatchOptionsWindow& options = *MatchOptionsWindow();
    CheckMenusFrames("match options forces", {0xc6c446a9, 0x78ef9a72, 0xc4fd9bc2});
    options.ShowTab(MCMatchOptionsWindow::MCTab::Play);
    Settle();
    CheckMenusFrames("match options play", {0x4395d487, 0xa38b9622, 0x8c139bc2});
    // Q and E move the tabs, as the briefing's do.
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    Settle();
    CHECK(options.Tab() == MCMatchOptionsWindow::MCTab::Weapons);
    CheckMenusFrames("match options weapons", {0xd51b87ca, 0x5a3deec6, 0x0e6ac3e3});
    options.PressDone();
    Settle();

    // A guest's: the values, the host setting them; the guest's room waits for the host.
    MultiPlayer()->IsHost = false;
    Settle();
    CheckMenusFrames("room guest", {0xef04f5cf, 0x8472c9b5, 0xf95098d5});
    MCTestGuest::PressInRoom("view-options");
    REQUIRE(MatchOptionsWindowShown());
    options.ShowTab(MCMatchOptionsWindow::MCTab::Weapons);
    Settle();
    CheckMenusFrames("match options guest", {0xad5ff921, 0x3f8f9012, 0x7260d083});
    options.PressDone();
    MultiPlayer()->IsHost = true;
    Settle();
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
    constexpr int32_t PilotRow = 8;
    /// <summary>The WEAPONS heading, its families' rows after it (WeaponFamilies' order).</summary>
    constexpr int32_t WeaponsRow = 9;
    constexpr int32_t FirstWeaponRow = 10;
    constexpr int32_t WeaponRows = static_cast<int32_t>(WeaponFamilies.size());
    constexpr std::array RuleRows = {WeatherRow, TimeRow,  BrakingRow, ArtilleryRow, ProbesRow,
                                     SensorsRow, MinesRow, PilotRow,   WeaponsRow};

    /// <summary>Every row held when a player's game has no rule of the match's: the rules' rows and the weapons'.</summary>
    std::vector<int32_t> HeldRows()
    {
        std::vector<int32_t> rows(RuleRows.begin(), RuleRows.end());

        for (int32_t row = FirstWeaponRow; row < FirstWeaponRow + WeaponRows; row++)
        {
            rows.push_back(row);
        }

        return rows;
    }
}

/// <summary>
/// The remade room's boxes take what the 1998 room's did. The chat sends what the 1998 chat box could
/// (MCLogChatInput): its characters one byte each, the accented 0xbf-0xfd as those bytes, '%' (the chat formatter's
/// code) and the rest left out, and shows that line back with its accents, as it does the chat's names and its own
/// lines. Enter in the box says what's typed (the box marked data-line: Enter is RmlUi's, not the next control's). The points take seven digits at most, as the 1998 RP box (whose tick would send more cut to seven).
/// </summary>
TEST_CASE_ISOLATED("game: the room's chat and points take what the 1998 room's boxes did")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MultiPlayer()->SessionManager->ConnectTcp(ChatHostAddress);
    MCTestGuest::HostOnLan("Bob", "Chat game", 6);
    REQUIRE(SettleUntil([] { return RoomScreenShown(); }));
    MCRoomScreen& room = *RoomScreen();

    // "Olé" in UTF-8, a '%' and the euro sign, which the 1998 box hadn't.
    room.Say("Ol\xc3\xa9, 100% sure \xe2\x82\xac!", false);
    Settle();
    REQUIRE(GlobalLog()->Chat != nullptr);
    REQUIRE(!GlobalLog()->Chat->Lines.empty());
    CHECK(GlobalLog()->Chat->Lines.back().Text.ends_with("Ol\xe9, 100 sure !"));
    const std::vector<std::string> chat = room.Seen().Chat;
    CHECK(std::ranges::find(chat, std::string("Bob: Ol\xc3\xa9, 100 sure !")) != chat.end());

    // A name with the chat's accented bytes (an MCO player's "René") and a game's line with one: shown in UTF-8 too.
    GlobalLog()->Chat->AddLine(std::format("%fc{}", GlobalLog()->PlayerColors[0]) + "Ren\xe9: %fc6hi");
    GlobalLog()->Chat->AddLine("Ren\xe9 joined.");
    Settle();
    const std::vector<std::string> accented = room.Seen().Chat;
    CHECK(std::ranges::find(accented, std::string("Ren\xc3\xa9: hi")) != accented.end());
    CHECK(std::ranges::find(accented, std::string("Ren\xc3\xa9 joined.")) != accented.end());

    // Typed into the chat's box and Enter, as a player says a line: said, the box emptied, the keys still in it.
    Rml::Element* say = MCTestGuest::ElementOf(room, "say");
    REQUIRE(say != nullptr);
    MCTestGuest::RealClickOn(*say);
    MCTestGuest::RealType("Ready when you are");
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    Settle();
    const std::vector<std::string> typed = room.Seen().Chat;
    CHECK(std::ranges::find(typed, std::string("Bob: Ready when you are")) != typed.end());
    CHECK_EQ(say->GetAttribute<Rml::String>("value", "unread"), std::string());
    REQUIRE(say->GetContext() != nullptr);
    CHECK(say->GetContext()->GetFocusElement() == say);

    room.TypePoints(MCRoomModel::Team1, "9,999,999");
    Settle();
    CHECK_EQ(room.Seen().Points1, std::string("9,999,999"));
    room.TypePoints(MCRoomModel::Team1, "12,345,678");
    Settle();
    CHECK_EQ(GlobalLog()->Room.Team1Points, 9'999'999);
    CHECK_EQ(room.Seen().Points1, std::string("9,999,999"));
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
/// Port: MATCH OPTIONS (SPEC-menus.md's stage 4: a window over the remade room, the room's CHANGE) shows the match's
/// options on FORCES, PLAY and WEAPONS, the 1998 tab's rows and no others. The match plays the host's choices as far as
/// every player's build plays them: while the guest's says nothing (as the original's) every option is held at the
/// mission's own (or off), saying whose game hasn't it; a build of the braking's round frees braking; one of the first
/// match options the weather, not the time of day (its sight is a rule of its own); one that plays them all, every
/// option. The host's changes are the match's and go to the guest at once, as do the options when the room opens; a
/// guest reads the values, the host setting them; DONE goes back to the room.
/// </summary>
TEST_CASE_ISOLATED("game: the room's MATCH OPTIONS sets the match's options and sends them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(OptionsHostAddress);
    // MULTIPLAYER opens the LAN games (the personal build).
    MCTestGuest::HostOnLan("Commander", "Options game", 6);
    Settle();
    REQUIRE(manager.DirectPlay != nullptr);
    MCMultiPlayer& game = *MultiPlayer();

    // The guest who joins is shown the room (its game says nothing: an older build's), and the host chooses options its
    // game hasn't (chosen first, they would turn it away).
    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay, OptionsHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(guest.Id()); }));
    game.MatchOptions = MCMatchOptions{
        .Weather = MCWeatherKind::Rain, .TimeOfDay = MCTimeOfDay::Night, .Rules = MCMatchRule::MechBraking};
    Settle();
    REQUIRE(RoomScreenShown());
    CHECK(!MatchOptionsWindowShown());
    SaveShot("match options 0 the room", ScreenHash());

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

    // The room's CHANGE opens the window at FORCES.
    MCTestGuest::PressInRoom("view-options");
    REQUIRE(MatchOptionsWindowShown());
    MCMatchOptionsWindow& window = *MatchOptionsWindow();
    CHECK(window.Tab() == MCMatchOptionsWindow::MCTab::Forces);
    CHECK(MCMatchOptionsWindow::RowsOf(MCMatchOptionsWindow::MCTab::Forces) == std::vector{PilotRow});
    const std::vector<int32_t> playRows = {WeatherRow, TimeRow,    BrakingRow, ArtilleryRow,
                                           ProbesRow,  SensorsRow, MinesRow};
    CHECK(MCMatchOptionsWindow::RowsOf(MCMatchOptionsWindow::MCTab::Play) == playRows);
    CHECK_EQ(MCMatchOptionsWindow::RowsOf(MCMatchOptionsWindow::MCTab::Weapons).size(),
             static_cast<size_t>(WeaponRows));
    const auto value = [&window](int32_t row) { return window.Seen(row).Value; };
    CHECK_EQ(value(WeatherRow), std::string("Mission's"));
    CHECK_EQ(value(TimeRow), std::string("Mission's"));
    CHECK_EQ(value(BrakingRow), std::string("Off"));

    for (const int32_t row : {ArtilleryRow, ProbesRow, SensorsRow, MinesRow})
    {
        CHECK_EQ(value(row), std::string("On"));
    }

    CHECK_EQ(value(PilotRow), std::string("Elite"));
    CHECK_EQ(window.Seen(PilotRow).Name, std::string("Most skilled pilot"));
    CHECK_EQ(window.Seen(FirstWeaponRow + 1).Name, std::string("SRMs"));
    constexpr std::string_view NotInGuests = "Tell Guest to upgrade.";
    const auto held = [&window](int32_t row) { return window.Seen(row).Held ? window.Seen(row).Line : std::string(); };

    for (const int32_t row : HeldRows())
    {
        if (row != WeaponsRow)
        {
            MCTest::Scope scope(std::to_string(row));
            CHECK_EQ(held(row), std::string(NotInGuests));
            CHECK(!window.Seen(row).Set);
        }
    }

    CHECK_EQ(value(FirstWeaponRow), std::string("On"));

    // A held option's step changes nothing.
    window.Step(WeatherRow, 1);
    Settle();
    CHECK_EQ(value(WeatherRow), std::string("Mission's"));
    CHECK(game.MatchOptions.Weather == MCWeatherKind::Rain);
    SaveShot("match options 1 held", ScreenHash());

    // A build of the braking's round: braking, not the weather or time of day.
    const uint32_t hostId = manager.MyPlayer()->Id;
    guest.SendRules(hostId, MCMatchRule::MechBraking);
    Settle();
    CHECK(held(BrakingRow).empty());
    CHECK(window.Seen(BrakingRow).Set);
    CHECK_EQ(value(BrakingRow), std::string("On"));
    CHECK_EQ(held(WeatherRow), std::string(NotInGuests));
    CHECK_EQ(held(TimeRow), std::string(NotInGuests));
    sentIs(MissionOwn, MissionOwn, MCMatchRule::MechBraking);

    // A build of the first match options: neither the weather (it hasn't its play) nor the time of day (its sight).
    guest.SendRules(hostId, MCMatchRule::MechBraking | MCMatchRule::MatchWeather | MCMatchRule::MatchTimeOfDay);
    Settle();
    CHECK_EQ(held(WeatherRow), std::string(NotInGuests));
    CHECK_EQ(held(TimeRow), std::string(NotInGuests));
    sentIs(MissionOwn, MissionOwn, MCMatchRule::MechBraking);

    // A build of the time of day's sight (night's first version, without the lights): the time of day at that
    // version, not the weather.
    constexpr uint32_t sightRules = MCMatchRule::MechBraking | MCMatchRule::MatchWeather | MCMatchRule::TimeOfDayRules;
    guest.SendRules(hostId, sightRules);
    Settle();
    CHECK_EQ(held(WeatherRow), std::string(NotInGuests));
    CHECK(held(TimeRow).empty());
    CHECK_EQ(value(TimeRow), std::string("Night"));
    sentIs(MissionOwn, static_cast<int8_t>(MCTimeOfDay::Night), MCMatchRule::MechBraking | MCMatchRule::TimeOfDayRules);

    // A build of night's second version (the lights too): night at that version.
    guest.SendRules(hostId, sightRules | MCMatchRule::NightLights);
    Settle();
    CHECK_EQ(held(WeatherRow), std::string(NotInGuests));
    CHECK(held(TimeRow).empty());
    CHECK_EQ(value(TimeRow), std::string("Night"));
    sentIs(MissionOwn, static_cast<int8_t>(MCTimeOfDay::Night),
           MCMatchRule::MechBraking | MCMatchRule::TimeOfDayRules | MCMatchRule::NightLights);

    // A build of the match's first rules (3 Nov, 524): neither the most skilled pilot nor the weapons.
    guest.SendRules(hostId, MCMatchRule::Supported & ~(MCMatchRule::PilotRanks | MCMatchRule::WeaponBans));
    Settle();
    CHECK(held(MinesRow).empty());
    CHECK_EQ(held(PilotRow), std::string(NotInGuests));
    CHECK_EQ(held(FirstWeaponRow), std::string(NotInGuests));
    CHECK_EQ(held(FirstWeaponRow + WeaponRows - 1), std::string(NotInGuests));
    window.Step(FirstWeaponRow, 1);
    CHECK_EQ(value(FirstWeaponRow), std::string("On"));
    window.ShowTab(MCMatchOptionsWindow::MCTab::Weapons);
    Settle();
    SaveShot("match options 1 weapons held", ScreenHash());

    // A build that plays them all: the host's choices, the time of day's sight, the weather's play and the night's
    // lights with them.
    guest.SendRules(hostId, MCMatchRule::Supported);
    Settle();

    for (const int32_t row : HeldRows())
    {
        CHECK(held(row).empty());
    }

    CHECK_EQ(value(WeatherRow), std::string("Rain"));
    CHECK_EQ(value(TimeRow), std::string("Night"));
    // A family's row says what it covers once it's no longer held.
    CHECK_EQ(window.Seen(FirstWeaponRow + 1).Line, std::string("Streak racks too."));
    sentIs(static_cast<int8_t>(MCWeatherKind::Rain), static_cast<int8_t>(MCTimeOfDay::Night),
           MCMatchRule::MechBraking | MCMatchRule::BrakingV2 | MCMatchRule::TimeOfDayRules | MCMatchRule::WeatherPlay |
               MCMatchRule::NightLights);
    // The guest heard every player's rules from the host (its own, and the host's).
    const std::optional<MCMPPlayersRulesMessage> players = guest.PlayersRules();
    REQUIRE(players.has_value());
    CHECK_EQ(players->Count, 2);

    // RAIN, DOWNPOUR; NIGHT, MISSION'S; braking off; artillery off: as the host's controls move them.
    window.ShowTab(MCMatchOptionsWindow::MCTab::Play);
    Settle();
    window.Step(WeatherRow, 1);
    window.Step(TimeRow, 1);
    window.Step(BrakingRow, 1);
    window.Step(ArtilleryRow, 1);
    Settle();
    CHECK_EQ(value(ArtilleryRow), std::string("Off"));
    CHECK_EQ(value(ProbesRow), std::string("On"));
    CHECK_EQ(value(SensorsRow), std::string("On"));
    CHECK_EQ(value(WeatherRow), std::string("Downpour"));
    CHECK_EQ(value(TimeRow), std::string("Mission's"));
    CHECK_EQ(value(BrakingRow), std::string("Off"));
    CHECK(game.MatchOptions.Weather == MCWeatherKind::Downpour);
    CHECK(!game.MatchOptions.TimeOfDay.has_value());
    CHECK_EQ(game.WantedRules() & MCMatchRule::MechBraking, 0u);
    sentIs(static_cast<int8_t>(MCWeatherKind::Downpour), MissionOwn,
           MCMatchRule::WeatherPlay | MCMatchRule::NoArtillery | MCMatchRule::NightLights);
    SaveShot("match options 2 play", ScreenHash());

    // A click on a switch's choice is the same change: ARTILLERY back on.
    {
        Rml::Element* artillery = window.Document()->get().GetElementById(std::format("opt-{}", ArtilleryRow));
        REQUIRE(artillery != nullptr);
        REQUIRE(artillery->GetNumChildren() == 2);
        MCTestGuest::RealClickOn(*artillery->GetChild(1));
        Settle();
        CHECK_EQ(value(ArtilleryRow), std::string("On"));
        CHECK_EQ(game.WantedRules() & MCMatchRule::NoArtillery, 0u);
        window.Step(ArtilleryRow, 1);
        Settle();
        CHECK_EQ(value(ArtilleryRow), std::string("Off"));
    }

    // VETERAN, then REGULAR; the first weapon family off.
    window.Step(PilotRow, 1);
    CHECK_EQ(value(PilotRow), std::string("Veteran"));
    window.Step(PilotRow, 1);
    CHECK_EQ(value(PilotRow), std::string("Regular"));
    window.Step(FirstWeaponRow, 1);
    CHECK_EQ(value(FirstWeaponRow), std::string("Off"));
    CHECK_EQ(value(FirstWeaponRow + 1), std::string("On"));
    Settle();
    sentIs(static_cast<int8_t>(MCWeatherKind::Downpour), MissionOwn,
           MCMatchRule::WeatherPlay | MCMatchRule::NoArtillery | MCMatchRule::NightLights |
               MCPilotRankRules(MCPilotRank::Regular) | WeaponFamilies.front().Rule);
    window.ShowTab(MCMatchOptionsWindow::MCTab::Weapons);
    Settle();
    SaveShot("match options 3 weapons", ScreenHash());

    // The room's options show what the match plays.
    window.PressDone();
    Settle();
    CHECK(!MatchOptionsWindowShown());
    CHECK(RoomScreenShown());
    const std::vector<std::string> shown = RoomScreen()->Seen().Options;
    CHECK(std::ranges::contains(shown, std::string("Weather | Downpour")));
    CHECK(std::ranges::contains(shown, std::string("Most skilled pilot | Regular")));
    CHECK(std::ranges::contains(shown, std::string("Weapons | 10 of 11")));

    // A guest's window: the values, and who sets them; its steps change nothing.
    game.IsHost = false;
    Settle();
    MCTestGuest::PressInRoom("view-options");
    REQUIRE(MatchOptionsWindowShown());
    window.ShowTab(MCMatchOptionsWindow::MCTab::Play);
    Settle();
    CHECK(!window.Seen(WeatherRow).Set);
    window.Step(WeatherRow, 1);
    Settle();
    CHECK_EQ(value(WeatherRow), std::string("Downpour"));
    Rml::Element* note = window.Document()->get().GetElementById("note");
    REQUIRE(note != nullptr);
    CHECK_EQ(note->GetInnerRML(), Rml::String("The host sets these."));
    SaveShot("match options 4 guest", ScreenHash());

    // Escape goes back to the room.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    Settle();
    CHECK(!MatchOptionsWindowShown());
    CHECK(RoomScreenShown());
    game.IsHost = true;
    Settle();
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

    /// <summary>Clicks <paramref name="dialog"/>'s OK (the remade dialog's, over the remade room).</summary>
    void ClickOk(MCMenuDialog& dialog)
    {
        if (MCRmlDialogs::Shown())
        {
            MCRmlDialogs::Dialog()->PressMain();
            return;
        }

        MCLogDialogButton& ok = *dialog.View()->OkButton;
        Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
    }
}

/// <summary>
/// Port: the host checks every player's mission and gameplay data against its own (network/MCGameDataHash.h; a build
/// with MCMatchRule::DataHashV2 sends the hash in its file report). A guest with the same data gets the mission as
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
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(DataHostAddress);
    // MULTIPLAYER opens the LAN games (the personal build).
    MCTestGuest::HostOnLan("Commander", "Data game", 6);
    Settle();
    REQUIRE(manager.DirectPlay != nullptr);

    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay, DataHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(guest.Id()); }));
    Settle();
    // The guest's game joins the group the inquiries go to as the host's setup arrives; the test's guest doesn't.
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, guest.Id()));

    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    MCMenuDialog& dialog = *GlobalLog()->Message;
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
    guest.SendRules(hostId, MCMatchRule::Supported & ~MCMatchRule::DataHashV2);
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

namespace
{
    /// <summary>The menus' test's loopback address, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view MenuHostAddress = "127.0.0.1:28893";
    /// <summary>The room's test's.</summary>
    constexpr std::string_view RoomHostAddress = "127.0.0.1:28923";
    /// <summary>The room's joiners' test's.</summary>
    constexpr std::string_view JoinHostAddress = "127.0.0.1:28925";

    /// <summary>Clicks the middle of <paramref name="button"/>, as a player does, and lets the game settle.</summary>
    void ClickButton(MCGuiObject& button)
    {
        Click(button.GlobalX() + button.Width() / 2, button.GlobalY() + button.Height() / 2);
        Settle();
    }
}

/// <summary>
/// The menus' way into a LAN game (the personal build): MULTIPLAYER opens the LAN games over TCP/IP with no question
/// (the original's connection screen and its "IPX or TCP?" are gone); their CANCEL goes back to the main menu. Hosting
/// goes straight into the room (the session screen, no ready room), the host alone in it, and its EXIT leaves the game
/// for the games again.
/// </summary>
TEST_CASE_ISOLATED("game: MULTIPLAYER opens the LAN games, and CANCEL goes back a step")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    CHECK(MCTestGuest::OnLan());
    CHECK(MCTestGuest::DialogText().empty());
    CHECK(MultiPlayer()->SessionManager->CurrentConnection == MCNetProtocol::TcpIp);

    // The games' CANCEL: MULTIPLAYER again; its BACK: the main menu, the multiplayer object gone.
    MCGamesScreen& lanScreen = *MCTestGuest::Games(MCGamesWhere::Lan);
    lanScreen.Back();
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnAccess));
    CHECK(MultiPlayer() != nullptr);
    MCTestGuest::ClickOn(MCMainPanel::Multiplayer, "back");
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnMainMenu));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);
    CHECK(MultiPlayer() == nullptr);

    // Hosting from the games: the room, the host alone in it, whose EXIT leaves the game and lists the games again.
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(MenuHostAddress);
    MCTestGuest::HostOnLan("Commander", "Menu game", 2);
    Settle();
    REQUIRE(manager.CurrentSession() != nullptr);
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    CHECK(!GlobalLog()->ConnectScreen->IsShowing());
    CHECK(MCTestGuest::RoomShows(manager.MyPlayer()->Id));
    CHECK(room.StartButton->Disabled);
    CHECK(!room.LoadMissionButton->Disabled);
    SaveShot("menus 0 host alone", ScreenHash());
    // A mission loaded with nobody else in the room: its file check is done at once (nobody to answer).
    room.LoadMission("ARENA");
    REQUIRE(SettleUntil([] { return !GlobalLog()->Message->IsShowing(); }));
    CHECK(!room.LoadMissionButton->Disabled);
    MCTestGuest::PressInRoom("leave");
    CHECK(MCTestGuest::OnLan());
    CHECK(!room.IsShowing());
    CHECK(manager.CurrentSession() == nullptr);
    CHECK(manager.CurrentConnection == MCNetProtocol::TcpIp);
    CHECK_EQ(lanScreen.PlayerName(), "Commander");
}

/// <summary>
/// The room (the personal build): a LAN game's host waits in the session screen; a player who joins is shown the room
/// once its game is up (it pinged) with the host's picks as they stand: the teams, the tech bases, the points and the
/// mission, every player asked for its file again; one who leaves is taken off every screen and out of the host's word
/// on the players' rules, and the room goes on; START closes the room.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN room takes players until START, the host's picks kept as they join and leave")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(RoomHostAddress);
    // MULTIPLAYER opens the LAN games (the personal build).
    MCTestGuest::HostOnLan("Commander", "Room game", 6);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    MCMenuDialog& dialog = *GlobalLog()->Message;
    const uint32_t hostId = manager.MyPlayer()->Id;
    constexpr std::string_view Mission = "ARENA";
    const uint32_t sameData = MCMissionDataHash(GamePath(SavePath, Mission, ".mpk"));
    REQUIRE(sameData != 0);
    const auto openToPlayers = [&manager]
    { return (manager.CurrentSession()->SessionDesc.dwFlags & DPSESSION_NEWPLAYERSDISABLED) == 0; };
    CHECK(openToPlayers());

    // ANN joins and is shown the room; the host picks the teams, Inner Sphere for team 2, the mission and the points.
    // (Their joins say their rules, as this build's do, so the host knows them before it asks for the mission's file.)
    Guest ann("ANN", MCMatchRule::Supported);
    REQUIRE(ann.Join(*manager.DirectPlay, RoomHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && ann.SessionScreens() == 1; }));
    // The guest's game joins the group the inquiries go to as the host's setup arrives; the test's guest doesn't.
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, ann.Id()));
    ann.AnswerFiles(sameData);
    room.AssignPlayer(hostId, 1, 0, false);
    room.AssignPlayer(ann.Id(), 2, 0, false);
    room.SetTeamTechBase(2, 1);
    room.LoadMission(Mission);
    REQUIRE(SettleUntil([&] { return ann.MissionsLoaded() == 1 && !dialog.IsShowing(); }));
    room.SetTeam1RP(12000);
    room.SetTeam2RP(9000);
    Settle();
    CHECK(!room.StartButton->Disabled);

    // CAL joins: not shown while its game is on its way (and START waits for it), then shown the room as it stands.
    Guest cal("CAL", MCMatchRule::Supported);
    REQUIRE(cal.Join(*manager.DirectPlay, RoomHostAddress, false));
    Settle();
    CHECK(!MCTestGuest::RoomShows(cal.Id()));
    CHECK(room.StartButton->Disabled);
    CHECK_EQ(ann.SessionScreens(), 1);
    REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, cal.Id()));
    cal.AnswerFiles(sameData);
    cal.Ping();
    REQUIRE(SettleUntil(
        [&]
        {
            const std::shared_ptr<MCFidpPlayer> player = manager.GetPlayer(cal.Id());
            return player != nullptr && player->HeardPing;
        }));
    const uint32_t pinged = MCPort::Milliseconds();
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(cal.Id()) && cal.SessionScreens() == 1; }));
    // Shown as its game is up: on the host's next look (twice a second), not its wait for a game that never says so.
    CHECK(MCPort::Milliseconds() - pinged < 1000);
    // The room and the mission go to ANN again as well: on its own link, so not always by the time CAL has its (under
    // a loaded machine ANN's came a frame later: the suite's 12 jobs failed here one run in a few).
    REQUIRE(SettleUntil([&] { return cal.MissionsLoaded() == 1 && ann.MissionsLoaded() == 2 && !dialog.IsShowing(); }));
    CHECK_EQ(ann.SessionScreens(), 2);
    CHECK_EQ(ann.MissionsLoaded(), 2);
    CHECK(MCSessionScreen::Room().Team1Players[0] == hostId);
    CHECK(MCSessionScreen::Room().Team2Players[0] == ann.Id());
    CHECK_EQ(MCSessionScreen::Room().Team1TechBase, 1);
    CHECK_EQ(MCSessionScreen::Room().Team2TechBase, 1);
    CHECK_EQ(std::string(room.Team1RPText->Text()), "12000");
    CHECK_EQ(std::string(room.Team2RPText->Text()), "9000");
    CHECK(!MCSessionScreen::Room().MissionFile.empty());
    SaveShot("room 0 a player joined", ScreenHash());

    // CAL unpicked holds START; on team 2 it lights.
    CHECK(room.StartButton->Disabled);
    room.AssignPlayer(cal.Id(), 2, 1, false);
    CHECK(!room.StartButton->Disabled);

    // ANN leaves: taken off the screen, the room goes on with CAL on team 2 (no "has left the game").
    ann.Leave();
    REQUIRE(SettleUntil([&] { return !MCTestGuest::RoomShows(ann.Id()); }));
    Settle();
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    CHECK(!dialog.IsShowing());
    CHECK(MCSessionScreen::Room().Team2Players[0] == MCSessionScreen::NoPlayer);
    CHECK(MCSessionScreen::Room().Team2Players[1] == cal.Id());
    CHECK(!room.StartButton->Disabled);
    CHECK(openToPlayers());
    SaveShot("room 1 a player left", ScreenHash());

    // DAN joins, and leaves before it answers for the mission's file (the test's guest is never asked: it isn't in the
    // group the inquiries go to): the answers go on without it, and the others get the mission.
    Guest dan("DAN", MCMatchRule::Supported);
    REQUIRE(dan.Join(*manager.DirectPlay, RoomHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(dan.Id()) && dialog.IsShowing(); }));
    // The host's word on every player's rules names the players in the room, not ANN, who left.
    REQUIRE(SettleUntil([&] { return dan.PlayersRules().has_value(); }));
    const MCMPPlayersRulesMessage said = *dan.PlayersRules();
    const auto named = [&said](uint32_t id)
    {
        return std::ranges::any_of(std::span(said.Players).first(said.Count),
                                   [id](const MCMPPlayersRulesMessage::Said& player) { return player.PlayerId == id; });
    };

    CHECK_EQ(said.Count, 3);
    CHECK(named(hostId));
    CHECK(named(cal.Id()));
    CHECK(named(dan.Id()));
    CHECK(!named(ann.Id()));
    Settle();
    CHECK(dialog.IsShowing());
    const int32_t calMissions = cal.MissionsLoaded();
    dan.Leave();
    REQUIRE(SettleUntil([&] { return !dialog.IsShowing() && cal.MissionsLoaded() == calMissions + 1; }));
    CHECK(!MCTestGuest::RoomShows(dan.Id()));
    CHECK(!MCSessionScreen::Room().MissionFile.empty());
    CHECK(MCSessionScreen::Room().Team2Players[1] == cal.Id());
    CHECK(!room.StartButton->Disabled);

    // START closes the room.
    MCTestGuest::PressInRoom("begin");
    CHECK(GlobalLog()->CurrentScreen() != GlobalLog()->SessionScreen);
    CHECK(manager.SessionLocked());
    CHECK(!openToPlayers());
}

/// <summary>
/// The room's joiners (the personal build), checked at the door: this build's join says its rules, so the host knows
/// them before the player is made. Before the host chooses anything a game joins whatever it lacks, the option it lacks
/// held for it ("TELL ... TO UPGRADE."); an older build's join (release 1's, upstream's) says nothing, and its rules
/// come once it is in, if ever. The room plays the host's choice at the latest version every game in it plays (night
/// at version 1 with release 1's game in the room), and a joiner that can't play that value at that version is refused
/// before it is in the room, its game told which and the host's chat saying so, the room never showing it or telling
/// anyone it left: a game whose night is a version behind, for night (not dusk, the same at every version); an older
/// build's join for every option chosen (it can't say what it has). A game that keeps an earlier version's play joins
/// a room at it. An option the room doesn't play (held for a player in it) refuses no one.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN room refuses at the door a joiner whose game can't play a value the host chose")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCMultiPlayer& game = *MultiPlayer();
    MCSessionManager& manager = *game.SessionManager;
    manager.ConnectTcp(JoinHostAddress);
    // MULTIPLAYER opens the LAN games (the personal build).
    MCTestGuest::HostOnLan("Commander", "Joiners' game", 6);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    MCLogChat& chat = *GlobalLog()->Chat;
    const uint32_t hostId = manager.MyPlayer()->Id;
    constexpr uint32_t WithoutArtillery = MCMatchRule::Supported & ~MCMatchRule::NoArtillery;
    // A game whose night is a version behind this build's: its sight, without the lights.
    constexpr uint32_t NightBehind = MCMatchRule::Supported & ~MCMatchRule::NightLights;
    // The chat's last line, its spaces and the breaks its history wraps it at left out.
    const auto squeezed = [](std::string text)
    {
        std::erase_if(text, [](unsigned char c) { return c <= ' '; });
        return text;
    };

    const auto lastLine = [&chat, &squeezed]
    { return squeezed(chat.Lines.empty() ? std::string() : chat.Lines.back().Text); };
    const auto players = [&manager] { return manager.GetPlayers(nullptr).size(); };
    // What a guest was last sent: the time of day it plays, and whether with the night's lights.
    const auto sentTime = [](Guest& guest, MCTimeOfDay time, bool lights)
    {
        const std::optional<MCMPMatchOptionsMessage> sent = guest.MatchOptions();
        return sent.has_value() && sent->TimeOfDay == static_cast<int8_t>(time) &&
               ((sent->Rules & MCMatchRule::NightLights) != 0) == lights;
    };

    const auto choose = [&game](MCMatchOptions options)
    {
        game.MatchOptions = options;
        game.SendMatchOptions();
    };

    // ANN joins before the host has chosen anything, its game without ARTILLERY: let in, its rules known from its join
    // (its game never sends them here). EVE's is release 1's, whose join says nothing: let in, and its rules known
    // once its game says them in the room. ARTILLERY is held for ANN; TIME OF DAY isn't (release 1 plays night's first
    // version).
    Guest ann("ANN", WithoutArtillery);
    REQUIRE(ann.Join(*manager.DirectPlay, JoinHostAddress));
    REQUIRE(SettleUntil([&] { return manager.GetPlayer(ann.Id()) != nullptr; }));
    CHECK(game.PlayersWithout(MCMatchRule::NoArtillery) == std::vector{ann.Id()});
    CHECK(game.PlayersWithout(WithoutArtillery).empty());
    Guest eve("EVE");
    REQUIRE(eve.Join(*manager.DirectPlay, JoinHostAddress));
    // Both shown the room with their games up: a slow machine hears EVE's ping a frame after ANN's, and showing the room
    // again for it after the click below would put the screen back on its chat tab.
    REQUIRE(
        SettleUntil([&] { return MCTestGuest::RoomShowsReady(ann.Id()) && MCTestGuest::RoomShowsReady(eve.Id()); }));
    CHECK(game.PlayersWithout(MCMatchRule::WeatherRules) == std::vector{eve.Id()});
    eve.SendRules(hostId, MCTestGuest::ReleaseOneRules);
    REQUIRE(SettleUntil([&] { return game.PlayersWithout(MCMatchRule::WeatherRules).empty(); }));
    CHECK(game.PlayersWithout(MCMatchRule::NightLights) == std::vector{eve.Id()});
    MCTestGuest::PressInRoom("view-options");
    REQUIRE(MatchOptionsWindowShown());
    const MCMatchOptionsWindow& window = *MatchOptionsWindow();
    CHECK(!window.Seen(TimeRow).Held);
    CHECK(!window.Seen(WeatherRow).Held);
    CHECK(window.Seen(ArtilleryRow).Held);
    CHECK_EQ(window.Seen(ArtilleryRow).Line, std::string("Tell ANN to upgrade."));
    MatchOptionsWindow()->PressDone();
    Settle();

    // The host chooses night: played at version 1 (no lights). FAY's game, this build's, keeps version 1's play: let in.
    choose(MCMatchOptions{.TimeOfDay = MCTimeOfDay::Night});
    REQUIRE(SettleUntil([&] { return sentTime(eve, MCTimeOfDay::Night, false); }));
    Guest fay("FAY", MCMatchRule::Supported);
    REQUIRE(fay.Join(*manager.DirectPlay, JoinHostAddress));
    CHECK(fay.Refusal().empty());
    REQUIRE(SettleUntil([&] { return sentTime(fay, MCTimeOfDay::Night, false); }));

    // EVE and FAY leave: the room plays night at version 2. The host chooses snow too, and ARTILLERY OFF, held for ANN.
    eve.Leave();
    fay.Leave();
    REQUIRE(SettleUntil([&] { return !MCTestGuest::RoomShows(eve.Id()) && !MCTestGuest::RoomShows(fay.Id()); }));
    choose(MCMatchOptions{
        .Weather = MCWeatherKind::Snow, .TimeOfDay = MCTimeOfDay::Night, .Rules = MCMatchRule::NoArtillery});
    const auto annPlays = [&]
    {
        const std::optional<MCMPMatchOptionsMessage> sent = ann.MatchOptions();
        return sentTime(ann, MCTimeOfDay::Night, true) && sent->Weather == static_cast<int8_t>(MCWeatherKind::Snow) &&
               (sent->Rules & MCMatchRule::NoArtillery) == 0;
    };

    REQUIRE(SettleUntil(annPlays));
    const int32_t annScreens = ann.SessionScreens();
    const size_t linesBefore = chat.Lines.size();
    const size_t inRoom = players();

    // BOB's night is a version behind: refused at the door, told why, never in the room; the room plays what it did
    // and shows itself to no one again.
    Guest bob("BOB", NightBehind);
    CHECK(!bob.Join(*manager.DirectPlay, JoinHostAddress));
    CHECK_EQ(bob.Refusal(), std::string("TIME OF DAY"));
    Settle();
    CHECK_EQ(players(), inRoom);
    CHECK_EQ(lastLine(), squeezed("BOB CAN'T JOIN: BOB'S GAME DOESN'T HAVE THIS MATCH'S TIME OF DAY."));
    CHECK_EQ(chat.Lines.size(), linesBefore + 1);
    CHECK_EQ(ann.SessionScreens(), annScreens);
    CHECK(annPlays());

    // CAL's is an older build's, whose join says nothing: it counts as having neither option.
    Guest cal("CAL");
    CHECK(!cal.Join(*manager.DirectPlay, JoinHostAddress));
    CHECK_EQ(cal.Refusal(), std::string("WEATHER AND TIME OF DAY"));
    Settle();
    CHECK_EQ(players(), inRoom);
    CHECK_EQ(lastLine(), squeezed("CAL CAN'T JOIN: CAL'S GAME DOESN'T HAVE THIS MATCH'S WEATHER AND TIME OF DAY."));

    // At dusk, the same at every version, GUS's game (its night a version behind, as BOB's) is let in; the host's
    // night then plays at version 1.
    choose(MCMatchOptions{
        .Weather = MCWeatherKind::Snow, .TimeOfDay = MCTimeOfDay::Dusk, .Rules = MCMatchRule::NoArtillery});
    REQUIRE(SettleUntil([&] { return sentTime(ann, MCTimeOfDay::Dusk, true); }));
    Guest gus("GUS", NightBehind);
    REQUIRE(gus.Join(*manager.DirectPlay, JoinHostAddress));
    CHECK(gus.Refusal().empty());
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(gus.Id()) && sentTime(gus, MCTimeOfDay::Dusk, false); }));
    choose(MCMatchOptions{
        .Weather = MCWeatherKind::Snow, .TimeOfDay = MCTimeOfDay::Night, .Rules = MCMatchRule::NoArtillery});
    REQUIRE(SettleUntil([&] { return sentTime(ann, MCTimeOfDay::Night, false); }));

    // DAN's game lacks ARTILLERY too, which the room doesn't play (it is held for ANN): let in and shown the room, its
    // options with it, the picks kept.
    room.AssignPlayer(ann.Id(), 2, 0, false);
    Guest dan("DAN", WithoutArtillery);
    REQUIRE(dan.Join(*manager.DirectPlay, JoinHostAddress));
    CHECK(dan.Refusal().empty());
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(dan.Id()) && sentTime(dan, MCTimeOfDay::Night, false); }));
    CHECK(MCSessionScreen::Room().Team2Players[0] == ann.Id());
    CHECK_EQ(window.Seen(ArtilleryRow).Line, std::string("Tell ANN and DAN to upgrade."));
}

namespace
{
    /// <summary>The rollover room test's port, and its host's role and the folder its two games signal through.</summary>
    constexpr std::string_view RolloverHostAddress = "127.0.0.1:28967";
    constexpr std::string_view RolloverRoleOption = "rollover-role";
    constexpr std::string_view RolloverFolderOption = "rollover-folder";
    /// <summary>The files the two games signal by: the host has picked; the guest has seen the picks.</summary>
    constexpr std::string_view PickedSignal = "picked";
    constexpr std::string_view SeenSignal = "seen";
    /// <summary>The mission the room's host picks.</summary>
    constexpr std::string_view RolloverMission = "ARENA";
    /// <summary>How long a game waits for the other process (it boots the game first).</summary>
    constexpr std::chrono::seconds PeerWait{90};

    /// <summary>Runs frames until <paramref name="done"/>, for <see cref="PeerWait"/> at most; whether it did.</summary>
    bool SettleLong(const std::function<bool()>& done)
    {
        const auto deadline = std::chrono::steady_clock::now() + PeerWait;

        while (!done())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            MCTestGuest::Step();
        }

        return true;
    }

    /// <summary>The player of the session named <paramref name="name"/>, if one is.</summary>
    std::shared_ptr<MCFidpPlayer> PlayerNamed(std::string_view name)
    {
        for (const auto& player : MultiPlayer()->SessionManager->GetPlayers(nullptr))
        {
            if (player != nullptr && player->Name == name)
            {
                return player;
            }
        }

        return nullptr;
    }

    /// <summary>Whether the room's chat has a line holding <paramref name="text"/>.</summary>
    bool ChatSays(std::string_view text)
    {
        return std::ranges::any_of(GlobalLog()->Chat->Lines, [&](const MCLogChat::HistoryLine& line)
                                   { return line.Text.find(text) != std::string::npos; });
    }

    /// <summary>
    /// The rollover room test's host, BOB, in a process of its own: hosts a LAN room, waits for ANN's game and the
    /// test's CAT, picks the teams (BOB on team 1, ANN and CAT on team 2), the mission and the points, says so, and once
    /// ANN's game has seen it all, leaves the room with EXIT.
    /// </summary>
    void HostTheRollingRoom(const std::filesystem::path& folder)
    {
        REQUIRE(MCTestGame::StartLogistics());
        MCTestGuest::OpenLan();
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        manager.ConnectTcp(RolloverHostAddress);
        // The LAN games (MULTIPLAYER's LAN).
        MCTestGuest::HostOnLan("BOB", "Rolling room", 6);
        Settle();
        REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
        MCSessionScreen& room = *GlobalLog()->SessionScreen;
        REQUIRE(SettleLong(
            [&]
            {
                const auto ann = PlayerNamed("ANN");
                const auto cat = PlayerNamed("CAT");
                return ann != nullptr && cat != nullptr && MCTestGuest::RoomShows(ann->Id) &&
                       MCTestGuest::RoomShows(cat->Id);
            }));
        const uint32_t ann = PlayerNamed("ANN")->Id;
        const uint32_t cat = PlayerNamed("CAT")->Id;
        // The test's CAT doesn't join the group the inquiries go to as ANN's game does (as the host's setup reaches it:
        // the mission waits for that, or ANN would never be asked).
        REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, cat));
        REQUIRE(SettleLong(
            [&]
            {
                const std::shared_ptr<MCFidpGroup> everyone = manager.GetGroup(MultiPlayer()->AllPlayerGroupID);
                return everyone != nullptr && std::ranges::contains(everyone->Players, ann);
            }));
        room.AssignPlayer(manager.MyPlayerID, 1, 0, false);
        room.AssignPlayer(ann, 2, 0, false);
        room.AssignPlayer(cat, 2, 1, false);
        room.SetTeamTechBase(2, 1);
        room.LoadMission(RolloverMission);
        REQUIRE(SettleLong(
            [&] { return !GlobalLog()->Message->IsShowing() && !MCSessionScreen::Room().MissionFile.empty(); }));
        room.SetTeam1RP(12000);
        room.SetTeam2RP(9000);
        Settle();
        std::ofstream(folder / PickedSignal).put('1');
        REQUIRE(SettleLong([&] { return std::filesystem::exists(folder / SeenSignal); }));
        LeaveRoom();
        Settle();
    }
}

/// <summary>
/// Port-only (Redux HD): a LAN room whose host leaves (its EXIT) rolls over to the earliest joined guest's game
/// (SPEC-host-rollover.md). The host, BOB, is another process (this test in its host's role); this game is ANN, and the
/// test's CAT joined after it. ANN's game hosts the room as it stood: BOB gone from it, ANN and CAT where BOB put them,
/// the points and the mission, the chat saying BOB left and that ANN is the server now; CAT's game is shown the room
/// again from ANN's, its player the same. ANN's controls are the host's: CAT moved to team 1 lights START, which plays,
/// closing the room through the session's new hub.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a LAN room whose host leaves rolls over to the earliest guest, whose game hosts it as it stood")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    if (const auto folder = MCTest::Option(RolloverFolderOption);
        folder.has_value() && MCTest::Option(RolloverRoleOption) == "host")
    {
        HostTheRollingRoom(std::filesystem::path(*folder));
        return;
    }

    MCScratchFolder folder("mc_tests_rollover", "room");

    /// <summary>The host's process, told to go on (it waits for ANN's game to have seen its picks) as the test ends.</summary>
    struct HostProcess
    {
        explicit HostProcess(const std::filesystem::path& folder)
            : Folder(folder)
            , End(MCTest::StartPeer(MCTest::CurrentTestName(),
                                    {{std::string(RolloverRoleOption), "host"},
                                     {std::string(RolloverFolderOption), MCFileSystem::ToUtf8(folder)}}))
        {
        }

        ~HostProcess()
        {
            Seen();

            if (End.valid())
            {
                Passed();
            }
        }

        HostProcess(const HostProcess&) = delete;
        HostProcess& operator=(const HostProcess&) = delete;

        void Seen() const { std::ofstream(Folder / SeenSignal).put('1'); }

        /// <summary>Whether its test passed (once it has ended), printing what it said when it didn't.</summary>
        bool Passed()
        {
            const MCTest::PeerResult result = End.get();

            if (!result.first)
            {
                std::cout << "  the room's host's process:\n" << result.second;
            }

            return result.first;
        }

        std::filesystem::path Folder;
        std::future<MCTest::PeerResult> End;
    } bob(folder.Path());

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(RolloverHostAddress);
    std::shared_ptr<MCFidpSession> found;
    REQUIRE(SettleLong(
        [&]
        {
            const auto sessions = manager.GetSessions();
            found = sessions.has_value() && !sessions->empty() ? sessions->front() : nullptr;
            return found != nullptr;
        }));
    const _GUID instance = found->SessionDesc.guidInstance;
    REQUIRE_EQ(manager.JoinSession(instance, "ANN"), 0);
    GlobalLog()->SetUpSessionScreen();
    const uint32_t ann = manager.MyPlayerID;
    Guest cat("CAT", MCMatchRule::Supported);
    MCDirectPlay noHostHere;
    REQUIRE(cat.Join(noHostHere, RolloverHostAddress));
    cat.AnswerFiles(MCMissionDataHash(GamePath(SavePath, RolloverMission, ".mpk")));
    REQUIRE(SettleLong([&] { return std::filesystem::exists(folder.Path() / PickedSignal); }));

    // The host's picks come from its process, as the signal did: its wait.
    MCSessionScreen& room = *GlobalLog()->SessionScreen;
    REQUIRE(SettleLong(
        [&]
        {
            return MCSessionScreen::Room().Team2Players[0] == ann &&
                   MCSessionScreen::Room().Team2Players[1] == cat.Id() &&
                   std::string(room.Team1RPText->Text()) == "12000" && !MCSessionScreen::Room().MissionName.empty();
        }));
    const std::string missionName = MCSessionScreen::Room().MissionName;
    const std::shared_ptr<MCFidpPlayer> host = PlayerNamed("BOB");
    REQUIRE(host != nullptr);
    const uint32_t bobId = host->Id;
    CHECK(MCSessionScreen::Room().Team1Players[0] == bobId);
    CHECK(!MultiPlayer()->IsHost);
    const int32_t catScreens = cat.SessionScreens();
    bob.Seen();

    // BOB's EXIT: the room is ANN's game's now, as it stood.
    REQUIRE(SettleLong([&] { return MultiPlayer()->IsHost && !MCTestGuest::RoomShows(bobId); }));
    REQUIRE(SettleUntil([&] { return cat.SessionScreens() > catScreens && !GlobalLog()->Message->IsShowing(); }));
    Settle();
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    CHECK_EQ(manager.ServerID, ann);
    CHECK(manager.GetPlayer(bobId) == nullptr);
    CHECK(MCSameGuid(manager.CurrentSession()->SessionDesc.guidInstance, instance));
    CHECK(MCTestGuest::RoomShows(cat.Id()));
    CHECK(MCSessionScreen::Room().Team1Players[0] == MCSessionScreen::NoPlayer);
    CHECK(MCSessionScreen::Room().Team2Players[0] == ann);
    CHECK(MCSessionScreen::Room().Team2Players[1] == cat.Id());
    CHECK_EQ(MCSessionScreen::Room().Team2TechBase, 1);
    CHECK_EQ(std::string(room.Team1RPText->Text()), "12000");
    CHECK_EQ(std::string(room.Team2RPText->Text()), "9000");
    CHECK(!MCSessionScreen::Room().MissionFile.empty());
    CHECK_EQ(MCSessionScreen::Room().MissionName, missionName);
    CHECK(ChatSays("BOB has left the game."));
    CHECK(ChatSays("is now the server."));

    // ANN's controls are the host's: CAT to team 1, and START plays, closing the room at the session's new hub.
    room.AssignPlayer(cat.Id(), 1, 0, false);
    Settle();
    REQUIRE(!room.StartButton->Disabled);
    MCTestGuest::PressInRoom("begin");
    CHECK(GlobalLog()->CurrentScreen() != GlobalLog()->SessionScreen);
    CHECK(manager.SessionLocked());
    CHECK_EQ(manager.DirectPlay->SetSessionDesc(manager.CurrentSession()->SessionDesc, 0), uint32_t{DP_OK});
    CHECK(bob.Passed());
}
