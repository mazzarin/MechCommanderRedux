#include "stdafx.h"
#include "lib/MCDiskFile.h"

std::optional<MCDiskFile> MCDiskFile::Open(const std::filesystem::path& path, MCDiskFileMode mode)
{
    // The stream opens the path as the system spells it (UTF-16 on Windows), so any name opens.
    constexpr std::ios::openmode read = std::ios::in | std::ios::binary;
    constexpr std::ios::openmode create = std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary;
    MCDiskFile file;
    file._Stream.open(path, mode == MCDiskFileMode::Create ? create : read);

    if (!file._Stream.is_open())
    {
        return std::nullopt;
    }

    return file;
}

size_t MCDiskFile::Read(std::span<uint8_t> buffer)
{
    _Stream.clear();
    _Stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    return static_cast<size_t>(_Stream.gcount());
}

size_t MCDiskFile::ReadAt(uint32_t offset, std::span<uint8_t> buffer)
{
    _Stream.clear();
    _Stream.seekg(offset);
    return Read(buffer);
}

size_t MCDiskFile::Write(std::span<const uint8_t> data)
{
    _Stream.clear();
    _Stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return _Stream.good() ? data.size() : 0;
}

size_t MCDiskFile::WriteAt(uint32_t offset, std::span<const uint8_t> data)
{
    _Stream.clear();
    _Stream.seekp(offset);
    return Write(data);
}

uint32_t MCDiskFile::Length()
{
    _Stream.clear();
    const std::streampos here = _Stream.tellg();
    _Stream.seekg(0, std::ios::end);
    const auto length = static_cast<uint32_t>(static_cast<std::streamoff>(_Stream.tellg()));
    _Stream.seekg(here);
    return length;
}
