#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCScreenChange.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCMechBar.h"
#include "iface/MCMechIcon.h"
#include "iface/MCTacticalInterface.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCMainMenuScreen.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFnvHash.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "object/MCMover.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "platform/MCShapeRows.h"
#include "platform/MCSharpPictures.h"
#include "platform/MCSoftwareRenderer.h"
#include "platform/MCVulkanRenderer.h"
#include "sprite/MCHdArt.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <numeric>

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
    /// The rehearsals a fade into the remade briefing runs: the screen's alone, as no 1998 screen slides in from it (its
    /// PURCHASING and 'MECH BAY are its own tabs).
    /// </summary>
    constexpr size_t RemadeBriefingRehearsals = 1;

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

    /// <summary>
    /// The soft benchmark's bar for a screen's frames as shown: the most a frame's sharp pictures should take of the
    /// renderer (a 240 Hz frame is 4.2 ms, shared with the rest of the frame). Printed every run, never failed: the
    /// suite's parallel runs stretch it (you, 19 Nov: "why not have the 1ms soft benchmark?").
    /// </summary>
    constexpr std::chrono::microseconds MostSharpFrameTime{1000};

    double Milliseconds(std::chrono::nanoseconds time)
    {
        return std::chrono::duration<double, std::milli>(time).count();
    }

    /// <summary>
    /// The renderer's sharp counts once the frame running is done (a frame may run on the presenter's thread, which
    /// puts its pictures).
    /// </summary>
    MCVulkanRenderer::SharpCounts SharpCountsNow(MCVulkanRenderer& renderer)
    {
        if (renderer.WaitForRun)
        {
            renderer.WaitForRun();
        }

        return renderer.SharpPictures();
    }

    /// <summary>
    /// NEW CAMPAIGN clicked as a player clicks it, waiting for nothing: its fade sets the briefing up, then rehearses
    /// it and the screens a slide brings in, at its darkest steps, where a redraw of line art the maker hasn't reached
    /// yet only holds the black longer (the fade keeps its pace): printed, never checked.
    /// </summary>
    void ClickNewCampaign(MCVulkanRenderer& renderer)
    {
        const MCVulkanRenderer::SharpCounts clicked = SharpCountsNow(renderer);
        NewCampaign();
        MCTestGame::FinishChanges();
        const MCVulkanRenderer::SharpCounts faded = SharpCountsNow(renderer);
        std::cout << std::format(
            "  the click's fade: {} sharp pictures taken made ahead, {} redrawn ({:.3f} ms of it), "
            "{:.3f} ms in all, at its darkest steps\n",
            faded.TakenAhead - clicked.TakenAhead, faded.Redrawn - clicked.Redrawn,
            Milliseconds(faded.RedrawnTime - clicked.RedrawnTime), Milliseconds(faded.PutTime - clicked.PutTime));
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
/// On a sharp screen, as a player sees it, waiting for nothing: after NEW CAMPAIGN the briefing and the purchase screen
/// put their text and line art with none redrawn on the frames shown (the fonts' made ahead at logistics' start, the
/// units' line art as the briefing is set up, both put on the GPU by the click's fade as it rehearses the screens). A
/// soft benchmark prints each screen's worst frame for them against MostSharpFrameTime, and what the fade redrew: unseen,
/// at its darkest steps, where the fade keeps its pace and the work only holds the black longer. The pictures stay on
/// the GPU while away from the screen, unused for longer than the renderer keeps other pictures, so the next visit
/// makes nothing again.
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

    ClickNewCampaign(*renderer);

    // A screen shown after <paramref name="click"/>, frame by frame: what its frames redrew, its worst frame printed.
    const auto shows = [&renderer](std::string_view which, const std::function<void()>& click) -> size_t
    {
        const MCVulkanRenderer::SharpCounts start = SharpCountsNow(*renderer);
        click();
        MCVulkanRenderer::SharpCounts last = start;
        std::chrono::nanoseconds worst{0};

        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
            const MCVulkanRenderer::SharpCounts now = SharpCountsNow(*renderer);
            worst = std::max(worst, now.PutTime - last.PutTime);
            last = now;
        }

        std::cout << std::format("  soft benchmark, {}: {} sharp pictures taken made ahead on its frames, {} redrawn; "
                                 "its worst frame {:.3f} ms for them (bar {:.0f} ms){}\n",
                                 which, last.TakenAhead - start.TakenAhead, last.Redrawn - start.Redrawn,
                                 Milliseconds(worst), Milliseconds(MostSharpFrameTime),
                                 worst < MostSharpFrameTime ? "" : ": over the bar");
        return last.Redrawn - start.Redrawn;
    };

    CHECK_EQ(shows("the briefing", [] {}), 0u);
    CHECK_EQ(shows("the purchase screen", [] { GlobalLog()->SetUpPurchaseScreen(false); }), 0u);

    const MCVulkanRenderer::SharpCounts shown = SharpCountsNow(*renderer);
    const MCSharpPictures::MCMadeTally tally = MCSharpPictures::Tally();
    CHECK(tally.Glyphs > 0);
    CHECK(tally.Drawings > 0);
    CHECK(shown.TakenAhead > 0);

    // Away for longer than an unused picture is kept (300 frames), then back.
    constexpr int32_t AwayFrames = 400;
    GlobalLog()->SetUpRepairScreen(false);

    for (int32_t frame = 0; frame < AwayFrames; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    const MCVulkanRenderer::SharpCounts away = SharpCountsNow(*renderer);
    GlobalLog()->SetUpPurchaseScreen(false);
    Settle();
    const MCVulkanRenderer::SharpCounts back = SharpCountsNow(*renderer);
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
}

/// <summary>
/// The screen as it stands rehearsed (as a fade into a screen rehearses it) shows nothing: what's shown is the remade
/// main menu still, and the frame after redraws none of the sharp pictures it shows. Port-only (SPEC-menus.md's stage
/// 9): the 1998 briefing it rehearsed showed only with the remade screens off, which went with the 1998 fallbacks.
/// </summary>
TEST_CASE_ISOLATED("game: a screen rehearsed shows nothing, and its first frame finds what it shows ready")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    const std::vector<uint8_t> menu = ShownBytes();
    const size_t rehearsals = RehearsalsRun();
    RehearseScreen();
    CHECK_EQ(RehearsalsRun(), rehearsals + 1);
    CHECK(ShownBytes() == menu);

    const size_t redrawn = renderer->SharpPictures().Redrawn;
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(renderer->SharpPictures().Redrawn, redrawn);
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
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
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

    // After the work, the fade did what a fade into the briefing does: the briefing rehearsed (no 1998 screen slides in
    // from the remade one). The next frame's fade, from black, does none of it again.
    CHECK_EQ(RehearsalsRun() - rehearsed, RemadeBriefingRehearsals);
    const size_t closed = RehearsalsRun();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(RehearsalsRun(), closed);
    GHdArt = 0;
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 2): NEW CAMPAIGN's click is a change of screen through the flow: the main menu fades
/// out live from the click while the campaign loads, a step a frame; the briefing is set up once the screen is black,
/// then what a fade into the briefing does, and the briefing fades in; its next frame does none of it again.
/// </summary>
TEST_CASE_ISOLATED("game: NEW CAMPAIGN starts the campaign in the fade from its click, once the screen is black")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartSharpLogistics();
    REQUIRE(MCMainMenuScreens::Shown(MCMainPanel::Main));

    // At each present of the change, how far the fade had gone and whether the campaign had started (the main menu
    // hidden: the work's last step).
    struct MCShown
    {
        int Fade = 0;
        bool Started = false;
    };

    std::vector<MCShown> shown;
    const size_t rehearsed = RehearsalsRun();
    MCTestGame::OnPresent = [&]
    { shown.push_back({.Fade = MCInput::Display()->Fade(), .Started = !MCMainMenuScreens::Shown(MCMainPanel::Main)}); };

    NewCampaign();
    REQUIRE(MCTestGame::FinishChanges());
    MCTestGame::OnPresent = {};
    REQUIRE(!shown.empty());
    REQUIRE(!shown.front().Started);
    const auto firstStarted = std::ranges::find_if(shown, &MCShown::Started);
    REQUIRE(firstStarted != shown.end());
    REQUIRE(firstStarted != shown.begin());
    // The main menu shown fading before the briefing's set-up, which waited for black.
    CHECK(std::ranges::any_of(shown.begin(), firstStarted,
                              [](const MCShown& at) { return at.Fade > 0 && at.Fade < MCScreenChange::Black; }));
    CHECK_EQ(std::prev(firstStarted)->Fade, MCScreenChange::Black);
    CHECK_EQ(shown.back().Fade, 0);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
    CHECK_EQ(RehearsalsRun() - rehearsed, RemadeBriefingRehearsals);
    const size_t started = RehearsalsRun();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(RehearsalsRun(), started);
    GHdArt = 0;
}

/// <summary>
/// Port-only: a fade into a screen (a new palette for the next frame, as a campaign's load sets) rehearses it and
/// readies the screens a slide from it brings in (none from the remade briefing) once the screen is nearly black, where
/// a piece's longer step can't be seen.
/// </summary>
TEST_CASE_ISOLATED("game: a fade into a screen rehearses it once the screen is nearly black")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartSharpLogistics();
    NewCampaign();
    REQUIRE(MCTestGame::FinishChanges());
    MCSharpPictures::WaitForMade();
    Settle();
    REQUIRE(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    REQUIRE(GuiSystem().CanFade());

    // At each present of the next frame, how far its fade had gone and the rehearsals run.
    struct MCShown
    {
        int Fade = 0;
        size_t Rehearsals = 0;
    };

    std::vector<MCShown> shown;
    const size_t rehearsed = RehearsalsRun();
    MCTestGame::OnPresent = [&]
    { shown.push_back({.Fade = MCInput::Display()->Fade(), .Rehearsals = RehearsalsRun()}); };
    GuiSystem().ActivatePaletteFromTga(ArtPath + "logart\\lsrupm05.tga");
    MCTestGame::RunFrame(FrameSeconds);
    MCTestGame::OnPresent = {};
    const auto firstRehearsed =
        std::ranges::find_if(shown, [rehearsed](const MCShown& at) { return at.Rehearsals > rehearsed; });
    REQUIRE(firstRehearsed != shown.end());
    REQUIRE(firstRehearsed != shown.begin());
    CHECK(std::prev(firstRehearsed)->Fade >= MCGuiSystem::FadeNearBlack);
    CHECK_EQ(RehearsalsRun() - rehearsed, RemadeBriefingRehearsals);
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

namespace
{
    /// <summary>
    /// The soft benchmark's bar: the most of the game's thread a unit's part diagram should take as the DATA page first
    /// shows it (a 240 Hz frame is 4.2 ms, and the page's line art shares it with the rest of the frame). Printed every
    /// run, never failed: the suite's parallel runs stretch it (you, 19 Nov: "why not have the 1ms soft benchmark?").
    /// </summary>
    constexpr std::chrono::microseconds MostDiagramTime{1000};
    /// <summary>The outlines the measure of the game's thread redrawing them is given for (a unit's, on a busy machine).</summary>
    constexpr size_t MeasuredOutlines = 5;
}

/// <summary>
/// With HD art on and a sharp screen (a player's 1920x1080 window, INTERFACE SIZE AUTO), the tactical map's DATA page
/// draws the shown unit's part diagram (mechrep##.shp, the store's diagrams' file) as the store does: its outlines as
/// line art, smooth strokes, the dark silhouette under them a shape. As a player sees it, waiting for nothing: the page
/// opened on the unit the interface chose at the mission's start, then another unit picked, each draws its outlines as
/// line art from the frame after the click, every one made ahead off the game's thread (MakePartDiagramsAhead), none
/// redrawn there. A soft benchmark prints the game's thread's time for them against MostDiagramTime (1 ms), never
/// failing; and what redrawing a unit's outlines on the game's thread would cost.
/// </summary>
TEST_CASE_ISOLATED("game: the tactical map's DATA page draws a unit's part diagram as line art with HD art on")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t PlayerWidth = 1920;
    constexpr int32_t PlayerHeight = 1080;
    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    MCTestGame::BootOptions.WindowWidth = PlayerWidth;
    MCTestGame::BootOptions.WindowHeight = PlayerHeight;
    MCTestGame::BootOptions.Started = []
    {
        GInterfaceSize = AutoInterfaceSize;
        GHdArt = 1;
    };

    REQUIRE(MCTestGame::StartMission(1));
    REQUIRE(MCInput::Display()->SharpScreen());
    const std::shared_ptr<MCVulkanRenderer> renderer =
        std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    const std::shared_ptr<MCFriendlyMechIcon> icon = TacticalInterface()->MechBar->GetButton(0);
    const std::shared_ptr<MCFriendlyMechIcon> other = TacticalInterface()->MechBar->GetButton(1);
    REQUIRE(icon != nullptr);
    REQUIRE(other != nullptr);
    Settle();

    // The page's first frame after the click: every outline it shows put on the GPU as line art then, none later, none
    // redrawn on the game's thread; the game's thread's time for them printed against MostDiagramTime.
    const auto showsAsPlayed = [&](const std::function<void()>& click, int32_t partId, std::string_view which)
    {
        const MCVulkanRenderer::SharpCounts before = renderer->SharpPictures();
        click();
        MCTestGame::RunFrame(FrameSeconds);
        const MCVulkanRenderer::SharpCounts first = renderer->SharpPictures();
        Settle();
        const MCVulkanRenderer::SharpCounts settled = renderer->SharpPictures();
        const auto mover = std::dynamic_pointer_cast<MCMover>(ObjectList()->FindObjectFromPart(partId));
        REQUIRE(mover != nullptr);
        const std::span<const uint8_t> shapes = map->PartShapes.Bytes();
        const int32_t silhouette = MCTacticalMap::PartSilhouette(*mover);
        const size_t redrawn = first.LinesRedrawn - before.LinesRedrawn;
        const std::chrono::nanoseconds time = first.LinesTime - before.LinesTime;
        std::cout << std::format("  {}: {} outlines on its first frame, {} made ahead, {} redrawn on the game's "
                                 "thread; {} put later\n",
                                 which, first.LinesPut - before.LinesPut, first.LinesPut - before.LinesPut - redrawn,
                                 redrawn, settled.LinesPut - first.LinesPut);
        std::cout << std::format(
            "  soft benchmark, {}: the game's thread {:.3f} ms for its outlines on the first frame "
            "(bar {:.0f} ms){}\n",
            which, Milliseconds(time), Milliseconds(MostDiagramTime), time < MostDiagramTime ? "" : ": over the bar");
        REQUIRE(silhouette < VfxShapeCount(shapes));
        CHECK(MCHdArt::LineArt(shapes, 0));
        CHECK(MCHdArt::LineArt(shapes, silhouette - 1));
        CHECK(!MCHdArt::LineArt(shapes, silhouette));
        CHECK(first.LinesPut > before.LinesPut);
        CHECK_EQ(settled.LinesPut, first.LinesPut);
        CHECK_EQ(redrawn, 0u);
    };

    // The DATA page opened (the unit the interface chose at the mission's start), then another unit's icon clicked in
    // the interface's DATA mode (MCFriendlyMechIcon's MCInterfaceMode::Info).
    showsAsPlayed([&] { map->SetDisplayType(MCTacmapPage::Info); }, icon->PartId, "the first unit");
    showsAsPlayed([&] { map->SetID(other->PartId); }, other->PartId, "another unit");

    // What the game's thread pays when none was made ahead: the shown unit's outlines redrawn there, one by one.
    const std::span<const uint8_t> shapes = map->PartShapes.Bytes();
    std::vector<std::chrono::nanoseconds> times;
    size_t pixels = 0;

    for (int32_t frame = 0; frame < VfxShapeCount(shapes); ++frame)
    {
        if (!MCHdArt::LineArt(shapes, frame))
        {
            continue;
        }

        const std::span<const MCSharpLines::MCDrawnLoop> drawn = MCHdArt::LineArtDrawn(shapes, frame);
        const std::optional<MCSharpPictures::MCLineArtCut> cut = MCSharpPictures::CutLineArt(shapes, frame, drawn);
        REQUIRE(cut.has_value());
        pixels += static_cast<size_t>(cut->Columns) * static_cast<size_t>(cut->Rows);
        const auto start = std::chrono::steady_clock::now();
        const std::vector<uint8_t> redrawn =
            MCSharpLines::Redraw(cut->Art, cut->Columns, cut->Columns, cut->Rows, drawn);
        times.push_back(std::chrono::steady_clock::now() - start);
        CHECK(!redrawn.empty());
    }

    REQUIRE(times.size() >= MeasuredOutlines);
    const std::chrono::nanoseconds total = std::accumulate(times.begin(), times.end(), std::chrono::nanoseconds{0});
    std::ranges::sort(times);
    constexpr auto Scale = static_cast<size_t>(MCSharpText::Scale);
    std::cout << std::format("  redrawn on the game's thread: {} outlines {:.3f} ms, {} at the median {:.3f} ms, the "
                             "slowest {:.3f} ms; {} original pixels, {:.1f} MB as RGBA\n",
                             times.size(), Milliseconds(total), MeasuredOutlines,
                             Milliseconds(times[times.size() / 2] * MeasuredOutlines), Milliseconds(times.back()),
                             pixels, static_cast<double>(pixels * Scale * Scale * 4) / (1024.0 * 1024.0));
    GHdArt = 0;
}
