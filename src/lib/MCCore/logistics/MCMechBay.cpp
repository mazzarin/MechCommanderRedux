#include "stdafx.h"
#include "logistics/MCMechBay.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarrior.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "main/MCWeaponBans.h"
#include "object/MCMasterComponent.h"
#include "main/MCLogisticsShared.h"

namespace
{
    /// <summary>The knob's track in the original's pixels.</summary>
    constexpr double TrackPixels = 61.0;

    /// <summary>A weapon's long range: the last of its ranges (MCMasterComponent::WeaponRange).</summary>
    constexpr size_t LongRangeIndex = 3;

    /// <summary>The armour or internal structure knob for <paramref name="current"/> points of <paramref name="maximum"/>.</summary>
    int32_t KnobFor(int32_t current, int32_t maximum)
    {
        return MCMechBay::KnobLeft - static_cast<int32_t>(static_cast<double>(current) / maximum *
                                                          static_cast<double>(-static_cast<float>(TrackPixels)));
    }

    /// <summary>The engine knob for engine damage <paramref name="hits"/> (0..3).</summary>
    constexpr std::array<int32_t, 4> EngineKnobs = {MCMechBay::KnobRight, 0x113, 0xff, 0xeb};

    /// <summary>Where the engine knob's levels part (a position under each is the level past it).</summary>
    constexpr int32_t EngineThreeBelow = 0xf5;
    constexpr int32_t EngineTwoBelow = 0x109;
    constexpr int32_t EngineOneBelow = 0x11d;

    /// <summary>A ratio of the repair order: less than the ratio when not full, by <paramref name="factor"/>.</summary>
    float Discount(float ratio, double factor)
    {
        return static_cast<float>(static_cast<double>(ratio) - static_cast<double>(ratio) * factor);
    }

    /// <summary>
    /// Puts back what knob <paramref name="points"/> repaired since <paramref name="arrived"/>, refunding it at
    /// <paramref name="cost"/>; then repairs up to <paramref name="position"/> as far as the resource points go.
    /// </summary>
    template <size_t N, typename Repair>
    int32_t MovePoints(std::array<MCLogMech::ArmorPoints, N>& points, const std::array<int32_t, N>& arrived,
                       int32_t cost, int32_t start, float scale, int32_t position, Repair repair)
    {
        const int32_t before = MCMechBay::Points(points, false);

        for (size_t i = 0; i < arrived.size(); ++i)
        {
            points[i].CurArmor = static_cast<uint8_t>(arrived[i]);
        }

        const int32_t restored = MCMechBay::Points(points, false);
        ResourcePoints = ResourcePoints + (before - restored) * cost;
        int32_t wanted;

        if (position < MCMechBay::KnobRight)
        {
            wanted = static_cast<int32_t>(static_cast<double>(position - start) / static_cast<double>(scale));
        }
        else
        {
            wanted = MCMechBay::Points(points, true) - restored;
        }

        const int32_t affordable = ResourcePoints / cost;

        if (wanted < affordable)
        {
            repair(wanted);
            ResourcePoints = ResourcePoints - cost * wanted;
            return position;
        }

        repair(affordable);
        ResourcePoints = ResourcePoints - cost * affordable;
        return static_cast<int32_t>(static_cast<double>(affordable) * static_cast<double>(scale) + start);
    }
}

int32_t MCMechBay::EngineKnob(uint8_t hits)
{
    return EngineKnobs[std::min<size_t>(hits, EngineKnobs.size() - 1)];
}

int32_t MCMechBay::Points(std::span<const MCLogMech::ArmorPoints> points, bool maximum)
{
    int32_t sum = 0;

    for (const MCLogMech::ArmorPoints& location : points)
    {
        sum += maximum ? location.MaxArmor : location.CurArmor;
    }

    return sum;
}

std::shared_ptr<MCLogInventoryStat> MCMechBay::EngineOf(const MCLogMech& mech)
{
    const auto engine = std::ranges::find_if(mech.Inventory->Items, [](const std::shared_ptr<MCLogInventoryItem>& item)
                                             { return ComponentForm(item->MasterID) == MCComponentForm::Engine; });
    return engine != mech.Inventory->Items.end() ? (*engine)->Stats.front() : nullptr;
}

MCMechBay::MCKnobs MCMechBay::KnobsOf(const MCLogMech& mech)
{
    // The scales: 61 pixels over the total maximum; the starting points are where the mech arrived.
    MCKnobs knobs;
    const int32_t totalArmor = Points(mech.Armor, true);
    knobs.ArmorScale = static_cast<float>(TrackPixels / static_cast<double>(totalArmor));
    const int32_t totalInternal = Points(mech.Internals, true);
    knobs.InternalScale = static_cast<float>(TrackPixels / static_cast<double>(totalInternal));
    const MCLogMech::ArrivalState& arrived = mech.Arrived;
    knobs.ArmorStart = KnobFor(std::ranges::fold_left(arrived.Armor, 0, std::plus<>()), totalArmor);
    knobs.InternalStart = KnobFor(std::ranges::fold_left(arrived.Internals, 0, std::plus<>()), totalInternal);
    knobs.EngineStart = EngineKnob(arrived.EngineHits);
    return knobs;
}

int32_t MCMechBay::KnobAt(const MCLogMech& mech, MCKnob knob)
{
    switch (knob)
    {
        case MCKnob::Armor:
            return mech.RepairSliders.Armor;
        case MCKnob::Internal:
            return mech.RepairSliders.Internal;
        case MCKnob::Engine:
            return mech.RepairSliders.Engine;
    }

    return KnobLeft;
}

void MCMechBay::PlaceKnob(MCLogMech& mech, MCKnob knob)
{
    switch (knob)
    {
        case MCKnob::Engine:
        {
            MCLogInventoryStat& engine = *EngineOf(mech);

            if (engine.Hits > MostEngineHits)
            {
                engine.Hits = MostEngineHits;
            }

            mech.RepairSliders.Engine = EngineKnob(engine.Hits);
            return;
        }
        case MCKnob::Internal:
        {
            mech.RepairSliders.Internal = KnobFor(Points(mech.Internals, false), Points(mech.Internals, true));
            return;
        }
        case MCKnob::Armor:
        {
            mech.RepairSliders.Armor = KnobFor(Points(mech.Armor, false), Points(mech.Armor, true));
            return;
        }
    }
}

void MCMechBay::PlaceKnobs(MCLogMech& mech)
{
    PlaceKnob(mech, MCKnob::Engine);
    PlaceKnob(mech, MCKnob::Armor);
    PlaceKnob(mech, MCKnob::Internal);
}

std::optional<int32_t> MCMechBay::MoveKnob(MCLogMech& mech, const MCKnobs& knobs, MCKnob knob, int32_t position)
{
    if (mech.Deployed != 0)
    {
        GlobalLog()->UndeployMech(mech);
    }

    position = std::min(position, KnobRight);
    std::optional<int32_t> last = position;
    const MCLogistics& logistics = *GlobalLog();

    switch (knob)
    {
        case MCKnob::Armor:
        {
            position = std::max(position, knobs.ArmorStart);
            mech.RepairSliders.Armor =
                MovePoints(mech.Armor, mech.Arrived.Armor, logistics.ArmorCost, knobs.ArmorStart, knobs.ArmorScale,
                           position, [&mech](int32_t points) { RepairArmor(mech, points); });
            last = position;
            break;
        }
        case MCKnob::Internal:
        {
            position = std::max(position, knobs.InternalStart);
            mech.RepairSliders.Internal =
                MovePoints(mech.Internals, mech.Arrived.Internals, logistics.InternalCost, knobs.InternalStart,
                           knobs.InternalScale, position, [&mech](int32_t points) { RepairInternal(mech, points); });
            last = position;
            break;
        }
        case MCKnob::Engine:
        {
            // (The original's inline undeploy here was never reached: the mech left the drop above.)
            const int32_t target = std::max(position, knobs.EngineStart);
            // The engine repairs in whole damage levels; each costs EngineCost.
            const int32_t cost = logistics.EngineCost;
            uint8_t& hits = EngineOf(mech)->Hits;

            // Damage level 3 (the floor) refunds what was repaired; the others are reached if the points pay.
            const auto level = [&](uint8_t to) -> bool
            {
                const int32_t change = (static_cast<int32_t>(to) - static_cast<int32_t>(hits)) * cost;

                if (to != MostEngineHits && change < 0 && ResourcePoints < -change)
                {
                    return false;
                }

                hits = to;
                ResourcePoints = ResourcePoints + change;
                return true;
            };

            const uint8_t to = target < EngineThreeBelow ? 3
                               : target < EngineTwoBelow ? 2
                               : target < EngineOneBelow ? 1
                                                         : 0;

            if (!level(to))
            {
                return std::nullopt;
            }

            mech.RepairSliders.Engine = EngineKnob(to);
            last = mech.RepairSliders.Engine;
            break;
        }
    }

    mech.CalcBR();
    return last;
}

int32_t MCMechBay::RestCost(const MCLogMech& mech, MCKnob knob)
{
    const MCLogistics& logistics = *GlobalLog();

    switch (knob)
    {
        case MCKnob::Armor:
            return (Points(mech.Armor, true) - Points(mech.Armor, false)) * logistics.ArmorCost;
        case MCKnob::Internal:
            return (Points(mech.Internals, true) - Points(mech.Internals, false)) * logistics.InternalCost;
        case MCKnob::Engine:
            return static_cast<int32_t>(EngineOf(mech)->Hits) * logistics.EngineCost;
    }

    return 0;
}

bool MCMechBay::StructureDamaged(const MCLogMech& mech)
{
    // The engine, the internal structure or the armor damaged.
    return EngineOf(mech)->Hits != 0 || Points(mech.Internals, false) != Points(mech.Internals, true) ||
           Points(mech.Armor, false) != Points(mech.Armor, true);
}

void MCMechBay::RepairArmor(MCLogMech& mech, int32_t points)
{
    if (points < 0)
    {
        for (auto& location : mech.Armor)
        {
            location.CurArmor = location.MaxArmor;
        }

        return;
    }

    // The head first, then point by point to the most damaged location (the cockpit, center torso and rear locations
    // count as more damaged than they are).
    int32_t headMissing = mech.Armor[0].MaxArmor - mech.Armor[0].CurArmor;

    if (headMissing != 0)
    {
        mech.Armor[0].CurArmor += static_cast<uint8_t>(points < headMissing ? points : headMissing);
        points -= headMissing;
    }

    if (points <= 0)
    {
        return;
    }

    // How much more damaged than its share each location counts (none for the arms, legs and side torsos).
    static constexpr std::array<double, 11> discount = {0.0, 0.2, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.4, 0.3, 0.3};
    std::array<float, 11> ratios = {};

    for (size_t location = 1; location < ratios.size(); ++location)
    {
        float ratio =
            static_cast<float>(static_cast<double>(mech.Armor[location].CurArmor) / mech.Armor[location].MaxArmor);
        ratios[location] = discount[location] != 0.0 && ratio < 1.0f ? Discount(ratio, discount[location]) : ratio;
    }

    size_t chosen = 1;

    for (; points != 0; --points)
    {
        for (size_t location = 1; location < ratios.size(); ++location)
        {
            if (ratios[location] < ratios[chosen])
            {
                chosen = location;
            }
        }

        uint8_t current = ++mech.Armor[chosen].CurArmor;
        uint8_t maximum = mech.Armor[chosen].MaxArmor;

        if (current < maximum)
        {
            auto ratio = static_cast<float>(static_cast<double>(current) / maximum);
            ratios[chosen] = discount[chosen] != 0.0 ? Discount(ratio, discount[chosen]) : ratio;
        }
        else
        {
            ratios[chosen] = 1.0f;
        }
    }
}

void MCMechBay::RepairInternal(MCLogMech& mech, int32_t points)
{
    if (points < 0)
    {
        for (auto& location : mech.Internals)
        {
            location.CurArmor = location.MaxArmor;
        }

        return;
    }

    // Point by point to the most damaged location (weighted like the armor).
    static constexpr std::array<double, 8> discount = {0.3, 0.5, 0.2, 0.2, 0.0, 0.0, 0.4, 0.4};
    std::array<float, 8> ratios = {};

    for (size_t location = 0; location < ratios.size(); ++location)
    {
        ratios[location] = static_cast<float>(static_cast<double>(mech.Internals[location].CurArmor) /
                                              mech.Internals[location].MaxArmor);

        if (discount[location] != 0.0 && ratios[location] < 1.0f)
        {
            ratios[location] = Discount(ratios[location], discount[location]);
        }
    }

    size_t chosen = 1;

    for (; points > 0; --points)
    {
        for (size_t location = 0; location < ratios.size(); ++location)
        {
            if (ratios[location] < ratios[chosen])
            {
                chosen = location;
            }
        }

        uint8_t current = ++mech.Internals[chosen].CurArmor;
        uint8_t maximum = mech.Internals[chosen].MaxArmor;

        if (current < maximum)
        {
            auto ratio = static_cast<float>(static_cast<double>(current) / maximum);
            ratios[chosen] = discount[chosen] != 0.0 ? Discount(ratio, discount[chosen]) : ratio;
        }
        else
        {
            ratios[chosen] = 1.0f;
        }
    }
}

bool MCMechBay::RepairAll(MCLogMech& mech)
{
    // The engine level by level, then internal structure, then armor, as far as the resource points go.
    if (mech.Deployed != 0)
    {
        GlobalLog()->UndeployMech(mech);
    }

    const MCLogistics& logistics = *GlobalLog();
    bool shortOfPoints = false;
    std::shared_ptr<MCLogInventoryStat> engine = EngineOf(mech);
    const int32_t engineCost = logistics.EngineCost;

    while (engine->Hits != 0 && !shortOfPoints)
    {
        if (ResourcePoints < engineCost)
        {
            shortOfPoints = true;
            break;
        }

        ResourcePoints = ResourcePoints - engineCost;
        --engine->Hits;
        mech.Status = 0;
    }

    // The internal structure, then the armour, each wholly if the points pay, else as far as they go.
    const auto repair = [&shortOfPoints](int32_t missing, int32_t cost, const std::function<void(int32_t)>& points)
    {
        if (shortOfPoints)
        {
            return;
        }

        int32_t repaired = missing;

        if (ResourcePoints < repaired * cost)
        {
            repaired = ResourcePoints / cost;
            points(repaired);
            shortOfPoints = true;
        }
        else
        {
            points(-1);
        }

        ResourcePoints = ResourcePoints - cost * repaired;
    };

    repair(Points(mech.Internals, true) - Points(mech.Internals, false), logistics.InternalCost,
           [&mech](int32_t points) { RepairInternal(mech, points); });
    repair(Points(mech.Armor, true) - Points(mech.Armor, false), logistics.ArmorCost,
           [&mech](int32_t points) { RepairArmor(mech, points); });
    PlaceKnob(mech, MCKnob::Armor);
    PlaceKnob(mech, MCKnob::Internal);
    PlaceKnob(mech, MCKnob::Engine);
    return shortOfPoints;
}

bool MCMechBay::ItemsDamaged(const MCLogMech& mech)
{
    // Any weapon, equipment or jammer copy damaged.
    for (const std::shared_ptr<MCLogInventoryItem>& item : mech.Inventory->Items)
    {
        const MCComponentForm form = ComponentForm(item->MasterID);

        if (!IsWeapon(form) && !IsEquipment(form) && form != MCComponentForm::Jammer)
        {
            continue;
        }

        for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
        {
            if (stat->Hits != 0)
            {
                return true;
            }
        }
    }

    return false;
}

std::string MCMechBay::Refit(MCLogMech& mech, std::vector<std::shared_ptr<MCLogInventoryItem>>& used)
{
    // Replace every damaged copy from the component inventory; list the ones missing.
    if (mech.Deployed != 0)
    {
        GlobalLog()->UndeployMech(mech);
    }

    std::string missing;

    for (const std::shared_ptr<MCLogInventoryItem>& item : mech.Inventory->Items)
    {
        MCComponentForm form = ComponentForm(item->MasterID);

        for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
        {
            if (!IsWeapon(form) && !IsEquipment(form))
            {
                if (form != MCComponentForm::Engine && stat->Hits != 0)
                {
                    stat->Hits = 0;
                }

                continue;
            }

            if (stat->Hits == 0)
            {
                continue;
            }

            MCInventoryList& components = *GlobalLog()->ComponentInventory;
            std::shared_ptr<MCLogInventoryItem> stockItem =
                components.GetItemInfo(components.GetIndexFromMasterID(item->MasterID));

            if (stockItem == nullptr || stockItem->Count == 0)
            {
                // Port fix: the name comes from the mech's own item when the inventory has none (the original read
                // the name through the null item).
                const std::string& name = stockItem != nullptr ? stockItem->Name : item->Name;
                missing += missing.empty() ? name : std::format(",{}", name);
                continue;
            }

            --stockItem->Count;
            used.push_back(stockItem);
            stat->Hits = 0;
        }
    }

    GlobalLog()->ReIndexInventory();
    return missing;
}

void MCMechBay::StripUnrepaired(MCLogMech& mech)
{
    MCInventoryList& inventory = *mech.Inventory;

    // The position of item (-1 when it is no longer in the inventory).
    auto positionOf = [&](const MCLogInventoryItem& item)
    {
        const auto found = std::ranges::find_if(inventory.Items, [&](const std::shared_ptr<MCLogInventoryItem>& entry)
                                                { return entry.get() == &item; });
        return found != inventory.Items.end() ? static_cast<int32_t>(found - inventory.Items.begin()) : -1;
    };

    // The item after item in the inventory, or null.
    auto following = [&](const MCLogInventoryItem& item) -> std::shared_ptr<MCLogInventoryItem>
    {
        const int32_t position = positionOf(item);
        return position >= 0 ? inventory.GetItemInfo(position + 1) : nullptr;
    };

    for (std::shared_ptr<MCLogInventoryItem> item = inventory.GetItemInfo(0); item != nullptr; item = following(*item))
    {
        const MCComponentForm form = ComponentForm(item->MasterID);

        if (!IsWeapon(form) && !IsEquipment(form))
        {
            continue;
        }

        // Every damaged copy the player could not replace leaves the mech.
        size_t copy = 0;

        while (copy < item->Stats.size())
        {
            const MCLogInventoryStat& stat = *item->Stats[copy];

            if (stat.Hits == 0)
            {
                ++copy;
                continue;
            }

            const uint8_t masterID = item->MasterID;
            const uint8_t statID = stat.StatID;

            // Original behaviour (OB-081): the loop moves on to the next item before the removal and walks its
            // stats whatever its form.
            if (item->Count == 1)
            {
                item = following(*item);
            }

            const float tonnage = FittedTons(masterID);

            if (UsesAmmo(masterID))
            {
                inventory.RemoveItem(MasterComponentList[masterID].AmmoMasterId, -1);
            }

            mech.UsedTonnage -= tonnage;
            mech.WeaponTonnage -= tonnage;
            inventory.RemoveItem(masterID, statID);

            // Port fix (OB-081): when the ammunition removed was the item the walk moved on to, the original went on
            // reading it after it was freed; the walk ends there.
            if (item != nullptr && positionOf(*item) < 0)
            {
                item = nullptr;
            }

            if (item == nullptr)
            {
                break;
            }

            copy = 0;
        }

        if (item == nullptr)
        {
            break;
        }
    }
}

float MCMechBay::FittedTons(uint8_t masterID)
{
    const MCMasterComponent& component = MasterComponentList[masterID];
    float tonnage = component.Tonnage;

    if (UsesAmmo(masterID))
    {
        tonnage = MasterComponentList[component.AmmoMasterId].Tonnage + tonnage;
    }

    return tonnage;
}

std::optional<MCMechBay::MCFitList> MCMechBay::FitListOf(uint8_t masterID)
{
    const MCMasterComponent& component = MasterComponentList[masterID];

    if (IsWeapon(component.Form))
    {
        const float range = component.WeaponRange[LongRangeIndex];
        return range < ShortRangeUnder    ? MCFitList::ShortRange
               : range < MediumRangeUnder ? MCFitList::MediumRange
                                          : MCFitList::LongRange;
    }

    return IsEquipment(component.Form) ? std::optional(MCFitList::Equipment) : std::nullopt;
}

std::vector<size_t> MCMechBay::DamageOrder(std::span<const float> damages)
{
    std::vector<size_t> order(damages.size());
    std::iota(order.begin(), order.end(), size_t{0});

    // The original's exchange sort: each place swaps in every later one of lower damage, as it is found.
    for (size_t i = 0; i + 1 < order.size(); ++i)
    {
        for (size_t j = i + 1; j < order.size(); ++j)
        {
            if (damages[order[j]] < damages[order[i]])
            {
                std::swap(order[i], order[j]);
            }
        }
    }

    return order;
}

std::vector<MCMechBay::MCFittedCopy> MCMechBay::FittedCopies(const MCLogMech& mech)
{
    constexpr size_t Lists = static_cast<size_t>(MCFitList::Equipment) + 1;
    std::array<std::vector<MCFittedCopy>, Lists> lists;

    for (const std::shared_ptr<MCLogInventoryItem>& item : mech.Inventory->Items)
    {
        if (const std::optional<MCFitList> list = FitListOf(item->MasterID); list.has_value())
        {
            for (const std::shared_ptr<MCLogInventoryStat>& copy : item->Stats)
            {
                lists[static_cast<size_t>(*list)].push_back({.Item = item, .Copy = copy});
            }
        }
    }

    std::vector<MCFittedCopy> copies;

    for (size_t list = 0; list < Lists; ++list)
    {
        const std::vector<MCFittedCopy>& listed = lists[list];

        if (list == static_cast<size_t>(MCFitList::Equipment))
        {
            copies.insert(copies.end(), listed.begin(), listed.end());
            continue;
        }

        std::vector<float> damages;
        damages.reserve(listed.size());

        for (const MCFittedCopy& copy : listed)
        {
            damages.push_back(MasterComponentList[copy.Item->MasterID].Damage);
        }

        for (const size_t at : DamageOrder(damages))
        {
            copies.push_back(listed[at]);
        }
    }

    return copies;
}

MCMechBay::MCFit MCMechBay::WhyNotFit(uint8_t masterID, float tonnage, MCOptionalRef<const MCLogMech> selected)
{
    if (!selected.has_value())
    {
        return MCFit::NoMech;
    }

    const MCLogMech& mech = *selected;

    if (static_cast<double>(mech.CurTonnage) - mech.UsedTonnage < tonnage)
    {
        return MCFit::TooHeavy;
    }

    // Port: nothing a match's weapon bans ban (MCWeaponBans.h).
    if (MCComponentBanned(masterID, MCPlayedWeaponBans()))
    {
        return MCFit::Banned;
    }

    // One ECM, sensor or probe per mech.
    const MCComponentForm form = ComponentForm(masterID);

    if (IsEquipment(form))
    {
        for (const std::shared_ptr<MCLogInventoryItem>& mounted : mech.Inventory->Items)
        {
            if (form == ComponentForm(mounted->MasterID))
            {
                return MCFit::OneFitted;
            }
        }
    }

    // Some components only fit the mechs of name index 5, 0xe and 0x10.
    const bool restricted = std::ranges::contains(RestrictedComps, masterID);
    return !restricted || mech.NameIndex == 0xe || mech.NameIndex == 0x10 || mech.NameIndex == 5 ? MCFit::None
                                                                                                 : MCFit::NotThisMech;
}

bool MCMechBay::Fit(MCLogMech& mech, uint8_t masterID)
{
    if (mech.Deployed != 0)
    {
        GlobalLog()->UndeployMech(mech);
    }

    const MCMasterComponent& component = MasterComponentList[masterID];
    const bool withAmmo = UsesAmmo(masterID);
    double tons = component.Tonnage;

    if (withAmmo)
    {
        tons += MasterComponentList[component.AmmoMasterId].Tonnage;
    }

    if (tons > static_cast<double>(mech.CurTonnage) - mech.UsedTonnage)
    {
        return false;
    }

    mech.Inventory->AddItem(masterID, mech.Inventory->CreateStat(masterID, 0, 1, 1, 0xff), false);

    if (withAmmo)
    {
        mech.Inventory->AddItem(component.AmmoMasterId, mech.Inventory->CreateStat(masterID, 0, 0, -1, 0xff), false);
    }

    const auto added = static_cast<float>(tons);
    mech.UsedTonnage += added;
    mech.WeaponTonnage += added;
    mech.CalcBR();
    return true;
}

std::optional<MCMechBay::MCTaken> MCMechBay::TakeOut(MCLogMech& mech, const MCLogInventoryItem& item, uint8_t itemNum)
{
    const uint8_t masterID = item.MasterID;
    MCInventoryList& inventory = *mech.Inventory;
    const int32_t index = inventory.GetIndexFromMasterID(masterID);
    const std::shared_ptr<MCLogInventoryItem> held = inventory.GetItemInfo(index);

    if (held == nullptr || std::ranges::none_of(held->Stats, [itemNum](const std::shared_ptr<MCLogInventoryStat>& stat)
                                                { return static_cast<uint32_t>(stat->ItemNum) == itemNum; }))
    {
        SDL_Log("MCMechBay: %s holds no copy %u of part %u to take out", mech.FileName.c_str(), itemNum, masterID);
        return std::nullopt;
    }

    const float tonnage = FittedTons(masterID);

    if (UsesAmmo(masterID))
    {
        inventory.RemoveItem(MasterComponentList[masterID].AmmoMasterId, -1);
    }

    // The item's place found again: the ammunition's removal can move it.
    const std::shared_ptr<MCLogInventoryItem> taken = inventory.GetItemInfo(inventory.GetIndexFromMasterID(masterID));
    mech.UsedTonnage -= tonnage;
    mech.WeaponTonnage -= tonnage;

    for (const std::shared_ptr<MCLogInventoryStat>& stat : taken->Stats)
    {
        if (static_cast<uint32_t>(stat->ItemNum) != itemNum)
        {
            continue;
        }

        const MCTaken out{.MasterID = masterID, .ItemNum = itemNum, .Hits = stat->Hits};
        inventory.RemoveItem(masterID, stat->StatID);
        mech.CalcBR();
        return out;
    }

    return std::nullopt;
}

MCMechBay::MCSpareRow MCMechBay::ToSpares(const MCTaken& taken, std::shared_ptr<MCLogInventoryItem>& spare)
{
    if (taken.Hits != 0)
    {
        return MCSpareRow::Thrown;
    }

    MCInventoryList& components = *GlobalLog()->ComponentInventory;
    spare = components.GetItemInfo(components.GetIndexFromMasterID(taken.MasterID));

    if (spare == nullptr)
    {
        spare = AddSpare(taken.MasterID);
    }

    if (spare->Count != 0)
    {
        ++spare->Count;
        return MCSpareRow::Counted;
    }

    spare->Count = 1;
    return MCSpareRow::Listed;
}

void MCMechBay::PutBack(MCLogMech& mech, const MCTaken& taken)
{
    MCInventoryList& inventory = *mech.Inventory;
    inventory.AddItem(taken.MasterID, inventory.CreateStat(taken.ItemNum, taken.Hits, 0, 1, 0xff), false);
    float tonnage = MasterComponentList[taken.MasterID].Tonnage;

    if (UsesAmmo(taken.MasterID))
    {
        const uint8_t ammo = MasterComponentList[taken.MasterID].AmmoMasterId;
        inventory.AddItem(ammo, inventory.CreateStat(inventory.NextStatID, 0, 0, -1, 0xff), false);
        tonnage = MasterComponentList[ammo].Tonnage + tonnage;
    }

    mech.UsedTonnage = tonnage + mech.UsedTonnage;
    mech.WeaponTonnage = tonnage + mech.WeaponTonnage;
    mech.CalcBR();
}

void MCMechBay::OutOfDrop(MCLogMech& mech)
{
    if (mech.Deployed != 0)
    {
        GlobalLog()->UndeployMech(mech);
    }
}

std::shared_ptr<MCLogWarrior> MCMechBay::PilotOut(MCLogMech& mech)
{
    MCLogistics& logistics = *GlobalLog();
    std::shared_ptr<MCLogWarrior> pilot;
    logistics.AssignedWarriorList->GetWarriorInfo(mech.PilotIndex, pilot);

    if (pilot == nullptr)
    {
        return nullptr;
    }

    if (mech.Deployed != 0)
    {
        logistics.UndeployMech(mech);
    }

    logistics.ShiftPilots(mech.PilotIndex, -1);
    pilot->Assigned = false;
    logistics.SetPilot(mech.SlotIndex, -1);
    logistics.ReorderWarriors();
    mech.CalcBR();
    mech.CalcPilotModifier();
    return pilot;
}

bool MCMechBay::CanBoard(const MCLogMech& mech)
{
    return mech.LocalPart && GlobalLog()->ForceMechList->GetMechIndex(
                                 std::static_pointer_cast<const MCLogMech>(mech.shared_from_this())) >= 0;
}

void MCMechBay::Assign(MCLogWarrior& warrior)
{
    const MCLogistics& logistics = *GlobalLog();
    warrior.Assigned = true;
    logistics.ReorderWarriors();
    logistics.ShiftPilots(warrior.ListIndex, 1);
}

void MCMechBay::Unassign(MCLogWarrior& warrior)
{
    const MCLogistics& logistics = *GlobalLog();
    warrior.Assigned = false;
    logistics.ShiftPilots(warrior.ListIndex, -1);
    logistics.ReorderWarriors();
}

void MCMechBay::Board(MCLogWarrior& warrior, MCLogMech& mech, int32_t row)
{
    const MCLogistics& logistics = *GlobalLog();
    const int32_t pilotRow = warrior.ListIndex;
    logistics.SetPilot(row, pilotRow);
    // The repair block's battle rating paint worked it out (the row draws it each frame).
    mech.CalcBR();
    logistics.SetPilot(row, pilotRow);
}

void MCMechBay::MakeRoomInDrop(bool vehicle)
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

bool MCMechBay::ForceFull()
{
    const MCLogistics& logistics = *GlobalLog();
    return logistics.ForceMechList->GetMechCount() + logistics.ForceVehicleList->GetVehicleCount() > ForceUnits;
}

bool MCMechBay::JoinForce(MCLogMech& mech)
{
    // The original's pick-up assigns the mech; its drop on the force keeps it there unless the force is past its size.
    mech.Assigned = true;
    GlobalLog()->ReorderMechs();

    if (ForceFull())
    {
        mech.Assigned = false;
        GlobalLog()->ReorderMechs();
        return false;
    }

    MakeRoomInDrop(false);
    return true;
}

bool MCMechBay::JoinForce(MCLogVehicle& vehicle)
{
    vehicle.Assigned = true;
    GlobalLog()->ReorderVehicles();

    if (ForceFull())
    {
        vehicle.Assigned = false;
        GlobalLog()->ReorderVehicles();
        return false;
    }

    MakeRoomInDrop(true);
    return true;
}

bool MCMechBay::LeaveForce(MCLogMech& mech)
{
    MCLogistics& logistics = *GlobalLog();
    const int32_t row = mech.SlotIndex;

    if (mech.Deployed != 0)
    {
        logistics.UndeployMech(mech);
    }

    const int32_t pilotIndex = mech.PilotIndex;
    const bool freed = pilotIndex >= 0;

    if (freed)
    {
        std::shared_ptr<MCLogWarrior> pilot;
        logistics.AssignedWarriorList->GetWarriorInfo(pilotIndex, pilot);
        pilot->Assigned = false;
        logistics.ShiftPilots(pilotIndex, -1);
        logistics.SetPilot(row, -1);
        logistics.ReorderWarriors();
    }

    // Off the drop slots (the original took the mech's briefing block off the screen or the deploy pane; the pane is
    // listed again when the briefing screen shows).
    for (auto& lance : logistics.DeploySlots)
    {
        for (auto& slot : lance)
        {
            if (slot.Unit < 0)
            {
                continue;
            }

            if (slot.Unit == row)
            {
                slot.Unit = -1;
            }
            else if (row < slot.Unit)
            {
                slot.Unit = slot.Unit - 1;
            }
        }
    }

    mech.Deployed = false;
    mech.Assigned = false;
    logistics.ReorderMechs();
    return freed;
}

void MCMechBay::LeaveForce(MCLogVehicle& vehicle)
{
    MCLogistics& logistics = *GlobalLog();
    logistics.RemoveVehicleFromSlots(vehicle.SlotIndex - logistics.ForceMechList->GetMechCount());
    vehicle.Deployed = false;
    vehicle.Assigned = false;
    logistics.ReorderVehicles();
}

std::shared_ptr<MCLogInventoryItem> MCMechBay::AddSpare(uint8_t masterID)
{
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;
    spares.AddItem(masterID, spares.CreateStat(masterID, 0, 1, 0, 0xff), false);
    std::shared_ptr<MCLogInventoryItem> item = spares.GetItemInfo(spares.GetIndexFromMasterID(masterID));
    MCInventoryList::ListAsSpare(*item);
    item->InventoryIndex = spares.NumItems() - 1;
    return item;
}
