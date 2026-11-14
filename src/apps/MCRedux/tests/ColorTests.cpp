#include "stdafx.h"
#include "MCTest.h"
#include "color/MCPalette.h"
#include "color/MCWaterCycle.h"
#include "fakes/MCMemoryFileSource.h"
#include "fixtures/MCRetailData.h"
#include "lib/MCFile.h"
#include "main/MCGameContext.h"
#include "sprite/MCSceneLights.h"

namespace
{
    /// <summary>A <c>.pal</c> image: first colour 0, <paramref name="count"/> colours, colour i = (i % 64, i / 4, 63 - i % 64).</summary>
    std::vector<uint8_t> PalFile(uint16_t count)
    {
        std::vector<uint8_t> pal = {0, 0, static_cast<uint8_t>(count & 0xff), static_cast<uint8_t>(count >> 8)};

        for (int32_t i = 0; i < count; ++i)
        {
            pal.push_back(static_cast<uint8_t>(i % 64));
            pal.push_back(static_cast<uint8_t>(i / 4));
            pal.push_back(static_cast<uint8_t>(63 - i % 64));
        }

        return pal;
    }

    /// <summary><paramref name="count"/> fade tables, every byte of table t holding t.</summary>
    std::vector<uint8_t> NumberedTables(int32_t count)
    {
        std::vector<uint8_t> tables(static_cast<size_t>(count) * MCPalette::FadeTableSize);

        for (size_t i = 0; i < tables.size(); ++i)
        {
            tables[i] = static_cast<uint8_t>(i / MCPalette::FadeTableSize);
        }

        return tables;
    }

    /// <summary>The number of the fade table <paramref name="table"/> points to in <paramref name="palette"/>.</summary>
    int32_t TableNumber(MCPalette& palette, MCColorTable table)
    {
        return static_cast<int32_t>((table.data() - palette.FadePalettes.data()) / MCPalette::FadeTableSize);
    }

    int32_t TableNumber(MCPalette& palette, std::optional<MCColorTable> table)
    {
        return table.has_value() ? TableNumber(palette, *table) : -1;
    }
}

TEST_CASE("palette: the colours come from the .pal file, from entry 0")
{
    const std::vector<uint8_t> pal = PalFile(256);
    MCPalette palette(pal, {}, 0);
    REQUIRE_EQ(palette.RgbData.size(), 768u);
    CHECK(std::equal(palette.RgbData.begin(), palette.RgbData.end(), pal.begin() + 4));
    CHECK_EQ(palette.Colors()[70].R, 70 % 64);
    CHECK_EQ(palette.Colors()[70].G, 70 / 4);

    // A file of fewer colours fills the start; the rest stay black.
    MCPalette shorter(PalFile(16), {}, 0);
    REQUIRE_EQ(shorter.RgbData.size(), 768u);
    CHECK_EQ(shorter.Colors()[15].G, 3);
    CHECK_EQ(shorter.Colors()[16].R, 0);
    CHECK_EQ(shorter.Colors()[255].B, 0);
}

TEST_CASE("palette: haze levels pick fade tables, clamped to the levels the FIT gives, the second set for negatives")
{
    // Three haze levels: tables 0..2 are the first set, 3..5 the second, then the actors' fade tables.
    MCPalette palette(PalFile(256), NumberedTables(10), 3);
    CHECK(!palette.GetHazePalette(0).has_value());
    CHECK_EQ(TableNumber(palette, palette.GetHazePalette(1)), 0);
    CHECK_EQ(TableNumber(palette, palette.GetHazePalette(3)), 2);
    CHECK_EQ(TableNumber(palette, palette.GetHazePalette(7)), 2);
    CHECK_EQ(TableNumber(palette, palette.GetHazePalette(-1)), 3);
    CHECK_EQ(TableNumber(palette, palette.GetHazePalette(-3)), 5);
    CHECK_EQ(TableNumber(palette, palette.GetHazePalette(-9)), 5);
    CHECK_EQ((*palette.GetHazePalette(2))[17], 1);

    // The actors' fade table n follows both haze sets.
    CHECK_EQ(TableNumber(palette, *palette.GetFadeTable(0)), 6);
    CHECK_EQ(TableNumber(palette, *palette.GetFadeTable(3)), 9);
}

TEST_CASE("palette: tweaks change the colours in place, wrapping at 256")
{
    MCPalette palette(PalFile(256), {}, 0);
    const std::array<MCVfxRgb, 3> colors = {MCVfxRgb{1, 2, 3}, MCVfxRgb{4, 5, 6}, MCVfxRgb{7, 8, 9}};
    palette.TweakPalette(254, colors);
    CHECK_EQ(palette.Colors()[254].R, 1);
    CHECK_EQ(palette.Colors()[255].G, 5);
    CHECK_EQ(palette.Colors()[0].B, 9);
    CHECK_EQ(palette.Colors()[1].R, 1);
}

TEST_CASE("palette: a palette FIT names the .pal and the fade tables, read from the palette path")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile(GamePath(PalettePath, "testpal", ".fit"),
                  "FITini\r\n[Palette]\r\nl NumBitmapHazeLevels = 2\r\nst PaletteFileName = \"tp\"\r\n"
                  "[Tables]\r\nst FadeTableFile = \"tfade\"\r\nFITend\r\n");
    files.AddFile(GamePath(PalettePath, "tp", ".pal"), PalFile(256));
    files.AddFile(GamePath(PalettePath, "tfade", ".tbl"), NumberedTables(5));

    std::expected<std::unique_ptr<MCPalette>, std::string> palette = MCPalette::Create("testpal");
    REQUIRE(palette.has_value());
    CHECK_EQ((*palette)->NumBitmapHazeLevels, 2);
    CHECK_EQ((*palette)->FadePalettes.size(), 5u * MCPalette::FadeTableSize);
    CHECK_EQ((*palette)->Colors()[5].B, 58);
    CHECK_EQ(TableNumber(**palette, (*palette)->GetHazePalette(-1)), 2);

    // A missing file or entry is an error, not a crash.
    CHECK(!MCPalette::Create("nopal").has_value());
    files.AddFile(GamePath(PalettePath, "broken", ".fit"),
                  "FITini\r\n[Palette]\r\nl NumBitmapHazeLevels = 2\r\nFITend\r\n");
    std::expected<std::unique_ptr<MCPalette>, std::string> broken = MCPalette::Create("broken");
    REQUIRE(!broken.has_value());
    CHECK(broken.error().find("PaletteFileName") != std::string::npos);
}

TEST_CASE("palette: the context's palette is the game palette, put back when a scenario's goes")
{
    MCTestContextScope scope;
    CHECK(scope.Context().SetPalette(std::make_unique<MCPalette>(PalFile(256), std::vector<uint8_t>{}, 1)) == nullptr);
    MCPalette& interfacePalette = GamePalette();

    // A scenario shows its own and keeps the interface's until it goes.
    std::unique_ptr<MCPalette> old =
        scope.Context().SetPalette(std::make_unique<MCPalette>(PalFile(16), std::vector<uint8_t>{}, 2));
    CHECK(old.get() == &interfacePalette);
    CHECK_EQ(GamePalette().NumBitmapHazeLevels, 2);
    scope.Context().SetPalette(std::move(old));
    CHECK(&GamePalette() == &interfacePalette);
}

TEST_CASE("water: each step gives the water entries the next colours of the cycle")
{
    MCPalette palette(PalFile(256), {}, 0);
    const uint8_t savedMagic = CurrentMagic;
    CurrentMagic = 0;

    for (int32_t step = 0; step < 10; ++step)
    {
        MCTest::Scope scope(std::format("step {}", step));
        CHECK_EQ(CurrentMagic, step % 8);
        StepWaterColors(palette, nullptr);

        for (int32_t i = 0; i < 8; ++i)
        {
            // Water entry i shows the colour of WaterMagicColors[(step + i) % 8], which the cycle never changes.
            const uint8_t source = WaterMagicColors[static_cast<size_t>((step + i) % 8)];
            CHECK_EQ(palette.Colors()[FirstWaterColor + i].R, source % 64);
            CHECK_EQ(palette.Colors()[FirstWaterColor + i].G, source / 4);
        }
    }

    CHECK_EQ(CurrentMagic, 2);
    CurrentMagic = savedMagic;
}

TEST_CASE("water: the cycle's steps keep the glows measured, a change elsewhere in the palette doesn't")
{
    // Every mission turns the cycle on, and its steps threw every glow measured away (13 Nov, the A10).
    MCTestContextScope scope;
    scope.Context().SetPalette(std::make_unique<MCPalette>(PalFile(256), std::vector<uint8_t>{}, 1));
    const uint8_t savedMagic = CurrentMagic;
    const size_t kept = MCSceneLights::GlowTablesKey();
    const MCVfxRgb water = GamePalette().Colors()[FirstWaterColor];

    for (int32_t step = 0; step < 3; ++step)
    {
        StepWaterColors(GamePalette(), nullptr);
        CHECK_EQ(MCSceneLights::GlowTablesKey(), kept);
    }

    const MCVfxRgb stepped = GamePalette().Colors()[FirstWaterColor];
    CHECK((stepped.R != water.R || stepped.G != water.G || stepped.B != water.B));

    // The entries either side of the water's still count.
    for (const int32_t entry : {FirstWaterColor - 1, FirstWaterColor + static_cast<int32_t>(WaterMagicColors.size())})
    {
        MCTest::Scope at(std::format("entry {:#x}", entry));
        const MCVfxRgb was = GamePalette().Colors()[static_cast<size_t>(entry)];
        const MCVfxRgb other{static_cast<uint8_t>((was.R + 1) % 64), was.G, was.B};
        GamePalette().TweakPalette(entry, std::span(&other, 1));
        CHECK(MCSceneLights::GlowTablesKey() != kept);
        GamePalette().TweakPalette(entry, std::span(&was, 1));
        CHECK_EQ(MCSceneLights::GlowTablesKey(), kept);
    }

    CurrentMagic = savedMagic;
}

TEST_CASE("game: the retail palette has HB.PAL's colours and 16 haze levels of fade tables")
{
    std::unique_ptr<MCMemoryFileSource> data =
        MCRetailData::Load({"data\\palette\\palette.fit", "data\\palette\\hb.pal", "data\\palette\\fade.tbl"});

    if (data == nullptr)
    {
        return;
    }

    const std::vector<uint8_t> hb(data->FindImage("data\\palette\\hb.pal")->begin(),
                                  data->FindImage("data\\palette\\hb.pal")->end());
    MCTestContextScope scope;
    scope.Context().SetFiles(std::move(data));
    const std::string savedPath = PalettePath;
    PalettePath = "data\\palette\\";
    std::expected<std::unique_ptr<MCPalette>, std::string> palette = MCPalette::Create("palette");
    PalettePath = savedPath;
    REQUIRE(palette.has_value());

    CHECK_EQ((*palette)->NumBitmapHazeLevels, 16);
    REQUIRE_EQ(hb.size(), 772u);
    CHECK(std::equal((*palette)->RgbData.begin(), (*palette)->RgbData.end(), hb.begin() + 4));
    // fade.tbl: two sets of 16 haze tables, then the actors' fade tables (151 tables in all).
    CHECK_EQ((*palette)->FadePalettes.size(), 151u * MCPalette::FadeTableSize);
    CHECK_EQ(TableNumber(**palette, (*palette)->GetHazePalette(-16)), 31);
    CHECK_EQ(TableNumber(**palette, *(*palette)->GetFadeTable(0)), 32);
}
