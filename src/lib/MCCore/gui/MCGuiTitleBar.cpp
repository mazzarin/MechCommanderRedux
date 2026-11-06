#include "stdafx.h"
#include "gui/MCGuiTitleBar.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// Keeps a dragged window's title bar inside the scroll rectangle: when the bar has left it, the window is moved
    /// back against the side it crossed.
    /// </summary>
    void KeepTitleBarInside(MCGuiTitleBar& bar, const tagRECT& area)
    {
        std::shared_ptr<MCGuiObject> window = bar.ParentObject();

        if (bar.RectIntersect(area))
        {
            return;
        }

        if (area.right < bar.Frame().X0)
        {
            window->MoveTo(area.right, window->Frame().Y0);
        }

        if (bar.Frame().X0 + bar.Width() < area.left)
        {
            window->MoveTo(area.left - bar.Width(), window->Frame().Y0);
        }

        if (area.bottom < bar.Frame().Y0)
        {
            window->MoveTo(bar.Frame().X0, area.bottom + 0xd);
        }

        if (bar.Frame().Y0 + bar.Height() < area.top)
        {
            window->MoveTo(bar.Frame().X0, area.top);
        }
    }

    /// <summary>Makes one of the bar's buttons: a 4x4 button with up and down art packets, added to the bar.</summary>
    template <typename TButton>
    std::expected<MCGuiHandle<TButton>, int32_t> MakeBarButton(MCGuiTitleBar& bar, int32_t upArt, int32_t downArt)
    {
        return bar.TryCreateChild<TButton>(
            [&](TButton& made)
            {
                if (const int32_t result = made.Init(0, 0, 4, 4, {}); result != 0)
                {
                    return result;
                }

                made.SetUpPicture(upArt);
                made.SetDownPicture(downArt);
                return 0;
            });
    }
}

auto TitleBarPaint(MCGuiObject& object) -> void
{
    VfxPaneWipe(object.Port().Frame(), 0x1b);
    VfxLineDraw(object.Port().Frame(), 0, 6, object.Width() - 1, 6, 0x10);
    VfxLineDraw(object.Port().Frame(), 0, 7, object.Width() - 1, 7, 0x10);
}

auto HandleSwoopyButtonEvent(MCGuiObject& object, MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::LeftButtonDown)
    {
        GuiSystem().Grab(object.weak_from_this());
        object.Draw();
    }
    else if (event.Type == MCGuiEventType::LeftButtonUp && GuiSystem().Grabs(object) && !object.Parent.expired())
    {
        std::shared_ptr<MCGuiObject> window = object.ParentObject()->ParentObject();

        if (window != nullptr && window->GetCamera() != nullptr)
        {
            window->GetCamera()->Swoopy = !window->GetCamera()->Swoopy;
            GuiSystem().Release();
            object.Draw();
        }
    }
}

// MCGuiTitleButton

auto MCGuiTitleButton::HandleEvent(MCGuiEvent& event) -> void
{
    MCGuiObject::HandleEvent(event);
}

// MCGuiTitleBar

auto MCGuiTitleBar::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    if (const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name); result != 0)
    {
        return result;
    }

    SetBackColor(8);
    Font = BlackFont;
    Title = !name.empty() ? std::string(name) : " ";

    auto close = MakeBarButton<MCGuiCloseButton>(*this, 0x12, 0x13);

    if (!close)
    {
        return close.error();
    }

    CloseButton = std::move(*close);
    CloseButton->MoveTo(WinWidth - CloseButton->Width(), 0);

    auto zoom = MakeBarButton<MCGuiButton>(*this, 0x14, 0x15);

    if (!zoom)
    {
        return zoom.error();
    }

    ZoomButton = std::move(*zoom);
    ZoomButton->MoveTo(0, 0);

    auto zoomOut = MakeBarButton<MCGuiButton>(*this, 0x16, 0x17);

    if (!zoomOut)
    {
        return zoomOut.error();
    }

    ZoomOutButton = std::move(*zoomOut);
    ZoomOutButton->MoveTo(ZoomButton->X() + ZoomButton->Width(), 0);
    ShowZoomButtons(false);

    auto swoopy = MakeBarButton<MCGuiTitleButton>(*this, 0x18, 0x19);

    if (!swoopy)
    {
        return swoopy.error();
    }

    SwoopyButton = std::move(*swoopy);
    SwoopyButton->MoveTo(WinWidth - CloseButton->Width() - SwoopyButton->Width(), 0);
    SwoopyButton->SetEventRoutine(HandleSwoopyButtonEvent);
    ShowSwoopyButton(false);
    return 0;
}

auto MCGuiTitleBar::SetZoomCallbacks() -> void
{
    if (Parent.expired())
    {
        return;
    }

    ZoomButton->Callback().SetMessage(Parent, MCGuiEventType::ZoomIn);
    ZoomOutButton->Callback().SetMessage(Parent, MCGuiEventType::ZoomOut);
}

auto MCGuiTitleBar::SetTitle(std::string_view newTitle) -> void
{
    Title = newTitle;
}

auto MCGuiTitleBar::HandleEvent(MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown: // start dragging the window
        {
            std::shared_ptr<MCGuiObject> window = ParentObject();
            LastX = event.X - window->X();
            LastY = event.Y - window->Y();

            if (!window->Parent.expired() && window->ParentObject()->ForemostChild(window->Depth()) != window)
            {
                window->BringToFront(false);
                ARedrawScreen();
            }

            GuiSystem().Grab(weak_from_this());
            ParentObject()->StartDrag(ParentObject()->X(), ParentObject()->Y());
            ParentObject()->Draw();
            return;
        }

        case MCGuiEventType::LeftButtonUp: // drop the window
        {
            if (!GuiSystem().Grabs(*this))
            {
                return;
            }

            std::shared_ptr<MCGuiObject> window = ParentObject();
            const tagRECT area = GuiSystem().ScrollRect;
            window->StopDrag();

            // The window's own children past its frame (the title bar, three bars and the handle come first).
            for (int32_t i = 9; i < window->NumberOfChildren(); i++)
            {
                window->Child(i)->ShowGuiWindow(true);
            }

            const int32_t mouseX = event.X;
            const int32_t mouseY = event.Y;
            window->MoveTo(mouseX - LastX, mouseY - LastY);
            KeepTitleBarInside(*this, area);
            ParentObject()->Draw();
            GuiSystem().Release();

            if (const std::shared_ptr<MCGuiObject> under = ScreenWindow()->FindObject(mouseX, mouseY); under != nullptr)
            {
                under->Enter();
            }

            return;
        }

        case MCGuiEventType::MouseMove: // drag, unless over a depth-100 (modal) object
        {
            if (!GuiSystem().Grabs(*this))
            {
                return;
            }

            const int32_t mouseX = event.X;
            const int32_t mouseY = event.Y;
            const tagRECT area = GuiSystem().ScrollRect;
            std::shared_ptr<MCGuiObject> under = ScreenWindow()->FindObject(mouseX, mouseY);

            if (under == nullptr || under->Depth() != 100)
            {
                ParentObject()->MoveTo(mouseX - LastX, mouseY - LastY);
                KeepTitleBarInside(*this, area);
            }

            return;
        }

        case MCGuiEventType::Paint:
        {
            Draw();
            return;
        }
    }
}

auto MCGuiTitleBar::Draw() -> void
{
    MCPane& pane = DisplayPort->Frame();
    VfxPaneWipe(pane, BackColor());
    VfxLineDraw(pane, 0, 0, Width(), 0, 0xe);
    VfxLineDraw(pane, 0, 0, 0, 0xd, 0xe);

    if (!Parent.expired())
    {
        VfxLineDraw(pane, 1, Height() - 1, ParentObject()->Width() + 2, Height() - 1, 3);
    }

    int32_t textX = 3;

    if (ZoomButton != nullptr && ZoomButton->IsShowing())
    {
        textX = ZoomButton->X() + ZoomButton->Width() + ZoomOutButton->Width() + 3;
    }

    Font->WriteString(pane, textX, 3, Title);
    MCGuiObject::Draw();
}

auto MCGuiTitleBar::ShowCloseButton(bool show) -> void
{
    if (CloseButton == nullptr)
    {
        return;
    }

    CloseButton->ShowGuiWindow(show);

    if (show)
    {
        CloseButton->Callback().SetMessage(Parent, MCGuiEventType::Close);
    }
}

auto MCGuiTitleBar::ShowZoomButton(bool show) const -> void
{
    if (ZoomButton != nullptr)
    {
        ZoomButton->ShowGuiWindow(show);
    }
}

auto MCGuiTitleBar::ShowZoomButtons(bool show) const -> void
{
    if (ZoomButton != nullptr)
    {
        ZoomButton->ShowGuiWindow(show);
    }

    if (ZoomOutButton != nullptr)
    {
        ZoomOutButton->ShowGuiWindow(show);
    }
}

auto MCGuiTitleBar::ShowSwoopyButton(bool show) const -> void
{
    if (SwoopyButton != nullptr)
    {
        SwoopyButton->ShowGuiWindow(show);
    }
}

auto MCGuiTitleBar::Resize(int32_t newWidth, int32_t newHeight) -> void
{
    MCGuiObject::Resize(newWidth, newHeight);
    CloseButton->MoveTo(WinWidth - CloseButton->Width(), 0);
    SwoopyButton->MoveTo(WinWidth - CloseButton->Width() - SwoopyButton->Width(), 0);
}

auto MCGuiTitleBar::ResizeOK(int32_t newWidth) const -> bool
{
    int32_t needed = 4;

    if (CloseButton != nullptr && CloseButton->IsShowing())
    {
        needed = CloseButton->Width() + 5;
    }

    if (ZoomButton != nullptr && ZoomButton->IsShowing())
    {
        needed += ZoomButton->Width() + 1;
    }

    if (ZoomOutButton != nullptr && ZoomOutButton->IsShowing())
    {
        needed += ZoomOutButton->Width() + 1;
    }

    return needed <= newWidth;
}
