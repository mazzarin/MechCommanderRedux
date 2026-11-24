#include "stdafx.h"
#include "MCTest.h"
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
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
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
    /// <summary>The logistics screens' frames (see the test), drawn by the GPU alone with <paramref name="gpu"/>.</summary>
    void LogisticsFrames(bool gpu);

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
/// Mission 1 won (every objective succeeded): the results screen counts the resource points up and lists the
/// statistics, the objectives and the pilots a step at a time, then the debriefing. Every frame of that, and the
/// finished screen, is pixel for pixel what the screen drew when it still painted each step into its picture (before
/// renderer phase 3 step 3).
/// </summary>
TEST_CASE_ISOLATED("game: the mission results screen matches the recorded frames")
{
    if (MCTestGame::Available())
    {
        ResultsScreenFrames(false);
    }
}

/// <summary>The mission results screen's recorded frames, drawn by the GPU alone.</summary>
TEST_CASE_ISOLATED("game: the GPU alone draws the mission results screen's recorded frames")
{
    if (MCTestGame::Available())
    {
        ResultsScreenFrames(true);
    }
}

/// <summary>
/// Each logistics screen, reached as its buttons reach it, is pixel for pixel what the game drew before the renderer
/// interface was introduced: the main menu, the preferences and load screens, then a new campaign's briefing, the
/// purchase screen with each inventory tab, the repair screen (mech lab) and the briefing again. (Renderer phase 1's
/// clip fixes changed none of them.)
/// </summary>
TEST_CASE_ISOLATED("game: the logistics screens match the pre-renderer frames")
{
    if (MCTestGame::Available())
    {
        LogisticsFrames(false);
    }
}

/// <summary>The logistics screens' recorded frames, drawn by the GPU alone.</summary>
TEST_CASE_ISOLATED("game: the GPU alone draws the logistics screens' recorded frames")
{
    if (MCTestGame::Available())
    {
        LogisticsFrames(true);
    }
}

namespace
{
    void ResultsScreenFrames(bool gpu)
    {
        // The 1998 results screen's frames (the remade results' are ResultsMenuTests'), kept until step 2 of stage 9.
        MCResultsMenu::Remade() = false;
        RequestDrawing(gpu);
        REQUIRE(MCTestGame::StartMission(1));

        if (!DrawingAsAsked(gpu))
        {
            return;
        }

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
        REQUIRE(Mission()->ResultsScreen != nullptr);

        // Five ticks a frame: a resource point step takes one tick, the others resultsStepTicks (20).
        uint32_t frames = 0x811c9dc5;
        uint32_t last = 0;
        int32_t frame = 0;

        for (; frame < 3000 && Mission()->ResultsScreen != nullptr && !Mission()->ResultsScreen->Finished(); frame++)
        {
            MouseTicks += 5;
            MCTestGame::RunFrame(1.0f / 15.0f);
            last = ScreenHash();
            frames = (frames ^ last) * 0x01000193;

            if (frame % 40 == 0)
            {
                SaveShot(std::format("results {:04}", frame), last);
            }
        }

        for (int32_t settle = 0; settle < 10; settle++)
        {
            MouseTicks += 5;
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        const uint32_t finished = ScreenHash();
        SaveShot("results finished", finished);
        CHECK_EQ(frame, 55);
        // The screen dims the world behind it: the software renderer maps those indices through tables, the GPU blends
        // the colours and keeps the indices, which are what is hashed here.
        CHECK_EQ(frames, gpu ? 0xcf555b41u : 0xa17463bcu);
        CHECK_EQ(finished, gpu ? 0xab264123u : 0xea82833au);
        CheckGpuAlone(gpu);
    }

    /// <summary>
    /// The settings the logistics steps were recorded with, as prefs.cfg in the tests' user folder (over the install's,
    /// which a player's game changes) while it lives: the preferences screen shows them. Difficulty regular,
    /// brightness 1, music 0, radio and effects 27 (the retail PREFS.CFG's).
    /// </summary>
    class MCRecordedPrefs
    {
    public:
        MCRecordedPrefs() : _Path(MCTestGame::UserFolder() / "prefs.cfg")
        {
            std::filesystem::create_directories(_Path.parent_path());
            std::ofstream file(_Path, std::ios::binary);
            file << "FITini \r\n\r\n[MechCommander]\r\nb PaletteCycle=TRUE\r\nb DirectDraw=TRUE\r\n"
                    "b Use90Pixel=TRUE\r\nb Force45Pixel=FALSE\r\nb Force16Mb=FALSE\r\nb Force32Mb=FALSE\r\n"
                    "l Difficulty=1\r\nl Brightness=1\r\nl MusicVolume=0\r\nl RadioVolume=27\r\nl SFXVolume=27\r\n"
                    "FITend \r\n";
        }

        ~MCRecordedPrefs() { std::filesystem::remove(_Path); }

        MCRecordedPrefs(const MCRecordedPrefs&) = delete;
        MCRecordedPrefs& operator=(const MCRecordedPrefs&) = delete;

    private:
        /// <summary>The file.</summary>
        std::filesystem::path _Path;
    };

    /// <summary>
    /// The logistics screens' steps (see the test).
    /// </summary>
    /// <remarks>
    /// Then, driven by the mouse, the steps renderer phase 3 step 4 (logistics drawn from its state) must keep: the
    /// mission briefing tab, two units dragged into force group 1, a hovered screen button, a mech bought (drag,
    /// purchase dialog, quantity, accept), an inventory row and a variant button clicked, the shop scrolled, the "not
    /// enough points" message and its OK, the component inventory, and the mech bay's payload list and second mech.
    /// Those were recorded on the code before step 4, as was the fold of every frame of the run (wipes and dialogs
    /// included) and the fold of every present (which also sees the frames a screen change's wipe draws inside its own
    /// loop). Renderer phase 5c added the RENDERER box to the preferences screen: its step went from 0xe7477674 to
    /// 0xa3f4d3ea, the frame fold from 0x16f4b349 to 0x26b623c9 and the present fold from 0x6919eded to 0x5e94c1ed
    /// (no other step changed; software and GPU agree). The drop-downs for DIFFICULTY and RENDERER (2026-10-04) took
    /// it to 0x686720c8, the folds to 0xe131e121 and 0x570e786d (GPU 0x306358a5 and 0x4d7c4e5b), again only there.
    /// The control audit (2026-10-04: everything drawn from its state, the original's stale paint gone, OB-131..134)
    /// changed the steps where units move between drop slots (an emptied slot keeps its border; no leftover picture)
    /// and the repair rows (a mech without a pilot shows its status bar), and the folds to 0x19ceb4be and 0x90a481ca
    /// (GPU 0x5cdc376e and 0x14d82a5d): presses end at the release, the screen chrome shows at once. The preferences
    /// panel (2026-10-17: a panel over the art, a box per group, INTERFACE VOLUME, ACCEPT and CANCEL at the bottom
    /// right) took the preferences step to 0x6ea95138 and the folds to 0x097f849e and 0x92debaca (GPU 0x3b586e8e and
    /// 0x9297295d), only there; the run now pins the settings it shows (<see cref="MCRecordedPrefs"/>). The UI
    /// RESOLUTION box (2026-10-18) took the preferences step to 0xd7aa9185 and the folds to 0xcfbe6bb8 and 0xac3d91aa
    /// (GPU 0x8d317260 and 0xd056cb3d), only there. The drop-down boxes lined up with the sliders' labels, wide enough
    /// for NATIVE (RETINA/HD), the CONTROLS button and the HD ART box (2026-10-25) took the preferences step to
    /// 0xa73fbdbc (software and GPU agree). The SCROLL SPEED and HD AUDIO boxes, the panel taller for HD AUDIO
    /// (personal, 2026-10-27) took it to 0x46079767 (software and GPU agree). The run now pins the saves too,
    /// to none (<see cref="MCTestGame::PinnedSaves"/>; 2026-10-31): the main menu's LOAD MISSION is off and the load
    /// screen's list empty on any machine, whatever saves its install or earlier tests left. That took the main menu to
    /// 0xe30aef7b, the load screen to 0xe34ed709, the preferences step to 0x924ff616 (software and GPU agree) and the
    /// folds to 0xe4d5afc8 and 0xca197992 (GPU 0xb80a21d0 and 0xf18c3e85). The HD box in place of the HD ART, HD AUDIO
    /// and HD LIGHTING boxes, the panel shorter (personal, 2026-11-02) took the preferences step to 0x7f8ed0c6 (software
    /// and GPU agree) and the folds to 0xd54ea188 and 0x6a56c692 (GPU 0xac28e610 and 0xcaada585), only there. The
    /// INTERFACE SIZE box, the right column's boxes 42 lines tall, the HD box level with SCROLL SPEED, SOUND's box in
    /// line with RENDERER's and the text buttons in the art's lettering (personal, 2026-11-02) took the preferences
    /// step to 0x1366f9ea (software and GPU agree) and the folds to 0x9a2c0b40 and 0xee685b12 (GPU 0x7c367c58 and 0xba21a105).
    /// NEW CAMPAIGN's click fading the main menu from the click, the campaign started near black, and a fade into a
    /// screen rehearsing it near black (port-only, 2026-11-15) took the folds to 0x0a3f55ec and 0x98c24128 (GPU
    /// 0x4cf80294 and 0x600d1763), every step as it was.
    /// </remarks>
    void LogisticsFrames(bool gpu)
    {
        // The 1998 campaign screens' own recording (kept until stage 9 removes them): the remade screens off.
        MCCampaignMenu::Remade() = false;
        const MCRecordedPrefs prefs;
        std::optional<MCTestGame::PinnedSaves> saves;
        RequestDrawing(gpu);
        const MCTestGame::OldMainMenu oldMenu;
        REQUIRE(MCTestGame::StartLogistics([&saves] { saves.emplace(); }));

        if (!DrawingAsAsked(gpu))
        {
            return;
        }

        // A screen change wipes from one screen to the next over several frames; two seconds settles every one.
        // Every frame on the way (the wipes, the dialogs opening) is folded into one more hash.
        uint32_t frames = 0x811c9dc5;
        const auto settle = [&frames]
        {
            uint32_t hash = 0;

            for (int32_t frame = 0; frame < 30; frame++)
            {
                MCTestGame::RunFrame(1.0f / 15.0f);
                hash = ScreenHash();
                frames = (frames ^ hash) * 0x01000193;
            }

            return hash;
        };

        // Every frame presented is folded into one more hash, including those the screen wipes draw inside their own loop
        // (--present-log lists them; see MCTestGame::OnPresent).
        uint32_t presents = 0x811c9dc5;
        MCTestGame::OnPresent = [&] { presents = (presents ^ ScreenHash()) * 0x01000193; };

        struct Step
        {
            std::string_view Name;
            std::function<void()> Action;
            uint32_t Expected;
        };

        // Port-only (SPEC-menus.md's stage 2, 23 Nov 2026): NEW CAMPAIGN is a change of screen through the flow, its
        // fade drawn on frames of its own (the palette's fade ran in a loop of its own), so the tests' clock moves
        // differently: from the briefing on only the clock's seconds (and the briefing's movie frame) differ from the
        // frames before (every frame 0x0a3f55ec, 0x4cf80294 on the GPU; every present 0x98c24128, 0x600d1763).

        // The GPU's frames where they differ: the darkened rows and the briefing box are a translate polygon, which the
        // GPU blends (AlphaPal's colours) instead of mapping the indices, so the indices read back there are the ones
        // under the blend. The mirror test checks every other pixel against the software renderer.
        const std::map<std::string_view, uint32_t> gpuExpected = {
            {"briefing", 0xaadfead9u},
            {"repair", 0xf3f62504u},
            {"briefing again", 0x885d7189u},
            {"briefing, mission tab", 0x9283c509u},
            {"briefing, deploy", 0x57766579u},
            {"briefing, deploy second", 0x0902cc4cu},
            {"briefing, hover", 0x5e8237bcu},
            {"briefing, slot to slot", 0xb554a65eu},
            {"briefing, onto a unit", 0x2bf39850u},
            {"briefing, box list scroll", 0xc1099574u},
            {"briefing, back to pane", 0x48da1909u},
            {"briefing, deploy again", 0x73ea3f9eu},
            {"repair again", 0x9414f0c0u},
            {"repair, payload", 0x6c58c107u},
            {"repair, second mech", 0xccb3194eu},
            {"repair, component info", 0x7592765bu},
            {"repair, weapon info", 0x22504c63u},
            {"repair, mech info", 0x6cd08e4fu},
            {"repair, weapon off", 0x8813dc44u},
            {"repair, mount laser", 0x053e8f6bu},
            {"repair, pilots tab", 0x6d500d1du},
            {"repair, pilot info", 0x0c921795u},
            {"repair, vehicles tab", 0x23cfb671u},
            {"repair, mechs tab", 0xe7400e9cu},
            {"repair, mech row info", 0x853ec87au},
            {"repair, mech into force", 0x16033c0cu},
            {"repair, select first", 0xdf7a59f9u},
            {"repair, mech out", 0x748527f6u},
            {"repair, weapon list scroll", 0x23483332u},
            {"repair, first mech out", 0x3a15c850u},
            {"briefing after repair", 0x4172eab1u},
            {"briefing, unit out of slot", 0x6ffea943u},
        };

        const Step steps[] = {
            {"main menu", [] {}, 0xe30aef7bu},
            {"preferences", [] { ShowPreferences(); }, 0x1366f9eau},
            {"main menu after preferences", [] { CancelPrefs(); }, 0xe30aef7bu},
            {"load screen",
             []
             {
                 CHECK(GlobalLog()->MainScreen->Element<MCLogButton>(MCTestGame::MainMenuLoadButton)->Disabled);
                 LoadScreen();
             },
             0xe34ed709u},
            {"main menu after load",
             []
             {
                 CHECK_EQ(GlobalLog()->LoadScreen->FilePane->NumFiles(), 0);
                 Cancel();
             },
             0xe30aef7bu},
            {"briefing", [] { NewCampaign(); }, 0x6079c72fu},
            {"purchase", [] { GlobalLog()->SetUpPurchaseScreen(true); }, 0x322b3955u},
            {"purchase, pilots", [] { GlobalLog()->PurchaseScreen->SetUpPilotInv(true, true); }, 0xb68af110u},
            {"purchase, components", [] { GlobalLog()->PurchaseScreen->SetUpCompInv(true, true); }, 0x67c5820au},
            {"purchase, mechs", [] { GlobalLog()->PurchaseScreen->SetUpMechInv(true, true); }, 0xe07b66f3u},
            {"repair", [] { GlobalLog()->SetUpRepairScreen(true); }, 0xd3119f41u},
            {"briefing again", [] { GlobalLog()->SetUpBriefingScreen(true); }, 0x1d06393fu},
            {"briefing, mission tab", [] { Click(202, 265); }, 0x042383bfu},
            {"briefing, deploy", [] { Drag(40, 385, 245, 60); }, 0xee69467bu},
            {"briefing, deploy second", [] { Drag(90, 385, 300, 60); }, 0x4fd75943u},
            {"briefing, hover", [] { SendMouse(7, 100, 60, false); }, 0x672b1af3u},
            {"briefing, slot to slot", [] { Drag(250, 60, 250, 110); }, 0x813d3c0cu},
            {"briefing, onto a unit", [] { Drag(250, 110, 300, 60); }, 0xaead37cau},
            {"briefing, box list scroll", [] { Click(626, 467); }, 0x9dd6cea0u},
            {"briefing, back to pane", [] { Drag(300, 60, 60, 400); }, 0x0bcb173du},
            {"briefing, deploy again", [] { Drag(40, 385, 245, 60); }, 0x965e223au},
            {"purchase again",
             []
             {
                 // Enough points and stock to buy with; the store shows the stock, as the game redraws a row whose stock
                 // changed.
                 ResourcePoints = 100000;
                 MCTestGame::StockStore(2);
                 Click(100, 60);
             },
             0x2a18e5e5u},
            {"purchase, buy drag", [] { Drag(300, 80, 100, 200); }, 0x1d17b6fcu},
            {"purchase, quantity up", [] { Click(378, 249); }, 0x4b037333u},
            {"purchase, accept", [] { Click(316, 299); }, 0xc9a03745u},
            {"purchase, inventory row", [] { Click(100, 120); }, 0x66f2e495u},
            {"purchase, variant", [] { Click(393, 32); }, 0x6a55f13eu},
            {"purchase, scroll", [] { Click(631, 474); }, 0x4d4a5516u},
            {"purchase, too poor",
             []
             {
                 ResourcePoints = 0;
                 Drag(300, 80, 100, 200);
             },
             0x9bdec923u},
            {"purchase, message ok", [] { Click(366, 283); }, 0x4a3369f2u},
            {"purchase, inventory info", [] { Click(60, 165); }, 0xb2803191u},
            {"purchase, pilots tab", [] { Click(202, 180); }, 0xbaf68448u},
            {"purchase, components tab",
             []
             {
                 ResourcePoints = 100000;
                 Click(202, 265);
             },
             0xa1c79fa9u},
            {"purchase, component drag", [] { Drag(300, 80, 100, 200); }, 0xf1d09d00u},
            {"purchase, component accept", [] { Click(316, 299); }, 0x47bb8e48u},
            {"purchase, vehicles tab", [] { Click(202, 335); }, 0x3c15fc48u},
            {"purchase, vehicle drag", [] { Drag(300, 80, 100, 200); }, 0xb46e9471u},
            {"purchase, vehicle accept", [] { Click(316, 299); }, 0x41a74e01u},
            {"purchase, mechs tab", [] { Click(202, 125); }, 0x717f9b92u},
            {"repair again", [] { Click(100, 78); }, 0x8454de91u},
            {"repair, payload", [] { Click(560, 60); }, 0xbc5da8afu},
            {"repair, second mech", [] { Click(300, 170); }, 0x0bccf3f2u},
            {"repair, component info", [] { SendMouse(7, 100, 160, false); }, 0xdd2483afu},
            {"repair, weapon info", [] { SendMouse(7, 560, 175, false); }, 0x53750197u},
            {"repair, mech info", [] { SendMouse(7, 300, 200, false); }, 0xba9395f3u},
            {"repair, weapon off", [] { Drag(560, 175, 100, 200); }, 0x736543bbu},
            {"repair, mount laser", [] { Drag(100, 160, 300, 190); }, 0x1b55df9bu},
            {"repair, pilots tab", [] { Click(202, 180); }, 0xddd1006au},
            {"repair, pilot info", [] { SendMouse(7, 100, 140, false); }, 0x404c9f5eu},
            {"repair, vehicles tab", [] { Click(202, 335); }, 0x68332784u},
            {"repair, mechs tab", [] { Click(202, 125); }, 0x9bc402d1u},
            {"repair, mech row info", [] { SendMouse(7, 100, 140, false); }, 0x63e1d1ffu},
            {"repair, mech into force", [] { Drag(100, 140, 300, 300); }, 0x821673bbu},
            {"repair, select first", [] { Click(300, 60); }, 0x5309cfeeu},
            {"repair, mech out", [] { Drag(260, 170, 100, 200); }, 0x98c20f9eu},
            {"repair, weapon list scroll", [] { Click(560, 125); }, 0x1f77777du},
            {"repair, first mech out", [] { Drag(260, 60, 100, 200); }, 0x989a68ffu},
            {"briefing after repair", [] { GlobalLog()->SetUpBriefingScreen(true); }, 0x556c6d3bu},
            {"briefing, unit out of slot", [] { Drag(300, 60, 60, 400); }, 0x8be41291u},
        };

        // --shots <folder>: a screenshot of each step, to see what a changed hash shows.
        int32_t index = 0;

        for (const Step& step : steps)
        {
            MCTest::Scope scope(step.Name);
            step.Action();
            const uint32_t hash = settle();
            SaveShot(std::format("logistics{:02} {}", index, step.Name), hash);
            const auto gpuHash = gpuExpected.find(step.Name);
            CHECK_EQ(hash, gpu && gpuHash != gpuExpected.end() ? gpuHash->second : step.Expected);
            index++;
        }

        MCTestGame::OnPresent = nullptr;
        SaveShot("logistics every frame", frames);
        CHECK_EQ(frames, gpu ? 0x5899660fu : 0xf22117f7u);
        CHECK_EQ(presents, gpu ? 0xfb0fb864u : 0x5b4eae67u);
        CheckGpuAlone(gpu);
    }
}
