#pragma once

#include "lib/MCLz.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

// FastFiles (lib/MCFastFile) built in a test's own folder, as the format lays them out.

namespace MCFastFileFixture
{
    /// <summary>A FastFile's directory record per entry: its offset, stored size and real size, then its name.</summary>
    inline constexpr size_t NameBytes = 250;
    inline constexpr size_t RecordBytes = 3 * sizeof(int32_t) + NameBytes;

    /// <summary>One entry of a FastFile built by <see cref="BuildFastFile"/>.</summary>
    struct FastEntry
    {
        std::string Name;
        std::vector<uint8_t> Data;
        bool Packed = false;
    };

    /// <summary><paramref name="value"/>'s bytes, little-endian, after <paramref name="out"/>'s.</summary>
    inline void Put32(std::vector<uint8_t>& out, int32_t value)
    {
        const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(int32_t)>>(value);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    /// <summary>
    /// A FastFile as the format lays it out: the entry count, a directory record per entry (offset, stored size, real
    /// size, name), then the data, each entry raw or LZ-packed.
    /// </summary>
    inline std::vector<uint8_t> BuildFastFile(const std::vector<FastEntry>& entries)
    {
        std::vector<uint8_t> out;
        Put32(out, static_cast<int32_t>(entries.size()));
        std::vector<uint8_t> data;
        const size_t dataStart = sizeof(int32_t) + entries.size() * RecordBytes;

        for (const FastEntry& entry : entries)
        {
            const std::vector<uint8_t> stored = entry.Packed ? LZCompress(entry.Data) : entry.Data;
            Put32(out, static_cast<int32_t>(dataStart + data.size()));
            Put32(out, static_cast<int32_t>(stored.size()));
            Put32(out, static_cast<int32_t>(entry.Data.size()));
            std::array<char, NameBytes> name{};
            std::ranges::copy(entry.Name, name.begin());
            out.insert(out.end(), name.begin(), name.end());
            data.insert(data.end(), stored.begin(), stored.end());
        }

        out.insert(out.end(), data.begin(), data.end());
        return out;
    }
}
