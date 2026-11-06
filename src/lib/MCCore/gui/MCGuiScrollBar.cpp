#include "stdafx.h"
#include "gui/MCGuiScrollBar.h"
#include "gui/MCGuiSystem.h"
#include "platform/MCInput.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The first repeat timer (1 s), replaced by the fast one (200 ms) once it fires.</summary>
    constexpr int32_t RepeatDelayTimer = 4;
    constexpr int32_t RepeatTimer = 5;

    /// <summary>
    /// Makes an arrow or area of a bar on <paramref name="bar"/>: placed, posting <paramref name="message"/>, moved to
    /// (<paramref name="xPos"/>, <paramref name="yPos"/>) before (<paramref name="moveFirst"/>) or after it is added.
    /// </summary>
    template <typename TPart>
    std::expected<MCGuiHandle<TPart>, int32_t> MakePart(MCGuiScrollBar& bar, int32_t height, int32_t artPacket,
                                                        int32_t message, int32_t xPos, int32_t yPos, bool moveFirst)
    {
        auto part = bar.TryCreateChild<TPart>(
            [&](TPart& made)
            {
                if (const int32_t result = made.Init(0, 0, 0x10, height, {}); result != 0)
                {
                    return result;
                }

                if (artPacket != 0)
                {
                    if (const int32_t result = made.SetBackground(artPacket); result != 0)
                    {
                        return result;
                    }
                }

                made.Message = message;
                made.SetEventRoutine(ScrollEventHandler);

                if (moveFirst)
                {
                    made.MoveTo(xPos, yPos);
                }

                return 0;
            });

        if (part && !moveFirst)
        {
            (*part)->MoveTo(xPos, yPos);
        }

        return part;
    }
}

auto ScrollEventHandler(MCGuiObject& obj, MCGuiEvent& event) -> void
{
    const int32_t message = static_cast<MCGuiScrollButton&>(obj).Message;

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            GuiSystem().Grab(obj.weak_from_this());
            GuiSystem().AddTimer(obj.weak_from_this(), RepeatDelayTimer, 1000, 0, 0, false);
            APostMessage(obj.Parent, message);
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            GuiSystem().Release();
            GuiSystem().RemoveTimer(obj, RepeatDelayTimer);
            GuiSystem().RemoveTimer(obj, RepeatTimer);
            break;
        }
        case MCGuiEventType::Timer:
        {
            if (event.Data == RepeatDelayTimer)
            {
                GuiSystem().RemoveTimer(obj, RepeatDelayTimer);
                GuiSystem().AddTimer(obj.weak_from_this(), RepeatTimer, 200, 0, 0, false);
            }

            APostMessage(obj.Parent, message);
            break;
        }
        default:
            break;
    }
}

auto ScrollTabHandler(MCGuiObject& obj, MCGuiEvent& event) -> void
{
    const auto bar = std::static_pointer_cast<MCGuiScrollBar>(obj.ParentObject());

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            // Centre the cursor on the thumb, then drag it.
            MCInput::SetCursorPos(obj.Width() / 2 + obj.GlobalX(), obj.Height() / 2 + obj.GlobalY());
            GuiSystem().Grab(obj.weak_from_this());
            break;
        }
        case MCGuiEventType::LeftButtonUp:
            GuiSystem().Release();
            break;
        case MCGuiEventType::MouseMove:
        {
            if (GuiSystem().Grabs(obj))
            {
                const int32_t offset = event.Y - bar->GlobalY();
                const int32_t steps = (bar->ScrollMax * offset) / (bar->Height() - 0x12);
                const auto newPos = static_cast<int32_t>(static_cast<float>(static_cast<double>(steps) + 0.5));

                if (newPos != bar->ScrollPos)
                {
                    MCGuiEvent dragEvent;
                    dragEvent.Clear();
                    dragEvent.Type = MCGuiScrollMessage::ThumbDragged;
                    dragEvent.LParam = newPos;
                    bar->HandleEvent(dragEvent);
                }
            }
            break;
        }
        default:
            break;
    }
}

auto ScrollTabPaint(MCGuiObject& obj) -> void
{
    const int32_t width = obj.Width();
    const int32_t height = obj.Height();
    MCPane& pane = obj.Port().Frame();
    VfxPaneWipe(pane, 0x39);

    // A black outline, a light bevel top and left, a dark one bottom and right.
    VfxLineDraw(pane, 0, height - 1, width, height - 1, 0);
    VfxLineDraw(pane, 0, 0, width, 0, 0);
    VfxLineDraw(pane, 0, 0, 0, height - 1, 0);
    VfxLineDraw(pane, width - 1, 0, width - 1, height - 1, 0);
    VfxLineDraw(pane, 1, 1, width - 2, 1, 0x3d);
    VfxLineDraw(pane, 2, 2, width - 3, 2, 0x3b);
    VfxLineDraw(pane, 1, 1, 1, height - 2, 0x3d);
    VfxLineDraw(pane, 2, 2, 2, height - 3, 0x3b);
    VfxLineDraw(pane, width - 2, 1, width - 2, height - 2, 0x35);
    VfxLineDraw(pane, width - 3, 2, width - 3, height - 3, 0x37);
    VfxLineDraw(pane, 1, height - 2, width - 2, height - 2, 0x35);
    VfxLineDraw(pane, 2, height - 3, width - 3, height - 3, 0x37);
}

// MCGuiScrollBar

auto MCGuiScrollBar::Init(int32_t xPos, int32_t yPos, int32_t, int32_t height, std::string_view name) -> int32_t
{
    if (const int32_t result = MCGuiObject::Init(xPos, yPos, 0x12, height, name); result != 0)
    {
        return result;
    }

    auto up = MakePart<MCGuiScrollButton>(*this, 8, 0x27, MCGuiScrollMessage::LineUp, 1, 1, false);

    if (!up)
    {
        return up.error();
    }

    UpButton = *up;
    auto down = MakePart<MCGuiScrollButton>(*this, 8, 0x28, MCGuiScrollMessage::LineDown, 1, height - 9, false);

    if (!down)
    {
        return down.error();
    }

    DownButton = *down;
    auto upArea = MakePart<MCGuiScrollArea>(*this, 1, 0, MCGuiScrollMessage::PageUp, 1, 9, false);

    if (!upArea)
    {
        return upArea.error();
    }

    UpArea = *upArea;
    auto downArea = MakePart<MCGuiScrollArea>(*this, 1, 0, MCGuiScrollMessage::PageDown, 1, Height() - 1, true);

    if (!downArea)
    {
        return downArea.error();
    }

    DownArea = *downArea;
    auto tab = TryCreateChild<MCGuiObject>(
        [](MCGuiObject& made)
        {
            made.SetDrawsLive();
            return made.Init(0, 0, 0x10, 0x10, {});
        });

    if (!tab)
    {
        return tab.error();
    }

    ScrollTab = *tab;
    ScrollTab->MoveTo(1, 8);
    ScrollTab->SetBackColor(6);
    ScrollTab->SetEventRoutine(ScrollTabHandler);
    ScrollTab->SetPaintRoutine(ScrollTabPaint);

    SetBackColor(0xb);
    MoveTo(xPos, yPos);
    SetDepth(10);
    UpArea->SetDepth(0xb);
    DownArea->SetDepth(0xb);
    UpButton->SetDepth(0xb);
    DownButton->SetDepth(0xb);
    ScrollTab->SetDepth(-0xc);
    SetScrollMax(0);
    SetScrollPos(0);
    return 0;
}

auto MCGuiScrollBar::HandleEvent(MCGuiEvent& event) -> void
{
    int32_t newPos = 0;

    switch (event.Type)
    {
        case MCGuiScrollMessage::LineUp:
            newPos = ScrollPos - 1;
            break;
        case MCGuiScrollMessage::LineDown:
            newPos = ScrollPos + 1;
            break;
        case MCGuiScrollMessage::PageUp:
            newPos = ScrollPos - 10;
            break;
        case MCGuiScrollMessage::PageDown:
            newPos = ScrollPos + 10;
            break;
        case MCGuiScrollMessage::ThumbDragged:
            newPos = event.LParam;
            break;
        case MCGuiScrollMessage::SetPositionQuietly:
        {
            SetScrollPos(event.LParam);
            return;
        }
        default:
            return;
    }

    SetScrollPos(newPos);

    if (!Parent.expired())
    {
        APostMessage(Parent, MCGuiScrollMessage::Changed);
    }
}

auto MCGuiScrollBar::Draw() -> void
{
    MCPane& pane = DisplayPort->Frame();
    VfxPaneWipe(pane, BackgroundColor);
    VfxLineDraw(pane, 0, 0, Width() - 1, 0, 0xf);
    VfxLineDraw(pane, Width() - 1, 0, Width() - 1, Height() - 1, 0xf);
    VfxLineDraw(pane, 0, Height() - 1, Width() - 1, Height() - 1, 0xf);
    VfxLineDraw(pane, 0, 0, 0, Height() - 1, 0xf);
    MCGuiObject::Draw();
}

auto MCGuiScrollBar::SetScrollMax(int32_t newMax) -> void
{
    ScrollMax = newMax;
    ScrollTab->ShowGuiWindow(newMax != 0);
    ResizeAreas();
}

auto MCGuiScrollBar::SetScrollPos(int32_t newPos) -> void
{
    if (newPos < 0)
    {
        newPos = 0;
    }

    if (newPos > ScrollMax)
    {
        newPos = ScrollMax;
    }

    ScrollPos = newPos;
    ResizeAreas();
}

auto MCGuiScrollBar::ResizeAreas() -> void
{
    if (ScrollMax == 0)
    {
        return;
    }

    // The thumb (16 high) travels between the arrows (9 each) and a 1-pixel border.
    const int32_t tabTop = (ScrollPos * (Height() - 0x22)) / ScrollMax;
    ScrollTab->MoveTo(1, tabTop + 9);
    UpArea->Resize(0x10, tabTop);
    DownArea->Resize(0x10, Height() + (-0x11 - (tabTop + 0x10)));
    DownArea->MoveTo(1, tabTop + 0x19);
}

// MCGuiScrollArea

auto MCGuiScrollArea::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view) -> int32_t
{
    WinWidth = width;
    WinY = yPos;
    WinHeight = height;
    WinX = xPos;
    WinState = MCGuiWindowState::Normal;
    ShowWindow = true;
    DragOn = false;
    DisplayPort.reset();
    FramePane = std::make_shared<MCPane>();
    FramePane->Window = ScreenPort()->SharedBitmap();
    FramePane->X0 = xPos;
    FramePane->Y0 = yPos;
    FramePane->X1 = xPos + width;
    FramePane->Y1 = yPos + height;
    PaintRoutine = nullptr;
    EventRoutine = nullptr;
    ChildList.clear();
    Parent.reset();
    WinDepth = 0;
    WindowAnimation.reset();
    Animating = false;
    IconAnimation.reset();
    return 0;
}

auto MCGuiScrollArea::HandleEvent(MCGuiEvent& event) -> void
{
    if (EventRoutine != nullptr)
    {
        EventRoutine(*this, event);
    }
}

auto MCGuiScrollArea::Resize(int32_t newWidth, int32_t newHeight) -> void
{
    WinWidth = newWidth;
    FramePane->X1 = FramePane->X0 - 1 + newWidth;
    WinHeight = newHeight;
    FramePane->Y1 = FramePane->Y0 - 1 + newHeight;
}
