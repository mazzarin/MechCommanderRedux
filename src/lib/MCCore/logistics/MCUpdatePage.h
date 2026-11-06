#pragma once

#include "gui/MCGuiRef.h"

class MCGenericScreen;
class MCGuiObject;
class MCLogButton;
class MCLogObject;
class MCLogPort;

// Port-only (the personal build): the main menu's UPDATE button, which takes ORIGINAL CINEMA's place in a build that
// updates itself (main/MCUpdater.h), lit while an update waits and grayed otherwise; and the UPDATE page it opens over
// the menu, in the preferences pages' style: the new version and the one running, what it brings, its INSTALL and SKIP;
// then the download's progress, and RESTART once it is in (quitting installs it too). DONE closes the page and leaves
// the update waiting.

/// <summary>The pictures of a main menu button: up (also lit), pressed and grayed.</summary>
struct MCMenuButtonPictures
{
    std::shared_ptr<MCLogPort> Up;
    std::shared_ptr<MCLogPort> Down;
    std::shared_ptr<MCLogPort> Gray;
};

/// <summary>
/// The UPDATE button's pictures: ORIGINAL CINEMA's with its word wiped and UPDATE written in the menu's own lettering,
/// its letters cut from the other buttons' words (in the white font if the art's words can't be read); up and pressed
/// in amber, as they show only while an update waits, and grayed as the menu's.
/// </summary>
/// <param name="palette">The main menu's palette, 6-bit RGB (the main screen's): it tells a letter from its edge.</param>
MCMenuButtonPictures UpdateButtonPictures(std::span<const uint8_t> palette);

/// <summary>
/// Makes <paramref name="mainScreen"/>'s ORIGINAL CINEMA the UPDATE button and adds the UPDATE page (hidden) to the
/// screen; the button follows the updater from then on.
/// </summary>
void AddUpdatePage(MCGenericScreen& mainScreen);

/// <summary>UPDATE: the main menu's page shows (each screen that shows the page has its own, all showing the updater).</summary>
void ShowUpdatePage();

/// <summary>
/// Adds to <paramref name="screen"/> the line saying an update waits, for the session screen of a game a lobby started
/// (which never shows the main menu), at (<paramref name="xPos"/>, <paramref name="yPos"/>), <paramref name="width"/> x
/// <paramref name="height"/>: a field in UPDATE's amber, <paramref name="palette"/> the screen's (6-bit RGB); a click on
/// it opens the page over that screen. Shown while an update waits.
/// </summary>
MCGuiHandle<MCLogObject> AddUpdateLine(MCGuiObject& screen, std::span<const uint8_t> palette, int32_t xPos,
                                       int32_t yPos, int32_t width, int32_t height);

/// <summary>The page shows over <paramref name="screen"/>, in front of what is on it.</summary>
void ShowUpdatePageOver(MCGuiObject& screen);

/// <summary>
/// What a click at (<paramref name="xPos"/>, <paramref name="yPos"/>) finds of the page shown over
/// <paramref name="screen"/> (the page itself between its controls); null when the page isn't over it, or the point is
/// off the page.
/// </summary>
std::shared_ptr<MCGuiObject> UpdatePageFind(const MCGuiObject& screen, int32_t xPos, int32_t yPos);

/// <summary>DONE (or SKIP): the page closes.</summary>
void HideUpdatePage();

/// <summary>Whether the UPDATE page is showing.</summary>
bool UpdatePageShown();

/// <summary>The page's status line now (what the update is doing).</summary>
std::string UpdatePageStatus();

/// <summary>The lines the page shows of what the update brings, as wrapped.</summary>
std::vector<std::string> UpdatePageLines();
