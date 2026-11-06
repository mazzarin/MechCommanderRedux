#include "stdafx.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScrollPane.h"
#include "main/MCGamePaths.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The pilot rows' drag.</summary>
    MCDragState PilotDrag;

    /// <summary>The rank name (string table 0x70..0x73); empty for a rank out of range.</summary>
    /// <remarks>Port fix: an out-of-range rank leaves the text empty (the original kept whatever was in the buffer).</remarks>
    std::string RankName(int32_t rank)
    {
        if (rank < 0 || rank > 3)
        {
            return {};
        }

        return LoadGameString(0x70 + static_cast<uint32_t>(rank), 0xfe);
    }
}

auto MCPilotInventoryBlock::Init(std::shared_ptr<MCLogWarrior> logWarrior) -> void
{
    _Warrior = logWarrior;
    InitRow();
    PortraitPort = std::make_shared<MCLogPort>();
    PortraitPort->Load(std::format("{}logart\\{}", ArtPath, Warrior()->Picture));
}

auto MCPilotInventoryBlock::ListIndex() const -> int32_t
{
    const std::shared_ptr<MCLogWarrior> warrior = Warrior();
    return warrior != nullptr ? warrior->ListIndex : -1;
}

auto MCPilotInventoryBlock::OnDestroy() -> void
{
    PortraitPort.reset();
    MCInventoryBlock::OnDestroy();
}

auto MCPilotInventoryBlock::Draw() -> void
{
    if (!Enabled)
    {
        DrawDisabled();
    }
}

auto MCPilotInventoryBlock::OnRefresh() -> void
{
    // On the repair screen a pilot can only go to the selected mech, and only when it has none.
    GreyedOut = false;

    if (OnRepairScreen())
    {
        std::shared_ptr<MCLogMech> selected = GlobalLog()->RepairScreen->SelectedMech();
        GreyedOut = selected == nullptr || selected->PilotIndex >= 0;
    }
}

auto MCPilotInventoryBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    std::shared_ptr<MCLogBlockPort> row = RowPicture(*GlobalLog()->InvBlockPort, port, top, true);
    PortraitPort->CopyTo(row->Frame(), 3, 2, true);
    WriteText(*YellowDropFont, *row, 0x26, 7, Warrior()->Callsign);
    WriteText(*BlueDropFont, *row, 0x26, 0x15,
              std::format("{} {}", RankName(Warrior()->Rank), LoadGameString(0x287, 0xfe)));

    if (GreyedOut)
    {
        MCLogistics::Darken(0, LogisticFadetable(), *row);
    }
}

auto MCPilotInventoryBlock::DrawPilotInfo(const MCLogWarrior& warrior, const MCLogPort& portrait, MCLogPort& port)
    -> void
{
    MCLogistics::DrawPilotSkillBar(warrior, 3, 0x56, 0x192, 0, 0x36, MCLogInvScreen::InvBlockHeight, port);
    MCLogistics::DrawPilotSkillBar(warrior, 0, 0x56, 0x19b, 0, 0x36, MCLogInvScreen::InvBlockHeight, port);
    MCLogistics::DrawPilotSkillBar(warrior, 1, 0x56, 0x1a4, 0, 0x36, MCLogInvScreen::InvBlockHeight, port);
    MCLogistics::DrawPilotSkillBar(warrior, 2, 0x56, 0x1ad, 0, 0x36, MCLogInvScreen::InvBlockHeight, port);
    portrait.CopyTo(port.Frame(), 9, 0x196, true);
    WriteText(*YellowDropFont, port, 0x9c, 0x19a, RankName(warrior.Rank));
    // One pip per point of health left.
    int32_t x = 0xf;

    for (int32_t pip = 0; static_cast<float>(pip) < warrior.Health; ++pip, x += 3)
    {
        AGPixelWrite(port.Frame(), x, 0x192, 0xcf);
        AGPixelWrite(port.Frame(), x + 1, 0x192, 0xcf);
        AGPixelWrite(port.Frame(), x + 1, 0x193, 0xee);
        AGPixelWrite(port.Frame(), x, 0x193, 0xcf);
    }

    DrawInfoDescription(port, 0xc3, 0x22, warrior.Description, 7, 0x1b8);
}

auto MCPilotInventoryBlock::OfferSale() -> void
{
    const std::shared_ptr<MCLogWarrior> warrior = Warrior();
    MCLogPort picture;
    picture.Load(std::format("{}logart\\{}", ArtPath, warrior->Picture));
    int32_t price = SalePrice(GlobalLog()->PilotCosts[warrior->Rank]);
    // The answer goes to the pilot, not this row: the list can drop the row while the dialog is open.
    OpenPurchaseDialog(3, -price, 1, warrior->Callsign, LoadGameString(0x5f, 0xfe), picture,
                       [warrior](int32_t result, int32_t) { OnSellConfirmed(*warrior, result); });
}

auto MCPilotInventoryBlock::OnSellConfirmed(MCLogWarrior& warrior, int32_t result) -> void
{
    if (result == 0)
    {
        warrior.Assigned = false;
        GlobalLog()->ShiftPilots(warrior.ListIndex, -1);
        GlobalLog()->ReorderWarriors();
        GlobalLog()->PurchaseScreen->CreatePilotInvBlock();
        GlobalLog()->PurchaseScreen->SetUpPilotInv(false, true);
        return;
    }

    int32_t row = warrior.ListIndex;
    SoundSystem()->PlayPilotSpeech(warrior.PilotAudio, 2);
    warrior.Sold = true;
    GlobalLog()->PurPilotList->SetPilotStatus(warrior.DescIndex, MCPurPilotData::SoldBack);
    GlobalLog()->AssignedWarriorList->RemoveWarrior(static_cast<uint8_t>(warrior.Id));
    GlobalLog()->ShiftPilots(row, -1);
    GlobalLog()->ReorderWarriors();
    ResourcePoints -= GlobalLog()->PurchaseDialog->UnitCost;
}

auto MCPilotInventoryBlock::BoardMech(MCLogWarrior& warrior, MCLogMech& mech, int32_t row) -> void
{
    int32_t pilotRow = warrior.ListIndex;
    GlobalLog()->SetPilot(row, pilotRow);
    // The repair block's battle rating paint worked it out (the row draws it each frame).
    mech.CalcBR();
    GlobalLog()->SetPilot(row, pilotRow);
    Screen()->CreatePilotInvBlock();
    Screen()->SetUpPilotInv(false, true);
    SoundSystem()->PlayPilotSpeech(warrior.PilotAudio, 2);
}

auto MCPilotInventoryBlock::BackToInventory(MCLogWarrior& warrior, MCGuiEvent& event) -> void
{
    std::shared_ptr<MCLogInvScreen> screen = Screen();
    warrior.Assigned = false;
    GlobalLog()->ShiftPilots(warrior.ListIndex, -1);
    GlobalLog()->ReorderWarriors();
    screen->CreatePilotInvBlock();
    screen->SetUpPilotInv(false, true);
    PlayLogSound(OverPaneInside(*screen->InventoryPane, event) ? 0x34 : 0x33);
}

auto MCPilotInventoryBlock::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCLogWarrior> warrior = Warrior();

    if (!PreHandleEvent(PilotDrag, event))
    {
        return;
    }

    std::shared_ptr<MCLogInvScreen> screen = Screen();

    if (PilotDrag.Idle())
    {
        // The info block: skills, portrait, rank, wounds and description.
        MCLogInvScreen::DrawBlankInvInfoBlock(-1);
        PrepareInfoDescription(warrior->Description);
        screen->ShowDetails(MCInvInfoBox::Kind::Pilot, [warrior, portrait = PortraitPort](MCLogPort& port)
                            { DrawPilotInfo(*warrior, *portrait, port); });
    }

    switch (event.Type)
    {
        case 1:
        {
            // Left button down: drag the pilot.
            if (GreyedOut || PilotDrag.Carrying)
            {
                break;
            }

            SoundSystem()->PlayPilotSpeech(warrior->PilotAudio, 10);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            PilotDrag.Dragging = true;
            MakeDragIcon(PilotDrag, event);
            warrior->Assigned = true;
            GlobalLog()->ReorderWarriors();
            GlobalLog()->ShiftPilots(warrior->ListIndex, 1);
            screen->CreatePilotInvBlock();
            screen->SetUpPilotInv(false, false);
            MCDragIcon::Current()->Raise();
            break;
        }

        case 3:
        {
            // Right button down: pick the pilot up.
            if (GreyedOut || PilotDrag.Dragging)
            {
                break;
            }

            PilotDrag.Carrying = true;
            SoundSystem()->PlayPilotSpeech(warrior->PilotAudio, 10);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            MakeDragIcon(PilotDrag, event);
            MCDragIcon::Current()->Raise();
            break;
        }

        case 4:
        {
            // Left button up: drop the dragged pilot.
            if (PilotDrag.Carrying || GuiSystem().GrabbedObject() == nullptr)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            PilotDrag.Dragging = false;
            MCDragIcon::Remove();
            DrawDropArt(*screen, 1);

            if (OverPaneInside(*screen->UnitPane, event))
            {
                if (OnPurchaseScreen())
                {
                    // Dropped on the store: offer to sell the pilot.
                    OfferSale();
                    break;
                }

                // Dropped on a mech: it must be the selected one.
                std::shared_ptr<MCScrollPane> pane = screen->UnitPane.Lock();
                int32_t index = (event.Y - pane->GlobalY() + pane->GetScrollOffset()) / 0x70;

                if (index < pane->NumberOfChildren() && index < GlobalLog()->ForceMechList->GetMechCount())
                {
                    std::shared_ptr<MCLogMech> target;
                    GlobalLog()->ForceMechList->GetMechInfo(index, target);

                    if (target != nullptr && target == GlobalLog()->RepairScreen->SelectedMech())
                    {
                        BoardMech(*warrior, *target, index);
                        break;
                    }
                }
            }

            BackToInventory(*warrior, event);
            break;
        }

        case 6:
        {
            // Right button up: put down the carried pilot.
            if (PilotDrag.Dragging)
            {
                break;
            }

            PilotDrag.Carrying = false;

            if (GuiSystem().GrabbedObject() == nullptr)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            MCDragIcon::Remove();
            warrior->Assigned = true;
            GlobalLog()->ReorderWarriors();
            GlobalLog()->ShiftPilots(warrior->ListIndex, 1);
            screen->CreatePilotInvBlock();
            screen->SetUpPilotInv(false, true);
            DrawDropArt(*screen, 1);

            if (OnPurchaseScreen())
            {
                OfferSale();
                break;
            }

            // Onto the selected mech; with none, back to the inventory (the list kept this row through its refill: the
            // pilot is still in the pilot list, assigned).
            if (std::shared_ptr<MCLogMech> selected = GlobalLog()->RepairScreen->SelectedMech(); selected != nullptr)
            {
                BoardMech(*warrior, *selected, selected->SlotIndex);
                break;
            }

            BackToInventory(*warrior, event);
            break;
        }

        case 7:
        {
            // Mouse move: the dragged icon follows; otherwise the ticker shows the row's help.
            if (!PilotDrag.Dragging)
            {
                if (event.Key == 0)
                {
                    GlobalLog()->Ticker->SetString(LoadGameString(OnPurchaseScreen() ? 0x2e : 0x33, 0xfe));
                }
            }
            else
            {
                PilotDrag.X = event.X - 0xf;
                PilotDrag.Y = event.Y - 0xf;
                MCDragIcon::Current()->MoveTo(PilotDrag.X, PilotDrag.Y, false);
            }
            break;
        }

        default:
            break;
    }
}
