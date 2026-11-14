#pragma once

// Port-only: the HD settings PREFS.CFG keeps, from "HDArt" to "MoreUnits", and "Reports" (main/MCTelemetry.h): the
// setting each key holds, its choices, what it reads as when PREFS lacks it, and how it is read and written. The
// preferences' HD OPTIONS page edits them (logistics/MCHdOptionsPanel.h).

#include <functional>

class MCFitIniFile;

/// <summary>
/// Personal build: PREFS.CFG's HD settings read as their personal defaults when it lacks them (on, lit at the mission's
/// time of day: <see cref="MCHdSettings::MCSetting::Personal"/>), instead of the original's game (off). The app calls it
/// before the game starts; the tests don't, so they keep the original's defaults.
/// </summary>
void UseHdByDefault();

/// <summary>
/// Personal build: PREFS.CFG's gameplay settings (<see cref="MCHdSettings::MCSetting::Gameplay"/>: BRAKING, MoreUnits)
/// read as their personal defaults when it lacks them, whether or not the looks do (<see cref="UseHdByDefault"/>, which
/// follows the HD answer): they need nothing of the HD pack. The app calls it before the game starts; the tests don't.
/// </summary>
void UsePersonalGameplay();

namespace MCHdSettings
{
    /// <summary>An HD setting: its PREFS key, the global it sets, its choices and its defaults.</summary>
    struct MCSetting
    {
        /// <summary>The PREFS key.</summary>
        std::string_view Key;
        /// <summary>The setting.</summary>
        std::reference_wrapper<int> Value;
        /// <summary>Its choices, Least to Most (PREFS values outside them are clamped).</summary>
        int32_t Least = 0;
        int32_t Most = 1;
        /// <summary>Written as a boolean (else as a number).</summary>
        bool Boolean = true;
        /// <summary>What it reads as when PREFS lacks it: the original's game, and the personal build.</summary>
        int32_t Original = 0;
        int32_t Personal = 1;
        /// <summary>The player's own answer, not a look (REPORTS): DEFAULTS and ORIGINAL leave it.</summary>
        bool Player = false;
        /// <summary>
        /// Changes play, not the look (BRAKING, MoreUnits): its personal default needs no HD
        /// (<see cref="UsePersonalGameplay"/>).
        /// </summary>
        bool Gameplay = false;
    };

    /// <summary>Every HD setting, in PREFS' order.</summary>
    std::span<const MCSetting> All();

    /// <summary>The setting PREFS keeps as <paramref name="key"/>; an unknown key is fatal.</summary>
    const MCSetting& Find(std::string_view key);

    /// <summary>
    /// What <paramref name="setting"/> reads as when PREFS lacks it (personal after <see cref="UseHdByDefault"/>, and a
    /// gameplay setting's after <see cref="UsePersonalGameplay"/> too).
    /// </summary>
    int32_t Default(const MCSetting& setting);

    /// <summary>Reads every HD setting from <paramref name="file"/>'s current block (a missing key: its default).</summary>
    void Read(MCFitIniFile& file);

    /// <summary>
    /// Writes into <paramref name="file"/>'s current block the HD settings that differ from their defaults (a missing
    /// key follows the build, so a changed default reaches players who never picked otherwise) and the player's own
    /// answers, always.
    /// </summary>
    void Write(MCFitIniFile& file);

    /// <summary>Every HD setting's value now, in <see cref="All"/>'s order (for <see cref="Restore"/>).</summary>
    std::vector<int32_t> Now();

    /// <summary>Puts back the values <see cref="Now"/> gave.</summary>
    void Restore(std::span<const int32_t> values);

    /// <summary>Every HD setting to its default (<see cref="Default"/>), but the player's own answers.</summary>
    void UseDefaults();

    /// <summary>Every HD setting to the original's game: off, but the player's own answers.</summary>
    void UseOriginal();
}
