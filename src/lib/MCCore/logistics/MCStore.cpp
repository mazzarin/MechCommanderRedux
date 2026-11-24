#include "stdafx.h"
#include "logistics/MCStore.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechBay.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGameStrings.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarrior.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"

namespace
{
    /// <summary>The resource points pay for <paramref name="each"/> (the original's drops ask for one).</summary>
    MCStore::MCRefusal Afford(int32_t each)
    {
        return ResourcePoints < each ? MCStore::MCRefusal::TooPoor : MCStore::MCRefusal::None;
    }

    /// <summary>The shown variant of <paramref name="purMech"/>.</summary>
    MCPurMechData& Shown(const MCPurMech& purMech)
    {
        return *purMech.Variants[static_cast<size_t>(purMech.CurVariant)];
    }

    /// <summary>
    /// Takes <paramref name="part"/> out of whichever of <paramref name="force"/> and <paramref name="spares"/> holds it
    /// (the original's sale found it in the force, where its drag had put it; the remade one sells a spare).
    /// </summary>
    template <typename Part, typename List, typename IndexOf, typename Remove>
    void TakeOut(const Part& part, List& force, List& spares, IndexOf indexOf, Remove remove)
    {
        const std::shared_ptr<const Part> shown = std::static_pointer_cast<const Part>(part.shared_from_this());

        if (const int32_t index = indexOf(force, shown); index >= 0)
        {
            remove(force, static_cast<uint8_t>(index));
            return;
        }

        if (const int32_t index = indexOf(spares, shown); index >= 0)
        {
            remove(spares, static_cast<uint8_t>(index));
            return;
        }

        SDL_Log("MCStore: a sold %s (%s) was in neither the force nor the spares",
                part.PartType == 1 ? "mech" : "vehicle", part.FileName.c_str());
    }

    /// <summary>The game's strings a part's texts read, and the buffer the original read them into.</summary>
    constexpr uint32_t ShortRangeString = 0x55;
    constexpr uint32_t MediumRangeString = 0x50;
    constexpr uint32_t LongRangeString = 0x6d;
    constexpr uint32_t TonsString = 0x6e;
    constexpr uint32_t NotApplicableString = 0x6c;
    constexpr int StringBuffer = 0xfe;
    /// <summary>The weapons whose damage the texts show tripled (their WeaponFlags), and by how much.</summary>
    constexpr uint8_t TripledWeaponFlags = 4;
    constexpr double DamageTripled = 3.0;

    /// <summary>The range word of a weapon in <paramref name="list"/>.</summary>
    uint32_t RangeWord(MCMechBay::MCFitList list)
    {
        return list == MCMechBay::MCFitList::ShortRange    ? ShortRangeString
               : list == MCMechBay::MCFitList::MediumRange ? MediumRangeString
                                                           : LongRangeString;
    }
}

bool MCStore::Lists(const MCLogInventoryItem& part)
{
    return part.SortOrder >= 0 && part.SortOrder < SortOrders;
}

std::vector<std::shared_ptr<MCLogInventoryItem>> MCStore::ListedParts()
{
    std::vector<std::shared_ptr<MCLogInventoryItem>> parts;

    for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->PurchaseComponents->Items)
    {
        if (Lists(*item))
        {
            parts.push_back(item);
        }
    }

    std::ranges::stable_sort(parts, {}, &MCLogInventoryItem::SortOrder);
    return parts;
}

MCStore::MCComponentTexts MCStore::ComponentTexts(uint8_t masterID)
{
    const MCMasterComponent& component = MasterComponentList[masterID];
    const MCComponentForm form = component.Form;
    MCComponentTexts texts;
    texts.Weight = std::format("{:.1f} {}", MCMechBay::FittedTons(masterID), LoadGameString(TonsString, StringBuffer));

    if (IsWeapon(form))
    {
        // Weapons: the long range as a word, damage and recycle time.
        texts.Range = LoadGameString(RangeWord(*MCMechBay::FitListOf(masterID)), StringBuffer);
        double damage = component.Damage;

        if (component.WeaponFlags == TripledWeaponFlags)
        {
            damage *= DamageTripled;
        }

        texts.Damage = std::format("{:.2f}", damage);
        texts.Recycle = std::format("{:.2f} s", component.RecycleTime);
        return texts;
    }

    std::string notApplicable = LoadGameString(NotApplicableString, StringBuffer);

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

int32_t MCStore::SalePrice(int32_t value)
{
    if (MultiPlayer() == nullptr && !Solo)
    {
        value /= 2;
    }

    return value;
}

int32_t MCStore::MostUnits(int32_t available)
{
    return MaxPurchase(available);
}

bool MCStore::PaysFor(int32_t each, int32_t quantity)
{
    return static_cast<int64_t>(each) * (quantity + 1) <= ResourcePoints;
}

MCStore::MCRefusal MCStore::WhyNotBuy(const MCPurMech& purMech)
{
    const MCPurMechData& data = Shown(purMech);
    return data.NumAvailable == 0 ? MCRefusal::SoldOut : Afford(data.Cost);
}

MCStore::MCRefusal MCStore::WhyNotHire(const MCPurPilotData& pilot)
{
    return Afford(pilot.Cost);
}

MCStore::MCRefusal MCStore::WhyNotBuy(const MCLogInventoryItem& part)
{
    // The original's drop asks the price against the points the other way round (the price at most the points).
    return part.Count == 0 ? MCRefusal::SoldOut : Afford(MasterComponentList[part.MasterID].ResourcePoints);
}

MCStore::MCRefusal MCStore::WhyNotBuy(const MCPurVehicle& purVehicle)
{
    const MCPurVehicleData& data = *purVehicle.Data;
    return data.NumAvailable == 0 ? MCRefusal::SoldOut : Afford(data.Cost);
}

int32_t MCStore::SalePrice(MCLogMech& mech)
{
    mech.CalcMechCost(false);
    return SalePrice(mech.ResourcePoints);
}

int32_t MCStore::SalePrice(const MCLogWarrior& warrior)
{
    return SalePrice(GlobalLog()->PilotCosts[warrior.Rank]);
}

int32_t MCStore::SalePrice(const MCLogInventoryItem& part)
{
    return SalePrice(MasterComponentList[part.MasterID].ResourcePoints);
}

int32_t MCStore::SalePrice(const MCLogVehicle& vehicle)
{
    return SalePrice(vehicle.VehicleResourcePoints);
}

void MCStore::BuyMechs(MCPurMech& purMech, int32_t quantity, int32_t each)
{
    MCPurMechData& data = Shown(purMech);

    for (int32_t count = quantity; count > 0; --count)
    {
        GlobalLog()->MechList->AddMech(data.FileName.data(), false, true, true);
    }

    GlobalLog()->ReorderMechs();
    data.NumAvailable -= quantity;
    ResourcePoints -= each * quantity;
}

void MCStore::HirePilot(MCPurPilotData& pilot, int32_t cost)
{
    GlobalLog()->WarriorList->AddWarrior(pilot.FileName.data(), true);
    GlobalLog()->ReorderWarriors();
    // A hired pilot (health cleared) is not drawn in the store.
    pilot.Health = 0;
    GlobalLog()->PurPilotList->SetPilotStatus(pilot.DescIndex, MCPurPilotData::Hired);
    ResourcePoints -= cost;
}

MCStore::MCPartRow MCStore::BuyParts(MCLogInventoryItem& part, int32_t quantity, int32_t each,
                                     std::shared_ptr<MCLogInventoryItem>& spare)
{
    part.Count -= quantity;
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;
    spare = spares.GetItemInfo(spares.GetIndexFromMasterID(part.MasterID));
    MCPartRow row = MCPartRow::Listed;

    if (spare == nullptr)
    {
        // A new spare component: its first copy and its inventory row.
        spares.AddItem(part.MasterID, spares.CreateStat(spares.NextStatID, 0, 0, 1, 0xff), false);
        spare = spares.GetItemInfo(spares.GetIndexFromMasterID(part.MasterID));
        spare->Count = quantity;
        MCInventoryList::ListAsSpare(*spare);
        spare->InventoryIndex = spares.NumItems() - 1;
    }
    else if (spare->Count == 0)
    {
        spare->Count = quantity;
    }
    else
    {
        spare->Count += quantity;
        row = MCPartRow::Counted;
    }

    ResourcePoints -= each * quantity;
    return row;
}

void MCStore::BuyVehicles(MCPurVehicle& purVehicle, int32_t quantity, int32_t each)
{
    MCPurVehicleData& data = *purVehicle.Data;

    for (int32_t count = quantity; count > 0; --count)
    {
        GlobalLog()->VehicleList->AddVehicle(data.FileName.data(), false, true, true);
    }

    GlobalLog()->ReorderVehicles();
    data.NumAvailable -= quantity;
    ResourcePoints -= each * quantity;
}

void MCStore::SellMech(MCLogMech& sold, int32_t price)
{
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;

    for (const std::shared_ptr<MCLogInventoryItem>& item : sold.Inventory->Items)
    {
        const uint8_t masterID = item->MasterID;
        const MCComponentForm form = MasterComponentList[masterID].Form;

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

    const MCLogistics& logistics = *GlobalLog();
    TakeOut(
        sold, *logistics.ForceMechList, *logistics.MechList,
        [](const MCLogMechList& list, const std::shared_ptr<const MCLogMech>& mech) { return list.GetMechIndex(mech); },
        [](MCLogMechList& list, uint8_t index) { list.RemoveMech(index); });
    logistics.ReorderMechs();
    ResourcePoints += price;
}

void MCStore::FirePilot(MCLogWarrior& warrior, int32_t price)
{
    const int32_t row = warrior.ListIndex;
    warrior.Sold = true;
    MCLogistics& logistics = *GlobalLog();
    logistics.PurPilotList->SetPilotStatus(warrior.DescIndex, MCPurPilotData::SoldBack);

    // The original's sale found the pilot among the assigned, where its drag had put them; the remade one sells a
    // spare, out of the pilot list.
    if (logistics.AssignedWarriorList->RemoveWarrior(static_cast<uint8_t>(warrior.Id)) < 0)
    {
        logistics.WarriorList->RemoveWarrior(static_cast<uint8_t>(warrior.Id));
    }

    logistics.ShiftPilots(row, -1);
    logistics.ReorderWarriors();
    ResourcePoints += price;
}

bool MCStore::SellParts(MCLogInventoryItem& sold, int32_t quantity, int32_t each)
{
    sold.Count -= quantity;
    ResourcePoints += each * quantity;
    return sold.Count == 0;
}

void MCStore::SellVehicle(MCLogVehicle& sold, int32_t price)
{
    const MCLogistics& logistics = *GlobalLog();
    TakeOut(
        sold, *logistics.ForceVehicleList, *logistics.VehicleList,
        [](const MCLogVehicleList& list, const std::shared_ptr<const MCLogVehicle>& vehicle)
        { return list.GetVehicleIndex(vehicle); },
        [](MCLogVehicleList& list, uint8_t index) { list.RemoveVehicle(index); });
    logistics.ReorderVehicles();
    ResourcePoints += price;
}
