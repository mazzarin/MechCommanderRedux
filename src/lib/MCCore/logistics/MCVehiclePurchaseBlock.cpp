#include "stdafx.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCStoreRow.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The vehicle rows' drag.</summary>
    MCDragState VehicleDrag;
}

auto MCVehiclePurchaseBlock::Init(std::shared_ptr<MCPurVehicle> newPurVehicle) -> void
{
    PicturePort.reset();
    _PurVehicle = newPurVehicle;
    MCLogObject::InitWithoutPort(0, 0, 0x19a, 0x70);
    const MCPurVehicleData& data = *PurVehicle()->Data;
    WeightClassText = LoadGameString(WeightClassString(data.CurTonnage), 0xf);
    ArmorText = LoadGameString(ArmorClassString(data.ArmorTonnage), 0xf);
}

auto MCVehiclePurchaseBlock::OnDestroy() -> void
{
    _PurVehicle.reset();
    WeightClassText.clear();
    ArmorText.clear();
    PicturePort.reset();
    MCListRow::OnDestroy();
}

auto MCVehiclePurchaseBlock::OnBuyConfirmed(MCPurVehicle& purVehicle, int32_t result, int32_t quantity) -> void
{
    if (result == 0)
    {
        return;
    }

    MCPurVehicleData& data = *purVehicle.Data;

    for (int32_t count = quantity; count > 0; --count)
    {
        GlobalLog()->VehicleList->AddVehicle(data.FileName.data(), false, true, true);
    }

    GlobalLog()->ReorderVehicles();
    MCPurchaseScreen::CreateVhclInvBlock();
    GlobalLog()->PurchaseScreen->SetUpVhclInv(true, true);
    data.NumAvailable -= quantity;
    MCLogInvScreen::RefreshStoreRow(purVehicle);
    ResourcePoints -= GlobalLog()->PurchaseDialog->UnitCost * quantity;
    CheckNumUnits();
}

auto MCVehiclePurchaseBlock::HandleEvent(MCGuiEvent& event) -> void
{
    int32_t localX = event.X - GlobalX();
    int32_t localY = event.Y - GlobalY();

    if (!MCStoreRow::PreHandleEvent(*this, VehicleDrag, event))
    {
        return;
    }

    int32_t type = event.Type;

    switch (type)
    {
        case 1:
        {
            if (VehicleDrag.Carrying)
            {
                return;
            }

            [[fallthrough]];
        }
        case 3:
        {
            if ((VehicleDrag.Dragging && type == 3) || CheckMaxUnits())
            {
                return;
            }

            if ((localX < 0x94 || localX > 0xc9 || localY < 5 || localY > 0x15) && OnRow(*this, event) &&
                PurVehicle()->Data->NumAvailable != 0)
            {
                // Pick the vehicle up (left button drags, right button carries).
                (type == 1 ? VehicleDrag.Dragging : VehicleDrag.Carrying) = true;
                PlayLogSound(0x35);
                GuiSystem().SetCursorVisible(false);
                GuiSystem().Grab(weak_from_this());
                VehicleDrag.X = event.X - 0x10;
                VehicleDrag.Y = event.Y - 0x10;
                MCStoreRow::MakeDragIcon(VehicleDrag, [this](MCLogPort& surface) { OnBeginDrag(surface); });
                return;
            }
            break;
        }

        case 4:
        {
            if (VehicleDrag.Carrying)
            {
                return;
            }

            [[fallthrough]];
        }
        case 6:
        {
            if (VehicleDrag.Dragging && type == 6)
            {
                return;
            }

            VehicleDrag.Carrying = false;

            if (GuiSystem().GrabbedObject() == nullptr)
            {
                return;
            }

            GuiSystem().Release();
            GuiSystem().SetCursorVisible(true);
            VehicleDrag.Dragging = false;
            MCDragIcon::Remove();

            if (type != 6 && !MCStoreRow::OverInventory(event))
            {
                if (MCStoreRow::OverStore(event))
                {
                    PlayLogSound(0x34);
                    return;
                }
                break;
            }

            // Buy it.
            MCPurVehicleData& data = *PurVehicle()->Data;

            if (data.Cost > ResourcePoints)
            {
                PlayLogSound(0x33);
                ShowLogMessage(0x4d);
                return;
            }

            PlayLogSound(0x34);
            OpenPurchaseDialog(6, data.Cost, MaxPurchase(data.NumAvailable), data.Name, {}, MCOptionalOf(PicturePort),
                               [weak = _PurVehicle](int32_t result, int32_t quantity)
                               {
                                   if (const std::shared_ptr<MCPurVehicle> bought = weak.lock(); bought != nullptr)
                                   {
                                       OnBuyConfirmed(*bought, result, quantity);
                                   }
                               });
            return;
        }

        case 7:
        {
            if (VehicleDrag.Dragging)
            {
                VehicleDrag.Y = event.Y - 0xf;
                VehicleDrag.X = event.X - 0xf;
                MCDragIcon::Current()->MoveTo(VehicleDrag.X, VehicleDrag.Y, false);
                return;
            }

            if (event.Key == 0)
            {
                GlobalLog()->Ticker->SetString(LoadGameString(0x30, 0xfe));
            }

            return;
        }

        default:
            return;
    }

    PlayLogSound(0x33);
}

auto MCVehiclePurchaseBlock::OnRefresh() -> void
{
    MCPurVehicleData& data = *PurVehicle()->Data;

    if (data.NumAvailable != 0)
    {
        // The diagram, kept for the purchase dialog (the original parked the row's picture in picturePort first).
        PicturePort = std::make_shared<MCLogPort>();
        PicturePort->Init(0x1e, 0x1e);
        VfxPaneWipe(PicturePort->Frame(), 0x10);

        for (int32_t location = 0; location < 5; ++location)
        {
            AGShapeDraw(PicturePort->Frame(), GlobalLog()->VehicleIconShapes[data.NameIndex].Bytes(), location, 4, 0);
        }
    }

    PrepareInfoDescription(data.Description);
}

auto MCVehiclePurchaseBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    MCPurchaseScreen& screen = *GlobalLog()->PurchaseScreen;
    std::shared_ptr<MCLogBlockPort> work = RowPicture(*screen.VehicleTabPort, port, top, false);
    MCPurVehicleData& data = *PurVehicle()->Data;
    WriteText(*YellowDropFont, *work, 0x52, 0x24,
              std::format("{:.0f} {}", data.CurTonnage, LoadGameString(0x6e, 0xfe)));
    WriteText(*YellowDropFont, *work, 0x52, 0x2d, WeightClassText);
    WriteText(*YellowDropFont, *work, 0xa7, 0x24, ArmorText);
    WriteText(*YellowDropFont, *work, 0x52, 0x36, std::format("{} m/s", data.MaxMoveSpeed));
    DrawInventoryList(*data.Inventory, *work);
    std::string stock;

    if (data.NumAvailable == 0)
    {
        // Sold out: the "sold out" name art, a blank picture and the sold-out mark.
        CopyArt(*work, 5, 4, std::format("lspfdv{:02}.tga", data.NameIndex));
        WipeBox(*work, 0xed, 6, 0x4b, 100, 0x10);
        AGShapeDraw(work->Frame(), GlobalLog()->VehicleRepShapes[data.NameIndex].Bytes(), 6, 0xed, 6);
        stock = "0";
    }
    else
    {
        int32_t index = data.NameIndex;
        CopyArt(*work, 5, 4, std::format("lspflv{:02}.tga", index));
        // The picture.
        MCLogBlockPort picture(work->Frame(), 0xed, 6, 0x4b, 100, true);
        VfxPaneWipe(picture.Frame(), 0x10);
        VfxShapeLookaside(GlobalLog()->ShapeLookaside[0]);

        for (int32_t shape = 0; shape < 5; ++shape)
        {
            VfxShapeTranslateDraw(picture.Frame(), GlobalLog()->VehicleRepShapes[index].Bytes(), shape, 0, 0);
        }

        for (int32_t location = 0; location < 5; ++location)
        {
            AGShapeDraw(work->Frame(), GlobalLog()->VehicleIconShapes[index].Bytes(), location, 9, 0x22);
        }

        // A negative stock shows the "unlimited" mark.
        stock = data.NumAvailable < 1 ? LoadGameString(0x385, 0xfe) : std::format("{}", data.NumAvailable);
    }

    WriteText(*YellowDropFont, *work, 0x25, 0x12, stock);
    WriteText(*YellowDropFont, *work, 0x52, 0x12, std::format("{}", data.Cost));
    DrawInfoDescription(*work, 0xc6, 0x26, data.Description, 6, 0x43);
}

auto MCVehiclePurchaseBlock::OnBeginDrag(MCLogPort& surface) -> void
{
    // The square at (6, 0x21) of the row, over the store's colour 0x10.
    VfxPaneWipe(surface.Frame(), 0x10);
    MCDragIcon::DrawFrom(surface, 6, 0x21, [this](MCLogPort& port) { DrawRow(port, 0); });
}
