#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
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
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRejoinNote.h"
#include "platform/MCDirectPlay.h"

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
    MCLobbyLaunchArgs host{.Active = true, .IsHost = true, .Address = ":28800", .PlayerName = "Big Bob"};
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(host),
             std::string("-host -port 28800 -player \"Big Bob\" -maxplayers 6 -rejoin"));

    MCLobbyLaunchArgs guest{.Active = true,
                            .IsHost = false,
                            .Address = "192.168.1.5:28801",
                            .PlayerName = "Ann \"Ace\" Lee",
                            .SessionName = "Ann's room",
                            .MaxPlayers = 4};
    const std::string line = MCRejoinNote::LobbyCommandLine(guest);
    CHECK_EQ(
        line,
        std::string("-join 192.168.1.5:28801 -player \"Ann Ace Lee\" -session \"Ann's room\" -maxplayers 4 -rejoin"));
    CHECK(StartsInLobby(line));

    // A join with no address (the game's own default), and a host on the default port.
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(MCLobbyLaunchArgs{.Active = true}),
             std::string("-join -maxplayers 6 -rejoin"));
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(MCLobbyLaunchArgs{.Active = true, .IsHost = true}),
             std::string("-host -maxplayers 6 -rejoin"));
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
    using MCUpdater::MCAnswer;

    /// <summary>What the stand-in for Sparkle was asked.</summary>
    struct MCBackendLog
    {
        std::vector<MCAnswer> Answers;
    };

    /// <summary>A stand-in for Sparkle: it keeps the answers; the test reports the steps.</summary>
    class MCFakeBackend final : public MCUpdater::MCBackend
    {
    public:
        explicit MCFakeBackend(std::shared_ptr<MCBackendLog> log) : _Log(std::move(log)) {}

        std::string RunningVersion() override { return "0.2.1 (468)"; }

        void Check(std::optional<uint64_t>) override {}

        void Answer(MCAnswer answer) override { _Log->Answers.push_back(answer); }

    private:
        std::shared_ptr<MCBackendLog> _Log;
    };

    /// <summary>Sets a stand-in backend for the test's length, then takes it away.</summary>
    class MCFakeUpdater
    {
    public:
        MCFakeUpdater() { MCUpdater::SetBackend(std::make_unique<MCFakeBackend>(Log)); }
        ~MCFakeUpdater() { MCUpdater::SetBackend(nullptr); }
        MCFakeUpdater(const MCFakeUpdater&) = delete;
        MCFakeUpdater& operator=(const MCFakeUpdater&) = delete;

        std::shared_ptr<MCBackendLog> Log = std::make_shared<MCBackendLog>();
    };

    /// <summary>The update the tests find, as the feed shows it.</summary>
    constexpr std::string_view NewVersion = "0.2.2 (480)";
    constexpr std::string_view NewNotes = "Rooms keep their players through an update.";

    /// <summary>Runs frames for two seconds of game time, giving the network a moment each.</summary>
    void Settle()
    {
        constexpr int32_t Frames = 30;
        constexpr uint32_t NetworkMs = 3;

        for (int32_t frame = 0; frame < Frames; frame++)
        {
            SDL_Delay(NetworkMs);
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>Runs <see cref="Settle"/> until <paramref name="done"/> says so, at most a few times; whether it did.</summary>
    bool SettleUntil(const std::function<bool()>& done)
    {
        constexpr int32_t MostSettles = 10;

        for (int32_t settle = 0; settle < MostSettles && !done(); settle++)
        {
            Settle();
        }

        return done();
    }

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

    /// <summary>
    /// Boots the game as a lobby's host on <paramref name="port"/>, has <paramref name="guests"/> join it, and opens the
    /// session screen (the host's GO, which closes the room).
    /// </summary>
    bool HostLobbyRoom(std::string_view port, std::initializer_list<std::reference_wrapper<Guest>> guests)
    {
        if (!MCTestGame::StartLobby(std::format("-host -port {} -player BOB", port)) || MultiPlayer() == nullptr ||
            MultiPlayer()->SessionManager->DirectPlay == nullptr)
        {
            return false;
        }

        for (Guest& guest : guests)
        {
            if (!guest.Join(*MultiPlayer()->SessionManager->DirectPlay, std::format("127.0.0.1:{}", port)))
            {
                return false;
            }
        }

        Settle();
        Go();
        Settle();
        return GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen;
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

    /// <summary>The update found, downloaded and ready: RESTART installs it.</summary>
    void UpdateReady()
    {
        MCUpdater::Found(std::string(NewVersion), std::string(NewNotes));
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
/// then the update installs; the guests leaving as it goes don't stop it with "has left the game".
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's host restarting into an update tells the guests that wait for it, and leaves the "
                   "rejoin note")
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
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED: ON RESTART, 1 OF 2 PLAYERS JOINS AGAIN."));
    MCScreenInput::SaveShot("lobby host 0 ready", MCScreenInput::ScreenHash());

    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(screen, "RESTART");
    REQUIRE(restart != nullptr);
    ClickOn(*restart);
    CHECK(SettleUntil([&] { return ann.Updatings() == 1; }));
    CHECK_EQ(cal.Updatings(), 0);
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
    CHECK(updater.Log->Answers.back() == MCAnswer::Install);

    const std::optional<std::string> note =
        MCRejoinNote::TakeAtStart(MCTestGame::UserFolder(), NewBuild, MCRejoinNote::Now());
    REQUIRE(note.has_value());
    CHECK_EQ(*note, std::string("-host -port 28885 -player BOB -maxplayers 6 -rejoin"));

    ann.Leave();
    cal.Leave();
    Settle();
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
}

/// <summary>
/// A lobby's guest restarting into an update: its Updating message has the host keep its place, with no "has left the
/// game" (which would end a lobby's game), the room open to it until it's back; once it has joined again the host shows
/// everyone the room again, the guest in it, and closes the room.
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
    CHECK(!RoomOpen());
    CHECK(Shows(screen, ann.Id()));

    ann.SendUpdating(hostId);
    CHECK(SettleUntil(RoomOpen));
    ann.Leave();
    Settle();
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    MCScreenInput::SaveShot("lobby guest 0 updating", MCScreenInput::ScreenHash());

    Guest back("ANN");
    REQUIRE(back.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28887"));
    CHECK(SettleUntil([&] { return back.SessionScreens() > 0; }));
    CHECK(Shows(screen, back.Id()));
    CHECK(!Shows(screen, ann.Id()));
    CHECK(!RoomOpen());
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    MCScreenInput::SaveShot("lobby guest 1 back", MCScreenInput::ScreenHash());
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

    /// <summary>Boots the game to rejoin a room at <paramref name="port"/>, which fails: what it says.</summary>
    std::string RejoinFails(std::string_view port)
    {
        if (!StartFromNote(
                std::format("-join 127.0.0.1:{} -player ANN -session \"{}\" -maxplayers 6 -rejoin", port, RoomName)))
        {
            return {};
        }

        CHECK(!InSession());
        return DialogText();
    }
}

/// <summary>
/// A lobby's guest started again from the rejoin note joins the room as it says. When its host restarts into an
/// update, its game leaves the room at once, says "BOB IS UPDATING." and joins the same address again: the old room
/// (closed, still answering) doesn't take it, the new one on the same port does, and it waits there for the host's GO.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's guest started from the rejoin note joins, and waits for its host restarting into "
                   "an update")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28889";
    auto bob = std::make_unique<Host>();
    REQUIRE(bob->Open(Port, std::string(HostName), std::string(RoomName)));
    REQUIRE(StartFromNote(
        std::format("-join 127.0.0.1:{} -player ANN -session \"{}\" -maxplayers 6 -rejoin", Port, RoomName)));
    REQUIRE(InSession());
    REQUIRE(SettleUntil([&] { return !bob->Joined().empty(); }));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->ConnectScreen);

    // The host's GO closed the room. The room's numbering tells a guest which player is its host (the test's host
    // numbers no one).
    bob->Lock();
    MultiPlayer()->ServerID = bob->Id();
    bob->SendUpdating(MultiPlayer()->SessionManager->MyPlayerID);
    CHECK(SettleUntil([] { return !InSession(); }));
    CHECK_EQ(DialogText(), std::string("BOB IS UPDATING."));
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->ConnectScreen);
    MCScreenInput::SaveShot("lobby guest wait 0 host updating", MCScreenInput::ScreenHash());

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
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->ConnectScreen);
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

    // The host's GO takes its guests to the session screen.
    GlobalLog()->SetUpSessionScreen();
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    UpdateReady();
    ShowUpdatePageOver(screen);
    Settle();
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADED: ON RESTART, YOU JOIN THIS ROOM AGAIN."));

    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(screen, "RESTART");
    REQUIRE(restart != nullptr);
    ClickOn(*restart);
    CHECK(SettleUntil([&] { return bob.Updatings() == 1; }));
    REQUIRE(SettleUntil([&] { return updater.Log->Answers.size() == 2; }));
    CHECK(updater.Log->Answers.back() == MCAnswer::Install);

    const std::optional<std::string> note =
        MCRejoinNote::TakeAtStart(MCTestGame::UserFolder(), NewBuild, MCRejoinNote::Now());
    REQUIRE(note.has_value());
    CHECK_EQ(*note, std::format("-join 127.0.0.1:28897 -player ANN -session \"{}\" -maxplayers 6 -rejoin", RoomName));
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
