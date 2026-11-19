#pragma once

// Port-only (Redux HD): where a game the menus took to its room goes when the room or its match ends
// (SPEC-in-game-lobby.md's "LAN and INTERNET alike"): back to the games screen it came from (the LAN games, or INTERNET
// CONNECTION), never the main menu.
//
// - The room ends under a guest (its host's game gone, whatever the cause, and nobody took over): back to the games,
//   saying why: "BOB CLOSED THE GAME." when the host said so as it left (its EXIT tells the guests whose builds take it,
//   MCMatchRule::Rematch, with MCMPMessageType::RoomEnds, and waits a moment for them to leave first), else "LOST THE
//   CONNECTION TO BOB'S GAME.". A match ended so goes back to the games once its scenario has ended.
// - At the match's results the battle is over: a session lost there leaves the results up, and DONE goes back to the
//   games.
// - REMATCH: the host's results give it beside DONE when a guest still there takes it (its build has
//   MCMatchRule::Rematch). It tells the room (RoomEnds), waits a moment for its guests to leave, and hosts the same room
//   again, a new session as each battle's is (SPEC-in-game-lobby.md's "No link outlives its battle"): its name, size
//   and port, or an internet game's code (its place on the service kept through the leave, MCInternetGame::KeepPlace),
//   and the picks the match started with: the mission, the points, the tech bases, the match options, and each player's
//   team and slot by name, taken again as the player returns. The room keeps its guests' places for a while, as it does
//   for guests away updating (network/MCLobbyUpdate.h). Each guest's results close, and its game joins the room again
//   as a guest whose host restarts into an update does; one that doesn't come back is said over the games.
// - A launcher's game (-host, -join) keeps the original's way: its room is the launcher's (the original's message,
//   whose OK ends the game; the game ends after the results), and it has no REMATCH.

class MCSessionScreen;

namespace MCRoomEnd
{
    /// <summary>The games screen a game's room was reached from.</summary>
    enum class MCGames : uint8_t
    {
        /// <summary>None of the menus' (a launcher's game, a modem or serial game): the original's way.</summary>
        None,
        Lan,
        Internet,
    };

    /// <summary>What a host's MCMPMessageType::RoomEnds says (its value).</summary>
    enum class MCHow : int32_t
    {
        /// <summary>The host closes its room (its EXIT).</summary>
        Closed = 0,
        /// <summary>The host hosts its room again for a rematch (REMATCH at the results): its guests join it again.</summary>
        Rematch = 1,
    };

    /// <summary>
    /// How long a host that told its guests the room ends waits at most for them to leave, so the word reaches them
    /// before its links close (a guest's game busy a moment takes it late).
    /// </summary>
    inline constexpr std::chrono::milliseconds LeaveWait{2000};

    /// <summary>The games screen this game's room was reached from, as the room stands now.</summary>
    MCGames GamesOf();

    /// <summary>
    /// Shows <paramref name="games"/> (the LAN games, or INTERNET CONNECTION), a multiplayer game made for it if there is
    /// none, and says <paramref name="line"/> over it (nothing when empty).
    /// </summary>
    void ShowGames(MCGames games, std::string_view line);

    /// <summary>
    /// The host's EXIT from its room (logistics/MCConnectMenu.h's LeaveRoom): with guests whose builds take its word, it
    /// tells them the room closes and leaves once they have (<see cref="LeaveWait"/> at most).
    /// </summary>
    /// <returns>Whether it took the EXIT (the leave comes later); false: the EXIT goes on now.</returns>
    bool CloseRoom();

    /// <summary>
    /// The host's MCMPMessageType::RoomEnds from player <paramref name="fromId"/>, saying <paramref name="how"/>.
    /// </summary>
    /// <returns>Whether the room ends here now (the host closed it): the game leaves as it does when the session is lost.</returns>
    bool HostSaid(uint32_t fromId, int32_t how);

    /// <summary>
    /// The session is lost under this game (DPSYS_SESSIONLOST, or its host's word): at the results it leaves the session
    /// and the results stay; in a game the menus took to its room, back to its games screen, saying why (after the
    /// scenario's end, in a match).
    /// </summary>
    /// <returns>Whether it took it; false: the original's way (the main menu and its message).</returns>
    bool SessionLost();

    /// <summary>
    /// Leaves the room this game is in as its EXIT does, its screens' own cleared, and shows the games screen it came
    /// from, saying <paramref name="line"/> (a match's mission that wouldn't load: MCMultiPlayerHandlers' LeaveUnloadedMatch).
    /// </summary>
    /// <returns>Whether it left; false: not a room the menus opened, so the original's way is the caller's.</returns>
    bool LeaveRoomSaying(std::string_view line);

    /// <summary>
    /// The match starts (host and guest, as the match's state is published): the room it is played from, and the host's
    /// picks, which a rematch takes again.
    /// </summary>
    void MatchStarts();

    /// <summary>
    /// Whether the host's results offer REMATCH: a game the menus took to its room, the room's host, with a guest still in
    /// the session whose build takes it.
    /// </summary>
    bool OffersRematch();

    /// <summary>
    /// The host's REMATCH: the room is told, and once its guests have left (<see cref="LeaveWait"/> at most) the results
    /// close and the room is hosted again (<see cref="AfterMatch"/>).
    /// </summary>
    void Rematch();

    /// <summary>Whether a rematch is under way from the results (DONE then does nothing).</summary>
    bool Rematching();

    /// <summary>
    /// The host shows its room again (MCSessionScreen::ShowRoomAgain): a rematch's player who returned goes back to its
    /// team and slot.
    /// </summary>
    void PlaceReturning(MCSessionScreen& room);

    /// <summary>
    /// The match's scenario has ended (MCMission::EndScenario, logistics' main menu up): a game the menus took to its
    /// room goes back to its games screen, or its rematch's room (the host hosting it again, a guest joining it).
    /// </summary>
    /// <returns>Whether it took it; false: the original's way (a launcher's game ends).</returns>
    bool AfterMatch();

    /// <summary>Whether <see cref="AfterMatch"/> would take the game on now (doing nothing).</summary>
    bool TakesOnAfterMatch();

    /// <summary>
    /// Each frame, in the menus and at the results: a host's wait for its guests to leave on its word, its internet
    /// game's way back to its place on the service, and a guest's leave on its host's REMATCH.
    /// </summary>
    void Frame();

    /// <summary>Tests' hook: the waits go by <paramref name="clock"/> (none: the steady clock).</summary>
    void SetClock(std::function<std::chrono::steady_clock::time_point()> clock);
}
