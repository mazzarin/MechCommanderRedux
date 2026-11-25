#include "stdafx.h"
#include "MCPort.h"
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
#include "logistics/MCMatchOptionRows.h"
#include "logistics/MCMatchOptionsWindow.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRoomScreen.h"
#include "main/MCWeaponBans.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "network/MCGameDataHash.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementScroll.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/StringUtilities.h>

using namespace MCScreenInput;
using MCTestGuest::Guest;
using MCTestGuest::Host;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;

namespace
{
    /// <summary>1998's NA maps (each one's description says NoArtilleryLine), each with its plain map.</summary>
    struct MCNoArtilleryMap
    {
        std::string_view Plain;
        std::string_view NoArtillery;
    };

    constexpr auto NoArtilleryMaps = std::to_array<MCNoArtilleryMap>({{"BRCHLINE", "BRCLINNA"},
                                                                      {"CPTRBASE", "CPTBASNA"},
                                                                      {"FORT", "FORTNA"},
                                                                      {"INDYSTRK", "INDSTKNA"},
                                                                      {"MANYBASE", "MNYBSENA"},
                                                                      {"POWRSURG", "PWRSRGNA"},
                                                                      {"SWAMPRAT", "SWAMPNA"},
                                                                      {"THREE", "THREENA"}});

    /// <summary>Whether <paramref name="maps"/> has room packet <paramref name="file"/>, whatever its capitals.</summary>
    bool HasMap(const std::vector<MCMatchMap>& maps, std::string_view file)
    {
        return std::ranges::any_of(maps,
                                   [file](const MCMatchMap& map) { return MCPort::StrICmp(map.File, file) == 0; });
    }

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
/// picked, points and tech bases set and a chat; with a map), CHOOSE MAP's window (from the keys too; a LOCKED map's
/// description scrolling, its banners the menus' own) and MATCH OPTIONS' (FORCES, PLAY and WEAPONS), the host's (a
/// guest's are a real guest's: the guest's room test), each drawn on the software path at 640x480, 1280x720 and
/// 1280x1024 (--shots saves them), and laid out and read at every window the players' reports name. CHOOSE MAP lists the game's room packets by name, each with its file, points and time limit,
/// and CHOOSE loads the one picked into the room as 1998's LOAD MAP did.
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

    // Not 1998's NA maps (MATCH OPTIONS' ARTILLERY OFF and PROBES OFF stand in): each one's plain map listed, the NA
    // one not; URBANA, no NA map, listed.
    for (const MCNoArtilleryMap& map : NoArtilleryMaps)
    {
        const MCTest::Scope scope{std::string(map.NoArtillery)};
        CHECK(HasMap(choose.Maps(), map.Plain));
        CHECK(!HasMap(choose.Maps(), map.NoArtillery));
    }

    CHECK(std::ranges::none_of(choose.Maps(), &MCMatchMap::NoArtillery));
    CHECK(HasMap(choose.Maps(), "URBANA"));
    CHECK(std::ranges::is_sorted(choose.Maps(), {},
                                 [](const MCMatchMap& map)
                                 {
                                     std::string name = map.Name;
                                     std::ranges::transform(name, name.begin(), [](unsigned char c)
                                                            { return static_cast<char>(std::tolower(c)); });
                                     return name;
                                 }));
    CHECK_EQ(choose.Picked(), 0);
    CheckMenusFrames("room choose map", {0xdf3df296, 0x5302f216, 0x2602111c});
    // The keys' focus is on the picked map: Down picks the next.
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    Settle();
    CHECK_EQ(choose.Picked(), 1);
    CheckMenusFrames("room choose map keys", {0xe40e0ba7, 0x000b1d7a, 0x99af7468});

    // Capture Base LOCKED: its description longer than its place, scrolling with its rail and fade, its banners the
    // menus' own.
    const auto locked = std::ranges::find(choose.Maps(), std::string("CPTRLOCK"), &MCMatchMap::File);
    REQUIRE(locked != choose.Maps().end());
    choose.Pick(static_cast<int>(locked - choose.Maps().begin()));
    Settle();
    CheckMenusFrames("room choose map locked", {0x9d9ee7e4, 0xff88d356, 0x311edf70});

    // Capture Base: its points and no time limit, CHOOSE loads it (the guest has the same file).
    const auto capture = std::ranges::find(choose.Maps(), std::string("CPTRBASE"), &MCMatchMap::File);
    REQUIRE(capture != choose.Maps().end());
    CHECK_EQ(capture->Name, std::string("Capture Base"));
    CHECK_EQ(MCChooseMapWindow::FactsOf(*capture), std::string("300,000 RP a team. No time limit."));
    choose.Pick(static_cast<int>(capture - choose.Maps().begin()));
    Settle();
    CheckMenusFrames("room choose map picked", {0x29816296, 0xdd65d3b4, 0x1973cb30});
    guest.AnswerFiles(MCMissionDataHash(GamePath(SavePath, "CPTRBASE", ".mpk")));
    choose.PressChoose();
    REQUIRE(SettleUntil([&] { return guest.MissionsLoaded() == 1; }));
    Settle();
    CHECK(!ChooseMapWindowShown());
    CHECK_EQ(room.Seen().Map, std::string("Capture Base"));
    CHECK(!room.Seen().MapText.empty());
    CheckMenusFrames("room map", {0x40f1d13b, 0x764def3b, 0x46d25c5c});

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
    CheckMenusFrames("match options forces", {0x4bd7de8d, 0x78ef9a72, 0x33cf33b2});
    options.ShowTab(MCMatchOptionsWindow::MCTab::Play);
    Settle();
    CheckMenusFrames("match options play", {0xaa9ffec7, 0xa38b9622, 0xe7ec340e});
    // Q and E move the tabs, as the briefing's do.
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    Settle();
    CHECK(options.Tab() == MCMatchOptionsWindow::MCTab::Weapons);
    CheckMenusFrames("match options weapons", {0x024f1962, 0x5a3deec6, 0xb8fdb49b});
    options.PressDone();
    Settle();
}

namespace
{
    /// <summary>The NA map test's host's port, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view NoArtilleryHostPort = "28974";
}

/// <summary>
/// 1998's NA maps are known by their description's line (NoArtilleryLine): the eight, no others. CHOOSE MAP leaves them
/// out (the frames test), but a host on another build may still pick one: a LAN guest's room loads it as before, its
/// name, points and description (the line too), and its game would play it.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN guest loads an NA map its host names, though CHOOSE MAP leaves them out")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // The NA maps by their descriptions, as the game reads every room packet.
    std::vector<std::string> marked;

    for (const MCMatchMap& map : MCMatchMaps())
    {
        if (map.NoArtillery)
        {
            std::string file = map.File;
            std::ranges::transform(file, file.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            marked.push_back(std::move(file));
        }
    }

    std::vector<std::string> named;
    std::ranges::transform(NoArtilleryMaps, std::back_inserter(named),
                           [](const MCNoArtilleryMap& map) { return std::string(map.NoArtillery); });
    std::ranges::sort(marked);
    std::ranges::sort(named);
    CHECK(marked == named);

    // ANN joins BOB's room, whose game (another build's) loads Capture Base's NA map.
    MCTestGuest::OpenLan();
    Host bob;
    REQUIRE(bob.Open(NoArtilleryHostPort, "BOB", "BOB'S GAME"));
    MCTestGuest::JoinFromLan(NoArtilleryHostPort, "ANN");
    const uint32_t ann = MultiPlayer()->SessionManager->MyPlayerID;
    constexpr std::string_view Mission = "CPTBASNA";
    const std::string path = GamePath(SavePath, Mission, ".mpk");
    REQUIRE(path.size() < Host::MaxMissionName);
    bob.SendLoadMission(ann, path);
    MCRoomModel& room = GlobalLog()->Room;
    REQUIRE(SettleUntil([&room] { return !room.LoadedMission.empty(); }));
    Settle();
    CHECK(MCTestGuest::InSession());
    CHECK(MCTestGuest::OnScreen(MCMenuScreens::Room()));
    CHECK_EQ(room.LoadedMission, std::string(Mission));
    CHECK_EQ(room.MissionName, std::string("Capture Base"));
    CHECK_EQ(room.MapFile, std::string("mp0701"));
    CHECK_EQ(room.Team1Points, 300'000);
    CHECK_EQ(room.Team2Points, 300'000);
    CHECK(std::ranges::any_of(room.Description,
                              [](const std::string& line) { return line.find(NoArtilleryLine) != std::string::npos; }));
    bob.Close();
}

namespace
{
    /// <summary>The guest's room test's host's port, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view GuestRoomHostPort = "28978";
    /// <summary>MCLogChat::ProcessChatString's colour for a line to all (its default, 1998's white).</summary>
    constexpr int32_t ChatToAll = -1;

    /// <summary>
    /// The test's host <paramref name="host"/> names room packet <paramref name="mission"/> to player
    /// <paramref name="to"/>, as its CHOOSE MAP does once every player has the file; once the game has read it.
    /// </summary>
    void HostLoads(Host& host, uint32_t to, std::string_view mission)
    {
        const MCTest::Scope scope{std::string(mission)};
        const std::string path = GamePath(SavePath, mission, ".mpk");
        REQUIRE(path.size() < Host::MaxMissionName);
        host.SendLoadMission(to, path);
        REQUIRE(SettleUntil([mission] { return GlobalLog()->Room.LoadedMission == mission; }));
        Settle();
    }

    /// <summary>Whether the remade room shows and what it shows passes <paramref name="test"/>.</summary>
    bool RoomShowsThat(const std::function<bool(const MCRoomScreen::MCSeen&)>& test)
    {
        return RoomScreenShown() && test(RoomScreen()->Seen());
    }

    /// <summary>What the remade room shows now.</summary>
    MCRoomScreen::MCSeen RoomSeen()
    {
        REQUIRE(RoomScreenShown());
        return RoomScreen()->Seen();
    }
}

/// <summary>
/// A LAN guest's room shows the map its host loads as the host's room shows a guest (SPEC-menus.md's room: its picture,
/// name, time limit and description, the mission's points), never "No map yet", and its foot what the guest waits for:
/// the host to choose a map and the teams, what is left of them, then to begin planning. The host's first load, a
/// change of map, and the room shown again (as a host shows it for a player joining or rejoining, and to a rematch's
/// guests back), its map named again; drawn at the frames' windows (the room's frames' guest's), and laid out and read
/// at the players'. A launcher's and MCO's rooms are a guest's room the same way (the same model and screen, the
/// host's same messages). Fails with a room keyed on the host's mission file, which a guest never has.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN guest's room shows the map its host loads, and what it waits for")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    using MCMenusPictures::CheckMenusFrames;
    REQUIRE(MCTestGame::StartLogistics());

    // Ann joins Bob's room: no map, nobody on a team.
    MCTestGuest::OpenLan();
    Host bob;
    REQUIRE(bob.Open(GuestRoomHostPort, "Bob", "Steel Rain"));
    MCTestGuest::JoinFromLan(GuestRoomHostPort, "Ann");
    const uint32_t ann = MultiPlayer()->SessionManager->MyPlayerID;
    bob.SendNumbers(ann, {bob.Id(), ann});
    REQUIRE(SettleUntil([&bob] { return MultiPlayer()->SessionManager->ServerID == bob.Id(); }));
    Settle();
    MCRoomScreen::MCSeen seen = RoomSeen();
    CHECK(!seen.Hosting);
    CHECK_EQ(seen.Map, std::string());
    CHECK_EQ(seen.Status, std::string("Waiting for Bob to choose a map and the teams."));

    // Bob loads Capture Base: its picture, name and description, and its points; no time limit. The teams to come.
    HostLoads(bob, ann, "CPTRBASE");
    seen = RoomSeen();
    CHECK_EQ(seen.Map, std::string("Capture Base"));
    CHECK_EQ(seen.MapPicture, std::format("{}mp0701", MCRmlArt::TerrainScheme));
    CHECK_EQ(seen.MapTime, std::string());
    CHECK(!seen.MapText.empty());
    CHECK_EQ(seen.Points1, std::string("300,000"));
    CHECK_EQ(seen.Points2, std::string("300,000"));
    CHECK_EQ(seen.Status, std::string("Waiting for Bob to choose the teams."));
    CheckMenusFrames("room guest map", {0x1e3bb482, 0x0ec70166, 0x18dcbcc3});

    // Bob on team 1, Ann on team 2 with the Clan's mechs, a line each way: the room waits for Bob to begin planning.
    bob.SendJoinTeam(ann, bob.Id(), MCRoomModel::Team1, 0);
    bob.SendJoinTeam(ann, ann, MCRoomModel::Team2, 0);
    bob.SendTechBase(ann, MCRoomModel::Team2, MCRoomModel::Clan);
    REQUIRE(SettleUntil(
        []
        {
            return RoomShowsThat([](const MCRoomScreen::MCSeen& now)
                                 { return now.Tech2 == "Clan" && now.Pool.empty(); });
        }));
    GlobalLog()->Chat->ProcessChatString(bob.Id(), "Evening all. Map in a minute.", ChatToAll);
    RoomScreen()->Say("Clan for me, if nobody minds.", false);
    Settle();
    seen = RoomSeen();
    CHECK(seen.Team1.front() == "Bob (host)");
    CHECK(seen.Team2.front() == "Ann");
    CHECK_EQ(seen.Map, std::string("Capture Base"));
    CHECK_EQ(seen.Status, std::string("Waiting for Bob to begin planning."));
    CheckMenusFrames("room guest", {0x99e2b5d4, 0x96e2b7e7, 0xeefba5f7});

    // A guest's MATCH OPTIONS: VIEW, the values, the host setting them.
    MCTestGuest::PressInRoom("view-options");
    REQUIRE(MatchOptionsWindowShown());
    MCMatchOptionsWindow& options = *MatchOptionsWindow();
    options.ShowTab(MCMatchOptionsWindow::MCTab::Weapons);
    Settle();
    CheckMenusFrames("match options guest", {0x3e0e1112, 0x2375272f, 0x65ae2f0c});
    options.PressDone();
    Settle();

    // A change of map: 3 is a Magic Number, its picture, points and time limit; the teams kept.
    HostLoads(bob, ann, "THREE");
    seen = RoomSeen();
    CHECK_EQ(seen.Map, std::string("3 is a Magic Number"));
    CHECK_EQ(seen.MapPicture, std::format("{}mp0722", MCRmlArt::TerrainScheme));
    CHECK_EQ(seen.MapTime, std::string("Time limit 15:00."));
    CHECK(!seen.MapText.empty());
    CHECK_EQ(seen.Points1, std::string("600,000"));
    CHECK_EQ(seen.Status, std::string("Waiting for Bob to begin planning."));
    CheckMenusFrames("room guest change map", {0x6b82b674, 0xd92d8408, 0x2b8f982b});

    // The room shown again: afresh, no map and nobody placed. The teams first this time, then the map named again.
    bob.SendRoomAgain(ann);
    REQUIRE(SettleUntil([] { return GlobalLog()->Room.LoadedMission.empty(); }));
    Settle();
    seen = RoomSeen();
    CHECK_EQ(seen.Map, std::string());
    CHECK_EQ(seen.Status, std::string("Waiting for Bob to choose a map and the teams."));
    bob.SendJoinTeam(ann, bob.Id(), MCRoomModel::Team1, 0);
    bob.SendJoinTeam(ann, ann, MCRoomModel::Team2, 0);
    REQUIRE(
        SettleUntil([] { return RoomShowsThat([](const MCRoomScreen::MCSeen& now) { return now.Pool.empty(); }); }));
    CHECK_EQ(RoomSeen().Status, std::string("Waiting for Bob to choose a map."));
    HostLoads(bob, ann, "THREE");
    seen = RoomSeen();
    CHECK_EQ(seen.Map, std::string("3 is a Magic Number"));
    CHECK(!seen.MapText.empty());
    CHECK_EQ(seen.Status, std::string("Waiting for Bob to begin planning."));
    bob.Close();
}

namespace
{
    /// <summary>The time limit test's host's address, and its guest's host's port, so it can run beside the others.</summary>
    constexpr std::string_view TimeLimitHostAddress = "127.0.0.1:29001";
    constexpr std::string_view TimeLimitGuestHostPort = "29003";
    /// <summary>3 is a Magic Number, and its room packet's time limit (MissionTime 900).</summary>
    constexpr std::string_view TimedMission = "THREE";
    constexpr std::string_view TimedLimit = "15:00";
    /// <summary>Capture Base, whose room packet has no MissionTime.</summary>
    constexpr std::string_view UntimedMission = "CPTRBASE";

    /// <summary>
    /// Loads 3 is a Magic Number, then Capture Base, with <paramref name="load"/>: the room's time limit the first's,
    /// then none, as the model keeps it (the 1998 session screen draws it) and as the remade room shows it.
    /// </summary>
    void CheckTimeLimitGoes(const std::function<void(std::string_view)>& load)
    {
        load(TimedMission);
        CHECK_EQ(GlobalLog()->Room.MissionTime, std::string(TimedLimit));
        CHECK_EQ(RoomSeen().MapTime, std::format("Time limit {}.", TimedLimit));
        load(UntimedMission);
        CHECK_EQ(RoomSeen().Map, std::string("Capture Base"));
        CHECK_EQ(GlobalLog()->Room.MissionTime, std::string());
        CHECK_EQ(RoomSeen().MapTime, std::string());
    }
}

/// <summary>
/// OB-195: a map whose room packet has no time limit shows none after a map with one (3 is a Magic Number's 15:00, then
/// Capture Base), in the host's room and in a LAN guest's. Fails with the room keeping the last map's time limit.
/// </summary>
TEST_CASE_ISOLATED("game: a map with no time limit shows none after one with a limit, for the host and a LAN guest")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // Bob hosts alone (the file check done at once) and loads them.
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MultiPlayer()->SessionManager->ConnectTcp(TimeLimitHostAddress);
    MCTestGuest::HostOnLan("Bob", "Time limits", 6);
    REQUIRE(SettleUntil([] { return RoomScreenShown(); }));
    {
        const MCTest::Scope scope{"host"};
        CheckTimeLimitGoes(
            [](std::string_view mission)
            {
                const MCTest::Scope missionScope{mission};
                GlobalLog()->Room.LoadMission(mission);
                REQUIRE(SettleUntil([] { return !GlobalLog()->Message->IsShowing(); }));
                Settle();
            });
    }

    // Bob leaves, and as Ann joins Dan's room, whose host loads them.
    MCTestGuest::PressInRoom("leave");
    REQUIRE(SettleUntil(MCTestGuest::OnLan));
    Host dan;
    REQUIRE(dan.Open(TimeLimitGuestHostPort, "Dan", "Steel Rain"));
    MCTestGuest::JoinFromLan(TimeLimitGuestHostPort, "Ann");
    const uint32_t ann = MultiPlayer()->SessionManager->MyPlayerID;
    dan.SendNumbers(ann, {dan.Id(), ann});
    REQUIRE(SettleUntil([&dan] { return MultiPlayer()->SessionManager->ServerID == dan.Id(); }));
    Settle();
    {
        const MCTest::Scope scope{"guest"};
        CheckTimeLimitGoes([&dan, ann](std::string_view mission) { HostLoads(dan, ann, mission); });
    }

    dan.Close();
}

namespace
{
    /// <summary>The description survey's address, its own so it can run beside the others (--jobs).</summary>
    constexpr std::string_view SurveyHostAddress = "127.0.0.1:29005";
    /// <summary>The descriptions' scrollers: CHOOSE MAP's picked map's, the room's (choose_map.rml's, room.rml's).</summary>
    constexpr const char* PickWordsId = "pick-words";
    constexpr const char* MapWordsId = "map-words";
    /// <summary>How far a scrolled text may be from its foot and still be at it (RmlUi's layout is floats).</summary>
    constexpr float ScrollSlack = 0.5f;

    /// <summary>How a description's text shows its length: whether it's longer than its room, its rail and its fade.</summary>
    struct MCLengthShown
    {
        bool Longer = false;
        bool Rail = false;
        bool Fade = false;
        /// <summary>The keys reach it (the kit's .long).</summary>
        bool Reached = false;
    };

    /// <summary>How <paramref name="screen"/>'s description's text <paramref name="id"/> shows its length now.</summary>
    MCLengthShown LengthShown(const MCRmlScreen& screen, const char* id)
    {
        Rml::Element* words = MCTestGuest::ElementOf(screen, id);
        REQUIRE(words != nullptr);
        Rml::Element* fade = words->GetNextSibling();
        return MCLengthShown{
            .Longer = words->GetScrollHeight() > words->GetClientHeight() + ScrollSlack,
            .Rail = words->GetElementScroll()->GetScrollbarSize(Rml::ElementScroll::VERTICAL) > 0.0f,
            .Fade = fade != nullptr && fade->IsClassSet("fade") && fade->IsVisible(),
            .Reached = words->IsClassSet("long"),
        };
    }

    /// <summary>
    /// What's wrong with how <paramref name="screen"/>'s description's text <paramref name="id"/>, at its top, shows its
    /// length: one longer than its room has its rail, its fade and the keys' reach, one that fits none of them.
    /// </summary>
    std::string LengthProblems(const MCRmlScreen& screen, const char* id)
    {
        const MCLengthShown shown = LengthShown(screen, id);

        if (shown.Rail == shown.Longer && shown.Fade == shown.Longer && shown.Reached == shown.Longer)
        {
            return {};
        }

        return std::format("{} {}: rail {}, fade {}, keys {}; ", id, shown.Longer ? "longer than its room" : "fits",
                           shown.Rail, shown.Fade, shown.Reached);
    }

    /// <summary>
    /// What the layout and legibility checks find in the menus shown now, <paramref name="what"/>, at every window the
    /// players' reports name and the frames' windows, and how <paramref name="screen"/>'s description's text
    /// <paramref name="id"/> shows its length there (LengthProblems): a line each, headed by both (nothing: none).
    /// </summary>
    std::string ProblemsAtEveryWindow(std::string_view what, const MCRmlScreen& screen, const char* id)
    {
        using MCMenusPictures::MCWindowSize;
        std::vector<MCWindowSize> windows(MCMenusPictures::PlayersWindows.begin(),
                                          MCMenusPictures::PlayersWindows.end());

        for (const MCWindowSize& frame : MCMenusPictures::FrameWindows)
        {
            if (std::ranges::none_of(windows, [&frame](const MCWindowSize& window)
                                     { return window.Width == frame.Width && window.Height == frame.Height; }))
            {
                windows.push_back(frame);
            }
        }

        std::string problems;

        for (const MCWindowSize& window : windows)
        {
            MCMenusPictures::MenusAt(window);
            Rml::Context& context = MCMenusPictures::GameMenus().Menus().Context();
            const std::string found =
                MCRmlChecks::BoxProblems(context) +
                MCRmlChecks::Illegible(context, MCRmlMenus::MenusUnit(window.Width, window.Height)) +
                LengthProblems(screen, id);

            if (!found.empty())
            {
                problems += std::format("{} at {}x{}: {}\n", what, window.Width, window.Height, found);
            }
        }

        return problems;
    }

    /// <summary>The name CHOOSE MAP's window shows for the map picked (its document's), none: empty.</summary>
    std::string PickedName(const MCChooseMapWindow& choose)
    {
        const MCOptionalRef<Rml::ElementDocument> document = choose.Document();
        Rml::Element* pick = document.has_value() ? document->get().GetElementById("map-pick") : nullptr;
        Rml::ElementList titles;

        if (pick != nullptr)
        {
            pick->GetElementsByClassName(titles, "map-title");
        }

        return titles.empty() ? std::string() : Rml::StringUtilities::DecodeRml(titles.front()->GetInnerRML());
    }

    /// <summary>
    /// A room packet's description as the room shows it: its paragraphs as one between 1998's banners
    /// (MCRoomScreen::ShowMatch).
    /// </summary>
    std::vector<MCDescriptionLine> RoomText(const MCMatchMap& map)
    {
        return MCDescriptionLines(MCDescriptionRows(map.Description, true));
    }
}

/// <summary>
/// Every room map's description and name, the room packet's own words, lie inside their boxes (no line past a side)
/// and read, at every window the players' reports name and the frames' windows: in CHOOSE MAP with the map picked, and
/// in the room once chosen, the host's and a guest's (MCRmlChecks::BoxProblems, MCRmlChecks::Illegible); a description
/// longer than its room scrolls in it, with its rail and fade, one that fits has neither (LengthProblems). King of the
/// Hill's ran a line 0.6 pixels past its box in the room at 1366x768 (RmlUi's wrap rounds a line's room up to a whole
/// pixel), BLOODGRW's name (one word) past its own at most windows, and 18 maps' descriptions past their room (CPTRLOCK's
/// three lines over MATCH OPTIONS at 1920x1200).
/// </summary>
TEST_CASE_ISOLATED("game: every room map's description lies inside its box in CHOOSE MAP and the room, host and guest")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    // The host alone in the room: no one to ask for the map's file, each one chosen is loaded at once.
    MultiPlayer()->SessionManager->ConnectTcp(SurveyHostAddress);
    MCTestGuest::HostOnLan("Bob", "Steel Rain", 6);
    Settle();
    REQUIRE(RoomScreenShown());
    const MCRoomScreen& room = *RoomScreen();
    std::string problems;
    int32_t surveyed = 0;

    for (bool first = true; const MCMatchMap& map : MCMatchMaps())
    {
        // CHOOSE MAP leaves out 1998's NA maps (a host on another build may still name one: the NA map test).
        if (map.NoArtillery)
        {
            continue;
        }

        MCTest::Scope scope(map.File);
        MCTestGuest::PressInRoom(first ? "choose-map" : "change-map");
        first = false;
        REQUIRE(ChooseMapWindowShown());
        MCChooseMapWindow& choose = *ChooseMapWindow();
        const auto listed = std::ranges::find(choose.Maps(), map.File, &MCMatchMap::File);
        REQUIRE(listed != choose.Maps().end());
        choose.Pick(static_cast<int>(listed - choose.Maps().begin()));
        REQUIRE(SettleUntil([&] { return PickedName(choose) == map.Name; }));
        problems += ProblemsAtEveryWindow(std::format("{} in CHOOSE MAP", map.File), choose, PickWordsId);

        // Chosen: the room shows it, the host's, then a guest's.
        choose.PressChoose();
        REQUIRE(SettleUntil([&] { return !ChooseMapWindowShown() && room.Seen().Map == map.Name; }));
        Settle();
        CHECK(room.Seen().MapText == RoomText(map));
        problems += ProblemsAtEveryWindow(std::format("{} in the host's room", map.File), room, MapWordsId);
        MultiPlayer()->IsHost = false;
        Settle();
        problems += ProblemsAtEveryWindow(std::format("{} in a guest's room", map.File), room, MapWordsId);
        MultiPlayer()->IsHost = true;
        Settle();
        surveyed++;
    }

    CHECK(surveyed > 0);
    CHECK_EQ(problems, "");
}

/// <summary>
/// SPEC-menus.md's room: 1998's asterisk banners in a map's description show as the menus' own. A row of words between
/// asterisks (the LOCKED maps' "LOCKED MISSION", the NA maps' "NO FUNCTIONING ARTILLERY CRATES") is its words, a label
/// between two thin rules; a row of asterisks alone a thin rule; any other banner's row (asterisks at an end) one line,
/// cut at the box's edge; any other row words. A banner ends the paragraph before it, with or without an empty line.
/// </summary>
TEST_CASE("session screen: a map's description shows 1998's asterisk banners as labels and rules")
{
    using enum MCDescriptionKind;
    const auto row = [](MCDescriptionKind kind, std::string_view text)
    { return MCDescriptionRow{.Kind = kind, .Text = std::string(text)}; };

    // The LOCKED maps' banner, whole (CPTRLOCK, KINGLOCK, MANYLOCK, POWRLOCK), and the NA maps' line.
    CHECK(MCDescriptionRowOf("***************** LOCKED MISSION *********************") == row(Label, "LOCKED MISSION"));
    CHECK(MCDescriptionRowOf(" The host cannot change tech base or resource points.") ==
          row(Text, " The host cannot change tech base or resource points."));
    CHECK(MCDescriptionRowOf("******************************************************") == row(Rule, ""));
    CHECK(MCDescriptionRowOf("**NO FUNCTIONING ARTILLERY CRATES**") == row(Label, "NO FUNCTIONING ARTILLERY CRATES"));

    // Blanks round the row and among the marks; a single mark.
    CHECK(MCDescriptionRowOf("  * * LOCKED * *  ") == row(Label, "LOCKED"));
    CHECK(MCDescriptionRowOf("\t* * *") == row(Rule, ""));
    CHECK(MCDescriptionRowOf("*") == row(Rule, ""));

    // Any other banner's row: one line, its words as they are (cut at the box's edge).
    CHECK(MCDescriptionRowOf("**NOTE** Read this first.") == row(Cut, "**NOTE** Read this first."));
    CHECK(MCDescriptionRowOf("Read this *now*") == row(Cut, "Read this *now*"));
    CHECK(MCDescriptionRowOf(" ** A ** B ** ") == row(Cut, "** A ** B **"));

    // Words: marks inside a row, none, or a blank row.
    CHECK(MCDescriptionRowOf("Destroy all *enemy* forces.") == row(Text, "Destroy all *enemy* forces."));
    CHECK(MCDescriptionRowOf("JUMP VARIANTS HIGHLY RECOMMENDED") == row(Text, "JUMP VARIANTS HIGHLY RECOMMENDED"));
    CHECK(MCDescriptionRowOf("   ") == row(Text, "   "));

    // A description's paragraphs and banners (KINGLOCK's), each paragraph its own (CHOOSE MAP's) or those between two
    // banners one (the room's).
    const std::vector<std::string> locked = {"This combat environment is perfectly suited to test",
                                             "our bravery.",
                                             "",
                                             "The objective is to capture the HQ at the top of the",
                                             "hill and hold it until the enemy is dead or the",
                                             "mission clock expires.",
                                             "",
                                             "***************** LOCKED MISSION *********************",
                                             " The host cannot change tech base or resource points.",
                                             "******************************************************",
                                             ""};
    const std::string first = "This combat environment is perfectly suited to test our bravery.";
    const std::string second = "The objective is to capture the HQ at the top of the hill and hold it until the enemy "
                               "is dead or the mission clock expires.";
    const std::string host = " The host cannot change tech base or resource points.";
    using MCRows = std::vector<MCDescriptionRow>;
    const MCRows apart = {row(Text, first), row(Text, second), row(Label, "LOCKED MISSION"), row(Text, host),
                          row(Rule, "")};
    const MCRows joined = {row(Text, first + " " + second), row(Label, "LOCKED MISSION"), row(Text, host),
                           row(Rule, "")};
    CHECK(MCDescriptionRows(locked, false) == apart);
    CHECK(MCDescriptionRows(locked, true) == joined);

    // A banner straight after a paragraph's last line ends it (FORTNA's).
    const std::vector<std::string> fortress = {"the base defenses to our side. However, the enemy had ",
                                               "some codes as well...", "**NO FUNCTIONING ARTILLERY CRATES**"};
    const MCRows fortressRows = {
        row(Text, "the base defenses to our side. However, the enemy had  some codes as well..."),
        row(Label, "NO FUNCTIONING ARTILLERY CRATES")};
    CHECK(MCDescriptionRows(fortress, true) == fortressRows);

    // As the documents bind them: the kinds by name.
    const std::vector<MCDescriptionLine> bound = {{.Text = "a", .Kind = "text"},
                                                  {.Text = "b", .Kind = "label"},
                                                  {.Text = "", .Kind = "rule"},
                                                  {.Text = "*c", .Kind = "cut"}};
    const MCRows kinds = {row(Text, "a"), row(Label, "b"), row(Rule, ""), row(Cut, "*c")};
    CHECK(MCDescriptionLines(kinds) == bound);
}

namespace
{
    /// <summary>The long description test's address, its own so it can run beside the others (--jobs).</summary>
    constexpr std::string_view LongHostAddress = "127.0.0.1:29007";
    /// <summary>
    /// A map whose description is longer than its room in CHOOSE MAP and the room, at every window, with banners
    /// (Capture Base LOCKED); one that fits at every window (Arena).
    /// </summary>
    constexpr std::string_view LongMap = "CPTRLOCK";
    constexpr std::string_view ShortMap = "ARENA";
    /// <summary>The narrowest of the frames' windows, where the descriptions take the most lines.</summary>
    constexpr MCMenusPictures::MCWindowSize NarrowWindow = MCMenusPictures::FrameWindows[0];
    /// <summary>More pages than the long description has.</summary>
    constexpr int32_t MostPages = 8;

    /// <summary>The scroller with the keys' focus: its id; empty with none.</summary>
    std::string FocusId()
    {
        const Rml::Element* focus = MCMenusPictures::GameMenus().Menus().Context().GetFocusElement();
        return focus != nullptr ? focus->GetId() : std::string();
    }
}

/// <summary>
/// SPEC-menus.md's room and CHOOSE MAP: a map's description longer than its place scrolls in it, its last line fading
/// and a rail at its right, as the briefing's orders do; one that fits shows neither. The keys reach a long one (the
/// arrow from CHOOSE MAP's picked map onto it), Down and Up scroll it a line, Page Down a page, its fade gone at its
/// foot, where Down moves the focus on; a new map's description shows from its top.
/// </summary>
TEST_CASE_ISOLATED("game: a map's description longer than its place scrolls with a rail and a fade, by the keys too")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MultiPlayer()->SessionManager->ConnectTcp(LongHostAddress);
    MCTestGuest::HostOnLan("Bob", "Steel Rain", 6);
    Settle();
    REQUIRE(RoomScreenShown());
    const MCRoomScreen& room = *RoomScreen();
    MCTestGuest::PressInRoom("choose-map");
    REQUIRE(ChooseMapWindowShown());
    MCChooseMapWindow& choose = *ChooseMapWindow();
    const auto indexOf = [&choose](std::string_view file)
    {
        const auto found = std::ranges::find(choose.Maps(), file, &MCMatchMap::File);
        REQUIRE(found != choose.Maps().end());
        return static_cast<int>(found - choose.Maps().begin());
    };

    // The short map: no rail, no fade, and the keys pass it by.
    choose.Pick(indexOf(ShortMap));
    Settle();
    MCMenusPictures::MenusAt(NarrowWindow);
    MCLengthShown shown = LengthShown(choose, PickWordsId);
    CHECK(!shown.Longer);
    CHECK(!shown.Rail);
    CHECK(!shown.Fade);
    CHECK(!shown.Reached);

    // The long one, its banners as the menus' own: the rail and the fade.
    choose.Pick(indexOf(LongMap));
    Settle();
    const auto kinds = choose.PickLines() | std::views::transform(&MCDescriptionLine::Kind);
    CHECK(std::ranges::count(kinds, "label") == 1);
    CHECK(std::ranges::count(kinds, "rule") == 1);
    MCMenusPictures::MenusAt(NarrowWindow);
    shown = LengthShown(choose, PickWordsId);
    CHECK(shown.Longer);
    CHECK(shown.Rail);
    CHECK(shown.Fade);
    CHECK(shown.Reached);

    // From the keys: the picked map's row, then Right onto its description (Left back).
    Rml::Element& words = *MCTestGuest::ElementOf(choose, PickWordsId);
    Rml::ElementList rows;
    MCTestGuest::ElementOf(choose, "list")->GetElementsByClassName(rows, "list-row");
    std::erase_if(rows, [](Rml::Element* listed) { return !listed->IsVisible(); });
    REQUIRE(rows.size() == choose.Maps().size());
    Rml::Element* picked = rows[static_cast<size_t>(choose.Picked())];
    picked->Focus(true);
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    Settle();
    REQUIRE_EQ(FocusId(), std::string(PickWordsId));
    MCScreenInput::RealKey(SDL_SCANCODE_LEFT);
    Settle();
    CHECK(MCMenusPictures::GameMenus().Menus().Context().GetFocusElement() == picked);
    CHECK_EQ(choose.Picked(), indexOf(LongMap));
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    Settle();
    REQUIRE_EQ(FocusId(), std::string(PickWordsId));
    const float line = words.GetComputedValues().line_height().value;
    CHECK(words.GetScrollTop() == 0.0f);
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    Settle();
    CHECK(std::abs(words.GetScrollTop() - line) <= ScrollSlack);
    MCScreenInput::RealKey(SDL_SCANCODE_UP);
    Settle();
    CHECK(words.GetScrollTop() == 0.0f);
    CHECK_EQ(FocusId(), std::string(PickWordsId));
    // Page Down a page (the window's height less a line), or to its foot.
    const float page = std::max(words.GetClientHeight() - line, line);
    const float most = words.GetScrollHeight() - words.GetClientHeight();
    MCScreenInput::RealKey(SDL_SCANCODE_PAGEDOWN);
    Settle();
    CHECK(std::abs(words.GetScrollTop() - std::min(page, most)) <= ScrollSlack);

    // At its foot, the fade gone; Down there moves the focus on.
    const auto atFoot = [&words]
    { return words.GetScrollTop() + words.GetClientHeight() >= words.GetScrollHeight() - ScrollSlack; };

    for (int32_t pressed = 0; pressed < MostPages && !atFoot(); ++pressed)
    {
        MCScreenInput::RealKey(SDL_SCANCODE_PAGEDOWN);
        Settle();
    }

    CHECK(atFoot());
    CHECK(!LengthShown(choose, PickWordsId).Fade);
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    Settle();
    CHECK(FocusId() != PickWordsId);

    // Another map's description from its top.
    choose.Pick(indexOf(ShortMap));
    Settle();
    choose.Pick(indexOf(LongMap));
    Settle();
    CHECK(words.GetScrollTop() == 0.0f);

    // Chosen: the room's, the same.
    choose.PressChoose();
    REQUIRE(SettleUntil([&] { return !ChooseMapWindowShown() && !room.Seen().Map.empty(); }));
    Settle();
    MCMenusPictures::MenusAt(NarrowWindow);
    shown = LengthShown(room, MapWordsId);
    CHECK(shown.Longer);
    CHECK(shown.Rail);
    CHECK(shown.Fade);
    CHECK(shown.Reached);
    MCTestGuest::PressInRoom("change-map");
    REQUIRE(ChooseMapWindowShown());
    choose.Pick(indexOf(ShortMap));
    choose.PressChoose();
    REQUIRE(SettleUntil([&] { return !ChooseMapWindowShown() && room.Seen().Map == "Arena"; }));
    Settle();
    MCMenusPictures::MenusAt(NarrowWindow);
    shown = LengthShown(room, MapWordsId);
    CHECK(!shown.Longer);
    CHECK(!shown.Rail);
    CHECK(!shown.Fade);
    CHECK(!shown.Reached);
}

namespace
{
    /// <summary>The MATCH OPTIONS list's rows (logistics/MCMatchOptionRows.cpp): its headings and its options.</summary>
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

    /// <summary>Clicks the dialog's OK (the remade dialog's, over the remade room).</summary>
    void ClickOk()
    {
        REQUIRE(MCRmlDialogs::Shown());
        MCRmlDialogs::Dialog()->PressMain();
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
    GlobalLog()->Room.LoadMission(Mission);
    CHECK(SettleUntil([&] { return guest.MissionsLoaded() == 1; }));
    CHECK(!dialog.IsShowing());

    // Its own gamesys.fit: named, as good as without the mission, which is called off.
    guest.AnswerFiles(ownGameSystem);
    GlobalLog()->Room.LoadMission(Mission);
    CHECK(SettleUntil([&] { return dialog.Text != LoadGameString(0xac, 0xfe); }));
    CHECK(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string("These players' game data differs from yours: Guest"));
    SaveShot("data check 0 differs", ScreenHash());
    ClickOk();
    Settle();
    CHECK(!dialog.IsShowing());
    CHECK_EQ(guest.MissionsLoaded(), 1);

    // An older build (no hash, the original's first four bytes): named, and the mission loads on the host's OK.
    guest.SendRules(hostId, MCMatchRule::Supported & ~MCMatchRule::DataHashV2);
    guest.AnswerFiles(FirstFourBytes(missionFile));
    Settle();
    GlobalLog()->Room.LoadMission(Mission);
    CHECK(SettleUntil([&] { return dialog.Text != LoadGameString(0xac, 0xfe); }));
    CHECK(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string("Can't check these players' game data: Guest. Tell them to upgrade."));
    SaveShot("data check 1 unchecked", ScreenHash());
    CHECK_EQ(guest.MissionsLoaded(), 1);
    ClickOk();
    CHECK(SettleUntil([&] { return guest.MissionsLoaded() == 2; }));
    CHECK(!dialog.IsShowing());

    // Without the file, as before: the original's text.
    guest.SendRules(hostId, MCMatchRule::Supported);
    guest.AnswerFiles(0);
    Settle();
    GlobalLog()->Room.LoadMission(Mission);
    CHECK(SettleUntil([&] { return dialog.Text != LoadGameString(0xac, 0xfe); }));
    CHECK_EQ(dialog.Text, LoadGameString(0xab, 0xfe) + "Guest");
    ClickOk();
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
    /// <summary>The room's messages' test's (28929 is LobbyUpdateTests' refused guest's).</summary>
    constexpr std::string_view MessagesHostAddress = "127.0.0.1:29009";

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
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
    CHECK(MultiPlayer() == nullptr);

    // Hosting from the games: the room, the host alone in it, whose EXIT leaves the game and lists the games again.
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(MenuHostAddress);
    MCTestGuest::HostOnLan("Commander", "Menu game", 2);
    Settle();
    REQUIRE(manager.CurrentSession() != nullptr);
    REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Room());
    CHECK(RoomScreenShown());
    CHECK(MCTestGuest::RoomShows(manager.MyPlayer()->Id));
    CHECK(!GlobalLog()->Room.CanStart);
    CHECK(GlobalLog()->Room.HostControls);
    SaveShot("menus 0 host alone", ScreenHash());
    // A mission loaded with nobody else in the room: its file check is done at once (nobody to answer).
    GlobalLog()->Room.LoadMission("ARENA");
    REQUIRE(SettleUntil([] { return !GlobalLog()->Message->IsShowing(); }));
    CHECK(GlobalLog()->Room.HostControls);
    MCTestGuest::PressInRoom("leave");
    CHECK(MCTestGuest::OnLan());
    CHECK(!RoomScreenShown());
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
    REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Room());
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
    GlobalLog()->Room.AssignPlayer(hostId, 1, 0, false);
    GlobalLog()->Room.AssignPlayer(ann.Id(), 2, 0, false);
    GlobalLog()->Room.SetTeamTechBase(2, 1);
    GlobalLog()->Room.LoadMission(Mission);
    REQUIRE(SettleUntil([&] { return ann.MissionsLoaded() == 1 && !dialog.IsShowing(); }));
    GlobalLog()->Room.SetTeam1RP(12000);
    GlobalLog()->Room.SetTeam2RP(9000);
    Settle();
    CHECK(GlobalLog()->Room.CanStart);

    // CAL joins: not shown while its game is on its way (and START waits for it), then shown the room as it stands.
    Guest cal("CAL", MCMatchRule::Supported);
    REQUIRE(cal.Join(*manager.DirectPlay, RoomHostAddress, false));
    Settle();
    CHECK(!MCTestGuest::RoomShows(cal.Id()));
    CHECK(!GlobalLog()->Room.CanStart);
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
    CHECK(GlobalLog()->Room.Team1Players[0] == hostId);
    CHECK(GlobalLog()->Room.Team2Players[0] == ann.Id());
    CHECK_EQ(GlobalLog()->Room.Team1TechBase, 1);
    CHECK_EQ(GlobalLog()->Room.Team2TechBase, 1);
    CHECK_EQ(GlobalLog()->Room.PointsText[0], "12000");
    CHECK_EQ(GlobalLog()->Room.PointsText[1], "9000");
    CHECK(!GlobalLog()->Room.MissionFile.empty());
    SaveShot("room 0 a player joined", ScreenHash());

    // CAL unpicked holds START; on team 2 it lights.
    CHECK(!GlobalLog()->Room.CanStart);
    GlobalLog()->Room.AssignPlayer(cal.Id(), 2, 1, false);
    CHECK(GlobalLog()->Room.CanStart);

    // ANN leaves: taken off the screen, the room goes on with CAL on team 2 (no "has left the game").
    ann.Leave();
    REQUIRE(SettleUntil([&] { return !MCTestGuest::RoomShows(ann.Id()); }));
    Settle();
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Room());
    CHECK(!dialog.IsShowing());
    CHECK(GlobalLog()->Room.Team2Players[0] == MCRoomModel::NoPlayer);
    CHECK(GlobalLog()->Room.Team2Players[1] == cal.Id());
    CHECK(GlobalLog()->Room.CanStart);
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
    CHECK(!GlobalLog()->Room.MissionFile.empty());
    CHECK(GlobalLog()->Room.Team2Players[1] == cal.Id());
    CHECK(GlobalLog()->Room.CanStart);

    // START closes the room.
    MCTestGuest::PressInRoom("begin");
    CHECK(MCMenuScreens::Top() != MCMenuScreens::Room());
    CHECK(manager.SessionLocked());
    CHECK(!openToPlayers());
}

/// <summary>
/// The room's messages (the personal build): what a LAN room's host sends its guests goes out in the same order and
/// bytes as the 1998 session screen sent them before the room's logic left it (SPEC-menus.md's stage 9, step 2), as
/// older builds and MCX.EXE read every byte: a join and the room shown, the host's picks (the teams, a tech base, the
/// mission, the points) and its options, a second guest shown the room and the first shown it again, a joiner refused
/// at the door (nobody told), a guest leaving, and START.
/// </summary>
TEST_CASE_ISOLATED("game: a LAN room's host sends its guests the same messages in the same order as the room goes")
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
    manager.ConnectTcp(MessagesHostAddress);
    // The session's ids where no message's other bytes look like one (as CheckRecorded reads them).
    MCDirectPlay::SetIdsHighWordForTests(MCTestGuest::RecordedIdsHighWord);
    MCTestGuest::HostOnLan("Commander", "Messages' game", 6);
    Settle();
    REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Room());
    const MCMenuDialog& dialog = *GlobalLog()->Message;
    const uint32_t hostId = manager.MyPlayer()->Id;
    constexpr std::string_view Mission = "ARENA";
    const uint32_t sameData = MCMissionDataHash(GamePath(SavePath, Mission, ".mpk"));
    REQUIRE(sameData != 0);
    constexpr size_t WeatherRow = 1;
    const auto received = [](Guest& guest, MCMPMessageType type)
    { return std::ranges::count(MCTestGuest::RecordedTypes(guest.Recorded()), static_cast<uint16_t>(type)); };

    // ANN joins and is shown the room; the host picks the teams, Inner Sphere for team 2, the mission and the points.
    Guest ann("ANN", MCMatchRule::Supported);
    ann.RecordAll();
    REQUIRE(ann.Join(*manager.DirectPlay, MessagesHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && ann.SessionScreens() == 1; }));
    REQUIRE(manager.AddPlayerToGroup(game.AllPlayerGroupID, ann.Id()));
    ann.AnswerFiles(sameData);
    GlobalLog()->Room.AssignPlayer(hostId, 1, 0, false);
    GlobalLog()->Room.AssignPlayer(ann.Id(), 2, 0, false);
    GlobalLog()->Room.SetTeamTechBase(2, 1);
    GlobalLog()->Room.LoadMission(Mission);
    REQUIRE(SettleUntil([&] { return ann.MissionsLoaded() == 1 && !dialog.IsShowing(); }));
    GlobalLog()->Room.SetTeam1RP(12000);
    GlobalLog()->Room.SetTeam2RP(9000);
    REQUIRE(SettleUntil([&] { return received(ann, MCMPMessageType::RPUpdate) > 0; }));
    Settle();

    // The host's options: rain.
    MatchOptionRows()[WeatherRow].Changed(static_cast<int32_t>(MCWeatherKind::Rain));
    REQUIRE(SettleUntil(
        [&]
        {
            const std::optional<MCMPMatchOptionsMessage> sent = ann.MatchOptions();
            return sent.has_value() && sent->Weather == static_cast<int8_t>(MCWeatherKind::Rain);
        }));
    Settle();

    // CAL joins, its game on its way; once it's up, it's shown the room as it stands, and ANN is shown it again. CAL
    // goes on team 2.
    Guest cal("CAL", MCMatchRule::Supported);
    cal.RecordAll();
    REQUIRE(cal.Join(*manager.DirectPlay, MessagesHostAddress, false));
    Settle();
    REQUIRE(manager.AddPlayerToGroup(game.AllPlayerGroupID, cal.Id()));
    cal.AnswerFiles(sameData);
    cal.Ping();
    REQUIRE(SettleUntil([&] { return cal.MissionsLoaded() == 1 && ann.MissionsLoaded() == 2 && !dialog.IsShowing(); }));
    GlobalLog()->Room.AssignPlayer(cal.Id(), 2, 1, false);
    Settle();

    // An older build's join, which says nothing of its rules, is refused at the door for the rain: nobody is told.
    Guest old("OLD");
    CHECK(!old.Join(*manager.DirectPlay, MessagesHostAddress));
    Settle();

    // CAL leaves, and START.
    cal.Leave();
    REQUIRE(SettleUntil([&] { return !MCTestGuest::RoomShows(cal.Id()); }));
    Settle();
    MCTestGuest::PressInRoom("begin");
    Settle();
    Settle();

    const std::array ids = {MCTestGuest::MCNamedId{hostId, "HOST"}, MCTestGuest::MCNamedId{ann.Id(), "ANN"},
                            MCTestGuest::MCNamedId{cal.Id(), "CAL"}};
    // As the 1998 session screen sent them (recorded from it before the move; the same in every run).
    constexpr std::array<uint16_t, 47> AnnTypes = {2,  3,  3,  3,  50, 16, 42, 49, 44, 44, 7,  7,  41, 7,   41, 37,
                                                   43, 43, 39, 43, 43, 43, 43, 49, 3,  50, 7,  42, 49, 37,  41, 41,
                                                   44, 44, 43, 43, 39, 43, 43, 7,  41, 33, 33, 5,  13, 260, 40};
    constexpr uint64_t AnnDigest = 0x51131235166865e5ull;
    constexpr std::array<uint16_t, 21> CalTypes = {2,  3,  3,  3,  50, 16, 7,  42, 49, 37, 41,
                                                   41, 44, 44, 43, 43, 39, 43, 43, 7,  41};
    constexpr uint64_t CalDigest = 0xfa35b473f6ac77a6ull;
    MCTestGuest::CheckRecorded(ann, ids, hostId, ann.Instance(), AnnTypes, AnnDigest);
    MCTestGuest::CheckRecorded(cal, ids, hostId, ann.Instance(), CalTypes, CalDigest);
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
    REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Room());
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
    GlobalLog()->Room.AssignPlayer(ann.Id(), 2, 0, false);
    Guest dan("DAN", WithoutArtillery);
    REQUIRE(dan.Join(*manager.DirectPlay, JoinHostAddress));
    CHECK(dan.Refusal().empty());
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(dan.Id()) && sentTime(dan, MCTimeOfDay::Night, false); }));
    CHECK(GlobalLog()->Room.Team2Players[0] == ann.Id());
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
        // The session's ids where no message's other bytes look like one (as the test's CheckRecorded reads them).
        MCDirectPlay::SetIdsHighWordForTests(MCTestGuest::RecordedIdsHighWord);
        // The LAN games (MULTIPLAYER's LAN).
        MCTestGuest::HostOnLan("BOB", "Rolling room", 6);
        Settle();
        REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Room());
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
        GlobalLog()->Room.AssignPlayer(manager.MyPlayerID, 1, 0, false);
        GlobalLog()->Room.AssignPlayer(ann, 2, 0, false);
        GlobalLog()->Room.AssignPlayer(cat, 2, 1, false);
        GlobalLog()->Room.SetTeamTechBase(2, 1);
        GlobalLog()->Room.LoadMission(RolloverMission);
        REQUIRE(
            SettleLong([&] { return !GlobalLog()->Message->IsShowing() && !GlobalLog()->Room.MissionFile.empty(); }));
        GlobalLog()->Room.SetTeam1RP(12000);
        GlobalLog()->Room.SetTeam2RP(9000);
        Settle();
        std::ofstream(folder / PickedSignal).put('1');
        REQUIRE(SettleLong([&] { return std::filesystem::exists(folder / SeenSignal); }));
        MCRoomEnd::Leave();
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
    cat.RecordAll(ann);
    MCDirectPlay noHostHere;
    REQUIRE(cat.Join(noHostHere, RolloverHostAddress));
    cat.AnswerFiles(MCMissionDataHash(GamePath(SavePath, RolloverMission, ".mpk")));
    REQUIRE(SettleLong([&] { return std::filesystem::exists(folder.Path() / PickedSignal); }));

    // The host's picks come from its process, as the signal did: its wait.
    REQUIRE(SettleLong(
        [&]
        {
            return GlobalLog()->Room.Team2Players[0] == ann && GlobalLog()->Room.Team2Players[1] == cat.Id() &&
                   GlobalLog()->Room.PointsText[0] == "12000" && !GlobalLog()->Room.MissionName.empty();
        }));
    const std::string missionName = GlobalLog()->Room.MissionName;
    const std::shared_ptr<MCFidpPlayer> host = PlayerNamed("BOB");
    REQUIRE(host != nullptr);
    const uint32_t bobId = host->Id;
    CHECK(GlobalLog()->Room.Team1Players[0] == bobId);
    CHECK(!MultiPlayer()->IsHost);
    const int32_t catScreens = cat.SessionScreens();
    bob.Seen();

    // BOB's EXIT: the room is ANN's game's now, as it stood.
    REQUIRE(SettleLong([&] { return MultiPlayer()->IsHost && !MCTestGuest::RoomShows(bobId); }));
    REQUIRE(SettleUntil([&] { return cat.SessionScreens() > catScreens && !GlobalLog()->Message->IsShowing(); }));
    Settle();
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Room());
    CHECK_EQ(manager.ServerID, ann);
    CHECK(manager.GetPlayer(bobId) == nullptr);
    CHECK(MCSameGuid(manager.CurrentSession()->SessionDesc.guidInstance, instance));
    CHECK(MCTestGuest::RoomShows(cat.Id()));
    CHECK(GlobalLog()->Room.Team1Players[0] == MCRoomModel::NoPlayer);
    CHECK(GlobalLog()->Room.Team2Players[0] == ann);
    CHECK(GlobalLog()->Room.Team2Players[1] == cat.Id());
    CHECK_EQ(GlobalLog()->Room.Team2TechBase, 1);
    CHECK_EQ(GlobalLog()->Room.PointsText[0], "12000");
    CHECK_EQ(GlobalLog()->Room.PointsText[1], "9000");
    CHECK(!GlobalLog()->Room.MissionFile.empty());
    CHECK_EQ(GlobalLog()->Room.MissionName, missionName);
    CHECK(ChatSays("BOB has left the game."));
    CHECK(ChatSays("is now the server."));

    // ANN's controls are the host's: CAT to team 1, and START plays, closing the room at the session's new hub.
    GlobalLog()->Room.AssignPlayer(cat.Id(), 1, 0, false);
    Settle();
    REQUIRE(GlobalLog()->Room.CanStart);
    MCTestGuest::PressInRoom("begin");
    CHECK(MCMenuScreens::Top() != MCMenuScreens::Room());
    CHECK(manager.SessionLocked());
    CHECK_EQ(manager.DirectPlay->SetSessionDesc(manager.CurrentSession()->SessionDesc, 0), uint32_t{DP_OK});
    CHECK(bob.Passed());

    // The room's messages (SPEC-menus.md's stage 9, step 2): what CAT's game was sent from ANN's, its chat and then its
    // taking the room over, in the same order and bytes as the 1998 session screen sent them (recorded from it before
    // the room's logic left it).
    Settle();
    const std::array ids = {MCTestGuest::MCNamedId{bobId, "BOB"}, MCTestGuest::MCNamedId{ann, "ANN"},
                            MCTestGuest::MCNamedId{cat.Id(), "CAT"}};
    constexpr std::array<uint16_t, 16> CatTypes = {13, 13, 42, 49, 37, 41, 41, 44, 44, 43, 43, 39, 43, 43, 41, 40};
    constexpr uint64_t CatDigest = 0x98f12fc85f8a5d4cull;
    // Or the room shown to CAT twice, the second the first's twelve messages again: when CAT's first ping to ANN's game
    // as the host comes after that game's first room tick as the host, the takeover's show finds CAT not yet heard, and
    // WatchRoom shows the room again once it is (its guest "shown the room before its game was up"). Which comes first
    // is the network's: 2 runs of 6 on one build (24 Nov), both recording this.
    constexpr std::array<uint16_t, 28> CatTypesShownTwice = {13, 13, 42, 49, 37, 41, 41, 44, 44, 43, 43, 39, 43, 43,
                                                             42, 49, 37, 41, 41, 44, 44, 43, 43, 39, 43, 43, 41, 40};
    constexpr uint64_t CatDigestShownTwice = 0xaa1cd18f02dc0233ull;

    if (cat.Recorded().size() == CatTypesShownTwice.size())
    {
        MCTestGuest::CheckRecorded(cat, ids, ann, cat.Instance(), CatTypesShownTwice, CatDigestShownTwice);
    }
    else
    {
        MCTestGuest::CheckRecorded(cat, ids, ann, cat.Instance(), CatTypes, CatDigest);
    }
}

namespace
{
    /// <summary>The room's seats' test's, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view SeatsHostAddress = "127.0.0.1:28931";
    /// <summary>The game's frame while a card is carried, as the room's tests step it.</summary>
    constexpr float CarryFrame = 1.0f / 15.0f;
    /// <summary>The pointer's moves from a card to where it's put.</summary>
    constexpr int32_t CarrySteps = 4;
    constexpr std::string_view LongName = "Commander";
    constexpr std::string_view ShortName = "Al";
    constexpr std::string_view LastName = "Bartholomew";
    /// <summary>An open place, as the room's Seen() reads it.</summary>
    constexpr std::string_view OpenPlaceName = "Open";

    Rml::Vector2f MiddleOf(Rml::Element& element)
    {
        return element.GetAbsoluteOffset(Rml::BoxArea::Border) + element.GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
    }

    /// <summary>The shown card of the player <paramref name="name"/>, on a team or on none.</summary>
    Rml::Element& CardNamed(Rml::ElementDocument& document, std::string_view name)
    {
        Rml::ElementList cards;
        document.QuerySelectorAll(cards, ".person");

        for (Rml::Element* card : cards)
        {
            Rml::ElementList names;
            card->GetElementsByClassName(names, "name");

            if (card->IsVisible(true) && !names.empty() && names.front()->GetInnerRML() == name)
            {
                return *card;
            }
        }

        FAIL(std::format("no card shows {}", name));
        return document;
    }

    /// <summary>Team <paramref name="team"/>'s place <paramref name="slot"/>: its OPEN well, or the card in it.</summary>
    Rml::Element& PlaceOf(Rml::ElementDocument& document, int8_t team, int8_t slot)
    {
        Rml::ElementList teams;
        document.QuerySelectorAll(teams, ".team");
        REQUIRE(teams.size() == static_cast<size_t>(MCRoomModel::Team2 - MCRoomModel::Team1 + 1));
        // A place is its well then its card, one of them shown.
        Rml::ElementList places;
        teams[static_cast<size_t>(team - MCRoomModel::Team1)]->QuerySelectorAll(places, ".slot, .person");
        REQUIRE(places.size() == static_cast<size_t>(MCRoomModel::TeamSlots) * 2);
        Rml::Element& well = *places[static_cast<size_t>(slot) * 2];
        return well.IsVisible(true) ? well : *places[static_cast<size_t>(slot) * 2 + 1];
    }

    /// <summary>How far a box may lie past another and still be clear of it (RmlUi's layout is floats).</summary>
    constexpr float BoxSlack = 0.5f;

    /// <summary>
    /// What's wrong with a card's parts (its name, HOST, its buttons): each as wide as its words, and each clear of the
    /// one before it. (The box checks pass a part with no width: its words spill unseen by them.) Empty: nothing.
    /// </summary>
    std::string PartsProblems(Rml::Element& card)
    {
        std::string wrong;
        float before = card.GetAbsoluteOffset(Rml::BoxArea::Content).x;

        for (int i = 0; i < card.GetNumChildren(); ++i)
        {
            Rml::Element& part = *card.GetChild(i);

            if (!part.IsVisible())
            {
                continue;
            }

            const float left = part.GetAbsoluteOffset(Rml::BoxArea::Border).x;
            const float width = part.GetBox().GetSize(Rml::BoxArea::Content).x;
            float words = 0.0f;

            // Its words' width in its own letters (a part's words are one line: the card's are white-space: nowrap).
            for (int j = 0; j < part.GetNumChildren(); ++j)
            {
                if (const auto* text = dynamic_cast<Rml::ElementText*>(part.GetChild(j)); text != nullptr)
                {
                    words += static_cast<float>(Rml::ElementUtilities::GetStringWidth(&part, text->GetText()));
                }
            }

            if (width + BoxSlack < words)
            {
                wrong += std::format("'{}' is wider than its {}; ", part.GetInnerRML(), part.GetClassNames());
            }

            if (left + BoxSlack < before)
            {
                wrong +=
                    std::format("'{}' ({}) lies over what's before it; ", part.GetInnerRML(), part.GetClassNames());
            }

            before = left + part.GetBox().GetSize(Rml::BoxArea::Border).x;
        }

        return wrong;
    }

    /// <summary>
    /// What the layout checks find in the menus as they stand, at the game's window (none laid out again), and in the
    /// room's shown cards' parts.
    /// </summary>
    std::string ProblemsNow(Rml::ElementDocument& room)
    {
        std::string problems = MCRmlChecks::BoxProblems(MCMenusPictures::GameMenus().Menus().Context());
        Rml::ElementList cards;
        room.QuerySelectorAll(cards, ".person");

        for (Rml::Element* card : cards)
        {
            if (card->IsVisible(true))
            {
                problems += PartsProblems(*card);
            }
        }

        return problems;
    }

    /// <summary>
    /// What the layout checks find in <paramref name="card"/> as a drag carries it: RmlUi's copy under the pointer
    /// (Context::CreateDragClone, out of the tests' reach) made the same way, the card cloned into a document of the
    /// room's style sheets alone and no size (the context's cursor proxy, which borrows the room's sheets, has no parent
    /// to take a size from), placed where the card lies, :drag on.
    /// </summary>
    std::string CarriedProblems(Rml::Element& card)
    {
        Rml::Context& context = MCMenusPictures::GameMenus().Menus().Context();
        Rml::ElementDocument* room = card.GetOwnerDocument();
        REQUIRE(room != nullptr);
        Rml::ElementDocument* carried = context.LoadDocumentFromMemory(
            R"(<rml><head><link type="text/rcss" href="look.rcss"/><link type="text/rcss" href="room.rcss"/></head>)"
            "<body></body></rml>",
            room->GetSourceURL());
        REQUIRE(carried != nullptr);
        carried->SetProperty(Rml::PropertyId::Width, Rml::Property(0.0f, Rml::Unit::PX));
        carried->SetProperty(Rml::PropertyId::Height, Rml::Property(0.0f, Rml::Unit::PX));
        const Rml::Vector2f at = card.GetAbsoluteOffset(Rml::BoxArea::Border);
        Rml::Element& copy = *carried->AppendChild(card.Clone());
        copy.SetProperty(Rml::PropertyId::Position, Rml::Property(Rml::Style::Position::Absolute));
        copy.SetProperty(Rml::PropertyId::Left, Rml::Property(at.x, Rml::Unit::PX));
        copy.SetProperty(Rml::PropertyId::Top, Rml::Property(at.y, Rml::Unit::PX));
        copy.SetProperty(Rml::PropertyId::MarginLeft, Rml::Property(0.0f, Rml::Unit::PX));
        copy.SetProperty(Rml::PropertyId::MarginTop, Rml::Property(0.0f, Rml::Unit::PX));
        copy.SetPseudoClass("drag", true);
        carried->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        context.Update();
        std::string problems = MCRmlChecks::BoxProblems(context) + PartsProblems(copy);
        carried->Close();
        context.Update();
        return problems;
    }

    /// <summary>
    /// <paramref name="card"/> carried by the mouse to <paramref name="place"/> and put there, as the host drags it:
    /// what the layout checks find while it's carried, and once it's put down.
    /// </summary>
    void Carry(Rml::Element& card, Rml::Element& place)
    {
        // The card may go once it's put down (the players on no team one fewer).
        Rml::ElementDocument* room = card.GetOwnerDocument();
        REQUIRE(room != nullptr);
        const Rml::Vector2f start = MiddleOf(card);
        const Rml::Vector2f end = MiddleOf(place);
        MCScreenInput::PixelMove(start.x, start.y, CarryFrame);
        MCScreenInput::PixelButton(SDL_BUTTON_LEFT, true, start.x, start.y, CarryFrame);

        for (int32_t step = 1; step <= CarrySteps; step++)
        {
            const Rml::Vector2f at = start + (end - start) * (static_cast<float>(step) / CarrySteps);
            MCScreenInput::PixelMove(at.x, at.y, CarryFrame);
        }

        CHECK_EQ(CarriedProblems(card), "");
        MCScreenInput::PixelButton(SDL_BUTTON_LEFT, false, end.x, end.y, CarryFrame);
        Settle();
        CHECK_EQ(ProblemsNow(*room), "");
    }
}

/// <summary>
/// The room's cards lay out their name and HOST apart while the host carries one (RmlUi's copy under the pointer took
/// no width of its own, and drew HOST over the name) and once it's put down: the host's and a guest's, from the players
/// on no team to a team and between teams, until none is left on no team, with names long and short, at the frames'
/// windows (4:3, 16:9, 5:4) as the game's window, laid out as the room goes (not afresh).
/// </summary>
TEST_CASE_ISOLATED("game: the room's cards keep their name and HOST apart while carried and once placed")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(SeatsHostAddress);
    MCTestGuest::HostOnLan(LongName, "Seats", 6);
    Settle();
    Guest al{std::string(ShortName)};
    REQUIRE(al.Join(*manager.DirectPlay, SeatsHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(al.Id()); }));
    Guest bart{std::string(LastName)};
    REQUIRE(bart.Join(*manager.DirectPlay, SeatsHostAddress));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(bart.Id()); }));
    Settle();
    REQUIRE(RoomScreenShown());
    MCRoomScreen& room = *RoomScreen();
    Rml::ElementDocument& document = room.Document()->get();
    MCDisplay& display = *MCInput::Display();
    const uint32_t hostId = manager.MyPlayer()->Id;

    for (const MCMenusPictures::MCWindowSize window : MCMenusPictures::FrameWindows)
    {
        const MCTest::Scope scope{std::format("{}x{}", window.Width, window.Height)};
        REQUIRE(MCMenusPictures::SetWindowPixels(display, window.Width, window.Height));
        Settle();
        REQUIRE_EQ(room.Seen().Pool.size(), size_t{3});
        CHECK_EQ(ProblemsNow(document), "");

        // A guest onto team 1, the host from the players on no team onto team 2, then between the teams; the last
        // player onto team 2, leaving none on no team.
        Carry(CardNamed(document, ShortName), PlaceOf(document, MCRoomModel::Team1, 0));
        Carry(CardNamed(document, LongName), PlaceOf(document, MCRoomModel::Team2, 0));
        CHECK_EQ(room.Seen().Team2.front(), std::format("{} (host)", LongName));
        Carry(CardNamed(document, LongName), PlaceOf(document, MCRoomModel::Team1, 1));
        Carry(CardNamed(document, LastName), PlaceOf(document, MCRoomModel::Team2, 0));
        const MCRoomScreen::MCSeen placed = room.Seen();
        const std::vector<std::string> team1 = {std::string(ShortName), std::format("{} (host)", LongName),
                                                std::string(OpenPlaceName)};
        CHECK(placed.Team1 == team1);
        CHECK_EQ(placed.Team2.front(), std::string(LastName));
        CHECK(placed.Pool.empty());

        // All on no team again for the next window.
        for (const uint32_t id : {hostId, al.Id(), bart.Id()})
        {
            room.Drop(id, MCRoomModel::NoTeam, 0);
        }

        Settle();
    }
}

namespace
{
    /// <summary>The room's team chat test's, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view TeamChatHostAddress = "127.0.0.1:28933";
    /// <summary>
    /// 1998's chat text to a team (MCLogChatInput's colour 4: blue.fnt, its ink palette 0xe3), the look's cyan, as
    /// 0xRRGGBB.
    /// </summary>
    constexpr uint32_t TeamLineInk = 0x7cd4ec;
    /// <summary>1998's chat text to all (colour 6: white.fnt, its ink 0x1f), the look's white.</summary>
    constexpr uint32_t AllLineInk = 0xfcfcfc;
    /// <summary>The 1998 chat's text colour codes (MCLogChatInput): to the team, and to all.</summary>
    constexpr int32_t TeamTextCode = 4;
    constexpr int32_t AllTextCode = 6;

    /// <summary>The game's menus' kit, keeping the sounds it plays from now on.</summary>
    MCRmlKit& KitKeepingSounds()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();
        REQUIRE(menus.has_value());
        MCRmlKit& kit = menus->get().Kit();
        kit.KeepPlayed(true);
        return kit;
    }

    /// <summary>The sounds <paramref name="kit"/> played since <paramref name="heard"/> of them, which it moves past.</summary>
    std::vector<uint32_t> SoundsSince(const MCRmlKit& kit, size_t& heard)
    {
        const std::vector<uint32_t>& played = kit.Played();
        std::vector<uint32_t> since(played.begin() + static_cast<std::ptrdiff_t>(std::min(heard, played.size())),
                                    played.end());
        heard = played.size();
        return since;
    }

    /// <summary>The colour <paramref name="element"/>'s letters are drawn in, as 0xRRGGBB.</summary>
    uint32_t InkOf(Rml::Element& element)
    {
        constexpr int RedShift = 16;
        constexpr int GreenShift = 8;
        const Rml::Colourb ink = element.GetComputedValues().color();
        return static_cast<uint32_t>(ink.red) << RedShift | static_cast<uint32_t>(ink.green) << GreenShift |
               static_cast<uint32_t>(ink.blue);
    }
}

/// <summary>
/// A line to the team reads apart from a line to all, as 1998's did (MCLogChatInput, MCLogChat::ProcessChatString):
/// the sayer's name in their colour on both, the team's text in 1998's team colour (blue.fnt's, the look's cyan) and
/// all's white, the team's marked TEAM; the player's own lines and a guest's alike.
/// </summary>
TEST_CASE_ISOLATED("game: the room's chat shows a line to the team in 1998's team colour, apart from a line to all")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MultiPlayer()->SessionManager->ConnectTcp(TeamChatHostAddress);
    MCTestGuest::HostOnLan("Bob", "Team chat", 6);
    REQUIRE(SettleUntil([] { return RoomScreenShown(); }));
    MCRoomScreen& room = *RoomScreen();
    const std::shared_ptr<MCFidpPlayer> host = MultiPlayer()->SessionManager->MyPlayer();
    REQUIRE(host != nullptr);
    room.PressJoin(host->Id, MCRoomModel::Team1);
    Settle();

    // The host's own, to the team then to all; a guest's (as the chat holds a received line), to the team then to all.
    room.Say("Flank left.", true);
    room.Say("Good luck.", false);
    constexpr int32_t AnnNumber = 1;
    const int32_t annColour = GlobalLog()->PlayerColors[static_cast<size_t>(AnnNumber)];
    GlobalLog()->Chat->AddLine(std::format("%fc{}Ann: %fc{}On it.", annColour, TeamTextCode));
    GlobalLog()->Chat->AddLine(std::format("%fc{}Ann: %fc{}You too.", annColour, AllTextCode));
    Settle();

    // The lines shown: RmlUi keeps the list's hidden template after them.
    Rml::ElementList lines;
    room.Document()->get().QuerySelectorAll(lines, "#talk .chat-line");
    std::erase_if(lines, [](const Rml::Element* line) { return !line->IsVisible(); });
    constexpr size_t Said = 4;
    REQUIRE(lines.size() >= Said);
    const std::span<Rml::Element*> last(lines.end() - static_cast<std::ptrdiff_t>(Said), lines.end());
    const std::array<bool, Said> toTeam = {true, false, true, false};

    for (size_t at = 0; at < Said; ++at)
    {
        Rml::Element& line = *last[at];
        const MCTest::Scope scope{line.GetInnerRML()};
        Rml::ElementList names;
        line.GetElementsByClassName(names, "name");
        REQUIRE(names.size() == 1);
        Rml::ElementList marks;
        line.GetElementsByClassName(marks, "to");
        REQUIRE(marks.size() == 1);
        CHECK_EQ(line.IsClassSet("team"), toTeam[at]);
        CHECK_EQ(InkOf(line), toTeam[at] ? TeamLineInk : AllLineInk);
        CHECK_EQ(marks.front()->IsVisible(), toTeam[at]);
        // The name in the sayer's colour either way: the host's first, then Ann's.
        constexpr size_t HostLines = 2;
        CHECK(names.front()->IsClassSet(MCChatView::ColourOf(at < HostLines ? host->PlayerNumber : AnnNumber)));
        CHECK(InkOf(*names.front()) != TeamLineInk);
    }
}

namespace
{
    /// <summary>The locked map test's, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view LockedHostAddress = "127.0.0.1:28949";
    /// <summary>A disabled form control's attribute (RmlUi's: no focus, no typing).</summary>
    constexpr const char* DisabledAttribute = "disabled";
}

/// <summary>
/// A locked map (1998's [Lock]: CPTRLOCK, team 1 Inner Sphere with 360,000 RP against team 2 Clan with 300,000) holds
/// the host's points boxes as it holds the tech base switches: each shows the map's points, grouped, takes no focus
/// and no typing, and a click on it plays the refusal of something off; its plain map's boxes take the host's typing.
/// </summary>
TEST_CASE_ISOLATED("game: a locked map holds the host's points boxes at its own points, as its tech bases")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MultiPlayer()->SessionManager->ConnectTcp(LockedHostAddress);
    MCTestGuest::HostOnLan("Bob", "Locked map", 6);
    REQUIRE(SettleUntil([] { return RoomScreenShown(); }));
    MCRoomScreen& room = *RoomScreen();
    MCRoomModel& model = GlobalLog()->Room;
    const MCRmlKit& kit = KitKeepingSounds();
    size_t heard = kit.Played().size();
    const auto load = [](std::string_view mission)
    {
        GlobalLog()->Room.LoadMission(mission);
        REQUIRE(SettleUntil([] { return !GlobalLog()->Message->IsShowing(); }));
        Settle();
    };

    Rml::Element* points1 = MCTestGuest::ElementOf(room, "points1");
    Rml::Element* points2 = MCTestGuest::ElementOf(room, "points2");
    REQUIRE(points1 != nullptr);
    REQUIRE(points2 != nullptr);
    Rml::Context* context = points1->GetContext();
    REQUIRE(context != nullptr);

    // The locked map: both boxes held at its points, as the tech base switches are.
    load("CPTRLOCK");
    REQUIRE(model.Locked);
    constexpr std::array<std::string_view, 2> LockedPoints = {"360,000", "300,000"};
    const std::array<Rml::Element*, 2> boxes = {points1, points2};
    const std::array<const char*, 2> techIds = {"tech1", "tech2"};

    for (size_t at = 0; at < boxes.size(); ++at)
    {
        const MCTest::Scope scope{boxes[at]->GetId()};
        CHECK(boxes[at]->IsClassSet("off"));
        CHECK(boxes[at]->HasAttribute(DisabledAttribute));
        CHECK_EQ(boxes[at]->GetAttribute<Rml::String>("value", ""), std::string(LockedPoints[at]));
        Rml::Element* tech = MCTestGuest::ElementOf(room, techIds[at]);
        REQUIRE(tech != nullptr);
        CHECK(tech->IsClassSet("off"));
    }

    CHECK_EQ(room.Seen().Points1, std::string(LockedPoints[0]));
    CHECK_EQ(room.Seen().Points2, std::string(LockedPoints[1]));

    // Clicked and typed in: refused, the keys not in it, the points as they were.
    MCTestGuest::RealClickOn(*points1);
    CHECK(context->GetFocusElement() != points1);
    CHECK(SoundsSince(kit, heard) == std::vector<uint32_t>{MCRmlKit::OffClickSound});
    MCTestGuest::RealType("5");
    Settle();
    CHECK_EQ(model.Team1Points, 360'000);
    CHECK_EQ(points1->GetAttribute<Rml::String>("value", ""), std::string(LockedPoints[0]));
    CHECK_EQ(room.Seen().Points1, std::string(LockedPoints[0]));

    // Its plain map: the boxes free, the host's typing its points.
    load("CPTRBASE");
    REQUIRE(!model.Locked);

    for (Rml::Element* box : boxes)
    {
        const MCTest::Scope scope{box->GetId()};
        CHECK(!box->IsClassSet("off"));
        CHECK(!box->HasAttribute(DisabledAttribute));
        CHECK_EQ(box->GetAttribute<Rml::String>("value", ""), std::string("300,000"));
    }

    MCTestGuest::RealClickOn(*points1);
    CHECK(context->GetFocusElement() == points1);
    CHECK(SoundsSince(kit, heard).empty());
    MCScreenInput::RealKey(SDL_SCANCODE_END);
    MCTestGuest::RealType("1");
    Settle();
    CHECK_EQ(model.Team1Points, 3'000'001);
}

namespace
{
    /// <summary>The held TEAM test's, so it can run beside the others (--jobs).</summary>
    constexpr std::string_view HeldTeamHostAddress = "127.0.0.1:28935";
    /// <summary>The chat's switch's choices (room.rml's #to): TEAM, then ALL.</summary>
    constexpr int TeamChoice = 0;
    constexpr int AllChoice = 1;

    /// <summary>The room's last chat line shown (RmlUi keeps the list's hidden template after the lines).</summary>
    Rml::Element* LastChatLine(const MCRoomScreen& room)
    {
        Rml::ElementList lines;
        room.Document()->get().QuerySelectorAll(lines, "#talk .chat-line");
        std::erase_if(lines, [](const Rml::Element* line) { return !line->IsVisible(); });
        return lines.empty() ? nullptr : lines.back();
    }
}

/// <summary>
/// The room's chat with the player on no team: its TEAM, dim, is held as the spec's held controls are (a click on it,
/// or Left onto it, refused with the sound of something off, the switch staying on ALL), and a line goes to all.
/// On a team TEAM is chosen with the button press and a line goes to the team; off it again, the switch back on ALL.
/// </summary>
TEST_CASE_ISOLATED("game: the room's chat refuses TEAM to a player on no team, whose line goes to all")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MultiPlayer()->SessionManager->ConnectTcp(HeldTeamHostAddress);
    MCTestGuest::HostOnLan("Bob", "Held team", 6);
    // The room shown and its change of screen over: a player's clicks are held while it fades.
    REQUIRE(SettleUntil([] { return RoomScreenShown() && !MCScreenChange::Running(); }));
    MCRoomScreen& room = *RoomScreen();
    const std::shared_ptr<MCFidpPlayer> host = MultiPlayer()->SessionManager->MyPlayer();
    REQUIRE(host != nullptr);
    const MCRmlKit& kit = KitKeepingSounds();
    size_t heard = kit.Played().size();
    Rml::Element* to = MCTestGuest::ElementOf(room, "to");
    Rml::Element* say = MCTestGuest::ElementOf(room, "say");
    REQUIRE(to != nullptr);
    REQUIRE(say != nullptr);
    REQUIRE(to->GetNumChildren() == 2);
    Rml::Element& team = *to->GetChild(TeamChoice);
    const auto sayLine = [say](std::string_view line)
    {
        MCTestGuest::RealClickOn(*say);
        MCTestGuest::RealType(line);
        MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
        Settle();
    };

    const auto lastToTeam = [&room](std::string_view line)
    {
        Rml::Element* last = LastChatLine(room);
        REQUIRE(last != nullptr);
        REQUIRE(last->GetInnerRML().find(line) != std::string::npos);
        return last->IsClassSet("team");
    };

    // On no team: TEAM held, the switch on ALL; its click and Left onto it refused.
    CHECK(team.IsClassSet("held"));
    CHECK(team.IsClassSet("off"));
    CHECK_EQ(MCRmlKit::Choice(*to), AllChoice);
    MCTestGuest::RealClickOn(team);
    CHECK_EQ(MCRmlKit::Choice(*to), AllChoice);
    CHECK(SoundsSince(kit, heard) == std::vector<uint32_t>{MCRmlKit::OffClickSound});
    to->Focus(true);
    MCScreenInput::RealKey(SDL_SCANCODE_LEFT);
    CHECK_EQ(MCRmlKit::Choice(*to), AllChoice);
    CHECK(SoundsSince(kit, heard) == std::vector<uint32_t>{MCRmlKit::OffClickSound});
    sayLine("Anyone here?");
    CHECK(!lastToTeam("Anyone here?"));

    // On team 1: TEAM chosen with the button press, the line to the team.
    room.PressJoin(host->Id, MCRoomModel::Team1);
    Settle();
    CHECK(!team.IsClassSet("held"));
    SoundsSince(kit, heard);
    MCTestGuest::RealClickOn(team);
    CHECK_EQ(MCRmlKit::Choice(*to), TeamChoice);
    CHECK(SoundsSince(kit, heard) == std::vector<uint32_t>{MCRmlKit::PressSound});
    sayLine("Flank left.");
    CHECK(lastToTeam("Flank left."));

    // Off the team again: the switch back on ALL, TEAM held, the line to all.
    room.Drop(host->Id, MCRoomModel::NoTeam, 0);
    Settle();
    CHECK(team.IsClassSet("held"));
    CHECK_EQ(MCRmlKit::Choice(*to), AllChoice);
    sayLine("Regroup.");
    CHECK(!lastToTeam("Regroup."));
}
