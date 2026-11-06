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
/// finish; then the content the start must get (a first start, a release older than the program plays, the one its
/// program was installed alone for), downloaded, checked and put in place in the first start's window
/// (MCFirstStart.h). Main.cpp's start uses it, and so does <c>--get-content</c>, the Windows setup's last step.
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
    /// The content a start must get before anything opens (<paramref name="need"/>, MCInstalled::AtStart's), in a build
    /// that updates its content: downloaded, checked and put in place, the window showing the progress. Content needed
    /// that doesn't come is asked about in the window (TRY AGAIN, QUIT); so is content only due when
    /// <paramref name="askAlways"/> (--get-content), which otherwise leaves the files to play as they are.
    /// <paramref name="record"/> is then the installed record as it is.
    /// </summary>
    MCOutcome GetAtStart(const MCStart& start, std::string_view commandLine, MCOptionalRef<MCUpdateLock> lock,
                         MCInstalled::MCStartNeed need, bool askAlways, MCInstalled::MCRecord& record);

    /// <summary>
    /// <c>--get-content</c>: what a first start does for the content, then no game. Any staged update goes in first;
    /// then the content the copy must get, in the window, under <paramref name="lock"/> (the game's, held shared).
    /// </summary>
    /// <returns>
    /// The exit code: 0 once the content is in place, or when none is needed (no window opens); 1 when the player quit,
    /// or it failed.
    /// </returns>
    int GetContentOnly(const MCStart& start, MCOptionalRef<MCUpdateLock> lock = std::nullopt);
}
