#pragma once

#include "lib/MCFile.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class MCPacketFile;

/// <summary>
/// Port-only: a shape file or picture opened for its loader to read whole (an effect's, a marker's, a shadow's, a damage
/// diagram's, a menu's TGA or ART.PAK picture). With HD art's numbered folders drawn, it's the HD pack's copy of the original's bytes
/// (MCHdArt::PackShapeFile), so the loader reads nothing of the original's; else the game's file as MCFile opens it, a
/// read the HD guard notes. It opens, sizes and reads as MCFile does.
/// </summary>
class MCShapeFile
{
public:
    /// <summary>The HD guard's kind of read for a shape file read from the game's files.</summary>
    static constexpr std::string_view FileRead = "a shape file or picture read from the game's files";
    /// <summary>The HD guard's kind of read for a PAK's shape packet read from the game's files.</summary>
    static constexpr std::string_view PacketRead = "a PAK's shape packet or picture read from the game's files";

    MCShapeFile() = default;
    MCShapeFile(const MCShapeFile&) = delete;
    MCShapeFile& operator=(const MCShapeFile&) = delete;

    /// <summary>Opens <paramref name="fileName"/> to read: the pack's copy, else the game's file.</summary>
    /// <returns>NO_ERR, or MCFile::Open's error.</returns>
    int32_t Open(std::string_view fileName);

    /// <summary>The file's length in bytes.</summary>
    uint32_t GetLength();

    /// <summary>The file's length in bytes (the same as <see cref="GetLength"/>).</summary>
    uint32_t FileSize() { return GetLength(); }

    /// <summary>Reads on from where the last read ended into <paramref name="buffer"/>, as MCFile::Read does.</summary>
    /// <returns>The bytes read.</returns>
    int32_t Read(std::span<uint8_t> buffer);

    /// <summary>Closes it.</summary>
    void Close();

    /// <summary>
    /// Reads packet <paramref name="packet"/> of PAK <paramref name="file"/> into <paramref name="buffer"/>, sized as
    /// the packet is: the pack's copy of it with HD art's numbered folders drawn, else the PAK's
    /// (MCPacketFile::ReadPacket, a read the HD guard notes).
    /// </summary>
    /// <returns>The bytes read, as MCPacketFile::ReadPacket gives them.</returns>
    static int32_t ReadPacket(MCPacketFile& file, int32_t packet, std::span<uint8_t> buffer);

private:
    /// <summary>The pack's copy of the file, when it's read from the pack.</summary>
    std::optional<std::vector<uint8_t>> _Pack;
    /// <summary>Where the next read of the pack's copy starts.</summary>
    size_t _Position = 0;
    /// <summary>The game's file, when it's read from there.</summary>
    MCFile _File;
    /// <summary>The file's name as opened (the guard's note).</summary>
    std::string _Name;
};
