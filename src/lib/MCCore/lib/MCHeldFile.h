#pragma once

// Only standard headers and MCDiskFile's: MCDiskFile.cpp, built without the precompiled header on Windows, includes it.
#include "lib/MCDiskFile.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

/// <summary>
/// Port-only (SPEC-first-use.md): a file's bytes read whole into memory on threads of their own, from the moment it's
/// held, so that the reads of the <see cref="MCDiskFile"/>s it's given to (<see cref="MCDiskFile::Hold"/>) come from
/// here rather than the disk: the original's small files a mission's load reads all through (its FastFiles' entries, its
/// packet files' fits and shapes), each a trip to a slow disk read where it lies, read here in a few large reads at once.
/// </summary>
/// <remarks>
/// The file is read in pieces of <see cref="PieceBytes"/>, by up to <see cref="ReadsAtOnce"/> threads shared by every
/// file held, in the order the files were held and each file's pieces in order. A read of bytes whose piece is still
/// waiting reads that piece itself, on its own thread, rather than wait behind the others; one whose piece is being read
/// waits for it. A piece that reads short (the file shorter than it was, or failing) fails the whole hold, its other
/// pieces left unread, and the disk is read as ever. Letting go of the last holder stops the pieces still waiting (one
/// being read keeps it until it's done). Threads: any.
/// </remarks>
class MCHeldFile
{
public:
    /// <summary>The bytes each of a file's reads takes, and the reads under way at once over every file held.</summary>
    static constexpr size_t PieceBytes = MCDiskFile::RunMostBytes;
    static constexpr size_t ReadsAtOnce = 4;

    /// <summary>
    /// The largest of the original's FastFiles a mission's load holds (its fits and scripts, its mechs' hot spots: ART,
    /// MISC, MISSION and SHAPES.FST, 0.3-4.7 MB; not TERRAIN.FST's 16.5 MB, of which it reads a few entries), and the
    /// largest of the packet files its type and sprite managers read through (sprites.pak's fits, object2.pak's types,
    /// shapes.pak's shapes, 1.2-17.6 MB; not shapes90.pak's 100 MB or the mechs' part PAKs).
    /// </summary>
    static constexpr uint32_t MostFastFileBytes = 8u * 1024u * 1024u;
    static constexpr uint32_t MostPacketFileBytes = 32u * 1024u * 1024u;

    /// <summary>
    /// Starts reading the <paramref name="length"/> bytes of <paramref name="path"/> (opened again for each piece, and
    /// refused unless it is still that long), after the pieces of the files held before it; <paramref name="name"/>
    /// names it in <see cref="Live"/>'s list.
    /// </summary>
    static std::shared_ptr<MCHeldFile> Read(std::filesystem::path path, uint32_t length, std::string name);

    /// <summary>
    /// The bytes at <paramref name="offset"/> into <paramref name="buffer"/>, as the file gives them (fewer at its end),
    /// once their pieces are read: waiting for those being read, reading those still waiting itself.
    /// </summary>
    /// <returns>The bytes copied, or none when the hold failed (the caller reads the disk).</returns>
    std::optional<size_t> ReadAt(uint32_t offset, std::span<uint8_t> buffer);

    /// <summary>What a held file did, for the frame log and the tests.</summary>
    struct MCHeldStats
    {
        std::string Name;
        uint32_t Bytes = 0;
        /// <summary>The reads it gave.</summary>
        size_t Served = 0;
        /// <summary>Its pieces, and those read by an asker rather than the readers.</summary>
        size_t Pieces = 0;
        size_t PiecesAsked = 0;
        /// <summary>Whether every piece is read, and from its hold to the last one's end.</summary>
        bool Done = false;
        double ReadMs = 0.0;
        bool Failed = false;
    };

    MCHeldStats Stats() const;

    /// <summary>Every file held now, in the order held.</summary>
    static std::vector<MCHeldStats> Live();

    /// <summary>Lets only <see cref="Read"/> make one.</summary>
    class Key
    {
        friend class MCHeldFile;
        Key() = default;
    };

    MCHeldFile(Key, std::filesystem::path path, uint32_t length, std::string name);
    MCHeldFile(const MCHeldFile&) = delete;
    MCHeldFile& operator=(const MCHeldFile&) = delete;

    /// <summary>
    /// The readers' work: reads piece <paramref name="piece"/> unless another thread has taken it; with
    /// <paramref name="asked"/>, for an asker.
    /// </summary>
    void ReadPiece(size_t piece, bool asked);

private:
    enum class MCPieceState : uint8_t
    {
        Waiting,
        Reading,
        Done
    };

    /// <summary>Marks piece <paramref name="piece"/> done, under <see cref="_Lock"/>.</summary>
    void PieceDone(size_t piece);

    const std::filesystem::path _Path;
    const uint32_t _Length;
    const std::string _Name;
    const std::chrono::steady_clock::time_point _Held;
    /// <summary>The file's bytes, each piece's written by the thread that read it before it's marked done.</summary>
    std::vector<uint8_t> _Bytes;

    /// <summary>Guards what follows.</summary>
    mutable std::mutex _Lock;
    /// <summary>Told as each piece is done.</summary>
    std::condition_variable _PieceDone;
    std::vector<MCPieceState> _Pieces;
    size_t _PiecesDone = 0;
    size_t _PiecesAsked = 0;
    size_t _Served = 0;
    bool _Failed = false;
    std::chrono::steady_clock::time_point _AllRead{};
};
