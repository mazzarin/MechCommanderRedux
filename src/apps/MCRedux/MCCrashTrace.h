#pragma once

#include <filesystem>
#include <functional>

/// <summary>
/// A development aid: on an unhandled crash, prints the exception and a symbolized call stack to stderr, runs the
/// reporter (game state worth knowing), and writes a minidump next to the executable (MCRedux.dmp or mc_tests.dmp, open
/// it in Visual Studio with the PDB beside it) before the process dies. The CRT's own failures (a Debug assertion such
/// as a checked iterator's, abort(), an invalid parameter, a pure virtual call) are reported the same way, with no
/// dialog. Without a terminal, the report goes to MCRedux.crash.txt
/// beside the executable instead, and a message box says where. Windows only (DbgHelp and the build's PDB); elsewhere
/// <see cref="Install"/> does nothing.
/// </summary>
namespace MCCrashTrace
{
    /// <summary>Installs the handler. Call once, early in <c>main</c>.</summary>
    void Install();

    /// <summary>Sets a function the handler calls after the stack, to print game state to stderr.</summary>
    void SetReporter(std::function<void()> reporter);

    /// <summary>
    /// Port (personal): a crash's stack also goes to <paramref name="path"/> (none when empty) as the reports' record
    /// (MCTelemetryWin.cpp): the exception's line, then a frame a line as its module's name and offset, with no paths.
    /// </summary>
    void SetRecordPath(const std::filesystem::path& path);
}
