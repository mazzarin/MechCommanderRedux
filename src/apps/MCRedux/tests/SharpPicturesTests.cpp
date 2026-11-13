#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCMainMenu.h"
#include "main/MCLogistics.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "platform/MCShapeRows.h"
#include "platform/MCSharpPictures.h"
#include "platform/MCSoftwareRenderer.h"
#include "platform/MCVulkanRenderer.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxFunctions.h"

// Port-only: the HD menus' sharp pictures made ahead (platform/MCSharpPictures) and kept by the GPU renderer.

namespace
{
    /// <summary>The printable characters, which a font has made ahead.</summary>
    constexpr int32_t FirstPrintable = 0x20;
    constexpr int32_t LastPrintable = 0x7e;
    /// <summary>A test window twice the screen across and down: a sharp screen with HD art on.</summary>
    constexpr int32_t SharpWidth = 1280;
    constexpr int32_t SharpHeight = 960;
    constexpr float FrameSeconds = 1.0f / 15.0f;
    /// <summary>Frames that settle a screen change's wipe.</summary>
    constexpr int32_t SettleFrames = 30;

    /// <summary>The picture made ahead under <paramref name="key"/> as the renderer uploads it (RGBA8); none when none was.</summary>
    std::optional<std::vector<uint8_t>> TakeRgba(uint64_t key)
    {
        const std::optional<MCSharpPictures::MCMadePicture> made = MCSharpPictures::Take(key);

        if (!made.has_value())
        {
            return std::nullopt;
        }

        std::vector<uint8_t> rgba(made->RgbaBytes());
        made->CopyRgbaTo(rgba);
        return rgba;
    }

    void Settle()
    {
        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }
}

/// <summary>
/// A picture made ahead is kept as its red and green alone when blue and alpha are 0 throughout (as the redraws leave
/// them), and given back byte for byte; one that isn't, whole.
/// </summary>
TEST_CASE("sharp pictures: a picture made ahead gives back its bytes, kept in half when it can be")
{
    std::vector<uint8_t> redGreen;

    for (uint32_t pixel = 0; pixel < 300; ++pixel)
    {
        redGreen.insert(redGreen.end(), {static_cast<uint8_t>(pixel), static_cast<uint8_t>(pixel * 7), 0, 0});
    }

    const MCSharpPictures::MCMadePicture compact(redGreen);
    CHECK_EQ(compact.RgbaBytes(), redGreen.size());
    CHECK_EQ(compact.KeptBytes(), redGreen.size() / 2);
    std::vector<uint8_t> out(compact.RgbaBytes(), 0xcd);
    compact.CopyRgbaTo(out);
    CHECK(out == redGreen);

    std::vector<uint8_t> whole = redGreen;
    whole[whole.size() - 1] = 1;
    const MCSharpPictures::MCMadePicture kept(whole);
    CHECK_EQ(kept.KeptBytes(), whole.size());
    std::vector<uint8_t> wholeOut(kept.RgbaBytes());
    kept.CopyRgbaTo(wholeOut);
    CHECK(wholeOut == whole);
}

/// <summary>
/// Every printable glyph of the game's fonts, made ahead, is the picture the draw would redraw (MCSharpText, its layers
/// for the two-tone fonts), under the key the draw looks it up by; once taken, it's handed over.
/// </summary>
TEST_CASE_ISOLATED("sharp pictures: a font's glyphs made ahead are what its draws redraw, under their keys")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // A one-colour font, and a two-tone one (yelldrp.fnt: a body and its drop shadow, a picture a layer).
    for (const std::shared_ptr<MCGuiFont>& font : {WhiteFont, YellowDropFont})
    {
        REQUIRE(font != nullptr);
        MCSharpPictures::MakeFont(font->FontData.Bytes());
    }

    MCSharpPictures::WaitForMade();
    int32_t layered = 0;
    int32_t compared = 0;

    for (const std::shared_ptr<MCGuiFont>& font : {WhiteFont, YellowDropFont})
    {
        const std::span<const uint8_t> bytes = font->FontData.Bytes();
        const MCSharpText::MCFontLayers layers = MCSharpText::LayersOf(bytes);

        for (int32_t character = FirstPrintable; character <= LastPrintable; ++character)
        {
            const MCTest::Scope scope(std::format("character {:#x}", character));
            const std::optional<MCSharpPictures::MCFontGlyph> glyph = MCSharpPictures::FontGlyph(bytes, character);
            REQUIRE(glyph.has_value());

            if (glyph->Pitch <= 0)
            {
                continue;
            }

            const MCSharpPictures::MCGlyphPictures how =
                MCSharpPictures::GlyphPictures(glyph->Pixels, glyph->Pitch, glyph->Height, layers);
            const std::vector<std::vector<uint8_t>> redrawn =
                MCSharpPictures::RedrawGlyph(glyph->Pixels, glyph->Pitch, glyph->Height, how);
            REQUIRE_EQ(redrawn.size(), how.Count());
            layered += how.Layers.has_value() ? 1 : 0;

            for (size_t picture = 0; picture < how.Count(); ++picture)
            {
                const std::optional<std::vector<uint8_t>> made = TakeRgba(how.KeyOf(picture));

                // A glyph shared by both fonts was taken already.
                if (!made.has_value())
                {
                    continue;
                }

                CHECK(*made == redrawn[picture]);
                CHECK(!TakeRgba(how.KeyOf(picture)).has_value());
                ++compared;
            }
        }
    }

    CHECK(layered > 0);
    CHECK(compared > LastPrintable - FirstPrintable);
    const MCSharpPictures::MCMadeTally tally = MCSharpPictures::Tally();
    CHECK_EQ(tally.Held, 0u);
    CHECK_EQ(tally.Taken, static_cast<size_t>(compared));
}

/// <summary>
/// The line art of a mech's repair shapes (its diagram's frames and its sold-out outline, the Atlas's eyes drawn by
/// hand), made ahead, is what the draw would redraw (MCSharpLines) from the shape as the draw cuts it, under its key.
/// </summary>
TEST_CASE_ISOLATED("sharp pictures: line art made ahead is what its draw redraws, under its key")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    GHdArt = 1;
    GlobalLog()->AttachHdArt();
    // The Atlas (its eyes drawn by hand) and another mech.
    constexpr std::array<size_t, 2> Mechs = {0, 4};
    int32_t compared = 0;
    int32_t drawnByHand = 0;

    for (const size_t mech : Mechs)
    {
        REQUIRE(!GlobalLog()->MechRepShapes[mech].Empty());
        const std::span<const uint8_t> table = GlobalLog()->MechRepShapes[mech].Bytes();

        for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
        {
            if (MCHdArt::LineArt(table, frame))
            {
                MCSharpPictures::MakeLineArt(table, frame, MCHdArt::LineArtDrawn(table, frame));
            }
        }
    }

    MCSharpPictures::WaitForMade();

    for (const size_t mech : Mechs)
    {
        const std::span<const uint8_t> table = GlobalLog()->MechRepShapes[mech].Bytes();

        for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
        {
            if (!MCHdArt::LineArt(table, frame))
            {
                continue;
            }

            // Cut as the draw of the whole shape cuts it: the rows its header covers, its widest row.
            const MCTest::Scope scope(std::format("mech {} frame {}", mech, frame));
            const std::span<const uint8_t> header = table.subspan(MCShapeOffset(table, frame));
            const std::span<const uint8_t> shapeRows = header.subspan(MCShapeRows::RowsAt);
            const int32_t rows = MCShapeRows::HeaderRows(header);
            int32_t columns = 0;
            MCShapeRows::Extent(shapeRows, rows, columns);
            const std::vector<uint8_t> art = MCSharpPictures::LineArt(shapeRows, columns, rows);
            const std::span<const MCSharpLines::MCDrawnLoop> drawn = MCHdArt::LineArtDrawn(table, frame);
            drawnByHand += drawn.empty() ? 0 : 1;
            const std::optional<std::vector<uint8_t>> made =
                TakeRgba(MCSharpPictures::LineArtKey(art, columns, rows, drawn));
            REQUIRE(made.has_value());
            CHECK(*made == MCSharpLines::Redraw(art, columns, columns, rows, drawn));
            ++compared;
        }
    }

    CHECK_EQ(drawnByHand, 1);
    CHECK(compared > 30);
    CHECK_EQ(MCSharpPictures::Tally().Drawings, static_cast<size_t>(compared));
}

/// <summary>
/// On a sharp screen, the purchase screen's text and line art are taken made ahead (the fonts' at logistics' start,
/// the units' as the briefing is set up), none redrawn as drawn; and they stay on the GPU while away from the screen,
/// unused for longer than the renderer keeps other pictures, so the next visit makes nothing again.
/// </summary>
TEST_CASE_ISOLATED("game: a sharp screen's text and line art are made ahead, and its next visit makes none again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
    REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), SharpWidth, SharpHeight));
    SDL_SyncWindow(&MCInput::Display()->Window());
    const std::shared_ptr<MCVulkanRenderer> renderer =
        std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);

    // HD art on, as the game turns it on (AttachHdArtToLoaded's logistics part): the fonts' glyphs made ahead.
    GHdArt = 1;
    GlobalLog()->AttachHdArt();
    GlobalLog()->MakeAhead();
    MCSharpPictures::WaitForMade();
    Settle();
    REQUIRE(MCInput::Display()->SharpScreen());
    NewCampaign();
    MCSharpPictures::WaitForMade();
    Settle();
    GlobalLog()->SetUpPurchaseScreen(false);
    Settle();

    const MCVulkanRenderer::SharpCounts shown = renderer->SharpPictures();
    const MCSharpPictures::MCMadeTally tally = MCSharpPictures::Tally();
    CHECK(tally.Glyphs > 0);
    CHECK(tally.Drawings > 0);
    CHECK(shown.TakenAhead > 0);
    CHECK_EQ(shown.Redrawn, 0u);

    // Away for longer than an unused picture is kept (300 frames), then back.
    constexpr int32_t AwayFrames = 400;
    GlobalLog()->SetUpRepairScreen(false);

    for (int32_t frame = 0; frame < AwayFrames; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    const MCVulkanRenderer::SharpCounts away = renderer->SharpPictures();
    GlobalLog()->SetUpPurchaseScreen(false);
    Settle();
    const MCVulkanRenderer::SharpCounts back = renderer->SharpPictures();
    CHECK_EQ(back.Redrawn, away.Redrawn);
    CHECK_EQ(back.TakenAhead, away.TakenAhead);
    GHdArt = 0;
}
