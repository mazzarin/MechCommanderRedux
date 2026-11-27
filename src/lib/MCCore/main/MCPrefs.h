#pragma once

// The player's settings that live nowhere else, and their writing to PREFS.CFG, which MCSystemConfig reads. Port-only
// (SPEC-menus.md's stage 9, step 2): pulled out of the 1998 preferences screen (logistics/MCPreferencesMenu), which
// edited them; what PREFS.CFG holds and how it's written are unchanged. Original source: logistics\logmain.cpp.

/// <summary>The difficulty (0 easy, 1 regular, 2 hard; starts at 1). An <c>int32_t</c>: the settings edit it.</summary>
extern int32_t GameDifficulty;

/// <summary>Draws the 45-pixel sprites only (PREFS "Force45Pixel"; the GUI, camera, interface and terrain map read it).</summary>
extern bool Only45Pixel;

/// <summary>Forces the 32 MB memory settings (PREFS "Force32Mb").</summary>
extern bool Force32MB;

/// <summary>Forces the 16 MB memory settings (PREFS "Force16Mb").</summary>
extern bool Force16MB;

/// <summary>
/// Port-only: DEPLOY's briefing map shown (PREFS "DeployMap"): the player's last choice, kept between launches (the
/// user's, 27 Nov); its M and buttons save it.
/// </summary>
extern bool DeployMapShown;

/// <summary>
/// Port-only: writes the settings as they are now to prefs.cfg (the settings' ACCEPT; the in-mission interface size
/// keys save through it too): its format (<see cref="PrefsVersion"/>), then only the settings that differ from their
/// defaults (<see cref="MCPrefsDefaults"/>, <see cref="MCHdSettings::Default"/>), so a missing key follows the build.
/// </summary>
void SavePrefs();

/// <summary>
/// Port-only: the interface size (<see cref="GInterfaceSize"/>'s percent) <paramref name="steps"/> choices of
/// InterfaceSizes on from <paramref name="size"/>, bigger for positive steps, within the choices; a size not among them
/// counts as the original's.
/// </summary>
int SteppedInterfaceSize(int size, int steps);

/// <summary>
/// Port-only: steps the interface size (<see cref="SteppedInterfaceSize"/>; the in-mission keys) and writes it to
/// prefs.cfg: AUTO steps on from the size it picks for the display (MCGuiSystem::AutoInterfaceSizeNow), and a step back
/// onto that size is AUTO again. The screen takes it at the next frame, as it follows a resized window.
/// </summary>
/// <returns>Whether the size changed (not at the end of the choices).</returns>
bool StepInterfaceSize(int steps);
