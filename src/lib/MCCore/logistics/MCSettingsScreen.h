#pragma once

#include "logistics/MCSettingsModel.h"
#include "ui/MCRmlScreen.h"

/// <summary>
/// Port-only (SPEC-menus.md, "Preferences" and "A pause menu"; stage 6): a screen of settings' rows, the settings
/// model's (logistics/MCSettingsModel): Preferences' sections and the pause's quick settings. Each row is the setting's
/// name, the line under it and its control (the look's switch, stepper, slider or button); a change sets the setting at
/// once, a held row (a setting it needs, a running mission) refuses its control and its line says why.
/// </summary>
class MCSettingsScreen : public MCRmlScreen
{
public:
    /// <summary>Every row's control, line and hold set from the settings as they are now.</summary>
    void Refresh();

protected:
    explicit MCSettingsScreen(bool inMission) : _InMission(inMission) {}

    /// <summary>Whether a running mission's holds apply (the screen is over a mission).</summary>
    bool InMission() const { return _InMission; }

    /// <summary>
    /// <paramref name="setting"/>'s row as the look marks it up (<paramref name="quick"/>: the pause's, without its
    /// line), and listened to once the document has it (<see cref="ListenToRow"/>).
    /// </summary>
    static std::string RowRml(const MCSettingsModel::MCSetting& setting, bool quick);
    /// <summary>
    /// Listens to <paramref name="setting"/>'s row, written into the document: a change or a press sets it (asking first
    /// when the setting asks), a slider's sound plays at its new level; then every row is shown again.
    /// </summary>
    void ListenToRow(const MCSettingsModel::MCSetting& setting);
    /// <summary>The rows that change by themselves (the HD pack's download) shown again.</summary>
    void RefreshLive();

    /// <summary>A setting changed (the pause keeps its changes as it closes).</summary>
    virtual void Changed() {}
    /// <summary>A press is about to restart the game (the HD pack's RESTART): Preferences keeps its changes first.</summary>
    virtual void BeforeRestart() {}

private:
    /// <summary>A change of <paramref name="setting"/> to <paramref name="value"/> from its control.</summary>
    void OnRowChange(const MCSettingsModel::MCSetting& setting, int value);
    /// <summary>A press of <paramref name="setting"/>'s button.</summary>
    void OnRowPress(const MCSettingsModel::MCSetting& setting);
    /// <summary>Asks <paramref name="question"/>, the rows shown again after either answer.</summary>
    void Ask(MCSettingsModel::MCQuestion question);
    /// <summary>Plays <paramref name="setting"/>'s sound at its level (a slider's, once the mouse lets go).</summary>
    void Preview(const MCSettingsModel::MCSetting& setting) const;
    /// <summary><paramref name="setting"/>'s row set from it, in <paramref name="document"/>.</summary>
    void RefreshRow(Rml::ElementDocument& document, const MCSettingsModel::MCSetting& setting) const;

    bool _InMission;
    /// <summary>The settings whose rows the document has, in its order.</summary>
    std::vector<std::reference_wrapper<const MCSettingsModel::MCSetting>> _Rows;
};
