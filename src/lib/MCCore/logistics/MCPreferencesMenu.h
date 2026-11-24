#pragma once

// The 1998 preferences screen's callbacks. The settings it edits and their writing to prefs.cfg are main/MCPrefs.h's.
// Original source: logistics\logmain.cpp.

class MCFitIniFile;
class MCGenericScreen;
class MCGuiEvent;
class MCGuiObject;
class MCLogComboBox;

/// <summary>Opens the preferences screen, remembering the settings for <see cref="CancelPrefs"/>.</summary>
void ShowPreferences();

/// <summary>Leaves the preferences, restoring the settings they opened with.</summary>
void CancelPrefs();

/// <summary>Leaves the preferences, writing the settings to prefs.cfg (<see cref="SavePrefs"/>).</summary>
void WritePrefs();

/// <summary>
/// Port-only: lays the preferences screen <paramref name="screen"/> out on a panel over its art, with room for more
/// settings: the brightness slider; the music, radio, sound effects and interface volume sliders; DIFFICULTY as a
/// drop-down (EASY, REGULAR, HARD; <see cref="GameDifficulty"/>) in place of the original's checks; a RENDERER
/// drop-down (VULKAN, SOFTWARE; PREFS "Renderer"; "VULKAN (FAILED)" when this run's GPU couldn't start); a UI RESOLUTION drop-down (NATIVE, 480P .. 1080P; PREFS
/// "UIResolution", <see cref="GUiResolution"/>); a SCROLL SPEED drop-down (AUTO, 1.5X .. 3X; PREFS "ScrollSpeed",
/// <see cref="GScrollSpeed"/>); an HD box whose OPTIONS button shows the HD OPTIONS page (every HD setting:
/// MCHdOptionsPanel.h); CONTROLS at the bottom left (MCControlsPanel.h); and ACCEPT and CANCEL side by side at the
/// bottom right. The renderer takes effect at the next start, which the message dialog says when the drop-down picks
/// another renderer than the running one; the UI resolution at the next scenario. The screen's sliders and buttons are
/// made again from its ini <paramref name="file"/> on the panel's boxes.
/// </summary>
void AddPreferencesPanel(MCGenericScreen& screen, MCFitIniFile& file);

/// <summary>Port-only: the preferences' RENDERER drop-down, or null before the panel is made.</summary>
std::shared_ptr<MCLogComboBox> PrefsRendererDropDown();

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
