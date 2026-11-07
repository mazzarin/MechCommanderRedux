#include "stdafx.h"
#include "platform/MCStringTable.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"

namespace
{
    /// <summary>The resource type of string blocks.</summary>
    constexpr uint32_t RT_STRING_TYPE = 6;

    /// <summary>The text file's comment lines start with this.</summary>
    constexpr char TextComment = '#';

    /// <summary>The text file's escapes: the letter after a backslash, and what it stands for.</summary>
    constexpr std::array<std::pair<char, char>, 4> TextEscapes{{{'\\', '\\'}, {'n', '\n'}, {'r', '\r'}, {'t', '\t'}}};

    /// <summary>A text file's line in Windows-1252, its escapes undone, or why it can't be.</summary>
    std::expected<std::string, std::string> unescapeText(std::string_view escaped)
    {
        std::string text;
        text.reserve(escaped.size());
        const char* cursor = escaped.data();
        size_t left = escaped.size();

        while (left > 0)
        {
            if (*cursor == '\\')
            {
                const auto escape = left < 2 ? TextEscapes.end()
                                             : std::ranges::find(TextEscapes, cursor[1], &std::pair<char, char>::first);

                if (escape == TextEscapes.end())
                {
                    return std::unexpected("an unknown escape");
                }

                text.push_back(escape->second);
                cursor += 2;
                left -= 2;
                continue;
            }

            // An invalid sequence comes back as U+FFFD, which Windows-1252 lacks: '?', as Parse's unmapped characters.
            const Uint32 code = SDL_StepUTF8(&cursor, &left);

            if (code == 0)
            {
                return std::unexpected("a NUL character");
            }

            const int windows = MCInput::CodePointToWindows1252(code);
            text.push_back(static_cast<char>(windows < 0 ? '?' : windows));
        }

        return text;
    }

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

std::expected<MCStringTable, std::string> MCStringTable::ParseText(std::string_view text)
{
    MCStringTable table;
    size_t lineNumber = 0;

    while (!text.empty())
    {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        lineNumber++;

        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }

        if (line.empty() || line.front() == TextComment)
        {
            continue;
        }

        const size_t tab = line.find('\t');
        const std::string_view number = line.substr(0, tab == std::string_view::npos ? 0 : tab);
        uint32_t id = 0;
        const auto [numberEnd, numberError] = std::from_chars(number.data(), number.data() + number.size(), id);

        if (number.empty() || numberError != std::errc() || numberEnd != number.data() + number.size())
        {
            return std::unexpected(std::format("line {}: not a number, a tab and a text", lineNumber));
        }

        auto unescaped = unescapeText(line.substr(tab + 1));

        if (!unescaped)
        {
            return std::unexpected(std::format("line {}: {}", lineNumber, unescaped.error()));
        }

        if (!table._Strings.emplace(id, std::move(*unescaped)).second)
        {
            return std::unexpected(std::format("line {}: text {} again", lineNumber, id));
        }
    }

    return table;
}

std::expected<MCStringTable, std::string> MCStringTable::LoadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected("can't open " + MCFileSystem::ToUtf8(path));
    }

    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto table = ParseText(text);

    if (!table)
    {
        return std::unexpected(MCFileSystem::ToUtf8(path) + ", " + table.error());
    }

    return table;
}

std::expected<MCStringTable, std::string> MCStringTable::LoadGame(const std::filesystem::path& exe,
                                                                  const std::filesystem::path& text)
{
    auto fromExe = Load(exe);

    if (fromExe && fromExe->Count() > 0)
    {
        return fromExe;
    }

    auto fromText = LoadText(text);

    if (!fromText)
    {
        const std::string exeProblem =
            fromExe ? MCFileSystem::ToUtf8(exe) + " holds no text" : MCFileSystem::ToUtf8(exe) + ": " + fromExe.error();
        return std::unexpected(std::format("{}, and {}", exeProblem, fromText.error()));
    }

    return fromText;
}

const MCStringTable& MCStringTable::Game()
{
    static const MCStringTable table = []()
    {
        auto loaded = LoadGame(MCFileSystem::Resolve("MCX.EXE"), MCFileSystem::Resolve(TextFileName));

        if (!loaded)
        {
            SDL_Log("Strings: the game's texts can't be read: %s", loaded.error().c_str());
            return MCStringTable();
        }

        return std::move(*loaded);
    }();
    return table;
}
