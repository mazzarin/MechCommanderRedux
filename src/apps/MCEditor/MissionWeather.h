#pragma once

#include "resource.h"

#include <algorithm>
#include <array>
#include <cstddef>

/// <summary>
/// Port-only: a weather a mission can be set in (Mission > Weather), its NewMissions row's Weather. MCRedux shows it
/// with HD weather on (the .FIT's [Environment] block); it is only a look, and changes nothing in play. The original
/// game shows every mission under a clear sky.
/// </summary>
struct MissionWeather
{
    /// <summary>Its NewMissions Weather.</summary>
    long value;
    /// <summary>Its name in the .FIT's [Environment] Weather: MCRedux's own for it (MCWeather.cpp's Weathers).</summary>
    const char* name;
    /// <summary>The Mission > Weather command that sets it.</summary>
    UINT command;
};

/// <summary>Port-only: a clear sky's NewMissions Weather: a mission's until it is set in another, as all the original's are.</summary>
inline constexpr long MISSION_WEATHER_CLEAR = 0;

/// <summary>
/// Port-only: the weathers Mission > Weather offers, clear first, their commands one after another in this order (the
/// view handles them as a range). Another weather is a row here, and its command and menu items.
/// </summary>
inline constexpr auto MISSION_WEATHERS = std::to_array<MissionWeather>({
    {MISSION_WEATHER_CLEAR, "Clear", ID_MISSION_WEATHER_CLEAR},
    {1, "Rain", ID_MISSION_WEATHER_RAIN},
    {2, "Downpour", ID_MISSION_WEATHER_DOWNPOUR},
});

/// <summary>Port-only: Mission > Weather's first command and its last, the range the view handles.</summary>
inline constexpr UINT MISSION_WEATHER_FIRST_COMMAND = MISSION_WEATHERS.front().command;
inline constexpr UINT MISSION_WEATHER_LAST_COMMAND = MISSION_WEATHERS.back().command;

/// <summary>
/// Port-only: whether MISSION_WEATHERS is as the editor takes it: clear first, no value twice, and the commands one
/// after another.
/// </summary>
constexpr bool MissionWeathersInOrder()
{
    for (size_t i = 0; i < MISSION_WEATHERS.size(); ++i)
    {
        if (MISSION_WEATHERS[i].command != MISSION_WEATHER_FIRST_COMMAND + i ||
            std::ranges::count(MISSION_WEATHERS, MISSION_WEATHERS[i].value, &MissionWeather::value) != 1)
        {
            return false;
        }
    }

    return MISSION_WEATHERS.front().value == MISSION_WEATHER_CLEAR;
}

static_assert(MissionWeathersInOrder());

/// <summary>Port-only: the weather a NewMissions row's Weather, <paramref name="value"/>, sets: clear for any other.</summary>
inline const MissionWeather& MissionWeatherOf(long value)
{
    const auto found = std::ranges::find(MISSION_WEATHERS, value, &MissionWeather::value);
    return found != MISSION_WEATHERS.end() ? *found : MISSION_WEATHERS.front();
}
