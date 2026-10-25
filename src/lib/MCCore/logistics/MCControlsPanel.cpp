#include "stdafx.h"
#include "logistics/MCControlsPanel.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogKeyList.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogTextButton.h"

namespace
{
    // The panel covers the screen below the logo: the main menu's art and the preferences' art, as the preferences
    // panel covers its art. Places are in the screen's pixels for the panel, from the panel's corner for what is on it.

    /// <summary>A place and a size, the outline included.</summary>
    struct MCPanelPlace
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    constexpr MCPanelPlace PanelPlace{16, 93, 608, 375};
    constexpr MCPanelPlace ListPlace{10, 30, 588, 312};

    /// <summary>The panel's outline and fill: the preferences panel's.</summary>
    constexpr uint8_t PanelOutlineColor = 0x18;
    constexpr uint8_t PanelFillColor = 0x10;

    /// <summary>The title, top left.</summary>
    constexpr std::string_view Title = "CONTROLS";
    constexpr int32_t TitleLeft = 12;
    constexpr int32_t TitleTop = 8;

    /// <summary>DEFAULTS at the bottom left, DONE at the bottom right, 10 pixels in.</summary>
    constexpr int32_t ButtonInset = 10;

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

        if (const std::shared_ptr<MCGuiObject> prefs = PrefsPanel.lock(); prefs != nullptr)
        {
            prefs->ShowGuiWindow(!controls);
        }

        if (const std::shared_ptr<MCGuiObject> panel = Panel.lock(); panel != nullptr)
        {
            panel->ShowGuiWindow(controls);
        }
    }

    /// <summary>DEFAULTS: the original's keys.</summary>
    void RestoreDefaultKeys()
    {
        if (const std::shared_ptr<MCLogKeyList> list = List.lock(); list != nullptr)
        {
            list->RestoreDefaults();
        }
    }
}

void AddControlsPanel(MCGenericScreen& screen, MCGuiObject& prefsPanel, int32_t buttonLeft, int32_t buttonTop)
{
    PrefsPanel = prefsPanel.weak_from_this();

    auto controlsButton = MCMakeGui<MCLogTextButton>();
    controlsButton->InitWithLabel(buttonLeft, buttonTop, Title, ShowControls);
    controlsButton->ShowGuiWindow(true);
    screen.AdoptChildOf(prefsPanel, std::move(controlsButton));

    auto newPanel = MCMakeGui<MCLogGroupBox>(PanelOutlineColor, PanelFillColor);
    newPanel->Init(PanelPlace.Left, PanelPlace.Top, PanelPlace.Width, PanelPlace.Height);
    newPanel->SetTransparent(true);
    newPanel->ShowGuiWindow(false);
    Panel = newPanel;
    MCLogGroupBox& panel = screen.AdoptChild(std::move(newPanel));

    auto title = MCMakeGui<MCLogLabel>(Title, LgWhiteFont);
    title->InitAt(TitleLeft, TitleTop);
    title->ShowGuiWindow(true);
    screen.AdoptChildOf(panel, std::move(title));

    auto newList = MCMakeGui<MCLogKeyList>();
    newList->Init(ListPlace.Left, ListPlace.Top, ListPlace.Width, ListPlace.Height, Edited);
    newList->ShowGuiWindow(true);
    List = newList;
    screen.AdoptChildOf(panel, std::move(newList));

    // The buttons take the art's size; they are placed once made.
    auto defaults = MCMakeGui<MCLogTextButton>();
    defaults->InitWithLabel(0, 0, "DEFAULTS", RestoreDefaultKeys);
    const int32_t buttonsTop = PanelPlace.Height - ButtonInset - defaults->Height();
    defaults->MoveTo(ButtonInset, buttonsTop);
    defaults->ShowGuiWindow(true);
    screen.AdoptChildOf(panel, std::move(defaults));

    auto done = MCMakeGui<MCLogTextButton>();
    done->InitWithLabel(0, 0, "DONE", HideControls);
    done->MoveTo(PanelPlace.Width - ButtonInset - done->Width(), buttonsTop);
    done->ShowGuiWindow(true);
    screen.AdoptChildOf(panel, std::move(done));
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
    const std::shared_ptr<MCGuiObject> panel = Panel.lock();
    return panel != nullptr && panel->IsShowing();
}

MCKeyBindings::MCKeyTable& EditedKeys()
{
    return Edited;
}

std::shared_ptr<MCLogKeyList> ControlsList()
{
    return List.lock();
}
