#include "stdafx.h"
#include "logistics/MCControlsPanel.h"
#include "gui/MCGuiSystem.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogKeyList.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCPrefsPage.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCLogistics.h"
#include "ui/MCRmlDialog.h"

namespace
{
    /// <summary>The page's title and its button on the preferences panel.</summary>
    constexpr std::string_view Title = "CONTROLS";

    /// <summary>The panels.</summary>
    std::weak_ptr<MCGuiObject> PrefsPanel;
    std::weak_ptr<MCGuiObject> Panel;
    std::weak_ptr<MCLogKeyList> List;

    /// <summary>The bindings the panel edits: the player's when the preferences opened.</summary>
    MCKeyBindings::MCKeyTable Edited = MCKeyBindings::Defaults();

    /// <summary>Shows the CONTROLS panel or the preferences panel.</summary>
    void ShowPanel(bool controls)
    {
        if (const std::shared_ptr<MCLogKeyList> list = List.lock(); list != nullptr)
        {
            list->StopListening();
        }

        ShowPrefsPage(PrefsPanel, Panel, controls);
    }

    /// <summary>DEFAULTS: the original's keys.</summary>
    void RestoreDefaultKeys()
    {
        if (const std::shared_ptr<MCLogKeyList> list = List.lock(); list != nullptr)
        {
            list->RestoreDefaults();
        }
    }

    /// <summary>LAPTOP: keys every laptop has (MCKeyBindings::Laptop).</summary>
    void UseLaptopKeys()
    {
        if (const std::shared_ptr<MCLogKeyList> list = List.lock(); list != nullptr)
        {
            list->UseLaptopKeys();
        }
    }
}

void AddControlsPanel(MCGenericScreen& screen, MCGuiObject& prefsPanel, int32_t buttonLeft, int32_t buttonTop)
{
    PrefsPanel = prefsPanel.weak_from_this();
    AddPageButton(prefsPanel, Title, buttonLeft, buttonTop, ShowControls);

    constexpr std::array<MCPageButton, 2> buttons = {
        MCPageButton{"DEFAULTS", RestoreDefaultKeys},
        MCPageButton{"LAPTOP", UseLaptopKeys},
    };

    const MCGuiHandle<MCLogGroupBox> panel = AddPrefsPage(screen, Title, buttons, HideControls);
    Panel = panel;
    List = panel->CreateChild<MCLogKeyList>(
        [](MCLogKeyList& list)
        {
            list.Init(PageListLeft, PageListTop, PageListWidth, PageListHeight, Edited);
            list.ShowGuiWindow(true);
        });
}

void ResetControls()
{
    Edited = MCKeyBindings::Player();

    if (const std::shared_ptr<MCLogKeyList> list = List.lock(); list != nullptr)
    {
        list->Reset();
    }

    ShowPanel(false);
}

void ShowControls()
{
    ShowPanel(true);
}

void HideControls()
{
    ShowPanel(false);
}

void AcceptControls()
{
    MCKeyBindings::SetPlayer(Edited);
}

bool ControlsShown()
{
    return PrefsPageShown(Panel);
}

MCKeyBindings::MCKeyTable& EditedKeys()
{
    return Edited;
}

std::shared_ptr<MCLogKeyList> ControlsList()
{
    return List.lock();
}

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

    if (MCRmlDialogs::AskKey(
            GivenWayText(*givenWay), [command](uint32_t key) { return TakeKey(command, key); }, [] { SavePrefs(); }))
    {
        return;
    }

    ShowMenuMessage(GivenWayText(*givenWay), [](int32_t) { SavePrefs(); });
    MCMenuDialog& dialog = *GlobalLog()->Message;
    dialog.KeyTaker = [&dialog, command](uint32_t key)
    {
        if (const std::optional<std::string> refused = TakeKey(command, key); refused.has_value())
        {
            dialog.SetText(*refused);
            dialog.Activate();
            return false;
        }

        return true;
    };

    dialog.TakeKeyboard();
}
