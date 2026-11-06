#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "color/MCPalette.h"
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
#include "platform/MCRenderer.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCPaletteNight.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCTerrain.h"
#include "camera/MCCameraList.h"
#include "camera/MCViewWindow.h"

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

namespace
{
    /// <summary>
    /// A headlight's beam on the ground of a world: the ground under its lamp at (<paramref name="x"/>,
    /// <paramref name="y"/>), its middle meeting the ground <paramref name="ahead"/> pixels to the right, its lamp 12
    /// pixels high, as bright as a headlight's.
    /// </summary>
    MCSceneLight BeamAt(float x, float y, float ahead)
    {
        MCSceneLight beam;
        beam.X0 = beam.X1 = x;
        beam.Y0 = beam.Y1 = y;
        beam.Height = 12.0f;
        beam.SpotX = ahead;
        beam.SpotCos = std::cos(MCSceneLights::HeadlightDegrees * std::numbers::pi_v<float> / 180.0f);
        beam.Reach = 180.0f;
        beam.Red = beam.Green = beam.Blue = MCSceneLights::HeadlightLight();
        return beam;
    }

    /// <summary>The darkening table each pixel of a world <paramref name="width"/> wide takes from <paramref name="shades"/>, -1 for none; -2 where two cover it.</summary>
    std::vector<int32_t> DimsOf(std::span<const MCPaletteNight::MCShade> shades, int32_t width, int32_t height)
    {
        std::vector<int32_t> dims(static_cast<size_t>(width * height), -1);

        for (const MCPaletteNight::MCShade& shade : shades)
        {
            for (int32_t y = shade.Rect.Y0; y <= shade.Rect.Y1; y++)
            {
                for (int32_t x = shade.Rect.X0; x <= shade.Rect.X1; x++)
                {
                    int32_t& dim = dims[static_cast<size_t>(y * width + x)];
                    dim = dim == -1 ? shade.Dim : -2;
                }
            }
        }

        return dims;
    }
}

/// <summary>
/// The palette's night dims the whole world through the night's darkening table; a beam lifts it to none from its lamp
/// to where its middle meets the ground, fading back into the night past there and at its edges, and never behind its
/// lamp. The rectangles cover every pixel once.
/// </summary>
TEST_CASE("palette night: the world is dimmed through the night's table and a beam lifts it to the art as drawn")
{
    using MCPaletteNight::NightDim;
    CHECK_EQ(MCPaletteNight::DimFor(0.0f), NightDim);
    CHECK_EQ(MCPaletteNight::DimFor(1.0f), 0);
    CHECK_EQ(MCPaletteNight::DimFor(2.0f), 0);
    CHECK_EQ(MCPaletteNight::DimFor(0.5f), (NightDim + 1) / 2);

    constexpr int32_t width = 400;
    constexpr int32_t height = 200;
    constexpr float aspect = 0.5f;
    const std::vector<MCPaletteNight::MCShade> dark = MCPaletteNight::Shades(width, height, {}, aspect);
    REQUIRE_EQ(dark.size(), 1u);
    CHECK_EQ(dark[0].Dim, NightDim);
    CHECK_EQ(dark[0].Rect.X0, 0);
    CHECK_EQ(dark[0].Rect.Y1, height - 1);

    const std::array<MCSceneLight, 1> beams{BeamAt(100.0f, 100.0f, 70.0f)};
    const std::vector<int32_t> dims = DimsOf(MCPaletteNight::Shades(width, height, beams, aspect), width, height);
    CHECK(std::ranges::all_of(dims, [](int32_t dim) { return dim >= 0; }));
    const auto at = [&](int32_t x, int32_t y) { return dims[static_cast<size_t>(y * width + x)]; };

    // Toward where its middle meets the ground: the art as drawn; past there it fades, and it's gone by its reach.
    CHECK_EQ(at(150, 100), 0);
    CHECK(at(170, 100) <= 1);
    CHECK(at(230, 100) > at(170, 100));
    CHECK(at(230, 100) < NightDim);
    CHECK_EQ(at(285, 100), NightDim);
    // Behind its lamp, and well off its middle (45 degrees along the ground): the night.
    CHECK_EQ(at(60, 100), NightDim);
    CHECK_EQ(at(170, 135), NightDim);
}

/// <summary>
/// The palette draws the night where the mission plays at night (its own time, or the match's host's) and HD lighting
/// doesn't draw it: HD LIGHTING off, or on with the CPU drawing.
/// </summary>
TEST_CASE("palette night: shown at night where HD lighting doesn't draw it")
{
    const int32_t lighting = GHdLighting;
    MCSceneLights::SetMatchTime(std::nullopt);
    MCSceneLights::SetMissionTime(MCTimeOfDay::Night);
    GHdLighting = HdLightingOff;
    CHECK(MCPaletteNight::Shown());
    GHdLighting = HdLightingOn;
    CHECK_EQ(MCPaletteNight::Shown(), MCRenderer::GpuDrawing() == MCGpuDrawing::Off);
    GHdLighting = HdLightingOff;
    MCSceneLights::SetMissionTime(MCTimeOfDay::Dusk);
    CHECK(!MCPaletteNight::Shown());
    MCSceneLights::SetMatchTime(MCTimeOfDay::Night);
    CHECK(MCPaletteNight::Shown());
    MCSceneLights::SetMatchTime(std::nullopt);
    MCSceneLights::SetMissionTime(MCTimeOfDay::Day);
    CHECK(!MCPaletteNight::Shown());
    GHdLighting = lighting;
}

/// <summary>
/// Mission 1 at night on the CPU, HD lighting off: the world is the day's through the night's darkening table, but for
/// the ground the lead mech's headlight shows as drawn. With --shots, the day's and the night's pictures.
/// </summary>
TEST_CASE_ISOLATED(
    "palette night: mission 1 at night is dimmed through the palette and a lit mech's beam shows the art")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Off);
    REQUIRE(MCTestGame::StartMission(1));
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::Off);
    GHdLighting = HdLightingOff;
    const std::shared_ptr<MCViewWindow> view = Eye()->View();
    REQUIRE(view != nullptr);
    const auto lead = std::dynamic_pointer_cast<MCBattleMech>(GetMoverFromPartId(MCMover::FirstPartId));
    REQUIRE(lead != nullptr);
    MCTestGame::FaceMech(*lead, 0.0f);
    Eye()->ChangeTarget(lead, true);
    const float frame = 1.0f / 15.0f;

    for (int32_t settle = 0; settle < 4; settle++)
    {
        MCTestGame::RunFrame(frame);
    }

    const std::vector<uint8_t> day(view->WorldWindow->Bytes().begin(), view->WorldWindow->Bytes().end());
    MCScreenInput::SaveShot("palette_night_day", MCScreenInput::ScreenHash());
    MCSceneLights::SetMissionTime(MCTimeOfDay::Night);
    MCTestGame::RunFrame(frame);
    const std::span<const uint8_t> night = view->WorldWindow->Bytes();
    MCScreenInput::SaveShot("palette_night", MCScreenInput::ScreenHash());
    REQUIRE_EQ(night.size(), day.size());

    // Most of the world (all but the beams and what moved): the day's through the night's table.
    const MCColorTable table = *GamePalette().GetHazePalette(-MCPaletteNight::NightDim);
    size_t dimmed = 0;

    for (size_t p = 0; p < day.size(); p++)
    {
        dimmed += night[p] == table[day[p]] ? 1 : 0;
    }

    CHECK(static_cast<double>(dimmed) > 0.9 * static_cast<double>(day.size()));

    // The ground just short of where the lead's beam's middle meets the ground (its pool's brightest): as drawn.
    const auto actor = std::static_pointer_cast<MCMechActor>(lead->Appearance);
    const std::optional<MCSceneLights::MCHeadlight> headlight = MCSceneLights::HeadlightOf(
        *actor, lead->Position, lead->GetFrame(), lead->TorsoRotation, lead->CockpitPosition());
    REQUIRE(headlight.has_value());
    const MCSceneLight& beam = headlight->Beam;
    const int32_t width = view->WorldWidth();
    size_t asDrawn = 0;
    size_t looked = 0;

    for (int32_t step = 16; step <= 18; step++)
    {
        const float share = static_cast<float>(step) / 20.0f;
        const auto x = static_cast<int32_t>(beam.X0 + beam.SpotX * share);
        const auto y = static_cast<int32_t>(beam.Y0 + beam.SpotY * share);
        REQUIRE(x >= 0);
        REQUIRE(x < width);
        REQUIRE(y >= 0);
        REQUIRE(y < view->WorldHeight());
        const size_t p = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
        asDrawn += night[p] == day[p] ? 1 : 0;
        looked++;
    }

    CHECK_EQ(asDrawn, looked);
}
