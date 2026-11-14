#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "logistics/MCBriefingScreen.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFnvHash.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPurchaseScreen.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
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

    /// <summary>
    /// The picture made ahead under <paramref name="key"/>, redrawn from <paramref name="columns"/> x
    /// <paramref name="rows"/> pixels, as the renderer uploads it (RGBA8); none when none was.
    /// </summary>
    std::optional<std::vector<uint8_t>> TakeRgba(uint64_t key, int32_t columns, int32_t rows)
    {
        const std::optional<MCSharpPictures::MCMadePicture> made = MCSharpPictures::Take(key, columns, rows);

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
                const std::optional<std::vector<uint8_t>> made =
                    TakeRgba(how.KeyOf(picture), glyph->Pitch, glyph->Height);

                // A glyph shared by both fonts was taken already.
                if (!made.has_value())
                {
                    continue;
                }

                CHECK(*made == redrawn[picture]);
                CHECK(!TakeRgba(how.KeyOf(picture), glyph->Pitch, glyph->Height).has_value());
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
                TakeRgba(MCSharpPictures::LineArtKey(art, columns, rows, drawn), columns, rows);
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

namespace
{
    /// <summary>The screen as the GPU shows it now (MCDisplay::ReadFrame), as bytes.</summary>
    std::vector<uint8_t> ShownBytes()
    {
        const std::expected<std::vector<SDL_Color>, std::string> frame = MCInput::Display()->ReadFrame();
        REQUIRE(frame.has_value());
        const auto bytes = std::as_bytes(std::span(*frame));
        std::vector<uint8_t> shown(bytes.size());
        std::memcpy(shown.data(), bytes.data(), bytes.size());
        return shown;
    }

    /// <summary>
    /// Logistics up in a sharp window, the GPU drawing and HD art on (as AttachHdArtToLoaded turns it on, without a
    /// pack), the fonts' glyphs made ahead and the main menu settled.
    /// </summary>
    std::shared_ptr<MCVulkanRenderer> StartSharpLogistics()
    {
        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
        REQUIRE(MCTestGame::StartLogistics());
        REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
        REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), SharpWidth, SharpHeight));
        SDL_SyncWindow(&MCInput::Display()->Window());
        std::shared_ptr<MCVulkanRenderer> renderer =
            std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
        REQUIRE(renderer != nullptr);
        GHdArt = 1;
        GlobalLog()->AttachHdArt();
        GlobalLog()->MakeAhead();
        MCSharpPictures::WaitForMade();
        Settle();
        REQUIRE(MCInput::Display()->SharpScreen());
        return renderer;
    }

    /// <summary>The sharp pictures the renderer has taken made ahead and redrawn itself, together.</summary>
    size_t SharpTaken(const MCVulkanRenderer& renderer)
    {
        const MCVulkanRenderer::SharpCounts counts = renderer.SharpPictures();
        return counts.TakenAhead + counts.Redrawn;
    }
}

TEST_CASE_ISOLATED("game: a screen rehearsed shows nothing, and its first frame finds what it shows ready")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    const std::vector<uint8_t> menu = ShownBytes();

    // The briefing set up (its palette waits for the next frame's fade), then rehearsed: what it shows is taken and
    // sent up, and the main menu is still what's shown.
    NewCampaign();
    MCSharpPictures::WaitForMade();
    const size_t before = SharpTaken(*renderer);
    RehearseScreen();
    const size_t rehearsed = SharpTaken(*renderer);
    CHECK(rehearsed > before);
    CHECK(ShownBytes() == menu);

    // The frame that fades the menu out and draws the briefing redraws nothing, and its draw takes next to nothing:
    // the briefing's text and line art were the rehearsal's (the fade rehearses it again, finding it all done, then
    // readies the screens a slide from it brings in, which takes theirs). What the draw takes is the letter its text,
    // typed out as time passes, has come to since (one, here).
    constexpr size_t MostTyped = 2;
    std::vector<size_t> taken;
    MCTestGame::OnPresent = [&] { taken.push_back(SharpTaken(*renderer)); };
    MCTestGame::RunFrame(FrameSeconds);
    MCTestGame::OnPresent = {};
    REQUIRE(taken.size() > 1);
    CHECK_EQ(renderer->SharpPictures().Redrawn, 0u);
    CHECK(taken.front() >= rehearsed);
    CHECK(taken.back() - taken[taken.size() - 2] <= MostTyped);
    CHECK(ShownBytes() != menu);
    GHdArt = 0;
}

TEST_CASE_ISOLATED("game: a slide to a tab the briefing's fade left unshown takes nothing, rehearsing nothing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    NewCampaign();
    MCSharpPictures::WaitForMade();
    Settle();

    // The shop opened on another tab than the briefing's fade left shown (the pilots'; it left the mechs'): the fade
    // readied every tab, so the purchase screen slides in over the briefing with nothing rehearsed at the click, and
    // no step takes a picture but the clock's new digits.
    constexpr size_t ClockDigits = 2;
    GlobalLog()->CurrentInvTab = 1;
    std::vector<size_t> taken;
    MCTestGame::OnPresent = [&] { taken.push_back(SharpTaken(*renderer)); };
    const size_t before = SharpTaken(*renderer);
    const size_t rehearsed = RehearsalsRun();
    GlobalLog()->SetUpPurchaseScreen(true);
    MCTestGame::OnPresent = {};
    REQUIRE(taken.size() > 2);
    CHECK_EQ(RehearsalsRun(), rehearsed);
    CHECK(taken.back() - before <= ClockDigits);
    GHdArt = 0;
}

TEST_CASE_ISOLATED(
    "game: the fade into the briefing readies the purchase and repair screens, whose slides take nothing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    NewCampaign();
    MCSharpPictures::WaitForMade();
    Settle();

    // The slides to the purchase screen and on to the repair screen take nothing, their rehearsals included: what
    // they show was the fade's. All they take is the clock's new digits (the screens show the time to the second).
    constexpr size_t ClockDigits = 2;
    std::vector<size_t> taken;
    MCTestGame::OnPresent = [&] { taken.push_back(SharpTaken(*renderer)); };
    const size_t before = SharpTaken(*renderer);
    GlobalLog()->SetUpPurchaseScreen(true);
    const size_t purchaseSteps = taken.size();
    GlobalLog()->SetUpRepairScreen(true);
    MCTestGame::OnPresent = {};
    REQUIRE(purchaseSteps > 2);
    REQUIRE(taken.size() > purchaseSteps + 2);
    CHECK(taken.back() - before <= ClockDigits);
    CHECK_EQ(renderer->SharpPictures().Redrawn, 0u);
    GHdArt = 0;
}

/// <summary>
/// Port-only: the fade into the briefing readies the purchase and repair screens at every inventory tab, so a tab's
/// first click on the purchase screen loads no art and takes no sharp picture but the clock's new digits (the original
/// art's pictures and the HD ones go up with the art: the GPU keeps them). The mechs' tab last: the one the fade left
/// shown.
/// </summary>
TEST_CASE_ISOLATED(
    "game: the fade into the briefing readies every inventory tab, whose first clicks load and take nothing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    NewCampaign();
    MCSharpPictures::WaitForMade();
    Settle();
    GlobalLog()->SetUpPurchaseScreen(true);
    Settle();
    const std::shared_ptr<MCPurchaseScreen> purchase = GlobalLog()->PurchaseScreen.Lock();
    REQUIRE(purchase != nullptr);
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen);

    // Each tab's click as the purchase screen takes it (MCPurchaseScreen's tabs): the inventory's, then the store's.
    struct MCTabClick
    {
        std::string_view Name;
        std::function<void()> Click;
    };

    const std::array clicks = {
        MCTabClick{"pilots",
                   [&]
                   {
                       purchase->SetUpPilotInv(true, true);
                       purchase->SetUpPilotPurchase();
                   }},
        MCTabClick{"components",
                   [&]
                   {
                       purchase->SetUpCompInv(true, true);
                       purchase->SetUpCompPurchase();
                   }},
        MCTabClick{"vehicles",
                   [&]
                   {
                       purchase->SetUpVhclInv(true, true);
                       purchase->SetUpVehiclePurchase();
                   }},
        MCTabClick{"mechs",
                   [&]
                   {
                       purchase->SetUpMechInv(true, true);
                       purchase->SetUpMechPurchase();
                   }},
    };

    constexpr size_t ClockDigits = 2;

    for (const MCTabClick& click : clicks)
    {
        MCTest::Scope scope{std::string(click.Name)};
        const size_t art = LogArtLoaded();
        const size_t taken = SharpTaken(*renderer);
        click.Click();
        MCTestGame::RunFrame(FrameSeconds);
        CHECK_EQ(LogArtLoaded(), art);
        CHECK(SharpTaken(*renderer) - taken <= ClockDigits);
    }

    GHdArt = 0;
}

TEST_CASE_ISOLATED("game: the way back from a mission's results is made in its fade, once the screen is nearly black")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartSharpLogistics();
    REQUIRE(MCTestGame::StartCampaignMission());
    constexpr uint32_t Won = 4;
    constexpr int32_t MostFramesToResults = 300;
    ScenarioResult = Won;
    Mission()->EndScenarioRequested = -1;

    for (int32_t frame = 0; frame < MostFramesToResults && Mission()->State != MCMissionState::Results; frame++)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    REQUIRE(Mission()->State == MCMissionState::Results);

    // At each present of the results' close, how far the fade had gone and whether logistics had been made again: the
    // results screen fades from the click, and the way back's work waits until it's nearly black.
    struct MCShown
    {
        int Fade = 0;
        bool Back = false;
    };

    // The briefing the way back shows: the next mission's on a win, this one's again otherwise; its map as the
    // briefing's set-up then loads it (read ahead as the fade begins: MCLogistics::ReadBriefingMapAhead).
    const auto briefedFor = [](uint32_t result)
    {
        const uint32_t was = ScenarioResult;
        ScenarioResult = result;
        const std::optional<size_t> briefed = Mission()->BriefedAfterResults();
        ScenarioResult = was;
        return briefed;
    };

    constexpr uint32_t Lost = 0;
    const auto current = static_cast<size_t>(Mission()->CurrentScenario);
    CHECK(briefedFor(Lost) == std::optional<size_t>(current));
    REQUIRE(Mission()->BriefedAfterResults() == std::optional<size_t>(current + 1));
    const std::optional<MCLogistics::MCPicturePixels> map =
        MCLogistics::BriefingMapOf(Mission()->Scenarios[current + 1]);
    REQUIRE(map.has_value());

    std::vector<MCShown> shown;
    const std::weak_ptr<MCLogistics> before = GlobalLog();
    const size_t rehearsed = RehearsalsRun();
    const size_t noted = MCFilesAhead::Noted().size();
    const size_t taken = MCFilesAhead::TakenCount();
    MCTestGame::OnPresent = [&]
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        shown.push_back(
            {.Fade = MCInput::Display()->Fade(), .Back = logistics != nullptr && logistics != before.lock()});
    };

    Mission()->CloseResultsScreen();
    MCTestGame::OnPresent = {};
    REQUIRE(!shown.empty());
    REQUIRE(!shown.front().Back);
    const auto firstBack = std::ranges::find_if(shown, &MCShown::Back);
    REQUIRE(firstBack != shown.end());
    CHECK(std::prev(firstBack)->Fade >= MCGuiSystem::FadeNearBlack);
    CHECK(Mission()->State == MCMissionState::Logistics);
    REQUIRE(GlobalLog() != nullptr);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
    CHECK(!GuiSystem().CanFade());
    REQUIRE(GlobalLog()->BriefingScreen->MapPicture != nullptr);
    const std::shared_ptr<MCTexture> briefingMap = GlobalLog()->BriefingScreen->MapPicture->Bitmap().Texture.lock();
    REQUIRE(briefingMap != nullptr);
    CHECK_EQ(briefingMap->Width, map->Width);
    CHECK_EQ(briefingMap->Height, map->Height);
    CHECK(std::ranges::equal(briefingMap->Pixels, map->Pixels));

    // The screens were made of files read ahead from the click (those logistics looked for in the archives before),
    // most of what it looked for there (555 of 585, 14 Nov); none is held past the fade.
    CHECK(MCFilesAhead::TakenCount() - taken >= noted / 2);
    CHECK_EQ(MCFilesAhead::HeldCount(), 0u);

    // After the work, the fade did what a fade into the briefing does: the briefing rehearsed and the two screens a
    // slide from it brings in readied. The next frame's fade, from black, does none of it again.
    CHECK_EQ(RehearsalsRun() - rehearsed, 1 + MCLogistics::MostScreensSlidIn * MCLogistics::InventoryTabs);
    const size_t closed = RehearsalsRun();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(RehearsalsRun(), closed);
    GHdArt = 0;
}

/// <summary>
/// A shape's rows are walked and decoded no further than their bytes: past them every byte is a row's end, whatever
/// follows in memory, as for shape rows that end before their header's do.
/// </summary>
TEST_CASE("sharp pictures: a shape's rows are read no further than their bytes")
{
    // Two rows: a literal of three pixels, then a run of two of 9 after a skip of one.
    const std::vector<uint8_t> rows = {(3 << 1) | 1, 4, 5, 6, 0, 1, 1, 2 << 1, 9, 0};

    // Cut inside the second row's run, then followed by bytes that would draw a wide row.
    for (const size_t cut : {size_t{0}, size_t{3}, size_t{7}, rows.size()})
    {
        const MCTest::Scope scope(std::format("the rows' first {} bytes", cut));
        std::vector<uint8_t> memory(rows.begin(), rows.begin() + static_cast<ptrdiff_t>(cut));
        std::vector<uint8_t> zeros = memory;
        zeros.resize(rows.size() + 8, 0);
        memory.resize(rows.size() + 8, 0x7f);
        const std::span<const uint8_t> given = std::span(memory).first(cut);
        int32_t width = 0;
        int32_t widthOfZeros = 0;
        CHECK_EQ(MCShapeRows::Extent(given, 2, width), std::min(MCShapeRows::Extent(zeros, 2, widthOfZeros), cut));
        CHECK_EQ(width, widthOfZeros);
        std::vector<uint8_t> decoded(static_cast<size_t>(width) * 2 * MCShapeRows::DecodedBytes + 1, 0xcd);
        std::vector<uint8_t> decodedZeros = decoded;
        MCShapeRows::Decode(given, 2, width, decoded);
        MCShapeRows::Decode(zeros, 2, width, decodedZeros);
        CHECK(decoded == decodedZeros);
        CHECK_EQ(decoded.back(), 0xcd);
    }

    int32_t width = 0;
    CHECK_EQ(MCShapeRows::Extent(rows, 2, width), rows.size());
    CHECK_EQ(width, 3);
}

/// <summary>
/// The game's vr134.shp's sold-out outline (frame 6) is a header and no rows: cut as line art it's nothing, every
/// time, rather than whatever lay past the table (its picture had another key each run, 14 Nov).
/// </summary>
TEST_CASE_ISOLATED("sharp pictures: a frame whose rows end with its table is no line art")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr size_t Vehicle = 34;
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(!GlobalLog()->VehicleRepShapes[Vehicle].Empty());
    const std::span<const uint8_t> table = GlobalLog()->VehicleRepShapes[Vehicle].Bytes();
    REQUIRE(VfxShapeCount(table) > MCLogistics::VehicleSoldOutShape);
    CHECK_EQ(MCShapeOffset(table, MCLogistics::VehicleSoldOutShape) + MCShapeRows::RowsAt, table.size());
    CHECK(!MCSharpPictures::CutLineArt(table, MCLogistics::VehicleSoldOutShape, {}).has_value());
    CHECK(MCSharpPictures::CutLineArt(table, MCLogistics::VehicleArmorShapes, {}).has_value());
}

/// <summary>
/// The redraws of <see cref="MCSharpPictures::RedrawVersion"/>, pinned: every printable glyph's pictures of a
/// one-colour font and a two-tone one, and the line art of the Atlas's repair shapes (its eyes drawn by hand) and a
/// vehicle's. When this fails, what MCSharpText or MCSharpLines draws has changed: add one to RedrawVersion (the HD art
/// pack's sharp.hda, made by this version, is then left alone), set the hash printed here, and make the pack's
/// sharp.hda again (tools/upscale/make_sharp_pack.py). On the Mac only, where the pack is made: line art's distances
/// go through hypot, which another system's library may round otherwise in the last place.
/// </summary>
TEST_CASE_ISOLATED("sharp pictures: the redraws are this version's, as the pack's sharp.hda has them")
{
#if defined(__APPLE__)
    if (!MCTestGame::Available())
    {
        return;
    }

    // Recorded 14 Nov (RedrawVersion 1).
    constexpr uint64_t Pinned = 0x5fde55b9d0c33e5f;
    REQUIRE(MCTestGame::StartLogistics());
    GHdArt = 1;
    GlobalLog()->AttachHdArt();
    uint64_t hash = MCFnvHash::Basis;
    size_t pictures = 0;
    const auto add = [&](uint64_t key, std::span<const uint8_t> rgba)
    {
        MCFnvHash::Mix(hash, key);

        for (const uint8_t byte : rgba)
        {
            MCFnvHash::Mix(hash, byte);
        }

        ++pictures;
    };

    for (const std::shared_ptr<MCGuiFont>& font : {WhiteFont, YellowDropFont})
    {
        REQUIRE(font != nullptr);
        const std::span<const uint8_t> bytes = font->FontData.Bytes();
        const MCSharpText::MCFontLayers layers = MCSharpText::LayersOf(bytes);

        for (int32_t character = FirstPrintable; character <= LastPrintable; ++character)
        {
            const std::optional<MCSharpPictures::MCFontGlyph> glyph = MCSharpPictures::FontGlyph(bytes, character);

            if (!glyph.has_value() || glyph->Pitch <= 0)
            {
                continue;
            }

            const MCSharpPictures::MCGlyphPictures how =
                MCSharpPictures::GlyphPictures(glyph->Pixels, glyph->Pitch, glyph->Height, layers);
            const std::vector<std::vector<uint8_t>> redrawn =
                MCSharpPictures::RedrawGlyph(glyph->Pixels, glyph->Pitch, glyph->Height, how);

            for (size_t picture = 0; picture < redrawn.size(); ++picture)
            {
                add(how.KeyOf(picture), redrawn[picture]);
            }
        }
    }

    for (const std::span<const uint8_t> table :
         {GlobalLog()->MechRepShapes[0].Bytes(), GlobalLog()->VehicleRepShapes[0].Bytes()})
    {
        for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
        {
            if (!MCHdArt::LineArt(table, frame))
            {
                continue;
            }

            const std::span<const MCSharpLines::MCDrawnLoop> drawn = MCHdArt::LineArtDrawn(table, frame);
            const std::optional<MCSharpPictures::MCLineArtCut> cut = MCSharpPictures::CutLineArt(table, frame, drawn);
            REQUIRE(cut.has_value());
            add(cut->Key, MCSharpLines::Redraw(cut->Art, cut->Columns, cut->Columns, cut->Rows, drawn));
        }
    }

    CHECK(pictures > 200);
    const MCTest::Scope scope(std::format("the redraws' hash: {:#018x}", hash));
    CHECK_EQ(hash, Pinned);
    GHdArt = 0;
#endif
}

/// <summary>
/// Tool: <c>--hd-make-sharp &lt;folder&gt;</c> writes the pictures a sharp screen draws its text and line art with
/// (platform/MCSharpPictures), for the HD art pack's <c>sharp.hda</c> (tools/upscale/make_sharp_pack.py packs the
/// folder): every glyph of every font the GUI loads (a picture a layer, MCSharpText) and every frame of the units'
/// repair shapes drawn as line art (MCSharpLines), each named by its key (<c>&lt;key&gt;.rg8</c>, 16 hex digits) and
/// holding its pixels' red and green bytes, the rest 0 as the redraws leave them.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-make-sharp writes the sharp screens' text and line art for the HD art pack")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-make-sharp");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    GHdArt = 1;
    GlobalLog()->AttachHdArt();
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);
    // Each key's picture's hash: one written again under a key must be the same picture (two that aren't share a
    // key, and the pack would give one for the other).
    std::map<uint64_t, uint64_t> written;
    size_t bytes = 0;
    size_t again = 0;
    const auto write = [&](uint64_t key, std::vector<uint8_t> rgba)
    {
        const MCSharpPictures::MCMadePicture picture(std::move(rgba));
        const std::optional<std::span<const uint8_t>> redGreen = picture.RedGreen();
        REQUIRE(redGreen.has_value());
        uint64_t hash = MCFnvHash::Basis;

        for (const uint8_t byte : *redGreen)
        {
            MCFnvHash::Mix(hash, byte);
        }

        if (const auto [kept, fresh] = written.try_emplace(key, hash); !fresh)
        {
            CHECK_EQ(kept->second, hash);
            ++again;
            return;
        }

        std::ofstream file(folder / std::format("{:016x}.rg8", key), std::ios::binary);
        file.write(reinterpret_cast<const char*>(redGreen->data()), static_cast<std::streamsize>(redGreen->size()));
        REQUIRE(file.good());
        bytes += redGreen->size();
    };

    // Every glyph of each font (the drop fonts serve two sizes).
    std::set<const MCGuiFont*> fonts;
    size_t glyphs = 0;

    for (const std::array<std::shared_ptr<MCGuiFont>, 3>& sizes : Fonts)
    {
        for (const std::shared_ptr<MCGuiFont>& font : sizes)
        {
            if (font == nullptr || font->FontData.Empty() || !fonts.insert(font.get()).second)
            {
                continue;
            }

            const std::span<const uint8_t> data = font->FontData.Bytes();
            const MCSharpText::MCFontLayers layers = MCSharpText::LayersOf(data);

            for (int32_t character = 0; character < MCSharpPictures::FontGlyphCount(data); ++character)
            {
                const std::optional<MCSharpPictures::MCFontGlyph> glyph = MCSharpPictures::FontGlyph(data, character);

                if (!glyph.has_value() || glyph->Pitch <= 0 || glyph->Height <= 0)
                {
                    continue;
                }

                const MCSharpPictures::MCGlyphPictures how =
                    MCSharpPictures::GlyphPictures(glyph->Pixels, glyph->Pitch, glyph->Height, layers);
                std::vector<std::vector<uint8_t>> redrawn =
                    MCSharpPictures::RedrawGlyph(glyph->Pixels, glyph->Pitch, glyph->Height, how);
                REQUIRE_EQ(redrawn.size(), how.Count());
                ++glyphs;

                for (size_t picture = 0; picture < how.Count(); ++picture)
                {
                    write(how.KeyOf(picture), std::move(redrawn[picture]));
                }
            }
        }
    }

    const size_t glyphPictures = written.size();
    const size_t glyphBytes = bytes;

    // Every frame of the units' repair shapes drawn as line art (MCLogistics::AttachHdArt).
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    size_t drawings = 0;

    for (const std::span<const MCRegisteredBlock> shapes :
         {std::span<const MCRegisteredBlock>(logistics->MechRepShapes),
          std::span<const MCRegisteredBlock>(logistics->VehicleRepShapes)})
    {
        for (const MCRegisteredBlock& shape : shapes)
        {
            if (shape.Empty())
            {
                continue;
            }

            const std::span<const uint8_t> table = shape.Bytes();

            for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
            {
                if (!MCHdArt::LineArt(table, frame))
                {
                    continue;
                }

                const std::span<const MCSharpLines::MCDrawnLoop> drawn = MCHdArt::LineArtDrawn(table, frame);
                const std::optional<MCSharpPictures::MCLineArtCut> cut =
                    MCSharpPictures::CutLineArt(table, frame, drawn);

                if (cut.has_value())
                {
                    write(cut->Key, MCSharpLines::Redraw(cut->Art, cut->Columns, cut->Columns, cut->Rows, drawn));
                    ++drawings;
                }
            }
        }
    }

    std::cout << std::format("  {} fonts, {} glyphs in {} pictures ({} bytes); {} line art drawings in {} pictures "
                             "({} bytes); {} written again under a key written already\n",
                             fonts.size(), glyphs, glyphPictures, glyphBytes, drawings, written.size() - glyphPictures,
                             bytes - glyphBytes, again);
    CHECK(glyphPictures > 0);
    CHECK(drawings > 0);
    GHdArt = 0;
}

// SCRATCH PROBE (never commit): the maker's time with and without a pack's sharp.hda.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif
TEST_CASE_ISOLATED("probe: sharp maker time")
{
    const char* const on = std::getenv("MC_PROBE_SHARP");

    if (on == nullptr || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    GHdArt = 1;
    GlobalLog()->AttachHdArt();

    if (const char* const pack = std::getenv("MC_PROBE_SHARP_PACK"); pack != nullptr)
    {
        MCHdArt::SetPackFolder(pack);
    }

    // The process's CPU time, user and system, in ms.
    const auto cpu = []
    {
#ifdef _WIN32
        FILETIME created{}, ended{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user);
        const auto ticks = [](const FILETIME& time)
        { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
#else
        rusage use{};
        getrusage(RUSAGE_SELF, &use);
        return static_cast<double>(use.ru_utime.tv_sec + use.ru_stime.tv_sec) * 1000.0 +
               static_cast<double>(use.ru_utime.tv_usec + use.ru_stime.tv_usec) / 1000.0;
#endif
    };
    const auto now = [] { return std::chrono::steady_clock::now(); };
    const auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    std::set<const MCGuiFont*> fonts;
    double cpu0 = cpu();
    auto t0 = now();

    for (const auto& sizes : Fonts)
    {
        for (const auto& font : sizes)
        {
            if (font != nullptr && !font->FontData.Empty() && fonts.insert(font.get()).second)
            {
                MCSharpPictures::MakeFont(font->FontData.Bytes());
            }
        }
    }

    MCSharpPictures::WaitForMade();
    const double fontsMs = ms(now() - t0);
    const double fontsCpu = cpu() - cpu0;
    cpu0 = cpu();
    t0 = now();

    for (size_t mech = 0; mech < 12; ++mech)
    {
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
    const double linesMs = ms(now() - t0);
    const double linesCpu = cpu() - cpu0;
    const MCSharpPictures::MCMadeTally tally = MCSharpPictures::Tally();
    std::cout << std::format("PROBE fonts {:.1f} ms ({:.1f} ms cpu), {} glyph pictures; line art {:.1f} ms ({:.1f} ms "
                             "cpu), {} drawings; from pack {} of {}\n",
                             fontsMs, fontsCpu, tally.GlyphPictures, linesMs, linesCpu, tally.Drawings, tally.FromPack,
                             tally.PackPictures);
}
