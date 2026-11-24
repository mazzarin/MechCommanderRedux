#pragma once

#include "lib/MCOptionalRef.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// Port-only: a row of a list of options, as data, and its choices: the HD OPTIONS page's (main/MCHdSettings) and the
// match options' (logistics/MCMatchOptionRows.h), which the 1998 option list (logistics/MCLogOptionList.h, whose Row
// and Choice they are) and the remade screens show. Port-only (SPEC-menus.md's stage 9, step 2): pulled out of the
// 1998 list, unchanged.

/// <summary>A choice of an option: its name and the setting's value.</summary>
struct MCOptionChoice
{
    std::string_view Name;
    int32_t Value = 0;
    /// <summary>Whether it is offered now (none: always).</summary>
    bool (*Offered)() = nullptr;
};

/// <summary>A row: an option, or a heading (no key).</summary>
struct MCOptionRow
{
    /// <summary>The option's name, or the heading.</summary>
    std::string_view Label;
    /// <summary>What the option does.</summary>
    std::string_view Description;
    /// <summary>The PREFS key of the setting it edits (<see cref="MCHdSettings::Find"/>).</summary>
    std::string_view Key;
    /// <summary>Port: the value it edits instead, with no PREFS key (none: the key's).</summary>
    MCOptionalRef<int> Value;
    /// <summary>
    /// Port: what <see cref="Value"/> stands for as things are now, read into it each time it's asked for, as the
    /// value is kept elsewhere (the updater's HD art size; none: <see cref="Value"/> is the setting itself).
    /// </summary>
    int32_t (*Current)() = nullptr;
    /// <summary>Its choices, in the order a click steps through them.</summary>
    std::span<const MCOptionChoice> Choices;
    /// <summary>Port: only the GPU renderer draws it (it shows nothing while the CPU draws the game).</summary>
    bool Gpu = false;
    /// <summary>The setting that must be on for it to show (its key), and that setting's name.</summary>
    std::string_view Needs;
    std::string_view NeedsLabel;
    /// <summary>Called with the new value when a click changes it.</summary>
    void (*Changed)(int32_t value) = nullptr;
    /// <summary>Port: its line as things stand, asked each frame instead of <see cref="Description"/> (none: that).</summary>
    std::string (*Line)() = nullptr;
    /// <summary>
    /// Port: why it can't be changed now, asked each frame (<see cref="MCLogOptionList::SetHeld"/>; none, or empty: it
    /// can).
    /// </summary>
    std::string (*HeldNow)() = nullptr;
    /// <summary>
    /// Port: an action, not a setting: a button of the page's own stands where the setting's box would be, told
    /// each frame where that box's middle is on the screen, or none while the row is scrolled away or held.
    /// </summary>
    void (*Place)(std::optional<std::pair<int32_t, int32_t>> centre) = nullptr;

    constexpr bool IsHeading() const { return Key.empty() && !Value && Place == nullptr; }
};
