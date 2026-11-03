#pragma once

class MCGenericScreen;
class MCLogButton;
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

/// <summary>UPDATE: the page shows over the main menu.</summary>
void ShowUpdatePage();

/// <summary>DONE (or SKIP): the page closes.</summary>
void HideUpdatePage();

/// <summary>Whether the UPDATE page is showing.</summary>
bool UpdatePageShown();

/// <summary>The page's status line now (what the update is doing).</summary>
std::string UpdatePageStatus();

/// <summary>The lines the page shows of what the update brings, as wrapped.</summary>
std::vector<std::string> UpdatePageLines();
