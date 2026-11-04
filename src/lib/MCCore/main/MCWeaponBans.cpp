#include "stdafx.h"
#include "main/MCWeaponBans.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMech.h"
#include "main/MCLogVehicle.h"
#include "object/MCMasterComponent.h"

#include <algorithm>

namespace
{
    /// <summary>The master component's missile types (MCMasterComponent::MissileType).</summary>
    constexpr uint8_t SrmMissiles = 1;
    constexpr uint8_t LrmMissiles = 2;
    constexpr uint8_t StreakMissiles = 3;
    /// <summary>The weapon flag of artillery pieces (MCMasterComponent::WeaponFlags): the Long Tom, the Sniper.</summary>
    constexpr uint8_t ArtilleryFlag = 8;

    /// <summary>Whether <paramref name="component"/>'s name has <paramref name="word"/>, ASCII case ignored.</summary>
    bool NameHas(const MCMasterComponent& component, std::string_view word)
    {
        const std::string_view name = component.Name;

        for (size_t at = 0; at + word.size() <= name.size(); at++)
        {
            if (MCIEquals(name.substr(at, word.size()), word))
            {
                return true;
            }
        }

        return false;
    }

    bool IsWeapon(MCComponentForm form)
    {
        return form == MCComponentForm::WeaponEnergy || form == MCComponentForm::WeaponBallistic ||
               form == MCComponentForm::WeaponMissile;
    }

    /// <summary>The copies of components taken out of an inventory: what they were worth and weighed.</summary>
    struct Removed
    {
        int32_t Copies = 0;
        int32_t ResourcePoints = 0;
        float Tonnage = 0.0f;
    };

    Removed RemoveBanned(MCInventoryList& inventory, uint32_t rules)
    {
        Removed removed;
        std::erase_if(inventory.Items,
                      [&](const std::shared_ptr<MCLogInventoryItem>& item)
                      {
                          if (!MCComponentBanned(item->MasterID, rules))
                          {
                              return false;
                          }

                          const MCMasterComponent& master = MasterComponentList[item->MasterID];
                          const auto copies = static_cast<int32_t>(item->Stats.size());
                          removed.Copies += copies;
                          removed.ResourcePoints += master.ResourcePoints * copies;
                          removed.Tonnage += master.Tonnage * static_cast<float>(copies);
                          return true;
                      });
        return removed;
    }
}

uint32_t MCWeaponFamilyOf(const MCMasterComponent& component)
{
    switch (component.Form)
    {
        case MCComponentForm::WeaponEnergy:
        {
            if (NameHas(component, "PPC"))
            {
                return MCMatchRule::NoPpcs;
            }

            if (NameHas(component, "Flamer"))
            {
                return MCMatchRule::NoFlamers;
            }

            // The large pulse lasers are "Large PS Laser".
            if (NameHas(component, "Pulse") || NameHas(component, "PS Laser"))
            {
                return MCMatchRule::NoPulseLasers;
            }

            return NameHas(component, "Laser") ? MCMatchRule::NoLasers : 0;
        }
        case MCComponentForm::WeaponBallistic:
        {
            if (NameHas(component, "Gauss") || NameHas(component, "Rail Gun"))
            {
                return MCMatchRule::NoGaussRifles;
            }

            if (NameHas(component, "Machine Gun"))
            {
                return MCMatchRule::NoMachineGuns;
            }

            // The clans' light LB-X is "C Light LBX".
            return NameHas(component, "Autocannon") || NameHas(component, "LBX") ? MCMatchRule::NoAutocannons : 0;
        }
        case MCComponentForm::WeaponMissile:
        {
            // The Heavy Thunderbolt fires as an LRM.
            if (NameHas(component, "Thunderbolt"))
            {
                return MCMatchRule::NoThunderbolts;
            }

            if ((component.WeaponFlags & ArtilleryFlag) != 0)
            {
                return MCMatchRule::NoLongToms;
            }

            if (component.MissileType == LrmMissiles)
            {
                return MCMatchRule::NoLrms;
            }

            return component.MissileType == SrmMissiles || component.MissileType == StreakMissiles ? MCMatchRule::NoSrms
                                                                                                   : 0;
        }
        default:
            return 0;
    }
}

bool MCComponentBanned(int32_t masterId, uint32_t rules)
{
    if ((rules & MCMatchRule::WeaponBans) == 0 || masterId < 0 ||
        static_cast<size_t>(masterId) >= MasterComponentList.size())
    {
        return false;
    }

    const MCMasterComponent& component = MasterComponentList[static_cast<size_t>(masterId)];

    if (IsWeapon(component.Form))
    {
        return (rules & MCWeaponFamilyOf(component)) != 0;
    }

    if (component.Form != MCComponentForm::Ammo)
    {
        return false;
    }

    // Ammo goes with the weapons that fire it: banned once every one of them is.
    bool fired = false;

    for (const MCMasterComponent& weapon : MasterComponentList)
    {
        if (weapon.MasterID < 0 || !IsWeapon(weapon.Form) || weapon.AmmoMasterId != masterId)
        {
            continue;
        }

        if ((rules & MCWeaponFamilyOf(weapon)) == 0)
        {
            return false;
        }

        fired = true;
    }

    return fired;
}

uint32_t MCPlayedWeaponBans()
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr ? game->Played.Rules.value_or(0) & MCMatchRule::WeaponBans : 0;
}

int32_t MCRemoveBanned(MCInventoryList& inventory, uint32_t rules)
{
    return RemoveBanned(inventory, rules).ResourcePoints;
}

void MCRemoveBannedWeapons(MCLogMech& mech, uint32_t rules)
{
    const Removed removed = RemoveBanned(*mech.Inventory, rules);

    if (removed.Copies == 0)
    {
        return;
    }

    // Weapons and their ammo weigh in the weapons' share too (MCLogMechList::AddMech), so the space for weapons
    // (FreeTonnage) is as it was.
    mech.UsedTonnage -= removed.Tonnage;
    mech.WeaponTonnage -= removed.Tonnage;
    mech.ResourcePoints -= removed.ResourcePoints;
    mech.CalcBR();
}

void MCRemoveBannedWeapons(MCLogVehicle& vehicle, uint32_t rules)
{
    RemoveBanned(*vehicle.Inventory, rules);
    vehicle.CalcVehicleCost();
}
