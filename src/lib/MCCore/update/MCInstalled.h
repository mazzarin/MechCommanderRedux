#pragma once

#include "lib/MCSha256.h"
#include "update/MCContentList.h"

#include <functional>
#include <map>
#include <set>

class MCUpdateDisk;

/// <summary>
/// Port-only: what a copy has installed (<c>Redux update/installed.json</c> in the game folder): the content release
/// and its list, the sets the copy has (the installer's HD answer, then HD OPTIONS'), and each managed file as it was
/// last seen, with its hash, size and time written. A file whose size and time are still as recorded keeps its
/// recorded hash, so a plan hashes only what changed since. Every listed file is the release's: one an update placed
/// is deleted when replaced or dropped; any other (the folder's own, found as the release has it, or one changed since)
/// is kept in <c>Redux backup</c> (update/MCContentSwap.h).
/// </summary>
namespace MCInstalled
{
    /// <summary>Where the record is, in the game folder.</summary>
    constexpr std::string_view RecordPath = MCContentList::InUpdateFolder<"installed.json">;

    struct MCFile
    {
        /// <summary>Its path as spelled on disk.</summary>
        std::string Path;
        /// <summary>The set it belongs to.</summary>
        std::string Set;
        MCSha256::MCDigest Sha256{};
        uint64_t Size = 0;
        /// <summary>The time it was written, in the file system's own ticks (only ever compared on this machine).</summary>
        int64_t Written = 0;
        /// <summary>
        /// Whether an update put it there (fetched or patched), not found in place as the release has it: only a file an
        /// update placed is deleted when replaced or dropped, and by the uninstall.
        /// </summary>
        bool Placed = true;
    };

    struct MCRecord
    {
        /// <summary>The content release installed (0: none yet) and its list's hash.</summary>
        uint64_t Release = 0;
        MCSha256::MCDigest List{};
        /// <summary>
        /// The epoch of the key list it was taken under (MCReleaseFile::MCKeyList's; 0 recorded by an earlier version): a
        /// release of a later epoch comes after it whatever its number.
        /// </summary>
        uint64_t Epoch = 0;
        /// <summary>The sets the copy has.</summary>
        std::set<std::string, std::less<>> Sets;
        /// <summary>
        /// The bytes of each set the release lists, had or not (what HD OPTIONS says getting HD takes; none recorded by
        /// an earlier version).
        /// </summary>
        std::map<std::string, uint64_t, std::less<>> SetBytes;
        /// <summary>
        /// The swap that wrote it (a random id each update's journal carries; empty none): the swap is committed once
        /// the record on disk is its own, whatever the release (a repair keeps the same one).
        /// </summary>
        std::string Swap;
        /// <summary>The files, by their listed paths folded (MCContentList::FoldedPath).</summary>
        std::map<std::string, MCFile, std::less<>> Files;
    };

    /// <summary>The record in <paramref name="gameFolder"/> on <paramref name="disk"/>; an empty one when there's none.</summary>
    /// <returns>The record, or why the one there can't be read.</returns>
    std::expected<MCRecord, std::string> Read(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>The record in <paramref name="gameFolder"/> on the disk (<see cref="Read(MCUpdateDisk&amp;, const std::filesystem::path&amp;)"/>).</summary>
    std::expected<MCRecord, std::string> Read(const std::filesystem::path& gameFolder);

    /// <summary>The record as written.</summary>
    std::string Text(const MCRecord& record);

    /// <summary>The record in <paramref name="text"/>.</summary>
    std::expected<MCRecord, std::string> FromText(std::string_view text);

    /// <summary>The time <paramref name="path"/> was written, as recorded, or none when it can't be read.</summary>
    std::optional<int64_t> WrittenAt(const std::filesystem::path& path);

    /// <summary>
    /// Whether the copy has <paramref name="set"/>: as <paramref name="record"/> says once a release is installed, else
    /// as the game folder holds it (MCContentUpdater::SetsFound: HD when its folder is there).
    /// </summary>
    bool Has(const MCRecord& record, const std::filesystem::path& gameFolder, std::string_view set);

    /// <summary>
    /// The content a game armed a program alone for (its release's list in a format it can't read): the release, and
    /// the program build that takes it (SPEC-install-and-updates.md's Formats only grow). In the game folder; a
    /// committed swap of that release or a later one deletes it.
    /// </summary>
    constexpr std::string_view DueFile = "Redux update/due.json";

    struct MCDue
    {
        /// <summary>The release's place in the order (MCReleaseFile::MCOrder): its key list's epoch, then its number.</summary>
        uint64_t Epoch = 0;
        uint64_t Release = 0;
        uint64_t LowestProgram = 0;

        bool operator==(const MCDue&) const = default;
    };

    /// <summary>Notes <paramref name="due"/> (<see cref="DueFile"/>), durably.</summary>
    std::expected<void, std::string> NoteDue(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                             const MCDue& due);

    /// <summary>The content noted due (<see cref="DueFile"/>); none without the note, or when it can't be read (logged).</summary>
    std::optional<MCDue> ReadDue(MCUpdateDisk& disk, const std::filesystem::path& gameFolder);

    /// <summary>What a start must get before anything opens.</summary>
    enum class MCStartNeed : uint8_t
    {
        /// <summary>Nothing: the files play as they are.</summary>
        Nothing,
        /// <summary>
        /// The release a program was armed alone for (<see cref="DueFile"/>), this program taking it: fetched first;
        /// if it can't be, the files play as they are.
        /// </summary>
        Due,
        /// <summary>
        /// The release, which the game can't play without: a first start (an installer's answer, nothing installed
        /// yet), or an installed release older than the program plays.
        /// </summary>
        Needed,
    };

    /// <summary>
    /// What a start of build <paramref name="runningBuild"/>, which plays content releases from
    /// <paramref name="lowestContent"/> on, must get before anything opens, the copy having <paramref name="record"/>.
    /// </summary>
    MCStartNeed AtStart(MCUpdateDisk& disk, const std::filesystem::path& gameFolder, const MCRecord& record,
                        uint64_t runningBuild, uint64_t lowestContent);
}
