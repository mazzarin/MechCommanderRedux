#pragma once

enum class MCRoomRefusal : uint8_t;

// Port-only (the personal build): updates for players a multiplayer lobby starts (SPEC-install-and-updates.md's
// "Players who come from a multiplayer lobby"). Their game exits back to the lobby when they leave the room or the match
// ends, so the session screen offers the update (logistics/MCUpdatePage.h), and RESTART brings the player back to the
// room:
//
// - RESTART is offered only before the match, and only when every other game in the room can rejoin
//   (MCMatchRule::RejoinsHost) and none is away updating; otherwise the update goes in as the game exits after the
//   match.
// - RESTART writes the rejoin note (network/MCRejoinNote.h) and tells the room (MCMPMessageType::Updating), then the
//   game quits and the update goes in; the new version joins or hosts again as the note says. A guest's room keeps its
//   place (no "has left the game", which ends a lobby's game), and its host opens the room to it again until it is
//   back, a minute at most, holding the match's start meanwhile; a match started anyway lets the places go and closes
//   the room. A room keeps a place only for a build that says it rejoins, and once a guest: a guest whose place it has
//   kept (its restart called off, or back from its update) installs its next update after the match. A host's guests
//   say so and join the same address again for up to a minute: a new room there (the host's new version) has kept no
//   one's place; the same room (the host's restart called off) still has.
// - RESTART is off when the update fails after it (or the room's match starts before the update goes in): the note
//   goes, and the room is told (MCMPMessageType::RestartOff), dropping the place it keeps; a host keeps its guests'
//   places, as they left to wait for it.
// - A rejoin that fails (the room gone, full or started) says so, and its OK ends the game.

namespace MCLobbyUpdate
{
    /// <summary>How long a room keeps a guest's place while it updates, and a guest waits for its host.</summary>
    inline constexpr std::chrono::seconds RejoinWait{60};
    /// <summary>
    /// How long RESTART waits before the update installs, so its messages leave this game first (the game keeps
    /// sending and taking messages each frame meanwhile).
    /// </summary>
    inline constexpr std::chrono::milliseconds SendTime{500};
    /// <summary>How often a guest waiting for its host tries to join again.</summary>
    inline constexpr std::chrono::milliseconds TryEvery{500};
    /// <summary>How long the host lets a guest back in settle (numbered by the session) before showing it the room.</summary>
    inline constexpr std::chrono::seconds SettleTime{1};

    /// <summary>A clock the room's waits go by.</summary>
    using MCClock = std::function<std::chrono::steady_clock::time_point()>;

    /// <summary>Whether this game is in a room a lobby started it in (RESTART brings it back).</summary>
    bool InRoom();

    /// <summary>Whether the room's match is under way (from the session screen's start): RESTART waits for its end.</summary>
    bool MatchUnderWay();

    /// <summary>
    /// Whether RESTART brings this game back to its room now: before the match, with every other game there able to
    /// rejoin and none away updating. Otherwise the update installs as the game exits after the match.
    /// </summary>
    bool CanRestart();

    /// <summary>
    /// Whom the room's start waits for: the guests away updating, whose places it keeps ("WAITING FOR ANN TO
    /// UPDATE."); empty when none is. The session screen holds its start meanwhile.
    /// </summary>
    std::string StartWaitsText();

    /// <summary>
    /// The UPDATE page's line for an update ready in a room: what RESTART does there, or that the update installs after
    /// the match (<see cref="CanRestart"/>); empty outside a room.
    /// </summary>
    std::string ReadyText();

    /// <summary>
    /// RESTART with an update ready. In a room that can (<see cref="CanRestart"/>): the note is written, the room told,
    /// and a moment later (the messages out) the update installs, unless the room can't rejoin by then; otherwise, or
    /// with no note written, nothing happens (it installs as the game exits).
    /// </summary>
    /// <returns>Whether it took RESTART; false outside a room (the page answers as usual).</returns>
    bool Restart();

    /// <summary>
    /// Each frame: a restart's moment, and the restart called off when the update no longer installs; the places kept
    /// for updating guests; a guest's wait for its host.
    /// </summary>
    void Frame();

    /// <summary>
    /// Player <paramref name="fromId"/>'s game restarts into an update (its MCMPMessageType::Updating). A guest's is
    /// ignored unless its rules say it rejoins (MCMatchRule::RejoinsHost), and once the room has kept its place.
    /// </summary>
    void Updating(uint32_t fromId);

    /// <summary>
    /// Player <paramref name="fromId"/>'s restart into an update is off (its MCMPMessageType::RestartOff): it stays, and
    /// the room no longer keeps its place (its own alone).
    /// </summary>
    void RestartOff(uint32_t fromId);

    /// <summary>Tests' hook: the room's waits go by <paramref name="clock"/> (none: the steady clock).</summary>
    void SetClock(MCClock clock);

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
    /// The lobby's join (or a host's room) at the game's start failed: a guest's rejoin (from the note) says why, a
    /// host's that it couldn't open the room again, another start the original's message; each OK ends the game.
    /// </summary>
    void JoinFailed();
}
