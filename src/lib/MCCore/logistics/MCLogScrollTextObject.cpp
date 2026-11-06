#include "stdafx.h"
#include "logistics/MCLogScrollTextObject.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// The row of <c>Fonts</c> a line is drawn in, from its colour byte (0x0b..0xf2 are the palette indices of the
    /// font colours; anything else is row 7).
    /// </summary>
    int32_t FontRowForColor(uint8_t color)
    {
        switch (color)
        {
            case 0x0b:
                return 3;
            case 0x0c:
                return 4;
            case 0x10:
                return 0;
            case 0x19:
                return 5;
            case 0x1f:
                return 6;
            case 0xef:
                return 1;
            case 0xf2:
                return 2;
            default:
                return 7;
        }
    }

    /// <summary>The space above and below the scroll tab's track (the list's arrows in the art).</summary>
    constexpr int32_t TrackEnd = 0xf;
}

// The scroll tab

auto LogPaintScrollTab(MCGuiObject& tab) -> void
{
    const int32_t width = tab.Width();
    const int32_t height = tab.Height();
    MCPane& pane = static_cast<MCLogObject&>(tab).Lport()->Frame();
    VfxPaneWipe(pane, 0x1a);
    VfxLineDraw(pane, 0, 0, width - 2, 0, 0x1f);
    VfxLineDraw(pane, 0, 0, 0, height - 2, 0x1f);
    VfxLineDraw(pane, width - 1, 0, width - 1, height - 1, 0x16);
    VfxLineDraw(pane, 0, height - 1, width - 1, height - 1, 0x16);
}

auto LogScrollTabHandleEvent(MCGuiObject& tab, MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            GuiSystem().Grab(tab.weak_from_this());
            tab.StartDrag(0, event.Y - tab.GlobalY());
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            GuiSystem().Release();
            tab.StopDrag();
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            if (GuiSystem().GrabbedObject() == nullptr)
            {
                break;
            }

            // The original is the GUI scroll text's handler compiled for this thumb: it reads the parent's thumb at
            // that class's offset and calls its CalcFirstPixel, which writes over the port pointer (OB-073). The port
            // uses the list's own thumb and CalcFirstPixel.
            const auto list = std::static_pointer_cast<MCLogScrollTextObject>(tab.ParentObject());
            const int32_t lowest = (-0x10 - list->ScrollTab->Height()) + list->Height();
            tab.MoveTo(tab.X(), (event.Y - tab.ParentObject()->Y()) - tab.DragStartY(), false);

            if (tab.Y() < TrackEnd)
            {
                tab.MoveTo(tab.X(), TrackEnd, false);
            }

            if (tab.Y() > lowest)
            {
                tab.MoveTo(tab.X(), lowest, false);
            }

            list->CalcFirstPixel(tab.Y() - TrackEnd);
            break;
        }

        default:
            break;
    }
}

// MCLogScrollTextObject

auto MCLogScrollTextObject::LineHeight() const -> int32_t
{
    return Fonts[0][FontIndex]->Height() + 4;
}

auto MCLogScrollTextObject::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view newText)
    -> int32_t
{
    int32_t result = MCLogObject::Init(xPos, yPos, width, height);

    if (result != 0)
    {
        return result;
    }

    auto made = TryCreateChild<MCLogObject>(
        [&](MCLogObject& tab)
        {
            if (const int32_t tabResult = tab.Init(0, 0, 9, height - 0x1e); tabResult != 0)
            {
                return tabResult;
            }

            tab.MoveTo(Width() + 2, TrackEnd, false);
            tab.SetDepth(100);
            return 0;
        });

    if (!made)
    {
        return made.error();
    }

    ScrollTab = *made;
    MCLogObject& tab = *ScrollTab;
    tab.ShowGuiWindow(true);
    tab.SetEventRoutine(LogScrollTabHandleEvent);
    tab.SetPaintRoutine(LogPaintScrollTab);
    tab.SetDrawsLive();
    tab.SetDepth(1);

    Text.clear();
    FontIndex = 0;
    NumLines = 0;
    FirstPixel = 0;
    Scrolling = true;
    HighlightLine.fill(-1);
    HighlightColor.fill(0xff);

    // Text given at init prints in colour 0x1f; without it the list doesn't scroll.
    if (!newText.empty())
    {
        Print(newText, 0x1f);
    }
    else
    {
        Scrolling = false;
    }

    TabColumn = -1;
    PositionScrollTab();
    return 0;
}

auto MCLogScrollTextObject::OnDestroy() -> void
{
    Text.clear();
    MCLogObject::OnDestroy();
}

auto MCLogScrollTextObject::Draw() -> void
{
    int32_t lineY = 2;
    const int32_t lineHeight = LineHeight();
    VfxPaneWipe(Lport()->Frame(), 0x10);

    // Highlighted lines.
    for (int32_t i = 0; i < HighlightCount; i++)
    {
        const int32_t start = HighlightLine[i];

        if (start != -1)
        {
            MCPane box = Lport()->Frame();
            box.X0 = 0;
            box.Y0 = start * lineHeight;
            box.X1 = Width();
            box.Y1 = (start + 1) * lineHeight;
            VfxPaneWipe(box, HighlightColor[i]);
        }
    }

    // Lines are (colour byte, text, '\n'), up to a NUL. The first tab from the line on (it may be a later line's)
    // becomes a space when the list has no second column; otherwise it ends the first piece, and the newline that
    // ends that piece becomes a tab again, the second piece starting at TabColumn: as the original did to its buffer.
    const size_t end = std::min(Text.find('\0'), Text.size());
    auto find = [&](char c, size_t from) { return std::min(Text.find(c, from), end); };
    size_t line = 0;

    while (line < end)
    {
        const auto color = static_cast<uint8_t>(Text[line]);
        line++;
        int32_t pieces = 0;
        int32_t lineX = 2;

        if (const size_t tab = find('\t', line); tab < end)
        {
            if (TabColumn < 0)
            {
                Text[tab] = ' ';
            }
            else
            {
                Text[tab] = '\n';
                pieces = 1;
            }
        }

        std::shared_ptr<MCGuiFont> font = Fonts[FontRowForColor(color)][FontIndex];
        bool more = true;

        do
        {
            font->WriteStringToNewline(Lport()->Frame(), lineX, lineY, std::string_view(Text).substr(line, end - line));
            const size_t newline = find('\n', line);
            more = newline < end;

            if (pieces > 0)
            {
                if (more)
                {
                    Text[newline] = '\t';
                }

                lineX = TabColumn;
            }

            pieces--;
            line = more ? newline + 1 : end;
        } while (pieces >= 0 && more);

        if (!more)
        {
            break;
        }

        lineY += lineHeight;
    }

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].Lock(); child != nullptr)
        {
            DrawChild(*child);
        }
    }
}

auto MCLogScrollTextObject::Display() -> void
{
    if (!ShowWindow)
    {
        return;
    }

    if (IsHidden() != 0 && HideOffset == 0)
    {
        return;
    }

    // The lines scrolled by FirstPixel (a list that doesn't scroll shows its top), then the children.
    DrawInFramePass(*Lport(), Scrolling ? FirstPixel : 0);
}

auto MCLogScrollTextObject::Resize(int32_t width, int32_t height) -> void
{
    const int32_t fontHeight = Fonts[0][FontIndex]->Height();

    if (width <= 0 || height <= 0 || (width == WinWidth && height == WinHeight))
    {
        return;
    }

    WinWidth = width;
    WinHeight = height;
    FramePane->X1 = FramePane->X0 - 1 + width;
    FramePane->Y1 = FramePane->Y0 - 1 + height;
    // A scrolling list's port holds all its lines.
    int32_t portHeight = height;

    if (Scrolling)
    {
        const int32_t textHeight = NumLines * (fontHeight + 4);

        if (textHeight > height)
        {
            portHeight = textHeight;
        }
    }

    Lport()->Resize(width, portHeight);
    ScrollTab->MoveTo(width + 2, ScrollTab->Y(), false);
    PositionScrollTab();
}

auto MCLogScrollTextObject::ResetPortSize() -> void
{
    if (!Scrolling)
    {
        return;
    }

    // The port only grows.
    const int32_t portHeight = std::max(LineHeight() * NumLines, Lport()->Height());
    Lport()->Resize(Lport()->Width(), portHeight);
}

auto MCLogScrollTextObject::Print(std::string_view line, uint8_t color) -> void
{
    line = line.substr(0, line.find('\0'));
    Text += static_cast<char>(color);
    Text += line;
    Text += '\n';
    NumLines++;
    const int32_t needed = NumLines * LineHeight();

    if (Scrolling && needed > Lport()->Height())
    {
        Lport()->Resize(Lport()->Width(), needed);
    }

    PositionScrollTab();
}

auto MCLogScrollTextObject::PrintBlank(uint8_t color) -> void
{
    Text += static_cast<char>(color);
    Text += '\n';
    NumLines++;
}

auto MCLogScrollTextObject::PrintWrapped(std::string_view text, uint8_t color, int32_t wrapWidth) -> void
{
    text = text.substr(0, text.find('\0'));

    if (wrapWidth == -1)
    {
        wrapWidth = Width();
    }

    for (;;)
    {
        std::shared_ptr<MCGuiFont> font = Fonts[0][FontIndex];
        std::string_view piece = text;
        size_t split = std::string_view::npos;

        if (font->Width(text) > wrapWidth - 6)
        {
            // Cut at the last space, then earlier spaces until the piece fits; a first word too wide takes the rest.
            split = text.rfind(' ');

            if (split != std::string_view::npos)
            {
                piece = text.substr(0, split);

                while (font->Width(piece) > wrapWidth - 6)
                {
                    split = piece.rfind(' ');

                    if (split == std::string_view::npos)
                    {
                        piece = text;
                        break;
                    }

                    piece = piece.substr(0, split);
                }
            }
        }

        Print(piece, color);

        if (split == std::string_view::npos)
        {
            return;
        }

        text = text.substr(split + 1);
    }
}

auto MCLogScrollTextObject::Clear() -> void
{
    FirstPixel = 0;
    NumLines = 0;
    Text.clear();
    HighlightLine.fill(-1);
}

auto MCLogScrollTextObject::CalcFirstPixel(int32_t tabPos) -> void
{
    const int32_t track = (-0x1e - ScrollTab->Height()) + Height();
    const int32_t range = Lport()->Height() - Height();

    if (Scrolling && track > 0 && range > 0)
    {
        FirstPixel = (range * tabPos) / track;
        return;
    }

    FirstPixel = 0;
}

auto MCLogScrollTextObject::PositionScrollTab() -> void
{
    if (GuiSystem().GrabbedObject().get() == ScrollTab.Lock().get())
    {
        return;
    }

    const int32_t track = Height() - 0x1e;
    const int32_t range = Lport()->Height() - Height();

    if (range == 0)
    {
        ScrollTab->ShowGuiWindow(false);
        return;
    }

    // The thumb's length is the visible fraction of the track (x87: float quotient, then times the track).
    const auto shown = static_cast<float>(Height());
    int32_t tabLength = static_cast<int32_t>(static_cast<double>(shown) / Lport()->Height() * track);

    if (tabLength < 3)
    {
        tabLength = 3;
    }

    ScrollTab->ShowGuiWindow(true);
    ScrollTab->Resize(ScrollTab->Width(), tabLength);
    ScrollTab->MoveTo(ScrollTab->X(), ((track - tabLength) * FirstPixel) / range + TrackEnd, false);
}

auto MCLogScrollTextObject::ReceiveClick(int32_t direction, int32_t yPos) -> void
{
    if (Height() == Lport()->Height())
    {
        return;
    }

    const int32_t lineHeight = LineHeight();
    auto clampToEnd = [this]()
    {
        if (FirstPixel > Lport()->Height() - Height())
        {
            FirstPixel = Lport()->Height() - Height();
        }
    };

    if (direction == -1)
    {
        FirstPixel = std::max(FirstPixel - lineHeight, 0);
    }
    else if (direction == 0)
    {
        // A page up isn't clamped at the top (unlike the GUI scroll text's).
        if (yPos < ScrollTab->Y())
        {
            FirstPixel -= Height();
        }

        if (yPos > ScrollTab->Bottom())
        {
            FirstPixel += Height();
            clampToEnd();
        }
    }
    else if (direction == 1)
    {
        FirstPixel += lineHeight;
        clampToEnd();
    }

    PositionScrollTab();
}

auto MCLogScrollTextObject::MouseWheel(int32_t steps, [[maybe_unused]] int32_t xPos, [[maybe_unused]] int32_t yPos)
    -> bool
{
    if (Height() == Lport()->Height())
    {
        return false;
    }

    for (; steps != 0; steps += steps < 0 ? 1 : -1)
    {
        ReceiveClick(steps < 0 ? -1 : 1, 0);
    }

    return true;
}

auto MCLogScrollTextObject::GetTextLine(int32_t line) const -> std::optional<std::string>
{
    if (line < 0)
    {
        return std::nullopt;
    }

    const std::string_view text = std::string_view(Text).substr(0, Text.find('\0'));
    size_t p = 0;

    // Lines 0 and 1 are both the first line.
    for (int32_t remaining = line - 1;; remaining--)
    {
        if (p >= text.size())
        {
            return std::nullopt;
        }

        if (remaining <= 0)
        {
            const std::string_view rest = text.substr(p + 1);
            return std::string(rest.substr(0, rest.find('\n')));
        }

        const size_t newline = text.find('\n', p + 1);

        if (newline == std::string_view::npos)
        {
            return std::nullopt;
        }

        p = newline + 1;
    }
}
