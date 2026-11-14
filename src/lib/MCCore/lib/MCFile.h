#pragma once

#include "lib/MCDiskFile.h"

#include <atomic>
#include <format>
#include <string>
#include <chrono>
#include <thread>

// PROBE: the game's thread's file work, summed.
namespace MCProbeIo
{
    inline std::atomic<std::thread::id> GameThread{};
    inline std::atomic<int64_t> FstNs{0}, Fsts{0}, ResolveNs{0}, Resolves{0}, TakeNs{0}, Takes{0};
    inline std::atomic<int64_t> OpenNs{0}, Opens{0}, DiskReadNs{0}, DiskReads{0}, TgaNs{0}, Tgas{0}, FitNs{0}, Fits{0};
    struct Timer
    {
        std::atomic<int64_t>& Ns;
        std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
        bool On = std::this_thread::get_id() == GameThread.load();
        explicit Timer(std::atomic<int64_t>& ns, std::atomic<int64_t>& count) : Ns(ns)
        {
            if (On)
            {
                ++count;
            }
        }
        ~Timer()
        {
            if (On)
            {
                Ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - Start).count();
            }
        }
    };
    struct Snap
    {
        int64_t V[14];
        std::chrono::steady_clock::time_point At;
    };
    inline Snap Take()
    {
        return {{OpenNs.load(), Opens.load(), DiskReadNs.load(), DiskReads.load(), TgaNs.load(), Tgas.load(), FitNs.load(),
                 Fits.load(), FstNs.load(), Fsts.load(), ResolveNs.load(), Resolves.load(), TakeNs.load(), Takes.load()},
                std::chrono::steady_clock::now()};
    }
    inline std::string Since(const Snap& from)
    {
        const Snap now = Take();
        const auto ms = [&](int i) { return static_cast<double>(now.V[i] - from.V[i]) / 1e6; };
        const auto n = [&](int i) { return now.V[i] - from.V[i]; };
        return std::format("{:.2f} ms in all; opens {} {:.2f} ms (ahead looked {} {:.2f} ms, resolve and disk {} {:.2f} ms, archive reads {} {:.2f} ms), disk reads {} {:.2f} ms, art {} {:.2f} ms, fit {} {:.2f} ms",
                           std::chrono::duration<double, std::milli>(now.At - from.At).count(), n(1), ms(0), now.V[13] - from.V[13],
                           (now.V[12] - from.V[12]) / 1e6, now.V[11] - from.V[11],
                           (now.V[10] - from.V[10]) / 1e6, now.V[9] - from.V[9], (now.V[8] - from.V[8]) / 1e6, n(3), ms(2),
                           n(5), ms(4), n(7), ms(6));
    }
}

// Result codes of the file layer. The values are the original's (0xBADF00xx, returned as negative longs); the names
// are the port's, since the original's weren't kept. Open failures from the OS return small positive codes as
// GetLastError did (FILE_NOT_FOUND = 2). They stay int32_t constants while the game's int32_t result chains carry them.
inline constexpr int32_t NO_ERR = 0;
inline constexpr int32_t FILE_NOT_FOUND = 2;
inline constexpr int32_t READ_ERR = 30;
inline constexpr int32_t READ_PAST_EOF = static_cast<int32_t>(0xBADF0008);
inline constexpr int32_t INVALID_SEEK = static_cast<int32_t>(0xBADF0009);
inline constexpr int32_t WRITE_ERR = static_cast<int32_t>(0xBADF000A);
inline constexpr int32_t TOO_MANY_CHILDREN = static_cast<int32_t>(0xBADF000F);
inline constexpr int32_t CANT_WRITE_TO_CHILD = static_cast<int32_t>(0xBADF0011);

/// <summary>How a <see cref="MCFile"/> is opened.</summary>
/// <remarks>The original also had append, write and read-write modes; nothing in the game opens a file with them.</remarks>
enum class MCFileMode : uint8_t
{
    /// <summary>Read an existing file (from the disk, a test's memory source or the FastFiles).</summary>
    Read,
    /// <summary>Create (truncate) a file in the user folder and write it.</summary>
    Create
};

/// <summary>What kind of <see cref="MCFile"/> an object is (<see cref="MCFile::GetFileClass"/>).</summary>
enum class MCFileClass : uint8_t
{
    Base,
    Ini,
    Packet
};

/// <summary>
/// The game's file: a file on disk, a file read whole into memory (a FastFile entry, a test's in-memory file), or a
/// "child" window into a parent file (a packet of a <c>PacketFile</c>). Every read of game data goes through it.
/// </summary>
/// <remarks>
/// Original source: <c>lib\file.cpp</c>. The original wrapped a Win32 handle; the port an <c>MCDiskFile</c>, with
/// paths resolved against the game install by <c>MCFileSystem</c>. A loose file on disk wins over a FastFile entry of
/// the same name, as in the original. The file closes when it is destroyed. The original kept a list of its children
/// and closed them with it; here a child watches its parent's <see cref="OpenState"/> and closes itself at its next
/// operation once that is gone, which leaves it in the same state.
/// </remarks>
class MCFile
{
public:
    MCFile();
    virtual ~MCFile();
    MCFile(const MCFile&) = delete;
    MCFile& operator=(const MCFile&) = delete;

    /// <summary>Whether the read position is at or past the end.</summary>
    bool Eof();

    /// <summary>Opens <paramref name="fileName"/>: from disk, or else from memory or the FastFiles (read whole).</summary>
    /// <returns>0, or an error code (FILE_NOT_FOUND when nothing has it).</returns>
    virtual int32_t Open(std::string_view fileName, MCFileMode mode = MCFileMode::Read);

    /// <summary>Opens a window of <paramref name="length"/> bytes into <paramref name="parent"/> from its read position.</summary>
    /// <returns>0, or CANT_WRITE_TO_CHILD or TOO_MANY_CHILDREN (a parent read whole takes no children).</returns>
    virtual int32_t Open(MCFile& parent, uint32_t length);

    /// <summary>Creates (truncates) <paramref name="fileName"/> for writing.</summary>
    virtual int32_t Create(std::string_view fileName);

    /// <summary>Closes the file and its children.</summary>
    virtual void Close();

    /// <summary>What kind of file this is.</summary>
    virtual MCFileClass GetFileClass() const { return MCFileClass::Base; }

    /// <summary>Moves the read position (<paramref name="from"/> is SEEK_SET, SEEK_CUR or SEEK_END).</summary>
    int32_t Seek(int32_t pos, int32_t from = SEEK_SET);

    /// <summary>Reads one byte.</summary>
    uint8_t ReadByte();

    /// <summary>Reads a 16-bit value.</summary>
    int16_t ReadWord();

    /// <summary>Reads a 16-bit value (the same as <see cref="ReadWord"/>).</summary>
    int16_t ReadShort();

    /// <summary>Reads a 32-bit value.</summary>
    int32_t ReadLong();

    /// <summary>Reads a 32-bit float.</summary>
    float ReadFloat();

    /// <summary>Reads into <paramref name="buffer"/> from the read position.</summary>
    /// <returns>The number of bytes read.</returns>
    int32_t Read(std::span<uint8_t> buffer);

    /// <summary>
    /// Reads a line ending in CR (or CR LF, or LF), looking at no more than <paramref name="maxLength"/> bytes, and
    /// moves past its ending.
    /// </summary>
    /// <remarks>
    /// Original behaviour (OB-135): a line longer than <paramref name="maxLength"/> is cut there, and the byte after
    /// the cut is skipped.
    /// </remarks>
    std::string ReadLine(int32_t maxLength);

    /// <summary>
    /// <see cref="ReadLine(int32_t)"/> into <paramref name="buffer"/> (at least <paramref name="maxLength"/> + 1
    /// bytes), zero-terminated.
    /// </summary>
    /// <returns>The line's length plus one, or 0 when the file isn't open.</returns>
    int32_t ReadLine(std::span<char> buffer, int32_t maxLength);

    /// <summary><see cref="ReadLine(std::span{char}, int32_t)"/> into bytes.</summary>
    int32_t ReadLine(std::span<uint8_t> buffer, int32_t maxLength);

    /// <summary>Writes one byte.</summary>
    int32_t WriteByte(uint8_t value);

    /// <summary>Writes a 32-bit value.</summary>
    int32_t WriteLong(int32_t value);

    /// <summary>Writes a string without a terminator.</summary>
    /// <returns>Its length, or -1 when the file can't be written.</returns>
    int32_t WriteString(std::string_view text);

    /// <summary>Writes a string and CR LF.</summary>
    /// <returns>The string's length, or -1 when the file can't be written.</returns>
    int32_t WriteLine(std::string_view text);

    /// <summary>Writes <paramref name="data"/> at the write position.</summary>
    /// <returns>The number of bytes written.</returns>
    int32_t Write(std::span<const uint8_t> data);

    /// <summary>Whether the file is open (on disk, through its parent's disk file, or in memory).</summary>
    bool IsOpen() const;

    /// <summary>The path the file was opened with (a child has its parent's).</summary>
    std::string_view GetFilename() const { return Orphaned() ? std::string_view() : std::string_view(_FileName); }

    /// <summary>The file's length in bytes.</summary>
    uint32_t GetLength();

    /// <summary>The file's length in bytes (the same as <see cref="GetLength"/>).</summary>
    uint32_t FileSize() { return GetLength(); }

    /// <summary>Moves the read position forward by <paramref name="bytesToSkip"/>.</summary>
    void Skip(int32_t bytesToSkip);

    /// <summary>The read position.</summary>
    uint32_t GetLogicalPosition() const { return _LogicalPosition; }

    /// <summary>Whether this is an open child of another file.</summary>
    bool IsChild() const { return _IsChild && !Orphaned(); }

protected:
    /// <summary>
    /// What a file's child windows read through while the file is open. A file opened from disk holds its disk file
    /// here; a window holds its parent's state. A child keeps only a <c>weak_ptr</c> to it: when the parent closes,
    /// the state goes and the child reads as closed, as when the original closed its children with it.
    /// </summary>
    struct OpenState
    {
        /// <summary>The open file on disk, for a file opened from disk.</summary>
        std::optional<MCDiskFile> Disk;
        /// <summary>The parent's state, for a window.</summary>
        std::weak_ptr<OpenState> Parent;
    };

    /// <summary>
    /// Opens a child window (see the public overload). With <paramref name="readWhole"/> its bytes are read into
    /// memory at once, and it takes no children of its own.
    /// </summary>
    int32_t OpenChild(MCFile& parent, uint32_t length, bool readWhole);

    /// <summary>The state of the file on disk this file reads: its own, or its parents'; null for a file in memory.</summary>
    std::shared_ptr<OpenState> DiskState() const;

    /// <summary>Whether this is a child whose parent (or a parent of it) has closed.</summary>
    bool Orphaned() const;

    /// <summary>
    /// Closes a child whose parent has closed (see <see cref="Orphaned"/>), as the original's parent closed it then.
    /// Every operation that changes or reads the file calls it first.
    /// </summary>
    void CloseIfOrphaned();

    /// <summary>
    /// Reads up to <paramref name="buffer"/>'s size at <paramref name="pos"/> from wherever the file lives (its memory
    /// image, the disk, or its parent's image for a child) without moving the read position.
    /// </summary>
    int32_t ReadRawAt(uint32_t pos, std::span<uint8_t> buffer);

    /// <summary>Writes at the read/write position, advancing it.</summary>
    int32_t WriteRaw(std::span<const uint8_t> data);

    /// <summary>The path the file was opened with.</summary>
    std::string _FileName;
    /// <summary>How it was opened.</summary>
    MCFileMode _FileMode = MCFileMode::Read;
    /// <summary>
    /// The state children read through, while the file is open and takes children: a file opened from disk (with
    /// its disk file) or a child window. A file read whole into memory has none and takes no children.
    /// </summary>
    std::shared_ptr<OpenState> _State;
    /// <summary>Cached length.</summary>
    uint32_t _Length = 0;
    /// <summary>The read/write position.</summary>
    uint32_t _LogicalPosition = 0;
    /// <summary>Whether this is a child file.</summary>
    bool _IsChild = false;
    /// <summary>The parent's state, for a child.</summary>
    std::weak_ptr<OpenState> _ParentState;
    /// <summary>Where a child starts within its parent.</summary>
    uint32_t _ParentOffset = 0;
    /// <summary>A child's length within its parent.</summary>
    uint32_t _PhysicalLength = 0;
    /// <summary>Whether the file's contents are in <see cref="_FileImage"/>.</summary>
    bool _InRam = false;
    /// <summary>The file's contents, for a file read whole into memory.</summary>
    std::vector<uint8_t> _FileImage;
};

/// <summary>Whether a loose file exists on disk (FastFiles aren't searched).</summary>
bool FileExists(std::string_view fileName);

/// <summary>
/// A game path built from a directory (with its trailing <c>\</c>), a name and an extension: <c>data\art\</c> +
/// <c>access00</c> + <c>.tga</c>.
/// </summary>
std::string GamePath(std::string_view directory, std::string_view name, std::string_view extension = {});
