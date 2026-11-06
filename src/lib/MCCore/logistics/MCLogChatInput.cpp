#include "stdafx.h"
#include "logistics/MCLogChatInput.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "lib/MCFatal.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogToolButton.h"
#include "network/MCMultiPlayer.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The caret blink timer.</summary>
    constexpr int32_t BlinkTimer = 0;
    /// <summary>Where the text starts (right of the team button).</summary>
    constexpr int32_t TextX = 0x14;
}

auto MCLogChatInput::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view newText)
    -> int32_t
{
    int32_t result = MCLogObject::Init(xPos, yPos, width, height);
    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't init chatsend window");
    TeamButton = CreateChild<MCLogToolButton>(
        [](MCLogToolButton& button)
        {
            const int32_t buttonResult = button.Init(1, 1, 0xc, 0x1a, {});
            Assert(buttonResult == 0, static_cast<uint32_t>(buttonResult),
                   " Couldn't init team button for chatsend window");
            // A picture that can't be loaded is fatal.
            button.SetOverPicture("lsbdw06.tga");
            button.SetDownPicture("lsbdw07.tga");
            button.SetUpPicture("lsbdw05.tga");
            button.Toggled = false;
            button.SetEventRoutine(ChatTeamButtonEventHandler);
        });

    if (const std::shared_ptr<MCLogChat> chat = Chat(); chat != nullptr)
    {
        chat->CursorX = 0x15;

        if (!newText.empty())
        {
            // Original behaviour: the starting text isn't counted in TextLength (nothing passes one).
            std::string_view start = newText.substr(0, MCLogChat::MaxLength);
            std::ranges::copy(start, chat->Text.begin());
        }
    }

    Font = WhiteFont;
    BackgroundColor = 0x10;
    RestartBlink();
    return 0;
}

auto MCLogChatInput::Draw() -> void
{
    VfxPaneWipe(_Port->Frame(), BackgroundColor);
    const std::shared_ptr<MCLogChat> chat = Chat();

    if (chat == nullptr)
    {
        MCLogObject::Draw();
        return;
    }

    std::string_view line = chat->ShownText();
    int32_t lineY = 1;

    if (!line.empty())
    {
        // The first line leaves room for the team button; the next ones break at spaces.
        int32_t fits = Font->CharactersToWidth(line, Width() - 0x18, false);

        while (fits > 0 && std::cmp_less(fits, line.size()))
        {
            Font->WriteString(_Port->Frame(), TextX, lineY, line.substr(0, static_cast<size_t>(fits)));
            line.remove_prefix(static_cast<size_t>(fits));
            fits = Font->CharactersToWidth(line, Width() - TextX, true);
            lineY += 3 + Font->Height();
        }

        Font->WriteString(_Port->Frame(), TextX, lineY, line);
    }

    // The caret, which the original's display drew into the picture each frame: a vertical line a text line high.
    const int32_t bottom = Font->Height() + 3 + chat->CursorY;
    VfxLineDraw(_Port->Frame(), chat->CursorX, chat->CursorY, chat->CursorX, bottom, chat->CursorOn ? 0x10 : 0x1f);
    MCLogObject::Draw();
}

auto MCLogChatInput::RestartBlink() -> void
{
    if (const std::shared_ptr<MCLogChat> chat = Chat(); chat != nullptr)
    {
        chat->CursorOn = true;
    }
}

auto MCLogChatInput::Display() -> void
{
    MCLogObject::Display();
}

auto MCLogChatInput::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCLogChat> chat = Chat();

    if (chat == nullptr)
    {
        MCGuiObject::HandleEvent(event);
        return;
    }

    // The team button's events come here after it toggled.
    chat->TeamOnly = TeamButton->Toggled;
    std::string& text = chat->Text;
    int32_t& textLength = chat->TextLength;

    switch (event.Type)
    {
        case 1:
            GuiSystem().SetText(weak_from_this());
            break;
        case 10:
        {
            if (!GuiSystem().HasText(*this))
            {
                break;
            }

            const uint8_t key = event.Key;

            if (key == '\b')
            {
                if (textLength != 0)
                {
                    text[--textLength] = 0;
                    SetCursorPos(textLength);
                    RestartBlink();
                }
            }
            else if (key == '\r')
            {
                if (MultiPlayer() != nullptr && textLength != 0)
                {
                    int32_t color;

                    if (!chat->TeamOnly)
                    {
                        MultiPlayer()->SendChat(0, text.data());
                        color = 6;
                    }
                    else
                    {
                        MultiPlayer()->SendChat(MultiPlayer()->HomeTeamGroupID, text.data());
                        color = 4;
                    }

                    chat->ProcessChatString(MultiPlayer()->SessionManager->MyPlayer()->Id, text.data(), color);
                }

                std::ranges::fill(text, '\0');
                textLength = 0;
                SetCursorPos(0);
                RestartBlink();
            }
            else if (textLength < MCLogChat::MaxLength && ((key > 0x1f && key < 0x7f) || (key > 0xbe && key < 0xfe)) &&
                     key != '%')
            {
                // '%' is the chat formatter's code character.
                text[textLength++] = static_cast<char>(key);
                SetCursorPos(textLength);
                RestartBlink();
            }
            break;
        }

        case 0x13:
        {
            if (event.Data == BlinkTimer)
            {
                chat->CursorOn = !chat->CursorOn;
            }
            break;
        }
        case 0x1e:
        {
            if (event.Data == 7)
            {
                GuiSystem().AddTimer(weak_from_this(), BlinkTimer, MCPort::CaretBlinkTime(), 0, 0, false);
            }
            else if (event.Data == 8)
            {
                GuiSystem().RemoveTimer(*this, BlinkTimer);
            }
            break;
        }
        default:
            break;
    }

    MCGuiObject::HandleEvent(event);
}

auto MCLogChatInput::SetCursorPos(int32_t position) -> void
{
    const std::shared_ptr<MCLogChat> chat = Chat();

    if (chat == nullptr)
    {
        return;
    }

    // The text up to position, as far as it is shown.
    std::string_view line = chat->ShownText().substr(0, static_cast<size_t>(position));
    chat->CursorX = TextX;
    chat->CursorY = 0;

    if (!line.empty())
    {
        int32_t fits = Font->CharactersToWidth(line, Width() - 0x18, false);

        while (fits > 0 && std::cmp_less(fits, line.size()))
        {
            line.remove_prefix(static_cast<size_t>(fits));
            fits = Font->CharactersToWidth(line, Width() - TextX, false);
            chat->CursorY += Font->Height() + 3;
        }

        chat->CursorX = Font->Width(line) + TextX + 1;
    }
}
