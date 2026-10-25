#pragma once

// The preferences screen's callbacks and the settings it edits (prefs.cfg). Original source: logistics\logmain.cpp.

class MCGenericScreen;
class MCGuiEvent;
class MCGuiObject;

/// <summary>The difficulty (0 easy, 1 regular, 2 hard; starts at 1). An <c>int32_t</c>: the drop-down edits it.</summary>
extern int32_t GameDifficulty;

/// <summary>Draws the 45-pixel sprites only (PREFS "Force45Pixel"; the GUI, camera, interface and terrain map read it).</summary>
extern bool Only45Pixel;

/// <summary>Forces the 32 MB memory settings (PREFS "Force32Mb").</summary>
extern bool Force32MB;

/// <summary>Forces the 16 MB memory settings (PREFS "Force16Mb").</summary>
extern bool Force16MB;

/// <summary>Opens the preferences screen, remembering the settings for <see cref="CancelPrefs"/>.</summary>
void ShowPreferences();

/// <summary>Leaves the preferences, restoring the settings they opened with.</summary>
void CancelPrefs();

/// <summary>Leaves the preferences, writing the settings to prefs.cfg.</summary>
void WritePrefs();

/// <summary>
/// Port-only: lays the preferences screen <paramref name="screen"/> out on a panel over its art, with room for more
/// settings: the brightness slider; the music, radio, sound effects and interface volume sliders; DIFFICULTY as a
/// drop-down (EASY, REGULAR, HARD; <see cref="GameDifficulty"/>) in place of the original's checks; a RENDERER
/// drop-down (VULKAN, SOFTWARE; PREFS "Renderer"); a UI RESOLUTION drop-down (NATIVE, 480P .. 1080P; PREFS
/// "UIResolution", <see cref="GUiResolution"/>); an HD ART drop-down (OFF, ON; PREFS "HDArt", <see cref="GHdArt"/>);
/// and ACCEPT and CANCEL side by side at the bottom right. The renderer takes effect at the next start, which the
/// message dialog says when the drop-down picks another renderer than the running one; the UI resolution and HD art at
/// the next scenario (the message dialog says when HD art is turned on without a pack).
/// </summary>
void AddPreferencesPanel(MCGenericScreen& screen);

/// <summary>The preferences screen's routine (it does nothing).</summary>
void PrefScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The brightness slider.</summary>
void SlideScreenBrightness(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The music volume slider: on release, the new volume and the menu music again.</summary>
void SlideMusicVolume(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The radio volume slider: on release, the new volume and a pilot's line to hear it.</summary>
void SlideRadioVolume(MCGuiObject& object, MCGuiEvent& event);

/// <summary>The sound effects volume slider: on release, the new volume and a sample to hear it.</summary>
void SlideFXVolume(MCGuiObject& object, MCGuiEvent& event);
/// <summary>The interface volume slider (the port's): on release, the new volume and a click to hear it.</summary>
void SlideInterfaceVolume(MCGuiObject& object, MCGuiEvent& event);

/// <summary>Difficulty: easy.</summary>
void EasyToggle();

/// <summary>Difficulty: regular.</summary>
void RegularToggle();

/// <summary>Difficulty: hard.</summary>
void HardToggle();
