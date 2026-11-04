#pragma once

// Port: a multiplayer match whose settings are all known before anyone connects (a launcher's command line, a script).
// The ready room, the map selector and the wait for the mission's settings exist only to collect these values, so a
// configured match skips them: "Waiting for Players" goes straight to the mech bay.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class MCWeatherKind : uint8_t;
enum class MCTimeOfDay : uint8_t;

/// <summary>
/// Port: what a match's host chose beyond the original's settings (the session screen's MATCH OPTIONS; a launcher's
/// -matchweather, -matchtime and -matchbraking): the weather and time of day every player sees in place of the mission's
/// own (a player's HD weather or HD lighting off still shows the original's look), and the rules the host wants. Each is
/// played only as far as every player's build plays it (<c>MCPlayedOptions</c>).
/// </summary>
struct MCMatchOptions
{
    /// <summary>The weather every player sees (none: the mission's own).</summary>
    std::optional<MCWeatherKind> Weather;
    /// <summary>The time of day every player sees, and units see as far as then (none: the mission's own).</summary>
    std::optional<MCTimeOfDay> TimeOfDay;
    /// <summary>The rules the host wants (<c>MCMatchRule</c> bits; none: the server's own PREFS).</summary>
    std::optional<uint32_t> Rules;

    bool operator==(const MCMatchOptions&) const = default;
};

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
/// command line does: -matchmap, -team1rp, -team2rp, -team1tech, -team2tech, -slot, -matchweather, -matchtime,
/// -matchbraking) and the host starts the match with <see cref="MPBeginConfiguredMatch"/> once every rostered player has
/// joined.
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
    /// <summary>The match's weather, time of day and rules (-matchweather, -matchtime, -matchbraking, -matchpilot, the bans).</summary>
    MCMatchOptions Options;

    /// <summary>Whether the configuration can be run, or why not (<paramref name="why"/>).</summary>
    [[nodiscard]] bool Validate(std::string& why) const;
};

extern MPGameConfiguration GameConfiguration;

/// <summary>The command line's match options (<see cref="MPReadMatchOption"/>).</summary>
inline constexpr std::string_view MatchWeatherFlag = "-matchweather";
inline constexpr std::string_view MatchTimeFlag = "-matchtime";
inline constexpr std::string_view MatchBrakingFlag = "-matchbraking";
inline constexpr std::string_view MatchArtilleryFlag = "-matchartillery";
inline constexpr std::string_view MatchSensorsFlag = "-matchsensors";
inline constexpr std::string_view MatchProbesFlag = "-matchprobes";
inline constexpr std::string_view MatchMinesFlag = "-matchmines";
inline constexpr std::string_view MatchPilotFlag = "-matchpilot";
inline constexpr std::string_view MatchLrmsFlag = "-matchlrms";
inline constexpr std::string_view MatchSrmsFlag = "-matchsrms";
inline constexpr std::string_view MatchThunderboltsFlag = "-matchthunderbolts";
inline constexpr std::string_view MatchPpcsFlag = "-matchppcs";
inline constexpr std::string_view MatchLasersFlag = "-matchlasers";
inline constexpr std::string_view MatchPulseLasersFlag = "-matchpulselasers";
inline constexpr std::string_view MatchFlamersFlag = "-matchflamers";
inline constexpr std::string_view MatchAutocannonsFlag = "-matchautocannons";
inline constexpr std::string_view MatchGaussFlag = "-matchgauss";
inline constexpr std::string_view MatchMachineGunsFlag = "-matchmachineguns";
inline constexpr std::string_view MatchLongTomsFlag = "-matchlongtoms";

/// <summary>Whether <paramref name="word"/> is one of the command line's match option flags.</summary>
bool MPIsMatchOptionFlag(std::string_view word);

/// <summary>
/// Reads a match option of the command line into <paramref name="options"/>: <paramref name="flag"/> -matchweather
/// (clear, rain, downpour, lightsnow, snow or blizzard), -matchtime (day, dusk or night), -matchbraking (on or off),
/// any of them <c>mission</c> (or for braking <c>prefs</c>) for the choice it would have without, -matchpilot (elite, as
/// the original, veteran, regular or green: the most skilled pilot for hire), or an on/off ban flag (-matchartillery,
/// -matchprobes, -matchsensors, -matchmines, and each weapon family's: -matchlrms, -matchsrms, -matchthunderbolts,
/// -matchppcs, -matchlasers, -matchpulselasers, -matchflamers, -matchautocannons, -matchgauss, -matchmachineguns, -matchlongtoms: on,
/// as the original, or off), in any case.
/// </summary>
/// <returns>Whether <paramref name="value"/> was one of the flag's; another is logged and changes nothing.</returns>
bool MPReadMatchOption(std::string_view flag, std::string_view value, MCMatchOptions& options);

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
