#include "stdafx.h"
#include "object/MCMover.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "mission/MCWeatherPlay.h"
#include "mission/MCDifficultySettings.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCSortList.h"
#include "object/MCTeam.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"

// The mover's weapons and ammo: ranges, readiness, attack chances, refits.

namespace
{
    /// <summary>
    /// The entries of the sort list the movers shared in MCX.EXE; a list as long keeps the order of the filler
    /// entries it sorted after the real ones.
    /// </summary>
    constexpr int32_t MoverSortListSize = 100;
}

auto MCMover::GetContacts(std::span<int32_t> contactList, int32_t contactCriteria, int32_t sortType) -> int32_t
{
    return Team.lock()->GetContacts(*this, contactList, contactCriteria, sortType);
}

auto MCMover::WeaponLocked(int32_t weaponIndex, MCVector3D targetPosition) -> float
{
    return RelFacingTo(targetPosition, -1);
}

auto MCMover::WeaponInRange(int32_t weaponIndex, float metersToTarget) -> int32_t
{
    const MCMasterComponent& weapon = MasterComponentList[Inventory[weaponIndex].MasterID];

    if (metersToTarget <= weapon.WeaponRange[0])
    {
        return 0;
    }

    if (metersToTarget <= weapon.WeaponRange[1])
    {
        return 2;
    }

    if (metersToTarget <= weapon.WeaponRange[2])
    {
        return 3;
    }

    return weapon.WeaponRange[3] < metersToTarget ? 0 : 4;
}

namespace
{
    /// <summary>
    /// How many entries of <paramref name="list"/> a script's <paramref name="listSize"/> names: no more than the list
    /// holds (issue #25: the size comes from the script, and the original read and wrote that many whatever the
    /// array).
    /// </summary>
    int32_t ListEntries(std::span<const int32_t> list, int32_t listSize)
    {
        return std::clamp<int32_t>(listSize, 0, static_cast<int32_t>(list.size()));
    }

    /// <summary>Writes <paramref name="value"/> at <paramref name="index"/> of <paramref name="list"/>, if it has it.</summary>
    void PutEntry(std::span<int32_t> list, int32_t index, int32_t value)
    {
        if (index >= 0 && static_cast<size_t>(index) < list.size())
        {
            list[static_cast<size_t>(index)] = value;
        }
    }
}

auto MCMover::GetWeaponsReady(std::span<int32_t> list, int32_t listSize) -> int32_t
{
    int32_t numReady = 0;

    if (listSize == -1)
    {
        for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
        {
            if (IsWeaponReady(i) != 0)
            {
                PutEntry(list, numReady, i);
                numReady++;
            }
        }
    }
    else
    {
        // A listed index that isn't one of the mover's weapons isn't ready (the original read the inventory there).
        for (int32_t i = 0; i < ListEntries(list, listSize); i++)
        {
            const int32_t weaponIndex = list[static_cast<size_t>(i)];

            if (IsWeaponIndex(weaponIndex) != 0 && IsWeaponReady(weaponIndex) != 0)
            {
                PutEntry(list, numReady, weaponIndex);
                numReady++;
            }
        }
    }

    return numReady;
}

auto MCMover::GetWeaponsLocked(std::span<int32_t> list, int32_t listSize) -> int32_t
{
    std::shared_ptr<MCGameObject> target = Pilot.lock()->GetLastTarget();

    if (target == nullptr)
    {
        return -2;
    }

    const MCVector3D targetPosition = target->GetPosition();
    int32_t numLocked = 0;
    const float fireArc = GetFireArc();
    const float negFireArc = -fireArc;

    if (listSize == -1)
    {
        for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
        {
            const float facing = WeaponLocked(i, targetPosition);

            if (negFireArc <= facing && facing <= fireArc)
            {
                PutEntry(list, numLocked, i);
                numLocked++;
            }
        }
    }
    else
    {
        for (int32_t i = 0; i < ListEntries(list, listSize); i++)
        {
            const int32_t weaponIndex = list[static_cast<size_t>(i)];
            const float facing = WeaponLocked(weaponIndex, targetPosition);

            if (negFireArc <= facing && facing <= fireArc)
            {
                PutEntry(list, numLocked, weaponIndex);
                numLocked++;
            }
        }
    }

    return numLocked;
}

auto MCMover::GetWeaponsInRange(std::span<int32_t> list, int32_t listSize, float orderFireRange) -> int32_t
{
    std::shared_ptr<MCGameObject> target = Pilot.lock()->GetLastTarget();

    if (target == nullptr)
    {
        return -2;
    }

    MCVector3D targetPosition = target->GetPosition();
    const float metersToTarget = static_cast<float>(DistanceFrom(targetPosition));
    int32_t numInRange = 0;

    if (listSize == -1)
    {
        for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
        {
            if (WeaponInRange(i, metersToTarget) != 0)
            {
                PutEntry(list, numInRange, i);
                numInRange++;
            }
        }
    }
    else
    {
        // A listed index that isn't one of the mover's weapons isn't in range (the original read the inventory there).
        for (int32_t i = 0; i < ListEntries(list, listSize); i++)
        {
            const int32_t weaponIndex = list[static_cast<size_t>(i)];

            if (IsWeaponIndex(weaponIndex) != 0 && WeaponInRange(weaponIndex, metersToTarget) != 0)
            {
                PutEntry(list, numInRange, weaponIndex);
                numInRange++;
            }
        }
    }

    return numInRange;
}

auto MCMover::GetWeaponShots(int32_t weaponIndex) -> int32_t
{
    if (IsWeaponIndex(weaponIndex) == 0)
    {
        return -1;
    }

    // Weapons without ammo (energy) never run out.
    if (MasterComponentList[Inventory[weaponIndex].MasterID].MissileType == 0)
    {
        return 9999;
    }

    return AmmoTypeTotal[Inventory[weaponIndex].AmmoIndex].CurAmount;
}

auto MCMover::GetWeaponAmmoLevel(int32_t weaponIndex) -> float
{
    if (IsWeaponIndex(weaponIndex) == 0)
    {
        return -1.0f;
    }

    const MCAmmoTally& ammo = AmmoTypeTotal[Inventory[weaponIndex].AmmoIndex];
    return static_cast<float>(static_cast<double>(ammo.CurAmount) / ammo.StartAmount);
}

auto MCMover::CalcWeaponEffectiveness(int setMax) -> void
{
    int32_t effectiveness = 0;
    LastWeaponEffectivenessCalc = ScenarioTime;
    float gunneryFactor = 1.0f;

    if (Pilot.lock() != nullptr)
    {
        gunneryFactor = static_cast<float>(static_cast<double>(Pilot.lock()->Skills[SkillGunnery]) * 0.02);
    }

    for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
    {
        if (setMax != 0 || (Inventory[i].Disabled == 0 && GetWeaponShots(i) > 0))
        {
            effectiveness =
                static_cast<int32_t>(static_cast<double>(Inventory[i].Effectiveness) * gunneryFactor + effectiveness);
        }
    }

    if (setMax != 0)
    {
        MaxWeaponEffectiveness = static_cast<float>(effectiveness);
        return;
    }

    WeaponEffectiveness = static_cast<float>(effectiveness);

    if (effectiveness == 0)
    {
        PlayMessage(static_cast<MCRadioMessageType>(0x23), 0);
    }
    else if (static_cast<double>(effectiveness) < static_cast<double>(MaxWeaponEffectiveness) * 0.5f)
    {
        PlayMessage(static_cast<MCRadioMessageType>(0x22), 0);
    }
}

auto MCMover::CalcWeaponRangeRatings() -> void
{
    for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
    {
        if (NumRangeRatings <= 0)
        {
            continue;
        }

        const double gunnery = Pilot.lock()->Skills[SkillGunnery];
        const MCMasterComponent& weapon = MasterComponentList[Inventory[i].MasterID];
        std::vector<float>& ratings = Inventory[i].RangeRatings;

        for (int32_t step = 0; step < NumRangeRatings; step++)
        {
            const float range = static_cast<float>(static_cast<double>(step) * RangeRatingIncrement);
            // Out of the weapon's range: 1000 less.
            double value = gunnery;

            if (!(weapon.WeaponRange[0] < range) ||
                (weapon.WeaponRange[1] < range && weapon.WeaponRange[2] < range && weapon.WeaponRange[3] < range))
            {
                value -= 1000.0;
            }

            ratings[static_cast<size_t>(step) * 2] = static_cast<float>(value);
            ratings[static_cast<size_t>(step) * 2 + 1] =
                static_cast<float>(weapon.Damage * value * 10.0 / weapon.RecycleTime);
        }
    }
}

auto MCMover::CalcAmmoTotals() -> void
{
    AmmoTypeTotal.clear();

    if (NumWeapons == 0)
    {
        return;
    }

    // One type per weapon ammo (9999 rounds for a weapon without ammo), then the bins' rounds.
    const int32_t firstAmmo = NumOther + NumWeapons;

    for (int32_t i = NumOther; i < firstAmmo; i++)
    {
        const MCMasterComponent& weapon = MasterComponentList[Inventory[i].MasterID];

        if (std::ranges::contains(AmmoTypeTotal, weapon.AmmoMasterId, &MCAmmoTally::MasterId))
        {
            continue;
        }

        const int32_t rounds = weapon.MissileType == 0 ? 9999 : 0;
        AmmoTypeTotal.push_back({weapon.AmmoMasterId, rounds, rounds});
    }

    for (int32_t i = firstAmmo; i < firstAmmo + NumAmmos; i++)
    {
        const auto type =
            std::ranges::find(AmmoTypeTotal, static_cast<int32_t>(Inventory[i].MasterID), &MCAmmoTally::MasterId);

        if (type != AmmoTypeTotal.end())
        {
            type->CurAmount += Inventory[i].Amount;
            type->StartAmount += Inventory[i].Amount;
        }
    }
}

auto MCMover::CalcOptimalRange(std::shared_ptr<MCGameObject> target) -> int
{
    const float oldRange = OptimalRange;
    LastOptimalRangeCalc = ScenarioTime;

    if (target == nullptr)
    {
        target = GetPilot()->GetLastTarget();
    }

    const float fireRange = GetFireRange(-2);

    // Outranging a mover target: stay just inside the longest range.
    if (target != nullptr && IsMoverClass(target->ObjectClass) &&
        std::static_pointer_cast<MCMover>(target)->LongestRangeWeapon != 0xff &&
        !(fireRange <= std::static_pointer_cast<MCMover>(target)->GetFireRange(-2)))
    {
        OptimalRange = static_cast<float>(static_cast<double>(fireRange) - 10.0);
        return OptimalRange != oldRange ? 1 : 0;
    }

    // Else the range step whose summed ratings (then damage rates, then the farthest step) are best, ranked in a
    // sort list of at least the movers' shared list's 100 entries.
    MCSortList sortList(std::max(MoverSortListSize, NumRangeRatings));
    auto setItem = [&sortList](int32_t index, float value, int32_t id)
    {
        if (index > -1 && index < sortList.NumItems())
        {
            sortList.List[index].Id = id;
            sortList.List[index].Value = value;
        }
    };

    int32_t numWorking = 0;
    sortList.Clear(true);

    for (int32_t step = 0; step < NumRangeRatings; step++)
    {
        float total = 0.0f;

        for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
        {
            if (Inventory[i].Disabled == 0 && GetWeaponShots(i) > 0)
            {
                if (step == 0)
                {
                    numWorking++;
                }

                total += Inventory[i].RangeRatings[step * 2];
            }
        }

        setItem(step, total, step);
    }

    if (NumRangeRatings <= 0 || numWorking == 0)
    {
        OptimalRange = 0.0f;
        return oldRange != 0.0f ? 1 : 0;
    }

    sortList.Sort(true);
    int32_t bestStep = sortList.List[0].Id;

    if (sortList.List[1].Value == sortList.List[0].Value)
    {
        sortList.Clear(true);

        for (int32_t step = 0; step < NumRangeRatings; step++)
        {
            float total = 0.0f;

            for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
            {
                if (Inventory[i].Disabled == 0 && GetWeaponShots(i) > 0)
                {
                    total += Inventory[i].RangeRatings[step * 2 + 1];
                }
            }

            setItem(step, total, step);
        }

        sortList.Sort(true);
        const std::span<const MCSortListNode> nodes = sortList.List;
        bestStep = nodes[0].Id;

        if (nodes[1].Value == nodes[0].Value)
        {
            const float bestValue = nodes[0].Value;
            size_t index = 0;

            // The original walked on while the values matched, past the list's end when they all did.
            do
            {
                if (bestStep < nodes[index].Id)
                {
                    bestStep = nodes[index].Id;
                }

                index++;
            } while (index < nodes.size() && nodes[index].Value == bestValue);
        }
    }

    OptimalRange = static_cast<float>(bestStep) * RangeRatingIncrement;
    return OptimalRange != oldRange ? 1 : 0;
}

auto MCMover::CalcLongestRangeWeapon() -> int32_t
{
    float longestRange = 0.0f;
    float shortestRange = 1000000.0f;
    LongestRangeWeapon = 0xff;
    ShortestRangeWeapon = 0xff;
    MaxMinRange = 0.0f;

    for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
    {
        if (Inventory[i].Disabled != 0 || GetWeaponShots(i) <= 0)
        {
            continue;
        }

        const MCMasterComponent& weapon = MasterComponentList[Inventory[i].MasterID];

        if (longestRange < weapon.WeaponRange[3])
        {
            LongestRangeWeapon = static_cast<uint8_t>(i);
            longestRange = weapon.WeaponRange[3];
        }

        if (weapon.WeaponRange[1] < shortestRange)
        {
            ShortestRangeWeapon = static_cast<uint8_t>(i);
            shortestRange = weapon.WeaponRange[1];
        }

        if (MaxMinRange < weapon.WeaponRange[0])
        {
            MaxMinRange = weapon.WeaponRange[0];
        }
    }

    return LongestRangeWeapon;
}

auto MCMover::GetFireRange(int32_t which) -> float
{
    switch (which)
    {
        case 0:
            return WeaponRange[0];
        case 1:
            return WeaponRange[1];
        case 2:
            return WeaponRange[2];
        case -4:
            return DefaultAttackRange;
        case -3:
            return 0.0f;
        case -2:
        {
            if (LongestRangeWeapon != 0xff)
            {
                return MasterComponentList[Inventory[LongestRangeWeapon].MasterID].WeaponRange[3];
            }
            break;
        }
        case -1:
            return OptimalRange;
        default:
            break;
    }

    return -1.0f;
}

auto MCMover::GetMaxFireRange() -> float
{
    return GetFireRange(-2);
}

auto MCMover::IsWeaponIndex(int32_t itemIndex) -> int
{
    return NumOther <= itemIndex && itemIndex < NumOther + NumWeapons ? 1 : 0;
}

auto MCMover::IsWeaponMissile(int32_t weaponIndex) -> int
{
    return MasterComponentList[Inventory[weaponIndex].MasterID].Form == MCComponentForm::WeaponMissile ? 1 : 0;
}

auto MCMover::IsWeaponReady(int32_t weaponIndex) -> int
{
    if (Inventory[weaponIndex].Disabled != 0)
    {
        return 0;
    }

    if (ScenarioTime < Inventory[weaponIndex].ReadyTime)
    {
        return 0;
    }

    return 1;
}

auto MCMover::IsWeaponWorking(int32_t weaponIndex) -> int
{
    if (Inventory[weaponIndex].Disabled != 0)
    {
        return 0;
    }

    return GetWeaponShots(weaponIndex) != 0 ? 1 : 0;
}

auto MCMover::StartWeaponRecycle(int32_t weaponIndex) -> void
{
    Inventory[weaponIndex].ReadyTime = MasterComponentList[Inventory[weaponIndex].MasterID].RecycleTime + ScenarioTime;
}

auto MCMover::TallyAmmo(int32_t ammoMasterId) -> int32_t
{
    int32_t total = 0;
    const int32_t firstAmmo = NumOther + NumWeapons;

    for (int32_t i = firstAmmo; i < firstAmmo + NumAmmos; i++)
    {
        if (Inventory[i].MasterID == ammoMasterId)
        {
            total += Inventory[i].Amount;
        }
    }

    return total;
}

auto MCMover::NeedsRefit(int armorOnly) -> int
{
    // Only a mech without a refit vehicle on the way.
    if (LockObject(RefitBuddy) != nullptr || ObjectClass != MCObjectClass::BattleMech)
    {
        return 0;
    }

    if (armorOnly == 0)
    {
        for (int32_t i = 0; i < NumArmorLocations(); i++)
        {
            if (i < NumBodyLocations())
            {
                // A destroyed arm needs neither structure nor armor.
                if ((i == MechLeftArm || i == MechRightArm) && BodyAt(i).DamageState == 2)
                {
                    continue;
                }

                if (BodyAt(i).CurInternalStructure < static_cast<float>(BodyAt(i).MaxInternalStructure))
                {
                    return 1;
                }
            }

            if (Armor[i].CurArmor < static_cast<float>(Armor[i].MaxArmor))
            {
                return 1;
            }
        }
    }

    for (int32_t i = 0; i < NumAmmoTypes(); i++)
    {
        if (AmmoTypeTotal[i].CurAmount < AmmoTypeTotal[i].StartAmount)
        {
            return 1;
        }
    }

    return 0;
}

auto MCMover::ReduceAmmo(int32_t ammoMasterId, int32_t amount) -> int32_t
{
    // From the bins in order.
    int32_t left = amount;
    const int32_t firstAmmo = NumOther + NumWeapons;

    for (int32_t i = firstAmmo; i < firstAmmo + NumAmmos; i++)
    {
        if (Inventory[i].MasterID != ammoMasterId)
        {
            continue;
        }

        if (left < Inventory[i].Amount)
        {
            Inventory[i].Amount = static_cast<int16_t>(Inventory[i].Amount - left);
            break;
        }

        left -= Inventory[i].Amount;
        Inventory[i].Amount = 0;
    }

    // Out of this ammo: the weapons, their effectiveness and the optimal range change.
    for (int32_t i = 0; i < NumAmmoTypes(); i++)
    {
        if (AmmoTypeTotal[i].MasterId != ammoMasterId)
        {
            continue;
        }

        const int32_t rounds = AmmoTypeTotal[i].CurAmount - amount;
        AmmoTypeTotal[i].CurAmount = rounds;

        if (rounds < 1)
        {
            AmmoTypeTotal[i].CurAmount = 0;
            CalcLongestRangeWeapon();
            CalcWeaponEffectiveness(0);
            CalcOptimalRange(nullptr);
        }

        return amount;
    }

    return amount;
}

auto MCMover::DeductWeaponShot(int32_t weaponIndex, int32_t ammoAmount) -> void
{
    if (ammoAmount > 0)
    {
        ReduceAmmo(MasterComponentList[Inventory[weaponIndex].MasterID].AmmoMasterId, ammoAmount);
    }
}

auto MCMover::SortWeapons(std::span<int32_t> weaponList, std::span<int32_t> valueList, int32_t listSize,
                          int32_t sortType, int skillCheck) -> int32_t
{
    std::shared_ptr<MCMechWarrior> myPilot = Pilot.lock();
    std::shared_ptr<MCGameObject> target = myPilot->GetLastTarget();

    if (target == nullptr)
    {
        return -2;
    }

    int32_t aimLocation = -1;

    if (myPilot != nullptr && myPilot->CurTacOrder.IsCombatOrder() != 0)
    {
        aimLocation = myPilot->CurTacOrder.AttackParams.AimLocation;
    }

    // Best attack chance first, ranked in a sort list of at least the movers' shared list's 100 entries; only sort
    // type 0 is known (the id goes in before the type is checked).
    MCSortList sortList(std::max<int32_t>({MoverSortListSize, NumWeapons, listSize}));
    auto setId = [&sortList](int32_t index, int32_t id)
    {
        if (index > -1 && index < sortList.NumItems())
        {
            sortList.List[index].Id = id;
        }
    };

    auto setValue = [&sortList](int32_t index, float value)
    {
        if (index > -1 && index < sortList.NumItems())
        {
            sortList.List[index].Value = value;
        }
    };

    sortList.Clear(true);

    if (listSize == -1)
    {
        for (int32_t i = NumOther; i < NumOther + NumWeapons; i++)
        {
            setId(i - NumOther, i);

            if (sortType != 0)
            {
                return -3;
            }

            int32_t range = 0;
            setValue(i - NumOther, CalcAttackChance(target, aimLocation, ScenarioTime, i, 0.0f, range, std::nullopt));
        }

        sortList.Sort(true);
        listSize = NumWeapons;

        if (listSize == 0)
        {
            return 0;
        }
    }
    else
    {
        listSize = ListEntries(weaponList, listSize);

        for (int32_t i = 0; i < listSize; i++)
        {
            setId(i, weaponList[i]);
            float chance;

            if (weaponList[i] == -1)
            {
                chance = -999.0f;
            }
            else
            {
                if (sortType != 0)
                {
                    return -3;
                }

                int32_t range = 0;
                chance = CalcAttackChance(target, aimLocation, ScenarioTime, weaponList[i], 0.0f, range, std::nullopt);
            }

            setValue(i, chance);
        }

        sortList.Sort(true);
    }

    // Only as many as both lists hold (the value list was the caller's 48 entries; issue #25).
    for (int32_t i = 0; i < std::min({listSize, ListEntries(weaponList, listSize), ListEntries(valueList, listSize)});
         i++)
    {
        weaponList[i] = sortList.List[i].Id;
        valueList[i] = static_cast<int32_t>(sortList.List[i].Value);
    }

    return 0;
}

auto MCMover::CalcAttackChance(std::shared_ptr<MCGameObject> target, int32_t aimLocation, float targetTime,
                               int32_t weaponIndex, float modifiers, int32_t& range,
                               std::optional<MCVector3D> targetPoint) -> float
{
    if (weaponIndex < NumOther || NumOther + NumWeapons <= weaponIndex)
    {
        return -9999.0f;
    }

    MCVector3D targetPosition;

    if (target == nullptr)
    {
        if (!targetPoint)
        {
            return -9999.0f;
        }

        targetPosition = *targetPoint;
    }
    else
    {
        targetPosition = target->GetPosition();
    }

    float gunnery = static_cast<float>(Pilot.lock()->Skills[SkillGunnery]);

    if (MultiPlayer() == nullptr)
    {
        if (GetAlignment() == HomeTeam()->Alignment)
        {
            gunnery = ApplyDifficultySkill(gunnery, 1);
        }
        else if (MultiPlayer() == nullptr && GetAlignment() != HomeTeam()->Alignment)
        {
            gunnery = ApplyDifficultySkill(gunnery, 0);
        }
    }

    const float metersToTarget = static_cast<float>(DistanceFrom(targetPosition));

    if (metersToTarget <= WeaponRange[0])
    {
        range = 0;
    }
    else if (metersToTarget <= WeaponRange[1])
    {
        range = 1;
    }
    else
    {
        range = 2;
    }

    // Out of the weapon's range: -1.
    const MCMasterComponent& weapon = MasterComponentList[Inventory[weaponIndex].MasterID];
    float rangeModifier;

    if (!(weapon.WeaponRange[0] < metersToTarget))
    {
        return -1.0f;
    }

    if (!(weapon.WeaponRange[1] < metersToTarget))
    {
        rangeModifier = WeaponFireModifiers[0];
    }
    else if (!(weapon.WeaponRange[2] < metersToTarget))
    {
        rangeModifier = WeaponFireModifiers[1];
    }
    else if (!(weapon.WeaponRange[3] < metersToTarget))
    {
        // Port: at long range, less the match's weather's (MCWeatherPlay::LongAim; outside a match, nothing).
        rangeModifier = WeaponFireModifiers[2] + MCWeatherPlayNow().LongAim;
    }
    else
    {
        return -1.0f;
    }

    modifiers = rangeModifier + modifiers;

    // Port fix: the original reads a null target's class when aimLocation isn't -1.
    const bool mechTarget = target != nullptr && target->ObjectClass == MCObjectClass::BattleMech;

    if (aimLocation > -1 && mechTarget)
    {
        switch (aimLocation)
        {
            case 0:
                modifiers = WeaponFireModifiers[3] + modifiers;
                break;
            case 1:
            case 2:
            case 3:
                modifiers = WeaponFireModifiers[4] + modifiers;
                break;
            case 4:
            case 5:
            case 6:
            case 7:
                modifiers = WeaponFireModifiers[5] + modifiers;
                break;
            default:
                break;
        }
    }

    // An aimed shot at a mech skips the target's movement.
    if (!(aimLocation != -1 && mechTarget) && target != nullptr)
    {
        if (IsMoverClass(target->ObjectClass))
        {
            GetVelocity();
            const MCVector3D targetVelocity = target->GetVelocity();

            if (target != LockObject(StationaryTarget))
            {
                StationaryTarget = target;
                StationaryTime = 0.0f;
            }
            else
            {
                // A target holding still gets easier, up to MaxStationaryTime.
                const double x = targetVelocity.X;
                const double y = targetVelocity.Y;
                const double z = targetVelocity.Z;

                if (std::sqrt(x * x + y * y + z * z) == 0.0)
                {
                    StationaryTime = FrameLength + StationaryTime;
                }
                else
                {
                    StationaryTime = 0.0f;
                }

                if (StationaryTime != 0.0f)
                {
                    double stationaryFactor = 1.0;

                    if (StationaryTime < MaxStationaryTime)
                    {
                        stationaryFactor = static_cast<double>(StationaryTime) / MaxStationaryTime;
                    }

                    modifiers = static_cast<float>(WeaponFireModifiers[23] * stationaryFactor + modifiers);
                }
            }
        }
        else
        {
            modifiers = WeaponFireModifiers[6] + modifiers;
        }
    }

    return static_cast<float>((static_cast<double>(modifiers) + 100.0f) * 0.01 * gunnery);
}

auto MCMover::AmmoExplosion(int32_t ammoIndex) -> void
{
    Pilot.lock()->Injure(2.0f, 1);
    Assert(ammoIndex < NumOther + NumWeapons + NumAmmos, ammoIndex, " Ammo Index out of range ");
    Assert(NumOther + NumWeapons <= ammoIndex, ammoIndex, " Ammo Index too low ");
    MCInventoryItem& bin = Inventory[ammoIndex];
    const int32_t hitLocation = bin.BodyLocation;
    const int32_t rounds = bin.Amount;
    float damage = static_cast<float>(
        static_cast<double>(static_cast<int32_t>(MasterComponentList[bin.MasterID].Damage)) * rounds);

    if (damage > 254.0f)
    {
        damage = 254.0f;
    }

    bin.Amount = 0;
    const int16_t typeIndex = bin.AmmoIndex;

    if (typeIndex == -1)
    {
        Fatal(-1, " Bad Ammo Index in Ammo Explosion ");
    }

    Assert(typeIndex < NumAmmoTypes(), typeIndex, " Too Many Ammo Types ");
    Assert(typeIndex > -1, typeIndex, " not enough Ammo Types ");
    AmmoTypeTotal[typeIndex].CurAmount -= rounds;
    MCWeaponShotInfo shotInfo;
    shotInfo.Init(nullptr, bin.MasterID, damage, hitLocation, 0.0f);
    HandleWeaponHit(shotInfo, 0);
}
