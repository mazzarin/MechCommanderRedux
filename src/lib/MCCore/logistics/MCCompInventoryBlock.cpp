#include "stdafx.h"
#include "logistics/MCCompInventoryBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScrollPane.h"
#include "main/MCGamePaths.h"
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
    /// <summary>The component rows' drag.</summary>
    MCDragState CompDrag;

    /// <summary>The string table index of the range word of a weapon with long range <paramref name="range"/>.</summary>
    uint32_t RangeWord(float range)
    {
        return range < 76.0f ? 0x55 : range < 151.0f ? 0x50 : 0x6d;
    }

    /// <summary>
    /// Mounts the dragged component on <paramref name="mech"/> (the one selected on the repair screen): undeploys it,
    /// and when it has the free tonnage adds the component (and a weapon's ammo) to its inventory.
    /// </summary>
    /// <returns>True when mounted; false after showing the "too heavy" message.</returns>
    bool MountComponent(MCLogInventoryItem& item, MCLogMech& mech)
    {
        if (mech.Deployed != 0)
        {
            GlobalLog()->UndeployMech(mech);
        }

        uint8_t masterID = item.MasterID;
        MCMasterComponent& component = MasterComponentList[masterID];
        bool withAmmo = UsesAmmo(masterID);
        double tons = component.Tonnage;

        if (withAmmo)
        {
            tons += MasterComponentList[component.AmmoMasterId].Tonnage;
        }

        if (tons > static_cast<double>(mech.CurTonnage) - mech.UsedTonnage)
        {
            ShowLogMessage(99, true);
            return false;
        }

        PlayLogSound(0x34);
        mech.Inventory->AddItem(masterID, mech.Inventory->CreateStat(masterID, 0, 1, 1, 0xff), false);

        if (withAmmo)
        {
            mech.Inventory->AddItem(component.AmmoMasterId, mech.Inventory->CreateStat(masterID, 0, 0, -1, 0xff),
                                    false);
        }

        float added = static_cast<float>(tons);
        mech.UsedTonnage += added;
        mech.WeaponTonnage += added;

        if (const std::shared_ptr<MCMechRepairBlock> row = GlobalLog()->RepairScreen->RowOf(mech); row != nullptr)
        {
            row->SetInventory(std::nullopt);
        }

        mech.CalcBR();
        GlobalLog()->RepairScreen->SelectMech(mech.SharedAs<MCLogMech>());
        return true;
    }
}

auto MCCompInventoryBlock::MountTonnage(uint8_t masterID) -> float
{
    const MCMasterComponent& component = MasterComponentList[masterID];
    float tonnage = component.Tonnage;

    if (UsesAmmo(masterID))
    {
        tonnage = MasterComponentList[component.AmmoMasterId].Tonnage + tonnage;
    }

    return tonnage;
}

auto MCCompInventoryBlock::Texts(uint8_t masterID) -> MCComponentTexts
{
    const MCMasterComponent& component = MasterComponentList[masterID];
    const MCComponentForm form = component.Form;
    MCComponentTexts texts;
    texts.Weight = std::format("{:.1f} {}", MountTonnage(masterID), LoadGameString(0x6e, 0xfe));

    if (IsWeapon(form))
    {
        // Weapons: the long range as a word, damage and recycle time.
        texts.Range = LoadGameString(RangeWord(component.WeaponRange[3]), 0xfe);
        double damage = component.Damage;

        if (component.WeaponFlags == 4)
        {
            damage *= 3.0;
        }

        texts.Damage = std::format("{:.2f}", damage);
        texts.Recycle = std::format("{:.2f} s", component.RecycleTime);
        return texts;
    }

    std::string notApplicable = LoadGameString(0x6c, 0xfe);

    if (form == MCComponentForm::Probe)
    {
        texts.Range = notApplicable;
    }
    else
    {
        // Original behaviour (OB-078): other equipment than ECM and sensors formats the item pointer's bits as the
        // range; for a heap address that is a denormal, so it read "0.0 m".
        const float shown =
            form == MCComponentForm::Ecm || form == MCComponentForm::Sensor ? component.RangeOrHeat : 0.0f;
        texts.Range = std::format("{:.1f} m", shown);
    }

    texts.Damage = notApplicable;
    texts.Recycle = std::move(notApplicable);
    return texts;
}

auto MCCompInventoryBlock::Init(std::shared_ptr<MCLogInventoryItem> newItem) -> void
{
    _Item = newItem;
    InitRow();
    const MCMasterComponent& component = MasterComponentList[Item()->MasterID];
    Tonnage = MountTonnage(Item()->MasterID);
    // The range colour: a weapon's long range, 0 for anything else.
    const float range = IsWeapon(component.Form) ? component.WeaponRange[3] : 0.0f;

    // The icon: the range colour's block background, name, a caption and the component picture.
    const std::string_view background = range < 76.0f ? "greeninv.tga" : range < 151.0f ? "blueinv.tga" : "redinv.tga";
    auto own = std::make_shared<MCLogPort>();
    own->Load(std::format("{}logart\\{}", ArtPath, background));
    SetOwnPort(std::move(own));
    IconArt = Icon{std::string(background), std::format("lscicc{:02}.tga", Item()->RangeIndex), component.Name,
                   LoadGameString(0x37d, 0xfe), std::nullopt};

    if (component.TechBase == 1)
    {
        // Clan technology: a small mark in the range colour.
        const float longRange = component.WeaponRange[3];
        IconArt.ClanMark = longRange < 76.0f ? 0xe : longRange < 151.0f ? 0xe5 : 0xee;
    }
}

auto MCCompInventoryBlock::OnDestroy() -> void
{
    _Item.reset();
    IconArt = {};
    MCInventoryBlock::OnDestroy();
}

auto MCCompInventoryBlock::CanMount(uint8_t masterID, float tonnage, MCOptionalRef<const MCLogMech> selected) -> bool
{
    if (!selected.has_value())
    {
        return false;
    }

    const MCLogMech& mech = *selected;

    if (static_cast<double>(mech.CurTonnage) - mech.UsedTonnage < tonnage)
    {
        return false;
    }

    // One ECM, sensor or probe per mech.
    const MCComponentForm form = ComponentForm(masterID);

    if (IsEquipment(form))
    {
        for (const std::shared_ptr<MCLogInventoryItem>& mounted : mech.Inventory->Items)
        {
            if (form == ComponentForm(mounted->MasterID))
            {
                return false;
            }
        }
    }

    // Some components only fit the mechs of name index 5, 0xe and 0x10.
    const bool restricted = std::ranges::contains(RestrictedComps, masterID);
    return !restricted || mech.NameIndex == 0xe || mech.NameIndex == 0x10 || mech.NameIndex == 5;
}

auto MCCompInventoryBlock::AddSpare(uint8_t masterID) -> std::shared_ptr<MCLogInventoryItem>
{
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;
    spares.AddItem(masterID, spares.CreateStat(masterID, 0, 1, 0, 0xff), false);
    std::shared_ptr<MCLogInventoryItem> item = spares.GetItemInfo(spares.GetIndexFromMasterID(masterID));
    MCInventoryList::ListAsSpare(*item);
    item->InventoryIndex = spares.NumItems() - 1;
    return item;
}

auto MCCompInventoryBlock::OfferSale() -> void
{
    const std::shared_ptr<MCLogInventoryItem> item = Item();
    PlayLogSound(0x34);
    MCLogPort picture;
    picture.Load(std::format("{}logart\\lscicc{:02}.tga", ArtPath, item->RangeIndex));
    const MCMasterComponent& component = MasterComponentList[item->MasterID];
    int32_t price = SalePrice(component.ResourcePoints);
    // The answer goes to the item, not this row (the list can drop the row while the dialog is open); the dialog keeps
    // its callback, so it refers to the item without keeping it.
    OpenPurchaseDialog(5, -price, item->Count, component.Name, {}, picture,
                       [weak = std::weak_ptr<MCLogInventoryItem>(item)](int32_t result, int32_t quantity)
                       {
                           if (const std::shared_ptr<MCLogInventoryItem> sold = weak.lock(); sold != nullptr)
                           {
                               OnSellConfirmed(*sold, result, quantity);
                           }
                       });
}

auto MCCompInventoryBlock::OnSellConfirmed(MCLogInventoryItem& sold, int32_t result, int32_t quantity) -> void
{
    if (result == 0)
    {
        return;
    }

    sold.Count -= quantity;

    if (sold.Count == 0)
    {
        MCPurchaseScreen::CreateCompInvBlock();
        GlobalLog()->PurchaseScreen->SetUpCompInv(false, true);
    }
    else
    {
        MCLogInvScreen::RefreshComponentRows(sold);
    }

    ResourcePoints -= GlobalLog()->PurchaseDialog->UnitCost * quantity;
}

auto MCCompInventoryBlock::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCLogInventoryItem> item = Item();

    if (!PreHandleEvent(CompDrag, event))
    {
        return;
    }

    std::shared_ptr<MCLogInvScreen> screen = Screen();

    if (CompDrag.Idle())
    {
        // The info block: picture, range, damage, recycle time and description.
        MCLogInvScreen::DrawBlankInvInfoBlock(-1);
        PrepareInfoDescription(item->Description);
        screen->ShowComponentInfo(*item, false);
    }

    // Where a drop that didn't mount or sell ends: the sound, then the copy goes back to the row.
    auto returnToInventory = [&](uint32_t sample)
    {
        PlayLogSound(sample);

        if (!OnPurchaseScreen() && !CompDrag.Carrying)
        {
            ++item->Count;
        }

        if (item->Count > 1)
        {
            MCLogInvScreen::RefreshComponentRows(*item);
        }
        else
        {
            MCLogInvScreen::CreateCompInvBlock();
            screen->SetUpCompInv(false, true);
        }

        CompDrag.Carrying = false;
    };

    switch (event.Type)
    {
        case 1:
        {
            // Left button down: drag one copy (off the row on the repair screen).
            if (CantMount || CompDrag.Carrying)
            {
                break;
            }

            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            CompDrag.Dragging = true;
            MakeDragIcon(CompDrag, event);

            if (screen != GlobalLog()->PurchaseScreen)
            {
                if (--item->Count != 0)
                {
                    MCLogInvScreen::RefreshComponentRows(*item);
                }
                else
                {
                    MCLogInvScreen::CreateCompInvBlock();
                    screen->SetUpCompInv(false, false);
                }
            }

            MCDragIcon::Current()->Raise();
            break;
        }

        case 3:
        {
            // Right button down: pick one copy up.
            if (CantMount || CompDrag.Dragging)
            {
                break;
            }

            CompDrag.Carrying = true;
            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            MakeDragIcon(CompDrag, event);

            if (screen != GlobalLog()->PurchaseScreen && --item->Count != 0)
            {
                MCLogInvScreen::RefreshComponentRows(*item);
            }

            MCDragIcon::Current()->Raise();
            break;
        }

        case 4:
        {
            // Left button up: drop the dragged copy.
            if (!CompDrag.Dragging)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            CompDrag.Dragging = false;
            MCDragIcon::Remove();

            if (OnRepairScreen())
            {
                DrawDropArt(*screen, 2);
                std::shared_ptr<MCScrollPane> unitPane = screen->UnitPane.Lock();

                if (OverPaneInside(*unitPane, event))
                {
                    // Onto a mech: it must be the selected one.
                    int32_t index = (event.Y - unitPane->GlobalY() + unitPane->GetScrollOffset()) / 0x70;

                    if (index < unitPane->NumberOfChildren() && index < GlobalLog()->ForceMechList->GetMechCount())
                    {
                        std::shared_ptr<MCLogMech> target;
                        GlobalLog()->ForceMechList->GetMechInfo(index, target);

                        if (target != nullptr && target == GlobalLog()->RepairScreen->SelectedMech() &&
                            MountComponent(*item, *target))
                        {
                            break;
                        }
                    }

                    returnToInventory(0x33);
                    break;
                }

                returnToInventory(
                    OverPaneInside(*screen->InventoryPane, *screen->InventoryPane, *screen->UnitPane, event) ? 0x34
                                                                                                             : 0x33);
                break;
            }

            if (OverPaneInside(*screen->UnitPane, event))
            {
                OfferSale();
                break;
            }

            returnToInventory(
                OverPaneInside(*screen->InventoryPane, *screen->UnitPane, *screen->UnitPane, event) ? 0x34 : 0x33);
            break;
        }

        case 6:
        {
            // Right button up: put down the carried copy.
            if (!CompDrag.Carrying)
            {
                break;
            }

            GuiSystem().SetCursorVisible(true);
            GuiSystem().Release();
            MCDragIcon::Remove();
            MCLogInvScreen::CreateCompInvBlock();
            screen->SetUpCompInv(false, true);

            if (OnRepairScreen())
            {
                // Onto the selected mech.
                // Original behaviour (OB-079): when it can't be mounted, the copy taken off the row at the pick-up is
                // not given back (the count is only restored while nothing is carried), so it is lost.
                std::shared_ptr<MCLogMech> selected = GlobalLog()->RepairScreen->SelectedMech();

                if (selected != nullptr && MountComponent(*item, *selected))
                {
                    CompDrag.Carrying = false;
                    break;
                }

                returnToInventory(0x33);
                break;
            }

            OfferSale();
            CompDrag.Carrying = false;
            break;
        }

        case 7:
        {
            // Mouse move: the dragged icon follows (a carried one only updates its position); otherwise the ticker
            // shows the row's help.
            if (CompDrag.Dragging)
            {
                CompDrag.Y = event.Y - 0xf;
                CompDrag.X = event.X - 0xf;
                MCDragIcon::Current()->MoveTo(CompDrag.X, CompDrag.Y, false);
            }
            else if (CompDrag.Carrying)
            {
                CompDrag.X = event.X - 0xf;
                CompDrag.Y = event.Y - 0xf;
            }
            else if (event.Key == 0)
            {
                GlobalLog()->Ticker->SetString(LoadGameString(OnPurchaseScreen() ? 0x2d : 0x32, 0xfe));
            }
            break;
        }

        default:
            break;
    }
}

auto MCCompInventoryBlock::ListIndex() const -> int32_t
{
    const std::shared_ptr<MCLogInventoryItem> item = Item();
    return item != nullptr ? item->ListIndex : -1;
}

auto MCCompInventoryBlock::OnRefresh() -> void
{
    if (ListIndex() < 0)
    {
        ShowGuiWindow(false);
        return;
    }

    ShowGuiWindow(true);
    CantMount = !OnPurchaseScreen() &&
                !CanMount(Item()->MasterID, Tonnage, MCOptionalOf(GlobalLog()->RepairScreen->SelectedMech()));
}

auto MCCompInventoryBlock::DrawIcon(MCLogPort& port) const -> void
{
    WriteText(*YellowDropFont, port, 0x26, 7, IconArt.Name);
    WriteText(*BlueDropFont, port, 0x26, 0x15, IconArt.Caption);
    LogScreenArt(IconArt.Picture)->CopyTo(port.Frame(), 3, 2, true);

    if (IconArt.ClanMark.has_value())
    {
        const int32_t color = *IconArt.ClanMark;
        MCPane& frame = port.Frame();
        VfxLineDraw(frame, 5, 7, 5, 8, color);
        VfxLineDraw(frame, 6, 5, 6, 8, color);
        VfxLineDraw(frame, 7, 4, 7, 8, color);
        VfxLineDraw(frame, 8, 5, 8, 8, color);
        VfxLineDraw(frame, 9, 7, 9, 8, color);
    }
}

auto MCCompInventoryBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    // Put together in place, as the original did in the block's own picture, then copied opaque; the icon too.
    std::shared_ptr<MCLogBlockPort> row = RowPicture(*LogScreenArt(IconArt.Background), port, top, false);
    DrawIcon(*row);
    WriteText(*BlueDropFont, *row, 0x67, 0x15, std::format("{}", Item()->Count));

    if (CantMount)
    {
        MCLogistics::Darken(0, LogisticFadetable(), *row);
    }
}
