#include "stdafx.h"
#include "network/MCLobbyUpdate.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "network/MCRejoinNote.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCFileSystem.h"

namespace
{
    using Clock = std::chrono::steady_clock;
    using MCLobbyUpdate::SendTime;
    using MCLobbyUpdate::SettleTime;
    using MCLobbyUpdate::TryEvery;

    /// <summary>The logistics state of the room's waiting screen (the connect screen), as a lobby's start shows it.</summary>
    constexpr int32_t WaitingState = 0xe;
    /// <summary>The original's "Your connection to the game was unsuccesful..." (a lobby's join that failed).</summary>
    constexpr uint32_t JoinFailedString = 0x370;
    constexpr int32_t StringBuffer = 0xfe;

    /// <summary>What a rejoin that failed says: no room at the address, a full one, one whose match started.</summary>
    constexpr std::string_view RoomGoneText = "Couldn't join the room again: it has closed.";
    constexpr std::string_view RoomFullText = "Couldn't join the room again: it is full.";
    constexpr std::string_view RoomStartedText = "Couldn't join the room again: its match has started.";
    /// <summary>What a host's rejoin that failed says: it couldn't host the room on its port again.</summary>
    constexpr std::string_view RoomNotReopenedText = "Couldn't open the room again.";

    /// <summary>The UPDATE page's line for an update ready in a room (MCLobbyUpdate::ReadyText).</summary>
    constexpr std::string_view AfterMatchText = "DOWNLOADED: IT INSTALLS AFTER THE MATCH.";
    constexpr std::string_view GuestRestartText = "DOWNLOADED: ON RESTART, YOU REJOIN THIS ROOM.";
    constexpr std::string_view HostAloneRestartText = "DOWNLOADED: ON RESTART, THE ROOM OPENS AGAIN.";
    constexpr std::string_view HostRestartText = "DOWNLOADED: ON RESTART, EVERYONE REJOINS.";

    /// <summary>
    /// The session screen's line while the room's start waits for guests away updating (or, after the host's restart
    /// was called off, waiting for it).
    /// </summary>
    constexpr std::string_view StartWaitsFormat = "WAITING FOR {} TO REJOIN.";
    /// <summary>What the room's other games say of player <paramref name="name"/> whose restart into an update is off.</summary>
    constexpr std::string_view UpdateOffFormat = "{}'S UPDATE IS OFF.";
    /// <summary>What the room's games say of player <paramref name="name"/> whose minute to rejoin is over.</summary>
    constexpr std::string_view NotBackFormat = "{} DIDN'T REJOIN IN TIME.";

    /// <summary>A guest restarting into an update, whose place the room keeps until it is back or the wait is over.</summary>
    struct MCComing
    {
        uint32_t Id = 0;
        std::string Name;
        Clock::time_point Until;
    };

    /// <summary>A guest whose place the room has kept, by its id and its name (it comes back with another id).</summary>
    struct MCKept
    {
        uint32_t Id = 0;
        std::string Name;
    };

    /// <summary>A guest's wait for its host, restarting into an update.</summary>
    struct MCHostWait
    {
        Clock::time_point Until;
        Clock::time_point NextTry;
    };

    /// <summary>RESTART was taken: when the update installs; this game then quits.</summary>
    std::optional<Clock::time_point> RestartAt;
    bool Restarting = false;
    /// <summary>The players in the room at RESTART, the host's guests: their places are kept if the restart is called off.</summary>
    std::vector<MCComing> RoomAtRestart;
    /// <summary>The rejoin note couldn't be written: RESTART waits for the match's end from then on.</summary>
    bool NoteUnwritten = false;
    /// <summary>The guests restarting into an update, by the order they said so.</summary>
    std::vector<MCComing> Coming;
    /// <summary>
    /// The guests whose places the room has kept: once each a room, so no game holds the room's start again and again
    /// with its Updating (one after its wait was over, or after it came back, is ignored).
    /// </summary>
    std::vector<MCKept> KeptOnce;
    /// <summary>
    /// Whether this guest's room has kept its place once already: it said it restarts and called it off, or it is back
    /// from its update (unset: as the game started, with -rejoin or not). Its next update then installs after the match,
    /// as the room would keep no place for it. A host restarting makes a new room; one whose restart is called off keeps
    /// its room, which remembers.
    /// </summary>
    std::optional<bool> OwnPlaceKept;
    /// <summary>The host shows its room again once a guest back from its update has settled.</summary>
    std::optional<Clock::time_point> RefreshAt;
    std::optional<MCHostWait> HostWait;
    /// <summary>
    /// The room (its session's instance) this guest left to wait for its restarting host: back in it, the host's restart
    /// was called off and the room's places kept stand; another is the host's new version's, which has kept none.
    /// </summary>
    std::optional<_GUID> RoomLeft;
    /// <summary>The tests' clock, when one is set (MCLobbyUpdate::SetClock).</summary>
    MCLobbyUpdate::MCClock TestClock;

    /// <summary>Now, for the room's waits.</summary>
    Clock::time_point Now()
    {
        return TestClock ? TestClock() : Clock::now();
    }

    /// <summary><paramref name="text"/> in capitals, as the menus write names.</summary>
    std::string Upper(std::string_view text)
    {
        std::string upper(text);
        std::ranges::transform(upper, upper.begin(),
                               [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
        return upper;
    }

    /// <summary>What the room's other games say of player <paramref name="name"/> restarting into an update.</summary>
    std::string UpdatingText(std::string_view name)
    {
        return std::format("{} IS UPDATING.", Upper(name));
    }

    /// <summary>
    /// The places kept changed: the session screen holds its start while any is kept, and lets it go after
    /// (MCSessionScreen::CheckGoodToGo).
    /// </summary>
    void PlacesChanged()
    {
        if (const std::shared_ptr<MCLogistics> logistics = GlobalLog();
            logistics != nullptr && logistics->SessionScreen != nullptr &&
            logistics->SessionScreen->StartButton != nullptr)
        {
            logistics->SessionScreen->CheckGoodToGo();
        }
    }

    /// <summary>
    /// The match starts while places are kept (a configured match, or a start that crossed a guest's Updating): they go,
    /// and the room takes no new players again.
    /// </summary>
    void DropPlaces()
    {
        SDL_Log("Lobby updates: the match starts: the room no longer keeps %zu updating players' places",
                Coming.size());
        Coming.clear();

        if (MultiPlayer()->IsHost)
        {
            MultiPlayer()->SessionManager->AdmitNewPlayers(false);
        }

        PlacesChanged();
    }

    /// <summary>The players in the room other than this game's.</summary>
    std::vector<std::shared_ptr<MCFidpPlayer>> Others()
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        std::vector<std::shared_ptr<MCFidpPlayer>> others;
        std::ranges::copy_if(manager.GetPlayers(nullptr), std::back_inserter(others),
                             [&](const std::shared_ptr<MCFidpPlayer>& player)
                             { return player != nullptr && player->Id != manager.MyPlayerID; });
        return others;
    }

    /// <summary>
    /// Whether <paramref name="player"/>'s build can rejoin around a restart into an update: it keeps a restarting
    /// guest's place, and waits for its restarting host.
    /// </summary>
    bool Rejoins(const MCFidpPlayer& player)
    {
        const auto said = MultiPlayer()->PlayerRules.find(player.Id);
        return said != MultiPlayer()->PlayerRules.end() && (said->second & MCMatchRule::RejoinsHost) != 0;
    }

    /// <summary>Whether this game is a guest whose room has kept its place once already (OwnPlaceKept).</summary>
    bool OwnPlaceUsed()
    {
        return !MultiPlayer()->IsHost && OwnPlaceKept.value_or(LobbyLaunchArgs.Rejoin);
    }

    /// <summary>Whether the room has kept a place for <paramref name="player"/> before, by its id or its name.</summary>
    bool KeptBefore(const MCFidpPlayer& player)
    {
        return std::ranges::any_of(KeptOnce, [&](const MCKept& kept)
                                   { return kept.Id == player.Id || kept.Name == player.Name; });
    }

    /// <summary>Sends this game's <paramref name="type"/> (Updating or RestartOff, a header alone) to every player.</summary>
    void SendToRoom(MCMPMessageType type)
    {
        MCMPLongMessage message{};
        message.Header = PlainHeader(type);
        MultiPlayer()->SessionManager->SendPlainMessage(0, LinkupBytes(message));
    }

    /// <summary>The name this game takes in the room (as SetupLobbyConnection takes it).</summary>
    std::string PlayerName()
    {
        return !LobbyLaunchArgs.PlayerName.empty() ? LobbyLaunchArgs.PlayerName : MyGetUserName().value_or("Player");
    }

    /// <summary>The address of the room (as SetupLobbyConnection joins it).</summary>
    std::string RoomAddress()
    {
        constexpr std::string_view OwnMachine = "127.0.0.1";
        return !LobbyLaunchArgs.Address.empty() ? LobbyLaunchArgs.Address : std::string(OwnMachine);
    }

    /// <summary>The room's waiting screen, as a lobby's start shows it (its button ends the game).</summary>
    void ShowWaitingScreen()
    {
        MCLogistics& logistics = *GlobalLog();
        logistics.CurrentScreen()->ShowGuiWindow(false);
        logistics.SetCurrentScreen(logistics.ConnectScreen);
        logistics.LogisticsState = WaitingState;
        logistics.ShowLogScreen(true, true);
    }

    /// <summary>
    /// The host (<paramref name="hostId"/>) restarts into an update: this guest leaves the room at once, says so, and
    /// joins the same address again (Frame) for up to a minute.
    /// </summary>
    void WaitForHost(uint32_t hostId)
    {
        MCMultiPlayer& game = *MultiPlayer();
        const std::shared_ptr<MCFidpPlayer> host = game.SessionManager->GetPlayer(hostId);
        const std::string name = host != nullptr ? host->Name : std::string("THE HOST");
        SDL_Log("Lobby updates: the host %s restarts into an update; joining %s again", name.c_str(),
                RoomAddress().c_str());
        // Known: the room is this game's (Updating takes only a room's).
        RoomLeft = game.SessionManager->CurrentSession()->SessionDesc.guidInstance;
        GlobalLog()->DestroyMultiplayer();
        game.LeaveSession();
        game.PlayerRules.clear();
        // The places this guest saw kept: the room's guests back (or not) while it is away go unseen. Which places the
        // room has kept stands until this guest knows whether it comes back to the same room (TryHostAgain).
        Coming.clear();
        RefreshAt.reset();
        game.SessionManager->ConnectTcp(RoomAddress());
        ShowWaitingScreen();
        ShowWaitingDialog(UpdatingText(name));
        const Clock::time_point now = Now();
        HostWait = MCHostWait{.Until = now + MCLobbyUpdate::RejoinWait, .NextTry = now};
    }

    /// <summary>Says a join failed with <paramref name="text"/>; its OK ends the game, back to the lobby.</summary>
    void ShowJoinFailed(std::string_view text)
    {
        ShowOkDialog(*GlobalLog()->MessageDialog, text, CancelToMPlayer);
    }

    /// <summary>The guest's next try at joining its host's room again, or the end of its wait.</summary>
    void TryHostAgain(Clock::time_point now)
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        const MCLobbyJoin tried = manager.TryLobbyJoin(PlayerName());

        if (tried == MCLobbyJoin::Joined)
        {
            // The host's new version opens a new room, which has kept no one's place; back in the same room (the host's
            // restart called off), its host remembers whose it has kept, this guest's too.
            const std::shared_ptr<MCFidpSession> room = manager.CurrentSession();
            const bool sameRoom =
                room != nullptr && RoomLeft.has_value() && MCSameGuid(room->SessionDesc.guidInstance, *RoomLeft);
            SDL_Log("Lobby updates: back in the room (%s)", sameRoom ? "the same one" : "the host's new one");

            if (!sameRoom)
            {
                KeptOnce.clear();
                OwnPlaceKept = false;
            }

            RoomLeft.reset();
            HostWait.reset();
            MultiPlayer()->StartLobbyGame();
            DestroyConnectStatusWindow();
            return;
        }

        // A room that refused the join (the host's old one, closed, still answering) is asked again afresh.
        if (tried == MCLobbyJoin::Refused)
        {
            manager.ConnectTcp(RoomAddress());
        }

        if (now < HostWait->Until)
        {
            HostWait->NextTry = now + TryEvery;
            return;
        }

        const MCRoomRefusal refusal = manager.LobbyRoomRefusal();
        SDL_Log("Lobby updates: the host's room didn't come back");
        HostWait.reset();
        ShowJoinFailed(MCLobbyUpdate::RejoinFailedText(refusal));
    }

    /// <summary>
    /// The places kept for guests away updating: one past its wait goes, the room closing again once none is kept. The
    /// guest is gone from the room: its name and its place on a team go too, so the room's start waits no more. A host
    /// left alone ends the game as the original's leaving does.
    /// </summary>
    void KeepPlaces(Clock::time_point now)
    {
        MCMultiPlayer& game = *MultiPlayer();

        for (auto coming = Coming.begin(); coming != Coming.end();)
        {
            if (now < coming->Until)
            {
                ++coming;
                continue;
            }

            // A guest still here never left (its host's restart was called off before it went).
            const bool stayed = game.SessionManager->GetPlayer(coming->Id) != nullptr;
            SDL_Log("Lobby updates: %s %s", coming->Name.c_str(), stayed ? "stayed" : "didn't rejoin in time");
            const MCComing gone = *coming;
            coming = Coming.erase(coming);

            if (game.IsHost && Coming.empty())
            {
                game.SessionManager->AdmitNewPlayers(false);
            }

            if (!stayed && GlobalLog()->SessionScreen != nullptr && GlobalLog()->SessionScreen->StartButton != nullptr)
            {
                GlobalLog()->SessionScreen->RemovePlayer(gone.Id);
            }

            PlacesChanged();

            if (stayed)
            {
                continue;
            }

            if (game.IsHost && Others().empty() && GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen)
            {
                ShowPlayerLeft(gone.Name);
            }
            else if (const std::shared_ptr<MCLogChatWindow> chat = GlobalLog()->ChatWindow; chat != nullptr)
            {
                chat->AddLine(std::format(NotBackFormat, Upper(gone.Name)));
            }
        }
    }

    /// <summary>
    /// RESTART is off (<paramref name="why"/>): the update failed, or the room can't rejoin now. The note goes, the room
    /// is told so that it drops the place it keeps for this game, and a host keeps its guests' places: they left to wait
    /// for it, and join the same address again.
    /// </summary>
    void CallOff(std::string_view why)
    {
        SDL_Log("Lobby updates: the restart is off: %.*s", static_cast<int>(why.size()), why.data());
        RestartAt.reset();
        Restarting = false;
        MCRejoinNote::Withdraw(MCFileSystem::UserRoot());

        if (!MCLobbyUpdate::InRoom())
        {
            RoomAtRestart.clear();
            return;
        }

        SendToRoom(MCMPMessageType::RestartOff);

        // The room kept this guest's place once: it keeps none again.
        if (!MultiPlayer()->IsHost)
        {
            OwnPlaceKept = true;
        }

        if (MultiPlayer()->IsHost && !RoomAtRestart.empty())
        {
            const Clock::time_point until = Now() + MCLobbyUpdate::RejoinWait;

            for (MCComing& guest : RoomAtRestart)
            {
                guest.Until = until;
                Coming.push_back(std::move(guest));
            }

            MultiPlayer()->SessionManager->AdmitNewPlayers(true);
            PlacesChanged();
        }

        RoomAtRestart.clear();
    }
}

auto MCLobbyUpdate::InRoom() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return LobbyLaunchArgs.Active && game != nullptr && game->SessionManager->CurrentSession() != nullptr;
}

auto MCLobbyUpdate::MatchUnderWay() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    return game != nullptr &&
           (game->InLogistics || game->InMission || (logistics != nullptr && logistics->MultiplayerInitialized));
}

auto MCLobbyUpdate::ReadyText() -> std::string
{
    if (!InRoom())
    {
        return {};
    }

    if (!CanRestart())
    {
        return std::string(AfterMatchText);
    }

    if (!MultiPlayer()->IsHost)
    {
        return std::string(GuestRestartText);
    }

    return std::string(Others().empty() ? HostAloneRestartText : HostRestartText);
}

auto MCLobbyUpdate::CanRestart() -> bool
{
    // A guest away updating can't be told: the room restarts only once it is back. The new version comes back only
    // with the note, which needs the build it starts as. A room keeps a guest's place once.
    return InRoom() && !MatchUnderWay() && Coming.empty() && !NoteUnwritten && !OwnPlaceUsed() &&
           MCUpdater::ExpectedBuild().has_value() &&
           std::ranges::all_of(Others(), [](const auto& player) { return Rejoins(*player); });
}

auto MCLobbyUpdate::StartWaitsText() -> std::string
{
    if (Coming.empty())
    {
        return {};
    }

    return std::format(StartWaitsFormat, MCNamedPlayers(
                                             Coming, [](const MCComing&) { return true; },
                                             [](const MCComing& coming) { return Upper(coming.Name); }));
}

auto MCLobbyUpdate::Restart() -> bool
{
    if (!InRoom())
    {
        return false;
    }

    if (RestartAt.has_value() || Restarting)
    {
        return true;
    }

    if (!CanRestart())
    {
        SDL_Log("Lobby updates: RESTART waits: the update installs as the game exits after the match");
        return true;
    }

    // Known: CanRestart.
    const uint64_t build = *MCUpdater::ExpectedBuild();
    const std::string commandLine = MCRejoinNote::LobbyCommandLine(LobbyLaunchArgs);

    if (const auto written = MCRejoinNote::Write(
            MCFileSystem::UserRoot(), {.CommandLine = commandLine, .Build = build, .Time = MCRejoinNote::Now()});
        !written)
    {
        SDL_Log(
            "Lobby updates: RESTART waits: no rejoin note (%s): the update installs as the game exits after the match",
            written.error().c_str());
        NoteUnwritten = true;
        return true;
    }

    // Every game in the room: the host's guests wait for it, a guest's place is kept.
    SendToRoom(MCMPMessageType::Updating);
    RoomAtRestart.clear();

    if (MultiPlayer()->IsHost)
    {
        for (const std::shared_ptr<MCFidpPlayer>& guest : Others())
        {
            RoomAtRestart.push_back({.Id = guest->Id, .Name = guest->Name});
        }
    }

    SDL_Log("Lobby updates: restarting into the update, to start again as %s", commandLine.c_str());
    RestartAt = Now() + SendTime;
    return true;
}

auto MCLobbyUpdate::Frame() -> void
{
    const Clock::time_point now = Now();

    if (RestartAt.has_value() && now >= *RestartAt)
    {
        // The room may have changed meanwhile: its match started (a guest's RESTART crossing the host's START).
        if (!CanRestart())
        {
            CallOff("the room can't rejoin now");
        }
        else
        {
            RestartAt.reset();
            MCUpdater::Answer(MCUpdater::MCAnswer::Install);
            Restarting = true;
        }
    }

    // An update no longer installing (it failed, meanwhile or since) leaves the room as it was.
    if (Restarting && MCUpdater::Status().Stage != MCUpdater::MCStage::Installing)
    {
        CallOff(MCUpdater::Status().Stage == MCUpdater::MCStage::Failed ? MCUpdater::Status().Error
                                                                        : std::string("the update isn't ready"));
    }

    if (MultiPlayer() == nullptr || GlobalLog() == nullptr)
    {
        return;
    }

    if (HostWait.has_value() && now >= HostWait->NextTry)
    {
        TryHostAgain(now);
    }

    if (!Coming.empty() && MatchUnderWay())
    {
        DropPlaces();
    }

    KeepPlaces(now);

    // A guest back from its update: the host's room shows it to everyone (and it to them) as the host's GO did.
    if (RefreshAt.has_value() && now >= *RefreshAt)
    {
        RefreshAt.reset();

        if (MultiPlayer()->IsHost && GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen &&
            MultiPlayer()->SessionManager->GetPlayers(nullptr).size() > 1)
        {
            GlobalLog()->SetUpSessionScreen();
        }
    }
}

auto MCLobbyUpdate::Updating(uint32_t fromId) -> void
{
    if (!InRoom() || GlobalLog() == nullptr || MatchUnderWay() || Restarting || RestartAt.has_value())
    {
        return;
    }

    MCMultiPlayer& game = *MultiPlayer();

    if (!game.IsHost && (fromId == game.ServerID || fromId == game.HostID))
    {
        WaitForHost(fromId);
        return;
    }

    const std::shared_ptr<MCFidpPlayer> player = game.SessionManager->GetPlayer(fromId);

    if (player == nullptr || std::ranges::contains(Coming, fromId, &MCComing::Id))
    {
        return;
    }

    // A kept place holds the room's start and opens the room: only for a build that comes back (its rules said so), and
    // once a room, so no guest holds the start again and again.
    if (!Rejoins(*player))
    {
        SDL_Log("Lobby updates: %s's Updating ignored: its game isn't known to rejoin", player->Name.c_str());
        return;
    }

    if (KeptBefore(*player))
    {
        SDL_Log("Lobby updates: %s's Updating ignored: the room has kept its place once already", player->Name.c_str());
        return;
    }

    SDL_Log("Lobby updates: %s restarts into an update; the room keeps its place", player->Name.c_str());
    KeptOnce.push_back({.Id = fromId, .Name = player->Name});
    Coming.push_back({.Id = fromId, .Name = player->Name, .Until = Now() + RejoinWait});

    if (game.IsHost)
    {
        game.SessionManager->AdmitNewPlayers(true);
    }

    PlacesChanged();

    if (const std::shared_ptr<MCLogChatWindow> chat = GlobalLog()->ChatWindow; chat != nullptr)
    {
        chat->AddLine(UpdatingText(player->Name));
    }
}

auto MCLobbyUpdate::RestartOff(uint32_t fromId) -> void
{
    // Only the sender's own place, by its id (a session never numbers two players alike).
    const auto coming = std::ranges::find(Coming, fromId, &MCComing::Id);

    if (!InRoom() || GlobalLog() == nullptr || coming == Coming.end())
    {
        return;
    }

    SDL_Log("Lobby updates: %s's restart is off; it stays in the room", coming->Name.c_str());
    const std::string name = coming->Name;
    Coming.erase(coming);

    if (MultiPlayer()->IsHost && Coming.empty())
    {
        MultiPlayer()->SessionManager->AdmitNewPlayers(false);
    }

    PlacesChanged();

    if (const std::shared_ptr<MCLogChatWindow> chat = GlobalLog()->ChatWindow; chat != nullptr)
    {
        chat->AddLine(std::format(UpdateOffFormat, Upper(name)));
    }
}

auto MCLobbyUpdate::SetClock(MCClock clock) -> void
{
    TestClock = std::move(clock);
}

auto MCLobbyUpdate::LeftToUpdate(uint32_t playerId) -> bool
{
    return Restarting || RestartAt.has_value() || std::ranges::contains(Coming, playerId, &MCComing::Id);
}

auto MCLobbyUpdate::PlayerJoined(uint32_t playerId) -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCFidpPlayer> player = game != nullptr ? game->SessionManager->GetPlayer(playerId) : nullptr;

    if (player == nullptr)
    {
        return;
    }

    const auto coming = std::ranges::find(Coming, player->Name, &MCComing::Name);

    if (coming == Coming.end())
    {
        return;
    }

    SDL_Log("Lobby updates: %s is back from its update", player->Name.c_str());
    Coming.erase(coming);

    if (game->IsHost)
    {
        if (Coming.empty())
        {
            game->SessionManager->AdmitNewPlayers(false);
        }

        RefreshAt = Now() + SettleTime;
    }

    PlacesChanged();
}

auto MCLobbyUpdate::RejoinFailedText(MCRoomRefusal refusal) -> std::string
{
    switch (refusal)
    {
        case MCRoomRefusal::Full:
            return std::string(RoomFullText);
        case MCRoomRefusal::Started:
            return std::string(RoomStartedText);
        case MCRoomRefusal::Gone:
            break;
    }

    return std::string(RoomGoneText);
}

auto MCLobbyUpdate::JoinFailed() -> void
{
    if (!LobbyLaunchArgs.Rejoin)
    {
        ShowJoinFailed(LoadGameString(JoinFailedString, StringBuffer));
    }
    else if (LobbyLaunchArgs.IsHost)
    {
        ShowJoinFailed(RoomNotReopenedText);
    }
    else
    {
        ShowJoinFailed(RejoinFailedText(MultiPlayer()->SessionManager->LastRoomRefusal));
    }
}
