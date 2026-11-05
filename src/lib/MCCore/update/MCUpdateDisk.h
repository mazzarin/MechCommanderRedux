#pragma once

// Only standard headers and lib/MCSha256.h: MCUpdateDisk.cpp is built without the precompiled header on Windows.
#include "lib/MCSha256.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
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

    /// <summary>Moves a file, replacing what's at <paramref name="to"/>; both on one disk, so it's all or nothing.</summary>
    virtual bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) = 0;

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

/// <summary>The disk.</summary>
std::unique_ptr<MCUpdateDisk> MakeDisk();

/// <summary>
/// Writes <paramref name="path"/> durably: a temporary file beside it written and flushed, renamed over it, the folder
/// flushed. Whatever happens, the file is then either all the old bytes or all the new.
/// </summary>
bool WriteDurably(MCUpdateDisk& disk, const std::filesystem::path& path, std::string_view bytes);
