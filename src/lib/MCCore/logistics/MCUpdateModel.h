#pragma once

// Port-only (the personal build): UPDATE's logic, what the 1998 UPDATE page (logistics/MCUpdatePage.h) and the remade
// UPDATE (logistics/MCUpdateScreen.h) both show of the updater (main/MCUpdater.h): the status line, the line saying an
// update waits, whether RESTART is offered and RESTART itself, and the version line's words. Port-only (SPEC-menus.md's
// stage 9, step 2): pulled out of the 1998 page; what each says and does is unchanged.

/// <summary>
/// What the version line says for the running version as shown, <paramref name="running"/>: "Redux HD 0.6.3" for
/// "0.6.3 (899)"; nothing without one.
/// </summary>
std::string VersionLineText(std::string_view running);

/// <summary>The release a version as shown names, without its build: "0.6.3" for "0.6.3 (899)".</summary>
std::string_view ReleaseOf(std::string_view shown);

/// <summary>
/// What the remade main menu's UPDATE says of the update as shown, <paramref name="update"/>, with
/// <paramref name="running"/> running: "Update to 0.6.4" for "0.6.4 (912)"; the bare "Update" when it names the release
/// running (that release's content alone, for a program that came ahead of it: one forced alone, or installed over an
/// older copy's files), which the version line beside it names already.
/// </summary>
std::string UpdateButtonText(std::string_view running, std::string_view update);

/// <summary>
/// Whether RESTART is offered for the update ready: only where what the game starts outlives it, not for a setup waiting
/// for the player's own start, and in a lobby's room only when every game there can rejoin (the old page's and the remade
/// UPDATE's, logistics/MCUpdateScreen.h).
/// </summary>
bool UpdateRestartOffered();

/// <summary>
/// RESTART: the game quits so the update installs, its next start skipping the intro (the note); in a lobby's room, it
/// comes back to the room.
/// </summary>
void RestartForUpdate();

/// <summary>The page's status line now (what the update is doing).</summary>
std::string UpdatePageStatus();

/// <summary>
/// Port-only (Redux HD): what the line saying an update waits says now (the main menu's under QUIT, and a lobby's
/// session screen's); empty when none waits.
/// </summary>
std::string UpdateLineText();
