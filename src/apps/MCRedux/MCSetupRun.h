#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// Port-only (the personal build): what Windows does for the program's updates (update/MCSetupBackend.h,
// update/MCProgramSwap.h): whether it will run a downloaded program, starting a setup to install as the game ends,
// whether what the game starts outlives it, and the Apps list's version (MCSetupRunWin.cpp). Standard headers only: the
// Windows side is built without the precompiled header.

/// <summary>How what the game starts fares once the game exits: a lobby may run the game in a job that ends it all.</summary>
enum class MCOutlives : uint8_t
{
    /// <summary>It runs on: no job, or one that doesn't end its processes as it closes, or lets them leave unasked.</summary>
    Yes,
    /// <summary>It runs on when started out of the game's job (CREATE_BREAKAWAY_FROM_JOB), which the job allows.</summary>
    BrokenAway,
    /// <summary>
    /// It ends with the game: the job ends every process in it as it closes (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) and
    /// lets none leave (MechCommander Online's).
    /// </summary>
    No,
};

/// <summary>Whether what the game starts outlives it: Windows is asked at the first call, and the answer kept.</summary>
MCOutlives MCStartedOutlive();

/// <summary>The flags that start a process out of the game's job where it must leave it to outlive the game; else 0.</summary>
unsigned long MCOutlivingFlags();

/// <summary>
/// Sets the version Windows' Apps list shows for the install in <paramref name="gameFolder"/> to <paramref
/// name="version"/>: the setup's uninstall entry's, where it's the player's own (an install for every user keeps the
/// setup's until a setup runs again).
/// </summary>
/// <returns>Empty, or why not.</returns>
std::string MCShowInstalledVersion(const std::filesystem::path& gameFolder, std::string_view version);

/// <summary>
/// Whether the setup installed the copy in <paramref name="gameFolder"/>: the folder its uninstall entry names, the
/// player's own or one for every user (update/MCProgramSwap.h's MCSetupOwns).
/// </summary>
bool MCSetupOwns(const std::filesystem::path& gameFolder);

/// <summary>Whether Windows will run <paramref name="setup"/> (Smart App Control, a policy): empty, or why not.</summary>
std::string MCSetupRunnable(const std::filesystem::path& setup);

/// <summary>
/// Starts <paramref name="setup"/> with <paramref name="arguments"/>; it waits for this process to end, then installs,
/// and with <paramref name="restart"/> starts the game again (src/installer/windows: the variables it reads).
/// </summary>
/// <returns>Empty, or why it didn't start.</returns>
std::string MCStartSetup(const std::filesystem::path& setup, std::string_view arguments, bool restart);
