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
#include "logistics/MCReusableDialog.h"
#include "main/MCLogistics.h"

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

    // However it closes, PREFS keeps the answer (the key, or none), so the player is asked once.
    ShowMenuMessage(GivenWayText(*givenWay), [](int32_t) { SavePrefs(); });
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    const MCKeyCommand command = givenWay->Command;
    dialog.KeyTaker = [&dialog, command](uint32_t key)
    {
        if (const MCKeyCommand owner = MCKeyBindings::FindCommand(MCKeyBindings::Player(), key, command);
            owner != MCKeyCommand::Unused)
        {
            dialog.SetText(InUseText(key, owner, command));
            dialog.Activate();
            return false;
        }

        MCKeyBindings::MCKeyTable keys = MCKeyBindings::Player();
        keys[static_cast<size_t>(command)] = key;
        MCKeyBindings::SetPlayer(keys);
        return true;
    };

    GuiSystem().SetText(dialog.weak_from_this());
}
