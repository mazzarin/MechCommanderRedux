#include "stdafx.h"
#include "logistics/MCSmuti.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"

auto MCSmuti::ProcessFile(std::string_view fileName, MCOptionalRef<MCGuiPort> port, int32_t width) -> int32_t
{
    return Process(ReadFile(fileName), port, width, 0);
}

auto MCSmuti::ReadFile(std::string_view fileName) -> std::string
{
    MCFile file;
    const int32_t result = file.Open(fileName);
    Assert(result == 0, result, "Could not open SMUTI file");
    std::vector<uint8_t> text(file.GetLength());
    file.Read(text);
    file.Close();
    // The last two bytes (the file's closing CR LF) are dropped.
    constexpr size_t LineEnd = 2;
    const size_t length = text.size() >= LineEnd ? text.size() - LineEnd : 0;
    return std::string(reinterpret_cast<const char*>(text.data()), length);
}

auto MCSmuti::At(int32_t index) const -> char
{
    return index >= 0 && static_cast<size_t>(index) < LineBuffer.size() ? LineBuffer[static_cast<size_t>(index)] : '\0';
}

auto MCSmuti::Put(int32_t index, char c) -> void
{
    if (static_cast<size_t>(index) >= LineBuffer.size())
    {
        LineBuffer.resize(static_cast<size_t>(index) + 1);
    }

    LineBuffer[static_cast<size_t>(index)] = c;
}

auto MCSmuti::Line() const -> std::string_view
{
    const std::string_view buffer = LineBuffer;
    return buffer.substr(0, buffer.find('\0'));
}

auto MCSmuti::WriteLine(int32_t xPos) -> void
{
    if (Port.has_value())
    {
        Font->WriteString(Port->get().Frame(), xPos, CurY, Line());
    }
}

auto MCSmuti::Process(std::string_view text, MCOptionalRef<MCGuiPort> port, int32_t width, int32_t startY) -> int32_t
{
    text = text.substr(0, text.find('\0'));

    if (port.has_value())
    {
        width = port->get().Width();
    }

    Width = width;
    Port = port;
    CurX = 0;
    CurY = startY;
    Font = GreenFont;
    FontSize = 0;
    FontColor = 3;
    LineLength = 0;
    LineStartX = 0;

    // The text's characters as the original walked them: 0 at and past the end.
    const auto size = static_cast<ptrdiff_t>(text.size());
    auto at = [&](ptrdiff_t index) -> uint8_t
    { return index < size ? static_cast<uint8_t>(text[static_cast<size_t>(index)]) : 0; };

    // Adds a character to the line (the buffer ends after it).
    auto append = [this](uint8_t c)
    {
        Put(LineLength++, static_cast<char>(c));
        Put(LineLength, '\0');
    };

    // Writes a character straight into the port at CurX and moves past it.
    auto drawChar = [this](uint8_t c)
    {
        if (Port.has_value())
        {
            Font->WriteChar(Port->get().Frame(), CurX, CurY, static_cast<char>(c));
        }

        CurX += Font->Width(c);
    };

    ptrdiff_t p = -1;

    for (;;)
    {
        uint8_t c = at(++p);

        if (c == 0)
        {
            break;
        }

        Centered = false;

        if (c != '%')
        {
            if (c > 0x1f)
            {
                CheckWrap(c);
                append(c);
                CurX += Font->Width(c);
            }

            continue;
        }

        // A code: p is on its '%'. A %c line can hand another '%' straight back here.
        bool finished = false;

        for (bool again = true; again;)
        {
            again = false;
            c = at(++p);

            if (c == 0)
            {
                finished = true;
                break;
            }

            switch (std::tolower(c))
            {
                case '%':
                {
                    CheckWrap(c);
                    append(c);
                    CurX += Font->Width(c);
                    break;
                }

                case 'c':
                {
                    // A centred line, measured and written straight into the port.
                    Centered = true;
                    std::string centerText;
                    int32_t lineWidth = 0;

                    if (CurX > 0)
                    {
                        CurX = 0;
                        CurY += Font->Height();
                    }

                    c = at(++p);

                    while (c != '%' && c != 0 && static_cast<int32_t>(centerText.size()) != MaxCenteredLength)
                    {
                        lineWidth += Font->Width(c);
                        centerText += static_cast<char>(c);

                        if (Width <= lineWidth)
                        {
                            break;
                        }

                        c = at(++p);
                    }

                    if (Width < lineWidth)
                    {
                        // Too wide: write it from the left edge (including the character that overflowed) and carry
                        // that character onto the next line.
                        if (Port.has_value())
                        {
                            Font->WriteString(Port->get().Frame(), 0, CurY, centerText);
                        }

                        CurX = 0;
                        CurY += Font->Height();
                        c = at(p);

                        if (c == '%')
                        {
                            again = true;
                            break;
                        }

                        // Original behaviour (OB-070): the overflowing character is drawn at x 0 and then again after
                        // itself.
                        if (Port.has_value())
                        {
                            Font->WriteChar(Port->get().Frame(), 0, CurY, static_cast<char>(c));
                        }

                        CurX = Font->Width(c);
                    }
                    else
                    {
                        const int32_t x = (Width - lineWidth) / 2;
                        CurX = x;

                        if (Port.has_value())
                        {
                            Font->WriteString(Port->get().Frame(), x, CurY, centerText);
                        }

                        CurX = Font->Width(centerText) + x;
                    }

                    if (at(p) == '%')
                    {
                        again = true;
                        break;
                    }

                    // The text ended inside the centred line (the original drew the terminator and read on past it).
                    if (at(p) == 0)
                    {
                        finished = true;
                        break;
                    }

                    // The character that stopped the line (the last one measured, or the 81st) is drawn after it.
                    drawChar(at(p));
                    break;
                }

                case 'f':
                {
                    const uint8_t code = at(++p);

                    if (code == 0)
                    {
                        finished = true;
                        break;
                    }

                    switch (std::tolower(code))
                    {
                        case 'c':
                        {
                            const uint8_t color = at(++p);

                            if (color == 0)
                            {
                                finished = true;
                                break;
                            }

                            // Original behaviour (OB-163): the line is written but stays in the buffer.
                            if (Port.has_value() && At(0) != '\0')
                            {
                                Put(LineLength, '\0');
                                WriteLine(LineStartX);
                                LineStartX += CurX;
                                CurX = 0;
                                LineLength = 0;
                            }

                            // Issue #25: the text comes from briefings, scripts and chat; a colour other than 0-9
                            // keeps the current one (the original indexed the ten-row table with it).
                            if (color >= '0' && color < '0' + static_cast<int32_t>(Fonts.size()))
                            {
                                FontColor = color - '0';
                                Font = Fonts[FontColor][FontSize];
                            }

                            break;
                        }

                        case 'l':
                        {
                            FontSize = 2;
                            Font = Fonts[FontColor][2];
                            break;
                        }
                        case 'm':
                        {
                            FontSize = 1;
                            Font = Fonts[FontColor][1];
                            break;
                        }
                        case 's':
                        {
                            FontSize = 0;
                            Font = Fonts[FontColor][0];
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }

                case 'n':
                {
                    Put(LineLength, '\0');
                    WriteLine(LineStartX);
                    LineStartX = 0;
                    CurX = 0;
                    LineLength = 0;
                    CurY += Font->Height() + 1;
                    break;
                }

                case 't':
                {
                    // Moves right by a decimal pixel count; p stays on its last digit.
                    int32_t tab = 0;

                    while (at(p + 1) >= '0' && at(p + 1) <= '9')
                    {
                        tab = tab * 10 + (at(++p) - '0');
                    }

                    const int32_t oldX = CurX;
                    CurX = oldX + tab;
                    LineStartX += tab;

                    if (Width <= oldX + tab)
                    {
                        CheckWrap(at(p));
                    }
                    break;
                }

                default:
                {
                    // Any other code is text: the '%' and the character both go into the line (the buffer isn't ended
                    // between them, so a wrap before the character sees what lies behind the '%').
                    CheckWrap('%');
                    Put(LineLength++, '%');
                    CurX += Font->Width('%');
                    CheckWrap(c);
                    append(c);
                    CurX += Font->Width(c);
                    break;
                }
            }
        }

        if (finished)
        {
            break;
        }
    }

    if (At(0) != '\0' && !Centered)
    {
        WriteLine(LineStartX);
    }

    Port.reset();
    return Font->Height() + CurY;
}

auto MCSmuti::CheckWrap(uint8_t nextChar) -> void
{
    if (Width > Font->Width(nextChar) + LineStartX + CurX)
    {
        return;
    }

    // Breaks at the last space (never at index 0); with none, the last character moves one right and the split
    // falls just before it.
    int32_t split = LineLength;

    for (; split > 0; split--)
    {
        if (At(split) == ' ')
        {
            Put(split, '\0');
            break;
        }
    }

    if (split == 0)
    {
        // An empty buffer: the original read and cleared the byte before its buffer (always 0).
        Put(LineLength, LineLength > 0 ? At(LineLength - 1) : '\0');
        split = LineLength - 1;

        if (LineLength > 0)
        {
            Put(LineLength - 1, '\0');
        }

        LineLength++;
    }

    WriteLine(LineStartX);
    int32_t count = 0;

    for (int32_t i = split + 1; i < LineLength; i++)
    {
        Put(count++, At(i));
    }

    Put(count, '\0');
    LineStartX = 0;
    CurX = Font->Width(Line());
    LineLength = count;
    CurY += Font->Height() + 1;
}
