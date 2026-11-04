#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "mission/MCScenario.h"
#include "mission/MCWeatherPlay.h"
#include "object/MCBattleMech.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "platform/MCRenderer.h"
#include "sprite/MCWeather.h"

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
/// by its long-range aim alone, and by its vehicles' speed alone. (Its fires and footing are checked on their own: the
/// battle has neither; its sight changes what each player sees, which the state doesn't hold.)
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
