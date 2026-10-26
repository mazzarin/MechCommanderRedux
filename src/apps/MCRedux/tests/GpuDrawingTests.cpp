#include "stdafx.h"
#include "MCTest.h"
#include "fixtures/MCRegisteredData.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "ai/MCTacticalOrder.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiSmackerWindow.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCBriefingScreen.h"
#include "gui/MCUpdateDisplay.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCSmacker.h"
#include "platform/MCVulkanRenderer.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxFunctions.h"

using namespace MCScreenInput;

namespace
{
    /// <summary>The GPU renderer drawing in mirror mode, or null (with a note) when this machine has none.</summary>
    std::shared_ptr<MCVulkanRenderer> MirrorRenderer()
    {
        auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());

        if (renderer == nullptr || MCRenderer::GpuDrawing() != MCGpuDrawing::Mirror)
        {
            std::cout << "  (skipped: no GPU renderer on this machine)\n";
            return nullptr;
        }

        return renderer;
    }

    /// <summary>Checks that every frame compared so far was the same on the GPU, and lists what it can't draw yet.</summary>
    void CheckMirror(const MCVulkanRenderer& renderer)
    {
        const MCVulkanRenderer::MirrorTally& tally = renderer.Mirror();

        for (const auto& [command, count] : renderer.Unsupported())
        {
            std::cout << std::format("  not on the GPU yet: {} ({} times)\n", command, count);
        }

        if (tally.DifferentFrames != 0)
        {
            std::cout << std::format("  first difference, frame {}: {}\n", tally.FirstFrame, tally.First);
        }

        CHECK(tally.Frames > 0);
        CHECK_EQ(tally.DifferentFrames, 0);
        CHECK_EQ(MCRenderer::UnregisteredDraws(), 0);
    }
}

/// <summary>
/// Mirror mode on mission 1: every command for the screen and the world surface is drawn by the software renderer
/// and by the GPU, and each frame the GPU's surfaces (and the screen as shown over the world) are the software
/// renderer's pixels.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU draws mission 1 as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    for (int32_t frame = 0; frame < 30; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    CheckMirror(*renderer);
}

/// <summary>
/// Mirror mode on mission 1 zoomed out and in: the GPU draws the world straight at the size it is shown (the zoom is
/// only the scale its draws are mapped by), and each drawn pixel is the software renderer's world pixel under its
/// centre, the pixel the composite would have shown from the 1x world; the screen as shown over it is the CPU's. The
/// ground comes from the map's terrain mesh, one layer a frame, while the software renderer draws its tiles one by one.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU draws mission 1 zoomed as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    for (const float zoom : {720.0f, 400.0f, 1000.0f, 533.0f, 2160.0f})
    {
        MCTest::Scope scope(std::format("{} lines", zoom));
        MCFixedZoomHeight = zoom;
        renderer->ResetMirror();
        const int64_t layers = renderer->TerrainLayersDrawn();

        for (int32_t frame = 0; frame < 10; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        CheckMirror(*renderer);
        CHECK_EQ(renderer->TerrainLayersDrawn() - layers, 10);
    }
}

/// <summary>
/// Mirror mode on mission 1 with a bigger interface: INTERFACE SIZE 150% in a 1600x1200 window at 1080P gives a screen
/// 720 lines tall, shown scaled up with the world view at the window's pixels, and the GPU still draws the screen and
/// the world as the software renderer does.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU draws mission 1 with a 150% interface as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int uiResolution = 1080;
    constexpr int interfaceSize = 150;
    constexpr int lines = 720;
    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    MCTestGame::BootInLargeWindow(uiResolution, interfaceSize);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    REQUIRE_EQ(MCInput::Display()->Height(), lines);
    renderer->ResetMirror();

    for (int32_t frame = 0; frame < 30; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    CheckMirror(*renderer);
}

/// <summary>
/// Mirror mode on mission 1 with the camera at each corner and edge of the map (as far as Camera::setPosition lets it
/// go), at the closest and the furthest zoom: the terrain window's grid reaches off the map there, so the ground mesh's
/// ring of off-map cells is drawn, and the GPU's world is still the software renderer's pixel for pixel. Any grid the
/// mesh can't stand for would stop the game (Fatal), so getting through is part of the test.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU draws mission 1's map edges as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    const float far =
        static_cast<float>(MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide) * MCTerrain::MetersPerVertex * 2.0f;
    const float directions[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};

    for (const float zoom : {480.0f, 2160.0f})
    {
        MCFixedZoomHeight = zoom;

        for (const auto& direction : directions)
        {
            MCTest::Scope scope(std::format("{} lines, towards ({}, {})", zoom, direction[0], direction[1]));
            renderer->ResetMirror();
            const int64_t layers = renderer->TerrainLayersDrawn();

            for (int32_t frame = 0; frame < 4; frame++)
            {
                MCVector3D position = Eye()->GetPosition();
                position.X = direction[0] * far;
                position.Y = direction[1] * far;
                Eye()->SetPosition(position);
                MCTestGame::RunFrame(1.0f / 15.0f);
            }

            CheckMirror(*renderer);
            CHECK_EQ(renderer->TerrainLayersDrawn() - layers, 4);
        }
    }
}

/// <summary>
/// The shape transforms (AG_shape_transform, AG_shape_translate_transform: the scaled and mirrored effects) drawn on
/// mission 1's screen, over the world and clipped at every edge, at full and half size, mirrored or not: the GPU draws
/// each shape's filled picture directly, and its pixels are the software renderer's (fill into the buffer, then blend).
/// </summary>
TEST_CASE_ISOLATED("game: the GPU draws the shape transforms as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    std::array<uint8_t, 256> table{};

    for (size_t i = 0; i < table.size(); ++i)
    {
        table[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    MCRenderer::RegisterData(std::span(table.data(), table.size()), MCDataKind::Tables);
    MCPane& pane = ScreenPort()->Frame();
    const int32_t width = pane.X1 - pane.X0 + 1;
    const int32_t height = pane.Y1 - pane.Y0 + 1;
    const int32_t places[][2] = {{width / 2, height / 2}, {2, 3}, {width - 3, height - 2}, {width / 3, -4}};
    int32_t draws = 0;

    for (int32_t shape = 0; shape < 128; shape += 5)
    {
        const std::span<const uint8_t> shapeTable = GuiSystem().CursorShapeData(shape);

        if (shapeTable.empty())
        {
            continue;
        }

        for (const auto& place : places)
        {
            for (int32_t variant = 0; variant < 8; ++variant)
            {
                const int32_t mirror = variant & 1;
                const int32_t fullSize = (variant >> 1) & 1;

                if ((variant & 4) != 0)
                {
                    AGShapeTranslateTransform(pane, shapeTable, 0, place[0], place[1], mirror, fullSize, table);
                }
                else
                {
                    AGShapeTransform(pane, shapeTable, 0, place[0], place[1], mirror, fullSize);
                }

                ++draws;
            }
        }
    }

    // The draws took their table rows when recorded.
    MCRenderer::UnregisterData(std::span(table.data(), table.size()));
    REQUIRE(draws > 0);
    REQUIRE(renderer->Flush(MCRenderer::Underlays()).has_value());
    const auto comparison = renderer->Compare(MCRenderer::Underlays(), {});
    REQUIRE(comparison.has_value());

    if (comparison->Different != 0)
    {
        std::cout << std::format("  {} pixels differ: {}\n", comparison->Different, comparison->First);
    }

    CHECK_EQ(comparison->Different, 0);
    CHECK(renderer->Unsupported().empty());
    CheckMirror(*renderer);
}

/// <summary>
/// Shapes are kept on the GPU by the address they're read from, in the data block their owner registered, never by
/// their bytes: a shape recoloured in place (VFX_shape_remap_colors says so) is drawn with its new colours, a block
/// unregistered and registered again over other shapes draws those, and a draw from memory nobody registered is counted
/// and left out. Each drawn screen is the software renderer's, pixel for pixel.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU keeps shapes by their registered address")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    // Two cursor shape files (the blocks the cursors registered), copied into memory of the test's own.
    std::vector<std::span<const uint8_t>> files;

    for (int32_t shape = 0; shape < 128 && files.size() < 2; shape++)
    {
        const std::span<const uint8_t> shapeTable = GuiSystem().CursorShapeData(shape);

        if (shapeTable.empty())
        {
            continue;
        }

        const std::optional<MCDataBlock> block = MCRegisteredBlockOf(MCAddressKey::OfData(shapeTable));

        if (block.has_value() && block->Begin == MCAddressKey::Of(shapeTable[0]) &&
            (files.empty() || block->End - block->Begin != static_cast<ptrdiff_t>(files[0].size())))
        {
            files.emplace_back(shapeTable.data(), static_cast<size_t>(block->End - block->Begin));
        }
    }

    REQUIRE_EQ(files.size(), size_t{2});
    std::vector<uint8_t> memory(std::max(files[0].size(), files[1].size()));
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;

    // Draws shape 0 of the memory and compares the screen with the software renderer's.
    const auto drawAndCompare = [&](std::string_view step)
    {
        MCTest::Scope scope(step);
        VfxShapeDraw(pane, memory, 0, x, y);
        REQUIRE(renderer->Flush(MCRenderer::Underlays()).has_value());
        const auto comparison = renderer->Compare(MCRenderer::Underlays(), {});
        REQUIRE(comparison.has_value());

        if (comparison->Different != 0)
        {
            std::cout << std::format("  {}: {} pixels differ: {}\n", step, comparison->Different, comparison->First);
        }

        CHECK_EQ(comparison->Different, 0);
    };

    std::ranges::copy(files[0], memory.begin());
    MCRenderer::RegisterData(std::span(memory.data(), memory.size()), MCDataKind::Shapes);
    drawAndCompare("first drawn");

    // Recoloured in place: every colour moves on by 16.
    std::array<uint8_t, 256> table{};

    for (size_t i = 0; i < table.size(); ++i)
    {
        table[i] = static_cast<uint8_t>(i + 16);
    }

    VfxShapeLookaside(table);
    VfxShapeRemapColors(memory, 0);
    drawAndCompare("recoloured");

    // The memory reused for the other file.
    MCRenderer::UnregisterData(std::span(memory.data(), memory.size()));
    std::ranges::fill(memory, uint8_t{0});
    std::ranges::copy(files[1], memory.begin());
    MCRenderer::RegisterData(std::span(memory.data(), files[1].size()), MCDataKind::Shapes);
    drawAndCompare("other shapes");

    // Unregistered: counted, and not drawn on the GPU.
    MCRenderer::UnregisterData(std::span(memory.data(), memory.size()));
    const int64_t unregistered = MCRenderer::UnregisteredDraws();
    MCRenderer::ExpectUnregistered expected;
    VfxShapeDraw(pane, memory, 0, x, y);
    CHECK_EQ(MCRenderer::UnregisteredDraws() - unregistered, 1);
    CHECK(!MCIsRegistered(MCAddressKey::OfData(memory)));
}

/// <summary>
/// Colour tables are kept on the GPU by the address they're read from, in the table block their owner registered,
/// never by their bytes: tables at any alignment, overlapping each other in one block, are each drawn through their
/// own bytes; a table changed in place (DataChanged says so) draws its new bytes from then on; a table outside every
/// registered block is counted and draws nothing. Each drawn screen is the software renderer's, pixel for pixel.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU keeps colour tables by their registered address")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    // An opaque cursor shape (a translucent one is blended on the GPU, which the comparison leaves out).
    std::span<const uint8_t> shapes;

    for (int32_t shape = 0; shape < 128 && shapes.empty(); shape++)
    {
        const std::span<const uint8_t> shapeTable = GuiSystem().CursorShapeData(shape);

        if (!shapeTable.empty() && !MCAgShapeIsAlpha(shapeTable, 0))
        {
            shapes = shapeTable;
        }
    }

    REQUIRE(!shapes.empty());
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;

    // A block of bytes counting up: the table at offset n maps colour c to c + n.
    std::vector<uint8_t> memory(1024);

    for (size_t i = 0; i < memory.size(); ++i)
    {
        memory[i] = static_cast<uint8_t>(i);
    }

    MCRenderer::RegisterData(std::span(memory.data(), memory.size()), MCDataKind::Tables);

    // Draws the shape through each table (side by side, one frame) and compares the screen with the software
    // renderer's.
    const auto drawAndCompare = [&](std::string_view step, std::initializer_list<size_t> offsets)
    {
        MCTest::Scope scope(step);
        int32_t left = x;

        for (const size_t offset : offsets)
        {
            AGShapeTranslateDraw(pane, shapes, 0, left, y, MCColorTable(memory.data() + offset, 256));
            left += 40;
        }

        REQUIRE(renderer->Flush(MCRenderer::Underlays()).has_value());
        const auto comparison = renderer->Compare(MCRenderer::Underlays(), {});
        REQUIRE(comparison.has_value());

        if (comparison->Different != 0)
        {
            std::cout << std::format("  {}: {} pixels differ: {}\n", step, comparison->Different, comparison->First);
        }

        CHECK_EQ(comparison->Different, 0);
    };

    drawAndCompare("tables at odd offsets, overlapping", {3, 100, 101, 600});
    const int64_t unregistered = MCRenderer::UnregisteredDraws();

    // Changed in place: the table at 3 now maps every colour to 0x13 (drawn before and after, in one frame).
    std::fill(memory.begin() + 3, memory.begin() + 3 + 256, uint8_t{0x13});
    MCRenderer::DataChanged(std::span(memory.data() + 3, 256));
    drawAndCompare("changed in place", {3, 100});

    // A table past the block's end reaches outside it: counted, and not drawn on the GPU.
    MCRenderer::ExpectUnregistered expected;
    AGShapeTranslateDraw(pane, shapes, 0, x, y, MCColorTable(memory.data() + memory.size() - 100, 256));
    CHECK_EQ(MCRenderer::UnregisteredDraws() - unregistered, 1);

    MCRenderer::UnregisterData(std::span(memory.data(), memory.size()));
    AGShapeTranslateDraw(pane, shapes, 0, x, y, MCColorTable(memory.data() + 3, 256));
    CHECK_EQ(MCRenderer::UnregisteredDraws() - unregistered, 2);
}

/// <summary>
/// Translucent colours drawn by the GPU alone are alpha blended over what is shown, as AlphaPal.ini describes them:
/// colour 254 (RGB 0, A 1, B2 0.5) halves the colour under it, colour 4 (RGB 116 104 54, A 1, B2 1) adds its colour
/// to it. Both are drawn as filled ellipses on the screen over the world; the frame outside them doesn't change.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU blends translucent colours over what is shown")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(1));

    if (MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        return;
    }

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    MCTestGame::RunFrame(1.0f / 15.0f);
    const auto before = display->ReadFrame();
    REQUIRE(before.has_value());
    MCPane& pane = ScreenPort()->Frame();
    const int32_t shadowX = 200;
    const int32_t lightX = 400;
    const int32_t y = 200;
    AGEllipseFill(pane, shadowX, y, 20, 20, 254);
    AGEllipseFill(pane, lightX, y, 20, 20, 4);
    const auto after = display->ReadFrame();
    REQUIRE(after.has_value());
    const int32_t width = display->Width();
    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t px, int32_t py)
    { return frame[static_cast<size_t>(py + pane.Y0) * width + px + pane.X0]; };
    const auto near = [](int32_t actual, int32_t expected) { return std::abs(actual - expected) <= 2; };

    for (int32_t dy = -10; dy <= 10; dy += 5)
    {
        for (int32_t dx = -10; dx <= 10; dx += 5)
        {
            MCTest::Scope scope(std::format("({}, {}) from the centres", dx, dy));
            const SDL_Color under = at(*before, shadowX + dx, y + dy);
            const SDL_Color shadow = at(*after, shadowX + dx, y + dy);
            CHECK(near(shadow.r, under.r / 2) && near(shadow.g, under.g / 2) && near(shadow.b, under.b / 2));
            const SDL_Color lit = at(*before, lightX + dx, y + dy);
            const SDL_Color light = at(*after, lightX + dx, y + dy);
            CHECK(near(light.r, std::min(lit.r + 116, 255)) && near(light.g, std::min(lit.g + 104, 255)) &&
                  near(light.b, std::min(lit.b + 54, 255)));
        }
    }

    // Away from the ellipses nothing changed.
    const SDL_Color away = at(*before, 300, 100);
    const SDL_Color still = at(*after, 300, 100);
    CHECK(away.r == still.r && away.g == still.g && away.b == still.b);
}

/// <summary>
/// Mission 1's battle drawn by the GPU alone (the lance attacks the Uller, the camera follows it): everything is drawn
/// on the GPU, nothing reads a frame surface's memory, and pictures go up when they are new, not every frame.
/// </summary>
TEST_CASE_ISOLATED("game: the GPU alone draws mission 1's battle without per-frame uploads")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(1));
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());

    if (renderer == nullptr || MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        return;
    }

    std::shared_ptr<MCMover> uller = GetMoverFromPartId(896);
    REQUIRE(uller != nullptr);

    for (int32_t partId = 0x200; partId < 0x203; partId++)
    {
        std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
        REQUIRE(mover != nullptr);
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackObject, 0);
        order.Target = uller;
        order.AttackParams.Type = 1;
        order.AttackParams.Method = 0;
        order.AttackParams.Range = -1;
        order.AttackParams.Pursue = -1;
        mover->HandleTacticalOrder(order, 1, 0);
    }

    int64_t frames = 0;
    int64_t framesWithPictures = 0;
    int64_t pictureBytes = 0;
    int64_t atlasBytes = 0;

    for (int32_t frame = 0; frame < 15 * 60; frame++)
    {
        if (uller->IsDestroyed() == 0)
        {
            Eye()->SetPosition(uller->GetPosition());
        }

        MCTestGame::RunFrame(1.0f / 15.0f);
        const MCVulkanRenderer::UploadTally& uploads = renderer->LastFrameUploads();
        frames++;
        framesWithPictures += uploads.Pictures > 0 ? 1 : 0;
        pictureBytes += uploads.PictureBytes;
        atlasBytes += uploads.AtlasBytes;
    }

    std::cout << std::format("  {} frames, {} with new pictures ({} KB), {} KB of new atlas images\n", frames,
                             framesWithPictures, pictureBytes / 1024, atlasBytes / 1024);

    for (const auto& [command, count] : renderer->Unsupported())
    {
        std::cout << std::format("  not on the GPU yet: {} ({} times)\n", command, count);
    }

    // The pictures (the interface's art, the map) went up while the mission loaded; the fog of war is drawn on the GPU
    // and the shape transforms come from their shapes, so nothing is a new picture in the battle. New atlas images
    // are shapes seen for the first time.
    CHECK_EQ(framesWithPictures, 0);
    CHECK(renderer->Unsupported().empty());
    CHECK_EQ(MCRenderer::StaleCpuReads(), 0);
    CHECK_EQ(MCRenderer::UnregisteredDraws(), 0);
}

/// <summary>Mirror mode on the logistics screens: the main menu, the briefing, the purchase and repair screens.</summary>
TEST_CASE_ISOLATED("game: the GPU draws the logistics screens as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    // Each screen settles, and then its frames send the GPU no new pictures (its art went up when first shown).
    const auto settle = [renderer]
    {
        int64_t pictures = 0;

        for (int32_t frame = 0; frame < 30; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
            pictures += frame >= 20 ? renderer->LastFrameUploads().Pictures : 0;
        }

        CHECK_EQ(pictures, 0);
    };

    settle();
    NewCampaign();
    settle();

    // The operation movie (stream uploads, compared with the software frames).
    MCBriefingScreen& briefing = *GlobalLog()->BriefingScreen;
    int32_t movieFrames = 0;

    for (int32_t frame = 0; frame < 300 && movieFrames < 30; frame++)
    {
        MCTestGame::RunFrame(1.0f / 30.0f);
        movieFrames += briefing.SmackerWindow != nullptr && briefing.SmackerWindow->Movie != nullptr ? 1 : 0;
    }

    CHECK_EQ(movieFrames, 30);
    CHECK(renderer->LastFrameUploads().Pictures == 0);
    GlobalLog()->SetUpPurchaseScreen(true);
    settle();
    GlobalLog()->SetUpRepairScreen(true);
    settle();
    CheckMirror(*renderer);
}

/// <summary>
/// Mirror mode on the small damage diagrams of the briefing's deploy pane, the purchase screen's inventory and the
/// repair screen, with every mech's left arm shot off and its right arm and torsos damaged: each location is recoloured
/// for its damage through the diagram's translate tables, which the GPU must take as the software renderer does (it
/// once drew them as the identity, so a mech showed undamaged).
/// </summary>
TEST_CASE_ISOLATED("game: the GPU draws damaged mechs' diagrams as the software renderer does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::Mirror);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCVulkanRenderer> renderer = MirrorRenderer();

    if (renderer == nullptr)
    {
        return;
    }

    NewCampaign();
    int32_t mechs = 0;

    for (const std::shared_ptr<MCLogMech>& mech : GlobalLog()->ForceMechList->Mechs)
    {
        mechs++;
        // Left arm (5) gone: no armor, no internals. Right arm (4) and the torsos at the other damage states.
        mech->Armor[5].CurArmor = 0;
        mech->Internals[5].CurArmor = 0;
        mech->Armor[4].CurArmor = 0;
        mech->Armor[1].CurArmor = static_cast<uint8_t>(mech->Armor[1].MaxArmor / 5);
        mech->Armor[2].CurArmor = static_cast<uint8_t>(mech->Armor[2].MaxArmor * 2 / 5);
        mech->Armor[3].CurArmor = static_cast<uint8_t>(mech->Armor[3].MaxArmor * 3 / 5);
        mech->CalcStatus();
    }

    REQUIRE(mechs > 0);
    const auto run = []
    {
        for (int32_t frame = 0; frame < 20; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    };

    GlobalLog()->SetUpBriefingScreen(true);
    run();
    GlobalLog()->SetUpPurchaseScreen(true);
    run();
    GlobalLog()->SetUpRepairScreen(true);
    run();
    CheckMirror(*renderer);
}

/// <summary>
/// The briefing's operation movie with the GPU drawing alone: each decoded frame is decoded straight into the movie
/// texture's upload memory and sent up once (its rectangle only), a frame with no new movie frame sends nothing, the
/// movie's memory on the CPU is never written (nor read), and its pixels are never uploaded whole again.
/// </summary>
TEST_CASE_ISOLATED("game: a movie's frames go straight to the GPU, once each")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartLogistics());
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());

    if (renderer == nullptr || MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        return;
    }

    // The operation movie starts after a delay.
    NewCampaign();
    MCBriefingScreen& briefing = *GlobalLog()->BriefingScreen;

    for (int32_t frame = 0;
         frame < 300 && (briefing.SmackerWindow == nullptr || briefing.SmackerWindow->Movie == nullptr); frame++)
    {
        MCTestGame::RunFrame(1.0f / 30.0f);
    }

    const std::shared_ptr<MCGuiSmackerWindow> window = briefing.SmackerWindow;
    REQUIRE(window != nullptr && window->Movie != nullptr);
    const std::shared_ptr<MCTexture> texture = window->MovieWindow->Texture.lock();
    REQUIRE(texture != nullptr);
    CHECK(texture->Use == MCTextureUse::Stream);
    const MCSmackerPlayer& player = *window->Movie->Player;
    const int64_t movieBytes = static_cast<int64_t>(player.Width()) * player.Height();
    int64_t decoded = 0;
    int64_t idle = 0;
    int64_t pictures = 0;

    for (int32_t frame = 0; frame < 60 && window->Movie != nullptr; frame++)
    {
        const uint32_t before = window->Movie->Player->FrameNum();
        MCTestGame::RunFrame(1.0f / 30.0f);
        const MCVulkanRenderer::UploadTally& uploads = renderer->LastFrameUploads();

        if (window->Movie == nullptr)
        {
            break;
        }

        MCTest::Scope scope(std::format("frame {}", frame));
        const bool advanced = window->Movie->Player->FrameNum() != before;
        CHECK_EQ(uploads.MovieFrames, advanced ? 1 : 0);
        CHECK_EQ(uploads.MovieBytes, advanced ? movieBytes : 0);
        decoded += advanced ? 1 : 0;
        idle += advanced ? 0 : 1;
        pictures += frame >= 5 ? uploads.Pictures : 0;
    }

    std::cout << std::format("  {} frames decoded, {} displays without a new frame\n", decoded, idle);
    CHECK(decoded > 10);
    CHECK(idle > 0);
    CHECK_EQ(pictures, 0);
    CHECK(texture->CpuStale);
    CHECK_EQ(MCRenderer::StaleCpuReads(), 0);
    CHECK_EQ(MCRenderer::UnregisteredDraws(), 0);
}
