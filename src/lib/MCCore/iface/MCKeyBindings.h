#pragma once

#include "iface/MCInterfaceTypes.h"

class MCFitIniFile;

/// <summary>
/// Port-only: the player's key bindings. The original's are fixed (<see cref="MCTacticalInterface::Init"/> set them);
/// the CONTROLS screen changes them, and PREFS.CFG's Controls block keeps the ones changed, each as a command's name and
/// its key's (<c>st Jump = "CTRL+K"</c>), so the file reads the same on every platform.
/// </summary>
/// <remarks>
/// A binding is packed as <see cref="MCTacticalInterface::Keys"/> packs it: a scan code (0x1xx for the extended keys)
/// with <see cref="KeyShift"/>, <see cref="KeyCtrl"/> and <see cref="KeyAlt"/>. A command with no key holds
/// <see cref="MCKeyBindings::NoKey"/>, which no key event matches.
/// </remarks>
namespace MCKeyBindings
{
    /// <summary>The bindings by <see cref="MCKeyCommand"/>, packed as the namespace remarks say.</summary>
    using MCKeyTable = std::array<uint32_t, NumKeyCommands>;

    /// <summary>The binding of a command without a key (the original's for <see cref="MCKeyCommand::Unbound"/>).</summary>
    inline constexpr uint32_t NoKey = 0xffffffffu;

    /// <summary>The modifier bits of a binding.</summary>
    inline constexpr uint32_t ModifierBits = KeyShift | KeyCtrl | KeyAlt;

    /// <summary>A row of the CONTROLS screen: a heading (no command), or a command with an alternate key's.</summary>
    struct MCControlRow
    {
        /// <summary>The text shown: the heading, or the command's name.</summary>
        std::string_view Label;
        /// <summary>The command the first key gives (<see cref="MCKeyCommand::Unused"/> for a heading).</summary>
        MCKeyCommand Command = MCKeyCommand::Unused;
        /// <summary>The command the second key gives: the same one in the original, or Unused for none.</summary>
        MCKeyCommand Alternate = MCKeyCommand::Unused;

        /// <summary>Whether the row is a heading.</summary>
        constexpr bool IsHeading() const { return Command == MCKeyCommand::Unused; }
    };

    /// <summary>
    /// Sets a binding's key code, keeping its modifier bits, then sets <paramref name="modifiers"/> (the original
    /// assigns a bitfield's members one by one).
    /// </summary>
    void BindKey(uint32_t& key, uint32_t code, uint32_t modifiers);

    /// <summary>The CONTROLS screen's rows, top to bottom: every command a player can bind, under headings.</summary>
    std::span<const MCControlRow> ControlRows();

    /// <summary>The original's bindings, as <see cref="MCTacticalInterface::Init"/> set them.</summary>
    const MCKeyTable& Defaults();

    /// <summary>The player's bindings: the defaults with the changes PREFS holds.</summary>
    const MCKeyTable& Player();

    /// <summary>Makes <paramref name="keys"/> the player's bindings (the CONTROLS screen accepted).</summary>
    void SetPlayer(const MCKeyTable& keys);

    /// <summary>
    /// The binding a key event makes: <paramref name="scanCode"/> with the modifiers held. Right ctrl and right alt
    /// count as the left ones (the interface folds them), and a modifier key alone carries its own modifier, as the
    /// original's alt binding does.
    /// </summary>
    uint32_t FromKeyEvent(int16_t scanCode, bool shift, bool ctrl, bool alt);

    /// <summary>Whether <paramref name="scanCode"/> is shift, ctrl or alt (left or right).</summary>
    bool IsModifierKey(int16_t scanCode);

    /// <summary>
    /// The binding's name as PREFS keeps it: the modifiers then the key, joined by <c>+</c> (<c>CTRL+F1</c>,
    /// <c>KEYPAD 8</c>, <c>ALT</c>), <c>NONE</c> for no key; a scan code with no name is <c>KEY</c> and the code in hex.
    /// </summary>
    std::string KeyName(uint32_t key);

    /// <summary>
    /// The binding's name as the CONTROLS screen shows it: <see cref="KeyName"/>, with the Mac's names for the keys
    /// its keyboard labels otherwise (OPTION, CMD, DELETE, RETURN) on macOS, and nothing for no key.
    /// </summary>
    std::string KeyLabel(uint32_t key);

    /// <summary>The binding <paramref name="name"/> (as <see cref="KeyName"/> writes it, case ignored) names, if any.</summary>
    std::optional<uint32_t> ParseKey(std::string_view name);

    /// <summary>The name PREFS keeps <paramref name="command"/> under (empty for a command a player can't bind).</summary>
    std::string_view CommandId(MCKeyCommand command);

    /// <summary>The name the CONTROLS screen shows for <paramref name="command"/> (its row's, with ALTERNATE for a second key).</summary>
    std::string CommandLabel(MCKeyCommand command);

    /// <summary>
    /// The listed command other than <paramref name="except"/> that <paramref name="key"/> gives in
    /// <paramref name="keys"/>, or <see cref="MCKeyCommand::Unused"/> for none (no key gives nothing).
    /// </summary>
    MCKeyCommand FindCommand(const MCKeyTable& keys, uint32_t key, MCKeyCommand except);

    /// <summary>
    /// Binds <paramref name="key"/> to <paramref name="command"/> in <paramref name="keys"/>. A listed command that
    /// had the key loses it (it then has none).
    /// </summary>
    /// <returns>The command that lost the key, or <see cref="MCKeyCommand::Unused"/>.</returns>
    MCKeyCommand Bind(MCKeyTable& keys, MCKeyCommand command, uint32_t key);

    /// <summary>
    /// Reads the player's bindings from <paramref name="file"/>'s Controls block over the defaults (none: the
    /// defaults). A command or key PREFS names that isn't one is left as it was, and logged.
    /// </summary>
    void ReadPrefs(MCFitIniFile& file);

    /// <summary>Writes the Controls block: the bindings of <see cref="Player"/> that differ from the defaults.</summary>
    void WritePrefs(MCFitIniFile& file);
}
