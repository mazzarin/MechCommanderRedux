#include "stdafx.h"
#include "logistics/MCRoomModel.h"

#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayer.h"

namespace
{
    /// <summary>The 1998 room, whose actions the model's are while they live there (none outside a game).</summary>
    MCOptionalRef<MCSessionScreen> Screen()
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (logistics == nullptr || logistics->SessionScreen == nullptr)
        {
            return std::nullopt;
        }

        return *logistics->SessionScreen;
    }

    /// <summary>The session's manager, in a multiplayer game.</summary>
    MCOptionalRef<MCSessionManager> Manager()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (game == nullptr || game->SessionManager == nullptr)
        {
            return std::nullopt;
        }

        return *game->SessionManager;
    }

    /// <summary>The slot of <paramref name="playerId"/> on <paramref name="team"/>, if it's there.</summary>
    std::optional<int8_t> SlotOf(std::span<const uint32_t> team, uint32_t playerId)
    {
        const auto found = std::ranges::find(team, playerId);
        return found == team.end() ? std::nullopt : std::optional(static_cast<int8_t>(found - team.begin()));
    }

    /// <summary>Whether <paramref name="team"/> is one of the two teams.</summary>
    bool IsTeam(int8_t team)
    {
        return team == MCRoomModel::Team1 || team == MCRoomModel::Team2;
    }
}

auto MCRoomModel::Players() const -> std::vector<MCPlayer>
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    const MCOptionalRef<MCSessionScreen> screen = Screen();

    if (!manager.has_value() || !screen.has_value())
    {
        return {};
    }

    // The players with a name on the room (the 1998 screen's name slots, by id), the session's names and numbers.
    std::vector<MCPlayer> players;

    for (const MCGuiHandle<MCPlayerNameObject>& name : screen->get().PlayerNames)
    {
        if (name == nullptr || name->PlayerId == NoPlayer)
        {
            continue;
        }

        const std::shared_ptr<MCFidpPlayer> session = manager->get().GetPlayer(name->PlayerId);

        if (session == nullptr)
        {
            continue;
        }

        MCPlayer player{.Id = session->Id,
                        .Name = session->Name,
                        .Number = session->PlayerNumber,
                        .Host = session->Id == manager->get().ServerID,
                        .Me = session->Id == manager->get().MyPlayerID};

        if (const std::optional<int8_t> slot = SlotOf(Team1Players, player.Id); slot.has_value())
        {
            player.Team = Team1;
            player.Slot = *slot;
        }
        else if (const std::optional<int8_t> slot2 = SlotOf(Team2Players, player.Id); slot2.has_value())
        {
            player.Team = Team2;
            player.Slot = *slot2;
        }

        players.push_back(std::move(player));
    }

    return players;
}

auto MCRoomModel::Hosting() const -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr && game->IsHost != 0;
}

auto MCRoomModel::HostName() const -> std::string
{
    const MCOptionalRef<MCSessionManager> manager = Manager();

    if (!manager.has_value())
    {
        return {};
    }

    const std::shared_ptr<MCFidpPlayer> host = manager->get().GetPlayer(manager->get().ServerID);
    return host != nullptr ? host->Name : std::string();
}

auto MCRoomModel::Name() const -> std::string
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    const std::shared_ptr<MCFidpSession> session = manager.has_value() ? manager->get().CurrentSession() : nullptr;
    return session != nullptr ? session->Name : std::string();
}

auto MCRoomModel::Most() const -> int32_t
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    const std::shared_ptr<MCFidpSession> session = manager.has_value() ? manager->get().CurrentSession() : nullptr;
    return session != nullptr ? static_cast<int32_t>(session->SessionDesc.dwMaxPlayers)
                              : static_cast<int32_t>(MaxPlayers);
}

auto MCRoomModel::Where() const -> MCWhere
{
    if (LaunchedFromLobby)
    {
        return MCWhere::Launcher;
    }

    const MCOptionalRef<MCSessionManager> manager = Manager();
    return manager.has_value() && manager->get().Internet != nullptr ? MCWhere::Internet : MCWhere::Lan;
}

auto MCRoomModel::Public() const -> bool
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    return manager.has_value() && manager->get().Internet != nullptr && manager->get().Internet->Public();
}

auto MCRoomModel::Code() const -> std::string
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    return manager.has_value() && manager->get().Internet != nullptr ? manager->get().Internet->Code() : std::string();
}

auto MCRoomModel::Points(int8_t team) const -> int32_t
{
    return team == Team1 ? Team1Points : team == Team2 ? Team2Points : 0;
}

auto MCRoomModel::TechBase(int8_t team) const -> int8_t
{
    return team == Team1 ? Team1TechBase : team == Team2 ? Team2TechBase : InnerSphere;
}

auto MCRoomModel::Assign(uint32_t playerId, int8_t team, int8_t slot) -> void
{
    if (const MCOptionalRef<MCSessionScreen> screen = Screen(); screen.has_value() && CanChangeTeams())
    {
        screen->get().AssignPlayer(playerId, IsTeam(team) ? team : NoTeam, IsTeam(team) ? slot : 0, false);
    }
}

auto MCRoomModel::JoinTeam(uint32_t playerId, int8_t team) -> void
{
    if (!IsTeam(team))
    {
        Assign(playerId, NoTeam, 0);
        return;
    }

    const std::array<uint32_t, TeamSlots>& players = team == Team1 ? Team1Players : Team2Players;

    // Already on it: it stays where it is.
    if (SlotOf(players, playerId).has_value())
    {
        return;
    }

    if (const std::optional<int8_t> open = SlotOf(players, NoPlayer); open.has_value())
    {
        Assign(playerId, team, *open);
    }
}

auto MCRoomModel::SetTechBase(int8_t team, int8_t techBase) -> void
{
    if (const MCOptionalRef<MCSessionScreen> screen = Screen();
        screen.has_value() && CanChangeForces() && IsTeam(team) && (techBase == InnerSphere || techBase == Clan))
    {
        screen->get().PickTechBase(team, techBase);
    }
}

auto MCRoomModel::SetPoints(int8_t team, int32_t points) -> void
{
    const MCOptionalRef<MCSessionScreen> screen = Screen();

    if (!screen.has_value() || !CanChangeForces())
    {
        return;
    }

    if (team == Team1)
    {
        screen->get().SetTeam1RP(std::max(points, 0));
    }
    else if (team == Team2)
    {
        screen->get().SetTeam2RP(std::max(points, 0));
    }
}

auto MCRoomModel::ChooseMission(std::string_view mission) -> void
{
    if (const MCOptionalRef<MCSessionScreen> screen = Screen(); screen.has_value() && CanChangeTeams())
    {
        screen->get().LoadMission(mission);
    }
}

auto MCRoomModel::BeginPlanning() -> void
{
    if (const MCOptionalRef<MCSessionScreen> screen = Screen(); screen.has_value() && Hosting() && CanStart)
    {
        screen->get().Begin();
    }
}
