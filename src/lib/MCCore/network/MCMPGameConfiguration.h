#pragma once

// Port: a multiplayer match whose settings are all known before anyone connects (a launcher's command line, a script).
// The ready room, the map selector and the wait for the mission's settings exist only to collect these values, so a
// configured match skips them: "Waiting for Players" goes straight to the mech bay.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// <summary>A player a launcher placed on a team: the name it launches with (-player), the team and the slot.</summary>
struct MPGamePlayer
{
    std::string Name;
    /// <summary>1 or 2.</summary>
    int8_t Team = 1;
    /// <summary>0 to 2 (the slot also decides which drop zones the player gets).</summary>
    int8_t Slot = 0;
};

/// <summary>
/// Everything the ready room, the map selector and the settings wait would have collected. Fill it in (the host's
/// command line does: -matchmap, -team1rp, -team2rp, -team1tech, -team2tech, -slot) and the host starts the match with
/// <see cref="MPBeginConfiguredMatch"/> once every rostered player has joined.
/// </summary>
struct MPGameConfiguration
{
    bool Active = false;
    /// <summary>The mission's file name, without folder or extension.</summary>
    std::string Mission;
    /// <summary>Resource points for each player of team 1 and team 2; -1 keeps the mission file's.</summary>
    int32_t Team1RpPerPlayer = -1;
    int32_t Team2RpPerPlayer = -1;
    /// <summary>1 Inner Sphere, -1 Clan, 0 keeps the mission's.</summary>
    int8_t Team1TechBase = 0;
    int8_t Team2TechBase = 0;
    std::vector<MPGamePlayer> Roster;

    /// <summary>Whether the configuration can be run, or why not (<paramref name="why"/>).</summary>
    [[nodiscard]] bool Validate(std::string& why) const;
};

extern MPGameConfiguration GameConfiguration;

/// <summary>
/// What the mech bay needs of a match: who is on each team, the tech bases and each team's resource point pool. The
/// session screen fills it as the match starts (<see cref="MCSessionScreen::PublishMatchState"/>); a configured match
/// fills it from the configuration. Nothing reads the session screen's widgets after this.
/// </summary>
struct MPMatchState
{
    static constexpr size_t TeamSlots = 3;
    static constexpr uint32_t NoPlayer = 0xffffffff;

    std::array<uint32_t, TeamSlots> Team1Players = {NoPlayer, NoPlayer, NoPlayer};
    std::array<uint32_t, TeamSlots> Team2Players = {NoPlayer, NoPlayer, NoPlayer};
    /// <summary>1 Inner Sphere, -1 Clan.</summary>
    int8_t Team1TechBase = 1;
    int8_t Team2TechBase = -1;
    /// <summary>A team's pool, shared out among its players when the mission loads.</summary>
    int32_t Team1Points = 0;
    int32_t Team2Points = 0;

    /// <summary>
    /// The ids of the players on the local player's team (<paramref name="myTeam"/>) or the other, by slot.
    /// </summary>
    [[nodiscard]] std::vector<uint32_t> TeamPlayerIds(bool myTeam, uint32_t localId) const;
};

/// <summary>
/// Host: every player the roster names has joined. Checks the roster against the session and the mission against every
/// machine, then sends the match to the guests and enters the mech bay. A problem is logged and the host is left on the
/// "Waiting for Players" screen to carry on by hand.
/// </summary>
void MPBeginConfiguredMatch();

/// <summary>Host: called as the waiting screen refreshes; gives up on guests that never confirm the mission file.</summary>
void MPPollConfiguredMatch();

/// <summary>
/// A guest's (or the host's own) report of whether it has the mission file. Returns true when a configured match was
/// waiting for it (the session screen then ignores it).
/// </summary>
bool MPConfiguredMatchFileReport(uint32_t playerId, bool haveFile);

/// <summary>
/// Guest: the host's start-match message (see <c>MCMPStartMatchMessage</c>): takes the teams, tech bases and points,
/// and enters the mech bay.
/// </summary>
void MPHandleStartMatch(uint32_t fromId, std::span<const uint8_t> message);
