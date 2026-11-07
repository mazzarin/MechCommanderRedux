#pragma once

/// <summary>
/// The string resources of the game's executable (its <c>STRINGTABLE</c>), read from the PE file itself so the port
/// needs no Win32 <c>LoadString</c>. MechCommander keeps its interface text there, offset by language
/// (<c>cLoadString</c>).
/// </summary>
class MCStringTable
{
public:
    /// <summary>Reads every RT_STRING block of a PE image (the first language of each block).</summary>
    /// <returns>The table, or why the image couldn't be read.</returns>
    static std::expected<MCStringTable, std::string> Parse(std::span<const uint8_t> image);

    /// <summary>Reads the string table of an executable on disk.</summary>
    static std::expected<MCStringTable, std::string> Load(const std::filesystem::path& path);

    /// <summary>String <paramref name="id"/> in Windows-1252, or none when there is none.</summary>
    std::optional<std::string> Find(uint32_t id) const;

    /// <summary>How many strings there are.</summary>
    size_t Count() const { return _Strings.size(); }

    /// <summary>
    /// <c>LoadStringA</c>: copies string <paramref name="id"/> into <paramref name="buffer"/>, cut to its size - 1
    /// characters and terminated.
    /// </summary>
    /// <returns>The characters copied (0 when there is no such string).</returns>
    int32_t LoadString(uint32_t id, std::span<char> buffer) const;

    /// <summary>
    /// The text file beside MCX.EXE holding its table, which every release writes from its own MCX.EXE
    /// (tools/release/game_strings.py): the Windows setup puts the game, which holds no text, in MCX.EXE's place.
    /// </summary>
    static constexpr std::string_view TextFileName = "MCX strings.txt";

    /// <summary>
    /// Reads a table written as <see cref="TextFileName"/> is: UTF-8 lines, comments starting with #, then a string a
    /// line: its id in decimal, a tab and its text, with a backslash, new line, carriage return and tab written \\,
    /// \n, \r and \t.
    /// </summary>
    /// <returns>The table in Windows-1252 (as <see cref="Parse"/> gives it), or which line couldn't be read.</returns>
    static std::expected<MCStringTable, std::string> ParseText(std::string_view text);

    /// <summary>Reads a table's text file on disk (<see cref="ParseText"/>).</summary>
    static std::expected<MCStringTable, std::string> LoadText(const std::filesystem::path& path);

    /// <summary>
    /// The game's table: the executable's own when it holds any text (a player's copy in another language keeps its
    /// own), else the text file's (Windows' game in MCX.EXE's place).
    /// </summary>
    /// <returns>The table, or why neither gave one.</returns>
    static std::expected<MCStringTable, std::string> LoadGame(const std::filesystem::path& exe,
                                                              const std::filesystem::path& text);

    /// <summary>The game's table: MCX.EXE's in the game folder, or its text file's (<see cref="LoadGame"/>), read on
    /// first use (empty, and logged, if neither can be).</summary>
    static const MCStringTable& Game();

    bool operator==(const MCStringTable&) const = default;

private:
    std::unordered_map<uint32_t, std::string> _Strings;
};
