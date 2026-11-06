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
