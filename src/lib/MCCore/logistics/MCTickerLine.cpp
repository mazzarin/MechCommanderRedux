#include "stdafx.h"
#include "logistics/MCTickerLine.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "logistics/MCTicker.h"

namespace
{
    /// <summary>How often the scroll timer fires, in milliseconds.</summary>
    constexpr int32_t ScrollInterval = 0x4b;
}

auto MCTickerLine::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::weak_ptr<MCTicker> ticker)
    -> void
{
    _Ticker = std::move(ticker);
    InitWithoutPort(xPos, yPos, width, height);
    XPos = xPos;
    YPos = yPos;
    ShowGuiWindow(false);
}

auto MCTickerLine::StartScrolling() -> void
{
    // Original behaviour (OB-072): the timer's id is 4, but the ticker only scrolls on timer 7.
    GuiSystem().AddTimer(weak_from_this(), MCTicker::ScrollTimer, ScrollInterval, 0, 0, false);
}

auto MCTickerLine::Show(bool show, bool shuffle) -> void
{
    // In front, as the original's ticker was when added to the screen (logistics' start then raised it again).
    if (show)
    {
        BringToFront(true);

        if (shuffle)
        {
            BringToFront(false);
        }
    }

    ShowGuiWindow(show);
}

auto MCTickerLine::HandleEvent(MCGuiEvent& event) -> void
{
    if (const std::shared_ptr<MCTicker> ticker = _Ticker.lock(); ticker != nullptr)
    {
        ticker->Advance(event.Type == MCGuiEventType::Timer && event.Data == MCTicker::ScrollStepTimer);
    }
}

auto MCTickerLine::SetBackPane(MCGuiPort& port) -> void
{
    BackPane = std::make_shared<MCLogPort>();
    BackPane->Init(port.Width(), port.Height());
    port.CopyTo(BackPane->Frame(), 0, 0, true);
}

auto MCTickerLine::DrawLine(const MCPane& target) const -> void
{
    const std::shared_ptr<MCTicker> ticker = _Ticker.lock();

    if (ticker == nullptr)
    {
        return;
    }

    const int32_t x = XPos;
    const int32_t y = YPos;

    if (BackPane != nullptr)
    {
        BackPane->CopyTo(target, x, y, true);
    }

    const int32_t maxWidth = ticker->MaxWidth;
    const int32_t textWidth = ticker->TextWidth;
    const bool wide = maxWidth < textWidth;

    if (textWidth == 0 || (wide && !ticker->ScrollShown))
    {
        return;
    }

    // The text picture: the text (after half a line of gap when it is wide) in a picture TextWidth wide and a line
    // high, keyed.
    const int32_t textX = wide ? maxWidth / 2 : 0;
    const int32_t lineHeight = MedWhiteFont->Height();
    auto writeClipped = [&](int32_t left, int32_t right, int32_t at)
    {
        MCPane clip = target;
        clip.X0 = target.X0 + left;
        clip.Y0 = target.Y0 + y;
        clip.X1 = target.X0 + right;
        clip.Y1 = target.Y0 + y + lineHeight - 1;

        if (clip.X0 <= clip.X1)
        {
            MedWhiteFont->WriteString(clip, at - left, 0, ticker->Text, -1);
        }
    };

    if (!wide)
    {
        writeClipped(x, x + textWidth - 1, x + textX);
        return;
    }

    // Scrolled: the line shows the picture from ScrollPos, and from its start again after its end.
    const int32_t scrollPos = ticker->ScrollPos;
    const int32_t windowRight = x + maxWidth - 1;
    const int32_t pictureEnd = x + textWidth - scrollPos;
    writeClipped(x, std::min(windowRight, pictureEnd - 1), x - scrollPos + textX);

    if (textWidth < scrollPos + maxWidth)
    {
        writeClipped(pictureEnd, windowRight, pictureEnd + textX);
    }
}
