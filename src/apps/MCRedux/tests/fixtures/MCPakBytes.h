#pragma once

#include <random>

#include "lib/MCLz.h"
#include "lib/MCPacketFile.h"

/// <summary>
/// A packet file's bytes laid out by hand, for the tests: each packet's storage bits as given (those the game never
/// writes among them, and a table entry that points anywhere), its bytes as stored, back to back after the table.
/// </summary>
namespace MCPakBytes
{
    /// <summary>One packet as stored.</summary>
    struct MCStored
    {
        /// <summary>The table entry's top 3 bits (an <see cref="MCPacketStorage"/>, or 5 and 6, which name none).</summary>
        uint32_t Storage = 0;
        /// <summary>Its bytes as stored (an LZ packet's size first).</summary>
        std::vector<uint8_t> Bytes;
        /// <summary>Where the table says it starts, if not where its bytes are laid.</summary>
        std::optional<uint32_t> Offset;
    };

    /// <summary>A packet of <paramref name="bytes"/> stored as <paramref name="storage"/>.</summary>
    inline MCStored As(MCPacketStorage storage, std::vector<uint8_t> bytes)
    {
        return MCStored{.Storage = static_cast<uint32_t>(storage), .Bytes = std::move(bytes)};
    }

    /// <summary>A raw packet of <paramref name="bytes"/>.</summary>
    inline MCStored Raw(std::vector<uint8_t> bytes)
    {
        return As(MCPacketStorage::Raw, std::move(bytes));
    }

    /// <summary>An LZ packet of <paramref name="data"/>: the size it gives (its own by default), then its LZ data.</summary>
    inline MCStored Lz(std::span<const uint8_t> data, std::optional<int32_t> size = std::nullopt)
    {
        const std::vector<uint8_t> packed = LZCompress(data);
        const auto sizeBytes = std::bit_cast<std::array<uint8_t, 4>>(size.value_or(static_cast<int32_t>(data.size())));
        std::vector<uint8_t> bytes(sizeBytes.begin(), sizeBytes.end());
        bytes.insert(bytes.end(), packed.begin(), packed.end());
        return As(MCPacketStorage::Lzd, std::move(bytes));
    }

    /// <summary><paramref name="size"/> bytes that differ from packet to packet (<paramref name="seed"/>) and pack a little.</summary>
    inline std::vector<uint8_t> Filled(size_t size, uint32_t seed)
    {
        constexpr size_t RunBytes = 7;
        std::vector<uint8_t> bytes(size);
        std::mt19937 random(seed);

        for (size_t i = 0; i < size; ++i)
        {
            // Runs of a value, as a tile's pixels have: something for LZ to pack.
            bytes[i] = i % RunBytes == 0 ? static_cast<uint8_t>(random()) : bytes[i - 1];
        }

        return bytes;
    }

    /// <summary>
    /// The file: the unchecked magic, the first packet's offset, the table, then the packets' bytes back to back.
    /// </summary>
    inline std::vector<uint8_t> Pak(const std::vector<MCStored>& packets)
    {
        constexpr uint32_t StorageShift = 29;
        constexpr uint32_t HeaderBytes = 8;
        constexpr uint32_t EntryBytes = 4;
        const auto put = [](std::vector<uint8_t>& out, uint32_t value)
        {
            const auto bytes = std::bit_cast<std::array<uint8_t, 4>>(value);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };

        std::vector<uint8_t> file;
        put(file, static_cast<uint32_t>(MCPacketFile::PacketFileMagic));
        const auto first = static_cast<uint32_t>(HeaderBytes + EntryBytes * packets.size());
        put(file, first);
        uint32_t at = first;

        for (const MCStored& packet : packets)
        {
            put(file, packet.Storage << StorageShift | packet.Offset.value_or(at));
            at += static_cast<uint32_t>(packet.Bytes.size());
        }

        for (const MCStored& packet : packets)
        {
            file.insert(file.end(), packet.Bytes.begin(), packet.Bytes.end());
        }

        return file;
    }

    /// <summary>What ReadPacket gives for <paramref name="packet"/>, read alone (sought, then read at its size).</summary>
    inline std::vector<uint8_t> ReadAlone(MCPacketFile& file, int32_t packet)
    {
        file.SeekPacket(packet);
        std::vector<uint8_t> bytes(static_cast<size_t>(std::max(file.GetPacketSize(), 0)));
        file.ReadPacket(packet, bytes);
        return bytes;
    }
}
