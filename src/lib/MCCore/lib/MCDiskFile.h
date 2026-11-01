#pragma once

/// <summary>How an <see cref="MCDiskFile"/> is opened.</summary>
enum class MCDiskFileMode : uint8_t
{
    /// <summary>Read an existing file.</summary>
    Read,
    /// <summary>Create (truncate) a file, then write and read it.</summary>
    Create
};

/// <summary>An open file on disk (a <c>std::fstream</c>), closed when destroyed.</summary>
class MCDiskFile
{
public:
    /// <summary>Opens <paramref name="path"/>.</summary>
    /// <returns>The open file, or nothing when it couldn't be opened.</returns>
    static std::optional<MCDiskFile> Open(const std::filesystem::path& path, MCDiskFileMode mode);

    /// <summary>Reads up to <paramref name="buffer"/>'s size from the current position.</summary>
    /// <returns>The number of bytes read.</returns>
    size_t Read(std::span<uint8_t> buffer);

    /// <summary>Reads up to <paramref name="buffer"/>'s size at <paramref name="offset"/>.</summary>
    /// <returns>The number of bytes read.</returns>
    size_t ReadAt(uint32_t offset, std::span<uint8_t> buffer);

    /// <summary>Writes <paramref name="data"/> at the current position.</summary>
    /// <returns>The number of bytes written.</returns>
    size_t Write(std::span<const uint8_t> data);

    /// <summary>Writes <paramref name="data"/> at <paramref name="offset"/>.</summary>
    /// <returns>The number of bytes written.</returns>
    size_t WriteAt(uint32_t offset, std::span<const uint8_t> data);

    /// <summary>The file's length in bytes (the position is kept).</summary>
    uint32_t Length();

private:
    MCDiskFile() = default;

    /// <summary>The open file.</summary>
    std::fstream _Stream;
};
