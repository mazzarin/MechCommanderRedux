#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "platform/MCInput.h"
#include "platform/MCStringTable.h"

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
