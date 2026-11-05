#pragma once

enum class MCRoomRefusal : uint8_t;

// Port-only (the personal build): updates for players a multiplayer lobby starts (SPEC-install-and-updates.md's
// "Players who come from a multiplayer lobby"). Their game exits back to the lobby when they leave the room or the match
// ends, so the session screen offers the update (logistics/MCUpdatePage.h), and RESTART brings the player back to the
// room:
//
// - Before the match, RESTART writes the rejoin note (network/MCRejoinNote.h) and tells the room
//   (MCMPMessageType::Updating), then the game quits and the update goes in; the new version joins or hosts again as the
//   note says. A guest tells every player, whose games keep its place (no "has left the game", which ends a lobby's
//   game) and whose host opens the room to it again until it is back, a minute at most. A host tells the guests whose
//   builds can wait for it (MCMatchRule::RejoinsHost): their games say so and join the same address again for up to a
//   minute; the others drop as it leaves.
// - Once the match is under way, RESTART waits: the update goes in as the game exits at the match's end.
// - A rejoin that fails (the room gone, full or started) says so, and its OK ends the game.

namespace MCLobbyUpdate
{
    /// <summary>How long a room keeps a guest's place while it updates, and a guest waits for its host.</summary>
    inline constexpr std::chrono::seconds RejoinWait{60};

    /// <summary>Whether this game is in a room a lobby started it in (RESTART brings it back).</summary>
    bool InRoom();

    /// <summary>Whether the room's match is under way (from the session screen's start): RESTART waits for its end.</summary>
    bool MatchUnderWay();

    /// <summary>
    /// The UPDATE page's line for an update ready in a room: what RESTART does there (on the host's, how many of the
    /// other players join again); empty outside a room.
    /// </summary>
    std::string ReadyText();

    /// <summary>
    /// RESTART with an update ready. In a room before its match: the note is written, the room told, and a moment later
    /// (the messages out) the update installs; during the match nothing happens (it installs as the game exits).
    /// </summary>
    /// <returns>Whether it took RESTART; false outside a room (the page answers as usual).</returns>
    bool Restart();

    /// <summary>Each frame: a restart's moment, the places kept for updating guests, a guest's wait for its host.</summary>
    void Frame();

    /// <summary>Player <paramref name="fromId"/>'s game restarts into an update (its MCMPMessageType::Updating).</summary>
    void Updating(uint32_t fromId);

    /// <summary>
    /// Player <paramref name="playerId"/> left the room before the match: whether it left to update (it comes back, so
    /// the game doesn't end), or this game is restarting itself.
    /// </summary>
    bool LeftToUpdate(uint32_t playerId);

    /// <summary>Player <paramref name="playerId"/> joined: a guest back from its update is shown in the room.</summary>
    void PlayerJoined(uint32_t playerId);

    /// <summary>What a rejoin that failed says, for <paramref name="refusal"/>.</summary>
    std::string RejoinFailedText(MCRoomRefusal refusal);

    /// <summary>
    /// The lobby's join at the game's start failed: a rejoin (from the note) says why, another start the original's
    /// message; either OK ends the game.
    /// </summary>
    void JoinFailed();
}
