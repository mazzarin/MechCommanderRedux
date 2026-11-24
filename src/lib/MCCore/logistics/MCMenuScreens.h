#pragma once

#include "gui/MCScreenFlow.h"

// Port-only (SPEC-menus.md's stage 2, "The screens' flow"): the old menu screens (MCLogistics') as the flow's kinds
// (gui/MCScreenFlow.h): which is shown, how each shows again on the way back, and what leaving each does (its own way
// back: CANCEL, EXIT, MAIN MENU). Escape on every one goes back one (MCScreenFlow::Back), as its way back does. The
// stack's ends: the main menu asks to quit; a launcher's room asks to leave the game, which ends it.

namespace MCMenuScreens
{
    /// <summary>Registers the old screens' kinds with the flow (again at each menus' start: their ids kept).</summary>
    void Register();

    /// <summary>The old screens' ids in the flow (registered on first use).</summary>
    MCScreenFlow::MCScreenId Main();
    MCScreenFlow::MCScreenId Multiplayer();
    MCScreenFlow::MCScreenId Lan();
    MCScreenFlow::MCScreenId Internet();
    MCScreenFlow::MCScreenId Room();
    MCScreenFlow::MCScreenId MissionPick();
    MCScreenFlow::MCScreenId Load();
    MCScreenFlow::MCScreenId Solo();
    MCScreenFlow::MCScreenId Save();
    MCScreenFlow::MCScreenId Preferences();
    /// <summary>The campaign's screens: the briefing, PURCHASING and the 'MECH BAY (sliding between them, one place).</summary>
    MCScreenFlow::MCScreenId Campaign();
    /// <summary>The original's modem and serial screens (MULTIPLAYER without a network).</summary>
    MCScreenFlow::MCScreenId Connection();
    /// <summary>A launcher's ready room (its first screen while the lobby's join goes on).</summary>
    MCScreenFlow::MCScreenId ReadyRoom();
    /// <summary>UPDATE opened from INTERNET: the main menu's page (until stage 6), going back to INTERNET.</summary>
    MCScreenFlow::MCScreenId Update();

    /// <summary>
    /// The stack under a match's games list (LAN or INTERNET), <paramref name="internet"/> saying which: the main menu,
    /// MULTIPLAYER ACCESS, the games; with <paramref name="room"/>, the room over them (a rematch, a rejoin).
    /// </summary>
    std::vector<MCScreenFlow::MCScreenId> GamesStack(bool internet, bool room);

    /// <summary>
    /// The stack under a match's room as a launcher's game has it: the room alone, at the stack's end (its LEAVE asks
    /// "Leave the game?" and ends it); else <see cref="GamesStack"/>'s.
    /// </summary>
    std::vector<MCScreenFlow::MCScreenId> RoomStack();

    /// <summary>Escape on the main menu (the stack's end): "Quit the game?", whose OK quits.</summary>
    void AskQuit();

    /// <summary>A launcher's room's LEAVE and Escape (the stack's end): "Leave the game?", whose OK ends the game.</summary>
    void AskLeaveGame();

    /// <summary>
    /// The campaign's screens' MAIN MENU (and Escape): in single player the main menu, RETURN TO GAME coming back; in a
    /// match the original's question first (CheckExit; over the remade briefing a remade dialog, its EXIT
    /// <see cref="ExitToGames"/>, a launcher's "Leave the game?" ending the game).
    /// </summary>
    void LeaveCampaign();

    /// <summary>
    /// Port-only (stage 8): a match left from its briefing: the original's leaving (LeaveMatch), landing on the games
    /// list it came from (the LAN games, INTERNET) as a room's end does; 1998 went to the main menu.
    /// </summary>
    void ExitToGames();

    /// <summary>
    /// The room opened at a network's word (a join, the host's word to show it, a lobby's room, a rematch): its set-up at
    /// once, as the original's, so what the host says next lands in it; shown through the flow's change named
    /// <paramref name="name"/> (one for every word before it shows; none when it shows already or the screen is dark).
    /// </summary>
    void OpenRoom(std::string name);

    /// <summary>
    /// The room set up at a network's word (<see cref="OpenRoom"/>) for a change that shows it itself
    /// (<see cref="ShowPreparedRoom"/>), as the INTERNET join's does.
    /// </summary>
    void PrepareRoom();

    /// <summary>The room readied (<see cref="PrepareRoom"/>) shown, in a change's dark work.</summary>
    void ShowPreparedRoom();

    /// <summary>The text of <see cref="AskQuit"/>'s and <see cref="AskLeaveGame"/>'s questions.</summary>
    inline constexpr std::string_view QuitQuestion = "Quit the game?";
    inline constexpr std::string_view LeaveGameQuestion = "Leave the game?";

    /// <summary>
    /// A match's "Exit Multiplayer?" over the remade briefing (<see cref="LeaveCampaign"/>): the original's string, and
    /// the buttons' words.
    /// </summary>
    inline constexpr uint32_t ExitMultiplayerString = 0xaf;
    inline constexpr int32_t GameStringBuffer = 0xfe;
    inline constexpr std::string_view ExitWord = "Exit";
    inline constexpr std::string_view LeaveWord = "Leave";
    inline constexpr std::string_view CancelWord = "Cancel";
}
