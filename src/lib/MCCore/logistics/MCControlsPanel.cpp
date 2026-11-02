#include "stdafx.h"
#include "logistics/MCControlsPanel.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogKeyList.h"
#include "logistics/MCPrefsPage.h"

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
    AddPageButton(screen, prefsPanel, Title, buttonLeft, buttonTop, ShowControls);

    constexpr std::array<MCPageButton, 2> buttons = {
        MCPageButton{"DEFAULTS", RestoreDefaultKeys},
        MCPageButton{"LAPTOP", UseLaptopKeys},
    };

    MCLogGroupBox& panel = AddPrefsPage(screen, Title, buttons, HideControls);
    Panel = panel.weak_from_this();

    auto newList = MCMakeGui<MCLogKeyList>();
    newList->Init(PageListLeft, PageListTop, PageListWidth, PageListHeight, Edited);
    newList->ShowGuiWindow(true);
    List = newList;
    screen.AdoptChildOf(panel, std::move(newList));
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
