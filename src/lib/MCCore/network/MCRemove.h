#pragma once

// Port-only (Redux HD; SPEC-mco-hosting.md's "REMOVE"): the host takes another player out of its room, from whichever
// side they came, and keeps them out for KeptOutFor:
// - an internet game's guest: our service frees its place and keeps its copy out (MCInternetGame::Remove);
// - MCO's players, waiting or let in, and a guest's seat in the game's MCO room: MCO's kick_peer, again whenever they
//   come back (MCMcoSeats::KeepOut);
// - a LAN guest: turned away at the host's door by its computer's address (AtDoor), for as long as the host's game runs.
// The removed player's game is told (MCRoomEnd's word, MCHow::Removed) and leaves for its games screen, saying who
// removed it ("Ann removed you from the game."); the host drops its machine a moment later should it still be there
// (MCDirectPlay::RemovePlayer). A join while kept out says "You were removed from Steel Rain. Try again in 8 minutes."

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace MCRemove
{
    /// <summary>How long a removed player is kept out, on every side (the games service's REMOVED_MS too).</summary>
    inline constexpr std::chrono::minutes KeptOutFor{10};

    /// <summary>
    /// The host removes the session's player <paramref name="playerId"/>, never its own: its game told, its machine
    /// dropped a moment later, the player kept out on the side it came from. Only in the room, never in a battle.
    /// </summary>
    /// <returns>Whether it did.</returns>
    bool Player(uint32_t playerId);

    /// <summary>The host removes MCO's waiting player <paramref name="name"/>: out of MCO's room, and kept out.</summary>
    /// <returns>Whether MCO's room had them.</returns>
    bool Waiting(std::string_view name);

    /// <summary>
    /// The host's door: the refusal for a player joining from <paramref name="ip"/> (host order) while it's kept out,
    /// none otherwise (MCMultiPlayer's admission).
    /// </summary>
    std::optional<std::string> AtDoor(uint32_t ip);

    /// <summary>What a removed player's game says of its host <paramref name="host"/>: "Ann removed you from the game."</summary>
    std::string RemovedLine(std::string_view host);
    /// <summary>
    /// What a join kept out of <paramref name="game"/> says, <paramref name="left"/> still to go: "You were removed from
    /// Steel Rain. Try again in 8 minutes." (whole minutes, rounded up).
    /// </summary>
    std::string KeptOutLine(std::string_view game, std::chrono::seconds left);

    // The parts the tests check.

    /// <summary>The door's refusal with <paramref name="left"/> to go, as the joiner's game reads it (DoorLeft).</summary>
    std::string DoorReason(std::chrono::seconds left);
    /// <summary>The time a door's refusal <paramref name="reason"/> says is left; none when it isn't the door's.</summary>
    std::optional<std::chrono::seconds> DoorLeft(std::string_view reason);
    /// <summary>The tests' clock for the door (none: the steady clock), and its list emptied.</summary>
    void SetClockForTests(std::function<std::chrono::steady_clock::time_point()> clock);
}
