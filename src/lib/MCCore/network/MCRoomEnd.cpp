#include "stdafx.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCMenuScreens.h"
#include "network/MCRoomEnd.h"
#include "ui/MCRmlFlow.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCGameContext.h"
#include "main/MCGameSession.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "network/MCInternetGame.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMcoRooms.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "network/MCRemove.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCHdArt.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>What a guest says of its host <paramref name="name"/>, who closed the room as it left.</summary>
    constexpr std::string_view ClosedFormat = "{} CLOSED THE GAME.";
    /// <summary>What a guest says of its host <paramref name="name"/>, whose game it lost (or lost its own connection).</summary>
    constexpr std::string_view LostFormat = "LOST THE CONNECTION TO {}'S GAME.";
    /// <summary>The host's name when the game doesn't know it.</summary>
    constexpr std::string_view SomeHost = "THE HOST";
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 3): the same over the remade games list, in sentence case with the host's name as
    /// typed ("Bob closed the game.").
    /// </summary>
    constexpr std::string_view ClosedSentence = "{} closed the game.";
    constexpr std::string_view LostSentence = "Lost the connection to {}'s game.";
    constexpr std::string_view SomeHostSentence = "the host";
    /// <summary>
    /// What a guest says while it joins its host <paramref name="name"/>'s room again for a rematch, over the games list
    /// (remade: SPEC-menus.md's stage 3, a sentence with the name as typed).
    /// </summary>
    constexpr std::string_view RejoiningFormat = "Rejoining {}'s game for a rematch...";
    /// <summary>What a host says over the games list while its internet game's place on the service comes back, and if it can't.</summary>
    constexpr std::string_view ReopeningText = "Opening your room again...";
    constexpr std::string_view NotReopenedText = "Couldn't open your room again.";

    /// <summary>A host's REMATCH: its wait for its guests to leave on its word, then the room hosted again.</summary>
    struct MCEnding
    {
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
    /// <summary>The host said it closed the room (its name), the guest's leave coming.</summary>
    std::optional<std::string> ClosedBy;
    /// <summary>Port-only (Redux HD): the host said it removed this game's player (its name), the guest's leave coming.</summary>
    std::optional<std::string> RemovedBy;
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
    /// What a guest says of its room's end: closed by its host <paramref name="closedBy"/>, as it said, else its
    /// connection to <paramref name="host"/>'s game lost.
    /// </summary>
    std::string LineOf(const std::optional<std::string>& closedBy, std::string_view host)
    {
        return closedBy.has_value() ? std::format(ClosedFormat, *closedBy) : std::format(LostFormat, host);
    }

    /// <summary>
    /// <see cref="LineOf"/> over the remade games list: <paramref name="host"/> as typed, in a sentence. A name the host
    /// said (<paramref name="closedBy"/>) is its own.
    /// </summary>
    std::string SentenceOf(const std::optional<std::string>& closedBy, std::string_view host)
    {
        return closedBy.has_value() ? std::format(ClosedSentence, *closedBy) : std::format(LostSentence, host);
    }

    /// <summary>
    /// The host's name in the menus' capitals, as the session has it: its host's player (as the transport marks it), or
    /// the game's host or server.
    /// </summary>
    std::string HostName(const MCMultiPlayer& game, bool capitals = true)
    {
        const auto named = [capitals](std::string_view name) { return capitals ? Upper(name) : std::string(name); };
        MCSessionManager& manager = *game.SessionManager;

        for (const std::shared_ptr<MCFidpPlayer>& player : manager.GetPlayers(nullptr))
        {
            if (player != nullptr && player->Id != manager.MyPlayerID && manager.DirectPlay != nullptr &&
                manager.DirectPlay->IsSessionHostPlayer(player->Id))
            {
                return named(player->Name);
            }
        }

        for (const uint32_t id : {game.HostID, game.ServerID, manager.ServerID})
        {
            if (const std::shared_ptr<MCFidpPlayer> host = id != 0 ? manager.GetPlayer(id) : nullptr; host != nullptr)
            {
                return named(host->Name);
            }
        }

        return std::string(capitals ? SomeHost : SomeHostSentence);
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
        return mission != nullptr && mission->State == MCMissionState::Results && mission->ResultsUp();
    }

    /// <summary>Tells player <paramref name="to"/> (0: every player) the room ends, <paramref name="how"/>.</summary>
    void SendRoomEnds(MCMultiPlayer& game, MCRoomEnd::MCHow how, uint32_t to = 0)
    {
        MCMPLongMessage message{};
        message.Header = PlainHeader(MCMPMessageType::RoomEnds);
        message.Value = std::to_underlying(how);
        game.SessionManager->SendPlainMessage(to, LinkupBytes(message));
    }

    /// <summary>
    /// Port-only (Redux HD): who removed this game's player, if its host did: as its host said, or as an internet game's
    /// service said (its copy kept out of the game), the host <paramref name="host"/> then.
    /// </summary>
    std::optional<std::string> RemovedByHost(const MCMultiPlayer& game, std::string_view host)
    {
        if (RemovedBy.has_value())
        {
            return RemovedBy;
        }

        // A joined guest's internet game plays on over its links when the service ends its socket: what the service
        // said is the socket's.
        const MCSessionManager& manager = *game.SessionManager;
        return manager.Internet != nullptr && manager.Internet->KeptOut().has_value() ? std::optional(std::string(host))
                                                                                      : std::nullopt;
    }

    /// <summary>
    /// Takes down the message up from the room (a player gone, a wait), unanswered: the room it was about has gone (its
    /// answer could end the multiplayer game the games screens need).
    /// </summary>
    void DropMessage(MCLogistics& logistics)
    {
        MCMenuDialog& dialog = *logistics.Message;

        if (dialog.IsShowing() != 0)
        {
            dialog.Callback = nullptr;
            dialog.Deactivate(0);
        }
    }

    /// <summary>The room's own, as its EXIT clears them: its timers, the players' lights, its chat.</summary>
    void ClearRoom(MCLogistics& logistics)
    {
        // Original behaviour (OB-167), as LeaveToGames: the same timer is removed twice.
        logistics.Room.StopBoxTimers();
        logistics.Room.StopTick();
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

        if (room.Hosting && GlobalLog() != nullptr && GlobalLog()->Room.Made())
        {
            room.Picks = GlobalLog()->Room.Picks();
        }

        return room;
    }

    /// <summary>The host's rematch's room can't be opened again: the games, saying so.</summary>
    void NotReopened(MCRoomEnd::MCGames from)
    {
        SDL_Log("Room end: the room couldn't be hosted again for the rematch");
        Returning.reset();
        // Port-only (SPEC-memory.md's "Between missions"): no rematch, so nothing plays the match's held art next.
        MCHdArt::LetGoHold();

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
        // Port-only (SPEC-menus.md's stage 2): through the flow, the room landing on the games and the main menu.
        // Set up at once (the picks and the guests' places after it), shown through the flow.
        MCScreenFlow::Land(MCMenuScreens::RoomStack());
        MCMenuScreens::OpenRoom("room again");
        GlobalLog()->Room.TakePicks(room.Picks);
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
            // Its wait over the games once they show, unless the room has opened meanwhile.
            MCRoomEnd::ShowGames(room.From, {},
                                 []
                                 {
                                     if (Reopening.has_value())
                                     {
                                         ShowWaitingDialog(ReopeningText);
                                     }
                                 });
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
        // The wait (its REJOINING line) over the games once they show.
        MCRoomEnd::ShowGames(rejoin->From, {}, [room = rejoin->Room] { MCLobbyUpdate::WaitForRoom(room); });
    }
}

auto MCRoomEnd::WaitsForGuests() -> bool
{
    return Ending.has_value();
}

auto MCRoomEnd::GuestLine() -> std::string
{
    // The launcher's guest says it in the 1998 dialog's capitals (the name kept as typed, for the games list's line).
    // Port-only (SPEC-menus.md's stage 4): over the remade room, in a sentence with the names as typed.
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    // Port-only (Redux HD; SPEC-mco-hosting.md's "REMOVE"): removed, the host named (its OK ends the game all the same).
    if (game != nullptr && game->SessionManager != nullptr)
    {
        if (const std::optional<std::string> removedBy = RemovedByHost(*game, HostName(*game, false));
            removedBy.has_value())
        {
            const std::string line = MCRemove::RemovedLine(*removedBy);
            return MCRmlFlow::Showing() ? line : Upper(line);
        }
    }

    if (MCRmlFlow::Showing())
    {
        return SentenceOf(ClosedBy, game != nullptr && game->SessionManager != nullptr ? HostName(*game, false)
                                                                                       : std::string(SomeHostSentence));
    }

    const std::optional<std::string> closedBy = ClosedBy.has_value() ? std::optional(Upper(*ClosedBy)) : std::nullopt;
    return LineOf(closedBy,
                  game != nullptr && game->SessionManager != nullptr ? HostName(*game) : std::string(SomeHost));
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

    // MCO's match, though the room has gone under it (the player taken out mid-match ends it from MCO's side first).
    if (MCMcoRooms::InMatch() || game->SessionManager->McoMatch)
    {
        return MCGames::Mco;
    }

    const MCNetProtocol connection = game->SessionManager->CurrentConnection;
    return connection == MCNetProtocol::TcpIp || connection == MCNetProtocol::Ipx ? MCGames::Lan : MCGames::None;
}

auto MCRoomEnd::ShowGames(MCGames games, std::string_view line, std::function<void()> then) -> void
{
    MCLogistics& logistics = *GlobalLog();
    // Back at the games, no rematch is pending (JoinAgain has taken its own before).
    Rejoin.reset();

    // MCO's match is over for this game: its player is back in MCO's room, which INTERNET shows.
    if (games == MCGames::Mco && MCMcoRooms::Available())
    {
        MCMcoRooms::Now().GameEnded();
    }

    RejoinHeard = false;
    JoinAfterResults = false;

    // The games screens find and join games through the multiplayer game, as MULTIPLAYER makes it.
    if (MultiPlayer() == nullptr)
    {
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        MultiPlayer()->Start();
    }

    // Port-only (SPEC-menus.md's stage 2): through the flow, with the one fade (at once on the way back from a match,
    // dark already), landing on the games with MULTIPLAYER ACCESS and the main menu under them.
    MCScreenFlow::Land(MCMenuScreens::GamesStack(games != MCGames::Lan, false));
    MCScreenFlow::Change("games",
                         [games, line = std::string(line), then = std::move(then)]
                         {
                             DropMessage(*GlobalLog());

                             if (games != MCGames::Lan)
                             {
                                 ShowInternetScreen();
                             }
                             else
                             {
                                 ShowLanScreen();
                             }

                             if (!line.empty())
                             {
                                 ShowMenuMessage(line);
                             }

                             if (then != nullptr)
                             {
                                 then();
                             }
                         });
}

auto MCRoomEnd::CloseRoom() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    // A rematch's room left: its picks are no one's.
    Returning.reset();

    if (Ending.has_value() || game == nullptr || !game->IsHost || game->SessionManager == nullptr ||
        game->SessionManager->CurrentSession() == nullptr)
    {
        return false;
    }

    MCSessionManager& manager = *game->SessionManager;

    // The room goes on under another game when one can take it over (MCDirectPlay::HasSuccessor): nothing to tell.
    if (manager.LeaveHandsOn())
    {
        SDL_Log("Room end: the host's LEAVE hands the room on");
        return false;
    }

    std::vector<uint32_t> told;
    std::ranges::transform(TakingGuests(*game), std::back_inserter(told), &MCLobbyUpdate::MCPlace::Id);

    if (told.empty())
    {
        return false;
    }

    // Port-only (SPEC-menus.md, "In the room"): the word first, then the session left at once, its goodbye kept apart
    // until the guests have left on it (LeaveWait at most): they say "Bob closed the game." as they go.
    SDL_Log("Room end: the host closes its room; its goodbye to %zu guests goes behind it", told.size());
    SendRoomEnds(*game, MCHow::Closed);
    manager.LeaveAfterGoodbye(std::move(told), LeaveWait);
    return true;
}

auto MCRoomEnd::LeaveLauncherGame() -> void
{
    CloseRoom();
    KillTheGame();
}

auto MCRoomEnd::FinishGoodbye(std::chrono::milliseconds internetWait) -> void
{
    if (!MCSessionManager::GoodbyeLingers())
    {
        return;
    }

    SDL_Log("Room end: the game ends once its goodbye to its room has gone, its window hidden meanwhile");

    if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
    {
        SDL_HideWindow(&display->Window());
    }

    MCSessionManager::FinishGoodbye(internetWait);
}

void MCRoomEnd::TellRemoved(uint32_t playerId)
{
    if (const std::shared_ptr<MCMultiPlayer> game = MultiPlayer(); game != nullptr && game->IsHost && playerId != 0)
    {
        SendRoomEnds(*game, MCHow::Removed, playerId);
    }
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
                                   .Text = std::format(RejoiningFormat, HostName(*game, false))}};
        RejoinHeard = true;
        return false;
    }

    // Port-only (Redux HD; SPEC-mco-hosting.md's "REMOVE"): the host removed this game's player: it leaves as the host
    // closing the room would make it.
    if (how == std::to_underlying(MCHow::Removed))
    {
        RemovedBy = HostName(*game, false);
        SDL_Log("Room end: %s removed this game's player", RemovedBy->c_str());
        return true;
    }

    if (how != std::to_underlying(MCHow::Closed))
    {
        SDL_Log("Room end: the host's word %d isn't one this game knows", how);
        return false;
    }

    // At the results the battle is over: the session goes as it does when lost there.
    ClosedBy = HostName(*game, false);
    SDL_Log("Room end: %s closed the room", ClosedBy->c_str());
    return true;
}

auto MCRoomEnd::SessionLost() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::optional<std::string> closedBy = std::exchange(ClosedBy, std::nullopt);
    // Port-only (Redux HD; SPEC-mco-hosting.md's "REMOVE"): removed by its host, as it said or the service did.
    const std::optional<std::string> removedBy = game != nullptr && game->SessionManager != nullptr
                                                     ? RemovedByHost(*game, HostName(*game, false))
                                                     : std::nullopt;
    RemovedBy.reset();

    // Only the room's own screens need the logistics (below): a mission has none while it plays and through its results
    // (MCMission drops it as the scenario loads). Asked first, it sent every match's end the original's way, which ended
    // the scenario under the results still up: their next draw read the home team it took (a guest's crash, 8 Nov).
    if (game == nullptr)
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

    // Said over the games list (remade: SPEC-menus.md's stage 3), in a sentence. MCO's match's link closing mid-match is
    // its host leaving, as MCO says it (SPEC-mco-rooms.md).
    // When MCO said why the player is out of its room (taken out, the room closed), INTERNET says that instead.
    const bool mcoMatch = games == MCGames::Mco && (game->PrepareScenarioReceived || game->InMission);
    const bool mcoSaid = games == MCGames::Mco && MCMcoRooms::Available() && !MCMcoRooms::Now().Seen().Left.empty();
    std::string line = removedBy.has_value() ? MCRemove::RemovedLine(*removedBy)
                       : mcoSaid             ? std::string()
                       : mcoMatch            ? std::format(MCMcoRooms::MatchEndedFormat, HostName(*game, false))
                                             : SentenceOf(closedBy, HostName(*game, false));
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

    const std::shared_ptr<MCLogistics> logisticsHeld = GlobalLog();

    if (logisticsHeld == nullptr)
    {
        SDL_Log("Room end: the room ended outside a match with no screens up; the original's way");
        return false;
    }

    LeaveRoomNow(*game);
    ShowGames(games, line);
    return true;
}

auto MCRoomEnd::LeaveRoomSaying(std::string_view line) -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr || GlobalLog() == nullptr)
    {
        return false;
    }

    const MCGames games = GamesOf();

    if (games == MCGames::None)
    {
        return false;
    }

    LeaveRoomNow(*game);
    ShowGames(games, line);
    return true;
}

auto MCRoomEnd::MatchStarts() -> void
{
    MatchRoom = RoomNow();
    Returning.reset();
    // A rematch's hosting is the last match's way back, never this one's.
    HostAfterResults = false;
}

auto MCRoomEnd::OffersRematch() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    // MCO's matches have none: MCO's own games quit after theirs.
    return game != nullptr && game->IsHost && !LaunchedFromLobby && MatchRoom.has_value() && MatchRoom->Hosting &&
           MatchRoom->From != MCGames::Mco && !Ending.has_value() &&
           game->SessionManager->CurrentSession() != nullptr && !TakingGuests(*game).empty();
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

    Ending = MCEnding{.Told = std::move(told), .Until = Now() + LeaveWait};
}

auto MCRoomEnd::Rematching() -> bool
{
    return Ending.has_value() || HostAfterResults;
}

auto MCRoomEnd::LeaveToGames() -> void
{
    // Port-only (the personal build): a LAN game's room (the session screen; the ready room's) goes back to the games.
    // The modem and serial screens (shown only without a network) go back to the connection screen, as in the original.
    // Port-only (SPEC-menus.md's stage 3): the remade LAN screen's own way back is its kind's (logistics/MCGamesScreen).
    const MCNetProtocol connection = MultiPlayer()->SessionManager->CurrentConnection;
    const MCScreenFlow::MCScreenId top = MCMenuScreens::Top();
    const bool fromRoom = top == MCMenuScreens::Room();
    const bool fromLanGame = (fromRoom || top == MCMenuScreens::ReadyRoom()) &&
                             (connection == MCNetProtocol::TcpIp || connection == MCNetProtocol::Ipx);
    // Port (Redux HD): an internet game's room goes back to the INTERNET screen; MCO's match's to its MCO room there.
    const bool fromMcoGame = fromRoom && GamesOf() == MCGames::Mco;
    const bool fromInternetGame = fromRoom && (MultiPlayer()->SessionManager->Internet != nullptr || fromMcoGame);
    // Original behaviour (OB-167): the same timer is removed twice (team 2's is left).
    GlobalLog()->Room.StopBoxTimers();

    // Port-only (SPEC-menus.md, "In the room"): a host's goodbye to its room goes behind its way out (network/MCRoomEnd.h's
    // CloseRoom), which leaves the session at once: the leave below then finds none to leave.
    if (fromRoom)
    {
        CloseRoom();
    }

    MultiPlayer()->LeaveSession();

    GlobalLog()->ResetPlayerLights();

    // The room's own: its timer, and its chat (the next room's starts empty, as from the main menu).
    if (fromRoom)
    {
        GlobalLog()->Room.StopTick();
        GlobalLog()->ClearChat();
    }

    if (fromMcoGame && MCMcoRooms::Available())
    {
        MCMcoRooms::Now().GameEnded();
    }

    // The room hides as the games show (MCLogistics::NoteTop).
    if (fromInternetGame)
    {
        ShowInternetScreen();
        return;
    }

    if (fromLanGame)
    {
        // The games found again, the names as typed.
        DoTheTcpThang();
        return;
    }

    // Port-only (stage 7): the remade MULTIPLAYER (the modem's and serial's screens aren't made), shown at once: the way
    // to it runs in a change's dark work.
    MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
}

auto MCRoomEnd::LeaveForLauncher() -> void
{
    KillTheGame();
}

auto MCRoomEnd::Leave(std::string line) -> void
{
    // Port-only (SPEC-menus.md's stage 2): the way out through the flow, with the one fade; (SPEC-menus.md, "In the
    // room") a host's goodbye to its guests goes behind it, nothing waiting for it (LeaveToGames).
    MCScreenFlow::Change("room back",
                         [line = std::move(line)]
                         {
                             const MCNetProtocol connection = MultiPlayer()->SessionManager->CurrentConnection;

                             if (connection == MCNetProtocol::TcpIp || connection == MCNetProtocol::Ipx)
                             {
                                 LeaveToGames();
                             }
                             else
                             {
                                 Cancel();
                             }

                             if (!line.empty())
                             {
                                 ShowMenuMessage(line);
                             }
                         });
}

auto MCRoomEnd::PlaceReturning(MCRoomModel& room) -> void
{
    if (Returning.has_value())
    {
        room.PlaceByName(*Returning);
    }
}

auto MCRoomEnd::TakesOnAfterMatch() -> bool
{
    if (AfterScenario.has_value() || HostAfterResults || JoinAfterResults)
    {
        return true;
    }

    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr && game->InMission && !LaunchedFromLobby && MatchRoom.has_value() &&
           MatchRoom->From != MCGames::None;
}

auto MCRoomEnd::RematchAfterMatch() -> bool
{
    return HostAfterResults || JoinAfterResults;
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
            Mission()->LeaveResults();
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

    Ending.reset();

    if (game == nullptr)
    {
        return;
    }

    // The rematch: the results close, and the room is hosted again (AfterMatch, once the menus are back); with them gone
    // already, at once. At the results the mission has no menus (it dropped them as the scenario loaded): asking for them
    // first dropped the rematch there, the room never hosted again.
    HostAfterResults = true;

    if (AtResults())
    {
        Mission()->LeaveResults();
    }
    else if (GlobalLog() != nullptr)
    {
        AfterMatch();
    }

    // Else the results are already closing: the mission's way back hosts it (TakesOnAfterMatch).
}

auto MCRoomEnd::SetClock(std::function<Clock::time_point()> clock) -> void
{
    TestClock = std::move(clock);
}
