#pragma once

#include "network/MCMultiPlayer.h"

#include <array>
#include <cstdint>
#include <string_view>

class MCInventoryList;
class MCLogMech;
class MCLogVehicle;
class MCMasterComponent;

/// <summary>
/// Port: a weapon family a match can ban (MATCH OPTIONS' WEAPONS rows): its row's name, what its line says, and the
/// rule (MCMatchRule) that bans it. Inner Sphere's and Clan's alike.
/// </summary>
struct MCWeaponFamily
{
    std::string_view Label;
    std::string_view Description;
    uint32_t Rule = 0;
};

/// <summary>Port: the weapon families, in their rows' order (MCWeaponFamilyOf says which a weapon is of).</summary>
inline constexpr std::array WeaponFamilies = {
    MCWeaponFamily{"LRMS", "LONG-RANGE MISSILE RACKS.", MCMatchRule::NoLrms},
    MCWeaponFamily{"SRMS", "SHORT-RANGE AND STREAK RACKS.", MCMatchRule::NoSrms},
    MCWeaponFamily{"THUNDERBOLTS", "THE HEAVY THUNDERBOLT.", MCMatchRule::NoThunderbolts},
    MCWeaponFamily{"PPCS", "PPCS AND ER PPCS.", MCMatchRule::NoPpcs},
    MCWeaponFamily{"LASERS", "EVERY LASER BUT THE PULSE.", MCMatchRule::NoLasers},
    MCWeaponFamily{"PULSE LASERS", "PULSE LASERS OF EVERY SIZE.", MCMatchRule::NoPulseLasers},
    MCWeaponFamily{"FLAMERS", "HEAVY FLAMERS.", MCMatchRule::NoFlamers},
    MCWeaponFamily{"AUTOCANNONS", "ULTRAS AND LB-XS TOO.", MCMatchRule::NoAutocannons},
    MCWeaponFamily{"GAUSS RIFLES", "LIGHT GAUSS AND RAIL GUN TOO.", MCMatchRule::NoGaussRifles},
    MCWeaponFamily{"MACHINE GUNS", "THE LIGHTEST GUNS.", MCMatchRule::NoMachineGuns},
    MCWeaponFamily{"LONG TOMS", "LONG TOM AND SNIPER GUNS.", MCMatchRule::NoLongToms},
};

/// <summary>
/// Port: the rule banning <paramref name="component"/>'s weapon family (WeaponFamilies), by its kind, name and missile
/// type (the artillery pieces, the Long Tom and the Sniper, by their weapon flag); 0 for none (not a weapon).
/// </summary>
uint32_t MCWeaponFamilyOf(const MCMasterComponent& component);

/// <summary>
/// Port: whether master component <paramref name="masterId"/> is banned by <paramref name="rules"/>: a weapon of a
/// banned family, or ammo that only such weapons fire.
/// </summary>
bool MCComponentBanned(int32_t masterId, uint32_t rules);

/// <summary>
/// Port: the weapon bans the match plays (MultiPlayer()->Played's MCMatchRule::WeaponBans; none outside a match), for
/// logistics, which comes before the scenario's rules are in force.
/// </summary>
uint32_t MCPlayedWeaponBans();

/// <summary>
/// Port: takes every copy of a component banned by <paramref name="rules"/> (MCComponentBanned) out of
/// <paramref name="inventory"/> (a profile's critical slots are rebuilt from it as it is written).
/// </summary>
/// <returns>The resource points the copies taken out were worth.</returns>
int32_t MCRemoveBanned(MCInventoryList& inventory, uint32_t rules);

/// <summary>
/// Port: takes the components banned by <paramref name="rules"/> off <paramref name="mech"/> (MCRemoveBanned), its
/// tonnage, value and battle rating with them: the space they took is free for others.
/// </summary>
void MCRemoveBannedWeapons(MCLogMech& mech, uint32_t rules);

/// <summary>Port: takes the components banned by <paramref name="rules"/> off <paramref name="vehicle"/>, its value with them.</summary>
void MCRemoveBannedWeapons(MCLogVehicle& vehicle, uint32_t rules);
