#include "stdafx.h"
#include "MCTest.h"
#include "MCScratchFolder.h"
#include "TestGame.h"
#include "platform/MCFileSystem.h"
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
