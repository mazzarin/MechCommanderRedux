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

    std::optional<MCEnding> Ending;
    /// <summary>The host's EXIT, its wait over: it leaves without telling the room again.</summary>
    bool LeavingNow = false;
    /// <summary>The host said it closed the room (its name), the guest's leave coming.</summary>
    std::optional<std::string> ClosedBy;
    /// <summary>The games screen of the room the match was started from (MCRoomEnd::MatchStarts).</summary>
    MCRoomEnd::MCGames MatchFrom = MCRoomEnd::MCGames::None;
    std::optional<MCBackToGames> AfterScenario;
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

    // The games screens find and join games through the multiplayer game, as MULTIPLAYER makes it.
    if (MultiPlayer() == nullptr)
    {
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        MultiPlayer()->Start();
    }

    logistics.CurrentScreen()->ShowGuiWindow(false);
    logistics.MessageDialog->Deactivate(0);

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

    if (LeavingNow || Ending.has_value() || game == nullptr || !game->IsHost || GamesOf() == MCGames::None ||
        game->SessionManager->CurrentSession() == nullptr)
    {
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
    MatchFrom = GamesOf();
}

auto MCRoomEnd::AfterMatch() -> bool
{
    if (std::optional<MCBackToGames> back = std::exchange(AfterScenario, std::nullopt); back.has_value())
    {
        MatchFrom = MCGames::None;
        ShowGames(back->Games, back->Line);
        return true;
    }

    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const MCGames from = std::exchange(MatchFrom, MCGames::None);

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

    if (game == nullptr || GlobalLog() == nullptr)
    {
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
