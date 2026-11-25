#pragma once

// Original source: mcx\lib\aerror.cpp. The game's error reports.

/// <summary>
/// Stops the game with an error: <paramref name="errCode"/> and the message(s), shown to the player. The original
/// raised its crash reporter (AssertTest and a breakpoint); the port logs, shows a message box and exits.
/// </summary>
[[noreturn]] void Fatal(int32_t errCode, std::string_view message = {}, std::string_view message2 = {});

/// <summary>A fatal error during a mission: the message with the scenario clock.</summary>
[[noreturn]] void FatalMsg(std::string_view message);

/// <summary>A general error message; in MCX.EXE it is the same as <see cref="FatalMsg"/>.</summary>
[[noreturn]] void GeneralMsg(std::string_view message);

/// <summary>Port-only: what the port's own reports to the player are titled: the game's name.</summary>
inline constexpr std::string_view MCReportTitle = "MechCommander Redux HD";

/// <summary>
/// Port-only: how the player repairs a copy missing what the program brings: the setup run again on Windows; the Mac's
/// app, which comes whole, downloaded again.
/// </summary>
#ifdef _WIN32
inline constexpr std::string_view MCRepairText = "Run the Redux HD setup again to repair the install.";
#else
inline constexpr std::string_view MCRepairText = "Download Redux HD again to repair it.";
#endif

/// <summary>Port-only: <paramref name="text"/>, what's wrong with the copy, then how to repair it.</summary>
std::string MCWithRepair(std::string_view text);

/// <summary>
/// Port-only: stops the game as <see cref="Fatal"/> does, telling the player <paramref name="text"/> (what's wrong with
/// the copy) and how to repair it (<see cref="MCWithRepair"/>), as the game's start says it, with no code: a copy found
/// damaged once the game is going.
/// </summary>
[[noreturn]] void MCFatalRepair(std::string_view text);

/// <summary>
/// When <paramref name="expression"/> is false: reports "<paramref name="errCode"/> : message" (the original's crash
/// reporter could then break into the debugger), and tells <see cref="MCAssertHook"/> when one is set; the game goes on.
/// </summary>
/// <param name="where">The call site, for <see cref="MCAssertHook"/>; callers leave it out.</param>
void Assert(bool expression, uint32_t errCode, std::string_view message = {}, std::string_view message2 = {},
            const std::source_location& where = std::source_location::current());

/// <summary>
/// Port-only: when set (by mc_tests), called with the report and the call site of every <see cref="Assert"/> that fails,
/// after it is logged. It must return: an assert can fail in a destructor or halfway through a frame.
/// </summary>
extern std::function<void(std::string_view report, const std::source_location& where)> MCAssertHook;

/// <summary>
/// Port-only: when set (by mc_tests), <see cref="Fatal"/> only logs, with no message box to block an unattended run.
/// </summary>
extern bool MCNoMessageBoxes;

/// <summary>
/// Port-only: when set (by a test that stops the game on purpose), called with the report of every error that stops the
/// game (<see cref="Fatal"/>, <see cref="FatalMsg"/>, <see cref="MCFatalRepair"/>) once it is logged, before the
/// message box and the process's end: it throws to end the test there instead (returning, the game stops as ever).
/// </summary>
extern std::function<void(std::string_view report)> MCFatalHook;
