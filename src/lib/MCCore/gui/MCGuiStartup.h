#pragma once

#include <span>
#include <string>
#include <string_view>

// The program's start (gui\asystem.cpp): the data paths, the GUI system's life, and the command line.

/// <summary>
/// The program's entry (RealWinMain, which the original's WinMain ran with CD autorun off and its crash reporter
/// watching): seeds the dice, sets the data paths, makes the GUI system (an <see cref="MCGameContext"/> system),
/// starts and runs it, then stops and deletes it.
/// </summary>
/// <returns>0, or -4 when the game didn't start.</returns>
/// <remarks>
/// Port-only: before the GUI stops, an internet game ends, waiting up to <paramref name="internetLeaveWait"/> for its
/// leaving to reach the service, and the internet screens let go of their requests (the program's quit; the tests'
/// none).
/// </remarks>
int RunGame(std::string_view commandLine, std::chrono::milliseconds internetLeaveWait = {});

/// <summary>
/// Reads the command line's switches: <c>-mission n</c> (or <c>+ n</c>), <c>-network file</c>, <c>-load file</c>, and
/// the port's <c>-renderer</c>, <c>-gpudraw</c>, <c>-gpudump</c>, <c>-framelog</c>, <c>-fps</c> and <c>-novsync</c>.
/// </summary>
void ParseCommandLine(std::string_view commandLine);

/// <summary>Whether the command line starts the game in a multiplayer lobby's room (<c>-host</c> or <c>-join</c>).</summary>
bool StartsInLobby(std::string_view commandLine);

/// <summary>
/// Port-only (the personal build): the lobby's own words in <paramref name="commandLine"/>, each with the words it takes
/// as ParseCommandLine reads them (<c>-host</c>, <c>-join</c>, <c>-port</c>, <c>-player</c>, <c>-session</c>,
/// <c>-maxplayers</c>, <c>-rejoin</c>, the match's options, <c>-matchmap</c>, <c>-slot</c> and the teams'), for the
/// rejoin note (network/MCRejoinNote.h): any other word is left out, and logged.
/// </summary>
std::string LobbyWords(std::string_view commandLine);

/// <summary>
/// Port (Redux HD): <paramref name="words"/> as a command line ParseCommandLine reads back the same (a word with a space,
/// or none, in double quotes).
/// </summary>
std::string LobbyCommandWords(std::span<const std::string> words);

/// <summary>
/// Port (Redux HD): what a lobby's words in <paramref name="commandLine"/> start, for the log: hosting or joining, and
/// the room's address or code ("joining K7QF", "hosting K7QF again"); never a place's token or anything else.
/// </summary>
std::string LobbyStartLogged(std::string_view commandLine);
