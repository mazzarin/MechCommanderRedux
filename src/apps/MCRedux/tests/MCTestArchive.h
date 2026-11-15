#pragma once

#include "lib/MCFastFile.h"

/// <summary>The tests' HD art archives (FastFiles, the HD art pack's archive layout), written as the pack's tools lay them out.</summary>
namespace MCTestArchive
{
    /// <summary>An archive entry to write: its name, its stored bytes, its unpacked size, and an earlier entry it shares.</summary>
    struct ArchiveEntry
    {
        std::string Name;
        std::vector<uint8_t> Stored;
        size_t RealSize = 0;
        std::optional<size_t> Shares;
    };

    /// <summary>A FastFile of <paramref name="entries"/> (the HD art pack's archive layout).</summary>
    inline std::vector<uint8_t> BuildArchive(const std::vector<ArchiveEntry>& entries)
    {
        std::vector<uint8_t> out(sizeof(int32_t) + entries.size() * sizeof(MCFileEntry));
        const auto count = static_cast<int32_t>(entries.size());
        std::memcpy(out.data(), &count, sizeof(count));
        std::vector<int32_t> offsets;

        for (size_t i = 0; i < entries.size(); i++)
        {
            const ArchiveEntry& entry = entries[i];
            int32_t offset = 0;

            if (entry.Shares.has_value())
            {
                offset = offsets[*entry.Shares];
            }
            else
            {
                offset = static_cast<int32_t>(out.size());
                out.insert(out.end(), entry.Stored.begin(), entry.Stored.end());
            }

            offsets.push_back(offset);
            MCFileEntry record{};
            record.Offset = offset;
            record.Size = static_cast<int32_t>(entry.Shares.has_value() ? entries[*entry.Shares].Stored.size()
                                                                        : entry.Stored.size());
            record.RealSize = static_cast<int32_t>(entry.RealSize);
            std::ranges::copy(entry.Name, record.Name.begin());
            std::memcpy(out.data() + sizeof(int32_t) + i * sizeof(MCFileEntry), &record, sizeof(record));
        }

        return out;
    }

    inline void WriteBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    /// <summary>A bundle entry of <paramref name="name"/> holding <paramref name="bytes"/>, stored raw.</summary>
    inline ArchiveEntry Raw(std::string name, std::vector<uint8_t> bytes)
    {
        const size_t size = bytes.size();
        return {std::move(name), std::move(bytes), size, std::nullopt};
    }
}
