#pragma once

// Port-only (SPEC-menus.md's stage 4, "Models and views"): the multiplayer room's state, pulled out of the 1998 session
// screen (logistics/MCSessionScreen), and what the room's views read of it and ask of it: the teams' players by slot,
// their tech bases and resource points, the players on no team, the loaded mission, what the start waits for. Logistics
// owns it (MCLogistics::Room), so it lives as long as the room's screens; what the room sends and when is unchanged.
// Port-only (SPEC-menus.md's stage 9): the room's logic too, out of the 1998 session screen, which is a view on it (an
// MCRoomView): every action and message of the room, and its 500 ms tick.

#include "gui/MCGuiRef.h"
#include "lib/MCOptionalRef.h"

enum class MCFileCheck : uint8_t;
class MCRoomTicker;
struct MCRoomPicks;

/// <summary>
/// Port-only (SPEC-menus.md's stage 9): what a view of the room is told as the room's logic goes, at the moments the 1998
/// session screen did each itself (logistics/MCSessionScreen, the one such view): its name slots, its points' boxes, its
/// description, its map and its controls.
/// </summary>
class MCRoomView
{
public:
    virtual ~MCRoomView() = default;

    /// <summary>The room opens afresh (Activate): its tabs and buttons a new room's, every name slot hidden at its row.</summary>
    virtual void Opening() = 0;
    /// <summary>The room opened (Activate's end): the chat, the ticker and the players' lights on the view.</summary>
    virtual void Opened() = 0;
    /// <summary>Seat <paramref name="seat"/>'s name to row <paramref name="row"/> of the players on no team.</summary>
    virtual void NameToRow(size_t seat, int32_t row) = 0;
    /// <summary>Seat <paramref name="seat"/>'s name to team <paramref name="team"/>'s slot <paramref name="slot"/>.</summary>
    virtual void NameToTeam(size_t seat, int8_t team, int8_t slot) = 0;
    /// <summary>Seat <paramref name="seat"/>'s name shown or hidden.</summary>
    virtual void ShowName(size_t seat, bool shown) = 0;
    /// <summary>Team <paramref name="team"/>'s points set (the view's box shows them).</summary>
    virtual void ShowPoints(int8_t team) = 0;
    /// <summary>The points as typed in the view (its boxes), into the model's <see cref="MCRoomModel::PointsText"/>.</summary>
    virtual void TakeTypedPoints() = 0;
    /// <summary>The mission's description box afresh: <paramref name="lines"/> in font <paramref name="font"/>.</summary>
    virtual void ShowMissionText(int32_t font, std::span<const std::string> lines) = 0;
    /// <summary>The map's picture for the model's map file (none when empty).</summary>
    virtual void ShowMap() = 0;
    /// <summary>The host's controls as the model has them (its forces' controls, names and mission pick).</summary>
    virtual void ShowControls() = 0;
    /// <summary>The teams' tech bases as the model has them.</summary>
    virtual void ShowTechBases() = 0;
    /// <summary>Whether the match can start, and the line of whom it waits for.</summary>
    virtual void ShowStart() = 0;
    /// <summary>The mission's files answered: the view's own mission pick (the 1998 load screen) put back, if it shows.</summary>
    virtual void LeftMissionPick() = 0;
    /// <summary>A guest told the points: the view drawn at once (as the original's handler drew the screen).</summary>
    virtual void DrawNow() = 0;
    /// <summary>The room left: its points' boxes' timers removed (the original's way: OB-167).</summary>
    virtual void StopBoxTimers() = 0;
};

class MCRoomModel
{
public:
    MCRoomModel();
    ~MCRoomModel();
    MCRoomModel(const MCRoomModel&) = delete;
    MCRoomModel& operator=(const MCRoomModel&) = delete;

    /// <summary>The most players in a session (a game rule: the name slots and the player lights).</summary>
    static constexpr size_t MaxPlayers = 6;
    /// <summary>The slots of a team (a game rule).</summary>
    static constexpr size_t TeamSlots = 3;
    /// <summary>A player id that stands for no player (an empty slot).</summary>
    static constexpr uint32_t NoPlayer = 0xffffffff;
    /// <summary>The teams, as the room's messages number them (0: no team).</summary>
    static constexpr int8_t NoTeam = 0;
    static constexpr int8_t Team1 = 1;
    static constexpr int8_t Team2 = 2;
    /// <summary>The tech bases, as the original kept them.</summary>
    static constexpr int8_t InnerSphere = 1;
    static constexpr int8_t Clan = -1;

    /// <summary>Team 1's tech base (<see cref="InnerSphere"/> or <see cref="Clan"/>).</summary>
    int8_t Team1TechBase = InnerSphere;
    /// <summary>Team 2's tech base.</summary>
    int8_t Team2TechBase = Clan;
    /// <summary>The number of players in the session.</summary>
    int32_t NumPlayers = 0;
    /// <summary>The number of players on no team.</summary>
    int32_t NumUnassigned = 0;
    /// <summary>Team 1's players by slot (<see cref="NoPlayer"/> = empty).</summary>
    std::array<uint32_t, TeamSlots> Team1Players = {};
    /// <summary>Team 2's players by slot.</summary>
    std::array<uint32_t, TeamSlots> Team2Players = {};
    /// <summary>Each team's resource points as they stand.</summary>
    int32_t Team1Points = 0;
    int32_t Team2Points = 0;
    /// <summary>Team 1's resource points last sent.</summary>
    int32_t Team1RP = 0;
    /// <summary>Team 2's resource points last sent.</summary>
    int32_t Team2RP = 0;
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9): each team's points as the 1998 room's box held them, which the tick and the
    /// match read: the number set, cut to the box's seven characters (its 8-byte buffer), or what the host typed there.
    /// </summary>
    std::array<std::string, 2> PointsText = {"0", "0"};
    /// <summary>The host's controls of the forces are live: the points and tech bases (off for a locked mission).</summary>
    bool ForcesLive = false;
    /// <summary>The loaded mission's name.</summary>
    std::string MissionName;
    /// <summary>The loaded mission's file; empty = none.</summary>
    std::string MissionFile;
    /// <summary>The loaded mission's map name.</summary>
    std::string MapName;
    /// <summary>The loaded mission's map's file name (its picture is the map's <c>.log.tga</c>); empty = none.</summary>
    std::string MapFile;
    /// <summary>The loaded mission's time ("m:ss"); empty without one.</summary>
    std::string MissionTime;
    /// <summary>The loaded mission's description (the mission packet's lines); empty without one.</summary>
    std::vector<std::string> Description;
    /// <summary>
    /// Port-only (the personal build): the name the host loaded its mission by, while one is loaded (Port-only, Redux HD:
    /// on a guest too, for a room rolled over to it).
    /// </summary>
    std::string LoadedMission;
    /// <summary>Port-only (the personal build): the loaded mission fixes its points and tech bases (its Lock block).</summary>
    bool Locked = false;
    /// <summary>The host's controls are live (the host's room, every player checked in).</summary>
    bool HostControls = false;
    /// <summary>Back from a match: the room waits for every player to check in again.</summary>
    bool CheckingIn = false;
    /// <summary>The match can start: every player on a team and shown, a mission loaded (the host's start lit).</summary>
    bool CanStart = false;
    /// <summary>
    /// Port-only (the personal build): whom the room waits for, guests away updating ("WAITING FOR ANN TO REJOIN."), as
    /// the start's check last found; empty when it waits for no one.
    /// </summary>
    std::string StartWaits;

    /// <summary>
    /// Whether a player has the loaded mission's file (the host's inquiry: the 1998 room's LoadMission and FileReport).
    /// </summary>
    enum class MCFileStatus : int8_t
    {
        /// <summary>No inquiry out.</summary>
        NotAsked = -1,
        /// <summary>Waiting for the answer.</summary>
        Waiting = 0,
        Missing = 1,
        Present = 2,
        /// <summary>Port: the file is there, but the player's game data differ from the host's (as good as missing).</summary>
        Differs = 3,
        /// <summary>Port: the file is there; the player's build says nothing of its game data (it plays).</summary>
        Unchecked = 4,
    };

    /// <summary>A player shown the room, in a seat of its own (the 1998 room's name slots, in the order it filled them).</summary>
    struct MCSeat
    {
        /// <summary>The player's network id, or <see cref="NoPlayer"/> for an empty seat.</summary>
        uint32_t Id = NoPlayer;
        /// <summary>The player's name, the session's as the player was seated.</summary>
        std::string Name;
        MCFileStatus Files = MCFileStatus::NotAsked;
        /// <summary>
        /// Whether the room shows the seat's name: hidden as the room opens, shown as it seats the player, hidden as the
        /// player leaves. A seat past the players the room last opened with keeps its id, hidden.
        /// </summary>
        bool Shown = false;
    };

    /// <summary>The players shown the room, a seat each.</summary>
    std::array<MCSeat, MaxPlayers> Seats;

    // ---- The room's watch (the 1998 room's tick runs it) ----

    /// <summary>Until when (the game clock) the room keeps pinging after it opens.</summary>
    uint32_t PingUntil = 0;
    /// <summary>The room still pings.</summary>
    bool Pinging = false;
    /// <summary>Port-only (Redux HD): the room has rolled over to this game, whose next look takes it (TakeOverRoom).</summary>
    bool TakingOver = false;
    /// <summary>
    /// Port-only (the personal build): the host's guests whose game was up (they had pinged) when they were last shown
    /// the room; one shown before it was up is shown it again once it is.
    /// </summary>
    std::set<uint32_t> ShownReady;
    /// <summary>Port-only (the personal build): when (the game clock) the host first saw each player in the session.</summary>
    std::map<uint32_t, uint32_t> SeenSince;

    /// <summary>
    /// Player <paramref name="playerId"/> put in seat <paramref name="seat"/> (<see cref="NoPlayer"/> empties it), with
    /// the session's name for it when the session has it (else the seat's name stays, as the 1998 name's did).
    /// </summary>
    void Seat(size_t seat, uint32_t playerId);
    /// <summary>Player <paramref name="playerId"/>'s seat; none when the room doesn't show it.</summary>
    MCOptionalRef<MCSeat> SeatOf(uint32_t playerId);
    /// <summary>Whether the room shows player <paramref name="playerId"/>'s name in a seat (<see cref="MCSeat::Shown"/>).</summary>
    bool ShowsName(uint32_t playerId) const;
    /// <summary>Whether the host's inquiry of the mission's file waits for a player's answer.</summary>
    bool AskingFiles() const;

    // ---- What the room's views read ----

    /// <summary>A player in the room, as its views show them.</summary>
    struct MCPlayer
    {
        uint32_t Id = NoPlayer;
        std::string Name;
        /// <summary>The player's number in the session (its colour's: the chat's and the lights').</summary>
        int32_t Number = 0;
        bool Host = false;
        /// <summary>This game's own player.</summary>
        bool Me = false;
        /// <summary>Its team (<see cref="NoTeam"/>, <see cref="Team1"/>, <see cref="Team2"/>) and its slot there.</summary>
        int8_t Team = NoTeam;
        int8_t Slot = 0;

        bool operator==(const MCPlayer&) const = default;
    };

    /// <summary>Where the room was found: on this network, on the internet, or by a launcher.</summary>
    enum class MCWhere : uint8_t
    {
        Lan,
        Internet,
        Launcher,
    };

    /// <summary>The players the room shows, as the session lists them (by id).</summary>
    std::vector<MCPlayer> Players() const;
    /// <summary>Whether this game hosts the room.</summary>
    bool Hosting() const;
    /// <summary>The host's name; empty when the session doesn't know it.</summary>
    std::string HostName() const;
    /// <summary>The room's name (the session's).</summary>
    std::string Name() const;
    /// <summary>The most players the room takes.</summary>
    int32_t Most() const;
    MCWhere Where() const;
    /// <summary>An internet room's: whether it's listed, and its code (empty while the service hasn't given it).</summary>
    bool Public() const;
    std::string Code() const;
    /// <summary>Team <paramref name="team"/>'s points and tech base.</summary>
    int32_t Points(int8_t team) const;
    int8_t TechBase(int8_t team) const;
    /// <summary>Whether the host may place players and choose the map now (its controls live).</summary>
    bool CanChangeTeams() const { return Hosting() && HostControls; }
    /// <summary>Whether the host may set the teams' tech bases and points (and the mission doesn't fix them).</summary>
    bool CanChangeForces() const { return CanChangeTeams() && !Locked; }
    // ---- What the room's views ask (the host's, as the 1998 screen's controls did; each sent as it was) ----

    /// <summary>Puts <paramref name="playerId"/> on <paramref name="team"/> at <paramref name="slot"/> (no team: off).</summary>
    void Assign(uint32_t playerId, int8_t team, int8_t slot);
    /// <summary>Puts <paramref name="playerId"/> in <paramref name="team"/>'s first open slot, if it has one.</summary>
    void JoinTeam(uint32_t playerId, int8_t team);
    /// <summary>Sets team <paramref name="team"/>'s tech base, told to the others.</summary>
    void SetTechBase(int8_t team, int8_t techBase);
    /// <summary>Sets team <paramref name="team"/>'s points (never under 0); sent on the room's next look.</summary>
    void SetPoints(int8_t team, int32_t points);
    /// <summary>Loads the multiplayer mission <paramref name="mission"/> (its packet's name), asking everyone for it.</summary>
    void ChooseMission(std::string_view mission);
    /// <summary>Starts the match (the host's BEGIN PLANNING), when it can start.</summary>
    void BeginPlanning();

    /// <summary>
    /// The room's work each frame, whichever room shows (the remade one's frame or the 1998 one's draw): the host's
    /// match options sent again when a player's rules change what the match plays.
    /// </summary>
    void Tend();

    // ---- The room's logic (the 1998 session screen's, moved: SPEC-menus.md's stage 9) ----

    /// <summary>
    /// The 1998 room, a view on the model while it lives (logistics/MCSessionScreen); nothing when none. A room is
    /// made while it has one (<see cref="Made"/>).
    /// </summary>
    void SetView(std::weak_ptr<MCRoomView> view);
    /// <summary>Whether logistics made its room (its 1998 session screen, the room's view, is made).</summary>
    bool Made() const;

    /// <summary>
    /// Fills the room from the session: the players, the teams and the controls for host or guest;
    /// <paramref name="refresh"/> (back from a mission) waits for everyone to check in again. Port-only (the personal
    /// build): a host opens it alone, waiting for players. The room's tick starts.
    /// </summary>
    void Activate(bool refresh);

    /// <summary>
    /// The room's tick, every 500 ms from <see cref="Activate"/> to the match's start or the room's end (the 1998 screen's
    /// timer, on the GUI's timers as it was): pings for 3 s, the points sent when the host changed them, the host's
    /// watch on its room.
    /// </summary>
    void Tick();
    /// <summary>The room's tick stops (the match starts, the room is left).</summary>
    void StopTick() const;
    /// <summary>The room left: the 1998 room's points' boxes' timers removed, as the original did (OB-167).</summary>
    void StopBoxTimers();

    /// <summary>
    /// Port-only (the personal build): the host shows its room to every player again, as a player joining needs (the
    /// original's GO did it once): each game's screen starts afresh (<c>SwitchScreen</c>) and the host sends its picks
    /// again in the messages every build takes: the teams, the mission (every player asked for its file again), the
    /// tech bases, the points and the match options.
    /// </summary>
    void ShowRoomAgain();

    /// <summary>Port-only (the personal build): whether every player in the session is on the screen.</summary>
    bool AllShown() const;

    /// <summary>
    /// Port-only (the personal build): on the host, whether guest <paramref name="playerId"/> was shown the room with its
    /// game up (it had pinged), so no showing again waits for it.
    /// </summary>
    bool ShownWithGameUp(uint32_t playerId) const;

    /// <summary>Port-only (Redux HD): the host's picks as they stand, the players by name (a rematch's, network/MCRoomEnd.h).</summary>
    MCRoomPicks Picks() const;

    /// <summary>
    /// Port-only (Redux HD): the host's room, hosted again, takes <paramref name="picks"/>: the mission (asked of everyone
    /// there), the points and tech bases unless it fixes them, and each player there back in its slot by name.
    /// </summary>
    void TakePicks(const MCRoomPicks& picks);

    /// <summary>
    /// Port-only (Redux HD): each player on the screen whose name has a slot in <paramref name="picks"/>, on no team, goes
    /// back to it if it is free, told to everyone.
    /// </summary>
    void PlaceByName(const MCRoomPicks& picks);

    /// <summary>
    /// The session's player ids for the name slots: <paramref name="ids"/> ascending, the empty slots
    /// (<see cref="NoPlayer"/>) after them.
    /// </summary>
    static std::array<uint32_t, MaxPlayers> SortedPlayerIds(std::span<const uint32_t> ids);

    /// <summary>
    /// Moves <paramref name="playerId"/> to team <paramref name="team"/> (0 = unassigned) at <paramref name="slot"/>;
    /// unless <paramref name="remote"/>, tells the other players.
    /// </summary>
    void AssignPlayer(uint32_t playerId, int8_t team, int8_t slot, bool remote);

    /// <summary>The map for mission file <paramref name="fileName"/> (none when empty).</summary>
    void SetMap(std::string_view fileName);

    void SetMissionName(std::string_view name);

    void SetMapName(std::string_view name);

    /// <summary>A player has the mission: when all have, the start button lights.</summary>
    void SomeoneCheckedIn();

    /// <summary>
    /// Player <paramref name="playerId"/> reports whether it has the mission file (<paramref name="check"/>, as the host
    /// checked it). When every player has: the load goes on (port: once the host is told whose game couldn't check its
    /// data); else the players without it are named, and (port) those whose data differ from the host's.
    /// </summary>
    void FileReport(uint32_t playerId, MCFileCheck check);

    /// <summary>
    /// Port-only (the personal build): player <paramref name="playerId"/> is gone from the room for good (it left before
    /// the match, or a guest away updating didn't rejoin in time): its name and its place on a team go, here only.
    /// </summary>
    void RemovePlayer(uint32_t playerId);

    /// <summary>
    /// Port-only (Redux HD): the room's host has gone and its session rolled over to this game, which hosts the room
    /// now: the host's controls come to this room, the match options are what the room played, and the room as it
    /// stands (every player's picks, the mission) is shown to everyone again from here.
    /// </summary>
    void TakeOverRoom();

    /// <summary>Loads mission <paramref name="fileName"/> chosen by the host and tells the others.</summary>
    void LoadMission(std::string_view fileName);

    /// <summary>Forgets the loaded mission.</summary>
    void CancelMission();

    /// <summary>
    /// Port: copies who is on each team, the tech bases and the points into <c>MultiPlayer()->Match</c>, which is all the
    /// mech bay reads of the match. Called as the match starts, host and guest. (A configured match fills it itself and
    /// never shows this room: see <c>MPGameConfiguration</c>.)
    /// </summary>
    void PublishMatchState();

    /// <summary>
    /// Lights the start when every player is on a team and a mission is loaded. Port-only (the personal build): and
    /// every player in the session is on the screen (a player who just joined is shown first), and no guest is away
    /// updating, for a while at most in all (network/MCLobbyUpdate.h); while one is, <see cref="StartWaits"/> says whom
    /// the room waits for, in the update's line's place.
    /// </summary>
    void CheckGoodToGo();

    /// <summary>Sets team <paramref name="team"/>'s tech base (1 Inner Sphere, -1 Clan) and its toggles.</summary>
    void SetTeamTechBase(int8_t team, int8_t techBase);

    /// <summary>
    /// A tech base toggle's press: sets team <paramref name="team"/>'s tech base and, on the host, tells the others.
    /// </summary>
    void PickTechBase(int8_t team, int8_t techBase);

    /// <summary>The start's press: starts the loaded mission (asking first while a guest is away updating).</summary>
    void Begin();

    /// <summary>Enables the host's controls.</summary>
    void ControlsOn();

    /// <summary>Disables the host's controls (a guest's room).</summary>
    void ControlsOff();

    /// <summary>Greys the forces' controls while <paramref name="lock"/>.</summary>
    void LockControls(bool lock);

    /// <summary>Sets team 1's resource points.</summary>
    void SetTeam1RP(int32_t resourcePoints);

    /// <summary>Sets team 2's resource points.</summary>
    void SetTeam2RP(int32_t resourcePoints);

    /// <summary>A guest told team <paramref name="team"/>'s points by the host (its handler drew the room at once).</summary>
    void TakePoints(int32_t resourcePoints, int32_t team);

    /// <summary>The mission's files answered: the view's own mission pick put back, if it shows.</summary>
    void LeftMissionPick() const;

    /// <summary>The seat of <paramref name="playerId"/> (as the 1998 name slots were found: the first with that id).</summary>
    std::optional<size_t> SeatIndexOf(uint32_t playerId) const;

private:
    /// <summary>The view's <paramref name="tell"/>, when the room has a view.</summary>
    template <typename F> void ToView(F&& tell) const;

    /// <summary>Seat <paramref name="seat"/>'s name shown or hidden, on the view too.</summary>
    void ShowSeat(size_t seat, bool shown);

    /// <summary>Lays the players on no team down the list of them (see the callers for the skips).</summary>
    void LayOutUnassigned(uint32_t skipId, size_t names, bool skipEmpty);

    /// <summary>
    /// Port-only (the personal build): the host's tech bases (told to everyone) and points again, unless the mission
    /// fixes them.
    /// </summary>
    void SetTeamsAgain(const std::array<int8_t, 2>& techBases, const std::array<int32_t, 2>& points);

    /// <summary>
    /// Port-only (the personal build): the host's watch on its room, each tick: a configured match's start, and a player
    /// who joined (or a guest whose game was not yet up when it was shown the room) shown the room
    /// (<see cref="ShowRoomAgain"/>) once its game is up (it pinged, <c>MCFidpPlayer::HeardPing</c>), or after
    /// <c>ShowAnywayMs</c>, as long as nothing is under way in the room.
    /// </summary>
    void WatchRoom();

    /// <summary>
    /// The answers to the mission file's inquiry so far: when every player has the file, the load goes on (port: once
    /// the host is told whose game couldn't check its data); when none is still waited for and some lack it, they are
    /// named and the mission is called off.
    /// </summary>
    void FilesAnswered();

    /// <summary>Team <paramref name="team"/>'s points set, and its box's text.</summary>
    void SetPointsOf(int8_t team, int32_t resourcePoints);

    std::weak_ptr<MCRoomView> _View;
    /// <summary>The GUI object the room's tick's timer goes to.</summary>
    MCGuiRef<MCRoomTicker> _Ticker;
};

/// <summary>
/// Port-only (Redux HD): the host's picks for its room, its players by name: a rematch's room, hosted again, takes them
/// (network/MCRoomEnd.h), each player back in its slot as it returns.
/// </summary>
struct MCRoomPicks
{
    /// <summary>The mission, as the host loaded it (MCRoomModel::LoadMission's name); empty: none.</summary>
    std::string Mission;
    /// <summary>Each team's players' names by slot; empty: no one.</summary>
    std::array<std::string, MCRoomModel::TeamSlots> Team1;
    std::array<std::string, MCRoomModel::TeamSlots> Team2;
    std::array<int8_t, 2> TechBases = {MCRoomModel::InnerSphere, MCRoomModel::Clan};
    std::array<int32_t, 2> Points = {};
};
