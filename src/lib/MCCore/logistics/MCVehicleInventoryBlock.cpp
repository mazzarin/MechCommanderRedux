#include "stdafx.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCVehicleRepairBlock.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The vehicle rows' drag.</summary>
    MCDragState VehicleDrag;
    /// <summary>The damage diagram's size, and the colour its picture was wiped to.</summary>
    constexpr int32_t DiagramWidth = 0x1c;
    constexpr int32_t DiagramHeight = 0x1e;
    constexpr int32_t DiagramBack = 0x10;
}

MCVehicleInventoryBlock::~MCVehicleInventoryBlock()
{
    MCVehicleInventoryBlock::Destroy();
}

auto MCVehicleInventoryBlock::Init(std::shared_ptr<MCLogVehicle> logVehicle) -> void
{
    _Vehicle = logVehicle;
    InitRow();
    ListIndex = Vehicle()->NameIndex;
    WeightClassText = LoadGameString(WeightClassString(Vehicle()->CurTonnage), 0xfe);
    ArmorText = LoadGameString(ArmorClassString(Vehicle()->ArmorTonnage), 0xf);
}

auto MCVehicleInventoryBlock::Destroy() -> void
{
    _Vehicle.reset();
    WeightClassText.clear();
    ArmorText.clear();
    PicturePort.reset();
    DiagramStates.reset();
    MCInventoryBlock::Destroy();
}

auto MCVehicleInventoryBlock::Draw() -> void
{
}

auto MCVehicleInventoryBlock::OfferSale() -> bool
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();

    if (vehicle->Required != 0)
    {
        return false;
    }

    PlayLogSound(0x34);
    int32_t price = SalePrice(vehicle->VehicleResourcePoints);
    OpenPurchaseDialog(7, -price, 1, vehicle->FileName, {}, MCOptionalOf(PicturePort),
                       [this](int32_t result, int32_t) { OnSellConfirmed(result); });
    return true;
}

auto MCVehicleInventoryBlock::OnSellConfirmed(int32_t result) const -> void
{
    std::shared_ptr<MCLogVehicle> sold = Vehicle();

    if (result == 0)
    {
        sold->Assigned = false;
        GlobalLog()->ReorderVehicles();
        MCPurchaseScreen::CreateVhclInvBlock();
        GlobalLog()->PurchaseScreen->SetUpVhclInv(false, true);
        return;
    }

    int32_t index = GlobalLog()->ForceVehicleList->GetVehicleIndex(sold);
    GlobalLog()->ForceVehicleList->RemoveVehicle(static_cast<uint8_t>(index));
    GlobalLog()->ReorderVehicles();
    ResourcePoints -= GlobalLog()->PurchaseDialog->UnitCost;
}

auto MCVehicleInventoryBlock::HandleEvent(MCGuiEvent& event) -> void
{
    if (!PreHandleEvent(VehicleDrag, event))
    {
        return;
    }

    std::shared_ptr<MCLogInvScreen> screen = Screen();

    if (VehicleDrag.Idle())
    {
        // The info block: diagram, tonnage, classes, speed and description.
        MCLogInvScreen::DrawBlankInvInfoBlock(-1);
        PrepareInfoDescription(Vehicle()->Description);
        screen->ShowInfo(MCInvInfoBox::Kind::Vehicle, weak_from_this());
    }

    auto forceFull = []
    { return GlobalLog()->ForceMechList->GetMechCount() + GlobalLog()->ForceVehicleList->GetVehicleCount() > 0xf; };

    auto backToInventory = [&]
    {
        Vehicle()->Assigned = false;
        GlobalLog()->ReorderVehicles();
        MCLogInvScreen::CreateVhclInvBlock();
        screen->SetUpVhclInv(false, true);
    };

    // Into the force on the repair screen (the vehicle bumps the drop slots' vehicle indices).
    auto joinForce = [&]
    {
        PlayLogSound(0x34);
        GlobalLog()->RepairScreen->UnitPane->AddChild(Vehicle()->RepairBlock);
        BumpDeploySlots(true);
        GlobalLog()->RepairScreen->AddVehicleToList(*Vehicle());
        GlobalLog()->RepairScreen->SelectVehicle(Vehicle());
    };

    switch (event.Type)
    {
        case 1:
        {
            if (VehicleDrag.Carrying)
            {
                break;
            }

            [[fallthrough]];
        }
        case 3:
        {
            // Left button down drags the vehicle (it joins the force while dragged); right button down picks it up.
            if (VehicleDrag.Dragging)
            {
                break;
            }

            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            MakeDragIcon(VehicleDrag, event);

            if (event.Type == 1)
            {
                VehicleDrag.Dragging = true;
                Vehicle()->Assigned = true;
                GlobalLog()->ReorderVehicles();
                MCLogInvScreen::CreateVhclInvBlock();
                screen->SetUpVhclInv(false, false);
            }
            else
            {
                VehicleDrag.Carrying = true;
            }

            MCDragIcon::Current()->Raise();
            [[fallthrough]];
        }
        case 7:
        {
            // Mouse move: the dragged icon follows; otherwise the ticker shows the row's help.
            if (!VehicleDrag.Dragging)
            {
                if (event.Key == 0)
                {
                    GlobalLog()->Ticker->SetString(LoadGameString(OnPurchaseScreen() ? 0x2d : 0x47, 0xfe));
                }
            }
            else
            {
                VehicleDrag.Y = event.Y - 0xf;
                VehicleDrag.X = event.X - 0xf;
                MCDragIcon::Current()->MoveTo(VehicleDrag.X, VehicleDrag.Y, false);
            }
            break;
        }

        case 4:
        {
            // Left button up: drop the dragged vehicle.
            if (VehicleDrag.Carrying || !VehicleDrag.Dragging)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            VehicleDrag.Dragging = false;
            MCDragIcon::Remove();

            if (OnRepairScreen())
            {
                if (forceFull())
                {
                    PlayLogSound(0x33);
                    backToInventory();
                    ShowLogMessage(0x285, true);
                    break;
                }

                DrawDropArt(*screen, 3);

                if (OverPaneInside(*screen->UnitPane, event))
                {
                    joinForce();
                    break;
                }

                PlayLogSound(OverPaneInside(*screen->InventoryPane, event) ? 0x34 : 0x33);
            }
            else if (OverPaneInside(*screen->UnitPane, event))
            {
                // Dropped on the store: offer to sell it.
                if (OfferSale())
                {
                    break;
                }

                PlayLogSound(OverPaneInside(*screen->InventoryPane, event) ? 0x34 : 0x33);
            }
            else
            {
                PlayLogSound(0x33);
            }

            backToInventory();
            break;
        }

        case 6:
        {
            // Right button up: put down the carried vehicle.
            if (VehicleDrag.Dragging)
            {
                break;
            }

            VehicleDrag.Carrying = false;

            if (GuiSystem().GrabbedObject() == nullptr)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            MCDragIcon::Remove();
            Vehicle()->Assigned = true;
            GlobalLog()->ReorderVehicles();
            MCLogInvScreen::CreateVhclInvBlock();
            screen->SetUpVhclInv(false, true);

            if (OnRepairScreen())
            {
                if (!forceFull())
                {
                    DrawDropArt(*screen, 3);
                    joinForce();
                    break;
                }

                PlayLogSound(0x33);
                backToInventory();
                ShowLogMessage(0x285, true);
                break;
            }

            if (OfferSale())
            {
                break;
            }

            PlayLogSound(0x33);
            backToInventory();
            break;
        }

        default:
            break;
    }
}

auto MCVehicleInventoryBlock::DrawBackground() -> void
{
    if (PicturePort != nullptr)
    {
        return;
    }

    std::array<std::optional<int32_t>, DiagramLocations>& states = DiagramStates.emplace();

    for (int32_t location = 0; location < DiagramLocations; ++location)
    {
        states[static_cast<size_t>(location)] = MCLogistics::VehicleBodyLocState(*Vehicle(), location);
    }

    PicturePort = std::make_shared<MCLogPort>();
    PicturePort->Init(DiagramWidth, DiagramHeight);
    PaintDiagram(*PicturePort);
}

auto MCVehicleInventoryBlock::PaintDiagram(MCLogPort& port) const -> void
{
    if (!DiagramStates.has_value())
    {
        return;
    }

    VfxPaneWipe(port.Frame(), DiagramBack);

    for (int32_t location = 0; location < DiagramLocations; ++location)
    {
        if (const std::optional<int32_t> state = (*DiagramStates)[static_cast<size_t>(location)]; state.has_value())
        {
            MCLogistics::DrawBodyLoc(GlobalLog()->VehicleIconShapes[static_cast<size_t>(Vehicle()->NameIndex)].Bytes(),
                                     location, *state, port, 0, 0);
        }
    }
}

auto MCVehicleInventoryBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    MCInventoryBlock::DrawRow(port, top);
    WriteText(*YellowDropFont, port, 0x26, top + 7, Vehicle()->FileName);
    WriteText(*BlueDropFont, port, 0x26, top + 0x15,
              MCFormatPrintf(LoadGameString(0x53, 0xfe).c_str(), static_cast<double>(Vehicle()->CurTonnage),
                             WeightClassText.c_str()));

    // The diagram, drawn in place (the original copied its picture keyed).
    if (DiagramStates.has_value())
    {
        MCLogBlockPort diagram(port.Frame(), 5, top + 2, DiagramWidth, DiagramHeight, true);
        PaintDiagram(diagram);
    }
}

auto MCVehicleInventoryBlock::DrawInfo(MCLogPort& port) -> void
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    // The 0x1e square at (3, 2) of the row (its art and the diagram), as the original copied it out of the tab's
    // picture.
    MCLogBlockPort square(port.Frame(), 9, 0x191, 0x1e, 0x1e, false);
    VfxPaneCopy(GlobalLog()->InvBlockPort->Frame(), 3, 2, square.Frame(), 0, 0, -1);

    if (DiagramStates.has_value())
    {
        MCLogBlockPort diagram(square.Frame(), 2, 0, DiagramWidth, DiagramHeight, true);
        PaintDiagram(diagram);
    }

    WriteText(*YellowDropFont, port, 0x53, 0x193,
              std::format("{:.0f} {}", vehicle->CurTonnage, LoadGameString(0x6e, 0xfe)));
    WriteText(*YellowDropFont, port, 0x53, 0x19c, WeightClassText);
    WriteText(*YellowDropFont, port, 0xa9, 0x193, ArmorText);
    WriteText(*YellowDropFont, port, 0x53, 0x1a5, std::format("{} m/s", vehicle->MaxMoveSpeed));
    DrawInfoDescription(port, 0xc3, 0x26, vehicle->Description, 8, 0x1b3);
}
