#pragma once

// Port (Redux HD): the multiplayer menu's internet screens (SPEC-in-game-lobby.md's "What the player sees"):
// MULTIPLAYER ACCESS, INTERNET (the count online under it) and LAN, and INTERNET CONNECTION, the LAN screen's
// layout over the service's list: its public games (open ones first, then those in battle, greyed), its counts, the
// picked game's players and where it is, a game joined from the list or by its code, a game hosted public or private.
// Joining and hosting move along on the screen's timer, "CONNECTING TO BOB'S GAME..." shown meanwhile with a way out:
// the game's own thread never waits on the network. A build with no internet games (no service set: the tests' games)
// opens the LAN games from MULTIPLAYER as before.

#include "logistics/MCLogScrollTextObject.h"
#include "network/MCInternetLobby.h"

class MCGuiEvent;
class MCGuiObject;
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

/// <summary>INTERNET: the INTERNET CONNECTION screen, the service's list opened.</summary>
void ShowInternetScreen();

/// <summary>
/// Sets up INTERNET CONNECTION (made from the LAN screen's lanscreen.fit): its panel retitled, its list of the service's
/// games, its counts' line, the CODE box, PUBLIC or PRIVATE in the host block, and its buttons' callbacks.
/// </summary>
void SetUpInternetScreen(MCSplashScreen& screen);

/// <summary>
/// The screens going out of sight (MCSplashScreen::ShowGuiWindow): MULTIPLAYER ACCESS's count, or INTERNET CONNECTION's
/// list and timer, stop.
/// </summary>
void AccessScreenHidden();
void InternetScreenHidden();

/// <summary>
/// Port-only: the game quitting, the screens' requests to the service let go (MULTIPLAYER ACCESS's count, INTERNET
/// CONNECTION's list) before the GUI goes, so their threads are kept (lib/MCThreadKeeper.h) before the quit joins them.
/// </summary>
void ReleaseInternetMenus();

/// <summary>
/// For the tests: MULTIPLAYER ACCESS's count as shown (empty while it isn't), INTERNET CONNECTION's counts as shown, and
/// whether a game hosted there is public.
/// </summary>
std::string AccessCountShown();
std::string InternetCountsShown();
bool InternetHostsPublic();

/// <summary>INTERNET CONNECTION's game list: the service's games, one picked.</summary>
class MCInternetGameList : public MCLogScrollTextObject
{
public:
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text) override;

    /// <summary>
    /// Lists <paramref name="listing"/>'s games (none yet: <paramref name="waiting"/>; none open:
    /// <paramref name="empty"/>), keeping the pick on its game and the list where it's scrolled (held to its new
    /// length); the screen hears when the pick goes.
    /// </summary>
    void Show(const std::optional<MCInternetListing>& listing, std::string_view waiting, std::string_view empty);

    /// <summary>A click picks a game, and tells the screen.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The picked game, while it's listed.</summary>
    std::optional<MCInternetListing::Game> Picked() const;

    /// <summary>
    /// Picks the game with <paramref name="code"/> (as a click does), if it's listed: only the rows' highlight and
    /// colours change, the list staying where it's scrolled.
    /// </summary>
    void Pick(std::string_view code);

    void ClearPick();

    /// <summary>
    /// The scroll bar's thumb put where the list stands, if a rebuild came while it was held (a rebuild doesn't move a
    /// held thumb): the screen's timer, each tick.
    /// </summary>
    void SettleScrollTab();

    /// <summary>Whether <paramref name="game"/> can be joined: open, not full, on a version this game plays.</summary>
    static bool Joinable(const MCInternetListing::Game& game);

private:
    /// <summary>A row's height: the list's font's and its gap.</summary>
    int32_t RowHeight() const;
    /// <summary>The lines again from the games or the note, the scroll kept (held to the new length).</summary>
    void Rebuild();
    /// <summary>The games' rows coloured, and the pick highlighted, as the pick stands.</summary>
    void ShowPick();

    std::vector<MCInternetListing::Game> _Games;
    std::optional<std::string> _PickedCode;
    std::vector<std::string> _Lines;
    /// <summary>A rebuild came while the thumb was held: it's put in place once let go.</summary>
    bool _TabStale = false;
};
