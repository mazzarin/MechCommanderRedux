#pragma once

#include "logistics/MCLogScrollList.h"

/// <summary>
/// Port-only: the HD OPTIONS page's list of settings (<see cref="MCHdSettings"/>). A row per option under headings:
/// its name, a line under it saying what it does, and its setting in a box at the right; a status line under the rows
/// says what a button did. The list edits the settings themselves and draws every frame from them, in the
/// CONTROLS list's style. The session screen's MATCH OPTIONS is one too (logistics/MCMatchOptionsTab.h), its rows
/// editing the screen's own values rather than PREFS keys, in a narrow panel.
/// </summary>
/// <remarks>
/// A click on an option's row moves its setting to the next of its choices (after the last, the first); a right click
/// to the one before. An option that needs another setting on (HD ART, HD LIGHTING, PARTICLES) shows dim while that one
/// is off, its line saying so; it can still be set. A held option (<see cref="SetHeld"/>) shows dim too, its line
/// saying why, and can't be set. With the SOFTWARE renderer chosen the status line says it draws none of them. The
/// wheel, the up and down keys and the bar at the right scroll the rows.
/// </remarks>
class MCLogOptionList : public MCLogScrollList
{
public:
    /// <summary>A choice of an option: its name and the setting's value.</summary>
    struct Choice
    {
        std::string_view Name;
        int32_t Value = 0;
        /// <summary>Whether it is offered now (none: always).</summary>
        bool (*Offered)() = nullptr;
    };

    /// <summary>A row: an option, or a heading (no key).</summary>
    struct Row
    {
        /// <summary>The option's name, or the heading.</summary>
        std::string_view Label;
        /// <summary>What the option does.</summary>
        std::string_view Description;
        /// <summary>The PREFS key of the setting it edits (<see cref="MCHdSettings::Find"/>).</summary>
        std::string_view Key;
        /// <summary>Port: the value it edits instead, with no PREFS key (none: the key's).</summary>
        int* Value = nullptr;
        /// <summary>Its choices, in the order a click steps through them.</summary>
        std::span<const Choice> Choices;
        /// <summary>The setting that must be on for it to show (its key), and that setting's name.</summary>
        std::string_view Needs;
        std::string_view NeedsLabel;
        /// <summary>Called with the new value when a click changes it.</summary>
        void (*Changed)(int32_t value) = nullptr;

        bool IsHeading() const { return Key.empty() && Value == nullptr; }
    };

    /// <summary>The height of a row: a line for the name and one for what it does.</summary>
    static constexpr int32_t RowHeight = 22;

    MCLogOptionList();

    /// <summary>
    /// Places the list at (<paramref name="xPos"/>, <paramref name="yPos"/>), <paramref name="width"/> x
    /// <paramref name="height"/>, showing <paramref name="rows"/>.
    /// </summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::span<const Row> rows);

    /// <summary>Draws the titles, the rows, the scroll bar and the status line from the settings.</summary>
    void Draw() override;

    bool DrawsLive() override { return true; }

    /// <summary>The mouse and keys, as the class remarks say.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The mouse left the list: no row is lit.</summary>
    void Leave() override;

    /// <summary>Back to the top with the status line clear (the screen opened).</summary>
    void Reset();

    /// <summary>Says <paramref name="news"/> on the status line (what a button did), until the next change.</summary>
    void SetNews(std::string_view news);

    /// <summary>
    /// Lays the rows out for a narrow panel: setting boxes <paramref name="settingWidth"/> wide, and what an option does
    /// written on under its box, out to the scroll bar.
    /// </summary>
    void SetNarrow(int32_t settingWidth);

    /// <summary>
    /// Locks the list (<paramref name="locked"/>): clicks change nothing and no box lights; the status line says
    /// <paramref name="hint"/> (why) when it has no news.
    /// </summary>
    void SetLocked(bool locked, std::string_view hint);

    /// <summary>
    /// Holds row <paramref name="row"/>'s option at its setting (<paramref name="reason"/> why; empty: not held): a click
    /// changes nothing and its box doesn't light, its name and setting show dim, and its line says the reason instead of
    /// what it does.
    /// </summary>
    void SetHeld(int32_t row, std::string_view reason);

    /// <summary>Why row <paramref name="row"/>'s option is held (<see cref="SetHeld"/>; empty: it isn't).</summary>
    std::string_view HeldReason(int32_t row) const;

    /// <summary>What the status line says with the SOFTWARE renderer chosen (empty: nothing).</summary>
    void SetSoftwareHint(std::string_view hint);

    /// <summary>The headings' colour, for a screen whose palette has no amber at the preferences' (HeadingColor).</summary>
    void SetHeadingColor(uint8_t color);

    /// <summary>Moves row <paramref name="row"/>'s setting <paramref name="steps"/> choices on (back when negative), among those offered.</summary>
    void Step(int32_t row, int32_t steps);

    /// <summary>The option row under the screen point (<paramref name="xPos"/>, <paramref name="yPos"/>), if any.</summary>
    std::optional<int32_t> OptionAt(int32_t xPos, int32_t yPos);

    /// <summary>The screen point at the middle of row <paramref name="row"/>'s setting box when the row is shown.</summary>
    std::optional<std::pair<int32_t, int32_t>> SettingCentre(int32_t row);

    /// <summary>The row of the option editing PREFS key <paramref name="key"/>, if it is listed.</summary>
    std::optional<int32_t> RowOf(std::string_view key) const;

    /// <summary>The name of row <paramref name="row"/>'s setting now (its value's choice).</summary>
    std::string_view ChoiceName(int32_t row) const;

    /// <summary>Whether row <paramref name="row"/>'s option can show: the setting it needs (if any) is on.</summary>
    bool CanShow(int32_t row) const;

    /// <summary>The status line's text.</summary>
    std::string_view Status() const;

protected:
    /// <summary>The number of rows.</summary>
    int32_t RowCount() override { return static_cast<int32_t>(_Rows.size()); }

private:
    /// <summary>The setting row <paramref name="row"/> edits.</summary>
    int& Setting(int32_t row) const;

    /// <summary>The option row under the screen point that a click would change, if any: none while locked or held.</summary>
    std::optional<int32_t> ChangeableAt(int32_t xPos, int32_t yPos);

    /// <summary>The left edge of the setting boxes, in the list's pixels.</summary>
    int32_t SettingLeft();

    /// <summary>Draws option row <paramref name="row"/> with its top at <paramref name="top"/>.</summary>
    void DrawOption(int32_t row, int32_t top);

    /// <summary>The rows.</summary>
    std::span<const Row> _Rows;
    /// <summary>The option row under the mouse, if any.</summary>
    std::optional<int32_t> _Hovered;
    /// <summary>What a button did, if the status line says so (until the next change).</summary>
    std::string _News;
    /// <summary>The setting boxes' width.</summary>
    int32_t _SettingWidth;
    /// <summary>What an option does is written on under its setting box (<see cref="SetNarrow"/>).</summary>
    bool _WideDescriptions = false;
    /// <summary>Clicks change nothing (<see cref="SetLocked"/>), and the status line's hint then.</summary>
    bool _Locked = false;
    std::string _LockedHint;
    /// <summary>Why each row's option is held (<see cref="SetHeld"/>; empty: not held).</summary>
    std::vector<std::string> _Held;
    /// <summary>The status line's hint with the SOFTWARE renderer chosen.</summary>
    std::string_view _SoftwareHint;
    /// <summary>The headings' colour.</summary>
    uint8_t _HeadingColor = HeadingColor;
};
