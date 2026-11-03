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

/// <summary>System message: a player was created (the server sends it its setup).</summary>
void HandleSysCreatePlayer(const DPMSG_CREATEPLAYERORGROUP& msg);

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
void HandleAppStartScenario(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppEndScenario(uint32_t fromID, std::span<const uint8_t> msg);
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

/// <summary>
/// A "checksum" of file <paramref name="fileName"/> (to check the players have the same mission files): its first
/// four bytes, 0 when it can't be read.
/// </summary>
uint32_t GetCheckSum(std::string_view fileName);

void HandleAppFileInquiry(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppFileReport(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppLoadMission(uint32_t fromID, std::span<const uint8_t> msg);
void HandleAppStart(uint32_t fromID, std::span<const uint8_t> msg);

/// <summary>The lost-connection dialog's exit: drops the multiplayer game (a lobby game ends the program).</summary>
void LostConnectionDialogExit();

/// <summary>This machine's player was removed from the session (or the session was lost).</summary>
void HandleLocalPlayerRemoved();

/// <summary>
/// The SessionManager's system-message handler: player created or destroyed, player added to a group, session lost.
/// </summary>
void MultiPlayerSystemCallback(MCFidpMessage& msg);

/// <summary>The SessionManager's application-message handler: dispatches on the message type.</summary>
void MultiPlayerApplicationCallback(MCFidpMessage& msg);
