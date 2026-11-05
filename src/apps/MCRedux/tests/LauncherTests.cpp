#include "stdafx.h"
#include "../../MCXLauncher/MCLauncherArguments.h"
#include "MCTest.h"

// Port-only (the personal build): what MCX.EXE, the Windows launcher, passes on to the game
// (MCXLauncher/MCLauncherArguments.h): the words it was started with, but the uninstaller's options.

namespace
{
    /// <summary><paramref name="text"/> in narrow characters (the tests' lines are ASCII).</summary>
    std::string Narrow(std::wstring_view text)
    {
        std::string narrow;
        std::ranges::transform(text, std::back_inserter(narrow), [](wchar_t c) { return static_cast<char>(c); });
        return narrow;
    }

    /// <summary>The words of <paramref name="line"/> as the game gets them, each between bars.</summary>
    std::string Split(std::wstring_view line)
    {
        std::string split = "|";

        for (const MCLauncherArguments::MCWord& word : MCLauncherArguments::Words(line))
        {
            split += Narrow(word.Text) + "|";
        }

        return split;
    }

    /// <summary>What the launcher passes on of <paramref name="line"/>, in narrow characters.</summary>
    std::string PassedOn(std::wstring_view line)
    {
        return Narrow(MCLauncherArguments::PassedOn(line));
    }
}

TEST_CASE("MCX.EXE launcher: a command line splits into words as Windows' C runtime splits the game's")
{
    CHECK_EQ(Split(LR"(-join 10.0.0.2 -player "Big Bob")"), std::string("|-join|10.0.0.2|-player|Big Bob|"));
    CHECK_EQ(Split(L"  a\tb  "), std::string("|a|b|"));
    CHECK_EQ(Split(L""), std::string("|"));
    CHECK_EQ(Split(LR"("")"), std::string("||"));

    // Backslashes are themselves but before a quote, where they are halved; an odd one left writes the quote.
    CHECK_EQ(Split(LR"(a\\b "c\\" d)"), std::string(R"(|a\\b|c\|d|)"));
    CHECK_EQ(Split(LR"(\"a "b\"c")"), std::string(R"(|"a|b"c|)"));
    CHECK_EQ(Split(LR"("C:\Games\\" -x)"), std::string(R"(|C:\Games\|-x|)"));

    // Inside quotes two quotes write one, still quoted; a quote mid-word quotes on to the next.
    CHECK_EQ(Split(LR"("a""b c" d)"), std::string(R"(|a"b c|d|)"));
    CHECK_EQ(Split(LR"(a"b c"d)"), std::string("|ab cd|"));
}

TEST_CASE("MCX.EXE launcher: the uninstaller's options never reach the game; the other words go as written")
{
    constexpr std::wstring_view LobbyLine = LR"(-join 10.0.0.2 -player "Big Bob" -session "Ann's room")";
    CHECK_EQ(PassedOn(LobbyLine), Narrow(LobbyLine));
    CHECK_EQ(PassedOn(L""), std::string());

    CHECK_EQ(PassedOn(LR"(--remove-content --data "C:\Games\MC" --report "C:\Temp\why.txt" -host)"),
             std::string(R"(--data "C:\Games\MC" -host)"));
    CHECK_EQ(PassedOn(LR"("--remove-content" -host --report)"), std::string("-host"));

    // A folder option's word is the folder, as the game takes it; another spelling is a game option.
    CHECK_EQ(PassedOn(L"--data --remove-content --user --report -host"),
             std::string("--data --remove-content --user --report -host"));
    CHECK_EQ(PassedOn(L"--REMOVE-CONTENT --report=why.txt"), std::string("--REMOVE-CONTENT --report=why.txt"));
}
