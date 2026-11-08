#include "stdafx.h"
#include "network/MCRoomEnd.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCGameContext.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "network/MCInternetGame.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "platform/MCDirectPlay.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>What a guest says of its host <paramref name="name"/>, who closed the room as it left.</summary>
    constexpr std::string_view ClosedFormat = "{} CLOSED THE GAME.";
    /// <summary>What a guest says of its host <paramref name="name"/>, whose game it lost (or lost its own connection).</summary>
    constexpr std::string_view LostFormat = "LOST THE CONNECTION TO {}'S GAME.";
    /// <summary>The host's name when the game doesn't know it.</summary>
    constexpr std::string_view SomeHost = "THE HOST";
    /// <summary>What a guest says while it joins its host <paramref name="name"/>'s room again for a rematch.</summary>
    constexpr std::string_view RejoiningFormat = "REJOINING {}'S GAME FOR A REMATCH...";
    /// <summary>What a host says while its internet game's place on the service comes back, and if it can't.</summary>
    constexpr std::string_view ReopeningText = "OPENING YOUR ROOM AGAIN...";
    constexpr std::string_view NotReopenedText = "COULDN'T OPEN YOUR ROOM AGAIN.";

    /// <summary>A host's wait for its guests to leave on its word, then what it does.</summary>
    struct MCEnding
    {
        MCRoomEnd::MCHow How = MCRoomEnd::MCHow::Closed;
        /// <summary>The guests told, by id: the wait ends when none is left in the session.</summary>
        std::vector<uint32_t> Told;
        Clock::time_point Until;
    };

    /// <summary>A match's room ended under it: the games screen to go back to after the scenario, and why.</summary>
    struct MCBackToGames
    {
        MCRoomEnd::MCGames Games = MCRoomEnd::MCGames::None;
        std::string Line;
    };

    /// <summary>The room a match is played from (MCRoomEnd::MatchStarts), which its end goes back to.</summary>
    struct MCMatchRoom
    {
        MCRoomEnd::MCGames From = MCRoomEnd::MCGames::None;
        bool Hosting = false;
        /// <summary>The session's name and size, and this game's player's name.</summary>
        std::string Name;
        int32_t MaxPlayers = 0;
        std::string PlayerName;
        /// <summary>A LAN host's own address (where it hosts again), a LAN guest's host's; an internet game's code.</summary>
        std::string Address;
        /// <summary>An internet game is public: a host's listed and relayed, a guest's relayed (as it knew it).</summary>
        bool Public = false;
        /// <summary>A host's internet game's place on the service, kept through its rematch's leave.</summary>
        std::optional<MCInternetPlace> Place;
        /// <summary>The host's picks as the match started.</summary>
        MCRoomPicks Picks;
        /// <summary>The guests whose places the rematch's room keeps.</summary>
        std::vector<MCLobbyUpdate::MCPlace> Guests;
    };

    /// <summary>A guest's way back to its host's room hosted again for a rematch: its games screen, and the room.</summary>
    struct MCRejoin
    {
        MCRoomEnd::MCGames From = MCRoomEnd::MCGames::None;
        MCLobbyUpdate::MCRoomAgain Room;
    };

    std::optional<MCEnding> Ending;
    /// <summary>The host's EXIT, its wait over: it leaves without telling the room again.</summary>
    bool LeavingNow = false;
    /// <summary>The host said it closed the room (its name), the guest's leave coming.</summary>
    std::optional<std::string> ClosedBy;
    std::optional<MCMatchRoom> MatchRoom;
    std::optional<MCBackToGames> AfterScenario;
    /// <summary>The host's REMATCH, its guests gone: the room is hosted again once the results have closed.</summary>
    bool HostAfterResults = false;
    /// <summary>The host's internet game coming back to its place on the service, the room then opened.</summary>
    std::optional<MCMatchRoom> Reopening;
    /// <summary>The host's rematch's picks, which each player takes again as it returns (until the next match).</summary>
    std::optional<MCRoomPicks> Returning;
    /// <summary>A guest's host said it hosts its room again: the guest leaves (its results closed) and joins it.</summary>
    std::optional<MCRejoin> Rejoin;
    bool RejoinHeard = false;
    /// <summary>A guest's results closing for its host's rematch: then it joins the room again.</summary>
    bool JoinAfterResults = false;
    std::function<Clock::time_point()> TestClock;

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

    /// <summary>
    /// The host's name in the menus' capitals, as the session has it: its host's player (as the transport marks it), or
    /// the game's host or server.
    /// </summary>
    std::string HostName(const MCMultiPlayer& game)
    {
        MCSessionManager& manager = *game.SessionManager;

        for (const std::shared_ptr<MCFidpPlayer>& player : manager.GetPlayers(nullptr))
        {
            if (player != nullptr && player->Id != manager.MyPlayerID && manager.DirectPlay != nullptr &&
                manager.DirectPlay->IsSessionHostPlayer(player->Id))
            {
                return Upper(player->Name);
            }
        }

        for (const uint32_t id : {game.HostID, game.ServerID, manager.ServerID})
        {
            if (const std::shared_ptr<MCFidpPlayer> host = id != 0 ? manager.GetPlayer(id) : nullptr; host != nullptr)
            {
                return Upper(host->Name);
            }
        }

        return std::string(SomeHost);
    }

    /// <summary>Whether player <paramref name="id"/>'s build takes the host's word that the room ends.</summary>
    bool HearsRoomEnds(const MCMultiPlayer& game, uint32_t id)
    {
        const auto said = game.PlayerRules.find(id);
        return said != game.PlayerRules.end() && (said->second & MCMatchRule::Rematch) != 0;
    }

    /// <summary>Whether the match's results are up (the battle over).</summary>
    bool AtResults()
    {
        const std::shared_ptr<MCMission> mission = Mission();
        return mission != nullptr && mission->State == MCMissionState::Results && mission->ResultsScreen != nullptr;
    }

    /// <summary>Tells every player the room ends, <paramref name="how"/>.</summary>
    void SendRoomEnds(MCMultiPlayer& game, MCRoomEnd::MCHow how)
    {
        MCMPLongMessage message{};
        message.Header = PlainHeader(MCMPMessageType::RoomEnds);
        message.Value = std::to_underlying(how);
        game.SessionManager->SendPlainMessage(0, LinkupBytes(message));
    }

    /// <summary>
    /// Takes down the message up from the room (a player gone, a wait), unanswered: the room it was about has gone (its
    /// answer could end the multiplayer game the games screens need).
    /// </summary>
    void DropMessage(MCLogistics& logistics)
    {
        MCReusableDialog& dialog = *logistics.MessageDialog;

        if (dialog.IsShowing() != 0)
        {
            dialog.Callback = nullptr;
            dialog.Deactivate(0);
        }
    }

    /// <summary>The room's own, as its EXIT clears them: its timers, the players' lights, its chat.</summary>
    void ClearRoom(MCLogistics& logistics)
    {
        // Original behaviour (OB-167), as CancelToConnect: the same timer is removed twice.
        GuiSystem().RemoveTimer(*logistics.SessionScreen->Team1RPText, 0);
        GuiSystem().RemoveTimer(*logistics.SessionScreen->Team1RPText, 0);
        GuiSystem().RemoveTimer(*logistics.SessionScreen, 0);
        logistics.ResetPlayerLights();
        logistics.ClearChat();
    }

    /// <summary>Leaves the room this game is in, its screens' own cleared, as its EXIT does.</summary>
    void LeaveRoomNow(MCMultiPlayer& game)
    {
        MCLogistics& logistics = *GlobalLog();
        game.InLogistics = false;
        logistics.DestroyMultiplayer();
        ClearRoom(logistics);
        game.LeaveSession();
    }

    /// <summary>The guests in <paramref name="game"/>'s session whose builds take the host's word.</summary>
    std::vector<MCLobbyUpdate::MCPlace> TakingGuests(MCMultiPlayer& game)
    {
        std::vector<MCLobbyUpdate::MCPlace> guests;

        for (const std::shared_ptr<MCFidpPlayer>& player : game.SessionManager->GetPlayers(nullptr))
        {
            if (player != nullptr && player->Id != game.SessionManager->MyPlayerID && HearsRoomEnds(game, player->Id))
            {
                guests.push_back({.Id = player->Id, .Name = player->Name});
            }
        }

        return guests;
    }

    /// <summary>A LAN guest's host's address as it reached it, else the connection's own.</summary>
    std::string LanAddress(MCSessionManager& manager, bool hosting)
    {
        const std::optional<std::string> joined =
            !hosting && manager.DirectPlay != nullptr ? manager.DirectPlay->JoinedAddress() : std::nullopt;
        return joined.value_or(manager.TcpAddress());
    }

    /// <summary>The room this game is in as it stands, as a match is started from it or a rematch rejoins it.</summary>
    std::optional<MCMatchRoom> RoomNow()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        const MCRoomEnd::MCGames from = MCRoomEnd::GamesOf();

        if (game == nullptr || from == MCRoomEnd::MCGames::None)
        {
            return std::nullopt;
        }

        MCSessionManager& manager = *game->SessionManager;
        const std::shared_ptr<MCFidpSession> session = manager.CurrentSession();
        const std::shared_ptr<MCFidpPlayer> self = manager.MyPlayer();

        if (session == nullptr || self == nullptr)
        {
            return std::nullopt;
        }

        MCMatchRoom room{.From = from,
                         .Hosting = game->IsHost != 0,
                         .Name = session->Name,
                         .MaxPlayers = static_cast<int32_t>(session->SessionDesc.dwMaxPlayers),
                         .PlayerName = self->Name};

        if (from == MCRoomEnd::MCGames::Internet)
        {
            room.Address = manager.Internet->Code();
            room.Public = manager.Internet->Public();
        }
        else
        {
            room.Address = LanAddress(manager, room.Hosting);
        }

        if (room.Hosting && GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
        {
            room.Picks = GlobalLog()->SessionScreen->Picks();
        }

        return room;
    }

    /// <summary>The host's rematch's room can't be opened again: the games, saying so.</summary>
    void NotReopened(MCRoomEnd::MCGames from)
    {
        SDL_Log("Room end: the room couldn't be hosted again for the rematch");
        Returning.reset();

        if (const std::shared_ptr<MCMultiPlayer> game = MultiPlayer(); game != nullptr)
        {
            game->SessionManager->EndInternet();
        }

        MCRoomEnd::ShowGames(from, NotReopenedText);
    }

    /// <summary>
    /// The host's rematch's session made (on its port, or over its internet game back in its place): the room opens,
    /// takes the match's picks, and keeps its guests' places.
    /// </summary>
    void OpenAgain(const MCMatchRoom& room)
    {
        MCMultiPlayer& game = *MultiPlayer();

        if (game.CreateSession(room.Name, room.PlayerName, room.MaxPlayers) != 0)
        {
            NotReopened(room.From);
            return;
        }

        SDL_Log("Room end: \"%s\" hosted again for the rematch; keeping %zu guests' places", room.Name.c_str(),
                room.Guests.size());
        DropMessage(*GlobalLog());
        GlobalLog()->SetUpSessionScreen();
        GlobalLog()->SessionScreen->TakePicks(room.Picks);
        MCLobbyUpdate::KeepPlacesFor(room.Guests);
    }

    /// <summary>
    /// The host's rematch, its results closed: the match's session left (an internet game's socket closing as a drop,
    /// its place kept) and the same room hosted again.
    /// </summary>
    void HostAgain()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (game == nullptr || !MatchRoom.has_value())
        {
            return;
        }

        const MCMatchRoom room = *MatchRoom;
        MCSessionManager& manager = *game->SessionManager;
        SDL_Log("Room end: hosting \"%s\" again for the rematch", room.Name.c_str());
        LeaveRoomNow(*game);
        game->PlayerRules.clear();
        Returning = room.Picks;

        if (room.From == MCRoomEnd::MCGames::Internet)
        {
            // As INTERNET CONNECTION hosts: the connection first, then the game back in its place.
            manager.ConnectTcp({});

            if (!room.Place.has_value() || !manager.BeginInternet(true, room.Place->Code, room.Public, room.Place))
            {
                NotReopened(room.From);
                return;
            }

            Reopening = room;
            MCRoomEnd::ShowGames(room.From, {});
            ShowWaitingDialog(ReopeningText);
            return;
        }

        manager.ConnectTcp(room.Address);
        OpenAgain(room);
    }

    /// <summary>Moves the host's internet game back to its place on the service along; the room opens once it's there.</summary>
    void TendReopening()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        const MCInternetGame::Stage stage = game != nullptr && game->SessionManager->Internet != nullptr
                                                ? game->SessionManager->Internet->Update()
                                                : MCInternetGame::Stage::Failed;

        if (stage != MCInternetGame::Stage::Ready && stage != MCInternetGame::Stage::Failed)
        {
            return;
        }

        const MCMatchRoom room = std::move(*Reopening);
        Reopening.reset();

        if (stage == MCInternetGame::Stage::Failed)
        {
            NotReopened(room.From);
            return;
        }

        OpenAgain(room);
    }

    /// <summary>
    /// A guest whose host hosts its room again: out of the session (from the room; the results' close has left it),
    /// over its games screen, it joins the room again.
    /// </summary>
    void JoinAgain()
    {
        const std::optional<MCRejoin> rejoin = std::exchange(Rejoin, std::nullopt);
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (!rejoin.has_value() || game == nullptr)
        {
            return;
        }

        if (game->SessionManager->CurrentSession() != nullptr)
        {
            LeaveRoomNow(*game);
        }

        game->PlayerRules.clear();
        MCRoomEnd::ShowGames(rejoin->From, {});
        MCLobbyUpdate::WaitForRoom(rejoin->Room);
    }
}

auto MCRoomEnd::GamesOf() -> MCGames
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (LaunchedFromLobby || game == nullptr || game->SessionManager == nullptr)
    {
        return MCGames::None;
    }

    if (game->SessionManager->Internet != nullptr)
    {
        return MCGames::Internet;
    }

    const MCNetProtocol connection = game->SessionManager->CurrentConnection;
    return connection == MCNetProtocol::TcpIp || connection == MCNetProtocol::Ipx ? MCGames::Lan : MCGames::None;
}

auto MCRoomEnd::ShowGames(MCGames games, std::string_view line) -> void
{
    MCLogistics& logistics = *GlobalLog();
    // Back at the games, no rematch is pending (JoinAgain has taken its own before).
    Rejoin.reset();
    RejoinHeard = false;
    JoinAfterResults = false;

    // The games screens find and join games through the multiplayer game, as MULTIPLAYER makes it.
    if (MultiPlayer() == nullptr)
    {
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        MultiPlayer()->Start();
    }

    logistics.CurrentScreen()->ShowGuiWindow(false);
    DropMessage(logistics);

    if (games == MCGames::Internet)
    {
        ShowInternetScreen();
    }
    else
    {
        ShowLanScreen();
    }

    logistics.ShowLogScreen(true, true);

    if (!line.empty())
    {
        ShowMenuMessage(line);
    }
}

auto MCRoomEnd::CloseRoom() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    // A rematch's room left: its picks are no one's.
    Returning.reset();

    if (LeavingNow || Ending.has_value() || game == nullptr || !game->IsHost || GamesOf() == MCGames::None ||
        game->SessionManager->CurrentSession() == nullptr)
    {
        return false;
    }

    // The room goes on under another game when one can take it over (MCDirectPlay::HasSuccessor): nothing to tell.
    if (game->SessionManager->LeaveHandsOn())
    {
        SDL_Log("Room end: the host's EXIT hands the room on");
        return false;
    }

    std::vector<uint32_t> told;

    for (const std::shared_ptr<MCFidpPlayer>& player : game->SessionManager->GetPlayers(nullptr))
    {
        if (player != nullptr && player->Id != game->SessionManager->MyPlayerID && HearsRoomEnds(*game, player->Id))
        {
            told.push_back(player->Id);
        }
    }

    if (told.empty())
    {
        return false;
    }

    SDL_Log("Room end: the host closes its room; waiting for %zu guests to leave first", told.size());
    SendRoomEnds(*game, MCHow::Closed);
    Ending = MCEnding{.How = MCHow::Closed, .Told = std::move(told), .Until = Now() + LeaveWait};

    // Nothing more to press on the room meanwhile.
    if (const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        logistics != nullptr && logistics->SessionScreen != nullptr && logistics->SessionScreen->ExitButton != nullptr)
    {
        logistics->SessionScreen->ExitButton->Disabled = true;
        logistics->SessionScreen->StartButton->Disabled = true;
    }

    return true;
}

auto MCRoomEnd::HostSaid(uint32_t fromId, int32_t how) -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr || game->IsHost ||
        (fromId != game->ServerID && fromId != game->HostID && !game->SessionManager->SpeaksForSession(fromId)))
    {
        SDL_Log("Room end: a room's end from player %u, not this game's host, ignored", fromId);
        return false;
    }

    if (how == std::to_underlying(MCHow::Rematch))
    {
        const std::optional<MCMatchRoom> room = RoomNow();

        if (!room.has_value())
        {
            SDL_Log("Room end: the host's rematch, ignored: not a room the menus opened");
            return false;
        }

        // The session goes once the results are closed (MCRoomEnd::Frame).
        SDL_Log("Room end: the host hosts its room again for a rematch");
        Rejoin = MCRejoin{.From = room->From,
                          .Room = {.Address = room->Address,
                                   .Internet = room->From == MCGames::Internet,
                                   .RelayOnly = room->Public,
                                   .PlayerName = room->PlayerName,
                                   .Text = std::format(RejoiningFormat, HostName(*game))}};
        RejoinHeard = true;
        return false;
    }

    if (how != std::to_underlying(MCHow::Closed))
    {
        SDL_Log("Room end: the host's word %d isn't one this game knows", how);
        return false;
    }

    // At the results the battle is over: the session goes as it does when lost there.
    ClosedBy = HostName(*game);
    SDL_Log("Room end: %s closed the room", ClosedBy->c_str());
    return true;
}

auto MCRoomEnd::SessionLost() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::optional<std::string> closedBy = std::exchange(ClosedBy, std::nullopt);

    if (game == nullptr || GlobalLog() == nullptr)
    {
        return false;
    }

    // The battle is over: the results stay, and DONE goes on as it does (the session already gone).
    if (AtResults())
    {
        SDL_Log("Room end: the session is lost at the results; they stay");
        game->LeaveSession();
        game->InMission = true;
        return true;
    }

    const MCGames games = GamesOf();

    if (games == MCGames::None)
    {
        return false;
    }

    // The host said REMATCH while this game was still in the battle, then hosted its room again: the end is that, and
    // this game joins the room once its scenario has ended (AfterMatch), as it would from its results.
    if (RejoinHeard && Rejoin.has_value() && (game->PrepareScenarioReceived || game->InMission))
    {
        SDL_Log("Room end: the host's room went for its rematch; joining it after the battle");
        RejoinHeard = false;
        JoinAfterResults = true;
        game->InMission = false;
        game->LeaveSession();
        Mission()->EndScenario();
        return true;
    }

    const std::string host = HostName(*game);
    std::string line = closedBy.has_value() ? std::format(ClosedFormat, *closedBy) : std::format(LostFormat, host);
    SDL_Log("Room end: the room ended under this game (%s)", line.c_str());

    // A match: back to the games once its scenario has ended (MCRoomEnd::AfterMatch).
    if (game->PrepareScenarioReceived || game->InMission)
    {
        AfterScenario = MCBackToGames{.Games = games, .Line = std::move(line)};
        game->InMission = false;
        game->LeaveSession();
        Mission()->EndScenario();
        return true;
    }

    MCLogistics& logistics = *GlobalLog();
    game->InLogistics = false;
    logistics.DestroyMultiplayer();
    ClearRoom(logistics);
    game->LeaveSession();
    ShowGames(games, line);
    return true;
}

auto MCRoomEnd::MatchStarts() -> void
{
    MatchRoom = RoomNow();
    Returning.reset();
}

auto MCRoomEnd::OffersRematch() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr && game->IsHost && !LaunchedFromLobby && MatchRoom.has_value() && MatchRoom->Hosting &&
           !Ending.has_value() && game->SessionManager->CurrentSession() != nullptr && !TakingGuests(*game).empty();
}

auto MCRoomEnd::Rematch() -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr || !game->IsHost || LaunchedFromLobby || !MatchRoom.has_value() || !MatchRoom->Hosting ||
        Ending.has_value() || HostAfterResults)
    {
        SDL_Log("Room end: REMATCH, ignored: not the host of a room the menus opened, or one under way");
        return;
    }

    MatchRoom->Guests = TakingGuests(*game);

    // An internet game keeps its place on the service through the leave, to come back to with the same code.
    if (MatchRoom->From == MCGames::Internet)
    {
        const std::unique_ptr<MCInternetGame>& internet = game->SessionManager->Internet;
        MatchRoom->Place = internet != nullptr ? internet->Place() : std::nullopt;

        if (!MatchRoom->Place.has_value())
        {
            SDL_Log("Room end: REMATCH, ignored: the room's game isn't on the service");
            return;
        }

        internet->KeepPlace(true);
    }

    std::vector<uint32_t> told;
    std::ranges::transform(MatchRoom->Guests, std::back_inserter(told), &MCLobbyUpdate::MCPlace::Id);
    SDL_Log("Room end: REMATCH: telling %zu guests, then hosting the room again", told.size());

    if (game->SessionManager->CurrentSession() != nullptr)
    {
        SendRoomEnds(*game, MCHow::Rematch);
    }

    Ending = MCEnding{.How = MCHow::Rematch, .Told = std::move(told), .Until = Now() + LeaveWait};
}

auto MCRoomEnd::Rematching() -> bool
{
    return (Ending.has_value() && Ending->How == MCHow::Rematch) || HostAfterResults;
}

auto MCRoomEnd::PlaceReturning(MCSessionScreen& room) -> void
{
    if (Returning.has_value())
    {
        room.PlaceByName(*Returning);
    }
}

auto MCRoomEnd::AfterMatch() -> bool
{
    if (std::optional<MCBackToGames> back = std::exchange(AfterScenario, std::nullopt); back.has_value())
    {
        MatchRoom.reset();
        ShowGames(back->Games, back->Line);
        return true;
    }

    // The rematch: the host hosts the room again, a guest joins it.
    if (std::exchange(HostAfterResults, false))
    {
        HostAgain();
        return true;
    }

    if (std::exchange(JoinAfterResults, false))
    {
        JoinAgain();
        return true;
    }

    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const MCGames from = MatchRoom.has_value() ? MatchRoom->From : MCGames::None;
    MatchRoom.reset();

    if (game == nullptr || !game->InMission || LaunchedFromLobby || from == MCGames::None)
    {
        return false;
    }

    // DONE: the session left (an internet game's place on the service with it), the games again.
    SDL_Log("Room end: the match is over; back to the games");
    MCGameContext::Current().SetMultiPlayer(nullptr);
    ShowGames(from, {});
    return true;
}

auto MCRoomEnd::Frame() -> void
{
    if (Reopening.has_value())
    {
        TendReopening();
    }

    // A guest's host's REMATCH: its results close, leaving the session first, then it joins the room again
    // (AfterMatch); told in the room, it joins straight away; in battle, its results come first.
    if (const std::shared_ptr<MCMultiPlayer> guest = MultiPlayer(); RejoinHeard && guest != nullptr)
    {
        if (AtResults())
        {
            RejoinHeard = false;
            JoinAfterResults = true;
            guest->LeaveSession();
            Mission()->CloseResultsScreen();
        }
        else if (!guest->InMission && !guest->PrepareScenarioReceived && GlobalLog() != nullptr)
        {
            RejoinHeard = false;
            JoinAgain();
        }
    }

    if (!Ending.has_value())
    {
        return;
    }

    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const bool left =
        game == nullptr || std::ranges::none_of(Ending->Told, [&game](uint32_t id)
                                                { return game->SessionManager->GetPlayer(id) != nullptr; });

    if (!left && Now() < Ending->Until)
    {
        return;
    }

    if (!left)
    {
        SDL_Log("Room end: not waiting longer for every guest to leave");
    }

    const MCHow how = Ending->How;
    Ending.reset();

    if (game == nullptr || GlobalLog() == nullptr)
    {
        return;
    }

    // The rematch: the results close, and the room is hosted again (AfterMatch); with them gone already, at once.
    if (how == MCHow::Rematch)
    {
        HostAfterResults = true;

        if (AtResults())
        {
            Mission()->CloseResultsScreen();
        }
        else
        {
            AfterMatch();
        }

        return;
    }

    // The EXIT, now: back to the games.
    LeavingNow = true;
    LeaveRoom();
    LeavingNow = false;
}

auto MCRoomEnd::SetClock(std::function<Clock::time_point()> clock) -> void
{
    TestClock = std::move(clock);
}
