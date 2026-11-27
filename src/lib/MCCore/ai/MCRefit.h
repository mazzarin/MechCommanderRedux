#pragma once

class MCMover;

/// <summary>
/// Port-only: PREFS "RepairWalkOut" (gameplay): 1 has a mech repaired at a repair bay walk out of it by itself, as the
/// original's repair order means it to (OB-197: the original's power-up dropped the walk, so the mech stayed in the bay
/// until ordered out); 0 as the original. Off by default. Outside a match only: a match keeps the original's
/// (<see cref="MCRepairWalkOutInForce"/>).
/// </summary>
extern int GRepairWalkOut;

/// <summary>Whether a repaired mech walks out of its bay by itself: RepairWalkOut, outside a match.</summary>
[[nodiscard]] bool MCRepairWalkOutInForce();

/// <summary>
/// One round of repairs on <paramref name="mover"/> from <paramref name="refitPoints"/>: a share of RefitAmount
/// spread over the armor and internal structure that need it (not destroyed arms), then the ammunition (only the
/// ammunition with <paramref name="ammoOnly"/>). <paramref name="pointsUsed"/> gets the points spent, rounded to
/// quarters.
/// </summary>
/// <returns>1 when the mover needs nothing more (or there were no points), else 0.</returns>
int32_t DoRefit(MCMover& mover, float refitPoints, float& pointsUsed, int ammoOnly);
