#pragma once

// The multiplayer connection screens' callbacks: the connection menu (modem, serial, LAN, internet), the LAN session
// list with hosting and joining, the modem and serial screens, and the ready room (port-only, the personal build: a
// modem or serial game's alone; a LAN or lobby game goes straight to the session screen). Original source:
// logistics\logmain.cpp.

class MCGuiEvent;
class MCGuiObject;

/// <summary>
/// The ready room's player list refreshes: counted up by its timer, cleared when a session is joined or created
/// (<c>logistics.cpp</c> clears it too).
/// </summary>
extern int32_t ReadyRoomTicks;

/// <summary>Set to stop a modem or serial wait: the screens' one-second retry timers then do nothing.</summary>
extern bool WhackTimer;

/// <summary>
/// MULTIPLAYER: makes the multiplayer object and opens the LAN games (the personal build), or, when this machine can't
/// use the network, the original's connection screen, each of its buttons enabled when the machine has it.
/// </summary>
void ConnectScreen();

/// <summary>
/// Leaves the session: from the LAN games back to the main menu, from a LAN game's room (the session screen, or the
/// ready room) back to the games.
/// </summary>
void CancelToConnect();

/// <summary>Leaves the game for the multiplayer lobby that launched it.</summary>
void CancelToMPlayer();

/// <summary>
/// Port-only (the personal build): the session screen's EXIT outside a lobby's game: a LAN game's goes back to the
/// games (<see cref="CancelToConnect"/>), a modem or serial game's to the main menu, as the original's.
/// </summary>
void LeaveRoom();

/// <summary>Back to the LAN (session list) screen.</summary>
void CancelToLan();

/// <summary>Back to the session screen from its load dialog.</summary>
void CancelToSession();

/// <summary>Opens the modem screen with the modems found.</summary>
void ShowModemScreen();

/// <summary>Opens the serial screen.</summary>
void ShowSerialScreen();

/// <summary>Connects over IPX and shows the session list.</summary>
void DoTheIpxThang();

/// <summary>Connects over TCP/IP and shows the session list.</summary>
void DoTheTcpThang();

/// <summary>The TCP/IP-or-IPX question's answer (1 IPX, 2 TCP/IP).</summary>
void TcpipxDialogCallback(int32_t result);

/// <summary>Shows the LAN session list over TCP/IP, the player's name and game's name filled in.</summary>
void ShowLanScreen();

/// <summary>Leaves the game for the Zone's internet lobby.</summary>
void DoExitToZone1();

/// <summary>Asks whether to leave for the Zone's internet lobby.</summary>
void DoExitToZone();

/// <summary>Starts the Mplayer lobby and quits, or says it failed.</summary>
void DoExitToMplayer();

/// <summary>Asks whether to leave for the internet lobby (<see cref="DoExitToMplayer"/>).</summary>
void ShowInternet();

/// <summary>Hosting: remembers the player name and opens the session name and size entries.</summary>
void HostGame();

/// <summary>Clears the ready room's and the session list's player lists.</summary>
void ResetReadyRoom();

/// <summary>Joins the session chosen in the list and opens its room (the session screen), or says it can't be joined.</summary>
void JoinGame();

/// <summary>Creates the hosted session (2 to 6 players) and opens its room (the session screen), the host alone in it.</summary>
void CreateSession();

/// <summary>Hosts a serial game on the COM port typed.</summary>
void CreateSerialSession();

/// <summary>The serial screen's join button: connects the COM port typed and joins.</summary>
void SerialJoinButtonPressed();

/// <summary>Joins the serial game, or tries again in a second.</summary>
void JoinSerialSession();

/// <summary>Joins the modem game, or tries again in a second.</summary>
void JoinModemSession();

/// <summary>Dials the number typed.</summary>
/// <returns>0 connected, 2 still dialling, 1 failed, -1 no multiplayer.</returns>
int32_t DialModemSession();

/// <summary>Everyone left: says so.</summary>
void AllGoneCallback(int32_t result);

/// <summary>Moves a connected player on from the ready room to the session screen and locks the session.</summary>
void GOCallback();

/// <summary>The ready room's go button.</summary>
void Go();

/// <summary>Does nothing (a button's callback).</summary>
void Leave();

/// <summary>Waits for an incoming modem call (hosting on the modem chosen).</summary>
void WaitForCall();

/// <summary>Opens the phone number entry.</summary>
void GetNumber();

/// <summary>Stops dialling.</summary>
void CancelDial();

/// <summary>Dials the number typed on the modem chosen.</summary>
void Dial();

/// <summary>The modem list: a click picks the modem on the line under the mouse.</summary>
void ModemListHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The session list's player list: a game picked lists its players; none clears it.</summary>
void PlayerListHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The ready room's player list: each tick lists the session's players with their pings.</summary>
void ReadyRoomPlayerListHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The LAN screen: timer ticks refresh its lists; a game picked can be joined unless full.</summary>
void LanScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The COM port entry: a port outside 1..4 is backspaced out; the screen hears whether the port is good.</summary>
void ComPortTextHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The serial screen: its retry timer, and the host and join buttons enabled by a good port.</summary>
void SerialScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The modem screen: its dial and join retry timers.</summary>
void ModemScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event);
