#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "appear/MCAppearance.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "platform/MCDisplay.h"

/// <summary>
/// While the zoom eases, units stay where the terrain is: each frame, the screen position a mover took in its update
/// is the one the camera projects it to when the frame is drawn. (The view's size used to change at the draw, after
/// the units had placed themselves with the last frame's size, so they wobbled until the zoom settled.)
/// </summary>
TEST_CASE_ISOLATED("game: units keep their place on the ground while the zoom eases")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCViewWindow> view = MCMainView();
    REQUIRE(view != nullptr);
    std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x200);
    REQUIRE(mover != nullptr);
    REQUIRE(mover->GetAppearance() != nullptr);

    MCFixedZoomHeight = 0.0f;
    REQUIRE(view->ZoomTo(800.0f));
    int32_t easedFrames = 0;

    for (int32_t frame = 0; frame < 30 && view->ZoomHeight != view->ZoomTarget; frame++)
    {
        MCTest::Scope scope(std::format("frame {}, {:.1f} lines", frame, view->ZoomHeight));
        MCTestGame::RunFrame(1.0f / 15.0f);
        const MCVector2D placed = mover->GetScreenPos(0);
        const MCVector2D projected = mover->GetAppearance()->GetScreenPos(*view->GetCamera());
        CHECK(std::fabs(placed.X - projected.X) <= 1.0f);
        CHECK(std::fabs(placed.Y - projected.Y) <= 1.0f);
        easedFrames++;
    }

    // The zoom took several frames to get there, and got there.
    CHECK(easedFrames > 2);
    CHECK_EQ(view->ZoomHeight, 800.0f);
}

namespace
{
    /// <summary>The window the starting-zoom tests boot in, as a player's window was in the menus.</summary>
    constexpr int LargeWindowWidth = 1600;
    constexpr int LargeWindowHeight = 1200;
    /// <summary>The corner of the screen the menus are drawn in, and the display shows for them.</summary>
    constexpr int MenuWidth = 640;
    constexpr int MenuHeight = 480;

    /// <summary>
    /// Boots mission 1 with the zoom free in a 1600x1200 window, the screen at most <paramref name="uiResolution"/>
    /// lines (PREFS "UIResolution"), so it is shown scaled up. Then, as the display showed when the mission's views
    /// started, only the menus' 640x480 corner is shown: a view starting now starts at one world pixel per window pixel,
    /// as tall as the window, whatever the screen's lines. (It read the corner's scale, 1, and started at the screen's
    /// lines; the mission start's zoom toggle then zooms out, which hid it, except for a camera starting zoomed out.)
    /// </summary>
    void CheckStartingZoom(int uiResolution)
    {
        if (!MCTestGame::Available())
        {
            return;
        }

        MCTestGame::BootOptions.WindowWidth = LargeWindowWidth;
        MCTestGame::BootOptions.WindowHeight = LargeWindowHeight;
        MCTestGame::BootOptions.FixedZoomHeight = 0.0f;
        MCTestGame::BootOptions.Started = [uiResolution] { GUiResolution = uiResolution; };
        REQUIRE(MCTestGame::StartMission(1));
        const std::shared_ptr<MCViewWindow> view = MCMainView();
        REQUIRE(view != nullptr);
        const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
        REQUIRE(display != nullptr);
        REQUIRE_EQ(display->Height(), uiResolution);
        REQUIRE_EQ(view->Height(), display->Height());
        constexpr auto windowLines = static_cast<float>(LargeWindowHeight);

        display->SetView(0, 0, MenuWidth, MenuHeight);
        CHECK_EQ(display->UnderlayScale(), 1.0f);
        CHECK(std::fabs(display->ScreenScale() * static_cast<float>(display->Height()) - windowLines) < 0.5f);
        view->ZoomHeight = 0.0f;
        view->StartZoom();
        CHECK(std::fabs(view->ZoomHeight - windowLines) < 0.5f);
        CHECK(std::fabs(view->ZoomInHeight() - windowLines) < 0.5f);
    }
}

TEST_CASE_ISOLATED("game: at 1080P a mission's view starts at one world pixel per window pixel")
{
    CheckStartingZoom(1080);
}

TEST_CASE_ISOLATED("game: at 720P a mission's view starts at one world pixel per window pixel")
{
    CheckStartingZoom(720);
}
