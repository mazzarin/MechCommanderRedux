#pragma once

// Original source: mcx\network\multplyr.cpp: the handlers of the game's messages (handleApp*, handleSys*) and the
// SessionManager callbacks that dispatch to them. Each application handler gets the sender's DPID and the message
// bytes.

#include "object/MCWeaponHitChunk.h"

class MCFidpMessage;
struct DPMSG_ADDPLAYERTOGROUP;
struct DPMSG_CREATEPLAYERORGROUP;

/// <summary>An empty weapon-hit chunk as the original's locals start (hit location -1, the rest 0).</summary>
MCWeaponHitChunk EmptyWeaponHitChunk();

/// <summary>
/// System message: a player was created (the server sends it its setup; port: the host keeps the rules its join card
/// holds, in <paramref name="message"/>'s data).
/// </summary>
void HandleSysCreatePlayer(const DPMSG_CREATEPLAYERORGROUP& msg, std::span<const uint8_t> message);

/// <summary>System message: a player was added to a group (its team is recorded).</summary>
void HandleSysAddPlayerToGroup(const DPMSG_ADDPLAYERTOGROUP& msg);

/// <summary>The default chat handler: prints the line in the ABL debugger.</summary>
void HandleAppChat(MCFidpMessage& msg);

void HandleAppNewServer(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerCheckIn(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerSetup(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerCheckInReceipt(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppStartPlanning(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppReadyForBattle(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppJoinTeam(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppRPUpdate(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppTechbaseChange(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppSwitchScreen(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>
/// Port: the host's match options: kept as the guest's <c>MatchOptions</c>; a weather or time of day this build doesn't
/// know is the mission's own, and the rules it doesn't play by are left out.
/// </summary>
void HandleAppMatchOptions(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>Port: the host's players' rules (MCMPPlayersRulesMessage): a guest's PlayerRules, for its MATCH OPTIONS.</summary>
void HandleAppPlayersRules(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppStartScenario(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppEndScenario(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>
/// Port-only (Redux HD): a player's LIGHTS (MCMPLightsMessage): the server turns the sender's own units' lights and sends
/// it on; a client takes it from the server.
/// </summary>
void HandleAppLights(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>
/// Port-only (Redux HD): the server's handover of a dropped player's units (MCMPHandoverMessage, network/MCMechHandover.h):
/// a client moves them in its rosters, and makes its own those its player was handed.
/// </summary>
void HandleAppHandover(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerOrder(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerMoverGroup(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerArtillery(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>Gives each rostered mover its move and status chunks and order id (drops out-of-order updates).</summary>
void HandleAppMoverUpdate(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>Sets each rostered turret's target from the server's turret update (drops out-of-order updates).</summary>
void HandleAppTurretUpdate(uint32_t fromID, std::span<const uint8_t> msg);
/// <summary>Hands each mover its weapon-fire chunks from the server's update.</summary>
void HandleAppMoverWeaponFireUpdate(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppTurretWeaponFireUpdate(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppMoverCriticalHitUpdate(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppWeaponHitUpdate(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppWorldStateUpdate(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppPlayerUpdate(uint32_t fromID, std::span<const uint8_t> msg);

void HandleAppFileInquiry(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppFileReport(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppLoadMission(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppStart(uint32_t fromID, std::span<const uint8_t> msg);

/// <summary>The lost-connection dialog's exit: drops the multiplayer game (a lobby game ends the program).</summary>
void LostConnectionDialogExit();

/// <summary>This machine's player was removed from the session (or the session was lost).</summary>
void HandleLocalPlayerRemoved();

/// <summary>
/// Port fix: a match's mission that wouldn't load (MCLogistics::LoadMatchMission): the game leaves its room as its EXIT
/// does, saying so (a host's room ends with it, not handed on mid-start); outside the menus' rooms, the original's way
/// (the main menu, saying so; its OK ends a launcher's game).
/// </summary>
void LeaveUnloadedMatch();

/// <summary>
/// Port-only (Redux HD): a session lost while the scenario started up (MCMultiPlayer::SessionLostHeld), taken as
/// HandleLocalPlayerRemoved takes it once the game waits for the host's start or its mission's start-up turns have
/// ended (as the original held it through them; the results wait for them too).
/// </summary>
void TakeHeldSessionLoss();

/// <summary>
/// The SessionManager's system-message handler: player created or destroyed, player added to a group, session lost.
/// </summary>
void MultiPlayerSystemCallback(MCFidpMessage& msg);

/// <summary>The SessionManager's application-message handler: dispatches on the message type.</summary>
void MultiPlayerApplicationCallback(MCFidpMessage& msg);
