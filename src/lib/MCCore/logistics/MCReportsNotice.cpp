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
        "MechCommander Redux sends anonymous reports of your computer, frame rates, settings and crashes to help it "
        "run better. Turn this off under REPORTS in HD OPTIONS.";
    constexpr std::string_view NoticeWithIdText =
        "MechCommander Redux sends reports of your computer, frame rates, settings and crashes, under a random ID, to "
        "help it run better.\nNo name or IP address is kept. Turn this off under REPORTS in HD OPTIONS.";
    constexpr std::string_view QuestionText =
        "Send reports of your computer, frame rates, settings and crashes, under a random ID, to help MechCommander "
        "Redux run better?\nNo name or IP address is kept. Change this later under REPORTS in HD OPTIONS.";

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

    if (prompt == MCPrompt::Ask)
    {
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
