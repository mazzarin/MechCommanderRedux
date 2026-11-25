#include "stdafx.h"
#include "MCTest.h"
#include "MCScratchFolder.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCHardwareCursor.h"
#include "gui/MCScreenChange.h"
#include "gui/MCUpdateDisplay.h"
#include "logistics/MCPauseScreen.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCStringTable.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlScreen.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
    /// <summary>A player's 16:9 window: the 1998 frame (640x480) 960x720 in its middle, the menus' stone beside it.</summary>
    constexpr int WideWidth = 1280;
    constexpr int WideHeight = 720;
    constexpr float FlowFrame = 1.0f / 60.0f;
    constexpr int32_t MostFrames = 3000;

    /// <summary>A window area as the checks print it.</summary>
    std::string AreaWords(const std::optional<SDL_Rect>& area)
    {
        return area ? std::format("{},{} {}x{}", area->x, area->y, area->w, area->h) : std::string("anywhere");
    }

    /// <summary>The area SDL was last asked to keep the mouse to (SDL_SetWindowMouseRect), or none.</summary>
    std::optional<SDL_Rect> AskedArea()
    {
        const SDL_Rect* asked = SDL_GetWindowMouseRect(&MCInput::Display()->Window());
        return asked != nullptr ? std::optional<SDL_Rect>(*asked) : std::nullopt;
    }

    /// <summary>The wide window kept off its corners: where the mouse may go over the remade menus, full screen.</summary>
    SDL_Rect WideWindowGuarded()
    {
        return MCInput::GuardCorners(SDL_Rect{0, 0, WideWidth, WideHeight});
    }

    /// <summary>The picture (the game's screen) kept off its corners: where the mouse may go over the game's own, full screen.</summary>
    SDL_Rect PictureGuarded()
    {
        const std::optional<SDL_Rect> picture = MCInput::PictureArea();
        REQUIRE(picture.has_value());
        return MCInput::GuardCorners(*picture);
    }

    /// <summary>Frames while a change of screen runs, then one (a change queued behind it starts), then its own.</summary>
    void RunWhileChanging()
    {
        for (int32_t pass = 0; pass < 2; pass++)
        {
            for (int32_t frame = 0; frame < MostFrames && MCScreenChange::Running(); frame++)
            {
                MCTestGame::RunFrame(FlowFrame);
            }

            MCTestGame::RunFrame(FlowFrame);
        }
    }
}

TEST_CASE("input: key lParam and Windows-1252")
{
    MCInput::MCScanCode escape = MCInput::Win32ScanCode(SDL_SCANCODE_ESCAPE);
    CHECK_EQ(escape.Code, 0x01);
    CHECK_EQ(MCInput::MakeKeyLParam(escape, false, false, false), 0x00010001);
    MCInput::MCScanCode up = MCInput::Win32ScanCode(SDL_SCANCODE_UP);
    CHECK(up.Extended);
    CHECK_EQ(static_cast<uint32_t>(MCInput::MakeKeyLParam(up, false, true, true)), 0xc1480001u);
    CHECK_EQ(MCInput::VirtualKeyFromScancode(SDL_SCANCODE_KP_8, SDLK_UNKNOWN, true), VK_NUMPAD8);
    CHECK_EQ(MCInput::VirtualKeyFromScancode(SDL_SCANCODE_KP_8, SDLK_UNKNOWN, false), VK_UP);
    // The key left of Z on European keyboards (a German one's <) has Windows' codes, so it can be bound.
    CHECK_EQ(MCInput::Win32ScanCode(SDL_SCANCODE_NONUSBACKSLASH).Code, 0x56);
    CHECK_EQ(MCInput::VirtualKeyFromScancode(SDL_SCANCODE_NONUSBACKSLASH, SDLK_LESS, true), VK_OEM_102);
    CHECK_EQ(MCInput::CodePointToWindows1252('A'), 'A');
    CHECK_EQ(MCInput::CodePointToWindows1252(0x20ac), 0x80);
    CHECK_EQ(MCInput::CodePointToWindows1252(0x00e9), 0xe9);
    CHECK_EQ(MCInput::CodePointToWindows1252(0x4e00), -1);
    CHECK_EQ(MCInput::MakePointLParam(-1, 2), 0x0002ffff);
}

TEST_CASE("input: full screen, the cursor stays off the screen's corners and still reaches its sides")
{
    // A 1512x982-point screen showing a 1663x1080 picture: the cursor keeps two points from either side.
    const SDL_Rect guarded = MCInput::GuardCorners(SDL_Rect{0, 0, 1512, 982});
    CHECK_EQ(guarded.x, MCInput::CornerGuardPoints);
    CHECK_EQ(guarded.x + guarded.w, 1512 - MCInput::CornerGuardPoints);
    CHECK_EQ(guarded.y, 0);
    CHECK_EQ(guarded.h, 982);

    // The last point it reaches at a side is that side, for edge scrolling; inside, the plain mapping holds.
    CHECK_EQ(MCInput::GuardedX(2.0f, guarded, 2, 0, 1663), 0);
    CHECK_EQ(MCInput::GuardedX(2.9f, guarded, 3, 0, 1663), 0);
    CHECK_EQ(MCInput::GuardedX(3.0f, guarded, 3, 0, 1663), 3);
    CHECK_EQ(MCInput::GuardedX(1508.9f, guarded, 1659, 0, 1663), 1659);
    CHECK_EQ(MCInput::GuardedX(1509.0f, guarded, 1659, 0, 1663), 1662);

    // Too narrow to lose four points, an area is kept whole.
    const SDL_Rect narrow = MCInput::GuardCorners(SDL_Rect{10, 0, 4, 10});
    CHECK_EQ(narrow.x, 10);
    CHECK_EQ(narrow.w, 4);
}

/// <summary>
/// Full screen, the mouse is kept to the picture over the game's own screens and to the whole window while the layer over
/// the game fills it (the remade menus, whose screen is the window), either off its corners; the game's ClipCursor
/// rectangle wins; windowed it goes anywhere.
/// </summary>
TEST_CASE("input: full screen, the mouse reaches the whole window while the menus fill it, else the picture")
{
    // A 1512x982-point window (an M1 Air's screen) with the 1998 frame 1309 points wide in its middle.
    MCInput::MCMouseAreaState area;
    area.FullscreenFocused = true;
    area.Picture = SDL_Rect{101, 0, 1309, 982};
    area.Window = SDL_Point{1512, 982};
    CHECK_EQ(AreaWords(MCInput::MouseArea(area)), AreaWords(SDL_Rect{103, 0, 1305, 982}));

    area.LayerFillsWindow = true;
    CHECK_EQ(AreaWords(MCInput::MouseArea(area)), AreaWords(SDL_Rect{2, 0, 1508, 982}));

    // A window whose size couldn't be read keeps the picture's.
    area.Window = SDL_Point{0, 0};
    CHECK_EQ(AreaWords(MCInput::MouseArea(area)), AreaWords(SDL_Rect{103, 0, 1305, 982}));
    area.Window = SDL_Point{1512, 982};

    const SDL_Rect clip{200, 100, 300, 200};
    area.Clip = clip;
    CHECK_EQ(AreaWords(MCInput::MouseArea(area)), AreaWords(clip));

    // Windowed (or without the focus), anywhere but the game's rectangle.
    area.FullscreenFocused = false;
    CHECK_EQ(AreaWords(MCInput::MouseArea(area)), AreaWords(clip));
    area.Clip.reset();
    CHECK_EQ(AreaWords(MCInput::MouseArea(area)), AreaWords(std::nullopt));
}

/// <summary>
/// The cursor shown: the game's own as a system cursor wherever it gives one; a cursor it draws into its frame has the
/// system's arrow in its place over the layer's screens (drawn over that frame, bars and all) and over the bars; a
/// cursor it hides stays hidden over the screens (a load's).
/// </summary>
TEST_CASE("input: the system's arrow stands in for the game's drawn cursor over the menus' screens and the bars")
{
    using MCInput::MCLayerShows;
    using MCInput::MCShownCursor;
    MCInput::MCCursorShowing showing;
    CHECK(MCInput::ShownCursor(showing) == MCShownCursor::Arrow);

    // The game hid the system's: its cursor drawn into the frame, over the picture.
    showing.Counter = -1;
    showing.DrawnCursor = true;
    CHECK(MCInput::ShownCursor(showing) == MCShownCursor::None);
    showing.OutsidePicture = true;
    CHECK(MCInput::ShownCursor(showing) == MCShownCursor::Arrow);
    showing.OutsidePicture = false;

    for (const MCLayerShows layer : {MCLayerShows::Window, MCLayerShows::OverPicture})
    {
        showing.Layer = layer;
        CHECK(MCInput::ShownCursor(showing) == MCShownCursor::Arrow);

        // The game shows no cursor (a load): none over the screens, beside the 1998 frame too.
        showing.DrawnCursor = false;
        showing.OutsidePicture = true;
        CHECK(MCInput::ShownCursor(showing) == MCShownCursor::None);
        showing.DrawnCursor = true;
        showing.OutsidePicture = false;
    }

    // The game's cursor as a system cursor (the default, hardware): it, over the screens and the bars too.
    showing.GameCursor = true;
    CHECK(MCInput::ShownCursor(showing) == MCShownCursor::Game);
    showing.OutsidePicture = true;
    showing.Layer = MCLayerShows::Nothing;
    CHECK(MCInput::ShownCursor(showing) == MCShownCursor::Game);
}

/// <summary>
/// Full screen in a 16:9 window: over the remade main menu the mouse reaches the whole window (playtest 1's "15%
/// deadzone on each side" was the 1998 frame in its middle, gone with the 1998 menus), and a move at the window's edge
/// is the menus'; in the mission begun from them, and under its pause, it stays on the battle's picture as before.
/// </summary>
TEST_CASE_ISOLATED(
    "game: full screen, the mouse reaches the whole window over the remade menus, the battle's picture in a mission")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::BootOptions.WindowWidth = WideWidth;
    MCTestGame::BootOptions.WindowHeight = WideHeight;
    const MCTestGame::KeepPrefs keptPrefs;
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    REQUIRE(MCRmlFlow::Showing());
    MCInput::TakeAsFullscreen(true);
    MCTestGame::RunFrame(FlowFrame);

    // The mouse reaches the window's sides.
    REQUIRE(MCInput::PictureArea().has_value());
    CHECK_EQ(AreaWords(AskedArea()), AreaWords(WideWindowGuarded()));

    // A move at the window's left side reaches the main menu's document.
    const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
    REQUIRE(menus.has_value());
    const MCOptionalRef<MCRmlScreen> top = menus->get().Menus().Top();
    REQUIRE(top.has_value());
    MCScreenInput::PixelMove(static_cast<float>(MCInput::CornerGuardPoints), static_cast<float>(WideHeight / 2),
                             FlowFrame);
    const Rml::Element* hovered = menus->get().Menus().Context().GetHoverElement();
    REQUIRE(hovered != nullptr);
    REQUIRE(top->get().Document().has_value());
    CHECK(hovered->GetOwnerDocument() == &top->get().Document()->get());

    // The mission begun from the remade campaign screens: the battle's picture.
    REQUIRE(MCTestGame::StartCampaignMission());

    for (int32_t step = 0; step < MostFrames && Turn <= Scenario()->StartUpTurns; step++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    REQUIRE(Turn > Scenario()->StartUpTurns);
    CHECK(!MCRmlFlow::Showing());
    CHECK_EQ(AreaWords(AskedArea()), AreaWords(PictureGuarded()));

    // The pause floats over the battle, which fills the window: the battle's picture still.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    REQUIRE(MCPauseScreen::Shows());
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(AreaWords(AskedArea()), AreaWords(PictureGuarded()));
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(!MCPauseScreen::Shows());
    MCInput::TakeAsFullscreen(false);
}

namespace
{
    /// <summary>Whether the window shows the system's own arrow (SDL's default cursor), not the game's.</summary>
    bool ArrowShows()
    {
        return SDL_CursorVisible() && SDL_GetCursor() != nullptr && SDL_GetCursor() == SDL_GetDefaultCursor();
    }

    /// <summary>
    /// Whether the game's frame (the screen as shown) has its cursor drawn where the game last drew it (UpdateDisplay's
    /// MouseScreenX, MouseScreenY): every pixel of the shape's picture there.
    /// </summary>
    bool CursorDrawnInFrame()
    {
        const int32_t shape = GuiSystem().CursorShape;

        if (shape < 0)
        {
            return false;
        }

        const MCCursorImage image = MCCursorImageFromShape(GuiSystem().CursorShapeData(shape), 0);
        const MCWindow& screen = ScreenPort()->Bitmap();
        const int32_t width = screen.XMax + 1;
        const int32_t height = screen.YMax + 1;
        const std::vector<uint8_t> shown = MCScreenInput::ShownScreen();
        int32_t opaque = 0;
        int32_t matching = 0;

        for (int32_t y = 0; y < image.Height; y++)
        {
            for (int32_t x = 0; x < image.Width; x++)
            {
                const size_t from = static_cast<size_t>(y) * static_cast<size_t>(image.Width) + static_cast<size_t>(x);
                const int32_t screenX = MouseScreenX - image.HotX + x;
                const int32_t screenY = MouseScreenY - image.HotY + y;

                if (image.Opaque[from] == 0 || screenX < 0 || screenY < 0 || screenX >= width || screenY >= height)
                {
                    continue;
                }

                opaque++;

                if (shown[static_cast<size_t>(screenY) * static_cast<size_t>(width) + static_cast<size_t>(screenX)] ==
                    image.Pixels[from])
                {
                    matching++;
                }
            }
        }

        return opaque > 0 && matching == opaque;
    }

    /// <summary>Where the mouse is put over the remade main menu: the 1998 frame's middle, and beside the frame.</summary>
    constexpr float MenuMiddleX = WideWidth / 2.0f;
    constexpr float MenuSideX = 40.0f;
    constexpr float MenuY = WideHeight / 2.0f;
    /// <summary>Where the mouse is put over the battle: its world, clear of the edges that scroll it and of the pause.</summary>
    constexpr float BattleX = WideWidth / 2.0f;
    constexpr float BattleY = WideHeight / 3.0f;
    constexpr float BesidePauseX = WideWidth / 8.0f;
    constexpr float BesidePauseY = WideHeight / 8.0f;
}

/// <summary>
/// With the software cursor (PREFS SoftwareCursor: the 1998 cursor drawn into the game's frame), over the remade screens,
/// which are drawn over that frame, the system's arrow shows at the mouse, and no cursor is drawn into the frame (round a
/// screen that floats over the battle, the pause, it would show beside the arrow); in the battle the cursor is drawn into
/// the frame and the system's hidden, as before.
/// </summary>
TEST_CASE_ISOLATED(
    "game: with the software cursor, the system's arrow over the remade screens, the drawn cursor in battle")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::BootOptions.WindowWidth = WideWidth;
    MCTestGame::BootOptions.WindowHeight = WideHeight;
    MCTestGame::BootOptions.Started = [] { GSoftwareCursor = 1; };
    const MCTestGame::KeepPrefs keptPrefs;
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    REQUIRE(MCRmlFlow::Showing());

    // The remade main menu: the arrow, over the 1998 frame's middle and beside it.
    for (const float x : {MenuMiddleX, MenuSideX})
    {
        MCTest::Scope scope(std::format("the main menu at {}", x));
        MCScreenInput::PixelMove(x, MenuY, FlowFrame);
        REQUIRE(GuiSystem().CursorShape >= 0);
        CHECK(ArrowShows());
        CHECK(!CursorDrawnInFrame());
    }

    // The mission begun from the remade campaign screens: the cursor drawn into the battle's frame at the mouse.
    REQUIRE(MCTestGame::StartCampaignMission());

    for (int32_t step = 0; step < MostFrames && Turn <= Scenario()->StartUpTurns; step++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    REQUIRE(Turn > Scenario()->StartUpTurns);
    REQUIRE(!MCRmlFlow::Showing());
    MCScreenInput::PixelMove(BattleX, BattleY, FlowFrame);
    CHECK(!SDL_CursorVisible());
    CHECK(CursorDrawnInFrame());

    // The pause over the battle: the arrow, over the battle beside it too.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    REQUIRE(MCPauseScreen::Shows());
    MCScreenInput::PixelMove(BesidePauseX, BesidePauseY, FlowFrame);
    CHECK(ArrowShows());
    CHECK(!CursorDrawnInFrame());

    // Back in the battle: drawn again, the system's hidden.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    REQUIRE(!MCPauseScreen::Shows());
    MCScreenInput::PixelMove(BattleX, BattleY, FlowFrame);
    CHECK(!SDL_CursorVisible());
    CHECK(CursorDrawnInFrame());
}

TEST_CASE("input: posted messages reach the window procedure")
{
    MCInput::Reset();
    std::vector<uint32_t> seen;
    MCInput::SetWindowProc(
        [&seen](uint32_t message, uint32_t, int32_t)
        {
            seen.push_back(message);
            return 0;
        });
    MCInput::PostMessage(WM_USER + 1, 0, 0);
    CHECK(MCInput::PumpMessages());
    REQUIRE_EQ(seen.size(), size_t{1});
    CHECK_EQ(seen[0], WM_USER + 1);
    MCInput::PostMessage(WM_QUIT, 3, 0);
    CHECK(!MCInput::PumpMessages());
    CHECK_EQ(MCInput::QuitCode(), 3);
    MCInput::SetWindowProc({});
    MCInput::Reset();
}

TEST_CASE("input: a posted WM_CLOSE destroys the window and quits, as DefWindowProc did")
{
    MCInput::Reset();
    std::vector<uint32_t> seen;
    bool cancel = true;
    MCInput::SetWindowProc(
        [&seen, &cancel](uint32_t message, uint32_t, int32_t)
        {
            seen.push_back(message);
            return message == WM_CLOSE && cancel ? 1 : 0;
        });

    // A window procedure that cancels the close keeps the game running.
    MCInput::PostMessage(WM_CLOSE, 0, 0);
    CHECK(MCInput::PumpMessages());
    CHECK(seen == std::vector<uint32_t>{WM_CLOSE});

    seen.clear();
    cancel = false;
    MCInput::PostMessage(WM_CLOSE, 0, 0);
    CHECK(!MCInput::PumpMessages());
    CHECK(seen == (std::vector<uint32_t>{WM_CLOSE, WM_DESTROY}));
    CHECK_EQ(MCInput::QuitCode(), 0);
    MCInput::SetWindowProc({});
    MCInput::Reset();
}

TEST_CASE("input: held, the mouse and keys reach nothing but Escape's own, and quitting still works")
{
    MCInput::Reset();
    std::vector<uint32_t> seen;
    MCInput::SetWindowProc(
        [&seen](uint32_t message, uint32_t, int32_t)
        {
            seen.push_back(message);
            return 0;
        });

    const auto key = [](SDL_Keycode code, SDL_Scancode scancode, bool down, bool repeat = false)
    {
        SDL_Event made{};
        made.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        made.key.key = code;
        made.key.scancode = scancode;
        made.key.down = down;
        made.key.repeat = repeat;
        MCInput::HandleEvent(made);
    };

    const auto motion = [](float x, float y)
    {
        SDL_Event made{};
        made.type = SDL_EVENT_MOUSE_MOTION;
        made.motion.x = x;
        made.motion.y = y;
        MCInput::HandleEvent(made);
    };

    // An A held as the hold starts is let go: its release would never come.
    key(SDLK_A, SDL_SCANCODE_A, true);
    REQUIRE(MCInput::IsKeyDown('A'));
    int32_t escapes = 0;
    MCInput::HoldInput(true, [&escapes] { escapes++; });
    CHECK(!MCInput::IsKeyDown('A'));
    MCInput::SetCursorPos(100, 80);
    seen.clear();

    key(SDLK_B, SDL_SCANCODE_B, true);
    key(SDLK_B, SDL_SCANCODE_B, false);
    motion(30.0f, 40.0f);
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    MCInput::HandleEvent(button);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0f;
    MCInput::HandleEvent(wheel);
    CHECK(seen.empty());
    CHECK(!MCInput::IsKeyDown('B'));
    CHECK(!MCInput::IsKeyDown(VK_LBUTTON));
    CHECK_EQ(MCInput::GetCursorPos().x, 100);
    CHECK_EQ(MCInput::GetCursorPos().y, 80);

    // Escape is the hold's own, once a press; the game never sees it.
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, true);
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, true, true);
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, false);
    CHECK_EQ(escapes, 1);
    CHECK(seen.empty());

    // Closing the window still closes it.
    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    MCInput::HandleEvent(close);
    CHECK(seen == (std::vector<uint32_t>{WM_CLOSE, WM_DESTROY}));

    // Let go, the mouse and keys reach the game again.
    MCInput::HoldInput(false);
    seen.clear();
    key(SDLK_B, SDL_SCANCODE_B, true);
    motion(30.0f, 40.0f);
    CHECK(MCInput::IsKeyDown('B'));
    CHECK(std::ranges::find(seen, WM_KEYDOWN) != seen.end());
    CHECK_EQ(MCInput::GetCursorPos().x, 30);
    CHECK_EQ(MCInput::GetCursorPos().y, 40);
    MCInput::SetWindowProc({});
    MCInput::Reset();
}

TEST_CASE("game: MCX.EXE string table loads")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCStringTable& table = MCStringTable::Game();
    CHECK(table.Count() > 100);
    char buffer[256];
    int found = 0;

    for (uint32_t id = 0; id < 0x1000 && found == 0; id++)
    {
        found = table.LoadString(id, buffer);
    }

    CHECK(found > 0);
    CHECK_EQ(static_cast<int>(std::strlen(buffer)), found);
}

namespace
{
    /// <summary>The string tests' scratch folders' group.</summary>
    constexpr std::string_view StringsGroup = "mc_tests_strings";

    void WriteFile(const std::filesystem::path& path, std::string_view bytes)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::string Text(const MCStringTable& table, uint32_t id)
    {
        return table.Find(id).value_or("<none>");
    }
}

TEST_CASE("strings: MCX.EXE's table read from its text file, escapes and all")
{
    // As tools/release/game_strings.py writes it, a comment first; a line ended as Windows' editors end them.
    const auto table = MCStringTable::ParseText("# The original game's texts.\n"
                                                "1\tObjective %i: %s --  \n"
                                                "97\tPre-Release build\\nDo Not Distribute\r\n"
                                                "\n"
                                                "153\ta\\\\b\\tc\\rd\n"
                                                "200\tcaf\xc3\xa9 \xe2\x82\xac \xe4\xb8\x80 \xff\n"
                                                "201\t");
    REQUIRE(table.has_value());
    CHECK_EQ(table->Count(), 5u);
    CHECK_EQ(Text(*table, 1), "Objective %i: %s --  ");
    CHECK_EQ(Text(*table, 97), "Pre-Release build\nDo Not Distribute");
    CHECK_EQ(Text(*table, 153), "a\\b\tc\rd");
    // Windows-1252, as the executable's table is: e acute and the euro sign kept, what it lacks (and bad UTF-8) '?'.
    CHECK_EQ(Text(*table, 200), "caf\xe9 \x80 ? ?");
    CHECK_EQ(Text(*table, 201), "");

    // No tab, no number, an unknown or cut-off escape, a text twice, a number past 32 bits, a NUL character.
    constexpr std::array<std::string_view, 8> refused{
        "x\ty",   "1 y",        "\tno number",         "1\ta\\q",
        "1\ta\\", "1\ta\n1\tb", "4294967296\ttoo big", std::string_view("1\ta\0b", 5)};

    for (const std::string_view line : refused)
    {
        if (MCStringTable::ParseText(line).has_value())
        {
            FAIL_CHECK(std::format("read, though it shouldn't be: {}", line));
        }
    }

    CHECK_EQ(MCStringTable::ParseText("1\ta\n1\tb").error(), "line 2: text 1 again");
}

TEST_CASE("strings: the game reads MCX.EXE's texts, and the text file's when MCX.EXE holds none")
{
    const MCScratchFolder folder(StringsGroup, "game");
    const std::filesystem::path exe = folder.Path() / "MCX.EXE";
    const std::filesystem::path text = folder.Path() / MCStringTable::TextFileName;

    // Neither: why not, both named.
    const auto neither = MCStringTable::LoadGame(exe, text);
    REQUIRE(!neither.has_value());
    CHECK(neither.error().find("MCX.EXE") != std::string::npos);
    CHECK(neither.error().find(MCStringTable::TextFileName) != std::string::npos);

    // The Windows setup's launcher in MCX.EXE's place: a program without the game's texts.
    WriteFile(exe, "MZ, a launcher");
    WriteFile(text, "1\tfrom the text file\n");
    const auto launcher = MCStringTable::LoadGame(exe, text);
    REQUIRE(launcher.has_value());
    CHECK_EQ(Text(*launcher, 1), "from the text file");

    // A text file that can't be read says where.
    WriteFile(text, "1\tfine\nnot a line\n");
    const auto broken = MCStringTable::LoadGame(exe, text);
    REQUIRE(!broken.has_value());
    CHECK(broken.error().find("line 2") != std::string::npos);

    if (!MCTestGame::Available())
    {
        return;
    }

    // A real program holding no texts, as the launcher is (the original's MCCONFIG.DLL has no resources).
    std::filesystem::copy_file(MCFileSystem::Resolve("MCCONFIG.DLL"), exe,
                               std::filesystem::copy_options::overwrite_existing);
    WriteFile(text, "1\tfrom the text file\n");
    const auto program = MCStringTable::LoadGame(exe, text);
    REQUIRE(program.has_value());
    CHECK_EQ(program->Count(), 1u);

    // The original's MCX.EXE: its own texts, whatever the text file holds (a copy in another language keeps its own).
    const auto original = MCStringTable::Load(MCFileSystem::Resolve("MCX.EXE"));
    REQUIRE(original.has_value());
    std::filesystem::copy_file(MCFileSystem::Resolve("MCX.EXE"), exe,
                               std::filesystem::copy_options::overwrite_existing);
    WriteFile(text, "1\tnot the executable's\n");
    const auto own = MCStringTable::LoadGame(exe, text);
    REQUIRE(own.has_value());
    CHECK(*own == *original);
}

/// <summary>
/// Tool: <c>--strings-exe &lt;MCX.EXE&gt; --strings-text &lt;MCX strings.txt&gt;</c> reads the original's table from
/// both with the game's code and checks they're the same: the release script's text file (tools/release/test_make.sh).
/// </summary>
TEST_CASE_ISOLATED("tool: --strings-exe and --strings-text hold the same texts")
{
    const std::optional<std::string_view> exe = MCTest::Option("strings-exe");

    if (!exe.has_value())
    {
        return;
    }

    const std::optional<std::string_view> text = MCTest::Option("strings-text");
    REQUIRE(text.has_value());
    const auto fromExe = MCStringTable::Load(MCFileSystem::FromUtf8(*exe));
    const auto fromText = MCStringTable::LoadText(MCFileSystem::FromUtf8(*text));

    if (!fromExe || !fromText)
    {
        std::cout << std::format("  {}\n", !fromExe ? fromExe.error() : fromText.error());
    }

    REQUIRE(fromExe.has_value());
    REQUIRE(fromText.has_value());
    REQUIRE(fromExe->Count() > 0);
    CHECK(*fromExe == *fromText);
    std::cout << std::format("  {} texts, {}\n", fromExe->Count(),
                             *fromExe == *fromText ? "the same in both" : "differing");
}
