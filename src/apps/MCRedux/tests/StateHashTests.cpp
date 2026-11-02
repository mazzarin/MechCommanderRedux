#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "mission/MCScenario.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "platform/MCRenderer.h"
#include "sprite/MCWeather.h"

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
    void MissionState(int32_t segment, uint32_t expected, int weather = HdWeatherOff)
    {
        REQUIRE(MCTestGame::StartMission(segment));
        GHdWeather = weather;
        uint32_t frames = 0x811c9dc5;
        std::set<uint32_t> distinct;

        for (int32_t frame = 0; frame < StateFrames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
            const uint32_t hash = MCTestGame::StateHash();
            frames = (frames ^ hash) * 0x01000193;
            distinct.insert(hash);
        }

        std::cout << std::format("  mission {}: state 0x{:08x}\n", segment, frames);
        // The game moved on every frame (the clock alone does that).
        CHECK_EQ(distinct.size(), static_cast<size_t>(StateFrames));
        CHECK_EQ(frames, expected);
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
        MissionState(1, 0x9ff2e0b4u);
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
        MissionState(1, 0x9ff2e0b4u, HdWeatherDownpour);
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
