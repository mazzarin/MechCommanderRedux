#include "stdafx.h"
#include "gui/MCGuiConsole.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCConsoleCommands.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCWin32Defs.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The caret blink timer's id.</summary>
    constexpr int32_t BlinkTimer = 0;
    /// <summary>The <see cref="MCGuiEventType::Focus"/> data for a gained focus, and a lost one.</summary>
    constexpr int32_t FocusGained = 7;
    constexpr int32_t FocusLost = 8;
    /// <summary>The console's colours: its back, and its frame, text caret and prompt.</summary>
    constexpr int32_t BackColour = 0x10;
    constexpr int32_t LineColour = 0x1f;
    /// <summary>The gap around the console on the screen, and around its text inside it.</summary>
    constexpr int32_t ScreenMargin = 8;
    constexpr int32_t TextMargin = 3;
    /// <summary>What the line starts with.</summary>
    constexpr std::string_view Prompt = "> ";

    /// <summary>Whether <paramref name="key"/> can be typed into the line.</summary>
    bool IsConsoleCharacter(uint8_t key)
    {
        return (key > 0x1f && key < 0x7f) || (key > 0xbe && key < 0xfe);
    }
}

auto ConsoleUsable() -> bool
{
    return DebugOn && Scenario() != nullptr && MultiPlayer() == nullptr && Turn > 0 &&
           EventsToMissionResultsScreen == 0 && !GamePaused && !GameAsked;
}

auto OpenConsole() -> void
{
    MCGuiSystem& gui = GuiSystem();

    if (gui.Console != nullptr || !ConsoleUsable() || ScreenWindow() == nullptr)
    {
        return;
    }

    const int32_t height = WhiteFont->Height() + 2 * TextMargin;
    const int32_t width = ScreenWindow()->Width() - 2 * ScreenMargin;
    auto console = ScreenWindow()->TryCreateChild<MCGuiConsole>(
        [&](MCGuiConsole& made) { return made.Init(ScreenMargin, ScreenMargin, width, height, {}); });

    if (!console)
    {
        return;
    }

    gui.Console = *console;
    gui.SetText(gui.Console);
}

auto CloseConsole() -> void
{
    if (!HasGuiSystem() || GuiSystem().Console == nullptr)
    {
        return;
    }

    // Taken out first: the lost focus below comes back here.
    const MCGuiHandle<MCGuiConsole> console = std::exchange(GuiSystem().Console, {});

    if (GuiSystem().HasText(*console))
    {
        GuiSystem().ReleaseText();
    }

    GuiSystem().Retire(console);
}

auto MCGuiConsole::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    if (const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name); result != 0)
    {
        return result;
    }

    BackgroundColor = BackColour;
    SetDepth(100);
    return 0;
}

auto MCGuiConsole::Draw() -> void
{
    if (!ConsoleUsable())
    {
        CloseConsole();
        return;
    }

    const MCPane& frame = DisplayPort->Frame();
    VfxPaneWipe(frame, BackgroundColor);
    DrawBox(LineColour, -1, -1, -1, -1);
    const std::string line = std::string(Prompt) + Text;
    WhiteFont->WriteString(frame, TextMargin, TextMargin, line);

    if (CursorVisible)
    {
        const int32_t caretX = TextMargin + WhiteFont->Width(line) + 1;
        VfxLineDraw(frame, caretX, TextMargin, caretX, TextMargin + WhiteFont->Height() - 1, LineColour);
    }

    MCGuiObject::Draw();
}

auto MCGuiConsole::HandleEvent(MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::Character:
        {
            if (!GuiSystem().HasText(*this))
            {
                break;
            }

            const uint8_t key = event.Key;

            if (key == '\r')
            {
                if (SkipReturn)
                {
                    SkipReturn = false;
                    break;
                }

                const std::string line = std::move(Text);
                CloseConsole();

                if (auto ran = RunConsoleCommand(line); !ran)
                {
                    SDL_Log("console: %s", ran.error().c_str());
                }
            }
            else if (key == VK_ESCAPE)
            {
                CloseConsole();
            }
            else if (key == '\b')
            {
                if (!Text.empty())
                {
                    Text.pop_back();
                }

                CursorVisible = true;
            }
            else if (Text.size() < MaxLength && IsConsoleCharacter(key) &&
                     WhiteFont->Width(std::string(Prompt) + Text + static_cast<char>(key)) + 2 * TextMargin < Width())
            {
                Text.push_back(static_cast<char>(key));
                CursorVisible = true;
            }
            break;
        }

        case MCGuiEventType::Timer:
        {
            if (event.Data == BlinkTimer)
            {
                CursorVisible = !CursorVisible;
            }
            break;
        }
        case MCGuiEventType::Focus:
        {
            if (event.Data == FocusGained)
            {
                GuiSystem().AddTimer(weak_from_this(), BlinkTimer, static_cast<int32_t>(MCPort::CaretBlinkTime()), 0, 0,
                                     false);
            }
            else if (event.Data == FocusLost)
            {
                GuiSystem().RemoveTimer(*this, BlinkTimer);
                // A click elsewhere took the keyboard away.
                CloseConsole();
            }
            break;
        }
        default:
            break;
    }

    MCGuiObject::HandleEvent(event);
}
