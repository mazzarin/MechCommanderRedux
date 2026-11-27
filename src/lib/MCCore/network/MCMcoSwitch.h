#pragma once

// Port-only (Redux HD; SPEC-mco-hosting.md's "The switch"): MechCommander Online in INTERNET, as the games service's
// list last said it (MCInternetList::McoSwitch). Its hosts part is MCMcoSeats' (off until the list has said, so hosting
// from INTERNET waits for our service anyway); its rooms part is kept in the user folder's mco.cfg, on until the list has
// ever said otherwise, so MCO's rooms can show after INTERNET's wait for our list while a switch turned off stays off.

#include "network/MCInternetLobby.h"

namespace MCMcoSwitch
{
    /// <summary>
    /// The list said <paramref name="said"/>: its hosts part applied (a game already hosting keeps its MCO room), its
    /// rooms part kept for INTERNET (<see cref="RoomsOn"/>).
    /// </summary>
    void Heard(const MCMcoSwitchSaid& said);

    /// <summary>Whether INTERNET shows MCO's rooms: the rooms part as last heard, kept; on if never heard.</summary>
    bool RoomsOn();

    /// <summary>The tests': the kept rooms part read again from the user folder, as at the game's next start.</summary>
    void ReadAgainForTests();
}
