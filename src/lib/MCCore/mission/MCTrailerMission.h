#pragma once

// Port-only: the missions of the Redux HD trailer's staged battle (mission/MCStagedBattle.h), made from the game's own:
// the player's company made the ten pilots of the trailer's radio track, each in a mech of their own and in one group,
// set down two to a rank behind the first; for the canyon (mission 16, mis0304) also the other side's mechs made
// heavies waiting further along the terrace, the company set down at its west end and the hovercraft sent away. The
// game reads the mission so made in place of its own (MCFileSystem::PutImage), so any install plays the same battle.
// The edits are trailer_mission.py's (the trailer's tools), byte for byte.

#include <expected>
#include <string>
#include <string_view>

namespace MCTrailerMission
{
    /// <summary>Whether <paramref name="mission"/> (a mission file's name: mis0304, mis0503) has a trailer's force.</summary>
    bool Knows(std::string_view mission);

    /// <summary>The game path of mission file <paramref name="mission"/>: data\missions\&lt;mission&gt;.fit.</summary>
    std::string FilePath(std::string_view mission);

    /// <summary>
    /// Mission file <paramref name="mission"/>'s text <paramref name="original"/> (the game's own, its bytes as they are)
    /// with the trailer's force.
    /// </summary>
    /// <returns>The text, or why it couldn't be made (a block or key the edit needs is missing).</returns>
    std::expected<std::string, std::string> Make(std::string_view mission, std::string_view original);

    /// <summary>
    /// From now on the game reads mission file <paramref name="mission"/> with the trailer's force, made from the
    /// FastFiles' copy the first time it's opened (a mission it can't be made for reads as the game's own, the reason
    /// in the log).
    /// </summary>
    /// <returns>Nothing, or why not (no trailer's force for that mission).</returns>
    std::expected<void, std::string> Put(std::string_view mission);
}
