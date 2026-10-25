#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "color/MCWaterCycle.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "logistics/MCPreferencesMenu.h"
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
#include "mission/MCScenario.h"
#include "gui/MCUpdateDisplay.h"
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
        MCRecordedPrefs() : _Path(std::filesystem::temp_directory_path() / "mc_tests_user" / "prefs.cfg")
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
    /// (GPU 0x8d317260 and 0xd056cb3d), only there. The CONTROLS button (2026-10-25) took the preferences step to
    /// 0x733386ba (software and GPU agree); the folds still need re-recording on the recording machine, whose test user
    /// folder (and so its load screen) can't be reproduced elsewhere.
    /// </remarks>
    void LogisticsFrames(bool gpu)
    {
        const MCRecordedPrefs prefs;
        RequestDrawing(gpu);
        REQUIRE(MCTestGame::StartLogistics());

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

        // The GPU's frames where they differ: the darkened rows and the briefing box are a translate polygon, which the
        // GPU blends (AlphaPal's colours) instead of mapping the indices, so the indices read back there are the ones
        // under the blend. The mirror test checks every other pixel against the software renderer.
        const std::map<std::string_view, uint32_t> gpuExpected = {
            {"briefing", 0xedf1c712u},
            {"repair", 0x86494ea8u},
            {"briefing again", 0x9283c509u},
            {"briefing, mission tab", 0x910061d7u},
            {"briefing, deploy", 0x7ad0a197u},
            {"briefing, deploy second", 0x6bb48f60u},
            {"briefing, hover", 0x424c8518u},
            {"briefing, slot to slot", 0xd5dce922u},
            {"briefing, onto a unit", 0xb7f5a906u},
            {"briefing, box list scroll", 0x4b19056au},
            {"briefing, back to pane", 0x23a19455u},
            {"briefing, deploy again", 0xccbb003du},
            {"repair again", 0x439473e2u},
            {"repair, payload", 0x87cb765eu},
            {"repair, second mech", 0x9c73975au},
            {"repair, component info", 0x63c55b33u},
            {"repair, weapon info", 0x87299c63u},
            {"repair, mech info", 0x90a58371u},
            {"repair, weapon off", 0xd2a02512u},
            {"repair, mount laser", 0x6ac823c3u},
            {"repair, pilots tab", 0xb0ca2b08u},
            {"repair, pilot info", 0xa92d2f68u},
            {"repair, vehicles tab", 0xe49639efu},
            {"repair, mechs tab", 0xfc995c38u},
            {"repair, mech row info", 0x4a729b92u},
            {"repair, mech into force", 0xc54c6411u},
            {"repair, select first", 0x822916a7u},
            {"repair, mech out", 0xd85d8be8u},
            {"repair, weapon list scroll", 0x45ae178au},
            {"repair, first mech out", 0x5af140cbu},
            {"briefing after repair", 0x6beaaa33u},
            {"briefing, unit out of slot", 0x51b8fdeeu},
        };

        const Step steps[] = {
            {"main menu", [] {}, 0x59acc997u},
            {"preferences", [] { ShowPreferences(); }, 0x733386bau},
            {"main menu after preferences", [] { CancelPrefs(); }, 0x59acc997u},
            {"load screen", [] { LoadScreen(); }, 0xb773197eu},
            {"main menu after load", [] { Cancel(); }, 0x59acc997u},
            {"briefing", [] { NewCampaign(); }, 0xbcd2ec30u},
            {"purchase", [] { GlobalLog()->SetUpPurchaseScreen(true); }, 0x7e077fadu},
            {"purchase, pilots", [] { GlobalLog()->PurchaseScreen->SetUpPilotInv(true, true); }, 0x4ae27598u},
            {"purchase, components", [] { GlobalLog()->PurchaseScreen->SetUpCompInv(true, true); }, 0x62910f9cu},
            {"purchase, mechs", [] { GlobalLog()->PurchaseScreen->SetUpMechInv(true, true); }, 0x2ef60e5eu},
            {"repair", [] { GlobalLog()->SetUpRepairScreen(true); }, 0xd28d50c5u},
            {"briefing again", [] { GlobalLog()->SetUpBriefingScreen(true); }, 0x042383bfu},
            {"briefing, mission tab", [] { Click(202, 265); }, 0xd643b44du},
            {"briefing, deploy", [] { Drag(40, 385, 245, 60); }, 0x9339c191u},
            {"briefing, deploy second", [] { Drag(90, 385, 300, 60); }, 0x9a752797u},
            {"briefing, hover", [] { SendMouse(7, 100, 60, false); }, 0x6391f04fu},
            {"briefing, slot to slot", [] { Drag(250, 60, 250, 110); }, 0xc9dbbea0u},
            {"briefing, onto a unit", [] { Drag(250, 110, 300, 60); }, 0x39358860u},
            {"briefing, box list scroll", [] { Click(626, 467); }, 0xd8a55766u},
            {"briefing, back to pane", [] { Drag(300, 60, 60, 400); }, 0xaec42c39u},
            {"briefing, deploy again", [] { Drag(40, 385, 245, 60); }, 0xbcc4420du},
            {"purchase again",
             []
             {
                 // Enough points and stock to buy with; the store shows the stock, as the game redraws a row whose stock
                 // changed.
                 ResourcePoints = 100000;

                 for (const auto& mech : GlobalLog()->PurMechList->Mechs)
                 {
                     for (const auto& variant : mech->Variants)
                     {
                         if (variant != nullptr)
                         {
                             variant->NumAvailable = 2;
                         }
                     }

                     mech->Block->DrawBackground(mech->Block->Row);
                 }

                 for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->PurchaseComponents->Items)
                 {
                     item->Count = 2;
                     item->PurchaseBlock->DrawBackground(item->PurchaseBlock->Row, item->MasterID);
                 }

                 for (const auto& vehicle : GlobalLog()->PurVehicleList->Vehicles)
                 {
                     vehicle->Data->NumAvailable = 2;
                     vehicle->Block->DrawBackground(vehicle->Block->Row);
                 }

                 Click(100, 60);
             },
             0x7949d30bu},
            {"purchase, buy drag", [] { Drag(300, 80, 100, 200); }, 0x829d115au},
            {"purchase, quantity up", [] { Click(378, 249); }, 0x58349157u},
            {"purchase, accept", [] { Click(316, 299); }, 0x3ff329e7u},
            {"purchase, inventory row", [] { Click(100, 120); }, 0xfdd71060u},
            {"purchase, variant", [] { Click(393, 32); }, 0xcccc0558u},
            {"purchase, scroll", [] { Click(631, 474); }, 0x112ee248u},
            {"purchase, too poor",
             []
             {
                 ResourcePoints = 0;
                 Drag(300, 80, 100, 200);
             },
             0x25dfbc93u},
            {"purchase, message ok", [] { Click(366, 283); }, 0x8ec3109bu},
            {"purchase, inventory info", [] { Click(60, 165); }, 0xfda8bafbu},
            {"purchase, pilots tab", [] { Click(202, 180); }, 0x4d9d3575u},
            {"purchase, components tab",
             []
             {
                 ResourcePoints = 100000;
                 Click(202, 265);
             },
             0xe76ce289u},
            {"purchase, component drag", [] { Drag(300, 80, 100, 200); }, 0xe85a82e2u},
            {"purchase, component accept", [] { Click(316, 299); }, 0xb8424311u},
            {"purchase, vehicles tab", [] { Click(202, 335); }, 0x95c3c146u},
            {"purchase, vehicle drag", [] { Drag(300, 80, 100, 200); }, 0xb60a5029u},
            {"purchase, vehicle accept", [] { Click(316, 299); }, 0x441a706du},
            {"purchase, mechs tab", [] { Click(202, 125); }, 0xdc78c644u},
            {"repair again", [] { Click(100, 78); }, 0xa345ababu},
            {"repair, payload", [] { Click(560, 60); }, 0xe9a831a2u},
            {"repair, second mech", [] { Click(300, 170); }, 0x440f6f6eu},
            {"repair, component info", [] { SendMouse(7, 100, 160, false); }, 0x686410c7u},
            {"repair, weapon info", [] { SendMouse(7, 560, 175, false); }, 0xb84e5197u},
            {"repair, mech info", [] { SendMouse(7, 300, 200, false); }, 0xcc3e106du},
            {"repair, weapon off", [] { Drag(560, 175, 100, 200); }, 0x40a5652du},
            {"repair, mount laser", [] { Drag(100, 160, 300, 190); }, 0xdfeac413u},
            {"repair, pilots tab", [] { Click(202, 180); }, 0xea90325bu},
            {"repair, pilot info", [] { SendMouse(7, 100, 140, false); }, 0xb5025f17u},
            {"repair, vehicles tab", [] { Click(202, 335); }, 0xabf60696u},
            {"repair, mechs tab", [] { Click(202, 125); }, 0xb51cd8ddu},
            {"repair, mech row info", [] { SendMouse(7, 100, 140, false); }, 0xceec3967u},
            {"repair, mech into force", [] { Drag(100, 140, 300, 300); }, 0xd3a49146u},
            {"repair, select first", [] { Click(300, 60); }, 0xb62690e0u},
            {"repair, mech out", [] { Drag(260, 170, 100, 200); }, 0xc5511bb0u},
            {"repair, weapon list scroll", [] { Click(560, 125); }, 0xe1d91675u},
            {"repair, first mech out", [] { Drag(260, 60, 100, 200); }, 0x36d78e5cu},
            {"briefing after repair", [] { GlobalLog()->SetUpBriefingScreen(true); }, 0x0680f0e1u},
            {"briefing, unit out of slot", [] { Drag(300, 60, 60, 400); }, 0xc392dab4u},
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
        CHECK_EQ(frames, gpu ? 0x8d317260u : 0xcfbe6bb8u);
        CHECK_EQ(presents, gpu ? 0xd056cb3du : 0xac3d91aau);
        CheckGpuAlone(gpu);
    }
}
