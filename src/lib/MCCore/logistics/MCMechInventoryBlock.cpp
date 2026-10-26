#include "stdafx.h"
#include "logistics/MCMechInventoryBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "object/MCMasterComponent.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The mech rows' drag.</summary>
    MCDragState MechDrag;
    /// <summary>The damage diagram's size, and the colour its picture was wiped to.</summary>
    constexpr int32_t DiagramWidth = 0x1c;
    constexpr int32_t DiagramHeight = 0x1e;
    constexpr int32_t DiagramBack = 0x10;
}

MCMechInventoryBlock::~MCMechInventoryBlock()
{
    MCMechInventoryBlock::Destroy();
}

auto MCMechInventoryBlock::Init(std::shared_ptr<MCLogMech> logMech) -> void
{
    Diagram.reset();
    _Mech = logMech;
    InitRow();
    ListIndex = Mech()->NameIndex;
}

auto MCMechInventoryBlock::Destroy() -> void
{
    Diagram.reset();
    _Mech.reset();
    MCInventoryBlock::Destroy();
}

auto MCMechInventoryBlock::Draw() -> void
{
    if (!Enabled)
    {
        DrawDisabled();
    }
}

auto MCMechInventoryBlock::DrawBackground() -> void
{
    if (Diagram.has_value())
    {
        return;
    }

    DiagramLook& look = Diagram.emplace();

    for (int32_t location = 0; location < DiagramLocations; ++location)
    {
        look.States[static_cast<size_t>(location)] = MCLogistics::MechBodyLocState(*Mech(), location);
    }

    // The battle rating bar along the left edge: 26 pixels at 18010.
    look.Bar = static_cast<int32_t>(static_cast<double>(Mech()->BattleRating) * 0x1.d1c6674f499a1p-15 * 26.0);
}

auto MCMechInventoryBlock::PaintDiagram(MCLogPort& port) const -> void
{
    if (!Diagram.has_value())
    {
        return;
    }

    VfxPaneWipe(port.Frame(), DiagramBack);

    for (int32_t location = 0; location < DiagramLocations; ++location)
    {
        MCLogistics::DrawBodyLoc(GlobalLog()->MechIconShapes[static_cast<size_t>(Mech()->NameIndex)].Bytes(), location,
                                 Diagram->States[static_cast<size_t>(location)], port, 2, 0);
    }

    VfxLineDraw(port.Frame(), 0, 0x1b, 0, 0x1b - Diagram->Bar, 0xe4);
    VfxLineDraw(port.Frame(), 1, 0x1b, 1, 0x1b - Diagram->Bar, 0xe4);
}

auto MCMechInventoryBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    MCInventoryBlock::DrawRow(port, top);
    WriteText(*YellowDropFont, port, 0x26, top + 7, mech->FileName);
    WriteText(*BlueDropFont, port, 0x26, top + 0x15,
              MCFormatPrintf(LoadGameString(0x4e, 0xfe).c_str(), static_cast<double>(mech->CurTonnage),
                             mech->WeightClassName.c_str()));

    // The diagram, drawn in place (the original copied its picture keyed).
    if (Diagram.has_value())
    {
        MCLogBlockPort diagram(port.Frame(), 5, top + 2, DiagramWidth, DiagramHeight, true);
        PaintDiagram(diagram);
    }
}

auto MCMechInventoryBlock::DrawInfo(MCLogPort& port) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    // The 0x1e square at (3, 2) of the row (its art and the diagram), as the original copied it out of the tab's
    // picture.
    MCLogBlockPort square(port.Frame(), 9, 0x191, 0x1e, 0x1e, false);
    VfxPaneCopy(GlobalLog()->InvBlockPort->Frame(), 3, 2, square.Frame(), 0, 0, -1);

    if (Diagram.has_value())
    {
        MCLogBlockPort diagram(square.Frame(), 2, 0, DiagramWidth, DiagramHeight, true);
        PaintDiagram(diagram);
    }

    WriteText(*YellowDropFont, port, 0x53, 0x193,
              std::format("{:.0f} {}", mech->CurTonnage, LoadGameString(0x6e, 0x1e)));
    WriteText(*YellowDropFont, port, 0x53, 0x19c, mech->WeightClassName);
    WriteText(*YellowDropFont, port, 0xa8, 0x193, mech->ChassisClassName);
    WriteText(*YellowDropFont, port, 0xa8, 0x19c, mech->ExtraName1);
    WriteText(*YellowDropFont, port, 0xa8, 0x1a5, mech->ExtraName2);
    WriteText(*YellowDropFont, port, 0x53, 0x1a5, std::format("{} m/s", mech->MaxRunSpeed));
    DrawInfoDescription(port, 0xc3, 0x26, mech->Description, 8, 0x1b3);
}

auto MCMechInventoryBlock::DeleteDiagram() -> void
{
    Diagram.reset();
}

auto MCMechInventoryBlock::OfferSale() -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();

    if (mech->Required != 0)
    {
        return;
    }

    std::string title =
        std::format("{:.0f} Ton {} 'Mech", mech->CurTonnage, LoadGameString(WeightClassString(mech->CurTonnage), 0xfe));
    mech->CalcMechCost(false);
    int32_t price = SalePrice(mech->ResourcePoints);
    // The dialog shows a copy of the diagram over the store's colour.
    MCLogPort picture;
    picture.Init(DiagramWidth, DiagramHeight);
    VfxPaneWipe(picture.Frame(), DiagramBack);
    {
        MCLogBlockPort diagram(picture.Frame(), 2, 0, DiagramWidth, DiagramHeight, true);
        PaintDiagram(diagram);
    }

    OpenPurchaseDialog(1, -price, 1, mech->FileName, title, picture,
                       [this](int32_t result, int32_t) { OnSellConfirmed(result); });
}

auto MCMechInventoryBlock::OnSellConfirmed(int32_t result) const -> void
{
    std::shared_ptr<MCLogMech> sold = Mech();

    if (result == 0)
    {
        sold->Assigned = false;
        GlobalLog()->ReorderMechs();
        MCPurchaseScreen::CreateMechInvBlock();
        GlobalLog()->PurchaseScreen->SetUpMechInv(false, true);
        return;
    }

    MCInventoryList& spares = *GlobalLog()->ComponentInventory;

    for (const std::shared_ptr<MCLogInventoryItem>& item : sold->Inventory->Items)
    {
        uint8_t masterID = item->MasterID;
        MCComponentForm form = MasterComponentList[masterID].Form;

        if (!IsWeapon(form) && !IsEquipment(form) && form != MCComponentForm::Jammer)
        {
            continue;
        }

        // Every undamaged copy goes back to the spare components.
        for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
        {
            if (stat->Hits != 0)
            {
                continue;
            }

            std::shared_ptr<MCLogInventoryItem> stockItem = spares.GetItemInfo(spares.GetIndexFromMasterID(masterID));

            if (stockItem == nullptr)
            {
                stockItem = MCCompInventoryBlock::AddSpare(masterID);
            }

            ++stockItem->Count;
        }
    }

    MCPurchaseScreen::CreateCompInvBlock();
    int32_t index = GlobalLog()->ForceMechList->GetMechIndex(sold);
    GlobalLog()->ForceMechList->RemoveMech(static_cast<uint8_t>(index));
    GlobalLog()->ReorderMechs();
    ResourcePoints -= GlobalLog()->PurchaseDialog->UnitCost;
}

auto MCMechInventoryBlock::HandleEvent(MCGuiEvent& event) -> void
{
    if (!PreHandleEvent(MechDrag, event))
    {
        return;
    }

    std::shared_ptr<MCLogInvScreen> screen = Screen();

    if (MechDrag.Idle())
    {
        // The info block: the row's diagram, tonnage, classes, speed and description.
        MCLogInvScreen::DrawBlankInvInfoBlock(-1);
        PrepareInfoDescription(Mech()->Description);
        screen->ShowInfo(MCInvInfoBox::Kind::Mech, weak_from_this());

        if (event.Type == 1)
        {
            // Left button down: drag the mech (it joins the force while dragged).
            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            MechDrag.Dragging = true;
            MakeDragIcon(MechDrag, event);
            Mech()->Assigned = true;
            GlobalLog()->ReorderMechs();
            MCLogInvScreen::CreateMechInvBlock();
            screen->SetUpMechInv(false, false);
            MCDragIcon::Current()->Raise();
        }
    }

    // The force is full at 16 units.
    auto forceFull = []
    { return GlobalLog()->ForceMechList->GetMechCount() + GlobalLog()->ForceVehicleList->GetVehicleCount() > 0xf; };

    // Back to the inventory.
    auto backToInventory = [&]
    {
        Mech()->Assigned = false;
        GlobalLog()->ReorderMechs();
        MCLogInvScreen::CreateMechInvBlock();
        screen->SetUpMechInv(false, true);
    };

    // Into the force on the repair screen.
    auto joinForce = [&]
    {
        BumpDeploySlots(false);
        GlobalLog()->RepairScreen->UnitPane->AddChild(Mech()->RepairBlock);
        GlobalLog()->RepairScreen->AddMechToList(*Mech());
        GlobalLog()->RepairScreen->SelectMech(Mech());
    };

    switch (event.Type)
    {
        case 3:
        {
            // Right button down: pick the mech up.
            if (MechDrag.Dragging)
            {
                break;
            }

            MechDrag.Carrying = true;
            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            MakeDragIcon(MechDrag, event);
            MCDragIcon::Current()->Raise();
            break;
        }

        case 4:
        {
            // Left button up: drop the dragged mech.
            if (MechDrag.Carrying || !MechDrag.Dragging)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            MechDrag.Dragging = false;
            MCDragIcon::Remove();
            uint32_t sample = 0x33;

            if (OnRepairScreen())
            {
                DrawDropArt(*screen, 0);

                if (OverPaneInside(*screen->UnitPane, event))
                {
                    if (forceFull())
                    {
                        PlayLogSound(0x33);
                        backToInventory();
                        ShowLogMessage(0x285, false);
                    }
                    else
                    {
                        PlayLogSound(0x34);
                        joinForce();
                    }
                    break;
                }

                if (OverPaneInside(*screen->InventoryPane, event))
                {
                    sample = 0x34;
                }
            }
            else
            {
                if (OverPaneInside(*screen->UnitPane, event))
                {
                    // Dropped on the store: offer to sell it.
                    PlayLogSound(0x34);
                    OfferSale();
                    break;
                }

                if (OverPaneInside(*screen->InventoryPane, event))
                {
                    sample = 0x34;
                }
            }

            PlayLogSound(sample);
            backToInventory();
            break;
        }

        case 6:
        {
            // Right button up: put down the carried mech.
            if (GuiSystem().GrabbedObject() == nullptr || MechDrag.Dragging)
            {
                break;
            }

            PlayLogSound(0x34);
            MechDrag.Carrying = false;
            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            MCDragIcon::Remove();
            Mech()->Assigned = true;
            GlobalLog()->ReorderMechs();
            MCLogInvScreen::CreateMechInvBlock();
            screen->SetUpMechInv(false, true);

            if (OnRepairScreen())
            {
                if (!forceFull())
                {
                    DrawDropArt(*screen, 0);
                    joinForce();
                }
                else
                {
                    PlayLogSound(0x33);
                    backToInventory();
                    ShowLogMessage(0x285, true);
                }
                break;
            }

            OfferSale();
            break;
        }

        case 7:
        {
            // Mouse move: the dragged icon follows; otherwise the ticker shows the row's help.
            if (!MechDrag.Dragging)
            {
                if (event.Key == 0)
                {
                    GlobalLog()->Ticker->SetString(LoadGameString(OnPurchaseScreen() ? 0x2d : 0x31, 0xfe));
                }
            }
            else
            {
                MechDrag.X = event.X - 0xf;
                MechDrag.Y = event.Y - 0xf;
                MCDragIcon::Current()->MoveTo(MechDrag.X, MechDrag.Y, false);
            }
            break;
        }

        default:
            break;
    }
}
