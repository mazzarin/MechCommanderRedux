#include "stdafx.h"
#include "platform/MCStringTable.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"

namespace
{
    /// <summary>The resource type of string blocks.</summary>
    constexpr uint32_t RT_STRING_TYPE = 6;

    /// <summary>A bounds-checked little-endian reader over the image.</summary>
    struct MCImageReader
    {
        std::span<const uint8_t> Image;

        bool Has(size_t offset, size_t size) const { return offset <= Image.size() && size <= Image.size() - offset; }

        uint16_t U16(size_t offset) const { return static_cast<uint16_t>(Image[offset] | (Image[offset + 1] << 8)); }

        uint32_t U32(size_t offset) const
        {
            return static_cast<uint32_t>(Image[offset]) | (static_cast<uint32_t>(Image[offset + 1]) << 8) |
                   (static_cast<uint32_t>(Image[offset + 2]) << 16) | (static_cast<uint32_t>(Image[offset + 3]) << 24);
        }
    };

    /// <summary>A section of the image: where its RVAs live in the file.</summary>
    struct MCSection
    {
        uint32_t VirtualAddress = 0;
        uint32_t VirtualSize = 0;
        uint32_t RawOffset = 0;
        uint32_t RawSize = 0;
    };

    /// <summary>The file offset of <paramref name="rva"/>, or nothing when no section holds it.</summary>
    std::optional<size_t> rvaToOffset(const std::vector<MCSection>& sections, uint32_t rva)
    {
        for (const MCSection& section : sections)
        {
            uint32_t size = std::max(section.VirtualSize, section.RawSize);

            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < size)
            {
                uint32_t delta = rva - section.VirtualAddress;

                if (delta >= section.RawSize)
                {
                    return std::nullopt;
                }

                return static_cast<size_t>(section.RawOffset) + delta;
            }
        }

        return std::nullopt;
    }

    /// <summary>The entries of the resource directory at <paramref name="offset"/>: (id, offset field) pairs.</summary>
    std::vector<std::pair<uint32_t, uint32_t>> directoryEntries(const MCImageReader& reader, size_t offset)
    {
        std::vector<std::pair<uint32_t, uint32_t>> entries;

        if (!reader.Has(offset, 16))
        {
            return entries;
        }

        size_t count = static_cast<size_t>(reader.U16(offset + 12)) + reader.U16(offset + 14);

        for (size_t i = 0; i < count; i++)
        {
            size_t entry = offset + 16 + i * 8;

            if (!reader.Has(entry, 8))
            {
                break;
            }

            entries.emplace_back(reader.U32(entry), reader.U32(entry + 4));
        }

        return entries;
    }
}

std::expected<MCStringTable, std::string> MCStringTable::Parse(std::span<const uint8_t> image)
{
    MCImageReader reader{image};

    if (!reader.Has(0, 0x40) || image[0] != 'M' || image[1] != 'Z')
    {
        return std::unexpected("not an MZ executable");
    }

    size_t pe = reader.U32(0x3c);

    if (!reader.Has(pe, 24) || reader.U32(pe) != 0x00004550)
    {
        return std::unexpected("no PE header");
    }

    size_t coff = pe + 4;
    uint16_t numSections = reader.U16(coff + 2);
    uint16_t optionalSize = reader.U16(coff + 16);
    size_t optional = coff + 20;

    if (!reader.Has(optional, optionalSize) || optionalSize < 2)
    {
        return std::unexpected("truncated optional header");
    }

    uint16_t magic = reader.U16(optional);
    size_t directories = optional + (magic == 0x20b ? 112 : 96);

    if (!reader.Has(directories + 2 * 8, 8))
    {
        return std::unexpected("no resource directory entry");
    }

    uint32_t resourceRva = reader.U32(directories + 2 * 8);

    std::vector<MCSection> sections;
    size_t sectionTable = optional + optionalSize;

    for (size_t i = 0; i < numSections; i++)
    {
        size_t header = sectionTable + i * 40;

        if (!reader.Has(header, 40))
        {
            return std::unexpected("truncated section table");
        }

        MCSection section;
        section.VirtualSize = reader.U32(header + 8);
        section.VirtualAddress = reader.U32(header + 12);
        section.RawSize = reader.U32(header + 16);
        section.RawOffset = reader.U32(header + 20);
        sections.push_back(section);
    }

    MCStringTable table;

    if (resourceRva == 0)
    {
        return table;
    }

    std::optional<size_t> root = rvaToOffset(sections, resourceRva);

    if (!root)
    {
        return std::unexpected("resource directory outside the sections");
    }

    for (auto [typeId, typeOffset] : directoryEntries(reader, *root))
    {
        if ((typeId & 0x80000000u) != 0 || typeId != RT_STRING_TYPE || (typeOffset & 0x80000000u) == 0)
        {
            continue;
        }

        for (auto [blockId, blockOffset] : directoryEntries(reader, *root + (typeOffset & 0x7fffffffu)))
        {
            if ((blockId & 0x80000000u) != 0 || (blockOffset & 0x80000000u) == 0)
            {
                continue;
            }

            auto languages = directoryEntries(reader, *root + (blockOffset & 0x7fffffffu));

            if (languages.empty() || (languages[0].second & 0x80000000u) != 0)
            {
                continue;
            }

            size_t dataEntry = *root + languages[0].second;

            if (!reader.Has(dataEntry, 8))
            {
                continue;
            }

            std::optional<size_t> data = rvaToOffset(sections, reader.U32(dataEntry));
            uint32_t size = reader.U32(dataEntry + 4);

            if (!data || !reader.Has(*data, size))
            {
                continue;
            }

            size_t position = *data;
            size_t end = *data + size;

            for (uint32_t i = 0; i < 16 && position + 2 <= end; i++)
            {
                uint16_t length = reader.U16(position);
                position += 2;

                if (position + length * 2u > end)
                {
                    break;
                }

                if (length != 0)
                {
                    std::string text;
                    text.reserve(length);

                    for (uint16_t c = 0; c < length; c++)
                    {
                        int code = MCInput::CodePointToWindows1252(reader.U16(position + c * 2u));
                        text.push_back(static_cast<char>(code < 0 ? '?' : code));
                    }

                    table._Strings[(blockId - 1) * 16 + i] = std::move(text);
                }

                position += length * 2u;
            }
        }
    }

    return table;
}

std::expected<MCStringTable, std::string> MCStringTable::Load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected("can't open " + MCFileSystem::ToUtf8(path));
    }

    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return Parse(image);
}

std::optional<std::string> MCStringTable::Find(uint32_t id) const
{
    auto found = _Strings.find(id);
    return found == _Strings.end() ? std::nullopt : std::optional<std::string>(found->second);
}

int32_t MCStringTable::LoadString(uint32_t id, std::span<char> buffer) const
{
    if (buffer.empty())
    {
        return 0;
    }

    const auto found = _Strings.find(id);

    if (found == _Strings.end())
    {
        buffer[0] = '\0';
        return 0;
    }

    const std::string& text = found->second;
    const size_t count = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), count);
    buffer[count] = '\0';
    return static_cast<int32_t>(count);
}

const MCStringTable& MCStringTable::Game()
{
    static const MCStringTable table = []()
    {
        auto loaded = Load(MCFileSystem::Resolve("MCX.EXE"));
        return loaded ? std::move(*loaded) : MCStringTable();
    }();
    return table;
}
