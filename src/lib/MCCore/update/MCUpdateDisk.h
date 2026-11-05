#pragma once

// Only standard headers and lib/MCSha256.h: MCUpdateDisk.cpp is built without the precompiled header on Windows.
#include "lib/MCSha256.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Port-only: the file operations updates make durable (update/MCContentSwap.h). A system keeps writes in its cache
/// and may lose or reorder those not flushed when the machine stops, so the swap flushes each file it writes and each
/// folder whose entries it changed before relying on them. The game uses the disk (<see cref="MakeDisk"/>); the tests
/// use one in memory that loses the unflushed changes at any step.
/// </summary>
class MCUpdateDisk
{
public:
    struct MCStat
    {
        bool Exists = false;
        bool Folder = false;
        uint64_t Size = 0;
        /// <summary>The time written, in the file system's ticks (MCInstalled::WrittenAt).</summary>
        int64_t Written = 0;
        /// <summary>
        /// Why it can't be told what's there (no access, a failing drive); empty when it can. Only a path the system
        /// says isn't there is missing: a decision to keep or remove a file never takes an error for that.
        /// </summary>
        std::string Error;
    };

    virtual ~MCUpdateDisk() = default;

    virtual MCStat Stat(const std::filesystem::path& path) = 0;
    virtual std::expected<MCSha256::MCDigest, std::string> Hash(const std::filesystem::path& path) = 0;
    virtual std::expected<std::string, std::string> ReadText(const std::filesystem::path& path) = 0;

    /// <summary>
    /// The files in <paramref name="folder"/> and its folders, as paths relative to it ('/' apart, UTF-8); none when
    /// it isn't there, or what failed.
    /// </summary>
    virtual std::expected<std::vector<std::string>, std::string> Files(const std::filesystem::path& folder) = 0;

    /// <summary>Makes the folder and those on its way.</summary>
    virtual bool MakeFolders(const std::filesystem::path& path) = 0;

    /// <summary>
    /// Moves a file, replacing what's at <paramref name="to"/>; both on one disk, so it's all or nothing. Only for the
    /// update's own records (<see cref="WriteDurably"/>, the journal's names): an update's files move with
    /// <see cref="Move"/>.
    /// </summary>
    virtual bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) = 0;

    /// <summary>
    /// Moves a file to <paramref name="to"/>, where nothing may be: false when something is (it's never replaced) or
    /// the move fails. Both on one disk, so it's all or nothing. On a volume whose system can't refuse a move where
    /// something is (<see cref="MCMoves"/>), it looks first.
    /// </summary>
    virtual bool Move(const std::filesystem::path& from, const std::filesystem::path& to) = 0;

    /// <summary>Deletes a file, or a folder and all it holds.</summary>
    virtual bool Remove(const std::filesystem::path& path) = 0;

    /// <summary>Deletes a folder when it holds nothing (false otherwise, or when it isn't a folder).</summary>
    virtual bool RemoveIfEmpty(const std::filesystem::path& path) = 0;

    /// <summary>Writes a file anew and flushes its bytes (its entry in its folder isn't flushed).</summary>
    virtual bool WriteFlushed(const std::filesystem::path& path, std::string_view bytes) = 0;

    /// <summary>Flushes a file's bytes, written another way (the staged files).</summary>
    virtual bool FlushFile(const std::filesystem::path& path) = 0;

    /// <summary>Flushes a folder's entries: what was moved, made or deleted in it.</summary>
    virtual bool FlushFolder(const std::filesystem::path& path) = 0;

    /// <summary>The bytes free to this program on the disk holding <paramref name="path"/>, or what failed.</summary>
    virtual std::expected<uint64_t, std::string> FreeBytes(const std::filesystem::path& path) = 0;
};

/// <summary>
/// How the disk moves an update's files where nothing may be (MCUpdateDisk::Move): as the system does in one step (on
/// the Mac renamex_np's RENAME_EXCL; Windows' MoveFileExW without replacing), falling back on a volume without it to
/// looking at the place first, then the rename; or always looking first (the tests', for that fallback).
/// </summary>
enum class MCMoves : uint8_t
{
    InOneStep,
    LookingFirst,
};

/// <summary>The disk.</summary>
std::unique_ptr<MCUpdateDisk> MakeDisk(MCMoves moves = MCMoves::InOneStep);

/// <summary>
/// A file made anew for writing (staging's outputs: an entry unpacked, a file patched): whatever is at its path (a file
/// cut short, a link) is deleted first, a folder refused, then it's created only where nothing is, never through a link
/// (O_EXCL and O_NOFOLLOW; Windows' CREATE_NEW on the reparse point itself), and read and written at any position.
/// </summary>
class MCNewFile
{
public:
    /// <returns>The file, open to read and write, or why it can't be made.</returns>
    static std::expected<MCNewFile, std::string> Create(const std::filesystem::path& path);

    MCNewFile(MCNewFile&& other) noexcept;
    MCNewFile& operator=(MCNewFile&&) = delete;
    MCNewFile(const MCNewFile&) = delete;
    MCNewFile& operator=(const MCNewFile&) = delete;
    ~MCNewFile();

    /// <summary>Writes <paramref name="bytes"/> at <paramref name="at"/>; false when they can't all be (logged).</summary>
    bool WriteAt(uint64_t at, std::span<const char> bytes);

    /// <summary>Reads as many bytes as <paramref name="bytes"/> holds from <paramref name="at"/>; false when they can't all be.</summary>
    bool ReadAt(uint64_t at, std::span<char> bytes);

    /// <summary>Closes it; false when that fails (logged: what was written may not all be there).</summary>
    bool Close();

private:
    MCNewFile(std::filesystem::path path, intptr_t handle) : _Path(std::move(path)), _Handle(handle) {}

    std::filesystem::path _Path;
    /// <summary>The descriptor (the Mac's) or HANDLE (Windows'); <see cref="Closed"/> once closed.</summary>
    intptr_t _Handle;
    static constexpr intptr_t Closed = -1;
};

/// <summary>Whether something is at <paramref name="path"/>, or why it can't be told (MCUpdateDisk::MCStat::Error).</summary>
std::expected<bool, std::string> IsThere(MCUpdateDisk& disk, const std::filesystem::path& path);

/// <summary>
/// Writes <paramref name="path"/> durably: a temporary file beside it written and flushed, renamed over it, the folder
/// flushed. Whatever happens, the file is then either all the old bytes or all the new.
/// </summary>
bool WriteDurably(MCUpdateDisk& disk, const std::filesystem::path& path, std::string_view bytes);
