#include "stdafx.h"
#include "logistics/MCTicker.h"
#include "gui/MCGuiFont.h"

MCTicker::MCTicker(int32_t width, std::shared_ptr<MCGuiFont> font) : Font(std::move(font)), MaxWidth(width)
{
}

auto MCTicker::Advance(bool scrollStep) -> void
{
    if (TextWidth == 0 || MaxWidth >= TextWidth)
    {
        return;
    }

    // A wide text shows only while it scrolls; every other event painted the back pane alone.
    ScrollShown = scrollStep;

    if (ScrollShown)
    {
        ScrollPos = TextWidth < ScrollPos ? ScrollPos - TextWidth : ScrollPos + 2;
    }
}

auto MCTicker::SetString(std::string_view text) -> void
{
    // A text ends at a NUL, as the C string did.
    text = text.substr(0, text.find('\0'));

    if (text.empty())
    {
        Text.clear();
        TextWidth = 0;
        ScrollPos = 0;
        return;
    }

    if (Text == text)
    {
        return;
    }

    Text = text;
    const int32_t stringWidth = Font->Width(Text);
    TextWidth = stringWidth;

    // A scrolling text gets half a line of gap before it repeats.
    if (MaxWidth < stringWidth)
    {
        TextWidth = MaxWidth / 2 + stringWidth;
    }

    ScrollPos = 0;
    ScrollShown = false;
}
