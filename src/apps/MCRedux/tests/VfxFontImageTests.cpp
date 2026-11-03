#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCFontGlyphs.h"
#include "lib/MCFile.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>An 8-bit window with a pane over all of it.</summary>
    struct TestSurface
    {
        std::shared_ptr<MCWindow> Window;
        MCPane Pane{};

        TestSurface(int32_t width, int32_t height, uint8_t fill = 0) : Window(MCWindow::Make(width, height))
        {
            std::ranges::fill(Pixels(), fill);
            Pane = MCPane::Over(Window);
        }

        std::span<uint8_t> Pixels() const { return Window->Bytes(); }

        uint8_t At(int32_t x, int32_t y) const { return Pixels()[static_cast<size_t>(y) * (Window->XMax + 1) + x]; }
    };

    void Put32(std::vector<uint8_t>& data, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            data.push_back(static_cast<uint8_t>(value >> (i * 8)));
        }
    }

    /// <summary>
    /// A font of 256 characters, 3 rows high: 'A' is 2 wide with pixels 1..6, 'B' 3 wide with 7..15, the rest empty
    /// (width 0).
    /// </summary>
    std::vector<uint8_t> MakeFont()
    {
        std::vector<uint8_t> font;
        Put32(font, 1);   // version
        Put32(font, 256); // char_count
        Put32(font, 3);   // char_height
        Put32(font, 0);   // font_background
        const size_t offsets = font.size();
        font.resize(offsets + 256 * 4);
        const auto setOffset = [&](int c, uint32_t offset) { std::memcpy(font.data() + offsets + c * 4, &offset, 4); };
        const uint32_t empty = static_cast<uint32_t>(font.size());
        Put32(font, 0);

        for (int c = 0; c < 256; ++c)
        {
            setOffset(c, empty);
        }

        setOffset('A', static_cast<uint32_t>(font.size()));
        Put32(font, 2);

        for (uint8_t p = 1; p <= 6; ++p)
        {
            font.push_back(p);
        }

        setOffset('B', static_cast<uint32_t>(font.size()));
        Put32(font, 3);

        for (uint8_t p = 7; p <= 15; ++p)
        {
            font.push_back(p);
        }

        return font;
    }

    /// <summary>LSB-first bit writer for GIF LZW data.</summary>
    struct BitWriter
    {
        std::vector<uint8_t> Bytes;
        uint32_t Buffer = 0;
        int Count = 0;

        void Write(uint32_t code, int bits)
        {
            Buffer |= code << Count;
            Count += bits;

            while (Count >= 8)
            {
                Bytes.push_back(static_cast<uint8_t>(Buffer));
                Buffer >>= 8;
                Count -= 8;
            }
        }

        void Flush()
        {
            if (Count > 0)
            {
                Bytes.push_back(static_cast<uint8_t>(Buffer));
            }

            Buffer = 0;
            Count = 0;
        }
    };

    /// <summary>
    /// A GIF87a of 4-colour pixels (minimum code size 2): a clear code before every two literal codes keeps the code
    /// size at 3 bits, which any LZW decoder reads back.
    /// </summary>
    std::vector<uint8_t> MakeGif(int width, int height, const std::vector<uint8_t>& pixels, bool interlaced)
    {
        std::vector<uint8_t> gif = {'G', 'I', 'F', '8', '7', 'a'};
        gif.push_back(static_cast<uint8_t>(width));
        gif.push_back(static_cast<uint8_t>(width >> 8));
        gif.push_back(static_cast<uint8_t>(height));
        gif.push_back(static_cast<uint8_t>(height >> 8));
        gif.push_back(0x81); // global colour table of 4 entries
        gif.push_back(3);    // background
        gif.push_back(0);
        const uint8_t colors[12] = {0, 0, 0, 252, 0, 0, 0, 128, 0, 4, 8, 255};
        gif.insert(gif.end(), colors, colors + 12);
        gif.push_back(',');

        for (int i = 0; i < 4; ++i)
        {
            gif.push_back(0);
        }

        gif.push_back(static_cast<uint8_t>(width));
        gif.push_back(static_cast<uint8_t>(width >> 8));
        gif.push_back(static_cast<uint8_t>(height));
        gif.push_back(static_cast<uint8_t>(height >> 8));
        gif.push_back(interlaced ? 0x40 : 0);
        gif.push_back(2); // minimum code size

        BitWriter bits;

        for (size_t i = 0; i < pixels.size(); ++i)
        {
            if (i % 2 == 0)
            {
                bits.Write(4, 3);
            }

            bits.Write(pixels[i], 3);
        }

        bits.Write(5, 3);
        bits.Flush();

        for (size_t i = 0; i < bits.Bytes.size(); i += 255)
        {
            const size_t n = std::min<size_t>(255, bits.Bytes.size() - i);
            gif.push_back(static_cast<uint8_t>(n));
            gif.insert(gif.end(), bits.Bytes.begin() + static_cast<ptrdiff_t>(i),
                       bits.Bytes.begin() + static_cast<ptrdiff_t>(i + n));
        }

        gif.push_back(0);
        gif.push_back(';');
        return gif;
    }

    std::vector<uint8_t> ReadGameFile(std::string_view name)
    {
        MCFile file;

        if (file.Open(name) != 0)
        {
            return {};
        }

        std::vector<uint8_t> data(file.FileSize());
        file.Read(data);
        return data;
    }
}

TEST_CASE("vfx font: glyphs copy whole without a table, and clip to the pane")
{
    std::vector<uint8_t> font = MakeFont();
    CHECK_EQ(VfxFontHeight(font), 3);
    CHECK_EQ(VfxCharacterWidth(font, 'A'), 2);
    CHECK_EQ(VfxCharacterWidth(font, 'B'), 3);

    TestSurface surface(8, 6, 99);
    CHECK_EQ(VfxCharacterDraw(surface.Pane, 1, 2, font, 'B', std::nullopt), 3);

    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            CHECK_EQ(surface.At(1 + x, 2 + y), static_cast<uint8_t>(7 + y * 3 + x));
        }
    }

    CHECK_EQ(surface.At(0, 2), 99);
    CHECK_EQ(surface.At(4, 2), 99);
    CHECK_EQ(surface.At(1, 1), 99);

    // Clipped at the top-left corner of a pane that starts at (2, 1): only the glyph's bottom-right pixel lands.
    TestSurface clipped(8, 6, 99);
    clipped.Pane = MCPane{clipped.Window, 2, 1, 5, 4};
    CHECK_EQ(VfxCharacterDraw(clipped.Pane, -2, -2, font, 'B', std::nullopt), 3);
    CHECK_EQ(clipped.At(2, 1), 15);
    CHECK_EQ(clipped.At(1, 1), 99);
    CHECK_EQ(clipped.At(2, 0), 99);
    // Wholly outside: nothing drawn, the width still returned.
    CHECK_EQ(VfxCharacterDraw(clipped.Pane, 10, 0, font, 'A', std::nullopt), 2);
    // Clipped at the right and bottom.
    CHECK_EQ(VfxCharacterDraw(clipped.Pane, 2, 2, font, 'B', std::nullopt), 3);
    CHECK_EQ(clipped.At(4, 3), 7);
    CHECK_EQ(clipped.At(5, 3), 8);
    CHECK_EQ(clipped.At(5, 4), 11);
    CHECK_EQ(clipped.At(6, 3), 99);
    CHECK_EQ(clipped.At(4, 5), 99);
}

TEST_CASE("vfx font: a colour table maps pixels, 255 is transparent, strings advance")
{
    std::vector<uint8_t> font = MakeFont();
    uint8_t table[256];

    for (int i = 0; i < 256; ++i)
    {
        table[i] = static_cast<uint8_t>(i + 100);
    }

    table[1] = 255;
    table[4] = 255;

    TestSurface surface(8, 4, 50);
    VfxStringDraw(surface.Pane, 0, 0, font, "AB", table);
    CHECK_EQ(surface.At(0, 0), 50); // pixel 1 transparent
    CHECK_EQ(surface.At(1, 0), 102);
    CHECK_EQ(surface.At(0, 1), 103);
    CHECK_EQ(surface.At(1, 1), 50);  // pixel 4 transparent
    CHECK_EQ(surface.At(2, 0), 107); // 'B' starts after 'A''s 2 columns
    CHECK_EQ(surface.At(4, 2), 115);
    CHECK_EQ(surface.At(5, 0), 50);
}

TEST_CASE("vfx pcx: an 8-bit RLE picture decodes row by row")
{
    std::vector<uint8_t> pcx(0x80, 0);
    pcx[0] = 10;
    pcx[1] = 5;
    pcx[2] = 1;
    pcx[3] = 8;
    pcx[8] = 3;   // xMax
    pcx[0xa] = 1; // yMax
    pcx[0x41] = 1;
    pcx[0x42] = 4; // bytes per line
    const uint8_t data[] = {0xc3, 7, 9, /* row 1 */ 0xc1, 0xc5, 1, 2, 0xc2, 3};
    pcx.insert(pcx.end(), data, data + sizeof(data));
    std::vector<uint8_t> palette(0x300);

    for (int i = 0; i < 0x300; ++i)
    {
        palette[i] = static_cast<uint8_t>(i);
    }

    pcx.push_back(12);
    pcx.insert(pcx.end(), palette.begin(), palette.end());

    CHECK_EQ(VfxPcxResolution(pcx), (4 << 16) | 2);
    TestSurface surface(6, 3, 0xee);
    CHECK_EQ(VfxPcxDraw(surface.Pane, pcx), 0);
    const uint8_t expected[2][4] = {{7, 7, 7, 9}, {0xc5, 1, 2, 3}};

    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            CHECK_EQ(surface.At(x, y), expected[y][x]);
        }
    }

    CHECK_EQ(surface.At(4, 0), 0xee);
    CHECK_EQ(surface.At(0, 2), 0xee);

    MCVfxRgb out[256];
    VfxPcxPalette(pcx, out);
    CHECK_EQ(out[1].R, 3 >> 2);
    CHECK_EQ(out[255].B, 0x2ff % 256 >> 2);
}

TEST_CASE("vfx gif: plain and interlaced pictures decode to their pixels")
{
    const int width = 3;
    const int height = 8;
    std::vector<uint8_t> pixels(width * height);

    for (size_t i = 0; i < pixels.size(); ++i)
    {
        pixels[i] = static_cast<uint8_t>((i * 7 + i / 5) % 4);
    }

    std::vector<uint8_t> gif = MakeGif(width, height, pixels, false);
    CHECK(VfxGifResolution(gif) == ((width << 16) | height));
    TestSurface surface(5, 10, 0xee);
    CHECK(VfxGifDraw(surface.Pane, gif) == 3);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            CHECK_EQ(surface.At(x, y), pixels[static_cast<size_t>(y * width + x)]);
        }
    }

    CHECK_EQ(surface.At(3, 0), 0xee);
    CHECK_EQ(surface.At(0, 8), 0xee);

    // Interlaced: stored rows 0, 4 | 2, 6 | 1, 3, 5, 7.
    const int order[8] = {0, 4, 2, 6, 1, 3, 5, 7};
    std::vector<uint8_t> stored;

    for (int row : order)
    {
        stored.insert(stored.end(), pixels.begin() + row * width, pixels.begin() + (row + 1) * width);
    }

    gif = MakeGif(width, height, stored, true);
    TestSurface interlaced(5, 10, 0xee);
    CHECK(VfxGifDraw(interlaced.Pane, gif).has_value());

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            CHECK_EQ(interlaced.At(x, y), pixels[static_cast<size_t>(y * width + x)]);
        }
    }

    MCVfxRgb palette[256] = {};
    CHECK(VfxGifPalette(gif, palette).has_value());
    CHECK_EQ(palette[1].R, 63);
    CHECK_EQ(palette[2].G, 32);
    CHECK_EQ(palette[3].B, 63);
    CHECK_EQ(palette[4].R, 0);
}

/// <summary>
/// A GIF the game can't draw is refused with its reason, before anything is drawn (issue #25: a zero-width image wrote
/// its pixels past the line, a code size of 12 or more ran over the code tables, and short files were read past).
/// Image data that ends early draws the pixels it holds.
/// </summary>
TEST_CASE("vfx gif: a damaged picture is refused, and a cut-off one draws what it holds")
{
    // MakeGif's layout: the image descriptor at 25 (its width at 30, height at 32), the code size at 35.
    const std::vector<uint8_t> pixels(3 * 8, 1);
    const std::vector<uint8_t> good = MakeGif(3, 8, pixels, false);
    const auto refused = [](std::vector<uint8_t> gif)
    {
        TestSurface surface(5, 10, 0xee);
        std::array<MCVfxRgb, 256> palette{};
        const bool refusedAll = !VfxGifDraw(surface.Pane, gif).has_value() && !VfxGifResolution(gif).has_value() &&
                                !VfxGifPalette(gif, palette).has_value();
        return refusedAll && std::ranges::all_of(surface.Pixels(), [](uint8_t p) { return p == 0xee; });
    };

    std::vector<uint8_t> zeroWidth = good;
    zeroWidth[30] = 0;
    CHECK(refused(zeroWidth));
    std::vector<uint8_t> zeroHeight = good;
    zeroHeight[32] = 0;
    CHECK(refused(zeroHeight));

    for (const uint8_t codeSize : {0, 12, 13, 255})
    {
        MCTest::Scope scope(std::format("code size {}", codeSize));
        std::vector<uint8_t> wide = good;
        wide[35] = codeSize;
        CHECK(refused(wide));
    }

    for (const size_t size : {0, 12, 20, 34, 35})
    {
        MCTest::Scope scope(std::format("{} bytes", size));
        CHECK(refused(std::vector<uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(size))));
    }

    // Cut inside the image data: the rows it holds are drawn, the rest left alone.
    TestSurface surface(5, 10, 0xee);
    const std::vector<uint8_t> cut(good.begin(), good.begin() + 40);
    CHECK(VfxGifDraw(surface.Pane, cut) == 3);
    CHECK_EQ(surface.At(0, 0), 1);
    CHECK_EQ(surface.At(0, 7), 0xee);
}

TEST_CASE("vfx ilbm: PBM pictures decode, raw and ByteRun1")
{
    const auto makeIff = [](std::string_view type, uint8_t compression, const std::vector<uint8_t>& body)
    {
        std::vector<uint8_t> iff = {'F', 'O', 'R', 'M', 0, 0, 0, 0};
        iff.insert(iff.end(), type.begin(), type.end());
        const std::array<uint8_t, 28> bmhd = {'B', 'M', 'H', 'D', 0,           0, 0, 20, 0, 4, 0, 2, 0, 0,
                                              0,   0,   8,   0,   compression, 0, 0, 9,  1, 1, 0, 4, 0, 2};
        iff.insert(iff.end(), bmhd.begin(), bmhd.end());
        const std::array<uint8_t, 8> header = {'B', 'O', 'D', 'Y', 0, 0, 0, static_cast<uint8_t>(body.size())};
        iff.insert(iff.end(), header.begin(), header.end());
        iff.insert(iff.end(), body.begin(), body.end());
        return iff;
    };

    std::vector<uint8_t> raw = makeIff("PBM ", 0, {1, 2, 3, 4, 5, 6, 7, 8});
    CHECK_EQ(VfxIlbmResolution(raw), (4 << 16) | 2);
    TestSurface surface(5, 3, 0xee);
    CHECK_EQ(VfxIlbmDraw(surface.Pane, raw), 9);

    for (int i = 0; i < 8; ++i)
    {
        CHECK_EQ(surface.At(i % 4, i / 4), static_cast<uint8_t>(i + 1));
    }

    CHECK_EQ(surface.At(4, 0), 0xee);

    std::vector<uint8_t> packed = makeIff("PBM ", 1, {0xfd, 5, /* row 1 */ 1, 6, 7, 0xff, 8});
    TestSurface unpacked(5, 3, 0xee);
    VfxIlbmDraw(unpacked.Pane, packed);
    const uint8_t expected[8] = {5, 5, 5, 5, 6, 7, 8, 8};

    for (int i = 0; i < 8; ++i)
    {
        CHECK_EQ(unpacked.At(i % 4, i / 4), expected[i]);
    }
}

TEST_CASE("vfx color_scan: distinct colours in scan order")
{
    TestSurface surface(4, 3, 0);
    std::ranges::copy(std::to_array<uint8_t>({1, 2, 3, 4, /**/ 5, 6, 7, 8, /**/ 9, 9, 9, 9}), surface.Pixels().begin());
    MCPane pane{surface.Window, 1, 0, 2, 1};
    uint32_t colors[256] = {};
    CHECK_EQ(VfxColorScan(pane, colors), 4);
    // Rows top to bottom, each right to left.
    CHECK_EQ(colors[0], 3u);
    CHECK_EQ(colors[1], 2u);
    CHECK_EQ(colors[2], 7u);
    CHECK_EQ(colors[3], 6u);
    CHECK_EQ(VfxColorScan(surface.Pane, {}), 9);
}

TEST_CASE("vfx window_fade: the used colours reach the target palette")
{
    TestSurface surface(4, 2, 1);
    surface.Pixels()[3] = 2;
    MCVfxRgb target[256] = {};

    for (int i = 0; i < 256; ++i)
    {
        VfxDacPalette[i] = MCVfxRgb{10, 20, 30};
        target[i] = MCVfxRgb{0, 0, 0};
    }

    target[1] = MCVfxRgb{63, 0, 30};
    target[2] = MCVfxRgb{0, 40, 5};
    static int waits;
    waits = 0;
    VfxWaitRetraceHook = [] { ++waits; };
    VfxWindowFade(*surface.Window, target, 20);
    VfxWaitRetraceHook = {};
    CHECK_EQ(VfxDacPalette[1].R, 63);
    CHECK_EQ(VfxDacPalette[1].G, 0);
    CHECK_EQ(VfxDacPalette[1].B, 30);
    CHECK_EQ(VfxDacPalette[2].R, 0);
    CHECK_EQ(VfxDacPalette[2].G, 40);
    CHECK_EQ(VfxDacPalette[2].B, 5);
    CHECK_EQ(VfxDacPalette[3].R, 10); // not in the window: untouched
    CHECK(waits >= 19 && waits <= 21);
}

TEST_CASE("game: palettex.gif decodes to the reference pixels")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    std::vector<uint8_t> gif = ReadGameFile("data\\palette\\palettex.gif");
    REQUIRE(!gif.empty());
    CHECK(VfxGifResolution(gif) == ((640 << 16) | 400));

    TestSurface surface(640, 400, 0);
    CHECK(VfxGifDraw(surface.Pane, gif) == 16);
    // FNV-1a of the pixels as an independent decoder (PIL) gives them.
    uint32_t hash = 0x811c9dc5;

    for (uint8_t p : surface.Pixels())
    {
        hash = (hash ^ p) * 0x01000193;
    }

    CHECK_EQ(hash, 0xa27515bfu);
    CHECK_EQ(VfxColorScan(surface.Pane, {}), 155);

    MCVfxRgb palette[256];
    CHECK(VfxGifPalette(gif, palette).has_value());
    CHECK_EQ(palette[0].R, 63);
    CHECK_EQ(palette[0].G, 0);
    CHECK_EQ(palette[0].B, 56);
}

TEST_CASE("game: the fonts decode and draw")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();

    for (const std::string_view name :
         {"data\\fonts\\WHITE12.FNT", "data\\fonts\\DIM10.FNT", "data\\fonts\\LBLACK.FNT"})
    {
        MCTest::Scope scope(name);
        std::vector<uint8_t> font = ReadGameFile(name);
        REQUIRE(font.size() > 0x10 + 4);
        int32_t count;
        std::memcpy(&count, font.data() + 4, 4);
        const int32_t height = VfxFontHeight(font);
        CHECK(height > 4 && height < 32);
        // char_count can exceed 256 (WHITE12.FNT has 280 entries, LBLACK.FNT 770); strings index only the first 256.
        REQUIRE(count > 0 && 0x10 + static_cast<size_t>(count) * 4 <= font.size());
        CHECK(VfxFontFits(font));

        // A glyph whose width runs past the data, or a table that does, is refused; a character past the table has
        // no width.
        CHECK_EQ(VfxCharacterWidth(font, count), 0);
        std::vector<uint8_t> damaged = font;
        int32_t firstGlyph = 0;
        std::memcpy(&firstGlyph, damaged.data() + 0x10, 4);
        const int32_t hugeWidth = 0x7fffffff;
        std::memcpy(damaged.data() + firstGlyph, &hugeWidth, 4);
        CHECK(!VfxFontFits(damaged));
        const int32_t longTable = static_cast<int32_t>(font.size());
        damaged = font;
        std::memcpy(damaged.data() + 4, &longTable, 4);
        CHECK(!VfxFontFits(damaged));

        // Every glyph lies inside the file.
        for (int32_t c = 0; c < count; ++c)
        {
            int32_t offset;
            std::memcpy(&offset, font.data() + 0x10 + c * 4, 4);
            REQUIRE(offset >= 0x10 + count * 4 && static_cast<size_t>(offset) + 4 <= font.size());
            const int32_t width = VfxCharacterWidth(font, c);
            CHECK(width >= 0 && width < 64);
            CHECK(static_cast<size_t>(offset) + 4 + static_cast<size_t>(width) * height <= font.size());
        }

        CHECK(VfxCharacterWidth(font, 'M') > 0);

        // Drawn through an identity table with the background made transparent, the text stays in its box.
        int32_t background;
        std::memcpy(&background, font.data() + 0xc, 4);
        uint8_t table[256];

        for (int i = 0; i < 256; ++i)
        {
            table[i] = static_cast<uint8_t>(i);
        }

        table[static_cast<uint8_t>(background)] = 255;
        TestSurface surface(200, 40, 0xfe);
        VfxStringDraw(surface.Pane, 5, 5, font, "MechCommander", table);
        int32_t drawn = 0;

        for (int y = 0; y < 40; ++y)
        {
            for (int x = 0; x < 200; ++x)
            {
                if (surface.At(x, y) == 0xfe)
                {
                    continue;
                }

                ++drawn;
                CHECK(y >= 5 && y < 5 + height && x >= 5);
            }
        }

        CHECK(drawn > 20);
    }
}

namespace
{
    /// <summary>A font's glyph: its width and its pixels, row by row (the layout in vfx/MCVfxFunctions.h).</summary>
    struct MCTestGlyph
    {
        int32_t Width = 0;
        std::vector<uint8_t> Pixels;

        bool operator==(const MCTestGlyph&) const = default;
    };

    MCTestGlyph GlyphOf(std::span<const uint8_t> font, uint8_t character)
    {
        int32_t offset = 0;
        std::memcpy(&offset, font.data() + 0x10 + static_cast<size_t>(character) * 4, 4);
        MCTestGlyph glyph{VfxCharacterWidth(font, character), {}};
        const auto pixels = font.subspan(static_cast<size_t>(offset) + 4,
                                         static_cast<size_t>(glyph.Width) * static_cast<size_t>(VfxFontHeight(font)));
        glyph.Pixels.assign(pixels.begin(), pixels.end());
        return glyph;
    }

    /// <summary>Game font <paramref name="name"/> as the files hold it, and with MCFontGlyphs' glyphs added.</summary>
    std::pair<std::vector<uint8_t>, std::vector<uint8_t>> FontBeforeAndAfter(std::string_view name)
    {
        std::vector<uint8_t> loaded = ReadGameFile(name);
        REQUIRE(VfxFontFits(loaded));
        std::vector<uint8_t> filled = loaded;
        MCFontGlyphs::AddMissing(filled);
        REQUIRE(VfxFontFits(filled));
        return {std::move(loaded), std::move(filled)};
    }
}

/// <summary>
/// Port-only (MCFontGlyphs): WHITE.FNT gets glyphs for the characters it lists blank: ´ is é's mark alone (its rows
/// above the e it shares with ë), ¿ is ? turned half round, and § is drawn in the font's ink from its capitals' top
/// row, with a clear column on its left. Its own glyphs stay as they were (¡ among them, the one character of
/// 0x80-0xBF it draws) and a character nothing makes (£) stays blank;
/// the drop-shadow font gets the cut glyphs but not the drawn ones (it has two inks), and a font listing nothing past
/// 0x8B is left alone.
/// </summary>
TEST_CASE("game: the fonts get glyphs for the characters they leave blank")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    constexpr uint8_t Clear = 0xff;
    constexpr uint8_t Pound = 0xa3;
    constexpr uint8_t Section = 0xa7;
    constexpr uint8_t Acute = 0xb4;
    constexpr uint8_t InvertedQuestion = 0xbf;
    constexpr uint8_t AcuteE = 0xe9;
    // WHITE.FNT's é: its mark is in rows 0-1, the e in the rows below; its ? fills rows 0-5 and columns 1-4.
    constexpr int32_t MarkRows = 2;
    constexpr int32_t QuestionRows = 6;
    constexpr int32_t QuestionLeft = 1;
    constexpr int32_t QuestionRight = 4;
    const auto [loaded, font] = FontBeforeAndAfter("data\\fonts\\WHITE.FNT");
    const int32_t height = VfxFontHeight(font);
    int32_t count = 0;
    std::memcpy(&count, font.data() + 4, 4);

    for (const uint8_t character : {0xa7, 0xa8, 0xab, 0xb0, 0xb2, 0xb4, 0xba, 0xbb, 0xbd, 0xbf})
    {
        MCTest::Scope scope(std::format("character {:#x}", character));
        CHECK(!MCFontGlyphs::Draws(loaded, character));
        CHECK(MCFontGlyphs::Draws(font, character));
    }

    CHECK(!MCFontGlyphs::Draws(font, Pound));

    for (int32_t character = 0; character < std::min(count, 256); character++)
    {
        if (MCFontGlyphs::Draws(loaded, static_cast<uint8_t>(character)))
        {
            MCTest::Scope scope(std::format("character {:#x}", character));
            CHECK(GlyphOf(font, static_cast<uint8_t>(character)) == GlyphOf(loaded, static_cast<uint8_t>(character)));
        }
    }

    const MCTestGlyph acute = GlyphOf(font, Acute);
    const MCTestGlyph acuteE = GlyphOf(font, AcuteE);
    REQUIRE_EQ(acute.Width, acuteE.Width);

    for (size_t i = 0; i < acute.Pixels.size(); i++)
    {
        const bool mark = static_cast<int32_t>(i) / acute.Width < MarkRows;
        CHECK_EQ(acute.Pixels[i], mark ? acuteE.Pixels[i] : Clear);
    }

    const MCTestGlyph inverted = GlyphOf(font, InvertedQuestion);
    const MCTestGlyph question = GlyphOf(font, '?');
    REQUIRE_EQ(inverted.Width, question.Width);

    for (int32_t y = 0; y < height; y++)
    {
        const int32_t fromY = y < QuestionRows ? QuestionRows - 1 - y : y;

        for (int32_t x = 0; x < question.Width; x++)
        {
            const int32_t fromX = x >= QuestionLeft && x <= QuestionRight ? QuestionLeft + QuestionRight - x : x;
            CHECK_EQ(inverted.Pixels[static_cast<size_t>(y * question.Width + x)],
                     question.Pixels[static_cast<size_t>(fromY * question.Width + fromX)]);
        }
    }

    // § in the ink of H, from H's top row.
    const MCTestGlyph capital = GlyphOf(font, 'H');
    const MCTestGlyph section = GlyphOf(font, Section);
    const auto firstInk = [](const MCTestGlyph& glyph)
    { return std::ranges::find_if(glyph.Pixels, [](uint8_t pixel) { return pixel != Clear; }); };
    const uint8_t ink = *firstInk(capital);
    CHECK_EQ(std::distance(section.Pixels.begin(), firstInk(section)) / section.Width,
             std::distance(capital.Pixels.begin(), firstInk(capital)) / capital.Width);

    for (size_t i = 0; i < section.Pixels.size(); i++)
    {
        CHECK((section.Pixels[i] == Clear || section.Pixels[i] == ink));
        CHECK((i % static_cast<size_t>(section.Width) != 0 || section.Pixels[i] == Clear));
    }

    const auto [dropLoaded, drop] = FontBeforeAndAfter("data\\fonts\\YELLDRP.FNT");
    CHECK(MCFontGlyphs::Draws(drop, Acute));
    CHECK(!MCFontGlyphs::Draws(drop, Section));

    const auto [dimLoaded, dim] = FontBeforeAndAfter("data\\fonts\\DIM.FNT");
    CHECK(dim == dimLoaded);
}
