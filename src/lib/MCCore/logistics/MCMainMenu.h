#pragma once

#include <string_view>

// The logistics main menu's callbacks: new campaigns, the cinematics, back to the game, the multiplayer menu and
// the way out. The load/save, preferences and multiplayer connection screens have their own files. Original source:
// logistics\logmain.cpp.

/// <summary>Starts a new original campaign.</summary>
void NewCampaign();

/// <summary>Starts a new Mech Commander Gold (MCX) campaign.</summary>
void NewMcxCampaign();

/// <summary>
/// Port-only: the campaign <paramref name="campaign"/> started from its start file <paramref name="startFile"/> on the
/// briefing screen at once, its palette left for the next frame to fade to: what <see cref="NewCampaign"/> and
/// <see cref="NewMcxCampaign"/> do in the fade from their click (the original's, then at the click).
/// <paramref name="firstPlanet"/> starts it on the first planet (the expansion's keeps the planet it had).
/// </summary>
void BeginCampaign(std::string_view campaign, std::string_view startFile, bool firstPlanet);

/// <summary>Opens the multiplayer menu.</summary>
void ShowMultiPlayer();

/// <summary>Replays the campaign's cinematics.</summary>
void ReplayCinema();

/// <summary>Returns from the menu to the logistics screen left (purchase, repair or briefing).</summary>
void ReturnToGame();

/// <summary>Ends the campaign (the player lost): the game quits.</summary>
void GameOverMan();

/// <summary>Back to the main menu (stops a modem or serial wait).</summary>
void Cancel();

/// <summary>Leaves the session (multiplayer) and goes back to the main menu; from a lobby launch, the game quits.</summary>
void DoExit();

/// <summary>The exit button: asks for confirmation, then <see cref="DoExit"/>.</summary>
void CheckExit();
