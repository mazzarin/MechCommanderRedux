#include "stdafx.h"
#include "lib/MCFitIniFile.h"

namespace
{
    /// <summary>The first line of every FIT file.</summary>
    constexpr std::string_view FitIniHeader = "FITini";
    /// <summary>The last line of every FIT file.</summary>
    constexpr std::string_view FitIniFooter = "FITend";

    /// <summary>The longest line the reader looks at (the original's line buffer); OB-135 for longer ones.</summary>
    constexpr int32_t MaxLineLength = 0xfe;
    /// <summary>The longest header line read.</summary>
    constexpr int32_t MaxHeaderLength = 0xb;
    /// <summary>The longest array element (the original's word buffer; OB-139).</summary>
    constexpr size_t MaxWordLength = 9;
    /// <summary>The longest element count of an array entry.</summary>
    constexpr size_t MaxCountLength = 9;

    /// <summary>The C library's isspace in the "C" locale.</summary>
    constexpr bool IsSpace(char c)
    {
        return c == ' ' || (c >= '\t' && c <= '\r');
    }

    /// <summary>
    /// The MSVC 5/6 CRT's atol, which the original called: optional spaces and sign, then digits accumulated in a
    /// 32-bit long that wraps on overflow (no clamping as in the modern CRT).
    /// </summary>
    int32_t OriginalAtol(std::string_view text)
    {
        size_t i = 0;

        while (i < text.size() && IsSpace(text[i]))
        {
            ++i;
        }

        const char sign = i < text.size() ? text[i] : '\0';

        if (sign == '-' || sign == '+')
        {
            ++i;
        }

        uint32_t total = 0;

        for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i)
        {
            total = total * 10 + static_cast<uint32_t>(text[i] - '0');
        }

        return sign == '-' ? static_cast<int32_t>(0u - total) : static_cast<int32_t>(total);
    }

    /// <summary>
    /// The hex parse the text-to-number conversions share: after the first "0x" anywhere in the text, the hex digits
    /// up to the first other character, summed from the last one up, each shifted 4 more bits (mod 32, as x86 shifts).
    /// </summary>
    std::optional<uint32_t> ParseHex(std::string_view text)
    {
        const size_t prefix = text.find("0x");

        if (prefix == std::string_view::npos)
        {
            return std::nullopt;
        }

        std::string_view digits = text.substr(prefix + 2);
        digits = digits.substr(
            0, std::ranges::find_if_not(digits, [](char c) { return std::isxdigit(static_cast<uint8_t>(c)) != 0; }) -
                   digits.begin());
        uint32_t value = 0;
        uint32_t shift = 0;

        for (auto c = digits.rbegin(); c != digits.rend(); ++c)
        {
            const char upper = static_cast<char>(std::toupper(static_cast<uint8_t>(*c)));
            const uint32_t digit =
                upper >= 'A' ? static_cast<uint32_t>(upper - 'A' + 10) : static_cast<uint32_t>(upper - '0');
            value += digit << (shift & 0x1f);
            shift += 4;
        }

        return value;
    }

    /// <summary>A number of an entry: hex after "0x", else atol (atof for floats).</summary>
    template <MCFitNumber T> T TextToNumber(std::string_view text)
    {
        if constexpr (std::same_as<T, float>)
        {
            // The original then tried an expression parser that was never written (it gave 0), so -0 reads as 0.
            const float value = static_cast<float>(std::atof(std::string(text).c_str()));
            return value == 0.0f ? 0.0f : value;
        }
        else if (const std::optional<uint32_t> hex = ParseHex(text); hex.has_value())
        {
            return static_cast<T>(*hex);
        }
        else
        {
            return static_cast<T>(OriginalAtol(text));
        }
    }

    /// <summary>The type tag of an entry of <typeparamref name="T"/>.</summary>
    template <MCFitValue T> constexpr std::string_view TypeTag()
    {
        if constexpr (std::same_as<T, float>)
        {
            return "f";
        }
        else if constexpr (std::same_as<T, int32_t>)
        {
            return "l";
        }
        else if constexpr (std::same_as<T, uint32_t>)
        {
            return "ul";
        }
        else if constexpr (std::same_as<T, int16_t>)
        {
            return "s";
        }
        else if constexpr (std::same_as<T, uint16_t>)
        {
            return "us";
        }
        else if constexpr (std::same_as<T, char>)
        {
            return "c";
        }
        else if constexpr (std::same_as<T, uint8_t>)
        {
            return "uc";
        }
        else if constexpr (std::same_as<T, bool>)
        {
            return "b";
        }
        else
        {
            return "st";
        }
    }

    /// <summary>
    /// The next array element of <paramref name="rest"/> (elements are separated by spaces, tabs and commas), and
    /// moves <paramref name="rest"/> past it.
    /// </summary>
    MCFitResult<std::string_view> NextWord(std::string_view& rest)
    {
        constexpr auto isSeparator = [](char c) { return c == ' ' || c == '\t' || c == ','; };

        while (!rest.empty() && isSeparator(rest.front()))
        {
            rest.remove_prefix(1);
        }

        if (rest.empty() || rest.front() == '/')
        {
            return std::unexpected(MCFitError::GetNextLine);
        }

        const size_t length = static_cast<size_t>(std::ranges::find_if(rest, isSeparator) - rest.begin());
        const std::string_view word = rest.substr(0, length);
        rest.remove_prefix(length);

        // Original behaviour (OB-139): longer elements don't fit the word buffer.
        if (word.size() > MaxWordLength)
        {
            return std::unexpected(MCFitError::BufferTooSmall);
        }

        return word;
    }
}

MCFitIniFile::~MCFitIniFile()
{
    Close();
}

std::string MCFitIniFile::ReadFitLine(int32_t maxLength)
{
    std::string line = ReadLine(maxLength);

    if (const size_t end = line.find('\0'); end != std::string::npos)
    {
        line.resize(end);
    }

    return line;
}

bool MCFitIniFile::FindNextBlockStart(std::string& line)
{
    do
    {
        line = ReadFitLine(MaxLineLength);
    } while (!Eof() && !line.starts_with('['));

    return !Eof();
}

int32_t MCFitIniFile::CountBlocks()
{
    int32_t count = 0;
    const uint32_t start = _LogicalPosition;
    std::string line;

    while (FindNextBlockStart(line))
    {
        ++count;
    }

    Seek(static_cast<int32_t>(start), SEEK_SET);
    return count;
}

int32_t MCFitIniFile::AfterOpen()
{
    if (_FileMode == MCFileMode::Create && !_IsChild)
    {
        WriteText(std::format("{} \r\n", FitIniHeader));
        _Blocks.clear();
        return 0;
    }

    if (!ReadFitLine(MaxHeaderLength).contains(FitIniHeader))
    {
        return NOT_A_FITINIFILE;
    }

    const int32_t totalBlocks = CountBlocks();

    // Original behaviour (OB-138): a file without blocks fails to open.
    if (totalBlocks <= 0)
    {
        return std::to_underlying(MCFitError::NoBlocks);
    }

    _Blocks.clear();
    _Blocks.reserve(static_cast<size_t>(totalBlocks));
    std::string line;

    while (FindNextBlockStart(line))
    {
        if (std::cmp_equal(_Blocks.size(), totalBlocks))
        {
            return std::to_underlying(MCFitError::TooManyBlocks);
        }

        // The name between '[' and ']'.
        const size_t close = line.find(']');

        // Port fix: a line without ']' ran past its end.
        if (close == std::string::npos)
        {
            return SYNTAX_ERROR;
        }

        _Blocks.push_back({line.substr(1, close - 1), _LogicalPosition});
    }

    if (std::cmp_not_equal(_Blocks.size(), totalBlocks))
    {
        return std::to_underlying(MCFitError::NotEnoughBlocks);
    }

    return 0;
}

void MCFitIniFile::AtClose()
{
    if (_FileMode == MCFileMode::Create)
    {
        Seek(0, SEEK_END);
        WriteText(std::format("{} \r\n", FitIniFooter));
    }

    _Blocks.clear();
}

int32_t MCFitIniFile::Open(std::string_view fileName, MCFileMode mode)
{
    const MCProbeIo::Timer probeTimer(MCProbeIo::FitNs, MCProbeIo::Fits);
    const int32_t result = MCFile::Open(fileName, mode);

    if (result != 0)
    {
        return result;
    }

    Seek(0, SEEK_SET);
    return AfterOpen();
}

int32_t MCFitIniFile::Open(MCFile& parent, uint32_t length)
{
    // The child's bytes are always read into memory at once.
    const int32_t result = OpenChild(parent, length, true);

    if (result != 0)
    {
        return result;
    }

    return AfterOpen();
}

int32_t MCFitIniFile::Create(std::string_view fileName)
{
    // MCFile::Create opens through the virtual Open, which writes the header.
    return MCFile::Create(fileName);
}

void MCFitIniFile::Close()
{
    AtClose();
    MCFile::Close();
}

int32_t MCFitIniFile::SeekBlock(std::string_view blockId)
{
    CloseIfOrphaned();
    const auto block = std::ranges::find(_Blocks, blockId, &Block::Id);

    if (block == _Blocks.end())
    {
        return BLOCK_NOT_FOUND;
    }

    SeekTo(block);
    return 0;
}

int32_t MCFitIniFile::SeekBlockAt(int32_t index)
{
    CloseIfOrphaned();

    if (index < 0 || index >= GetNumBlocks())
    {
        return BLOCK_NOT_FOUND;
    }

    SeekTo(_Blocks.begin() + index);
    return 0;
}

void MCFitIniFile::SeekTo(std::vector<Block>::const_iterator block)
{
    Seek(static_cast<int32_t>(block->Offset), SEEK_SET);
    _CurrentBlockOffset = block->Offset;
    _CurrentBlockSize = (block + 1 != _Blocks.end() ? (block + 1)->Offset : GetLength()) - _CurrentBlockOffset;
}

MCFitResult<std::string> MCFitIniFile::FindIdLine(std::string_view tag, std::string_view name)
{
    Seek(static_cast<int32_t>(_CurrentBlockOffset), SEEK_SET);
    const uint32_t blockEnd = _CurrentBlockSize + _CurrentBlockOffset;
    const std::string search = std::format("{} {}", tag, name);
    std::string line;
    size_t equals = std::string::npos;

    do
    {
        line = ReadFitLine(MaxLineLength);

        if (MCIStartsWith(line, search))
        {
            size_t after = search.size();

            while (after < line.size() && IsSpace(line[after]))
            {
                ++after;
            }

            if (after < line.size() && line[after] == '=')
            {
                equals = after;
                break;
            }
        }
    } while (_LogicalPosition < blockEnd);

    // Original behaviour (OB-136): a match on the block's last line (the position then being at its end) is missing.
    if (blockEnd <= _LogicalPosition || equals == std::string::npos)
    {
        return std::unexpected(MCFitError::VariableNotFound);
    }

    return line.substr(equals + 1);
}

template <MCFitValue T> MCFitResult<T> MCFitIniFile::Read(std::string_view name)
{
    if constexpr (std::same_as<T, bool>)
    {
        return ReadBooleanValue(name).transform([](int32_t value) { return value != 0; });
    }
    else
    {
        MCFitResult<std::string> text = FindIdLine(TypeTag<T>(), name);

        if (!text.has_value())
        {
            return std::unexpected(text.error());
        }

        if constexpr (std::same_as<T, std::string>)
        {
            const size_t open = text->find('"');

            // Port fix: the original scanned for the quotes without stopping at the end of the line.
            if (open == std::string::npos)
            {
                return std::unexpected(MCFitError::SyntaxError);
            }

            const size_t close = text->find('"', open + 1);
            return text->substr(open + 1, close == std::string::npos ? std::string::npos : close - open - 1);
        }
        else
        {
            return TextToNumber<T>(*text);
        }
    }
}

MCFitResult<int32_t> MCFitIniFile::ReadBooleanValue(std::string_view name)
{
    return FindIdLine("b", name).transform(
        [](const std::string& text)
        {
            const auto first = std::ranges::find_if_not(text, IsSpace);
            return first != text.end() && std::toupper(static_cast<uint8_t>(*first)) == 'T'
                       ? 1
                       : TextToNumber<int32_t>(text);
        });
}

int32_t MCFitIniFile::ReadIdBoolean(std::string_view varName, int& value)
{
    return ToLegacy(ReadBooleanValue(varName), value);
}

int32_t MCFitIniFile::ReadIdString(std::string_view varName, std::span<char> result)
{
    const MCFitResult<std::string> text = Read<std::string>(varName);

    if (!text.has_value())
    {
        return std::to_underlying(text.error());
    }

    if (text->size() >= result.size())
    {
        std::copy_n(text->begin(), result.size(), result.begin());
        return BUFFER_TOO_SMALL;
    }

    std::ranges::copy(*text, result.begin());
    result[text->size()] = 0;
    return 0;
}

int32_t MCFitIniFile::GetIdStringLength(std::string_view varName)
{
    // FindIdLine returns the text after the '='; the original looked for the quotes from the start of the line, and
    // the tag and name hold none.
    const MCFitResult<std::string> text = FindIdLine("st", varName);

    if (!text.has_value())
    {
        return std::to_underlying(text.error());
    }

    const size_t open = text->find('"');
    const size_t close = open == std::string::npos ? std::string::npos : text->find('"', open + 1);

    if (close == std::string::npos)
    {
        return SYNTAX_ERROR;
    }

    return static_cast<int32_t>(close - open - 1) + 1;
}

MCFitResult<MCFitIniFile::ArrayLine> MCFitIniFile::FindArrayLine(std::string_view tag, std::string_view name)
{
    Seek(static_cast<int32_t>(_CurrentBlockOffset), SEEK_SET);
    const uint32_t blockEnd = _CurrentBlockSize + _CurrentBlockOffset;
    // Matched anywhere in the line, with case (so "l[" also matches a "ul[" line).
    const std::string typeText = std::format("{}[", tag);
    const std::string nameText = std::format("] {}", name);
    ArrayLine found;
    size_t typeAt = std::string::npos;
    size_t nameAt = std::string::npos;

    do
    {
        found.Line = ReadFitLine(MaxLineLength);
        typeAt = found.Line.find(typeText);
        nameAt = found.Line.find(nameText);

        if (typeAt != std::string::npos && nameAt != std::string::npos)
        {
            break;
        }
    } while (_LogicalPosition < blockEnd);

    // Original behaviour (OB-136): an array on the block's last line is missing.
    if (blockEnd <= _LogicalPosition || typeAt == std::string::npos || nameAt == std::string::npos)
    {
        return std::unexpected(MCFitError::VariableNotFound);
    }

    // The element count between "l[" and "] name".
    const size_t countAt = typeAt + typeText.size();

    // Port fix: "] name" before the type gave a negative length the original passed to strncpy.
    if (nameAt < countAt)
    {
        return std::unexpected(MCFitError::SyntaxError);
    }

    if (nameAt - countAt > MaxCountLength)
    {
        return std::unexpected(MCFitError::TooManyElements);
    }

    found.Count = TextToNumber<uint32_t>(std::string_view(found.Line).substr(countAt, nameAt - countAt));
    return found;
}

template <MCFitNumber T> MCFitResult<uint32_t> MCFitIniFile::ArraySize(std::string_view name)
{
    return FindArrayLine(TypeTag<T>(), name).transform([](const ArrayLine& found) { return found.Count; });
}

template <MCFitNumber T> MCFitResult<uint32_t> MCFitIniFile::ReadArray(std::string_view name, std::span<T> values)
{
    MCFitResult<ArrayLine> found = FindArrayLine(TypeTag<T>(), name);

    if (!found.has_value())
    {
        return std::unexpected(found.error());
    }

    if (values.size() < found->Count)
    {
        return std::unexpected(MCFitError::UserArrayTooSmall);
    }

    std::string line = std::move(found->Line);
    const size_t equals = line.find('=');

    if (equals == std::string::npos)
    {
        return std::unexpected(MCFitError::SyntaxError);
    }

    const uint32_t blockEnd = _CurrentBlockSize + _CurrentBlockOffset;
    std::string_view rest = std::string_view(line).substr(equals + 1);
    uint32_t count = 0;

    // The elements may go on over the following lines.
    while (_LogicalPosition < blockEnd && count < found->Count)
    {
        const MCFitResult<std::string_view> word = NextWord(rest);

        if (word.has_value())
        {
            values[count++] = TextToNumber<T>(*word);
        }
        else if (word.error() == MCFitError::GetNextLine)
        {
            line = ReadFitLine(MaxLineLength);
            rest = line;
        }
        else
        {
            return std::unexpected(word.error());
        }
    }

    // Original behaviour (OB-137): elements on the block's last line aren't read (the loop tests the position first).
    if (count < found->Count)
    {
        return std::unexpected(MCFitError::NotEnoughElementsForArray);
    }

    return count;
}

template <MCFitNumber T> MCFitResult<std::vector<T>> MCFitIniFile::ReadArray(std::string_view name)
{
    const MCFitResult<uint32_t> size = ArraySize<T>(name);

    if (!size.has_value())
    {
        return std::unexpected(size.error());
    }

    std::vector<T> values(*size);
    return ReadArray(name, std::span(values)).transform([&](uint32_t) { return std::move(values); });
}

// The entry types (MCFitNumber, MCFitValue).
template MCFitResult<float> MCFitIniFile::Read<float>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<float>(std::string_view);
template MCFitResult<std::vector<float>> MCFitIniFile::ReadArray<float>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<float>(std::string_view, std::span<float>);
template MCFitResult<int32_t> MCFitIniFile::Read<int32_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<int32_t>(std::string_view);
template MCFitResult<std::vector<int32_t>> MCFitIniFile::ReadArray<int32_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<int32_t>(std::string_view, std::span<int32_t>);
template MCFitResult<uint32_t> MCFitIniFile::Read<uint32_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<uint32_t>(std::string_view);
template MCFitResult<std::vector<uint32_t>> MCFitIniFile::ReadArray<uint32_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<uint32_t>(std::string_view, std::span<uint32_t>);
template MCFitResult<int16_t> MCFitIniFile::Read<int16_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<int16_t>(std::string_view);
template MCFitResult<std::vector<int16_t>> MCFitIniFile::ReadArray<int16_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<int16_t>(std::string_view, std::span<int16_t>);
template MCFitResult<uint16_t> MCFitIniFile::Read<uint16_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<uint16_t>(std::string_view);
template MCFitResult<std::vector<uint16_t>> MCFitIniFile::ReadArray<uint16_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<uint16_t>(std::string_view, std::span<uint16_t>);
template MCFitResult<char> MCFitIniFile::Read<char>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<char>(std::string_view);
template MCFitResult<std::vector<char>> MCFitIniFile::ReadArray<char>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<char>(std::string_view, std::span<char>);
template MCFitResult<uint8_t> MCFitIniFile::Read<uint8_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ArraySize<uint8_t>(std::string_view);
template MCFitResult<std::vector<uint8_t>> MCFitIniFile::ReadArray<uint8_t>(std::string_view);
template MCFitResult<uint32_t> MCFitIniFile::ReadArray<uint8_t>(std::string_view, std::span<uint8_t>);
template MCFitResult<bool> MCFitIniFile::Read<bool>(std::string_view);
template MCFitResult<std::string> MCFitIniFile::Read<std::string>(std::string_view);

int32_t MCFitIniFile::WriteText(std::string_view text)
{
    return Write(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

int32_t MCFitIniFile::WriteBlock(std::string_view blockId)
{
    return WriteText(std::format("\r\n[{}]\r\n", blockId));
}

int32_t MCFitIniFile::WriteIdFloat(std::string_view varName, float value)
{
    return WriteText(std::format("f {}={:f}\r\n", varName, value));
}

int32_t MCFitIniFile::WriteIdBoolean(std::string_view varName, bool value)
{
    return WriteText(std::format("b {}={}\r\n", varName, value ? "TRUE" : "FALSE"));
}

int32_t MCFitIniFile::WriteIdLong(std::string_view varName, int32_t value)
{
    return WriteText(std::format("l {}={}\r\n", varName, value));
}

int32_t MCFitIniFile::WriteIdShort(std::string_view varName, int16_t value)
{
    return WriteText(std::format("s {}={}\r\n", varName, value));
}

int32_t MCFitIniFile::WriteIdChar(std::string_view varName, char value)
{
    return WriteText(std::format("c {}={}\r\n", varName, static_cast<int>(value)));
}

int32_t MCFitIniFile::WriteIdULong(std::string_view varName, uint32_t value)
{
    return WriteText(std::format("ul {}={}\r\n", varName, static_cast<int32_t>(value)));
}

int32_t MCFitIniFile::WriteIdUShort(std::string_view varName, uint16_t value)
{
    return WriteText(std::format("us {}={}\r\n", varName, value));
}

int32_t MCFitIniFile::WriteIdUChar(std::string_view varName, uint8_t value)
{
    return WriteText(std::format("uc {}={}\r\n", varName, value));
}

int32_t MCFitIniFile::WriteIdString(std::string_view varName, std::string_view text)
{
    return WriteText(std::format("st {}=\"{}\"\r\n", varName, text));
}

int32_t MCFitIniFile::WriteIdUShortArray(std::string_view varName, std::span<const uint16_t> values)
{
    std::string text = std::format("us[{}] {}=", values.size(), varName);

    for (const uint16_t value : values)
    {
        std::format_to(std::back_inserter(text), "{},", value);
    }

    return WriteText(text + "\r\n");
}

int32_t MCFitIniFile::WriteIdLongArray(std::string_view varName, std::span<const int32_t> values)
{
    std::string text = std::format("l[{}] {}=", values.size(), varName);

    for (const int32_t value : values)
    {
        std::format_to(std::back_inserter(text), "{},", value);
    }

    return WriteText(text + "\r\n");
}

int32_t MCFitIniFile::WriteIdFloatArray(std::string_view varName, std::span<const float> values)
{
    std::string text = std::format("f[{}] {}=", values.size(), varName);

    for (size_t i = 0; i < values.size(); ++i)
    {
        std::format_to(std::back_inserter(text), "{}{:.2f},", i == 0 ? "" : " ", values[i]);
    }

    return WriteText(text + "\r\n");
}

int32_t MCFitIniFile::WriteIdUCharArray(std::string_view varName, std::span<const uint8_t> values)
{
    std::string text = std::format("uc[{}] {}=", values.size(), varName);

    for (size_t i = 0; i < values.size(); ++i)
    {
        std::format_to(std::back_inserter(text), "{}{},", i == 0 ? "" : " ", values[i]);
    }

    return WriteText(text + "\r\n");
}
