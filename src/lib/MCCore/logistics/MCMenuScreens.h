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
    /// <summary>Port-only (stage 7): the main menu's submenus (logistics/MCMainMenuScreen.h).</summary>
    MCScreenFlow::MCScreenId Campaigns();
    MCScreenFlow::MCScreenId Multiplayer();
    MCScreenFlow::MCScreenId Lan();
    MCScreenFlow::MCScreenId Internet();
    MCScreenFlow::MCScreenId Room();
    MCScreenFlow::MCScreenId MissionPick();
    MCScreenFlow::MCScreenId Load();
    MCScreenFlow::MCScreenId Solo();
    MCScreenFlow::MCScreenId Save();
    MCScreenFlow::MCScreenId Preferences();
    /// <summary>Port-only (stage 6): UPDATE remade, from the main menu, INTERNET and LAN.</summary>
    MCScreenFlow::MCScreenId Update();
    /// <summary>The campaign's screens: the briefing, PURCHASING and the 'MECH BAY (sliding between them, one place).</summary>
    MCScreenFlow::MCScreenId Campaign();
    /// <summary>A launcher's ready room (its first screen while the lobby's join goes on).</summary>
    MCScreenFlow::MCScreenId ReadyRoom();

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

    /// <summary>
    /// Escape and QUIT on the main menu (the stack's end): "Quit the game?", whose answer quits; over the remade main
    /// menu (logistics/MCMainMenuScreen.h), else the 1998 dialog.
    /// </summary>
    void AskQuit();

    /// <summary>
    /// A launcher's room's LEAVE and Escape (the stack's end): "Leave the game?", whose OK ends the game (a host's
    /// goodbye to its guests first: network/MCRoomEnd.h's LeaveLauncherGame).
    /// </summary>
    void AskLeaveGame();

    /// <summary>
    /// The campaign's screens' MAIN MENU (and Escape): in single player the main menu, RETURN TO GAME coming back; in a
    /// match the original's question first (CheckExit).
    /// </summary>
    void LeaveCampaign();

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
}
