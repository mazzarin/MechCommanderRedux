#pragma once

// Port-only (the personal build): what MCX.EXE, the launcher (Main.cpp), passes on to MCRedux.exe: the words it was
// started with, as written, but the options only the Windows uninstaller gives the game, --remove-content and
// --report <file> (it runs MCRedux.exe itself, never MCX.EXE), so nothing that starts MCX.EXE (a lobby, a shortcut) has
// the game remove its content. Without Windows, so the game's tests check it.

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace MCLauncherArguments
{
    /// <summary>The uninstaller's option, which the launcher never passes on (MCRedux's Main.cpp).</summary>
    inline constexpr std::wstring_view RemoveContentOption = L"--remove-content";
    /// <summary>The uninstaller's option the launcher never passes on, with its file, the word after it.</summary>
    inline constexpr std::wstring_view ReportOption = L"--report";
    /// <summary>The game's options whose next word is a folder, never an option (MCRedux's Main.cpp).</summary>
    inline constexpr std::array<std::wstring_view, 3> FolderOptions{L"--data", L"-d", L"--user"};

    /// <summary>One word of a command line: as written there, and as the program gets it.</summary>
    struct MCWord
    {
        std::wstring_view Written;
        std::wstring Text;
    };

    /// <summary>Whether <paramref name="c"/> parts words outside quotes.</summary>
    inline bool IsSpace(wchar_t c)
    {
        return c == L' ' || c == L'\t';
    }

    /// <summary>
    /// The words of <paramref name="arguments"/> (a command line after the program's name) as Windows' C runtime splits
    /// them for wmain: spaces and tabs part them outside quotes; a quote starts or ends the quoting, and inside it two
    /// quotes write one; backslashes before a quote are halved, an odd one left writing the quote; other backslashes
    /// are themselves.
    /// </summary>
    inline std::vector<MCWord> Words(std::wstring_view arguments)
    {
        constexpr wchar_t Quote = L'"';
        constexpr wchar_t Backslash = L'\\';
        std::vector<MCWord> words;
        size_t at = 0;

        for (;;)
        {
            while (at < arguments.size() && IsSpace(arguments[at]))
            {
                ++at;
            }

            if (at >= arguments.size())
            {
                return words;
            }

            const size_t start = at;
            std::wstring text;
            bool quoted = false;

            while (at < arguments.size() && (quoted || !IsSpace(arguments[at])))
            {
                size_t backslashes = 0;

                while (at < arguments.size() && arguments[at] == Backslash)
                {
                    ++backslashes;
                    ++at;
                }

                if (at < arguments.size() && arguments[at] == Quote)
                {
                    text.append(backslashes / 2, Backslash);

                    if (backslashes % 2 == 1 || (quoted && at + 1 < arguments.size() && arguments[at + 1] == Quote))
                    {
                        // A quote written: after an odd backslash, or the first of two inside quotes.
                        text += Quote;
                        at += backslashes % 2 == 1 ? 1 : 2;
                    }
                    else
                    {
                        quoted = !quoted;
                        ++at;
                    }

                    continue;
                }

                text.append(backslashes, Backslash);

                if (at < arguments.size() && (quoted || !IsSpace(arguments[at])))
                {
                    text += arguments[at];
                    ++at;
                }
            }

            words.push_back({arguments.substr(start, at - start), std::move(text)});
        }
    }

    /// <summary>
    /// What the launcher passes on of <paramref name="arguments"/>: each word as written, a space between them, but
    /// --remove-content, and --report with the word after it. The word after a folder option stays, whatever it says:
    /// the game takes it for the folder.
    /// </summary>
    inline std::wstring PassedOn(std::wstring_view arguments)
    {
        const std::vector<MCWord> words = Words(arguments);
        std::wstring passed;
        const auto pass = [&](const MCWord& word)
        {
            if (!passed.empty())
            {
                passed += L' ';
            }

            passed += word.Written;
        };

        for (size_t i = 0; i < words.size(); i++)
        {
            const std::wstring& text = words[i].Text;

            if (text == RemoveContentOption)
            {
                continue;
            }

            if (text == ReportOption)
            {
                // And its file.
                ++i;
                continue;
            }

            pass(words[i]);

            if (std::ranges::contains(FolderOptions, std::wstring_view(text)) && i + 1 < words.size())
            {
                pass(words[++i]);
            }
        }

        return passed;
    }
}
