#include "stdafx.h"
#include "lib/MCFastFile.h"
#include "lib/MCLz.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>The name in the case the index keys use.</summary>
    std::string IndexKey(std::string_view name)
    {
        std::string key(name);

        for (char& c : key)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }

        return key;
    }
}

std::expected<std::unique_ptr<MCFastFile>, std::string> MCFastFile::Create(std::string_view fileName)
{
    auto fastFile = std::make_unique<MCFastFile>(Key());
    fastFile->_Handle = MCDiskFile::Open(MCFileSystem::Resolve(fileName), MCDiskFileMode::Read);

    if (!fastFile->_Handle.has_value())
    {
        return std::unexpected(std::format("{}: not found", fileName));
    }

    MCDiskFile& file = *fastFile->_Handle;
    std::array<uint8_t, sizeof(int32_t)> countBytes{};

    if (file.Read(countBytes) != countBytes.size())
    {
        return std::unexpected(std::format("{}: no directory", fileName));
    }

    // Issue #25: the directory must lie in the file (the original allocated whatever the count said).
    const int32_t count = std::bit_cast<int32_t>(countBytes);

    if (count < 0 ||
        static_cast<uint64_t>(count) * sizeof(MCFileEntry) > file.Length() - std::min<uint64_t>(file.Length(), 4))
    {
        return std::unexpected(std::format("{}: a directory of {} entries", fileName, count));
    }

    fastFile->_Entries.assign(static_cast<size_t>(count), MCFileEntry{});

    for (size_t i = 0; i < fastFile->_Entries.size(); ++i)
    {
        // The original ignores the byte count; a short directory leaves the rest of the entry zeroed.
        std::array<uint8_t, sizeof(MCFileEntry)> entryBytes{};
        file.Read(entryBytes);
        MCFileEntry& entry = fastFile->_Entries[i];
        entry = std::bit_cast<MCFileEntry>(entryBytes);
        entry.Name.back() = 0;

        // Its stored bytes lie in the archive, and it unpacks to no more than its LZ data can (each 12-bit code
        // gives at most a 4096-byte string); readers allocate the unpacked size.
        const int64_t stored = entry.Size;
        const int64_t unpacked = entry.RealSize;

        if (entry.Offset < 0 || stored < 0 || entry.Offset + stored > static_cast<int64_t>(file.Length()) ||
            unpacked < 0 || unpacked > (stored == unpacked ? stored : stored * 4096))
        {
            return std::unexpected(
                std::format("{}: entry {} ({}) lies outside the archive", fileName, i, entry.GetName()));
        }

        fastFile->_Index.try_emplace(IndexKey(entry.GetName()), static_cast<int32_t>(i));
    }

    return fastFile;
}

std::optional<int32_t> MCFastFile::Find(std::string_view gamePath) const
{
    if (const auto found = _Index.find(IndexKey(gamePath)); found != _Index.end())
    {
        return found->second;
    }

    return std::nullopt;
}

std::vector<uint8_t> MCFastFile::ReadStored(int32_t index)
{
    const MCFileEntry& entry = _Entries.at(static_cast<size_t>(index));
    std::vector<uint8_t> stored(static_cast<size_t>(std::max(entry.Size, 0)));
    stored.resize(_Handle->ReadAt(static_cast<uint32_t>(entry.Offset), stored));
    return stored;
}

int32_t MCFastFile::ReadEntry(int32_t index, std::span<uint8_t> data)
{
    const MCFileEntry& entry = _Entries.at(static_cast<size_t>(index));
    data = data.first(std::min(data.size(), static_cast<size_t>(std::max(entry.RealSize, 0))));
    const auto offset = static_cast<uint32_t>(entry.Offset);

    if (entry.Size == entry.RealSize)
    {
        return static_cast<int32_t>(_Handle->ReadAt(offset, data));
    }

    // Packed: read the whole stored entry and unpack it (no more than the archive holds past the entry's start).
    const uint32_t stored = _Handle->Length() - std::min(_Handle->Length(), offset);
    std::vector<uint8_t> packed(std::min<size_t>(static_cast<size_t>(std::max(entry.Size, 0)), stored));
    packed.resize(_Handle->ReadAt(offset, packed));
    const int32_t unpacked = LZDecomp(data, packed);
    return unpacked == entry.RealSize ? unpacked : 0;
}
