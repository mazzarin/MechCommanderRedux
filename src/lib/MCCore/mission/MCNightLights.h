#pragma once

// Port-only (Redux HD): the units' headlights at night change sight. Where the night cuts sight (a match playing the
// host's night with every player's game having the rule, MCMatchRule::NightLights, or a mission set at night), every
// unit sees NightRoundSight of the day's range all round; a lit unit's beam shows BeamSight of it ahead, and the
// enemy's units see a lit unit from BeamSight of the day's range, as a unit firing gives itself away. A dark unit (the
// player's LIGHTS) is seen only by what the enemy sees round itself or lights up. Sensors are untouched.

#include "mission/MCScenario.h"

#include <array>
#include <optional>
#include <span>

class MCMover;
enum class MCTimeOfDay : uint8_t;

namespace MCNightLights
{
    /// <summary>The share of the day's sight every unit has all round at night, lit or dark.</summary>
    inline constexpr double NightRoundSight = 0.5;
    /// <summary>The share of the day's sight a lit unit's beam shows ahead, and the enemy sees a lit unit from.</summary>
    inline constexpr double BeamSight = 0.85;
    /// <summary>The beam's half-width, in degrees to each side: as the drawn one (MCSceneLights' headlight).</summary>
    inline constexpr float BeamHalfDegrees = 22.0f;
    /// <summary>How often a lit unit that hasn't moved marks what its light shows again (its torso turns, foes come).</summary>
    inline constexpr float RemarkSeconds = 1.0f;

    /// <summary>The sight a night with the lights' rule plays: every unit's all round, and a lit unit's beam.</summary>
    struct MCLitSight
    {
        std::array<int32_t, VisualRangeTableSize> Round{};
        std::array<int32_t, VisualRangeTableSize> Beam{};
        /// <summary>The share of the game system's MaxVisualRange the enemy sees a lit unit from.</summary>
        double SeenFromShare = BeamSight;

        bool operator==(const MCLitSight&) const = default;
    };

    /// <summary>
    /// The sight a mission plays with the lights' rule (<paramref name="lightsRule"/>) at <paramref name="time"/>, from
    /// the game system's <paramref name="dayTable"/>, the weather's <paramref name="weatherShare"/> on top: none by
    /// day or at dusk (no headlights), nor without the rule.
    /// </summary>
    std::optional<MCLitSight> SightFor(bool lightsRule, MCTimeOfDay time,
                                       const std::array<int32_t, VisualRangeTableSize>& dayTable, double weatherShare);

    /// <summary>Sets the sight the mission being played has (MCScenario::Load): none, the original's.</summary>
    void SetSight(const std::optional<MCLitSight>& sight);

    /// <summary>Whether the mission being played plays the lights' rule.</summary>
    bool InForce();

    /// <summary>Whether <paramref name="unit"/>'s light shines: on (its LightsOn) and the unit running or starting up.</summary>
    bool Shines(const MCMover& unit);

    /// <summary>
    /// Marks what <paramref name="unit"/>'s light shows (its beam's wedge ahead, turned <paramref name="turn"/>
    /// degrees counterclockwise from its facing: a mech's torso's) for its side, and gives it away to the enemy's units
    /// within the beam's reach: when it has <paramref name="moved"/> a vertex, or <see cref="RemarkSeconds"/> after it
    /// last did. Nothing while the rule isn't in force or the light is off.
    /// </summary>
    void Mark(MCMover& unit, float turn, bool moved);

    /// <summary>
    /// Turns <paramref name="units"/>' lights <paramref name="on"/> or off; a light turned on marks what it shows at
    /// its unit's next update.
    /// </summary>
    void SetLights(std::span<const std::shared_ptr<MCMover>> units, bool on);

    /// <summary>
    /// The player's LIGHTS for <paramref name="units"/> (their selection): off if any is on, else on. Outside a match it
    /// turns them at once; in a match only where the match plays the rule, through the server (MCMultiPlayer::SendLights).
    /// </summary>
    /// <returns>Whether the lights were turned (or asked for).</returns>
    bool Toggle(std::span<const std::shared_ptr<MCMover>> units);
} // namespace MCNightLights
