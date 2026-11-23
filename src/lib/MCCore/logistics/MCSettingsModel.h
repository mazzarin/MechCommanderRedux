#pragma once

#include "lib/MCOptionalRef.h"

// Port-only (SPEC-menus.md's stage 6, "Preferences"): the settings model. Every setting a player changes, from the
// main menu's Preferences and the pause, as one table: where it shows (its section and group), its name and control,
// how it reads and sets the game's own setting (the globals and PREFS keys today's code keeps), the line under its name,
// what holds it (a setting it needs, a running mission). The screens (logistics/MCPrefsScreen, logistics/MCPauseScreen)
// are views on it: they copy its values into their controls and set it as the player moves them. The settings
// themselves stay where the game keeps them; PREFS.CFG is written as today (SavePrefs).

namespace MCSettingsModel
{
    /// <summary>Preferences' sections, its tabs down the left, in order.</summary>
    enum class MCSection : uint8_t
    {
        Game,
        Display,
        Hd,
        Sound,
        Controls,
        Reports,
    };

    /// <summary>The sections, in order, and each one's tab.</summary>
    inline constexpr std::array Sections = {MCSection::Game,  MCSection::Display,  MCSection::Hd,
                                            MCSection::Sound, MCSection::Controls, MCSection::Reports};
    std::string_view SectionName(MCSection section);

    /// <summary>A setting's control: the look's switch (two to four choices), stepper (more), slider or a button.</summary>
    enum class MCControl : uint8_t
    {
        Switch,
        Stepper,
        Slider,
        Button,
    };

    /// <summary>What a running mission holds of a setting (its line then says why).</summary>
    enum class MCInMission : uint8_t
    {
        /// <summary>Changed in a mission as anywhere.</summary>
        Free,
        /// <summary>RENDERER, the HD pack and its art: "Not in a mission."</summary>
        NotInMission,
        /// <summary>The difficulty and every setting that changes play: "Between missions."</summary>
        BetweenMissions,
    };

    /// <summary>The pause's groups of settings changed mid-battle.</summary>
    enum class MCQuick : uint8_t
    {
        None,
        View,
        Sound,
    };

    /// <summary>The HD pack's button word that restarts the game (a screen keeps its changes first).</summary>
    inline constexpr std::string_view RestartWord = "Restart";

    /// <summary>What a change asks before it's made (REMOVE: "Remove the HD pack?"): the question, its act's word, the act.</summary>
    struct MCQuestion
    {
        std::string Heading;
        std::string Act;
        std::function<void()> Yes;
    };

    /// <summary>A setting: a row of Preferences (and of the pause, when it's <see cref="Quick"/>).</summary>
    struct MCSetting
    {
        /// <summary>Its control's element id in the documents (lower case, words by '-').</summary>
        std::string_view Id;
        MCSection Section = MCSection::Game;
        /// <summary>The group's name over its rows (cyan); a new group starts where it changes.</summary>
        std::string_view Group;
        std::string_view Name;
        MCControl Control = MCControl::Switch;
        /// <summary>The choices' words, a switch's or a stepper's, by index (read as each row is shown).</summary>
        std::function<std::vector<std::string>()> Choices;
        /// <summary>The choice's index, or the slider's level (0 to <see cref="Most"/>), now.</summary>
        std::function<int()> Get;
        /// <summary>Sets it as the player moved it, at once: <see cref="Get"/>'s kind of value.</summary>
        std::function<void(int)> Set;
        /// <summary>A slider's sound for its new level (a volume's sample), once the player lets go of it.</summary>
        std::function<void(bool inMission)> Preview;
        /// <summary>A button's press (the HD pack's GET, REMOVE, STOP or RESTART).</summary>
        std::function<void()> Press;
        /// <summary>
        /// The question a press or a choice (its index; a button's -1) asks first, made by its Yes; none: it's made at once.
        /// </summary>
        std::function<std::optional<MCQuestion>(int value)> AskFirst;
        /// <summary>A button's word now; empty: no button now.</summary>
        std::function<std::string()> Word;
        /// <summary>
        /// The line under the name, for what the name can't say (a key, a restart, the pack's size), or empty.
        /// </summary>
        std::function<std::string()> Line;
        /// <summary>Why it's held now (FLECKS: "Needs particles."), greyed until the other is on; empty: not held.</summary>
        std::function<std::string()> Held;
        /// <summary>DEFAULTS: this build's default (none: DEFAULTS leaves it, as REPORTS, the player's own answer).</summary>
        std::function<void()> UseDefault;
        /// <summary>ORIGINAL: as 1998 draws it (only the HD settings have one).</summary>
        std::function<void()> UseOriginal;
        MCInMission InMission = MCInMission::Free;
        /// <summary>Marked CHANGES PLAY beside its name.</summary>
        bool ChangesPlay = false;
        /// <summary>In the pause (its group there), without its line.</summary>
        MCQuick Quick = MCQuick::None;
        /// <summary>A slider's last level (its first is 0), and its step on Left and Right.</summary>
        int Most = 100;
        int Step = 5;
        /// <summary>Changes by itself (the HD pack's download): the screens show it again each frame.</summary>
        bool Live = false;
    };

    /// <summary>Every setting, in each section's order (a game that doesn't manage its content has no HD pack rows).</summary>
    std::span<const MCSetting> All();

    /// <summary>The setting whose control is <paramref name="id"/>, if any.</summary>
    MCOptionalRef<const MCSetting> Find(std::string_view id);

    /// <summary>
    /// Why <paramref name="setting"/> is held now, the line its row shows in place of its own: a running mission's
    /// (when <paramref name="inMission"/>), else its own <see cref="MCSetting::Held"/>; empty when it isn't.
    /// </summary>
    std::string HeldNow(const MCSetting& setting, bool inMission);

    /// <summary>A slider's level as its row says it, a percent of its range.</summary>
    int LevelPercent(const MCSetting& setting, int level);

    /// <summary>The settings as a screen opened with them (CANCEL puts them back).</summary>
    struct MCSaved
    {
        int32_t PaletteCycle = 0;
        bool Only45Pixel = false;
        int32_t Brightness = 0;
        int32_t Music = 0;
        int32_t Radio = 0;
        int32_t Effects = 0;
        int32_t Interface = 0;
        int32_t Difficulty = 0;
        int32_t Renderer = 0;
        int32_t UiResolution = 0;
        int32_t InterfaceSize = 0;
        int32_t ScrollSpeed = 0;
        std::vector<int32_t> Hd;
    };

    /// <summary>
    /// A screen opens on the settings (Preferences, the pause): in the menus a debug console's art mode gives way to
    /// the player's own (MCArtModes.h).
    /// </summary>
    void Opened(bool inMission);
    /// <summary>The settings now, for <see cref="Restore"/>.</summary>
    MCSaved Save();
    /// <summary>CANCEL: the settings put back as <paramref name="saved"/> has them.</summary>
    void Restore(const MCSaved& saved);
    /// <summary>
    /// ACCEPT, and the pause closing: the settings written to PREFS.CFG (SavePrefs), and REPORTS, if it changed, given
    /// as the player's answer (MCTelemetry::Chosen).
    /// </summary>
    void Keep();
    /// <summary>
    /// DEFAULTS: <paramref name="section"/>'s settings put back to this build's defaults, but those a running mission
    /// holds (when <paramref name="inMission"/>). CONTROLS' keys are the screen's.
    /// </summary>
    void UseDefaults(MCSection section, bool inMission);
    /// <summary>ORIGINAL (HD): every HD look off, as 1998 draws (HD audio too), but those a running mission holds.</summary>
    void UseOriginal(bool inMission);
}
