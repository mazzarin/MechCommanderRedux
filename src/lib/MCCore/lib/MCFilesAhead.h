#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

    /// <summary>
    /// Port-only (the workspace's SPEC-memory.md, "From the briefing"): a piece of a file in the game folder (the
    /// install, the release's own copy in it) that a mission's load read from the disk: the file's path from the game
    /// folder (<c>/</c> between its parts, as spelled on the disk) and where its bytes lie.
    /// </summary>
    struct MCFilePiece
    {
        std::string File;
        uint64_t Offset = 0;
        uint64_t Bytes = 0;

        bool operator==(const MCFilePiece&) const = default;
    };

    /// <summary>
    /// From now on, until <see cref="StopNotingPieces"/>, notes the pieces of the game folder's files the calling
    /// thread reads from the disk (MCDiskFile says so through <see cref="ReadFromDisk"/>), the ones before forgotten.
    /// </summary>
    void StartNotingPieces();

    /// <summary>Stops <see cref="StartNotingPieces"/>'s noting; what was noted stays.</summary>
    void StopNotingPieces();

    /// <summary>
    /// MCDiskFile's word that it read <paramref name="bytes"/> at <paramref name="offset"/> of <paramref name="path"/>
    /// from the disk: noted while the calling thread notes pieces.
    /// </summary>
    void ReadFromDisk(const std::filesystem::path& path, uint64_t offset, uint64_t bytes);

    /// <summary>
    /// The pieces noted (see <see cref="StartNotingPieces"/>) of the game folder's files, each file's in order, those
    /// that meet or lie within MCDiskFile::RunGapBytes of each other joined; the files by name.
    /// </summary>
    std::vector<MCFilePiece> NotedPieces();

    /// <summary>
    /// Starts reading <paramref name="pieces"/> of the game folder's files on a thread of its own, one read at a time of
    /// at most MCDiskFile::RunMostBytes, each file's in order, into the system's cache of files, keeping nothing: a
    /// load's reads of them then come from memory. The reads of a call before are stopped first. A file not there, or
    /// shorter, is read as far as it goes: the pieces say only what to read ahead, never what's in it.
    /// </summary>
    void ReadPiecesAhead(std::vector<MCFilePiece> pieces);

    /// <summary>Stops <see cref="ReadPiecesAhead"/>'s reads still running (the system keeps what they read).</summary>
    void ForgetPieces();

    /// <summary>For the tests: the bytes <see cref="ReadPiecesAhead"/>'s reads have read since the game started.</summary>
    uint64_t PiecesReadBytes();

    /// <summary>
    /// <paramref name="pieces"/> as a list's lines (a mission's learned list, the tools'): a piece a line, its file, offset
    /// and bytes apart by tabs.
    /// </summary>
    std::string PiecesText(const std::vector<MCFilePiece>& pieces);

    /// <summary>
    /// The pieces <paramref name="text"/>'s lines name (<see cref="PiecesText"/>'s), leaving out a line that names no
    /// file under the game folder (absolute, or climbing out of it) or no bytes.
    /// </summary>
    std::vector<MCFilePiece> PiecesOfText(std::string_view text);
}
