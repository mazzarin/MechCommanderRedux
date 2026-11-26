#include "stdafx.h"
#include "main/MCLogMech.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "object/MCBattleMech.h"

namespace
{
    /// <summary>
    /// A weapon's worth in the condition figures: its damage per 10 seconds, as a short, times its long range over 24,
    /// as a short.
    /// </summary>
    int16_t WeaponWorth(const MCMasterComponent& component)
    {
        auto perTen = static_cast<int16_t>(static_cast<int32_t>(static_cast<double>(component.Damage) * 10.0 /
                                                                static_cast<double>(component.RecycleTime)));
        return static_cast<int16_t>(
            static_cast<int32_t>(static_cast<double>(perTen) * static_cast<double>(component.WeaponRange[3]) *
                                 static_cast<double>(0.041666668f)));
    }
}

MCLogPart::MCLogPart() = default;

MCLogPart::~MCLogPart() = default;

MCLogMech::MCLogMech()
{
    PartType = 1;
}

MCLogMech::~MCLogMech()
{
    Inventory.reset();
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

auto MCLogMech::TakeArrivalState() -> void
{
    for (size_t i = 0; i < Armor.size(); ++i)
    {
        Arrived.Armor[i] = Armor[i].CurArmor;
    }

    for (size_t i = 0; i < Internals.size(); ++i)
    {
        Arrived.Internals[i] = Internals[i].CurArmor;
    }

    Arrived.EngineHits = 0;

    if (Inventory == nullptr)
    {
        return;
    }

    const auto engine =
        std::ranges::find_if(Inventory->Items, [](const std::shared_ptr<MCLogInventoryItem>& item)
                             { return MasterComponentList[item->MasterID].Form == MCComponentForm::Engine; });

    if (engine != Inventory->Items.end() && !(*engine)->Stats.empty())
    {
        Arrived.EngineHits = (*engine)->Stats.front()->Hits;
    }
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

auto MCLogMech::CalcStatus() const -> float
{
    if (PilotIndex < 0 && NetworkPilot.expired())
    {
        return 0.0f;
    }

    float pilotFactor = 0.0f;
    std::shared_ptr<MCLogWarrior> warrior;

    if (!LocalPart)
    {
        warrior = NetworkPilot.lock();
    }
    else
    {
        GlobalLog()->AssignedWarriorList->GetWarriorInfo(PilotIndex, warrior);
    }

    if (warrior != nullptr)
    {
        pilotFactor = static_cast<float>(static_cast<double>(warrior->Skills[3]) * 0.02);
    }

    const float firepower = FirepowerLeft(pilotFactor);

    if (firepower == 0.0f)
    {
        return 0.0f;
    }

    const MCBodyStatus body = BodyStatus();

    if (body.Body == 0.0)
    {
        return 0.0f;
    }

    // The pilot's wounds.
    static constexpr float woundFactors[7] = {1.0f, 0.95f, 0.85f, 0.75f, 0.5f, 0.3f, 0.0f};
    float woundFactor = body.Head;

    if (warrior != nullptr)
    {
        auto wounds = static_cast<int32_t>(warrior->Wounds);
        // Port fix: wounds past the table read 0 (the original read the stack beyond it).
        woundFactor = wounds >= 0 && wounds < 7 ? woundFactors[wounds] : 0.0f;
    }

    return static_cast<float>(static_cast<double>(woundFactor) * body.Body * firepower);
}

auto MCLogMech::CalcCondition() const -> float
{
    // Any pilot's factor: the firepower's share is the same whatever it is, past none.
    constexpr float AnyPilotFactor = 1.0f;
    const float firepower = FirepowerLeft(AnyPilotFactor);

    if (firepower == 0.0f)
    {
        return 0.0f;
    }

    return static_cast<float>(BodyStatus().Body * firepower);
}

auto MCLogMech::FirepowerLeft(float pilotFactor) const -> float
{
    // The undamaged weapons' worth over all weapons' worth (0/0 without a pilot), each by the pilot's factor.
    double working = 0.0;
    double total = 0.0;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Inventory->Items)
    {
        const MCMasterComponent& component = MasterComponentList[item->MasterID];

        if (!IsWeapon(component.Form) || item->Stats.empty())
        {
            continue;
        }

        int16_t value = WeaponWorth(component);

        for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
        {
            if (stat->Hits == 0)
            {
                working += static_cast<double>(value) * pilotFactor;
            }

            total += static_cast<double>(value) * pilotFactor;
        }
    }

    auto firepower = static_cast<float>(working / total);
    return std::isnan(firepower) ? 0.0f : firepower;
}

auto MCLogMech::BodyStatus() const -> MCBodyStatus
{
    // The head's armor, the weaker of two torso locations, the legs' and the side torsos' armor.
    auto head = static_cast<float>(static_cast<double>(Armor[0].CurArmor) / Armor[0].MaxArmor * 0.6 + 0.4);
    double weakCurrent = Armor[1].CurArmor;
    double weakMaximum = Armor[1].MaxArmor;

    if (static_cast<double>(Armor[8].CurArmor) < weakCurrent)
    {
        weakCurrent = Armor[8].CurArmor;
        weakMaximum = Armor[8].MaxArmor;
    }

    auto legs = static_cast<float>(static_cast<double>(Armor[5].CurArmor + Armor[4].CurArmor) /
                                       (Armor[5].MaxArmor + Armor[4].MaxArmor) * 0.25 +
                                   0.75);
    double sides = static_cast<double>(Armor[9].CurArmor + Armor[10].CurArmor + Armor[3].CurArmor + Armor[2].CurArmor) /
                       (Armor[10].MaxArmor + Armor[9].MaxArmor + Armor[3].MaxArmor + Armor[2].MaxArmor) * 0.25 +
                   0.75;
    double body = sides * ((weakCurrent / weakMaximum + 1.0) * 0.5) * legs * legs * head;
    return MCBodyStatus{.Body = std::isnan(body) ? 0.0 : body, .Head = head};
}
