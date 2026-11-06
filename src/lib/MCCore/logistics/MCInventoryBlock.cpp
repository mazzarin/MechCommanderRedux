#include "stdafx.h"
#include "logistics/MCInventoryBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCLogInvScreen.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "vfx/MCVfxFunctions.h"

auto MCInventoryBlock::InitRow() -> void
{
    MCLogObject::InitWithoutPort(0, 0, 0xad, 0x2b);
    Enabled = true;
}

auto MCInventoryBlock::DrawBackground(MCLogPort& port) -> void
{
    VfxPaneCopy(GlobalLog()->InvBlockPort->Frame(), 0, 0, port.Frame(), 0, ListIndex() * WinHeight, -1);
}

auto MCInventoryBlock::DrawDisabled() -> void
{
}

auto MCInventoryBlock::SetEnabled(bool enable) -> void
{
    Enabled = enable;
    Draw();
}

auto MCInventoryBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    VfxPaneCopy(GlobalLog()->InvBlockPort->Frame(), 0, 0, port.Frame(), 0, top, -1);
}

auto MCInventoryBlock::OnBeginDrag(MCLogPort& surface) -> void
{
    VfxPaneWipe(surface.Frame(), 0x10);
    MCDragIcon::DrawFrom(surface, 2, 1, [this](MCLogPort& port) { DrawRow(port, 0); });
}

auto MCInventoryBlock::PreHandleEvent(const MCDragState& drag, MCGuiEvent& event) -> bool
{
    if (!Parent.expired() && drag.Idle() && (event.Type == 8 || event.Type == 9))
    {
        ParentObject()->HandleEvent(event);
        return false;
    }

    std::shared_ptr<MCLogInvScreen> screen = Screen();

    if (drag.Idle())
    {
        std::shared_ptr<MCGuiObject> pane = screen->InventoryPane.Lock();

        if (event.Y < pane->GlobalY() || pane->GlobalY() + pane->Height() < event.Y)
        {
            MCLogInvScreen::DrawBlankInvInfoBlock(-1);
            screen->HandleEvent(event);
            return false;
        }

        if (IsShowing() == 0)
        {
            return false;
        }
    }

    return true;
}

auto MCInventoryBlock::MakeDragIcon(MCDragState& drag, MCGuiEvent& event) -> void
{
    drag.X = 2;
    drag.Y = ListIndex() * WinHeight + 1;
    const std::shared_ptr<MCDragIcon> icon =
        MCDragIcon::Create(*Screen(), drag.X, drag.Y, 0x20, 0x20, [this](MCLogPort& surface) { OnBeginDrag(surface); });
    icon->MoveTo(event.X - 0xf, event.Y - 0xf, false);
}

auto MCInventoryBlock::Screen() -> std::shared_ptr<MCLogInvScreen>
{
    const std::shared_ptr<MCLogObject> current = GlobalLog()->CurrentScreen();
    return current != nullptr ? current->SharedAs<MCLogInvScreen>() : nullptr;
}

auto MCInventoryBlock::DrawDropArt(MCLogInvScreen& screen, int32_t tab) -> void
{
    screen.Info.Blank(tab);
}

auto MCInventoryBlock::SalePrice(int32_t value) -> int32_t
{
    if (MultiPlayer() == nullptr && !Solo)
    {
        value /= 2;
    }

    return value;
}

auto MCInventoryBlock::BumpDeploySlots(bool vehicle) -> void
{
    for (auto& lance : GlobalLog()->DeploySlots)
    {
        for (auto& slot : lance)
        {
            int32_t& value = vehicle ? slot.Vehicle : slot.Unit;

            if (value > -1)
            {
                ++value;
            }
        }
    }
}
