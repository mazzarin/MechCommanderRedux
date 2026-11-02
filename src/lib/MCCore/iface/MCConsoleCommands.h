#pragma once

/// <summary>
/// Port-only: runs a debug console line (<see cref="MCGuiConsole"/>): its first word names the command, the rest are
/// its arguments, case blind. The commands:
/// <list type="bullet">
/// <item><c>destroyarm right|left</c>: blows that arm off the selected mechs, as a destroyed arm does.</item>
/// <item><c>art original|hd|day|dusk|night</c>: shows the original's art, HD art, HD art lit at the mission's own time
/// of day (by day in the original's missions) or HD art at dusk or at night from the next frame on (MCArtModes.h), as
/// HD ART and HD LIGHTING set so would, without saving them.</item>
/// </list>
/// </summary>
/// <returns>Nothing, or why the line did nothing (an unknown command, a wrong argument, nothing to act on).</returns>
std::expected<void, std::string> RunConsoleCommand(std::string_view line);
