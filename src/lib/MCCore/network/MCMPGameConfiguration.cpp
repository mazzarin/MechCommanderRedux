#include "stdafx.h"
#include "network/MCMPGameConfiguration.h"
#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"

MPGameConfiguration GameConfiguration;

namespace
{
    /// <summary>How long the host waits for every guest to say it has the mission file.</summary>
    constexpr uint32_t FileReportTimeoutMs = 15000;

    /// <summary>What a mission file says about its own settings.</summary>
    struct MissionInfo
    {
        int32_t Team1Points = 0;
        int32_t Team2Points = 0;
        /// <summary>A locked mission fixes the tech bases (team 1 Inner Sphere, team 2 Clan).</summary>
        bool Locked = false;
    };

    /// <summary>
    /// Reads the settings the session screen's <c>LoadMission</c> reads from a mission file, without the screen and
    /// without asserting: a file that can't be read is the caller's to report.
    /// </summary>
    std::optional<MissionInfo> ReadMissionInfo(const std::string& path)
    {
        MCPacketFile packetFile;
        MCFitIniFile iniFile;

        if (packetFile.Open(path) != 0 || packetFile.SeekPacket(0) != 0 ||
            iniFile.Open(packetFile, packetFile.GetPacketSize()) != 0 || iniFile.SeekBlock("Multiplayer") != 0 ||
            iniFile.SeekBlock("ResourcePoints") != 0)
        {
            return std::nullopt;
        }

        MissionInfo info;
        uint32_t value = 0;
        const bool team1Missing = iniFile.ReadIdULong("Team1Points", value) != 0;

        if (!team1Missing)
        {
            info.Team1Points = static_cast<int32_t>(value);
        }

        const bool team2Missing = iniFile.ReadIdULong("Team2Points", value) != 0;

        if (!team2Missing)
        {
            info.Team2Points = static_cast<int32_t>(value);
        }

        if (team1Missing || team2Missing)
        {
            // An older file: one figure for both teams.
            uint32_t both = 0;

            if (iniFile.ReadIdULong("numPoints", both) != 0)
            {
                both = 0;
            }

            info.Team1Points = team1Missing ? static_cast<int32_t>(both) : info.Team1Points;
            info.Team2Points = team2Missing ? static_cast<int32_t>(both) : info.Team2Points;
        }

        info.Locked = iniFile.SeekBlock("Lock") == 0;
        iniFile.Close();
        return info;
    }

    /// <summary>Where the host's side of a configured match is.</summary>
    enum class Step
    {
        Idle,
        AwaitingFiles,
        /// <summary>Started, or gave up: either way the host is not asked again.</summary>
        Finished
    };

    struct FileState
    {
        uint32_t PlayerId = 0;
        std::string Name;
        /// <summary>0 waiting, 1 has the file, 2 does not.</summary>
        int8_t State = 0;
    };

    struct HostMatch
    {
        Step Current = Step::Idle;
        uint32_t Deadline = 0;
        /// <summary>The mission file as the guests are asked for it (the same string the session screen sends).</summary>
        std::string Path;
        MPMatchState Match;
        std::vector<FileState> Files;
    };

    HostMatch Pending;

    void GiveUp(std::string_view why)
    {
        SDL_Log("Match: %.*s; the match is left to the host", static_cast<int>(why.size()), why.data());
        Pending.Current = Step::Finished;
        Pending.Files.clear();
    }

    /// <summary>The player ids of the session, ascending (the order the ready lights use).</summary>
    std::vector<uint32_t> SortedSessionIds()
    {
        std::vector<uint32_t> ids;

        for (const auto& player : MultiPlayer()->SessionManager->GetPlayers(nullptr))
        {
            ids.push_back(player->Id);
        }

        std::ranges::sort(ids);
        return ids;
    }

    /// <summary>
    /// Puts the players in their team groups and tells this machine its own side: what the session screen's
    /// <c>AssignPlayer</c> does on every machine as the host's join-team messages arrive.
    /// </summary>
    void ApplyTeams(const MPMatchState& match)
    {
        MCMultiPlayer& game = *MultiPlayer();
        MCSessionManager& manager = *game.SessionManager;
        const uint32_t localId = manager.MyPlayer()->Id;

        for (int32_t team = 1; team <= 2; ++team)
        {
            const auto& players = team == 1 ? match.Team1Players : match.Team2Players;
            const uint32_t groupId = team == 1 ? game.InnerSphereGroupID : game.ClanGroupID;

            for (const uint32_t id : players)
            {
                if (id == MPMatchState::NoPlayer)
                {
                    continue;
                }

                manager.AddPlayerToGroup(groupId, id);

                if (id == localId)
                {
                    game.HomeTeam = team == 1 ? 0 : 1;
                    game.HomeTeamGroupID = groupId;
                    game.EnemyTeamGroupID = team == 1 ? game.ClanGroupID : game.InnerSphereGroupID;
                }
            }
        }
    }

    /// <summary>
    /// The one way into the mech bay for a configured match, host and guest alike: the tail of the session screen's
    /// start, without the session screen.
    /// </summary>
    void EnterMechBay(const std::string& mission, const MPMatchState& match)
    {
        MCMultiPlayer& game = *MultiPlayer();
        game.Match = match;
        ApplyTeams(match);
        GlobalLog()->SetUpPlayerLights(SortedSessionIds());
        game.SessionManager->SendLatencyInfo();
        SoundSystem()->PlayBettySample(0x19);
        GlobalLog()->InitializeMultiplayer();
        GlobalLog()->LoadCampaign(mission, ".MPK", false, false);
        GlobalLog()->SetUpBriefingScreen(false);
    }

    /// <summary>Every guest has the file: lock the session, tell the guests and go to the mech bay.</summary>
    void StartMatch()
    {
        MCMultiPlayer& game = *MultiPlayer();
        game.SessionManager->LockSession();
        const MPMatchState& match = Pending.Match;

        MCMPStartMatchMessage header{};
        header.Tagger.Clear();
        header.Header = GuaranteedHeader(MCMPMessageType::StartMatch);
        header.Team1Points = match.Team1Points;
        header.Team2Points = match.Team2Points;
        header.Team1TechBase = match.Team1TechBase;
        header.Team2TechBase = match.Team2TechBase;

        for (int32_t team = 1; team <= 2; ++team)
        {
            const auto& players = team == 1 ? match.Team1Players : match.Team2Players;

            for (size_t slot = 0; slot < players.size(); ++slot)
            {
                if (players[slot] != MPMatchState::NoPlayer)
                {
                    header.Players[header.NumPlayers++] = {players[slot], static_cast<int8_t>(team),
                                                           static_cast<int8_t>(slot)};
                }
            }
        }

        const std::string& mission = GameConfiguration.Mission;
        std::vector<uint8_t> buffer(sizeof(header) + mission.size() + 1, 0);
        std::memcpy(buffer.data(), &header, sizeof(header));
        std::ranges::copy(mission, buffer.begin() + sizeof(header));
        game.SessionManager->SendMessageToGroup(0, buffer);

        Pending.Current = Step::Finished;
        Pending.Files.clear();
        EnterMechBay(mission, match);
    }
}

bool MPGameConfiguration::Validate(std::string& why) const
{
    if (Mission.empty() || Mission.find_first_of("\\/:") != std::string::npos)
    {
        why = "needs a mission's bare name";
        return false;
    }

    if (Roster.empty() || Roster.size() > MCMPStartMatchMessage::MaxMatchPlayers)
    {
        why = "needs 1-6 -slot entries";
        return false;
    }

    std::set<std::pair<int, int>> taken;
    std::set<std::string> names;
    std::array<int, 2> perTeam{};

    for (const MPGamePlayer& player : Roster)
    {
        if ((player.Team != 1 && player.Team != 2) || player.Slot < 0 ||
            static_cast<size_t>(player.Slot) >= MPMatchState::TeamSlots ||
            !taken.insert({player.Team, player.Slot}).second || !names.insert(player.Name).second)
        {
            why = "a -slot entry has a bad team or slot, or repeats a name or slot";
            return false;
        }

        ++perTeam[static_cast<size_t>(player.Team - 1)];
    }

    if (perTeam[0] == 0 || perTeam[1] == 0)
    {
        why = "needs a player on each team";
        return false;
    }

    return true;
}

std::vector<uint32_t> MPMatchState::TeamPlayerIds(bool myTeam, uint32_t localId) const
{
    // Team 2 when the local player is on it and myTeam is set, or when it isn't and myTeam is clear.
    const bool onTeam2 = std::ranges::find(Team2Players, localId) != Team2Players.end();
    const auto& players = myTeam == onTeam2 ? Team2Players : Team1Players;
    std::vector<uint32_t> ids;

    for (const uint32_t id : players)
    {
        if (id != NoPlayer)
        {
            ids.push_back(id);
        }
    }

    return ids;
}

void MPBeginConfiguredMatch()
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (!GameConfiguration.Active || Pending.Current != Step::Idle || game == nullptr || !game->IsHost ||
        game->SessionManager == nullptr)
    {
        return;
    }

    MCSessionManager& manager = *game->SessionManager;
    const auto& players = manager.GetPlayers(nullptr);
    MPMatchState match;

    // Each rostered player's id, by the name it joined with; the session must hold exactly the roster (a player on no
    // team would be left out of the mech bay).
    for (const MPGamePlayer& wanted : GameConfiguration.Roster)
    {
        const auto found =
            std::ranges::find_if(players, [&wanted](const auto& player) { return player->Name == wanted.Name; });

        if (found == players.end())
        {
            GiveUp(std::format("{} is not in the session", wanted.Name));
            return;
        }

        (wanted.Team == 1 ? match.Team1Players : match.Team2Players)[static_cast<size_t>(wanted.Slot)] = (*found)->Id;
    }

    if (players.size() != GameConfiguration.Roster.size())
    {
        GiveUp("the session has players the roster does not name");
        return;
    }

    // The host's own file first: a missing one is fatal to the mission loader, and here it is only a message.
    Pending.Path = GamePath(SavePath, GameConfiguration.Mission, ".mpk");

    if (!FileExists(Pending.Path))
    {
        GiveUp(std::format("the mission file {} is not on the host", Pending.Path));
        return;
    }

    const std::optional<MissionInfo> info = ReadMissionInfo(Pending.Path);

    if (!info.has_value())
    {
        GiveUp(std::format("the mission file {} can't be read", Pending.Path));
        return;
    }

    // The mission's points are a team's pool, shared out among its players when the mission loads, so a figure for
    // each player is multiplied up by the team's size. A locked mission keeps its own points and tech bases.
    auto teamSize = [](const std::array<uint32_t, MPMatchState::TeamSlots>& team)
    {
        return static_cast<int32_t>(
            std::ranges::count_if(team, [](uint32_t id) { return id != MPMatchState::NoPlayer; }));
    };

    match.Team1Points = info->Team1Points;
    match.Team2Points = info->Team2Points;
    match.Team1TechBase = 1;
    match.Team2TechBase = -1;

    if (info->Locked)
    {
        SDL_Log("Match: %s is a locked mission; its own points and tech bases are kept",
                GameConfiguration.Mission.c_str());
    }
    else
    {
        if (GameConfiguration.Team1RpPerPlayer >= 0)
        {
            match.Team1Points = GameConfiguration.Team1RpPerPlayer * teamSize(match.Team1Players);
        }

        if (GameConfiguration.Team2RpPerPlayer >= 0)
        {
            match.Team2Points = GameConfiguration.Team2RpPerPlayer * teamSize(match.Team2Players);
        }

        if (GameConfiguration.Team1TechBase != 0)
        {
            match.Team1TechBase = GameConfiguration.Team1TechBase;
        }

        if (GameConfiguration.Team2TechBase != 0)
        {
            match.Team2TechBase = GameConfiguration.Team2TechBase;
        }
    }

    Pending.Match = match;
    Pending.Files.clear();

    // Every other machine says whether it has the file (the server and the host have it already).
    for (const auto& player : players)
    {
        if (player->Id != manager.MyPlayer()->Id && player->Id != manager.ServerID)
        {
            Pending.Files.push_back({player->Id, player->Name, 0});
        }
    }

    if (Pending.Files.empty())
    {
        StartMatch();
        return;
    }

    Pending.Current = Step::AwaitingFiles;
    Pending.Deadline = MCPort::Milliseconds() + FileReportTimeoutMs;
    game->SendFileInquiry(Pending.Path);
}

void MPPollConfiguredMatch()
{
    if (Pending.Current == Step::AwaitingFiles && MCPort::Milliseconds() > Pending.Deadline)
    {
        GiveUp("a guest never said whether it has the mission file");
    }
}

bool MPConfiguredMatchFileReport(uint32_t playerId, bool haveFile)
{
    if (Pending.Current != Step::AwaitingFiles)
    {
        return false;
    }

    for (FileState& file : Pending.Files)
    {
        if (file.PlayerId == playerId)
        {
            file.State = haveFile ? 1 : 2;
        }
    }

    std::string missing;
    bool waiting = false;

    for (const FileState& file : Pending.Files)
    {
        waiting = waiting || file.State == 0;

        if (file.State == 2)
        {
            missing += (missing.empty() ? "" : ", ") + file.Name;
        }
    }

    if (!missing.empty())
    {
        GiveUp(std::format("{} do not have the mission file", missing));
    }
    else if (!waiting)
    {
        StartMatch();
    }

    return true;
}

void MPHandleStartMatch(uint32_t fromId, std::span<const uint8_t> message)
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    auto drop = [fromId](std::string_view why)
    { SDL_Log("Match: dropped a start-match from player %u: %.*s", fromId, static_cast<int>(why.size()), why.data()); };

    if (game == nullptr || game->SessionManager == nullptr || GlobalLog() == nullptr)
    {
        return;
    }

    // The host sends it once, to players still waiting; one already in logistics or a mission has no use for it.
    if (game->IsHost || game->InLogistics || game->InMission)
    {
        drop("not a guest waiting to start");
        return;
    }

    if (message.size() < sizeof(MCMPStartMatchMessage))
    {
        drop("too short");
        return;
    }

    const auto start = ReadLinkupMessage<MCMPStartMatchMessage>(message);
    const std::string mission(MessageText(message, sizeof(MCMPStartMatchMessage)));

    // The host sends the mission's bare name (LoadCampaign adds the folder and extension), as the session screen's
    // start does.
    if (mission.empty() || !MCFileSystem::IsContainedPath(mission) || mission.find_first_of("\\/") != std::string::npos)
    {
        drop("a mission outside the game's folders");
        return;
    }

    if (start.NumPlayers == 0 || start.NumPlayers > MCMPStartMatchMessage::MaxMatchPlayers ||
        (start.Team1TechBase != 1 && start.Team1TechBase != -1) ||
        (start.Team2TechBase != 1 && start.Team2TechBase != -1) || start.Team1Points < 0 || start.Team2Points < 0)
    {
        drop("bad settings");
        return;
    }

    MPMatchState match;
    match.Team1Points = start.Team1Points;
    match.Team2Points = start.Team2Points;
    match.Team1TechBase = start.Team1TechBase;
    match.Team2TechBase = start.Team2TechBase;
    const uint32_t localId = game->SessionManager->MyPlayer()->Id;
    bool localPlaced = false;

    for (size_t index = 0; index < start.NumPlayers; ++index)
    {
        const MCMPMatchPlayer& player = start.Players[index];
        auto& team = player.Team == 1 ? match.Team1Players : match.Team2Players;

        if ((player.Team != 1 && player.Team != 2) || player.Slot < 0 ||
            static_cast<size_t>(player.Slot) >= MPMatchState::TeamSlots ||
            team[static_cast<size_t>(player.Slot)] != MPMatchState::NoPlayer ||
            game->SessionManager->GetPlayer(player.PlayerID) == nullptr)
        {
            drop("a bad or repeated player, team or slot");
            return;
        }

        team[static_cast<size_t>(player.Slot)] = player.PlayerID;
        localPlaced = localPlaced || player.PlayerID == localId;
    }

    if (!localPlaced)
    {
        drop("this player is on no team");
        return;
    }

    EnterMechBay(mission, match);
}
