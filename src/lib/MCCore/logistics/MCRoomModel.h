#pragma once

// Port-only (SPEC-menus.md's stage 4, "Models and views"): the multiplayer room's state, pulled out of the 1998 session
// screen (logistics/MCSessionScreen), and what the room's views read of it and ask of it: the teams' players by slot,
// their tech bases and resource points, the players on no team, the loaded mission, what the start waits for. Logistics
// owns it (MCLogistics::Room), so it lives as long as the room's screens; what the room sends and when is unchanged.

class MCRoomModel
{
public:
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
};
