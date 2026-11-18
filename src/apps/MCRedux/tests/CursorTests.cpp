#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCHardwareCursor.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCPacketFile.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxFunctions.h"

// The system (hardware) cursor: cursor pictures, their colours and scaling, and the game's cursor shapes as pictures.

namespace
{
    /// <summary>A picture from rows of characters: '.' transparent, any other character that palette index.</summary>
    MCCursorImage FromRows(std::initializer_list<std::string_view> rows, int hotX, int hotY)
    {
        const int height = static_cast<int>(rows.size());
        const int width = static_cast<int>(rows.begin()->size());
        MCCursorImage image = MCCursorImage::Blank(width, height, hotX, hotY);
        int y = 0;

        for (const std::string_view row : rows)
        {
            for (int x = 0; x < width; ++x)
            {
                if (row[x] == '.')
                {
                    continue;
                }

                image.Pixels[static_cast<size_t>(y * width + x)] = static_cast<uint8_t>(row[x]);
                image.Opaque[static_cast<size_t>(y * width + x)] = 1;
            }

            ++y;
        }

        return image;
    }

    /// <summary>The picture as rows of characters, '.' for transparent.</summary>
    std::vector<std::string> ToRows(const MCCursorImage& image)
    {
        std::vector<std::string> rows;

        for (int y = 0; y < image.Height; ++y)
        {
            std::string row;

            for (int x = 0; x < image.Width; ++x)
            {
                const size_t i = static_cast<size_t>(y * image.Width + x);
                row += image.Opaque[i] != 0 ? static_cast<char>(image.Pixels[i]) : '.';
            }

            rows.push_back(row);
        }

        return rows;
    }

    /// <summary>A palette where index i is (i, 255 - i, i / 2).</summary>
    std::array<SDL_Color, 256> TestColors()
    {
        std::array<SDL_Color, 256> colors{};

        for (int i = 0; i < 256; ++i)
        {
            colors[static_cast<size_t>(i)] = {static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i),
                                              static_cast<uint8_t>(i / 2), 255};
        }

        return colors;
    }
}

TEST_CASE("cursor: Overlay puts both hot spots on one point and keeps the lower picture where the upper is clear")
{
    // A 4x4 dragged icon with the mouse at (1, 2) on it, and a 3x2 arrow whose tip (hot spot) is its top-left.
    const MCCursorImage icon = FromRows({"AAAA", "AAAA", "AAAA", "AAAA"}, 1, 2);
    const MCCursorImage arrow = FromRows({"BB.", "B.."}, 0, 0);
    const MCCursorImage both = MCCursorImage::Overlay(icon, arrow);
    CHECK_EQ(both.HotX, 1);
    CHECK_EQ(both.HotY, 2);
    const std::vector<std::string> expected{"AAAA", "AAAA", "ABBA", "ABAA"};
    CHECK(ToRows(both) == expected);
}

TEST_CASE("cursor: Overlay grows past either picture")
{
    // The arrow hangs off the icon's bottom-right corner.
    const MCCursorImage icon = FromRows({"AA", "AA"}, 1, 1);
    const MCCursorImage arrow = FromRows({"BBB", "BBB"}, 0, 0);
    const MCCursorImage both = MCCursorImage::Overlay(icon, arrow);
    CHECK_EQ(both.Width, 4);
    CHECK_EQ(both.Height, 3);
    CHECK_EQ(both.HotX, 1);
    CHECK_EQ(both.HotY, 1);
    const std::vector<std::string> expected{"AA..", "ABBB", ".BBB"};
    CHECK(ToRows(both) == expected);
}

TEST_CASE("cursor: WithHotSpotInside grows a picture whose hot spot is off it")
{
    const MCCursorImage inside = FromRows({"AB", "CD"}, 1, 1);
    CHECK(MCCursorImage::WithHotSpotInside(inside) == inside);

    // Hot spot two pixels left of the picture: two transparent columns are added on the left.
    const MCCursorImage outside = FromRows({"AB"}, -2, 0);
    const MCCursorImage grown = MCCursorImage::WithHotSpotInside(outside);
    CHECK_EQ(grown.HotX, 0);
    CHECK_EQ(grown.HotY, 0);
    const std::vector<std::string> expected{"..AB"};
    CHECK(ToRows(grown) == expected);
}

TEST_CASE("cursor: Rasterize colours each pixel from the palette and leaves transparent pixels clear")
{
    const std::array<SDL_Color, 256> colors = TestColors();
    const MCCursorImage image = FromRows({"\x10.", ".\xf0"}, 0, 0);
    const MCCursorBitmap bitmap = MCCursor::Rasterize(image, colors, 1.0f, 1.0f);
    REQUIRE_EQ(bitmap.Width, 2);
    REQUIRE_EQ(bitmap.Height, 2);
    const std::vector<uint8_t> expected{
        0x10, 0xef, 0x08, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0xf0, 0x0f, 0x78, 255,
    };

    CHECK(bitmap.Rgba == expected);
}

TEST_CASE("cursor: Rasterize scales by the nearest pixel and puts the hot spot on its scaled pixel's corner")
{
    const std::array<SDL_Color, 256> colors = TestColors();
    const MCCursorImage image = FromRows({"AB", "C."}, 1, 1);

    // Twice as large: each pixel becomes 2x2; the hot spot pixel (1, 1) starts at (2, 2).
    const MCCursorBitmap doubled = MCCursor::Rasterize(image, colors, 2.0f, 2.0f);
    REQUIRE_EQ(doubled.Width, 4);
    REQUIRE_EQ(doubled.Height, 4);
    CHECK_EQ(doubled.HotX, 2);
    CHECK_EQ(doubled.HotY, 2);
    auto red = [&](const MCCursorBitmap& b, int x, int y)
    { return b.Rgba[static_cast<size_t>((y * b.Width + x) * 4)]; };
    auto alpha = [&](const MCCursorBitmap& b, int x, int y)
    { return b.Rgba[static_cast<size_t>((y * b.Width + x) * 4 + 3)]; };
    CHECK_EQ(red(doubled, 1, 1), 'A');
    CHECK_EQ(red(doubled, 2, 0), 'B');
    CHECK_EQ(red(doubled, 3, 1), 'B');
    CHECK_EQ(red(doubled, 0, 3), 'C');
    CHECK_EQ(alpha(doubled, 2, 2), 0);
    CHECK_EQ(alpha(doubled, 3, 3), 0);

    // A stretched screen scales across and down apart: 1280x1024 shown in 1920x1080 is 1.5 x ~1.05.
    const MCCursorBitmap stretched = MCCursor::Rasterize(image, colors, 1.5f, 1.0f);
    CHECK_EQ(stretched.Width, 3);
    CHECK_EQ(stretched.Height, 2);
    CHECK_EQ(stretched.HotX, 1);
    CHECK_EQ(stretched.HotY, 1);
    CHECK_EQ(red(stretched, 0, 0), 'A');
    CHECK_EQ(red(stretched, 1, 0), 'A');
    CHECK_EQ(red(stretched, 2, 0), 'B');
}

namespace
{
    /// <summary>
    /// A one-row picture of <paramref name="indices"/> whose HD copy (<paramref name="scale"/> times its size) is
    /// <paramref name="drawn"/> all over, the pack made with <paramref name="madeWith"/>'s colours.
    /// </summary>
    MCCursorImage WithHdCopy(std::string_view indices, int scale, const std::array<uint8_t, 3>& drawn,
                             const std::array<SDL_Color, 256>& madeWith)
    {
        MCCursorImage image = FromRows({indices}, 0, 0);
        auto hd = std::make_shared<MCCursorHd>();
        hd->Scale = scale;

        for (const SDL_Color& color : madeWith)
        {
            hd->MadeWith.insert(hd->MadeWith.end(), {color.r, color.g, color.b});
        }

        for (int i = 0; i < image.Width * scale * scale; ++i)
        {
            hd->Rgba.insert(hd->Rgba.end(), {drawn[0], drawn[1], drawn[2], 255});
        }

        image.Hd = hd;
        return image;
    }

    /// <summary>Every bitmap pixel's colour channels within <paramref name="slack"/> of <paramref name="expected"/>.</summary>
    bool AllNear(const MCCursorBitmap& bitmap, const std::array<uint8_t, 3>& expected, int slack)
    {
        for (size_t i = 0; i < bitmap.Rgba.size(); i += 4)
        {
            for (size_t c = 0; c < expected.size(); ++c)
            {
                if (std::abs(static_cast<int>(bitmap.Rgba[i + c]) - static_cast<int>(expected[c])) > slack)
                {
                    return false;
                }
            }
        }

        return true;
    }
}

/// <summary>
/// Port-only: a picture with an HD copy is drawn from it, between its pixels (so its edges are smooth at any size), each
/// colour corrected by today's colours of the original pixels around it against the pack's.
/// </summary>
TEST_CASE("cursor: Rasterize draws a picture's HD copy smoothly, in today's colours")
{
    constexpr uint8_t Left = 1;
    constexpr uint8_t Right = 2;
    constexpr int HdScale = 2;
    // The pack's colours of the two pixels, and today's: the left one half as bright now.
    constexpr std::array<uint8_t, 3> LeftThen = {200, 40, 40};
    constexpr std::array<uint8_t, 3> RightThen = {40, 40, 200};
    MCCursorImage image = FromRows({"\x01\x02"}, 0, 0);
    auto hd = std::make_shared<MCCursorHd>();
    hd->Scale = HdScale;
    hd->MadeWith.assign(768, 0);
    std::ranges::copy(LeftThen, hd->MadeWith.begin() + Left * 3);
    std::ranges::copy(RightThen, hd->MadeWith.begin() + Right * 3);

    for (int y = 0; y < HdScale; ++y)
    {
        for (int x = 0; x < 2 * HdScale; ++x)
        {
            const std::array<uint8_t, 3>& color = x < HdScale ? LeftThen : RightThen;
            hd->Rgba.insert(hd->Rgba.end(), {color[0], color[1], color[2], 255});
        }
    }

    image.Hd = hd;
    std::array<SDL_Color, 256> colors{};
    colors[Left] = SDL_Color{100, 20, 20, 255};
    colors[Right] = SDL_Color{40, 40, 200, 255};
    constexpr float Scale = 4.0f;
    const MCCursorBitmap bitmap = MCCursor::Rasterize(image, colors, Scale, Scale);
    REQUIRE_EQ(bitmap.Width, 8);
    REQUIRE_EQ(bitmap.Height, 4);
    const auto pixel = [&](int x, int y)
    { return std::span(bitmap.Rgba).subspan(static_cast<size_t>((y * bitmap.Width + x) * 4), 4); };

    CHECK_EQ(pixel(1, 1)[0], 100);
    CHECK_EQ(pixel(1, 1)[3], 255);
    CHECK_EQ(pixel(6, 2)[2], 200);
    // Where the two meet, a colour between them (the nearest pixel would give one or the other).
    CHECK(pixel(4, 1)[0] > RightThen[0]);
    CHECK(pixel(4, 1)[0] < LeftThen[0]);
}

/// <summary>
/// Port-only: an HD copy drawn over the original's black pixels (a drawn cursor's highlight beside where the
/// original's lay) shows as drawn: a channel the pack's colour has at 0 doesn't say how today's palette changed it.
/// </summary>
TEST_CASE("cursor: Rasterize shows an HD copy as drawn over black pixels when today's colours are the pack's")
{
    constexpr uint8_t Black = 1;
    constexpr uint8_t Red = 2;
    constexpr uint8_t Grey = 3;
    constexpr std::array<uint8_t, 3> Drawn = {220, 210, 200};
    std::array<SDL_Color, 256> colors{};
    colors[Black] = SDL_Color{0, 0, 0, 255};
    colors[Red] = SDL_Color{200, 0, 0, 255};
    colors[Grey] = SDL_Color{160, 160, 160, 255};

    const auto shown = [&](std::string_view indices)
    { return MCCursor::Rasterize(WithHdCopy(indices, 2, Drawn, colors), colors, 3.0f, 3.0f); };

    REQUIRE_EQ(shown("\x01\x01\x01").Width, 9);
    CHECK(AllNear(shown("\x01\x01\x01"), Drawn, 0));
    // Beside pure red, which has no green or blue either.
    CHECK(AllNear(shown("\x01\x02\x01"), Drawn, 0));
    CHECK(AllNear(shown("\x01\x03\x01"), Drawn, 0));
}

namespace
{
    constexpr uint8_t GameBlack = 16;
    constexpr uint8_t GameWhite = 31;
    constexpr uint8_t GameGrey = 36;
    constexpr size_t FirstGameEntry = 10;
    constexpr size_t EndGameEntry = 0xf6;
    constexpr std::array<uint8_t, 3> GreyMade = {116, 120, 124};
    constexpr std::array<uint8_t, 3> WhiteMade = {252, 252, 252};

    /// <summary>
    /// A palette as the pack's: its game entries hold every value the game's colours have (multiples of 4), with a
    /// cursor's black, white and grey among them.
    /// </summary>
    std::array<SDL_Color, 256> PackPalette()
    {
        constexpr size_t GreenStep = 3;
        constexpr size_t BlueStep = 5;
        constexpr size_t ValueStep = 4;
        std::array<SDL_Color, 256> colors{};

        for (size_t entry = FirstGameEntry; entry < EndGameEntry; ++entry)
        {
            colors[entry] = SDL_Color{static_cast<uint8_t>(entry * ValueStep % 256),
                                      static_cast<uint8_t>(entry * GreenStep * ValueStep % 256),
                                      static_cast<uint8_t>(entry * BlueStep * ValueStep % 256), 255};
        }

        colors[GameBlack] = SDL_Color{0, 0, 0, 255};
        colors[GameWhite] = SDL_Color{WhiteMade[0], WhiteMade[1], WhiteMade[2], 255};
        colors[GameGrey] = SDL_Color{GreyMade[0], GreyMade[1], GreyMade[2], 255};
        return colors;
    }

    /// <summary>A brightness step, as the game's gamma table bends (whole values, rounded down).</summary>
    uint8_t Brighter(float value)
    {
        constexpr float Bend = 0.6f;
        return static_cast<uint8_t>(std::floor(255.0f * std::pow(value / 255.0f, Bend)));
    }

    /// <summary><paramref name="colors"/> with every game entry brighter (<see cref="Brighter"/>).</summary>
    std::array<SDL_Color, 256> Brightened(std::array<SDL_Color, 256> colors)
    {
        for (size_t entry = FirstGameEntry; entry < EndGameEntry; ++entry)
        {
            SDL_Color& color = colors[entry];
            color = {Brighter(color.r), Brighter(color.g), Brighter(color.b), 255};
        }

        return colors;
    }

    /// <summary>The colour of <paramref name="bitmap"/>'s pixel (x, y), its alpha last.</summary>
    std::array<uint8_t, 4> PixelOf(const MCCursorBitmap& bitmap, int x, int y)
    {
        const size_t at = static_cast<size_t>((y * bitmap.Width + x) * 4);
        return {bitmap.Rgba[at], bitmap.Rgba[at + 1], bitmap.Rgba[at + 2], bitmap.Rgba[at + 3]};
    }
}

/// <summary>
/// Port-only: with a brightness step (the game's palette through one curve) an HD copy goes through the same curve, its
/// colours between the palette's too, also where it was drawn over the original's black pixels (which stay black, so
/// say nothing of the step).
/// </summary>
TEST_CASE("cursor: Rasterize puts an HD copy through a brightness step's curve, over black pixels too")
{
    constexpr std::array<uint8_t, 3> Drawn = {230, 150, 70};
    const std::array<SDL_Color, 256> made = PackPalette();
    const MCCursorImage highlight = WithHdCopy("\x10\x10\x10\x1f", 2, Drawn, made);
    const MCCursorBitmap shown = MCCursor::Rasterize(highlight, Brightened(made), 3.0f, 3.0f);

    CHECK(AllNear(shown, {Brighter(Drawn[0]), Brighter(Drawn[1]), Brighter(Drawn[2])}, 2));
    CHECK_EQ(PixelOf(shown, 4, 1)[3], 255);
}

/// <summary>Port-only: between screens, the game's colours black, an HD copy is black too.</summary>
TEST_CASE("cursor: Rasterize shows an HD copy black when the game's colours are, between screens")
{
    const std::array<SDL_Color, 256> made = PackPalette();
    std::array<SDL_Color, 256> black = made;
    std::fill(black.begin() + FirstGameEntry, black.begin() + EndGameEntry, SDL_Color{0, 0, 0, 255});
    const MCCursorBitmap shown =
        MCCursor::Rasterize(WithHdCopy("\x10\x1f\x24", 2, {200, 180, 160}, made), black, 3.0f, 3.0f);

    CHECK(AllNear(shown, {0, 0, 0}, 0));
    CHECK_EQ(PixelOf(shown, 4, 1)[3], 255);
}

/// <summary>
/// Port-only: on a screen with a palette of its own (the logistics screens' grey is blue-grey), no one curve takes the
/// pack's colours to today's, so the HD copy is recoloured where the picture's colours changed and shown as made where
/// they didn't.
/// </summary>
TEST_CASE("cursor: Rasterize recolours an HD copy by the picture's own colours on a screen with its own palette")
{
    constexpr std::array<uint8_t, 3> GreyToday = {72, 96, 144};
    const std::array<SDL_Color, 256> made = PackPalette();
    std::array<SDL_Color, 256> screen = made;
    screen[GameGrey] = SDL_Color{GreyToday[0], GreyToday[1], GreyToday[2], 255};
    // A grey pixel beside a white one, each drawn in its colour as made.
    const MCCursorImage image = WithHdCopy("\x24\x1f", 4, GreyMade, made);
    auto hd = std::make_shared<MCCursorHd>(*image.Hd);
    constexpr int HdScale = 4;

    for (int y = 0; y < HdScale; ++y)
    {
        for (int x = HdScale; x < 2 * HdScale; ++x)
        {
            std::ranges::copy(WhiteMade, hd->Rgba.begin() + (y * 2 * HdScale + x) * 4);
        }
    }

    MCCursorImage twoColours = image;
    twoColours.Hd = hd;
    const MCCursorBitmap shown = MCCursor::Rasterize(twoColours, screen, 4.0f, 4.0f);
    const std::array<uint8_t, 4> grey = PixelOf(shown, 1, 1);
    const std::array<uint8_t, 4> white = PixelOf(shown, 6, 2);

    for (size_t c = 0; c < GreyToday.size(); ++c)
    {
        CHECK(std::abs(grey[c] - GreyToday[c]) <= 1);
        CHECK_EQ(white[c], WhiteMade[c]);
    }
}

/// <summary>
/// Port-only: the game rewrites the water's palette entries as the water moves, so they hold other entries' colours, off
/// the curve: they are left out of it, and a brightness step on a map with water goes through the curve all the same.
/// </summary>
TEST_CASE("cursor: Rasterize leaves the water's entries out of a brightness step's curve")
{
    constexpr size_t FirstWater = 0xd8;
    constexpr size_t WaterEntries = 8;
    constexpr std::array<uint8_t, 3> Drawn = {230, 150, 70};
    const std::array<SDL_Color, 256> made = PackPalette();
    const std::array<SDL_Color, 256> bright = Brightened(made);
    // The water one step on: each entry in the next one's colour.
    std::array<SDL_Color, 256> moved = bright;

    for (size_t i = 0; i < WaterEntries; ++i)
    {
        moved[FirstWater + i] = bright[FirstWater + (i + 1) % WaterEntries];
    }

    const MCCursorImage highlight = WithHdCopy("\x10\x10\x10\x1f", 2, Drawn, made);

    CHECK_EQ(MCCursor::Rasterize(highlight, moved, 3.0f, 3.0f).Rgba,
             MCCursor::Rasterize(highlight, bright, 3.0f, 3.0f).Rgba);
}

TEST_CASE("cursor: a pane becomes an opaque picture with the given hot spot")
{
    // A 3x2 pane at (1, 1) inside a 5x4 window.
    const std::shared_ptr<MCWindow> window = MCWindow::Make(5, 4);
    const std::span<uint8_t> pixels = window->Bytes();

    for (size_t i = 0; i < pixels.size(); ++i)
    {
        pixels[i] = static_cast<uint8_t>('a' + i);
    }

    const MCPane pane{window, 1, 1, 3, 2};
    const MCCursorImage image = MCCursorImageFromPane(pane, 2, 0);
    CHECK_EQ(image.HotX, 2);
    CHECK_EQ(image.HotY, 0);
    const std::vector<std::string> expected{"ghi", "lmn"};
    CHECK(ToRows(image) == expected);
}

TEST_CASE("cursor: a shape becomes a picture that draws exactly as the game draws the shape")
{
    // A hand-made shape: 3x3, origin at its centre, a plus sign of colour 7 with transparent corners.
    const std::shared_ptr<MCWindow> sourceWindow = MCWindow::Make(9, 9);
    const std::span<uint8_t> source = sourceWindow->Bytes();
    std::ranges::fill(source, uint8_t{0xff});
    const MCPane sourcePane{sourceWindow, 0, 0, 2, 2};
    source[1] = 7;
    source[9 + 0] = 7;
    source[9 + 1] = 7;
    source[9 + 2] = 7;
    source[18 + 1] = 7;
    std::vector<uint8_t> shape(4096);
    const int32_t size = VfxShapeScan(sourcePane, 0xff, 1, 1, shape);
    REQUIRE(size > 0x18);
    // A one-shape table around it: version, count, the shape's offset and its (absent) palette's.
    std::vector<uint8_t> table(16 + static_cast<size_t>(size));
    std::memcpy(table.data(), "1.10", 4);
    const int32_t header[3] = {1, 16, 0};
    std::memcpy(table.data() + 4, header, sizeof(header));
    std::memcpy(table.data() + 16, shape.data(), static_cast<size_t>(size));
    const MCCursorImage image = MCCursorImageFromShape(table, 0);
    CHECK_EQ(image.HotX, 1);
    CHECK_EQ(image.HotY, 1);
    const std::vector<std::string> expected{".\x07.", "\x07\x07\x07", ".\x07."};
    CHECK(ToRows(image) == expected);
}

TEST_CASE("game: every cursor shape becomes a picture that draws exactly as the game draws it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    MCPacketFile pak;
    REQUIRE_EQ(pak.Open("data\\sprites\\cursors.pak"), 0);
    REQUIRE(pak.GetNumPackets() > 0);
    int withTransparency = 0;

    for (int32_t i = 0; i < pak.GetNumPackets(); ++i)
    {
        MCTest::Scope scope(std::format("cursor {}", i));
        REQUIRE_EQ(pak.SeekPacket(i), 0);
        std::vector<uint8_t> shape(static_cast<size_t>(pak.GetPacketSize()) + 16);
        pak.ReadPacket(i, shape);
        // The game's hardware cursor can't blend with the screen; no cursor asks it to.
        CHECK(!MCAgShapeIsAlpha(shape, 0));

        const MCCursorImage image = MCCursorImageFromShape(shape, 0);
        REQUIRE(image.Width > 0);
        CHECK(image.HotX >= 0 && image.HotX < image.Width);
        CHECK(image.HotY >= 0 && image.HotY < image.Height);

        if (std::ranges::count(image.Opaque, 0) > 0)
        {
            ++withTransparency;
        }

        // The game's draw onto a background, against the picture laid over the same background.
        constexpr int32_t side = 64;
        constexpr int32_t mouseX = 30;
        constexpr int32_t mouseY = 30;
        constexpr uint8_t background = 0x37;
        const std::shared_ptr<MCWindow> window = MCWindow::Make(side, side);
        const std::span<uint8_t> drawn = window->Bytes();
        std::ranges::fill(drawn, background);
        const MCPane pane = MCPane::Over(window);
        AGShapeDraw(pane, shape, 0, mouseX, mouseY);
        std::vector<uint8_t> laid(side * side, background);

        for (int y = 0; y < image.Height; ++y)
        {
            for (int x = 0; x < image.Width; ++x)
            {
                const size_t from = static_cast<size_t>(y * image.Width + x);

                if (image.Opaque[from] != 0)
                {
                    laid[static_cast<size_t>((mouseY - image.HotY + y) * side + mouseX - image.HotX + x)] =
                        image.Pixels[from];
                }
            }
        }

        CHECK(std::ranges::equal(drawn, laid));
    }

    // Cursors are arrows and crosshairs, not rectangles.
    CHECK_EQ(withTransparency, pak.GetNumPackets());
}

namespace
{
    /// <summary>The cursor shapes the game has (each a slot of the GUI's 128 with a picture in it).</summary>
    std::vector<size_t> CursorShapesInPlay()
    {
        constexpr size_t ShapeSlots = 128;
        std::vector<size_t> shapes;

        for (size_t shape = 0; shape < ShapeSlots; ++shape)
        {
            if (!GuiSystem().CursorShapeData(static_cast<int32_t>(shape)).empty())
            {
                shapes.push_back(shape);
            }
        }

        return shapes;
    }
}

/// <summary>
/// Port-only (SPEC-first-use.md): no cursor shape is made before it's shown: each is made the first time, once; then an
/// unchanged palette and window make no more, whatever is shown. (The slow part on Windows is the icon built when one is
/// first shown over the window, which a hidden test window never has, so the count is what is checked.)
/// </summary>
TEST_CASE_ISOLATED("game: each cursor shape is made when first shown, and switching between shapes shown in today's "
                   "colours makes none")
{
    if (!MCTestGame::Available() || !MCTestGame::StartMission(1))
    {
        return;
    }

    const std::vector<size_t> shapes = CursorShapesInPlay();
    REQUIRE(shapes.size() > 2);
    // The boot and the mission made only those shown so far.
    const uint64_t started = MCCursor::CursorsMade();
    CHECK(started < shapes.size());

    for (const size_t shape : shapes)
    {
        MCCursor::ShowShape(shape);
    }

    CHECK(MCCursor::CursorsMade() - started <= shapes.size());
    const uint64_t before = MCCursor::CursorsMade();

    for (int i = 0; i < 200; ++i)
    {
        MCCursor::ShowShape(shapes[static_cast<size_t>(i) % shapes.size()]);
    }

    CHECK_EQ(MCCursor::CursorsMade() - before, uint64_t{0});
    CHECK_EQ(MCCursor::ColdCursors(), shapes.size());

    // A dragged item's picture is made once while it doesn't change, and a new one for a new picture.
    const MCCursorImage item = MCCursorImage::Blank(8, 8, 2, 2);
    MCCursorImage otherItem = item;
    otherItem.HotX = 3;

    for (int i = 0; i < 10; ++i)
    {
        MCCursor::Show(item);
    }

    MCCursor::Show(otherItem);
    MCCursor::ShowShape(shapes[0]);
    CHECK_EQ(MCCursor::CursorsMade() - before, uint64_t{2});
}

/// <summary>
/// After a brightness step in play only the shape on screen is made again in its new colours, however many frames
/// show it; each other shape is made again when it is next shown, once.
/// </summary>
TEST_CASE_ISOLATED("game: a brightness step makes only the cursor shown again, and each other shape when shown")
{
    if (!MCTestGame::Available() || !MCTestGame::StartMission(1))
    {
        return;
    }

    const std::vector<size_t> shapes = CursorShapesInPlay();
    REQUIRE(shapes.size() > 2);
    MCCursor::ShowShape(shapes[0]);
    const uint64_t before = MCCursor::CursorsMade();
    GuiSystem().GammaCorrectCurrentPalette(1);
    constexpr int Frames = 100;

    for (int i = 0; i < Frames; ++i)
    {
        MCCursor::ShowShape(shapes[0]);
    }

    CHECK_EQ(MCCursor::CursorsMade() - before, uint64_t{1});

    for (int round = 0; round < 2; ++round)
    {
        for (const size_t shape : shapes)
        {
            MCCursor::ShowShape(shape);
        }
    }

    // Shapes whose colours the step left as they were aren't made again.
    const uint64_t made = MCCursor::CursorsMade() - before;
    CHECK(made > uint64_t{2});
    CHECK(made <= shapes.size());

    for (const size_t shape : shapes)
    {
        MCCursor::ShowShape(shape);
    }

    CHECK_EQ(MCCursor::CursorsMade() - before, made);
}
