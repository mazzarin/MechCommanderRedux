#include "stdafx.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "main/MCGamePaths.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCStoreRow.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "object/MCMasterComponent.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The component rows' drag.</summary>
    MCDragState CompDrag;

}

auto MCCompPurchaseBlock::Init(std::shared_ptr<MCLogInventoryItem> newItem) -> void
{
    _Item = newItem;
    MCLogObject::InitWithoutPort(0, 0, 0x19a, 0x70);
    const MCMasterComponent& component = MasterComponentList[Item()->MasterID];
    MCComponentForm form = component.Form;
    WeightText = MCFormatPrintf(LoadGameString(0x27f, 0xfe).c_str(), static_cast<double>(component.Tonnage));

    if (!IsWeapon(form))
    {
        std::string notApplicable = LoadGameString(0x6c, 0xfe);

        if (form == MCComponentForm::Probe)
        {
            RangeText = notApplicable;
        }
        else
        {
            // Original behaviour (OB-078): other equipment than ECM and sensors formats the item pointer's bits as the
            // range, which read "0.0 m".
            const float range =
                form == MCComponentForm::Ecm || form == MCComponentForm::Sensor ? component.RangeOrHeat : 0.0f;
            RangeText = std::format("{:.1f} m", range);
        }

        DamageText = notApplicable;
        RecycleText = notApplicable;
        return;
    }

    // Weapons: range, damage and recycle time with their rating words.
    float range = component.WeaponRange[3];
    RangeText = LoadGameString(range < 76.0f ? 0x55 : range < 151.0f ? 0x50 : 0x6d, 0xfe);
    float damage = component.Damage;

    if (component.WeaponFlags == 4)
    {
        damage = static_cast<float>(damage * 3.0);
    }

    uint32_t damageId = damage < 1.0f   ? 100u
                        : damage < 3.0f ? 0x4fu
                        : damage < 5.0f ? 0x65u
                        : damage < 7.0f ? 0x51u
                        : damage < 9.0f ? 0x66u
                                        : 0x67u;
    DamageText = std::format("{:.2f} ({})", damage, LoadGameString(damageId, 0xfe));
    float recycle = component.RecycleTime;
    uint32_t recycleId = recycle < 2.0f   ? 0x68u
                         : recycle < 3.0f ? 0x69u
                         : recycle < 5.0f ? 0x65u
                         : recycle < 8.0f ? 0x6au
                                          : 0x6bu;
    RecycleText = std::format("{:.2f} s ({})", recycle, LoadGameString(recycleId, 0xfe));
}

auto MCCompPurchaseBlock::OnBuyConfirmed(MCLogInventoryItem& bought, int32_t result, int32_t quantity) -> void
{
    if (result == 0)
    {
        return;
    }

    bought.Count -= quantity;
    MCLogInvScreen::RefreshStoreRow(bought);
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;
    std::shared_ptr<MCLogInventoryItem> stockItem = spares.GetItemInfo(spares.GetIndexFromMasterID(bought.MasterID));

    if (stockItem == nullptr)
    {
        // A new spare component: its first copy and its inventory row.
        spares.AddItem(bought.MasterID, spares.CreateStat(spares.NextStatID, 0, 0, 1, 0xff), false);
        stockItem = spares.GetItemInfo(spares.GetIndexFromMasterID(bought.MasterID));
        stockItem->Count = quantity;
        MCInventoryList::ListAsSpare(*stockItem);
        stockItem->InventoryIndex = spares.NumItems() - 1;
        MCPurchaseScreen::CreateCompInvBlock();
        GlobalLog()->PurchaseScreen->SetUpCompInv(false, true);
    }
    else if (stockItem->Count == 0)
    {
        stockItem->Count = quantity;
        MCPurchaseScreen::CreateCompInvBlock();
        GlobalLog()->PurchaseScreen->SetUpCompInv(false, true);
    }
    else
    {
        stockItem->Count += quantity;
        MCLogInvScreen::RefreshComponentRows(*stockItem);
    }

    ResourcePoints -= GlobalLog()->PurchaseDialog->UnitCost * quantity;
}

auto MCCompPurchaseBlock::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCLogInventoryItem> item = Item();

    if (!MCStoreRow::PreHandleEvent(*this, CompDrag, event))
    {
        return;
    }

    int32_t type = event.Type;

    switch (type)
    {
        case 1:
        case 3:
        {
            if (!CompDrag.Idle())
            {
                return;
            }

            if (OnRow(*this, event) && item->Count != 0)
            {
                // Pick the component up (left button drags, right button carries). The cursor stays shown.
                (type == 1 ? CompDrag.Dragging : CompDrag.Carrying) = true;
                PlayLogSound(0x35);
                GuiSystem().SetCursorVisible(true);
                GuiSystem().Grab(weak_from_this());
                CompDrag.Y = event.Y - 0x10;
                CompDrag.X = event.X - 0x10;
                MCStoreRow::MakeDragIcon(CompDrag, [this](MCLogPort& surface) { OnBeginDrag(surface); });
                return;
            }
            break;
        }

        case 4:
        {
            if (CompDrag.Carrying)
            {
                return;
            }

            [[fallthrough]];
        }
        case 6:
        {
            if (CompDrag.Dragging && type == 6)
            {
                return;
            }

            if (GuiSystem().GrabbedObject() == nullptr)
            {
                return;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            CompDrag.Dragging = false;
            CompDrag.Carrying = false;
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

            // Buy some.
            const MCMasterComponent& component = MasterComponentList[item->MasterID];

            if (component.ResourcePoints <= ResourcePoints)
            {
                PlayLogSound(0x34);
                MCLogPort picture;
                picture.Load(std::format("{}logart\\lscicc{:02}.tga", ArtPath, item->RangeIndex));
                OpenPurchaseDialog(4, component.ResourcePoints, item->Count, component.Name, {}, picture,
                                   [weak = _Item](int32_t result, int32_t quantity)
                                   {
                                       if (const std::shared_ptr<MCLogInventoryItem> bought = weak.lock();
                                           bought != nullptr)
                                       {
                                           OnBuyConfirmed(*bought, result, quantity);
                                       }
                                   });
                return;
            }

            ShowLogMessage(0x4d);
            break;
        }

        case 7:
        {
            if (CompDrag.Dragging)
            {
                CompDrag.Y = event.Y - 0xf;
                CompDrag.X = event.X - 0xf;
                MCDragIcon::Current()->MoveTo(CompDrag.X, CompDrag.Y, false);
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

auto MCCompPurchaseBlock::OnRefresh() -> void
{
    PrepareInfoDescription(Item()->Description);
}

auto MCCompPurchaseBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    const std::shared_ptr<MCLogInventoryItem> item = Item();
    MCPurchaseScreen& screen = *GlobalLog()->PurchaseScreen;
    std::shared_ptr<MCLogBlockPort> work = RowPicture(*screen.CompTabPort, port, top, true);
    const std::string stock = item->Count < 0 ? MCFormatPrintf(LoadGameString(0x385, 0xfe).c_str(), item->Count)
                                              : std::format("{}", item->Count);
    WriteText(*YellowDropFont, *work, 0x26, 0x12, stock);
    WriteText(*YellowDropFont, *work, 0x52, 0x12,
              std::format("{}", MasterComponentList[item->MasterID].ResourcePoints));
    const int32_t picture = item->RangeIndex;

    if (item->Count == 0)
    {
        // Sold out: a blank icon, the "sold out" picture and name art.
        WipeBox(*work, 7, 0x22, 0x1e, 0x1e, 0x10);
        CopyArt(*work, 0xed, 6, std::format("lspidc{:02}.tga", picture));
        CopyArt(*work, 5, 4, std::format("lspfdc{:02}.tga", picture));
    }
    else
    {
        CopyArt(*work, 7, 0x22, std::format("lscicc{:02}.tga", picture));
        CopyArt(*work, 0xed, 6, std::format("lspilc{:02}.tga", picture));
        CopyArt(*work, 5, 4, std::format("lspflc{:02}.tga", picture));
    }

    WriteText(*YellowDropFont, *work, 0x52, 0x35, RangeText);
    WriteText(*YellowDropFont, *work, 0x52, 0x2c, DamageText);
    WriteText(*YellowDropFont, *work, 0x52, 0x23, RecycleText);
    DrawInfoDescription(*work, 0xc6, 0x25, item->Description, 6, 0x44);
}

auto MCCompPurchaseBlock::OnBeginDrag(MCLogPort& surface) -> void
{
    // The square at (6, 0x21) of the row, over the store's colour 0x10.
    VfxPaneWipe(surface.Frame(), 0x10);
    MCDragIcon::DrawFrom(surface, 6, 0x21, [this](MCLogPort& port) { DrawRow(port, 0); });
}
