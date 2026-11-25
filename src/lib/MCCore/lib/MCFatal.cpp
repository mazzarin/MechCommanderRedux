#include "stdafx.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"

bool MCNoMessageBoxes = false;
std::function<void(std::string_view report)> MCFatalHook;

namespace
{
    /// <summary>"code : message" as the crash reporter showed it.</summary>
    std::string ErrorText(uint32_t errCode, std::string_view message, std::string_view message2)
    {
        return std::format("{:08X} : {}{}", errCode, message, message2);
    }

    /// <summary>What the original's error reports were titled.</summary>
    constexpr std::string_view FatalTitle = "MechCommander";

    /// <summary>
    /// Logs <paramref name="report"/>, tells the tests' hook (MCFatalHook), shows it titled <paramref name="title"/>
    /// (unless the tests run), and ends the process.
    /// </summary>
    [[noreturn]] void ShowFatal(const std::string& report, std::string_view title = FatalTitle)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", report.c_str());

        if (MCFatalHook)
        {
            MCFatalHook(report);
        }

        if (!MCNoMessageBoxes)
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, std::string(title).c_str(), report.c_str(), nullptr);
        }

        std::exit(1);
    }
}

void Fatal(int32_t errCode, std::string_view message, std::string_view message2)
{
    const uint32_t code = static_cast<uint32_t>(errCode);
    ShowFatal(message.empty() ? std::format("Fatal Error : {:08X} ", code) : ErrorText(code, message, message2));
}

void FatalMsg(std::string_view message)
{
    // The original's report had the mission's application name between the two lines; the port has none.
    ShowFatal(
        std::format("Fatal Error (ID: {})\n\nScenarioTime: {:06.2f}   ScenarioTurn: {}", message, ScenarioTime, Turn));
}

void GeneralMsg(std::string_view message)
{
    FatalMsg(message);
}

std::string MCWithRepair(std::string_view text)
{
    return std::format("{}\n\n{}", text, MCRepairText);
}

void MCFatalRepair(std::string_view text)
{
    ShowFatal(MCWithRepair(text), MCReportTitle);
}

std::function<void(std::string_view report, const std::source_location& where)> MCAssertHook;

void Assert(bool expression, uint32_t errCode, std::string_view message, std::string_view message2,
            const std::source_location& where)
{
    if (expression)
    {
        return;
    }

    // The original asked the crash reporter (AssertTest) whether to break into the debugger; the port logs and
    // carries on.
    const std::string report = ErrorText(errCode, message, message2);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Assert failed: %s", report.c_str());

    if (MCAssertHook)
    {
        MCAssertHook(report, where);
    }
}
