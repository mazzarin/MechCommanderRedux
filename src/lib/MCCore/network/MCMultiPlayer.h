#pragma once

// Original source: mcx\network\multplyr.cpp: the game's side of multiplayer. MultiPlayer owns the SessionManager
// (linkup), keeps the rosters of movers and turrets that are synchronised, queues the "chunks" (packed 32-bit records
// of moves, weapon fire, hits, status and world events) and sends them at fixed frequencies; the handlers in
// MCMultiPlayerHandlers apply the messages the other machines send.
//
// Port: SessionManager runs over platform/MCDirectPlay (TCP/UDP) instead of DirectPlay. MultiPlayer() stays null in a
// single-player game, as MPlayer did in the original. The chunk classes that single-player code also uses live with
// their objects: MoveChunk (ai/MCMoveChunk.h), StatusChunk (object/MCStatusChunk.h), WeaponFireChunk and
// WeaponHitChunk (object/MCWeaponFireChunk.h, object/MCWeaponHitChunk.h), ArtilleryChunk (object/MCArtilleryChunk.h).

#include "linkup/MCSessionManager.h"
#include "network/MCMPGameConfiguration.h"
#include "network/MCMultiPlayerMessages.h"
#include "network/MCWorldStateChunk.h"
#include "object/MCWeaponHitChunk.h"
#include "object/MCWeaponShotInfo.h"

class MCFidpMessage;
class MCFitIniFile;
class MCGameObject;
class MCMover;
class MCMoverGroup;
class MCReusableDialog;
class MCTacticalOrder;
class MCTurret;
class MCVector3D;

/// <summary>
/// Port: the rules beyond the original a match can play by, as bits (MCMultiPlayer::MatchRules), and the host's match
/// options a build can play (MATCH OPTIONS): what a player's rules message says its build plays by.
/// </summary>
namespace MCMatchRule
{
    /// <summary>Mechs brake to a halt (PREFS MechBraking, sprite/MCMechStops.h).</summary>
    inline constexpr uint32_t MechBraking = 1;
    /// <summary>The weather the host chose (MCMatchOptions::Weather, MCWeather::SetMatchWeather).</summary>
    inline constexpr uint32_t MatchWeather = 2;
    /// <summary>The time of day the host chose (MCMatchOptions::TimeOfDay, MCSceneLights::SetMatchTime).</summary>
    inline constexpr uint32_t MatchTimeOfDay = 4;
    /// <summary>
    /// At the host's time of day, units see as far as a mission set then lets them (MCMatchSight): in the match's
    /// rules when it plays the host's time of day.
    /// </summary>
    inline constexpr uint32_t TimeOfDaySight = 8;
    /// <summary>What a player's build must play for the match to play the host's time of day: its look and its sight.</summary>
    inline constexpr uint32_t TimeOfDayRules = MatchTimeOfDay | TimeOfDaySight;
    /// <summary>
    /// The weather the match plays changes play (mission/MCWeatherPlay.h: fires, sight, long-range aim, footing,
    /// vehicles' speed): in the match's rules whenever every player's build plays it, for the host's weather or the
    /// mission's own.
    /// </summary>
    inline constexpr uint32_t WeatherPlay = 16;
    /// <summary>What a player's build must play for the match to play the host's weather: its look and its play.</summary>
    inline constexpr uint32_t WeatherRules = MatchWeather | WeatherPlay;
    /// <summary>Commanders call in no artillery strikes (MATCH OPTIONS' ARTILLERY OFF; MCCommander).</summary>
    inline constexpr uint32_t NoArtillery = 32;
    /// <summary>
    /// Units and towers find their foes by sight alone (SENSORS OFF): no unit's or tower's sensors
    /// (MCSensorSystem::Enabled). A commander's sensor probe still senses: that is <see cref="NoProbes"/>'s.
    /// </summary>
    inline constexpr uint32_t NoSensors = 64;
    /// <summary>No mines (MINES OFF): the map's are cleared at load (MCScenarioMap::ClearMines), mine layers lay none.</summary>
    inline constexpr uint32_t NoMines = 128;
    /// <summary>Commanders send no sensor probes or camera drones (PROBES OFF; MCCommander).</summary>
    inline constexpr uint32_t NoProbes = 256;
    /// <summary>
    /// Not a rule the match plays by: the build's file report carries the hash of the mission's and the shared
    /// gameplay data (MCMissionDataHash), which the host checks against its own (MCMultiPlayer::CheckFileReport); a
    /// build without it sends the original's "checksum", which says only that the file is there.
    /// </summary>
    inline constexpr uint32_t DataHash = 1u << 23;
    /// <summary>
    /// Not a rule the match plays by: the build takes a host's MCMPMessageType::Updating (network/MCLobbyUpdate.h),
    /// waits for the host's game to come back and joins it again. The host tells only the guests whose builds say so;
    /// the others drop as the host leaves.
    /// </summary>
    inline constexpr uint32_t RejoinsHost = 1u << 24;
    /// <summary>
    /// Not a rule, never sent: in the host's chosen rules (MCMatchOptions::Rules, from the command line), braking as the
    /// host's PREFS say (MCWantedRules), with the other rules chosen.
    /// </summary>
    inline constexpr uint32_t BrakingFromPrefs = 1u << 31;
    /// <summary>Every rule this build plays by, and what its file report carries (<see cref="DataHash"/>).</summary>
    inline constexpr uint32_t Supported = MechBraking | WeatherRules | TimeOfDayRules | NoArtillery | NoSensors |
                                          NoMines | NoProbes | DataHash | RejoinsHost;
} // namespace MCMatchRule

/// <summary>
/// Port: what a player's file report says of the mission file the host asked about (MCMultiPlayer::CheckFileReport).
/// </summary>
enum class MCFileCheck : uint8_t
{
    /// <summary>The player hasn't the file.</summary>
    Missing,
    /// <summary>The player has the file and the same gameplay data as the host.</summary>
    Same,
    /// <summary>The player's mission or gameplay data differ from the host's: as good as missing.</summary>
    Differs,
    /// <summary>The player has the file; its build says nothing of its data (an older one, upstream's), so it plays.</summary>
    Unchecked,
};

/// <summary>
/// Port: the names (<paramref name="nameOf"/>) of those of <paramref name="players"/> that <paramref name="wanted"/>
/// takes, a comma between: the players a message or the log names.
/// </summary>
template <std::ranges::input_range Players, typename Wanted, typename NameOf>
std::string MCNamedPlayers(const Players& players, Wanted wanted, NameOf nameOf)
{
    std::string names;

    for (const auto& player : players)
    {
        if (wanted(player))
        {
            names += std::format("{}{}", names.empty() ? "" : ", ", nameOf(player));
        }
    }

    return names;
}

/// <summary>
/// Port: sets the rules the mission being played plays by (MCScenario::Load: its match's, MCMultiPlayer::Played; none
/// outside a match).
/// </summary>
void MCSetMatchRulesInForce(uint32_t rules);

/// <summary>Port: whether the mission being played plays by every one of <paramref name="rules"/>.</summary>
bool MCMatchPlays(uint32_t rules);

/// <summary>
/// Port: the rules a host wants of its <paramref name="chosen"/> ones (none: braking as its PREFS,
/// <paramref name="prefsBraking"/>; MCMatchRule::BrakingFromPrefs the same, with the others chosen).
/// </summary>
uint32_t MCWantedRules(std::optional<uint32_t> chosen, bool prefsBraking);

/// <summary>
/// Port: of the rules <paramref name="wanted"/>, those every one of <paramref name="players"/> plays by, as its rules
/// message (<paramref name="said"/>, by player id) said; a player whose build sent none plays by none.
/// </summary>
uint32_t MCAgreedRules(uint32_t wanted, std::span<const uint32_t> players, const std::map<uint32_t, uint32_t>& said);

/// <summary>
/// Port: those of <paramref name="players"/> whose build doesn't play by <paramref name="rule"/>, as their rules messages
/// (<paramref name="said"/>) said; a player whose build sent none is one.
/// </summary>
std::vector<uint32_t> MCPlayersWithout(uint32_t rule, std::span<const uint32_t> players,
                                       const std::map<uint32_t, uint32_t>& said);

/// <summary>
/// Port: what a match plays of the host's <paramref name="chosen"/> options and the rules it wants
/// (<paramref name="wantedRules"/>), with <paramref name="agreed"/> the rules every player's build plays by: its weather
/// and time of day only if every build plays the host's (else the mission's own; the time of day's sight with it, a
/// rule), the rules wanted that every build plays by.
/// </summary>
MCMatchOptions MCPlayedOptions(const MCMatchOptions& chosen, uint32_t wantedRules, uint32_t agreed);

/// <summary>The game's DirectPlay application GUID.</summary>
inline constexpr _GUID MultiPlayerAppGuid{0x09608800, 0x4815, 0x11d2, {0x92, 0xd2, 0x00, 0x60, 0x97, 0x3c, 0xfb, 0x2c}};

/// <summary>Whether a multiplayer game is set up by a lobby (SetupLobbyGame).</summary>
extern bool IsMPlayerGame;
/// <summary>Consecutive "session lost" system messages during a mission's start-up; over 10 ends the game.</summary>
extern int32_t BadSessionCounter;
/// <summary>The number of players in a LAN game (1 outside multiplayer).</summary>
extern int32_t NumLanPlayers;
/// <summary>The connection type of the last game, kept for the next session screen.</summary>
extern MCNetProtocol LastConnectionType;
/// <summary>The multiplayer game speed factor ("WarpFactor" of the Multiplayer block), used by mech and vehicle moves.</summary>
extern float WarpFactor;

/// <summary>A player's DPID and the team it joined (MultiPlayer::PlayerTeams).</summary>
struct MCMPPlayerTeam
{
    uint32_t PlayerID = 0;
    int32_t Team = 0;
};

/// <summary>
/// The multiplayer game: the session, the players' check-in and teams, the synchronised rosters, the queues of
/// outgoing chunks and the update timers.
/// </summary>
class MCMultiPlayer
{
public:
    /// <summary>The movers one machine controls: a mover group message packs them into 12 bits.</summary>
    static constexpr int32_t MaxLocalMovers = 12;
    /// <summary>The synchronised movers: the start-scenario message has a flag byte for each of 24.</summary>
    static constexpr int32_t MaxMovers = 24;
    /// <summary>The synchronised turrets: a turret weapon-fire entry holds the roster index in 6 bits.</summary>
    static constexpr int32_t MaxTurrets = 64;
    /// <summary>The bytes a message is built in.</summary>
    static constexpr size_t MsgBufferSize = 0x1400;

    /// <summary>A game with no session, the chat handler and the startup parameters set.</summary>
    MCMultiPlayer();
    /// <summary>Deletes the SessionManager.</summary>
    ~MCMultiPlayer();

    MCMultiPlayer(const MCMultiPlayer&) = delete;
    MCMultiPlayer& operator=(const MCMultiPlayer&) = delete;

    /// <summary>
    /// Makes the SessionManager (if there is none), points its home directory at the working directory, installs the
    /// message callbacks and clears the message buffer.
    /// </summary>
    /// <remarks>The original's <c>init(heapSize, maxMessageSize, maxMessages)</c>; its arguments were not used.</remarks>
    int32_t Start();

    /// <summary>
    /// Reads the mover, turret and world-state update periods from prefs.cfg's Multiplayer block; a missing one or one
    /// outside 0-5 seconds takes the default for the connection (0.2/0.5/0.33 on a LAN outside a lobby, else
    /// 0.33/1/0.75).
    /// </summary>
    void InitUpdateFrequencies();

    /// <summary>
    /// Reads a scripted game's Multiplayer block (Server, NumPlayers, CheckInId, HomeTeam, frequencies, Protocol,
    /// SessionName, PlayerName) and connects.
    /// </summary>
    /// <returns>0, or SetupLobbyConnection's result.</returns>
    int32_t StartScriptedGame(MCFitIniFile& file);

    /// <summary>The number of players (the session's in a lobby game, else NumLanPlayers).</summary>
    int32_t NumPlayers() const;

    /// <summary>Connects the lobby's session; the server creates the groups and sends its setup.</summary>
    int32_t SetupLobbyGame();

    /// <summary>
    /// Port: SetupLobbyGame once the lobby's session is joined or hosted (also a guest's rejoin, network/MCLobbyUpdate.h):
    /// this game's part, and the server's groups and setup.
    /// </summary>
    void StartLobbyGame();

    /// <summary>Adds a mover this machine controls (at most 12); stores its index in the mover.</summary>
    void AddToLocalMovers(std::shared_ptr<MCMover> mover);

    /// <summary>Adds a mover to the synchronised roster (at most 24); stores its index in the mover.</summary>
    void AddToMoverRoster(std::shared_ptr<MCMover> mover);

    /// <summary>Adds a mover to player <paramref name="playerNumber"/>'s roster (first free of 12 slots).</summary>
    void AddToPlayerMoverRoster(int32_t playerNumber, std::shared_ptr<MCMover> mover);

    /// <summary>Adds a turret to the synchronised roster (at most 64); stores its index in the turret.</summary>
    void AddToTurretRoster(std::shared_ptr<MCTurret> turret);

    /// <summary>The mover at <paramref name="index"/> of the synchronised roster; null past <see cref="NumMovers"/>.</summary>
    /// <remarks>Issue #25: the index comes from another machine's message.</remarks>
    std::shared_ptr<MCMover> RosterMover(int32_t index) const;

    /// <summary>The turret at <paramref name="index"/> of the synchronised roster; null past <see cref="NumTurrets"/>.</summary>
    /// <remarks>Issue #25: the index comes from another machine's message.</remarks>
    std::shared_ptr<MCTurret> RosterTurret(int32_t index) const;

    /// <summary>Packs <paramref name="chunk"/> (checking it unpacks the same) and queues it.</summary>
    /// <returns>The number queued.</returns>
    int32_t AddWorldStateChunk(MCWorldStateChunk& chunk);

    int32_t AddMissionScriptMessageChunk(int32_t message, int32_t value);

    int32_t AddArtilleryChunk(int32_t commanderId, int32_t strikeType, const MCVector3D& location, int32_t seconds);

    int32_t AddMineChunk(int32_t tileRow, int32_t tileCol, int32_t teamId, int32_t mineState, int32_t explosionType);

    /// <summary>Queues a terrain fire (only buildings, trees, misc terrain objects and tree buildings burn).</summary>
    int32_t AddLightOnFireChunk(MCGameObject& object, int32_t seconds);

    int32_t AddPilotKillStat(MCMover& mover, int32_t killType);

    /// <summary>Packs <paramref name="chunk"/> (checking it unpacks the same) and queues it.</summary>
    int32_t AddWeaponHitChunk(MCWeaponHitChunk& chunk);

    /// <summary>
    /// Builds a weapon-hit chunk for a shot at <paramref name="target"/> and queues it. <paramref name="hitFlag"/> goes
    /// through WeaponHitChunk::Build to BuildMoverTarget.
    /// </summary>
    int32_t AddWeaponHitChunk(MCGameObject& target, MCWeaponShotInfo& shotInfo, int hitFlag);

    /// <summary>Moves up to <paramref name="chunks"/>' size of the queued weapon-hit words into it.</summary>
    /// <returns>How many.</returns>
    size_t GrabWeaponHitChunks(std::span<uint32_t> chunks);

    int32_t ConnectIpx() const;

    /// <summary>Hosts a session named <paramref name="sessionName"/> for <paramref name="maxPlayers"/>.</summary>
    int32_t CreateSession(std::string_view sessionName, std::string_view playerName, int32_t maxPlayers);

    /// <summary>Hosts a session under the scripted game's names.</summary>
    int32_t CreateSession(int32_t maxPlayers);

    /// <summary>Joins session <paramref name="sessionName"/> as <paramref name="playerName"/>.</summary>
    int32_t JoinSession(std::string_view sessionName, std::string_view playerName);

    /// <summary>Joins the scripted game's session under its player name.</summary>
    int32_t JoinSession();

    /// <summary>Runs the SessionManager's message pump.</summary>
    int32_t ProcessReceiveList() const;

    /// <summary>Sends chat <paramref name="text"/> to <paramref name="toID"/> (0 = everyone).</summary>
    int32_t SendChat(uint32_t toID, std::string_view text);

    /// <summary>Port: first sends the server this build's rules (<see cref="SendPlayerRules"/>).</summary>
    int32_t SendPlayerCheckIn();

    /// <summary>
    /// Port: sends the server the rules this build plays by (MCMatchRule::Supported): as the session screen opens, with
    /// its answer to a file inquiry and at check-in.
    /// </summary>
    int32_t SendPlayerRules();

    /// <summary>
    /// Port, the server: sends everyone the rules the match plays by, and plays by them (<see cref="AgreedRules"/> of
    /// those its match options last played, <see cref="Played"/>; without any sent, of <see cref="WantedRules"/>).
    /// </summary>
    int32_t SendMatchRules();

    /// <summary>Port: the rules the server wants: the host's choice (<see cref="MatchOptions"/>), else its own PREFS.</summary>
    uint32_t WantedRules() const;

    /// <summary>
    /// Port, the host: sends everyone what the match plays of its <see cref="MatchOptions"/> (the session screen's
    /// MATCH OPTIONS; <see cref="PlayedOptions"/>), and keeps it as <see cref="Played"/>.
    /// </summary>
    int32_t SendMatchOptions();

    /// <summary>
    /// Port, the host: sends everyone the rules each player's build plays by as it heard them (<see cref="PlayerRules"/>,
    /// its own with them), as one comes: a guest's MATCH OPTIONS then names whose game lacks an option as the host's
    /// does.
    /// </summary>
    void SendPlayersRules();

    /// <summary>
    /// Port, the host: sends its match options again (<see cref="SendMatchOptions"/>) when what the match plays of them
    /// has changed: a player's rules came, or a player left.
    /// </summary>
    void UpdateMatchOptions();

    /// <summary>
    /// Port, the host: what the match plays of its <see cref="MatchOptions"/> among the session's players now
    /// (MCPlayedOptions).
    /// </summary>
    MCMatchOptions PlayedOptions() const;

    /// <summary>
    /// Port: MCAgreedRules among the session's other players (with a number); this build plays by its own
    /// (MCMatchRule::Supported).
    /// </summary>
    uint32_t AgreedRules(uint32_t wanted) const;

    /// <summary>Port: MCPlayersWithout among the session's other players (with a number).</summary>
    std::vector<uint32_t> PlayersWithout(uint32_t rule) const;

    /// <summary>The server's setup message to <paramref name="toID"/> (0 = everyone): the group ids.</summary>
    int32_t SendPlayerSetup(uint32_t toID, uint32_t innerSphereGroupID, uint32_t clanGroupID);

    int32_t SendStartPlanning();

    int32_t SendReadyForBattle();

    int32_t SendPrepareScenario();

    /// <summary>
    /// Records the new server <paramref name="serverID"/>; a machine that just became server announces it and takes
    /// over the movers' orders.
    /// </summary>
    void SetServer(uint32_t serverID);

    /// <summary>The server starts the scenario on every machine.</summary>
    int32_t SendStartScenario(std::span<const int32_t> playerValues, std::string_view missionName);

    int32_t SendEndScenario(int32_t result);

    /// <summary>Sends a player's tactical order for its movers or groups to the server.</summary>
    int32_t SendPlayerOrder(MCTacticalOrder& order, bool queued, std::span<const int32_t> moverParts,
                            std::span<const std::weak_ptr<MCMoverGroup>> groups, bool fromGroup);

    int32_t SendPlayerMoverGroup(int32_t groupId, std::span<const std::shared_ptr<MCMover>> movers, int32_t pointIndex);

    int32_t SendPlayerArtillery(int32_t strikeType, const MCVector3D& location, int32_t seconds);

    /// <summary>Broadcasts every rostered mover's move chunk, status chunk and its pilot's order id.</summary>
    int32_t SendMoverUpdate();

    /// <summary>
    /// Port-only: whether a rostered mover's last move chunk said its move goes on past it and its pilot's paths have
    /// changed since (MCMoveChunk::AwaitsMore): its next path or leg planned, or its wait over. The server then sends the
    /// next mover update now, not at its time, so the guests' mechs don't reach that chunk's end first and stop there.
    /// </summary>
    bool MoverCellsAwaited() const;

    int32_t SendTurretUpdate();

    int32_t SendMoverWeaponFireUpdate();

    int32_t SendTurretWeaponFireUpdate();

    int32_t SendMoverCriticalHitUpdate();

    int32_t SendWeaponHitUpdate();

    int32_t SendWorldStateUpdate();

    /// <summary>
    /// Asks every player whether it has mission file <paramref name="fileName"/>. Port: first works out this game's own
    /// hash of it (<see cref="AskedFileHash"/>), for the answers to be checked against.
    /// </summary>
    int32_t SendFileInquiry(std::string_view fileName);

    /// <summary>
    /// Port, the host: what player <paramref name="playerId"/>'s file report (<paramref name="value"/>) says of the file
    /// last asked about: 0 none; from a build that hashes its data (its rules message has MCMatchRule::DataHash) the
    /// same data only as <see cref="AskedFileHash"/>; from any other, the file is there, unchecked.
    /// </summary>
    [[nodiscard]] MCFileCheck CheckFileReport(uint32_t playerId, uint32_t value) const;

    /// <summary>The server's per-frame sends: world state and weapons, movers and turrets, each on its timer.</summary>
    int32_t UpdateClients();

    int PlayersInSession() const;

    /// <summary>The ids of the players in this machine's team group, or none without the group.</summary>
    std::optional<std::vector<uint32_t>> PlayersOnHomeTeam() const;

    /// <summary>The ids of the players in the other team's group, or none without the group.</summary>
    std::optional<std::vector<uint32_t>> PlayersOnEnemyTeam() const;

    bool IsMyTeammate(uint32_t playerID) const;

    /// <summary>Whether every numbered player has checked in.</summary>
    bool AllPlayersCheckedIn();

    void SwitchServers();

    /// <summary>Handles player <paramref name="playerID"/> leaving: host and server hand-over, messages, ending a lone game.</summary>
    void PlayerLeftGame(uint32_t playerID);

    void LeaveSession();

    /// <summary>Clears the rosters, check-ins, teams, timers and queues for the next game.</summary>
    void ResetForNewGame();

    /// <summary>Clears everything <see cref="ResetForNewGame"/> does and the groups, ids, names and roles.</summary>
    void InitStartupParameters();

    /// <summary>Starts a guaranteed message of <paramref name="type"/> in the message buffer.</summary>
    void StartGuaranteedMessage(MCMPMessageType type);

    /// <summary>
    /// Starts a guaranteed message of <paramref name="type"/> in the message buffer and returns it as a
    /// <typeparamref name="T"/>, for <see cref="PutMessage"/> once its fields are set.
    /// </summary>
    /// <remarks>
    /// The fields hold what the buffer holds: the original wrote only the fields it set, so the others keep the bytes of
    /// the last message built there.
    /// </remarks>
    template <class T> T StartGuaranteedMessage(MCMPMessageType type)
    {
        StartGuaranteedMessage(type);
        return ReadLinkupMessage<T>(MsgBuffer);
    }

    /// <summary>Starts a plain message of <paramref name="type"/> in the message buffer.</summary>
    void StartPlainMessage(MCMPMessageType type);

    /// <summary>Writes the message layout <paramref name="message"/> at the start of the message buffer.</summary>
    template <class T> void PutMessage(const T& message)
    {
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= MsgBufferSize);
        std::memcpy(MsgBuffer.data(), &message, sizeof(T));
    }

    /// <summary>The first <paramref name="size"/> bytes of the message buffer.</summary>
    std::span<const uint8_t> Built(size_t size) const { return std::span(MsgBuffer).first(size); }

    /// <summary>
    /// The first <paramref name="size"/> bytes of the message buffer, for a send (which may set its flags). Port
    /// (personal): the send's type and size go in the reports' black box.
    /// </summary>
    std::span<uint8_t> Outgoing(size_t size);

    std::unique_ptr<MCSessionManager> SessionManager;
    /// <summary>Where messages are built.</summary>
    std::array<uint8_t, MsgBufferSize> MsgBuffer{};
    uint32_t AllPlayerGroupID = 0;
    uint32_t InnerSphereGroupID = 0;
    uint32_t ClanGroupID = 0;
    /// <summary>The group of this machine's team (one of the two above).</summary>
    uint32_t HomeTeamGroupID = 0;
    uint32_t EnemyTeamGroupID = 0;
    /// <summary>The server's DPID.</summary>
    uint32_t ServerID = 0;
    /// <summary>The host's DPID (SendToHost; the server takes over when the host leaves).</summary>
    uint32_t HostID = 0;
    int32_t NumLocalMovers = 0;
    int32_t NumMovers = 0;
    int32_t NumTurrets = 0;
    /// <summary>The movers this machine controls.</summary>
    std::array<std::weak_ptr<MCMover>, MaxLocalMovers> LocalMovers{};
    /// <summary>Every synchronised mover, by roster index.</summary>
    std::array<std::weak_ptr<MCMover>, MaxMovers> MoverRoster{};
    /// <summary>Per player number: its movers.</summary>
    std::array<std::array<std::weak_ptr<MCMover>, MaxLocalMovers>, MaxLinkupPlayers> PlayerMoverRoster{};
    /// <summary>Every synchronised turret, by roster index.</summary>
    std::array<std::weak_ptr<MCTurret>, MaxTurrets> TurretRoster{};
    /// <summary>
    /// Set around a send to have this machine also handle the message itself (the host's own chat, setup, start
    /// planning, ...).
    /// </summary>
    bool HandleOwnMessages = false;
    /// <summary>Whether this machine is the server.</summary>
    bool IsServer = false;
    /// <summary>Whether this machine hosted the session.</summary>
    bool IsHost = false;
    /// <summary>Set when the host left and the server took over.</summary>
    bool HostLeft = false;
    /// <summary>This machine's check-in id (player slot; -1 until known, 0 on a scripted server).</summary>
    int32_t CheckInId = -1;
    /// <summary>This machine's team (-1 until chosen).</summary>
    int32_t HomeTeam = -1;
    /// <summary>
    /// Port: who is on each team, the tech bases and the points pools the mech bay starts from. Set as the match starts
    /// (the session screen's start, or a configured match's); the mech bay reads it, never the session screen.
    /// </summary>
    MPMatchState Match;
    std::string SessionName;
    std::string PlayerName;
    /// <summary>Per player number: checked in (the DPID on a scripted server).</summary>
    std::array<int32_t, MaxLinkupPlayers> PlayerCheckedIn{};
    /// <summary>Per player number: set by message 45 (SessionScreen::SomeoneCheckedIn).</summary>
    std::array<int32_t, MaxLinkupPlayers> PlayerSessionCheckIn{};
    /// <summary>Port: the rules each player's build plays by (its rules message), by player id.</summary>
    std::map<uint32_t, uint32_t> PlayerRules;
    /// <summary>
    /// Port: this game's MCMissionDataHash of the mission file it last asked the players about (SendFileInquiry); 0
    /// before.
    /// </summary>
    uint32_t AskedFileHash = 0;
    /// <summary>
    /// Port: the rules the match plays by (MCMatchRule bits; the server's rules message): none, the original's game,
    /// until the server sends them.
    /// </summary>
    uint32_t MatchRules = 0;
    /// <summary>
    /// Port, the host: its match options, as it chose them (from its command line's, until it changes them); what the
    /// match plays of them is <see cref="Played"/>.
    /// </summary>
    MCMatchOptions MatchOptions = GameConfiguration.Options;
    /// <summary>
    /// Port: the match options the match plays (MCScenarioLoad, the session screen's MATCH OPTIONS): those the host last
    /// sent, on a guest its last match options message; none (the mission's own, no rules) until then.
    /// </summary>
    MCMatchOptions Played;
    /// <summary>Set by the start-planning message: the players are in logistics.</summary>
    bool InLogistics = false;
    /// <summary>Whether a mission runs (UpdateClients only sends then).</summary>
    bool InMission = false;
    /// <summary>
    /// Set by the prepare-scenario message and cleared once the scenario starts: a dropped connection then ends the
    /// scenario instead of going back to the session screen.
    /// </summary>
    bool PrepareScenarioReceived = false;
    /// <summary>The scenario result the end-scenario message carried.</summary>
    int32_t ScenarioResult = 0;
    /// <summary>Each player's DPID and team.</summary>
    std::array<MCMPPlayerTeam, MaxLinkupPlayers> PlayerTeams{};
    /// <summary>The chat handler (HandleAppChat; the logistics screens and the mission install their own).</summary>
    std::function<void(MCFidpMessage& msg)> ChatCallback;
    /// <summary>scenarioTime of the next mover update.</summary>
    float NextMoverUpdateTime = 0;
    /// <summary>Seconds between mover updates.</summary>
    float MoverUpdateFrequency = 0;
    /// <summary>
    /// Port-only: the share of <see cref="MoverUpdateFrequency"/> since the last mover update before a mover awaiting
    /// more of its move (<see cref="MoverCellsAwaited"/>) has the next sent early: at most three a regular interval.
    /// </summary>
    static constexpr float EarlyMoverUpdateShare = 1.0f / 3.0f;
    /// <summary>The number of the next mover update (receivers drop older ones).</summary>
    uint16_t MoverUpdateSequence = 0;
    float NextTurretUpdateTime = 0;
    float TurretUpdateFrequency = 0;
    /// <summary>The number of the next turret update (receivers drop older ones).</summary>
    uint16_t TurretUpdateSequence = 0;
    float NextWorldStateUpdateTime = 0;
    float WorldStateUpdateFrequency = 0;
    /// <summary>
    /// The update periods in seconds, as InitUpdateFrequencies settled them: mover updates, turret updates, world-state
    /// (and weapon) updates.
    /// </summary>
    std::array<float, 3> BroadcastFrequencies{};
    /// <summary>Queued packed weapon-hit chunks.</summary>
    std::vector<uint32_t> WeaponHitChunks;
    /// <summary>Queued packed world-state chunks.</summary>
    std::vector<uint32_t> WorldStateChunks;
    /// <summary>Per world-state type: how many were queued since the last update (statistics).</summary>
    std::array<int32_t, MCWorldStateChunk::NumTypes> WorldStateChunkTally{};
};

/// <summary>The multiplayer game, or null in a single-player game.</summary>
std::shared_ptr<MCMultiPlayer> MultiPlayer();

/// <summary>
/// Player <paramref name="name"/> left the session screen: says so, and (OK, or 15 seconds) back to the main menu, or
/// out of a lobby's game.
/// </summary>
void ShowPlayerLeft(std::string_view name);

/// <summary>Shows <paramref name="text"/> in <paramref name="dialog"/> with one OK button, which runs <paramref name="exit"/>.</summary>
void ShowOkDialog(MCReusableDialog& dialog, std::string_view text, std::function<void()> exit);

/// <summary>Shows <paramref name="text"/> in the logistics screen's message dialog with no button: the game waits.</summary>
void ShowWaitingDialog(std::string_view text);

/// <summary>Shows the "connecting" dialog of the logistics screen (a waiting dialog), drawn at once.</summary>
void ShowConnectStatus();

/// <summary>Hides the "connecting" dialog.</summary>
void DestroyConnectStatusWindow();

/// <summary>Reads the Multiplayer block of the game system file (WarpFactor).</summary>
int32_t LoadMultiplayerGameSystem(MCFitIniFile& file);
