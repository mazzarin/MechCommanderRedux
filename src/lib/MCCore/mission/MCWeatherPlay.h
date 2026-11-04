#pragma once

#include "sprite/MCWeather.h"

#include <cstdint>

/// <summary>
/// Port-only: what a weather changes in play, in a match whose every player's game plays by it
/// (MCMatchRule::WeatherPlay; MCScenario::Load sets it from MCMatchWeatherPlay). Outside such a match, and in clear
/// weather, nothing: the original's play exactly. Each is a multiplier or an addend on the original's numbers, so no
/// dice are rolled that the original didn't roll.
/// </summary>
struct MCWeatherPlay
{
    /// <summary>The share of the mission's MaxFireBurnTime a flame burns for (MCFire).</summary>
    float Burn = 1.0f;
    /// <summary>The chance (percent) a fire spreads to what it touches (MCFireType::HandleCollision; the original's 10).</summary>
    int32_t Spread = 10;
    /// <summary>
    /// Added to an attack's modifiers (percentage points) at a weapon's long range (MCMover::CalcAttackChance,
    /// MCTurret::CalcAttackChance); nearer, nothing.
    /// </summary>
    float LongAim = 0.0f;
    /// <summary>The share of the sight table's ranges units see (MCScenario::Load, MCSightCut).</summary>
    double Sight = 1.0;
    /// <summary>Added to a piloting or jumping check's roll out of 100 (MCBattleMech::PilotingCheck): a fall likelier.</summary>
    double Footing = 0.0;
    /// <summary>The share of its top and walking speeds a ground vehicle drives at (MCGroundVehicleDynamics).</summary>
    float VehicleSpeed = 1.0f;

    bool operator==(const MCWeatherPlay&) const = default;
};

/// <summary>Port-only: what <paramref name="kind"/> changes in play (clear: nothing).</summary>
MCWeatherPlay MCWeatherPlayOf(MCWeatherKind kind);

/// <summary>Port-only: sets what the weather changes in the mission being played (MCScenario::Load).</summary>
void MCSetWeatherPlay(const MCWeatherPlay& play);

/// <summary>Port-only: what the weather changes in the mission being played (nothing until one is set).</summary>
const MCWeatherPlay& MCWeatherPlayNow();
