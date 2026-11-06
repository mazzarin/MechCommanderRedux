#include "stdafx.h"
#include "mission/MCNightLights.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "object/MCMover.h"
#include "object/MCObjectList.h"
#include "object/MCObjectSystem.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The interface's click a tactical order plays (MCCommandParser::SendTacOrder).</summary>
    constexpr int32_t OrderSample = 0xf;

    std::optional<MCNightLights::MCLitSight>& Sight()
    {
        static std::optional<MCNightLights::MCLitSight> sight;
        return sight;
    }

    /// <summary>
    /// <paramref name="unit"/>'s facing on the ground, turned <paramref name="turn"/> degrees counterclockwise: its
    /// frame turned an eighth of a turn, the facing its art is drawn at (as MCSceneLights::AddHeadlight's).
    /// </summary>
    std::pair<float, float> Facing(const MCMover& unit, float turn)
    {
        const float eighth = std::numbers::sqrt2_v<float> * 0.5f;
        const float bodyX = (unit.Frame.I.X - unit.Frame.J.X) * eighth;
        const float bodyY = (unit.Frame.I.Y - unit.Frame.J.Y) * eighth;
        const float radians = turn * std::numbers::pi_v<float> / 180.0f;
        return {bodyX * std::cos(radians) - bodyY * std::sin(radians),
                bodyX * std::sin(radians) + bodyY * std::cos(radians)};
    }

    /// <summary>The side bits <paramref name="alignment"/>'s units mark (MCTerrain::MarkSeen's who), 0 for neither.</summary>
    uint8_t SideOf(int32_t alignment)
    {
        return alignment == 1 ? 1 : alignment == -1 ? 2 : 0;
    }

    /// <summary>
    /// Gives a lit <paramref name="unit"/> away to the enemy's units within the beam's reach of it (the side's mech list
    /// holds its mechs, vehicles and elementals): the ground round it marked as theirs, as a unit firing is
    /// (MCBattleMechCombat.cpp's RevealFiring).
    /// </summary>
    void GiveAway(MCMover& unit, const MCNightLights::MCLitSight& sight)
    {
        const std::shared_ptr<MCObjectList> enemies = unit.Alignment == 1    ? ClanMechList()
                                                      : unit.Alignment == -1 ? InnerSphereMechList()
                                                                             : nullptr;
        const uint8_t seenBy = SideOf(-unit.Alignment);
        const std::shared_ptr<MCScenario> scenario = Scenario();

        if (enemies == nullptr || seenBy == 0 || scenario == nullptr)
        {
            return;
        }

        const double reach = scenario->MaxVisualRange * sight.SeenFromShare;

        for (const std::shared_ptr<MCBaseObject>& enemy : *enemies)
        {
            auto& foe = static_cast<MCGameObject&>(*enemy);
            MCVector3D foePosition = foe.GetPosition();

            if (foe.IsDisabled() == 0 && unit.DistanceFrom(foePosition) < reach)
            {
                Terrain()->MarkRadiusSeen(unit.Position, unit.Frame.J, 360.0f, scenario->FireVisualRange, seenBy);
                return;
            }
        }
    }
}

namespace MCNightLights
{
    std::optional<MCLitSight> SightFor(bool lightsRule, MCTimeOfDay time,
                                       const std::array<int32_t, VisualRangeTableSize>& dayTable, double weatherShare)
    {
        if (!lightsRule || time != MCTimeOfDay::Night)
        {
            return std::nullopt;
        }

        return MCLitSight{.Round = MCSightCut(dayTable, NightRoundSight * weatherShare),
                          .Beam = MCSightCut(dayTable, BeamSight * weatherShare),
                          .SeenFromShare = BeamSight * weatherShare};
    }

    void SetSight(const std::optional<MCLitSight>& sight)
    {
        Sight() = sight;
    }

    bool InForce()
    {
        return Sight().has_value();
    }

    bool Shines(const MCMover& unit)
    {
        return unit.LightsOn && MCSceneLights::HeadlightOn(unit.Status);
    }

    void Mark(MCMover& unit, float turn, bool moved)
    {
        const std::optional<MCLitSight>& sight = Sight();
        const uint8_t who = SideOf(unit.Alignment);

        if (!sight.has_value() || who == 0 || !Shines(unit) || unit.IsDisabled() != 0)
        {
            return;
        }

        if (!moved && unit.LitMarkedAt != MCMover::NeverLitMarked && ActualTime - unit.LitMarkedAt < RemarkSeconds)
        {
            return;
        }

        const auto [facingX, facingY] = Facing(unit, turn);
        Terrain()->MarkBeamSeen(unit.Position, facingX, facingY, BeamHalfDegrees, sight->Beam, who);
        GiveAway(unit, *sight);
        unit.LitMarkedAt = ActualTime;
    }

    void SetLights(std::span<const std::shared_ptr<MCMover>> units, bool on)
    {
        for (const std::shared_ptr<MCMover>& unit : units)
        {
            if (unit != nullptr && unit->LightsOn != on)
            {
                unit->LightsOn = on;
                unit->LitMarkedAt = MCMover::NeverLitMarked;
            }
        }
    }

    bool Toggle(std::span<const std::shared_ptr<MCMover>> units)
    {
        if (units.empty())
        {
            return false;
        }

        const bool on = std::ranges::none_of(units, [](const std::shared_ptr<MCMover>& unit)
                                             { return unit != nullptr && unit->LightsOn; });
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (game != nullptr && (game->Played.Rules.value_or(0) & MCMatchRule::NightLights) == 0)
        {
            return false;
        }

        // In a match, as many as the message carries (a player's movers), so every game turns the same ones.
        if (game != nullptr)
        {
            units = units.first(std::min(units.size(), MaxLightsParts));
        }

        if (game == nullptr || game->IsServer)
        {
            SetLights(units, on);
        }

        if (game != nullptr)
        {
            game->SendLights(game->CheckInId, units, on);
        }

        if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
        {
            sound->PlayInterfaceSample(OrderSample);
        }

        return true;
    }
} // namespace MCNightLights
