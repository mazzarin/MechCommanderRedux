#pragma once

#include "lib/MCFile.h"

// Original source: mcx\lib\inifile.cpp (and lib\inifile.h). The FIT text-table format every game data file uses:
//
//   FITini
//   [Block]
//   l Name = 42              // typed entries: f l ul s us c uc b st, one per line
//   f[3] Values = 1.0, 2.5, 3
//   FITend

/// <summary>Why a FIT file couldn't be opened or an entry read. The values are the original's (0xFADA00xx).</summary>
enum class MCFitError : int32_t
{
    /// <summary>No block of that name.</summary>
    BlockNotFound = static_cast<int32_t>(0xFADA0000),
    /// <summary>A value or word doesn't fit the caller's buffer.</summary>
    BufferTooSmall = static_cast<int32_t>(0xFADA0003),
    /// <summary>The file doesn't start with <c>FITini</c>.</summary>
    NotAFitIniFile = static_cast<int32_t>(0xFADA0004),
    /// <summary>The file has no blocks (OB-138; the original's name: NO_RAM_FOR_INI_BLOCKS).</summary>
    NoBlocks = static_cast<int32_t>(0xFADA0005),
    /// <summary>The end of the file was reached looking for a block.</summary>
    NoMoreBlocks = static_cast<int32_t>(0xFADA0006),
    /// <summary>More blocks were found than counted.</summary>
    TooManyBlocks = static_cast<int32_t>(0xFADA0007),
    /// <summary>Fewer blocks were found than counted.</summary>
    NotEnoughBlocks = static_cast<int32_t>(0xFADA0008),
    /// <summary>The current block has no entry of that type and name.</summary>
    VariableNotFound = static_cast<int32_t>(0xFADA0009),
    /// <summary>A line is malformed (no quotes, an unclosed block name, no <c>=</c> after an array).</summary>
    SyntaxError = static_cast<int32_t>(0xFADA000A),
    /// <summary>An array ran out of lines before all its elements were read.</summary>
    NotEnoughElementsForArray = static_cast<int32_t>(0xFADA000B),
    /// <summary>Nothing more on this line (the end, or a <c>/</c> comment); internal to the array reader.</summary>
    GetNextLine = static_cast<int32_t>(0xFADA000C),
    /// <summary>The array in the file is bigger than the caller's.</summary>
    UserArrayTooSmall = static_cast<int32_t>(0xFADA000D),
    /// <summary>An array's element count is more than nine characters long.</summary>
    TooManyElements = static_cast<int32_t>(0xFADA000E)
};

// The same codes as int32_t, for the game's int32_t result chains (and the ReadId* methods below).
inline constexpr int32_t BLOCK_NOT_FOUND = std::to_underlying(MCFitError::BlockNotFound);
inline constexpr int32_t BUFFER_TOO_SMALL = std::to_underlying(MCFitError::BufferTooSmall);
inline constexpr int32_t NOT_A_FITINIFILE = std::to_underlying(MCFitError::NotAFitIniFile);
inline constexpr int32_t VARIABLE_NOT_FOUND = std::to_underlying(MCFitError::VariableNotFound);
inline constexpr int32_t SYNTAX_ERROR = std::to_underlying(MCFitError::SyntaxError);
inline constexpr int32_t USER_ARRAY_TOO_SMALL = std::to_underlying(MCFitError::UserArrayTooSmall);

/// <summary>A FIT read's value, or why there is none.</summary>
template <typename T> using MCFitResult = std::expected<T, MCFitError>;

/// <summary>
/// The types a FIT entry holds, by their type tag: <c>f</c> float, <c>l</c> int32_t, <c>ul</c> uint32_t, <c>s</c>
/// int16_t, <c>us</c> uint16_t, <c>c</c> char, <c>uc</c> uint8_t.
/// </summary>
template <typename T>
concept MCFitNumber =
    std::same_as<T, float> || std::same_as<T, int32_t> || std::same_as<T, uint32_t> || std::same_as<T, int16_t> ||
    std::same_as<T, uint16_t> || std::same_as<T, char> || std::same_as<T, uint8_t>;

/// <summary>The types a scalar FIT entry holds: the numbers, <c>b</c> bool and <c>st</c> std::string.</summary>
template <typename T>
concept MCFitValue = MCFitNumber<T> || std::same_as<T, bool> || std::same_as<T, std::string>;

/// <summary>
/// A FIT file: a <see cref="MCFile"/> read as blocks of typed <c>name = value</c> entries. Open it, pick a block with
/// <see cref="SeekBlock"/>, then read entries with <see cref="Read"/> and <see cref="ReadArray"/>; or create one and
/// write blocks and entries.
/// </summary>
/// <remarks>
/// Original source: <c>lib\inifile.cpp</c>. Entry names are matched with case ignored, block names with case. The
/// <c>ReadId*</c>/<c>GetId*</c> methods are the original's interface (int32_t result codes, out parameters); each
/// folder's modernization step moves its calls to <see cref="Read"/>, and P4 deletes them.
/// </remarks>
class MCFitIniFile : public MCFile
{
public:
    MCFitIniFile() = default;
    ~MCFitIniFile() override;

    /// <summary>What kind of file this is.</summary>
    MCFileClass GetFileClass() const override { return MCFileClass::Ini; }

    /// <summary>Opens a FIT file and reads its block table (writes the header when creating).</summary>
    /// <returns>0, a File error, or an <see cref="MCFitError"/> value.</returns>
    int32_t Open(std::string_view fileName, MCFileMode mode = MCFileMode::Read) override;

    /// <summary>Opens a FIT file stored inside <paramref name="parent"/> (read into memory at once).</summary>
    int32_t Open(MCFile& parent, uint32_t length) override;

    /// <summary>Creates a FIT file for writing.</summary>
    int32_t Create(std::string_view fileName) override;

    /// <summary>Writes the footer when creating, forgets the block table and closes the file.</summary>
    void Close() override;

    using MCFile::Read;

    /// <summary>Makes the block named <paramref name="blockId"/> (case matters; the first of that name) current.</summary>
    /// <returns>0, or BLOCK_NOT_FOUND.</returns>
    int32_t SeekBlock(std::string_view blockId);

    /// <summary>
    /// Port-only: makes block <paramref name="index"/> (in the file's order) current, for reading every block in one
    /// pass.
    /// </summary>
    /// <returns>0, or BLOCK_NOT_FOUND.</returns>
    int32_t SeekBlockAt(int32_t index);

    /// <summary>The entry <paramref name="name"/> of the current block, with the type tag of <typeparamref name="T"/>.</summary>
    /// <remarks>
    /// A number that doesn't parse reads as 0; <c>0x</c> anywhere in it reads the hex digits after it. A bool is
    /// true for text starting with T, else for a nonzero number. A string is the text between the first quote after
    /// the <c>=</c> and the next one (or the end of the line).
    /// </remarks>
    template <MCFitValue T> MCFitResult<T> Read(std::string_view name);

    /// <summary>The element count of the array entry <paramref name="name"/> (<c>l[n] name = ...</c>).</summary>
    template <MCFitNumber T> MCFitResult<uint32_t> ArraySize(std::string_view name);

    /// <summary>The array entry <paramref name="name"/>; its elements may continue on the following lines.</summary>
    template <MCFitNumber T> MCFitResult<std::vector<T>> ReadArray(std::string_view name);

    /// <summary>
    /// The array entry <paramref name="name"/> into <paramref name="values"/> (UserArrayTooSmall when the file's is
    /// bigger). On an error part way through, the elements before it are already stored.
    /// </summary>
    /// <returns>The number of elements read.</returns>
    template <MCFitNumber T> MCFitResult<uint32_t> ReadArray(std::string_view name, std::span<T> values);

    /// <summary>Reads <c>f varName = value</c> (0 when missing).</summary>
    int32_t ReadIdFloat(std::string_view varName, float& value) { return ToLegacy(Read<float>(varName), value); }
    /// <summary>Reads <c>l varName = value</c> (0 when missing).</summary>
    int32_t ReadIdLong(std::string_view varName, int32_t& value) { return ToLegacy(Read<int32_t>(varName), value); }
    /// <summary>Reads <c>b varName = value</c>: 1 for text starting with T, else the number (0 when missing).</summary>
    int32_t ReadIdBoolean(std::string_view varName, int& value);
    /// <summary>Reads <c>s varName = value</c> (0 when missing).</summary>
    int32_t ReadIdShort(std::string_view varName, int16_t& value) { return ToLegacy(Read<int16_t>(varName), value); }
    /// <summary>Reads <c>c varName = value</c> (0 when missing).</summary>
    int32_t ReadIdChar(std::string_view varName, char& value) { return ToLegacy(Read<char>(varName), value); }
    /// <summary>Reads <c>ul varName = value</c> (0 when missing).</summary>
    int32_t ReadIdULong(std::string_view varName, uint32_t& value) { return ToLegacy(Read<uint32_t>(varName), value); }
    /// <summary>Reads <c>us varName = value</c> (0 when missing).</summary>
    int32_t ReadIdUShort(std::string_view varName, uint16_t& value) { return ToLegacy(Read<uint16_t>(varName), value); }
    /// <summary>Reads <c>uc varName = value</c> (0 when missing).</summary>
    int32_t ReadIdUChar(std::string_view varName, uint8_t& value) { return ToLegacy(Read<uint8_t>(varName), value); }

    /// <summary>
    /// Reads <c>st varName = "text"</c> into <paramref name="result"/>, zero-terminated. Text of as many characters
    /// as <paramref name="result"/> holds or more gives BUFFER_TOO_SMALL, with the buffer filled and no terminator.
    /// </summary>
    int32_t ReadIdString(std::string_view varName, std::span<char> result);

    /// <summary>The length of <c>st varName</c>'s text plus one, or an error (SYNTAX_ERROR without both quotes).</summary>
    int32_t GetIdStringLength(std::string_view varName);

    /// <summary>Reads <c>f[n] varName = ...</c> into <paramref name="result"/>.</summary>
    int32_t ReadIdFloatArray(std::string_view varName, std::span<float> result)
    {
        return ToLegacy(ReadArray(varName, result));
    }

    /// <summary>Reads <c>l[n] varName = ...</c>.</summary>
    int32_t ReadIdLongArray(std::string_view varName, std::span<int32_t> result)
    {
        return ToLegacy(ReadArray(varName, result));
    }

    /// <summary>Reads <c>us[n] varName = ...</c>.</summary>
    int32_t ReadIdUShortArray(std::string_view varName, std::span<uint16_t> result)
    {
        return ToLegacy(ReadArray(varName, result));
    }

    /// <summary>Reads <c>uc[n] varName = ...</c>.</summary>
    int32_t ReadIdUCharArray(std::string_view varName, std::span<uint8_t> result)
    {
        return ToLegacy(ReadArray(varName, result));
    }

    /// <summary>The n of <c>f[n] varName</c>, or an error code (as an unsigned value).</summary>
    uint32_t GetIdFloatArrayElements(std::string_view varName) { return ToLegacyCount(ArraySize<float>(varName)); }
    /// <summary>The n of <c>l[n] varName</c>, or an error code.</summary>
    uint32_t GetIdLongArrayElements(std::string_view varName) { return ToLegacyCount(ArraySize<int32_t>(varName)); }
    /// <summary>The n of <c>uc[n] varName</c>, or an error code.</summary>
    uint32_t GetIdUCharArrayElements(std::string_view varName) { return ToLegacyCount(ArraySize<uint8_t>(varName)); }

    /// <summary>Writes a <c>[blockId]</c> line (after an empty line).</summary>
    /// <returns>The bytes written.</returns>
    int32_t WriteBlock(std::string_view blockId);
    /// <summary>Writes <c>f varName=value</c>.</summary>
    int32_t WriteIdFloat(std::string_view varName, float value);
    /// <summary>Writes <c>b varName=TRUE</c> or <c>FALSE</c>.</summary>
    int32_t WriteIdBoolean(std::string_view varName, bool value);
    /// <summary>Writes <c>l varName=value</c>.</summary>
    int32_t WriteIdLong(std::string_view varName, int32_t value);
    /// <summary>Writes <c>s varName=value</c>.</summary>
    int32_t WriteIdShort(std::string_view varName, int16_t value);
    /// <summary>Writes <c>c varName=value</c>.</summary>
    int32_t WriteIdChar(std::string_view varName, char value);
    /// <summary>Writes <c>ul varName=value</c> (printed signed, as the original's %d).</summary>
    int32_t WriteIdULong(std::string_view varName, uint32_t value);
    /// <summary>Writes <c>us varName=value</c>.</summary>
    int32_t WriteIdUShort(std::string_view varName, uint16_t value);
    /// <summary>Writes <c>uc varName=value</c>.</summary>
    int32_t WriteIdUChar(std::string_view varName, uint8_t value);
    /// <summary>Writes <c>st varName="text"</c>.</summary>
    int32_t WriteIdString(std::string_view varName, std::string_view text);
    /// <summary>Writes <c>us[n] varName=v1,v2,...,</c>.</summary>
    int32_t WriteIdUShortArray(std::string_view varName, std::span<const uint16_t> values);
    /// <summary>Writes <c>l[n] varName=v1,v2,...,</c>.</summary>
    int32_t WriteIdLongArray(std::string_view varName, std::span<const int32_t> values);
    /// <summary>Writes <c>f[n] varName=v1, v2, ...,</c> with two decimals.</summary>
    int32_t WriteIdFloatArray(std::string_view varName, std::span<const float> values);
    /// <summary>Writes <c>uc[n] varName=v1, v2, ...,</c>.</summary>
    int32_t WriteIdUCharArray(std::string_view varName, std::span<const uint8_t> values);

    /// <summary>The number of blocks.</summary>
    int32_t GetNumBlocks() const { return static_cast<int32_t>(_Blocks.size()); }

    /// <summary>The name of block <paramref name="index"/>, or empty (for tools and tests).</summary>
    std::string_view GetBlockName(int32_t index) const
    {
        return index >= 0 && index < GetNumBlocks() ? std::string_view(_Blocks[static_cast<size_t>(index)].Id)
                                                    : std::string_view();
    }

private:
    /// <summary>A block: its name and where its first entry starts.</summary>
    struct Block
    {
        /// <summary>The block's name, between the brackets.</summary>
        std::string Id;
        /// <summary>Offset of the line after the block's <c>[name]</c> line.</summary>
        uint32_t Offset = 0;
    };

    /// <summary>An array entry found by <see cref="FindArrayLine"/>.</summary>
    struct ArrayLine
    {
        /// <summary>The line (the array's first).</summary>
        std::string Line;
        /// <summary>The element count it declares.</summary>
        uint32_t Count = 0;
    };

    /// <summary>The legacy result code of <paramref name="result"/>; stores its value, or 0 when it is missing.</summary>
    template <typename T> static int32_t ToLegacy(const MCFitResult<T>& result, T& value)
    {
        if (result.has_value())
        {
            value = *result;
            return 0;
        }

        if (result.error() == MCFitError::VariableNotFound)
        {
            value = T{};
        }

        return std::to_underlying(result.error());
    }

    /// <summary>The legacy result code of an array read.</summary>
    static int32_t ToLegacy(const MCFitResult<uint32_t>& result)
    {
        return result.has_value() ? 0 : std::to_underlying(result.error());
    }

    /// <summary>The legacy element count: the count, or the error code as an unsigned value.</summary>
    static uint32_t ToLegacyCount(const MCFitResult<uint32_t>& result)
    {
        return result.has_value() ? *result : static_cast<uint32_t>(std::to_underlying(result.error()));
    }

    /// <summary>The next line, cut at its first zero byte, as the original's C strings were.</summary>
    std::string ReadFitLine(int32_t maxLength);

    /// <summary>Reads lines until one starts with <c>[</c> (into <paramref name="line"/>).</summary>
    /// <returns>Whether one was found before the end of the file (OB-136: a block on the last line isn't).</returns>
    bool FindNextBlockStart(std::string& line);

    /// <summary>Counts the blocks from the read position on, then returns to it.</summary>
    int32_t CountBlocks();

    /// <summary>Checks the header and builds the block table (or writes the header when creating).</summary>
    int32_t AfterOpen();

    /// <summary>Writes the footer when creating and forgets the block table.</summary>
    void AtClose();

    /// <summary>
    /// The text after the <c>=</c> of the current block's line that starts with <paramref name="tag"/>, a space,
    /// <paramref name="name"/>, then optional spaces and <c>=</c>.
    /// </summary>
    MCFitResult<std::string> FindIdLine(std::string_view tag, std::string_view name);

    /// <summary>
    /// The line of the current block holding both <paramref name="tag"/> + <c>[</c> and <c>] name</c>, and the
    /// element count between them.
    /// </summary>
    MCFitResult<ArrayLine> FindArrayLine(std::string_view tag, std::string_view name);

    /// <summary>The <c>b</c> entry as the original read it: 1 for text starting with T, else the number.</summary>
    MCFitResult<int32_t> ReadBooleanValue(std::string_view name);

    /// <summary>Makes <paramref name="block"/>, one of the block table's, current.</summary>
    void SeekTo(std::vector<Block>::const_iterator block);

    /// <summary>Writes formatted text, returning the bytes written.</summary>
    int32_t WriteText(std::string_view text);

    /// <summary>The block table.</summary>
    std::vector<Block> _Blocks;
    /// <summary>Where the current block's entries start.</summary>
    uint32_t _CurrentBlockOffset = 0;
    /// <summary>The current block's length in bytes (to the next block's first entry, or the end of the file).</summary>
    uint32_t _CurrentBlockSize = 0;
};

/// <summary>
/// Reads entries of a <see cref="MCFitIniFile"/>'s current block one after another until one fails: from then on the
/// reads do nothing, and <see cref="Error"/> says which error stopped them. For loaders that give up at the first
/// missing entry.
/// </summary>
class MCFitReader
{
public:
    explicit MCFitReader(MCFitIniFile& file) : _File(file) {}

    /// <summary>Whether a read failed.</summary>
    bool Failed() const { return _Error.has_value(); }

    /// <summary>The error of the read that failed (call only when <see cref="Failed"/>).</summary>
    MCFitError Error() const { return *_Error; }

    /// <summary>
    /// Reads entry <paramref name="name"/> into <paramref name="value"/>. A missing entry stores zero (as the
    /// original's reads did) and stops the reading; another error leaves the value alone.
    /// </summary>
    template <MCFitValue T> void Value(std::string_view name, T& value)
    {
        if (_Error.has_value())
        {
            return;
        }

        const MCFitResult<T> result = _File.Read<T>(name);

        if (result.has_value())
        {
            value = *result;
            return;
        }

        if (result.error() == MCFitError::VariableNotFound)
        {
            value = T{};
        }

        _Error = result.error();
    }

private:
    MCFitIniFile& _File;
    std::optional<MCFitError> _Error;
};
