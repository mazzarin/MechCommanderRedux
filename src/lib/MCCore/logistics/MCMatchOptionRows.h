#pragma once

#include "logistics/MCOptionRow.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class MCMultiPlayer;

// Port-only: the multiplayer room's MATCH OPTIONS as data (logistics/MCOptionRow.h's rows): the weather and time of
// day every player sees, and the rules the match plays by (MCMatchOptions, MultiPlayer()->Played), each row's value
// read from what the match plays, its change the host's (into its options, and what the match plays of them to the
// guests); an option a player's game doesn't have held, saying whose. The 1998 tab
// (logistics/MCMatchOptionsTab.h), the remade MATCH OPTIONS (logistics/MCMatchOptionsWindow.h) and the room
// (logistics/MCRoomScreen.h) show them. Port-only (SPEC-menus.md's stage 9, step 2): pulled out of the 1998 tab; the
// rows, their words and the messages are unchanged.

/// <summary>
/// Port-only (SPEC-menus.md's stage 4): the MATCH OPTIONS rows, headings and options in the list's order (PLAY, then
/// WEAPONS' families), each option's value the one <see cref="ReadMatchOptions"/> read, its change the host's (sent as
/// the list's is).
/// </summary>
std::span<const MCOptionRow> MatchOptionRows();

/// <summary>The rows' values (<see cref="MatchOptionRows"/>) read from what the match plays now (MultiPlayer()->Played).</summary>
void ReadMatchOptions();

/// <summary>
/// Why the host can't change row <paramref name="row"/> of <see cref="MatchOptionRows"/> ("TELL BOB TO UPGRADE."), a
/// weapon family's with its heading's; empty when it can.
/// </summary>
std::string MatchOptionHeld(int32_t row);

/// <summary>
/// Port-only (SPEC-menus.md's stage 4): the other players whose game holds row <paramref name="row"/> (MatchOptionHeld),
/// their names as typed; none when it isn't held, or out of a session.
/// </summary>
std::vector<std::string> MatchOptionHeldBy(int32_t row);

/// <summary>
/// Why an option is held, for the other players (<paramref name="names"/>, as shown) whose game doesn't have it, every
/// one named: "TELL BOB, ANN AND SALLY TO UPGRADE." (the list wraps it over as many lines as it needs,
/// MCLogOptionList::SetHeld). Empty for no players.
/// </summary>
std::string MatchTellToUpgrade(std::span<const std::string> names);

/// <summary>
/// An option's row of <see cref="MatchOptionRows"/>, and why it's held (<see cref="MatchOptionHeld"/>'s words).
/// </summary>
struct MCMatchOptionHold
{
    int32_t Row = 0;
    /// <summary>Why the host can't change it ("TELL BOB TO UPGRADE."); empty when it can.</summary>
    std::string Why;
};

/// <summary>
/// Each option's row (one per option of the capability map; the WEAPONS heading's for its families), in the rows'
/// order, and why it's held in <paramref name="game"/>'s session now, for the 1998 tab's list
/// (MCLogOptionList::SetHeld).
/// </summary>
std::vector<MCMatchOptionHold> MatchOptionHolds(MCMultiPlayer& game);
