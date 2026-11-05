#pragma once

#include "main/MCUpdater.h"
#include "update/MCContentStage.h"

/// <summary>
/// Port-only: the updater's program half where the game installs the program itself, with its own installer run
/// silently (Windows: the Inno setup, src/installer/windows; main/MCUpdater.h's backend, as Sparkle is the Mac's). The
/// program release.json gives this copy is that platform's setup: its address, size, SHA-256 and the arguments that
/// run it silently (SPEC-install-and-updates.md's Releases). INSTALL downloads it into <c>Redux update/program/</c>
/// (resuming a cut-off download), and it counts only once its size and SHA-256 are release.json's: updates trust
/// release.json, never the system's signature. A setup the system won't run (Windows' Smart App Control) fails the
/// update, saying why, and the old program runs on. Otherwise it's armed, recorded in <c>Redux update/program/</c>,
/// and the content with it (MCUpdater); RESTART or quitting runs it as the game ends, RESTART asking it to start the
/// game again. A start that finds a setup armed and never run (the game crashed or was ended first) runs it then.
/// </summary>
namespace MCSetupBackend
{
    /// <summary>Where the setup goes, and the record of the one armed, in the game folder.</summary>
    constexpr std::string_view ProgramFolder = "Redux update/program";
    constexpr std::string_view ArmedFile = "Redux update/program/armed.json";

    /// <summary>What the platform does for the backend.</summary>
    struct MCPlatform
    {
        /// <summary>Whether the system will run <c>setup</c>: empty, or why not, as the UPDATE page says it.</summary>
        std::function<std::string(const std::filesystem::path& setup)> Runnable;
        /// <summary>
        /// Starts <c>setup</c> with <c>arguments</c>, to install once this process has ended, and with
        /// <c>restart</c> to start the game again after: empty, or why it can't.
        /// </summary>
        std::function<std::string(const std::filesystem::path& setup, std::string_view arguments, bool restart)> Start;
        /// <summary>Quits the game for RESTART (none: the input's quit, as the player closing the game).</summary>
        std::function<void()> Quit;
    };

    /// <summary>A transfer to the folder at an address (https), where a program is hosted, or why there's none.</summary>
    using MCSourceFor =
        std::function<std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string>(std::string_view folder)>;

    /// <summary>
    /// The backend for the copy in <paramref name="gameFolder"/>, running version <paramref name="runningVersion"/>
    /// (as shown), fetching programs through <paramref name="sources"/>.
    /// </summary>
    std::unique_ptr<MCUpdater::MCBackend> Make(std::filesystem::path gameFolder, std::string runningVersion,
                                               MCSourceFor sources, MCPlatform platform);

    /// <summary>
    /// At a start: a setup armed in an earlier run and never run, for a build newer than <paramref name="runningBuild"/>,
    /// is started (asked to start the game again), its hash checked again first. One for this build or older has done
    /// its work: its folder goes.
    /// </summary>
    /// <returns>Whether it started one: the game then quits so it can install.</returns>
    bool RunLeftOver(const std::filesystem::path& gameFolder, uint64_t runningBuild, const MCPlatform& platform);
}
