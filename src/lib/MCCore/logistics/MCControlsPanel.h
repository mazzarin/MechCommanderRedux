#pragma once

#include "iface/MCKeyBindings.h"

class MCGenericScreen;
class MCGuiObject;
class MCLogKeyList;

// Port-only: the preferences screen's CONTROLS panel, where the player changes the in-mission keys. The CONTROLS button
// on the preferences panel shows it in that panel's place, over the screen; DONE goes back. It edits a copy of the
// player's bindings: the preferences' ACCEPT keeps the copy (and writes it to PREFS), CANCEL drops it.

/// <summary>
/// Adds the CONTROLS panel (hidden) to <paramref name="screen"/>, and the CONTROLS button that shows it to
/// <paramref name="prefsPanel"/> at (<paramref name="buttonLeft"/>, <paramref name="buttonTop"/>).
/// </summary>
void AddControlsPanel(MCGenericScreen& screen, MCGuiObject& prefsPanel, int32_t buttonLeft, int32_t buttonTop);

/// <summary>The preferences screen opened: the preferences panel shows, and the CONTROLS panel edits a fresh copy.</summary>
void ResetControls();

/// <summary>The CONTROLS button: the CONTROLS panel shows in the preferences panel's place.</summary>
void ShowControls();

/// <summary>DONE: back to the preferences panel.</summary>
void HideControls();

/// <summary>The preferences' ACCEPT: the copy edited becomes the player's bindings.</summary>
void AcceptControls();

/// <summary>Whether the CONTROLS panel is showing.</summary>
bool ControlsShown();

/// <summary>The bindings the CONTROLS panel edits.</summary>
MCKeyBindings::MCKeyTable& EditedKeys();

/// <summary>The CONTROLS panel's list, or null before the panel is made.</summary>
std::shared_ptr<MCLogKeyList> ControlsList();
