#include "stdafx.h"
#include "sprite/MCShapeFile.h"
#include "lib/MCPacketFile.h"
#include "sprite/MCHdArt.h"

#include <algorithm>
#include <format>

namespace
{
    /// <summary>
    /// A shape file's (or packet's) <paramref name="bytes"/> read from the game's files with HD art's numbered folders
    /// drawn: noted by the guard as <paramref name="kind"/>, and told to HD art so finding their frames by their hash
    /// doesn't note them again. Without them, nothing: the read is as it was before the pack.
    /// </summary>
    void NoteGameRead(std::string_view kind, std::string_view what, std::span<const uint8_t> bytes)
    {
        if (MCHdArt::NumberedFolders())
        {
            MCHdArt::NoteOriginalRead(kind, what);
            MCHdArt::NoteShapeLoaded(bytes);
        }
    }
}

int32_t MCShapeFile::Open(std::string_view fileName)
{
    Close();
    _Name = fileName;

    // Without HD art's numbered folders the game's file, found and read as before.
    if (const std::optional<uint32_t> size = MCHdArt::NumberedFolders() ? FileSizeOf(fileName) : std::nullopt;
        size.has_value())
    {
        _Pack = MCHdArt::PackShapeFile(fileName, std::nullopt, *size);

        if (_Pack.has_value())
        {
            return NO_ERR;
        }
    }

    return _File.Open(fileName);
}

uint32_t MCShapeFile::GetLength()
{
    return _Pack.has_value() ? static_cast<uint32_t>(_Pack->size()) : _File.GetLength();
}

int32_t MCShapeFile::Read(std::span<uint8_t> buffer)
{
    if (_Pack.has_value())
    {
        const size_t count = std::min(buffer.size(), _Pack->size() - _Position);
        std::copy_n(_Pack->begin() + static_cast<std::ptrdiff_t>(_Position), count, buffer.begin());
        _Position += count;
        MCHdArt::NoteShapeLoaded(*_Pack);
        return static_cast<int32_t>(count);
    }

    const int32_t read = _File.Read(buffer);
    NoteGameRead(FileRead, _Name, buffer.first(static_cast<size_t>(std::max(read, 0))));
    return read;
}

void MCShapeFile::Close()
{
    _Pack.reset();
    _Position = 0;
    _File.Close();
}

int32_t MCShapeFile::ReadPacket(MCPacketFile& file, int32_t packet, std::span<uint8_t> buffer)
{
    const std::string name(file.GetFilename());

    if (const std::optional<std::vector<uint8_t>> pack =
            MCHdArt::NumberedFolders() ? MCHdArt::PackShapeFile(name, packet, static_cast<uint32_t>(buffer.size()))
                                       : std::nullopt;
        pack.has_value())
    {
        std::ranges::copy(*pack, buffer.begin());
        MCHdArt::NoteShapeLoaded(*pack);
        return static_cast<int32_t>(pack->size());
    }

    const int32_t read = file.ReadPacket(packet, buffer);
    NoteGameRead(PacketRead, std::format("{} packet {}", name, packet),
                 buffer.first(static_cast<size_t>(std::max(read, 0))));
    return read;
}
