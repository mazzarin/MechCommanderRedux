#include "stdafx.h"
#include "mission/MCWeatherPlay.h"

namespace
{
    /// <summary>
    /// What each weather changes: wet ground and falling water or snow damp fires (a downpour nearly puts them out),
    /// heavy rain and driving snow hide what's far off (units see less far, and shots at a weapon's long range miss
    /// more), wet ground and snow make a mech likelier to fall when a check asks (a leg or gyro hit, a jump's landing),
    /// and vehicles drive slower on them. Sensors see as they do.
    /// </summary>
    constexpr MCWeatherPlay RainPlay{.Burn = 0.6f, .Spread = 5, .Footing = 5.0, .VehicleSpeed = 0.95f};
    constexpr MCWeatherPlay DownpourPlay{
        .Burn = 0.3f, .Spread = 2, .LongAim = -5.0f, .Sight = 0.8, .Footing = 10.0, .VehicleSpeed = 0.9f};
    constexpr MCWeatherPlay LightSnowPlay{.Burn = 0.8f, .Spread = 8, .Footing = 5.0, .VehicleSpeed = 0.95f};
    constexpr MCWeatherPlay SnowPlay{.Burn = 0.6f, .Spread = 5, .Sight = 0.9, .Footing = 10.0, .VehicleSpeed = 0.85f};
    constexpr MCWeatherPlay BlizzardPlay{
        .Burn = 0.4f, .Spread = 3, .LongAim = -5.0f, .Sight = 0.7, .Footing = 15.0, .VehicleSpeed = 0.75f};

    MCWeatherPlay& Now()
    {
        static MCWeatherPlay play;
        return play;
    }
}

MCWeatherPlay MCWeatherPlayOf(MCWeatherKind kind)
{
    switch (kind)
    {
        case MCWeatherKind::Rain:
            return RainPlay;
        case MCWeatherKind::Downpour:
            return DownpourPlay;
        case MCWeatherKind::LightSnow:
            return LightSnowPlay;
        case MCWeatherKind::Snow:
            return SnowPlay;
        case MCWeatherKind::Blizzard:
            return BlizzardPlay;
        case MCWeatherKind::Clear:
        default:
            return {};
    }
}

void MCSetWeatherPlay(const MCWeatherPlay& play)
{
    Now() = play;
}

const MCWeatherPlay& MCWeatherPlayNow()
{
    return Now();
}
