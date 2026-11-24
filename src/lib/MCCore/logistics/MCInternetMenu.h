#pragma once

// Port (Redux HD): the multiplayer menu's MULTIPLAYER ACCESS (SPEC-in-game-lobby.md's "What the player sees"): INTERNET
// (the count online under it) and LAN. The INTERNET and LAN screens themselves are remade (SPEC-menus.md's stage 3,
// logistics/MCGamesScreen) on the games list (network/MCGamesList). A build with no internet games (no service set: the
// tests' games) opens the LAN games from MULTIPLAYER as before.

#include "logistics/MCGamesScreen.h"

class MCSplashScreen;

/// <summary>Whether MULTIPLAYER shows the access screen: this game has internet games (MCInternetGame::Available).</summary>
bool HasInternetGames();

/// <summary>MULTIPLAYER ACCESS: INTERNET and LAN, the count online asked as it opens.</summary>
void ShowAccessScreen();

/// <summary>Sets up MULTIPLAYER ACCESS (made from mpscreen.fit): its count's line and its timer's events.</summary>
void SetUpAccessScreen(MCSplashScreen& screen);

/// <summary>
/// Puts MULTIPLAYER ACCESS's buttons as the original had them (MODEM, SERIAL, NETWORK, INTERNET), for a machine without
/// a network, which shows the original's connection screen.
/// </summary>
void ShowOriginalAccessButtons();

/// <summary>
/// INTERNET shown at once (a change's dark work: back from a room, a match or an update), the screen shown hidden
/// (logistics/MCGamesScreen's ShowGamesNow).
/// </summary>
void ShowInternetScreen();

/// <summary>MULTIPLAYER ACCESS going out of sight (MCSplashScreen::ShowGuiWindow): its count stops.</summary>
void AccessScreenHidden();

/// <summary>
/// Port-only: the game quitting, the screens' requests to the service let go (MULTIPLAYER ACCESS's count, INTERNET's
/// list) before the GUI goes, so their threads are kept (lib/MCThreadKeeper.h) before the quit joins them.
/// </summary>
void ReleaseInternetMenus();

/// <summary>For the tests: MULTIPLAYER ACCESS's count as shown (empty while it isn't).</summary>
std::string AccessCountShown();
