#pragma once

// Port-only (Redux HD): a player whose game drops out of a multiplayer battle leaves no dead weight: the server hands its
// units to a teammate still playing, who commands them as its own. The server picks the teammate as the player's leave
// reaches it, one message (MCMPMessageType::Handover) tells every game, and every game moves the units in its rosters
// (MCMultiPlayer::PlayerMoverRoster: an order names its units by its player's number and their slots there). The
// units keep their commander (CommanderId): their kills, before and after, count for the player who dropped, and
// nothing about how they move or fight changes. Only the server's game and the receiver's need the rule
// (MCMatchRule::MechHandover): every other game shows the units as the server's updates move them.

class MCMover;
class MCMultiPlayer;

namespace MCMechHandover
{
    /// <summary>A unit handed on: its part id, and its new player (number) and slot in that player's roster.</summary>
    struct MCHanded
    {
        int32_t Part = 0;
        int32_t Player = 0;
        int32_t Slot = 0;

        bool operator==(const MCHanded&) const = default;
    };

    /// <summary>
    /// A teammate who may take a dropped player's units: its player number, how many of the units it commands are
    /// still in the fight, and its roster's free slots, first to last.
    /// </summary>
    struct MCTaker
    {
        int32_t Player = 0;
        int32_t Units = 0;
        std::vector<int32_t> FreeSlots;
    };

    /// <summary>
    /// The server's choice: <paramref name="parts"/> (the dropped player's units, in its roster's order) go into the
    /// free slots of <paramref name="takers"/> (the teammates whose games can take them): the one with the fewest
    /// units first, ties to the lowest player number; those that don't fit go to the next by the same rule. Any left
    /// after that stay the dropped player's: they aren't in the plan.
    /// </summary>
    std::vector<MCHanded> Plan(std::span<const int32_t> parts, std::span<const MCTaker> takers);

    /// <summary>
    /// The server: the teammates of player <paramref name="leaver"/> still in the battle (the session's other players
    /// with a number whose commander is on the leaver's commander's team) whose games can take its units: their rules
    /// say MCMatchRule::MechHandover (this game's own always do).
    /// </summary>
    std::vector<MCTaker> Takers(const MCMultiPlayer& game, int32_t leaver);

    /// <summary>
    /// The units in player <paramref name="player"/>'s roster still in the fight (neither disabled nor destroyed), by
    /// part id, in slot order: what the player's leave hands on.
    /// </summary>
    std::vector<int32_t> UnitsToHand(const MCMultiPlayer& game, int32_t player);

    /// <summary>
    /// The server, as player <paramref name="leaver"/>'s leave reaches it in a mission: hands its units on
    /// (<see cref="Plan"/> of <see cref="Takers"/>), tells every game (<see cref="Send"/>) and moves them here
    /// (<see cref="Apply"/>). With no teammate able to take any, nothing is sent: the units stand as they were.
    /// </summary>
    /// <returns>What it handed on.</returns>
    std::vector<MCHanded> HandOn(MCMultiPlayer& game, int32_t leaver);

    /// <summary>Sends every game the handover of player <paramref name="leaver"/>'s <paramref name="units"/>.</summary>
    void Send(MCMultiPlayer& game, int32_t leaver, std::span<const MCHanded> units);

    /// <summary>
    /// Every game: moves each of <paramref name="units"/> from player <paramref name="leaver"/>'s roster into its new
    /// slot. The receiver's game (its check-in id the unit's new player) makes it its own, as the scenario's load makes
    /// a player's own: its local mover at that slot (Mover::NetPlayerId, by which its orders name it) and a button on
    /// the mech bar, after its own. A unit not in the leaver's roster, or a slot already taken, is skipped (logged): the
    /// rosters disagree.
    /// </summary>
    /// <returns>How many were moved.</returns>
    int32_t Apply(MCMultiPlayer& game, int32_t leaver, std::span<const MCHanded> units);

    /// <summary>Whether player <paramref name="player"/>'s roster holds <paramref name="unit"/>: the player commands it.</summary>
    bool Commands(const MCMultiPlayer& game, int32_t player, const MCMover& unit);
}
