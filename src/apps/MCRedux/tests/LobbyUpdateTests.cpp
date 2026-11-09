#include "stdafx.h"
#include "MCFakeUpdaterBackend.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiCallback.h"
#include "gui/MCGuiStartup.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCGameSession.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRejoinNote.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCSocket.h"

// Port-only (the personal build): updates for players a multiplayer lobby starts (SPEC-install-and-updates.md's
// "Players who come from a multiplayer lobby"): the rejoin note a lobby's game leaves as it restarts into an update.

namespace
{
    constexpr std::string_view ScratchGroup = "mc_tests_lobby_updates";
    constexpr uint64_t NoteBuild = 600;
    constexpr int64_t WrittenAt = 1'800'000'000;

    /// <summary>The note's file in <paramref name="folder"/>.</summary>
    std::filesystem::path NotePath(const MCScratchFolder& folder)
    {
        return folder.Path() / MCRejoinNote::FileName;
    }

    /// <summary>Writes a note for <paramref name="build"/> at <paramref name="time"/> holding <paramref name="line"/>.</summary>
    void WriteNote(const MCScratchFolder& folder, uint64_t build, int64_t time, std::string_view line)
    {
        REQUIRE(MCRejoinNote::Write(folder.Path(), {.CommandLine = std::string(line), .Build = build, .Time = time}));
    }
}

TEST_CASE("lobby updates: the rejoin note holds the words the lobby started the game with")
{
    // The whole command line, so the restart starts as the lobby started it: its match's options and slots too.
    constexpr std::string_view HostLine = "-host -port 28800 -player \"Big Bob\" -matchweather rain -matchtime night "
                                          "-matchmap ARENA -slot \"Big Bob\" 1 0 -slot ANN 2 0 -team1tech clan  ";
    const MCLobbyLaunchArgs host{.Active = true, .IsHost = true, .CommandLine = std::string(HostLine)};
    const std::string line = MCRejoinNote::LobbyCommandLine(host);
    CHECK_EQ(line, std::string(HostLine.substr(0, HostLine.find_last_not_of(' ') + 1)) + " -rejoin");
    CHECK(StartsInLobby(line));

    // A rejoin's start has the switch already: once is enough.
    constexpr std::string_view RejoinLine =
        "-join 192.168.1.5:28801 -player \"Ann Lee\" -rejoin -session \"Ann's room\"";
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(
                 MCLobbyLaunchArgs{.Active = true, .Rejoin = true, .CommandLine = std::string(RejoinLine)}),
             std::string(RejoinLine));
}

/// <summary>
/// An internet game's room the menus opened comes back after a restart by its own words: a host's game back in its
/// place on the service (its code and token), a guest's by the code, each the menus' again once in; the note keeps
/// them all.
/// </summary>
TEST_CASE("lobby updates: an internet room the menus opened comes back by a host's place or a guest's code")
{
    constexpr std::string_view Token = "0123456789abcdef0123456789abcdef";
    const std::string host = MCRejoinNote::InternetRoomCommandLine({.Hosting = true,
                                                                    .Code = "K7QF",
                                                                    .Token = std::string(Token),
                                                                    .Public = true,
                                                                    .PlayerName = "Big Bob",
                                                                    .SessionName = "Big Bob's game",
                                                                    .MaxPlayers = 4});
    CHECK_EQ(host, std::format("-host -internet -public -player \"Big Bob\" -session \"Big Bob's game\" -maxplayers 4 "
                               "-place K7QF {} -menus -rejoin",
                               Token));
    CHECK_EQ(LobbyWords(host), host);
    CHECK(StartsInLobby(host));

    const std::string guest =
        MCRejoinNote::InternetRoomCommandLine({.Code = "K7QF", .PlayerName = "Ann", .SessionName = "Big Bob's game"});
    CHECK_EQ(guest, std::string("-join K7QF -internet -player Ann -menus -rejoin"));
    CHECK_EQ(LobbyWords(guest), guest);

    // A public game's guest comes back through relays only, as it played it.
    const std::string relayed =
        MCRejoinNote::InternetRoomCommandLine({.Code = "K7QF", .Public = true, .PlayerName = "Ann"});
    CHECK_EQ(relayed, std::string("-join K7QF -internet -public -player Ann -menus -rejoin"));
    CHECK_EQ(LobbyWords(relayed), relayed);
}

/// <summary>
/// The rejoin note starts the game again with no command line of its own, so it holds the lobby's words alone, each
/// with the words it takes: the game's other switches (a file written, a folder dumped to, a mission or a save loaded)
/// are left out when it is written, and again when it is read, as anything that writes the user folder can write one.
/// </summary>
TEST_CASE("lobby updates: the rejoin note keeps only the lobby's words, written and read")
{
    const MCLobbyLaunchArgs host{
        .Active = true,
        .IsHost = true,
        .CommandLine = "-host -port 28800 -framelog /tmp/frames.log -player BOB -gpudump /tmp/dump -mission 3 "
                       "-network net.fit -load save.pak -matchweather rain -slot BOB 1 0 -fps"};
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(host),
             std::string("-host -port 28800 -player BOB -matchweather rain -slot BOB 1 0 -rejoin"));

    // An internet game's switches are the lobby's too: a guest's rejoin finds the game by its code again.
    const MCLobbyLaunchArgs guest{.Active = true, .CommandLine = "-join PXYL -internet -fps -player ANN -public"};
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(guest), std::string("-join PXYL -internet -player ANN -public -rejoin"));

    // A lobby's switch keeps the word it takes, whatever it says; a -slot short of its words is nothing.
    const MCScratchFolder folder(ScratchGroup, "lobby words");
    WriteNote(folder, NoteBuild, WrittenAt,
              R"(-join 10.0.0.2 -framelog "/tmp/a b.log" -player -load -session "Ann's room" -rejoin -slot ANN 2)");
    const std::optional<std::string> taken = MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt);
    REQUIRE(taken.has_value());
    CHECK_EQ(*taken, std::string(R"(-join 10.0.0.2 -player -load -session "Ann's room" -rejoin)"));
}

/// <summary>
/// The log says what a rejoin note starts, its action and the room's address or code, never its words: a host's place's
/// token is among them.
/// </summary>
TEST_CASE("lobby updates: a rejoin note's start is logged by what it starts, never a place's token")
{
    constexpr std::string_view Token = "0123456789abcdef0123456789abcdef";
    const std::string host = MCRejoinNote::InternetRoomCommandLine({.Hosting = true,
                                                                    .Code = "K7QF",
                                                                    .Token = std::string(Token),
                                                                    .Public = true,
                                                                    .PlayerName = "Big Bob",
                                                                    .SessionName = "Big Bob's game",
                                                                    .MaxPlayers = 4});
    CHECK_EQ(LobbyStartLogged(host), std::string("hosting K7QF again"));
    CHECK(!LobbyStartLogged(host).contains(Token));
    CHECK_EQ(
        LobbyStartLogged(MCRejoinNote::InternetRoomCommandLine({.Code = "K7QF", .Public = true, .PlayerName = "Ann"})),
        std::string("joining K7QF"));
    CHECK_EQ(LobbyStartLogged("-host -port 28800 -player BOB -rejoin"), std::string("hosting"));
    CHECK_EQ(LobbyStartLogged("-join 192.168.1.5:28801 -player \"Ann Lee\" -rejoin"),
             std::string("joining 192.168.1.5:28801"));
}

/// <summary>
/// A host starting again (a restart into an update) takes its port at once, though its last run's end of each link
/// waits out TIME_WAIT (the host's end closes first): the listener reuses the address.
/// </summary>
TEST_CASE("lobby updates: a host's port is taken again at once after its own end closed the last run's links")
{
    constexpr uint16_t Port = 28911;
    constexpr int32_t MostWaits = 200;
    const MCSocket::Handle listener = MCSocket::ListenTcp(Port);
    REQUIRE(listener != MCSocket::InvalidHandle);
    const MCSocket::Handle guest = MCSocket::ConnectTcp({MCSocket::LoopbackIp, Port}, 1000);
    REQUIRE(guest != MCSocket::InvalidHandle);
    MCSocket::Handle accepted = MCSocket::InvalidHandle;
    MCSocket::Address from;

    for (int32_t wait = 0; wait < MostWaits && accepted == MCSocket::InvalidHandle; wait++)
    {
        accepted = MCSocket::AcceptTcp(listener, from);
        SDL_Delay(5);
    }

    REQUIRE(accepted != MCSocket::InvalidHandle);

    // The host's end closes first, as its game quits: that end of the link waits out TIME_WAIT. The guest's end sees it
    // close, and closes too.
    MCSocket::Close(accepted);
    MCSocket::Close(listener);
    std::array<uint8_t, 16> buffer{};
    int32_t read = 0;

    for (int32_t wait = 0; wait < MostWaits && read >= 0; wait++)
    {
        read = MCSocket::Receive(guest, buffer);
        SDL_Delay(5);
    }

    CHECK(read < 0);
    MCSocket::Close(guest);

    const MCSocket::Handle again = MCSocket::ListenTcp(Port);
    CHECK(again != MCSocket::InvalidHandle);
    MCSocket::Close(again);
}

TEST_CASE("lobby updates: a rejoin note too big to read back isn't written")
{
    const MCScratchFolder folder(ScratchGroup, "too big");
    constexpr size_t Long = 5000;
    CHECK(!MCRejoinNote::Write(folder.Path(),
                               {.CommandLine = std::string(Long, 'x'), .Build = NoteBuild, .Time = WrittenAt}));
    CHECK(!std::filesystem::exists(NotePath(folder)));
}

TEST_CASE("lobby updates: a start with no command line takes a fresh rejoin note for its build, once")
{
    const MCScratchFolder folder(ScratchGroup, "fresh");
    constexpr std::string_view Line = "-join 10.0.0.2 -player ANN -maxplayers 6 -rejoin";
    WriteNote(folder, NoteBuild, WrittenAt, Line);

    // Within two minutes, either way of the clock.
    const std::optional<std::string> taken = MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 119);
    REQUIRE(taken.has_value());
    CHECK_EQ(*taken, std::string(Line));
    CHECK(!std::filesystem::exists(NotePath(folder)));
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 1).has_value());

    WriteNote(folder, NoteBuild, WrittenAt, Line);
    CHECK(MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt - 30).has_value());
}

TEST_CASE("lobby updates: a rejoin note too old, or for another build, is ignored")
{
    const MCScratchFolder folder(ScratchGroup, "ignored");
    constexpr std::string_view Line = "-host -port 28800 -player BOB -maxplayers 6 -rejoin";

    // Two minutes old: ignored, and gone.
    WriteNote(folder, NoteBuild, WrittenAt, Line);
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 120).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));
    WriteNote(folder, NoteBuild, WrittenAt, Line);
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt - 121).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));

    // For another build (the update's, while this one starts): ignored, and left for that build.
    WriteNote(folder, NoteBuild + 1, WrittenAt, Line);
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 5).has_value());
    CHECK(std::filesystem::exists(NotePath(folder)));
    CHECK(MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild + 1, WrittenAt + 6).has_value());

    // Not a note, or not a lobby's words: gone, unread.
    {
        std::ofstream file(NotePath(folder), std::ios::binary);
        file << "{\"commandLine\": 5}";
    }

    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));
    WriteNote(folder, NoteBuild, WrittenAt, "-mission 3");
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));
}

namespace
{
    using MCTestGuest::Guest;
    using MCTestGuest::HostLobbyRoom;
    using MCTestGuest::Settle;
    using MCTestGuest::SettleUntil;
    using MCTestUpdater::MCFakeUpdater;
    using MCUpdater::MCAnswer;

    /// <summary>The update the tests find, as the feed shows it.</summary>
    constexpr std::string_view NewVersion = "0.2.2 (480)";
    constexpr std::string_view NewNotes = "Rooms keep their players through an update.";

    /// <summary>The room's waits go by a clock the test moves (MCLobbyUpdate::SetClock), for the test's length.</summary>
    class MCLobbyClock
    {
    public:
        MCLobbyClock()
        {
            MCLobbyUpdate::SetClock([this] { return _Now; });
        }

        ~MCLobbyClock() { MCLobbyUpdate::SetClock(nullptr); }
        MCLobbyClock(const MCLobbyClock&) = delete;
        MCLobbyClock& operator=(const MCLobbyClock&) = delete;

        /// <summary>Moves the clock on by <paramref name="time"/>, and the game sees it.</summary>
        void Pass(std::chrono::milliseconds time)
        {
            _Now += time;
            Settle();
        }

    private:
        std::chrono::steady_clock::time_point _Now = std::chrono::steady_clock::now();
    };

    /// <summary>The middle of <paramref name="object"/>, on the screen.</summary>
    std::pair<int32_t, int32_t> Middle(MCGuiObject& object)
    {
        return {object.GlobalX() + object.Width() / 2, object.GlobalY() + object.Height() / 2};
    }

    /// <summary>Clicks the middle of <paramref name="object"/> as a player does.</summary>
    void ClickOn(MCGuiObject& object)
    {
        const auto [x, y] = Middle(object);
        MCScreenInput::RealClick(x, y);
        Settle();
    }

}

/// <summary>
/// A game a lobby started never shows the main menu, so its session screen offers the update: a line in UPDATE's amber
/// left of the start button while one waits, whose click opens the UPDATE page over the screen; the page takes every
/// click on it (none reaches the start button under it), and DONE brings the screen back.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's session screen offers the update, its line opening the UPDATE page there")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCFakeUpdater updater;
    Guest guest("ANN");
    REQUIRE(HostLobbyRoom("28883", {guest}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    REQUIRE(screen.UpdateLine != nullptr);
    const auto [lineX, lineY] = Middle(*screen.UpdateLine);

    // Nothing waits: no line, and nothing there to click.
    CHECK(screen.FindObject(lineX, lineY) != screen.UpdateLine);
    MCScreenInput::SaveShot("lobby session 0 nothing new", MCScreenInput::ScreenHash());

    MCUpdater::Found(std::string(NewVersion), std::string(NewNotes));
    Settle();
    CHECK(screen.FindObject(lineX, lineY) == screen.UpdateLine);
    MCScreenInput::SaveShot("lobby session 1 update waits", MCScreenInput::ScreenHash());

    ClickOn(*screen.UpdateLine);
    REQUIRE(UpdatePageShown());
    CHECK_EQ(UpdatePageStatus(), std::string("A NEWER VERSION IS OUT."));
    const std::shared_ptr<MCLogTextButton> install = MCScreenInput::FindTextButton(screen, "INSTALL");
    const std::shared_ptr<MCLogTextButton> done = MCScreenInput::FindTextButton(screen, "DONE");
    REQUIRE(install != nullptr);
    REQUIRE(done != nullptr);
    CHECK(install->IsShowing());
    MCScreenInput::SaveShot("lobby session 2 update page", MCScreenInput::ScreenHash());

    // The start button is under the page: a click there stays on the page.
    const auto [startX, startY] = Middle(*screen.StartButton);
    CHECK(screen.FindObject(startX, startY) != screen.StartButton);

    ClickOn(*done);
    CHECK(!UpdatePageShown());
    CHECK(screen.FindObject(lineX, lineY) == screen.UpdateLine);
    CHECK(screen.FindObject(startX, startY) == screen.StartButton);
}

namespace
{
    /// <summary>The build the found update (<see cref="NewVersion"/>) brings.</summary>
    constexpr uint64_t NewBuild = 480;

    /// <summary>The update found (as <paramref name="version"/>), downloaded and ready: RESTART installs it.</summary>
    void UpdateReady(std::string_view version = NewVersion)
    {
        MCUpdater::Found(std::string(version), std::string(NewNotes));
        MCUpdater::Answer(MCAnswer::Install);
        MCUpdater::Ready();
        Settle();
    }

    /// <summary>Whether the room this game hosts takes new players.</summary>
    bool RoomOpen()
    {
        return (MultiPlayer()->SessionManager->CurrentSession()->SessionDesc.dwFlags & DPSESSION_NEWPLAYERSDISABLED) ==
               0;
    }

    /// <summary>Whether the session screen shows player <paramref name="id"/>.</summary>
    bool Shows(const MCSessionScreen& screen, uint32_t id)
    {
        return std::ranges::any_of(screen.PlayerNames, [id](const auto& name)
                                   { return name != nullptr && name->ShowWindow && name->PlayerId == id; });
    }
}

/// <summary>
/// A lobby's host restarting into an update: its page says how many of the other players join again (the guests whose
/// builds wait for it); RESTART tells them, writes the rejoin note with the lobby's words for the update's build, and
/// then the update installs; the guests leaving as it goes don't stop it with "has left the game". Once the match is
/// under way, RESTART waits for the game to exit at its end.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a lobby's host offers RESTART only when every guest can rejoin, before the match; restarting, it "
    "tells them and leaves the rejoin note")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCFakeUpdater updater;
    Guest ann("ANN");
    Guest cal("CAL");
    REQUIRE(HostLobbyRoom("28885", {ann, cal}));
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    ann.SendRules(hostId, MCMatchRule::Supported);
    cal.SendRules(hostId, MCMatchRule::Supported & ~MCMatchRule::RejoinsHost);
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    UpdateReady();
    ShowUpdatePageOver(screen);
    Settle();
    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(screen, "RESTART");
    REQUIRE(restart != nullptr);

    // CAL's game can't rejoin: no RESTART, and the update goes in as the game exits after the match.
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));
    CHECK(!restart->IsShowing());
    CHECK(MCLobbyUpdate::Restart());
    Settle();
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});
    CHECK_EQ(ann.Updatings(), 0);

    // Every guest can.
    cal.SendRules(hostId, MCMatchRule::Supported);
    REQUIRE(SettleUntil([&] { return restart->IsShowing(); }));
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. RESTART TO INSTALL IT; EVERYONE WILL REJOIN."));
    MCScreenInput::SaveShot("lobby host 0 ready", MCScreenInput::ScreenHash());

    // Once the match is under way, neither: the update goes in as the game exits at its end.
    MultiPlayer()->InLogistics = true;
    Settle();
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));
    CHECK(!restart->IsShowing());
    CHECK(MCLobbyUpdate::Restart());
    Settle();
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});
    CHECK_EQ(ann.Updatings(), 0);
    MultiPlayer()->InLogistics = false;
    Settle();

    // Before it, the game restarts into the update, telling every guest.
    ClickOn(*restart);
    CHECK(SettleUntil([&] { return ann.Updatings() == 1 && cal.Updatings() == 1; }));
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
    CHECK(updater.Log->Answers.back() == MCAnswer::Install);

    const std::optional<std::string> note =
        MCRejoinNote::TakeAtStart(MCTestGame::UserFolder(), NewBuild, MCRejoinNote::Now());
    REQUIRE(note.has_value());
    CHECK_EQ(*note, std::string("-host -port 28885 -player BOB -rejoin"));

    ann.Leave();
    cal.Leave();
    Settle();
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
}

/// <summary>
/// A lobby's guest restarting into an update: its Updating message has the host keep its place (its name in its slot),
/// the room taking players until START; once it has joined again the host shows everyone the room again, the guest in
/// it. The room keeps its place once: back, its next Updating is ignored.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest restarting into an update keeps its place in the room, and is shown the room "
                   "when it's back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Guest ann("ANN");
    REQUIRE(HostLobbyRoom("28887", {ann}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    // The room takes players until START.
    CHECK(RoomOpen());
    CHECK(Shows(screen, ann.Id()));

    ann.SendRules(hostId, MCMatchRule::Supported);
    ann.SendUpdating(hostId);
    CHECK(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(ann.Id()); }));
    ann.Leave();
    Settle();
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    MCScreenInput::SaveShot("lobby guest 0 updating", MCScreenInput::ScreenHash());

    Guest back("ANN");
    REQUIRE(back.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28887"));
    CHECK(SettleUntil([&] { return back.SessionScreens() > 0; }));
    CHECK(Shows(screen, back.Id()));
    CHECK(!Shows(screen, ann.Id()));
    CHECK(RoomOpen());
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    MCScreenInput::SaveShot("lobby guest 1 back", MCScreenInput::ScreenHash());

    back.SendRules(hostId, MCMatchRule::Supported);
    back.SendUpdating(hostId);
    Settle();
    CHECK(RoomOpen());
    CHECK(!MCLobbyUpdate::LeftToUpdate(back.Id()));
}

namespace
{
    /// <summary>A mission the host's start needs loaded (the screen asks only that one is).</summary>
    constexpr std::string_view StartMission = "ARENA";

    /// <summary>
    /// The host's picks that light START: itself on team 1, <paramref name="others"/> on team 2, a mission loaded.
    /// </summary>
    void PickTeams(MCSessionScreen& screen, std::initializer_list<uint32_t> others)
    {
        screen.AssignPlayer(MultiPlayer()->SessionManager->MyPlayer()->Id, 1, 0, false);
        int8_t slot = 0;

        for (const uint32_t id : others)
        {
            screen.AssignPlayer(id, 2, slot++, false);
        }

        screen.MissionFile = StartMission;
        screen.CheckGoodToGo();
    }
}

/// <summary>
/// While a guest is away updating, the line left of START says whom the room waits for, and START asks before it starts
/// without the guest (CANCEL stays in the room); once the guest is back (the room shown again, its picks started over)
/// START no longer asks. A match that starts lets the places kept go and closes the room, so no new game joins it.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's START asks while a guest is away updating, and the room closes as a match starts")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Guest ann("ANN");
    Guest cal("CAL");
    REQUIRE(HostLobbyRoom("28899", {ann, cal}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    ann.SendRules(hostId, MCMatchRule::Supported);
    cal.SendRules(hostId, MCMatchRule::Supported);
    PickTeams(screen, {ann.Id(), cal.Id()});
    REQUIRE(!screen.StartButton->Disabled);
    CHECK(screen.StartWaits.empty());

    ann.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(ann.Id()); }));
    CHECK(!screen.StartButton->Disabled);
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR ANN TO REJOIN."));
    MCScreenInput::SaveShot("lobby start waits 0 guest away", MCScreenInput::ScreenHash());

    // The guest gone (as it quits), its name left in its slot: START asks, and CANCEL stays in the room.
    ann.Leave();
    Settle();
    MCReusableDialog& question = *GlobalLog()->QuestionDialog;
    ClickOn(*screen.StartButton);
    REQUIRE(question.IsShowing());
    CHECK_EQ(question.Text, std::string("ANN IS UPDATING. START WITHOUT ANN?"));
    MCScreenInput::SaveShot("lobby start waits 1 start asks", MCScreenInput::ScreenHash());
    ClickOn(*question.CancelButton);
    CHECK(!question.IsShowing());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    CHECK(MCLobbyUpdate::LeftToUpdate(ann.Id()));
    CHECK(RoomOpen());

    Guest back("ANN");
    REQUIRE(back.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28899"));
    REQUIRE(SettleUntil([&] { return back.SessionScreens() > 0; }));
    CHECK(screen.StartWaits.empty());
    CHECK(MCLobbyUpdate::StartQuestion().empty());
    CHECK(RoomOpen());
    PickTeams(screen, {back.Id(), cal.Id()});
    CHECK(!screen.StartButton->Disabled);

    // A match started with a place kept: the place goes, and the room closes.
    cal.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(cal.Id()); }));
    CHECK(MCLobbyUpdate::LeftToUpdate(cal.Id()));
    MultiPlayer()->InLogistics = true;
    Settle();
    CHECK(!RoomOpen());
    CHECK(!MCLobbyUpdate::LeftToUpdate(cal.Id()));
    CHECK(MCLobbyUpdate::StartWaitsText().empty());
    MultiPlayer()->InLogistics = false;
}

/// <summary>
/// A guest away updating has half a minute to rejoin: then its place goes, its name and its place on a team with it, and
/// START lights with the picks left; the room still takes players. A guest whose restart is off (its update failed)
/// says so, and the room lets its place go at once.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's room lets a guest away updating go after its half minute, or at once when its "
                   "restart is off")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLobbyClock clock;
    Guest ann("ANN");
    Guest cal("CAL");
    REQUIRE(HostLobbyRoom("28901", {ann, cal}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    ann.SendRules(hostId, MCMatchRule::Supported);
    cal.SendRules(hostId, MCMatchRule::Supported);
    PickTeams(screen, {ann.Id(), cal.Id()});
    REQUIRE(!screen.StartButton->Disabled);

    cal.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(cal.Id()); }));
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR CAL TO REJOIN."));
    cal.SendRestartOff(hostId);
    REQUIRE(SettleUntil([&] { return !MCLobbyUpdate::LeftToUpdate(cal.Id()); }));
    CHECK(screen.StartWaits.empty());
    CHECK(!MCLobbyUpdate::LeftToUpdate(cal.Id()));

    ann.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(ann.Id()); }));
    ann.Leave();
    Settle();
    clock.Pass(MCLobbyUpdate::RejoinWait - std::chrono::seconds(1));
    CHECK(RoomOpen());
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR ANN TO REJOIN."));
    CHECK(Shows(screen, ann.Id()));

    clock.Pass(std::chrono::seconds(1));
    CHECK(RoomOpen());
    CHECK(!Shows(screen, ann.Id()));
    CHECK(screen.StartWaits.empty());
    CHECK(!screen.StartButton->Disabled);
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    MCScreenInput::SaveShot("lobby start waits 1 guest not back", MCScreenInput::ScreenHash());
}

/// <summary>
/// A kept place has START ask, so the room keeps one only for a guest whose build says it rejoins (its rules), and once
/// a room: a guest that never said its rules, or whose build doesn't rejoin, is ignored;
/// another player's RestartOff leaves the place be; and an Updating after the guest's half minute is over, or after its own
/// RestartOff, keeps nothing (no guest keeps the room open again and again).
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's room keeps a guest's place only for a build that rejoins, and once")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLobbyClock clock;
    Guest ann("ANN");
    Guest cal("CAL");
    REQUIRE(HostLobbyRoom("28915", {ann, cal}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    PickTeams(screen, {ann.Id(), cal.Id()});
    REQUIRE(!screen.StartButton->Disabled);

    // ANN never said its rules; CAL's build doesn't rejoin.
    ann.SendUpdating(hostId);
    cal.SendRules(hostId, MCMatchRule::Supported & ~MCMatchRule::RejoinsHost);
    cal.SendUpdating(hostId);
    Settle();
    CHECK(!screen.StartButton->Disabled);
    CHECK(screen.StartWaits.empty());
    CHECK(!MCLobbyUpdate::LeftToUpdate(ann.Id()));
    CHECK(!MCLobbyUpdate::LeftToUpdate(cal.Id()));

    // CAL's build that does: its place is kept, and ANN's RestartOff doesn't drop it.
    cal.SendRules(hostId, MCMatchRule::Supported);
    cal.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(cal.Id()); }));
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR CAL TO REJOIN."));
    ann.SendRestartOff(hostId);
    Settle();
    CHECK(RoomOpen());
    CHECK(MCLobbyUpdate::LeftToUpdate(cal.Id()));

    // Its half minute over (it stayed), the place goes; its next Updating is ignored.
    clock.Pass(MCLobbyUpdate::RejoinWait);
    CHECK(!screen.StartButton->Disabled);
    cal.SendUpdating(hostId);
    Settle();
    CHECK(!screen.StartButton->Disabled);
    CHECK(screen.StartWaits.empty());
    CHECK(!MCLobbyUpdate::LeftToUpdate(cal.Id()));

    // ANN's, called off by its RestartOff before its half minute is over: its next Updating keeps nothing either.
    ann.SendRules(hostId, MCMatchRule::Supported);
    ann.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(ann.Id()); }));
    ann.SendRestartOff(hostId);
    REQUIRE(SettleUntil([&] { return !MCLobbyUpdate::LeftToUpdate(ann.Id()); }));
    ann.SendUpdating(hostId);
    Settle();
    CHECK(!screen.StartButton->Disabled);
    CHECK(!MCLobbyUpdate::LeftToUpdate(ann.Id()));
}

/// <summary>
/// START counts only the players there: a team of guests away updating alone grays it (the line says whom the room
/// waits for), and with a player there on each team it asks before it starts without the guests away, naming them all.
/// Its OK starts the match: they leave their teams before the teams are published, so the match's teams and their slots
/// are the players there; the places go and the room closes.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's START counts only the players there, and asks before it starts without the guests "
                   "away updating, who leave their teams first")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Guest ann("ANN");
    Guest cal("CAL");
    Guest dan("DAN");
    REQUIRE(HostLobbyRoom("28919", {ann, cal, dan}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;

    for (Guest& guest : {std::ref(ann), std::ref(cal), std::ref(dan)})
    {
        guest.SendRules(hostId, MCMatchRule::Supported);
    }

    // The host and DAN on team 1, ANN and CAL on team 2.
    screen.AssignPlayer(hostId, 1, 0, false);
    screen.AssignPlayer(dan.Id(), 1, 1, false);
    screen.AssignPlayer(ann.Id(), 2, 0, false);
    screen.AssignPlayer(cal.Id(), 2, 1, false);
    screen.MissionFile = StartMission;
    screen.CheckGoodToGo();
    REQUIRE(!screen.StartButton->Disabled);

    for (Guest& guest : {std::ref(ann), std::ref(cal)})
    {
        guest.SendUpdating(hostId);
        REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(guest.Id()); }));
        guest.Leave();
        Settle();
    }

    // Team 2 only the guests away: START grays, and a click asks nothing.
    CHECK(screen.StartButton->Disabled);
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR ANN, CAL TO REJOIN."));
    MCReusableDialog& question = *GlobalLog()->QuestionDialog;
    ClickOn(*screen.StartButton);
    CHECK(!question.IsShowing());

    // DAN on team 2 too: START asks, naming both, and OK starts the match without them.
    screen.AssignPlayer(dan.Id(), 2, 2, false);
    CHECK(!screen.StartButton->Disabled);
    ClickOn(*screen.StartButton);
    REQUIRE(question.IsShowing());
    CHECK_EQ(question.Text, std::string("ANN, CAL ARE UPDATING. START WITHOUT THEM?"));
    ClickOn(*question.OkButton);
    CHECK(!question.IsShowing());
    CHECK(GlobalLog()->CurrentScreen() != GlobalLog()->SessionScreen);
    CHECK(std::ranges::contains(MultiPlayer()->Match.Team1Players, hostId));
    CHECK(std::ranges::contains(MultiPlayer()->Match.Team2Players, dan.Id()));

    for (const uint32_t id : {ann.Id(), cal.Id()})
    {
        CHECK(!std::ranges::contains(MultiPlayer()->Match.Team1Players, id));
        CHECK(!std::ranges::contains(MultiPlayer()->Match.Team2Players, id));
        CHECK(!MCLobbyUpdate::LeftToUpdate(id));
    }

    CHECK(MCLobbyUpdate::StartQuestion().empty());
    CHECK(!RoomOpen());
}

/// <summary>
/// A guest left unpicked holds START, as the original's screen does; leaving to update, it no longer does: START lights
/// as the session lets it go, with no other change in the room.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's START lights as an unpicked guest leaves to update")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Guest ann("ANN");
    Guest dan("DAN");
    REQUIRE(HostLobbyRoom("28921", {ann, dan}));
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    ann.SendRules(hostId, MCMatchRule::Supported);
    dan.SendRules(hostId, MCMatchRule::Supported);
    PickTeams(screen, {dan.Id()});
    REQUIRE(screen.StartButton->Disabled);

    ann.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(ann.Id()); }));
    CHECK(screen.StartButton->Disabled);
    ann.Leave();
    CHECK(SettleUntil([&] { return !screen.StartButton->Disabled; }));
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR ANN TO REJOIN."));
}

namespace
{
    /// <summary>The version shown without its build: the new version's build isn't known.</summary>
    constexpr std::string_view VersionWithoutBuild = "0.2.2";

    /// <summary>The rejoin note's file in the game's user folder.</summary>
    std::filesystem::path GameNotePath()
    {
        return MCTestGame::UserFolder() / MCRejoinNote::FileName;
    }

    /// <summary>The UPDATE page over the session screen, its RESTART button (null when the page has none).</summary>
    std::shared_ptr<MCLogTextButton> RestartOnPage(MCSessionScreen& screen)
    {
        ShowUpdatePageOver(screen);
        Settle();
        return MCScreenInput::FindTextButton(screen, "RESTART");
    }
}

/// <summary>
/// A lobby's host's RESTART waits for its guests' games to take the room's word, or a guest would find its link gone
/// with the word unread and take the host's leave for a lost connection (seen live, 7 Nov): RESTART shows once the
/// newest guest's game is up (it pinged; the page says what RESTART will do meanwhile), and the update goes in once the
/// guests have left to wait for the host.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's host's RESTART waits for its guests' games to open the room, and to leave it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28931";
    MCLobbyClock clock;
    MCFakeUpdater updater;
    Guest ann("ANN");
    REQUIRE(HostLobbyRoom(Port, {ann}));
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    ann.SendRules(hostId, MCMatchRule::Supported);

    // CAL has joined, its rules known from its join card; its game hasn't opened the room yet.
    Guest cal("CAL", MCMatchRule::Supported);
    REQUIRE(cal.Join(*MultiPlayer()->SessionManager->DirectPlay, std::format("127.0.0.1:{}", Port), false));
    UpdateReady();
    const std::shared_ptr<MCLogTextButton> restart = RestartOnPage(screen);
    REQUIRE(restart != nullptr);
    CHECK(MultiPlayer()->PlayerRules.contains(cal.Id()));
    CHECK(!MCLobbyUpdate::CanRestart());
    CHECK(!restart->IsShowing());
    CHECK_EQ(MCLobbyUpdate::ReadyText(), std::string("DOWNLOADED. RESTART TO INSTALL IT; EVERYONE WILL REJOIN."));

    // Its game is up: RESTART shows.
    cal.Ping();
    REQUIRE(SettleUntil([&] { return restart->IsShowing(); }));
    CHECK(MCLobbyUpdate::CanRestart());

    // RESTART: the room told, and the update in once both guests have left to wait for the host.
    ClickOn(*restart);
    REQUIRE(SettleUntil([&] { return ann.Updatings() == 1 && cal.Updatings() == 1; }));
    clock.Pass(MCLobbyUpdate::SendTime);
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});
    ann.Leave();
    cal.Leave();
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
}

/// <summary>
/// RESTART brings a lobby's game back to its room only with the rejoin note, which names the build it starts as: with
/// the update's build unknown, or the note unwritable, the page says the update installs after the match, and the
/// room is told nothing.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's RESTART waits for the match's end when the update's build isn't known, or the "
                   "rejoin note can't be written")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCFakeUpdater updater;
    Guest ann("ANN");
    REQUIRE(HostLobbyRoom("28903", {ann}));
    ann.SendRules(MultiPlayer()->SessionManager->MyPlayer()->Id, MCMatchRule::Supported);
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    UpdateReady(VersionWithoutBuild);
    const std::shared_ptr<MCLogTextButton> restart = RestartOnPage(screen);
    REQUIRE(restart != nullptr);
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));
    CHECK(!restart->IsShowing());
    CHECK(MCLobbyUpdate::Restart());
    Settle();
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});

    // The build known, but the note's file taken by a folder.
    std::filesystem::create_directories(GameNotePath());
    UpdateReady();
    REQUIRE(SettleUntil([&] { return restart->IsShowing(); }));
    ClickOn(*restart);
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));
    CHECK(!restart->IsShowing());
    Settle();
    CHECK_EQ(updater.Log->Answers.size(), size_t{2});
    CHECK_EQ(ann.Updatings(), 0);
    CHECK(!MCLobbyUpdate::LeftToUpdate(ann.Id()));
    std::filesystem::remove(GameNotePath());
}

/// <summary>
/// A lobby's host whose update fails after RESTART told the room: the restart is off, the note goes, and the guests
/// still there are told; those gone to wait for it (as their games do at once) have their places kept, the room open,
/// and rejoin it as they try the same address again: the host shows them the room. It is the same room: a guest whose
/// place it kept once before still has none kept, as that guest's game knows.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's host whose update fails after RESTART keeps its guests' places, and they rejoin")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLobbyClock clock;
    MCFakeUpdater updater;
    Guest annFirst("ANN");
    Guest cal("CAL");
    REQUIRE(HostLobbyRoom("28905", {annFirst, cal}));
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayer()->Id;
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;

    // ANN has updated in this room already: the room has kept its place once.
    annFirst.SendRules(hostId, MCMatchRule::Supported);
    annFirst.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(annFirst.Id()); }));
    annFirst.Leave();
    Settle();
    Guest ann("ANN");
    REQUIRE(ann.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28905"));
    Settle();
    REQUIRE(SettleUntil([&] { return ann.SessionScreens() > 0; }));
    ann.SendRules(hostId, MCMatchRule::Supported);
    cal.SendRules(hostId, MCMatchRule::Supported);
    UpdateReady();
    const std::shared_ptr<MCLogTextButton> restart = RestartOnPage(screen);
    REQUIRE(restart != nullptr);
    REQUIRE(SettleUntil([&] { return restart->IsShowing(); }));
    ClickOn(*restart);
    REQUIRE(SettleUntil([&] { return ann.Updatings() == 1 && cal.Updatings() == 1; }));
    CHECK(std::filesystem::exists(GameNotePath()));

    // The update waits for the guests to leave to wait for the host: ANN's game has gone; CAL's is still on its way
    // out, and the update goes in without it after LeaveWait.
    clock.Pass(MCLobbyUpdate::SendTime);
    ann.Leave();
    Settle();
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});
    clock.Pass(MCLobbyUpdate::LeaveWait);
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{2});
    MCUpdater::Failed("the disk is full");
    REQUIRE(SettleUntil([&] { return cal.RestartOffs() == 1; }));
    CHECK(!std::filesystem::exists(GameNotePath()));
    CHECK(RoomOpen());
    // ANN, back from its own update, joined the room after CAL.
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR CAL, ANN TO REJOIN."));
    cal.Leave();
    Settle();
    CHECK(!GlobalLog()->MessageDialog->IsShowing());

    Guest annBack("ANN");
    Guest calBack("CAL");
    REQUIRE(annBack.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28905"));
    REQUIRE(calBack.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28905"));
    Settle();
    REQUIRE(SettleUntil([&] { return annBack.SessionScreens() > 0 && calBack.SessionScreens() > 0; }));
    CHECK(RoomOpen());
    CHECK(screen.StartWaits.empty());
    CHECK(Shows(screen, annBack.Id()));
    CHECK(Shows(screen, calBack.Id()));
    CHECK(!GlobalLog()->MessageDialog->IsShowing());

    // The same room: it still has kept ANN's place once (ANN's game offers no RESTART), so ANN's Updating keeps
    // nothing. CAL's place it kept only for its host's restart: CAL's own is kept.
    annBack.SendRules(hostId, MCMatchRule::Supported);
    calBack.SendRules(hostId, MCMatchRule::Supported);
    annBack.SendUpdating(hostId);
    Settle();
    CHECK(screen.StartWaits.empty());
    CHECK(!MCLobbyUpdate::LeftToUpdate(annBack.Id()));
    calBack.SendUpdating(hostId);
    REQUIRE(SettleUntil([&] { return MCLobbyUpdate::LeftToUpdate(calBack.Id()); }));
    CHECK_EQ(screen.StartWaits, std::string("WAITING FOR CAL TO REJOIN."));
    CHECK(MCLobbyUpdate::LeftToUpdate(calBack.Id()));
}

namespace
{
    using MCTestGuest::Host;

    /// <summary>The room the guest tests join: its host's name and the session's.</summary>
    constexpr std::string_view HostName = "BOB";
    constexpr std::string_view RoomName = "BOB's room";

    /// <summary>Whether this game is in a room.</summary>
    bool InSession()
    {
        return MultiPlayer() != nullptr && MultiPlayer()->SessionManager->CurrentSession() != nullptr;
    }

    /// <summary>The message dialog's text, when it shows.</summary>
    std::string DialogText()
    {
        const MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        return dialog.ShowWindow ? dialog.Text : std::string();
    }

    /// <summary>
    /// Boots the game as a lobby's start after its restart into an update: with no command line, the rejoin note
    /// <paramref name="noteLine"/> left for this build in the user folder gives its words (as the app's start takes
    /// them), and it joins.
    /// </summary>
    bool StartFromNote(std::string_view noteLine)
    {
        const std::filesystem::path user = MCTestGame::UserFolder();
        std::filesystem::create_directories(user);
        REQUIRE(MCRejoinNote::Write(
            user, {.CommandLine = std::string(noteLine), .Build = NewBuild, .Time = MCRejoinNote::Now()}));
        const std::optional<std::string> line = MCRejoinNote::TakeAtStart(user, NewBuild, MCRejoinNote::Now());
        REQUIRE(line.has_value());
        return MCTestGame::StartLobby(*line);
    }

    /// <summary>Whether the message dialog's OK ends the game, back to the lobby (CancelToMPlayer, which quits it).</summary>
    bool OkEndsTheGame()
    {
        const MCGuiCallback& ok = GlobalLog()->MessageDialog->OkButton->Callback();

        if (!ok.Runs(CancelToMPlayer))
        {
            return false;
        }

        try
        {
            ok.Exec();
        }
        catch (const MCGameQuit& quit)
        {
            return quit.ExitCode == 0;
        }

        return false;
    }

    /// <summary>
    /// Boots the game to rejoin a room at <paramref name="port"/>, which fails: what it says (its OK ending the game, as
    /// the test checks).
    /// </summary>
    std::string RejoinFails(std::string_view port)
    {
        if (!StartFromNote(
                std::format("-join 127.0.0.1:{} -player ANN -session \"{}\" -maxplayers 6 -rejoin", port, RoomName)))
        {
            return {};
        }

        CHECK(!InSession());
        std::string text = DialogText();
        CHECK(OkEndsTheGame());
        return text;
    }
}

/// <summary>
/// A lobby's guest started again from the rejoin note joins the room as it says; the room has kept its place once, so
/// its next update waits for the match's end. When its host restarts into an update, its game leaves the room at once,
/// says "BOB IS UPDATING." and joins the same address again: a restart called off (the host's update failed) opens the
/// same room to it again, which still has kept its place once (no RESTART); after a restart that goes in, the old room
/// (closed, still answering) doesn't take it, the new one on the same port does, and it goes straight into the room
/// (the session screen). That room is new: RESTART comes back.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest started from the rejoin note joins, and waits for its host restarting into "
                   "an update")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCFakeUpdater updater;
    constexpr std::string_view Port = "28889";
    auto bob = std::make_unique<Host>();
    REQUIRE(bob->Open(Port, std::string(HostName), std::string(RoomName)));
    REQUIRE(StartFromNote(
        std::format("-join 127.0.0.1:{} -player ANN -session \"{}\" -maxplayers 6 -rejoin", Port, RoomName)));
    REQUIRE(InSession());
    REQUIRE(SettleUntil([&] { return !bob->Joined().empty(); }));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    MultiPlayer()->PlayerRules[bob->Id()] = MCMatchRule::Supported;
    UpdateReady();
    CHECK(!MCLobbyUpdate::CanRestart());

    // The host's START closed the room. The room's numbering tells a guest which player is its host (the test's host
    // numbers no one).
    auto hostRestarts = [&]
    {
        bob->Lock();
        MultiPlayer()->ServerID = bob->Id();
        bob->SendUpdating(MultiPlayer()->SessionManager->MyPlayerID);
        return SettleUntil([] { return !InSession(); });
    };

    CHECK(hostRestarts());
    CHECK_EQ(DialogText(), std::string("BOB IS UPDATING."));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->ConnectScreen);
    MCScreenInput::SaveShot("lobby guest wait 0 host updating", MCScreenInput::ScreenHash());

    // The host's update fails: it calls its restart off, and its room takes its guests again. Back in that room, which
    // has kept this guest's place once, as its host remembers: still no RESTART.
    bob->Unlock();
    REQUIRE(SettleUntil(InSession));
    CHECK(DialogText().empty());
    MultiPlayer()->PlayerRules[bob->Id()] = MCMatchRule::Supported;
    CHECK(!MCLobbyUpdate::CanRestart());
    CHECK_EQ(MCLobbyUpdate::ReadyText(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));

    // Windows' program put in place already (the swap) plays from the next match the lobby starts; a setup a lobby's
    // job would end waits for a start the player makes.
    const auto readyAs = [&](MCUpdater::MCGoesIn goesIn)
    {
        updater.Log->GoesIn = goesIn;
        MCUpdater::Ready();
        return MCLobbyUpdate::ReadyText();
    };

    CHECK_EQ(readyAs(MCUpdater::MCGoesIn::NextStart),
             std::format("VERSION {} IS IN. IT PLAYS FROM YOUR NEXT MATCH.", NewVersion));
    CHECK_EQ(readyAs(MCUpdater::MCGoesIn::PlayersStart),
             std::format("VERSION {} INSTALLS WHEN YOU START REDUX HD YOURSELF.", NewVersion));
    CHECK_EQ(readyAs(MCUpdater::MCGoesIn::AsItQuits), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));

    // It restarts again, its update going in this time.
    CHECK(hostRestarts());
    CHECK_EQ(DialogText(), std::string("BOB IS UPDATING."));

    // The host's game quits, and its new version opens the room again.
    bob->Close();
    bob.reset();
    Settle();
    CHECK(!InSession());
    Host newBob;
    REQUIRE(newBob.Open(Port, std::string(HostName), std::string(RoomName)));
    CHECK(SettleUntil([&] { return !newBob.Joined().empty(); }));
    CHECK(InSession());
    CHECK(DialogText().empty());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    MultiPlayer()->PlayerRules[newBob.Id()] = MCMatchRule::Supported;
    CHECK(MCLobbyUpdate::CanRestart());
}

/// <summary>
/// A lobby's guest restarting into an update from the session screen: its page says it joins the room again; RESTART
/// tells every player, writes the rejoin note with the lobby's words for the update's build, then the update installs.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest restarting into an update tells the room, and leaves the rejoin note")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCFakeUpdater updater;
    Host bob;
    REQUIRE(bob.Open("28897", std::string(HostName), std::string(RoomName)));
    REQUIRE(MCTestGame::StartLobby(std::format("-join 127.0.0.1:28897 -player ANN -session \"{}\"", RoomName)));
    REQUIRE(SettleUntil([&] { return !bob.Joined().empty(); }));

    // Joined, the guest is in its room.
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    UpdateReady();
    ShowUpdatePageOver(screen);
    Settle();
    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(screen, "RESTART");
    REQUIRE(restart != nullptr);

    // Until the host's rules come (relayed with every player's), its game isn't known to rejoin.
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));
    CHECK(!restart->IsShowing());
    MultiPlayer()->PlayerRules[bob.Id()] = MCMatchRule::Supported;
    Settle();
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. RESTART TO INSTALL IT AND REJOIN THIS ROOM."));
    REQUIRE(restart->IsShowing());
    ClickOn(*restart);
    CHECK(SettleUntil([&] { return bob.Updatings() == 1; }));
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
    CHECK(updater.Log->Answers.back() == MCAnswer::Install);

    const std::optional<std::string> note =
        MCRejoinNote::TakeAtStart(MCTestGame::UserFolder(), NewBuild, MCRejoinNote::Now());
    REQUIRE(note.has_value());
    CHECK_EQ(*note, std::format("-join 127.0.0.1:28897 -player ANN -session \"{}\" -rejoin", RoomName));
}

/// <summary>
/// A lobby's guest whose host said it restarts but whose room doesn't come back within its half minute says the room has
/// closed, and its OK ends the game, back to the lobby.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a lobby's guest whose host isn't back within half a minute says the room has closed, and its OK "
    "ends the game")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLobbyClock clock;
    auto bob = std::make_unique<Host>();
    REQUIRE(bob->Open("28909", std::string(HostName), std::string(RoomName)));
    REQUIRE(MCTestGame::StartLobby(std::format("-join 127.0.0.1:28909 -player ANN -session \"{}\"", RoomName)));
    REQUIRE(SettleUntil([&] { return !bob->Joined().empty(); }));
    bob->Lock();
    MultiPlayer()->ServerID = bob->Id();
    bob->SendUpdating(MultiPlayer()->SessionManager->MyPlayerID);
    REQUIRE(SettleUntil([] { return !InSession(); }));
    bob->Close();
    bob.reset();

    clock.Pass(MCLobbyUpdate::RejoinWait - MCLobbyUpdate::TryEvery);
    CHECK_EQ(DialogText(), std::string("BOB IS UPDATING."));
    clock.Pass(MCLobbyUpdate::TryEvery);
    CHECK(!InSession());
    CHECK_EQ(DialogText(), std::string("Couldn't join the room again: it has closed."));
    CHECK(OkEndsTheGame());
}

namespace
{
    /// <summary>
    /// Boots the game as a lobby's guest in the test's room on <paramref name="port"/>, in the room (the session screen)
    /// from its join, the host's build known to rejoin, the update ready: its UPDATE page's RESTART, showing.
    /// </summary>
    std::shared_ptr<MCLogTextButton> GuestReadyToRestart(Host& bob, std::string_view port)
    {
        if (!bob.Open(port, std::string(HostName), std::string(RoomName)) ||
            !MCTestGame::StartLobby(std::format("-join 127.0.0.1:{} -player ANN -session \"{}\"", port, RoomName)) ||
            !SettleUntil([&] { return !bob.Joined().empty(); }))
        {
            return nullptr;
        }

        if (GlobalLog()->CurrentScreen() != GlobalLog()->SessionScreen)
        {
            return nullptr;
        }

        MultiPlayer()->PlayerRules[bob.Id()] = MCMatchRule::Supported;
        UpdateReady();
        std::shared_ptr<MCLogTextButton> restart = RestartOnPage(*GlobalLog()->SessionScreen);
        return restart != nullptr && SettleUntil([&] { return restart->IsShowing(); }) ? restart : nullptr;
    }
}

/// <summary>
/// A lobby's guest calls its RESTART off when the room's match starts before the update goes in (the host's START
/// crossing it): the note goes, the host is told, and the game stays in the room as before. Its room has kept its
/// place once, so it keeps none again: its next RESTART waits for the match's end, telling the room nothing.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest calls its restart off when the match starts first, telling the room, and "
                   "then waits for the match's end")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLobbyClock clock;
    MCFakeUpdater updater;
    Host bob;
    const std::shared_ptr<MCLogTextButton> restart = GuestReadyToRestart(bob, "28907");
    REQUIRE(restart != nullptr);

    ClickOn(*restart);
    REQUIRE(SettleUntil([&] { return bob.Updatings() == 1; }));
    MultiPlayer()->InLogistics = true;
    clock.Pass(MCLobbyUpdate::SendTime);
    REQUIRE(SettleUntil([&] { return bob.RestartOffs() == 1; }));
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});
    CHECK(!std::filesystem::exists(GameNotePath()));
    CHECK(!MCLobbyUpdate::LeftToUpdate(bob.Id()));
    MultiPlayer()->InLogistics = false;
    Settle();

    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED. IT WILL INSTALL AFTER THE MATCH."));
    CHECK(!restart->IsShowing());
    CHECK(MCLobbyUpdate::Restart());
    clock.Pass(MCLobbyUpdate::SendTime);
    CHECK_EQ(bob.Updatings(), 1);
    CHECK_EQ(updater.Log->Answers.size(), size_t{1});
    CHECK(!std::filesystem::exists(GameNotePath()));
    CHECK(InSession());
}

/// <summary>
/// A lobby's guest calls its RESTART off when the update fails after it went in: the note goes, the host is told, and
/// the game stays in the room as before.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest calls its restart off when the update fails, telling the room")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLobbyClock clock;
    MCFakeUpdater updater;
    Host bob;
    const std::shared_ptr<MCLogTextButton> restart = GuestReadyToRestart(bob, "28917");
    REQUIRE(restart != nullptr);

    ClickOn(*restart);
    REQUIRE(SettleUntil([&] { return bob.Updatings() == 1; }));
    clock.Pass(MCLobbyUpdate::SendTime);
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{2});
    CHECK(MCLobbyUpdate::LeftToUpdate(bob.Id()));
    MCUpdater::Failed("the disk is full");
    REQUIRE(SettleUntil([&] { return bob.RestartOffs() == 1; }));
    CHECK(!std::filesystem::exists(GameNotePath()));
    CHECK(!MCLobbyUpdate::LeftToUpdate(bob.Id()));
    CHECK(InSession());
}

/// <summary>
/// A lobby's host started again from the rejoin note that can't open its room on its port (something else holds it)
/// says so, not a guest's reason; its OK ends the game.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's host whose rejoin can't open its room says so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28913";
    Host other;
    REQUIRE(other.Open(Port, "CAL", "CAL's room"));
    REQUIRE(StartFromNote(std::format("-host -port {} -player BOB -rejoin", Port)));
    CHECK(!InSession());
    CHECK_EQ(DialogText(), std::string("Couldn't reopen your room."));
    CHECK(OkEndsTheGame());
}

/// <summary>A rejoin finding no room at the address says the room has closed (its OK ends the game).</summary>
TEST_CASE_ISOLATED("game: a rejoin that finds no room says it has closed")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    CHECK_EQ(RejoinFails("28891"), std::string("Couldn't join the room again: it has closed."));
}

/// <summary>A rejoin finding the room's match started says so.</summary>
TEST_CASE_ISOLATED("game: a rejoin to a room whose match started says so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Host bob;
    REQUIRE(bob.Open("28893", std::string(HostName), std::string(RoomName), DPSESSION_NEWPLAYERSDISABLED));
    CHECK_EQ(RejoinFails("28893"), std::string("Couldn't join the room again: its match has started."));
}

/// <summary>A rejoin finding the room full says so.</summary>
TEST_CASE_ISOLATED("game: a rejoin to a full room says so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Host bob;
    REQUIRE(bob.Open("28895", std::string(HostName), std::string(RoomName), 0, 1));
    CHECK_EQ(RejoinFails("28895"), std::string("Couldn't join the room again: it is full."));
}

namespace
{
    /// <summary>What a game refused at the door for the time of day and the weather says.</summary>
    constexpr std::string_view RefusedForTime =
        "YOUR GAME DOESN'T HAVE THIS MATCH'S TIME OF DAY. UPDATE YOUR GAME TO JOIN.";
    constexpr std::string_view RefusedForWeather =
        "YOUR GAME DOESN'T HAVE THIS MATCH'S WEATHER. UPDATE YOUR GAME TO JOIN.";
}

/// <summary>
/// A lobby's guest whose host refuses it at the door (its join saying this build's rules) says what its game lacks, as
/// the host said; its OK ends the game, back to the lobby. It never joined.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest refused at the door says what its game lacks, and its OK ends the game")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Host bob;
    bob.RefuseWith("time of day");
    REQUIRE(bob.Open("28927", std::string(HostName), std::string(RoomName)));
    REQUIRE(MCTestGame::StartLobby(std::format("-join 127.0.0.1:28927 -player ANN -session \"{}\"", RoomName)));
    CHECK(!InSession());
    CHECK_EQ(DialogText(), std::string(RefusedForTime));
    CHECK((bob.Cards() == std::vector<std::optional<uint32_t>>{MCMatchRule::Supported}));
    CHECK(bob.Joined().empty());
    CHECK_EQ(MultiPlayer()->SessionManager->RefusalReason(), std::string("time of day"));

    // A later join that fails otherwise (its room gone from the list) says nothing of that refusal.
    CHECK(MultiPlayer()->SessionManager->JoinSession(_GUID{}, "ANN") != 0);
    CHECK(MultiPlayer()->SessionManager->RefusalReason().empty());
    CHECK(OkEndsTheGame());
}

/// <summary>
/// A lobby's guest waiting for its host to come back from an update, whose new version refuses it at the door, says why
/// at once: waiting out the half minute changes nothing.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest waiting for its host's new version, refused at the door, says why at once")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28929";
    auto bob = std::make_unique<Host>();
    REQUIRE(bob->Open(Port, std::string(HostName), std::string(RoomName)));
    REQUIRE(MCTestGame::StartLobby(std::format("-join 127.0.0.1:{} -player ANN -session \"{}\"", Port, RoomName)));
    REQUIRE(SettleUntil([&] { return !bob->Joined().empty(); }));
    bob->Lock();
    MultiPlayer()->ServerID = bob->Id();
    bob->SendUpdating(MultiPlayer()->SessionManager->MyPlayerID);
    REQUIRE(SettleUntil([] { return !InSession(); }));
    CHECK_EQ(DialogText(), std::string("BOB IS UPDATING."));
    bob->Close();
    bob.reset();

    Host newBob;
    newBob.RefuseWith("WEATHER");
    REQUIRE(newBob.Open(Port, std::string(HostName), std::string(RoomName)));
    const auto opened = std::chrono::steady_clock::now();
    REQUIRE(SettleUntil([] { return DialogText() == RefusedForWeather; }));
    CHECK(std::chrono::steady_clock::now() - opened < MCLobbyUpdate::RejoinWait);
    CHECK(!InSession());
    CHECK(newBob.Joined().empty());
    CHECK_EQ(newBob.Cards().size(), size_t{1});
    CHECK(OkEndsTheGame());
}
