#include "stdafx.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCPrefs.h"
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

    /// <summary>The remade dialog's document and its places (ui/dialog_reports.rml).</summary>
    constexpr const char* ReportsDocument = "dialog_reports.rml";
    constexpr const char* HeadingId = "heading";
    constexpr const char* AskId = "ask";
    constexpr const char* KeptId = "kept";
    constexpr const char* SendId = "yes";
    constexpr const char* DontSendId = "no";
    constexpr const char* Display = "display";
    constexpr const char* NoDisplay = "none";
    constexpr const char* EscapeKey = "Esc";

    /// <summary>What the remade dialog says: its title, its two sentences, its answer's word.</summary>
    struct MCReportsWords
    {
        std::string_view Heading;
        std::string_view Ask;
        std::string_view Kept;
        std::string_view Answer;
    };

    /// <summary>The question (the style frames' dialog_reports.rml: today's question, its two sentences).</summary>
    constexpr MCReportsWords QuestionWords{
        .Heading = "Send reports?",
        .Ask = "Send reports of your computer, frame rates, settings and crashes, under a random ID, to help "
               "MechCommander Redux run better?",
        .Kept = "No name or IP address is kept.",
        .Answer = "Send",
    };

    /// <summary>The notices (today's, in sentence case), with OK alone.</summary>
    constexpr MCReportsWords NoticeWords{
        .Heading = "Reports",
        .Ask =
            "MechCommander Redux sends anonymous reports of your computer, frame rates, settings and crashes to help "
            "it run better.",
        .Kept = "Turn this off under Reports in Preferences.",
        .Answer = "OK",
    };

    constexpr MCReportsWords NoticeWithIdWords{
        .Heading = "Reports",
        .Ask =
            "MechCommander Redux sends reports of your computer, frame rates, settings and crashes, under a random ID, "
            "to help it run better.",
        .Kept = "No name or IP address is kept. Turn this off under Reports in Preferences.",
        .Answer = "OK",
    };

    /// <summary>
    /// Port-only (SPEC-menus.md, "The reports question"; stage 6): the question and the notices remade, a dialog over
    /// the main menu (or the room, for a launcher's players). The question: SEND turns REPORTS on, DON'T SEND (and
    /// Escape) leaves it off. A notice: OK alone, and however it closes (OK, Enter, Escape) the player has seen it.
    /// Preferences' REPORTS changes it later.
    /// </summary>
    class MCReportsDialog final : public MCRmlScreen
    {
    public:
        explicit MCReportsDialog(MCPrompt prompt) : _Asks(prompt == MCPrompt::Ask)
        {
            SetDocument(ReportsDocument);
            Modal = true;
            Keys = {{EscapeKey, _Asks ? "Don't send" : "OK"}};
            _Words = !_Asks ? (prompt == MCPrompt::NoticeWithId ? NoticeWithIdWords : NoticeWords) : QuestionWords;
        }

        void Answered(bool send)
        {
            // Closed first: the answer's save can't reach a dialog already gone.
            const std::shared_ptr<MCRmlScreen> pinned = shared_from_this();
            Close();
            Answer(send);
        }

        /// <summary>Escape: the question's DON'T SEND; a notice seen.</summary>
        void Dismissed() { Answered(!_Asks); }

    protected:
        void OnLoaded(Rml::ElementDocument& document) override
        {
            for (const auto& [id, words] : {std::pair{HeadingId, _Words.Heading}, std::pair{AskId, _Words.Ask},
                                            std::pair{KeptId, _Words.Kept}, std::pair{SendId, _Words.Answer}})
            {
                if (Rml::Element* element = document.GetElementById(id); element != nullptr)
                {
                    element->SetInnerRML(Rml::StringUtilities::EncodeRml(std::string(words)));
                }
            }

            // A notice has nothing to refuse: OK alone.
            if (Rml::Element* dontSend = document.GetElementById(DontSendId); dontSend != nullptr && !_Asks)
            {
                dontSend->SetProperty(Display, NoDisplay);
            }

            OnClick(SendId, [this] { Answered(true); });
            OnClick(DontSendId, [this] { Answered(false); });
        }

    private:
        /// <summary>Whether it asks (the question) or tells (a notice).</summary>
        bool _Asks;
        MCReportsWords _Words;
    };

    /// <summary>Whether a remade screen shows on the menus' layer (Preferences over the main menu, the question itself).</summary>
    bool RemadeShows()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        return menus.has_value() && menus->get().Menus().Showing();
    }

    /// <summary>
    /// <paramref name="prompt"/> shown as the remade dialog; false when the menus' RmlUi can't start or its document
    /// can't be read.
    /// </summary>
    bool ShowRemade(MCPrompt prompt)
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return false;
        }

        const MCGuiHandle<MCReportsDialog> dialog = menus->get().Menus().CreateChild<MCReportsDialog>(
            [](MCReportsDialog& made) { made.Back = [&made] { made.Dismissed(); }; }, prompt);

        if (const std::expected<void, std::string> opened = dialog->Open(); !opened)
        {
            SDL_Log("Reports: the %s can't be shown as %s (%s); shown as the original's dialog",
                    prompt == MCPrompt::Ask ? "question" : "notice", ReportsDocument, opened.error().c_str());
            dialog->Close();
            return false;
        }

        return true;
    }
}

bool NoticeScreenFree()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    // Port-only (stage 7): the remade main menu with nothing over it is free (logistics/MCMainMenuScreen.h); (stage 4)
    // so is the remade room, which a launcher's game never leaves. Only with nothing over it: a notice already shown
    // holds the next (each frame asks again).
    return logistics != nullptr && OnNoticeScreen(*logistics) && !logistics->Message->IsShowing() &&
           !logistics->Question->IsShowing() && !UpdatePageShown() &&
           (!RemadeShows() || MCMainMenuScreens::MainOnTop() || RoomScreenOnTop());
}

void ShowReportsNotice()
{
    const MCPrompt prompt = MCTelemetry::Prompt();

    if (prompt == MCPrompt::None || !NoticeScreenFree())
    {
        return;
    }

    if (ShowRemade(prompt))
    {
        return;
    }

    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (prompt == MCPrompt::Ask)
    {
        AskMenuQuestion(
            *logistics->Question, QuestionText, [] { Answer(true); }, [] { Answer(false); }, "bg_cancl.tga", true);
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
