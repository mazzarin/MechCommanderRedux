#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/// <summary>
/// Port-only: a mission's time of day, its NewMissions row's TimeOfDay. MCRedux shows it with HD lighting on (the
/// .FIT's [Environment] block), and at dusk or night the mission's units see less of the map (its VisualRangeTable);
/// the original game plays every mission by day.
/// </summary>
/// <remarks>Mission > Time of Day's commands follow this order.</remarks>
enum class MissionTimeOfDay : long
{
    Day = 0,
    Dusk = 1,
    Night = 2,
};

/// <summary>Port-only: a NewMissions row's TimeOfDay, <paramref name="value"/>, as a time of day: day for any other.</summary>
inline MissionTimeOfDay MissionTimeOfDayOf(long value)
{
    return value == static_cast<long>(MissionTimeOfDay::Dusk) || value == static_cast<long>(MissionTimeOfDay::Night)
               ? static_cast<MissionTimeOfDay>(value)
               : MissionTimeOfDay::Day;
}

/// <summary>
/// The tiles around a unit it sees, by the elevation level of the ground under it (the editor's table at 0x00502918,
/// the retail data\missions\gamesys.fit's VisualRangeTable): rising over the levels listed, VISUAL_RANGE_MOST above,
/// VISUAL_RANGE_LEVELS levels in all.
/// </summary>
inline constexpr std::array<int32_t, 24> VISUAL_RANGES_RISING = {4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  10, 10,
                                                                 13, 13, 17, 17, 22, 22, 26, 31, 32, 34, 36, 38};
inline constexpr int32_t VISUAL_RANGE_MOST = 40;
inline constexpr int32_t VISUAL_RANGE_LEVELS = 0x100;
/// <summary>
/// Port-only: the share of the day's sight a dusk mission's units have, and a night one's; the least a level keeps, in
/// tiles.
/// </summary>
inline constexpr double DUSK_SIGHT = 0.8;
inline constexpr double NIGHT_SIGHT = 0.6;
inline constexpr int32_t LEAST_SIGHT = 1;

/// <summary>
/// The sight at elevation level <paramref name="level"/> (clamped to the table's) in a mission set at
/// <paramref name="time"/>: by day the game system's; at dusk or night (port-only) that scaled by DUSK_SIGHT or
/// NIGHT_SIGHT, the table the mission's .FIT then holds.
/// </summary>
inline int32_t MissionVisualRange(int32_t level, MissionTimeOfDay time)
{
    level = std::clamp(level, 0, VISUAL_RANGE_LEVELS - 1);
    const auto index = static_cast<size_t>(level);
    const int32_t range = index < VISUAL_RANGES_RISING.size() ? VISUAL_RANGES_RISING[index] : VISUAL_RANGE_MOST;

    if (time == MissionTimeOfDay::Day)
    {
        return range;
    }

    const double sight = time == MissionTimeOfDay::Dusk ? DUSK_SIGHT : NIGHT_SIGHT;
    // (std::max): windows.h's max macro.
    return (std::max)(static_cast<int32_t>(std::lround(range * sight)), LEAST_SIGHT);
}
