#include "stdafx.h"
#include "iface/MCKeyBindings.h"
#include "lib/MCFitIniFile.h"

namespace
{
    using MCKeyBindings::MCControlRow;
    using MCKeyBindings::MCKeyTable;
    using MCKeyBindings::NoKey;

    /// <summary>A default key binding: the command, the scan code and the modifiers it needs.</summary>
    struct MCDefaultKey
    {
        MCKeyCommand Command = MCKeyCommand::Unused;
        uint32_t Code = 0;
        uint32_t Modifiers = 0;
    };

    /// <summary>
    /// The bindings <see cref="MCTacticalInterface::Init"/> set, in its order. Codes are DirectInput scan codes (0x1xx
    /// = extended keys).
    /// </summary>
    constexpr MCDefaultKey DefaultKeys[] = {
        {MCKeyCommand::Eject, 0x147, 0},
        {MCKeyCommand::Stop, 0xe, 0},
        {MCKeyCommand::Run, 0x39, 0},
        {MCKeyCommand::ForcedOrder, 0x43, 0},
        {MCKeyCommand::ForcedRun, 0x44, 0},
        {MCKeyCommand::ForcedJump, 0x57, 0},
        {MCKeyCommand::ForcedOrderAlternate, 0x58, 0},
        {MCKeyCommand::RunAlternate, 0x39, KeyCtrl},
        {MCKeyCommand::Info, 0x17, 0},
        {MCKeyCommand::AttackOptimalRange, 0x18, 0},
        {MCKeyCommand::AttackConservingAmmo, 0x1e, 0},
        {MCKeyCommand::AttackLongRange, 0x26, 0},
        {MCKeyCommand::AttackMediumRange, 0x32, 0},
        {MCKeyCommand::AttackShortRange, 0x1f, 0},
        {MCKeyCommand::AttackFromPosition, 0x2e, 0},
        {MCKeyCommand::Jump, 0x24, 0},
        {MCKeyCommand::JumpAlternate, 0x24, KeyCtrl},
        {MCKeyCommand::LayMines, 0x21, 0},
        {MCKeyCommand::LayMinesAlternate, 0x21, KeyCtrl},
        {MCKeyCommand::Guard, 0x22, 0},
        {MCKeyCommand::Unbound, NoKey, 0},
        {MCKeyCommand::PowerUp, 0x149, 0},
        {MCKeyCommand::PowerDown, 0x151, 0},
        {MCKeyCommand::AimHead, 0x48, 0},
        {MCKeyCommand::AimLeftTorso, 0x47, 0},
        {MCKeyCommand::AimRightTorso, 0x49, 0},
        {MCKeyCommand::AimCenterTorso, 0x4c, 0},
        {MCKeyCommand::AimLeftArm, 0x4b, 0},
        {MCKeyCommand::AimRightArm, 0x4d, 0},
        {MCKeyCommand::AimLeftLeg, 0x4f, 0},
        {MCKeyCommand::AimRightLeg, 0x51, 0},
        {MCKeyCommand::CameraFollow, 0x14, 0},
        {MCKeyCommand::Artillery1, 2, 0},
        {MCKeyCommand::Artillery2, 3, 0},
        {MCKeyCommand::Artillery3, 4, 0},
        {MCKeyCommand::Artillery4, 5, 0},
        {MCKeyCommand::DebugStrike, 0x30, 0},
        {MCKeyCommand::LinkLance1, 0x3b, KeyCtrl},
        {MCKeyCommand::LinkLance2, 0x3c, KeyCtrl},
        {MCKeyCommand::LinkLance3, 0x3d, KeyCtrl},
        {MCKeyCommand::LinkLance4, 0x3e, KeyCtrl},
        {MCKeyCommand::SelectLance1, 0x3b, 0},
        {MCKeyCommand::SelectLance2, 0x3c, 0},
        {MCKeyCommand::SelectLance3, 0x3d, 0},
        {MCKeyCommand::SelectLance4, 0x3e, 0},
        {MCKeyCommand::AddLance1, 0x3b, KeyShift},
        {MCKeyCommand::AddLance2, 0x3c, KeyShift},
        {MCKeyCommand::AddLance3, 0x3d, KeyShift},
        {MCKeyCommand::AddLance4, 0x3e, KeyShift},
        {MCKeyCommand::BreakLances, 0x3f, 0},
        {MCKeyCommand::ZoomIn, 0x4e, 0},
        {MCKeyCommand::ZoomOut, 0x4a, 0},
        {MCKeyCommand::ZoomInAlternate, 0xd, 0},
        {MCKeyCommand::ZoomOutAlternate, 0xc, 0},
        {MCKeyCommand::TacticalMapZoomIn, 0x4e, KeyCtrl},
        {MCKeyCommand::TacticalMapZoomOut, 0x4a, KeyCtrl},
        {MCKeyCommand::TacticalMapZoomInAlternate, 0xd, KeyCtrl},
        {MCKeyCommand::TacticalMapZoomOutAlternate, 0xc, KeyCtrl},
        {MCKeyCommand::HoldTacticalMap, 0x38, KeyAlt},
        {MCKeyCommand::MapPage, 0x32, KeyAlt},
        {MCKeyCommand::SalvagePage, 0x1f, KeyAlt},
        {MCKeyCommand::InfoPage, 0x20, KeyAlt},
        {MCKeyCommand::MissionPage, 0x30, KeyAlt},
        {MCKeyCommand::ScrollUp, 0x148, 0},
        {MCKeyCommand::ScrollDown, 0x150, 0},
        {MCKeyCommand::ScrollLeft, 0x14b, 0},
        {MCKeyCommand::ScrollRight, 0x14d, 0},
        {MCKeyCommand::TacticalMapScrollUp, 0x148, KeyCtrl},
        {MCKeyCommand::TacticalMapScrollDown, 0x150, KeyCtrl},
        {MCKeyCommand::TacticalMapScrollLeft, 0x14b, KeyCtrl},
        {MCKeyCommand::TacticalMapScrollRight, 0x14d, KeyCtrl},
        {MCKeyCommand::TogglePalette, 0xf, 0},
        {MCKeyCommand::SelectVisible, 0x12, 0},
        {MCKeyCommand::Chat, 0x1c, 0},
    };

    /// <summary>
    /// The CONTROLS screen's rows. The debug strike (B with the bunny-strikes switch) isn't listed: it is a cheat's, and
    /// its key gives nothing without it.
    /// </summary>
    constexpr MCControlRow ControlRowTable[] = {
        {"ORDERS"},
        {"RUN", MCKeyCommand::Run, MCKeyCommand::RunAlternate},
        {"JUMP", MCKeyCommand::Jump, MCKeyCommand::JumpAlternate},
        {"STOP", MCKeyCommand::Stop},
        {"GUARD", MCKeyCommand::Guard},
        {"LAY MINES", MCKeyCommand::LayMines, MCKeyCommand::LayMinesAlternate},
        {"POWER UP", MCKeyCommand::PowerUp},
        {"POWER DOWN", MCKeyCommand::PowerDown},
        {"EJECT", MCKeyCommand::Eject},
        {"INFO", MCKeyCommand::Info},
        {"FORCE ORDER (HOLD)", MCKeyCommand::ForcedOrder, MCKeyCommand::ForcedOrderAlternate},
        {"FORCE RUN (HOLD)", MCKeyCommand::ForcedRun},
        {"FORCE JUMP (HOLD)", MCKeyCommand::ForcedJump},
        {"ATTACK"},
        {"OPTIMAL RANGE", MCKeyCommand::AttackOptimalRange},
        {"SHORT RANGE", MCKeyCommand::AttackShortRange},
        {"MEDIUM RANGE", MCKeyCommand::AttackMediumRange},
        {"LONG RANGE", MCKeyCommand::AttackLongRange},
        {"FROM POSITION", MCKeyCommand::AttackFromPosition},
        {"CONSERVE AMMO", MCKeyCommand::AttackConservingAmmo},
        {"AIMED SHOTS"},
        {"HEAD", MCKeyCommand::AimHead},
        {"CENTER TORSO", MCKeyCommand::AimCenterTorso},
        {"LEFT TORSO", MCKeyCommand::AimLeftTorso},
        {"RIGHT TORSO", MCKeyCommand::AimRightTorso},
        {"LEFT ARM", MCKeyCommand::AimLeftArm},
        {"RIGHT ARM", MCKeyCommand::AimRightArm},
        {"LEFT LEG", MCKeyCommand::AimLeftLeg},
        {"RIGHT LEG", MCKeyCommand::AimRightLeg},
        {"LANCES"},
        {"SELECT LANCE 1", MCKeyCommand::SelectLance1},
        {"SELECT LANCE 2", MCKeyCommand::SelectLance2},
        {"SELECT LANCE 3", MCKeyCommand::SelectLance3},
        {"SELECT LANCE 4", MCKeyCommand::SelectLance4},
        {"ADD LANCE 1", MCKeyCommand::AddLance1},
        {"ADD LANCE 2", MCKeyCommand::AddLance2},
        {"ADD LANCE 3", MCKeyCommand::AddLance3},
        {"ADD LANCE 4", MCKeyCommand::AddLance4},
        {"MAKE LANCE 1", MCKeyCommand::LinkLance1},
        {"MAKE LANCE 2", MCKeyCommand::LinkLance2},
        {"MAKE LANCE 3", MCKeyCommand::LinkLance3},
        {"MAKE LANCE 4", MCKeyCommand::LinkLance4},
        {"BREAK UP LANCES", MCKeyCommand::BreakLances},
        {"SELECT ALL ON SCREEN", MCKeyCommand::SelectVisible},
        {"SUPPORT"},
        {"SUPPORT 1", MCKeyCommand::Artillery1},
        {"SUPPORT 2", MCKeyCommand::Artillery2},
        {"SUPPORT 3", MCKeyCommand::Artillery3},
        {"SUPPORT 4", MCKeyCommand::Artillery4},
        {"CAMERA"},
        {"SCROLL UP", MCKeyCommand::ScrollUp},
        {"SCROLL DOWN", MCKeyCommand::ScrollDown},
        {"SCROLL LEFT", MCKeyCommand::ScrollLeft},
        {"SCROLL RIGHT", MCKeyCommand::ScrollRight},
        {"ZOOM IN", MCKeyCommand::ZoomIn, MCKeyCommand::ZoomInAlternate},
        {"ZOOM OUT", MCKeyCommand::ZoomOut, MCKeyCommand::ZoomOutAlternate},
        {"FOLLOW", MCKeyCommand::CameraFollow},
        {"TACTICAL MAP"},
        {"SHOW MAP (HOLD)", MCKeyCommand::HoldTacticalMap},
        {"MAP PAGE", MCKeyCommand::MapPage},
        {"SALVAGE PAGE", MCKeyCommand::SalvagePage},
        {"INFO PAGE", MCKeyCommand::InfoPage},
        {"MISSION PAGE", MCKeyCommand::MissionPage},
        {"MAP ZOOM IN", MCKeyCommand::TacticalMapZoomIn, MCKeyCommand::TacticalMapZoomInAlternate},
        {"MAP ZOOM OUT", MCKeyCommand::TacticalMapZoomOut, MCKeyCommand::TacticalMapZoomOutAlternate},
        {"MAP SCROLL UP", MCKeyCommand::TacticalMapScrollUp},
        {"MAP SCROLL DOWN", MCKeyCommand::TacticalMapScrollDown},
        {"MAP SCROLL LEFT", MCKeyCommand::TacticalMapScrollLeft},
        {"MAP SCROLL RIGHT", MCKeyCommand::TacticalMapScrollRight},
        {"COMMAND PALETTE", MCKeyCommand::TogglePalette},
        {"MULTIPLAYER"},
        {"CHAT", MCKeyCommand::Chat},
    };

    /// <summary>The name PREFS keeps each command a player can bind under (the command's own).</summary>
    struct MCCommandId
    {
        MCKeyCommand Command = MCKeyCommand::Unused;
        std::string_view Id;
    };

    constexpr MCCommandId CommandIds[] = {
        {MCKeyCommand::Eject, "Eject"},
        {MCKeyCommand::Stop, "Stop"},
        {MCKeyCommand::Run, "Run"},
        {MCKeyCommand::ForcedOrder, "ForcedOrder"},
        {MCKeyCommand::ForcedRun, "ForcedRun"},
        {MCKeyCommand::ForcedOrderAlternate, "ForcedOrderAlternate"},
        {MCKeyCommand::ForcedJump, "ForcedJump"},
        {MCKeyCommand::RunAlternate, "RunAlternate"},
        {MCKeyCommand::AttackOptimalRange, "AttackOptimalRange"},
        {MCKeyCommand::AttackLongRange, "AttackLongRange"},
        {MCKeyCommand::AttackMediumRange, "AttackMediumRange"},
        {MCKeyCommand::AttackShortRange, "AttackShortRange"},
        {MCKeyCommand::AttackFromPosition, "AttackFromPosition"},
        {MCKeyCommand::AttackConservingAmmo, "AttackConservingAmmo"},
        {MCKeyCommand::Jump, "Jump"},
        {MCKeyCommand::JumpAlternate, "JumpAlternate"},
        {MCKeyCommand::Guard, "Guard"},
        {MCKeyCommand::PowerUp, "PowerUp"},
        {MCKeyCommand::PowerDown, "PowerDown"},
        {MCKeyCommand::AimHead, "AimHead"},
        {MCKeyCommand::AimLeftTorso, "AimLeftTorso"},
        {MCKeyCommand::AimRightTorso, "AimRightTorso"},
        {MCKeyCommand::AimCenterTorso, "AimCenterTorso"},
        {MCKeyCommand::AimLeftArm, "AimLeftArm"},
        {MCKeyCommand::AimRightArm, "AimRightArm"},
        {MCKeyCommand::AimLeftLeg, "AimLeftLeg"},
        {MCKeyCommand::AimRightLeg, "AimRightLeg"},
        {MCKeyCommand::LayMines, "LayMines"},
        {MCKeyCommand::LayMinesAlternate, "LayMinesAlternate"},
        {MCKeyCommand::SelectLance1, "SelectLance1"},
        {MCKeyCommand::SelectLance2, "SelectLance2"},
        {MCKeyCommand::SelectLance3, "SelectLance3"},
        {MCKeyCommand::SelectLance4, "SelectLance4"},
        {MCKeyCommand::AddLance1, "AddLance1"},
        {MCKeyCommand::AddLance2, "AddLance2"},
        {MCKeyCommand::AddLance3, "AddLance3"},
        {MCKeyCommand::AddLance4, "AddLance4"},
        {MCKeyCommand::LinkLance1, "LinkLance1"},
        {MCKeyCommand::LinkLance2, "LinkLance2"},
        {MCKeyCommand::LinkLance3, "LinkLance3"},
        {MCKeyCommand::LinkLance4, "LinkLance4"},
        {MCKeyCommand::BreakLances, "BreakLances"},
        {MCKeyCommand::Artillery1, "Support1"},
        {MCKeyCommand::Artillery2, "Support2"},
        {MCKeyCommand::Artillery4, "Support4"},
        {MCKeyCommand::Artillery3, "Support3"},
        {MCKeyCommand::Info, "Info"},
        {MCKeyCommand::HoldTacticalMap, "ShowMap"},
        {MCKeyCommand::ZoomIn, "ZoomIn"},
        {MCKeyCommand::ZoomOut, "ZoomOut"},
        {MCKeyCommand::ZoomInAlternate, "ZoomInAlternate"},
        {MCKeyCommand::ZoomOutAlternate, "ZoomOutAlternate"},
        {MCKeyCommand::TacticalMapZoomIn, "MapZoomIn"},
        {MCKeyCommand::TacticalMapZoomOut, "MapZoomOut"},
        {MCKeyCommand::TacticalMapZoomInAlternate, "MapZoomInAlternate"},
        {MCKeyCommand::TacticalMapZoomOutAlternate, "MapZoomOutAlternate"},
        {MCKeyCommand::MapPage, "MapPage"},
        {MCKeyCommand::SalvagePage, "SalvagePage"},
        {MCKeyCommand::InfoPage, "InfoPage"},
        {MCKeyCommand::MissionPage, "MissionPage"},
        {MCKeyCommand::ScrollUp, "ScrollUp"},
        {MCKeyCommand::ScrollDown, "ScrollDown"},
        {MCKeyCommand::ScrollLeft, "ScrollLeft"},
        {MCKeyCommand::ScrollRight, "ScrollRight"},
        {MCKeyCommand::TacticalMapScrollUp, "MapScrollUp"},
        {MCKeyCommand::TacticalMapScrollDown, "MapScrollDown"},
        {MCKeyCommand::TacticalMapScrollLeft, "MapScrollLeft"},
        {MCKeyCommand::TacticalMapScrollRight, "MapScrollRight"},
        {MCKeyCommand::TogglePalette, "CommandPalette"},
        {MCKeyCommand::CameraFollow, "CameraFollow"},
        {MCKeyCommand::SelectVisible, "SelectVisible"},
        {MCKeyCommand::Chat, "Chat"},
    };

    /// <summary>A key's name (as PREFS keeps it), and the Mac keyboard's label for it when that differs.</summary>
    struct MCKeyNameEntry
    {
        uint16_t Code = 0;
        std::string_view Name;
        std::string_view MacName;
    };

    /// <summary>The scan codes the game's input gives (MCInput::Win32ScanCode), with their names.</summary>
    constexpr MCKeyNameEntry KeyNames[] = {
        {0x01, "ESC"},
        {0x02, "1"},
        {0x03, "2"},
        {0x04, "3"},
        {0x05, "4"},
        {0x06, "5"},
        {0x07, "6"},
        {0x08, "7"},
        {0x09, "8"},
        {0x0a, "9"},
        {0x0b, "0"},
        {0x0c, "-"},
        {0x0d, "="},
        {0x0e, "BACKSPACE", "DELETE"},
        {0x0f, "TAB"},
        {0x10, "Q"},
        {0x11, "W"},
        {0x12, "E"},
        {0x13, "R"},
        {0x14, "T"},
        {0x15, "Y"},
        {0x16, "U"},
        {0x17, "I"},
        {0x18, "O"},
        {0x19, "P"},
        {0x1a, "["},
        {0x1b, "]"},
        {0x1c, "ENTER", "RETURN"},
        {0x1d, "CTRL"},
        {0x1e, "A"},
        {0x1f, "S"},
        {0x20, "D"},
        {0x21, "F"},
        {0x22, "G"},
        {0x23, "H"},
        {0x24, "J"},
        {0x25, "K"},
        {0x26, "L"},
        {0x27, ";"},
        {0x28, "'"},
        {0x29, "`"},
        {0x2a, "SHIFT"},
        {0x2b, "\\"},
        {0x2c, "Z"},
        {0x2d, "X"},
        {0x2e, "C"},
        {0x2f, "V"},
        {0x30, "B"},
        {0x31, "N"},
        {0x32, "M"},
        {0x33, ","},
        {0x34, "."},
        {0x35, "/"},
        {0x36, "RIGHT SHIFT"},
        {0x37, "KEYPAD *"},
        {0x38, "ALT", "OPTION"},
        {0x39, "SPACE"},
        {0x3a, "CAPS LOCK"},
        {0x3b, "F1"},
        {0x3c, "F2"},
        {0x3d, "F3"},
        {0x3e, "F4"},
        {0x3f, "F5"},
        {0x40, "F6"},
        {0x41, "F7"},
        {0x42, "F8"},
        {0x43, "F9"},
        {0x44, "F10"},
        {0x45, "NUM LOCK"},
        {0x46, "SCROLL LOCK"},
        {0x47, "KEYPAD 7"},
        {0x48, "KEYPAD 8"},
        {0x49, "KEYPAD 9"},
        {0x4a, "KEYPAD -"},
        {0x4b, "KEYPAD 4"},
        {0x4c, "KEYPAD 5"},
        {0x4d, "KEYPAD 6"},
        {0x4e, "KEYPAD +"},
        {0x4f, "KEYPAD 1"},
        {0x50, "KEYPAD 2"},
        {0x51, "KEYPAD 3"},
        {0x52, "KEYPAD 0"},
        {0x53, "KEYPAD ."},
        {0x57, "F11"},
        {0x58, "F12"},
        {0x11c, "KEYPAD ENTER"},
        {0x135, "KEYPAD /"},
        {0x137, "PRINT SCREEN"},
        {0x147, "HOME"},
        {0x148, "UP"},
        {0x149, "PAGE UP"},
        {0x14b, "LEFT"},
        {0x14d, "RIGHT"},
        {0x14f, "END"},
        {0x150, "DOWN"},
        {0x151, "PAGE DOWN"},
        {0x152, "INSERT"},
        {0x153, "DELETE", "FWD DELETE"},
        {0x15b, "WINDOWS", "CMD"},
        {0x15c, "RIGHT WINDOWS", "RIGHT CMD"},
        {0x15d, "MENU"},
    };

    /// <summary>The modifiers' names, in the order a binding's name gives them, with the keys that are them.</summary>
    struct MCModifierName
    {
        uint32_t Bit = 0;
        std::string_view Name;
        std::string_view MacName;
        uint16_t LeftCode = 0;
        uint16_t RightCode = 0;
    };

    constexpr MCModifierName ModifierNames[] = {
        {KeyCtrl, "CTRL", "CTRL", 0x1d, 0x11d},
        {KeyAlt, "ALT", "OPTION", 0x38, 0x138},
        {KeyShift, "SHIFT", "SHIFT", 0x2a, 0x36},
    };

    /// <summary>The name of no key, as PREFS keeps it.</summary>
    constexpr std::string_view NoKeyName = "NONE";
    /// <summary>The name of a scan code without one: this and the code in hex.</summary>
    constexpr std::string_view UnnamedKeyPrefix = "KEY ";
    /// <summary>The block of PREFS.CFG the changed bindings are in.</summary>
    constexpr std::string_view PrefsBlock = "Controls";
    /// <summary>Mask of a binding's key code.</summary>
    constexpr uint32_t CodeBits = 0xffff;

    /// <summary>Whether the build is the Mac's (its keyboard's names for the keys).</summary>
    constexpr bool MacNames =
#if defined(__APPLE__)
        true;
#else
        false;
#endif

    /// <summary>The upper-case copy of <paramref name="text"/>.</summary>
    std::string Upper(std::string_view text)
    {
        std::string result(text);
        std::ranges::transform(result, result.begin(),
                               [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
        return result;
    }

    /// <summary>The player's bindings (the defaults until PREFS or the CONTROLS screen changes them).</summary>
    MCKeyTable& PlayerKeys()
    {
        static MCKeyTable keys = MCKeyBindings::Defaults();
        return keys;
    }

    /// <summary>Calls <paramref name="visit"/> with each command a player can bind, in the CONTROLS screen's order.</summary>
    template <typename F> void ForEachListedCommand(F&& visit)
    {
        for (const MCControlRow& row : ControlRowTable)
        {
            if (row.IsHeading())
            {
                continue;
            }

            visit(row.Command);

            if (row.Alternate != MCKeyCommand::Unused)
            {
                visit(row.Alternate);
            }
        }
    }

    /// <summary>The name of the key with scan code <paramref name="code"/> (the Mac's label when asked and there is one).</summary>
    std::string CodeName(uint32_t code, bool macName)
    {
        const auto entry = std::ranges::find(KeyNames, code, &MCKeyNameEntry::Code);

        if (entry == std::end(KeyNames))
        {
            return std::format("{}{:X}", UnnamedKeyPrefix, code);
        }

        return std::string(macName && !entry->MacName.empty() ? entry->MacName : entry->Name);
    }

    /// <summary>The binding's name with or without the Mac keyboard's labels.</summary>
    std::string BindingName(uint32_t key, bool macNames)
    {
        if (key == NoKey)
        {
            return std::string(NoKeyName);
        }

        const uint32_t code = key & CodeBits;
        std::string name;

        for (const MCModifierName& modifier : ModifierNames)
        {
            // A modifier key alone carries its own modifier: its name says it once.
            if ((key & modifier.Bit) != 0 && code != modifier.LeftCode && code != modifier.RightCode)
            {
                name += std::format("{}+", macNames ? modifier.MacName : modifier.Name);
            }
        }

        return name + CodeName(code, macNames);
    }
}

void MCKeyBindings::BindKey(uint32_t& key, uint32_t code, uint32_t modifiers)
{
    key = (key & ModifierBits) + code;

    if (modifiers != 0)
    {
        key = (key & ~modifiers) + modifiers;
    }
}

auto MCKeyBindings::ControlRows() -> std::span<const MCControlRow>
{
    return ControlRowTable;
}

auto MCKeyBindings::Defaults() -> const MCKeyTable&
{
    static const MCKeyTable defaults = []
    {
        MCKeyTable keys{};

        for (const MCDefaultKey& binding : DefaultKeys)
        {
            BindKey(keys[static_cast<size_t>(binding.Command)], binding.Code, binding.Modifiers);
        }

        return keys;
    }();

    return defaults;
}

auto MCKeyBindings::Player() -> const MCKeyTable&
{
    return PlayerKeys();
}

void MCKeyBindings::SetPlayer(const MCKeyTable& keys)
{
    PlayerKeys() = keys;
}

bool MCKeyBindings::IsModifierKey(int16_t scanCode)
{
    return std::ranges::any_of(ModifierNames, [scanCode](const MCModifierName& modifier)
                               { return scanCode == modifier.LeftCode || scanCode == modifier.RightCode; });
}

uint32_t MCKeyBindings::FromKeyEvent(int16_t scanCode, bool shift, bool ctrl, bool alt)
{
    // Right ctrl and right alt count as the left ones, as MCTacticalInterface::HandleKeyDown folds them.
    uint32_t code = static_cast<uint16_t>(scanCode);
    uint32_t key = 0;

    for (const MCModifierName& modifier : ModifierNames)
    {
        if (modifier.Bit != KeyShift && code == modifier.RightCode)
        {
            code = modifier.LeftCode;
        }

        if (code == modifier.LeftCode || code == modifier.RightCode)
        {
            key |= modifier.Bit;
        }
    }

    key |= (shift ? KeyShift : 0) | (ctrl ? KeyCtrl : 0) | (alt ? KeyAlt : 0);
    return key | code;
}

std::string MCKeyBindings::KeyName(uint32_t key)
{
    return BindingName(key, false);
}

std::string MCKeyBindings::KeyLabel(uint32_t key)
{
    return key == NoKey ? std::string() : BindingName(key, MacNames);
}

auto MCKeyBindings::ParseKey(std::string_view name) -> std::optional<uint32_t>
{
    const std::string upper = Upper(name);
    std::string_view rest = upper;

    if (rest == NoKeyName)
    {
        return NoKey;
    }

    uint32_t modifiers = 0;

    // The modifiers lead, each with its +; what is left names the key (which may itself hold a +: KEYPAD +).
    for (bool found = true; found;)
    {
        found = false;

        for (const MCModifierName& modifier : ModifierNames)
        {
            if (rest.size() > modifier.Name.size() && rest.starts_with(modifier.Name) &&
                rest[modifier.Name.size()] == '+')
            {
                modifiers |= modifier.Bit;
                rest.remove_prefix(modifier.Name.size() + 1);
                found = true;
            }
        }
    }

    std::optional<uint32_t> code;

    if (const auto entry = std::ranges::find(KeyNames, rest, &MCKeyNameEntry::Name); entry != std::end(KeyNames))
    {
        code = entry->Code;
    }
    else if (rest.starts_with(UnnamedKeyPrefix))
    {
        const std::string_view digits = rest.substr(UnnamedKeyPrefix.size());
        uint32_t value = 0;

        if (const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
            error == std::errc() && end == digits.data() + digits.size() && value != 0 && value <= CodeBits)
        {
            code = value;
        }
    }

    if (!code.has_value())
    {
        return std::nullopt;
    }

    const auto scanCode = static_cast<int16_t>(*code);
    return FromKeyEvent(scanCode, (modifiers & KeyShift) != 0, (modifiers & KeyCtrl) != 0, (modifiers & KeyAlt) != 0);
}

std::string_view MCKeyBindings::CommandId(MCKeyCommand command)
{
    const auto entry = std::ranges::find(CommandIds, command, &MCCommandId::Command);
    return entry != std::end(CommandIds) ? entry->Id : std::string_view();
}

std::string MCKeyBindings::CommandLabel(MCKeyCommand command)
{
    for (const MCControlRow& row : ControlRowTable)
    {
        if (!row.IsHeading() && row.Command == command)
        {
            return std::string(row.Label);
        }

        if (!row.IsHeading() && row.Alternate == command)
        {
            return std::format("{} ALTERNATE", row.Label);
        }
    }

    return {};
}

MCKeyCommand MCKeyBindings::FindCommand(const MCKeyTable& keys, uint32_t key, MCKeyCommand except)
{
    MCKeyCommand found = MCKeyCommand::Unused;

    if (key == NoKey)
    {
        return found;
    }

    ForEachListedCommand(
        [&](MCKeyCommand command)
        {
            if (found == MCKeyCommand::Unused && command != except && keys[static_cast<size_t>(command)] == key)
            {
                found = command;
            }
        });

    return found;
}

MCKeyCommand MCKeyBindings::Bind(MCKeyTable& keys, MCKeyCommand command, uint32_t key)
{
    const MCKeyCommand other = FindCommand(keys, key, command);

    if (other != MCKeyCommand::Unused)
    {
        keys[static_cast<size_t>(other)] = NoKey;
    }

    keys[static_cast<size_t>(command)] = key;
    return other;
}

void MCKeyBindings::ReadPrefs(MCFitIniFile& file)
{
    MCKeyTable keys = Defaults();

    if (file.SeekBlock(PrefsBlock) == 0)
    {
        ForEachListedCommand(
            [&](MCKeyCommand command)
            {
                const std::string_view id = CommandId(command);
                const MCFitResult<std::string> name = file.Read<std::string>(id);

                if (!name.has_value())
                {
                    return;
                }

                if (const std::optional<uint32_t> key = ParseKey(*name); key.has_value())
                {
                    keys[static_cast<size_t>(command)] = *key;
                }
                else
                {
                    SDL_Log("MCKeyBindings: PREFS %.*s gives %.*s no key it knows (\"%s\"); it keeps its own",
                            static_cast<int>(PrefsBlock.size()), PrefsBlock.data(), static_cast<int>(id.size()),
                            id.data(), name->c_str());
                }
            });
    }

    SetPlayer(keys);
}

void MCKeyBindings::WritePrefs(MCFitIniFile& file)
{
    const MCKeyTable& keys = Player();
    const MCKeyTable& defaults = Defaults();
    bool blockWritten = false;

    ForEachListedCommand(
        [&](MCKeyCommand command)
        {
            const auto index = static_cast<size_t>(command);

            if (keys[index] == defaults[index])
            {
                return;
            }

            if (!blockWritten)
            {
                file.WriteBlock(PrefsBlock);
                blockWritten = true;
            }

            file.WriteIdString(CommandId(command), KeyName(keys[index]));
        });
}
