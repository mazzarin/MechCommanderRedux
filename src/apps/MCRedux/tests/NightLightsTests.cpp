#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "engine/MCByteFlag.h"
#include "main/MCGameContext.h"
#include "mission/MCNightLights.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "network/MCMultiPlayerMessages.h"
#include "object/MCBattleMech.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectTypeManager.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCTerrain.h"

// Port-only (Redux HD): the units' headlights at night change sight (mission/MCNightLights.h).

namespace
{
    /// <summary>The game system's day table, every level <paramref name="range"/> vertices.</summary>
    std::array<int32_t, VisualRangeTableSize> DayTable(int32_t range)
    {
        std::array<int32_t, VisualRangeTableSize> day{};
        day.fill(range);
        return day;
    }

    /// <summary>A Lights message as the network carries it: player <paramref name="checkInId"/>'s units by part.</summary>
    std::vector<uint8_t> LightsBytes(int8_t checkInId, bool on, std::initializer_list<uint16_t> parts)
    {
        MCMPLightsMessage message;
        message.CheckInId = checkInId;
        message.On = on ? 1 : 0;

        for (const uint16_t part : parts)
        {
            message.Parts[message.Count++] = part;
        }

        std::vector<uint8_t> bytes(sizeof(message));
        std::memcpy(bytes.data(), &message, sizeof(message));
        return bytes;
    }

    /// <summary>The map vertex (row, column) <paramref name="at"/> stands on, as MCTerrain::MarkSeen finds it.</summary>
    std::pair<int32_t, int32_t> VertexOf(const MCVector3D& at)
    {
        const auto col =
            static_cast<int32_t>(std::floor(MCTerrain::OneOvermetersPerVertex * (at.X - MCTerrain::MapTopLeft3d100.X)));
        const auto row =
            static_cast<int32_t>(std::floor(MCTerrain::OneOvermetersPerVertex * (MCTerrain::MapTopLeft3d100.Y - at.Y)));
        return {row, col};
    }

    /// <summary>
    /// Whether side bits <paramref name="bits"/> show the vertex <paramref name="rows"/> down and <paramref name="cols"/>
    /// right of <paramref name="vertex"/> (row, column).
    /// </summary>
    bool Shown(const MCByteFlag& bits, std::pair<int32_t, int32_t> vertex, int32_t rows, int32_t cols)
    {
        return bits.GetFlag(static_cast<uint32_t>(vertex.first + rows), static_cast<uint32_t>(vertex.second + cols));
    }
}

/// <summary>
/// At night, with the rule, every unit sees half the day's sight all round and a lit unit's beam 85% of it, the enemy
/// seeing a lit unit from 85% of its own; the weather's share comes on top of both. By day, at dusk (no headlights) or
/// without the rule, the sight is the original's.
/// </summary>
TEST_CASE("night lights: at night units see half the day's sight round them and a lit unit's beam 85% of it")
{
    const std::array<int32_t, VisualRangeTableSize> day = DayTable(20);
    const std::optional<MCNightLights::MCLitSight> night = MCNightLights::SightFor(true, MCTimeOfDay::Night, day, 1.0);
    REQUIRE(night.has_value());
    CHECK_EQ(night->Round[0], 10);
    CHECK_EQ(night->Beam[0], 17);
    CHECK_EQ(night->SeenFromShare, MCNightLights::BeamSight);

    // A blizzard (70% of the sight) on both.
    const std::optional<MCNightLights::MCLitSight> blizzard =
        MCNightLights::SightFor(true, MCTimeOfDay::Night, day, 0.7);
    REQUIRE(blizzard.has_value());
    CHECK_EQ(blizzard->Round[VisualRangeTableSize - 1], 7);
    CHECK_EQ(blizzard->Beam[VisualRangeTableSize - 1], 12);
    CHECK(std::abs(blizzard->SeenFromShare - MCNightLights::BeamSight * 0.7) < 1e-9);

    CHECK(!MCNightLights::SightFor(true, MCTimeOfDay::Day, day, 1.0).has_value());
    CHECK(!MCNightLights::SightFor(true, MCTimeOfDay::Dusk, day, 1.0).has_value());
    CHECK(!MCNightLights::SightFor(false, MCTimeOfDay::Night, day, 1.0).has_value());
}

/// <summary>
/// A client takes a player's LIGHTS only from the server, for that player's own units, and only in a match that plays
/// the rule; the server takes them only from the player whose units they are (here a sender without a player number).
/// </summary>
TEST_CASE("night lights: a client turns the lights the server sends for a player's own units, and drops any other")
{
    MCTestContextScope scope;
    scope.Context().SetScenario(std::make_unique<MCScenario>());
    scope.Context().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
    scope.Context().SetObjectSystem(std::make_shared<MCObjectSystem>(std::make_unique<MCObjectTypeManager>()));
    const std::shared_ptr<MCMultiPlayer> game = scope.Context().MultiPlayer();
    NumLanPlayers = 2;
    game->InMission = true;
    game->ServerID = 7;
    game->Played.Rules = MCMatchRule::NightLights;

    const auto mine = std::make_shared<MCBattleMech>();
    mine->PartId = MCMover::FirstPartId;
    mine->SetCommanderId(1);
    const auto theirs = std::make_shared<MCBattleMech>();
    theirs->PartId = MCMover::FirstPartId + 1;
    theirs->SetCommanderId(2);
    MoverRoster[0] = mine;
    MoverRoster[1] = theirs;

    const auto mineId = static_cast<uint16_t>(mine->PartId);
    const auto theirsId = static_cast<uint16_t>(theirs->PartId);
    HandleAppLights(9, LightsBytes(1, false, {mineId}));
    CHECK(mine->LightsOn);
    HandleAppLights(7, LightsBytes(1, false, {mineId}));
    CHECK(!mine->LightsOn);
    HandleAppLights(7, LightsBytes(1, false, {theirsId}));
    CHECK(theirs->LightsOn);

    game->Played.Rules = 0u;
    HandleAppLights(7, LightsBytes(1, true, {mineId}));
    CHECK(!mine->LightsOn);

    game->Played.Rules = MCMatchRule::NightLights;
    game->IsServer = true;
    HandleAppLights(5, LightsBytes(1, true, {mineId}));
    CHECK(!mine->LightsOn);

    // Outside a match's rule the player's LIGHTS ask for nothing.
    game->Played.Rules = 0u;
    CHECK(!MCNightLights::Toggle(std::array{std::static_pointer_cast<MCMover>(mine)}));

    MoverRoster[0].reset();
    MoverRoster[1].reset();
    NumLanPlayers = 1;
}

/// <summary>
/// In mission 1 at night, with the rule: a lit mech's beam shows the ground ahead of it (turned with its torso), not
/// behind or to the side; a dark one shows nothing; a lit unit that hasn't moved marks again only after a second; the
/// clan's Uller, lit, is given away to the player's mechs within the beam's reach, and not from further. Outside a
/// match the player's LIGHTS turn the selection off, and on again once all are off.
/// </summary>
TEST_CASE_ISOLATED("night lights: a lit mech's beam shows the ground ahead and a lit unit is given away within reach")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const auto lead = std::dynamic_pointer_cast<MCBattleMech>(GetMoverFromPartId(MCMover::FirstPartId));
    const std::shared_ptr<MCMover> uller = GetMoverFromPartId(896);
    REQUIRE(lead != nullptr);
    REQUIRE(uller != nullptr);

    const std::optional<MCNightLights::MCLitSight> sight =
        MCNightLights::SightFor(true, MCTimeOfDay::Night, VisualRangeTable, 1.0);
    REQUIRE(sight.has_value());
    MCNightLights::SetSight(sight);
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    MCByteFlag& ours = *terrain->ISVisibleBits;
    // The vertices checked ahead lie inside the beam at any elevation level a looker can stand at (6 bits; the lowest
    // ground sees 4 vertices by day, its beam 3 at night).
    constexpr int32_t step = 2;
    constexpr size_t levels = 64;
    REQUIRE(std::ranges::min(std::span(sight->Beam).first(levels)) > step);

    // The lead faces along the grid's columns (+x), its torso straight.
    MCTestGame::FaceMech(*lead, 45.0f);
    const std::pair<int32_t, int32_t> at = VertexOf(lead->Position);
    ours.ResetAll(false);
    MCNightLights::Mark(*lead, 0.0f, true);
    CHECK(Shown(ours, at, 0, 0));
    CHECK(Shown(ours, at, 0, step));
    CHECK(!Shown(ours, at, 0, -step));
    CHECK(!Shown(ours, at, -step, 0));

    // Its torso turned a quarter counterclockwise: up the map.
    ours.ResetAll(false);
    MCNightLights::Mark(*lead, 90.0f, true);
    CHECK(Shown(ours, at, -step, 0));
    CHECK(!Shown(ours, at, 0, step));

    // Not moved, within the second: nothing marked again.
    ours.ResetAll(false);
    MCNightLights::Mark(*lead, 0.0f, false);
    CHECK(!Shown(ours, at, 0, step));

    // Dark: nothing.
    const std::array<std::shared_ptr<MCMover>, 1> leadAlone{lead};
    MCNightLights::SetLights(leadAlone, false);
    MCNightLights::Mark(*lead, 0.0f, true);
    CHECK(!Shown(ours, at, 0, step));

    // The player's LIGHTS (the lead dark, its mate lit): off while any is on, then on again once all are off.
    const std::array<std::shared_ptr<MCMover>, 2> pair{lead, GetMoverFromPartId(MCMover::FirstPartId + 1)};
    REQUIRE(pair[1] != nullptr);
    CHECK(MCNightLights::Toggle(pair));
    CHECK(!lead->LightsOn);
    CHECK(!pair[1]->LightsOn);
    CHECK(MCNightLights::Toggle(pair));
    CHECK(lead->LightsOn);
    CHECK(pair[1]->LightsOn);

    // The Uller, lit: where it starts, beyond the beam's reach of every one of the player's mechs, it's not given away.
    const double reach = Scenario()->MaxVisualRange * sight->SeenFromShare;

    for (int32_t part = MCMover::FirstPartId; part < MCMover::FirstPartId + 3; part++)
    {
        MCVector3D mechAt = GetMoverFromPartId(part)->Position;
        REQUIRE(uller->DistanceFrom(mechAt) > reach);
    }

    REQUIRE(MCNightLights::Shines(*uller));
    ours.ResetAll(false);
    MCNightLights::Mark(*uller, 0.0f, true);
    CHECK(!Shown(ours, VertexOf(uller->Position), 0, 0));

    // Brought within reach of the lead, it is.
    MCVector3D near = lead->Position;
    near.X += static_cast<float>(reach * 0.5);
    uller->Position = near;
    ours.ResetAll(false);
    MCNightLights::Mark(*uller, 0.0f, true);
    CHECK(Shown(ours, VertexOf(uller->Position), 0, 0));

    MCNightLights::SetSight(std::nullopt);
}
