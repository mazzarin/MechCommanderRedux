#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class MCFastFileSet;

/// <summary>
/// Port-only: the game's files read ahead on threads of their own, for work about to open them: the way back from a
/// mission's results, which makes the logistics' screens again from the files they were made of before, their art, the
/// campaign's data. Each file is looked for as <c>MCFile::Open</c> looks for a file to read (one the game put, a loose
/// file in the user folder or the install, a test's file in memory, then the archives), and kept when only an archive
/// holds it (a FastFile entry, read whole and unpacked) or nothing does; <c>MCFile::Open</c> then takes it from here
/// rather than looking and reading itself: the same bytes, or the same file not found. Which files: those the game's
/// thread looked for in the archives while the logistics' screens were up (<see cref="StartNoting"/>).
/// </summary>
/// <remarks>
/// A file written, removed, renamed or put (MCFileSystem) is dropped, read ahead or still to be, so its next open looks
/// again. Threads: everything from the game's thread but <see cref="Drop"/> (any); the reads run on threads of their own.
/// </remarks>
namespace MCFilesAhead
{
    /// <summary>The most bytes a read ahead keeps, and the largest file it reads (the logistics' backgrounds: 300 KB).</summary>
    inline constexpr size_t MostBytes = 64u * 1024u * 1024u;
    inline constexpr size_t MostFileBytes = 4u * 1024u * 1024u;

    /// <summary>
    /// What <see cref="Take"/> hands over: the file's bytes, unpacked, or that no file of that name is there to read.
    /// </summary>
    struct MCAheadFile
    {
        bool Found = false;
        std::vector<uint8_t> Bytes;
    };

    /// <summary>
    /// From now on, until <see cref="StopNoting"/>, notes the files the calling thread looks for to read and finds in an
    /// archive or nowhere (<see cref="Noted"/>; MCFile::Open says so through <see cref="Looked"/>).
    /// </summary>
    void StartNoting();

    /// <summary>Stops <see cref="StartNoting"/>'s noting; what was noted stays.</summary>
    void StopNoting();

    /// <summary>
    /// MCFile::Open's word that it looked for <paramref name="gamePath"/> to read and found it in an archive, or nowhere:
    /// noted while the calling thread notes.
    /// </summary>
    void Looked(std::string_view gamePath);

    /// <summary>The files noted (see <see cref="StartNoting"/>), each once, in the order first looked for.</summary>
    std::vector<std::string> Noted();

    /// <summary>
    /// Starts reading <paramref name="gamePaths"/> ahead from <paramref name="archives"/> (the game's, which must stay
    /// open until <see cref="Forget"/>), on threads of their own, up to <see cref="MostBytes"/>. What a read ahead before
    /// kept and wasn't taken is let go first.
    /// </summary>
    void ReadAhead(std::vector<std::string> gamePaths, MCFastFileSet& archives);

    /// <summary>
    /// <paramref name="gamePath"/> as read ahead (waiting while it's still being read), a copy for each open until
    /// <see cref="Forget"/> (the screens open many files twice), or none when it wasn't (not asked for, too large, a
    /// loose file, dropped): MCFile::Open then looks for it as ever.
    /// </summary>
    std::optional<MCAheadFile> Take(std::string_view gamePath);

    /// <summary>Drops <paramref name="gamePath"/> (written, removed, renamed or put), read ahead or still to be.</summary>
    void Drop(std::string_view gamePath);

    /// <summary>Lets go of what was read ahead and not taken, stopping the reads still running.</summary>
    void Forget();

    /// <summary>For the tests: the opens that took a file read ahead since the game started, and the files held now.</summary>
    size_t TakenCount();
    size_t HeldCount();
}
