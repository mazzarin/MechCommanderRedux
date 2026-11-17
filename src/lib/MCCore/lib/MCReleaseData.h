#pragma once

#include "lib/MCFastFileSet.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/// <summary>
/// Port-only (Redux HD): the release's own copy of a match's data (update/MCReleaseCopy.h), as the game reads it. While
/// a match is played (<see cref="SetPlaying"/>: MCGameContext::SetMultiPlayer), a file the copy holds is read only from
/// it (MCFile::Open, MCFilesAhead): the game folder's copy, its other archives, the user folder's and a launcher's are
/// passed over for that name, so a mod never changes a match. What the copy doesn't hold reads as ever: the art, sound
/// and movies, and what a mod or a launcher adds (a map, its terrain, a received mission). Single player, and a game
/// folder without a copy (a developer's data, the tests'), read as ever.
/// </summary>
namespace MCReleaseData
{
    /// <summary>
    /// Opens the copy in the game folder <paramref name="gameFolder"/>, as its record says it holds (a file changed since
    /// is left out); none there leaves the game without one.
    /// </summary>
    void Open(const std::filesystem::path& gameFolder);

    /// <summary>Lets go of the copy.</summary>
    void Close();

    /// <summary>Whether the game has a copy.</summary>
    bool Has();

    /// <summary>
    /// Whether the copy holds every file it should (none lacking: a game folder's file a mod was written over couldn't
    /// fill it).
    /// </summary>
    bool Whole();

    /// <summary>The listed files the copy lacks, as the content list spells them.</summary>
    const std::vector<std::string>& Lacking();

    /// <summary>
    /// Sets whether a match is played, so the copy is read (only with a copy): false outside one.
    /// </summary>
    /// <returns>Whether that changed what's read.</returns>
    bool SetPlaying(bool playing);

    /// <summary>Whether the copy is read now.</summary>
    bool Playing();

    /// <summary>
    /// How many times what's read has changed since the game started: what's kept across missions (the component list,
    /// logistics' game system values) is read again when it was read at another count.
    /// </summary>
    uint32_t Switches();

    /// <summary>Where the copy holds <paramref name="gamePath"/>, when it's read now.</summary>
    struct MCHeld
    {
        /// <summary>A loose file of the copy: the path on disk.</summary>
        std::optional<std::filesystem::path> Loose;
        /// <summary>An entry of one of the copy's archives.</summary>
        bool InArchive = false;

        explicit operator bool() const { return Loose.has_value() || InArchive; }
    };

    /// <summary>
    /// Where the copy holds <paramref name="gamePath"/> (as the game spells it: case and <c>\</c> versus <c>/</c>
    /// ignored) while a match is played; nothing outside one, or for a file the copy doesn't hold.
    /// </summary>
    MCHeld Find(std::string_view gamePath);

    /// <summary>The bytes of <paramref name="gamePath"/> from the copy's archives (<see cref="MCHeld::InArchive"/>).</summary>
    std::optional<std::vector<uint8_t>> ReadArchived(std::string_view gamePath);

    /// <summary>
    /// The names of the files the copy holds in the game folder <paramref name="folder"/> (as the game spells it,
    /// "data\\missions\\warriors"), not in its folders below: loose and in its archives, each once.
    /// </summary>
    std::vector<std::string> NamesIn(std::string_view folder);
}
