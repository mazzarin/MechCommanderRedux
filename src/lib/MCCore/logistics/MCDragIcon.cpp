#include "stdafx.h"
#include "logistics/MCDragIcon.h"
#include "gui/MCHardwareCursor.h"
#include "main/MCLogistics.h"
#include "vfx/MCVfxFunctions.h"

auto MCDragIcon::Display() -> void
{
    if (IsShowing() == 0)
    {
        return;
    }

    // Port fix: with the system cursor, the icon rides on the cursor instead of being drawn into the frame, so
    // it follows the mouse as closely as the cursor does.
    if (MCHardwareCursorCarry(*this, Lport()->Frame()))
    {
        return;
    }

    VfxPaneCopy(Lport()->Frame(), 0, 0, *FramePane, 0, 0, -1);
}

auto MCDragIcon::Begin(int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                       const std::function<void(MCLogPort& surface)>& render) -> void
{
    Init(xPos, yPos, width, height);
    MCPane& surface = Lport()->Frame();
    VfxPaneWipe(surface, 0);
    render(*Lport());
    const int32_t right = width - 1;
    const int32_t bottom = height - 1;
    VfxLineDraw(surface, 0, 0, right, 0, 0xea);
    VfxLineDraw(surface, 0, 1, 0, bottom - 1, 0xea);
    VfxLineDraw(surface, right, 1, right, bottom - 1, 0xea);
    VfxLineDraw(surface, 0, bottom, right, bottom, 0xea);
}

auto MCDragIcon::Raise() -> void
{
    ShowGuiWindow(true);
    SetDepth(100);
}

auto MCDragIcon::ShowAt(int32_t xPos, int32_t yPos) -> void
{
    Raise();
    MoveTo(xPos, yPos, false);
}

auto MCDragIcon::DrawFrom(MCLogPort& surface, int32_t xPos, int32_t yPos,
                          const std::function<void(MCLogPort& port)>& draw) -> void
{
    MCPane& pane = surface.Frame();
    const MCPane whole = pane;
    pane.X0 = whole.X0 - xPos;
    pane.Y0 = whole.Y0 - yPos;
    draw(surface);
    pane = whole;
}

auto MCDragIcon::Create(MCGuiObject& screen, int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                        const std::function<void(MCLogPort& surface)>& render, bool hidden)
    -> std::shared_ptr<MCDragIcon>
{
    Remove();
    GlobalLog()->DragIcon = screen.CreateChild<MCDragIcon>(
        [&](MCDragIcon& icon)
        {
            icon.Begin(xPos, yPos, width, height, render);

            // Not on the screen yet: the original added it later, or not at all.
            if (hidden)
            {
                icon.ShowGuiWindow(false);
            }
        });
    return GlobalLog()->DragIcon.Lock();
}

auto MCDragIcon::Current() -> std::shared_ptr<MCDragIcon>
{
    return GlobalLog()->DragIcon.Lock();
}

auto MCDragIcon::Remove() -> void
{
    CloseGuiObject(GlobalLog()->DragIcon);
    GlobalLog()->DragIcon.reset();
}
