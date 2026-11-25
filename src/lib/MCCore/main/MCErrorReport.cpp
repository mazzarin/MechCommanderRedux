#include "stdafx.h"
#include "main/MCErrorReport.h"
#include "lib/MCFatal.h"
#include "main/MCGameSession.h"
#include "main/MCLogistics.h"

namespace
{
    /// <summary>Set while a report is open: a second error while reporting one can only quit.</summary>
    bool InAssertTest = false;
}

bool AssertTest(int32_t errorCode, std::string_view text)
{
    if (InAssertTest)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal Error", "Unable to continue execution.", nullptr);
        std::exit(1);
    }

    InAssertTest = true;

    const std::string message(text);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AssertTest %02X: %s", static_cast<uint32_t>(errorCode),
                 message.c_str());

    // An unattended run (mc_tests) can't answer the box: the error fails the run.
    if (MCNoMessageBoxes)
    {
        std::exit(1);
    }

    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Continue"},
        {0, 1, "Debug"},
        {0, 2, "Exit"},
    };

    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_ERROR,   nullptr, "ERROR", message.c_str(),
                                    SDL_arraysize(buttons), buttons, nullptr};
    int choice = 0;

    if (!SDL_ShowMessageBox(&box, &choice))
    {
        choice = 0;
    }

    InAssertTest = false;

    if (choice == 2)
    {
        KillTheGame(1);
    }

    return choice == 1;
}
