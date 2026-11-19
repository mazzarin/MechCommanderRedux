#pragma once

#include "lib/MCSha256.h"
#include "update/MCContentList.h"
#include "update/MCInstalled.h"

#include <filesystem>
#include <map>

class MCUpdateDisk;

/// <summary>
/// Port-only (Redux HD): the release's own copy of the data a match plays, kept in a folder the game owns
/// (<c>Redux/Release/</c> in the game folder) so a mod in the game folder never changes a match: the files the data
/// check covers (network/MCGameDataHash.h), the stock archives and <c>DATA/OBJECTS</c>' and <c>DATA/SAVEGAME</c>'s
/// listed files, about 28 MB of the release's 626. A match reads a file the copy holds only from it
/// (lib/MCReleaseData.h); single player reads the game folder as ever, mods and all.
///
/// Each start fills it after the content's step (<see cref="Sync"/>): a listed file the copy lacks, or holds as an
/// older release had it, comes from the game folder's copy when that is the release's (as installed.json records it,
/// else as its hash says). On the Mac it's a clone (APFS: no space until one side changes); elsewhere, or on another
/// volume, a copy. A file the game folder holds changed (a mod written over a stock archive) can't fill it: the copy
/// lacks it, and the match reads the game folder's for that name until an update or a repair puts the release's back.
/// <c>release.json</c> in the folder records each file held: its hash, size and time written, so a start hashes only
/// what changed.
/// </summary>
namespace MCReleaseCopy
{
    /// <summary>The copy's folder, and its record, in the game folder.</summary>
    constexpr std::string_view Folder = "Redux/Release";
    constexpr std::string_view RecordName = "release.json";
    /// <summary>
    /// The lock a start holds exclusive while it fills the copy (update/MCUpdateLock.h), so two copies of the game
    /// starting together don't fill it at once: the one that can't take it leaves the filling to the other.
    /// </summary>
    constexpr std::string_view FillLock = MCContentList::InUpdateFolder<"copy.lock">;

    /// <summary>
    /// Whether the listed file <paramref name="listedPath"/> (as the content list spells it, from the game folder) is
    /// one the copy holds: an archive (<c>.fst</c>) in the folder's root, or a file under <c>DATA/OBJECTS/</c>, or a
    /// mission (<c>.mpk</c>) in <c>DATA/SAVEGAME/</c>. Case is ignored.
    /// </summary>
    bool Holds(std::string_view listedPath);

    /// <summary>A file the copy holds.</summary>
    struct MCHeld
    {
        /// <summary>Its path from the copy's folder, as spelled in the game folder ("DATA/OBJECTS/COMPBAS.CSV").</summary>
        std::string Path;
        MCSha256::MCDigest Sha256{};
        uint64_t Size = 0;
        /// <summary>The time the copy's file was written, in the file system's ticks (MCInstalled::WrittenAt).</summary>
        int64_t Written = 0;
    };

    /// <summary>What the copy holds, from its record.</summary>
    struct MCRecord
    {
        /// <summary>The content release it was filled from (0: none).</summary>
        uint64_t Release = 0;
        /// <summary>The files held, by their paths folded (MCContentList::FoldedPath).</summary>
        std::map<std::string, MCHeld, std::less<>> Files;
        /// <summary>The listed files it lacks (the game folder's changed), by their paths as spelled.</summary>
        std::vector<std::string> Lacking;
    };

    /// <summary>The copy's folder in <paramref name="gameFolder"/>.</summary>
    std::filesystem::path FolderIn(const std::filesystem::path& gameFolder);

    /// <summary>
    /// The copy's record in <paramref name="gameFolder"/>: none when there's no copy; or why it can't be read.
    /// </summary>
    std::expected<std::optional<MCRecord>, std::string> Read(MCUpdateDisk& disk,
                                                             const std::filesystem::path& gameFolder);

    /// <summary>How a <see cref="Sync"/> went.</summary>
    struct MCSynced
    {
        /// <summary>The files the copy holds, and those of them filled now.</summary>
        size_t Held = 0;
        size_t Filled = 0;
        /// <summary>The files hashed (the game folder's or the copy's, changed since recorded).</summary>
        size_t Hashed = 0;
        /// <summary>The listed files the copy lacks: the game folder's isn't the release's.</summary>
        std::vector<std::string> Lacking;
    };

    /// <summary>
    /// Fills the copy in <paramref name="gameFolder"/> from the files there, for the release <paramref name="installed"/>
    /// records: each listed file it <see cref="Holds"/> that the copy lacks or holds otherwise comes from the game
    /// folder when that file is the release's; a file the copy holds that isn't listed now goes. A copy whose file is
    /// as recorded is left alone. Its record is written last, durably. With no release installed, nothing is done.
    /// </summary>
    /// <returns>How it went, or why the copy can't be written (then the match reads the game folder).</returns>
    std::expected<MCSynced, std::string> Sync(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                              const MCInstalled::MCRecord& installed);

    /// <summary>
    /// Copies <paramref name="from"/> to <paramref name="to"/> (which mustn't exist): a clone where the file system
    /// gives one (APFS), else a copy.
    /// </summary>
    /// <returns>Nothing, or why it couldn't.</returns>
    std::expected<void, std::string> CloneOrCopy(const std::filesystem::path& from, const std::filesystem::path& to);
}
