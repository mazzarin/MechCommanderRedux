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
class MCTacticalOrder;
class MCTurret;
class MCVector3D;

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

    int32_t SendPlayerCheckIn();

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

    int32_t SendTurretUpdate();

    int32_t SendMoverWeaponFireUpdate();

    int32_t SendTurretWeaponFireUpdate();

    int32_t SendMoverCriticalHitUpdate();

    int32_t SendWeaponHitUpdate();

    int32_t SendWorldStateUpdate();

    int32_t SendFileInquiry(std::string_view fileName);

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

    /// <summary>The first <paramref name="size"/> bytes of the message buffer, for a send (which may set its flags).</summary>
    std::span<uint8_t> Outgoing(size_t size) { return std::span(MsgBuffer).first(size); }

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

/// <summary>Shows the "connecting" dialog of the logistics screen.</summary>
void ShowConnectStatus();

/// <summary>Hides the "connecting" dialog.</summary>
void DestroyConnectStatusWindow();

/// <summary>Reads the Multiplayer block of the game system file (WarpFactor).</summary>
int32_t LoadMultiplayerGameSystem(MCFitIniFile& file);
