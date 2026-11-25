#include "stdafx.h"
#include "logistics/MCKeysNotice.h"
#include "iface/MCKeyBindings.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCReportsNotice.h"
#include "main/MCLogistics.h"
#include "main/MCPrefs.h"
#include "ui/MCRmlDialog.h"

namespace
{
    /// <summary>What the player is asked about <paramref name="givenWay"/> first, and after pressing a key in use.</summary>
    std::string GivenWayText(const MCKeyBindings::MCGivenWay& givenWay)
    {
        const std::string command = MCKeyBindings::CommandLabel(givenWay.Command);
        const std::string key = MCKeyBindings::KeyLabel(givenWay.Key);
        return std::format("{} is new and takes {}, but {} is your {} key. Press a key for {}, or OK to leave it "
                           "without one: CONTROLS can set it later.",
                           command, key, key, MCKeyBindings::CommandLabel(givenWay.Owner), command);
    }

    std::string InUseText(uint32_t key, MCKeyCommand owner, MCKeyCommand command)
    {
        return std::format("{} is your {} key. Press another key for {}, or OK to leave it without one.",
                           MCKeyBindings::KeyLabel(key), MCKeyBindings::CommandLabel(owner),
                           MCKeyBindings::CommandLabel(command));
    }

    /// <summary>
    /// <paramref name="key"/> made <paramref name="command"/>'s; when another command has it, nothing changes and the
    /// words say whose it is.
    /// </summary>
    std::optional<std::string> TakeKey(MCKeyCommand command, uint32_t key)
    {
        if (const MCKeyCommand owner = MCKeyBindings::FindCommand(MCKeyBindings::Player(), key, command);
            owner != MCKeyCommand::Unused)
        {
            return InUseText(key, owner, command);
        }

        MCKeyBindings::MCKeyTable keys = MCKeyBindings::Player();
        keys[static_cast<size_t>(command)] = key;
        MCKeyBindings::SetPlayer(keys);
        return std::nullopt;
    }
}

void AskGivenWayKeys()
{
    if (!NoticeScreenFree())
    {
        return;
    }

    const std::optional<MCKeyBindings::MCGivenWay> givenWay = MCKeyBindings::TakeGivenWay();

    if (!givenWay.has_value())
    {
        return;
    }

    // However it closes, PREFS keeps the answer (the key, or none), so the player is asked once. Port-only
    // (SPEC-menus.md's stage 7): over the remade main menu the remade dialog asks, taking the key itself.
    const MCKeyCommand command = givenWay->Command;

    MCRmlDialogs::AskKey(
        GivenWayText(*givenWay), [command](uint32_t key) { return TakeKey(command, key); }, [] { SavePrefs(); });
}
