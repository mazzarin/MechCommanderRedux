#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMechWarrior.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTacticalMap.h"

using namespace MCScreenInput;

/// <summary>
/// The tactical map's video window shows the idle picture, and while a pilot speaks on the radio the pilot's name over
/// it, with a line from the window to the pilot's unit on the map. Recorded on the code before the window drew itself
/// each frame (renderer phase 3, step 4).
/// </summary>
TEST_CASE_ISOLATED("game: the tactical map's video window matches the pre-renderer frames")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    REQUIRE(map->VideoWindow != nullptr);
    std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x200);
    REQUIRE(mover != nullptr);
    std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
    REQUIRE(pilot != nullptr);

    uint32_t presents = 0x811c9dc5;
    REQUIRE(MCInput::Display() != nullptr);
    MCTestGame::OnPresent = [&] { presents = (presents ^ ScreenHash()) * 0x01000193; };

    const auto settle = [](std::string_view name)
    {
        for (int32_t frame = 0; frame < 10; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        const uint32_t hash = ScreenHash();
        SaveShot(std::format("video {}", name), hash);
        return hash;
    };

    // (Recorded again for OB-182: the map's objective numbers at the font's double scale under the hover tag.)
    CHECK_EQ(settle("idle"), 0xa9e4a145u);
    map->VideoWindow->SetStar(pilot);
    CHECK_EQ(settle("speaking"), 0x49ccf27du);
    map->VideoWindow->SetStar(nullptr);
    CHECK_EQ(settle("done"), 0xd9566f22u);
    MCTestGame::OnPresent = nullptr;
    CHECK_EQ(presents, 0x82dbb42au);
}
