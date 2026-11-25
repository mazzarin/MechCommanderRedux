#pragma once

#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCPrefsScreen.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlScreen.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

/// <summary>
/// Port-only (SPEC-menus.md): what the game tests share of the remade screens: the screen on top of a layer of the
/// game's menus' RmlUi, its elements and their words, a press of one, a key on one, the dialog's words and its answer,
/// Preferences opened as the main menu opens it, and the campaign's screen on a tab.
/// </summary>
namespace MCRemadeTest
{
    /// <summary>A test frame's length: the game's pace.</summary>
    inline constexpr float FrameSeconds = 1.0f / 15.0f;

    /// <summary>The menus' layer, or the mission's (over the battlefield).</summary>
    enum class MCLayer : uint8_t
    {
        Menus,
        Mission,
    };

    /// <summary>The screen on top of <paramref name="layer"/> as a <typeparamref name="T"/>; null when another shows, or none.</summary>
    template <typename T> std::shared_ptr<T> OnTop(MCLayer layer = MCLayer::Menus)
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();

        if (!menus.has_value())
        {
            return nullptr;
        }

        const MCRmlLayer& shown = layer == MCLayer::Menus ? menus->get().Menus() : menus->get().Mission();
        const MCOptionalRef<MCRmlScreen> top = shown.Top();
        return top.has_value() ? std::dynamic_pointer_cast<T>(top->get().shared_from_this()) : nullptr;
    }

    /// <summary>The element <paramref name="id"/> of <paramref name="screen"/>'s document; null without one.</summary>
    inline Rml::Element* Find(const MCRmlScreen& screen, const std::string& id)
    {
        const MCOptionalRef<Rml::ElementDocument> document = screen.Document();
        return document.has_value() ? document->get().GetElementById(id) : nullptr;
    }

    /// <summary>The words element <paramref name="id"/> shows (its markup decoded); empty without one.</summary>
    inline std::string Words(const MCRmlScreen& screen, const std::string& id)
    {
        const Rml::Element* element = Find(screen, id);
        return element != nullptr ? Rml::StringUtilities::DecodeRml(element->GetInnerRML()) : std::string();
    }

    /// <summary>Whether element <paramref name="id"/> shows (it and every element round it).</summary>
    inline bool Visible(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* element = Find(screen, id);
        return element != nullptr && element->IsVisible(true);
    }

    /// <summary>Element <paramref name="id"/> pressed as a click on it does; false when it isn't shown.</summary>
    inline bool Press(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* element = Find(screen, id);

        if (element == nullptr || !element->IsVisible(true))
        {
            return false;
        }

        element->Click();
        return true;
    }

    /// <summary>The words of switch <paramref name="id"/>'s chosen choice (the look's switch); empty without one.</summary>
    inline std::string ChosenWords(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* control = Find(screen, id);

        if (control == nullptr)
        {
            return {};
        }

        const int choice = MCRmlKit::Choice(*control);
        const Rml::Element* chosen =
            choice >= 0 && choice < control->GetNumChildren() ? control->GetChild(choice) : nullptr;
        return chosen != nullptr ? Rml::StringUtilities::DecodeRml(chosen->GetInnerRML()) : std::string();
    }

    /// <summary>
    /// Choice <paramref name="choice"/> of switch <paramref name="id"/> clicked as a player picks it, then a frame; false
    /// when the switch or that choice isn't there.
    /// </summary>
    inline bool ClickChoice(const MCRmlScreen& screen, const std::string& id, int choice)
    {
        Rml::Element* control = Find(screen, id);

        if (control == nullptr || !control->IsVisible(true) || choice < 0 || choice >= control->GetNumChildren())
        {
            return false;
        }

        control->GetChild(choice)->Click();
        MCTestGame::RunFrame(FrameSeconds);
        return true;
    }

    /// <summary>The words stepper <paramref name="id"/> shows between its arrows (the look's stepper); empty without one.</summary>
    inline std::string StepWords(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* stepper = Find(screen, id);

        if (stepper == nullptr)
        {
            return {};
        }

        Rml::ElementList parts;
        stepper->GetElementsByClassName(parts, "step-value");
        return parts.size() == 1 ? Rml::StringUtilities::DecodeRml(parts.front()->GetInnerRML()) : std::string();
    }

    /// <summary>
    /// Key <paramref name="key"/> pressed <paramref name="times"/> times with the focus on element <paramref name="id"/>
    /// (a switch or stepper takes Left and Right), a frame after each; false when it isn't shown.
    /// </summary>
    inline bool KeyOn(const MCRmlScreen& screen, const std::string& id, SDL_Scancode key, int32_t times = 1)
    {
        Rml::Element* element = Find(screen, id);

        if (element == nullptr || !element->IsVisible(true))
        {
            return false;
        }

        for (int32_t press = 0; press < times; press++)
        {
            element->Focus(true);
            MCScreenInput::RealKey(key);
            MCTestGame::RunFrame(FrameSeconds);
        }

        return true;
    }

    /// <summary>
    /// The remade dialog, which must show, answered with its main button (OK) or, <paramref name="main"/> false, its other
    /// (Cancel), and its fade out run.
    /// </summary>
    inline void AnswerDialog(bool main = true)
    {
        REQUIRE(MCRmlDialogs::Shown());
        main ? MCRmlDialogs::Dialog()->PressMain() : MCRmlDialogs::Dialog()->PressOther();
        // Two seconds at most: its fade out.
        constexpr int32_t MostFrames = 30;

        for (int32_t frame = 0; frame < MostFrames && MCRmlDialogs::Shown(); frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        CHECK(!MCRmlDialogs::Shown());
    }

    /// <summary>
    /// The remade dialog, which must show saying <paramref name="text"/> (an old message, in sentence case), closed with its
    /// main button (OK).
    /// </summary>
    inline void CloseDialog(std::string_view text)
    {
        REQUIRE(MCRmlDialogs::Shown());
        CHECK_EQ(MCRmlDialogs::Dialog()->Said().Text, MCRmlDialogs::AsSentence(text));
        AnswerDialog();
    }

    /// <summary>
    /// Preferences opened from the main menu as its PREFERENCES button opens it (through the screens' flow), on
    /// <paramref name="section"/>; null when it doesn't show.
    /// </summary>
    inline std::shared_ptr<MCPrefsScreen> OpenPreferences(MCSettingsModel::MCSection section)
    {
        if (!MCScreenFlow::Open(MCMenuScreens::Preferences()) || !MCTestGame::FinishChanges())
        {
            return nullptr;
        }

        std::shared_ptr<MCPrefsScreen> prefs = OnTop<MCPrefsScreen>();

        if (prefs != nullptr)
        {
            prefs->ShowSection(section);
            MCTestGame::RunFrame(FrameSeconds);
        }

        return prefs;
    }

    /// <summary>
    /// Preferences' <paramref name="button"/> (ACCEPT, CANCEL) pressed and the change back to the screen under it run;
    /// false when the button isn't shown or the change never ends.
    /// </summary>
    inline bool LeavePreferences(const MCPrefsScreen& prefs, const std::string& button)
    {
        return Press(prefs, button) && MCTestGame::FinishChanges();
    }

    /// <summary>
    /// The campaign's remade screen, which must show on the flow's top once the change of screen running ends, on
    /// <paramref name="tab"/>.
    /// </summary>
    inline MCCampaignScreen& CampaignScreen(MCCampaignScreen::MCTab tab)
    {
        REQUIRE(MCTestGame::FinishChanges());
        REQUIRE(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
        REQUIRE(MCCampaignMenu::Screen().has_value());
        REQUIRE(MCCampaignMenu::Shown());
        MCCampaignScreen& screen = MCCampaignMenu::Screen()->get();
        screen.ChooseTab(tab);
        MCTestGame::RunFrame(FrameSeconds);
        return screen;
    }
}
