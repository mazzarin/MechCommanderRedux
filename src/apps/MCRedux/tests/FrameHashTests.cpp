#include "stdafx.h"
#include "MCTest.h"
#include "MCMenusPictures.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "color/MCWaterCycle.h"
#include "engine/MCFont.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCResultsMenu.h"
#include "mission/MCScenario.h"
#include "gui/MCUpdateDisplay.h"
#include "object/MCForces.h"
#include "object/MCTeam.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTacticalMap.h"

using namespace MCScreenInput;

namespace
{
    // The recorded-frame tests run twice: drawn by the software renderer, and drawn by the GPU alone (the frame
    // composited from its surfaces, and the hashed screen read back from them).

    /// <summary>
    /// Before the boot: asks for the GPU to draw the frame surfaces alone with <paramref name="gpu"/>, else for the
    /// software renderer.
    /// </summary>
    void RequestDrawing(bool gpu)
    {
        MCRenderer::RequestGpuDrawing(gpu ? MCGpuDrawing::On : MCGpuDrawing::Off);
    }

    /// <summary>After the boot: false (with a note) when the GPU was asked for and this machine has no GPU renderer.</summary>
    bool DrawingAsAsked(bool gpu)
    {
        if (gpu && MCRenderer::GpuDrawing() != MCGpuDrawing::On)
        {
            std::cout << "  (skipped: no GPU renderer on this machine)\n";
            return false;
        }

        return true;
    }

    /// <summary>
    /// At the end, with the GPU drawing alone: nothing read a frame surface's memory (which nothing draws), and the
    /// screen's memory isn't what was shown (the frame really came from the GPU).
    /// </summary>
    void CheckGpuAlone(bool gpu)
    {
        if (!gpu)
        {
            return;
        }

        CHECK_EQ(MCRenderer::StaleCpuReads(), 0);
        CHECK_EQ(MCRenderer::UnregisteredDraws(), 0);
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        REQUIRE(display != nullptr);
        const MCWindow& screen = display->Screen();

        const std::vector<uint8_t> shown = display->ComposeScreen();
        CHECK(!std::equal(shown.begin(), shown.end(), screen.Pixels.get()));
    }

    /// <summary>The mission results screen's frames (see the test), drawn by the GPU alone with <paramref name="gpu"/>.</summary>
    void ResultsScreenFrames(bool gpu);

    /// <summary>
    /// Mission 1's screen, right after the scenario starts and 60 frames later, is pixel for pixel the recorded frame
    /// (a regression check on the whole draw path: terrain, overlays, objects and the interface).
    /// </summary>
    /// <remarks>
    /// Baselines: the pre-renderer code drew 0x1888f902 (both frames: the lance stands still). Renderer phase 1 (the
    /// vfx clip fixes, OB-112..128) moved the terrain overlays up a row (OB-117), and nothing else, giving 0x2bc262d5.
    /// Renderer phase 2 (zoom): the mission starts zoomed out (camera scale 1, the half-size art) in the original; now
    /// the camera stays at scale 100 and the test fixes the zoom at 480 lines, one world pixel per screen pixel, so the
    /// frame shows the full-size art closer in: 0xd9566f22.
    /// </remarks>
    void MissionOneFrames(bool gpu)
    {
        RequestDrawing(gpu);
        REQUIRE(MCTestGame::StartMission(1));

        if (!DrawingAsAsked(gpu))
        {
            return;
        }

        const uint32_t started = ScreenHash();
        SaveShot("mission1 start", started);

        for (int32_t frame = 0; frame < 60; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        const uint32_t later = ScreenHash();
        SaveShot("mission1 later", later);
        CHECK_EQ(started, 0xd9566f22u);
        CHECK_EQ(later, 0xd9566f22u);
        CheckGpuAlone(gpu);
    }
}

TEST_CASE_ISOLATED("game: mission 1's screen matches the recorded frames")
{
    if (MCTestGame::Available())
    {
        MissionOneFrames(false);
    }
}

/// <summary>Mission 1's recorded frames, drawn by the GPU alone.</summary>
TEST_CASE_ISOLATED("game: the GPU alone draws mission 1's recorded frames")
{
    if (MCTestGame::Available())
    {
        MissionOneFrames(true);
    }
}

/// <summary>
/// The composite shader shows what the CPU composite computes: in mission 1, at one world pixel per screen pixel and
/// zoomed out and in (the world scaled into the view, the overlays and translucent bars over it), every pixel of the
/// frame read back from the GPU is the CPU composite's palette colour.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU composite matches the CPU composite")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);

    if (!display->CompositesOnGpu())
    {
        std::cout << "  (skipped: no GPU composite on this machine)\n";
        return;
    }

    for (const float zoom : {480.0f, 720.0f, 400.0f})
    {
        MCTest::Scope scope(std::format("{} lines", zoom));
        MCFixedZoomHeight = zoom;

        for (int32_t frame = 0; frame < 3; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        const auto read = display->ReadFrame();
        REQUIRE(read.has_value());
        const std::vector<uint8_t> composed = display->ComposeScreen();
        SDL_Color colors[256];
        display->GetShownColors(colors);
        int32_t different = 0;

        for (size_t i = 0; i < composed.size(); i++)
        {
            const SDL_Color& expected = colors[composed[i]];
            const SDL_Color& actual = (*read)[i];

            if (expected.r != actual.r || expected.g != actual.g || expected.b != actual.b)
            {
                different++;
            }
        }

        CHECK_EQ(different, 0);
    }
}

/// <summary>
/// The water's colour cycle (cycleColors, every scenario CycleLength seconds): the eight water entries from 0xd8 show
/// the colours of WaterMagicColors' entries, rotated by one each cycle, while the palette itself (what the GPU holds)
/// keeps its own entries there; and the GPU's frame shows the cycled colours as the CPU's does.
/// </summary>
TEST_CASE_ISOLATED("game: the water colours cycle over the palette without changing it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    MCVfxRgb water[8];
    display->GetPalette(0xd8, water);
    std::set<int32_t> steps;

    for (int32_t frame = 0; frame < 90; frame++)
    {
        MCTest::Scope scope(std::format("frame {}", frame));
        MCTestGame::RunFrame(1.0f / 15.0f);
        MCVfxRgb palette[256];
        display->GetPalette(0, palette);
        SDL_Color shown[256];
        display->GetShownColors(shown);

        for (int32_t i = 0; i < 8; i++)
        {
            CHECK(palette[0xd8 + i].R == water[i].R && palette[0xd8 + i].G == water[i].G &&
                  palette[0xd8 + i].B == water[i].B);
        }

        // The step the shown water colours are at: each entry the colour of its source.
        const auto showsStep = [&](int32_t step)
        {
            for (int32_t i = 0; i < 8; i++)
            {
                const MCVfxRgb& source = palette[WaterMagicColors[(i + step) & 7]];
                const SDL_Color& colour = shown[0xd8 + i];

                if (colour.r != source.R || colour.g != source.G || colour.b != source.B)
                {
                    return false;
                }
            }

            return true;
        };

        for (int32_t step = 0; step < 8; step++)
        {
            if (showsStep(step))
            {
                steps.insert(step);
                break;
            }
        }
    }

    // The cycle ran and moved on.
    CHECK(steps.size() >= 2u);

    if (display->CompositesOnGpu())
    {
        const auto read = display->ReadFrame();
        REQUIRE(read.has_value());
        const std::vector<uint8_t> composed = display->ComposeScreen();
        SDL_Color colors[256];
        display->GetShownColors(colors);
        int32_t different = 0;

        for (size_t i = 0; i < composed.size(); i++)
        {
            const SDL_Color& expected = colors[composed[i]];
            const SDL_Color& actual = (*read)[i];

            if (expected.r != actual.r || expected.g != actual.g || expected.b != actual.b)
            {
                different++;
            }
        }

        CHECK_EQ(different, 0);
    }
}

/// <summary>
/// The tactical map draws each of its pages (it draws itself each frame from its state since renderer phase 3): the
/// map, the info page with the first mech of the lance and without a unit, the mission page and the salvage page all
/// show, each different from the others, and the map page comes back as it was.
/// </summary>
TEST_CASE_ISOLATED("game: the tactical map draws every page")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    std::shared_ptr<MCFriendlyMechIcon> icon = TacticalInterface()->MechBar->GetButton(0);
    REQUIRE(icon != nullptr);

    // Each page shown for 10 frames.
    auto show = [map](std::string_view name, auto choose)
    {
        choose();

        for (int32_t frame = 0; frame < 10; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        const uint32_t hash = ScreenHash();
        SaveShot(std::format("tacmap {}", name), hash);
        return hash;
    };

    const uint32_t mapPage = show("map", [] {});
    const uint32_t infoEmpty = show("info empty",
                                    [map]
                                    {
                                        map->InfoObject.reset();
                                        map->SetDisplayType(MCTacmapPage::Info);
                                    });
    const uint32_t infoMech = show("info mech", [map, icon] { map->SetID(icon->PartId); });
    const uint32_t infoPayload = show("info payload", [map] { map->SetDataDisplayMode(2, -1); });
    const uint32_t missionPage = show("mission", [map] { map->SetDisplayType(MCTacmapPage::Mission); });
    const uint32_t salvagePage = show("salvage", [map] { map->SetDisplayType(MCTacmapPage::Salvage); });
    const uint32_t mapAgain = show("map again", [map] { map->SetDisplayType(MCTacmapPage::Map); });

    const uint32_t pages[] = {mapPage, infoEmpty, infoMech, infoPayload, missionPage, salvagePage};

    for (size_t i = 0; i < std::size(pages); i++)
    {
        for (size_t j = i + 1; j < std::size(pages); j++)
        {
            MCTest::Scope scope(std::format("pages {} and {}", i, j));
            CHECK(pages[i] != pages[j]);
        }
    }

    // The lance stands still in mission 1, so the map page looks as it did.
    CHECK_EQ(mapAgain, mapPage);
}

/// <summary>
/// The map's objective numbers keep their size whatever scale the shared line font was left at: the hover tag leaves
/// it at 1.0 unscaled, the video window at 2.0 (OB-182, issue #47). With the markers lit, the tactical map looks the
/// same with the cursor on bare ground (the font left at 2.0, then at 1.0) and on a mech, where the tag draws first.
/// </summary>
TEST_CASE_ISOLATED("game: the map's objective numbers keep their size after the hover tag")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    REQUIRE(map->DisplayType == MCTacmapPage::Map);

    // Mission 1 has a pending objective with a position, so the map shows a number.
    int32_t numbered = 0;

    const std::shared_ptr<MCTeam> home = HomeTeam();
    REQUIRE(home != nullptr);

    for (int32_t i = 0; i < static_cast<int32_t>(home->NumObjectives); i++)
    {
        const MCScenarioObjective& objective = Scenario()->Objectives[home->FirstObjective + i];

        if (objective.Status == 0 && objective.Position[0] != -99.0f && objective.Position[1] != -99.0f &&
            objective.Position[2] != -99.0f)
        {
            numbered++;
        }
    }

    REQUIRE(numbered > 0);

    // The cursor at a point for 10 frames, then a short frame with the markers lit and the font left as given; the
    // hash of the tactical map's part of the screen.
    auto drawn = [map](std::string_view name, int32_t x, int32_t y, float scale, bool scaled)
    {
        SendMouse(7, x, y, false);

        for (int32_t frame = 0; frame < 10; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        MCTacticalMap::MarkersLit = true;
        MCTacticalMap::MarkerBlinkTime = 0.0f;
        LineFont()->Scale = scale;
        LineFont()->Scaled = scaled;
        MCTestGame::RunFrame(0.001f);
        SaveShot(std::format("objective numbers {}", name), ScreenHash());

        const MCWindow& screen = ScreenPort()->Bitmap();
        const int32_t width = screen.XMax + 1;
        const std::vector<uint8_t> shown = MCInput::Display()->ComposeScreen();
        uint32_t hash = 0x811c9dc5;

        for (int32_t row = map->GlobalY(); row < map->GlobalY() + map->Height(); row++)
        {
            for (int32_t column = map->GlobalX(); column < map->GlobalX() + map->Width(); column++)
            {
                hash = (hash ^ shown[static_cast<size_t>(row) * static_cast<size_t>(width) + column]) * 0x01000193;
            }
        }

        return hash;
    };

    REQUIRE(MCInput::Display() != nullptr);
    const uint32_t ground = drawn("ground", 500, 120, 2.0f, true);
    const uint32_t groundSmall = drawn("ground, font at 1.0", 500, 120, 1.0f, false);
    const uint32_t tagged = drawn("tag", 320, 215, 2.0f, true);
    CHECK_EQ(groundSmall, ground);
    CHECK_EQ(tagged, ground);
}

/// <summary>
/// The info page's front, rear and payload buttons switch the view on the next frame: the payload's weapon list shows
/// at once, and goes again at once for an armor view (OB-177; the port once waited for its half-second refresh).
/// </summary>
TEST_CASE_ISOLATED("game: the info page's views switch on the next frame")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    std::shared_ptr<MCFriendlyMechIcon> icon = TacticalInterface()->MechBar->GetButton(0);
    REQUIRE(icon != nullptr);
    map->SetDisplayType(MCTacmapPage::Info);
    map->SetID(icon->PartId);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(!map->InfoText->IsShowing());

    map->SetDataDisplayMode(2, -1);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(map->InfoText->IsShowing());
    CHECK(map->InfoText->NumLines > 0);

    map->SetDataDisplayMode(1, -1);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(!map->InfoText->IsShowing());

    map->SetDataDisplayMode(2, -1);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(map->InfoText->IsShowing());

    map->SetDataDisplayMode(0, -1);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(!map->InfoText->IsShowing());
}

/// <summary>
/// Mission 1 won (every objective succeeded): the remade results over the battlefield count the resource points up and
/// list the statistics, the objectives and the pilots a step at a time, in 56 frames (1998's took 55). Every frame of
/// that, the battlefield's screen under it and the whole window, and the finished results at the recorded frames'
/// windows, are pixel for pixel the recorded frames.
/// </summary>
TEST_CASE_ISOLATED("game: the mission results screen matches the recorded frames")
{
    if (MCTestGame::Available())
    {
        ResultsScreenFrames(false);
    }
}

/// <summary>
/// The mission results' count drawn by the GPU alone: the battlefield's screen under it is the recorded frames (the
/// window's pictures, the menus' RmlUi among them, differ by driver: the software renderer's are recorded).
/// </summary>
TEST_CASE_ISOLATED("game: the GPU alone draws the mission results screen's recorded frames")
{
    if (MCTestGame::Available())
    {
        ResultsScreenFrames(true);
    }
}

namespace
{
    void ResultsScreenFrames(bool gpu)
    {
        RequestDrawing(gpu);
        REQUIRE(MCTestGame::StartMission(1));

        if (!DrawingAsAsked(gpu))
        {
            return;
        }

        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        REQUIRE(display != nullptr);
        // The count's frames at the first of the recorded windows, the mouse in its corner over no button.
        const MCMenusPictures::MCWindowSize& counted = MCMenusPictures::FrameWindows.front();
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, counted.Width, counted.Height));
        MCScreenInput::RealMove(0, 0);

        // The screen's steps are timed in MouseTicks, which the mouse timer's thread counts in real time; the test counts
        // them itself.
        MouseTimerKill();

        for (int32_t i = 0; i < Scenario()->Objectives.Count(); i++)
        {
            Scenario()->Objectives[i].Status = 1;
        }

        ScenarioResult = 4;

        for (int32_t frame = 0; frame < 30 && Mission()->State != MCMissionState::Results; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        REQUIRE(Mission()->State == MCMissionState::Results);
        REQUIRE(Mission()->ResultsUp());
        // The remade results over the battlefield.
        REQUIRE(MCRemadeTest::OnTop<MCResultsScreen>(MCRemadeTest::MCLayer::Mission) != nullptr);

        // Five ticks a frame: a resource point step takes one tick, the others resultsStepTicks (20). The battlefield's
        // screen under the results is hashed each frame, and on the software renderer the whole window too (the GPU's
        // pictures of the menus' RmlUi differ by driver).
        uint32_t frames = 0x811c9dc5;
        uint32_t windows = 0x811c9dc5;
        int32_t frame = 0;

        for (; frame < 3000 && !Mission()->ResultsFinished(); frame++)
        {
            MouseTicks += 5;
            MCTestGame::RunFrame(1.0f / 15.0f);
            frames = (frames ^ ScreenHash()) * 0x01000193;

            if (!gpu)
            {
                const auto window = display->ReadWindow();
                REQUIRE(window.has_value());
                windows = (windows ^ MCMenusPictures::PictureHash(*window)) * 0x01000193;
            }

            if (frame % 40 == 0)
            {
                MCScreenInput::SaveWindowShot(std::format("results {:04}", frame));
            }
        }

        const auto settle = []
        {
            for (int32_t step = 0; step < 10; step++)
            {
                MouseTicks += 5;
                MCTestGame::RunFrame(1.0f / 15.0f);
            }
        };

        settle();
        const uint32_t finished = ScreenHash();
        SaveShot("results finished", finished);
        // The remade count shows its last step a frame after 1998's screen did (its 55).
        CHECK_EQ(frame, 56);
        // The battlefield under the results: the same indices on the software renderer and the GPU.
        CHECK_EQ(frames, 0x223bf1a5u);
        CHECK_EQ(finished, 0xe0da7160u);

        if (gpu)
        {
            CheckGpuAlone(gpu);
            return;
        }

        CHECK_EQ(windows, 0x0fa9d4cbu);
        MCMenusPictures::CheckWindowFrames(*display, "results finished", {0xa932861au, 0xee1a04f2u, 0x5da2cbb5u},
                                           settle);
    }
}
