#pragma once

#include "update/MCContentList.h"
#include "update/MCContentStage.h"
#include "update/MCReleaseFile.h"

class MCUpdateDisk;

/// <summary>
/// Port-only: the Windows program's update put in place by the game itself while it runs, the next start running it
/// (SPEC-install-and-updates.md's "The program's update on Windows"). The setup then runs only for a release that
/// changes the setup's own work (its level, release.json's and the copy's <c>Redux update/setup.json</c>), so an update
/// goes in whoever started the game: a lobby that ends everything the game starts (MechCommander Online's job) included.
///
/// Windows won't overwrite or delete a running program or a loaded DLL, but lets either be renamed. INSTALL stages the
/// program's zip into <c>Redux update/swap/staged/&lt;build&gt;/</c>, each file checked against release.json. Once
/// staged, the swap checks each file again and asks Windows whether it will run the new programs (Smart App Control),
/// writes its journal (<c>Redux update/swap/journal.json</c>), then, <c>MCRedux.exe</c> last, renames each old file into
/// <c>Redux update/swap/old/&lt;build&gt;/</c> and the new one into its place, makes the game's other names (MCX.EXE and the
/// resolutions' copies, where they were the game) links to the new <c>MCRedux.exe</c>, flushes, sets the Apps list's
/// version, and deletes the journal: the commit. Stopped midway, the next start finishes it from the journal, or puts
/// the old files back when a staged one is gone or damaged; a start with nothing under way deletes what earlier swaps
/// set aside, as no copy runs it any more.
/// </summary>
namespace MCProgramSwap
{
    /// <summary>The swap's folders and records, in the game folder.</summary>
    constexpr std::string_view SwapFolder = MCContentList::InUpdateFolder<"swap">;
    constexpr std::string_view StagedFolder = MCContentList::InUpdateFolder<"swap/staged">;
    constexpr std::string_view OldFolder = MCContentList::InUpdateFolder<"swap/old">;
    constexpr std::string_view JournalFile = MCContentList::InUpdateFolder<"swap/journal.json">;
    /// <summary>The setup's record, which it writes as it installs: its level (<see cref="SetupLevel"/>).</summary>
    constexpr std::string_view SetupFile = MCContentList::InUpdateFolder<"setup.json">;

    /// <summary>The game's program, which goes in last, and the editor: Windows is asked whether it runs both.</summary>
    constexpr std::string_view GameName = "MCRedux.exe";
    constexpr std::string_view EditorName = "MCEditor.exe";

    /// <summary>What the platform does for the swap.</summary>
    struct MCPlatform
    {
        /// <summary>Whether the system will run <c>program</c>: empty, or why not, as the UPDATE page says it.</summary>
        std::function<std::string(const std::filesystem::path& program)> Runnable;
        /// <summary>
        /// Sets the version the system's list of programs shows for the install in <c>gameFolder</c> (Windows' Apps
        /// list: the uninstall entry's, where it's the player's own): empty, or why not (logged).
        /// </summary>
        std::function<std::string(const std::filesystem::path& gameFolder, std::string_view version)> ShowVersion;
    };

    /// <summary>The level of the setup that last installed the copy in <paramref name="gameFolder"/>; 0 without one.</summary>
    uint64_t SetupLevel(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>
    /// Whether <paramref name="program"/> goes in by the swap for a copy whose setup was at level <paramref
    /// name="copyLevel"/>: its files (the game's among them) and zip given, and its setup's level the copy's.
    /// </summary>
    bool Swaps(uint64_t copyLevel, const MCReleaseFile::MCProgram& program);

    /// <summary>
    /// Stages <paramref name="program"/>'s files in <paramref name="gameFolder"/>: those staged already and still right
    /// stay, the others are unpacked from its zip (the hosted <paramref name="zipName"/> on <paramref name="source"/>,
    /// resumed when cut off), each checked against release.json and flushed; other builds' staged files go. A program
    /// in its place already needs nothing; while a swap stopped midway awaits a start, nothing is staged.
    /// <paramref name="received"/> gets the zip's bytes already here, then each count that arrives (0: asked whether to
    /// go on), and returns false to stop.
    /// </summary>
    /// <returns>Nothing, or what failed.</returns>
    std::expected<void, std::string> Stage(MCUpdateDisk& disk, MCContentStage::MCSource& source,
                                           std::string_view zipName, const std::filesystem::path& gameFolder,
                                           const MCReleaseFile::MCProgram& program,
                                           const std::function<bool(uint64_t)>& received);

    /// <summary>
    /// Swaps <paramref name="program"/>'s staged files in (as shown, version <paramref name="version"/>), under the
    /// staging lock. A file held past a few tries, or anything else failing, puts the old files back. A program in its
    /// place already (its swap committed, the update tried again) is left as it is.
    /// </summary>
    /// <returns>Nothing once committed, or what failed (the old files are back, or the next start puts them back).</returns>
    std::expected<void, std::string> Swap(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                          const MCReleaseFile::MCProgram& program, std::string_view version,
                                          const MCPlatform& platform);

    enum class MCOutcome : uint8_t
    {
        /// <summary>No swap was under way.</summary>
        Nothing,
        /// <summary>A swap stopped midway is finished: the next start runs the new program.</summary>
        Finished,
        /// <summary>A swap stopped midway is put back (a staged file gone or damaged): the old program stays.</summary>
        PutBack,
        /// <summary>
        /// A swap stopped midway was overtaken: a file is neither its old one nor its new one (the setup ran since), so
        /// its journal goes and nothing moves.
        /// </summary>
        Overtaken,
        /// <summary>It can't be finished or put back (<see cref="MCResult::Error"/>): the setup repairs it.</summary>
        Stuck,
    };

    struct MCResult
    {
        MCOutcome Outcome = MCOutcome::Nothing;
        std::string Error;
    };

    /// <summary>
    /// At a start, under the staging lock: a swap stopped midway is finished, or put back (or dropped, where something
    /// else has installed since: <see cref="MCOutcome::Overtaken"/>). With none under way, the
    /// files earlier swaps set aside go, and those staged for build <paramref name="runningBuild"/> or older (what a
    /// running copy still holds stays for a later start).
    /// </summary>
    MCResult Recover(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, uint64_t runningBuild,
                     const MCPlatform& platform);
}
