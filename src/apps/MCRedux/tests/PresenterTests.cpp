#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ScreenInput.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPreferencesMenu.h"
#include "main/MCSystemConfig.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"

namespace
{
    bool SameViewport(const MCViewport& actual, const MCViewport& expected)
    {
        return actual.X == expected.X && actual.Y == expected.Y && actual.W == expected.W && actual.H == expected.H;
    }

    /// <summary>
    /// Points of the shown view mapped to the window and back land on the same logical pixel (the mouse goes where
    /// the cursor is drawn).
    /// </summary>
    void CheckMouseRoundTrip(const MCDisplay& display)
    {
        const SDL_Rect view = display.View();

        for (const auto& [x, y] :
             {std::pair{0, 0}, std::pair{view.w / 2, view.h / 3}, std::pair{view.w - 1, view.h - 1}})
        {
            MCTest::Scope scope(std::format("({}, {})", x, y));
            float windowX = 0.0f;
            float windowY = 0.0f;
            display.LogicalToWindow(static_cast<float>(view.x + x) + 0.5f, static_cast<float>(view.y + y) + 0.5f,
                                    windowX, windowY);
            float logicalX = 0.0f;
            float logicalY = 0.0f;
            CHECK(display.WindowToLogical(windowX, windowY, logicalX, logicalY));
            CHECK_EQ(static_cast<int>(std::floor(logicalX)), view.x + x);
            CHECK_EQ(static_cast<int>(std::floor(logicalY)), view.y + y);
        }
    }

    /// <summary>The frame read back from the presenter is the CPU's composite through the shown palette.</summary>
    void CheckReadFrame(MCDisplay& display)
    {
        const auto read = display.ReadFrame();
        REQUIRE(read.has_value());
        const std::vector<uint8_t> composed = display.ComposeScreen();
        REQUIRE_EQ(read->size(), composed.size());
        SDL_Color colors[256];
        display.GetShownColors(colors);
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

    /// <summary>
    /// The screen fade (the scene fades between screens) darkens the frame as shown, whatever was drawn: each component
    /// loses the fade's levels, down to black, and the frame comes back when the fade is lifted.
    /// </summary>
    void CheckFade(MCDisplay& display)
    {
        const auto unfaded = display.ReadFrame();
        REQUIRE(unfaded.has_value());

        for (const int levels : {100, 255})
        {
            MCTest::Scope scope(std::format("fade {}", levels));
            display.SetFade(levels);
            const auto faded = display.ReadFrame();
            REQUIRE(faded.has_value());
            REQUIRE_EQ(faded->size(), unfaded->size());
            int32_t different = 0;

            for (size_t i = 0; i < unfaded->size(); i++)
            {
                const SDL_Color& before = (*unfaded)[i];
                const SDL_Color& after = (*faded)[i];

                if (after.r != std::max(before.r - levels, 0) || after.g != std::max(before.g - levels, 0) ||
                    after.b != std::max(before.b - levels, 0))
                {
                    different++;
                }
            }

            CHECK_EQ(different, 0);
        }

        display.SetFade(0);
        const auto lifted = display.ReadFrame();
        REQUIRE(lifted.has_value());
        REQUIRE_EQ(lifted->size(), unfaded->size());
        CHECK(std::memcmp(lifted->data(), unfaded->data(), unfaded->size() * sizeof(SDL_Color)) == 0);
    }
}

/// <summary>
/// The shown view is fitted into the window as SDL's logical presentation fits it: letterboxed and centred, by whole
/// multiples when asked, or stretched over the whole window.
/// </summary>
TEST_CASE("presenter: the view is letterboxed, integer-scaled or stretched into the window")
{
    CHECK(SameViewport(MCPresentViewport(1920, 1080, 640, 480, {}), MCViewport{240.0f, 0.0f, 1440.0f, 1080.0f}));
    CHECK(SameViewport(MCPresentViewport(1280, 1024, 640, 480, {}), MCViewport{0.0f, 32.0f, 1280.0f, 960.0f}));
    CHECK(SameViewport(MCPresentViewport(1280, 960, 640, 480, {}), MCViewport{0.0f, 0.0f, 1280.0f, 960.0f}));

    MCPresentation integer;
    integer.IntegerScale = true;
    CHECK(SameViewport(MCPresentViewport(1920, 1080, 640, 480, integer), MCViewport{320.0f, 60.0f, 1280.0f, 960.0f}));
    // Smaller than the view: shown at 1x, centred (cut off at the edges).
    CHECK(SameViewport(MCPresentViewport(600, 400, 640, 480, integer), MCViewport{-20.0f, -40.0f, 640.0f, 480.0f}));

    MCPresentation stretch;
    stretch.Stretch = true;
    stretch.IntegerScale = true;
    CHECK(SameViewport(MCPresentViewport(1920, 1080, 640, 480, stretch), MCViewport{0.0f, 0.0f, 1920.0f, 1080.0f}));

    // Nothing to show into, or nothing to show.
    CHECK(SameViewport(MCPresentViewport(0, 1080, 640, 480, {}), MCViewport{}));
    CHECK(SameViewport(MCPresentViewport(1920, 1080, 0, 480, {}), MCViewport{}));
}

/// <summary>
/// A screen shown scaled up keeps its underlay at the output's resolution (PREFS "UIResolution" under the window's
/// height): each output pixel shows the screen pixel under its centre, and where that is the key over the underlay,
/// the world pixel under the output pixel's own centre. A 4x2 screen at 8x4 with a 6x4 world under its right three
/// columns: the UI's pixels are doubled, the world's shown one for one, and a key outside the underlay stays the key.
/// </summary>
TEST_CASE("presenter: a scaled-up screen composites its underlay at the output's resolution")
{
    constexpr uint8_t Key = MCRenderer::UnderlayKey;
    const std::array<uint8_t, 8> screenPixels = {7, Key, Key, 9, Key, Key, 8, Key};
    auto world = std::make_shared<MCWindow>();
    world->Pixels = std::make_shared<uint8_t[]>(6 * 4);
    world->XMax = 5;
    world->YMax = 3;

    for (size_t i = 0; i < 6 * 4; i++)
    {
        world->Pixels[i] = static_cast<uint8_t>(100 + i);
    }

    MCWindow screen;
    MCUnderlay underlay;
    underlay.Rect = MCRect{1, 0, 3, 1};
    underlay.Source = world;
    const std::array<MCUnderlay, 1> underlays = {underlay};
    MCFrame frame{.Screen = screen};
    frame.Pixels = screenPixels;
    frame.Width = 4;
    frame.Height = 2;
    frame.View = SDL_Rect{0, 0, 4, 2};
    frame.Underlays = underlays;
    frame.OutputWidth = 8;
    frame.OutputHeight = 4;
    REQUIRE(frame.ScaledOutput());

    std::vector<uint8_t> composed;
    MCComposeScaled(frame, composed);
    const std::vector<uint8_t> expected = {
        7,   7,   100, 101, 102, 103, 9,   9,   //
        7,   7,   106, 107, 108, 109, 9,   9,   //
        Key, Key, 112, 113, 8,   8,   116, 117, //
        Key, Key, 118, 119, 8,   8,   122, 123,
    };

    CHECK(composed == expected);
}

namespace
{
    /// <summary>
    /// Boots mission 1, picks <paramref name="uiResolution"/> (PREFS "UIResolution") and makes the window 1600x1200:
    /// the screen is then <paramref name="lines"/> tall at the window's aspect, and the world view is composited at
    /// the window's pixels (the display's underlay scale is the window's lines per screen line).
    /// </summary>
    void CheckUiResolution(int uiResolution, int lines)
    {
        if (!MCTestGame::Available())
        {
            return;
        }

        REQUIRE(MCTestGame::StartMission(1));
        const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
        REQUIRE(display != nullptr);
        GUiResolution = uiResolution;
        REQUIRE(SDL_SetWindowSize(&display->Window(), 1600, 1200));
        SDL_SyncWindow(&display->Window());
        MCTestGame::RunFrame(1.0f / 15.0f);

        int pixelWidth = 0;
        int pixelHeight = 0;
        REQUIRE(SDL_GetWindowSizeInPixels(&display->Window(), &pixelWidth, &pixelHeight));
        REQUIRE(pixelHeight > lines);
        CHECK_EQ(GuiSystem().Height(), lines);
        CHECK_EQ(GuiSystem().Width(), static_cast<int32_t>(std::lround(static_cast<double>(pixelWidth) * lines /
                                                                       static_cast<double>(pixelHeight))));
        CHECK_EQ(display->Height(), lines);
        CHECK(std::fabs(display->UnderlayScale() - static_cast<float>(pixelHeight) / static_cast<float>(lines)) <
              0.01f);
    }
}

TEST_CASE_ISOLATED("game: UI RESOLUTION 480P draws the mission's UI 480 lines tall in a taller window")
{
    CheckUiResolution(480, 480);
}

TEST_CASE_ISOLATED("game: UI RESOLUTION NATIVE stops at 1080 lines")
{
    CheckUiResolution(0, MaxUiResolution);
}

/// <summary>
/// A window-following screen's size: the window's pixels up to the most lines (UI RESOLUTION, or the window's points
/// for NATIVE on a Retina screen), at the window's aspect; a bigger interface takes that many times fewer lines, never
/// fewer than the smallest screen's 480; a window shorter than the most is the line count it divides.
/// </summary>
TEST_CASE("presenter: a bigger interface gives the screen that many times fewer lines")
{
    /// <summary>A window (pixels, pixels a point), the screen's limits at 1080P or NATIVE, and the screen it gets.</summary>
    struct MCFitCase
    {
        int WindowWidth = 0;
        int WindowHeight = 0;
        float DisplayScale = 1.0f;
        bool Native = false;
        float InterfaceScale = 1.0f;
        int ScreenWidth = 0;
        int ScreenHeight = 0;
    };

    const MCFitCase cases[] = {
        // 1600x1200 at 1080P: 1080 lines, then 864 at 125%, 720 at 150% and 540 at 200%.
        {1600, 1200, 1.0f, false, 1.0f, 1440, 1080},
        {1600, 1200, 1.0f, false, 1.25f, 1152, 864},
        {1600, 1200, 1.0f, false, 1.5f, 960, 720},
        {1600, 1200, 1.0f, false, 2.0f, 720, 540},
        // A Retina MacBook's 3024x1890 at NATIVE: its 945 points, 630 lines at 150%, and 480 (not 473) at 200%.
        {3024, 1890, 2.0f, true, 1.0f, 1512, 945},
        {3024, 1890, 2.0f, true, 1.5f, 1008, 630},
        {3024, 1890, 2.0f, true, 2.0f, 768, 480},
        // A window shorter than the most: its own lines are divided.
        {1280, 800, 1.0f, false, 1.0f, 1280, 800},
        {1280, 800, 1.0f, false, 1.25f, 1024, 640},
        // The smallest window keeps the smallest screen.
        {640, 480, 1.0f, false, 2.0f, 640, 480},
    };

    for (const MCFitCase& fitCase : cases)
    {
        MCTest::Scope scope(std::format("{}x{} at {}x, interface {}", fitCase.WindowWidth, fitCase.WindowHeight,
                                        fitCase.DisplayScale, fitCase.InterfaceScale));
        const MCScreenFit fit{.MaxHeight = MaxUiResolution,
                              .FollowDisplayScale = fitCase.Native,
                              .InterfaceScale = fitCase.InterfaceScale};
        const SDL_Point screen =
            MCDisplay::FollowingScreenSize(fitCase.WindowWidth, fitCase.WindowHeight, fitCase.DisplayScale, fit);
        CHECK_EQ(screen.x, fitCase.ScreenWidth);
        CHECK_EQ(screen.y, fitCase.ScreenHeight);
    }
}

/// <summary>The interface size keys step through INTERFACE SIZE's choices and stop at either end.</summary>
TEST_CASE("presenter: the interface size steps through its choices")
{
    CHECK_EQ(SteppedInterfaceSize(OriginalInterfaceSize, 1), 125);
    CHECK_EQ(SteppedInterfaceSize(150, -1), 125);
    CHECK_EQ(SteppedInterfaceSize(175, 1), 200);
    CHECK_EQ(SteppedInterfaceSize(200, 1), 200);
    CHECK_EQ(SteppedInterfaceSize(OriginalInterfaceSize, -1), OriginalInterfaceSize);
    // A size that isn't a choice counts as the original's.
    CHECK_EQ(SteppedInterfaceSize(137, 1), 125);
}

namespace
{
    /// <summary>The UI RESOLUTION the interface size tests boot at: 1080 lines in the 1600x1200 window.</summary>
    constexpr int LargeWindowLines = 1080;

    /// <summary>
    /// Boots mission 1 in the 1600x1200 window at 1080P with the zoom free, the interface at the original's size.
    /// </summary>
    bool StartInLargeWindow()
    {
        MCTestGame::BootInLargeWindow(LargeWindowLines, OriginalInterfaceSize);
        return MCTestGame::StartMission(1);
    }

    /// <summary>Sends an SDL mouse event at window point (<paramref name="x"/>, <paramref name="y"/>) as play does, then a frame.</summary>
    void WindowMouse(SDL_EventType type, float x, float y)
    {
        SDL_Event event{};
        event.type = type;

        if (type == SDL_EVENT_MOUSE_MOTION)
        {
            event.motion.windowID = SDL_GetWindowID(&MCInput::Display()->Window());
            event.motion.x = x;
            event.motion.y = y;
        }
        else
        {
            event.button.windowID = SDL_GetWindowID(&MCInput::Display()->Window());
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = x;
            event.button.y = y;
        }

        MCInput::HandleEvent(event);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }
}

/// <summary>
/// INTERFACE SIZE 150% in a 1600x1200 window at 1080P: the screen goes from 1080 lines to 720 (960x720) at once, the
/// main view still shows the same lines of the world (one world pixel per window pixel still 1200 of them), and a
/// click where the window shows the tactical map's map moves the camera to that point of it.
/// </summary>
TEST_CASE_ISOLATED("game: INTERFACE SIZE 150% draws a 1080P mission's interface 720 lines tall over the same world")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(StartInLargeWindow());
    const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
    const std::shared_ptr<MCViewWindow> view = MCMainView();
    REQUIRE(display != nullptr);
    REQUIRE(view != nullptr);
    REQUIRE_EQ(display->Height(), LargeWindowLines);
    const float zoom = view->ZoomHeight;
    const int32_t worldHeight = view->WorldHeight();
    CHECK(std::fabs(view->ZoomInHeight() - static_cast<float>(MCTestGame::LargeWindowHeight)) < 0.5f);

    GInterfaceSize = 150;
    MCTestGame::RunFrame(1.0f / 15.0f);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(display->Width(), 960);
    CHECK_EQ(display->Height(), 720);
    CHECK_EQ(GuiSystem().Height(), 720);
    CHECK_EQ(view->Height(), 720);
    CHECK_EQ(view->ZoomHeight, zoom);
    CHECK_EQ(view->WorldHeight(), worldHeight);
    CHECK(std::fabs(view->ZoomInHeight() - static_cast<float>(MCTestGame::LargeWindowHeight)) < 0.5f);

    // The middle of the map page, where the window shows it: the screen point times the window's pixels a line.
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    REQUIRE(map->IsHidden() == 0);
    REQUIRE(map->DisplayType == MCTacmapPage::Map);
    const int32_t localX = (map->MapRect.left + map->MapRect.right) / 2;
    const int32_t localY = (map->MapRect.top + map->MapRect.bottom) / 2;
    const float scale = static_cast<float>(MCTestGame::LargeWindowHeight) / static_cast<float>(display->Height());
    const float windowX = (static_cast<float>(map->GlobalX() + localX) + 0.5f) * scale;
    const float windowY = (static_cast<float>(map->GlobalY() + localY) + 0.5f) * scale;
    MCVector3D target(static_cast<float>(localX), static_cast<float>(localY), 0.0f);
    map->TacMapToWorld(target, true);
    const MCVector3D before = view->GetCamera()->GetPosition();
    REQUIRE(std::fabs(before.X - target.X) + std::fabs(before.Y - target.Y) > 1.0f);

    WindowMouse(SDL_EVENT_MOUSE_MOTION, windowX, windowY);
    WindowMouse(SDL_EVENT_MOUSE_MOTION, windowX, windowY);
    WindowMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, windowX, windowY);
    WindowMouse(SDL_EVENT_MOUSE_BUTTON_UP, windowX, windowY);
    const MCVector3D position = view->GetCamera()->GetPosition();
    CHECK(std::fabs(position.X - target.X) < 1.0f);
    CHECK(std::fabs(position.Y - target.Y) < 1.0f);
}

/// <summary>
/// In a mission, alt+= makes the interface a size bigger and alt+- a size smaller, at once, and PREFS keeps it; alt
/// let go after them doesn't toggle the tactical map, as alt alone does.
/// </summary>
TEST_CASE_ISOLATED("game: alt+= and alt+- step the interface size in a mission and PREFS keeps it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(StartInLargeWindow());
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::optional<std::string> savedPrefs;

    if (std::ifstream file(prefsPath, std::ios::binary); file)
    {
        savedPrefs = std::string(std::istreambuf_iterator<char>(file), {});
    }

    const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    const int32_t mapHidden = map->IsHidden();

    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_EQUALS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, 125);
    CHECK_EQ(display->Height(), 864);
    CHECK_EQ(map->IsHidden(), mapHidden);

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefs.Read<int32_t>("InterfaceSize"), MCFitResult<int32_t>(125));
    }

    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_MINUS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, OriginalInterfaceSize);
    CHECK_EQ(display->Height(), LargeWindowLines);
    CHECK_EQ(map->IsHidden(), mapHidden);

    // PREFS read back: a size that isn't a choice is the original's.
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Create(prefsPath.string()), 0);
        prefs.WriteBlock("MechCommander");
        prefs.WriteIdLong("InterfaceSize", 175);
    }

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        ReadPreferences(prefs);
        CHECK_EQ(GInterfaceSize, 175);
        prefs.Close();
        REQUIRE_EQ(prefs.Create(prefsPath.string()), 0);
        prefs.WriteBlock("MechCommander");
        prefs.WriteIdLong("InterfaceSize", 137);
    }

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        ReadPreferences(prefs);
        CHECK_EQ(GInterfaceSize, OriginalInterfaceSize);
    }

    // The test user folder as it was: later tests read its PREFS.
    if (savedPrefs)
    {
        std::ofstream(prefsPath, std::ios::binary) << *savedPrefs;
    }
    else
    {
        std::filesystem::remove(prefsPath);
    }
}

/// <summary>The renderer's name in PREFS "Renderer" and on the command line (-renderer), any case.</summary>
TEST_CASE("presenter: renderer names")
{
    CHECK(MCRendererKindFromName("vulkan") == MCRendererKind::Vulkan);
    CHECK(MCRendererKindFromName("Vulkan") == MCRendererKind::Vulkan);
    CHECK(MCRendererKindFromName("SOFTWARE") == MCRendererKind::Software);
    CHECK(!MCRendererKindFromName("d3d12").has_value());
    CHECK(!MCRendererKindFromName("").has_value());
    CHECK(!MCRendererKindFromName("vulkan2").has_value());

    for (const MCRendererKind kind : {MCRendererKind::Vulkan, MCRendererKind::Software})
    {
        CHECK(MCRendererKindFromName(MCRendererKindName(kind)) == kind);
    }
}

/// <summary>
/// The software renderer (PREFS "Renderer" = software): the game boots on it, the frame it shows is the CPU's
/// composite, and the mouse maps into the view and back.
/// </summary>
TEST_CASE_ISOLATED("game: the software renderer shows the CPU's composite")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    CHECK(display->RendererKind() == MCRendererKind::Software);
    CHECK(!display->CompositesOnGpu());
    MCTestGame::RunFrame(1.0f / 15.0f);
    CheckReadFrame(*display);
    CheckFade(*display);
    CheckMouseRoundTrip(*display);
}

/// <summary>
/// The Vulkan renderer (the default): the game boots on it, presents through its swapchain, and the mouse maps into
/// the view and back.
/// </summary>
TEST_CASE_ISOLATED("game: the Vulkan renderer presents and maps the mouse")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);

    if (display->RendererKind() != MCRendererKind::Vulkan)
    {
        std::cout << "  (skipped: Vulkan can't start on this machine)\n";
        return;
    }

    CHECK(display->CompositesOnGpu());

    for (int32_t frame = 0; frame < 3; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
        CHECK(display->Present().has_value());
    }

    // The GPU's surfaces hold colours already resolved through the palette, so this is the fade the palette can't do.
    CheckFade(*display);
    CheckMouseRoundTrip(*display);
}
