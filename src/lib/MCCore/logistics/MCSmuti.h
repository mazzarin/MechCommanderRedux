#pragma once

#include "lib/MCOptionalRef.h"

class MCGuiFont;
class MCGuiPort;

/// <summary>
/// The logistics text formatter: lays out a string with embedded <c>%</c> codes into a port, wrapping words at the
/// port's width. Codes: <c>%%</c> a percent sign, <c>%n</c> new line, <c>%c</c> centre the rest of the line,
/// <c>%tNN</c> move right NN pixels, <c>%fcN</c> font colour N, <c>%fs</c>/<c>%fm</c>/<c>%fl</c> small/medium/large
/// font (from the <c>Fonts</c> table).
/// </summary>
/// <remarks>
/// Original source: <c>logistics\smuti.cpp</c> (<c>SMUTI</c>). The GUI system keeps one
/// (<c>MCGuiSystem::TextFormatter</c>), which the inventory blocks and the chat windows use. With no port,
/// <see cref="Process"/> only measures.
/// </remarks>
class MCSmuti
{
public:
    /// <summary>
    /// The characters a <c>%c</c> line takes at most; the one after them is drawn behind the line (a game rule: the
    /// layout the texts were written for).
    /// </summary>
    static constexpr int32_t MaxCenteredLength = 80;

    /// <summary>Reads the text file <paramref name="fileName"/> and formats it into <paramref name="port"/>.</summary>
    /// <returns>The height used, as <see cref="Process"/>.</returns>
    int32_t ProcessFile(std::string_view fileName, MCOptionalRef<MCGuiPort> port, int32_t width);

    /// <summary>The text of the file <paramref name="fileName"/> as <see cref="ProcessFile"/> formats it.</summary>
    static std::string ReadFile(std::string_view fileName);

    /// <summary>
    /// Formats <paramref name="text"/> (up to a NUL) into <paramref name="port"/> starting at line
    /// <paramref name="startY"/>; <paramref name="width"/> is the wrap width when there is no port (otherwise the
    /// port's width).
    /// </summary>
    /// <returns>The y just below the last line written.</returns>
    int32_t Process(std::string_view text, MCOptionalRef<MCGuiPort> port, int32_t width, int32_t startY);

    /// <summary>
    /// Before <paramref name="nextChar"/> is added: when it would pass the wrap width, writes the line buffer up to
    /// its last space and carries the rest to the next line.
    /// </summary>
    void CheckWrap(uint8_t nextChar);

    /// <summary>The current font.</summary>
    std::shared_ptr<MCGuiFont> Font;
    /// <summary>The port written into during <see cref="Process"/>; absent while only measuring.</summary>
    MCOptionalRef<MCGuiPort> Port;
    /// <summary>The wrap width in pixels.</summary>
    int32_t Width = 0;
    /// <summary>The pixel width of the pending line buffer.</summary>
    int32_t CurX = 0;
    /// <summary>The y of the current line.</summary>
    int32_t CurY = 0;
    /// <summary>The x where the pending line buffer will be written.</summary>
    int32_t LineStartX = 0;
    /// <summary>The font size: 0 small, 1 medium, 2 large (the column of <c>Fonts</c>).</summary>
    int32_t FontSize = 0;
    /// <summary>The font colour (the row of <c>Fonts</c>); starts at 3.</summary>
    int32_t FontColor = 0;
    /// <summary>
    /// The text of the current line not yet written, as the original's character buffer held it: the line ends at the
    /// first NUL, and what lies behind it is left from earlier lines (a colour change writes the line out but leaves
    /// it in the buffer until the next character). It grows as needed (the original's held 256).
    /// </summary>
    std::string LineBuffer;
    /// <summary>The number of characters of <see cref="LineBuffer"/> in the current line.</summary>
    int32_t LineLength = 0;
    /// <summary>Set while a <c>%c</c> centred line was written directly (the buffer must not be flushed).</summary>
    bool Centered = false;

private:
    /// <summary>The buffer's character <paramref name="index"/> (0 past its end).</summary>
    char At(int32_t index) const;
    /// <summary>Sets the buffer's character <paramref name="index"/>, growing it as needed.</summary>
    void Put(int32_t index, char c);
    /// <summary>The line in the buffer: its characters up to the first NUL.</summary>
    std::string_view Line() const;
    /// <summary>Writes <see cref="Line"/> at (<paramref name="xPos"/>, <see cref="CurY"/>) when there is a port.</summary>
    void WriteLine(int32_t xPos);
};
