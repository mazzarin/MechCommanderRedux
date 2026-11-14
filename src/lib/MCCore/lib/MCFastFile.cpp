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

    /// <summary><paramref name="entry"/>'s stored bytes, read through <paramref name="file"/> (from any thread).</summary>
    std::vector<uint8_t> StoredBytes(const MCFileEntry& entry, const MCDiskFile& file)
    {
        std::vector<uint8_t> stored(static_cast<size_t>(std::max(entry.Size, 0)));
        stored.resize(file.ReadShared(static_cast<uint32_t>(entry.Offset), stored));
        return stored;
    }
}

std::expected<std::unique_ptr<MCFastFile>, std::string> MCFastFile::Create(std::string_view fileName)
{
    auto fastFile = std::make_unique<MCFastFile>(Key());
    fastFile->_Path = MCFileSystem::Resolve(fileName);
    std::optional<MCDiskFile> opened = MCDiskFile::Open(fastFile->_Path, MCDiskFileMode::Read);

    if (!opened.has_value())
    {
        return std::unexpected(std::format("{}: not found", fileName));
    }

    fastFile->_Handle = std::make_shared<MCDiskFile>(std::move(*opened));
    const MCDiskFile& file = *fastFile->_Handle;
    std::array<uint8_t, sizeof(int32_t)> countBytes{};

    // Read without the window (a 64 KB fill for 4 bytes): nothing reads the archive a little at a time.
    if (file.ReadShared(0, countBytes) != countBytes.size())
    {
        return std::unexpected(std::format("{}: no directory", fileName));
    }

    // Issue #25: the directory must lie in the file (the original allocated whatever the count said).
    const int32_t count = std::bit_cast<int32_t>(countBytes);
    const uint32_t length = file.Length();
    fastFile->_Length = length;
    std::error_code timeError;
    fastFile->_Written = std::filesystem::last_write_time(fastFile->_Path, timeError);

    if (count < 0 || static_cast<uint64_t>(count) * sizeof(MCFileEntry) > length - std::min<uint64_t>(length, 4))
    {
        return std::unexpected(std::format("{}: a directory of {} entries", fileName, count));
    }

    // The directory in one read (an HD archive has thousands of entries). The original ignores the byte count; a
    // short directory leaves the rest zeroed.
    std::vector<MCFileEntry>& entries = fastFile->_Entries;
    entries.assign(static_cast<size_t>(count), MCFileEntry{});
    file.ReadShared(static_cast<uint32_t>(countBytes.size()),
                    std::span(reinterpret_cast<uint8_t*>(entries.data()), entries.size() * sizeof(MCFileEntry)));
    fastFile->_Index.reserve(entries.size());

    for (size_t i = 0; i < entries.size(); ++i)
    {
        MCFileEntry& entry = entries[i];
        entry.Name.back() = 0;

        // Its stored bytes lie in the archive, and it unpacks to no more than its LZ data can (each 12-bit code
        // gives at most a 4096-byte string); readers allocate the unpacked size.
        const int64_t stored = entry.Size;
        const int64_t unpacked = entry.RealSize;

        if (entry.Offset < 0 || stored < 0 || entry.Offset + stored > static_cast<int64_t>(length) || unpacked < 0 ||
            unpacked > (stored == unpacked ? stored : stored * 4096))
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

std::shared_ptr<MCDiskFile> MCFastFile::OpenAgain() const
{
    // A file changed since (another written in its place) would be read by the old directory's offsets.
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(_Path, error);

    if (error || written != _Written)
    {
        return nullptr;
    }

    std::optional<MCDiskFile> file = MCDiskFile::Open(_Path, MCDiskFileMode::Read);
    return file.has_value() && file->Length() == _Length ? std::make_shared<MCDiskFile>(std::move(*file)) : nullptr;
}

std::shared_ptr<MCDiskFile> MCFastFile::Reopened(std::shared_ptr<MCDiskFile> opened)
{
    if (_Handle == nullptr)
    {
        _Handle = std::move(opened);
    }

    return _Handle;
}

std::shared_ptr<MCDiskFile> MCFastFile::File()
{
    // Released: opened again only if it is the file the directory was read from.
    return _Handle != nullptr ? _Handle : Reopened(OpenAgain());
}

std::vector<uint8_t> MCFastFile::ReadStored(int32_t index, const MCDiskFile& file) const
{
    return StoredBytes(_Entries.at(static_cast<size_t>(index)), file);
}

int32_t MCFastFile::ReadEntry(int32_t index, std::span<uint8_t> data)
{
    const MCFileEntry& entry = _Entries.at(static_cast<size_t>(index));
    const std::shared_ptr<MCDiskFile> opened = File();

    if (opened == nullptr)
    {
        return 0;
    }

    const MCDiskFile& file = *opened;
    data = data.first(std::min(data.size(), static_cast<size_t>(std::max(entry.RealSize, 0))));
    const auto offset = static_cast<uint32_t>(entry.Offset);

    if (entry.Size == entry.RealSize)
    {
        return static_cast<int32_t>(file.ReadShared(offset, data));
    }

    // Packed: read the whole stored entry and unpack it (no more than the archive holds past the entry's start).
    const uint32_t stored = file.Length() - std::min(file.Length(), offset);
    std::vector<uint8_t> packed(std::min<size_t>(static_cast<size_t>(std::max(entry.Size, 0)), stored));
    packed.resize(file.ReadShared(offset, packed));
    const int32_t unpacked = LZDecomp(data, packed);
    return unpacked == entry.RealSize ? unpacked : 0;
}
