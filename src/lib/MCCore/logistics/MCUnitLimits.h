#pragma once

/// <summary>The most mechs and vehicles the player may own (inventory and force; a game rule).</summary>
inline constexpr int32_t OriginalMaxUnits = 50;

/// <summary>From this many units on, a purchase warns that the limit is near.</summary>
inline constexpr int32_t OriginalUnitWarning = 40;

/// <summary>Port-only (MoreUnits): the most mechs and vehicles a player outside a match may own.</summary>
inline constexpr int32_t RaisedMaxUnits = 100;

/// <summary>Port-only (MoreUnits): from this many units on, a purchase outside a match warns that the limit is near.</summary>
inline constexpr int32_t RaisedUnitWarning = 80;

/// <summary>
/// Port-only: PREFS "MoreUnits" (gameplay): 1 lets the player own <see cref="RaisedMaxUnits"/> mechs and vehicles
/// outside a match, warned from <see cref="RaisedUnitWarning"/>; 0 as the original, 50 warned from 40. Off by default.
/// A match keeps the original's limit (<see cref="MaxUnits"/>).
/// </summary>
extern int GMoreUnits;

/// <summary>The player's resource points.</summary>
extern int32_t ResourcePoints;

/// <summary>
/// The most mechs and vehicles the player may own: the original's 50, or <see cref="RaisedMaxUnits"/> with MoreUnits
/// outside a match. Every count against the limit asks it.
/// </summary>
int32_t MaxUnits();

/// <summary>
/// From how many units on a purchase warns that the limit is near: the original's 40, or
/// <see cref="RaisedUnitWarning"/> with MoreUnits outside a match.
/// </summary>
int32_t UnitWarning();

/// <summary>How many mechs and vehicles the player owns (inventory and force).</summary>
int32_t NumUnits();

/// <summary>How many units a purchase may buy: the room left under <see cref="MaxUnits"/>, or the stock when that is less.</summary>
int32_t MaxPurchase(int32_t available);

/// <summary>Warns (a dialog) when the player owns <see cref="MaxUnits"/> units or more.</summary>
/// <returns>True when no more may be bought.</returns>
bool CheckMaxUnits();

/// <summary>Warns (a dialog) when the player owns <see cref="UnitWarning"/> units or more.</summary>
void CheckNumUnits();
