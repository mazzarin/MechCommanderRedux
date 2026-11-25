#pragma once

#include "logistics/MCPrefsScreen.h"
#include "logistics/MCQuestionWindow.h"
#include "logistics/MCSettingsScreen.h"

/// <summary>
/// Port-only (SPEC-menus.md, "A pause menu"; stage 6): Escape in a mission opens it over the battlefield
/// (<c>ui/pause.rml</c>, the mission's layer, at its interface size): single player PAUSED, the mission paused behind
/// it, ABORT MISSION; multiplayer MENU, the battle playing on, LEAVE MATCH (asking "Exit multiplayer?" first). RESUME
/// and Escape go back to the battle; ALL PREFERENCES opens Preferences over the battlefield, which comes back to it.
/// Beside them the settings players change mid-battle (the settings model's quick rows), each taking effect as it
/// moves and kept (PREFS.CFG) as the pause closes.
/// </summary>
class MCPauseScreen final : public MCSettingsScreen
{
public:
    explicit MCPauseScreen(bool multiplayer);

    /// <summary>
    /// Escape in a mission: the pause opened, the keys held down let go (their release would never reach the battle).
    /// Port-only (stage 9): where it can't be read, the game stops (MCRmlFlow::Broken).
    /// </summary>
    static void OpenOverMission();
    /// <summary>Whether the pause (or Preferences from it) shows: the battle's edges don't scroll under it.</summary>
    static bool Shows();
    /// <summary>The mission ends (its results come, a match's other players won): the pause goes, its changes kept.</summary>
    static void MissionEnding();

    /// <summary>RESUME, and Escape: back to the battle, the changes kept.</summary>
    void Resume();

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnDestroy() override;
    void Changed() override { _Changed = true; }

private:
    /// <summary>ALL PREFERENCES: Preferences over the battlefield, the pause hidden until it closes.</summary>
    void AllPreferences();
    /// <summary>Preferences from the pause closed (ACCEPT or CANCEL): the pause again, its rows as the settings are.</summary>
    void PreferencesClosed();
    /// <summary>ABORT MISSION (1998's Enter while paused), or LEAVE MATCH once asked.</summary>
    void LeaveMission();
    /// <summary>The changes kept, once.</summary>
    void Keep();
    /// <summary>
    /// What is open over the pause closed: Preferences as CANCEL leaves it (what it opened with put back, so nothing of
    /// it is kept), and LEAVE MATCH's question (it lives on the layer, and would hold the keys over the results).
    /// </summary>
    void CloseOver();
    /// <summary>The mission let go, if this pause is what paused it.</summary>
    void Unpause();

    bool _Multiplayer;
    bool _Changed = false;
    /// <summary>
    /// Whether this pause paused the mission (single player, once it opened): one that never opened leaves the mission's
    /// pause as it was, for 1998's (the Pause key's) to take.
    /// </summary>
    bool _Paused = false;
    MCGuiHandle<MCPrefsScreen> _Prefs;
    MCGuiHandle<MCQuestionWindow> _Question;
};
