#include "stdafx.h"
#include "gui/MCGuiObject.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "platform/MCInput.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// The scissors of the objects drawing in the frame pass around the one displaying now (innermost last), with the
    /// window each is on: a child that draws itself is cut to its nearest such ancestor's.
    /// </summary>
    std::vector<std::pair<MCAddressKey, MCRect>> ViewClips;

    /// <summary>The object drawing in the frame pass right now (its view open), or none.</summary>
    MCAddressKey DrawingLive;

    /// <summary>A size snapped to the 40-pixel grid: to the nearer multiple (19 and less round down).</summary>
    int32_t SnapToGrid(int32_t size)
    {
        if (size % 40 > 19)
        {
            size += 40;
        }

        return size - size % 40;
    }
}

MCGuiObject::~MCGuiObject()
{
    DestroyObject();
}

auto MCGuiObject::Place(int32_t xPos, int32_t yPos, int32_t width, int32_t height) -> void
{
    WinWidth = width;
    WinHeight = height;
    WinX = xPos;
    WinY = yPos;
    MaxWidth = width;
    MaxHeight = height;
    MaxX = xPos;
    MaxY = yPos;
    NormalWidth = width;
    NormalHeight = height;
    NormalX = xPos;
    NormalY = yPos;
    IconWidth = width;
    IconHeight = height;
    IconX = xPos;
    IconY = yPos;
    HideOffset = 0;
    HomeX = xPos;
    HomeY = yPos;
    WinState = MCGuiWindowState::Normal;
    ShowWindow = true;
    DragOn = false;
    BackgroundColor = 0xff;

    // The pane starts one pixel wider and taller than the object (the original's); the first move puts it right.
    FramePane = std::make_shared<MCPane>();
    FramePane->Window = ScreenPort()->SharedBitmap();
    FramePane->X0 = xPos;
    FramePane->Y0 = yPos;
    FramePane->X1 = xPos + width;
    FramePane->Y1 = yPos + height;
    Hidden = false;
    HideDirection = MCDirection::Down;
    PaintRoutine = nullptr;
    EventRoutine = nullptr;
    // The original forgot its children without telling them.
    ChildList.clear();
    Parent.reset();
    WinDepth = 0;
    WindowAnimation.reset();
    Animating = false;
    IconAnimation.reset();
    ObjectType = -1;
}

auto MCGuiObject::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                       [[maybe_unused]] std::string_view name) -> int32_t
{
    // Asked before the state is reset, as the original made the port first.
    const bool live = DrawsLive();
    Transparent = false;
    DropTargets.clear();
    Place(xPos, yPos, width, height);
    DisplayPort = std::make_unique<MCGuiPort>();
    return live ? DisplayPort->InitView(width, height) : DisplayPort->Init(width, height);
}

auto MCGuiObject::Destroy() -> void
{
    DestroyObject();
}

auto MCGuiObject::DestroyObject() -> void
{
    if (HasGuiSystem())
    {
        GuiSystem().RemoveTimers(*this);
    }

    DisplayPort.reset();
    FramePane.reset();
    BackgroundPort.reset();
    IconAnimation.reset();
    WindowAnimation.reset();
    DropTargets.clear();
    // A child destroyed with this object (its member) can't reach it to leave its list, so its entry stays, expired.
    Assert(std::ranges::all_of(ChildList, &std::weak_ptr<MCGuiObject>::expired), 0, " Number of Children NOT Zero ");
    LeaveParent();
    Animating = false;

    if (HasGuiSystem())
    {
        GuiSystem().LetGo(*this);
    }
}

auto MCGuiObject::LeaveParent() -> void
{
    if (const std::shared_ptr<MCGuiObject> parent = Parent.lock(); parent != nullptr)
    {
        parent->RemoveChild(*this);
    }

    Parent.reset();
}

auto MCGuiObject::SetDisplayPort(MCGuiPort& newPort) -> void
{
    FramePane->Window = newPort.SharedBitmap();

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].lock(); child != nullptr)
        {
            child->SetDisplayPort(newPort);
        }
    }
}

auto MCGuiObject::PointInside(int32_t xPos, int32_t yPos) -> bool
{
    return FramePane->X0 <= xPos && xPos <= FramePane->X1 && FramePane->Y0 <= yPos && yPos <= FramePane->Y1;
}

auto MCGuiObject::RectIntersect(int32_t left, int32_t top, int32_t right, int32_t bottom) -> bool
{
    return FramePane->X0 < right && left < FramePane->X1 && FramePane->Y0 < bottom && top < FramePane->Y1;
}

auto MCGuiObject::RectIntersect(tagRECT area) -> bool
{
    return RectIntersect(area.left, area.top, area.right, area.bottom);
}

auto MCGuiObject::SetPaintRoutine(std::function<void(MCGuiObject&)> routine) -> void
{
    PaintRoutine = std::move(routine);
}

auto MCGuiObject::SetEventRoutine(std::function<void(MCGuiObject&, MCGuiEvent&)> routine) -> void
{
    EventRoutine = std::move(routine);
}

auto MCGuiObject::Paint() -> void
{
    if (PaintRoutine)
    {
        PaintRoutine(*this);
    }
}

auto MCGuiObject::FindObject(int32_t xPos, int32_t yPos) -> std::shared_ptr<MCGuiObject>
{
    if (WinState != MCGuiWindowState::Iconized)
    {
        if (!ShowWindow)
        {
            return nullptr;
        }

        for (size_t i = ChildList.size(); i > 0; i--)
        {
            const std::shared_ptr<MCGuiObject> child = ChildList[i - 1].lock();

            if (child == nullptr)
            {
                continue;
            }

            if (std::shared_ptr<MCGuiObject> found = child->FindObject(xPos, yPos); found != nullptr)
            {
                return found;
            }
        }
    }

    if (ShowWindow && FramePane != nullptr && PointInside(xPos, yPos))
    {
        return shared_from_this();
    }

    return nullptr;
}

auto MCGuiObject::SetParent(std::weak_ptr<MCGuiObject> newParent) -> void
{
    Parent = std::move(newParent);
}

auto MCGuiObject::SetDepth(int32_t newDepth) -> void
{
    const std::shared_ptr<MCGuiObject> owner = Parent.lock();

    if (owner != nullptr)
    {
        owner->RemoveChild(*this);
    }

    WinDepth = newDepth;

    if (owner != nullptr)
    {
        owner->AddChild(weak_from_this());
    }
}

auto MCGuiObject::Depth() -> int32_t
{
    return WinDepth;
}

auto MCGuiObject::StartAnimation() -> void
{
    Animating = true;
}

auto MCGuiObject::StopAnimation() -> void
{
    Animating = false;
}

auto MCGuiObject::StartModal() -> void
{
    GuiSystem().SetModalObject(*this);
}

auto MCGuiObject::StopModal() -> void
{
    GuiSystem().ClearModal();
}

auto MCGuiObject::SetBackColor(int32_t color) -> void
{
    BackgroundColor = color;
}

auto MCGuiObject::BackColor() -> int32_t
{
    return BackgroundColor;
}

auto MCGuiObject::DrawBox(uint8_t color, tagRECT area) -> void
{
    DrawBox(color, area.left, area.top, area.right, area.bottom);
}

auto MCGuiObject::DrawBox(uint8_t color, int32_t left, int32_t top, int32_t right, int32_t bottom) -> void
{
    if (left == -1)
    {
        left = 0;
    }

    if (top == -1)
    {
        top = 0;
    }

    if (right == -1)
    {
        right = Width() - 1;
    }

    if (bottom == -1)
    {
        bottom = Height() - 1;
    }

    MCPane& pane = DisplayPort->Frame();
    VfxLineDraw(pane, left, top, right, top, color);
    VfxLineDraw(pane, left, top, left, bottom, color);
    VfxLineDraw(pane, left, bottom, right, bottom, color);
    VfxLineDraw(pane, right, top, right, bottom, color);
}

auto MCGuiObject::DrawFramed(bool pushed, bool fill) -> void
{
    // Raised: light top and left, dark bottom and right; sunken the other way round.
    const int32_t innerTopLeft = pushed ? 4 : 0xc;
    const int32_t outerTopLeft = pushed ? 6 : 10;
    const int32_t innerBottomRight = pushed ? 0xc : 4;
    const int32_t outerBottomRight = pushed ? 10 : 6;
    MCPane& pane = DisplayPort->Frame();

    if (fill && BackgroundColor != 0xff)
    {
        VfxPaneWipe(pane, BackColor());
    }

    VfxLineDraw(pane, 0, Height() - 1, Width(), Height() - 1, 0x10);
    VfxLineDraw(pane, 0, 0, Width(), 0, 0x10);
    VfxLineDraw(pane, 0, 0, 0, Height() - 1, 0x10);
    VfxLineDraw(pane, Width() - 1, 0, Width() - 1, Height() - 1, 0x10);
    VfxLineDraw(pane, 1, 1, Width() - 2, 1, innerTopLeft);
    VfxLineDraw(pane, 2, 2, Width() - 3, 2, outerTopLeft);
    VfxLineDraw(pane, 1, 1, 1, Height() - 2, innerTopLeft);
    VfxLineDraw(pane, 2, 2, 2, Height() - 3, outerTopLeft);
    VfxLineDraw(pane, Width() - 2, 1, Width() - 2, Height() - 2, innerBottomRight);
    VfxLineDraw(pane, Width() - 3, 2, Width() - 3, Height() - 3, outerBottomRight);
    VfxLineDraw(pane, 1, Height() - 2, Width() - 2, Height() - 2, innerBottomRight);
    VfxLineDraw(pane, 2, Height() - 3, Width() - 3, Height() - 3, outerBottomRight);
}

auto MCGuiObject::FillBox(int16_t left, int16_t top, int16_t right, int16_t bottom, uint8_t color) const -> void
{
    MCPane box = DisplayPort->Frame();
    box.X0 = left;
    box.Y0 = top;
    box.X1 = right;
    box.Y1 = bottom;
    VfxPaneWipe(box, color);
}

auto MCGuiObject::BringToFront(bool noShuffle) -> void
{
    const std::shared_ptr<MCGuiObject> parent = Parent.lock();

    if (parent == nullptr)
    {
        return;
    }

    parent->BringToFront(noShuffle);
    std::vector<std::weak_ptr<MCGuiObject>>& siblings = parent->ChildList;

    if (siblings.size() <= 1)
    {
        return;
    }

    // The children are kept sorted by depth: the ones behind this object's depth stay, then this depth's others, with
    // this object last (in front), then the rest.
    const auto depthOf = [](const std::weak_ptr<MCGuiObject>& sibling)
    {
        const std::shared_ptr<MCGuiObject> object = sibling.lock();
        return object != nullptr ? object->Depth() : std::numeric_limits<int32_t>::min();
    };

    const std::vector<std::weak_ptr<MCGuiObject>> sorted = siblings;
    size_t next = 0;
    size_t placed = 0;

    while (next < sorted.size() && depthOf(sorted[next]) < WinDepth)
    {
        siblings[placed++] = sorted[next++];
    }

    while (next < sorted.size() && depthOf(sorted[next]) == WinDepth)
    {
        if (!MCRefersTo(sorted[next], *this))
        {
            siblings[placed++] = sorted[next];
        }

        next++;
    }

    siblings[placed] = weak_from_this();

    for (size_t slot = placed + 1; slot < siblings.size(); slot++)
    {
        siblings[slot] = sorted[next++];
    }

    if (GridAligned && !noShuffle)
    {
        for (int32_t i = parent->NumberOfChildren() - 1; i >= 0; i--)
        {
            const std::shared_ptr<MCGuiObject> sibling = parent->Child(i);

            if (sibling != nullptr && sibling->IsShowing() && sibling.get() != this && sibling->GridAligned &&
                sibling->X() / 40 == X() / 40 && sibling->Y() / 40 == Y() / 40)
            {
                sibling->GridAligned = false;
                const int32_t newY = 5 - sibling->Y() % 40 + sibling->Y();
                const int32_t newX = 5 - sibling->X() % 40 + sibling->X();
                sibling->MoveTo(newX, newY);
                sibling->GridAligned = true;
            }
        }
    }
}

auto MCGuiObject::NumberOfChildren() -> int32_t
{
    return static_cast<int32_t>(ChildList.size());
}

auto MCGuiObject::Children() const -> std::vector<std::shared_ptr<MCGuiObject>>
{
    std::vector<std::shared_ptr<MCGuiObject>> children;
    children.reserve(ChildList.size());

    for (const std::weak_ptr<MCGuiObject>& listed : ChildList)
    {
        if (std::shared_ptr<MCGuiObject> child = listed.lock(); child != nullptr)
        {
            children.push_back(std::move(child));
        }
    }

    return children;
}

auto MCGuiObject::AddChild(std::weak_ptr<MCGuiObject> child) -> void
{
    const std::shared_ptr<MCGuiObject> added = child.lock();

    if (added == nullptr)
    {
        return;
    }

    Assert(added->Parent.expired() || MCRefersTo(added->Parent, *this), 0, " Adding child that's someone else's ");
    RemoveChild(*added);
    added->SetParent(weak_from_this());
    ChildList.push_back(std::move(child));
    added->BringToFront(true);
    added->MoveTo(added->X(), added->Y());
}

auto MCGuiObject::RemoveChild(MCGuiObject& child) -> void
{
    const auto found = std::ranges::find_if(ChildList, [&](const std::weak_ptr<MCGuiObject>& listed)
                                            { return MCRefersTo(listed, child); });

    if (found == ChildList.end())
    {
        return;
    }

    ChildList.erase(found);
    child.SetParent({});
}

auto MCGuiObject::Dragging() -> bool
{
    return DragOn;
}

auto MCGuiObject::StartDrag(int32_t xPos, int32_t yPos) -> void
{
    DragOn = true;
    DragX = xPos;
    DragY = yPos;
}

auto MCGuiObject::StopDrag() -> void
{
    DragOn = false;
}

auto MCGuiObject::DragStartX() -> int32_t
{
    return DragX;
}

auto MCGuiObject::DragStartY() -> int32_t
{
    return DragY;
}

auto MCGuiObject::ForemostChild(int32_t atDepth) -> std::shared_ptr<MCGuiObject>
{
    for (size_t i = ChildList.size(); i > 0; i--)
    {
        if (std::shared_ptr<MCGuiObject> child = ChildList[i - 1].lock();
            child != nullptr && child->WinDepth == atDepth)
        {
            return child;
        }
    }

    return nullptr;
}

auto MCGuiObject::Child(int32_t index) -> std::shared_ptr<MCGuiObject>
{
    if (index < 0 || static_cast<size_t>(index) >= ChildList.size())
    {
        return nullptr;
    }

    return ChildList[static_cast<size_t>(index)].lock();
}

auto MCGuiObject::Width() -> int32_t
{
    return WinWidth;
}

auto MCGuiObject::Height() -> int32_t
{
    return WinHeight;
}

auto MCGuiObject::Port() -> MCGuiPort&
{
    SDL_assert(DisplayPort != nullptr);
    return *DisplayPort;
}

auto MCGuiObject::X() -> int32_t
{
    return WinX;
}

auto MCGuiObject::Y() -> int32_t
{
    return WinY;
}

auto MCGuiObject::GlobalX() -> int32_t
{
    int32_t result = WinX;

    for (std::shared_ptr<MCGuiObject> owner = Parent.lock(); owner != nullptr; owner = owner->Parent.lock())
    {
        result += owner->X();
    }

    return result;
}

auto MCGuiObject::GlobalY() -> int32_t
{
    int32_t result = WinY;

    for (std::shared_ptr<MCGuiObject> owner = Parent.lock(); owner != nullptr; owner = owner->Parent.lock())
    {
        result += owner->Y();
    }

    return result;
}

auto MCGuiObject::Frame() -> MCPane&
{
    SDL_assert(FramePane != nullptr);
    return *FramePane;
}

auto MCGuiObject::MoveTo(int32_t xPos, int32_t yPos, bool temporary) -> void
{
    WinY = yPos;
    WinX = xPos;
    const std::shared_ptr<MCGuiObject> parent = Parent.lock();
    const int32_t parentX = parent != nullptr ? parent->GlobalX() : 0;
    const int32_t parentY = parent != nullptr ? parent->GlobalY() : 0;
    FramePane->X0 = parentX + xPos;
    FramePane->Y0 = parentY + yPos;
    FramePane->X1 = WinWidth - 1 + FramePane->X0;
    FramePane->Y1 = WinHeight - 1 + FramePane->Y0;

    if (!temporary)
    {
        HomeX = xPos;
        HomeY = yPos;
    }

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].lock(); child != nullptr)
        {
            child->MoveTo(child->X(), child->Y());
        }
    }
}

auto MCGuiObject::Resize(int32_t newWidth, int32_t newHeight) -> void
{
    if (newWidth <= 0 || newHeight <= 0 || (newWidth == WinWidth && newHeight == WinHeight))
    {
        return;
    }

    if (GridAligned)
    {
        newWidth = SnapToGrid(newWidth);

        if (newWidth == 0)
        {
            newWidth = 40;
        }

        newHeight = SnapToGrid(newHeight);
    }

    if (DisplayPort != nullptr)
    {
        DisplayPort->Resize(newWidth, newHeight);
    }

    WinWidth = newWidth;
    WinHeight = newHeight;
    FramePane->X1 = FramePane->X0 - 1 + newWidth;
    FramePane->Y1 = FramePane->Y0 - 1 + newHeight;
}

auto MCGuiObject::Draw() -> void
{
    if (WinState == MCGuiWindowState::Iconized)
    {
        IconAnimation->Draw(DisplayPort->Frame(), 0, 0);
        return;
    }

    if (BackgroundPort != nullptr)
    {
        BackgroundPort->CopyTo(DisplayPort->Frame(), 0, 0, true);
    }

    if (WindowAnimation != nullptr && Animating)
    {
        WindowAnimation->Draw(DisplayPort->Frame(), 0, 0);
    }

    Paint();

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].lock(); child != nullptr)
        {
            DrawChild(*child);
        }
    }
}

auto MCGuiObject::DisplayChildren() -> void
{
    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].lock(); child != nullptr)
        {
            child->Display();
        }
    }
}

auto MCGuiObject::Display() -> void
{
    if (!ShowWindow || (IsHidden() && HideOffset == 0))
    {
        return;
    }

    if (DrawsLive())
    {
        SlideStep();
        DrawInFramePass(*DisplayPort);
        return;
    }

    if (WinState == MCGuiWindowState::Iconized)
    {
        if (IconAnimation != nullptr)
        {
            Draw();
        }
    }
    else if (WindowAnimation != nullptr)
    {
        WindowAnimation->Draw(DisplayPort->Frame(), 0, 0);
        Draw();
    }

    SlideStep();

    if (DisplayPort != nullptr)
    {
        DisplayPort->CopyTo(*FramePane, 0, 0, Transparent);
    }

    if (WinState != MCGuiWindowState::Iconized)
    {
        DisplayChildren();
    }
}

auto MCGuiObject::SetDrawsLive() -> void
{
    LiveDraw = true;

    if (DisplayPort != nullptr && !DisplayPort->IsView())
    {
        DisplayPort->InitView(Width(), Height());
    }
}

auto MCGuiObject::DrawsChild(MCGuiObject& child) -> bool
{
    return !child.DrawsLive() && DrawingLive != MCAddressKey::Of(*this);
}

auto MCGuiObject::DrawChild(MCGuiObject& child) -> void
{
    if (DrawsChild(child))
    {
        child.Draw();
    }
}

auto MCGuiObject::DrawInFramePass(MCGuiPort& port, int32_t scrollY, bool wipe, bool displayChildren) -> void
{
    // The view lies over the pane, on the window the pane is on (the screen, or a scroll pane's content), cut to the
    // window and to the scissor of the nearest clipping ancestor on the same window.
    std::shared_ptr<MCWindow> target = FramePane->Window.lock();

    if (target == nullptr)
    {
        return;
    }

    const MCAddressKey targetKey = MCAddressKey::Of(*target);
    MCRect scissor{std::max(FramePane->X0, 0), std::max(FramePane->Y0, 0), std::min(FramePane->X1, target->XMax),
                   std::min(FramePane->Y1, target->YMax)};

    if (!ViewClips.empty() && ViewClips.back().first == targetKey)
    {
        const MCRect& outer = ViewClips.back().second;
        scissor.X0 = std::max(scissor.X0, outer.X0);
        scissor.Y0 = std::max(scissor.Y0, outer.Y0);
        scissor.X1 = std::min(scissor.X1, outer.X1);
        scissor.Y1 = std::min(scissor.Y1, outer.Y1);
    }

    if (scissor.X1 < scissor.X0 || scissor.Y1 < scissor.Y0)
    {
        // An empty scissor: the shut one draws nothing either way, and the children are cut away by it.
        scissor = MCRect{0, 0, -1, -1};
    }

    port.OpenView(std::move(target), FramePane->X0, FramePane->Y0 - scrollY, scissor, Transparent);
    const MCAddressKey outerDrawing = DrawingLive;
    DrawingLive = MCAddressKey::Of(*this);

    // A picture that was never painted held zeros (a port's bitmap starts zeroed), and an opaque object copied them to
    // the screen; a transparent one let what was under it show.
    if (!Transparent && wipe)
    {
        VfxPaneWipe(port.Frame(), 0);
    }

    if (WinState != MCGuiWindowState::Iconized || IconAnimation != nullptr)
    {
        Draw();
    }

    DrawingLive = outerDrawing;
    port.CloseView();

    if (WinState != MCGuiWindowState::Iconized && displayChildren)
    {
        const bool clips = ClipsChildren();

        if (clips)
        {
            ViewClips.emplace_back(targetKey, scissor);
        }

        DisplayChildren();

        if (clips)
        {
            ViewClips.pop_back();
        }
    }
}

auto MCGuiObject::SlideStep() -> void
{
    if (HideOffset == 0)
    {
        return;
    }

    // A slide (HideMe) moves the whole offset each frame until the object is off the screen, or back home.
    if (HideDirection == MCDirection::Left || HideDirection == MCDirection::Right)
    {
        MoveTo(X() + HideOffset, Y(), true);
    }
    else
    {
        MoveTo(X(), Y() + HideOffset, true);
    }

    if (Hidden)
    {
        const tagRECT screen = {0, 0, GuiSystem().Width(), GuiSystem().Height()};

        if (!RectIntersect(screen))
        {
            HideOffset = 0;
        }

        return;
    }

    bool home = false;

    if (HideOffset < 0)
    {
        home = HomeX >= GlobalX() && HomeY >= GlobalY();
    }
    else if (HideOffset > 0)
    {
        home = HomeX <= GlobalX() && HomeY <= GlobalY();
    }

    if (home)
    {
        const std::shared_ptr<MCGuiObject> parent = Parent.lock();
        const int32_t homeYOffset = HomeY - parent->GlobalY();
        MoveTo(HomeX - parent->GlobalX(), homeYOffset, true);
        HideOffset = 0;
    }
}

auto MCGuiObject::HandleEvent(MCGuiEvent& event) -> void
{
    MCGuiSystem& gui = GuiSystem();

    if (event.Type == MCGuiEventType::LeftButtonDown)
    {
        if (!Parent.expired())
        {
            BringToFront(false);
            ARedrawScreen();
        }
    }
    else if (event.Type == MCGuiEventType::ScreenResized)
    {
        for (size_t i = 0; i < ChildList.size(); i++)
        {
            if (const std::shared_ptr<MCGuiObject> child = ChildList[i].lock(); child != nullptr)
            {
                child->HandleEvent(event);
            }
        }
    }

    if (WinState == MCGuiWindowState::Iconized)
    {
        // An icon is dragged by the mouse, and opened by a double click.
        switch (event.Type)
        {
            case MCGuiEventType::LeftButtonDown:
            {
                gui.Grab(weak_from_this());
                LastX = event.X;
                LastY = event.Y;
                break;
            }
            case MCGuiEventType::LeftButtonUp:
            {
                if (gui.Grabs(*this))
                {
                    gui.Release();

                    if (const std::shared_ptr<MCGuiObject> target = ScreenWindow()->FindObject(event.X, event.Y);
                        target != nullptr)
                    {
                        target->Enter();
                    }
                }
                break;
            }
            case MCGuiEventType::MouseMove:
            {
                if (gui.Grabs(*this))
                {
                    const int32_t eventY = event.Y;
                    const std::shared_ptr<MCGuiObject> target = ScreenWindow()->FindObject(event.X, eventY);

                    if (target != nullptr && target->Depth() == 100)
                    {
                        return;
                    }

                    const int32_t newY = Y() + (eventY - LastY);
                    MoveTo(X() + (event.X - LastX), newY);
                    LastX = event.X;
                    LastY = eventY;
                }
                break;
            }
            case MCGuiEventType::LeftDoubleClick:
            {
                if (gui.Grabs(*this))
                {
                    gui.Release();
                }

                Normalize();
                break;
            }
        }
    }
    else if (event.Type == MCGuiEventType::Close)
    {
        Destroy();
    }

    if (EventRoutine)
    {
        EventRoutine(*this, event);
    }
}

auto MCGuiObject::SetBackground(std::string_view fileName) -> int32_t
{
    BackgroundPort = std::make_shared<MCGuiPort>();
    return BackgroundPort->Init(fileName);
}

auto MCGuiObject::SetBackground(int32_t artPacket) -> int32_t
{
    BackgroundPort = std::make_shared<MCGuiPort>();
    return BackgroundPort->Init(artPacket);
}

auto MCGuiObject::Maximize() -> void
{
    SetState(std::to_underlying(MCGuiWindowState::Maximized));
}

auto MCGuiObject::Normalize() -> void
{
    SetState(std::to_underlying(MCGuiWindowState::Normal));
}

auto MCGuiObject::Iconize() -> void
{
    SetState(std::to_underlying(MCGuiWindowState::Iconized));
}

auto MCGuiObject::State() -> MCGuiWindowState
{
    return WinState;
}

auto MCGuiObject::SetState(int32_t state) -> void
{
    const auto newState = static_cast<MCGuiWindowState>(state);

    // Saves the placement of the state left, then takes the new state's. From the iconized state the new state is
    // stored first, so a state the switch doesn't list sticks (with the icon's placement).
    const auto take = [this](MCGuiWindowState taken, int32_t width, int32_t height, int32_t xPos, int32_t yPos)
    {
        WinX = xPos;
        WinState = taken;
        WinY = yPos;
        WinWidth = width;
        WinHeight = height;
    };

    switch (WinState)
    {
        case MCGuiWindowState::Normal:
        {
            NormalY = WinY;
            NormalX = WinX;
            NormalWidth = WinWidth;
            NormalHeight = WinHeight;

            if (newState == MCGuiWindowState::Maximized)
            {
                take(newState, MaxWidth, MaxHeight, MaxX, MaxY);
            }
            else if (newState == MCGuiWindowState::Iconized)
            {
                take(newState, IconWidth, IconHeight, IconX, IconY);
            }
            break;
        }
        case MCGuiWindowState::Maximized:
        {
            MaxY = WinY;
            MaxX = WinX;
            MaxWidth = WinWidth;
            MaxHeight = WinHeight;

            if (newState == MCGuiWindowState::Normal)
            {
                take(newState, NormalWidth, NormalHeight, NormalX, NormalY);
            }
            else if (newState == MCGuiWindowState::Iconized)
            {
                take(newState, IconWidth, IconHeight, IconX, IconY);
            }
            break;
        }
        case MCGuiWindowState::Iconized:
        {
            IconX = WinX;
            IconY = WinY;
            WinState = newState;
            IconWidth = WinWidth;
            IconHeight = WinHeight;

            if (newState == MCGuiWindowState::Normal)
            {
                take(newState, NormalWidth, NormalHeight, NormalX, NormalY);
            }
            else if (newState == MCGuiWindowState::Maximized)
            {
                take(newState, MaxWidth, MaxHeight, MaxX, MaxY);
            }
            break;
        }
    }

    Resize(WinWidth, WinHeight);
    MoveTo(WinX, WinY);
    ARedrawScreen();
}

auto MCGuiObject::SetAnimation(std::string_view fileName) -> int32_t
{
    WindowAnimation = std::make_shared<MCGuiAnimation>();
    return WindowAnimation->Load(fileName);
}

auto MCGuiObject::SetIcon(std::string_view fileName) -> int32_t
{
    IconAnimation = std::make_shared<MCGuiAnimation>();
    const int32_t result = IconAnimation->Load(fileName);

    if (result == 0)
    {
        IconWidth = IconAnimation->Width();
        IconHeight = IconAnimation->Height();
    }

    return result;
}

auto MCGuiObject::Animation() -> std::shared_ptr<MCGuiAnimation>
{
    return WindowAnimation;
}

auto MCGuiObject::Icon() -> std::shared_ptr<MCGuiAnimation>
{
    return IconAnimation;
}

auto MCGuiObject::Background() -> std::shared_ptr<MCGuiPort>
{
    return BackgroundPort;
}

auto MCGuiObject::SetBit(int32_t xPos, int32_t yPos, uint8_t color) -> void
{
    if (xPos < 0 || xPos >= Width() || yPos < 0 || yPos >= Height())
    {
        return;
    }

    if (const std::span<uint8_t> pixels = DisplayPort->Pixels(); !pixels.empty())
    {
        pixels[static_cast<size_t>(Width() * yPos + xPos)] = color;
    }
    else if (DisplayPort->IsView())
    {
        // A view has no pixels; the pixel is drawn through it.
        VfxPixelWrite(DisplayPort->Frame(), xPos, yPos, color);
    }
}

auto MCGuiObject::HideMe(bool hide) -> void
{
    if (Hidden == hide || HideOffset != 0)
    {
        return;
    }

    if (hide)
    {
        HomeX = GlobalX();
        HomeY = GlobalY();
        Hidden = true;

        switch (HideDirection)
        {
            case MCDirection::Left:
                HideOffset = -(GlobalX() + Width());
                break;
            case MCDirection::Up:
                HideOffset = -(GlobalY() + Height());
                break;
            case MCDirection::Right:
                HideOffset = GuiSystem().Width() - GlobalX();
                break;
            case MCDirection::Down:
                HideOffset = GuiSystem().Height() - GlobalY();
                break;
        }

        return;
    }

    Hidden = false;
    HideOffset = HomeX != GlobalX() ? HomeX - GlobalX() : HomeY - GlobalY();
}
