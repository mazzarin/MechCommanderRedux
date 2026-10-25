#include "stdafx.h"
#include "MCTest.h"
#include "iface/MCKeyBindings.h"

// The player's key bindings (iface/MCKeyBindings): the defaults are the original's, every command a player can bind is
// listed once, the names read back, and a key moves from the command that had it.

namespace
{
    using MCKeyBindings::MCKeyTable;

    /// <summary>The commands a player can bind: every row's command and alternate, in the list's order.</summary>
    std::vector<MCKeyCommand> ListedCommands()
    {
        std::vector<MCKeyCommand> commands;

        for (const MCKeyBindings::MCControlRow& row : MCKeyBindings::ControlRows())
        {
            if (row.IsHeading())
            {
                continue;
            }

            commands.push_back(row.Command);

            if (row.Alternate != MCKeyCommand::Unused)
            {
                commands.push_back(row.Alternate);
            }
        }

        return commands;
    }
}

/// <summary>
/// The defaults are the original's (MCX.EXE's table at InterfaceObject +0x118, as MCTacticalInterface::Init set it):
/// a sample of each kind, the alternates with their modifiers and the alt key alone with its own modifier.
/// </summary>
TEST_CASE("controls: the default keys are the original's")
{
    const MCKeyTable& keys = MCKeyBindings::Defaults();
    const auto key = [&](MCKeyCommand command) { return keys[static_cast<size_t>(command)]; };
    CHECK_EQ(key(MCKeyCommand::Eject), 0x147u);
    CHECK_EQ(key(MCKeyCommand::Run), 0x39u);
    CHECK_EQ(key(MCKeyCommand::RunAlternate), 0x39u | KeyCtrl);
    CHECK_EQ(key(MCKeyCommand::Jump), 0x24u);
    CHECK_EQ(key(MCKeyCommand::AimHead), 0x48u);
    CHECK_EQ(key(MCKeyCommand::AddLance1), 0x3bu | KeyShift);
    CHECK_EQ(key(MCKeyCommand::LinkLance4), 0x3eu | KeyCtrl);
    CHECK_EQ(key(MCKeyCommand::HoldTacticalMap), 0x38u | KeyAlt);
    CHECK_EQ(key(MCKeyCommand::SalvagePage), 0x1fu | KeyAlt);
    CHECK_EQ(key(MCKeyCommand::Artillery4), 5u);
    CHECK_EQ(key(MCKeyCommand::Unbound), MCKeyBindings::NoKey);
    CHECK_EQ(key(MCKeyCommand::Unused), 0u);

    // Before PREFS or the CONTROLS screen changes them, the player's are the defaults.
    CHECK(MCKeyBindings::Player() == MCKeyBindings::Defaults());
}

/// <summary>
/// Every command with a default key is listed once (the debug strike, a cheat's, isn't), with a name PREFS keeps it
/// under, and no two listed commands share a default key.
/// </summary>
TEST_CASE("controls: every command a player can bind is listed once, with its own key")
{
    const std::vector<MCKeyCommand> listed = ListedCommands();
    const MCKeyTable& keys = MCKeyBindings::Defaults();
    std::set<std::string_view> ids;

    for (size_t index = 0; index < keys.size(); index++)
    {
        const auto command = static_cast<MCKeyCommand>(index);
        const bool bindable =
            keys[index] != 0 && command != MCKeyCommand::Unbound && command != MCKeyCommand::DebugStrike;
        MCTest::Scope scope(std::format("command {}", index));
        CHECK_EQ(std::ranges::count(listed, command), bindable ? 1 : 0);

        if (bindable)
        {
            const std::string_view id = MCKeyBindings::CommandId(command);
            CHECK(!id.empty());
            CHECK(ids.insert(id).second);
            CHECK(!MCKeyBindings::CommandLabel(command).empty());
            CHECK_EQ(MCKeyBindings::FindCommand(keys, keys[index], command), MCKeyCommand::Unused);
        }
    }
}

/// <summary>
/// A binding's name reads back as the same binding: every default, the modifiers in any case, a key holding a
/// plus, a modifier alone, no key, and a scan code without a name.
/// </summary>
TEST_CASE("controls: key names read back as the same keys")
{
    for (const MCKeyCommand command : ListedCommands())
    {
        const uint32_t key = MCKeyBindings::Defaults()[static_cast<size_t>(command)];
        MCTest::Scope scope(MCKeyBindings::KeyName(key));
        CHECK_EQ(MCKeyBindings::ParseKey(MCKeyBindings::KeyName(key)), std::optional(key));
    }

    CHECK_EQ(MCKeyBindings::KeyName(0x21 | KeyCtrl | KeyShift), "CTRL+SHIFT+F");
    CHECK_EQ(MCKeyBindings::KeyName(0x4e | KeyCtrl), "CTRL+KEYPAD +");
    CHECK_EQ(MCKeyBindings::KeyName(0x38 | KeyAlt), "ALT");
    CHECK_EQ(MCKeyBindings::KeyName(MCKeyBindings::NoKey), "NONE");
    CHECK_EQ(MCKeyBindings::KeyName(0x56), "KEY 56");
    CHECK_EQ(MCKeyBindings::ParseKey("ctrl+shift+f"), std::optional(0x21u | KeyCtrl | KeyShift));
    CHECK_EQ(MCKeyBindings::ParseKey("ALT"), std::optional(0x38u | KeyAlt));
    CHECK_EQ(MCKeyBindings::ParseKey("KEY 56"), std::optional(0x56u));
    CHECK_EQ(MCKeyBindings::ParseKey("NONE"), std::optional(MCKeyBindings::NoKey));
    CHECK(!MCKeyBindings::ParseKey("HYPER+J").has_value());
    CHECK(!MCKeyBindings::ParseKey("").has_value());
    CHECK(!MCKeyBindings::ParseKey("KEY 0").has_value());

    // The CONTROLS screen shows no key as nothing, and on a Mac the keys by the Mac keyboard's names.
    CHECK(MCKeyBindings::KeyLabel(MCKeyBindings::NoKey).empty());
#if defined(__APPLE__)
    CHECK_EQ(MCKeyBindings::KeyLabel(0x38 | KeyAlt), "OPTION");
    CHECK_EQ(MCKeyBindings::KeyLabel(0x24 | KeyAlt), "OPTION+J");
    CHECK_EQ(MCKeyBindings::KeyLabel(0x0e), "DELETE");
#else
    CHECK_EQ(MCKeyBindings::KeyLabel(0x38 | KeyAlt), "ALT");
#endif
}

/// <summary>
/// A key event's binding: the modifiers held, right ctrl and right alt as the left ones (the interface folds them), and
/// a modifier key alone with its own modifier.
/// </summary>
TEST_CASE("controls: a key event makes the binding the interface matches")
{
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x24, false, true, false), 0x24u | KeyCtrl);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x3b, true, false, false), 0x3bu | KeyShift);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x138, false, false, false), 0x38u | KeyAlt);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x11d, false, true, false), 0x1du | KeyCtrl);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x36, false, false, false), 0x36u | KeyShift);
    CHECK(MCKeyBindings::IsModifierKey(0x2a));
    CHECK(MCKeyBindings::IsModifierKey(0x138));
    CHECK(!MCKeyBindings::IsModifierKey(0x24));
}

/// <summary>
/// Binding a key another listed command has moves it: that command is left without a key, and is the one returned; a
/// free key moves nothing; a command can't be found by no key.
/// </summary>
TEST_CASE("controls: binding a key another command has moves it from that command")
{
    MCKeyTable keys = MCKeyBindings::Defaults();
    const uint32_t f1 = keys[static_cast<size_t>(MCKeyCommand::SelectLance1)];
    CHECK_EQ(MCKeyBindings::Bind(keys, MCKeyCommand::Jump, f1), MCKeyCommand::SelectLance1);
    CHECK_EQ(keys[static_cast<size_t>(MCKeyCommand::Jump)], f1);
    CHECK_EQ(keys[static_cast<size_t>(MCKeyCommand::SelectLance1)], MCKeyBindings::NoKey);
    CHECK_EQ(MCKeyBindings::Bind(keys, MCKeyCommand::SelectLance1, 0x25), MCKeyCommand::Unused);
    CHECK_EQ(MCKeyBindings::FindCommand(keys, MCKeyBindings::NoKey, MCKeyCommand::Unused), MCKeyCommand::Unused);

    // The same key with another modifier is another key.
    CHECK_EQ(MCKeyBindings::Bind(keys, MCKeyCommand::Guard, 0x25 | KeyShift), MCKeyCommand::Unused);
}
