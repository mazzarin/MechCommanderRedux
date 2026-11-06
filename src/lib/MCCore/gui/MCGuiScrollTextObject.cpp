#include "stdafx.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The gap the section highlight leaves at the right.</summary>
    constexpr int32_t HighlightRightGap = 3;

    /// <summary>The thumb's track: the object's height less 16 pixels above and below.</summary>
    constexpr int32_t TrackInset = 0x10;

    /// <summary>The font row (the <c>Fonts</c> table's first index) a line of colour <paramref name="color"/> is drawn in.</summary>
    int32_t FontRowForColor(uint8_t color)
    {
        switch (color)
        {
            case 0x10:
                return 0;
            case 0xef:
                return 1;
            case 0xf2:
                return 2;
            case 0x0b:
                return 3;
            case 0x0c:
                return 4;
            case 0x19:
                return 5;
            case 0x1f:
                return 6;
            default:
                return 7;
        }
    }
}

auto PaintScrollTab(MCGuiObject& obj) -> void
{
    const int32_t width = obj.Width();
    const int32_t height = obj.Height();
    MCPane& pane = obj.Port().Frame();
    VfxPaneWipe(pane, 0x1a);
    VfxLineDraw(pane, 0, 0, width - 2, 0, 0x1f);
    VfxLineDraw(pane, 0, 0, 0, height - 2, 0x1f);
    VfxLineDraw(pane, width - 1, 0, width - 1, height - 1, 0x16);
    VfxLineDraw(pane, 0, height - 1, width - 1, height - 1, 0x16);
}

auto ScrollTabEventHandler(MCGuiObject& obj, MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            GuiSystem().Grab(obj.weak_from_this());
            obj.StartDrag(0, event.Y - obj.GlobalY());
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            GuiSystem().Release();
            obj.StopDrag();
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            // Any grab will do (the original doesn't check that it is this thumb).
            if (GuiSystem().GrabbedObject() == nullptr)
            {
                break;
            }

            const auto text = std::static_pointer_cast<MCGuiScrollTextObject>(obj.ParentObject());
            const int32_t lowest = text->Height() - TrackInset - text->ScrollTab->Height();
            obj.MoveTo(obj.X(), (event.Y - text->Y()) - obj.DragStartY());

            if (obj.Y() < TrackInset)
            {
                obj.MoveTo(obj.X(), TrackInset);
            }

            if (obj.Y() > lowest)
            {
                obj.MoveTo(obj.X(), lowest);
            }

            text->CalcFirstPixel(obj.Y() - TrackInset);
            break;
        }
        default:
            break;
    }
}

auto MCGuiScrollTextObject::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text)
    -> int32_t
{
    // The base's init, with a scroll port for the port.
    const bool live = DrawsLive();
    InitFailed = false;
    Transparent = false;
    Place(xPos, yPos, width, height);
    DisplayPort = std::make_unique<MCGuiScrollPort>();

    if (const int32_t result = live ? DisplayPort->InitView(width, height) : DisplayPort->Init(width, height);
        result != 0)
    {
        InitFailed = true;
        return result;
    }

    auto tab = TryCreateChild<MCGuiObject>(
        [&](MCGuiObject& made)
        {
            made.SetDrawsLive();

            if (const int32_t result = made.Init(0, 0, 9, height - 2 * TrackInset, {}); result != 0)
            {
                return result;
            }

            made.MoveTo(Width() + 2, TrackInset);
            made.SetDepth(100);
            return 0;
        });

    if (!tab)
    {
        InitFailed = true;
        return tab.error();
    }

    ScrollTab = *tab;
    ScrollTab->ShowGuiWindow(true);
    ScrollTab->SetEventRoutine(ScrollTabEventHandler);
    ScrollTab->SetPaintRoutine(PaintScrollTab);
    ScrollTab->SetDepth(1);

    TextBuffer.clear();
    NumLines = 0;
    FirstPixel = 0;
    FontIndex = 0;
    SectionStarts.fill(-1);
    SectionColors.fill(0xff);

    if (!text.empty())
    {
        Print(text, 0x1f);
    }

    return 0;
}

auto MCGuiScrollTextObject::OnDestroy() -> void
{
    TextBuffer = {};
    MCGuiObject::OnDestroy();
}

auto MCGuiScrollTextObject::LineHeight() const -> int32_t
{
    return Fonts[0][FontIndex]->Height() + 2;
}

auto MCGuiScrollTextObject::Draw() -> void
{
    const int32_t lineHeight = LineHeight();
    VfxPaneWipe(Port().Frame(), 0x10);

    // Each section highlights one line.
    for (size_t i = 0; i < NumSections; i++)
    {
        if (const int32_t start = SectionStarts[i]; start != -1)
        {
            FillBox(1, static_cast<int16_t>(lineHeight * start + 1), static_cast<int16_t>(Width() - HighlightRightGap),
                    static_cast<int16_t>((start + 1) * lineHeight - 1), SectionColors[i]);
        }
    }

    // Lines are (colour byte, text, '\n'). A tab splits a line into two pieces, the second drawn at TabStop. (The
    // tab is looked for in the rest of the text, as the original did, so a line without one takes a later line's.)
    // The text ends at its first null, as the original's C string did.
    const size_t end = std::string_view(TextBuffer.c_str()).size();
    size_t line = 0;
    bool more = true;
    int32_t lineY = 2;

    while (more && line < end)
    {
        const auto color = static_cast<uint8_t>(TextBuffer[line]);
        line++;
        int32_t pieces = 0;
        int32_t lineX = 2;

        if (const size_t tab = TextBuffer.find('\t', line); tab < end)
        {
            if (TabStop < 0)
            {
                TextBuffer[tab] = ' ';
            }
            else
            {
                TextBuffer[tab] = '\n';
                pieces = 1;
            }
        }

        std::shared_ptr<MCGuiFont> font = Fonts[FontRowForColor(color)][FontIndex];

        do
        {
            font->WriteStringToNewline(Port().Frame(), lineX, lineY,
                                       std::string_view(TextBuffer).substr(line, end - line));
            const size_t newline = TextBuffer.find('\n', line);
            more = newline < end;

            if (pieces > 0)
            {
                // Put the tab back.
                if (more)
                {
                    TextBuffer[newline] = '\t';
                }

                lineX = TabStop;
            }

            pieces--;

            if (more)
            {
                line = newline + 1;
            }
        } while (pieces >= 0 && more);

        lineY += lineHeight;
    }

    for (const std::shared_ptr<MCGuiObject>& child : Children())
    {
        if (DrawsChild(*child))
        {
            child->Draw();
        }
    }
}

auto MCGuiScrollTextObject::Display() -> void
{
    if (!ShowWindow || (IsHidden() && HideOffset == 0))
    {
        return;
    }

    // Port: the port is the whole text (taller than the object), drawn each frame scrolled by FirstPixel.
    if (DrawsLive())
    {
        DrawInFramePass(Port(), FirstPixel);
        return;
    }

    if (DisplayPort != nullptr)
    {
        VfxPaneCopy(Port().Frame(), 0, 0, *FramePane.get(), 0, -FirstPixel, -1);
    }

    DisplayChildren();
}

auto MCGuiScrollTextObject::Resize(int32_t newWidth, int32_t newHeight) -> void
{
    if (newWidth <= 0 || newHeight <= 0 || (newWidth == WinWidth && newHeight == WinHeight))
    {
        return;
    }

    WinWidth = newWidth;
    WinHeight = newHeight;
    FramePane->X1 = FramePane->X0 - 1 + newWidth;
    FramePane->Y1 = FramePane->Y0 - 1 + newHeight;
    Port().Resize(newWidth, std::max(NumLines * LineHeight(), newHeight));
    ScrollTab->MoveTo(newWidth + 2, ScrollTab->Y());
    PositionScrollTab();
}

auto MCGuiScrollTextObject::CopyTextFrom(const MCGuiScrollTextObject& other) -> void
{
    FontIndex = other.FontIndex;
    TabStop = other.TabStop;
    TextBuffer = other.TextBuffer;
    NumLines = other.NumLines;
    SectionColors = other.SectionColors;
    SectionStarts = other.SectionStarts;
    FirstPixel = other.FirstPixel;
    Port().Resize(Port().Width(), std::max(LineHeight() * NumLines, other.DisplayPort->Height()));
    PositionScrollTab();
}

auto MCGuiScrollTextObject::ResetPortSize() -> void
{
    // The port only grows.
    Port().Resize(Port().Width(), std::max(LineHeight() * NumLines, Port().Height()));
}

auto MCGuiScrollTextObject::PrintBlank(uint8_t color) -> void
{
    TextBuffer.push_back(static_cast<char>(color));
    TextBuffer.push_back('\n');
    NumLines++;
}

auto MCGuiScrollTextObject::Print(std::string_view line, uint8_t color) -> void
{
    // The line ends at its first NUL, as the original's C string.
    line = line.substr(0, line.find('\0'));
    TextBuffer.push_back(static_cast<char>(color));
    TextBuffer += line;
    TextBuffer.push_back('\n');
    NumLines++;

    if (const int32_t needed = NumLines * LineHeight(); needed > Port().Height())
    {
        if (Port().Resize(Port().Width(), needed) != 0)
        {
            InitFailed = true;
        }
    }

    PositionScrollTab();
}

auto MCGuiScrollTextObject::PrintWrapped(std::string_view line, uint8_t color, int32_t wrapWidth) -> void
{
    if (wrapWidth == -1)
    {
        wrapWidth = Width();
    }

    line = line.substr(0, line.find('\0'));

    while (true)
    {
        std::shared_ptr<MCGuiFont> font = Fonts[0][FontIndex];
        size_t split = std::string_view::npos;

        if (font->Width(line) > wrapWidth - 6)
        {
            // Cut at the last space, then at earlier spaces until the piece fits. When even the first word doesn't
            // fit, the whole rest is printed as one line.
            split = line.rfind(' ');

            while (split != std::string_view::npos && font->Width(line.substr(0, split)) > wrapWidth - 6)
            {
                split = line.substr(0, split).rfind(' ');
            }
        }

        if (split == std::string_view::npos)
        {
            Print(line, color);
            return;
        }

        Print(line.substr(0, split), color);
        line.remove_prefix(split + 1);
    }
}

auto MCGuiScrollTextObject::Clear() -> void
{
    FirstPixel = 0;
    NumLines = 0;
    TextBuffer.clear();
    SectionStarts.fill(-1);
}

auto MCGuiScrollTextObject::CalcFirstPixel(int32_t thumbY) -> void
{
    const int32_t track = Height() - 2 * TrackInset - ScrollTab->Height();
    const int32_t range = Port().Height() - Height();
    FirstPixel = track > 0 && range > 0 ? (range * thumbY) / track : 0;
}

auto MCGuiScrollTextObject::PositionScrollTab() -> void
{
    if (GuiSystem().GrabbedObject().get() == ScrollTab.Lock().get())
    {
        return;
    }

    const int32_t track = Height() - 2 * TrackInset;
    const int32_t range = Port().Height() - Height();

    if (range == 0)
    {
        ScrollTab->ShowGuiWindow(false);
        return;
    }

    // The thumb's length is the visible fraction of the track (x87: float quotient, then times the track).
    const auto shown = static_cast<float>(Height());
    const int32_t tabLength = std::max(static_cast<int32_t>(static_cast<double>(shown) / Port().Height() * track), 3);
    ScrollTab->ShowGuiWindow(true);
    ScrollTab->Resize(ScrollTab->Width(), tabLength);
    ScrollTab->MoveTo(ScrollTab->X(), ((track - tabLength) * FirstPixel) / range + TrackInset);
}

auto MCGuiScrollTextObject::ReceiveClick(int32_t direction, int32_t yPos) -> void
{
    if (Height() == Port().Height())
    {
        return;
    }

    const int32_t lastPixel = Port().Height() - Height();

    if (direction == -1)
    {
        FirstPixel = std::max(FirstPixel - LineHeight(), 0);
    }
    else if (direction == 0)
    {
        if (yPos < ScrollTab->Y())
        {
            FirstPixel = std::max(FirstPixel - Height(), 0);
        }
        else if (yPos > ScrollTab->Bottom())
        {
            FirstPixel = std::min(FirstPixel + Height(), lastPixel);
        }
    }
    else if (direction == 1)
    {
        FirstPixel = std::min(FirstPixel + LineHeight(), lastPixel);
    }

    PositionScrollTab();
}

auto MCGuiScrollTextObject::MouseWheel(int32_t steps, int32_t, int32_t) -> bool
{
    if (Height() == Port().Height())
    {
        return false;
    }

    for (; steps != 0; steps += steps < 0 ? 1 : -1)
    {
        ReceiveClick(steps < 0 ? -1 : 1, 0);
    }

    return true;
}
