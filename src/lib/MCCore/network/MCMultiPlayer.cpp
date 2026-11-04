#include "stdafx.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameContext.h"
#include "main/MCGameStrings.h"
#include "main/MCTelemetry.h"
#include "main/MCLogistics.h"
#include "main/MCSystemConfig.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "object/MCGameObject.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCTurret.h"

// The multiplayer game's life, its rosters and chunk queues, its session, and the server hand-over. The sends are in
// MCMultiPlayerSend.cpp, the handlers of what arrives in MCMultiPlayerHandlers.cpp.

bool IsMPlayerGame = false;
int32_t BadSessionCounter = 0;
int32_t NumLanPlayers = 1;
MCNetProtocol LastConnectionType = MCNetProtocol::None;
float WarpFactor = 1.0f;

namespace
{
    /// <summary>The names of the three groups the server makes.</summary>
    constexpr std::string_view AllPlayerGroupName = "AllPlayerGroup";
    constexpr std::string_view InnerSphereGroupName = "InnerSphereGroup";
    constexpr std::string_view ClanGroupName = "ClanGroup";

    /// <summary>
    /// Reads <paramref name="name"/> as the original's ReadId calls did: the value on success, 0 when the entry is
    /// missing, unchanged otherwise.
    /// </summary>
    /// <returns>Whether it was read.</returns>
    template <MCFitValue T> bool ReadEntry(MCFitIniFile& file, std::string_view name, T& value)
    {
        const MCFitResult<T> result = file.Read<T>(name);

        if (result.has_value())
        {
            value = *result;
            return true;
        }

        if (result.error() == MCFitError::VariableNotFound)
        {
            value = T{};
        }

        return false;
    }

    /// <summary>
    /// A machine that just became the server takes over the movers: each gets the server's control (SetControl 2) and
    /// its pilot's current order is cleared.
    /// </summary>
    /// <remarks>Inline in the original (SetServer and PlayerLeftGame).</remarks>
    void TakeOverMovers(MCMultiPlayer& multiPlayer)
    {
        for (int32_t i = 0; i < multiPlayer.NumMovers; i++)
        {
            const std::shared_ptr<MCMover> mover = LockObject(multiPlayer.MoverRoster[i]);

            if (mover == nullptr)
            {
                continue;
            }

            if (const int32_t result = mover->SetControl(2, 0xffffffff, -1); result != 0)
            {
                Fatal(result, " MPlayer.setServer: unable to set control ");
            }

            if (std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot(); pilot != nullptr)
            {
                pilot->ClearCurTacOrder(0, 0);
                pilot->OrderState = MCOrderState::General;
            }
        }
    }

    /// <summary>The session's players with a number, but for this machine's.</summary>
    std::vector<uint32_t> OtherPlayers(MCSessionManager& manager)
    {
        const std::shared_ptr<MCFidpPlayer> me = manager.MyPlayer();
        std::vector<uint32_t> players;

        for (const auto& player : manager.GetPlayers(nullptr))
        {
            if (player->HasPlayerNumber && player != me)
            {
                players.push_back(player->Id);
            }
        }

        return players;
    }
}

std::shared_ptr<MCMultiPlayer> MultiPlayer()
{
    return MCGameContext::Current().MultiPlayer();
}

// ---- lifetime ------------------------------------------------------------------------------------------------------

MCMultiPlayer::MCMultiPlayer()
{
    if (GlobalLog() != nullptr)
    {
        GlobalLog()->CurrentMission = -1;
    }

    BadSessionCounter = 0;
    ChatCallback = HandleAppChat;
    InitStartupParameters();
    IsMPlayerGame = false;
}

MCMultiPlayer::~MCMultiPlayer() = default;

int32_t MCMultiPlayer::Start()
{
    if (SessionManager == nullptr)
    {
        SessionManager = std::make_unique<MCSessionManager>(MultiPlayerAppGuid);
    }

    // The original also set the session manager's home directory (_getcwd) and installed empty file-sent and
    // file-received handlers; the port has no file transfers.
    SessionManager->ApplicationCallback = MultiPlayerApplicationCallback;
    SessionManager->SystemCallback = MultiPlayerSystemCallback;
    MsgBuffer.fill(0);
    return 0;
}

void MCMultiPlayer::InitUpdateFrequencies()
{
    MoverUpdateFrequency = -1.0f;
    TurretUpdateFrequency = -1.0f;
    WorldStateUpdateFrequency = -1.0f;
    MCFitIniFile prefsFile;
    Assert(prefsFile.Open("prefs.cfg") == 0, 0, "Could not open prefs.cfg");

    if (prefsFile.SeekBlock("Multiplayer") == 0)
    {
        for (const auto& [name, frequency] : {std::pair{"MoverUpdateFrequency", &MoverUpdateFrequency},
                                              std::pair{"TurretUpdateFrequency", &TurretUpdateFrequency},
                                              std::pair{"WorldStateUpdateFrequency", &WorldStateUpdateFrequency}})
        {
            if (!ReadEntry(prefsFile, name, *frequency))
            {
                *frequency = -1.0f;
            }
        }
    }

    prefsFile.Close();

    // A period outside 0-5 seconds (or missing) takes the default: shorter ones on a LAN (IPX or TCP/IP) outside a
    // lobby.
    const bool lan = SessionManager->CurrentConnection == MCNetProtocol::Ipx ||
                     SessionManager->CurrentConnection == MCNetProtocol::TcpIp;
    const bool fast = lan && !LaunchedFromLobby;
    auto settle = [](float& frequency, float fallback)
    {
        if (frequency < 0.0f || frequency > 5.0f)
        {
            frequency = fallback;
        }
    };

    settle(MoverUpdateFrequency, fast ? 0.2f : 0.33f);
    settle(WorldStateUpdateFrequency, fast ? 0.33f : 0.75f);
    settle(TurretUpdateFrequency, fast ? 0.5f : 1.0f);
    BroadcastFrequencies = {MoverUpdateFrequency, TurretUpdateFrequency, WorldStateUpdateFrequency};
}

int32_t MCMultiPlayer::StartScriptedGame(MCFitIniFile& file)
{
    Assert(ReadEntry(file, "Server", IsServer), 0, " could not find Multiplayer:Server ");

    if (!LaunchedFromLobby)
    {
        Assert(ReadEntry(file, "NumPlayers", NumLanPlayers), 0, " could not find Multiplayer:NumPlayers ");
    }

    if (!StartupPakFile.empty() || !ReadEntry(file, "CheckInId", CheckInId))
    {
        CheckInId = IsServer ? 0 : -1;
    }

    Assert(ReadEntry(file, "HomeTeam", HomeTeam), 0, " could not find Multiplayer:HomeTeam ");
    const uint32_t connectResult = SessionManager->SetupLobbyConnection(nullptr, nullptr);
    auto readFrequencies = [&]
    {
        Assert(ReadEntry(file, "MoverUpdateFrequency", MoverUpdateFrequency), 0,
               " could not find Multiplayer:MoverUpdateFrequency ");
        Assert(ReadEntry(file, "WorldStateUpdateFrequency", WorldStateUpdateFrequency), 0,
               " could not find Multiplayer:WorldStateUpdateFrequency ");
    };

    if (connectResult == 0)
    {
        IsServer = SessionManager->IsHost;

        if (IsServer)
        {
            PlayerCheckedIn[CheckInId] = static_cast<int32_t>(SessionManager->MyPlayer()->Id);
            SessionManager->CreateGroup(AllPlayerGroupID, AllPlayerGroupName, {}, 0);
            SessionManager->CreateGroup(InnerSphereGroupID, InnerSphereGroupName, {}, 0);
            SessionManager->CreateGroup(ClanGroupID, ClanGroupName, {}, 0);
            readFrequencies();
        }
    }
    else if (connectResult == DPERR_NOTLOBBIED)
    {
        if (IsServer)
        {
            readFrequencies();
        }

        uint32_t protocol = 0;
        Assert(ReadEntry(file, "Protocol", protocol), 0, " could not find protocol in Multiplayer info file");
        Assert(ReadEntry(file, "SessionName", SessionName), 0, " could not find Multiplayer:SessionName ");
        Assert(ReadEntry(file, "PlayerName", PlayerName), 0, " could not find Multiplayer:PlayerName ");
        Assert((SessionManager->AvailableProtocols & protocol) != 0, 0,
               "Connection protocol specified is not available");
        const int32_t result = SessionManager->SetCurrentConnection(static_cast<MCNetProtocol>(protocol));
        Assert(result == 0, 0, "Could not connect.");
    }

    return static_cast<int32_t>(connectResult);
}

int32_t MCMultiPlayer::NumPlayers() const
{
    if (LaunchedFromLobby)
    {
        return static_cast<int32_t>(SessionManager->GetPlayers(nullptr).size());
    }

    return NumLanPlayers;
}

int32_t MCMultiPlayer::SetupLobbyGame()
{
    const uint32_t result = SessionManager->SetupLobbyConnection(ShowConnectStatus, DestroyConnectStatusWindow);

    if (result == 0)
    {
        IsMPlayerGame = true;
        IsServer = SessionManager->IsHost;
        IsHost = IsServer;

        if (SessionManager->MyPlayer() != nullptr)
        {
            if (IsServer)
            {
                ServerID = SessionManager->MyPlayer()->Id;
                HostID = ServerID;
            }
            else
            {
                ServerID = SessionManager->ServerID;
            }
        }

        if (SessionManager->CurrentConnection != MCNetProtocol::Lobby)
        {
            NumLanPlayers = static_cast<int32_t>(SessionManager->GetPlayers(nullptr).size());
        }

        if (IsServer)
        {
            SessionManager->CreateGroup(AllPlayerGroupID, AllPlayerGroupName, {}, 0);
            SessionManager->CreateGroup(InnerSphereGroupID, InnerSphereGroupName, {}, 0);
            SessionManager->CreateGroup(ClanGroupID, ClanGroupName, {}, 0);

            if (HomeTeam == 0)
            {
                SessionManager->AddPlayerToGroup(InnerSphereGroupID, 0);
                HomeTeamGroupID = InnerSphereGroupID;
                EnemyTeamGroupID = ClanGroupID;
            }
            else if (HomeTeam == 1)
            {
                SessionManager->AddPlayerToGroup(ClanGroupID, 0);
                HomeTeamGroupID = ClanGroupID;
                EnemyTeamGroupID = InnerSphereGroupID;
            }

            if (CheckInId == -1)
            {
                CheckInId = 0;
            }

            InitUpdateFrequencies();
            HandleOwnMessages = true;
            SendPlayerSetup(0, InnerSphereGroupID, ClanGroupID);
            HandleOwnMessages = false;
        }
    }

    return static_cast<int32_t>(result);
}

// ---- rosters -------------------------------------------------------------------------------------------------------

void MCMultiPlayer::AddToLocalMovers(std::shared_ptr<MCMover> mover)
{
    if (NumLocalMovers == MaxLocalMovers)
    {
        Fatal(0, " Too many local movers for network ");
    }

    LocalMovers[NumLocalMovers] = mover;
    mover->NetPlayerId = NumLocalMovers;
    NumLocalMovers++;
}

void MCMultiPlayer::AddToMoverRoster(std::shared_ptr<MCMover> mover)
{
    if (NumMovers == MaxMovers)
    {
        Fatal(0, " Too many movers for multiplay ");
    }

    MoverRoster[NumMovers] = mover;
    mover->NetRosterIndex = NumMovers;
    NumMovers++;
}

void MCMultiPlayer::AddToPlayerMoverRoster(int32_t playerNumber, std::shared_ptr<MCMover> mover)
{
    std::array<std::weak_ptr<MCMover>, MaxLocalMovers>& roster = PlayerMoverRoster[playerNumber];
    const auto free =
        std::ranges::find_if(roster, [](const std::weak_ptr<MCMover>& slot) { return LockObject(slot) == nullptr; });
    Assert(free != roster.end(), 0, " MultiPlayer.addToPlayerMoverRoster: Too many local movers ");

    if (free != roster.end())
    {
        *free = mover;
    }
}

void MCMultiPlayer::AddToTurretRoster(std::shared_ptr<MCTurret> turret)
{
    if (NumTurrets == MaxTurrets)
    {
        Fatal(0, " Too many turrets for multiplay ");
    }

    TurretRoster[NumTurrets] = turret;
    turret->NetRosterIndex = NumTurrets;
    NumTurrets++;
}

std::shared_ptr<MCMover> MCMultiPlayer::RosterMover(int32_t index) const
{
    return index >= 0 && index < NumMovers ? LockObject(MoverRoster[static_cast<size_t>(index)]) : nullptr;
}

std::shared_ptr<MCTurret> MCMultiPlayer::RosterTurret(int32_t index) const
{
    return index >= 0 && index < NumTurrets ? LockObject(TurretRoster[static_cast<size_t>(index)]) : nullptr;
}

// ---- chunk queues --------------------------------------------------------------------------------------------------

int32_t MCMultiPlayer::AddWorldStateChunk(MCWorldStateChunk& chunk)
{
    chunk.Pack();
    MCWorldStateChunk check;
    check.Data = chunk.Data;
    check.Unpack();

    if (!chunk.EqualTo(check))
    {
        Fatal(0, " MultiPlayer.addWorldStateChunk: WorldState Chunks don't match ");
    }

    WorldStateChunkTally[chunk.Type]++;
    WorldStateChunks.push_back(chunk.Data);
    return static_cast<int32_t>(WorldStateChunks.size());
}

int32_t MCMultiPlayer::AddMissionScriptMessageChunk(int32_t message, int32_t value)
{
    MCWorldStateChunk chunk;
    chunk.BuildMissionScriptMessage(message, value);
    return AddWorldStateChunk(chunk);
}

int32_t MCMultiPlayer::AddArtilleryChunk(int32_t commanderId, int32_t strikeType, const MCVector3D& location,
                                         int32_t seconds)
{
    MCWorldStateChunk chunk;
    chunk.BuildArtillery(commanderId, strikeType, location, seconds);
    return AddWorldStateChunk(chunk);
}

int32_t MCMultiPlayer::AddMineChunk(int32_t tileRow, int32_t tileCol, int32_t teamId, int32_t mineState,
                                    int32_t explosionType)
{
    MCWorldStateChunk chunk;
    chunk.BuildMine(tileRow, tileCol, teamId, mineState, explosionType);
    return AddWorldStateChunk(chunk);
}

int32_t MCMultiPlayer::AddLightOnFireChunk(MCGameObject& object, int32_t seconds)
{
    MCWorldStateChunk chunk;

    if (object.GetObjectType() != nullptr)
    {
        switch (object.ObjectClass)
        {
            case MCObjectClass::Building:
            case MCObjectClass::Tree:
            case MCObjectClass::MiscTerrainObject:
            case MCObjectClass::TreeBuilding:
            {
                chunk.BuildTerrainFire(object, seconds);
                break;
            }

            default:
            {
                Fatal(0, " MultiPlayer.addLightOnFireChunk: bad fire victim ");
            }
        }
    }

    // A victim without a type queues the empty chunk (a mine at cell -1, -1).
    return AddWorldStateChunk(chunk);
}

int32_t MCMultiPlayer::AddPilotKillStat(MCMover& mover, int32_t killType)
{
    MCWorldStateChunk chunk;
    chunk.BuildPilotKillStat(mover.NetRosterIndex, killType, NumMovers);
    return AddWorldStateChunk(chunk);
}

int32_t MCMultiPlayer::AddWeaponHitChunk(MCWeaponHitChunk& chunk)
{
    chunk.Pack();
    MCWeaponHitChunk check = EmptyWeaponHitChunk();
    check.Data = chunk.Data;
    check.Unpack();

    if (chunk.EqualTo(check) == 0)
    {
        Fatal(0, " MultiPlayer.addWeaponHitChunk: WeaponHit chunks don't match (save whchunk.dbg file) ");
    }

    WeaponHitChunks.push_back(chunk.Data);
    return static_cast<int32_t>(WeaponHitChunks.size());
}

int32_t MCMultiPlayer::AddWeaponHitChunk(MCGameObject& target, MCWeaponShotInfo& shotInfo, int hitFlag)
{
    MCWeaponHitChunk chunk = EmptyWeaponHitChunk();
    chunk.Build(target, shotInfo, hitFlag);
    return AddWeaponHitChunk(chunk);
}

size_t MCMultiPlayer::GrabWeaponHitChunks(std::span<uint32_t> chunks)
{
    const size_t count = std::min(chunks.size(), WeaponHitChunks.size());
    std::copy_n(WeaponHitChunks.begin(), count, chunks.begin());
    WeaponHitChunks.erase(WeaponHitChunks.begin(), WeaponHitChunks.begin() + static_cast<ptrdiff_t>(count));
    return count;
}

// ---- the session ---------------------------------------------------------------------------------------------------

int32_t MCMultiPlayer::ConnectIpx() const
{
    if (SessionManager == nullptr)
    {
        return -1;
    }

    SessionManager->SetCurrentConnection(MCNetProtocol::Ipx);
    return 0;
}

int32_t MCMultiPlayer::CreateSession(int32_t maxPlayers)
{
    return CreateSession(std::string(SessionName), std::string(PlayerName), maxPlayers);
}

int32_t MCMultiPlayer::CreateSession(std::string_view sessionName, std::string_view playerName, int32_t maxPlayers)
{
    if (SessionManager == nullptr)
    {
        return -1;
    }

    MCFidpSession session(MultiPlayerAppGuid);
    session.SetName(sessionName);
    session.SessionDesc.dwMaxPlayers = static_cast<uint32_t>(maxPlayers);

    if (SessionManager->HostSession(session, playerName) != 0)
    {
        return -1;
    }

    IsServer = true;
    IsHost = true;
    ServerID = SessionManager->MyPlayer()->Id;
    HostID = ServerID;
    SessionManager->CreateGroup(AllPlayerGroupID, AllPlayerGroupName, {}, 0);
    SessionManager->CreateGroup(InnerSphereGroupID, InnerSphereGroupName, {}, 0);
    SessionManager->CreateGroup(ClanGroupID, ClanGroupName, {}, 0);

    if (HomeTeam == 0)
    {
        SessionManager->AddPlayerToGroup(InnerSphereGroupID, 0);
        HomeTeamGroupID = InnerSphereGroupID;
        EnemyTeamGroupID = ClanGroupID;
    }
    else if (HomeTeam == 1)
    {
        SessionManager->AddPlayerToGroup(ClanGroupID, 0);
        HomeTeamGroupID = ClanGroupID;
        EnemyTeamGroupID = InnerSphereGroupID;
    }

    if (CheckInId == -1)
    {
        CheckInId = 0;
    }

    InitUpdateFrequencies();
    return 0;
}

int32_t MCMultiPlayer::JoinSession()
{
    return JoinSession(std::string(SessionName), std::string(PlayerName));
}

int32_t MCMultiPlayer::JoinSession(std::string_view sessionName, std::string_view playerName)
{
    if (SessionManager == nullptr)
    {
        return -1;
    }

    const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> sessions = SessionManager->GetSessions();

    if (!sessions.has_value() || sessions->empty())
    {
        return -1;
    }

    for (const auto& session : *sessions)
    {
        if (session->Name == sessionName)
        {
            if (SessionManager->JoinSession(session->SessionDesc.guidInstance, playerName) != 0)
            {
                return -2;
            }

            ServerID = SessionManager->ServerID;
            return 0;
        }
    }

    return -1;
}

int32_t MCMultiPlayer::ProcessReceiveList() const
{
    Assert(SessionManager != nullptr, 0);
    SessionManager->ProcessMessages();
    return 0;
}

int MCMultiPlayer::PlayersInSession() const
{
    return static_cast<int>(SessionManager->GetPlayers(nullptr).size());
}

std::optional<std::vector<uint32_t>> MCMultiPlayer::PlayersOnHomeTeam() const
{
    const auto group = SessionManager->GetGroup(HomeTeamGroupID);
    return group != nullptr ? std::optional(group->Players) : std::nullopt;
}

std::optional<std::vector<uint32_t>> MCMultiPlayer::PlayersOnEnemyTeam() const
{
    const auto group = SessionManager->GetGroup(EnemyTeamGroupID);
    return group != nullptr ? std::optional(group->Players) : std::nullopt;
}

bool MCMultiPlayer::IsMyTeammate(uint32_t playerID) const
{
    const std::optional<std::vector<uint32_t>> players = PlayersOnHomeTeam();
    return players.has_value() && std::ranges::contains(*players, playerID);
}

namespace
{
    /// <summary>The rules the mission being played plays by (MCSetMatchRulesInForce).</summary>
    uint32_t& RulesInForce()
    {
        static uint32_t rules = 0;
        return rules;
    }
}

void MCSetMatchRulesInForce(uint32_t rules)
{
    RulesInForce() = rules;
}

bool MCMatchPlays(uint32_t rules)
{
    return (RulesInForce() & rules) == rules;
}

uint32_t MCWantedRules(std::optional<uint32_t> chosen, bool prefsBraking)
{
    const uint32_t rules = chosen.value_or(MCMatchRule::BrakingFromPrefs);

    if ((rules & MCMatchRule::BrakingFromPrefs) == 0)
    {
        return rules;
    }

    return (rules & ~(MCMatchRule::BrakingFromPrefs | MCMatchRule::MechBraking)) |
           (prefsBraking ? MCMatchRule::MechBraking : 0);
}

uint32_t MCAgreedRules(uint32_t wanted, std::span<const uint32_t> players, const std::map<uint32_t, uint32_t>& said)
{
    uint32_t rules = wanted & MCMatchRule::Supported;

    for (const uint32_t player : players)
    {
        const auto rulesSaid = said.find(player);
        rules &= rulesSaid != said.end() ? rulesSaid->second : 0;
    }

    return rules;
}

std::vector<uint32_t> MCPlayersWithout(uint32_t rule, std::span<const uint32_t> players,
                                       const std::map<uint32_t, uint32_t>& said)
{
    std::vector<uint32_t> without;

    for (const uint32_t player : players)
    {
        if (MCAgreedRules(rule, std::span(&player, 1), said) != rule)
        {
            without.push_back(player);
        }
    }

    return without;
}

namespace
{
    /// <summary>Each rank's ban, from the most skilled down: a rank is for hire while neither it nor one under it is banned.</summary>
    constexpr std::array<std::pair<MCPilotRank, uint32_t>, 3> RankBans = {
        std::pair{MCPilotRank::Elite, MCMatchRule::NoElitePilots},
        std::pair{MCPilotRank::Veteran, MCMatchRule::NoVeteranPilots},
        std::pair{MCPilotRank::Regular, MCMatchRule::NoRegularPilots}};
}

MCPilotRank MCMostSkilledPilot(uint32_t rules)
{
    // The rank under the lowest banned.
    MCPilotRank most = MCPilotRank::Elite;

    for (const auto& [rank, ban] : RankBans)
    {
        if ((rules & ban) != 0)
        {
            most = static_cast<MCPilotRank>(static_cast<int32_t>(rank) - 1);
        }
    }

    return most;
}

uint32_t MCPilotRankRules(MCPilotRank rank)
{
    uint32_t rules = 0;

    for (const auto& [banned, ban] : RankBans)
    {
        rules |= banned > rank ? ban : 0;
    }

    return rules;
}

MCMatchOptions MCPlayedOptions(const MCMatchOptions& chosen, uint32_t wantedRules, uint32_t agreed)
{
    // The time of day's rules come with the time of day, not with what the host wants; the weather's play with every
    // player's build playing it, whatever the weather (the mission's own too).
    constexpr uint32_t timeRules = MCMatchRule::TimeOfDayRules;
    constexpr uint32_t weatherRules = MCMatchRule::WeatherRules;
    MCMatchOptions played;
    played.Weather = (agreed & weatherRules) == weatherRules ? chosen.Weather : std::nullopt;
    played.TimeOfDay = (agreed & timeRules) == timeRules ? chosen.TimeOfDay : std::nullopt;
    played.Rules = (wantedRules & agreed & ~(timeRules | weatherRules)) |
                   (played.TimeOfDay.has_value() ? timeRules : 0) | (agreed & MCMatchRule::WeatherPlay);
    return played;
}

uint32_t MCMultiPlayer::AgreedRules(uint32_t wanted) const
{
    return MCAgreedRules(wanted, OtherPlayers(*SessionManager), PlayerRules);
}

std::vector<uint32_t> MCMultiPlayer::PlayersWithout(uint32_t rule) const
{
    return MCPlayersWithout(rule, OtherPlayers(*SessionManager), PlayerRules);
}

MCMatchOptions MCMultiPlayer::PlayedOptions() const
{
    return MCPlayedOptions(MatchOptions, WantedRules(), AgreedRules(MCMatchRule::Supported));
}

bool MCMultiPlayer::AllPlayersCheckedIn()
{
    return std::ranges::all_of(SessionManager->GetPlayers(nullptr),
                               [this](const auto& player)
                               {
                                   if (!player->HasPlayerNumber)
                                   {
                                       return true;
                                   }

                                   Assert(player->PlayerNumber >= 0 && player->PlayerNumber <= 5, 0,
                                          "Invalid player number");
                                   return PlayerCheckedIn[player->PlayerNumber] != 0;
                               });
}

void MCMultiPlayer::SwitchServers()
{
    if (IsServer)
    {
        SessionManager->SwitchServers();
        ServerID = SessionManager->ServerID;
        IsServer = SessionManager->IsHost;
    }
}

void MCMultiPlayer::SetServer(uint32_t newServerID)
{
    ServerID = newServerID;
    const bool wasServer = IsServer;
    IsServer = SessionManager->IsHost;

    if (IsServer && !wasServer)
    {
        const std::shared_ptr<MCFidpPlayer> me = SessionManager->MyPlayer();
        HandleOwnMessages = true;
        SendChat(0, MCFormatPrintf(LoadGameString(0x378, 0xfe).c_str(), me->Name.c_str()));
        HandleOwnMessages = false;
        PlayerCheckedIn.fill(0);
    }

    if (InMission && IsServer && !wasServer)
    {
        TakeOverMovers(*this);
    }
}

void MCMultiPlayer::PlayerLeftGame(uint32_t playerID)
{
    // Port (personal): the match's report counts the players lost (main/MCTelemetry.h).
    MCTelemetry::PlayerLost();

    if (SessionManager->CurrentConnection != MCNetProtocol::Lobby)
    {
        NumLanPlayers--;
    }

    if (EventsToMissionResultsScreen != 0)
    {
        return;
    }

    const bool wasServer = IsServer;

    if (playerID == HostID)
    {
        HostID = SessionManager->ServerID;
        HostLeft = true;
        IsHost = SessionManager->IsHost;
    }

    if (SessionManager->IsHost)
    {
        IsServer = true;

        if (const auto player = SessionManager->GetPlayer(playerID); player != nullptr)
        {
            std::string text = MCFormatPrintf(LoadGameString(0x382, 0xfe).c_str(), player->Name.c_str());

            if (!wasServer)
            {
                text += LoadGameString(899, 0xfe);
            }

            HandleOwnMessages = true;
            SendChat(0, text);
            HandleOwnMessages = false;
        }
    }

    if (playerID == ServerID && InLogistics && PrepareScenarioReceived)
    {
        SendPlayerCheckIn();
    }

    ServerID = SessionManager->ServerID;

    if (!LaunchedFromLobby ? NumPlayers() < 2 : NumPlayers() == 2)
    {
        Mission()->EndScenarioRequested = 1;
    }

    if (InMission)
    {
        if (IsServer && !wasServer)
        {
            TakeOverMovers(*this);
        }

        return;
    }

    if (!InLogistics)
    {
        if (GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen ||
            GlobalLog()->CurrentScreen() == GlobalLog()->LoadScreen)
        {
            const std::string reason = LoadGameString(!LaunchedFromLobby ? 0x35f : 0x365, 0xfe);
            const auto player = SessionManager->GetPlayer(playerID);
            // Port fix: the player is already gone when DirectPlay reports it; the original printed its freed name.
            MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
            dialog.SetText(std::format("{} {}", player != nullptr ? player->Name : std::string(), reason));
            dialog.SetTwoButton(false);
            dialog.Callback = CancelBool;
            dialog.OkButton->Callback().SetExec(nullptr);
            char upArt[] = "bh_okay.tga";
            char downArt[] = "bg_okay.tga";
            dialog.OkButton->SetUpPicture(upArt);
            dialog.OkButton->SetDownPicture(downArt);
            dialog.OkButton->Disabled = false;
            dialog.OkButton->Draw();
            dialog.Timeout = 15000;
            dialog.Activate();
        }

        return;
    }

    if (GlobalLog() != nullptr)
    {
        bool onHomeTeam = false;

        if (const auto team = std::ranges::find(PlayerTeams, playerID, &MCMPPlayerTeam::PlayerID);
            team != PlayerTeams.end())
        {
            onHomeTeam = static_cast<uint32_t>(team->Team) == HomeTeamGroupID;
        }

        GlobalLog()->HandleLostPlayer(playerID, onHomeTeam ? 1 : 0);
    }

    if (IsServer && AllPlayersCheckedIn() && PrepareScenarioReceived)
    {
        HandleOwnMessages = true;
        SendStartScenario(PlayerCheckedIn, "");
        HandleOwnMessages = false;
    }
}

void MCMultiPlayer::LeaveSession()
{
    SessionManager->LeaveSession();
    InitStartupParameters();
}

void MCMultiPlayer::ResetForNewGame()
{
    if (HomeTeamGroupID != 0)
    {
        SessionManager->RemovePlayerFromGroup(HomeTeamGroupID, 0);
    }

    HomeTeamGroupID = 0;
    EnemyTeamGroupID = 0;
    NumLocalMovers = 0;
    NumMovers = 0;
    NumTurrets = 0;
    HandleOwnMessages = false;
    HostLeft = false;
    HomeTeam = -1;
    PlayerCheckedIn.fill(0);
    PlayerMoverRoster = {};
    InLogistics = false;
    InMission = false;
    PrepareScenarioReceived = false;
    ScenarioResult = 0;
    MoverRoster.fill({});
    PlayerTeams = {};
    NextMoverUpdateTime = 0.0f;
    MoverUpdateSequence = 0;
    NextTurretUpdateTime = 0.0f;
    TurretUpdateSequence = 0;
    NextWorldStateUpdateTime = 0.0f;
    WeaponHitChunks.clear();
    WorldStateChunks.clear();
}

void MCMultiPlayer::InitStartupParameters()
{
    AllPlayerGroupID = 0;
    InnerSphereGroupID = 0;
    ClanGroupID = 0;
    ServerID = 0;
    HomeTeamGroupID = 0;
    EnemyTeamGroupID = 0;
    HostID = 0;
    NumLocalMovers = 0;
    NumMovers = 0;
    NumTurrets = 0;
    HandleOwnMessages = false;
    HostLeft = false;
    IsHost = false;
    IsServer = false;
    CheckInId = -1;
    NumLanPlayers = 1;
    HomeTeam = -1;
    SessionName.clear();
    PlayerName.clear();
    PlayerCheckedIn.fill(0);
    PlayerMoverRoster = {};
    InLogistics = false;
    InMission = false;
    PrepareScenarioReceived = false;
    ScenarioResult = 0;
    MoverRoster.fill({});
    PlayerTeams = {};
    NextMoverUpdateTime = 0.0f;
    MoverUpdateSequence = 0;
    NextTurretUpdateTime = 0.0f;
    TurretUpdateSequence = 0;
    NextWorldStateUpdateTime = 0.0f;
    WeaponHitChunks.clear();
    WorldStateChunks.clear();
}

void MCMultiPlayer::StartGuaranteedMessage(MCMPMessageType type)
{
    MCFIGuaranteedMessageHeader header;
    header.Header = GuaranteedHeader(type);
    PutMessage(header);
}

void MCMultiPlayer::StartPlainMessage(MCMPMessageType type)
{
    PutMessage(PlainHeader(type));
}

// ---- the lobby's status dialog -------------------------------------------------------------------------------------

void ShowConnectStatus()
{
    const std::string text = LoadGameString(0x354, 0xfe);

    if (GlobalLog() == nullptr)
    {
        return;
    }

    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.SetText(text);

    if (dialog.OkButton != nullptr)
    {
        dialog.OkButton->ShowGuiWindow(false);
    }

    if (dialog.CancelButton != nullptr)
    {
        dialog.CancelButton->ShowGuiWindow(false);
    }

    dialog.Callback = nullptr;
    dialog.Activate();
    UpdateDisplay(false, false, 0, false, 0);
}

void DestroyConnectStatusWindow()
{
    if (GlobalLog() == nullptr)
    {
        return;
    }

    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.Deactivate(0);

    if (dialog.OkButton != nullptr)
    {
        dialog.OkButton->ShowGuiWindow(true);
    }

    if (dialog.CancelButton != nullptr)
    {
        dialog.CancelButton->ShowGuiWindow(true);
    }

    dialog.SetTwoButton(true);
}

int32_t LoadMultiplayerGameSystem(MCFitIniFile& file)
{
    if (const int32_t result = file.SeekBlock("Multiplayer"); result != 0)
    {
        return result;
    }

    const MCFitResult<float> warp = file.Read<float>("WarpFactor");

    if (warp.has_value())
    {
        WarpFactor = *warp;
        return 0;
    }

    if (warp.error() == MCFitError::VariableNotFound)
    {
        WarpFactor = 0.0f;
    }

    return std::to_underlying(warp.error());
}
