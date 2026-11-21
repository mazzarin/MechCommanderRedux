#pragma once

#include "MCFirstStart.h"
#include "lib/MCOptionalRef.h"
#include "update/MCContentUpdater.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateWork.h"

class MCUpdateLock;

/// <summary>
/// Port-only: what a start does with the game's content before anything opens (SPEC-install-and-updates.md's Applying
/// an update and The content list's first start): a staged update put in place, or the recovery screen when it can't
/// finish; then the content the game can't play without (a first start, a release older than the program plays),
/// downloaded, checked and put in place in the first start's window (MCFirstStart.h). The art a program installed alone
/// came for doesn't hold the start up: the game's updater downloads it by itself once the game is open (Forced art).
/// Main.cpp's start uses it, and so does <c>--get-content</c>, the Windows setup's last step, which gets that art too.
/// </summary>
namespace MCContentAtStart
{
    /// <summary>The copy starting, and how it gets its content.</summary>
    struct MCStart
    {
        /// <summary>The game folder, and the program's build.</summary>
        std::filesystem::path GameFolder;
        uint64_t RunningBuild = 0;
        /// <summary>The copy and build for the game's own updater; none in a build that updates no content.</summary>
        std::optional<MCContentUpdater::MCSetup> Content;
        /// <summary>Makes the transfers to the release's host.</summary>
        MCUpdateWork::MCSourceMaker Source;
        /// <summary>The lowest content release the program plays (0: any).</summary>
        uint64_t LowestContentRelease = 0;
        /// <summary>How the first start's windows open (the tests': hidden, their answer given).</summary>
        MCFirstStart::MCWindowOptions Window;
    };

    /// <summary>How the content's step at the start ended.</summary>
    enum class MCOutcome : uint8_t
    {
        /// <summary>The files can be played.</summary>
        Play,
        /// <summary>The player closed the window, or chose QUIT: the next start carries on.</summary>
        Quit,
        /// <summary>The content needed couldn't go in place.</summary>
        Failed,
    };

    /// <summary>The window's heading while the content downloads.</summary>
    constexpr std::string_view GettingHeading = "GETTING THE GAME READY";

    /// <summary>
    /// Puts a staged content update in place, or finishes one a crash stopped (update/MCContentSwap.h), before any of
    /// the game's files opens. A lobby's start (<paramref name="commandLine"/>) aims to keep it within
    /// MCContentSwap::LobbyStartTime: past half of it the files stop moving, the previous ones play, and the next start
    /// tries again. Content needing a newer program waits for it. Files it can't play go to the recovery screen (REPAIR
    /// in a build that updates its content).
    /// </summary>
    /// <returns>False when the game can't play the files as they are (the player quit the recovery screen).</returns>
    bool SwapStaged(const MCStart& start, std::string_view commandLine,
                    MCOptionalRef<MCUpdateLock> lock = std::nullopt);

    /// <summary>
    /// A start's content step once any staged update is in place (<see cref="SwapStaged"/>), in a build that updates
    /// its content: the release the game can't play without (MCInstalled::MCStartNeed::Needed), downloaded, checked and
    /// put in place, the window showing the progress, and asked about there (TRY AGAIN, QUIT) when it doesn't come.
    /// The art a program installed alone came for (MCStartNeed::Due) isn't waited for: the game opens on the files it
    /// has. <paramref name="record"/> is then the installed record as it is.
    /// </summary>
    MCOutcome BeforeOpening(const MCStart& start, std::string_view commandLine, MCOptionalRef<MCUpdateLock> lock,
                            MCInstalled::MCRecord& record);

    /// <summary>
    /// <c>--get-content</c>: what a first start does for the content, then no game. Any staged update goes in first;
    /// then the content the copy needs (the art a program installed alone came for too), in the window, asked about
    /// there when it doesn't come, under <paramref name="lock"/> (the game's, held shared).
    /// </summary>
    /// <returns>
    /// The exit code: 0 once the content is in place, or when none is needed (no window opens); 1 when the player quit,
    /// or it failed.
    /// </returns>
    int GetContentOnly(const MCStart& start, MCOptionalRef<MCUpdateLock> lock = std::nullopt);
}
