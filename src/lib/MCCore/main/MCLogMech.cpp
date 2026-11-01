#include "stdafx.h"
#include "main/MCLogMech.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCMechBriefBlock.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "object/MCBattleMech.h"

MCLogPart::MCLogPart() = default;

MCLogPart::~MCLogPart() = default;

MCLogMech::MCLogMech()
{
    PartType = 1;
}

MCLogMech::~MCLogMech()
{
    Inventory.reset();
    BriefingBox.reset();
    RepairBlock.reset();
    InventoryBlock.reset();
    BriefBlock.reset();
}

auto MCLogMech::CalcPilotModifier() -> int32_t
{
    std::shared_ptr<MCLogWarrior> warrior;

    if (PilotIndex < 0 || GlobalLog()->AssignedWarriorList->GetWarriorInfo(PilotIndex, warrior) != 0)
    {
        PilotModifier = 0;
        return 0;
    }

    // A pilot ranked below the mech's weight class costs 0x400 a step; a better one gives nothing.
    PilotModifier = std::min((warrior->Rank - LogWeightClass(CurTonnage)) * 0x400, 0);
    return PilotModifier;
}

auto MCLogMech::CalcMechCost(bool repaired) -> void
{
    ResourcePoints = BaseResourcePoints;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Inventory->Items)
    {
        const MCMasterComponent& master = MasterComponentList[item->MasterID];
        const MCComponentForm form = master.Form;
        // Weapons, ammunition and equipment count only when repaired; the rest always.
        const bool fitted = form == MCComponentForm::WeaponEnergy || form == MCComponentForm::WeaponBallistic ||
                            form == MCComponentForm::WeaponMissile || form == MCComponentForm::Ammo ||
                            form == MCComponentForm::Sensor || form == MCComponentForm::Ecm ||
                            form == MCComponentForm::Probe || form == MCComponentForm::Jammer;

        if (!repaired && fitted)
        {
            continue;
        }

        for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
        {
            if (stat->Hits == 0)
            {
                ResourcePoints += master.ResourcePoints;
            }
        }
    }

    uint32_t internal = 0;

    for (const ArmorPoints& points : Internals)
    {
        internal += points.CurArmor;
    }

    uint32_t armorLeft = 0;

    for (const ArmorPoints& points : Armor)
    {
        armorLeft += points.CurArmor;
    }

    ResourcePoints = static_cast<int32_t>(static_cast<uint32_t>(ResourcePoints) + internal * 0x32 + armorLeft * 0x28);
}

auto MCLogMech::CalcBR() -> int32_t
{
    // The undamaged copies' battle ratings, summed in the x87's precision, plus the chassis's, truncated.
    double rating = 0.0;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Inventory->Items)
    {
        for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
        {
            if (stat->Hits == 0)
            {
                rating += MasterComponentList[item->MasterID].BattleRating;
            }
        }
    }

    BattleRating = static_cast<int32_t>(static_cast<double>(ChassisBR) + rating);
    return BattleRating;
}

auto MCLogMech::PlaceItem(uint8_t masterID, int32_t itemNum, int32_t hits) -> void
{
    const auto number = static_cast<uint8_t>(itemNum);
    const auto damage = static_cast<uint8_t>(hits);
    auto slotsOf = [&](int32_t location)
    {
        return std::span(ItemSlots[static_cast<size_t>(location)])
            .first(static_cast<size_t>(NumLocationCriticalSpaces[location]));
    };

    auto fill = [&](int32_t location, bool setMaster)
    {
        for (ItemSlot& entry : slotsOf(location))
        {
            if (entry.Row == EmptySlot)
            {
                entry.Row = number;
                entry.Column = damage;

                if (setMaster)
                {
                    entry.MasterID = masterID;
                }

                return;
            }
        }
    };

    auto holds = [&](int32_t location)
    { return std::ranges::any_of(slotsOf(location), [&](const ItemSlot& slot) { return slot.MasterID == masterID; }); };

    // OB-182 (fixed): MCX.EXE placed master ids below 100 by form, where a weapon matches no case, so Desperate
    // Measures' Rail Gun (98) and Light Gauss Rifle (99) got no critical slot in the profile a mission loads. They
    // still fired, but were never lost with a location, their fire arc followed the hull instead of the torso, and
    // each shot read its hot spot from body location 0xff, past the mech's body locations.
    if (masterID < FirstWeaponID)
    {
        switch (MasterComponentList[masterID].Form)
        {
            case MCComponentForm::Cockpit:
            case MCComponentForm::Sensor:
            case MCComponentForm::LifeSupport:
            case MCComponentForm::Ecm:
            case MCComponentForm::Probe:
            {
                // Head equipment (the component is not recorded).
                fill(MechHead, false);
                return;
            }
            case MCComponentForm::Actuator:
            {
                if (masterID != 4 && masterID != 0x21)
                {
                    // Leg actuators: the left leg, or the right leg when the left already has one.
                    fill(holds(MechLeftLeg) ? MechRightLeg : MechLeftLeg, true);
                    return;
                }

                // Arm actuators (4 and 0x21): the left arm, or the right arm when the left already has one.
                fill(holds(MechLeftArm) ? MechRightArm : MechLeftArm, true);
                return;
            }
            case MCComponentForm::Engine:
            case MCComponentForm::Gyroscope:
            {
                // Centre torso (the component is not recorded).
                fill(MechCenterTorso, false);
                return;
            }
            case MCComponentForm::JumpJet:
            {
                // Jump jets: the leg with fewer of them, the left on a tie.
                // OB-092 (fixed): MCX.EXE read both legs at one slot index that only moved on when the left leg's slot
                // held a jet (and took master ids 10..13 as the jets), then put this jet in every empty slot of the
                // chosen leg.
                auto countJets = [&](int32_t location)
                {
                    return std::ranges::count_if(slotsOf(location), [](const ItemSlot& slot)
                                                 { return LogSlotForm(slot.MasterID) == MCComponentForm::JumpJet; });
                };

                fill(countJets(MechRightLeg) < countJets(MechLeftLeg) ? MechRightLeg : MechLeftLeg, true);
                return;
            }
            default:
            {
                return;
            }
        }
    }

    // Weapons: the arm or side torso with room and the fewest weapons of their size, ties going to the one searched
    // first. Small weapons search the arms first, large weapons the side torsos.
    // OB-092 (fixed): MCX.EXE started the large search from the left torso's count but with the left arm chosen, so
    // large weapons piled into the left arm, and it wrote into a full location's slot 12 (the next location's first).
    static constexpr int32_t smallWeaponOrder[4] = {MechLeftArm, MechRightArm, MechLeftTorso, MechRightTorso};
    static constexpr int32_t largeWeaponOrder[4] = {MechLeftTorso, MechRightTorso, MechLeftArm, MechRightArm};
    const bool large = GetWeaponLarge(masterID);
    int32_t location = -1;
    int32_t fewest = 0;

    for (const int32_t candidate : large ? largeWeaponOrder : smallWeaponOrder)
    {
        if (std::ranges::none_of(slotsOf(candidate), [](const ItemSlot& slot) { return slot.Row == EmptySlot; }))
        {
            continue;
        }

        const int32_t count = large ? GetLargeWeaponCount(candidate) : GetSmallWeaponCount(candidate);

        if (location < 0 || count < fewest)
        {
            location = candidate;
            fewest = count;
        }
    }

    // No arm or side torso has room: the weapon gets no critical slot.
    if (location >= 0)
    {
        fill(location, true);
    }
}

auto MCLogMech::GetWeaponLarge(uint8_t masterID) -> bool
{
    if ((masterID >= 100 && masterID <= 0x68) || (masterID >= 0x6e && masterID <= 0x71))
    {
        return true;
    }

    switch (masterID)
    {
        case 0x79:
        case 0x7a:
        case 0x83:
        case 0x84:
        case 0x8d:
        case 0x8e:
        case 0x91:
        case 0x92:
        case 0x96:
        case 0x97:
        case 0x9a:
            return true;
        default:
            return false;
    }
}

auto MCLogMech::GetLargeWeaponCount(int32_t location) const -> int32_t
{
    return static_cast<int32_t>(std::ranges::count_if(ItemSlots[static_cast<size_t>(location)], [](const ItemSlot& slot)
                                                      { return GetWeaponLarge(slot.MasterID); }));
}

auto MCLogMech::GetSmallWeaponCount(int32_t location) const -> int32_t
{
    // Ammunition counts as a small weapon.
    auto smallWeapon = [](const ItemSlot& slot)
    {
        const MCComponentForm form = LogSlotForm(slot.MasterID);
        return (form == MCComponentForm::WeaponEnergy || form == MCComponentForm::WeaponBallistic ||
                form == MCComponentForm::WeaponMissile || form == MCComponentForm::Ammo) &&
               !GetWeaponLarge(slot.MasterID);
    };

    return static_cast<int32_t>(std::ranges::count_if(ItemSlots[static_cast<size_t>(location)], smallWeapon));
}

auto MCLogMech::LoadDescription(int32_t descIndex) -> void
{
    if (descIndex < 0 || !Description.empty())
    {
        return;
    }

    Description = LoadDescriptionText(DescIndex);
}
