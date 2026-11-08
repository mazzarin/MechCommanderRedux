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

auto MCVehicleInventoryBlock::Init(std::shared_ptr<MCLogVehicle> logVehicle) -> void
{
    _Vehicle = logVehicle;
    InitRow();
}

auto MCVehicleInventoryBlock::ListIndex() const -> int32_t
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    return vehicle != nullptr ? vehicle->ListIndex : -1;
}

auto MCVehicleInventoryBlock::OnDestroy() -> void
{
    _Vehicle.reset();
    PicturePort.reset();
    DiagramStates.reset();
    MCInventoryBlock::OnDestroy();
}

auto MCVehicleInventoryBlock::Draw() -> void
{
}

auto MCVehicleInventoryBlock::OfferSale(const std::shared_ptr<MCLogVehicle>& vehicle,
                                        const std::shared_ptr<MCLogPort>& picture) -> bool
{
    if (vehicle->Required != 0)
    {
        return false;
    }

    PlayLogSound(0x34);
    int32_t price = SalePrice(vehicle->VehicleResourcePoints);
    // The answer goes to the vehicle, not this row (the list can drop the row while the dialog is open); the dialog
    // keeps its callback, so it refers to the vehicle without keeping it.
    OpenPurchaseDialog(7, -price, 1, vehicle->FileName, {}, MCOptionalOf(picture),
                       [weak = std::weak_ptr<MCLogVehicle>(vehicle)](int32_t result, int32_t)
                       {
                           if (const std::shared_ptr<MCLogVehicle> sold = weak.lock(); sold != nullptr)
                           {
                               OnSellConfirmed(*sold, result);
                           }
                       });
    return true;
}

auto MCVehicleInventoryBlock::OnSellConfirmed(MCLogVehicle& sold, int32_t result) -> void
{
    if (result == 0)
    {
        sold.Assigned = false;
        GlobalLog()->ReorderVehicles();
        MCPurchaseScreen::CreateVhclInvBlock();
        GlobalLog()->PurchaseScreen->SetUpVhclInv(false, true);
        return;
    }

    int32_t index = GlobalLog()->ForceVehicleList->GetVehicleIndex(
        std::static_pointer_cast<const MCLogVehicle>(sold.shared_from_this()));
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

    // The vehicle and its picture, taken once for the whole event: a list refresh below drops this row once its
    // vehicle leaves the inventory (put down with the right button, the vehicle joins the force before the sale or the
    // force's listing reads it), and a dropped row has neither. A row already dropped takes no events.
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();

    if (vehicle == nullptr)
    {
        return;
    }

    const std::shared_ptr<MCLogPort> picture = PicturePort;
    std::shared_ptr<MCLogInvScreen> screen = Screen();

    if (VehicleDrag.Idle())
    {
        // The info block: diagram, tonnage, classes, speed and description.
        MCLogInvScreen::DrawBlankInvInfoBlock(-1);
        PrepareInfoDescription(vehicle->Description);
        // Shown while the vehicle lives (the box went blank with the vehicle's block before), whatever happens to the
        // row.
        screen->ShowDetails(
            MCInvInfoBox::Kind::Vehicle,
            [weak = std::weak_ptr<MCLogVehicle>(vehicle), diagram = DiagramStates](MCLogPort& port)
            {
                if (const std::shared_ptr<MCLogVehicle> shown = weak.lock(); shown != nullptr)
                {
                    DrawVehicleInfo(
                        *shown, diagram.has_value() ? MCOptionalRef<const DiagramLook>(*diagram) : std::nullopt, port);
                }
            });
    }

    auto forceFull = []
    { return GlobalLog()->ForceMechList->GetMechCount() + GlobalLog()->ForceVehicleList->GetVehicleCount() > 0xf; };

    auto backToInventory = [&]
    {
        vehicle->Assigned = false;
        GlobalLog()->ReorderVehicles();
        MCLogInvScreen::CreateVhclInvBlock();
        screen->SetUpVhclInv(false, true);
    };

    // Into the force on the repair screen (the vehicle bumps the drop slots' vehicle indices; its listing makes the
    // vehicle's row).
    auto joinForce = [&]
    {
        PlayLogSound(0x34);
        BumpDeploySlots(true);
        GlobalLog()->RepairScreen->AddVehicleToList(*vehicle);
        GlobalLog()->RepairScreen->SelectVehicle(vehicle);
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
                vehicle->Assigned = true;
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
                if (OfferSale(vehicle, picture))
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
            vehicle->Assigned = true;
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

            if (OfferSale(vehicle, picture))
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

auto MCVehicleInventoryBlock::OnRefresh() -> void
{
    if (PicturePort != nullptr)
    {
        return;
    }

    DiagramLook& states = DiagramStates.emplace();

    for (int32_t location = 0; location < DiagramLocations; ++location)
    {
        states[static_cast<size_t>(location)] = MCLogistics::VehicleBodyLocState(*Vehicle(), location);
    }

    PicturePort = std::make_shared<MCLogPort>();
    PicturePort->Init(DiagramWidth, DiagramHeight);
    PaintDiagram(*Vehicle(), states, *PicturePort);
}

auto MCVehicleInventoryBlock::PaintDiagram(const MCLogVehicle& vehicle, const DiagramLook& states, MCLogPort& port)
    -> void
{
    VfxPaneWipe(port.Frame(), DiagramBack);

    for (int32_t location = 0; location < DiagramLocations; ++location)
    {
        if (const std::optional<int32_t> state = states[static_cast<size_t>(location)]; state.has_value())
        {
            MCLogistics::DrawBodyLoc(GlobalLog()->VehicleIconShapes[static_cast<size_t>(vehicle.NameIndex)].Bytes(),
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
                             Vehicle()->WeightClassName.c_str()));

    // The diagram, drawn in place (the original copied its picture keyed).
    if (DiagramStates.has_value())
    {
        MCLogBlockPort diagram(port.Frame(), 5, top + 2, DiagramWidth, DiagramHeight, true);
        PaintDiagram(*Vehicle(), *DiagramStates, diagram);
    }
}

auto MCVehicleInventoryBlock::DrawVehicleInfo(const MCLogVehicle& vehicle, MCOptionalRef<const DiagramLook> diagram,
                                              MCLogPort& port) -> void
{
    // The 0x1e square at (3, 2) of the row (its art and the diagram), as the original copied it out of the tab's
    // picture.
    MCLogBlockPort square(port.Frame(), 9, 0x191, 0x1e, 0x1e, false);
    VfxPaneCopy(GlobalLog()->InvBlockPort->Frame(), 3, 2, square.Frame(), 0, 0, -1);

    if (diagram)
    {
        MCLogBlockPort shown(square.Frame(), 2, 0, DiagramWidth, DiagramHeight, true);
        PaintDiagram(vehicle, *diagram, shown);
    }

    WriteText(*YellowDropFont, port, 0x53, 0x193,
              std::format("{:.0f} {}", vehicle.CurTonnage, LoadGameString(0x6e, 0xfe)));
    WriteText(*YellowDropFont, port, 0x53, 0x19c, vehicle.WeightClassName);
    WriteText(*YellowDropFont, port, 0xa9, 0x193, vehicle.ArmorClassName);
    WriteText(*YellowDropFont, port, 0x53, 0x1a5, std::format("{} m/s", vehicle.MaxMoveSpeed));
    DrawInfoDescription(port, 0xc3, 0x26, vehicle.Description, 8, 0x1b3);
}
