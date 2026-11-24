#include "stdafx.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "ui/MCRmlFlow.h"
#include "main/MCLogistics.h"
#include "main/MCTelemetry.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlScreen.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    using MCTelemetry::MCPrompt;

    constexpr std::string_view NoticeText =
        "MechCommander Redux sends anonymous reports of your computer, frame rates, settings and crashes to help it "
        "run better. Turn this off under REPORTS in PREFERENCES.";
    constexpr std::string_view NoticeWithIdText =
        "MechCommander Redux sends reports of your computer, frame rates, settings and crashes, under a random ID, to "
        "help it run better.\nNo name or IP address is kept. Turn this off under REPORTS in PREFERENCES.";
    constexpr std::string_view QuestionText =
        "Send reports of your computer, frame rates, settings and crashes, under a random ID, to help MechCommander "
        "Redux run better?\nNo name or IP address is kept. Change this later under REPORTS in PREFERENCES.";

    /// <summary>The main menu's logistics state, and the session screen's.</summary>
    constexpr int32_t MainMenuState = 1;
    constexpr int32_t SessionState = 8;

    /// <summary>
    /// Whether <paramref name="logistics"/> shows the main menu, or the session screen: a game a lobby starts never
    /// shows the main menu, so its players see the notice there, before the match.
    /// </summary>
    bool OnNoticeScreen(MCLogistics& logistics)
    {
        return (logistics.LogisticsState == MainMenuState && logistics.CurrentScreen() == logistics.MainScreen) ||
               (logistics.LogisticsState == SessionState && logistics.CurrentScreen() == logistics.SessionScreen);
    }

    /// <summary>The player's answer, kept in PREFS at once.</summary>
    void Answer(bool send)
    {
        MCTelemetry::Answered(send);
        SavePrefs();
    }

    /// <summary>The remade question's document and its places (ui/dialog_reports.rml).</summary>
    constexpr const char* ReportsDocument = "dialog_reports.rml";
    constexpr const char* AskId = "ask";
    constexpr const char* KeptId = "kept";
    constexpr const char* SendId = "yes";
    constexpr const char* DontSendId = "no";

    /// <summary>The remade question's words (the style frames' dialog_reports.rml: today's question, its two sentences).</summary>
    constexpr std::string_view AskText = "Send reports of your computer, frame rates, settings and crashes, under a "
                                         "random ID, to help MechCommander Redux run better?";
    constexpr std::string_view KeptText = "No name or IP address is kept.";

    /// <summary>
    /// Port-only (SPEC-menus.md, "The reports question"; stage 6): the question remade, a dialog over the main menu (or
    /// the room, for a launcher's players): SEND turns REPORTS on, DON'T SEND (and Escape) leaves it off; Preferences'
    /// REPORTS changes it later.
    /// </summary>
    class MCReportsDialog final : public MCRmlScreen
    {
    public:
        MCReportsDialog()
        {
            SetDocument(ReportsDocument);
            Modal = true;
            // The focus on DON'T SEND: Enter alone never agrees to reports.
            Keys = {{"Esc", "Don't send"}};
        }

        void Answered(bool send)
        {
            // Closed first: the answer's save can't reach a dialog already gone.
            const std::shared_ptr<MCRmlScreen> pinned = shared_from_this();
            Close();
            Answer(send);
        }

    protected:
        void OnLoaded(Rml::ElementDocument& document) override
        {
            for (const auto& [id, words] : {std::pair{AskId, AskText}, std::pair{KeptId, KeptText}})
            {
                if (Rml::Element* element = document.GetElementById(id); element != nullptr)
                {
                    element->SetInnerRML(Rml::StringUtilities::EncodeRml(std::string(words)));
                }
            }

            OnClick(SendId, [this] { Answered(true); });
            OnClick(DontSendId, [this] { Answered(false); });
        }
    };

    /// <summary>Whether a remade screen shows on the menus' layer (Preferences over the main menu, the question itself).</summary>
    bool RemadeShows()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        return menus.has_value() && menus->get().Menus().Showing();
    }

    /// <summary>The question asked as the remade dialog; false when the menus' RmlUi can't start or its document can't be read.</summary>
    bool AskRemade()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return false;
        }

        const MCGuiHandle<MCReportsDialog> question = menus->get().Menus().CreateChild<MCReportsDialog>(
            [](MCReportsDialog& made) { made.Back = [&made] { made.Answered(false); }; });

        if (const std::expected<void, std::string> opened = question->Open(); !opened)
        {
            SDL_Log("Reports: the question can't be asked as %s (%s); asked as the original's dialog", ReportsDocument,
                    opened.error().c_str());
            question->Close();
            return false;
        }

        return true;
    }
}

bool NoticeScreenFree()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    // Port-only (stage 7): the remade main menu with nothing over it is free (logistics/MCMainMenuScreen.h).
    return logistics != nullptr && OnNoticeScreen(*logistics) && !logistics->MessageDialog->IsShowing() &&
           !logistics->QuestionDialog->IsShowing() && !UpdatePageShown() &&
           (!RemadeShows() || MCMainMenuScreens::MainOnTop());
}

void ShowReportsNotice()
{
    const MCPrompt prompt = MCTelemetry::Prompt();

    if (prompt == MCPrompt::None || !NoticeScreenFree())
    {
        return;
    }

    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (prompt == MCPrompt::Ask)
    {
        if (AskRemade())
        {
            return;
        }

        AskMenuQuestion(
            *logistics->QuestionDialog, QuestionText, [] { Answer(true); }, [] { Answer(false); }, "bg_cancl.tga",
            true);
    }
    else
    {
        // However it closes (OK, Enter, Escape), the player has seen it.
        ShowMenuMessage(ReportsText(prompt), [](int32_t) { Answer(true); });
    }
}

std::string_view ReportsText(MCTelemetry::MCPrompt prompt)
{
    switch (prompt)
    {
        case MCPrompt::Notice:
            return NoticeText;
        case MCPrompt::NoticeWithId:
            return NoticeWithIdText;
        case MCPrompt::Ask:
            return QuestionText;
        case MCPrompt::None:
            break;
    }

    return {};
}
