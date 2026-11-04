#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "engine/MCByteFlag.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "mission/MCWeatherPlay.h"
#include "network/MCMultiPlayer.h"
#include "object/MCArtillery.h"
#include "object/MCBattleMech.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCCommander.h"
#include "object/MCForces.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCSensorSystem.h"
#include "platform/MCRenderer.h"
#include "sprite/MCWeather.h"
#include "terrain/MCTerrain.h"

#include <functional>

namespace
{
    /// <summary>The frames each mission runs (40 seconds of play at 15 frames a second).</summary>
    constexpr int32_t StateFrames = 600;

    /// <summary>
    /// Boots mission <paramref name="segment"/>, runs <see cref="StateFrames"/> frames and folds each frame's
    /// MCTestGame::StateHash into one hash, which must be <paramref name="expected"/>. --state-log lists the frames'
    /// hashes, to find the first frame two builds disagree on (tools/ci/baseline.py). Port-only: shown in
    /// <paramref name="weather"/> (GHdWeather, set once the mission has read the preferences).
    /// </summary>
    /// <summary>
    /// Boots mission <paramref name="segment"/> (then <paramref name="setup"/>, if any), runs <see cref="StateFrames"/>
    /// frames and folds each frame's MCTestGame::StateHash into one hash.
    /// </summary>
    uint32_t MissionStateHash(int32_t segment, int weather, const std::function<void()>& setup = {},
                              int32_t stateFrames = StateFrames)
    {
        REQUIRE(MCTestGame::StartMission(segment));
        GHdWeather = weather;

        if (setup)
        {
            setup();
        }

        uint32_t frames = 0x811c9dc5;
        std::set<uint32_t> distinct;

        for (int32_t frame = 0; frame < stateFrames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
            const uint32_t hash = MCTestGame::StateHash();
            frames = (frames ^ hash) * 0x01000193;
            distinct.insert(hash);
        }

        std::cout << std::format("  mission {}: state 0x{:08x}\n", segment, frames);
        // The game moved on every frame (the clock alone does that).
        CHECK_EQ(distinct.size(), static_cast<size_t>(stateFrames));
        return frames;
    }

    void MissionState(int32_t segment, uint32_t expected, int weather = HdWeatherOff)
    {
        CHECK_EQ(MissionStateHash(segment, weather), expected);
    }

    /// <summary>Mission 1's recorded run (the original's play).</summary>
    constexpr uint32_t MissionOneState = 0x9ff2e0b4u;

    /// <summary>Mission 1's battle staged (MCTestGame::OrderMissionOneAttack) runs two minutes for the weather's play.</summary>
    constexpr int32_t BattleFrames = 15 * 120;

    /// <summary>
    /// Mission 1's battle, staged, with <paramref name="play"/> in force from its start (MCWeatherPlay; its sight cut
    /// as the scenario's load cuts it): its state over <see cref="BattleFrames"/> frames.
    /// </summary>
    uint32_t MissionOneBattle(const MCWeatherPlay& play)
    {
        const uint32_t state = MissionStateHash(
            1, HdWeatherOff,
            [&play]
            {
                MCSetWeatherPlay(play);

                // As the scenario's load cuts it: not at all at the whole.
                if (play.Sight != 1.0)
                {
                    VisualRangeTable = MCSightCut(VisualRangeTable, play.Sight);
                }

                REQUIRE(MCTestGame::OrderMissionOneAttack() != nullptr);
            },
            BattleFrames);
        std::cout << std::format("  staged battle: 0x{:08x}\n", state);
        MCSetWeatherPlay({});
        return state;
    }

    /// <summary>The staged battle without any weather's play (the original's play: its hash in the plain run's test).</summary>
    constexpr uint32_t BattleState = 0x596bbd27u;

    /// <summary>Port-only: the staged battle with <paramref name="play"/> plays out unlike the original's.</summary>
    void ChangesMissionOne(const MCWeatherPlay& play)
    {
        CHECK(MissionOneBattle(play) != BattleState);
    }
}

/// <summary>
/// Mission 1's game state over 600 frames (its movers, pilots, dice, clock and objectives, hashed each frame) is the
/// recorded run: a change that alters gameplay changes it.
/// </summary>
TEST_CASE_ISOLATED("game: mission 1's game state matches the recorded run")
{
    if (MCTestGame::Available())
    {
        MissionState(1, MissionOneState);
    }
}

/// <summary>
/// Port-only (HD weather): mission 1's game state in a downpour drawn by the GPU (drops falling over the battle, splashing
/// on the ground and on the units' tops, dripping off them) is the recorded run: the weather never touches the game.
/// </summary>
TEST_CASE_ISOLATED("game: mission 1's game state in a downpour drawn by the GPU matches the recorded run")
{
    if (MCTestGame::Available())
    {
        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
        MissionState(1, MissionOneState, HdWeatherDownpour);
        CHECK(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
        CHECK(MCWeather::Count() > 0);
        GHdWeather = HdWeatherOff;
    }
}

/// <summary>
/// Port-only (HD weather's snow): mission 1's game state in a blizzard drawn by the GPU (flakes falling over the battle
/// and resting where they land, the snow settling, the footprints filling in) is the recorded run: the snow never
/// touches the game.
/// </summary>
TEST_CASE_ISOLATED("game: mission 1's game state in a blizzard drawn by the GPU matches the recorded run")
{
    if (MCTestGame::Available())
    {
        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
        MissionState(1, MissionOneState, HdWeatherBlizzard);
        CHECK(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
        CHECK(MCWeather::Count() > 0);
        GHdWeather = HdWeatherOff;
    }
}

/// <summary>Mission 2's game state over 600 frames is the recorded run.</summary>
TEST_CASE_ISOLATED("game: mission 2's game state matches the recorded run")
{
    if (MCTestGame::Available())
    {
        MissionState(2, 0x9f462502u);
    }
}

/// <summary>Mission 5's game state over 600 frames is the recorded run.</summary>
TEST_CASE_ISOLATED("game: mission 5's game state matches the recorded run")
{
    if (MCTestGame::Available())
    {
        MissionState(5, 0xf028994au);
    }
}

/// <summary>
/// Port-only: a match's blizzard (MCWeatherPlay, when every player's game plays it) changes mission 1's staged battle
/// by its long-range aim alone, and by its vehicles' speed alone. (Its fires, footing and sight are checked on their
/// own: the battle has no fire or piloting check, and every shot in it is fired well inside the blizzard's sight.)
/// </summary>
TEST_CASE_ISOLATED("game: mission 1's staged battle without the weather's play matches the recorded run")
{
    if (MCTestGame::Available())
    {
        CHECK_EQ(MissionOneBattle({}), BattleState);
    }
}

TEST_CASE_ISOLATED("game: a match's blizzard changes mission 1's play by its long-range aim")
{
    if (MCTestGame::Available())
    {
        ChangesMissionOne({.LongAim = MCWeatherPlayOf(MCWeatherKind::Blizzard).LongAim});
    }
}

/// <summary>
/// Port-only: a match's weather's footing (MCWeatherPlay::Footing) is added to a mech's piloting check's roll: a roll
/// pushed far past any pilot's skill fails, one far under it passes. (Mission 1's staged battle makes no check.)
/// </summary>
TEST_CASE_ISOLATED("game: a match's weather's footing makes a mech's piloting check likelier to fail")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCMover> mech = GetMoverFromPartId(0x200);
    REQUIRE(mech != nullptr);
    REQUIRE(mech->ObjectClass == MCObjectClass::BattleMech);
    constexpr double FarPast = 1000.0;

    for (const auto& [footing, falls] : {std::pair(FarPast, true), std::pair(-FarPast, false)})
    {
        MCSetWeatherPlay({.Footing = footing});
        mech->PilotingCheckPending = false;
        mech->PilotingCheck(0, 0.0f);
        CHECK_EQ(mech->PilotingCheckPending != 0, falls);
    }

    mech->PilotingCheckPending = false;
    MCSetWeatherPlay({});
}

TEST_CASE_ISOLATED("game: a match's blizzard changes mission 1's play by its vehicles' speed")
{
    if (MCTestGame::Available())
    {
        ChangesMissionOne({.VehicleSpeed = MCWeatherPlayOf(MCWeatherKind::Blizzard).VehicleSpeed});
    }
}

namespace
{
    /// <summary>The steps a sight's edge is looked for in (a meter, in world units, is finer than a map cell).</summary>
    constexpr float SightStepMeters = 1.0f;
    /// <summary>Farther than any level's sight reaches (the edge is looked for no further).</summary>
    constexpr float SightSearchMeters = 2000.0f;
    /// <summary>How far the two sides' kept shares of their sight may differ (each edge is found to a meter).</summary>
    constexpr double SightSidesSlack = 0.02;

    /// <summary>The spot <paramref name="reach"/> world units from <paramref name="at"/> toward <paramref name="to"/>.</summary>
    MCVector3D Along(const MCVector3D& at, const MCVector3D& to, float reach)
    {
        const float dx = to.X - at.X;
        const float dy = to.Y - at.Y;
        const float length = std::sqrt(dx * dx + dy * dy);
        REQUIRE(length > 0.0f);
        return MCVector3D(at.X + dx / length * reach, at.Y + dy / length * reach, at.Z);
    }

    /// <summary>
    /// Marks <paramref name="looker"/>'s side's seen ground afresh with <paramref name="table"/> as the sight table:
    /// cleared, then marked from where it stands, as a mech, vehicle or elemental marks it each map vertex it moves
    /// (MCTerrain::MarkSeen; a full circle, so no look vector).
    /// </summary>
    void MarkSightWith(MCMover& looker, const std::array<int32_t, VisualRangeTableSize>& table)
    {
        VisualRangeTable = table;
        const uint8_t who = looker.GetAlignment() == 1 ? 1 : 2;
        const std::shared_ptr<MCTerrain> terrain = Terrain();
        (who == 1 ? *terrain->ISVisibleBits : *terrain->ClanVisibleBits).ResetAll(false);
        terrain->MarkSeen(looker.GetPosition(), MCVector3D(), 360.0f, 0.0f, who);
    }

    /// <summary>
    /// How far <paramref name="looker"/>'s side sees toward <paramref name="toward"/> with <paramref name="table"/> as the
    /// sight table (MarkSightWith, then its side's line of sight walked out), in world units.
    /// </summary>
    float SightEdge(MCMover& looker, const MCVector3D& toward, const std::array<int32_t, VisualRangeTableSize>& table)
    {
        MarkSightWith(looker, table);
        const MCVector3D at = looker.GetPosition();
        const float step = SightStepMeters * WorldUnitsPerMeter;
        const float limit = SightSearchMeters * WorldUnitsPerMeter;
        float edge = 0.0f;

        while (looker.LineOfSight(Along(at, toward, edge + step)) != 0 && edge < limit)
        {
            edge += step;
        }

        REQUIRE(edge < limit);
        return edge;
    }
}

/// <summary>
/// Port-only: a match's blizzard's sight (MCWeatherPlay::Sight, cut into the sight table as the scenario's load cuts
/// it) shortens the computer's side's sight as much as the players'. Mission 1's Uller (the Clans', the computer's)
/// and the first of the player's mechs each mark their side's seen ground; each side's line of sight, which every
/// unit's fire check asks (a mech, vehicle, elemental or turret fires at nothing its side doesn't see) and the
/// mission scripts' lineofsight too, ends nearer by the same share, and a spot between the two edges is in the
/// Uller's sight in clear weather and out of it in the blizzard. (The table counts whole map cells: on mission 1's low
/// ground, 4 cut to 3, so the share kept is coarser than the weather's.)
/// </summary>
TEST_CASE_ISOLATED("game: a match's blizzard shortens the computer's sight as much as the players'")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr int32_t UllerPart = 896;
    constexpr int32_t PlayerMechPart = 0x200;
    const std::shared_ptr<MCMover> uller = GetMoverFromPartId(UllerPart);
    const std::shared_ptr<MCMover> mech = GetMoverFromPartId(PlayerMechPart);
    REQUIRE(uller != nullptr);
    REQUIRE(mech != nullptr);
    REQUIRE(uller->GetAlignment() == -1);
    REQUIRE(mech->GetAlignment() == 1);

    const std::array<int32_t, VisualRangeTableSize> clear = VisualRangeTable;
    const std::array<int32_t, VisualRangeTableSize> blizzard =
        MCSightCut(clear, MCWeatherPlayOf(MCWeatherKind::Blizzard).Sight);

    std::vector<double> kept;

    for (const auto& [looker, toward] : {std::pair(uller, mech), std::pair(mech, uller)})
    {
        const float clearEdge = SightEdge(*looker, toward->GetPosition(), clear);
        const float blizzardEdge = SightEdge(*looker, toward->GetPosition(), blizzard);
        std::cout << std::format("  side {}: sees {:.0f} m clear, {:.0f} m in a blizzard\n", looker->GetAlignment(),
                                 clearEdge / WorldUnitsPerMeter, blizzardEdge / WorldUnitsPerMeter);
        CHECK(blizzardEdge < clearEdge);
        kept.push_back(static_cast<double>(blizzardEdge / clearEdge));
    }

    CHECK(std::abs(kept[0] - kept[1]) < SightSidesSlack);

    // Halfway between the two edges, the Uller's side sees the spot only in clear weather.
    const MCVector3D to = mech->GetPosition();
    const float between = (SightEdge(*uller, to, clear) + SightEdge(*uller, to, blizzard)) / 2.0f;
    const MCVector3D spot = Along(uller->GetPosition(), to, between);
    MarkSightWith(*uller, clear);
    CHECK(uller->LineOfSight(spot) != 0);
    MarkSightWith(*uller, blizzard);
    CHECK(uller->LineOfSight(spot) == 0);
    VisualRangeTable = clear;
}

/// <summary>
/// Port-only: a match without sensors (SENSORS OFF, MCMatchRule::NoSensors) turns off the units' sensors
/// (MCSensorSystem::Enabled), but a commander's sensor probe, dropped by the player's mech, still senses: the probes
/// are their own option (PROBES OFF, MCMatchRule::NoProbes), which gives commanders none.
/// </summary>
TEST_CASE_ISOLATED("game: a match without sensors keeps a commander's sensor probe sensing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr int32_t PlayerMechPart = 0x200;
    const std::shared_ptr<MCMover> mech = GetMoverFromPartId(PlayerMechPart);
    REQUIRE(mech != nullptr);
    REQUIRE(mech->SensorSystem.lock() != nullptr);
    const MCSensorSystem& mechSensors = *mech->SensorSystem.lock();
    CHECK(mechSensors.Enabled() != 0);
    MCSetMatchRulesInForce(MCMatchRule::NoSensors);
    CHECK(mechSensors.Enabled() == 0);

    // The commander drops a sensor probe at the mech; it lands and its sensor comes on.
    const std::shared_ptr<MCCommander> commander = HomeCommander();
    commander->SetNumSensorStrikes(1);
    REQUIRE_EQ(commander->NumSensorStrikes, 1);
    constexpr int32_t SensorProbe = 2;
    constexpr int32_t ProbeFrames = 15 * 20;
    CallArtillery(commander->GetId(), SensorProbe, mech->GetPosition(), 0, 0);
    std::shared_ptr<MCArtillery> probe;

    for (int32_t frame = 0; frame < ProbeFrames && (probe == nullptr || !probe->SensorActive); frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);

        for (const std::shared_ptr<MCBaseObject>& object : ObjectList()->DefaultList())
        {
            if (object->ObjectClass == MCObjectClass::Artillery)
            {
                probe = std::static_pointer_cast<MCArtillery>(object);
            }
        }
    }

    REQUIRE(probe != nullptr);
    REQUIRE(probe->SensorActive);
    REQUIRE(probe->SensorSystem.lock() != nullptr);
    CHECK(probe->SensorSystem.lock()->Enabled() != 0);
    CHECK(mechSensors.Enabled() == 0);

    // Without probes, the commander has none to drop.
    MCSetMatchRulesInForce(MCMatchRule::NoProbes);
    commander->SetNumSensorStrikes(1);
    CHECK_EQ(commander->NumSensorStrikes, 0);
    MCSetMatchRulesInForce(0);
}
