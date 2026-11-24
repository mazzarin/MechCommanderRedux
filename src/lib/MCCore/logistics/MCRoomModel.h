#pragma once

// Port-only (SPEC-menus.md's stage 4, "Models and views"): the multiplayer room's state, pulled out of the 1998 session
// screen (logistics/MCSessionScreen), which views it until the remade room does: the teams' players by slot, their tech
// bases and resource points, the players on no team, the loaded mission. Logistics owns it (MCLogistics::Room), so it
// lives as long as the room's screens; what the room sends and when is unchanged.

class MCRoomModel
{
public:
    /// <summary>The most players in a session (a game rule: the name slots and the player lights).</summary>
    static constexpr size_t MaxPlayers = 6;
    /// <summary>The slots of a team (a game rule).</summary>
    static constexpr size_t TeamSlots = 3;
    /// <summary>A player id that stands for no player (an empty slot).</summary>
    static constexpr uint32_t NoPlayer = 0xffffffff;

    /// <summary>Team 1's tech base (1 Inner Sphere, -1 Clan, as the original kept it).</summary>
    int8_t Team1TechBase = 1;
    /// <summary>Team 2's tech base.</summary>
    int8_t Team2TechBase = -1;
    /// <summary>The number of players in the session.</summary>
    int32_t NumPlayers = 0;
    /// <summary>The number of players on no team.</summary>
    int32_t NumUnassigned = 0;
    /// <summary>Team 1's players by slot (<see cref="NoPlayer"/> = empty).</summary>
    std::array<uint32_t, TeamSlots> Team1Players = {};
    /// <summary>Team 2's players by slot.</summary>
    std::array<uint32_t, TeamSlots> Team2Players = {};
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
};
