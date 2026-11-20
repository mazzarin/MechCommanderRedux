#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ScreenInput.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
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
    /// <summary>A surface's window size and the size it's drawn at, on one axis.</summary>
    struct GridSizes
    {
        uint32_t Window = 0;
        uint32_t Drawn = 0;
    };
}

/// <summary>
/// Port-only (MCDrawnGrid): a world drawn at a size a window pixel isn't a whole number of drawn pixels of (764 window
/// pixels at 3599, full screen on this Mac; 640 at 1600; 1000 at 640) lies on a grid whose offset puts the map's corner
/// on a whole drawn pixel. At every place of the corner a pan reaches, the offset is within half a window's width,
/// the window's first and last drawn pixels show its own pixels, a pan of any length moves what each drawn pixel shows
/// by whole drawn pixels (the grid's whole places' difference: the same map pixel shown there), and the drawn pixel
/// holding a window pixel shows it. Where a window pixel is a whole number of drawn pixels (2x, 1x), it is 0 throughout.
/// </summary>
TEST_CASE("presenter: a world's drawn grid moves its picture by whole drawn pixels as the view pans")
{
    constexpr std::array<int32_t, 7> Moves = {1, 2, 3, 7, -1, -4, 25};
    constexpr int64_t DrawnSamples = 101;

    for (const GridSizes sizes :
         {GridSizes{764, 3599}, GridSizes{640, 1600}, GridSizes{1000, 640}, GridSizes{800, 1600}, GridSizes{480, 480}})
    {
        MCTest::Scope scope(std::format("{} window pixels drawn at {}", sizes.Window, sizes.Drawn));
        const int64_t window = sizes.Window;
        const int64_t drawn = sizes.Drawn;
        const bool wholeScale = drawn % window == 0;
        const int64_t step = std::max<int64_t>(drawn / DrawnSamples, 1);
        int64_t badOffsets = 0;
        int64_t outside = 0;
        int64_t slid = 0;
        int64_t unheld = 0;
        std::set<int32_t> offsets;

        for (auto origin = static_cast<int32_t>(-2 * window); origin <= 2 * window; ++origin)
        {
            const int32_t offset = MCDrawnGrid::Offset(origin, sizes.Window, sizes.Drawn);
            const int64_t whole = MCDrawnGrid::Whole(origin, sizes.Window, sizes.Drawn);
            offsets.insert(offset);
            badOffsets += 2 * offset <= -window || 2 * offset > window || (wholeScale && offset != 0) ||
                                  whole * window - origin * drawn != offset
                              ? 1
                              : 0;

            for (const int64_t edge : {int64_t{0}, drawn - 1})
            {
                const int32_t shown = MCDrawnGrid::WindowPixel(edge, sizes.Window, sizes.Drawn, offset);
                outside += shown < 0 || shown >= window ? 1 : 0;
            }

            for (const int32_t move : Moves)
            {
                const int32_t to = origin + move;
                const int32_t toOffset = MCDrawnGrid::Offset(to, sizes.Window, sizes.Drawn);
                const int64_t shift = MCDrawnGrid::Whole(to, sizes.Window, sizes.Drawn) - whole;

                for (int64_t p = std::max<int64_t>(-shift, 0); p < drawn && p + shift < drawn; p += step)
                {
                    const int32_t before = MCDrawnGrid::WindowPixel(p, sizes.Window, sizes.Drawn, offset) - origin;
                    const int32_t after = MCDrawnGrid::WindowPixel(p + shift, sizes.Window, sizes.Drawn, toOffset) - to;
                    slid += before != after ? 1 : 0;
                }
            }

            for (int64_t t = 0; drawn >= window && t < window; ++t)
            {
                const int32_t holding = MCDrawnGrid::DrawnPixel(t, sizes.Window, sizes.Drawn, offset);
                unheld += MCDrawnGrid::WindowPixel(holding, sizes.Window, sizes.Drawn, offset) != t ? 1 : 0;
            }
        }

        CHECK_EQ(badOffsets, 0);
        CHECK_EQ(outside, 0);
        CHECK_EQ(slid, 0);
        CHECK_EQ(unheld, 0);
        CHECK_EQ(offsets.size() == 1, wholeScale);
    }
}

/// <summary>
/// Port-only (HDSmoothCamera): with the camera's fraction of a window pixel, the drawn grid puts the map's
/// corner on the whole drawn pixel nearest where the camera puts it unrounded (within half a drawn pixel), the offset
/// still the whole place's less the drawn one, and a pan between any two places and fractions moves what each drawn
/// pixel shows by whole drawn pixels (the kept ground slides, never redrawn for the camera's place). A fraction of 0 is
/// the grid without one.
/// </summary>
TEST_CASE("presenter: a world's drawn grid moves its picture by the camera's fraction in whole drawn pixels")
{
    constexpr int32_t FractionStep = 37;
    constexpr int64_t DrawnSamples = 61;
    constexpr int32_t Steps = MCDrawnGrid::FractionSteps;

    for (const GridSizes sizes : {GridSizes{764, 3599}, GridSizes{640, 1600}, GridSizes{1000, 640}})
    {
        MCTest::Scope scope(std::format("{} window pixels drawn at {}", sizes.Window, sizes.Drawn));
        const int64_t window = sizes.Window;
        const int64_t drawn = sizes.Drawn;
        const int64_t step = std::max<int64_t>(drawn / DrawnSamples, 1);
        int64_t unlike = 0;
        int64_t badOffsets = 0;
        int64_t farOff = 0;
        int64_t slid = 0;

        for (auto origin = static_cast<int32_t>(-window); origin <= window; origin += 7)
        {
            unlike += MCDrawnGrid::Offset(origin, sizes.Window, sizes.Drawn, 0) !=
                              MCDrawnGrid::Offset(origin, sizes.Window, sizes.Drawn)
                          ? 1
                          : 0;

            for (int32_t fraction = 1 - Steps; fraction < Steps; fraction += FractionStep)
            {
                const int64_t whole = MCDrawnGrid::Whole(origin, sizes.Window, sizes.Drawn, fraction);
                const int32_t offset = MCDrawnGrid::Offset(origin, sizes.Window, sizes.Drawn, fraction);
                badOffsets += whole * window - origin * drawn != offset ? 1 : 0;
                // |whole - (origin - fraction / F) D / W| <= 1/2, in whole numbers.
                const int64_t apart = 2 * (whole * window * Steps - (int64_t{origin} * Steps - fraction) * drawn);
                farOff += apart > window * Steps || apart < -window * Steps ? 1 : 0;

                for (const auto [move, toFraction] : {std::pair{0, Steps - 1 - fraction}, std::pair{1, -fraction},
                                                      std::pair{-3, fraction / 2}, std::pair{11, 0}})
                {
                    const int32_t to = origin + move;
                    const int32_t toOffset = MCDrawnGrid::Offset(to, sizes.Window, sizes.Drawn, toFraction);
                    const int64_t shift = MCDrawnGrid::Whole(to, sizes.Window, sizes.Drawn, toFraction) - whole;

                    for (int64_t p = std::max<int64_t>(-shift, 0); p < drawn && p + shift < drawn; p += step)
                    {
                        const int32_t before = MCDrawnGrid::WindowPixel(p, sizes.Window, sizes.Drawn, offset) - origin;
                        const int32_t after =
                            MCDrawnGrid::WindowPixel(p + shift, sizes.Window, sizes.Drawn, toOffset) - to;
                        slid += before != after ? 1 : 0;
                    }
                }
            }
        }

        CHECK_EQ(unlike, 0);
        CHECK_EQ(badOffsets, 0);
        CHECK_EQ(farOff, 0);
        CHECK_EQ(slid, 0);
    }
}

/// <summary>
/// Port-only (HDSmoothCamera): the drawn pixels that show a window pixel (MCDrawnGrid::Showing, which the composite
/// shows the world within, its edge past them) are exactly those whose window pixel is in [0, W): all of them without a
/// fraction; with one, every drawn pixel but a strip past the edge the picture moved off, under one window pixel wide
/// (D / W drawn pixels), on one side only.
/// </summary>
TEST_CASE("presenter: a world's drawn pixels that show its window, past which it is shown as its edge")
{
    constexpr int32_t FractionStep = 29;
    constexpr int32_t Steps = MCDrawnGrid::FractionSteps;

    for (const GridSizes sizes :
         {GridSizes{764, 3599}, GridSizes{640, 1600}, GridSizes{1000, 640}, GridSizes{640, 640}})
    {
        MCTest::Scope scope(std::format("{} window pixels drawn at {}", sizes.Window, sizes.Drawn));
        const int64_t window = sizes.Window;
        const int64_t drawn = sizes.Drawn;
        // The widest strip: a whole window pixel's drawn pixels, rounded up.
        const int64_t widest = (drawn + window - 1) / window;
        int64_t wrong = 0;
        int64_t wideStrips = 0;
        int64_t bothSides = 0;
        int64_t cutWithout = 0;

        for (auto origin = static_cast<int32_t>(-window); origin <= window; origin += 13)
        {
            for (int32_t fraction = 1 - Steps; fraction < Steps; fraction += FractionStep)
            {
                const int32_t offset = MCDrawnGrid::Offset(origin, sizes.Window, sizes.Drawn, fraction);
                const auto [first, last] = MCDrawnGrid::Showing(sizes.Window, sizes.Drawn, offset);

                for (int64_t p = 0; p < drawn; ++p)
                {
                    const int32_t shown = MCDrawnGrid::WindowPixel(p, sizes.Window, sizes.Drawn, offset);
                    const bool showing = shown >= 0 && shown < window;
                    wrong += showing != (p >= first && p <= last) ? 1 : 0;
                }

                wideStrips += first > widest || drawn - 1 - last > widest ? 1 : 0;
                bothSides += first > 0 && last < drawn - 1 ? 1 : 0;
            }

            const int32_t whole = MCDrawnGrid::Offset(origin, sizes.Window, sizes.Drawn);
            const auto [first, last] = MCDrawnGrid::Showing(sizes.Window, sizes.Drawn, whole);
            cutWithout += first != 0 || last != drawn - 1 ? 1 : 0;
        }

        CHECK_EQ(wrong, 0);
        CHECK_EQ(wideStrips, 0);
        CHECK_EQ(bothSides, 0);
        CHECK_EQ(cutWithout, 0);
    }
}

/// <summary>
/// Port-only (MCDrawnGrid): over a world on a grid of its own, the mouse picks the window pixel the picture shows where
/// it points (MCDisplay::OnShownGrids). A 764-pixel world drawn at 3599 (full screen on this Mac) at its corner's every
/// offset across a window pixel: at each drawn pixel's centre (but those exactly on a window pixel's edge), the point
/// lands in the window pixel the drawn pixel shows, on both sides of every edge, where the plain mapping misses some;
/// a point over a world on no offset, or past its rectangle, stays where it was.
/// </summary>
TEST_CASE("presenter: over a world on its drawn grid, the mouse picks the pixel the picture shows there")
{
    constexpr uint32_t Window = 764;
    constexpr uint32_t Drawn = 3599;
    constexpr uint32_t WindowRows = 430;
    constexpr uint32_t DrawnRows = 2022;
    constexpr int32_t Left = 18;
    constexpr int32_t Top = 7;
    MCShownGrid grid;
    grid.Rect = MCRect{Left, Top, Left + static_cast<int32_t>(Window) - 1, Top + static_cast<int32_t>(WindowRows) - 1};
    grid.Window = {Window, WindowRows};
    grid.Drawn = {Drawn, DrawnRows};
    const std::array<MCShownGrid, 1> grids = {grid};
    int64_t points = 0;
    int64_t missed = 0;
    int64_t plainMissed = 0;
    int64_t edges = 0;
    int64_t movedDown = 0;
    std::set<int32_t> offsets;
    constexpr float Middle = static_cast<float>(Top) + 0.5f * static_cast<float>(WindowRows);

    // The corner's places across a window pixel's worth of the drawn grid's offsets.
    for (int32_t origin = 0; origin < 2 * static_cast<int32_t>(Window); origin += 3)
    {
        grid.Offset = {MCDrawnGrid::Offset(origin, Window, Drawn), 0};
        offsets.insert(grid.Offset[0]);
        const std::array<MCShownGrid, 1> shown = {grid};
        int32_t last = -1;

        for (int64_t m = 0; m < Drawn; ++m)
        {
            const int32_t expected = MCDrawnGrid::WindowPixel(m, Window, Drawn, grid.Offset[0]);
            const bool edge = expected != last;
            last = expected;

            // A centre exactly on a window pixel's edge belongs to the one after it, which a float can't promise.
            if (((2 * m + 1) * Window - 2 * static_cast<int64_t>(grid.Offset[0])) % (2 * Drawn) == 0)
            {
                continue;
            }

            float x = static_cast<float>(Left) +
                      (static_cast<float>(m) + 0.5f) * static_cast<float>(Window) / static_cast<float>(Drawn);
            float y = Middle;
            const int32_t plain = static_cast<int32_t>(std::floor(x)) - Left;
            MCDisplay::OnShownGrids(shown, x, y);
            const int32_t picked = static_cast<int32_t>(std::floor(x)) - Left;
            ++points;
            edges += edge ? 1 : 0;
            missed += picked != expected ? 1 : 0;
            plainMissed += plain != expected ? 1 : 0;
            movedDown += y != Middle ? 1 : 0;
        }
    }

    std::cout << std::format("  {} points, {} at a window pixel's first drawn pixel; {} the plain mapping misses\n",
                             points, edges, plainMissed);
    CHECK(offsets.size() > 2);
    CHECK(edges > 0);
    CHECK_EQ(missed, 0);
    CHECK(plainMissed > 0);
    CHECK_EQ(movedDown, 0);

    // No offset: unchanged; past the rectangle: unchanged.
    float x = 123.456f;
    float y = 45.5f;
    MCDisplay::OnShownGrids(grids, x, y);
    CHECK_EQ(x, 123.456f);
    CHECK_EQ(y, 45.5f);
    grid.Offset = {MCDrawnGrid::Offset(1, Window, Drawn), MCDrawnGrid::Offset(1, WindowRows, DrawnRows)};
    REQUIRE(grid.Offset[0] != 0);
    const std::array<MCShownGrid, 1> offset = {grid};
    x = static_cast<float>(Left) - 0.25f;
    y = static_cast<float>(Top) + 3.5f;
    MCDisplay::OnShownGrids(offset, x, y);
    CHECK_EQ(x, static_cast<float>(Left) - 0.25f);
    CHECK_EQ(y, static_cast<float>(Top) + 3.5f);
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
        // AUTO: the user's 14-inch Retina MacBook at 1080P and at NATIVE (945 lines) both take 200%, the original's
        // proportions (NATIVE's 473 lines held to the smallest screen's 480).
        {3024, 1890, 2.0f, false, MCDisplay::AutoInterface, 864, 540},
        {3024, 1890, 2.0f, true, MCDisplay::AutoInterface, 768, 480},
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

/// <summary>
/// AUTO's interface scale keeps the original's proportions: the screen's lines over 480, in quarter steps from 1 to 2.
/// </summary>
TEST_CASE("presenter: AUTO's interface scale keeps the original's proportions")
{
    // 1080P, and NATIVE on a 14-inch Retina laptop (945 lines): 2, the most.
    CHECK_EQ(MCDisplay::AutoInterfaceScale(1080), 2.0f);
    CHECK_EQ(MCDisplay::AutoInterfaceScale(945), 2.0f);
    // 900P and 720P; the original's 480 lines and fewer take none.
    CHECK_EQ(MCDisplay::AutoInterfaceScale(900), 2.0f);
    CHECK_EQ(MCDisplay::AutoInterfaceScale(720), 1.5f);
    CHECK_EQ(MCDisplay::AutoInterfaceScale(600), 1.25f);
    CHECK_EQ(MCDisplay::AutoInterfaceScale(MCDisplay::OriginalLines), 1.0f);
    CHECK_EQ(MCDisplay::AutoInterfaceScale(400), 1.0f);
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

    /// <summary>The tests' user folder's PREFS, which the game reads over the install's.</summary>
    std::filesystem::path UserPrefsPath()
    {
        return MCFileSystem::UserRoot() / "prefs.cfg";
    }

    /// <summary>The interface size saved in PREFS.</summary>
    MCFitResult<int32_t> SavedInterfaceSize()
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(UserPrefsPath().string()), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        return prefs.Read<int32_t>("InterfaceSize");
    }

    /// <summary>
    /// The tests' user folder's PREFS as it was when made, put back when it goes (later tests read it): a PREFS
    /// written meanwhile is removed if there was none.
    /// </summary>
    class MCKeptPrefs
    {
    public:
        MCKeptPrefs()
        {
            if (std::ifstream file(UserPrefsPath(), std::ios::binary); file)
            {
                _Kept = std::string(std::istreambuf_iterator<char>(file), {});
            }
        }

        ~MCKeptPrefs()
        {
            if (_Kept)
            {
                std::ofstream(UserPrefsPath(), std::ios::binary) << *_Kept;
            }
            else
            {
                std::filesystem::remove(UserPrefsPath());
            }
        }

        MCKeptPrefs(const MCKeptPrefs&) = delete;
        MCKeptPrefs& operator=(const MCKeptPrefs&) = delete;

    private:
        std::optional<std::string> _Kept;
    };

    /// <summary>The drop-down under <paramref name="parent"/> (at any depth) that offers <paramref name="label"/>.</summary>
    std::shared_ptr<MCLogComboBox> FindDropDownOffering(MCGuiObject& parent, std::string_view label)
    {
        for (const std::shared_ptr<MCGuiObject>& child : parent.Children())
        {
            if (auto combo = std::dynamic_pointer_cast<MCLogComboBox>(child);
                combo != nullptr && std::ranges::contains(combo->Items(), label, [](const MCLogComboBox::Item& item)
                                                          { return std::string_view(item.Label); }))
            {
                return combo;
            }

            if (std::shared_ptr<MCLogComboBox> found = FindDropDownOffering(*child, label); found != nullptr)
            {
                return found;
            }
        }

        return nullptr;
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
/// let go after them doesn't toggle the tactical map, as alt alone does. Alt with the keypad's + and - does the same.
/// </summary>
TEST_CASE_ISOLATED("game: alt+= and alt+- step the interface size in a mission and PREFS keeps it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(StartInLargeWindow());
    const MCKeptPrefs keptPrefs;
    const std::filesystem::path prefsPath = UserPrefsPath();
    const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    const int32_t mapHidden = map->IsHidden();

    // A size the keys land on is kept as AUTO when it is AUTO's pick for this window (200% at 1080 lines).
    const auto autoSize = static_cast<int>(
        std::lround(MCDisplay::AutoInterfaceScale(LargeWindowLines) * static_cast<float>(OriginalInterfaceSize)));
    const auto kept = [autoSize](int size) { return size == autoSize ? AutoInterfaceSize : size; };

    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_EQUALS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, kept(125));
    CHECK_EQ(display->Height(), 864);
    CHECK_EQ(map->IsHidden(), mapHidden);
    CHECK_EQ(SavedInterfaceSize(), MCFitResult<int32_t>(kept(125)));

    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_MINUS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, kept(OriginalInterfaceSize));
    CHECK_EQ(display->Height(), LargeWindowLines);
    CHECK_EQ(map->IsHidden(), mapHidden);

    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_KP_PLUS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, kept(125));
    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_KP_MINUS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, kept(OriginalInterfaceSize));
    CHECK_EQ(map->IsHidden(), mapHidden);

    // AUTO: the screen takes AUTO's pick's lines.
    GInterfaceSize = AutoInterfaceSize;
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(display->Height(), std::max(static_cast<int>(std::lround(static_cast<double>(LargeWindowLines) *
                                                                      OriginalInterfaceSize / autoSize)),
                                         MCDisplay::OriginalLines));

    // From AUTO the keys step on from its pick, and a step back onto the pick is AUTO again.
    REQUIRE(autoSize > OriginalInterfaceSize);
    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_MINUS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, SteppedInterfaceSize(autoSize, -1));
    CHECK_EQ(SavedInterfaceSize(), MCFitResult<int32_t>(SteppedInterfaceSize(autoSize, -1)));
    MCScreenInput::RealChord(SDL_SCANCODE_LALT, SDL_KMOD_LALT, SDL_SCANCODE_EQUALS);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(GInterfaceSize, AutoInterfaceSize);
    // AUTO, the default, isn't written: PREFS without the key reads as AUTO.
    CHECK(!SavedInterfaceSize().has_value());
    GInterfaceSize = OriginalInterfaceSize;
    MCTestGame::RunFrame(1.0f / 15.0f);

    // PREFS read back: a size that isn't a choice is AUTO.
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
        CHECK_EQ(GInterfaceSize, AutoInterfaceSize);
    }
}

/// <summary>
/// The preferences screen's INTERFACE SIZE offers AUTO, labelled with the size it picks at the UI RESOLUTION chosen,
/// and 100% to 200%: a choice sets the interface size, CANCEL puts it back, and ACCEPT saves it in PREFS. The next
/// mission draws its interface at it: 150% of 1080P is 720 lines.
/// </summary>
TEST_CASE_ISOLATED("game: INTERFACE SIZE on the preferences screen is saved in PREFS and sizes the next mission")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::BootInLargeWindow(LargeWindowLines, OriginalInterfaceSize);
    REQUIRE(MCTestGame::StartLogistics());
    const MCKeptPrefs keptPrefs;
    constexpr int ChosenSize = 150;

    const auto frames = []
    {
        for (int32_t frame = 0; frame < 5; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    };

    ShowPreferences();
    frames();
    const std::shared_ptr<MCLogComboBox> dropDown =
        FindDropDownOffering(*GlobalLog()->PrefScreen, std::format("{}%", ChosenSize));
    REQUIRE(dropDown != nullptr);
    const std::vector<MCLogComboBox::Item>& items = dropDown->Items();
    REQUIRE_EQ(items.size(), InterfaceSizes.size() + 1);
    CHECK_EQ(items[0].Value, AutoInterfaceSize);

    for (size_t i = 0; i < InterfaceSizes.size(); i++)
    {
        CHECK(items[i + 1].Label == std::format("{}%", InterfaceSizes[i]));
        CHECK_EQ(items[i + 1].Value, InterfaceSizes[i]);
    }

    // AUTO says the size it picks at the UI RESOLUTION chosen: 200% at 1080P.
    const auto autoLabel = [](int lines)
    {
        return std::format("AUTO ({}%)", std::lround(MCDisplay::AutoInterfaceScale(lines) *
                                                     static_cast<float>(OriginalInterfaceSize)));
    };

    CHECK(items[0].Label == autoLabel(LargeWindowLines));

    // The field shows the size as it is; a click on a drop-down's field opens its list, a click on a row chooses it.
    const auto shown = [&] { return items[static_cast<size_t>(dropDown->Selected())].Value; };
    CHECK_EQ(shown(), OriginalInterfaceSize);
    const auto choose = [&](MCLogComboBox& combo, int32_t value)
    {
        const std::vector<MCLogComboBox::Item>& choices = combo.Items();
        const auto row = static_cast<int32_t>(
            std::distance(choices.begin(), std::ranges::find(choices, value, &MCLogComboBox::Item::Value)));
        REQUIRE(row < std::ssize(choices));
        const int32_t middleX = combo.GlobalX() + combo.Width() / 2;
        MCScreenInput::Click(middleX, combo.GlobalY() + MCLogComboBox::FieldHeight / 2);
        frames();
        REQUIRE(combo.IsOpen());
        MCScreenInput::SaveShot(std::format("prefs choosing {}", value), MCScreenInput::ScreenHash());
        MCScreenInput::Click(middleX, combo.GlobalY() + MCLogComboBox::FieldHeight + row * MCLogComboBox::RowHeight +
                                          MCLogComboBox::RowHeight / 2);
        frames();
        CHECK(!combo.IsOpen());
    };

    // UI RESOLUTION 720P: AUTO is 150% there.
    constexpr int OtherLines = 720;
    const std::shared_ptr<MCLogComboBox> uiResolution =
        FindDropDownOffering(*GlobalLog()->PrefScreen, std::format("{}P", OtherLines));
    REQUIRE(uiResolution != nullptr);
    choose(*uiResolution, OtherLines);
    CHECK_EQ(GUiResolution, OtherLines);
    CHECK(items[0].Label == autoLabel(OtherLines));

    choose(*dropDown, ChosenSize);
    CHECK_EQ(GInterfaceSize, ChosenSize);
    CHECK_EQ(shown(), ChosenSize);
    CancelPrefs();
    frames();
    CHECK_EQ(GInterfaceSize, OriginalInterfaceSize);
    CHECK_EQ(GUiResolution, LargeWindowLines);

    ShowPreferences();
    frames();
    CHECK_EQ(shown(), OriginalInterfaceSize);
    CHECK(items[0].Label == autoLabel(LargeWindowLines));
    choose(*dropDown, ChosenSize);
    WritePrefs();
    frames();
    CHECK_EQ(SavedInterfaceSize(), MCFitResult<int32_t>(ChosenSize));

    // The next mission: the screen has that many times fewer lines than UI RESOLUTION's, at the window's aspect.
    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartCampaignMission());
    const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
    REQUIRE(display != nullptr);
    const int lines = LargeWindowLines * OriginalInterfaceSize / ChosenSize;
    CHECK_EQ(display->Height(), lines);
    CHECK_EQ(display->Width(), lines * MCTestGame::LargeWindowWidth / MCTestGame::LargeWindowHeight);
    CHECK_EQ(GuiSystem().Height(), lines);
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
