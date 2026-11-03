#include "stdafx.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCLogistics.h"
#include "main/MCTelemetry.h"

namespace
{
    using MCTelemetry::MCPrompt;

    constexpr std::string_view NoticeText =
        "MechCommander Redux sends anonymous reports of your computer, how smoothly missions and matches run, and "
        "crashes, to help it run well on more machines. No name or ID; your IP address isn't kept. REPORTS in HD "
        "OPTIONS turns them off.";
    constexpr std::string_view QuestionText =
        "Send anonymous reports of your computer, how smoothly missions and matches run, and crashes, to help "
        "MechCommander Redux run well on more machines? No name or ID; your IP address isn't kept. REPORTS in HD "
        "OPTIONS changes this later.";

    /// <summary>The main menu's logistics state.</summary>
    constexpr int32_t MainMenuState = 1;

    /// <summary>The player's answer, kept in PREFS at once.</summary>
    void Answer(bool send)
    {
        MCTelemetry::Answered(send);
        SavePrefs();
    }
}

void ShowReportsNotice()
{
    const MCPrompt prompt = MCTelemetry::Prompt();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (prompt == MCPrompt::None || logistics == nullptr || logistics->LogisticsState != MainMenuState ||
        logistics->CurrentScreen() != logistics->MainScreen || logistics->MessageDialog->IsShowing() ||
        logistics->QuestionDialog->IsShowing() || UpdatePageShown())
    {
        return;
    }

    if (prompt == MCPrompt::Notice)
    {
        // However it closes (OK, Enter, Escape), the player has seen it.
        ShowMenuMessage(NoticeText, [](int32_t) { Answer(true); });
    }
    else
    {
        AskMenuQuestion(
            *logistics->QuestionDialog, QuestionText, [] { Answer(true); }, [] { Answer(false); }, "bg_cancl.tga",
            true);
    }
}

std::string_view ReportsNoticeText()
{
    return NoticeText;
}

std::string_view ReportsQuestionText()
{
    return QuestionText;
}
