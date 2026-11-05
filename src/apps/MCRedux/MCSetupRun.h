#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Port-only (the personal build): what Windows does for the program's updates (update/MCSetupBackend.h): whether it
// will run a downloaded setup, and starting one to install as the game ends (MCSetupRunWin.cpp). Standard headers
// only: the Windows side is built without the precompiled header.

/// <summary>Whether Windows will run <paramref name="setup"/> (Smart App Control, a policy): empty, or why not.</summary>
std::string MCSetupRunnable(const std::filesystem::path& setup);

/// <summary>
/// Starts <paramref name="setup"/> with <paramref name="arguments"/>; it waits for this process to end, then installs,
/// and with <paramref name="restart"/> starts the game again (src/installer/windows: the variables it reads).
/// </summary>
/// <returns>Empty, or why it didn't start.</returns>
std::string MCStartSetup(const std::filesystem::path& setup, std::string_view arguments, bool restart);
