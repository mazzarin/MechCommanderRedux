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

    /// <summary>Port-only (stage 9): the campaign screen's pages, shown one at a time.</summary>
    enum class MCCampaignPage
    {
        Briefing,
        Purchasing,
        MechBay
    };

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9, step 2): the flow's top screen, which says which screen shows for the music,
    /// the notices, the idle check, the network's room and the benchmark: Main (the main menu, its submenus, LAN and
    /// INTERNET over it), ReadyRoom, Load (LOAD GAME, SOLO's), MissionPick, Save, Preferences, Room or Campaign (at
    /// <see cref="TopPage"/>); <see cref="MCScreenFlow::NoScreen"/> with none, or no logistics. Until the 1998 screens
    /// go, the one logistics shows sets it (<see cref="NoteTop"/>).
    /// </summary>
    MCScreenFlow::MCScreenId Top();

    /// <summary>The campaign's page shown while <see cref="Top"/> is the campaign.</summary>
    MCCampaignPage TopPage();

    /// <summary>Whether the top is the campaign, on <paramref name="page"/>.</summary>
    bool CampaignOn(MCCampaignPage page);

    /// <summary>The flow's top screen is now <paramref name="id"/> (on <paramref name="page"/>, the campaign's).</summary>
    void NoteTop(MCScreenFlow::MCScreenId id, MCCampaignPage page = MCCampaignPage::Briefing);

    /// <summary>How <see cref="ShowBack"/> shows the screen it goes back to.</summary>
    enum class MCBackWay
    {
        /// <summary>Shown, the screen shown left as it is (the campaign's end).</summary>
        Shown,
        /// <summary>The screen shown hidden first, then it shown (a match's end, a session left).</summary>
        InPlace,
        /// <summary>Made the screen, hidden (a launcher's join failed: its message ends the game).</summary>
        Hidden
    };

    /// <summary>
    /// The way back from a mission or a session onto the flow's screen <paramref name="id"/>, shown as
    /// <paramref name="way"/> says: the main menu (logistics' main screen with the main menu's state, shown with its
    /// palette), SOLO MISSIONS (the main menu where it can't show) or the campaign's briefing. The caller's landing
    /// (MCScreenFlow::Land) and Noted stay its own.
    /// </summary>
    void ShowBack(MCScreenFlow::MCScreenId id, MCBackWay way);

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
