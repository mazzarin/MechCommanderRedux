#pragma once

// Port (Redux HD): what the multiplayer menus share of the internet games (SPEC-in-game-lobby.md's "What the player
// sees"). MULTIPLAYER (INTERNET with the count online under it, and LAN) is remade in the main menu's panel
// (SPEC-menus.md's stage 7, logistics/MCMainMenuScreen.h); the INTERNET and LAN screens themselves are remade
// (stage 3, logistics/MCGamesScreen) on the games list (network/MCGamesList). A build with no internet games (no service
// set: the tests' games) lists LAN alone.

#include "logistics/MCGamesScreen.h"

/// <summary>Whether MULTIPLAYER lists INTERNET: this game has internet games (MCInternetGame::Available).</summary>
bool HasInternetGames();

/// <summary>
/// INTERNET shown at once (a change's dark work: back from a room, a match or an update), the screen shown hidden
/// (logistics/MCGamesScreen's ShowGamesNow).
/// </summary>
void ShowInternetScreen();

/// <summary>
/// Port-only: the game quitting, the screens' requests to the service let go (MULTIPLAYER's count, INTERNET's list)
/// before the GUI goes, so their threads are kept (lib/MCThreadKeeper.h) before the quit joins them.
/// </summary>
void ReleaseInternetMenus();
