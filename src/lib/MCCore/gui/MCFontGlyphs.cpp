#include "stdafx.h"
#include "gui/MCFontGlyphs.h"
#include "vfx/MCVfxClip.h"

namespace
{
    /// <summary>What a glyph's clear pixels hold: the fonts' colour tables keep 255 as transparent.</summary>
    constexpr uint8_t Clear = 0xff;
    /// <summary>The font header's words: how many characters it lists, and their height; then each one's offset.</summary>
    constexpr size_t CountAt = 4;
    constexpr size_t HeightAt = 8;
    constexpr size_t TableAt = 0x10;
    /// <summary>The bytes of a header word, a glyph offset and a glyph's width.</summary>
    constexpr size_t WordBytes = 4;

    /// <summary>A drawn glyph picture's ink, and the end of each of its rows (anything else is clear).</summary>
    constexpr char Ink = '#';
    constexpr char RowEnd = '/';

    // The Windows-1252 characters made, and those they are made from.
    constexpr uint8_t Section = 0xa7;
    constexpr uint8_t Diaeresis = 0xa8;
    constexpr uint8_t LeftQuotes = 0xab;
    constexpr uint8_t Degree = 0xb0;
    constexpr uint8_t Squared = 0xb2;
    constexpr uint8_t Acute = 0xb4;
    constexpr uint8_t Ordinal = 0xba;
    constexpr uint8_t RightQuotes = 0xbb;
    constexpr uint8_t Half = 0xbd;
    constexpr uint8_t InvertedQuestion = 0xbf;
    constexpr uint8_t DiaeresisA = 0xe4;
    constexpr uint8_t RingA = 0xe5;
    constexpr uint8_t AcuteE = 0xe9;
    constexpr uint8_t DiaeresisE = 0xeb;
    /// <summary>The capital a font's cap line and ink are read from.</summary>
    constexpr uint8_t Capital = 'H';

    /// <summary>
    /// A glyph drawn for the fonts whose capitals are <see cref="CapRows"/> rows tall: its picture, row by row down from
    /// the cap line.
    /// </summary>
    struct MCDrawnGlyph
    {
        int32_t CapRows = 0;
        uint8_t Character = 0;
        std::string_view Picture;
    };

    // In the fonts' manner: one-pixel strokes, corners left open, superscripts on the cap line.
    constexpr MCDrawnGlyph DrawnGlyphs[] = {
        // The 8-line fonts.
        {6, Section,
         ".##/"
         "#../"
         ".#./"
         "#.#/"
         ".#./"
         "..#/"
         "##./"},
        {6, Squared,
         "##./"
         "..#/"
         ".#./"
         "###/"},
        {6, Ordinal,
         ".#./"
         "#.#/"
         ".#./"
         ".../"
         "###/"},
        {6, Degree,
         ".#./"
         "#.#/"
         ".#./"},
        {6, Half,
         "#...#./"
         "#..#../"
         "#.#.../"
         ".#.##./"
         "#....#/"
         "....#./"
         "...###/"},
        // The 10- and 12-line fonts.
        {8, Section,
         ".##./"
         "#..#/"
         ".#../"
         "#.#./"
         ".#.#/"
         "..#./"
         "#..#/"
         ".##./"},
        {8, Squared,
         ".##./"
         "#..#/"
         "..#./"
         ".#../"
         "####/"},
        {8, Ordinal,
         ".##./"
         "#..#/"
         "#..#/"
         ".##./"
         "..../"
         "####/"},
        {8, Half,
         ".#....#/"
         "##...#./"
         ".#..#../"
         ".#.#.../"
         "..#.##./"
         ".#.#..#/"
         "#....#./"
         "....#../"
         "...####/"},
    };

    /// <summary>A glyph's pixels, row by row.</summary>
    struct MCGlyph
    {
        int32_t Width = 0;
        int32_t Height = 0;
        std::vector<uint8_t> Pixels;

        MCGlyph(int32_t width, int32_t height)
            : Width(width), Height(height), Pixels(static_cast<size_t>(width) * static_cast<size_t>(height), Clear)
        {
        }

        uint8_t& At(int32_t x, int32_t y) { return Pixels[static_cast<size_t>(y * Width + x)]; }
        uint8_t At(int32_t x, int32_t y) const { return Pixels[static_cast<size_t>(y * Width + x)]; }

        std::span<const uint8_t> Row(int32_t y) const
        {
            return std::span(Pixels).subspan(static_cast<size_t>(y * Width), static_cast<size_t>(Width));
        }

        bool Blank() const
        {
            return std::ranges::all_of(Pixels, [](uint8_t pixel) { return pixel == Clear; });
        }
    };

    /// <summary>The first and last of a glyph's rows (or, <paramref name="columns"/>, columns) with ink, if any.</summary>
    std::optional<std::pair<int32_t, int32_t>> DrawnSpan(const MCGlyph& glyph, bool columns)
    {
        std::optional<std::pair<int32_t, int32_t>> span;

        for (int32_t y = 0; y < glyph.Height; y++)
        {
            for (int32_t x = 0; x < glyph.Width; x++)
            {
                if (glyph.At(x, y) == Clear)
                {
                    continue;
                }

                const int32_t at = columns ? x : y;
                span = span ? std::pair(std::min(span->first, at), std::max(span->second, at)) : std::pair(at, at);
            }
        }

        return span;
    }

    /// <summary>The width of <paramref name="character"/>'s glyph and its pixels in <paramref name="font"/>, if it lists it.</summary>
    std::optional<std::pair<int32_t, std::span<const uint8_t>>> PixelsOf(std::span<const uint8_t> font,
                                                                         uint8_t character)
    {
        if (character >= MCVfxRead32(font, CountAt))
        {
            return std::nullopt;
        }

        const auto at = static_cast<uint32_t>(MCVfxRead32(font, TableAt + character * WordBytes));
        const int32_t width = MCVfxRead32(font, at);
        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(MCVfxRead32(font, HeightAt));
        return std::pair(width, font.subspan(at + WordBytes, size));
    }

    /// <summary>The glyph of <paramref name="character"/>, if <paramref name="font"/> lists it.</summary>
    std::optional<MCGlyph> GlyphOf(std::span<const uint8_t> font, uint8_t character)
    {
        const auto pixels = PixelsOf(font, character);

        if (!pixels)
        {
            return std::nullopt;
        }

        MCGlyph glyph(pixels->first, MCVfxRead32(font, HeightAt));
        std::ranges::copy(pixels->second, glyph.Pixels.begin());
        return glyph;
    }

    /// <summary>
    /// The mark of accented letter <paramref name="letter"/> alone: its rows above those it shares with
    /// <paramref name="sibling"/>, the same letter under another mark (é's ´ above the e it shares with ë).
    /// </summary>
    std::optional<MCGlyph> Mark(std::span<const uint8_t> font, uint8_t letter, uint8_t sibling)
    {
        std::optional<MCGlyph> mark = GlyphOf(font, letter);
        const std::optional<MCGlyph> other = GlyphOf(font, sibling);

        if (!mark || !other || mark->Width != other->Width)
        {
            return std::nullopt;
        }

        int32_t body = mark->Height;

        while (body > 0 && std::ranges::equal(mark->Row(body - 1), other->Row(body - 1)))
        {
            body--;
        }

        // Letters with nothing in common below, or nothing different, aren't one letter under two marks.
        if (body == mark->Height || body == 0)
        {
            return std::nullopt;
        }

        std::fill(mark->Pixels.begin() + static_cast<ptrdiff_t>(body) * mark->Width, mark->Pixels.end(), Clear);
        return mark->Blank() ? std::nullopt : mark;
    }

    /// <summary><paramref name="source"/> turned half round within the rows and columns it draws in: ¿ from ?.</summary>
    std::optional<MCGlyph> Turned(std::span<const uint8_t> font, uint8_t source)
    {
        const std::optional<MCGlyph> glyph = GlyphOf(font, source);
        const std::optional<std::pair<int32_t, int32_t>> rows = glyph ? DrawnSpan(*glyph, false) : std::nullopt;

        if (!rows)
        {
            return std::nullopt;
        }

        const auto [left, right] = *DrawnSpan(*glyph, true);
        MCGlyph turned = *glyph;

        for (int32_t y = rows->first; y <= rows->second; y++)
        {
            for (int32_t x = 0; x < glyph->Width; x++)
            {
                const int32_t fromX = x >= left && x <= right ? left + right - x : x;
                turned.At(x, y) = glyph->At(fromX, rows->first + rows->second - y);
            }
        }

        return turned;
    }

    /// <summary>Two of <paramref name="source"/> side by side, the second half its drawn width on: « from &lt;.</summary>
    std::optional<MCGlyph> Doubled(std::span<const uint8_t> font, uint8_t source)
    {
        const std::optional<MCGlyph> glyph = GlyphOf(font, source);
        const std::optional<std::pair<int32_t, int32_t>> columns = glyph ? DrawnSpan(*glyph, true) : std::nullopt;

        if (!columns)
        {
            return std::nullopt;
        }

        // Half the drawn width on, rounded up.
        const int32_t drawnWidth = columns->second - columns->first + 1;
        const int32_t step = (drawnWidth + 1) / 2;
        MCGlyph doubled(glyph->Width + step, glyph->Height);

        for (const int32_t shift : {0, step})
        {
            for (int32_t y = 0; y < glyph->Height; y++)
            {
                for (int32_t x = 0; x < glyph->Width; x++)
                {
                    if (glyph->At(x, y) != Clear)
                    {
                        doubled.At(x + shift, y) = glyph->At(x, y);
                    }
                }
            }
        }

        return doubled;
    }

    /// <summary>A font's capitals: their ink, and the row of their cap line and how many rows they are tall.</summary>
    struct MCCapitals
    {
        uint8_t Ink = 0;
        int32_t Top = 0;
        int32_t Rows = 0;
    };

    /// <summary>The capitals of <paramref name="font"/>, if it draws them in one ink (the drop-shadow fonts use two).</summary>
    std::optional<MCCapitals> CapitalsOf(std::span<const uint8_t> font)
    {
        const std::optional<MCGlyph> glyph = GlyphOf(font, Capital);
        const std::optional<std::pair<int32_t, int32_t>> rows = glyph ? DrawnSpan(*glyph, false) : std::nullopt;

        if (!rows)
        {
            return std::nullopt;
        }

        auto inked = glyph->Pixels | std::views::filter([](uint8_t pixel) { return pixel != Clear; });
        const uint8_t ink = *inked.begin();

        if (!std::ranges::all_of(inked, [ink](uint8_t pixel) { return pixel == ink; }))
        {
            return std::nullopt;
        }

        return MCCapitals{ink, rows->first, rows->second - rows->first + 1};
    }

    /// <summary>
    /// The glyph of <paramref name="drawn"/> in a font of <paramref name="height"/> rows and
    /// <paramref name="capitals"/>, with a clear column on its left as the fonts' glyphs have.
    /// </summary>
    MCGlyph Drawn(const MCDrawnGlyph& drawn, int32_t height, const MCCapitals& capitals)
    {
        std::vector<std::string_view> rows;

        for (const auto row : std::views::split(drawn.Picture, RowEnd))
        {
            if (!row.empty())
            {
                rows.emplace_back(row.begin(), row.end());
            }
        }

        const size_t widest = std::ranges::max(rows, {}, [](std::string_view row) { return row.size(); }).size();
        MCGlyph glyph(static_cast<int32_t>(widest) + 1, height);

        for (size_t row = 0; row < rows.size() && capitals.Top + static_cast<int32_t>(row) < height; row++)
        {
            for (size_t column = 0; column < rows[row].size(); column++)
            {
                if (rows[row][column] == Ink)
                {
                    glyph.At(static_cast<int32_t>(column) + 1, capitals.Top + static_cast<int32_t>(row)) = capitals.Ink;
                }
            }
        }

        return glyph;
    }

    /// <summary>Writes <paramref name="value"/> at <paramref name="at"/> in <paramref name="data"/>, low byte first.</summary>
    void Write32(std::span<uint8_t> data, size_t at, uint32_t value)
    {
        for (size_t i = 0; i < WordBytes; i++)
        {
            data[at + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    }
}

bool MCFontGlyphs::Draws(std::span<const uint8_t> font, uint8_t character)
{
    // Read in place: the CONTROLS screen asks for every key it shows, each frame.
    const auto pixels = PixelsOf(font, character);
    return pixels && std::ranges::any_of(pixels->second, [](uint8_t pixel) { return pixel != Clear; });
}

void MCFontGlyphs::AddMissing(std::vector<uint8_t>& font)
{
    // The glyphs are made from the font as loaded, then added after its data, each table entry pointing at its own.
    const std::span<const uint8_t> loaded = font;
    std::vector<std::pair<uint8_t, MCGlyph>> made;

    const auto add = [&](uint8_t character, const auto& make)
    {
        const std::optional<MCGlyph> blank = GlyphOf(loaded, character);

        if (!blank || !blank->Blank() || std::ranges::contains(made | std::views::keys, character))
        {
            return;
        }

        if (std::optional<MCGlyph> glyph = make(); glyph)
        {
            made.emplace_back(character, std::move(*glyph));
        }
    };

    if (const std::optional<MCCapitals> capitals = CapitalsOf(loaded); capitals)
    {
        const int32_t height = MCVfxRead32(loaded, HeightAt);

        for (const MCDrawnGlyph& drawn : DrawnGlyphs)
        {
            if (drawn.CapRows == capitals->Rows)
            {
                add(drawn.Character, [&] { return std::optional(Drawn(drawn, height, *capitals)); });
            }
        }
    }

    add(Acute, [&] { return Mark(loaded, AcuteE, DiaeresisE); });
    add(Diaeresis, [&] { return Mark(loaded, DiaeresisE, AcuteE); });
    add(Degree, [&] { return Mark(loaded, RingA, DiaeresisA); });
    add(InvertedQuestion, [&] { return Turned(loaded, '?'); });
    add(LeftQuotes, [&] { return Doubled(loaded, '<'); });
    add(RightQuotes, [&] { return Doubled(loaded, '>'); });

    for (const auto& [character, glyph] : made)
    {
        const size_t at = font.size();
        font.resize(at + WordBytes);
        Write32(font, at, static_cast<uint32_t>(glyph.Width));
        font.insert(font.end(), glyph.Pixels.begin(), glyph.Pixels.end());
        Write32(font, TableAt + character * WordBytes, static_cast<uint32_t>(at));
    }
}
