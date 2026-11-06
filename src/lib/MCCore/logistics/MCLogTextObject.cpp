#include "stdafx.h"
#include "logistics/MCLogTextObject.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCMainMenu.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The cursor blink timer's id.</summary>
    constexpr int32_t BlinkTimer = 0;
}

auto MCLogTextObject::OnDestroy() -> void
{
    BufferSize = 0;
    Buffer.clear();
    OriginalText.clear();
    MCLogObject::OnDestroy();
}

auto MCLogTextObject::Text() const -> std::string_view
{
    const std::string_view buffer = Buffer;
    return buffer.substr(0, buffer.find('\0'));
}

auto MCLogTextObject::Draw() -> void
{
    VfxPaneWipe(_Port->Frame(), static_cast<uint32_t>(BackgroundColor));
    Font->WriteString(_Port->Frame(), 1, 1, Text());

    if (CursorPos > -1 && CursorPos < BufferSize)
    {
        const uint32_t color = CursorLit ? 0x10 : 0x1f;
        VfxLineDraw(_Port->Frame(), CursorPixel, 0, CursorPixel, Height(), color);
    }
}

auto MCLogTextObject::SetCursorPos(int32_t pos) -> void
{
    CursorPos = pos;
    // The cursor sits one pixel after the text up to it.
    std::string_view before = std::string_view(Buffer).substr(0, static_cast<size_t>(std::max(pos, 0)));
    before = before.substr(0, before.find('\0'));
    CursorPixel = Font->Width(before) + 1;
}

auto MCLogTextObject::IsValid(char key) const -> bool
{
    switch (AllowedInput)
    {
        case MCLogInputType::Text:
            return key >= ' ' && key < 0x7f;
        case MCLogInputType::Digits:
            return key >= '0' && key <= '9';
        case MCLogInputType::Any:
            return true;
        case MCLogInputType::Port:
            return key >= '2' && key <= '6';
        default:
            return false;
    }
}

auto MCLogTextObject::HandleEvent(MCGuiEvent& event) -> void
{
    // Tells the parent the entry is finished (Enter, or a double click).
    auto sendDone = [this]()
    {
        MCGuiEvent done;
        done.Clear();
        done.Type = MCGuiEventType::Focus;
        done.Data = MCLogNotice::EntryDone;
        ParentObject()->HandleEvent(done);
    };

    // Clears the text (what lies behind it stays).
    auto clearText = [this]()
    {
        std::fill_n(Buffer.begin(), TextLength, '\0');
        TextLength = 0;
    };

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
            GuiSystem().SetText(weak_from_this());
            break;
        case MCGuiEventType::Character:
        {
            if (AllowedInput == MCLogInputType::None)
            {
                break;
            }

            const uint8_t key = event.Key;

            if (key == 8)
            {
                if (TextLength != 0)
                {
                    // Backspace on untouched text clears all of it.
                    int32_t newPos = 0;

                    if (Text() == OriginalText)
                    {
                        clearText();
                    }
                    else
                    {
                        Buffer[static_cast<size_t>(TextLength - 1)] = '\0';
                        newPos = CursorPos - 1;
                        TextLength--;
                    }

                    SetCursorPos(newPos);
                    RestartBlink();
                }
            }
            else if (key == 0x0d)
            {
                sendDone();
            }
            else if (key == 0x1b)
            {
                Cancel();
            }
            else if (TextLength + 1 < BufferSize && IsValid(static_cast<char>(key)))
            {
                // Original behaviour (OB-161): the text isn't ended after the character, so what a longer text left
                // behind the old end shows again.
                Buffer[static_cast<size_t>(TextLength++)] = static_cast<char>(key);
                SetCursorPos(CursorPos + 1);
                RestartBlink();
            }
            break;
        }

        case MCGuiEventType::LeftDoubleClick:
            sendDone();
            break;
        case MCGuiEventType::Timer:
        {
            if (event.Data == BlinkTimer)
            {
                CursorLit = !CursorLit;
            }
            break;
        }
        case MCGuiEventType::Focus:
        {
            if (event.Data == MCLogNotice::FocusGained)
            {
                // Focus: start the blink, and clear an empty-slot name so the player can type one.
                GuiSystem().AddTimer(weak_from_this(), BlinkTimer, static_cast<int32_t>(MCPort::CaretBlinkTime()), 0, 0,
                                     false);

                if (ClearEmptyOnFocus && Text() == EmptyFile)
                {
                    clearText();
                    SetCursorPos(0);
                    RestartBlink();
                }
            }
            else if (event.Data == MCLogNotice::FocusLost)
            {
                GuiSystem().RemoveTimer(*this, BlinkTimer);
                CursorLit = true;
            }
            break;
        }
        default:
            break;
    }

    MCGuiObject::HandleEvent(event);
}

auto MCLogTextObject::InitBuffer(int32_t size, MCLogInputType type) -> void
{
    BufferSize = std::max(size, 0);
    Buffer.assign(static_cast<size_t>(BufferSize), '\0');
    OriginalText.clear();
    AllowedInput = type;
}

auto MCLogTextObject::SetStringBuffer(std::string_view text) -> int32_t
{
    text = text.substr(0, text.find('\0'));
    int32_t result = 0;
    auto length = static_cast<int32_t>(text.size());

    if (BufferSize == 0)
    {
        // No buffer to set (the original wrote through a null one).
        return -1;
    }

    if (length < BufferSize)
    {
        std::ranges::copy(text, Buffer.begin());
        Buffer[text.size()] = '\0';
        TextLength = length;
    }
    else
    {
        // Too long: cut to the field (no NUL is written; the buffer's last character stays one). The length is left as
        // it was.
        length = BufferSize - 1;
        text = text.substr(0, static_cast<size_t>(length));
        std::ranges::copy(text, Buffer.begin());
        result = -1;
    }

    OriginalText = text;
    SetCursorPos(length);
    RestartBlink();
    return result;
}
