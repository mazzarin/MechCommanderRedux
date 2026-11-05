#pragma once

class MCGenericScreen;
class MCGuiObject;
class MCLogOptionList;

// Port-only: the preferences screen's HD OPTIONS page, where the player sets every HD setting (main/MCHdSettings.h):
// the packs, the lighting, the effects, the mechs' motion and the reports (main/MCTelemetry.h), a row each under
// headings, in a list that scrolls. In a game that manages its content (main/MCUpdater.h), HD PACK's button gets the HD pack
// or removes it at once, and RESTART puts it in. The OPTIONS button in the preferences panel's HD box shows it in that panel's
// place; DONE goes back. It edits the settings themselves: the preferences' ACCEPT writes them to PREFS, CANCEL puts
// back what they were when the screen opened.

/// <summary>
/// Adds the HD OPTIONS page (hidden) to <paramref name="screen"/>, and the OPTIONS button that shows it to
/// <paramref name="box"/> at (<paramref name="buttonLeft"/>, <paramref name="buttonTop"/>), <paramref name="prefsPanel"/>
/// being the panel it shows in place of.
/// </summary>
void AddHdOptionsPanel(MCGenericScreen& screen, MCGuiObject& prefsPanel, MCGuiObject& box, int32_t buttonLeft,
                       int32_t buttonTop);

/// <summary>
/// The preferences screen opened: the preferences panel shows, the list is back at its top, and HD AUDIO's ORCHESTRAL
/// without the pack's orchestral music is ON.
/// </summary>
void ResetHdOptions();

/// <summary>The OPTIONS button: the HD OPTIONS page shows in the preferences panel's place.</summary>
void ShowHdOptions();

/// <summary>DONE: back to the preferences panel.</summary>
void HideHdOptions();

/// <summary>Whether the HD OPTIONS page is showing.</summary>
bool HdOptionsShown();

/// <summary>The HD OPTIONS page's list, or null before the page is made.</summary>
std::shared_ptr<MCLogOptionList> HdOptionsList();
