#include "stdafx.h"
#include "network/MCLobbyUpdate.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCMultiPlayer.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayerMessages.h"
#include "network/MCRejoinNote.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCFileSystem.h"

namespace
{
    using Clock = std::chrono::steady_clock;
    using MCLobbyUpdate::SendTime;
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
    constexpr std::string_view RoomNotReopenedText = "Couldn't reopen your room.";

    /// <summary>The UPDATE page's line for an update ready to join a game the INTERNET screen picked.</summary>
    constexpr std::string_view JoinAfterFormat = "DOWNLOADED. RESTART TO INSTALL IT AND JOIN {}.";

    /// <summary>The UPDATE page's line for an update ready in a room (MCLobbyUpdate::ReadyText).</summary>
    constexpr std::string_view AfterMatchText = "DOWNLOADED. IT WILL INSTALL AFTER THE MATCH.";
    constexpr std::string_view GuestRestartText = "DOWNLOADED. RESTART TO INSTALL IT AND REJOIN THIS ROOM.";
    constexpr std::string_view HostAloneRestartText = "DOWNLOADED. RESTART TO INSTALL IT; YOUR ROOM WILL REOPEN.";
    constexpr std::string_view HostRestartText = "DOWNLOADED. RESTART TO INSTALL IT; EVERYONE WILL REJOIN.";

    /// <summary>
    /// The session screen's line while the room waits for guests away updating (or, after the host's restart was called
    /// off, waiting for it).
    /// </summary>
    constexpr std::string_view StartWaitsFormat = "WAITING FOR {} TO REJOIN.";
    /// <summary>What START asks while several guests are away updating (one: its name, as UpdatingText says it).</summary>
    constexpr std::string_view StartWithoutThemFormat = "{} ARE UPDATING. START WITHOUT THEM?";
    /// <summary>What START asks of one guest away updating, after UpdatingText.</summary>
    constexpr std::string_view StartWithoutOneFormat = "{} START WITHOUT {}?";
    /// <summary>Port-only (Redux HD): what START asks of a rematch's guest not back yet, and of several.</summary>
    constexpr std::string_view NotBackYetFormat = "{} ISN'T BACK YET. START WITHOUT {}?";
    constexpr std::string_view NotBackYetThemFormat = "{} AREN'T BACK YET. START WITHOUT THEM?";
    /// <summary>What the room's other games say of player <paramref name="name"/> whose restart into an update is off.</summary>
    constexpr std::string_view UpdateOffFormat = "{} ISN'T UPDATING AFTER ALL.";
    /// <summary>What the room's games say of player <paramref name="name"/> whose time to rejoin is over.</summary>
    constexpr std::string_view NotBackFormat = "{} DIDN'T REJOIN IN TIME.";

    /// <summary>A guest restarting into an update, whose place the room keeps until it is back or the wait is over.</summary>
    struct MCComing
    {
        uint32_t Id = 0;
        std::string Name;
        Clock::time_point Until;
        /// <summary>Port-only (Redux HD): a rematch's guest (MCLobbyUpdate::KeepPlacesFor), not one away updating.</summary>
        bool Rematch = false;
    };

    /// <summary>A guest whose place the room has kept, by its id and its name (it comes back with another id).</summary>
    struct MCKept
    {
        uint32_t Id = 0;
        std::string Name;
    };

    /// <summary>
    /// How often a guest of an internet game tries its restarting host's code: the service lets an address try 30 codes
    /// a minute.
    /// </summary>
    constexpr std::chrono::milliseconds InternetTryEvery{2000};

    /// <summary>A guest's wait for its host, restarting into an update.</summary>
    struct MCHostWait
    {
        Clock::time_point Until;
        Clock::time_point NextTry;
        /// <summary>The name this guest joins with again.</summary>
        std::string PlayerName;
        /// <summary>An internet game's code: the host comes back to it (its place kept on the service).</summary>
        std::optional<std::string> Code;
        /// <summary>Where the room is looked for again: the code, or the host's address.</summary>
        std::string Address;
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
    std::optional<MCHostWait> HostWait;
    /// <summary>
    /// The room (its session's instance) this guest left to wait for its restarting host: back in it, the host's restart
    /// was called off and the room's places kept stand; another is the host's new version's, which has kept none.
    /// </summary>
    std::optional<_GUID> RoomLeft;
    /// <summary>
    /// A guest away updating left the session: the session screen looks again at the next frame, once the session has
    /// let it go (its leave is told before then), so it no longer counts for START.
    /// </summary>
    bool AwayLeft = false;
    /// <summary>A game the INTERNET screen picked that needs the update: the new version joins it.</summary>
    std::optional<MCLobbyUpdate::MCJoinAfter> JoinAfter;
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
    /// The places kept changed: the session screen's line names whom the room waits for (MCSessionScreen::CheckGoodToGo).
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
    /// The room's last kept place went: the room closes again if a match closed it (the session locked at START); before
    /// START it takes players anyway.
    /// </summary>
    void CloseRoomAgain()
    {
        if (MCSessionManager& manager = *MultiPlayer()->SessionManager; manager.SessionLocked())
        {
            manager.AdmitNewPlayers(false);
        }
    }

    /// <summary>
    /// The match starts while places are kept (START's OK, a configured match, or a start that crossed a guest's
    /// Updating): they go, a guest still away leaves its team (its slots would be counted for a player
    /// who can't come back), and the room takes no new players again.
    /// </summary>
    void DropPlaces()
    {
        SDL_Log("Lobby updates: the match starts: the room no longer keeps %zu updating players' places",
                Coming.size());

        for (const MCComing& coming : Coming)
        {
            if (MCLobbyUpdate::Away(coming.Id) && GlobalLog()->SessionScreen != nullptr &&
                GlobalLog()->SessionScreen->StartButton != nullptr)
            {
                GlobalLog()->SessionScreen->RemovePlayer(coming.Id);
            }
        }

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

    /// <summary>
    /// Whether every guest's game is up to hear the room's word: on the host, each has pinged from its room's screen
    /// (MCFidpPlayer::HeardPing). A joiner's game still opening the room would miss the host's restart, and take its
    /// leave for a lost connection.
    /// </summary>
    bool GuestsUp()
    {
        return !MultiPlayer()->IsHost || std::ranges::all_of(Others(), [](const std::shared_ptr<MCFidpPlayer>& player)
                                                             { return player->HeardPing; });
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

    /// <summary>
    /// Port (Redux HD): whether this game is in an internet game's room the menus opened (the INTERNET screen's), which
    /// restarts into an update and comes back as a lobby's does.
    /// </summary>
    bool InMenusInternetRoom()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        return !LobbyLaunchArgs.Active && game != nullptr && game->SessionManager != nullptr &&
               game->SessionManager->Internet != nullptr;
    }

    /// <summary>
    /// The words a restart into an update starts with to come back to the room: a lobby's own, or an internet room's the
    /// menus opened (a host's game back in its place on the service); none when the room can't be named yet.
    /// </summary>
    std::optional<std::string> RoomCommandLine()
    {
        if (LobbyLaunchArgs.Active)
        {
            return MCRejoinNote::LobbyCommandLine(LobbyLaunchArgs);
        }

        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        const std::shared_ptr<MCFidpSession> session = manager.CurrentSession();
        const std::shared_ptr<MCFidpPlayer> self = manager.MyPlayer();

        if (manager.Internet == nullptr || session == nullptr || self == nullptr)
        {
            return std::nullopt;
        }

        MCRejoinNote::MCInternetRoom room{.Hosting = MultiPlayer()->IsHost != 0,
                                          .Code = manager.Internet->Code(),
                                          .Public = manager.Internet->Public(),
                                          .PlayerName = self->Name,
                                          .SessionName = session->Name,
                                          .MaxPlayers = static_cast<int32_t>(session->SessionDesc.dwMaxPlayers)};

        if (room.Hosting)
        {
            const std::optional<MCInternetPlace> place = manager.Internet->Place();

            if (!place.has_value())
            {
                return std::nullopt;
            }

            room.Code = place->Code;
            room.Token = place->Token;
        }

        return MCRejoinNote::InternetRoomCommandLine(room);
    }

    /// <summary>Port (Redux HD): <paramref name="words"/> for the log: a host's place's token left out.</summary>
    std::string WithoutToken(std::string_view words)
    {
        constexpr std::string_view Place = "-place ";
        std::string logged(words);
        const size_t place = logged.find(Place);
        const size_t token = place != std::string::npos ? logged.find(' ', place + Place.size()) : std::string::npos;

        if (token != std::string::npos)
        {
            const size_t end = logged.find(' ', token + 1);
            logged.replace(token + 1, (end == std::string::npos ? logged.size() : end) - token - 1, "(token)");
        }

        return logged;
    }

    /// <summary>
    /// A host's internet game keeps its place on the service through a restart (its sockets close as drops), or no
    /// longer does (the restart called off).
    /// </summary>
    void KeepInternetPlace(bool kept)
    {
        if (const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
            game != nullptr && game->IsHost && game->SessionManager->Internet != nullptr)
        {
            game->SessionManager->Internet->KeepPlace(kept);
        }
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
    /// joins the same address again (Frame) for MCLobbyUpdate::RejoinWait.
    /// </summary>
    void WaitForHost(uint32_t hostId)
    {
        MCMultiPlayer& game = *MultiPlayer();
        MCSessionManager& manager = *game.SessionManager;
        const std::shared_ptr<MCFidpPlayer> host = manager.GetPlayer(hostId);
        const std::string name = host != nullptr ? host->Name : std::string("THE HOST");
        // Port (Redux HD): an internet game's host comes back to the same code, its place kept on the service.
        std::optional<std::string> code =
            manager.Internet != nullptr ? std::optional(manager.Internet->Code()) : std::nullopt;
        const std::string address = code.value_or(RoomAddress());
        const std::shared_ptr<MCFidpPlayer> self = manager.MyPlayer();
        const std::string playerName = LobbyLaunchArgs.Active || self == nullptr ? PlayerName() : self->Name;
        SDL_Log("Lobby updates: the host %s restarts into an update; joining %s again", name.c_str(), address.c_str());
        // Known: the room is this game's (Updating takes only a room's).
        RoomLeft = manager.CurrentSession()->SessionDesc.guidInstance;
        GlobalLog()->DestroyMultiplayer();
        game.LeaveSession();
        game.PlayerRules.clear();
        // The places this guest saw kept: the room's guests back (or not) while it is away go unseen. Which places the
        // room has kept stands until this guest knows whether it comes back to the same room (TryHostAgain).
        Coming.clear();
        manager.ConnectTcp(address);

        // A lobby's game waits on the lobby's waiting screen; the menus' on the INTERNET screen it came from.
        if (LobbyLaunchArgs.Active)
        {
            ShowWaitingScreen();
        }
        else
        {
            GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
            ShowInternetScreen();
        }

        ShowWaitingDialog(UpdatingText(name));
        const Clock::time_point now = Now();
        HostWait = MCHostWait{.Until = now + MCLobbyUpdate::RejoinWait,
                              .NextTry = now,
                              .PlayerName = playerName,
                              .Code = std::move(code),
                              .Address = address};
    }

    /// <summary>
    /// Says a join failed with <paramref name="text"/>: a lobby's game's OK ends the game, back to the lobby; the menus'
    /// says it over the INTERNET screen.
    /// </summary>
    void ShowJoinFailed(std::string_view text)
    {
        if (LobbyLaunchArgs.Active)
        {
            ShowOkDialog(*GlobalLog()->MessageDialog, text, CancelToMPlayer);
            return;
        }

        GlobalLog()->MessageDialog->Deactivate(0);
        ShowMenuMessage(text);
    }

    /// <summary>
    /// Port (Redux HD): an internet guest's try at its host's code: the service's answer and the link, across frames,
    /// before the room is looked for over it. Whether the link is open; otherwise the next try is set.
    /// </summary>
    bool ReachHost(Clock::time_point now)
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;

        if (manager.Internet == nullptr && !manager.BeginInternet(false, *HostWait->Code, false))
        {
            HostWait->NextTry = now + InternetTryEvery;
            return false;
        }

        const MCInternetGame::Stage stage = manager.Internet->Update();

        if (stage == MCInternetGame::Stage::Ready)
        {
            return true;
        }

        // The host still away (the service says so), or no link: tried again afresh.
        if (stage == MCInternetGame::Stage::Failed)
        {
            manager.EndInternet();
            HostWait->NextTry = now + InternetTryEvery;
            return false;
        }

        HostWait->NextTry = now;
        return false;
    }

    /// <summary>The guest's next try at joining its host's room again, or the end of its wait.</summary>
    void TryHostAgain(Clock::time_point now)
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;

        if (HostWait->Code.has_value() && !ReachHost(now))
        {
            if (now >= HostWait->Until)
            {
                SDL_Log("Lobby updates: the host's game didn't come back");
                manager.EndInternet();
                HostWait.reset();
                ShowJoinFailed(MCLobbyUpdate::RejoinFailedText(MCRoomRefusal::Gone));
            }

            return;
        }

        const MCLobbyJoin tried = manager.TryLobbyJoin(HostWait->PlayerName);

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

            // A lobby's game is the lobby's again; port-only (Redux HD): the menus' is theirs, as their JOIN GAME leaves
            // it (the host's setup names its host and server).
            if (LobbyLaunchArgs.Active)
            {
                MultiPlayer()->StartLobbyGame();
            }
            else
            {
                NumLanPlayers = static_cast<int32_t>(manager.GetPlayers(nullptr).size());
            }

            DestroyConnectStatusWindow();
            // Straight into the room, which its host shows again with this guest in it.
            GlobalLog()->SetUpSessionScreen();
            return;
        }

        // A host's new version whose match this game can't play refused it at the door: waiting changes nothing.
        if (tried == MCLobbyJoin::Refused && manager.LastRoomRefusal == MCRoomRefusal::Lacks)
        {
            SDL_Log("Lobby updates: the host's room refused this game: it lacks %s", manager.RefusalReason().c_str());
            HostWait.reset();
            ShowJoinFailed(MCLobbyUpdate::RejoinFailedText(MCRoomRefusal::Lacks));
            return;
        }

        // A room that refused the join (the host's old one, closed, still answering) is asked again afresh.
        if (tried == MCLobbyJoin::Refused)
        {
            manager.EndInternet();
            manager.ConnectTcp(HostWait->Address);
        }

        if (now < HostWait->Until)
        {
            HostWait->NextTry = now + TryEvery;
            return;
        }

        const MCRoomRefusal refusal = manager.LobbyRoomRefusal();
        SDL_Log("Lobby updates: the host's room didn't come back");
        manager.EndInternet();
        HostWait.reset();
        ShowJoinFailed(MCLobbyUpdate::RejoinFailedText(refusal));
    }

    /// <summary>
    /// The places kept for guests away updating: one past its wait goes. The guest is gone from the room: its name and its
    /// place on a team go too, so the room's start waits no more; the room says so, and a host left alone waits for
    /// others.
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
                CloseRoomAgain();
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

            if (const std::shared_ptr<MCLogChat> chat = GlobalLog()->Chat; chat != nullptr)
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
        KeepInternetPlace(false);

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
    return (LobbyLaunchArgs.Active || InMenusInternetRoom()) && game != nullptr &&
           game->SessionManager->CurrentSession() != nullptr;
}

auto MCLobbyUpdate::MatchUnderWay() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    return game != nullptr &&
           (game->InLogistics || game->InMission || (logistics != nullptr && logistics->MultiplayerInitialized));
}

namespace
{
    /// <summary>
    /// Whether the room can restart into the update, but for a guest's game still opening it (GuestsUp): a guest away
    /// updating can't be told, so the room restarts only once it is back; the new version comes back only with the
    /// note, which needs the build it starts as; a room keeps a guest's place once.
    /// </summary>
    bool RoomRejoins()
    {
        return MCLobbyUpdate::InRoom() && !MCLobbyUpdate::MatchUnderWay() && Coming.empty() && !NoteUnwritten &&
               !OwnPlaceUsed() && MCUpdater::ExpectedBuild().has_value() &&
               std::ranges::all_of(Others(), [](const auto& player) { return Rejoins(*player); });
    }
}

auto MCLobbyUpdate::ReadyText() -> std::string
{
    if (!InRoom())
    {
        return JoinAfter.has_value() && MCUpdater::ExpectedBuild().has_value()
                   ? std::format(JoinAfterFormat, Upper(JoinAfter->Name))
                   : std::string();
    }

    // A lobby's game exits after the match, installing it; the menus' installs it as it restarts or quits. A joiner's
    // game still opening the room holds RESTART back for a moment, the text as it will be.
    if (!RoomRejoins())
    {
        return LobbyLaunchArgs.Active ? std::string(AfterMatchText) : std::string();
    }

    if (!MultiPlayer()->IsHost)
    {
        return std::string(GuestRestartText);
    }

    return std::string(Others().empty() ? HostAloneRestartText : HostRestartText);
}

auto MCLobbyUpdate::CanRestart() -> bool
{
    return RoomRejoins() && GuestsUp();
}

namespace
{
    /// <summary>The guests away updating, named as the menus write names ("ANN, CAL").</summary>
    std::string ComingNames()
    {
        return MCNamedPlayers(
            Coming, [](const MCComing&) { return true; }, [](const MCComing& coming) { return Upper(coming.Name); });
    }
}

auto MCLobbyUpdate::StartWaitsText() -> std::string
{
    return Coming.empty() ? std::string() : std::format(StartWaitsFormat, ComingNames());
}

auto MCLobbyUpdate::StartQuestion() -> std::string
{
    if (Coming.empty())
    {
        return {};
    }

    const std::string names = ComingNames();

    // Port-only (Redux HD): a rematch's guests aren't updating.
    if (Coming.front().Rematch)
    {
        return Coming.size() == 1 ? std::format(NotBackYetFormat, names, names)
                                  : std::format(NotBackYetThemFormat, names);
    }

    return Coming.size() == 1 ? std::format(StartWithoutOneFormat, UpdatingText(Coming.front().Name), names)
                              : std::format(StartWithoutThemFormat, names);
}

auto MCLobbyUpdate::Away(uint32_t playerId) -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return std::ranges::contains(Coming, playerId, &MCComing::Id) && game != nullptr &&
           game->SessionManager != nullptr && game->SessionManager->GetPlayer(playerId) == nullptr;
}

auto MCLobbyUpdate::Restart() -> bool
{
    if (!InRoom())
    {
        // Port (Redux HD): a game the INTERNET screen picked, joined by the new version (the page then installs).
        if (const std::optional<uint64_t> build = MCUpdater::ExpectedBuild(); JoinAfter.has_value() && build)
        {
            const std::string words = MCRejoinNote::InternetRoomCommandLine(
                {.Code = JoinAfter->Code, .PlayerName = JoinAfter->PlayerName, .Rejoin = false});

            if (const auto written = MCRejoinNote::Write(
                    MCFileSystem::UserRoot(), {.CommandLine = words, .Build = *build, .Time = MCRejoinNote::Now()});
                !written)
            {
                SDL_Log("Lobby updates: the game to join after the update isn't noted (%s)", written.error().c_str());
            }
            else
            {
                SDL_Log("Lobby updates: restarting into the update, to join game %s", JoinAfter->Code.c_str());
            }
        }

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
    const std::optional<std::string> roomWords = RoomCommandLine();
    const std::string commandLine = roomWords.value_or(std::string());

    if (const auto written =
            roomWords.has_value()
                ? MCRejoinNote::Write(MCFileSystem::UserRoot(),
                                      {.CommandLine = commandLine, .Build = build, .Time = MCRejoinNote::Now()})
                : std::expected<void, std::string>(std::unexpected("the room's game isn't on the service yet"));
        !written)
    {
        SDL_Log(
            "Lobby updates: RESTART waits: no rejoin note (%s): the update installs as the game exits after the match",
            written.error().c_str());
        NoteUnwritten = true;
        return true;
    }

    // Port (Redux HD): a host's internet game keeps its place on the service, for the new version to come back to.
    KeepInternetPlace(true);

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

    SDL_Log("Lobby updates: restarting into the update, to start again as %s", WithoutToken(commandLine).c_str());
    RestartAt = Now() + SendTime;
    return true;
}

namespace
{
    /// <summary>The guests in a host's room at its RESTART still in it (none have left to wait for it yet).</summary>
    std::string GuestsStaying()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        return game == nullptr ? std::string()
                               : MCNamedPlayers(
                                     RoomAtRestart, [&game](const MCComing& guest)
                                     { return game->SessionManager->GetPlayer(guest.Id) != nullptr; },
                                     [](const MCComing& guest) { return Upper(guest.Name); });
    }
}

auto MCLobbyUpdate::Frame() -> void
{
    const Clock::time_point now = Now();

    // A host's restart waits for its guests to leave to wait for it (LeaveWait at most): one whose game takes the word
    // late would find its link gone with the word unread.
    if (RestartAt.has_value() && now >= *RestartAt)
    {
        const std::string staying = GuestsStaying();

        if (staying.empty() || now >= *RestartAt + LeaveWait)
        {
            if (!staying.empty())
            {
                SDL_Log("Lobby updates: installing without waiting longer for %s to leave", staying.c_str());
            }

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

    if (AwayLeft)
    {
        AwayLeft = false;
        PlacesChanged();
    }

    KeepPlaces(now);
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

    // A kept place has START ask and opens the room: only for a build that comes back (its rules said so), and once a
    // room, so no guest keeps the room open again and again.
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

    if (const std::shared_ptr<MCLogChat> chat = GlobalLog()->Chat; chat != nullptr)
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
        CloseRoomAgain();
    }

    PlacesChanged();

    if (const std::shared_ptr<MCLogChat> chat = GlobalLog()->Chat; chat != nullptr)
    {
        chat->AddLine(std::format(UpdateOffFormat, Upper(name)));
    }
}

auto MCLobbyUpdate::SetClock(MCClock clock) -> void
{
    TestClock = std::move(clock);
}

auto MCLobbyUpdate::MatchStarts() -> void
{
    if (MultiPlayer() != nullptr && GlobalLog() != nullptr && !Coming.empty())
    {
        DropPlaces();
    }
}

auto MCLobbyUpdate::LeftToUpdate(uint32_t playerId) -> bool
{
    return Restarting || RestartAt.has_value() || std::ranges::contains(Coming, playerId, &MCComing::Id);
}

auto MCLobbyUpdate::PlayerLeft(uint32_t playerId) -> void
{
    AwayLeft = AwayLeft || std::ranges::contains(Coming, playerId, &MCComing::Id);
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

    // The session screen shows it the room as it shows any player who joins (MCSessionScreen::WatchRoom).
    if (game->IsHost && Coming.empty())
    {
        CloseRoomAgain();
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
        case MCRoomRefusal::Lacks:
            return MCRefusedText(MultiPlayer()->SessionManager->RefusalReason());
        case MCRoomRefusal::Gone:
            break;
    }

    return std::string(RoomGoneText);
}

auto MCLobbyUpdate::JoinFailed() -> void
{
    // Port: a host that refused this game at the door said why.
    const MCRoomRefusal refusal = MultiPlayer()->SessionManager->LastRoomRefusal;
    const std::string text = refusal == MCRoomRefusal::Lacks ? RejoinFailedText(MCRoomRefusal::Lacks)
                             : !LobbyLaunchArgs.Rejoin       ? LoadGameString(JoinFailedString, StringBuffer)
                             : LobbyLaunchArgs.IsHost        ? std::string(RoomNotReopenedText)
                                                             : RejoinFailedText(refusal);

    // Port (Redux HD): a room the menus opened that the game couldn't come back to: the INTERNET screen, saying why.
    if (LobbyLaunchArgs.FromMenus)
    {
        BackToMenus();
        GlobalLog()->SetCurrentScreen(GlobalLog()->MainScreen);
        ShowInternetScreen();
        GlobalLog()->ShowLogScreen(true, true);
        ShowMenuMessage(text);
        return;
    }

    ShowJoinFailed(text);
}

auto MCLobbyUpdate::JoinAfterUpdate(std::optional<MCJoinAfter> game) -> void
{
    JoinAfter = std::move(game);
}

auto MCLobbyUpdate::BackToMenus() -> void
{
    SDL_Log("Lobby updates: the room was the menus': the game is the menus' again");
    LaunchedFromLobby = false;
    // Port-only (Redux HD): nor is it a lobby's game after its match (the results' timeout, the game's end).
    IsMPlayerGame = false;
    LobbyLaunchArgs.Active = false;
    LobbyLaunchArgs.FromMenus = false;
    LobbyLaunchArgs.Place.reset();
}

auto MCLobbyUpdate::KeepPlacesFor(std::span<const MCPlace> guests) -> void
{
    const Clock::time_point until = Now() + RejoinWait;

    for (const MCPlace& guest : guests)
    {
        SDL_Log("Lobby updates: the room keeps %s's place for the rematch", guest.Name.c_str());
        Coming.push_back({.Id = guest.Id, .Name = guest.Name, .Until = until, .Rematch = true});
    }

    PlacesChanged();
}

auto MCLobbyUpdate::WaitForRoom(const MCRoomAgain& room) -> void
{
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    SDL_Log("Lobby updates: joining the host's room at %s again", room.Address.c_str());
    // A new room, which has kept no one's place.
    RoomLeft.reset();
    Coming.clear();
    KeptOnce.clear();
    OwnPlaceKept = false;
    manager.ConnectTcp(room.Address);
    ShowWaitingDialog(room.Text);
    const Clock::time_point now = Now();
    HostWait = MCHostWait{.Until = now + RejoinWait,
                          .NextTry = now,
                          .PlayerName = room.PlayerName,
                          .Code = room.Internet ? std::optional(room.Address) : std::nullopt,
                          .Address = room.Address};
}
