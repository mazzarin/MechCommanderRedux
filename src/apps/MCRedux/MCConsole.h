#pragma once

#include <optional>
#include <string>
#include <string_view>

/// <summary>
/// MCRedux is a Windows-subsystem program, so it opens no console window of its own. When it is started from a
/// terminal, it borrows that terminal for stdout and stderr; otherwise their output goes nowhere, and messages the
/// player must see go to a message box instead.
/// </summary>
namespace MCConsole
{
    /// <summary>Attaches stdout and stderr to the parent process's console, if it has one. Call once, early in <c>main</c>.</summary>
    void AttachParent();

    /// <summary>Whether stderr reaches a console or file (false when started without a terminal).</summary>
    bool HasOutput();

    /// <summary>
    /// Shows <paramref name="prompt"/> at the terminal and reads the line typed there, not shown as it's typed when
    /// <paramref name="hidden"/> (a password).
    /// </summary>
    /// <returns>The line, without its end; none without a terminal, or once its input has ended.</returns>
    std::optional<std::string> ReadLine(std::string_view prompt, bool hidden);
}
