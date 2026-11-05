#pragma once

#include <span>
#include <string>

// Port-only (the personal build): the game started again once it has quit, for an update with no program to install
// (main/MCUpdater.h's RESTART; Sparkle restarts the game itself after a program update): the same program, with the
// same command line.

/// <summary>Starts this program again with <paramref name="arguments"/> (the program's name first, as main gets them).</summary>
/// <returns>Whether it started (why not is logged).</returns>
bool MCStartAgain(std::span<const std::string> arguments);
