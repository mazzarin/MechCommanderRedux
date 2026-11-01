#include "stdafx.h"
#include "MCHdMovieFixture.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCScenarioMap.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "camera/MCViewWindow.h"
#include "color/MCPalette.h"
#include "engine/MCCraterManager.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCHardwareCursor.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCBriefingScreen.h"
#include "main/MCInventoryList.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "main/MCArtModes.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "main/MCMissionGlobals.h"
#include "object/MCArtillery.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCBullet.h"
#include "object/MCCommander.h"
#include "object/MCExplosion.h"
#include "object/MCForces.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCLaser.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectSystem.h"
#include "object/MCProjectileLaser.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCRegisteredBlock.h"
#include "platform/MCRenderer.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCVulkanRenderer.h"
#include "sprite/MCArmAppearanceType.h"
#include "sprite/MCElementalTree.h"
#include "sprite/MCGVAppearanceType.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCHdMovie.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCPUAppearanceType.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "sprite/MCVfxAppearance.h"
#include "sprite/MCVfxAppearanceType.h"
#include "sprite/MCVfxBuildingAppearanceType.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSmackerWindow.h"
#include "logistics/MCLoadSaveMenu.h"
#include "main/MCLogistics.h"
#include "lib/MCFile.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPreferencesMenu.h"
#include "platform/MCSharpLines.h"
#include "platform/MCSharpText.h"
// The final audit's screen tour and call stacks.
#include "gui/MCScrollPane.h"
#include "gui/MCUpdateDisplay.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMechBriefBlock.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogMech.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#if __has_include("logistics/MCLogTextButton.h")
// A build with the preferences' CONTROLS panel (the controls branch): the tour shows it.
#include "logistics/MCLogTextButton.h"
#define MC_AUDIT_CONTROLS 1
#endif
#if __has_include(<execinfo.h>) && __has_include(<dlfcn.h>)
#include <dlfcn.h>
#include <execinfo.h>
#define MC_AUDIT_STACKS 1
#endif

namespace
{
    constexpr int32_t BytesPerPixel = 4;
    /// <summary>The index picture's green where a pixel has an index (sprite/MCHdArt.h).</summary>
    constexpr uint8_t HasIndex = 255;
    /// <summary>The facings walked to find every packet of a gesture, and the step between them (degrees).</summary>
    constexpr float FirstFacing = -180.0f;
    constexpr float LastFacing = 180.0f;
    constexpr float FacingStep = 0.5f;
    /// <summary>The mech of mission 1 that takes move orders (the walk test's).</summary>
    constexpr int32_t PlayerMechPartId = 0x200;
    /// <summary>Gesture 0, standing.</summary>
    constexpr int32_t StandGesture = 0;
    /// <summary>A frame's length in the tests (the game's 15 frames a second).</summary>
    constexpr float FrameSeconds = 1.0f / 15.0f;

    /// <summary>One frame of a shape table, decoded: its bounds and, per pixel, its index and whether it is drawn.</summary>
    struct DecodedFrame
    {
        int32_t Width = 0;
        int32_t Height = 0;
        /// <summary>The bounds' top-left corner from the hot spot.</summary>
        int32_t XMin = 0;
        int32_t YMin = 0;
        /// <summary>Whether the shape is drawn translucent (its data starts with the token pair 03 00).</summary>
        bool Translucent = false;
        std::vector<uint8_t> Index;
        std::vector<uint8_t> Drawn;
    };

    /// <summary>
    /// The widest and tallest shape <see cref="Decode"/> takes, and the farthest its corner may lie from its hot spot (the
    /// mech parts are under 100 pixels).
    /// </summary>
    constexpr int64_t MaxShapeSide = 1024;

    /// <summary>
    /// Frame <paramref name="frame"/> of <paramref name="table"/> (VFX's run-length format, see
    /// platform/MCSoftwareRendererShapes.cpp), or why it can't be read: some retail tables count frames they don't
    /// hold, or end inside a row.
    /// </summary>
    std::expected<DecodedFrame, std::string> Decode(std::span<const uint8_t> table, int32_t frame)
    {
        DecodedFrame decoded;
        const auto bad = [&](std::string_view why) { return std::unexpected(std::format("frame {}: {}", frame, why)); };

        const size_t offset =
            static_cast<size_t>(static_cast<uint32_t>(MCVfxRead32(table, 8 + static_cast<size_t>(frame) * 8)));

        if (offset + sizeof(MCVfxShapeHeader) > table.size())
        {
            return bad(std::format("its offset {} lies past the table's {} bytes", offset, table.size()));
        }

        const std::span<const uint8_t> shape = table.subspan(offset);
        MCVfxShapeHeader header{};
        std::memcpy(&header, shape.data(), sizeof(header));
        const int64_t width = static_cast<int64_t>(header.XMax) - header.XMin + 1;
        const int64_t height = static_cast<int64_t>(header.YMax) - header.YMin + 1;

        if (width <= 0 || height <= 0)
        {
            return decoded;
        }

        if (width > MaxShapeSide || height > MaxShapeSide ||
            std::abs(static_cast<int64_t>(header.XMin)) > MaxShapeSide ||
            std::abs(static_cast<int64_t>(header.YMin)) > MaxShapeSide)
        {
            return bad(std::format("its bounds are {}x{} at ({}, {})", width, height, header.XMin, header.YMin));
        }

        decoded.XMin = header.XMin;
        decoded.YMin = header.YMin;
        decoded.Width = static_cast<int32_t>(width);
        decoded.Height = static_cast<int32_t>(height);
        const std::span<const uint8_t> bytes = shape.subspan(sizeof(header));
        decoded.Translucent = bytes.size() >= 2 && bytes[0] == 3 && bytes[1] == 0;
        decoded.Index.assign(static_cast<size_t>(decoded.Width) * decoded.Height, 0);
        decoded.Drawn.assign(decoded.Index.size(), 0);
        size_t data = 0;

        for (int32_t row = 0; row < decoded.Height; ++row)
        {
            int32_t x = 0;

            for (;;)
            {
                if (data >= bytes.size())
                {
                    return bad(std::format("row {} runs past the table", row));
                }

                const uint8_t token = bytes[data++];
                const int32_t count = token >> 1;

                if (count == 0)
                {
                    if ((token & 1) == 0)
                    {
                        break;
                    }

                    if (data >= bytes.size())
                    {
                        return bad(std::format("row {} runs past the table", row));
                    }

                    x += bytes[data++];
                    continue;
                }

                const size_t length = (token & 1) ? static_cast<size_t>(count) : 1;

                if (data + length > bytes.size())
                {
                    return bad(std::format("row {} runs past the table", row));
                }

                for (int32_t i = 0; i < count; ++i)
                {
                    if (x + i < decoded.Width)
                    {
                        const size_t at = static_cast<size_t>(row) * decoded.Width + static_cast<size_t>(x + i);
                        decoded.Index[at] = (token & 1) ? bytes[data + i] : bytes[data];
                        decoded.Drawn[at] = 1;
                    }
                }

                data += length;
                x += count;
            }
        }

        return decoded;
    }

    bool SameColor(const SDL_Color& a, const SDL_Color& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    /// <summary>Saves <paramref name="width"/> x <paramref name="height"/> RGBA pixels as a PNG.</summary>
    void SavePng(const std::filesystem::path& path, int32_t width, int32_t height, std::vector<uint8_t>& rgba)
    {
        const MCSdlHandle<SDL_Surface> surface(
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, rgba.data(), width * BytesPerPixel));
        REQUIRE(surface != nullptr);

        if (!SDL_SavePNG(surface.get(), path.string().c_str()))
        {
            MCTest::Scope scope(std::format("{}: {}", path.string(), SDL_GetError()));
            REQUIRE(false);
        }
    }

    /// <summary>
    /// A pack's sequence: one shape table (a mech's: a gesture's part at one facing, as the tree uses it; or a packet of
    /// another appearance's PAK) and its folder in the pack (sprite/MCHdArt.h).
    /// </summary>
    struct Sequence
    {
        std::filesystem::path Folder;
        /// <summary>A hash of the shape table's bytes (another appearance's sequence with the same one repeats it).</summary>
        size_t Table = 0;
        MCHdArt::MechShapeKey Key;
        int32_t Gesture = 0;
        /// <summary>The tree's part numbering (0 legs, 1 torso, 2 and 3 the arms' sprites).</summary>
        int32_t Part = 0;
        float Facing = 0.0f;
        bool Reverse = false;
        /// <summary>Whether its translucent frames hold their blended pixels (an effect's, MCHdArt::Frame::Blended).</summary>
        bool Blended = false;
        /// <summary>The frames a second the game plays it at (a VFX state's), 0 when not known.</summary>
        float FrameRate = 0.0f;
        std::vector<DecodedFrame> Frames;
    };

    /// <summary>
    /// Writes a picture of the game's art for the HD art generator, named by its hash (MCHdArt::PictureHash), once:
    /// <c>&lt;name&gt;.png</c> (its colours, opaque) and <c>&lt;name&gt;_1x_idx.png</c> (its palette indices, in red),
    /// and a line of the manifest (name, source, width, height).
    /// </summary>
    void WritePicture(const std::filesystem::path& folder, std::ofstream& manifest, std::string_view source,
                      std::span<const uint8_t> pixels, int32_t width, int32_t height,
                      std::span<const SDL_Color, 256> colors, std::set<uint64_t>& written)
    {
        const uint64_t hash = MCHdArt::PictureHash(pixels, width, height);

        if (!written.insert(hash).second)
        {
            return;
        }

        const std::string name = MCHdArt::PictureName(hash);
        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<uint8_t> rgba(size * BytesPerPixel);
        std::vector<uint8_t> indices(size * BytesPerPixel);

        for (size_t i = 0; i < size; ++i)
        {
            const SDL_Color color = colors[pixels[i]];
            rgba[i * BytesPerPixel] = color.r;
            rgba[i * BytesPerPixel + 1] = color.g;
            rgba[i * BytesPerPixel + 2] = color.b;
            rgba[i * BytesPerPixel + 3] = 255;
            indices[i * BytesPerPixel] = pixels[i];
            indices[i * BytesPerPixel + 1] = pixels[i];
            indices[i * BytesPerPixel + 2] = pixels[i];
            indices[i * BytesPerPixel + 3] = 255;
        }

        SavePng(folder / std::format("{}.png", name), width, height, rgba);
        SavePng(folder / std::format("{}_1x_idx.png", name), width, height, indices);
        manifest << name << '\t' << source << '\t' << width << '\t' << height << '\n';
    }

    /// <summary>Whether <paramref name="text"/> ends with <paramref name="ending"/>, letters in either case.</summary>
    bool EndsWithIgnoringCase(std::string_view text, std::string_view ending)
    {
        return text.size() >= ending.size() &&
               std::ranges::equal(text.substr(text.size() - ending.size()), ending,
                                  [](char a, char b)
                                  {
                                      return std::tolower(static_cast<unsigned char>(a)) ==
                                             std::tolower(static_cast<unsigned char>(b));
                                  });
    }

    /// <summary>A colour channel (0..1) as a byte.</summary>
    uint8_t ChannelByte(float value)
    {
        constexpr float Full = 255.0f;
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * Full));
    }

    /// <summary>
    /// The shape tables of <paramref name="tree"/>'s gestures (only <paramref name="onlyGesture"/>'s when given), each
    /// once, with the first facing that draws it, decoded.
    /// </summary>
    std::vector<Sequence> MechSequences(const std::shared_ptr<MCSpriteTree>& tree, std::optional<int32_t> onlyGesture)
    {
        const std::shared_ptr<MCSpriteManager> sprites = SpriteManager();
        REQUIRE(sprites != nullptr);
        std::vector<Sequence> sequences;
        std::set<std::pair<int32_t, uint32_t>> seen;
        int32_t skipped = 0;
        std::string firstSkipped;

        for (int32_t gesture = 0; gesture < static_cast<int32_t>(tree->Gestures.size()); ++gesture)
        {
            if (onlyGesture.has_value() && gesture != *onlyGesture)
            {
                continue;
            }

            for (int32_t part = 0; part < MechPartCount; ++part)
            {
                for (float facing = FirstFacing; facing < LastFacing; facing += FacingStep)
                {
                    const std::optional<MCGesturePacket> packet = tree->GesturePacket(gesture, part, facing);

                    if (!packet.has_value() ||
                        !seen.insert({static_cast<int32_t>(packet->FilePart), packet->Packet}).second)
                    {
                        continue;
                    }

                    const uint32_t mechNum = tree->FileNumbers[packet->FilePart];
                    const std::shared_ptr<MCShape> shape =
                        sprites->GetMechShapeData(mechNum, packet->Packet, packet->FilePart, Turn, tree, true);

                    if (shape == nullptr || !VfxIsShapeTable(shape->FrameList()))
                    {
                        continue;
                    }

                    Sequence& sequence = sequences.emplace_back();
                    sequence.Key = {sprites->PartFileName(packet->FilePart, true), mechNum, packet->Packet};
                    sequence.Folder = MCHdArt::FolderOf(sequence.Key);
                    sequence.Gesture = gesture;
                    sequence.Part = part;
                    sequence.Facing = facing;
                    sequence.Reverse = packet->Reverse;

                    for (int32_t frame = 0; frame < VfxShapeCount(shape->FrameList()); ++frame)
                    {
                        auto decoded = Decode(shape->FrameList(), frame);

                        if (!decoded)
                        {
                            // Left out of the pack (drawn from the original).
                            if (skipped++ == 0)
                            {
                                firstSkipped = std::format("{}/{}/{} {}", sequence.Key.PartFile, sequence.Key.MechNum,
                                                           sequence.Key.Packet, decoded.error());
                            }

                            sequence.Frames.emplace_back();
                            continue;
                        }

                        sequence.Frames.push_back(std::move(*decoded));
                    }
                }
            }
        }

        if (skipped > 0)
        {
            std::cout << std::format("  {} frames can't be read (the first: {})\n", skipped, firstSkipped);
        }

        return sequences;
    }

    /// <summary>
    /// Writes <paramref name="frame"/> as a PNG pair, <paramref name="stem"/>.png and <paramref name="stem"/>_idx.png
    /// (sprite/MCHdArt.h), at <paramref name="scale"/> times its size (each pixel repeated), its colours through
    /// <paramref name="paint"/>. With <paramref name="blended"/>, the frame holds its blended pixels: premultiplied, a
    /// blended pixel of a translucent frame the colour and coverage AlphaPal.ini's colour draws, without an index.
    /// </summary>
    void WriteFramePngs(const std::filesystem::path& stem, const DecodedFrame& frame,
                        std::span<const SDL_Color, 256> colors, int32_t scale,
                        const std::function<SDL_Color(SDL_Color)>& paint, bool blended)
    {
        const int32_t width = frame.Width * scale;
        const int32_t height = frame.Height * scale;
        std::vector<uint8_t> color(static_cast<size_t>(width) * height * BytesPerPixel, 0);
        std::vector<uint8_t> index(color.size(), 0);

        for (int32_t y = 0; y < height; ++y)
        {
            for (int32_t x = 0; x < width; ++x)
            {
                const size_t from = static_cast<size_t>(y / scale) * frame.Width + static_cast<size_t>(x / scale);
                uint8_t* outColor = &color[(static_cast<size_t>(y) * width + x) * BytesPerPixel];
                uint8_t* outIndex = &index[(static_cast<size_t>(y) * width + x) * BytesPerPixel];
                outIndex[2] = blended ? MCHdArt::HoldsBlended : 0;
                outIndex[3] = HasIndex;

                if (frame.Drawn[from] == 0)
                {
                    continue;
                }

                if (const uint8_t value = frame.Index[from]; blended && frame.Translucent && SpecialColor[value] != 0)
                {
                    const MCAlphaBlend blend = AlphaColorBlend(MCAlphaColors[value]);
                    outColor[0] = ChannelByte(blend.R);
                    outColor[1] = ChannelByte(blend.G);
                    outColor[2] = ChannelByte(blend.B);
                    outColor[3] = ChannelByte(blend.Coverage);
                    continue;
                }

                const SDL_Color painted = paint(colors[frame.Index[from]]);
                outColor[0] = painted.r;
                outColor[1] = painted.g;
                outColor[2] = painted.b;
                outColor[3] = HasIndex;
                outIndex[0] = frame.Index[from];
                outIndex[1] = HasIndex;
            }
        }

        SavePng(std::filesystem::path(stem).concat(".png"), width, height, color);
        SavePng(std::filesystem::path(stem).concat("_idx.png"), width, height, index);
    }

    /// <summary>
    /// Writes <paramref name="sequences"/> into a pack at <paramref name="pack"/>: each drawn frame as a PNG pair
    /// (<see cref="WriteFramePngs"/>) named by its number in its sequence's folder.
    /// </summary>
    void WritePack(const std::filesystem::path& pack, std::span<const Sequence> sequences,
                   std::span<const SDL_Color, 256> colors, int32_t scale,
                   const std::function<SDL_Color(SDL_Color)>& paint)
    {
        for (const Sequence& sequence : sequences)
        {
            const std::filesystem::path folder = pack / sequence.Folder;
            std::filesystem::create_directories(folder);

            for (size_t frameNum = 0; frameNum < sequence.Frames.size(); ++frameNum)
            {
                if (sequence.Frames[frameNum].Width > 0)
                {
                    WriteFramePngs(folder / std::to_string(frameNum), sequence.Frames[frameNum], colors, scale, paint,
                                   sequence.Blended);
                }
            }
        }
    }

    /// <summary>A sequence's frames' bounds as JSON (the HD art generator's): {"width": ...}, ... (width 0: none).</summary>
    std::string FramesJson(const Sequence& sequence)
    {
        std::string json;

        for (size_t f = 0; f < sequence.Frames.size(); ++f)
        {
            const DecodedFrame& frame = sequence.Frames[f];
            json += std::format("{}{{\"width\": {}, \"height\": {}, \"xmin\": {}, \"ymin\": {}, \"translucent\": {}}}",
                                f == 0 ? "" : ", ", frame.Width, frame.Height, frame.XMin, frame.YMin,
                                frame.Translucent ? "true" : "false");
        }

        return json;
    }

    /// <summary>
    /// Writes <paramref name="colors"/> as <paramref name="folder"/>'s palette.rgb: the colours a pack's pictures are made
    /// from (MCHdArt::MadeWith).
    /// </summary>
    void WritePaletteRgb(const std::filesystem::path& folder, std::span<const SDL_Color, 256> colors)
    {
        std::ofstream rgb(folder / "palette.rgb", std::ios::binary);

        for (const SDL_Color& color : colors)
        {
            rgb.put(static_cast<char>(color.r)).put(static_cast<char>(color.g)).put(static_cast<char>(color.b));
        }
    }

    /// <summary>The shown colours as JSON: [[r, g, b], ...].</summary>
    std::string ColorsJson(std::span<const SDL_Color, 256> colors)
    {
        std::string json = "[";

        for (size_t i = 0; i < colors.size(); ++i)
        {
            json += std::format("{}[{}, {}, {}]", i == 0 ? "" : ", ", colors[i].r, colors[i].g, colors[i].b);
        }

        return json + "]";
    }

    /// <summary>The mech of mission 1 given <paramref name="treeName"/>'s tree, its actor and the tree.</summary>
    struct PlayerMech
    {
        std::shared_ptr<MCBattleMech> Mech;
        std::shared_ptr<MCMechActor> Actor;
        std::shared_ptr<MCSpriteTree> Tree;
    };

    PlayerMech GivePlayerMech(std::string_view treeName)
    {
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(PlayerMechPartId);
        REQUIRE(mover != nullptr);
        REQUIRE(mover->ObjectClass == MCObjectClass::BattleMech);
        PlayerMech player;
        player.Mech = std::static_pointer_cast<MCBattleMech>(mover);
        player.Actor = std::static_pointer_cast<MCMechActor>(player.Mech->Appearance);
        REQUIRE(player.Actor != nullptr);
        player.Tree = MCTestGame::FindMechTree(treeName);
        REQUIRE(player.Tree != nullptr);
        player.Tree->PreloadGestures();
        REQUIRE_EQ(player.Actor->Init(player.Tree, player.Mech), 0);
        Eye()->ChangeTarget(player.Mech, true);
        return player;
    }

    /// <summary>The shown frame (at the output's resolution), read back from a screenshot.</summary>
    struct Shown
    {
        int32_t Width = 0;
        int32_t Height = 0;
        std::vector<uint8_t> Pixels;
    };

    Shown ReadShown(const std::filesystem::path& scratch)
    {
        const std::filesystem::path path = scratch / "shown.png";
        const auto saved = MCInput::Display()->SaveShownScreenshot(path);

        if (!saved)
        {
            MCTest::Scope scope(saved.error());
            REQUIRE(false);
        }

        const MCSdlHandle<SDL_Surface> loaded(SDL_LoadPNG(path.string().c_str()));
        REQUIRE(loaded != nullptr);
        const MCSdlHandle<SDL_Surface> rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));
        REQUIRE(rgba != nullptr);
        Shown shown{rgba->w, rgba->h, {}};

        for (int32_t y = 0; y < rgba->h; ++y)
        {
            const auto* row = static_cast<const uint8_t*>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch;
            shown.Pixels.insert(shown.Pixels.end(), row, row + static_cast<ptrdiff_t>(rgba->w) * BytesPerPixel);
        }

        return shown;
    }

    /// <summary>Makes the window <paramref name="width"/> x <paramref name="height"/> with the UI <paramref name="lines"/>
    /// lines tall, so the world view is drawn at the window's pixels, larger than the screen's.</summary>
    void ScaleUp(int32_t width, int32_t height, int lines)
    {
        GUiResolution = lines;
        REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), width, height));
        SDL_SyncWindow(&MCInput::Display()->Window());
        MCTestGame::RunFrame(FrameSeconds);
        REQUIRE(MCInput::Display()->UnderlayScale() > 1.0f);
    }
}

/// <summary>Tool: <c>--hd-mechs 1</c> lists the mech trees, for --hd-extract's --mech: a line "mech &lt;name&gt;" each,
/// then its packet in the legs, torso, right and left arm PAKs (trees that share them share an HD pack's frames).</summary>
TEST_CASE_ISOLATED("tool: --hd-mechs lists the mech trees")
{
    if (!MCTest::Option("hd-mechs").has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));

    for (const auto& [name, packet] : MCTestGame::MechTrees())
    {
        const auto tree = std::static_pointer_cast<MCSpriteTree>(
            AppearanceTypeList().GetAppearance(0x01000000u | static_cast<uint32_t>(packet)));
        REQUIRE(tree != nullptr);
        std::cout << std::format("mech {} {} {} {} {}\n", name, tree->FileNumbers[MCMechPart::Legs],
                                 tree->FileNumbers[MCMechPart::Torso], tree->FileNumbers[MCMechPart::RightArm],
                                 tree->FileNumbers[MCMechPart::LeftArm]);
    }
}

/// <summary>
/// Tool: <c>--hd-extract &lt;folder&gt;</c> writes the mech trees <c>--mech &lt;name&gt;</c> (default "madcat") as an HD art
/// pack at its own size, for tools/upscale/make_hd_pack.py to upscale: every shape table of every gesture, each frame a
/// PNG with its index picture (sprite/MCHdArt.h), plus <c>&lt;name&gt;.json</c> (each sequence's gesture, part, facing and
/// frames' bounds) and <c>palette.json</c> (the shown colours, the special colours, the actors' fade tables).
/// </summary>
namespace
{
    /// <summary>The terrain tile sets (data\tiles\&lt;name&gt;.pak and the rotated &lt;name&gt;90.pak).</summary>
    constexpr std::array<std::string_view, 2> TileSets = {"tiles", "gtiles"};
    constexpr std::array<std::string_view, 2> TileSetSuffixes = {"", "90"};
    /// <summary>The archive of the missions' FITs, and where they lie in it.</summary>
    constexpr std::string_view MissionArchive = "MISSION.FST";
    constexpr std::string_view MissionFolder = "missions";
    /// <summary>The base game's palette (mission 1's).</summary>
    constexpr std::string_view BasePalette = "palette";

    /// <summary>Where row <paramref name="row"/> of a terrain tile starts (VFX's tile format, vfx/vfxtile.cpp).</summary>
    uint32_t TileRow(std::span<const uint8_t> tile, int32_t row)
    {
        return static_cast<uint32_t>(MCVfxRead32(tile, 4 + static_cast<size_t>(row) * 4));
    }

    /// <summary>A terrain tile's pixels: each one's index and whether it is drawn.</summary>
    struct DecodedTile
    {
        int32_t Width = 0;
        int32_t Height = 0;
        std::vector<uint8_t> Index;
        std::vector<uint8_t> Drawn;
    };

    DecodedTile DecodeTile(std::span<const uint8_t> tile)
    {
        DecodedTile decoded;
        decoded.Height = tile[2];

        for (int32_t row = 0; row < decoded.Height; ++row)
        {
            const uint32_t offset = TileRow(tile, row);
            const auto length = static_cast<int32_t>(TileRow(tile, row + 1) - offset);

            if (length > 1)
            {
                decoded.Width = std::max(decoded.Width, tile[offset] + length - 1);
            }
        }

        decoded.Index.assign(static_cast<size_t>(decoded.Width) * decoded.Height, 0);
        decoded.Drawn.assign(decoded.Index.size(), 0);

        for (int32_t row = 0; row < decoded.Height; ++row)
        {
            const uint32_t offset = TileRow(tile, row);
            const auto length = static_cast<int32_t>(TileRow(tile, row + 1) - offset);

            for (int32_t i = 0; i < length - 1; ++i)
            {
                const size_t at = static_cast<size_t>(row) * decoded.Width + tile[offset] + i;
                decoded.Index[at] = tile[offset + 1 + i];
                decoded.Drawn[at] = 1;
            }
        }

        return decoded;
    }
}

/// <summary>
/// Tool: <c>--hd-extract-terrain &lt;folder&gt;</c> writes every terrain tile of the tile sets (data\tiles: the
/// normal and the rotated views) once, as the HD art generator takes them: <c>&lt;name&gt;.png</c>, the tile's shown
/// colours (mission 1's palette) where it is drawn, and <c>&lt;name&gt;_idx.png</c> (red the index, green 255 where
/// drawn), named as the pack names them (MCHdArt::TerrainTileName), and those colours as palette.rgb. Tiles whose data
/// doesn't fit their table are left out, as the ground mesh leaves them out. It also lists the palettes the missions
/// name (palettes.json) and how far each lies from mission 1's.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-terrain writes the terrain tiles for the HD art generator")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-terrain");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);

    // The missions' palettes.
    std::map<std::string, std::vector<std::string>> palettes;
    auto archive = MCFastFile::Create((MCFileSystem::GameRoot() / MissionArchive).string());
    REQUIRE(archive.has_value());

    for (const MCFileEntry& entry : (*archive)->GetEntries())
    {
        std::string name(entry.GetName());
        std::string lower = name;
        std::ranges::transform(lower, lower.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });

        if (!lower.ends_with(".fit") || lower.find(MissionFolder) == std::string::npos)
        {
            continue;
        }

        MCFitIniFile fit;

        if (fit.Open(name) != 0 || fit.SeekBlock("PaletteSystem") != 0)
        {
            continue;
        }

        if (const auto palette = fit.Read<std::string>("PaletteSystem"); palette.has_value())
        {
            palettes[*palette].push_back(name);
        }
    }

    std::string palettesJson = "{";

    for (const auto& [palette, missions] : palettes)
    {
        palettesJson += std::format("{}\n \"{}\": {}", palettesJson.size() == 1 ? "" : ",", palette, missions.size());
        std::cout << std::format("  palette {}: {} missions\n", palette, missions.size());
    }

    std::ofstream(folder / "palettes.json") << palettesJson << "\n}\n";

    // The colours the tiles are coloured with, for the pack (MCHdArt::MadeWith).
    WritePaletteRgb(folder, colors);

    // How far each palette's colours lie from the base game's, mission 1's (the tiles' colours are its).
    if (auto first = MCPalette::Create(BasePalette); first.has_value())
    {
        for (const auto& [palette, missions] : palettes)
        {
            auto other = MCPalette::Create(palette);

            if (!other.has_value())
            {
                continue;
            }

            int32_t differing = 0;
            int32_t largest = 0;

            for (int32_t i = 0; i < MCPalette::ColorCount; ++i)
            {
                const MCVfxRgb a = (*first)->Colors()[static_cast<size_t>(i)];
                const MCVfxRgb b = (*other)->Colors()[static_cast<size_t>(i)];
                const int32_t d = std::max({std::abs(a.R - b.R), std::abs(a.G - b.G), std::abs(a.B - b.B)});
                differing += d != 0 ? 1 : 0;
                largest = std::max(largest, d);
            }

            std::cout << std::format("  palette {}: {} of {} colours differ from mission 1's, by up to {}\n", palette,
                                     differing, MCPalette::ColorCount, largest);
        }
    }

    // The tiles, each once.
    std::set<std::string> written;
    size_t tiles = 0;
    size_t skipped = 0;

    for (const std::string_view set : TileSets)
    {
        for (const std::string_view suffix : TileSetSuffixes)
        {
            MCPacketFile file;

            if (file.Open(std::format("data\\tiles\\{}{}.pak", set, suffix)) != 0)
            {
                continue;
            }

            for (int32_t packet = 0; packet < file.GetNumPackets(); ++packet)
            {
                if (file.SeekPacket(packet) != 0 || file.GetPacketSize() <= 0)
                {
                    continue;
                }

                std::vector<uint8_t> data(static_cast<size_t>(file.GetPacketSize()));

                if (file.ReadPacket(packet, data) < 0 || data.size() < 8 || !VfxTileFits(data))
                {
                    ++skipped;
                    continue;
                }

                // As the ground mesh keeps it: up to its last row's end.
                const uint32_t end = TileRow(data, data[2]);
                const std::span<const uint8_t> tile = std::span<const uint8_t>(data).first(end);
                const std::string name = MCHdArt::TerrainTileName(tile);
                ++tiles;

                if (!written.insert(name).second)
                {
                    continue;
                }

                const DecodedTile decoded = DecodeTile(tile);

                if (decoded.Width == 0)
                {
                    continue;
                }

                std::vector<uint8_t> color(decoded.Index.size() * BytesPerPixel, 0);
                std::vector<uint8_t> index(color.size(), 0);

                for (size_t i = 0; i < decoded.Index.size(); ++i)
                {
                    const SDL_Color shown = colors[decoded.Index[i]];
                    index[i * BytesPerPixel] = decoded.Index[i];
                    index[i * BytesPerPixel + 3] = HasIndex;

                    if (decoded.Drawn[i] != 0)
                    {
                        color[i * BytesPerPixel] = shown.r;
                        color[i * BytesPerPixel + 1] = shown.g;
                        color[i * BytesPerPixel + 2] = shown.b;
                        color[i * BytesPerPixel + 3] = HasIndex;
                        index[i * BytesPerPixel + 1] = HasIndex;
                    }
                }

                SavePng(folder / (name + ".png"), decoded.Width, decoded.Height, color);
                SavePng(folder / (name + "_idx.png"), decoded.Width, decoded.Height, index);
            }
        }
    }

    std::cout << std::format("  {} tiles, {} different, {} that don't fit their table\n", tiles, written.size(),
                             skipped);
    CHECK(!written.empty());
}

namespace
{
    /// <summary>
    /// The first shape of <paramref name="table"/> when it is a fast-shape table (a terrain overlay's data, tagged
    /// 'DNAH'): its bytes to the end of its last row (FastShapeSize); none when it isn't one or doesn't fit.
    /// </summary>
    std::optional<std::span<const uint8_t>> OverlayShape(std::span<const uint8_t> table)
    {
        constexpr std::array<uint8_t, 4> Tag = {'D', 'N', 'A', 'H'};
        constexpr size_t FirstShapeOffset = 8;

        if (table.size() < FirstShapeOffset + sizeof(uint32_t) || !std::ranges::equal(table.first(Tag.size()), Tag))
        {
            return std::nullopt;
        }

        const auto offset = static_cast<size_t>(static_cast<uint32_t>(MCVfxRead32(table, FirstShapeOffset)));

        if (offset >= table.size())
        {
            return std::nullopt;
        }

        const std::span<const uint8_t> shape = table.subspan(offset);
        const std::optional<size_t> size = FastShapeSize(shape);
        return size.has_value() ? std::optional(shape.first(*size)) : std::nullopt;
    }

    /// <summary>
    /// Terrain overlay <paramref name="shape"/> (vfx/MCFastShape.cpp's format) decoded: each pixel's index and whether it
    /// is drawn. A run of colour 255 isn't; nor, in a translucent one (its first row starting with the word 1), are
    /// colours 0 and 255, which blend to nothing.
    /// </summary>
    DecodedFrame DecodeOverlay(std::span<const uint8_t> shape)
    {
        constexpr size_t RowOffsets = 0xc;
        constexpr uint32_t Literal = 0x80;
        constexpr uint8_t Clear = 0;
        constexpr uint8_t Key = 255;
        const auto read16 = [&shape](size_t at) { return static_cast<int32_t>(shape[at] | shape[at + 1] << 8); };
        DecodedFrame frame;
        frame.Height = read16(8);
        frame.Width = read16(10) + 1;
        frame.XMin = -static_cast<int16_t>(read16(4));
        frame.YMin = -static_cast<int16_t>(read16(6));
        frame.Translucent = frame.Height > 0 && read16(static_cast<size_t>(read16(RowOffsets))) == 1;
        frame.Index.assign(static_cast<size_t>(frame.Width) * frame.Height, 0);
        frame.Drawn.assign(frame.Index.size(), 0);
        const auto put = [&](int32_t row, int32_t x, uint8_t value, bool run)
        {
            const size_t at = static_cast<size_t>(row) * frame.Width + static_cast<size_t>(x);
            frame.Index[at] = value;
            frame.Drawn[at] = (run && value == Key) || (frame.Translucent && (value == Clear || value == Key)) ? 0 : 1;
        };

        for (int32_t row = 0; row < frame.Height; ++row)
        {
            size_t data = static_cast<size_t>(read16(RowOffsets + static_cast<size_t>(row) * 2));

            for (int32_t x = 0; x < frame.Width && data < shape.size();)
            {
                const uint32_t c = shape[data++];

                if (c >= Literal)
                {
                    for (uint32_t i = 0; i < c - Literal && x < frame.Width && data + i < shape.size(); ++i, ++x)
                    {
                        put(row, x, shape[data + i], false);
                    }

                    data += c - Literal;
                }
                else if (data < shape.size())
                {
                    const uint8_t value = shape[data++];

                    for (uint32_t i = 0; i < c && x < frame.Width; ++i, ++x)
                    {
                        put(row, x, value, true);
                    }
                }
            }
        }

        return frame;
    }
}

namespace
{
    /// <summary>
    /// Calls <paramref name="visit"/> with each different terrain overlay of the tile sets (its fast-shape table and the
    /// shape's bytes, OverlayShape) once; returns how many the tile sets hold, repeats counted.
    /// </summary>
    size_t ForEachOverlay(
        const std::function<void(std::span<const uint8_t> table, std::span<const uint8_t> shape)>& visit)
    {
        std::set<std::string> seen;
        size_t overlays = 0;

        for (const std::string_view set : TileSets)
        {
            for (const std::string_view suffix : TileSetSuffixes)
            {
                MCPacketFile file;

                if (file.Open(std::format("data\\tiles\\{}{}.pak", set, suffix)) != 0)
                {
                    continue;
                }

                for (int32_t packet = 0; packet < file.GetNumPackets(); ++packet)
                {
                    if (file.SeekPacket(packet) != 0 || file.GetPacketSize() <= 0)
                    {
                        continue;
                    }

                    std::vector<uint8_t> data(static_cast<size_t>(file.GetPacketSize()));
                    const std::optional<std::span<const uint8_t>> shape =
                        file.ReadPacket(packet, data) < 0 ? std::nullopt : OverlayShape(data);

                    if (!shape.has_value())
                    {
                        continue;
                    }

                    ++overlays;

                    if (seen.insert(MCHdArt::TerrainTileName(*shape)).second)
                    {
                        visit(data, *shape);
                    }
                }
            }
        }

        return overlays;
    }

    /// <summary>What <see cref="WriteOverlays"/> wrote: the overlays the tile sets hold, repeats counted, the different
    /// ones with pixels, the translucent among them, and their original pixels.</summary>
    struct OverlayCounts
    {
        size_t Overlays = 0;
        size_t Written = 0;
        size_t Translucent = 0;
        size_t Pixels = 0;
    };

    /// <summary>
    /// Writes every terrain overlay with pixels into <paramref name="folder"/> as a PNG pair named as the pack names it
    /// (<see cref="WriteFramePngs"/>, a translucent one holding its blended pixels), at <paramref name="scale"/>, its
    /// colours through <paramref name="paint"/>.
    /// </summary>
    OverlayCounts WriteOverlays(const std::filesystem::path& folder, std::span<const SDL_Color, 256> colors,
                                int32_t scale, const std::function<SDL_Color(SDL_Color)>& paint)
    {
        std::filesystem::create_directories(folder);
        size_t written = 0;
        size_t translucent = 0;
        size_t pixels = 0;
        const size_t overlays = ForEachOverlay(
            [&](std::span<const uint8_t>, std::span<const uint8_t> shape)
            {
                const DecodedFrame frame = DecodeOverlay(shape);

                if (std::ranges::find(frame.Drawn, 1) == frame.Drawn.end())
                {
                    return;
                }

                ++written;
                translucent += frame.Translucent ? 1 : 0;
                pixels += frame.Index.size();
                WriteFramePngs(folder / MCHdArt::TerrainTileName(shape), frame, colors, scale, paint,
                               frame.Translucent);
            });
        return {overlays, written, translucent, pixels};
    }

    /// <summary>The folder of a pack at <paramref name="pack"/> its overlays go in (sprite/MCHdArt.h).</summary>
    std::filesystem::path OverlayFolderOf(const std::filesystem::path& pack)
    {
        return pack / "overlays";
    }
}

/// <summary>
/// Tool: <c>--hd-extract-overlays &lt;folder&gt;</c> writes every terrain overlay of the tile sets (the fast shapes drawn
/// over the ground: roads, rivers' banks, rubble, mines) once, as the HD art generator takes them:
/// <c>&lt;name&gt;.png</c> and <c>&lt;name&gt;_idx.png</c> (sprite/MCHdArt.h; a translucent one holding its blended
/// pixels), named as the pack names them (MCHdArt::TerrainTileName of the shape's bytes), and the shown colours as
/// palette.rgb.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-overlays writes the terrain overlays for the HD art generator")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-overlays");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);
    WritePaletteRgb(folder, colors);
    const OverlayCounts counts = WriteOverlays(folder, colors, 1, [](SDL_Color color) { return color; });
    std::cout << std::format("  {} overlays, {} different with pixels ({} translucent), {} original pixels\n",
                             counts.Overlays, counts.Written, counts.Translucent, counts.Pixels);
    CHECK(counts.Written > 0);
}

TEST_CASE_ISOLATED("tool: --hd-extract writes a mech's frames for the HD art generator")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::string name(MCTest::Option("mech").value_or("madcat"));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);

    // Every tree of that name (the retail PAK has two "hunchbk"s), each shape table once.
    std::vector<Sequence> sequences;
    std::set<std::string> folders;

    for (const auto& [treeName, packet] : MCTestGame::MechTrees())
    {
        if (treeName != name)
        {
            continue;
        }

        const auto tree = std::static_pointer_cast<MCSpriteTree>(
            AppearanceTypeList().GetAppearance(0x01000000u | static_cast<uint32_t>(packet)));
        REQUIRE(tree != nullptr);

        for (Sequence& sequence : MechSequences(tree, std::nullopt))
        {
            if (folders.insert(sequence.Folder.generic_string()).second)
            {
                sequences.push_back(std::move(sequence));
            }
        }
    }

    REQUIRE(!sequences.empty());
    const std::filesystem::path pack(*out);
    WritePack(pack, sequences, colors, 1, [](SDL_Color color) { return color; });

    std::string json = std::format("{{\"mech\": \"{}\", \"sequences\": [", name);
    size_t frameCount = 0;

    for (size_t i = 0; i < sequences.size(); ++i)
    {
        const Sequence& sequence = sequences[i];
        json += std::format("{}\n  {{\"folder\": \"{}\", \"gesture\": {}, \"part\": {}, \"facing\": {}, "
                            "\"reverse\": {}, \"frames\": [",
                            i == 0 ? "" : ",", sequence.Folder.generic_string(), sequence.Gesture, sequence.Part,
                            sequence.Facing, sequence.Reverse ? "true" : "false");

        json += FramesJson(sequence) + "]}";
        frameCount += sequence.Frames.size();
    }

    json += "\n]}\n";
    std::ofstream(pack / std::format("{}.json", name)) << json;

    // The palette, which indices blend (AlphaPal.ini's special colours), and the actors' fade tables (paint schemes).
    std::string palette = std::format("{{\"colors\": {}, \"special\": [", ColorsJson(colors));

    for (size_t i = 0; i < 256; ++i)
    {
        palette += std::format("{}{}", i == 0 ? "" : ", ", SpecialColor[i]);
    }

    palette += "], \"fades\": [";

    for (int32_t index = 0;; ++index)
    {
        const std::optional<MCColorTable> table = GamePalette().GetFadeTable(index);

        if (!table.has_value())
        {
            break;
        }

        palette += index == 0 ? "\n  [" : ",\n  [";

        for (size_t i = 0; i < 256; ++i)
        {
            palette += std::format("{}{}", i == 0 ? "" : ", ", (*table)[i]);
        }

        palette += "]";
    }

    palette += "\n]}\n";
    std::ofstream(pack / "palette.json") << palette;
    std::cout << std::format("  {}: {} sequences, {} frames into {}\n", name, sequences.size(), frameCount,
                             pack.string());
}

namespace
{
    /// <summary>What an appearance other than a mech's is drawn for: the HD art generator's kinds.</summary>
    enum class ObjectKind : uint8_t
    {
        /// <summary>Ground vehicles, train cars, the camera drone.</summary>
        Vehicle,
        Elemental,
        /// <summary>Pop-up turrets and gates.</summary>
        Turret,
        /// <summary>Buildings and tree buildings.</summary>
        Building,
        Tree,
        /// <summary>Terrain objects (rocks, walls...).</summary>
        Object,
        /// <summary>
        /// Explosions, fire, smoke, lights (the glow an explosion, fire or shot casts), weapons' shots, debris, jets: every
        /// other class with shapes.
        /// </summary>
        Effect,
    };

    /// <summary>
    /// A kind's name (the tools' and the generator's), whether its packets are animations (the generator upscales each as
    /// one sequence) or stills, and whether its translucent frames hold their blended pixels (smoke and glow, drawn from
    /// the pack as AlphaPal.ini's colours draw them) or leave them to the original (glass and shadows).
    /// </summary>
    struct ObjectKindInfo
    {
        std::string_view Name;
        bool Animated = false;
        bool Blended = false;
    };

    constexpr std::array<ObjectKindInfo, 7> ObjectKinds = {{{"vehicle", true},
                                                            {"elemental", true},
                                                            {"turret", true},
                                                            {"building", false},
                                                            {"tree", false},
                                                            {"object", false},
                                                            {"effect", true, true}}};

    const ObjectKindInfo& KindInfo(ObjectKind kind)
    {
        return ObjectKinds[static_cast<size_t>(kind)];
    }

    /// <summary>
    /// The object classes (an object type's "ObjectClass" ObjectTypeNum, as MCObjectTypeManager numbers them) and their
    /// kinds. Mechs have folders of their own; every other class with shapes of its own draws effects (explosions, fire,
    /// smoke, lights, weapons, debris, artillery, jets).
    /// </summary>
    constexpr std::array<std::pair<int32_t, ObjectKind>, 11> ClassKinds = {{
        {0x00, ObjectKind::Tree},      // tree
        {0x01, ObjectKind::Building},  // building
        {0x03, ObjectKind::Vehicle},   // ground vehicle
        {0x0b, ObjectKind::Object},    // terrain object
        {0x0e, ObjectKind::Elemental}, // elemental
        {0x0f, ObjectKind::Object},    // misc terrain object
        {0x12, ObjectKind::Building},  // tree building
        {0x13, ObjectKind::Vehicle},   // camera drone
        {0x14, ObjectKind::Vehicle},   // train car
        {0x15, ObjectKind::Turret},    // turret
        {0x16, ObjectKind::Turret},    // gate
    }};

    /// <summary>An appearance type id's class (its top byte) and packet in the sprite PAKs (the rest).</summary>
    constexpr int AppearanceClassShift = 24;
    constexpr uint32_t AppearancePacketMask = 0xffffff;
    /// <summary>The facings walked to find every packet a type draws (degrees, whole ones as the types take them).</summary>
    constexpr int32_t FirstRotation = -180;
    constexpr int32_t LastRotation = 180;

    /// <summary>
    /// An appearance the pack can hold: its type id, kind, and the names of the object types that use it (none: no
    /// object type uses it, every packet is one no type draws).
    /// </summary>
    struct ObjectAppearance
    {
        uint32_t Id = 0;
        ObjectKind Kind = ObjectKind::Object;
        std::vector<std::string> Types;
    };

    /// <summary>
    /// The appearances the object PAK's types use, by type id: each with the kind of the first class that uses it
    /// (ClassKinds; an effect's when none does), except mechs' and lines' (no shapes of their own).
    /// </summary>
    std::map<uint32_t, ObjectAppearance> FindObjectAppearances()
    {
        std::map<uint32_t, ObjectAppearance> found;
        std::map<uint32_t, ObjectAppearance> effects;
        MCPacketFile pak;
        REQUIRE(pak.Open(GamePath(ObjectPath, ObjectPakName)) == 0);

        for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
        {
            MCFitIniFile fit;

            if (pak.SeekPacket(packet) != 0 || pak.GetPacketSize() <= 0 ||
                fit.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())) != 0 || fit.SeekBlock("ObjectClass") != 0)
            {
                continue;
            }

            const MCFitResult<int32_t> objectClass = fit.Read<int32_t>("ObjectTypeNum");

            if (!objectClass.has_value() || fit.SeekBlock("ObjectType") != 0)
            {
                continue;
            }

            const MCFitResult<int32_t> appearance = fit.Read<int32_t>("Appearance");
            const auto id = static_cast<uint32_t>(appearance.value_or(0));
            const auto appearanceClass = static_cast<MCAppearanceClass>(id >> AppearanceClassShift);

            if (!appearance.has_value() ||
                (appearanceClass != MCAppearanceClass::Vfx && appearanceClass != MCAppearanceClass::GroundVehicle &&
                 appearanceClass != MCAppearanceClass::Arm && appearanceClass != MCAppearanceClass::Building &&
                 appearanceClass != MCAppearanceClass::ElementalTree && appearanceClass != MCAppearanceClass::PopUp))
            {
                continue;
            }

            const auto classKind = std::ranges::find(ClassKinds, *objectClass, &std::pair<int32_t, ObjectKind>::first);
            const bool effect = classKind == ClassKinds.end();
            const ObjectKind kind = effect ? ObjectKind::Effect : classKind->second;
            const std::string name = fit.Read<std::string>("Name").value_or(std::format("type {}", packet));
            auto [entry, fresh] = (effect ? effects : found).try_emplace(id, ObjectAppearance{id, kind, {}});
            entry->second.Types.push_back(name);

            if (!fresh && entry->second.Kind != kind)
            {
                std::cout << std::format("  appearance {:#010x}: a {} and a {} ({}); kept as the first\n", id,
                                         KindInfo(entry->second.Kind).Name, KindInfo(kind).Name, name);
            }
        }

        // An appearance an effect shares with another class is that class's: the effect draws the same shapes.
        found.merge(effects);
        return found;
    }

    /// <summary>Every shape in <paramref name="list"/> still loaded, by packet.</summary>
    void KeepLoaded(std::span<const std::weak_ptr<MCShape>> list, std::map<uint32_t, std::shared_ptr<MCShape>>& shapes)
    {
        for (size_t packet = 0; packet < list.size(); ++packet)
        {
            if (std::shared_ptr<MCShape> shape = list[packet].lock())
            {
                shapes.emplace(static_cast<uint32_t>(packet), std::move(shape));
            }
        }
    }

    /// <summary>
    /// The shapes of <paramref name="type"/>'s PAK that it draws, by packet: what its own lookups give over every
    /// state, part and facing. The packets left out are the original's zoomed-out views.
    /// </summary>
    std::map<uint32_t, std::shared_ptr<MCShape>> DrawnShapes(const std::shared_ptr<MCAppearanceType>& type)
    {
        std::map<uint32_t, std::shared_ptr<MCShape>> shapes;
        float frameRate = 0.0f;

        switch (static_cast<MCAppearanceClass>(type->AppearanceNum >> AppearanceClassShift))
        {
            case MCAppearanceClass::Vfx:
            {
                auto& vfx = static_cast<MCVfxAppearanceType&>(*type);

                for (size_t state = 0; state < vfx.States.size(); ++state)
                {
                    // No sub-state, then each one.
                    for (size_t sub = 0; sub <= vfx.SubStates.size(); ++sub)
                    {
                        for (int32_t rotation = FirstRotation; rotation < LastRotation; ++rotation)
                        {
                            bool reverse = false;
                            vfx.GetShape(static_cast<MCActorState>(state),
                                         sub == 0 ? NoSubState : static_cast<uint8_t>(sub - 1), rotation, frameRate,
                                         reverse);
                        }
                    }
                }

                KeepLoaded(vfx.ShapeList, shapes);
                break;
            }
            case MCAppearanceClass::GroundVehicle:
            {
                auto& vehicle = static_cast<MCGVAppearanceType&>(*type);

                for (int32_t state = 0; state < GVActorStateCount; ++state)
                {
                    for (int32_t part = 0; part < static_cast<int32_t>(vehicle.NumParts); ++part)
                    {
                        for (int32_t rotation = FirstRotation; rotation < LastRotation; ++rotation)
                        {
                            vehicle.GetShape(static_cast<MCGVActorState>(state), rotation, part, frameRate);
                        }
                    }
                }

                KeepLoaded(vehicle.ShapeList, shapes);
                break;
            }
            case MCAppearanceClass::Arm:
            {
                // A weapon's flash at its shooter (a projectile laser's), one packet a facing.
                auto& arm = static_cast<MCArmAppearanceType&>(*type);

                for (int32_t rotation = FirstRotation; rotation < LastRotation; ++rotation)
                {
                    bool reverse = false;
                    arm.GetShape(rotation, frameRate, reverse);
                }

                KeepLoaded(arm.ShapeList, shapes);
                break;
            }
            case MCAppearanceClass::Building:
            {
                auto& building = static_cast<MCVfxBuildingAppearanceType&>(*type);

                for (uint32_t frame = 0; frame < building.ShapeList.size(); ++frame)
                {
                    building.GetShape(frame);
                    building.GetTileShape(frame);
                }

                KeepLoaded(building.ShapeList, shapes);
                break;
            }
            case MCAppearanceClass::ElementalTree:
            {
                auto& elemental = static_cast<MCElementalTree&>(*type);

                for (int32_t gesture = 0; gesture < static_cast<int32_t>(elemental.Gestures.size()); ++gesture)
                {
                    for (float facing = FirstFacing; facing < LastFacing; facing += FacingStep)
                    {
                        elemental.GetGesture(gesture, facing);
                    }
                }

                KeepLoaded(elemental.ShapeList, shapes);
                break;
            }
            case MCAppearanceClass::PopUp:
            {
                auto& popUp = static_cast<MCPUAppearanceType&>(*type);

                for (int32_t state = 0; state < PUActorStateCount; ++state)
                {
                    for (int32_t rotation = FirstRotation; rotation < LastRotation; ++rotation)
                    {
                        popUp.GetShape(static_cast<MCPUActorState>(state), rotation, 0, frameRate);
                    }
                }

                KeepLoaded(popUp.ShapeList, shapes);
                break;
            }
            default:
                break;
        }

        return shapes;
    }

    /// <summary>
    /// Which of an appearance's packets the object tools take (<c>--hd-packets drawn|all|undrawn</c>): those its type's
    /// lookups draw (the default), every packet of its PAK, or only those its lookups never reach (the original's
    /// zoomed-out views, buildings' packets 10 and 11, facings past a type's rotations). Past the default, the
    /// appearances no object type uses are taken too, every packet of each.
    /// </summary>
    enum class PacketChoice : uint8_t
    {
        Drawn,
        All,
        Undrawn,
    };

    constexpr std::array<std::pair<std::string_view, PacketChoice>, 3> PacketChoices = {
        {{"drawn", PacketChoice::Drawn}, {"all", PacketChoice::All}, {"undrawn", PacketChoice::Undrawn}}};

    /// <summary>The packets <c>--hd-packets</c> picks.</summary>
    PacketChoice PacketsOption()
    {
        const std::string_view name = MCTest::Option("hd-packets").value_or(PacketChoices.front().first);
        const auto found = std::ranges::find(PacketChoices, name, &std::pair<std::string_view, PacketChoice>::first);
        MCTest::Scope scope(std::format("--hd-packets {} (drawn, all or undrawn)", name));
        REQUIRE(found != PacketChoices.end());
        return found->second;
    }

    /// <summary>
    /// The shapes of <paramref name="type"/>'s PAK that <paramref name="packets"/> picks, by packet: those it draws
    /// (<see cref="DrawnShapes"/>), every one, or every one it doesn't draw.
    /// </summary>
    std::map<uint32_t, std::shared_ptr<MCShape>> ChosenShapes(const std::shared_ptr<MCAppearanceType>& type,
                                                              PacketChoice packets)
    {
        std::map<uint32_t, std::shared_ptr<MCShape>> drawn = DrawnShapes(type);

        if (packets == PacketChoice::Drawn)
        {
            return drawn;
        }

        const uint32_t appearance = type->AppearanceNum & AppearancePacketMask;
        std::map<uint32_t, std::shared_ptr<MCShape>> shapes;

        for (int32_t packet = 0; packet < SpriteManager()->GetNumShapes(appearance); ++packet)
        {
            const auto number = static_cast<uint32_t>(packet);

            if (packets == PacketChoice::Undrawn && drawn.contains(number))
            {
                continue;
            }

            if (std::shared_ptr<MCShape> shape = SpriteManager()->GetShapeData(appearance, number, Turn, type))
            {
                shapes.emplace(number, std::move(shape));
            }
        }

        return shapes;
    }

    /// <summary>The frames of <paramref name="sequence"/> with pixels (those a pack holds).</summary>
    size_t DrawnFrames(const Sequence& sequence)
    {
        return static_cast<size_t>(
            std::ranges::count_if(sequence.Frames, [](const DecodedFrame& frame) { return frame.Width > 0; }));
    }

    /// <summary>
    /// Leaves out of a translucent frame (drawn through AlphaPal.ini) the pixels of colours 0 and 255, which draw nothing,
    /// and, unless <paramref name="keepBlended"/>, those it blends: the pack's frame then shows its opaque pixels over the
    /// original's blended ones.
    /// </summary>
    void LeaveOutUndrawn(DecodedFrame& frame, bool keepBlended)
    {
        constexpr uint8_t Clear = 0;
        constexpr uint8_t Key = 255;

        for (size_t i = 0; i < frame.Index.size(); ++i)
        {
            const uint8_t index = frame.Index[i];

            if (index == Clear || index == Key || (!keepBlended && SpecialColor[index] != 0))
            {
                frame.Drawn[i] = 0;
            }
        }
    }

    /// <summary>An appearance's sequences (one a packet it draws, its frames decoded), and what was left out.</summary>
    struct ObjectSequences
    {
        std::vector<Sequence> Sequences;
        /// <summary>
        /// The frames drawn translucent (their blended pixels left out, the original's still blending them, unless they
        /// are kept), and those that can't be read (left out).
        /// </summary>
        size_t Translucent = 0;
        size_t Unreadable = 0;
        /// <summary>The original pixels of the frames kept (their bounds).</summary>
        size_t Pixels = 0;
        /// <summary>Whether the type's frames are deltas drawn over its first (a VFX type's FIT "delta").</summary>
        bool Delta = false;
    };

    /// <summary>
    /// The frames a second a VFX type plays <paramref name="packet"/> at: its state's, which the game keeps in a
    /// sub-state too (MCVfxAppearance::Update); 0 when no state draws the packet.
    /// </summary>
    float VfxPacketFrameRate(const MCVfxAppearanceType& vfx, uint32_t packet)
    {
        // A state's facings are its packets from its base (every other one when Scaled: the zoomed-out views between).
        const uint32_t step = vfx.Scaled ? 2u : 1u;
        const auto draws = [&](const MCActorData& state, const MCActorData& frames)
        {
            const uint32_t facings = std::max<uint32_t>(state.NumRotations, 1u);
            return frames.NumFrames > 0 && packet >= frames.BasePacketNumber &&
                   packet < frames.BasePacketNumber + facings * step;
        };

        for (const MCActorData& state : vfx.States)
        {
            if (draws(state, state))
            {
                return state.FrameRate;
            }
        }

        // The sub-states are the normal state's (MCVfxAppearanceType::GetShape).
        const MCActorData& normal = vfx.StateData(MCActorState::Normal);

        for (const MCActorData& sub : vfx.SubStates)
        {
            if (draws(normal, sub))
            {
                return normal.FrameRate;
            }
        }

        return 0.0f;
    }

    /// <summary>
    /// The frames a second <paramref name="type"/> plays <paramref name="packet"/> at: a VFX state's, a weapon effect's
    /// (its one state's); 0 for the other classes.
    /// </summary>
    float PacketFrameRate(const MCAppearanceType& type, uint32_t packet)
    {
        switch (static_cast<MCAppearanceClass>(type.AppearanceNum >> AppearanceClassShift))
        {
            case MCAppearanceClass::Vfx:
                return VfxPacketFrameRate(static_cast<const MCVfxAppearanceType&>(type), packet);
            case MCAppearanceClass::Arm:
                return static_cast<const MCArmAppearanceType&>(type).ActorData.FrameRate;
            default:
                return 0.0f;
        }
    }

    /// <summary>
    /// <paramref name="type"/>'s sequences (one a packet <paramref name="packets"/> picks), their translucent frames
    /// holding their blended pixels when <paramref name="keepBlended"/> (an effect's).
    /// </summary>
    ObjectSequences ObjectSequencesOf(const std::shared_ptr<MCAppearanceType>& type, bool keepBlended,
                                      PacketChoice packets = PacketChoice::Drawn)
    {
        const std::shared_ptr<MCSpriteManager> sprites = SpriteManager();
        REQUIRE(sprites != nullptr);
        const uint32_t appearance = type->AppearanceNum & AppearancePacketMask;
        const std::string shapeFile = sprites->ShapeFileName(appearance);
        ObjectSequences made;

        if (static_cast<MCAppearanceClass>(type->AppearanceNum >> AppearanceClassShift) == MCAppearanceClass::Vfx)
        {
            made.Delta = static_cast<const MCVfxAppearanceType&>(*type).Delta;
        }

        for (const auto& [packet, shape] : ChosenShapes(type, packets))
        {
            if (!VfxIsShapeTable(shape->FrameList()))
            {
                continue;
            }

            Sequence& sequence = made.Sequences.emplace_back();
            sequence.Folder = MCHdArt::FolderOf(MCHdArt::ObjectShapeKey{shapeFile, appearance, packet});
            sequence.Blended = keepBlended;
            sequence.FrameRate = PacketFrameRate(*type, packet);
            const std::span<const uint8_t> table = shape->FrameList();
            sequence.Table = std::hash<std::string_view>{}(
                std::string_view(reinterpret_cast<const char*>(table.data()), table.size()));

            for (int32_t frame = 0; frame < VfxShapeCount(shape->FrameList()); ++frame)
            {
                auto decoded = Decode(shape->FrameList(), frame);

                if (!decoded)
                {
                    ++made.Unreadable;
                }
                else if (decoded->Translucent)
                {
                    ++made.Translucent;
                    LeaveOutUndrawn(*decoded, keepBlended);
                }

                // Left out of the pack (drawn from the original): a frame that can't be read, or has no opaque pixel.
                if (!decoded || std::ranges::find(decoded->Drawn, 1) == decoded->Drawn.end())
                {
                    sequence.Frames.emplace_back();
                    continue;
                }

                made.Pixels += static_cast<size_t>(decoded->Width) * static_cast<size_t>(decoded->Height);
                sequence.Frames.push_back(std::move(*decoded));
            }

            // A table with nothing the pack would hold gets no folder (the shape isn't attached).
            if (DrawnFrames(sequence) == 0)
            {
                made.Sequences.pop_back();
            }
        }

        return made;
    }

    /// <summary>The items of a comma-separated option (none when it isn't given).</summary>
    std::set<std::string> ListOption(std::string_view name)
    {
        std::set<std::string> items;

        for (const auto part : std::views::split(MCTest::Option(name).value_or(""), ','))
        {
            if (!part.empty())
            {
                items.emplace(part.begin(), part.end());
            }
        }

        return items;
    }

    /// <summary>
    /// The classes an appearance no object type uses is tried as (its FIT in the sprite PAK, the first class whose loader
    /// takes it), and the kind each gives it: the classes with blocks of their own first, the VFX's before the pop-ups'
    /// (whose FIT a VFX one, with Symmetrical in its states, also fits). A VFX appearance's kind is whether it is Scaled
    /// (<see cref="VfxKind"/>), not the one here.
    /// </summary>
    constexpr std::array<std::pair<MCAppearanceClass, ObjectKind>, 6> UntypedClasses = {{
        {MCAppearanceClass::ElementalTree, ObjectKind::Elemental},
        {MCAppearanceClass::GroundVehicle, ObjectKind::Vehicle},
        {MCAppearanceClass::Arm, ObjectKind::Effect},
        {MCAppearanceClass::Building, ObjectKind::Building},
        {MCAppearanceClass::Vfx, ObjectKind::Object},
        {MCAppearanceClass::PopUp, ObjectKind::Turret},
    }};

    /// <summary>
    /// The kind a VFX appearance no object type uses is made as: a still object when it is Scaled (has the original's
    /// zoomed-out views, as the world's trees, buildings and objects do), else an effect (explosions, fire, smoke). Of the
    /// VFX appearances types use, 631 of 648 are of the kind this gives (lights and two effects are Scaled).
    /// </summary>
    ObjectKind VfxKind(const MCVfxAppearanceType& vfx)
    {
        return vfx.Scaled ? ObjectKind::Object : ObjectKind::Effect;
    }

    /// <summary>
    /// The sprite PAK's appearances with shapes that no appearance in <paramref name="typed"/> is, each with the first
    /// class of <see cref="UntypedClasses"/> its FIT loads as and that class's kind; one none loads is named and left out.
    /// </summary>
    std::map<uint32_t, ObjectAppearance> FindUntypedAppearances(const std::map<uint32_t, ObjectAppearance>& typed)
    {
        std::set<uint32_t> typedPackets;

        for (const auto& [id, appearance] : typed)
        {
            typedPackets.insert(id & AppearancePacketMask);
        }

        std::map<uint32_t, ObjectAppearance> found;

        for (uint32_t packet = 0; packet < SpriteManager()->GetNumAppearances(); ++packet)
        {
            if (typedPackets.contains(packet) || SpriteManager()->GetNumShapes(packet) <= 0)
            {
                continue;
            }

            std::shared_ptr<MCAppearanceType> type;
            const auto loads =
                std::ranges::find_if(UntypedClasses,
                                     [packet, &type](const std::pair<MCAppearanceClass, ObjectKind>& candidate)
                                     {
                                         type = AppearanceTypeList().GetAppearance(
                                             std::to_underlying(candidate.first) << AppearanceClassShift | packet);
                                         return type != nullptr;
                                     });

            if (loads == UntypedClasses.end())
            {
                std::cout << std::format("  sprite PAK appearance {} ({}): no type uses it and its FIT loads as no "
                                         "class; left out\n",
                                         packet, SpriteManager()->ShapeFileName(packet));
                continue;
            }

            const ObjectKind kind = loads->first == MCAppearanceClass::Vfx
                                        ? VfxKind(static_cast<const MCVfxAppearanceType&>(*type))
                                        : loads->second;
            found.emplace(type->AppearanceNum, ObjectAppearance{type->AppearanceNum, kind, {}});
        }

        return found;
    }

    /// <summary>
    /// The appearances <c>--kind &lt;name,...&gt;</c> and <c>--appearance &lt;packet,...&gt;</c> pick (every one when
    /// neither is given), each loaded; past <see cref="PacketChoice::Drawn"/>, those no object type uses too.
    /// </summary>
    std::vector<std::pair<ObjectAppearance, std::shared_ptr<MCAppearanceType>>> PickedObjectAppearances(
        PacketChoice packetChoice = PacketChoice::Drawn)
    {
        const std::set<std::string> kinds = ListOption("kind");
        std::set<uint32_t> packets;

        for (const std::string& packet : ListOption("appearance"))
        {
            uint32_t number = 0;
            const auto [end, error] = std::from_chars(packet.data(), packet.data() + packet.size(), number);
            MCTest::Scope scope(std::format("--appearance {}", packet));
            REQUIRE((error == std::errc() && end == packet.data() + packet.size()));
            packets.insert(number);
        }

        std::vector<std::pair<ObjectAppearance, std::shared_ptr<MCAppearanceType>>> picked;
        std::map<uint32_t, ObjectAppearance> appearances = FindObjectAppearances();

        if (packetChoice != PacketChoice::Drawn)
        {
            appearances.merge(FindUntypedAppearances(appearances));
        }

        for (const auto& [id, appearance] : appearances)
        {
            if ((!kinds.empty() && !kinds.contains(std::string(KindInfo(appearance.Kind).Name))) ||
                (!packets.empty() && !packets.contains(id & AppearancePacketMask)))
            {
                continue;
            }

            if (std::shared_ptr<MCAppearanceType> type = AppearanceTypeList().GetAppearance(id); type != nullptr)
            {
                picked.emplace_back(appearance, std::move(type));
            }
        }

        return picked;
    }

    /// <summary>
    /// The packets of <paramref name="appearance"/> that <paramref name="packets"/> picks: every one of an appearance no
    /// object type uses (no type draws any).
    /// </summary>
    PacketChoice PacketsOf(const ObjectAppearance& appearance, PacketChoice packets)
    {
        return appearance.Types.empty() ? PacketChoice::All : packets;
    }

    /// <summary>What --hd-objects counts per kind, and how much of it repeats another sequence (made once).</summary>
    struct KindTotals
    {
        size_t Appearances = 0;
        size_t Sequences = 0;
        size_t Frames = 0;
        size_t Pixels = 0;
        size_t RepeatedSequences = 0;
        size_t RepeatedPixels = 0;
    };

    /// <summary>
    /// The sequences seen so far by shape table and by how the generator makes them (animated or still, delta or not),
    /// so that one repeating an earlier one is made once and copied.
    /// </summary>
    class MCRepeats
    {
    public:
        /// <summary>The folder of the first sequence <paramref name="sequence"/> repeats; none (noted) for a first.</summary>
        std::optional<std::filesystem::path> Of(const Sequence& sequence, const ObjectKindInfo& kind, bool delta)
        {
            const auto [found, fresh] =
                _First.try_emplace(std::tuple{sequence.Table, kind.Animated, kind.Blended, delta}, sequence.Folder);
            return fresh ? std::nullopt : std::optional(found->second);
        }

    private:
        std::map<std::tuple<size_t, bool, bool, bool>, std::filesystem::path> _First;
    };
}

/// <summary>
/// Tool: <c>--hd-objects 1</c> lists the appearances the HD art pack holds besides the mechs, for
/// --hd-extract-objects: a line "object &lt;kind&gt; &lt;type id&gt; &lt;folder&gt; &lt;sequences&gt; &lt;frames&gt;
/// &lt;pixels&gt; &lt;delta&gt; &lt;object types&gt;" each (the kinds: vehicle, elemental, turret and effect animated;
/// building, tree and object still), then each kind's totals and the sequences that repeat another's shape table (made
/// once). <c>--kind</c>, <c>--appearance</c> and <c>--hd-packets</c> pick as --hd-extract-objects does (an appearance no
/// type uses is listed with "(no type)").
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-objects lists the vehicles, Elementals and objects")
{
    if (!MCTest::Option("hd-objects").has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<KindTotals, ObjectKinds.size()> totals{};
    MCRepeats repeats;

    const PacketChoice packets = PacketsOption();

    for (const auto& [appearance, type] : PickedObjectAppearances(packets))
    {
        const ObjectSequences made =
            ObjectSequencesOf(type, KindInfo(appearance.Kind).Blended, PacketsOf(appearance, packets));

        // An appearance no type uses is listed only when it has frames.
        if (appearance.Types.empty() && made.Sequences.empty())
        {
            continue;
        }

        KindTotals& total = totals[static_cast<size_t>(appearance.Kind)];
        size_t frames = 0;

        for (const Sequence& sequence : made.Sequences)
        {
            frames += DrawnFrames(sequence);

            if (repeats.Of(sequence, KindInfo(appearance.Kind), made.Delta).has_value())
            {
                ++total.RepeatedSequences;

                for (const DecodedFrame& frame : sequence.Frames)
                {
                    total.RepeatedPixels += static_cast<size_t>(frame.Width) * static_cast<size_t>(frame.Height);
                }
            }
        }

        std::string types = appearance.Types.empty() ? std::string(" (no type)") : std::string();

        for (const std::string& name : appearance.Types)
        {
            types += " " + name;
        }

        const std::filesystem::path folder =
            made.Sequences.empty() ? std::filesystem::path("-") : made.Sequences.front().Folder.parent_path();
        std::cout << std::format("object {} {:#010x} {} {} {} {} {}{}\n", KindInfo(appearance.Kind).Name, appearance.Id,
                                 folder.generic_string(), made.Sequences.size(), frames, made.Pixels,
                                 made.Delta ? "delta" : "-", types);

        if (made.Translucent + made.Unreadable > 0)
        {
            std::cout << std::format("  ({} translucent frames, their blended pixels {}; {} that can't be read)\n",
                                     made.Translucent, KindInfo(appearance.Kind).Blended ? "kept" : "left out",
                                     made.Unreadable);
        }

        ++total.Appearances;
        total.Sequences += made.Sequences.size();
        total.Frames += frames;
        total.Pixels += made.Pixels;
    }

    for (size_t kind = 0; kind < ObjectKinds.size(); ++kind)
    {
        const KindTotals& total = totals[kind];
        std::cout << std::format("  {}: {} appearances, {} sequences, {} frames, {} original pixels; {} sequences ({} "
                                 "pixels) repeat another's\n",
                                 ObjectKinds[kind].Name, total.Appearances, total.Sequences, total.Frames, total.Pixels,
                                 total.RepeatedSequences, total.RepeatedPixels);
    }
}

/// <summary>
/// Tool: <c>--hd-extract-objects &lt;folder&gt;</c> writes the vehicles', Elementals', turrets', buildings', trees',
/// terrain objects' and effects' frames (<c>--kind</c> and <c>--appearance</c> pick, as --hd-objects lists them) as an HD
/// art pack at their own size, for tools/upscale/make_object_pack.py: every packet each appearance draws, each frame a
/// PNG with its index picture (sprite/MCHdArt.h; a translucent frame without the pixels it blends, which the original
/// still draws, except an effect's, which holds them premultiplied), plus <c>objects.json</c> (each appearance's kind,
/// whether it is animated or still, whether it holds its blended pixels, and its sequences' frames' bounds, as
/// --hd-extract's) and the shown colours as palette.rgb. A sequence whose shape table an earlier one has (made the same
/// way) is written only as <c>"same_as": "&lt;that one's folder&gt;"</c>: the generator copies it. With
/// <c>--hd-packets all</c> every packet of each appearance's PAK is written, with <c>--hd-packets undrawn</c> only those
/// no lookup reaches (the original's zoomed-out views...), each under the folder the game would look it up by; either
/// adds the appearances no object type uses (no types in objects.json), each of the kind its class gives.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-objects writes the vehicles', Elementals' and objects' frames")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-objects");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path pack(*out);
    std::filesystem::create_directories(pack);
    WritePaletteRgb(pack, colors);
    std::string json = "{\"appearances\": [";
    size_t appearances = 0;
    size_t frames = 0;
    MCRepeats repeats;

    const PacketChoice packets = PacketsOption();

    for (const auto& [appearance, type] : PickedObjectAppearances(packets))
    {
        const ObjectSequences made =
            ObjectSequencesOf(type, KindInfo(appearance.Kind).Blended, PacketsOf(appearance, packets));

        if (made.Sequences.empty())
        {
            continue;
        }

        std::vector<std::optional<std::filesystem::path>> repeating;
        std::vector<Sequence> written;

        for (const Sequence& sequence : made.Sequences)
        {
            repeating.push_back(repeats.Of(sequence, KindInfo(appearance.Kind), made.Delta));

            if (!repeating.back().has_value())
            {
                written.push_back(sequence);
                frames += DrawnFrames(sequence);
            }
        }

        WritePack(pack, written, colors, 1, [](SDL_Color color) { return color; });
        std::string types;

        for (const std::string& name : appearance.Types)
        {
            types += std::format("{}\"{}\"", types.empty() ? "" : ", ", name);
        }

        json += std::format("{}\n {{\"id\": {}, \"kind\": \"{}\", \"animated\": {}, \"blended\": {}, \"delta\": {}, "
                            "\"types\": [{}], \"sequences\": [",
                            appearances++ == 0 ? "" : ",", appearance.Id, KindInfo(appearance.Kind).Name,
                            KindInfo(appearance.Kind).Animated ? "true" : "false",
                            KindInfo(appearance.Kind).Blended ? "true" : "false", made.Delta ? "true" : "false", types);

        for (size_t i = 0; i < made.Sequences.size(); ++i)
        {
            const Sequence& sequence = made.Sequences[i];
            const std::string sameAs = repeating[i].has_value()
                                           ? std::format(", \"same_as\": \"{}\"", repeating[i]->generic_string())
                                           : std::string();
            const std::string frameRate =
                sequence.FrameRate > 0.0f ? std::format(", \"frame_rate\": {}", sequence.FrameRate) : std::string();
            json += std::format("{}\n  {{\"folder\": \"{}\"{}{}, \"frames\": [{}]}}", i == 0 ? "" : ",",
                                sequence.Folder.generic_string(), sameAs, frameRate, FramesJson(sequence));
        }

        json += "]}";
    }

    json += "\n]}\n";
    std::ofstream(pack / "objects.json") << json;
    std::cout << std::format("  {} appearances, {} frames (besides the repeated sequences') into {}\n", appearances,
                             frames, pack.string());
    CHECK(appearances > 0);
}

namespace
{
    /// <summary>Mission 1 drawn by <paramref name="drawing"/>, its world view shown larger than the screen, the
    /// player's mech a Mad Cat standing in the middle, and a pack of its standing gesture: the original's pixels
    /// doubled, their colours inverted.</summary>
    struct HdScene
    {
        std::shared_ptr<MCVulkanRenderer> Renderer;
        PlayerMech Player;
        std::filesystem::path Scratch;
        std::filesystem::path Pack;
    };

    /// <summary>The tests' packs: the original's pixels at twice their size, their colours inverted.</summary>
    constexpr int32_t TestPackScale = 2;

    SDL_Color Inverted(SDL_Color color)
    {
        return SDL_Color{static_cast<uint8_t>(255 - color.r), static_cast<uint8_t>(255 - color.g),
                         static_cast<uint8_t>(255 - color.b), 255};
    }

    /// <summary>
    /// Mission <paramref name="mission"/> drawn by <paramref name="drawing"/>, its world view shown larger than the
    /// screen; null (noted) when the GPU can't draw it so.
    /// </summary>
    std::shared_ptr<MCVulkanRenderer> StartScaledUp(MCGpuDrawing drawing, int32_t mission = 1)
    {
        MCRenderer::RequestGpuDrawing(drawing);
        REQUIRE(MCTestGame::StartMission(mission));
        auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());

        if (renderer == nullptr || MCRenderer::GpuDrawing() != drawing)
        {
            std::cout << "  (skipped: no GPU renderer on this machine)\n";
            return nullptr;
        }

        constexpr int32_t WindowWidth = 1600;
        constexpr int32_t WindowHeight = 1200;
        constexpr int UiLines = 480;
        ScaleUp(WindowWidth, WindowHeight, UiLines);
        return renderer;
    }

    std::optional<HdScene> SetUpHdScene(MCGpuDrawing drawing)
    {
        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        HdScene scene;
        scene.Renderer = StartScaledUp(drawing);

        if (scene.Renderer == nullptr)
        {
            return std::nullopt;
        }

        scene.Player = GivePlayerMech("madcat");
        MCTestGame::FaceMech(*scene.Player.Mech, 0.0f);
        MCTestGame::RunFrame(FrameSeconds);

        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        scene.Scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_pack";
        std::filesystem::remove_all(scene.Scratch);
        scene.Pack = scene.Scratch / "pack";
        const std::vector<Sequence> standing = MechSequences(scene.Player.Tree, StandGesture);
        REQUIRE(!standing.empty());
        WritePack(scene.Pack, standing, colors, TestPackScale, Inverted);
        return scene;
    }

    /// <summary>
    /// HD art on with a pack while it lives (the shapes loaded from then on find its frames), and off again however the
    /// test ends.
    /// </summary>
    class MCHdArtOn
    {
    public:
        explicit MCHdArtOn(const std::filesystem::path& pack)
        {
            GHdArt = 1;
            MCHdArt::SetPackFolder(pack);

            // None before the game starts. The shapes still in use are attached as the game attaches them as it loads
            // them with HD art on.
            if (const std::shared_ptr<MCSpriteManager> sprites = SpriteManager(); sprites != nullptr)
            {
                sprites->DumpAll();
            }

            AttachHdArtToLoaded();
        }

        ~MCHdArtOn()
        {
            GHdArt = 0;
            MCHdArt::SetPackFolder({});
        }

        MCHdArtOn(const MCHdArtOn&) = delete;
        MCHdArtOn& operator=(const MCHdArtOn&) = delete;
    };

    /// <summary>
    /// A pack with no frames (only palette.rgb), so with HD art on everything is drawn from the original's pixels (lines
    /// and ellipses drawn smooth); under the tests' scratch folder <paramref name="name"/>.
    /// </summary>
    std::filesystem::path EmptyPack(std::string_view name)
    {
        const std::filesystem::path scratch = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(scratch);
        const std::filesystem::path pack = scratch / "pack";
        std::filesystem::create_directories(pack);
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        WritePaletteRgb(pack, colors);
        return pack;
    }

    /// <summary>The pixels whose shown colour differs between <paramref name="a"/> and <paramref name="b"/> (within
    /// <paramref name="within"/> when given), and their mean position.</summary>
    struct Changed
    {
        int32_t Count = 0;
        int64_t X = 0;
        int64_t Y = 0;
    };

    Changed Compare(const Shown& a, const Shown& b, std::optional<MCRect> within = std::nullopt)
    {
        REQUIRE_EQ(a.Width, b.Width);
        REQUIRE_EQ(a.Height, b.Height);
        const MCRect region = within.value_or(MCRect{0, 0, a.Width - 1, a.Height - 1});
        Changed changed;

        for (int32_t y = region.Y0; y <= region.Y1; ++y)
        {
            for (int32_t x = region.X0; x <= region.X1; ++x)
            {
                const size_t at = (static_cast<size_t>(y) * a.Width + x) * BytesPerPixel;

                if (std::memcmp(&a.Pixels[at], &b.Pixels[at], 3) != 0)
                {
                    ++changed.Count;
                    changed.X += x;
                    changed.Y += y;
                }
            }
        }

        if (changed.Count > 0)
        {
            changed.X /= changed.Count;
            changed.Y /= changed.Count;
        }

        return changed;
    }
}

/// <summary>
/// With HD art on and a pack holding frames of the standing mech's shapes, the GPU shows the mech from the pack in a
/// world view shown larger than the screen: the shown colours change where the mech is, and nowhere else. (The mech's
/// animation is held, so the frames before and after differ only in how the mech is drawn.)
/// </summary>
TEST_CASE_ISOLATED("game: an HD art pack's frames show in a mech's place")
{
    const std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    SingleStepMode = 1;
    MCTestGame::RunFrame(FrameSeconds);
    const Shown original = ReadShown(scene->Scratch);
    MCTestGame::RunFrame(FrameSeconds);
    const Changed still = Compare(original, ReadShown(scene->Scratch));
    CHECK_EQ(still.Count, 0);

    std::optional<MCHdArtOn> hdArt(std::in_place, scene->Pack);
    const int64_t drawnBefore = scene->Renderer->HdDrawn();
    MCTestGame::RunFrame(FrameSeconds);
    const Shown hd = ReadShown(scene->Scratch);
    hdArt.reset();

    CHECK(scene->Renderer->HdDrawn() > drawnBefore);
    CHECK(MCHdArt::FramesRead() > 0);
    const Changed changed = Compare(original, hd);
    std::cout << std::format("  {} shown pixels changed of {}x{}, about ({}, {})\n", changed.Count, hd.Width, hd.Height,
                             changed.X, changed.Y);
    REQUIRE(changed.Count > 0);
    CHECK(std::abs(changed.X - hd.Width / 2) < hd.Width / 4);
    CHECK(std::abs(changed.Y - hd.Height / 2) < hd.Height / 4);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>
/// HD art changes only the colours shown: in mirror mode, with the mech drawn from the pack, the GPU's palette indices
/// (what the hashes, the fog test and the comparison read) are still the software renderer's, frame after frame.
/// </summary>
TEST_CASE_ISOLATED("game: HD art keeps the original's palette indices")
{
    const std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::Mirror);

    if (!scene.has_value())
    {
        return;
    }

    constexpr int32_t Frames = 10;
    std::optional<MCHdArtOn> hdArt(std::in_place, scene->Pack);
    scene->Renderer->ResetMirror();
    const int64_t drawnBefore = scene->Renderer->HdDrawn();

    for (int32_t frame = 0; frame < Frames; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    hdArt.reset();
    const MCVulkanRenderer::MirrorTally& tally = scene->Renderer->Mirror();
    CHECK(scene->Renderer->HdDrawn() > drawnBefore);
    CHECK_EQ(tally.Frames, Frames);
    CHECK_EQ(tally.DifferentFrames, 0);

    if (tally.DifferentFrames != 0)
    {
        std::cout << std::format("  first difference, frame {}: {}\n", tally.FirstFrame, tally.First);
    }

    std::filesystem::remove_all(scene->Scratch);
}

namespace
{
    /// <summary>
    /// The object nearest the player's mech in mission 1 that <paramref name="wanted"/> picks (it has an appearance):
    /// from the movers' part table and from the object lists.
    /// </summary>
    std::shared_ptr<MCGameObject> NearestObject(const std::function<bool(MCBaseObject&)>& wanted)
    {
        const std::shared_ptr<MCMover> player = GetMoverFromPartId(PlayerMechPartId);
        REQUIRE(player != nullptr);
        const MCVector3D from = player->GetPosition();
        std::shared_ptr<MCGameObject> nearest;
        float nearestDistance = std::numeric_limits<float>::max();

        const auto consider = [&](const std::shared_ptr<MCBaseObject>& object)
        {
            if (object == nullptr || object->GetAppearance() == nullptr)
            {
                return;
            }

            const auto game = std::static_pointer_cast<MCGameObject>(object);
            const MCVector3D at = game->GetPosition();
            const float distance = std::hypot(at.X - from.X, at.Y - from.Y);

            if (distance < nearestDistance && wanted(*object))
            {
                nearest = game;
                nearestDistance = distance;
            }
        };

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
        {
            consider(GetMoverFromPartId(partId));
        }

        for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
        {
            for (const std::shared_ptr<MCBaseObject> object : *list)
            {
                consider(object);
            }
        }

        return nearest;
    }

    /// <summary>The object of <paramref name="objectClass"/> nearest the player's mech in mission 1.</summary>
    std::shared_ptr<MCGameObject> NearestObject(MCObjectClass objectClass)
    {
        return NearestObject([objectClass](const MCBaseObject& object) { return object.ObjectClass == objectClass; });
    }

    /// <summary>What the object tests draw from a pack, nearest the player's mech in mission 1.</summary>
    enum class HdSubject : uint8_t
    {
        Vehicle,
        Building,
        /// <summary>An object with translucent frames (drawn through AlphaPal.ini).</summary>
        Translucent,
    };

    /// <summary>The mission a subject is looked for in: mission 1 has no object drawn translucent; mission 2 has cargo containers.</summary>
    int32_t MissionOf(HdSubject subject)
    {
        constexpr int32_t TranslucentMission = 2;
        return subject == HdSubject::Translucent ? TranslucentMission : 1;
    }

    std::shared_ptr<MCGameObject> NearestSubject(HdSubject subject)
    {
        switch (subject)
        {
            case HdSubject::Vehicle:
                return NearestObject(MCObjectClass::GroundVehicle);
            case HdSubject::Building:
                return NearestObject(MCObjectClass::Building);
            case HdSubject::Translucent:
                break;
        }

        // Each appearance's frames looked at once.
        std::map<uint32_t, bool> translucent;
        return NearestObject(
            [&translucent](MCBaseObject& object)
            {
                const std::shared_ptr<MCAppearanceType> type = object.GetAppearance()->GetAppearanceType();

                if (type == nullptr)
                {
                    return false;
                }

                const auto [found, fresh] = translucent.try_emplace(type->AppearanceNum, false);

                if (fresh)
                {
                    found->second = ObjectSequencesOf(type, false).Translucent > 0;
                }

                return found->second;
            });
    }

    /// <summary>
    /// <paramref name="subject"/>'s mission drawn by <paramref name="drawing"/>, its world view shown larger than the
    /// screen, the camera on the subject, and a pack of every packet its appearance draws (the tests' pack: the original's
    /// pixels doubled, their colours inverted).
    /// </summary>
    struct HdObjectScene
    {
        std::shared_ptr<MCVulkanRenderer> Renderer;
        std::shared_ptr<MCGameObject> Object;
        std::filesystem::path Scratch;
        std::filesystem::path Pack;
    };

    std::optional<HdObjectScene> SetUpHdObjectScene(MCGpuDrawing drawing, HdSubject subject)
    {
        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        HdObjectScene scene;
        scene.Renderer = StartScaledUp(drawing, MissionOf(subject));

        if (scene.Renderer == nullptr)
        {
            return std::nullopt;
        }

        // The whole map shown (the cheat), so no fog hides the object.
        RevealAll();
        scene.Object = NearestSubject(subject);
        REQUIRE(scene.Object != nullptr);
        Eye()->ChangeTarget(scene.Object, true);
        MCTestGame::RunFrame(FrameSeconds);
        MCTestGame::RunFrame(FrameSeconds);

        const std::shared_ptr<MCAppearanceType> type = scene.Object->GetAppearance()->GetAppearanceType();
        REQUIRE(type != nullptr);
        const ObjectSequences made = ObjectSequencesOf(type, false);
        REQUIRE(!made.Sequences.empty());
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        scene.Scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_objects";
        std::filesystem::remove_all(scene.Scratch);
        scene.Pack = scene.Scratch / "pack";
        WritePack(scene.Pack, made.Sequences, colors, TestPackScale, Inverted);
        std::cout << std::format("  object class {:#x}, appearance {:#010x}: {} packets in the pack\n",
                                 static_cast<int32_t>(scene.Object->ObjectClass), type->AppearanceNum,
                                 made.Sequences.size());
        return scene;
    }
}

namespace
{
    /// <summary>
    /// With HD art on and a pack holding each of <paramref name="subjects"/>' frames in turn, the GPU shows the object
    /// from the pack in a world view shown larger than the screen: the shown colours change around it, in the middle of
    /// the view, and the frames before HD art went on were alike (so the change is only how the object is drawn).
    /// </summary>
    void CheckShownFromPack(const HdObjectScene& scene)
    {
        // The game paused, so nothing moves between the frames compared (mission 2's animate), and only the middle of
        // the view compared (the tactical map's blips blink). The frames before are drawn with HD art on from a pack
        // with no frames, so they differ from the pack's only where it has frames (not where HD art draws lines and
        // ellipses smooth).
        // (A frame settles once HD art goes on: it reloads the sprite cache.)
        GamePaused = true;
        std::optional<MCHdArtOn> hdArt(std::in_place, EmptyPack("mc_tests_hd_empty_pack"));
        MCTestGame::RunFrame(FrameSeconds);
        MCTestGame::RunFrame(FrameSeconds);
        const Shown original = ReadShown(scene.Scratch);
        const MCRect middle{original.Width / 4, original.Height / 4, original.Width * 3 / 4, original.Height * 3 / 4};
        MCTestGame::RunFrame(FrameSeconds);
        CHECK_EQ(Compare(original, ReadShown(scene.Scratch), middle).Count, 0);

        hdArt.reset();
        hdArt.emplace(scene.Pack);
        const int64_t drawnBefore = scene.Renderer->HdDrawn();
        MCTestGame::RunFrame(FrameSeconds);
        const Shown hd = ReadShown(scene.Scratch);
        hdArt.reset();
        GamePaused = false;

        CHECK(scene.Renderer->HdDrawn() > drawnBefore);
        const Changed changed = Compare(original, hd, middle);
        std::cout << std::format("  {} shown pixels changed of {}x{}, about ({}, {})\n", changed.Count, hd.Width,
                                 hd.Height, changed.X, changed.Y);
        REQUIRE(changed.Count > 0);
        CHECK(std::abs(changed.X - hd.Width / 2) < hd.Width / 4);
        CHECK(std::abs(changed.Y - hd.Height / 2) < hd.Height / 4);
        std::filesystem::remove_all(scene.Scratch);
    }

    void CheckShownFromPack(std::span<const HdSubject> subjects)
    {
        for (const HdSubject subject : subjects)
        {
            const std::optional<HdObjectScene> scene = SetUpHdObjectScene(MCGpuDrawing::On, subject);

            if (!scene.has_value())
            {
                return;
            }

            MCTest::Scope scope(std::format("subject {}", static_cast<int32_t>(subject)));
            CheckShownFromPack(*scene);
        }
    }

    /// <summary>
    /// HD art changes only the colours shown: in mirror mode, with each of <paramref name="subjects"/> drawn from the
    /// pack in turn, the GPU's palette indices are still the software renderer's, frame after frame.
    /// </summary>
    void CheckIndicesKept(const HdObjectScene& scene)
    {
        constexpr int32_t Frames = 10;
        std::optional<MCHdArtOn> hdArt(std::in_place, scene.Pack);
        scene.Renderer->ResetMirror();
        const int64_t drawnBefore = scene.Renderer->HdDrawn();

        for (int32_t frame = 0; frame < Frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        hdArt.reset();
        const MCVulkanRenderer::MirrorTally& tally = scene.Renderer->Mirror();
        CHECK(scene.Renderer->HdDrawn() > drawnBefore);
        CHECK_EQ(tally.Frames, Frames);
        CHECK_EQ(tally.DifferentFrames, 0);

        if (tally.DifferentFrames != 0)
        {
            std::cout << std::format("  first difference, frame {}: {}\n", tally.FirstFrame, tally.First);
        }

        std::filesystem::remove_all(scene.Scratch);
    }

    void CheckIndicesKept(std::span<const HdSubject> subjects)
    {
        for (const HdSubject subject : subjects)
        {
            const std::optional<HdObjectScene> scene = SetUpHdObjectScene(MCGpuDrawing::Mirror, subject);

            if (!scene.has_value())
            {
                return;
            }

            MCTest::Scope scope(std::format("subject {}", static_cast<int32_t>(subject)));
            CheckIndicesKept(*scene);
        }
    }

    /// <summary>The subjects of mission 1, and of mission 2 (mission 1 has no object drawn translucent).</summary>
    constexpr std::array<HdSubject, 2> FirstMissionSubjects = {HdSubject::Vehicle, HdSubject::Building};
    constexpr std::array<HdSubject, 1> SecondMissionSubjects = {HdSubject::Translucent};
}

TEST_CASE_ISOLATED("game: an HD art pack's frames show in a vehicle's and a building's place")
{
    CheckShownFromPack(FirstMissionSubjects);
}

TEST_CASE_ISOLATED("game: an HD art pack's frames show in a translucent object's place")
{
    CheckShownFromPack(SecondMissionSubjects);
}

TEST_CASE_ISOLATED("game: HD art keeps a vehicle's and a building's palette indices")
{
    CheckIndicesKept(FirstMissionSubjects);
}

TEST_CASE_ISOLATED("game: HD art keeps a translucent object's palette indices")
{
    CheckIndicesKept(SecondMissionSubjects);
}

namespace
{
    /// <summary>
    /// A mine's explosion (GameSys's) going off at the player's mech in mission 1, drawn by <paramref name="drawing"/>
    /// in a world view shown larger than the screen, the camera on it, and a pack of every packet its appearance draws,
    /// holding their blended pixels (the tests' pack: the original's pixels doubled, the opaque ones' colours inverted).
    /// </summary>
    std::optional<HdObjectScene> SetUpHdExplosionScene(MCGpuDrawing drawing)
    {
        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        HdObjectScene scene;
        scene.Renderer = StartScaledUp(drawing);

        if (scene.Renderer == nullptr)
        {
            return std::nullopt;
        }

        RevealAll();
        const std::shared_ptr<MCMover> player = GetMoverFromPartId(PlayerMechPartId);
        REQUIRE(player != nullptr);
        MCVector3D at = player->GetPosition();
        CreateExplosion(MineExplosion, at, 0.0f, 0.0f);
        Eye()->ChangeTarget(player, true);
        MCTestGame::RunFrame(FrameSeconds);
        std::shared_ptr<MCExplosion> explosion;

        for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
        {
            for (const std::shared_ptr<MCBaseObject> object : *list)
            {
                if (object != nullptr && object->ObjectClass == MCObjectClass::Explosion)
                {
                    explosion = std::static_pointer_cast<MCExplosion>(object);
                }
            }
        }

        REQUIRE(explosion != nullptr);
        REQUIRE(explosion->Appearance != nullptr);
        const std::shared_ptr<MCAppearanceType> type = explosion->Appearance->GetAppearanceType();
        REQUIRE(type != nullptr);
        const ObjectSequences made = ObjectSequencesOf(type, true);
        REQUIRE(!made.Sequences.empty());
        CHECK(made.Translucent > 0);
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        scene.Object = explosion;
        scene.Scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_effects";
        std::filesystem::remove_all(scene.Scratch);
        scene.Pack = scene.Scratch / "pack";
        WritePack(scene.Pack, made.Sequences, colors, TestPackScale, Inverted);
        std::cout << std::format("  explosion type {}, appearance {:#010x}: {} packets ({} translucent frames)\n",
                                 MineExplosion, type->AppearanceNum, made.Sequences.size(), made.Translucent);
        return scene;
    }
}

TEST_CASE_ISOLATED("game: an HD art pack's frames show in an explosion's place")
{
    if (const std::optional<HdObjectScene> scene = SetUpHdExplosionScene(MCGpuDrawing::On); scene.has_value())
    {
        CheckShownFromPack(*scene);
    }
}

TEST_CASE_ISOLATED("game: HD art keeps an explosion's palette indices")
{
    if (const std::optional<HdObjectScene> scene = SetUpHdExplosionScene(MCGpuDrawing::Mirror); scene.has_value())
    {
        CheckIndicesKept(*scene);
    }
}

namespace
{
    /// <summary>The crater PAKs (data\sprites): the missions name one of them as their CraterFile.</summary>
    constexpr std::array<std::string_view, 2> CraterFiles = {"crater", "feet"};

    /// <summary>
    /// Every shape of the crater PAKs (craters and footprints, a frame a facing) as a pack's sequences
    /// (craters/&lt;file&gt;/&lt;packet&gt;), holding their blended pixels as effects' do; translucent frames counted in
    /// <paramref name="translucent"/>.
    /// </summary>
    std::vector<Sequence> CraterSequences(size_t& translucent)
    {
        std::vector<Sequence> sequences;

        for (const std::string_view file : CraterFiles)
        {
            MCPacketFile pak;

            if (pak.Open(GamePath(SpritePath, file, ".pak")) != 0)
            {
                continue;
            }

            for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
            {
                // The packet's size once it is sought (as the crater manager reads it).
                if (pak.SeekPacket(packet) != 0)
                {
                    continue;
                }

                std::vector<uint8_t> table(static_cast<size_t>(std::max(pak.GetPacketSize(), 0)));

                if (table.empty() || pak.ReadPacket(packet, table) < 0 || !VfxIsShapeTable(table))
                {
                    continue;
                }

                Sequence sequence;
                sequence.Folder =
                    MCHdArt::FolderOf(MCHdArt::CraterShapeKey{std::string(file), static_cast<uint32_t>(packet)});
                sequence.Blended = true;

                for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
                {
                    auto decoded = Decode(table, frame);

                    if (decoded.has_value() && decoded->Translucent)
                    {
                        ++translucent;
                        LeaveOutUndrawn(*decoded, true);
                    }

                    if (!decoded.has_value() || std::ranges::find(decoded->Drawn, 1) == decoded->Drawn.end())
                    {
                        sequence.Frames.emplace_back();
                        continue;
                    }

                    sequence.Frames.push_back(std::move(*decoded));
                }

                if (DrawnFrames(sequence) > 0)
                {
                    sequences.push_back(std::move(sequence));
                }
            }
        }

        return sequences;
    }
}

namespace
{
    /// <summary>Whether <paramref name="name"/> (lower case) is a mech bar icon's shape file: mi##.shp or vi#.shp.</summary>
    bool IsIconShapeFile(std::string_view name)
    {
        constexpr std::string_view Extension = ".shp";
        constexpr size_t Prefix = 2;

        if (!name.ends_with(Extension) || !(name.starts_with("mi") || name.starts_with("vi")))
        {
            return false;
        }

        const std::string_view number = name.substr(Prefix, name.size() - Prefix - Extension.size());
        return !number.empty() && std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; });
    }

    /// <summary>
    /// The mech bar icons' damage shapes (data\art's mi##.shp and vi#.shp in the game's FastFiles, a frame a body part)
    /// as a pack's sequences (icons/&lt;file&gt;), translucent frames (none in the retail game) without the pixels they
    /// blend, counted in <paramref name="translucent"/>.
    /// </summary>
    std::vector<Sequence> IconSequences(size_t& translucent)
    {
        std::vector<Sequence> sequences;
        std::set<std::string> seen;
        std::error_code error;

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(MCFileSystem::GameRoot(), error))
        {
            if (!EndsWithIgnoringCase(entry.path().filename().string(), ".fst"))
            {
                continue;
            }

            auto archive = MCFastFile::Create(entry.path().filename().string());
            REQUIRE(archive.has_value());

            for (int32_t i = 0; i < (*archive)->GetNumFiles(); ++i)
            {
                const MCFileEntry& file = (*archive)->GetEntries()[static_cast<size_t>(i)];
                // The entry's name past its folders (a FastFile's paths use backslashes).
                const std::string_view path = file.GetName();
                std::string name(path.substr(path.find_last_of("\\/") + 1));
                std::ranges::transform(name, name.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::vector<uint8_t> table(static_cast<size_t>(std::max(file.RealSize, 0)));

                // A file in more than one FastFile is taken from the first.
                if (!IsIconShapeFile(name) || !seen.insert(name).second || table.empty() ||
                    (*archive)->ReadEntry(i, table) != file.RealSize || !VfxIsShapeTable(table))
                {
                    continue;
                }

                Sequence sequence;
                sequence.Folder = MCHdArt::FolderOf(MCHdArt::IconShapeKey{name.substr(0, name.find('.'))});

                for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
                {
                    auto decoded = Decode(table, frame);

                    if (decoded.has_value() && decoded->Translucent)
                    {
                        ++translucent;
                        LeaveOutUndrawn(*decoded, false);
                    }

                    if (!decoded.has_value() || std::ranges::find(decoded->Drawn, 1) == decoded->Drawn.end())
                    {
                        sequence.Frames.emplace_back();
                        continue;
                    }

                    sequence.Frames.push_back(std::move(*decoded));
                }

                if (DrawnFrames(sequence) > 0)
                {
                    sequences.push_back(std::move(sequence));
                }
            }
        }

        return sequences;
    }
}

/// <summary>
/// Tool: <c>--hd-extract-icons &lt;folder&gt;</c> writes the mech bar icons' damage shapes (<see cref="IconSequences"/>)
/// as an HD art pack at their own size, for tools/upscale/make_object_pack.py: each frame a PNG pair (sprite/MCHdArt.h),
/// plus <c>objects.json</c> (one "icon" appearance, still, its sequences' frames' bounds) and the shown colours as
/// palette.rgb.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-icons writes the mech bar icons' damage shapes")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-icons");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path pack(*out);
    std::filesystem::create_directories(pack);
    WritePaletteRgb(pack, colors);
    size_t translucent = 0;
    const std::vector<Sequence> sequences = IconSequences(translucent);
    REQUIRE(!sequences.empty());
    WritePack(pack, sequences, colors, 1, [](SDL_Color color) { return color; });
    std::string json = "{\"appearances\": [\n {\"id\": 0, \"kind\": \"icon\", \"animated\": false, \"blended\": false, "
                       "\"delta\": false, \"types\": [\"icon\"], \"sequences\": [";
    size_t frames = 0;

    for (size_t i = 0; i < sequences.size(); ++i)
    {
        json += std::format("{}\n  {{\"folder\": \"{}\", \"frames\": [{}]}}", i == 0 ? "" : ",",
                            sequences[i].Folder.generic_string(), FramesJson(sequences[i]));
        frames += DrawnFrames(sequences[i]);
    }

    std::ofstream(pack / "objects.json") << json << "]}]}\n";
    std::cout << std::format("  {} icon shape files, {} frames ({} translucent)\n", sequences.size(), frames,
                             translucent);
}

namespace
{
    /// <summary>
    /// Mission 1 drawn by <paramref name="drawing"/> in a window shown larger than the screen, its mech bar showing the
    /// lance's damage diagrams, and a pack of every icon's damage shapes (the tests' pack: pixels doubled, colours
    /// inverted).
    /// </summary>
    std::optional<HdObjectScene> SetUpHdIconScene(MCGpuDrawing drawing)
    {
        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        HdObjectScene scene;
        scene.Renderer = StartScaledUp(drawing);

        if (scene.Renderer == nullptr)
        {
            return std::nullopt;
        }

        const std::shared_ptr<MCMover> player = GetMoverFromPartId(PlayerMechPartId);
        REQUIRE(player != nullptr);
        Eye()->ChangeTarget(player, true);
        MCTestGame::RunFrame(FrameSeconds);
        size_t translucent = 0;
        const std::vector<Sequence> sequences = IconSequences(translucent);
        REQUIRE(!sequences.empty());
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        scene.Object = std::static_pointer_cast<MCGameObject>(player);
        scene.Scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_icons";
        std::filesystem::remove_all(scene.Scratch);
        scene.Pack = scene.Scratch / "pack";
        WritePack(scene.Pack, sequences, colors, TestPackScale, Inverted);
        return scene;
    }
}

/// <summary>
/// With HD art on and a pack holding the icons' damage shapes, the mech bar's damage diagrams are drawn from it: the
/// shown colours change, and only in the mech bar (the window's bottom left), the game paused.
/// </summary>
TEST_CASE_ISOLATED("game: an HD art pack's frames show in the mech bar's damage diagrams")
{
    const std::optional<HdObjectScene> scene = SetUpHdIconScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    // The frame before is drawn with HD art on from a pack with no frames, so the two differ only where the pack has
    // frames (not where HD art draws lines and ellipses smooth).
    GamePaused = true;
    std::optional<MCHdArtOn> hdArt(std::in_place, EmptyPack("mc_tests_hd_empty_pack"));
    MCTestGame::RunFrame(FrameSeconds);
    MCTestGame::RunFrame(FrameSeconds);
    const Shown original = ReadShown(scene->Scratch);
    hdArt.reset();
    hdArt.emplace(scene->Pack);
    const int64_t drawnBefore = scene->Renderer->HdDrawn();
    MCTestGame::RunFrame(FrameSeconds);
    const Shown hd = ReadShown(scene->Scratch);
    hdArt.reset();
    GamePaused = false;

    CHECK(scene->Renderer->HdDrawn() > drawnBefore);
    const Changed changed = Compare(original, hd, MCRect{0, 0, hd.Width - 1, hd.Height - 1});
    std::cout << std::format("  {} shown pixels changed, about ({}, {})\n", changed.Count, changed.X, changed.Y);
    REQUIRE(changed.Count > 0);
    CHECK(changed.X < hd.Width / 2);
    CHECK(changed.Y > hd.Height / 2);
    std::filesystem::remove_all(scene->Scratch);
}

TEST_CASE_ISOLATED("game: HD art keeps the mech bar's palette indices")
{
    if (const std::optional<HdObjectScene> scene = SetUpHdIconScene(MCGpuDrawing::Mirror); scene.has_value())
    {
        CheckIndicesKept(*scene);
    }
}

/// <summary>
/// Tool: <c>--hd-extract-craters &lt;folder&gt;</c> writes the craters' and footprints' frames (the crater PAKs'
/// shapes, a frame a facing) as an HD art pack at their own size, for tools/upscale/make_object_pack.py: each frame a PNG
/// pair holding its blended pixels (sprite/MCHdArt.h), plus <c>objects.json</c> (one "crater" appearance, animated and
/// blended, its sequences' frames' bounds) and the shown colours as palette.rgb.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-craters writes the craters' and footprints' frames")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-craters");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path pack(*out);
    std::filesystem::create_directories(pack);
    WritePaletteRgb(pack, colors);
    size_t translucent = 0;
    const std::vector<Sequence> sequences = CraterSequences(translucent);
    REQUIRE(!sequences.empty());
    WritePack(pack, sequences, colors, 1, [](SDL_Color color) { return color; });
    std::string json = "{\"appearances\": [\n {\"id\": 0, \"kind\": \"crater\", \"animated\": true, \"blended\": true, "
                       "\"delta\": false, \"types\": [\"crater\"], \"sequences\": [";
    size_t frames = 0;

    for (size_t i = 0; i < sequences.size(); ++i)
    {
        json += std::format("{}\n  {{\"folder\": \"{}\", \"frames\": [{}]}}", i == 0 ? "" : ",",
                            sequences[i].Folder.generic_string(), FramesJson(sequences[i]));
        frames += DrawnFrames(sequences[i]);
    }

    std::ofstream(pack / "objects.json") << json << "]}]}\n";
    std::cout << std::format("  {} crater and footprint shapes, {} frames ({} translucent)\n", sequences.size(), frames,
                             translucent);
}

namespace
{
    /// <summary>
    /// Mission 1 drawn by <paramref name="drawing"/> in a world view shown larger than the screen, the camera on the
    /// player's mech, every crater and footprint shape placed beside it, and a pack of them (the tests' pack: the
    /// original's pixels doubled, the opaque ones' colours inverted).
    /// </summary>
    std::optional<HdObjectScene> SetUpHdCraterScene(MCGpuDrawing drawing)
    {
        constexpr float Spacing = 12.0f;
        constexpr int32_t Row = 6;

        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        HdObjectScene scene;
        scene.Renderer = StartScaledUp(drawing);

        if (scene.Renderer == nullptr)
        {
            return std::nullopt;
        }

        RevealAll();
        const std::shared_ptr<MCMover> player = GetMoverFromPartId(PlayerMechPartId);
        REQUIRE(player != nullptr);
        Eye()->ChangeTarget(player, true);
        MCCraterManager& craters = CraterManager();

        for (int32_t type = 0; type < craters.ShapeCount(); ++type)
        {
            MCVector3D at = player->GetPosition();
            at.X += Spacing * static_cast<float>(type % Row - Row / 2);
            at.Y += Spacing * static_cast<float>(type / Row);
            craters.Place(type, at, 0);
        }

        MCTestGame::RunFrame(FrameSeconds);
        size_t translucent = 0;
        const std::vector<Sequence> sequences = CraterSequences(translucent);
        REQUIRE(!sequences.empty());
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        scene.Object = std::static_pointer_cast<MCGameObject>(player);
        scene.Scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_craters";
        std::filesystem::remove_all(scene.Scratch);
        scene.Pack = scene.Scratch / "pack";
        WritePack(scene.Pack, sequences, colors, TestPackScale, Inverted);
        std::cout << std::format("  {} crater shapes placed, {} in the pack\n", craters.ShapeCount(), sequences.size());
        return scene;
    }
}

TEST_CASE_ISOLATED("game: an HD art pack's frames show in a crater's place")
{
    if (const std::optional<HdObjectScene> scene = SetUpHdCraterScene(MCGpuDrawing::On); scene.has_value())
    {
        CheckShownFromPack(*scene);
    }
}

TEST_CASE_ISOLATED("game: HD art keeps the craters' palette indices")
{
    if (const std::optional<HdObjectScene> scene = SetUpHdCraterScene(MCGpuDrawing::Mirror); scene.has_value())
    {
        CheckIndicesKept(*scene);
    }
}

namespace
{
    /// <summary>A frame of a VFX shape table: its bounds' top left from the hot spot, its size, and its encoded rows.</summary>
    struct TableFrame
    {
        int32_t XMin = 0;
        int32_t YMin = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        std::span<const uint8_t> Rows;
    };

    /// <summary>A shape's VFX table of <paramref name="frames"/>, as the game's encoder lays one out (vfx/MCVfxShapeScan.cpp).</summary>
    std::vector<uint8_t> FramesTable(std::span<const TableFrame> frames)
    {
        std::vector<uint8_t> table = {'1', '.', '1', '0'};
        const auto put = [&table](int32_t value)
        {
            for (int32_t byte = 0; byte < 4; ++byte)
            {
                table.push_back(static_cast<uint8_t>(static_cast<uint32_t>(value) >> (8 * byte)));
            }
        };

        // The directory (each frame's offset, then 0) after the count, then each frame's header and rows.
        constexpr int32_t DirectoryAt = 8;
        constexpr int32_t EntryBytes = 8;
        constexpr int32_t HeaderBytes = 0x18;
        constexpr int BoundsShift = 16;
        const auto count = static_cast<int32_t>(frames.size());
        put(count);
        int32_t offset = DirectoryAt + EntryBytes * count;

        for (const TableFrame& frame : frames)
        {
            put(offset);
            put(0);
            offset += HeaderBytes + static_cast<int32_t>(frame.Rows.size());
        }

        for (const TableFrame& frame : frames)
        {
            put(frame.Width << BoundsShift | frame.Height);
            put(0);
            put(frame.XMin);
            put(frame.YMin);
            put(frame.XMin + frame.Width - 1);
            put(frame.YMin + frame.Height - 1);
            table.insert(table.end(), frame.Rows.begin(), frame.Rows.end());
        }

        return table;
    }

    /// <summary>A shape's VFX table, one frame, as the game's encoder lays one out (vfx/MCVfxShapeScan.cpp).</summary>
    std::vector<uint8_t> OneFrameTable(int32_t width, int32_t height, std::span<const uint8_t> rows)
    {
        return FramesTable(std::array{TableFrame{0, 0, width, height, rows}});
    }
}

namespace
{
    /// <summary>The appearance number the effect tests' pack keeps their frame under (none of the game's).</summary>
    constexpr uint32_t TestEffectAppearance = 0xfffff;

    /// <summary>
    /// An opaque colour (one of the free colours, 10..245) with every channel at 40 or more: an HD colour is corrected
    /// channel by channel by its index's colour.
    /// </summary>
    uint8_t FullColorIndex(std::span<const SDL_Color, 256> colors)
    {
        constexpr int32_t FirstFree = 10;
        constexpr int32_t EndFree = 246;
        constexpr uint8_t Channel = 40;
        const auto free = std::views::iota(FirstFree, EndFree);
        const auto full = std::ranges::find_if(free,
                                               [&](int32_t i)
                                               {
                                                   const SDL_Color& c = colors[static_cast<size_t>(i)];
                                                   return SpecialColor[static_cast<size_t>(i)] == 0 && c.r >= Channel &&
                                                          c.g >= Channel && c.b >= Channel;
                                               });
        REQUIRE(full != free.end());
        return static_cast<uint8_t>(*full);
    }

    /// <summary>
    /// Writes a frame holding its blended pixels (MCHdArt::Frame::Blended) as a PNG pair, <paramref name="stem"/>.png and
    /// _idx.png: <paramref name="width"/> x <paramref name="height"/> original pixels, <paramref name="scale"/> HD pixels
    /// each, every pixel of an original column <paramref name="colorOf"/>'s premultiplied colour and
    /// <paramref name="indexOf"/>'s index (none: a blended pixel).
    /// </summary>
    void WriteBlendedFrame(const std::filesystem::path& stem, int32_t width, int32_t height, int32_t scale,
                           const std::function<SDL_Color(int32_t)>& colorOf,
                           const std::function<std::optional<uint8_t>(int32_t)>& indexOf)
    {
        std::vector<uint8_t> color(static_cast<size_t>(width * scale) * height * scale * BytesPerPixel);
        std::vector<uint8_t> index(color.size());

        for (int32_t y = 0; y < height * scale; ++y)
        {
            for (int32_t x = 0; x < width * scale; ++x)
            {
                const size_t at = (static_cast<size_t>(y) * width * scale + x) * BytesPerPixel;
                const SDL_Color c = colorOf(x / scale);
                const std::optional<uint8_t> original = indexOf(x / scale);
                std::ranges::copy(std::array{c.r, c.g, c.b, c.a}, color.begin() + static_cast<ptrdiff_t>(at));
                std::ranges::copy(std::array<uint8_t, 4>{original.value_or(0), original ? HasIndex : uint8_t{0},
                                                         MCHdArt::HoldsBlended, HasIndex},
                                  index.begin() + static_cast<ptrdiff_t>(at));
            }
        }

        SavePng(std::filesystem::path(stem).concat(".png"), width * scale, height * scale, color);
        SavePng(std::filesystem::path(stem).concat("_idx.png"), width * scale, height * scale, index);
    }
}

/// <summary>
/// An effect's HD frame (one holding its blended pixels) is all that shows of a translucent shape: drawn on the screen
/// by the GPU over mission 1, a shape of colour 0 (nothing), 254 (halves what is under it), 4 (adds its colour) and an
/// opaque colour shows the frame's colours in their places, light added where the frame has colour without coverage,
/// and none of the original's blending.
/// </summary>
TEST_CASE_ISOLATED("game: an effect's HD frame draws its blended pixels and light, none of the original's")
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

    constexpr uint8_t Nothing = 0;
    constexpr uint8_t Halves = 254;
    constexpr uint8_t Adds = 4;
    REQUIRE(SpecialColor[Halves] != 0);
    REQUIRE(SpecialColor[Adds] != 0);

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const uint8_t opaque = FullColorIndex(colors);

    // Each row: one pixel of colour 0 (a translucent shape's mark), then 2 halving, 2 adding and 3 opaque.
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    constexpr uint8_t OnePixel = 3;
    constexpr uint8_t SevenPixels = 15;
    constexpr uint8_t RowEnd = 0;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Height; ++row)
    {
        rows.insert(rows.end(),
                    {OnePixel, Nothing, SevenPixels, Halves, Halves, Adds, Adds, opaque, opaque, opaque, RowEnd});
    }

    const std::vector<uint8_t> table = OneFrameTable(Width, Height, rows);
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    const auto shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);
    REQUIRE(!shape->FrameList().empty());
    REQUIRE(MCAgShapeIsAlpha(shape->FrameList(), 0));

    // The frame: the faint error a generated frame holds where it is empty (drawn: nothing, or stacked copies would add it
    // up into a box), light, black covering what is under it, and the opaque colour at half its brightness, by the
    // original's columns.
    constexpr int32_t Scale = 2;
    constexpr SDL_Color Light = {90, 60, 30, 0};
    // Light loses draw.pshader's floor (its LightFloor, in 8-bit steps), below which an effect's frame holds only error.
    constexpr int32_t LightFloor = 6;
    constexpr SDL_Color Error = {4, 4, 4, 3};
    constexpr SDL_Color Cover = {0, 0, 0, 255};
    const SDL_Color lit = {static_cast<uint8_t>(colors[opaque].r / 2), static_cast<uint8_t>(colors[opaque].g / 2),
                           static_cast<uint8_t>(colors[opaque].b / 2), 255};
    const auto frameColor = [&](int32_t column)
    {
        return column == 0 ? Error : column < 3 ? Light : column < 5 ? Cover : lit;
    };

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_effect_frame";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const MCHdArt::ObjectShapeKey key{"shapes", TestEffectAppearance, 0};
    std::filesystem::create_directories(pack / MCHdArt::FolderOf(key));
    constexpr int32_t FirstOpaque = 5;
    WriteBlendedFrame(pack / MCHdArt::FolderOf(key) / "0", Width, Height, Scale, frameColor,
                      [&](int32_t column) { return column >= FirstOpaque ? std::optional(opaque) : std::nullopt; });
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCTestGame::RunFrame(FrameSeconds);
    const auto before = display->ReadFrame();
    REQUIRE(before.has_value());
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCHdArt::Attach(shape, key);
    const int64_t drawnBefore = renderer->HdDrawn();
    AGShapeDraw(pane, shape->FrameList(), 0, x, y);
    const auto after = display->ReadFrame();
    REQUIRE(after.has_value());
    CHECK(renderer->HdDrawn() > drawnBefore);
    hdArt.reset();

    const int32_t width = display->Width();
    const auto shown = [&](const std::vector<SDL_Color>& frame, int32_t column, int32_t row)
    { return frame[static_cast<size_t>(y + row + pane.Y0) * width + x + column + pane.X0]; };
    const auto near = [](int32_t actual, int32_t expected) { return std::abs(actual - expected) <= 2; };
    const auto sameAs = [&](SDL_Color actual, int32_t r, int32_t g, int32_t b)
    { return near(actual.r, r) && near(actual.g, g) && near(actual.b, b); };

    for (int32_t row = 0; row < Height; ++row)
    {
        for (int32_t column = 0; column < Width; ++column)
        {
            const SDL_Color under = shown(*before, column, row);
            const SDL_Color drawn = shown(*after, column, row);
            MCTest::Scope scope(std::format("column {}, row {}: {} {} {} over {} {} {}", column, row, drawn.r, drawn.g,
                                            drawn.b, under.r, under.g, under.b));

            if (column == 0)
            {
                CHECK(sameAs(drawn, under.r, under.g, under.b));
            }
            else if (column < 3)
            {
                CHECK(sameAs(drawn, std::min(under.r + Light.r - LightFloor, 255),
                             std::min(under.g + Light.g - LightFloor, 255),
                             std::min(under.b + Light.b - LightFloor, 255)));
            }
            else if (column < 5)
            {
                CHECK(sameAs(drawn, Cover.r, Cover.g, Cover.b));
            }
            else
            {
                CHECK(sameAs(drawn, lit.r, lit.g, lit.b));
            }
        }
    }

    std::filesystem::remove_all(scratch);
}

/// <summary>
/// An object's baked shadow (MCHdArt::MarkBakedShadows) is its black outside its art's outline, in groups more against
/// empty pixels than drawn ones: a block of black beside the body is marked, a black pixel inside the body is not, and
/// a frame holding its blended pixels (an effect's) keeps no marks.
/// </summary>
TEST_CASE("hd art: an object's baked shadow is its black outside its outline, not the black inside it")
{
    // 10 x 6: the body in columns 0-4 (black at 2, 2 inside it), its shadow in columns 5-8 of rows 3-5.
    constexpr int32_t Width = 10;
    constexpr int32_t Height = 6;
    constexpr uint8_t BodyIndex = 50;
    constexpr int32_t BodyColumns = 5;
    constexpr std::pair<int32_t, int32_t> Inside = {2, 2};
    const auto shadowAt = [](int32_t x, int32_t y) { return x >= BodyColumns && x < Width - 1 && y >= 3; };
    MCHdArt::Frame frame;
    frame.Width = Width;
    frame.Height = Height;
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, 0);

    for (int32_t y = 0; y < Height; ++y)
    {
        for (int32_t x = 0; x < Width; ++x)
        {
            const size_t at = (static_cast<size_t>(y) * Width + x) * MCHdArt::IndexBytes;
            const bool body = x < BodyColumns;

            if (body || shadowAt(x, y))
            {
                frame.Indices[at] = body && std::pair{x, y} != Inside ? BodyIndex : MCHdArt::BakedShadowIndex;
                frame.Indices[at + 1] = HasIndex;
            }
        }
    }

    MCHdArt::Frame blended = frame;
    blended.Blended = true;
    MCHdArt::MarkBakedShadows(frame);
    MCHdArt::MarkBakedShadows(blended);
    const auto green = [](const MCHdArt::Frame& marked, int32_t x, int32_t y)
    { return marked.Indices[(static_cast<size_t>(y) * Width + x) * MCHdArt::IndexBytes + 1]; };

    for (int32_t y = 0; y < Height; ++y)
    {
        for (int32_t x = 0; x < Width; ++x)
        {
            MCTest::Scope scope(std::format("pixel {}, {}", x, y));
            const uint8_t expected = shadowAt(x, y) ? MCHdArt::BakedShadow : x < BodyColumns ? HasIndex : uint8_t{0};
            CHECK_EQ(green(frame, x, y), expected);
            CHECK_EQ(green(blended, x, y), x < BodyColumns || shadowAt(x, y) ? HasIndex : uint8_t{0});
        }
    }
}

namespace
{
    /// <summary>
    /// Writes an object's frame (not holding its blended pixels) as a PNG pair, <paramref name="stem"/>.png and _idx.png:
    /// <paramref name="width"/> x <paramref name="height"/> original pixels, <paramref name="scale"/> HD pixels each,
    /// opaque where <paramref name="indexAt"/> gives an index, in that index's colour of <paramref name="colors"/>.
    /// </summary>
    void WriteObjectFrame(const std::filesystem::path& stem, int32_t width, int32_t height, int32_t scale,
                          std::span<const SDL_Color, 256> colors,
                          const std::function<std::optional<uint8_t>(int32_t, int32_t)>& indexAt)
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(width * scale) * height * scale * BytesPerPixel);
        std::vector<uint8_t> index(pixels.size());

        for (int32_t y = 0; y < height * scale; ++y)
        {
            for (int32_t x = 0; x < width * scale; ++x)
            {
                const size_t at = (static_cast<size_t>(y) * width * scale + x) * BytesPerPixel;
                const std::optional<uint8_t> original = indexAt(x / scale, y / scale);
                const SDL_Color c = colors[original.value_or(0)];
                const uint8_t alpha = original.has_value() ? HasIndex : uint8_t{0};
                std::ranges::copy(std::array{c.r, c.g, c.b, alpha}, pixels.begin() + static_cast<ptrdiff_t>(at));
                std::ranges::copy(std::array<uint8_t, 4>{original.value_or(0), alpha, 0, HasIndex},
                                  index.begin() + static_cast<ptrdiff_t>(at));
            }
        }

        SavePng(std::filesystem::path(stem).concat(".png"), width * scale, height * scale, pixels);
        SavePng(std::filesystem::path(stem).concat("_idx.png"), width * scale, height * scale, index);
    }
}

/// <summary>
/// Sun shadows (GHdSunShadows), on the screen drawn by the GPU over mission 1, paused: an object's frame from the pack
/// (its body, its baked shadow in black beside it) shows its shadow solid black with them off, and with them on as
/// what lies under it at half its light; its body is the same either way. A part drawn as a unit's sun shadow
/// (MCRenderer::SunShadowScope), with the shadow cast a pixel right and up for every pixel above its foot, darkens the
/// ground where its body's rows land (its baked shadow casts nothing) and leaves its own place as it was. At dusk and
/// night (HD lighting showing them) the baked shadow is hidden, the ground showing through it, and the object casts its
/// own: at dusk farther (MCSceneLights::ShownLighting), at night not at all (no sun).
/// </summary>
TEST_CASE_ISOLATED("game: sun shadows darken the ground under an object's baked shadow, and cast a unit's own")
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

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const uint8_t body = FullColorIndex(colors);

    // 8 x 4, standing 12 to 9 rows above its foot: the body in columns 0-3, its baked shadow in columns 4-7.
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    constexpr int32_t BodyColumns = 4;
    constexpr int32_t Top = -12;
    constexpr uint8_t EightPixels = 2 * Width + 1;
    constexpr uint8_t RowEnd = 0;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Height; ++row)
    {
        rows.push_back(EightPixels);

        for (int32_t column = 0; column < Width; ++column)
        {
            rows.push_back(column < BodyColumns ? body : MCHdArt::BakedShadowIndex);
        }

        rows.push_back(RowEnd);
    }

    const std::vector<uint8_t> table = FramesTable(std::array{TableFrame{0, Top, Width, Height, rows}});
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    const auto shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);
    REQUIRE(!shape->FrameList().empty());

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_sun_shadows";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const MCHdArt::ObjectShapeKey key{"shapes", TestEffectAppearance, 0};
    std::filesystem::create_directories(pack / MCHdArt::FolderOf(key));
    constexpr int32_t Scale = 2;
    WriteObjectFrame(pack / MCHdArt::FolderOf(key) / "0", Width, Height, Scale, colors,
                     [&](int32_t column, int32_t) { return column < BodyColumns ? body : MCHdArt::BakedShadowIndex; });
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const int32_t width = display->Width();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCHdArt::Attach(shape, key);
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    constexpr float Darkness = 0.5f;
    lighting.SunShadow = Darkness;
    lighting.SunShadowRight = 1.0f;
    lighting.SunShadowUp = 1.0f;
    GamePaused = true;

    // The ground, then the shape drawn on it (as a sun shadow inside the scope): as it is (a shape draw) or mirrored
    // about its hot spot (a transform's blit), into the pane or a copy of it ending at window column paneRight.
    const auto drawn =
        [&](int32_t sunShadows, bool cast, bool mirrored = false, std::optional<int32_t> paneRight = std::nullopt)
    {
        GHdSunShadows = sunShadows;
        MCTestGame::RunFrame(FrameSeconds);
        auto under = display->ReadFrame();
        REQUIRE(under.has_value());
        {
            MCPane cut = pane;
            cut.X1 = paneRight.value_or(pane.X1);
            const MCRenderer::SunShadowScope scope(cast);

            if (mirrored)
            {
                AGShapeTransform(cut, shape->FrameList(), 0, x, y, 1, 1);
            }
            else
            {
                AGShapeDraw(cut, shape->FrameList(), 0, x, y);
            }
        }

        auto over = display->ReadFrame();
        REQUIRE(over.has_value());
        return std::pair{std::move(*under), std::move(*over)};
    };

    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t column, int32_t row)
    { return frame[static_cast<size_t>(y + row + pane.Y0) * width + x + column + pane.X0]; };
    const auto near = [](int32_t actual, int32_t expected) { return std::abs(actual - expected) <= 3; };
    const auto sameAs = [&](SDL_Color actual, float r, float g, float b)
    {
        return near(actual.r, static_cast<int32_t>(std::lround(r))) &&
               near(actual.g, static_cast<int32_t>(std::lround(g))) &&
               near(actual.b, static_cast<int32_t>(std::lround(b)));
    };

    // The inner pixels of the body (columns 1-2) and of its shadow (5-6), rows 1-2 of the frame.
    const auto [offUnder, off] = drawn(0, false);
    const auto [onUnder, on] = drawn(1, false);

    for (int32_t row = Top + 1; row < Top + Height - 1; ++row)
    {
        for (const int32_t column : {1, 2, 5, 6})
        {
            const SDL_Color under = at(onUnder, column, row);
            MCTest::Scope scope(std::format("column {}, row {}", column, row));

            if (column < BodyColumns)
            {
                CHECK(sameAs(at(off, column, row), colors[body].r, colors[body].g, colors[body].b));
                CHECK(sameAs(at(on, column, row), colors[body].r, colors[body].g, colors[body].b));
            }
            else
            {
                CHECK(sameAs(at(off, column, row), 0.0f, 0.0f, 0.0f));
                CHECK(sameAs(at(on, column, row), under.r * (1.0f - Darkness), under.g * (1.0f - Darkness),
                             under.b * (1.0f - Darkness)));
            }
        }
    }

    // Cast: a row h above the foot lands h pixels right of where it stands (column c of a mirrored frame stands at -c);
    // its own place keeps the ground. In a pane ending at the frame's last column (the cast lands wholly beyond it), the
    // ground beyond is untouched.
    for (const bool mirrored : {false, true})
    {
        const int32_t lastColumn = mirrored ? 0 : Width - 1;

        for (const bool cut : {false, true})
        {
            const auto [castUnder, cast] =
                drawn(1, true, mirrored, cut ? std::optional(pane.X0 + x + lastColumn) : std::nullopt);

            for (int32_t row = Top + 1; row < Top + Height - 1; ++row)
            {
                for (const int32_t column : {1, 2, 5, 6})
                {
                    const int32_t stands = mirrored ? -column : column;
                    const int32_t right = stands - row;
                    REQUIRE(right > lastColumn);
                    const SDL_Color under = at(castUnder, right, row);
                    MCTest::Scope scope(std::format("{}{}, column {}, row {}: cast to column {}",
                                                    mirrored ? "mirrored" : "as is", cut ? ", cut" : "", column, row,
                                                    right));
                    const float kept = cut || column >= BodyColumns ? 1.0f : 1.0f - Darkness;
                    CHECK(sameAs(at(cast, right, row), under.r * kept, under.g * kept, under.b * kept));
                    const SDL_Color own = at(castUnder, stands, row);
                    CHECK(sameAs(at(cast, stands, row), own.r, own.g, own.b));
                }
            }
        }
    }

    // Dusk and night: where the frame's middle row casts the body's and the baked shadow's middles (the day's way when
    // the sun is down), the window pixel holding the place.
    const int lightingWas = GHdLighting;
    constexpr float MiddleRow = Top + Height / 2;
    const auto sameColor = [&](SDL_Color actual, SDL_Color expected)
    { return sameAs(actual, expected.r, expected.g, expected.b); };
    const auto luma = [](SDL_Color color) { return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b; };

    for (const int shown : {HdLightingDusk, HdLightingNight})
    {
        GHdLighting = shown;
        const MCSceneLights::MCLighting sun = MCSceneLights::ShownLighting();
        const bool sunUp = sun.SunShadowUp > 0.0f;
        const float right = sunUp ? sun.SunShadowRight : lighting.SunShadowRight;
        const float up = sunUp ? sun.SunShadowUp : lighting.SunShadowUp;
        MCTest::Scope timeScope(shown == HdLightingDusk ? "dusk" : "night");
        CHECK(MCSceneLights::RecastsSunShadows());
        CHECK_EQ(sunUp, shown == HdLightingDusk);

        if (sunUp)
        {
            CHECK(right > lighting.SunShadowRight);
            CHECK(up > lighting.SunShadowUp);
        }

        const auto [hiddenUnder, hidden] = drawn(1, false);

        for (int32_t row = Top + 1; row < Top + Height - 1; ++row)
        {
            for (const int32_t column : {1, 2, 5, 6})
            {
                MCTest::Scope placeScope(std::format("column {}, row {}", column, row));
                const bool same = sameColor(at(hidden, column, row), at(hiddenUnder, column, row));
                CHECK(same == (column >= BodyColumns));
            }
        }

        const auto [castUnder, cast] = drawn(1, true);

        for (const float column : {BodyColumns / 2.0f, BodyColumns + BodyColumns / 2.0f})
        {
            const float above = -MiddleRow;
            const auto landX = static_cast<int32_t>(std::floor(column + right * above));
            const auto landY = static_cast<int32_t>(std::floor(-up * above));
            const SDL_Color under = at(castUnder, landX, landY);
            const SDL_Color landed = at(cast, landX, landY);
            MCTest::Scope placeScope(std::format("column {} cast to {}, {}", column, landX, landY));

            if (sunUp && column < BodyColumns)
            {
                CHECK(luma(landed) < luma(under) * (1.0f - Darkness / 2.0f));
            }
            else
            {
                CHECK(sameColor(landed, under));
            }
        }
    }

    GHdLighting = lightingWas;
    GamePaused = false;
    GHdSunShadows = 0;
    lighting = kept;
    hdArt.reset();
    std::filesystem::remove_all(scratch);
}

namespace
{
    /// <summary>The share of each component a picture's pixels show of another's (1 where they are the same).</summary>
    struct ShownShare
    {
        float Red = 1.0f;
        float Green = 1.0f;
        float Blue = 1.0f;
        float Luma = 1.0f;
    };

    /// <summary>A pixel's brightness (Rec. 601), 0 to 255.</summary>
    float LumaOf(SDL_Color color)
    {
        return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
    }
}

/// <summary>
/// Cloud shadows (GHdClouds), on the screen drawn by the GPU over mission 1, paused, where the world shows clear of the
/// interface (right of its panels, below the paused message): with them on, part of the world darkens by their darkness
/// (its blue less, the sky's light) and the rest shows as drawn, none of it darker or brighter; the same game time shows
/// the same clouds. As the game's time goes on they drift with the wind at their speed: the darkening a while later is
/// the earlier one moved that far along the wind's way on the ground.
/// </summary>
TEST_CASE_ISOLATED("game: cloud shadows darken part of the world, drifting with the wind as the game's time goes")
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
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    constexpr float Darkness = 0.5f;
    constexpr float Speed = 5.0f;
    // A while: the clouds drift Speed * Later meters, about as many pixels.
    constexpr float Later = 6.0f;
    lighting.Clouds = Darkness;
    lighting.CloudCover = 0.5f;
    lighting.CloudSize = 150.0f;
    lighting.CloudSpeed = Speed;
    const float now = ScenarioTime;
    GamePaused = true;

    const auto shown = [&](int32_t clouds, float time)
    {
        GHdClouds = clouds;
        ScenarioTime = time;
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    const std::vector<SDL_Color> clear = shown(0, now);
    const std::vector<SDL_Color> clouded = shown(1, now);
    const std::vector<SDL_Color> again = shown(1, now);
    const std::vector<SDL_Color> drifted = shown(1, now + Later);
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    const float distance = Speed * Later;
    const MCVector2D from = ProjectToScreen(MCVector3D(0.0f, 0.0f, 0.0f), *camera);
    const MCVector2D to = ProjectToScreen(
        MCVector3D(MCSceneLights::WindEast * distance, MCSceneLights::WindNorth * distance, 0.0f), *camera);
    GamePaused = false;
    GHdClouds = 0;
    ScenarioTime = now;
    lighting = kept;

    // The world clear of the interface: the panels take the screen's left third (the minimap's blips blink), the paused
    // message its top quarter's middle.
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    const auto inWorld = [&](size_t i)
    { return static_cast<int32_t>(i % width) >= left && static_cast<int32_t>(i / width) >= top; };
    // A pixel's share of the clear one's components, where it isn't too dark to measure (none otherwise).
    constexpr uint8_t MinComponent = 24;
    const auto share = [&](const std::vector<SDL_Color>& frame, size_t i) -> std::optional<ShownShare>
    {
        const SDL_Color under = clear[i];
        const SDL_Color over = frame[i];

        if (!inWorld(i) || std::min({under.r, under.g, under.b}) < MinComponent)
        {
            return std::nullopt;
        }

        return ShownShare{static_cast<float>(over.r) / under.r, static_cast<float>(over.g) / under.g,
                          static_cast<float>(over.b) / under.b, LumaOf(over) / LumaOf(under)};
    };

    // Every pixel between as drawn and its darkness (a component's rounding either way); the darkest have taken it
    // all, the blue less.
    constexpr int32_t Rounding = 2;
    constexpr float BlueKept = 0.2f;
    const auto atLeast = [](int32_t actual, float expected) { return actual >= std::lround(expected) - Rounding; };
    size_t measured = 0;
    size_t covered = 0;
    size_t open = 0;
    double redCovered = 0.0;
    double blueCovered = 0.0;
    size_t outside = 0;
    size_t changed = 0;

    for (size_t i = 0; i < clear.size(); ++i)
    {
        if (!inWorld(i))
        {
            continue;
        }

        const SDL_Color under = clear[i];
        const SDL_Color over = clouded[i];
        const bool between = over.r <= under.r + Rounding && over.g <= under.g + Rounding &&
                             over.b <= under.b + Rounding && atLeast(over.r, under.r * (1.0f - Darkness)) &&
                             atLeast(over.g, under.g * (1.0f - Darkness)) &&
                             atLeast(over.b, under.b * (1.0f - Darkness * (1.0f - BlueKept)));
        outside += between ? 0 : 1;
        changed += SameColor(over, again[i]) ? 0 : 1;

        if (const std::optional<ShownShare> part = share(clouded, i); part.has_value())
        {
            ++measured;

            if (part->Luma < 1.0f - 0.9f * Darkness)
            {
                ++covered;
                redCovered += part->Red;
                blueCovered += part->Blue;
            }
            else if (part->Luma > 0.98f)
            {
                ++open;
            }
        }
    }

    REQUIRE(measured > static_cast<size_t>(width - left) * (height - top) / 4);
    const double coveredShare = static_cast<double>(covered) / measured;
    const double openShare = static_cast<double>(open) / measured;
    std::cout << std::format("  under the clouds: {:.0f}% of the world, {:.0f}% in the open, {} pixels out of bounds\n",
                             100.0 * coveredShare, 100.0 * openShare, outside);
    CHECK(changed == 0);
    CHECK(outside == 0);
    CHECK(coveredShare > 0.1);
    CHECK(openShare > 0.1);
    REQUIRE(covered > 0);
    CHECK(blueCovered / covered - redCovered / covered > 0.5 * Darkness * BlueKept);

    // The drift: the shift of the later darkening that matches the earlier best, searched within twice the expected
    // one's reach (on every Sample-th pixel), lies near it.
    const float expectedX = to.X - from.X;
    const float expectedY = to.Y - from.Y;
    const int32_t reach =
        static_cast<int32_t>(std::ceil(2.0f * std::max(std::abs(expectedX), std::abs(expectedY)))) + 8;
    REQUIRE(2 * reach < std::min(width - left, height - top));
    constexpr int32_t Sample = 5;
    constexpr float Unmeasured = -1.0f;
    std::vector<float> earlier(clear.size(), Unmeasured);
    std::vector<float> later(clear.size(), Unmeasured);

    for (size_t i = 0; i < clear.size(); ++i)
    {
        const auto luma = [](const ShownShare& part) { return part.Luma; };
        earlier[i] = share(clouded, i).transform(luma).value_or(Unmeasured);
        later[i] = share(drifted, i).transform(luma).value_or(Unmeasured);
    }

    const auto mismatch = [&](int32_t dx, int32_t dy)
    {
        double sum = 0.0;
        size_t count = 0;

        for (int32_t y = top + reach; y < height - reach; y += Sample)
        {
            for (int32_t x = left + reach; x < width - reach; x += Sample)
            {
                const float a = earlier[static_cast<size_t>(y) * width + x];
                const float b = later[static_cast<size_t>(y + dy) * width + x + dx];

                if (a != Unmeasured && b != Unmeasured)
                {
                    sum += std::abs(a - b);
                    ++count;
                }
            }
        }

        return count > 0 ? sum / count : std::numeric_limits<double>::max();
    };

    double best = std::numeric_limits<double>::max();
    int32_t bestX = 0;
    int32_t bestY = 0;

    for (int32_t dy = -reach; dy <= reach; ++dy)
    {
        for (int32_t dx = -reach; dx <= reach; ++dx)
        {
            if (const double off = mismatch(dx, dy); off < best)
            {
                best = off;
                bestX = dx;
                bestY = dy;
            }
        }
    }

    const double still = mismatch(0, 0);
    std::cout << std::format("  drifted ({}, {}) pixels in {:.0f} s, expected ({:.1f}, {:.1f}); mismatch {:.3f} there, "
                             "{:.3f} unmoved\n",
                             bestX, bestY, Later, expectedX, expectedY, best, still);
    CHECK(std::hypot(expectedX, expectedY) > 10.0f);
    CHECK(std::hypot(bestX - expectedX, bestY - expectedY) < 0.2f * std::hypot(expectedX, expectedY) + 3.0f);
    CHECK(best < 0.5 * still);
}

namespace
{
    /// <summary>Saves a frame <c>ReadFrame</c> read as <paramref name="name"/>.png in the <c>--shots</c> folder, if any.</summary>
    void SaveShownFrame(const std::vector<SDL_Color>& frame, int32_t width, int32_t height, std::string_view name)
    {
        const std::optional<std::string_view> shots = MCTest::Option("shots");

        if (!shots.has_value())
        {
            return;
        }

        std::vector<uint8_t> rgba;
        rgba.reserve(frame.size() * BytesPerPixel);

        for (const SDL_Color& color : frame)
        {
            rgba.insert(rgba.end(), {color.r, color.g, color.b, 255});
        }

        std::filesystem::create_directories(std::filesystem::path(*shots));
        SavePng(std::filesystem::path(*shots) / std::format("{}.png", name), width, height, rgba);
    }
}

/// <summary>
/// A mission's time of day (port-only): its scenario file's [Environment] TimeOfDay, named in any case; day without the
/// block or the key, and for a name that isn't a time of day. HD lighting ON shows the mission's own, DUSK and NIGHT
/// their own whatever the mission's, and off shows day (as drawn); dusk dims the world's light, night more and blue, its
/// colours paler and the lights stronger.
/// </summary>
TEST_CASE("hd lighting: a mission's time of day is its scenario file's, shown as HD LIGHTING says")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    const auto read = [&](std::string_view name, std::string_view environment)
    {
        const std::string path = GamePath(MissionPath, name, ".fit");
        files.AddFile(path, std::format("FITini\r\n[Music]\r\nuc scenarioTuneNum = 0\r\n{}FITend\r\n", environment));
        MCFitIniFile file;
        REQUIRE_EQ(file.Open(path), 0);
        return MCSceneLights::ReadTimeOfDay(file);
    };

    CHECK(read("plain", "") == MCTimeOfDay::Day);
    CHECK(read("bare", "[Environment]\r\n") == MCTimeOfDay::Day);
    CHECK(read("day", "[Environment]\r\nst TimeOfDay = \"Day\"\r\n") == MCTimeOfDay::Day);
    CHECK(read("dusk", "[Environment]\r\nst TimeOfDay = \"Dusk\"\r\n") == MCTimeOfDay::Dusk);
    CHECK(read("night", "[Environment]\r\nst TimeOfDay = \"NIGHT\"\r\n") == MCTimeOfDay::Night);
    CHECK(read("noon", "[Environment]\r\nst TimeOfDay = \"Noon\"\r\n") == MCTimeOfDay::Day);

    const int lightingWas = GHdLighting;
    const MCTimeOfDay missionWas = MCSceneLights::MissionTime();
    const auto shownAt = [](int lighting, MCTimeOfDay mission)
    {
        GHdLighting = lighting;
        MCSceneLights::SetMissionTime(mission);
        return MCSceneLights::TimeOfDay();
    };

    CHECK(shownAt(HdLightingOff, MCTimeOfDay::Night) == MCTimeOfDay::Day);
    CHECK(shownAt(HdLightingOn, MCTimeOfDay::Day) == MCTimeOfDay::Day);
    CHECK(shownAt(HdLightingOn, MCTimeOfDay::Dusk) == MCTimeOfDay::Dusk);
    CHECK(shownAt(HdLightingOn, MCTimeOfDay::Night) == MCTimeOfDay::Night);
    CHECK(shownAt(HdLightingDusk, MCTimeOfDay::Night) == MCTimeOfDay::Dusk);
    CHECK(shownAt(HdLightingNight, MCTimeOfDay::Day) == MCTimeOfDay::Night);

    shownAt(HdLightingOn, MCTimeOfDay::Day);
    const MCSceneLights::MCLighting day = MCSceneLights::ShownLighting();
    shownAt(HdLightingOn, MCTimeOfDay::Dusk);
    const MCSceneLights::MCLighting dusk = MCSceneLights::ShownLighting();
    shownAt(HdLightingOn, MCTimeOfDay::Night);
    const MCSceneLights::MCLighting night = MCSceneLights::ShownLighting();
    GHdLighting = lightingWas;
    MCSceneLights::SetMissionTime(missionWas);

    CHECK(dusk.Ambient < day.Ambient);
    CHECK(night.Ambient < dusk.Ambient);
    CHECK(dusk.Strength > day.Strength);
    CHECK(night.Strength > dusk.Strength);
    CHECK(night.AmbientTint[2] > night.AmbientTint[0]);
    CHECK(night.AmbientGrey > 0.0f);
    CHECK(dusk.AmbientGrey == 0.0f);
}

/// <summary>
/// A mission's time of day (port-only), on the screen drawn by the GPU over mission 1, paused, where the world shows
/// clear of the interface: the original's missions load by day. With HD lighting off a mission set at night shows as
/// drawn; on, one set at dusk shows its world darker than by day, one set at night darker still (its unlit ground, the
/// median) and bluer, and HD LIGHTING's DUSK shows a night mission just as a dusk one. At night, and only then, the
/// units in view shine headlights: a spotlight each, which brighten the world; shut down, they shine none.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a mission set at dusk or night shows its world darker, at night bluer, its units' headlights on")
{
    // A mover's status shut down (MCMover::ShutDown).
    constexpr int32_t MechShutDown = 4;

    if (!MCTestGame::Available())
    {
        return;
    }

    MCSceneLights::SetMissionTime(MCTimeOfDay::Night);
    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(1));
    CHECK(MCSceneLights::MissionTime() == MCTimeOfDay::Day);

    if (MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        return;
    }

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    GamePaused = true;

    // The spotlights among the lights the frame last showed.
    size_t spots = 0;
    const auto shown = [&](int lighting, MCTimeOfDay time)
    {
        GHdLighting = lighting;
        MCSceneLights::SetMissionTime(time);
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
        REQUIRE(camera != nullptr);
        const std::shared_ptr<MCWindow> world = camera->View()->SharedWorldFrame()->Window.lock();
        REQUIRE(world != nullptr);
        spots = static_cast<size_t>(std::ranges::count_if(MCSceneLights::Shown(*world), [](const MCSceneLight& light)
                                                          { return light.SpotCos > 0.0f; }));
        return std::move(*frame);
    };

    const std::vector<SDL_Color> drawn = shown(HdLightingOff, MCTimeOfDay::Day);
    const std::vector<SDL_Color> unlitNight = shown(HdLightingOff, MCTimeOfDay::Night);
    const std::vector<SDL_Color> day = shown(HdLightingOn, MCTimeOfDay::Day);
    const size_t daySpots = spots;
    const std::vector<SDL_Color> dusk = shown(HdLightingOn, MCTimeOfDay::Dusk);
    const size_t duskSpots = spots;
    const std::vector<SDL_Color> night = shown(HdLightingOn, MCTimeOfDay::Night);
    const size_t nightSpots = spots;

    // Every mech shut down (MCMover::ShutDown's status), then as it was.
    std::vector<std::pair<std::shared_ptr<MCBaseObject>, int32_t>> statuses;

    for (const std::shared_ptr<MCObjectList>& list : {InnerSphereMechList(), ClanMechList()})
    {
        for (const std::shared_ptr<MCBaseObject>& object : *list)
        {
            auto& mech = static_cast<MCBattleMech&>(*object);
            statuses.emplace_back(object, mech.Status);
            mech.Status = MechShutDown;
        }
    }

    shown(HdLightingOn, MCTimeOfDay::Night);
    const size_t shutDownSpots = spots;

    for (const auto& [object, status] : statuses)
    {
        static_cast<MCBattleMech&>(*object).Status = status;
    }

    const std::vector<SDL_Color> duskChosen = shown(HdLightingDusk, MCTimeOfDay::Night);
    GamePaused = false;
    GHdLighting = HdLightingOff;
    MCSceneLights::SetMissionTime(MCTimeOfDay::Day);
    SaveShownFrame(day, width, height, "time-of-day-day");
    SaveShownFrame(dusk, width, height, "time-of-day-dusk");
    SaveShownFrame(night, width, height, "time-of-day-night");

    // The world clear of the interface (as the cloud shadows' test): right of the panels, below the paused message.
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    struct Measure
    {
        double Luma = 0.0;
        double MedianLuma = 0.0;
        double Red = 0.0;
        double Blue = 0.0;
    };

    const auto measure = [&](const std::vector<SDL_Color>& frame)
    {
        Measure sum;
        std::vector<float> lumas;

        for (int32_t y = top; y < height; ++y)
        {
            for (int32_t x = left; x < width; ++x)
            {
                const SDL_Color color = frame[static_cast<size_t>(y) * width + x];
                lumas.push_back(LumaOf(color));
                sum.Luma += lumas.back();
                sum.Red += color.r;
                sum.Blue += color.b;
            }
        }

        REQUIRE(!lumas.empty());
        const auto middle = lumas.begin() + static_cast<std::ptrdiff_t>(lumas.size() / 2);
        std::ranges::nth_element(lumas, middle);
        const double count = static_cast<double>(lumas.size());
        return Measure{sum.Luma / count, *middle, sum.Red / count, sum.Blue / count};
    };

    // Different pixels in that world (none where two frames show it the same).
    const auto different = [&](const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    {
        size_t count = 0;

        for (int32_t y = top; y < height; ++y)
        {
            for (int32_t x = left; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                count += SameColor(a[i], b[i]) ? 0 : 1;
            }
        }

        return count;
    };

    const Measure byDay = measure(day);
    const Measure atDusk = measure(dusk);
    const Measure atNight = measure(night);
    std::cout << std::format("  the world's brightness: {:.1f} by day, {:.1f} at dusk, {:.1f} at night (median {:.1f}, "
                             "{:.1f}, {:.1f}); blue per red {:.2f} by day, {:.2f} at night; {} headlights at night\n",
                             byDay.Luma, atDusk.Luma, atNight.Luma, byDay.MedianLuma, atDusk.MedianLuma,
                             atNight.MedianLuma, byDay.Blue / byDay.Red, atNight.Blue / atNight.Red, nightSpots);
    CHECK_EQ(different(unlitNight, drawn), size_t{0});
    CHECK_EQ(different(duskChosen, dusk), size_t{0});
    CHECK(atDusk.Luma < 0.8 * byDay.Luma);
    CHECK(atNight.MedianLuma < 0.75 * atDusk.MedianLuma);
    CHECK(atNight.Blue / atNight.Red > 1.2 * (byDay.Blue / byDay.Red));
    CHECK_EQ(daySpots, size_t{0});
    CHECK_EQ(duskSpots, size_t{0});
    CHECK(nightSpots >= 3);
    CHECK_EQ(shutDownSpots, size_t{0});
}

/// <summary>
/// HD particles (GHdParticles), on the screen drawn by the GPU over mission 1, paused, where the world shows clear of the
/// interface: with them off a burst makes nothing. On, a shot's hit on a unit and a big explosion on the ground in the
/// view's middle show there as they're made and nowhere else; as the game's time goes on they fly out from it, and by the
/// time they've all gone the world shows as drawn again. The same bursts at the same times show the same, the particles'
/// dice starting again (MCParticles::Clear), and none of it throws the game's dice. Flecks: breaking off fires' edges (the
/// default), bursts with no explosion's art make none, and a real explosion's do as it burns, past the burst it makes as
/// it's first drawn; falling, a burst throws them.
/// </summary>
TEST_CASE_ISOLATED("game: particles burst where shots land, fly out as the game's time goes, and never touch the game")
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
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    const float now = ScenarioTime;
    GamePaused = true;
    // A frame's time step the particles take (MCParticles' most), and a while they've all gone by.
    constexpr float Step = 0.1f;
    constexpr float AllGone = 9.0f;
    // A hit on a unit (its height above the ground) and a big explosion on the ground (a unit's end), at the view's
    // middle, as big as the battles' (their fires' and their extents' sides).
    constexpr float HitHeight = 30.0f;
    constexpr MCParticles::MCBurstSize HitSize{10.0f, 18.0f};
    constexpr MCParticles::MCBurstSize BigSize{77.0f, 100.0f};
    MCVector3D ground = camera->Position;
    ground.Z = MCTerrain::GetTerrainElevation(ground);
    MCVector3D hit = ground;
    hit.Z += HitHeight;
    const MCVector2D middle = ProjectToScreen(ground, *camera);

    const auto shown = [&](float time)
    {
        ScenarioTime = time;
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    // The times the frames are drawn at: the bursts', then a step at a time until they've all gone.
    std::vector<float> times{now};

    while (times.back() < now + AllGone)
    {
        times.push_back(times.back() + Step);
    }

    constexpr float OutAfter = 0.5f;
    const auto outFrame = static_cast<size_t>(std::lround(OutAfter / Step));

    // The bursts and the frames after them: as they're made, a while on, and once they've all gone.
    struct Run
    {
        std::vector<SDL_Color> Made;
        std::vector<SDL_Color> Out;
        std::vector<SDL_Color> Gone;
        size_t Count = 0;
        size_t Flecks = 0;
        size_t Left = 0;
    };

    const auto run = [&](int32_t particles)
    {
        GHdParticles = particles;
        MCParticles::Clear();
        MCParticles::AddBurst(hit, HitSize, false);
        MCParticles::AddBurst(ground, BigSize, false);
        Run made;

        for (size_t i = 0; i < times.size(); ++i)
        {
            std::vector<SDL_Color> frame = shown(times[i]);

            if (i == 0)
            {
                made.Made = std::move(frame);
                made.Count = MCParticles::Count();
                made.Flecks = MCParticles::FleckCount();
            }
            else if (i == outFrame)
            {
                made.Out = std::move(frame);
            }
            else if (i + 1 == times.size())
            {
                made.Gone = std::move(frame);
            }
        }

        made.Left = MCParticles::Count();
        return made;
    };

    // Off, then on twice; the game paused, nothing else throws its dice.
    const Run clear = run(0);
    const uint32_t dice = MCGameContext::Current().Random().State();
    const Run first = run(1);
    const Run second = run(1);
    CHECK(MCGameContext::Current().Random().State() == dice);

    // Falling flecks: a burst throws them.
    GHdFlecks = static_cast<int>(MCParticles::MCFleckStyle::Falling);
    MCParticles::Clear();
    MCParticles::AddBurst(hit, HitSize, false);
    shown(now);
    const size_t falling = MCParticles::FleckCount();
    GHdFlecks = static_cast<int>(MCParticles::MCFleckStyle::Edge);

    // A real explosion: it bursts once it has been updated and drawn, and flecks break off its fire's edge as it burns.
    constexpr int32_t Burning = 20;
    MCParticles::Clear();
    constexpr int32_t ExplosionType = 0x290;
    MCVector3D at = ground;
    ::CreateExplosion(ExplosionType, at, 0.0f, 0.0f);
    GamePaused = false;
    MCTestGame::RunFrame(FrameSeconds);
    MCTestGame::RunFrame(FrameSeconds);
    const size_t exploded = MCParticles::Count();
    size_t broken = 0;

    for (int32_t frame = 0; frame < Burning; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
        broken = std::max(broken, MCParticles::FleckCount());
    }

    GHdParticles = 0;
    ScenarioTime = now;
    SaveShownFrame(clear.Made, width, height, "particles_clear");
    SaveShownFrame(first.Made, width, height, "particles_made");
    SaveShownFrame(first.Out, width, height, "particles_out");

    // The world clear of the interface (as the cloud shadows' test): the panels take the screen's left third, the
    // paused message its top quarter's middle.
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    REQUIRE(middle.X > static_cast<float>(left));
    REQUIRE(middle.Y > static_cast<float>(top));
    // As they're made, they're all near where they burst (flecks a third of the fire's side from it, smoke and embers
    // about as wide as a tenth of the explosion, over the ground and the hit above it); a while on they've flown
    // further out.
    constexpr float MadeReach = 60.0f;
    constexpr float FlownOut = 10.0f;
    size_t changed = 0;
    float madeReach = 0.0f;
    float outReach = 0.0f;
    size_t leftOver = 0;
    size_t unlike = 0;

    for (int32_t y = top; y < height; ++y)
    {
        for (int32_t x = left; x < width; ++x)
        {
            const size_t i = static_cast<size_t>(y) * width + x;
            const float off = std::hypot(static_cast<float>(x) - middle.X, static_cast<float>(y) - middle.Y);

            if (!SameColor(first.Made[i], clear.Made[i]))
            {
                ++changed;
                madeReach = std::max(madeReach, off);
            }

            if (!SameColor(first.Out[i], clear.Out[i]))
            {
                outReach = std::max(outReach, off);
            }

            leftOver += SameColor(first.Gone[i], clear.Gone[i]) ? 0 : 1;
            unlike += SameColor(first.Made[i], second.Made[i]) && SameColor(first.Out[i], second.Out[i]) ? 0 : 1;
        }
    }

    std::cout << std::format("  {} particles made: {} pixels changed, out to {:.0f}; {:.1f} s on out to {:.0f}; {} "
                             "left once gone; {} unlike the second time; a real explosion made {}, {} flecks broke off "
                             "it at most at once; a falling burst threw {}\n",
                             first.Count, changed, madeReach, OutAfter, outReach, leftOver, unlike, exploded, broken,
                             falling);
    CHECK(clear.Count == 0);
    CHECK(first.Count > 0);
    CHECK(first.Left == 0);
    CHECK(changed > 0);
    CHECK(madeReach < MadeReach + HitHeight);
    CHECK(outReach > madeReach + FlownOut);
    CHECK(leftOver == 0);
    CHECK(second.Count == first.Count);
    CHECK(unlike == 0);
    CHECK(exploded > 0);
    CHECK(first.Flecks == 0);
    CHECK(broken > 0);
    CHECK(falling > 0);
}

/// <summary>
/// The shake (GHdShake), on the screen drawn by the GPU over mission 1, paused, with no particles: a burst that shakes (a
/// large artillery strike's, a building's end) in the view's middle moves the world shown, all of it the same whole
/// pixels, the way the shake says; a big one that doesn't (a unit's end) leaves it be, and once the shake has died away
/// the world shows where it was. Then, the game going on, a large artillery strike called there shakes it as its
/// explosions go off, and a small one doesn't.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a large artillery strike or a building's end shakes the world shown, a unit's end doesn't, and "
    "the shake dies away")
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
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    REQUIRE(camera->View() != nullptr);
    const std::shared_ptr<MCWindow> world = camera->View()->SharedWorldFrame()->Window.lock();
    REQUIRE(world != nullptr);
    const float now = ScenarioTime;
    GamePaused = true;
    GHdShake = 1;
    MCVector3D ground = camera->Position;
    ground.Z = MCTerrain::GetTerrainElevation(ground);

    const auto shown = [&](float time)
    {
        ScenarioTime = time;
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    // A frame with the shake off, then on, at the same time (the shake it began goes on).
    const auto unshaken = [&](float time)
    {
        GHdShake = 0;
        std::vector<SDL_Color> frame = shown(time);
        GHdShake = 1;
        return frame;
    };

    // A unit's end and a strike's explosion, as big as the battles' (their fires' and their extents' sides).
    constexpr MCParticles::MCBurstSize UnitEnd{77.0f, 100.0f};
    constexpr MCParticles::MCBurstSize Strike{90.0f, 100.0f};
    const std::vector<SDL_Color> still = unshaken(now);
    MCParticles::AddBurst(ground, UnitEnd, false);
    const std::vector<SDL_Color> hit = shown(now);
    const MCParticles::MCShake hitShake = MCParticles::ShownShake(*world);
    MCParticles::AddBurst(ground, Strike, true);
    shown(now);
    // A moment on, where the shake moves the world a pixel or more; and a while on, once it has died away.
    constexpr float Moment = 0.02f;
    constexpr float Later = 2.0f;
    const std::vector<SDL_Color> stillMoment = unshaken(now + Moment);
    const std::vector<SDL_Color> shaken = shown(now + Moment);
    const MCParticles::MCShake shake = MCParticles::ShownShake(*world);
    const std::vector<SDL_Color> stillLater = unshaken(now + Later);
    const std::vector<SDL_Color> settled = shown(now + Later);
    const MCParticles::MCShake settledShake = MCParticles::ShownShake(*world);
    ScenarioTime = now;
    GamePaused = false;

    // A strike called on the view's middle, landing as soon as one can (sooner is taken for the type's own time), and
    // how far the world shook by the time its explosions have gone off.
    constexpr int32_t SmallStrikeType = 0;
    constexpr int32_t LargeStrikeType = 1;
    constexpr int32_t LandsSoon = 3;
    constexpr float StrikeLasts = 9.0f;
    const auto strike = [&](int32_t strikeType)
    {
        MCParticles::Clear();
        const std::shared_ptr<MCCommander> commander = HomeCommander();
        commander->SetNumSmallStrikes(commander->NumSmallStrikes + 1);
        commander->SetNumLargeStrikes(commander->NumLargeStrikes + 1);
        CallArtillery(commander->Id, strikeType, ground, LandsSoon, 0);
        float most = 0.0f;

        for (float time = 0.0f; time < StrikeLasts; time += FrameSeconds)
        {
            MCTestGame::RunFrame(FrameSeconds);
            const MCParticles::MCShake shook = MCParticles::ShownShake(*world);
            most = std::max(most, std::hypot(shook.X, shook.Y));
        }

        return most;
    };

    const float largeShook = strike(LargeStrikeType);
    const float smallShook = strike(SmallStrikeType);
    GHdShake = 0;

    // The world clear of the interface, and of the edges a shake shows smeared.
    constexpr int32_t Most = 8;
    const int32_t left = width / 3 + Most;
    const int32_t top = height / 4 + Most;
    const auto mismatch =
        [&](const std::vector<SDL_Color>& frame, const std::vector<SDL_Color>& under, int32_t dx, int32_t dy)
    {
        size_t different = 0;

        for (int32_t y = top; y < height - Most; ++y)
        {
            for (int32_t x = left; x < width - Most; ++x)
            {
                different += SameColor(frame[static_cast<size_t>(y) * width + x],
                                       under[static_cast<size_t>(y - dy) * width + x - dx])
                                 ? 0
                                 : 1;
            }
        }

        return different;
    };

    size_t best = std::numeric_limits<size_t>::max();
    int32_t bestX = 0;
    int32_t bestY = 0;

    for (int32_t dy = -Most; dy <= Most; ++dy)
    {
        for (int32_t dx = -Most; dx <= Most; ++dx)
        {
            if (const size_t off = mismatch(shaken, stillMoment, dx, dy); off < best)
            {
                best = off;
                bestX = dx;
                bestY = dy;
            }
        }
    }

    // The window pixels' size on the screen.
    const float scaleX = static_cast<float>(width) / static_cast<float>(world->XMax + 1);
    const float scaleY = static_cast<float>(height) / static_cast<float>(world->YMax + 1);
    std::cout << std::format("  shaken ({:.2f}, {:.2f}) window pixels: the world moved ({}, {}) screen pixels, {} "
                             "pixels off; a large strike shook it {:.2f}, a small one {:.2f}\n",
                             shake.X, shake.Y, bestX, bestY, best, largeShook, smallShook);
    CHECK(hitShake.X == 0.0f);
    CHECK(hitShake.Y == 0.0f);
    CHECK(mismatch(hit, still, 0, 0) == 0);
    CHECK(std::hypot(shake.X, shake.Y) >= 1.0f);
    CHECK(best == 0);
    CHECK(std::abs(static_cast<float>(bestX) - shake.X * scaleX) <= scaleX);
    CHECK(std::abs(static_cast<float>(bestY) - shake.Y * scaleY) <= scaleY);
    CHECK(settledShake.X == 0.0f);
    CHECK(settledShake.Y == 0.0f);
    CHECK(mismatch(settled, stillLater, 0, 0) == 0);
    CHECK(largeShook >= 1.0f);
    CHECK(smallShook == 0.0f);
}

/// <summary>
/// An effect's frame part of the way to its next (MCRenderer::InBetweenScope) shows the pack's in-between instead, over
/// the two frames' bounds together: on the screen, drawn by the GPU over mission 1, a two-frame shape whose second frame
/// lies two columns right of its first shows its frame alone, then within the scope the in-between's columns where the
/// two frames' are, drawn as it is and mirrored about the hot spot (as the frame is).
/// </summary>
TEST_CASE_ISOLATED("game: an effect's frame part of the way to its next shows the pack's in-between")
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

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const uint8_t opaque = FullColorIndex(colors);

    // Two frames of 4 x 4 opaque pixels, the second 2 columns right of the first: 6 columns together.
    constexpr int32_t Side = 4;
    constexpr int32_t Shift = 2;
    constexpr int32_t Together = Side + Shift;
    constexpr uint8_t FourPixels = 9;
    constexpr uint8_t RowEnd = 0;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Side; ++row)
    {
        rows.insert(rows.end(), {FourPixels, opaque, opaque, opaque, opaque, RowEnd});
    }

    const std::vector<uint8_t> table =
        FramesTable(std::array{TableFrame{0, 0, Side, Side, rows}, TableFrame{Shift, 0, Side, Side, rows}});
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    const auto shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);
    REQUIRE(VfxShapeCount(shape->FrameList()) == 2);

    // The frames red and green; the in-between of the first toward the second blue in its first 3 columns, yellow in
    // the rest, so a mirrored one shows which way it lies.
    constexpr int32_t Scale = 2;
    constexpr int32_t FirstYellow = 3;
    constexpr SDL_Color Red = {200, 0, 0, 255};
    constexpr SDL_Color Green = {0, 200, 0, 255};
    constexpr SDL_Color Blue = {0, 0, 200, 255};
    constexpr SDL_Color Yellow = {200, 200, 0, 255};
    const auto none = [](int32_t) { return std::optional<uint8_t>(); };
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_in_between";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const MCHdArt::ObjectShapeKey key{"shapes", TestEffectAppearance, 0};
    const std::filesystem::path folder = pack / MCHdArt::FolderOf(key);
    std::filesystem::create_directories(folder);
    WriteBlendedFrame(folder / "0", Side, Side, Scale, [&](int32_t) { return Red; }, none);
    WriteBlendedFrame(folder / "1", Side, Side, Scale, [&](int32_t) { return Green; }, none);
    WriteBlendedFrame(
        folder / "0_1", Together, Side, Scale, [&](int32_t column) { return column < FirstYellow ? Blue : Yellow; },
        none);
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const int32_t width = display->Width();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCHdArt::Attach(shape, key);

    // The screen's pixels after drawing frame 0 with its hot spot at (x, y), as it is or mirrored, in the scope of an
    // in-between (none: none), and before.
    const auto drawn = [&](bool mirrored, const std::optional<MCInBetween>& inBetween)
    {
        MCTestGame::RunFrame(FrameSeconds);
        auto before = display->ReadFrame();
        REQUIRE(before.has_value());
        {
            const MCRenderer::InBetweenScope scope(inBetween);

            if (mirrored)
            {
                AGShapeTransform(pane, shape->FrameList(), 0, x, y, 1, 1);
            }
            else
            {
                AGShapeDraw(pane, shape->FrameList(), 0, x, y);
            }
        }

        auto after = display->ReadFrame();
        REQUIRE(after.has_value());
        return std::pair{std::move(*before), std::move(*after)};
    };

    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t column, int32_t row)
    { return frame[static_cast<size_t>(y + row + pane.Y0) * width + x + column + pane.X0]; };
    const auto near = [](int32_t actual, int32_t expected) { return std::abs(actual - expected) <= 2; };
    const auto same = [&](SDL_Color actual, SDL_Color expected)
    { return near(actual.r, expected.r) && near(actual.g, expected.g) && near(actual.b, expected.b); };

    // The hot spot's columns each way the two frames reach: a column of the first frame shows red alone, of the
    // in-between blue or yellow (as it is, column c of the bounds at x + c; mirrored, at x - c).
    for (const bool mirrored : {false, true})
    {
        for (const bool between : {false, true})
        {
            const auto [before, after] = drawn(mirrored, between ? std::optional(MCInBetween{0, 1, 1}) : std::nullopt);

            for (int32_t row = 0; row < Side; ++row)
            {
                for (int32_t column = 0; column < Together; ++column)
                {
                    const int32_t shown = mirrored ? -column : column;
                    const SDL_Color pixel = at(after, shown, row);
                    MCTest::Scope scope(std::format("{}{}, column {}, row {}: {} {} {}",
                                                    mirrored ? "mirrored" : "as is", between ? ", in-between" : "",
                                                    column, row, pixel.r, pixel.g, pixel.b));

                    if (between)
                    {
                        CHECK(same(pixel, column < FirstYellow ? Blue : Yellow));
                    }
                    else
                    {
                        CHECK(same(pixel, column < Side ? Red : at(before, shown, row)));
                    }
                }
            }
        }
    }

    hdArt.reset();
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// An effect's HD frame drawn through a table (XlatAlpha: the haze, a fade) is drawn from the pack, its pixels taking the
/// table's change of colour: through a fade that darkens, its light and its opaque colour show darker than drawn
/// without one.
/// </summary>
TEST_CASE_ISOLATED("game: an effect's HD frame drawn through a table takes the table's change of colour")
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

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const uint8_t opaque = FullColorIndex(colors);

    // A fade that darkens the palette.
    const auto brightness = [&](uint8_t index)
    {
        const SDL_Color& c = colors[index];
        return static_cast<int32_t>(c.r) + c.g + c.b;
    };

    std::optional<MCColorTable> fade;

    for (int32_t table = 0; !fade.has_value(); ++table)
    {
        const std::optional<MCColorTable> candidate = GamePalette().GetFadeTable(table);
        REQUIRE(candidate.has_value());
        int64_t change = 0;

        for (int32_t i = 0; i < 256; ++i)
        {
            change += brightness((*candidate)[static_cast<size_t>(i)]) - brightness(static_cast<uint8_t>(i));
        }

        if (change < 0 && brightness((*candidate)[opaque]) < brightness(opaque))
        {
            fade = candidate;
        }
    }

    // One pixel of colour 0 (a translucent shape's mark), then 2 adding light and 2 opaque, each row.
    constexpr uint8_t Nothing = 0;
    constexpr uint8_t Adds = 4;
    constexpr int32_t Width = 5;
    constexpr int32_t Height = 4;
    constexpr int32_t Scale = 2;
    constexpr int32_t FirstOpaque = 3;
    constexpr uint8_t OnePixel = 3;
    constexpr uint8_t FourPixels = 9;
    constexpr uint8_t RowEnd = 0;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Height; ++row)
    {
        rows.insert(rows.end(), {OnePixel, Nothing, FourPixels, Adds, Adds, opaque, opaque, RowEnd});
    }

    const std::vector<uint8_t> table = OneFrameTable(Width, Height, rows);
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    const auto shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);
    REQUIRE(MCAgShapeIsAlpha(shape->FrameList(), 0));
    constexpr SDL_Color Light = {90, 60, 30, 0};
    const auto frameColor = [&](int32_t column)
    {
        return column == 0 ? SDL_Color{0, 0, 0, 0} : column < FirstOpaque ? Light : colors[opaque];
    };

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_effect_table";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const MCHdArt::ObjectShapeKey key{"shapes", TestEffectAppearance, 0};
    std::filesystem::create_directories(pack / MCHdArt::FolderOf(key));
    WriteBlendedFrame(pack / MCHdArt::FolderOf(key) / "0", Width, Height, Scale, frameColor,
                      [&](int32_t column) { return column >= FirstOpaque ? std::optional(opaque) : std::nullopt; });
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCHdArt::Attach(shape, key);
    GamePaused = true;
    const auto drawn = [&](const std::optional<MCColorTable>& through)
    {
        MCTestGame::RunFrame(FrameSeconds);
        const int64_t drawnBefore = renderer->HdDrawn();

        if (through.has_value())
        {
            AGShapeTranslateDraw(pane, shape->FrameList(), 0, x, y, *through);
        }
        else
        {
            AGShapeDraw(pane, shape->FrameList(), 0, x, y);
        }

        const auto shown = display->ReadFrame();
        REQUIRE(shown.has_value());
        CHECK(renderer->HdDrawn() > drawnBefore);
        return *shown;
    };

    const std::vector<SDL_Color> plain = drawn(std::nullopt);
    const std::vector<SDL_Color> faded = drawn(fade);
    GamePaused = false;
    hdArt.reset();
    const int32_t width = display->Width();
    const auto sum = [&](const std::vector<SDL_Color>& frame, int32_t column)
    {
        const SDL_Color c = frame[static_cast<size_t>(y + 1 + pane.Y0) * width + x + column + pane.X0];
        return static_cast<int32_t>(c.r) + c.g + c.b;
    };

    std::cout << std::format("  light {} -> {}, opaque {} -> {}\n", sum(plain, 1), sum(faded, 1),
                             sum(plain, FirstOpaque), sum(faded, FirstOpaque));
    CHECK(sum(faded, 1) < sum(plain, 1));
    CHECK(sum(faded, FirstOpaque) < sum(plain, FirstOpaque));
    std::filesystem::remove_all(scratch);
}

namespace
{
    /// <summary>The appearance the smooth-edge tests' pack keeps their frame under (none of the game's).</summary>
    constexpr uint32_t TestEdgeAppearance = 0xffffe;
    /// <summary>The rows of the smooth-edge tests' shapes: a staircase, one pixel longer each row.</summary>
    constexpr int32_t StairRows = 24;
    /// <summary>The original rows left out at the staircase's top and bottom, where its corners round.</summary>
    constexpr int32_t StairEnds = 4;
    /// <summary>A shape row's run of n pixels (the VFX encoding: n << 1 | 1, then the pixels), and its end.</summary>
    constexpr uint8_t RunOf(int32_t pixels)
    {
        return static_cast<uint8_t>(pixels << 1 | 1);
    }

    constexpr uint8_t RowEnd = 0;

    /// <summary>
    /// Writes a frame that leaves out the blended pixels (MCHdArt::Frame not Blended) as a PNG pair,
    /// <paramref name="stem"/>.png and _idx.png: <paramref name="width"/> x <paramref name="height"/> original pixels,
    /// <paramref name="scale"/> HD pixels each (nearest, so its alpha keeps the original's steps), the pixels
    /// <paramref name="indexAt"/> gives an index opaque in <paramref name="color"/>, the rest transparent.
    /// </summary>
    void WriteSteppedFrame(const std::filesystem::path& stem, int32_t width, int32_t height, int32_t scale,
                           SDL_Color color, const std::function<std::optional<uint8_t>(int32_t, int32_t)>& indexAt)
    {
        std::vector<uint8_t> colors(static_cast<size_t>(width * scale) * height * scale * BytesPerPixel);
        std::vector<uint8_t> index(colors.size());

        for (int32_t y = 0; y < height * scale; ++y)
        {
            for (int32_t x = 0; x < width * scale; ++x)
            {
                const size_t at = (static_cast<size_t>(y) * width * scale + x) * BytesPerPixel;
                const std::optional<uint8_t> original = indexAt(x / scale, y / scale);
                const uint8_t alpha = original.has_value() ? HasIndex : uint8_t{0};
                std::ranges::copy(std::array{color.r, color.g, color.b, alpha},
                                  colors.begin() + static_cast<ptrdiff_t>(at));
                std::ranges::copy(std::array<uint8_t, 4>{original.value_or(0), alpha, 0, HasIndex},
                                  index.begin() + static_cast<ptrdiff_t>(at));
            }
        }

        SavePng(std::filesystem::path(stem).concat(".png"), width * scale, height * scale, colors);
        SavePng(std::filesystem::path(stem).concat("_idx.png"), width * scale, height * scale, index);
    }

    /// <summary>A free colour (10..245, not a special one) far from <paramref name="from"/>'s.</summary>
    uint8_t ContrastingIndex(std::span<const SDL_Color, 256> colors, uint8_t from)
    {
        constexpr int32_t FirstFree = 10;
        constexpr int32_t EndFree = 246;
        constexpr int32_t Apart = 240;
        const SDL_Color& a = colors[from];
        const auto free = std::views::iota(FirstFree, EndFree);
        const auto found =
            std::ranges::find_if(free,
                                 [&](int32_t i)
                                 {
                                     const SDL_Color& b = colors[static_cast<size_t>(i)];
                                     return SpecialColor[static_cast<size_t>(i)] == 0 &&
                                            std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) >= Apart;
                                 });
        REQUIRE(found != free.end());
        return static_cast<uint8_t>(*found);
    }

    /// <summary>How far a shown pixel's colour lies from another's (the channels' differences summed).</summary>
    int32_t ColourDistance(const uint8_t* a, const uint8_t* b)
    {
        return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
    }

    /// <summary>
    /// What a smooth-edge test saw of a staircase drawn over a plain colour: per shown row of what changed, how many
    /// pixels' worth it covers (each changed pixel's distance from the plain colour over a whole pixel's), and the
    /// scale the screen is shown at.
    /// </summary>
    struct StairCover
    {
        std::vector<double> Rows;
        double Scale = 1.0;
    };

    /// <summary>
    /// The staircase <paramref name="draw"/> draws on the screen pane over a plain colour (index
    /// <paramref name="plain"/>), with HD art on from <paramref name="pack"/> (none: off): per shown row, its cover
    /// (StairCover), a whole pixel being as far from the plain colour as the shown pixel at <paramref name="full"/> of
    /// what changed (fractions of its width and height).
    /// </summary>
    StairCover CoverOfStairs(const std::optional<std::filesystem::path>& pack, uint8_t plain,
                             const std::function<void()>& draw, std::pair<double, double> full,
                             const std::filesystem::path& scratch)
    {
        std::optional<MCHdArtOn> on;

        if (pack.has_value())
        {
            on.emplace(*pack);
        }

        MCTestGame::RunFrame(FrameSeconds);
        VfxPaneWipe(ScreenPort()->Frame(), plain);
        const Shown before = ReadShown(scratch);
        draw();
        const Shown after = ReadShown(scratch);
        REQUIRE_EQ(before.Width, after.Width);
        MCRect box{after.Width, after.Height, -1, -1};

        for (int32_t y = 0; y < after.Height; ++y)
        {
            for (int32_t x = 0; x < after.Width; ++x)
            {
                const size_t at = (static_cast<size_t>(y) * after.Width + x) * BytesPerPixel;

                if (ColourDistance(&before.Pixels[at], &after.Pixels[at]) != 0)
                {
                    box = {std::min(box.X0, x), std::min(box.Y0, y), std::max(box.X1, x), std::max(box.Y1, y)};
                }
            }
        }

        REQUIRE(box.X1 >= box.X0);
        const auto pixel = [&](const Shown& shown, int32_t x, int32_t y)
        { return &shown.Pixels[(static_cast<size_t>(y) * shown.Width + x) * BytesPerPixel]; };
        const int32_t fullX = box.X0 + static_cast<int32_t>(full.first * (box.X1 - box.X0));
        const int32_t fullY = box.Y0 + static_cast<int32_t>(full.second * (box.Y1 - box.Y0));
        const double whole = ColourDistance(pixel(before, fullX, fullY), pixel(after, fullX, fullY));
        REQUIRE(whole > 0.0);
        StairCover cover;
        cover.Scale = static_cast<double>(box.Y1 - box.Y0 + 1) / StairRows;

        for (int32_t y = box.Y0; y <= box.Y1; ++y)
        {
            double row = 0.0;

            for (int32_t x = box.X0; x <= box.X1; ++x)
            {
                row += std::min(ColourDistance(pixel(before, x, y), pixel(after, x, y)) / whole, 1.0);
            }

            cover.Rows.push_back(row);
        }

        return cover;
    }

    /// <summary>
    /// The largest change of cover between neighbouring shown rows, away from the staircase's ends: its edge's slope
    /// (a pixel a row) where it is smooth, about the screen's scale where it keeps the original's steps.
    /// </summary>
    double LargestStep(const StairCover& cover)
    {
        const auto ends = static_cast<size_t>(StairEnds * cover.Scale);
        double largest = 0.0;

        for (size_t row = ends; row + 1 + ends < cover.Rows.size(); ++row)
        {
            largest = std::max(largest, cover.Rows[row + 1] - cover.Rows[row]);
        }

        return largest;
    }

    /// <summary>
    /// A shape of <paramref name="rows"/> (OneFrameTable's) drawn by the smooth-edge tests, its frame in a pack under
    /// TestEdgeAppearance, and the scratch folder.
    /// </summary>
    struct StairShape
    {
        std::shared_ptr<MCShape> Shape;
        std::filesystem::path Scratch;
        std::filesystem::path Pack;
        MCHdArt::ObjectShapeKey Key{"shapes", TestEdgeAppearance, 0};
    };

    StairShape MakeStairShape(std::string_view name, int32_t width, std::span<const uint8_t> rows)
    {
        StairShape stairs;
        const std::vector<uint8_t> table = OneFrameTable(width, StairRows, rows);
        MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
        std::ranges::copy(table, block.Bytes().begin());
        stairs.Shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);
        REQUIRE(!stairs.Shape->FrameList().empty());
        stairs.Scratch = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(stairs.Scratch);
        stairs.Pack = stairs.Scratch / "pack";
        std::filesystem::create_directories(stairs.Pack / MCHdArt::FolderOf(stairs.Key));
        return stairs;
    }
}

/// <summary>
/// An HD frame's edges come from the original's pixels, smoothed: a staircase (a pixel longer each row) drawn on the
/// screen by the GPU over mission 1 (a window shown larger than the screen), its frame's alpha the original's steps
/// scaled up, shows with HD art off a jump of about the screen's scale every few shown rows, and with it on an edge
/// that moves about a pixel a shown row.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws a sprite's HD frame with its original's edges smoothed")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    if (StartScaledUp(MCGpuDrawing::On) == nullptr)
    {
        return;
    }

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const uint8_t opaque = FullColorIndex(colors);
    const uint8_t plain = ContrastingIndex(colors, opaque);
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < StairRows; ++row)
    {
        rows.push_back(RunOf(row + 1));
        rows.insert(rows.end(), static_cast<size_t>(row + 1), opaque);
        rows.push_back(RowEnd);
    }

    const StairShape stairs = MakeStairShape("mc_tests_hd_smooth_frame", StairRows, rows);
    constexpr int32_t Scale = 2;
    WriteSteppedFrame(stairs.Pack / MCHdArt::FolderOf(stairs.Key) / "0", StairRows, StairRows, Scale, colors[opaque],
                      [&](int32_t x, int32_t y) { return x <= y ? std::optional(opaque) : std::nullopt; });
    WritePaletteRgb(stairs.Pack, colors);

    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const auto draw = [&]
    {
        MCHdArt::Attach(stairs.Shape, stairs.Key);
        AGShapeDraw(pane, stairs.Shape->FrameList(), 0, x, y);
    };

    // A whole pixel: the bottom row's left end, inside the staircase.
    constexpr std::pair<double, double> Inside = {0.1, 0.99};
    GamePaused = true;
    const StairCover original = CoverOfStairs(std::nullopt, plain, draw, Inside, stairs.Scratch);
    const StairCover smooth = CoverOfStairs(stairs.Pack, plain, draw, Inside, stairs.Scratch);
    GamePaused = false;
    std::cout << std::format("  largest step between rows (scale {:.2f}): {:.2f} with HD art off, {:.2f} on\n",
                             smooth.Scale, LargestStep(original), LargestStep(smooth));
    CHECK(LargestStep(original) > smooth.Scale * 0.75);
    CHECK(LargestStep(smooth) < smooth.Scale * 0.6);
    std::filesystem::remove_all(stairs.Scratch);
}

/// <summary>
/// A translucent shape's blended pixels that its HD frame leaves out (a shadow) are drawn with smooth edges: a shape
/// whose rows hold two opaque pixels and then a halving staircase (a pixel longer each row), drawn as the frame test's,
/// shows the shadow's edge with HD art off stepping about the screen's scale every few shown rows, and with it on
/// moving about a pixel a shown row.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws a translucent sprite's shadow with smooth edges under its HD frame")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    if (StartScaledUp(MCGpuDrawing::On) == nullptr)
    {
        return;
    }

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const uint8_t opaque = FullColorIndex(colors);
    const uint8_t plain = ContrastingIndex(colors, opaque);
    constexpr uint8_t Mark = 0;
    constexpr uint8_t Halves = 254;
    constexpr int32_t Opaque = 2;
    REQUIRE(SpecialColor[Halves] != 0);
    std::vector<uint8_t> rows;

    // Each row: the translucent shape's mark (a pixel of colour 0), two opaque pixels, then the shadow.
    for (int32_t row = 0; row < StairRows; ++row)
    {
        rows.insert(rows.end(), {RunOf(1), Mark, RunOf(Opaque + row + 1), opaque, opaque});
        rows.insert(rows.end(), static_cast<size_t>(row + 1), Halves);
        rows.push_back(RowEnd);
    }

    constexpr int32_t Width = 1 + Opaque + StairRows;
    const StairShape stairs = MakeStairShape("mc_tests_hd_smooth_shadow", Width, rows);
    REQUIRE(MCAgShapeIsAlpha(stairs.Shape->FrameList(), 0));
    constexpr int32_t Scale = 2;
    WriteSteppedFrame(stairs.Pack / MCHdArt::FolderOf(stairs.Key) / "0", Width, StairRows, Scale, colors[opaque],
                      [&](int32_t x, int32_t) { return x >= 1 && x <= Opaque ? std::optional(opaque) : std::nullopt; });
    WritePaletteRgb(stairs.Pack, colors);

    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const auto draw = [&]
    {
        MCHdArt::Attach(stairs.Shape, stairs.Key);
        AGShapeDraw(pane, stairs.Shape->FrameList(), 0, x, y);
    };

    // A whole pixel of shadow: the bottom row's middle.
    constexpr std::pair<double, double> Inside = {0.6, 0.99};
    GamePaused = true;
    const StairCover original = CoverOfStairs(std::nullopt, plain, draw, Inside, stairs.Scratch);
    const StairCover smooth = CoverOfStairs(stairs.Pack, plain, draw, Inside, stairs.Scratch);
    GamePaused = false;
    std::cout << std::format("  largest step between rows (scale {:.2f}): {:.2f} with HD art off, {:.2f} on\n",
                             smooth.Scale, LargestStep(original), LargestStep(smooth));
    CHECK(LargestStep(original) > smooth.Scale * 0.75);
    CHECK(LargestStep(smooth) < smooth.Scale * 0.6);
    std::filesystem::remove_all(stairs.Scratch);
}

namespace
{
    /// <summary>
    /// With
    /// <c>--hd-lighting &lt;strength&gt;[,&lt;ambient&gt;[,&lt;scatter&gt;,&lt;knee&gt;,&lt;most&gt;,&lt;self-lit&gt;[,&lt;facing&gt;[,&lt;shadow&gt;[,&lt;tint&gt;,&lt;bloom&gt;,&lt;bloom-from&gt;,&lt;haze&gt;[,&lt;glow-ovals&gt;[,&lt;water&gt;]]]]]]]</c>,
    /// turns HD lighting on (sprite/MCSceneLights.h), its lights that strong and the world's own light that bright (1 as
    /// drawn, below 1 dusk), and the tone and the looks as given (MCSceneLights::MCLighting's).
    /// </summary>
    void LightingFromOptions()
    {
        const std::optional<std::string_view> lighting = MCTest::Option("hd-lighting");

        if (!lighting.has_value())
        {
            return;
        }

        MCSceneLights::MCLighting& settings = MCSceneLights::Lighting();
        const std::string given(*lighting);
        REQUIRE(std::sscanf(given.c_str(), "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &settings.Strength,
                            &settings.Ambient, &settings.Scatter, &settings.Knee, &settings.Most, &settings.SelfLit,
                            &settings.Facing, &settings.Shadow, &settings.Tint, &settings.Bloom, &settings.BloomFrom,
                            &settings.Haze, &settings.GlowOvals, &settings.Water) >= 1);
        GHdLighting = 1;
        std::cout << std::format(
            "  HD lighting: strength {:.2f}, the world's own light {:.2f}, scatter {:.2f}, knee {:.2f}, "
            "most {:.2f}, self-lit above {:.2f}, facing {:.1f}, shadow {:.2f}, tint {:.2f}, bloom {:.2f} above {:.2f}, "
            "haze {:.1f}, glow ovals {:.2f}, water {:.2f}\n",
            settings.Strength, settings.Ambient, settings.Scatter, settings.Knee, settings.Most, settings.SelfLit,
            settings.Facing, settings.Shadow, settings.Tint, settings.Bloom, settings.BloomFrom, settings.Haze,
            settings.GlowOvals, settings.Water);
    }

    /// <summary>
    /// With <c>--hd-sun-shadows &lt;darkness&gt;</c>, turns sun shadows on (GHdSunShadows): objects' shadows take that
    /// much of the light under them (MCLighting::SunShadow, 0 to 1).
    /// </summary>
    void SunShadowsFromOptions()
    {
        const std::optional<std::string_view> darkness = MCTest::Option("hd-sun-shadows");

        if (!darkness.has_value())
        {
            return;
        }

        // strtof: libc++ has floating-point from_chars only from macOS 26.
        MCSceneLights::Lighting().SunShadow = std::strtof(std::string(*darkness).c_str(), nullptr);
        GHdSunShadows = 1;
        std::cout << std::format("  sun shadows: {:.2f} of the light under them taken\n",
                                 MCSceneLights::Lighting().SunShadow);
    }

    /// <summary>
    /// With <c>--hd-clouds &lt;darkness&gt;[,&lt;cover&gt;[,&lt;size&gt;[,&lt;speed&gt;]]]</c>, turns cloud shadows on
    /// (GHdClouds): they take that much of the world's own light, over that share of the ground, that big (meters) and
    /// drifting that fast (meters a second); each left out keeps MCSceneLights::MCLighting's.
    /// </summary>
    void CloudsFromOptions()
    {
        const std::optional<std::string_view> clouds = MCTest::Option("hd-clouds");

        if (!clouds.has_value())
        {
            return;
        }

        MCSceneLights::MCLighting& settings = MCSceneLights::Lighting();
        const std::string given(*clouds);
        REQUIRE(std::sscanf(given.c_str(), "%f,%f,%f,%f", &settings.Clouds, &settings.CloudCover, &settings.CloudSize,
                            &settings.CloudSpeed) >= 1);
        GHdClouds = 1;
        std::cout << std::format(
            "  cloud shadows: {:.2f} of the light taken, {:.2f} of the ground, {:.0f} m, {:.1f} m/s\n", settings.Clouds,
            settings.CloudCover, settings.CloudSize, settings.CloudSpeed);
    }

    /// <summary>
    /// With <c>--hd-particles &lt;amount&gt;</c>, turns the particles on (GHdParticles), that many as made
    /// (MCParticles::MCParticleLook::Amount); with <c>--hd-shake &lt;strength&gt;</c>, the shake (GHdShake), that strong;
    /// <c>--hd-flecks edge|falling</c> picks how explosions' flecks fly (GHdFlecks).
    /// </summary>
    void ParticlesFromOptions()
    {
        MCParticles::MCParticleLook& look = MCParticles::Look();

        // strtof: libc++ has floating-point from_chars only from macOS 26.
        if (const std::optional<std::string_view> amount = MCTest::Option("hd-particles"); amount.has_value())
        {
            look.Amount = std::strtof(std::string(*amount).c_str(), nullptr);
            GHdParticles = 1;
            std::cout << std::format("  particles: {:.2f} as many as made\n", look.Amount);
        }

        if (const std::optional<std::string_view> shake = MCTest::Option("hd-shake"); shake.has_value())
        {
            look.Shake = std::strtof(std::string(*shake).c_str(), nullptr);
            GHdShake = 1;
            std::cout << std::format("  shake: {:.2f} as strong as made\n", look.Shake);
        }

        if (const std::optional<std::string_view> flecks = MCTest::Option("hd-flecks"); flecks.has_value())
        {
            const bool falling = *flecks == "falling";
            GHdFlecks =
                static_cast<int>(falling ? MCParticles::MCFleckStyle::Falling : MCParticles::MCFleckStyle::Edge);
            std::cout << std::format("  flecks: {}\n", falling ? "falling" : "breaking off the fire's edge");
        }
    }

    /// <summary>
    /// With <c>--hd-air-beams &lt;strength&gt;</c>, the spotlights' beams show in the air that much
    /// (MCLighting::AirBeams).
    /// </summary>
    void AirBeamsFromOptions()
    {
        const std::optional<std::string_view> strength = MCTest::Option("hd-air-beams");

        if (!strength.has_value())
        {
            return;
        }

        // strtof: libc++ has floating-point from_chars only from macOS 26.
        MCSceneLights::Lighting().AirBeams = std::strtof(std::string(*strength).c_str(), nullptr);
        std::cout << std::format("  beams in the air: {:.2f}\n", MCSceneLights::Lighting().AirBeams);
    }

    /// <summary>
    /// With <c>--hd-sun-relief &lt;strength&gt;</c>, the sun shows the world's relief that much (MCLighting::SunRelief,
    /// 0 to 1).
    /// </summary>
    void SunReliefFromOptions()
    {
        const std::optional<std::string_view> strength = MCTest::Option("hd-sun-relief");

        if (!strength.has_value())
        {
            return;
        }

        MCSceneLights::Lighting().SunRelief = std::strtof(std::string(*strength).c_str(), nullptr);
        std::cout << std::format("  sun relief: {:.2f}\n", MCSceneLights::Lighting().SunRelief);
    }

    /// <summary>
    /// With <c>--hd-time-of-day day|dusk|night</c>, sets the mission at that time of day, as its scenario file's
    /// [Environment] TimeOfDay would (MCSceneLights::SetMissionTime): shown so with HD lighting on.
    /// </summary>
    void TimeOfDayFromOptions()
    {
        const std::optional<std::string_view> name = MCTest::Option("hd-time-of-day");

        if (!name.has_value())
        {
            return;
        }

        const std::optional<MCTimeOfDay> time = MCSceneLights::TimeOfDayNamed(*name);
        REQUIRE(time.has_value());
        MCSceneLights::SetMissionTime(*time);
        std::cout << std::format("  time of day: {}\n", *name);
    }

    /// <summary>
    /// The picture tools' start: mission <c>--mission &lt;n&gt;</c> (default 1) drawn by the GPU in a window of
    /// <c>--window &lt;w&gt;x&lt;h&gt;</c> (default 3024x1890, a Retina MacBook's pixels) with the UI 720 lines tall, and
    /// HD art on with the pack at
    /// <paramref name="pack"/> (into <paramref name="hdArt"/>; its ground read up front, as a mission's load reads it)
    /// unless <c>--hd-original 1</c>, and HD lighting as <c>--hd-lighting</c> asks (<see cref="LightingFromOptions"/>),
    /// at the time of day <c>--hd-time-of-day</c> sets (<see cref="TimeOfDayFromOptions"/>).
    /// </summary>
    /// <returns>Whether the pictures are the original's (<c>--hd-original</c>).</returns>
    bool StartPictures(std::string_view pack, std::optional<MCHdArtOn>& hdArt)
    {
        // The folder is made absolute before the game starts: the game works in the install's folder.
        const std::filesystem::path packFolder = std::filesystem::absolute(std::filesystem::path(pack));
        int32_t mission = 1;

        if (const std::optional<std::string_view> given = MCTest::Option("mission"); given.has_value())
        {
            std::from_chars(given->data(), given->data() + given->size(), mission);
        }

        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
        REQUIRE(MCTestGame::StartMission(mission));
        REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);

        int32_t width = 3024;
        int32_t height = 1890;
        constexpr int UiLines = 720;

        if (const std::optional<std::string_view> window = MCTest::Option("window"); window.has_value())
        {
            const size_t x = window->find('x');
            REQUIRE(x != std::string_view::npos);
            std::from_chars(window->data(), window->data() + x, width);
            std::from_chars(window->data() + x + 1, window->data() + window->size(), height);
        }

        ScaleUp(width, height, UiLines);
        const bool original = MCTest::Option("hd-original").has_value();

        if (!original)
        {
            hdArt.emplace(packFolder);
            MCTerrainReadHdTiles();
        }

        LightingFromOptions();
        TimeOfDayFromOptions();
        AirBeamsFromOptions();
        SunReliefFromOptions();
        SunShadowsFromOptions();
        CloudsFromOptions();
        ParticlesFromOptions();
        return original;
    }

    /// <summary>
    /// The art modes <c>--hd-art-modes &lt;frame&gt;:&lt;mode&gt;,...</c> asks a picture tool to switch to as it plays,
    /// each from that frame on (the tool's frames, as its shots number them), by frame; modes as the debug console's art
    /// command names them (main/MCArtModes.h: original, hd, lit, dusk, night). Without <c>--hd-original 1</c>, so the
    /// pack is there for the modes that draw HD art.
    /// </summary>
    std::map<int32_t, MCArtMode> ArtModesFromOptions()
    {
        std::map<int32_t, MCArtMode> modes;

        for (const std::string& entry : ListOption("hd-art-modes"))
        {
            MCTest::Scope scope(std::format("--hd-art-modes {}", entry));
            const size_t colon = entry.find(':');
            REQUIRE(colon != std::string::npos);
            int32_t frame = 0;
            const auto [end, error] = std::from_chars(entry.data(), entry.data() + colon, frame);
            REQUIRE((error == std::errc() && end == entry.data() + colon));
            const std::optional<MCArtMode> mode = ArtModeNamed(std::string_view(entry).substr(colon + 1));
            REQUIRE(mode.has_value());
            modes.insert_or_assign(frame, *mode);
        }

        REQUIRE((modes.empty() || !MCTest::Option("hd-original").has_value()));
        return modes;
    }

    /// <summary>
    /// Shows the art mode <paramref name="modes"/> (<see cref="ArtModesFromOptions"/>) asks for at
    /// <paramref name="frame"/>, if any.
    /// </summary>
    void SwitchArtMode(const std::map<int32_t, MCArtMode>& modes, int32_t frame)
    {
        if (const auto found = modes.find(frame); found != modes.end())
        {
            MCTest::Scope scope(std::format("--hd-art-modes {}:{}", frame, found->second.Name));

            if (const std::expected<void, std::string> shown = ShowArtMode(found->second); !shown.has_value())
            {
                FAIL(shown.error());
            }

            std::cout << std::format("  frame {}: {}\n", frame, found->second.Name);
        }
    }

    /// <summary>
    /// Saves the shown frame as <paramref name="folder"/>/&lt;name&gt;_hd.png (or _original.png), and prints how many HD
    /// frames have been read so far.
    /// </summary>
    void Shoot(const std::filesystem::path& folder, std::string_view name, bool original)
    {
        const std::filesystem::path path = folder / std::format("{}_{}.png", name, original ? "original" : "hd");
        REQUIRE(MCInput::Display()->SaveShownScreenshot(path).has_value());
        const size_t read = MCHdArt::FramesRead();
        const auto readTime = std::chrono::duration<double, std::milli>(MCHdArt::ReadTime()).count();
        std::cout << std::format("  {}: {} HD frames read so far ({:.2f} ms each), {} MB kept\n", name, read,
                                 read > 0 ? readTime / static_cast<double>(read) : 0.0, MCHdArt::CachedBytes() >> 20);
    }
}

/// <summary>
/// Tool: <c>--hd-pack &lt;folder&gt; --shots &lt;folder&gt;</c> pictures a pack's mech: mission 1's mech given the tree
/// <c>--mech &lt;name&gt;</c> (default "madcat"), standing at several facings, walking, and zoomed in, shown with HD art
/// (<c>&lt;name&gt;_hd.png</c>), or with <c>--hd-original 1</c> without it (<c>&lt;name&gt;_original.png</c>). A run plays
/// out the same every time, so the two runs' pictures differ only in how the mech is drawn. The window is
/// <c>--window &lt;w&gt;x&lt;h&gt;</c> (default 3024x1890, a Retina MacBook's pixels) with the UI 720 lines tall.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-pack pictures a pack's mech beside the original")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-pack");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    const PlayerMech player = GivePlayerMech(MCTest::Option("mech").value_or("madcat"));
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const auto shoot = [&](const std::string& name) { Shoot(folder, name, original); };

    for (const float facing : {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, -45.0f, -90.0f, -135.0f})
    {
        MCTestGame::FaceMech(*player.Mech, facing);
        MCTestGame::RunFrame(FrameSeconds);
        MCTestGame::RunFrame(FrameSeconds);
        shoot(std::format("stand_{:+04.0f}", facing));
    }

    // Walking: a few steps into the walk, then a picture every few frames.
    constexpr float WalkDistance = 500.0f;
    constexpr int32_t WalkFrames = 24;
    constexpr int32_t ShotEvery = 6;
    const std::shared_ptr<MCMechWarrior> pilot = player.Mech->GetPilot();
    REQUIRE(pilot != nullptr);
    const MCVector3D start = player.Mech->GetPosition();
    pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, MCVector3D(start.X + WalkDistance, start.Y, start.Z), -1, 0);

    for (int32_t frame = 1; frame <= WalkFrames; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);

        if (frame % ShotEvery == 0)
        {
            shoot(std::format("walk_{:02}", frame));
        }
    }

    // Zoomed in.
    constexpr float ZoomedLines = 300.0f;
    MCFixedZoomHeight = ZoomedLines;

    for (int32_t frame = 0; frame < ShotEvery; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    shoot("zoomed");

    // Other paint schemes (the actors' fade tables), from the mech's own.
    std::cout << std::format("  the mech's paint scheme: {}\n", player.Actor->FadeTableIndex);

    for (const int32_t scheme : {37, 92, 103, 109})
    {
        player.Actor->FadeTableIndex = scheme;
        MCTestGame::RunFrame(FrameSeconds);
        shoot(std::format("paint_{}", scheme));
    }
}

/// <summary>
/// Tool: <c>--hd-pack-objects &lt;folder&gt; --shots &lt;folder&gt;</c> pictures a pack's vehicles, buildings and trees:
/// in mission 1 (or <c>--mission</c>'s; the map revealed), the camera on the vehicle, the building, the object with
/// translucent frames and the tree nearest the player's mech in turn, each at the usual zoom and zoomed in, shown with HD art
/// (<c>&lt;name&gt;_hd.png</c>), or with
/// <c>--hd-original 1</c> without it (<c>&lt;name&gt;_original.png</c>). <c>--window</c> as --hd-pack's.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-pack-objects pictures a pack's vehicles, buildings and trees beside the original")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-pack-objects");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    RevealAll();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    constexpr int32_t SettleFrames = 3;
    constexpr float ZoomedLines = 300.0f;
    const std::array<std::pair<std::string_view, std::function<std::shared_ptr<MCGameObject>()>>, 4> subjects = {{
        {"vehicle", [] { return NearestSubject(HdSubject::Vehicle); }},
        {"building", [] { return NearestSubject(HdSubject::Building); }},
        {"translucent", [] { return NearestSubject(HdSubject::Translucent); }},
        {"tree", [] { return NearestObject(MCObjectClass::Tree); }},
    }};

    for (const auto& [name, nearest] : subjects)
    {
        const std::shared_ptr<MCGameObject> object = nearest();

        if (object == nullptr)
        {
            continue;
        }

        Eye()->ChangeTarget(object, true);

        for (const float zoom : {0.0f, ZoomedLines})
        {
            MCFixedZoomHeight = zoom;

            for (int32_t frame = 0; frame < SettleFrames; ++frame)
            {
                MCTestGame::RunFrame(FrameSeconds);
            }

            Shoot(folder, zoom > 0.0f ? std::format("{}_zoomed", name) : std::string(name), original);
        }

        MCFixedZoomHeight = 0.0f;
    }
}

namespace
{
    /// <summary>The object type named <paramref name="name"/> (its packet in the object PAK, which CreateObject takes).</summary>
    std::optional<int32_t> ObjectTypeNamed(std::string_view name)
    {
        MCPacketFile pak;
        REQUIRE(pak.Open(GamePath(ObjectPath, ObjectPakName)) == 0);

        for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
        {
            MCFitIniFile fit;

            if (pak.SeekPacket(packet) == 0 && pak.GetPacketSize() > 0 &&
                fit.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())) == 0 && fit.SeekBlock("ObjectType") == 0 &&
                fit.Read<std::string>("Name").value_or("") == name)
            {
                return packet;
            }
        }

        return std::nullopt;
    }

    /// <summary>
    /// Object type <paramref name="type"/> (its packet in the object PAK) as its name and appearance ("name (appearance
    /// 0x...)"), empty when it has none.
    /// </summary>
    std::string ObjectTypeLabel(uint32_t type)
    {
        MCPacketFile pak;
        REQUIRE(pak.Open(GamePath(ObjectPath, ObjectPakName)) == 0);
        MCFitIniFile fit;

        if (static_cast<int32_t>(type) >= pak.GetNumPackets() || pak.SeekPacket(static_cast<int32_t>(type)) != 0 ||
            pak.GetPacketSize() <= 0 || fit.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())) != 0 ||
            fit.SeekBlock("ObjectType") != 0)
        {
            return {};
        }

        return std::format("{} (appearance {:#x})", fit.Read<std::string>("Name").value_or(""),
                           fit.Read<int32_t>("Appearance").value_or(0));
    }
}

/// <summary>
/// Tool: <c>--hd-pack-effects &lt;folder&gt; --shots &lt;folder&gt;</c> pictures a pack's effects: in mission 1, each of
/// <c>--explosion &lt;name,...&gt;</c> (object types; default minedet, mdst and gasexpl) goes off beside the player's mech,
/// shot as it plays (<c>&lt;name&gt;_&lt;frame&gt;_hd.png</c>), or with <c>--hd-original 1</c> without HD art. A run plays
/// out the same every time, so the two runs' pictures differ only in how the effects are drawn. <c>--window</c> as
/// --hd-pack's. With <c>--hd-effects-video &lt;n&gt;</c>, each plays n frames at 60 a second, every one shot
/// (<c>&lt;name&gt;_&lt;frame, 4 digits&gt;</c>; the pack's in-betweens show between the art's frames). With
/// <c>--hd-effects-arms 1</c>, the player's mech throws off both its arms instead (<c>arms_...</c>; the debris named), each
/// tumbling at 7 frames a second as it flies. <c>--hd-art-modes &lt;frame&gt;:&lt;mode&gt;,...</c> switches the art as
/// each plays (original, hd, lit, dusk: the debug console's art command).
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-pack-effects pictures a pack's explosions beside the original")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-pack-effects");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    RevealAll();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const std::shared_ptr<MCMover> player = GetMoverFromPartId(PlayerMechPartId);
    REQUIRE(player != nullptr);
    Eye()->ChangeTarget(player, true);
    std::set<std::string> names = ListOption("explosion");

    if (names.empty())
    {
        names = {"minedet", "mdst", "gasexpl"};
    }

    // Beside the mech (world units), the frames shot after it goes off, and the frames it is given to finish; a video's
    // frames are shorter.
    constexpr float Beside = 60.0f;
    constexpr std::array<int32_t, 5> ShotFrames = {2, 5, 9, 14, 20};
    constexpr int32_t Frames = 45;
    constexpr float VideoFrameSeconds = 1.0f / 60.0f;
    int32_t videoFrames = 0;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-effects-video"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), videoFrames);
    }

    const bool video = videoFrames > 0;
    const std::map<int32_t, MCArtMode> artModes = ArtModesFromOptions();

    // Plays the effect just set off, shooting its frames as <name>_<frame>.
    const auto play = [&](std::string_view name)
    {
        for (int32_t frame = 1; frame <= (video ? videoFrames : Frames); ++frame)
        {
            SwitchArtMode(artModes, frame);
            MCTestGame::RunFrame(video ? VideoFrameSeconds : FrameSeconds);

            if (video)
            {
                Shoot(folder, std::format("{}_{:04}", name, frame), original);
            }
            else if (std::ranges::contains(ShotFrames, frame))
            {
                Shoot(folder, std::format("{}_{:02}", name, frame), original);
            }
        }
    };

    if (MCTest::Option("hd-effects-arms").has_value())
    {
        const auto mech = std::dynamic_pointer_cast<MCBattleMech>(player);
        REQUIRE(mech != nullptr);
        const auto& type = static_cast<const MCBattleMechType&>(*mech->GetObjectType());
        std::cout << std::format("  arms: {} and {}\n", ObjectTypeLabel(type.LeftArmDebrisId),
                                 ObjectTypeLabel(type.RightArmDebrisId));
        mech->LeftArmBlownThisFrame = true;
        mech->RightArmBlownThisFrame = true;
        play("arms");
        return;
    }

    for (const std::string& name : names)
    {
        const std::optional<int32_t> type = ObjectTypeNamed(name);
        MCTest::Scope scope(std::format("--explosion {}", name));
        REQUIRE(type.has_value());
        MCVector3D at = player->GetPosition();
        at.X += Beside;
        CreateExplosion(*type, at, 0.0f, 0.0f);
        play(name);
    }
}

namespace
{
    /// <summary>
    /// What the HD art audit tallies a draw by: the command, whether it was sharp, what it drew (data, or the window it
    /// copied from), and where.
    /// </summary>
    using AuditKey = std::tuple<std::string, bool, const uint8_t*, const MCWindow*, const MCWindow*>;

    /// <summary>The draws of one <see cref="AuditKey"/>: how many, the window pixels they covered, their frames.</summary>
    struct AuditTally
    {
        int64_t Count = 0;
        int64_t Pixels = 0;
        bool Scaled = false;
        std::set<int32_t> Frames;

        // Taken as they are drawn, since a closed screen's windows and data are gone by the report: the window's size,
        // what drew (AuditOwner), a copy's picture, and the first few frames of a shape table with how the pack finds
        // them (its attached folder, else its hash).
        int32_t WindowWidth = 0;
        int32_t WindowHeight = 0;
        std::string Owner;
        std::optional<DecodedFrame> Picture;
        // The colours shown when it was first drawn (a copy's picture is saved in them), and what was on screen then
        // (AuditLabel, when the tool sets one).
        std::array<SDL_Color, 256> Colors{};
        std::string Label;
        std::map<int32_t, DecodedFrame> ShapeFrames;
        std::optional<std::filesystem::path> Attached;
        std::optional<uint64_t> TableHash;
    };

    /// <summary>How many of a shape table's frames an audit keeps (decoded as they are drawn) to picture.</summary>
    constexpr size_t AuditFramesKept = 4;

    /// <summary>What a tool shows while it audits (a screen's name), noted with each tally's first draw.</summary>
    std::string AuditLabel;

    std::function<void(const MCVulkanRenderer::AuditedDraw&)> AuditInto(std::map<AuditKey, AuditTally>& tally);
    void ReportAudit(const std::map<AuditKey, AuditTally>& tally, const std::filesystem::path& folder, int32_t frames);

    /// <summary>
    /// Whether a shape's frame shows nothing: no pixel drawn, or a translucent one's every pixel colour 0 (AlphaPal.ini's
    /// marker colour, transparent).
    /// </summary>
    bool BlankFrame(const DecodedFrame& frame)
    {
        for (size_t i = 0; i < frame.Index.size(); ++i)
        {
            if (frame.Drawn[i] != 0 && (!frame.Translucent || frame.Index[i] != 0))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>
    /// Whether the audit counts a draw as sharp: drawn so, or showing nothing (it covered no pixels, or its shape's
    /// frame is blank: invisible with HD art on or off), so nothing of the original's pixels shows.
    /// </summary>
    bool AuditSharp(const MCVulkanRenderer::AuditedDraw& draw)
    {
        if (draw.Sharp || draw.Pixels == 0)
        {
            return true;
        }

        if (!VfxIsShapeTable(draw.Data) || draw.Frame < 0)
        {
            return false;
        }

        const auto decoded = Decode(draw.Data, draw.Frame);
        return decoded.has_value() && BlankFrame(*decoded);
    }
}

namespace
{
    /// <summary>The pictures a weapon of --hd-weapons gets: frames after it fires, spread over its flight and hit.</summary>
    constexpr int32_t WeaponShots = 6;

    /// <summary>A weapon --hd-weapons fires: its pictures' name, its master component id and the frames it is shot at.</summary>
    struct PicturedWeapon
    {
        std::string_view Name;
        int32_t MasterId = 0;
        std::array<int32_t, WeaponShots> ShotFrames{};
    };

    /// <summary>
    /// The weapons --hd-weapons fires, by their Inner Sphere master ids (logistics' FitComponents): the PPC, the Gauss
    /// rifle and the large ER laser, all three projectile lasers. Over mission 1's 200 units the PPC's bolt grows for five
    /// frames and its hit plays from the seventh; the slug and the laser's bolt fly four and hit from the sixth.
    /// </summary>
    constexpr std::array<PicturedWeapon, 3> PicturedWeapons = {{
        {"ppc", 0x91, {1, 3, 5, 7, 9, 13}},
        {"gauss", 0x68, {1, 2, 4, 6, 7, 9}},
        {"erlaser", 0x8d, {1, 2, 4, 6, 8, 10}},
    }};

    /// <summary>
    /// Sends <paramref name="masterId"/>'s weapon effect from <paramref name="shooter"/>'s right arm to
    /// <paramref name="target"/>'s centre torso as a hit of MCBattleMech::FireWeapon launches it (its LaunchWeaponFX),
    /// with a shot doing <paramref name="damage"/> (none: the target stands for every weapon). Returns the effect.
    /// </summary>
    std::shared_ptr<MCGameObject> LaunchWeapon(MCBattleMech& shooter, const std::shared_ptr<MCBattleMech>& target,
                                               int32_t masterId, float damage = 0.0f)
    {
        const MCMasterComponent& weapon = MasterComponentList[static_cast<size_t>(masterId)];
        std::shared_ptr<MCGameObject> fx = CreateObject(static_cast<int32_t>(WeaponFXTable[weapon.WeaponEffect]));
        MCTest::Scope scope(std::format("weapon {} ({}), effect {}", masterId, weapon.Name, weapon.WeaponEffect));
        REQUIRE(fx != nullptr);
        const std::shared_ptr<MCGameObject> source = shooter.SharedAs<MCGameObject>();
        const int32_t sourceHotSpot = shooter.BodyAt(MechRightArm).HotSpotNumber;
        const int32_t targetHotSpot = target->BodyAt(MechCenterTorso).HotSpotNumber;
        const float entryAngle = target->RelFacingTo(shooter.GetPosition(), -1);
        const auto aim = [&](MCWeaponShotInfo& shot)
        { shot.Init(source, masterId, damage, MechCenterTorso, entryAngle); };
        const auto launch = [&](auto& effect, MCWeaponShotInfo& shot)
        {
            aim(shot);
            effect.Owner = source;
            effect.Target = target;
            effect.OwnerHotSpot = sourceHotSpot;
            effect.TargetHotSpot = targetHotSpot;
        };

        if (fx->ObjectClass == MCObjectClass::Bullet)
        {
            auto& bullet = static_cast<MCBullet&>(*fx);
            launch(bullet, bullet.NewShot());
        }
        else if (fx->ObjectClass == MCObjectClass::ProjectileLaser)
        {
            auto& projectile = static_cast<MCProjectileLaser&>(*fx);
            launch(projectile, projectile.ShotInfo);
        }
        else
        {
            REQUIRE(fx->ObjectClass == MCObjectClass::Laser);
            auto& laser = static_cast<MCLaser&>(*fx);
            aim(laser.ShotInfo);
            laser.Source = source;
            laser.Target = target;
            laser.SourceHotSpot = sourceHotSpot;
            laser.TargetHotSpot = targetHotSpot;
        }

        std::cout << std::format("  {}: {} (effect {}, object type {}, class {})\n", masterId, weapon.Name,
                                 weapon.WeaponEffect, WeaponFXTable[weapon.WeaponEffect],
                                 static_cast<int32_t>(fx->ObjectClass));
        WeaponList()->Add(fx);
        return fx;
    }

    /// <summary>
    /// Turns <paramref name="mech"/> to face <paramref name="point"/>: FaceMech's facing turned by the mech's relative
    /// facing to the point one way or the other, whichever leaves it facing the point.
    /// </summary>
    void FaceMechTo(MCBattleMech& mech, MCVector3D point)
    {
        MCTestGame::FaceMech(mech, 0.0f);
        const float turn = mech.RelFacingTo(point, -1);
        float best = std::numeric_limits<float>::max();
        float facing = 0.0f;

        for (const float candidate : {turn, -turn})
        {
            MCTestGame::FaceMech(mech, candidate);

            if (const float left = std::abs(mech.RelFacingTo(point, -1)); left < best)
            {
                best = left;
                facing = candidate;
            }
        }

        MCTestGame::FaceMech(mech, facing);
    }
}

/// <summary>
/// Tool: <c>--hd-weapons &lt;pack&gt; --shots &lt;folder&gt;</c> pictures weapons firing: in mission 1 (the map
/// revealed), the player's mech faces the next mech of its lance and fires a PPC, a Gauss rifle and a large ER laser at
/// it in turn (shots that do no damage), the camera between the two, each shot at frames spread over its flight and hit
/// (<c>&lt;weapon&gt;_&lt;n&gt;_hd.png</c>), or with <c>--hd-original 1</c> without HD art. A run plays out the same
/// every time, so the two runs' pictures differ only in how the effects are drawn. <c>--window</c> as --hd-pack's.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-weapons pictures weapons firing beside the original")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-weapons");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    RevealAll();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const std::shared_ptr<MCMover> shooterMover = GetMoverFromPartId(PlayerMechPartId);
    const std::shared_ptr<MCMover> targetMover = GetMoverFromPartId(PlayerMechPartId + 1);
    REQUIRE(shooterMover != nullptr);
    REQUIRE(targetMover != nullptr);
    REQUIRE(shooterMover->ObjectClass == MCObjectClass::BattleMech);
    REQUIRE(targetMover->ObjectClass == MCObjectClass::BattleMech);
    const auto shooter = std::static_pointer_cast<MCBattleMech>(shooterMover);
    const auto target = std::static_pointer_cast<MCBattleMech>(targetMover);
    const MCVector3D from = shooter->GetPosition();
    const MCVector3D to = target->GetPosition();
    std::cout << std::format("  shooter at ({:.0f}, {:.0f}), target at ({:.0f}, {:.0f}), {:.0f} apart\n", from.X,
                             from.Y, to.X, to.Y, std::hypot(to.X - from.X, to.Y - from.Y));
    FaceMechTo(*shooter, to);
    Eye()->ChangeTarget(nullptr, true);
    Eye()->SetPosition(MCVector3D((from.X + to.X) * 0.5f, (from.Y + to.Y) * 0.5f, (from.Z + to.Z) * 0.5f));

    // The frames before each weapon fires (the last one's hit effect gone), and those it is given to finish.
    constexpr int32_t SettleFrames = 15;
    constexpr int32_t Frames = 30;

    for (const PicturedWeapon& weapon : PicturedWeapons)
    {
        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        const std::weak_ptr<MCGameObject> fx = LaunchWeapon(*shooter, target, weapon.MasterId);
        int32_t shot = 0;
        int32_t lasted = 0;

        for (int32_t frame = 1; frame <= Frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);

            if (!fx.expired())
            {
                lasted = frame;
            }

            if (std::ranges::contains(weapon.ShotFrames, frame))
            {
                Shoot(folder, std::format("{}_{}", weapon.Name, shot++), original);
            }
        }

        std::cout << std::format("  {}: the shot flew {} frames\n", weapon.Name, lasted);
    }
}

/// <summary>
/// Tool: <c>--hd-mech-death &lt;pack&gt; --shots &lt;folder&gt;</c> pictures a mech's death: --hd-weapons' scene, the
/// shot a PPC doing enough damage to destroy its target's centre torso, then a picture every few frames from its fall
/// to its wreck (<c>death_&lt;frame&gt;_hd.png</c>, or with <c>--hd-original 1</c> without HD art). With
/// <c>--hd-audit-frames n</c>, the draws of its first n frames are tallied as --hd-audit tallies a battle's (into
/// <c>audit</c> in the folder).
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-mech-death pictures a mech's death beside the original")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-mech-death");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    RevealAll();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const auto shooter = std::static_pointer_cast<MCBattleMech>(GetMoverFromPartId(PlayerMechPartId));
    const auto target = std::static_pointer_cast<MCBattleMech>(GetMoverFromPartId(PlayerMechPartId + 1));
    REQUIRE(shooter != nullptr);
    REQUIRE(target != nullptr);
    const MCVector3D from = shooter->GetPosition();
    const MCVector3D to = target->GetPosition();
    FaceMechTo(*shooter, to);
    Eye()->ChangeTarget(nullptr, true);
    Eye()->SetPosition(to);

    // A PPC's master id, a hit no centre torso survives, and the pictures' spacing and the frames after the blast.
    constexpr int32_t Ppc = 0x91;
    constexpr float Lethal = 1000.0f;
    constexpr int32_t Every = 3;
    constexpr int32_t MostFrames = 600;
    constexpr int32_t AfterBlast = 30;
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);
    const int32_t audited = std::stoi(std::string(MCTest::Option("hd-audit-frames").value_or("0")));
    std::map<AuditKey, AuditTally> tally;
    LaunchWeapon(*shooter, target, Ppc, Lethal);
    int32_t blast = -1;

    for (int32_t frame = 1; frame <= MostFrames && (blast < 0 || frame <= blast + AfterBlast); ++frame)
    {
        renderer->Audit = frame <= audited ? AuditInto(tally) : nullptr;
        MCTestGame::RunFrame(FrameSeconds);

        if (blast < 0 && target->DeathExplosionDone)
        {
            blast = frame;
        }

        if (frame % Every == 0)
        {
            Shoot(folder, std::format("death_{:03}", frame), original);
        }
    }

    renderer->Audit = nullptr;
    std::cout << std::format("  destroyed: {}, blew up at frame {}\n", target->IsDestroyed() != 0, blast);
    CHECK(blast > 0);

    if (audited > 0)
    {
        ReportAudit(tally, folder / "audit", audited);
    }
}

namespace
{
    /// <summary>The terrain test's HD tiles' scale.</summary>
    constexpr int32_t TerrainScale = 2;

    /// <summary>
    /// Writes the HD copies of the ground mesh's tiles into <paramref name="pack"/>'s terrain folder: each pixel's shown
    /// colour inverted, at <see cref="TerrainScale"/> (nearest), opaque, with the index picture beside it.
    /// </summary>
    size_t WriteTerrainPack(const std::filesystem::path& pack, const MCTerrainMesh& mesh,
                            std::span<const SDL_Color, 256> colors)
    {
        const std::filesystem::path folder = pack / "terrain";
        std::filesystem::create_directories(folder);
        size_t written = 0;

        for (const std::vector<uint8_t>& tile : mesh.Tiles)
        {
            const DecodedTile decoded = DecodeTile(tile);

            if (decoded.Width == 0)
            {
                continue;
            }

            const int32_t width = decoded.Width * TerrainScale;
            const int32_t height = decoded.Height * TerrainScale;
            std::vector<uint8_t> color(static_cast<size_t>(width) * height * BytesPerPixel);
            std::vector<uint8_t> index(color.size());

            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < width; ++x)
                {
                    const size_t from = static_cast<size_t>(y / TerrainScale) * decoded.Width + x / TerrainScale;
                    const size_t to = (static_cast<size_t>(y) * width + x) * BytesPerPixel;
                    const SDL_Color shown = colors[decoded.Index[from]];
                    color[to] = static_cast<uint8_t>(255 - shown.r);
                    color[to + 1] = static_cast<uint8_t>(255 - shown.g);
                    color[to + 2] = static_cast<uint8_t>(255 - shown.b);
                    color[to + 3] = HasIndex;
                    index[to] = decoded.Index[from];
                    index[to + 1] = decoded.Drawn[from] != 0 ? HasIndex : 0;
                    index[to + 3] = HasIndex;
                }
            }

            const std::string name = MCHdArt::TerrainTileName(tile);
            SavePng(folder / (name + ".png"), width, height, color);
            SavePng(folder / (name + "_idx.png"), width, height, index);
            ++written;
        }

        return written;
    }
}

namespace
{
    /// <summary>What the HD terrain tests saw: the view before and after HD art went on, and the tiles in the pack.</summary>
    struct HdGround
    {
        Shown Before;
        Shown After;
        size_t Tiles = 0;
        bool HdTerrain = false;
    };

    /// <summary>
    /// Mission 1 drawn <paramref name="drawing"/> (<paramref name="scene"/> set up for it), then with a pack of the map's
    /// tiles (colours inverted) for <paramref name="frames"/> frames.
    /// </summary>
    std::optional<HdGround> DrawHdGround(MCGpuDrawing drawing, int32_t frames, std::optional<HdScene>& scene)
    {
        scene = SetUpHdScene(drawing);

        if (!scene.has_value())
        {
            return std::nullopt;
        }

        SingleStepMode = 1;
        MCTestGame::RunFrame(FrameSeconds);
        const MCTerrainMesh* mesh = MCTerrainMeshInUse();
        REQUIRE(mesh != nullptr);
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        HdGround ground;
        ground.Tiles = WriteTerrainPack(scene->Pack, *mesh, colors);
        REQUIRE(ground.Tiles > 0);
        ground.Before = ReadShown(scene->Scratch);
        const MCHdArtOn hdArt(scene->Pack);
        scene->Renderer->ResetMirror();

        for (int32_t frame = 0; frame < frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        ground.After = ReadShown(scene->Scratch);
        ground.HdTerrain = scene->Renderer->HdTerrain();
        return ground;
    }
}

/// <summary>HD terrain: with a pack holding the map's tiles (colours inverted), most of the view shows the pack's colours.</summary>
TEST_CASE_ISOLATED("game: HD art draws the ground in the pack's colours")
{
    std::optional<HdScene> scene;
    const std::optional<HdGround> ground = DrawHdGround(MCGpuDrawing::On, 1, scene);

    if (!ground.has_value())
    {
        return;
    }

    const Changed changed = Compare(ground->Before, ground->After);
    const size_t pixels = static_cast<size_t>(ground->After.Width) * static_cast<size_t>(ground->After.Height);
    std::cout << std::format("  {} tiles; {} of {} shown pixels changed\n", ground->Tiles, changed.Count, pixels);
    CHECK(ground->HdTerrain);
    CHECK(static_cast<size_t>(changed.Count) * 2 > pixels);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>
/// HD terrain is read while the mission loads (MCTerrainReadHdTiles): every tile of the map's ground mesh, so its first
/// frame with HD art reads none.
/// </summary>
TEST_CASE_ISOLATED("game: HD art's ground is read before the first frame needs it")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    SingleStepMode = 1;
    MCTestGame::RunFrame(FrameSeconds);
    const MCTerrainMesh* mesh = MCTerrainMeshInUse();
    REQUIRE(mesh != nullptr);
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    // A pack of the ground alone: the scene's pack has the mech's frames, which a frame reads when it draws the mech.
    const std::filesystem::path pack = scene->Scratch / "ground";
    const size_t tiles = WriteTerrainPack(pack, *mesh, colors);
    REQUIRE(tiles > 0);
    const MCHdArtOn hdArt(pack);
    const size_t before = MCHdArt::FramesRead();
    MCTerrainReadHdTiles();
    CHECK_EQ(MCHdArt::FramesRead() - before, tiles);

    // The frame uploads the ground with the pack's tiles, all read already.
    const size_t loaded = MCHdArt::FramesRead();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(scene->Renderer->HdTerrain());
    CHECK_EQ(MCHdArt::FramesRead(), loaded);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>HD terrain changes only the colours: in mirror mode every palette index stays the software renderer's.</summary>
TEST_CASE_ISOLATED("game: HD art's ground keeps the original's palette indices")
{
    constexpr int32_t Frames = 5;
    std::optional<HdScene> scene;
    const std::optional<HdGround> ground = DrawHdGround(MCGpuDrawing::Mirror, Frames, scene);

    if (!ground.has_value())
    {
        return;
    }

    const MCVulkanRenderer::MirrorTally& tally = scene->Renderer->Mirror();
    CHECK(ground->HdTerrain);
    CHECK_EQ(tally.Frames, Frames);
    CHECK_EQ(tally.DifferentFrames, 0);

    if (tally.DifferentFrames != 0)
    {
        std::cout << std::format("  first difference, frame {}: {}\n", tally.FirstFrame, tally.First);
    }

    std::filesystem::remove_all(scene->Scratch);
}

namespace
{
    /// <summary>
    /// The audit's details beside its tally (--hd-audit, and --sharp-menus with --hd-audit-menus), for telling what drew
    /// each thing from the original's pixels: per tally entry, the call stack of its first draw, the font a glyph is
    /// from and the screens it was drawn on; and per call stack, the draws from the original's pixels (commands without
    /// data, a clear, a polygon, a status bar, can only be told apart by where they are drawn from).
    /// </summary>
    struct AuditDetail
    {
        /// <summary>A tally entry's details, taken at its first draw (the screens: every draw's).</summary>
        struct Entry
        {
            std::vector<uintptr_t> Stack;
            std::string Font;
            std::set<std::string> Screens;
            /// <summary>The colours shown at the first draw (a menu picture's palette is its screen's).</summary>
            std::array<SDL_Color, 256> Colors{};
            /// <summary>
            /// A copy's picture as its latest draw from the original's pixels copied it, and the colours then (a
            /// window composed at run time is often still empty at its first copy).
            /// </summary>
            std::optional<DecodedFrame> Latest;
            std::array<SDL_Color, 256> LatestColors{};
        };

        /// <summary>The draws from the original's pixels of one command, on windows of one size, from one call stack.</summary>
        struct Site
        {
            bool Scaled = false;
            int64_t Count = 0;
            int64_t Pixels = 0;
            std::set<std::string> Screens;
        };

        /// <summary>A site: the command, the window's width and height, and the call stack.</summary>
        using SiteKey = std::tuple<std::string, int32_t, int32_t, std::vector<uintptr_t>>;
        std::map<AuditKey, Entry> Entries;
        std::map<SiteKey, Site> Sites;
        /// <summary>The screen being shown, which every draw is tagged with.</summary>
        std::string Screen = "mission";
    };

    /// <summary>AuditInto's tally of each draw, with <paramref name="detail"/>'s details beside it.</summary>
    std::function<void(const MCVulkanRenderer::AuditedDraw&)> AuditInDetail(std::map<AuditKey, AuditTally>& tally,
                                                                            AuditDetail& detail);

    /// <summary>
    /// Writes an audit's tables into <paramref name="folder"/> for working through across runs: <c>audit.tsv</c>, every
    /// tally entry drawn from the original's pixels with a stable id, whether the pack has its art, its call stack and
    /// screens; <c>sites.tsv</c>, those draws by call stack; and <c>items/&lt;id&gt;.png</c>, a picture of each.
    /// </summary>
    void WriteAuditTables(const std::map<AuditKey, AuditTally>& tally, const AuditDetail& detail,
                          const std::filesystem::path& folder, int32_t frames);

    /// <summary>
    /// Shows the screens --sharp-menus' first round leaves out, each through <paramref name="shoot"/> (named): the other
    /// inventory tabs of the mech bay and the purchase screen, the briefing's operation tab, the main menu in a campaign, the save and single-mission load screens, the exit question, the multiplayer screens (the
    /// connection menu, the serial and modem screens, the IPX/TCP question, the LAN games, hosting's ready room and
    /// session screen), the expansion's briefing, then a mission launched from the briefing (its loading screen),
    /// paused, the tactical map's info, objectives and salvage pages, the preferences' CONTROLS panel where the build
    /// has one, the results and debriefing, and logistics after the mission.
    /// </summary>
    void TourScreens(const std::function<void(const std::string&)>& shoot, AuditDetail& detail);
}

namespace
{
    /// <summary>The seam test's two tile colours, a channel apart: a tile's every HD pixel is one of them.</summary>
    constexpr SDL_Color SeamRed = {220, 30, 30, 255};
    constexpr SDL_Color SeamBlue = {30, 30, 220, 255};
    /// <summary>How far a shown channel may be from the colour it is taken for.</summary>
    constexpr int32_t SeamTolerance = 12;
    /// <summary>A mix of the two lies this far from both, at least, in red.</summary>
    constexpr int32_t SeamMixed = 40;
    /// <summary>FNV-1a's 64-bit offset basis and prime (a tile's colour is picked by its name's hash).</summary>
    constexpr uint64_t SeamHashBasis = 0xcbf29ce484222325ull;
    constexpr uint64_t SeamHashPrime = 0x100000001b3ull;

    /// <summary>
    /// Writes HD copies of the ground mesh's tiles into <paramref name="pack"/>'s terrain folder, each one colour
    /// (SeamRed or SeamBlue, by its name's hash), at TerrainScale, with the index picture WriteTerrainPack writes.
    /// </summary>
    size_t WriteTwoColourTerrain(const std::filesystem::path& pack, const MCTerrainMesh& mesh)
    {
        const std::filesystem::path folder = pack / "terrain";
        std::filesystem::create_directories(folder);
        size_t written = 0;

        for (const std::vector<uint8_t>& tile : mesh.Tiles)
        {
            const DecodedTile decoded = DecodeTile(tile);

            if (decoded.Width == 0)
            {
                continue;
            }

            const std::string name = MCHdArt::TerrainTileName(tile);
            // FNV-1a over the name: the same split on every platform.
            uint64_t hash = SeamHashBasis;

            for (const char c : name)
            {
                hash = (hash ^ static_cast<uint8_t>(c)) * SeamHashPrime;
            }

            const SDL_Color colour = hash % 2 == 0 ? SeamRed : SeamBlue;
            const int32_t width = decoded.Width * TerrainScale;
            const int32_t height = decoded.Height * TerrainScale;
            std::vector<uint8_t> color(static_cast<size_t>(width) * height * BytesPerPixel);
            std::vector<uint8_t> index(color.size());

            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < width; ++x)
                {
                    const size_t from = static_cast<size_t>(y / TerrainScale) * decoded.Width + x / TerrainScale;
                    const size_t to = (static_cast<size_t>(y) * width + x) * BytesPerPixel;
                    std::ranges::copy(std::array{colour.r, colour.g, colour.b, HasIndex},
                                      color.begin() + static_cast<ptrdiff_t>(to));
                    index[to] = decoded.Index[from];
                    index[to + 1] = decoded.Drawn[from] != 0 ? HasIndex : 0;
                    index[to + 3] = HasIndex;
                }
            }

            SavePng(folder / (name + ".png"), width, height, color);
            SavePng(folder / (name + "_idx.png"), width, height, index);
            ++written;
        }

        return written;
    }
}

/// <summary>
/// HD ground: the tiles' HD colours fade into each other across their seams (each tile is upscaled on its own): with a
/// pack whose tiles are each one of two colours, the shown ground holds mixes of the two along the tiles' seams, and is
/// still mostly the two colours inside the tiles.
/// </summary>
TEST_CASE_ISOLATED("game: HD art's ground fades from tile to tile across their seams")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    RevealAll();
    SingleStepMode = 1;
    MCTestGame::RunFrame(FrameSeconds);
    const MCTerrainMesh* mesh = MCTerrainMeshInUse();
    REQUIRE(mesh != nullptr);
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path pack = scene->Scratch / "ground";
    REQUIRE(WriteTwoColourTerrain(pack, *mesh) > 0);
    WritePaletteRgb(pack, colors);
    const MCHdArtOn hdArt(pack);
    MCTestGame::RunFrame(FrameSeconds);
    REQUIRE(scene->Renderer->HdTerrain());
    const Shown shown = ReadShown(scene->Scratch);

    // The view's middle half (the interface lies around it).
    const auto near = [](const uint8_t* pixel, const SDL_Color& colour)
    {
        return std::abs(pixel[0] - colour.r) <= SeamTolerance && std::abs(pixel[1] - colour.g) <= SeamTolerance &&
               std::abs(pixel[2] - colour.b) <= SeamTolerance;
    };

    int64_t plain = 0;
    int64_t mixed = 0;
    int64_t pixels = 0;

    for (int32_t y = shown.Height / 4; y < shown.Height * 3 / 4; ++y)
    {
        for (int32_t x = shown.Width / 4; x < shown.Width * 3 / 4; ++x)
        {
            const uint8_t* pixel = &shown.Pixels[(static_cast<size_t>(y) * shown.Width + x) * BytesPerPixel];
            const int32_t sum = pixel[0] + pixel[2];
            ++pixels;

            if (near(pixel, SeamRed) || near(pixel, SeamBlue))
            {
                ++plain;
            }
            else if (std::abs(pixel[1] - SeamRed.g) <= SeamTolerance &&
                     std::abs(sum - (SeamRed.r + SeamRed.b)) <= 2 * SeamTolerance &&
                     pixel[0] >= SeamBlue.r + SeamMixed && pixel[0] <= SeamRed.r - SeamMixed)
            {
                ++mixed;
            }
        }
    }

    std::cout << std::format("  of {} pixels: {} one tile's colour, {} a mix of two\n", pixels, plain, mixed);
    // Without the fade, every tile's pixel would be one colour or the other; the seams' mixes are a band a few shown
    // pixels wide along them.
    constexpr int64_t MixedShare = 1000;
    CHECK(plain * 2 > pixels);
    CHECK(mixed * MixedShare > pixels);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>
/// Tool: <c>--sharp-menus &lt;folder&gt;</c> shows the main menu and the logistics screens in a window the size of a
/// Retina MacBook's screen (<c>--window WxH</c>, default 3024x1890) with HD art on (a sharp screen: text redrawn sharp,
/// and the pack's menu art with <c>--hd-pack &lt;pack&gt;</c>), and saves each as shown into the folder; with
/// <c>--hd-original 1</c>, as the game shows them without. With <c>--hd-audit-menus 1</c> every draw is audited as
/// --hd-audit's are (into the folder's audit/), and with it or <c>--sharp-menus-tour 1</c> every other screen is shown
/// too (TourScreens: the multiplayer screens, a mission from its loading screen to its results, and more).
/// </summary>
TEST_CASE_ISOLATED("tool: --sharp-menus pictures the menus with HD art on or off")
{
    const std::optional<std::string_view> shots = MCTest::Option("sharp-menus");

    if (!shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    // The pack's folder made absolute before the game starts, as --hd-pack's tool does (none: the user folder's).
    const std::optional<std::string_view> packOption = MCTest::Option("hd-pack");
    const std::filesystem::path pack = packOption.has_value()
                                           ? std::filesystem::absolute(std::filesystem::path(*packOption))
                                           : std::filesystem::path();
    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);

    int32_t width = 3024;
    int32_t height = 1890;

    if (const std::optional<std::string_view> window = MCTest::Option("window"); window.has_value())
    {
        const size_t x = window->find('x');
        REQUIRE(x != std::string_view::npos);
        std::from_chars(window->data(), window->data() + x, width);
        std::from_chars(window->data() + x + 1, window->data() + window->size(), height);
    }

    REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), width, height));
    SDL_SyncWindow(&MCInput::Display()->Window());
    // The menus have no sprites to let go of, so HD art goes on without MCHdArtOn's.
    const bool original = MCTest::Option("hd-original").has_value();
    GHdArt = original ? 0 : 1;
    MCHdArt::SetPackFolder(pack);
    // The logistics' shapes, loaded before HD art came on, tied to the pack as the game ties them with it on.
    GlobalLog()->AttachHdArt();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    // Two seconds settle a screen change's wipe.
    constexpr int32_t SettleFrames = 30;
    // The purchase screen's tabs (MCLogistics::CurrentInvTab), and the stock its store is given to show.
    constexpr int32_t MechTab = 0;
    constexpr int32_t PilotTab = 1;
    constexpr int32_t ComponentTab = 2;
    constexpr int32_t VehicleTab = 3;
    constexpr int32_t StockShown = 2;
    // With --hd-audit-menus 1, every screen's draws are tallied as --hd-audit tallies a battle's.
    const bool audit = MCTest::Option("hd-audit-menus").has_value();
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);
    std::map<AuditKey, AuditTally> tally;
    AuditDetail detail;
    int32_t audited = 0;

    if (audit)
    {
        renderer->Audit = AuditInDetail(tally, detail);
    }

    const auto shoot = [&](const std::string& name)
    {
        AuditLabel = name;
        detail.Screen = name;

        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
            ++audited;
        }

        const std::filesystem::path path = folder / std::format("{}_{}.png", name, original ? "original" : "sharp");
        REQUIRE(MCInput::Display()->SaveShownScreenshot(path).has_value());
        std::cout << std::format("  {}: {}\n", name, path.string());
    };

    shoot("main_menu");
    ShowPreferences();
    shoot("preferences");
    CancelPrefs();
    LoadScreen();
    shoot("load");
    Cancel();
    AuditLabel = "to briefing";
    NewCampaign();
    shoot("briefing");
    GlobalLog()->BriefingScreen->SetUpMission();
    shoot("briefing_mission");
    // A screen change's slide pictured part way (its SlideShot'th present).
    constexpr int32_t SlideShot = 2;
    int32_t slidePresents = -1;
    std::string slideName;
    MCTestGame::OnPresent = [&]
    {
        if (slidePresents >= 0 && ++slidePresents == SlideShot)
        {
            const std::filesystem::path path =
                folder / std::format("{}_slide_{}.png", slideName, original ? "original" : "sharp");
            REQUIRE(MCInput::Display()->SaveShownScreenshot(path).has_value());
            std::cout << std::format("  {} slide: {}\n", slideName, path.string());
        }
    };

    const auto slide = [&](std::string name, const std::function<void()>& change)
    {
        AuditLabel = "to " + name;
        slideName = std::move(name);
        slidePresents = 0;
        change();
        slidePresents = -1;
    };

    slide("purchase", [] { GlobalLog()->SetUpPurchaseScreen(true); });
    shoot("purchase");
    // The store's vehicles, then both in stock (a new campaign's store has none: its rows show the sold-out outlines).
    GlobalLog()->CurrentInvTab = VehicleTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    shoot("purchase_vehicles");
    MCTestGame::StockStore(StockShown);
    // One of the first vehicle bought (the inventory list shows it, with its diagram).
    GlobalLog()->PurVehicleList->Vehicles.front()->Block->OnBuyConfirmed(1, 1);
    shoot("purchase_vehicles_stocked");
    GlobalLog()->CurrentInvTab = PilotTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    shoot("purchase_pilots");
    GlobalLog()->CurrentInvTab = ComponentTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    shoot("purchase_components");
    GlobalLog()->CurrentInvTab = MechTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    GlobalLog()->PurMechList->Mechs.front()->Block->OnBuyConfirmed(1, 1);
    shoot("purchase_stocked");
    slide("repair", [] { GlobalLog()->SetUpRepairScreen(true); });
    shoot("repair");
    slide("briefing", [] { GlobalLog()->SetUpBriefingScreen(true); });
    shoot("briefing_again");
    MCTestGame::OnPresent = nullptr;

    // With --hd-audit-menus or --sharp-menus-tour 1, every other screen too.
    if (audit || MCTest::Option("sharp-menus-tour").has_value())
    {
        TourScreens(shoot, detail);
    }

    // The tables are written with the pack still set: they say whether it has each picture drawn from the original.
    if (audit)
    {
        renderer->Audit = nullptr;
        AuditLabel.clear();
        ReportAudit(tally, folder / "audit", audited);
        WriteAuditTables(tally, detail, folder / "audit", audited);
    }

    GHdArt = 0;
    MCHdArt::SetPackFolder({});
}

namespace
{
    /// <summary>Where a TGA's 8-bit pixels start: after the 18-byte header and a 256-entry palette (as the game reads them).</summary>
    constexpr size_t TgaPixels = 0x312;
    /// <summary>A TGA's colour-mapped image type, and its pixel depth (the game's art is both).</summary>
    constexpr uint8_t TgaColorMapped = 1;
    constexpr uint8_t TgaDepth = 8;
    /// <summary>Where a TGA's palette starts (after the header): 256 colours of blue, green and red.</summary>
    constexpr size_t TgaPalette = 18;
    constexpr size_t TgaPaletteEntry = 3;

}

namespace
{
    /// <summary>
    /// Calls <paramref name="each"/> with every picture of the menus' and logistics' art and where it came from: the
    /// TGAs in every FastFile beside SYSTEM.CFG (in their own palettes; a TGA is shown in the palette the screen showing
    /// it loads) and the GUI's art file's GIF packets (in <paramref name="shown"/>). Counts what can't be read in
    /// <paramref name="skipped"/>.
    /// </summary>
    void ForEachPicture(
        std::span<const SDL_Color, 256> shown, int32_t& skipped,
        const std::function<void(std::string_view source, std::span<const uint8_t> pixels, int32_t width,
                                 int32_t height, std::span<const SDL_Color, 256> colors)>& each)
    {
        std::error_code error;

        // The TGAs in every FastFile beside SYSTEM.CFG.
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(MCFileSystem::GameRoot(), error))
        {
            if (!EndsWithIgnoringCase(entry.path().filename().string(), ".fst"))
            {
                continue;
            }

            // By name: the game opens a FastFile in its folder (a relative --game folder would be taken twice).
            auto archive = MCFastFile::Create(entry.path().filename().string());
            REQUIRE(archive.has_value());

            for (int32_t i = 0; i < (*archive)->GetNumFiles(); ++i)
            {
                const MCFileEntry& file = (*archive)->GetEntries()[static_cast<size_t>(i)];

                if (!EndsWithIgnoringCase(file.GetName(), ".tga") || file.RealSize <= static_cast<int32_t>(TgaPixels))
                {
                    continue;
                }

                std::vector<uint8_t> data(static_cast<size_t>(file.RealSize));

                if ((*archive)->ReadEntry(i, data) != file.RealSize || data[2] != TgaColorMapped ||
                    data[16] != TgaDepth)
                {
                    ++skipped;
                    continue;
                }

                int16_t width = 0;
                int16_t height = 0;
                std::memcpy(&width, data.data() + 0x0c, sizeof(width));
                std::memcpy(&height, data.data() + 0x0e, sizeof(height));
                const size_t size = static_cast<size_t>(std::max<int16_t>(width, 0)) * std::max<int16_t>(height, 0);

                if (size == 0 || TgaPixels + size > data.size())
                {
                    ++skipped;
                    continue;
                }

                // A TGA is shown in its own palette (the screen that shows it loads it), so its colours are drawn in that.
                std::array<SDL_Color, 256> own{};

                for (size_t c = 0; c < own.size(); ++c)
                {
                    const uint8_t* entry = data.data() + TgaPalette + c * TgaPaletteEntry;
                    own[c] = SDL_Color{entry[2], entry[1], entry[0], 255};
                }

                each(file.GetName(), std::span(data).subspan(TgaPixels, size), width, height, own);
            }
        }

        // The GUI's art file's GIF packets (packets that aren't GIFs are left out).
        MCPacketFile& art = *GuiSystem().ArtFile;

        for (int32_t packet = 0; packet < art.GetNumPackets(); ++packet)
        {
            MCFile file;

            if (art.SeekPacket(packet) != 0 || file.Open(art, static_cast<uint32_t>(art.GetPacketSize())) != 0)
            {
                ++skipped;
                continue;
            }

            std::vector<uint8_t> gif(file.FileSize());
            file.Read(gif);
            file.Close();
            const std::expected<int32_t, std::string> resolution = VfxGifResolution(gif);

            if (!resolution.has_value())
            {
                ++skipped;
                continue;
            }

            const auto dimensions = static_cast<uint32_t>(*resolution);
            MCGuiPort port;
            REQUIRE(port.Init(static_cast<int32_t>(dimensions >> 16), static_cast<int32_t>(dimensions & 0xffff)) == 0);
            REQUIRE(VfxGifDraw(port.Frame(), gif).has_value());
            const MCWindow& window = port.Bitmap();
            each(std::format("art.pak packet {}", packet), port.Pixels(), window.XMax + 1, window.YMax + 1, shown);
        }
    }
}

/// <summary>
/// Tool: <c>--hd-extract-menus &lt;folder&gt;</c> writes every picture of the menus' and logistics' art for the HD art
/// generator (tools/upscale/make_menu_pack.py): the TGAs in the install's FastFiles and the GIF packets of the GUI's art
/// file, in the logistics palette, each named by its pixels' hash, with a manifest of where each came from.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-menus writes the menus' art for the HD art generator")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-menus");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);
    std::ofstream manifest(folder / "manifest.tsv");
    std::set<uint64_t> written;
    int32_t pictures = 0;
    int32_t skipped = 0;
    ForEachPicture(colors, skipped,
                   [&](std::string_view source, std::span<const uint8_t> pixels, int32_t width, int32_t height,
                       std::span<const SDL_Color, 256> pictureColors)
                   {
                       WritePicture(folder, manifest, source, pixels, width, height, pictureColors, written);
                       ++pictures;
                   });

    std::cout << std::format("  {} pictures ({} distinct) in {}, {} skipped\n", pictures, written.size(),
                             folder.string(), skipped);
    CHECK(!written.empty());
}

/// <summary>
/// With HD art on, the menus' screen is drawn at the window's resolution (a sharp screen, two drawn pixels a screen
/// pixel in a 1280x960 window), and what it shows in palette indices (what the tests, the fog test and mirror mode
/// read) is what the screen shows without.
/// </summary>
TEST_CASE_ISOLATED("game: a sharp screen draws the menus at the window's resolution and keeps their indices")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
    constexpr int32_t Width = 1280;
    constexpr int32_t Height = 960;
    constexpr int32_t SettleFrames = 30;
    REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), Width, Height));
    SDL_SyncWindow(&MCInput::Display()->Window());

    const auto settle = []
    {
        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    };

    settle();
    const std::vector<uint8_t> plain = MCInput::Display()->ComposeScreen();
    CHECK(!MCInput::Display()->SharpScreen());

    GHdArt = 1;
    settle();
    const std::shared_ptr<MCVulkanRenderer> renderer =
        std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);
    const auto [scaleX, scaleY] = renderer->SurfaceScale(MCInput::Display()->Screen());
    const std::vector<uint8_t> sharp = MCInput::Display()->ComposeScreen();
    GHdArt = 0;

    constexpr float Tolerance = 0.01f;
    CHECK(std::abs(scaleX - 2.0f) < Tolerance);
    CHECK(std::abs(scaleY - 2.0f) < Tolerance);
    REQUIRE_EQ(sharp.size(), plain.size());
    CHECK(sharp == plain);
}

namespace
{
    /// <summary>
    /// A glyph's redrawn coverage at (x, y) in its redrawn pixels, between their centres, as shaders/draw.pshader takes
    /// it: none past its border, or, for a layer that runs on (<paramref name="runsOn"/>), its border's.
    /// </summary>
    float CoverageBetween(std::span<const uint8_t> redrawn, int32_t width, int32_t height, bool runsOn, float x,
                          float y)
    {
        const auto texel = [&](int32_t i, int32_t j)
        {
            if (!runsOn && (i < 0 || j < 0 || i >= width || j >= height))
            {
                return 0.0f;
            }

            const size_t at = static_cast<size_t>(std::clamp(j, 0, height - 1)) * static_cast<size_t>(width) +
                              static_cast<size_t>(std::clamp(i, 0, width - 1));
            return static_cast<float>(redrawn[at * BytesPerPixel + 1]) / 255.0f;
        };

        const auto i = static_cast<int32_t>(std::floor(x));
        const auto j = static_cast<int32_t>(std::floor(y));
        const float fx = x - static_cast<float>(i);
        const float fy = y - static_cast<float>(j);
        const float top = std::lerp(texel(i, j), texel(i + 1, j), fx);
        const float bottom = std::lerp(texel(i, j + 1), texel(i + 1, j + 1), fx);
        return std::lerp(top, bottom, fy);
    }
}

/// <summary>
/// On a sharp screen, two-tone text (yelldrp.fnt, the logistics screens' yellow figures, a body and its drop shadow) is
/// shown from its layers: each pixel of the window is the glyphs' layers (MCSharpText::RedrawLayers, the whole glyph
/// where a pane cuts it) sampled where it lies, each over the one behind it; the indices stay the original's.
/// </summary>
TEST_CASE_ISOLATED("game: a sharp screen shows two-tone text from its layers and keeps its indices")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
    REQUIRE(YellowDropFont != nullptr);
    REQUIRE(!YellowDropFont->FontData.Empty());
    constexpr int32_t Width = 2560;
    constexpr int32_t Height = 1920;
    constexpr int32_t SettleFrames = 30;
    REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), Width, Height));
    SDL_SyncWindow(&MCInput::Display()->Window());

    const auto settle = []
    {
        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    };

    // A box of one colour (neither of the font's, in the screen's settled palette), the text in it, its first letter
    // cut by the box's left side.
    settle();
    const std::span<const uint8_t> font = YellowDropFont->FontData.Bytes();
    const MCSharpText::MCFontLayers fontLayers = MCSharpText::LayersOf(font);
    REQUIRE(fontLayers.Shadow.has_value());
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const auto background =
        static_cast<uint8_t>(std::ranges::find_if(colors,
                                                  [&](const SDL_Color& color)
                                                  {
                                                      return !SameColor(color, colors[fontLayers.Shadow->Body]) &&
                                                             !SameColor(color, colors[fontLayers.Shadow->Shadow]);
                                                  }) -
                             colors.begin());
    const std::string_view text = "A25 Tons AVKWxy47";
    constexpr int32_t Cut = 2;
    constexpr int32_t Margin = 3;
    const int32_t fontHeight = VfxFontHeight(font);
    const MCPane& screen = ScreenPort()->Frame();
    const MCPane box{screen.Window, screen.X0 + 40, screen.Y0 + 40,
                     screen.X0 + 40 + YellowDropFont->Width(text) - Cut + Margin,
                     screen.Y0 + 40 + fontHeight + 2 * Margin};

    const auto drawn = [&]
    {
        settle();
        VfxPaneWipe(box, background);
        YellowDropFont->WriteString(box, -Cut, Margin, text);
        return MCInput::Display()->ComposeScreen();
    };

    const std::vector<uint8_t> plain = drawn();
    GHdArt = 1;
    const std::vector<uint8_t> sharp = drawn();
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_sharp_two_tone";
    std::filesystem::create_directories(scratch);
    const Shown shown = ReadShown(scratch);
    GHdArt = 0;
    std::filesystem::remove_all(scratch);
    CHECK(sharp == plain);

    // Each glyph's place in the window and its layers (as a one-colour glyph, the space, its one).
    struct Placed
    {
        int32_t X = 0;
        int32_t Columns = 0;
        std::vector<std::vector<uint8_t>> Layers;
        MCSharpText::MCGlyphLayers How;
    };

    std::vector<Placed> glyphs;
    int32_t x = box.X0 - Cut;

    for (const char c : text)
    {
        const auto character = static_cast<uint8_t>(c);
        const int32_t columns = VfxCharacterWidth(font, character);
        const auto offset = static_cast<size_t>(static_cast<uint32_t>(MCVfxRead32(font, 0x10 + character * 4))) + 4;
        const std::span<const uint8_t> glyph =
            font.subspan(offset, static_cast<size_t>(columns) * static_cast<size_t>(fontHeight));
        Placed placed{x, columns, {}, {}};

        if (MCSharpText::Layered(glyph, columns, columns, fontHeight))
        {
            placed.How = MCSharpText::LayersOf(glyph, columns, columns, fontHeight, fontLayers);
            placed.Layers = MCSharpText::RedrawLayers(glyph, columns, columns, fontHeight, placed.How);
        }
        else
        {
            placed.Layers.push_back(MCSharpText::Redraw(glyph, columns, columns, fontHeight));
        }

        glyphs.push_back(std::move(placed));
        x += columns;
    }

    // Every shown pixel inside the box, against the layers sampled as shaders/draw.pshader's KindSharpGlyph does.
    constexpr int32_t Scale = MCSharpText::Scale;
    constexpr int32_t Tolerance = 3;
    const float perX = static_cast<float>(MCInput::Display()->Width()) / static_cast<float>(shown.Width);
    const float perY = static_cast<float>(MCInput::Display()->Height()) / static_cast<float>(shown.Height);
    int32_t worst = 0;
    int64_t compared = 0;
    int64_t covered = 0;

    for (int32_t oy = 0; oy < shown.Height; ++oy)
    {
        const float wy = (static_cast<float>(oy) + 0.5f) * perY;

        for (int32_t ox = 0; ox < shown.Width; ++ox)
        {
            const float wx = (static_cast<float>(ox) + 0.5f) * perX;

            if (wx < static_cast<float>(box.X0) || wx >= static_cast<float>(box.X1 + 1) ||
                wy < static_cast<float>(box.Y0) || wy >= static_cast<float>(box.Y1 + 1))
            {
                continue;
            }

            std::array<float, 3> expected = {static_cast<float>(colors[background].r),
                                             static_cast<float>(colors[background].g),
                                             static_cast<float>(colors[background].b)};
            const float gy = wy - static_cast<float>(box.Y0 + Margin);

            for (const Placed& glyph : glyphs)
            {
                const float gx = wx - static_cast<float>(glyph.X);

                if (gx < 0.0f || gx >= static_cast<float>(glyph.Columns) || gy < 0.0f ||
                    gy >= static_cast<float>(fontHeight))
                {
                    continue;
                }

                for (size_t place = 0; place < glyph.Layers.size(); ++place)
                {
                    const std::vector<uint8_t>& layer = glyph.Layers[place];
                    const int32_t width = glyph.Columns * Scale;
                    const int32_t height = fontHeight * Scale;
                    const float coverage = CoverageBetween(layer, width, height, glyph.How.RunsOn(place),
                                                           gx * Scale - 0.5f, gy * Scale - 0.5f);
                    const size_t nearest =
                        static_cast<size_t>(std::clamp(static_cast<int32_t>(gy * Scale), 0, height - 1)) *
                            static_cast<size_t>(width) +
                        static_cast<size_t>(std::clamp(static_cast<int32_t>(gx * Scale), 0, width - 1));
                    const SDL_Color& color = colors[layer[nearest * BytesPerPixel]];
                    covered += coverage > 0.0f ? 1 : 0;
                    expected = {std::lerp(expected[0], static_cast<float>(color.r), coverage),
                                std::lerp(expected[1], static_cast<float>(color.g), coverage),
                                std::lerp(expected[2], static_cast<float>(color.b), coverage)};
                }
            }

            const uint8_t* pixel =
                &shown.Pixels[(static_cast<size_t>(oy) * static_cast<size_t>(shown.Width) + ox) * BytesPerPixel];

            for (size_t channel = 0; channel < expected.size(); ++channel)
            {
                worst = std::max(worst, std::abs(static_cast<int32_t>(pixel[channel]) -
                                                 static_cast<int32_t>(std::lround(expected[channel]))));
            }

            ++compared;
        }
    }

    std::cout << std::format("  {} shown pixels compared ({} under a layer), farthest {} from the layers\n", compared,
                             covered, worst);
    CHECK(compared > 0);
    CHECK(covered > 0);
    CHECK(worst <= Tolerance);
}

namespace
{
    /// <summary>A redrawn glyph's half coverage: a pixel brought back to the glyph's size is ink from here on.</summary>
    constexpr uint8_t HalfCoverage = 128;
    /// <summary>How far a covered redrawn pixel's centre may lie from an ink pixel, in glyph pixels: half a pixel, and
    /// the smoothing's half a redrawn pixel.</summary>
    constexpr float StrokeReach = 0.5f + 0.5f / static_cast<float>(MCSharpText::Scale) + 1e-4f;

    /// <summary>The byte and coverage of a redrawn glyph's pixel at (x, y) in glyph pixels (MCSharpText's RGBA8).</summary>
    std::pair<uint8_t, uint8_t> RedrawnAt(std::span<const uint8_t> redrawn, int32_t columns, float x, float y)
    {
        constexpr int32_t Scale = MCSharpText::Scale;
        const auto column = static_cast<size_t>(x * Scale);
        const auto row = static_cast<size_t>(y * Scale);
        const size_t offset = (row * static_cast<size_t>(columns * Scale) + column) * BytesPerPixel;
        return {redrawn[offset], redrawn[offset + 1]};
    }

    /// <summary>
    /// A glyph's layers brought back to its size: at each pixel's centre, the byte of the topmost layer covering it at
    /// least half; no ink where none does.
    /// </summary>
    std::vector<uint8_t> ReadBack(const std::vector<std::vector<uint8_t>>& layers, int32_t columns, int32_t rows)
    {
        std::vector<uint8_t> bytes(static_cast<size_t>(columns) * static_cast<size_t>(rows), MCSharpText::NoInk);

        for (int32_t y = 0; y < rows; ++y)
        {
            for (int32_t x = 0; x < columns; ++x)
            {
                for (const std::vector<uint8_t>& layer : layers)
                {
                    const auto [byte, coverage] =
                        RedrawnAt(layer, columns, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);

                    if (coverage >= HalfCoverage)
                    {
                        bytes[static_cast<size_t>(y) * static_cast<size_t>(columns) + x] = byte;
                    }
                }
            }
        }

        return bytes;
    }

    /// <summary>The covered redrawn pixel farthest from the glyph's ink, and how far, in glyph pixels.</summary>
    struct Farthest
    {
        float Distance = 0.0f;
        float X = 0.0f;
        float Y = 0.0f;
    };

    /// <summary>
    /// The covered pixel of <paramref name="redrawn"/> (the glyph <paramref name="glyph"/>, <paramref name="columns"/> x
    /// <paramref name="rows"/>, redrawn) that lies farthest from the glyph's ink pixels.
    /// </summary>
    Farthest FarthestCover(std::span<const uint8_t> redrawn, std::span<const uint8_t> glyph, int32_t columns,
                           int32_t rows)
    {
        constexpr int32_t Scale = MCSharpText::Scale;
        const auto ink = [&](int32_t x, int32_t y)
        {
            return x >= 0 && y >= 0 && x < columns && y < rows &&
                   glyph[static_cast<size_t>(y) * static_cast<size_t>(columns) + x] != MCSharpText::NoInk;
        };

        Farthest farthest;

        for (int32_t j = 0; j < rows * Scale; ++j)
        {
            for (int32_t i = 0; i < columns * Scale; ++i)
            {
                if (redrawn[(static_cast<size_t>(j) * static_cast<size_t>(columns * Scale) + i) * BytesPerPixel + 1] ==
                    0)
                {
                    continue;
                }

                const float px = (static_cast<float>(i) + 0.5f) / Scale;
                const float py = (static_cast<float>(j) + 0.5f) / Scale;
                float nearest = std::numeric_limits<float>::max();

                for (int32_t y = static_cast<int32_t>(py) - 1; y <= static_cast<int32_t>(py) + 1; ++y)
                {
                    for (int32_t x = static_cast<int32_t>(px) - 1; x <= static_cast<int32_t>(px) + 1; ++x)
                    {
                        if (ink(x, y))
                        {
                            const float dx = std::max(std::abs(px - (static_cast<float>(x) + 0.5f)) - 0.5f, 0.0f);
                            const float dy = std::max(std::abs(py - (static_cast<float>(y) + 0.5f)) - 0.5f, 0.0f);
                            nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy));
                        }
                    }
                }

                if (nearest > farthest.Distance)
                {
                    farthest = {nearest, px, py};
                }
            }
        }

        return farthest;
    }

    /// <summary>
    /// A VFX font (vfx/MCVfxFunctions.h's fonts) of <paramref name="glyphs"/>, characters 0 on, each its width and its
    /// pixels, <paramref name="height"/> rows of them.
    /// </summary>
    std::vector<uint8_t> MakeFont(int32_t height, const std::vector<std::pair<int32_t, std::vector<uint8_t>>>& glyphs)
    {
        constexpr size_t CountAt = 4;
        constexpr size_t HeightAt = 8;
        constexpr size_t TableAt = 0x10;
        constexpr size_t Word = 4;
        std::vector<uint8_t> font(TableAt + glyphs.size() * Word, 0);

        const auto put = [&font](size_t at, int32_t value) { std::memcpy(font.data() + at, &value, Word); };
        put(CountAt, static_cast<int32_t>(glyphs.size()));
        put(HeightAt, height);

        for (size_t character = 0; character < glyphs.size(); ++character)
        {
            put(TableAt + character * Word, static_cast<int32_t>(font.size()));
            font.resize(font.size() + Word);
            put(font.size() - Word, glyphs[character].first);
            font.insert(font.end(), glyphs[character].second.begin(), glyphs[character].second.end());
        }

        return font;
    }
}

/// <summary>
/// A two-tone glyph, a body with its drop shadow one pixel down and one down and right (yelldrp.fnt's), is redrawn as
/// two layers: the body's strokes, over the same strokes swept between the shadow's offsets. Brought back to the
/// glyph's size it is the glyph; the shadow lies under the body's diagonal, never above it.
/// </summary>
TEST_CASE("sharp text: a body and its drop shadow are redrawn as two layers, the shadow swept under the body")
{
    constexpr int32_t Columns = 4;
    constexpr int32_t Rows = 4;
    constexpr uint8_t Body = 161;
    constexpr uint8_t Shade = 16;
    constexpr uint8_t No = MCSharpText::NoInk;
    // A diagonal from (0, 0) to (2, 2), its shadow under it.
    const std::vector<uint8_t> glyph = {Body,  No,    No,    No, //
                                        Shade, Body,  No,    No, //
                                        No,    Shade, Body,  No, //
                                        No,    No,    Shade, Shade};
    const std::vector<uint8_t> font = MakeFont(Rows, {{Columns, glyph}});
    const MCSharpText::MCFontLayers fontLayers = MCSharpText::LayersOf(font);
    REQUIRE(fontLayers.Shadow.has_value());
    CHECK_EQ(fontLayers.Shadow->Body, Body);
    CHECK_EQ(fontLayers.Shadow->Shadow, Shade);
    CHECK(MCSharpText::Layered(glyph, Columns, Columns, Rows));

    const MCSharpText::MCGlyphLayers layers = MCSharpText::LayersOf(glyph, Columns, Columns, Rows, fontLayers);
    const std::vector<uint8_t> backToFront = {Shade, Body};
    CHECK(layers.Bytes == backToFront);
    CHECK(layers.Shadow.has_value());
    const std::vector<std::vector<uint8_t>> redrawn = MCSharpText::RedrawLayers(glyph, Columns, Columns, Rows, layers);
    REQUIRE_EQ(redrawn.size(), size_t{2});
    CHECK(ReadBack(redrawn, Columns, Rows) == glyph);

    const std::vector<uint8_t>& shadow = redrawn[0];
    const std::vector<uint8_t>& body = redrawn[1];
    constexpr uint8_t Whole = 255;
    CHECK_EQ(RedrawnAt(body, Columns, 1.0f, 1.0f).first, Body);
    CHECK_EQ(RedrawnAt(body, Columns, 1.0f, 1.0f).second, Whole);
    // Under the diagonal, the shadow (the diagonal moved down, and swept right): not the body.
    CHECK_EQ(RedrawnAt(shadow, Columns, 1.0f, 2.0f).first, Shade);
    CHECK_EQ(RedrawnAt(shadow, Columns, 1.0f, 2.0f).second, Whole);
    CHECK_EQ(RedrawnAt(shadow, Columns, 0.6f, 1.8f).second, Whole);
    CHECK_EQ(RedrawnAt(body, Columns, 0.6f, 1.8f).second, uint8_t{0});
    // Above it, neither.
    CHECK_EQ(RedrawnAt(shadow, Columns, 1.4f, 0.6f).second, uint8_t{0});
    CHECK_EQ(RedrawnAt(body, Columns, 1.6f, 0.4f).second, uint8_t{0});
    CHECK(FarthestCover(shadow, glyph, Columns, Rows).Distance <= StrokeReach);
}

/// <summary>
/// A letter on a box (dim.fnt's, the small lblue.fnt's) is redrawn as the letter's strokes over the box, the box's
/// edges on the glyph's border carried on past it: its corners are square, so boxes side by side join, and it fills
/// the corners between the letter's strokes and itself.
/// </summary>
TEST_CASE("sharp text: a letter on a box is redrawn over the box, which keeps its square corners")
{
    constexpr int32_t Columns = 3;
    constexpr int32_t Rows = 3;
    constexpr uint8_t Letter = 227;
    constexpr uint8_t Box = 31;
    // A box with a diagonal through it: the box has more pixels, so it lies behind.
    const std::vector<uint8_t> glyph = {Letter, Box, Box, Box, Letter, Box, Box, Box, Letter};
    const std::vector<uint8_t> font = MakeFont(Rows, {{Columns, glyph}});
    const MCSharpText::MCFontLayers fontLayers = MCSharpText::LayersOf(font);
    CHECK(!fontLayers.Shadow.has_value());

    const MCSharpText::MCGlyphLayers layers = MCSharpText::LayersOf(glyph, Columns, Columns, Rows, fontLayers);
    const std::vector<uint8_t> backToFront = {Box, Letter};
    REQUIRE(layers.Bytes == backToFront);
    const std::vector<std::vector<uint8_t>> redrawn = MCSharpText::RedrawLayers(glyph, Columns, Columns, Rows, layers);
    REQUIRE_EQ(redrawn.size(), size_t{2});
    CHECK(ReadBack(redrawn, Columns, Rows) == glyph);

    const std::vector<uint8_t>& box = redrawn[0];
    const std::vector<uint8_t>& letter = redrawn[1];
    // The box's corner pixels, at the glyph's corners, are whole (one-colour strokes would round them).
    constexpr uint8_t Whole = 255;
    CHECK_EQ(RedrawnAt(box, Columns, 2.97f, 0.03f).second, Whole);
    CHECK_EQ(RedrawnAt(box, Columns, 0.03f, 2.97f).second, Whole);
    // Between the box and the diagonal, the box (its strokes join the letter's pixels); on it, the letter, whose own
    // strokes are a one-colour glyph's.
    CHECK_EQ(RedrawnAt(box, Columns, 1.95f, 1.0f).second, Whole);
    CHECK_EQ(RedrawnAt(letter, Columns, 1.95f, 1.0f).second, uint8_t{0});
    CHECK_EQ(RedrawnAt(letter, Columns, 1.0f, 1.0f).second, Whole);
    CHECK_EQ(RedrawnAt(letter, Columns, 2.5f, 0.5f).second, uint8_t{0});
}

/// <summary>
/// A glyph redrawn sharp covers its ink pixels' centres fully, keeps each one's font byte, joins diagonal neighbours
/// with a straight stroke (the middle of a diagonal pair is covered), and leaves the background far from the ink empty.
/// </summary>
TEST_CASE("sharp text: a glyph redrawn as strokes covers its ink and keeps its bytes")
{
    constexpr int32_t Columns = 4;
    constexpr int32_t Rows = 3;
    constexpr uint8_t Ink = 7;
    constexpr uint8_t Shade = 9;
    constexpr uint8_t No = MCSharpText::NoInk;
    // A diagonal from (0, 0) to (2, 2), and a lone shaded pixel at (3, 0).
    const std::array<uint8_t, Columns * Rows> glyph = {Ink, No, No, Shade, No, Ink, No, No, No, No, Ink, No};
    const std::vector<uint8_t> redrawn = MCSharpText::Redraw(glyph, Columns, Columns, Rows);
    constexpr int32_t Scale = MCSharpText::Scale;
    REQUIRE_EQ(redrawn.size(), static_cast<size_t>(Columns * Scale * Rows * Scale * 4));

    // The byte and coverage of the redrawn pixel at (x, y) in glyph pixels.
    const auto at = [&](float x, float y)
    {
        const auto column = static_cast<size_t>(x * Scale);
        const auto row = static_cast<size_t>(y * Scale);
        const size_t offset = (row * Columns * Scale + column) * 4;
        return std::pair{redrawn[offset], redrawn[offset + 1]};
    };

    CHECK_EQ(at(0.5f, 0.5f).first, Ink);
    CHECK_EQ(at(0.5f, 0.5f).second, uint8_t{255});
    CHECK_EQ(at(3.5f, 0.5f).first, Shade);
    CHECK_EQ(at(3.5f, 0.5f).second, uint8_t{255});
    // Halfway along the diagonal, between (0, 0) and (1, 1).
    CHECK_EQ(at(1.0f, 1.0f).second, uint8_t{255});
    // The corner far from any ink.
    CHECK_EQ(at(0.1f, 2.9f).second, uint8_t{0});
}

namespace
{
    /// <summary>A font of the install's (a FastFile's .fnt): its name and bytes.</summary>
    struct MCGameFont
    {
        std::string Name;
        std::vector<uint8_t> Data;
    };

    /// <summary>Every font in the install's FastFiles that the game can draw (VfxFontFits), each once by name.</summary>
    std::vector<MCGameFont> GameFonts()
    {
        std::vector<MCGameFont> fonts;
        std::set<std::string> names;
        std::error_code error;

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(MCFileSystem::GameRoot(), error))
        {
            if (!EndsWithIgnoringCase(entry.path().filename().string(), ".fst"))
            {
                continue;
            }

            // By name: the game opens a FastFile in its folder (a relative --game folder would be taken twice).
            auto archive = MCFastFile::Create(entry.path().filename().string());
            REQUIRE(archive.has_value());

            for (int32_t i = 0; i < (*archive)->GetNumFiles(); ++i)
            {
                const MCFileEntry& file = (*archive)->GetEntries()[static_cast<size_t>(i)];
                std::string name(file.GetName().substr(file.GetName().find_last_of('\\') + 1));
                std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });

                if (!EndsWithIgnoringCase(name, ".fnt") || !names.insert(name).second)
                {
                    continue;
                }

                std::vector<uint8_t> data(static_cast<size_t>(file.RealSize));

                if ((*archive)->ReadEntry(i, data) == file.RealSize && VfxFontFits(data))
                {
                    fonts.push_back({std::move(name), std::move(data)});
                }
            }
        }

        return fonts;
    }

    /// <summary>A glyph of a font: its width, and its rows (the font's height of them), <c>width</c> bytes each.</summary>
    std::span<const uint8_t> GlyphOf(std::span<const uint8_t> font, int32_t character, int32_t& width)
    {
        const auto offset = static_cast<size_t>(static_cast<uint32_t>(MCVfxRead32(font, 0x10 + character * 4)));
        width = MCVfxRead32(font, offset);
        return font.subspan(offset + 4, static_cast<size_t>(width) * static_cast<size_t>(VfxFontHeight(font)));
    }
}

/// <summary>
/// Every glyph of every font in the install, redrawn as strokes (what a sharp screen shows; a two-tone glyph as a layer
/// per byte), reads as the original: brought back down to the font's size (each glyph pixel's centre) it is the glyph,
/// ink and bytes, and no stroke reaches farther than half a glyph pixel (and the edge's smoothing) from an ink pixel.
/// The two drop-shadow fonts are taken as such.
/// </summary>
TEST_CASE_ISOLATED("game: every font's glyphs redrawn sharp read as the originals")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::vector<MCGameFont> fonts = GameFonts();
    REQUIRE(!fonts.empty());
    int64_t glyphs = 0;
    int64_t layered = 0;
    int64_t wrong = 0;
    std::string first;
    std::set<std::string> twoTone;
    std::set<std::string> shadowed;

    const auto fail = [&](const MCGameFont& font, int32_t character, std::string_view what)
    {
        if (wrong++ == 0)
        {
            first = std::format("{} glyph {}: {}", font.Name, character, what);
        }
    };

    for (const MCGameFont& font : fonts)
    {
        const int32_t height = VfxFontHeight(font.Data);
        const MCSharpText::MCFontLayers fontLayers = MCSharpText::LayersOf(font.Data);

        if (fontLayers.Shadow.has_value())
        {
            shadowed.insert(font.Name);
        }

        for (int32_t character = 0; character < MCVfxRead32(font.Data, 4); ++character)
        {
            int32_t width = 0;
            const std::span<const uint8_t> glyph = GlyphOf(font.Data, character, width);

            if (width <= 0 || height <= 0)
            {
                continue;
            }

            // As a sharp screen draws it: a two-tone glyph a layer per byte.
            ++glyphs;
            std::vector<std::vector<uint8_t>> redrawn;

            if (MCSharpText::Layered(glyph, width, width, height))
            {
                ++layered;
                twoTone.insert(font.Name);
                const MCSharpText::MCGlyphLayers layers =
                    MCSharpText::LayersOf(glyph, width, width, height, fontLayers);
                redrawn = MCSharpText::RedrawLayers(glyph, width, width, height, layers);

                if (redrawn.size() != layers.Bytes.size())
                {
                    fail(font, character, std::format("{} layers for {} bytes", redrawn.size(), layers.Bytes.size()));
                    continue;
                }
            }
            else
            {
                redrawn.push_back(MCSharpText::Redraw(glyph, width, width, height));
            }

            const std::vector<uint8_t> back = ReadBack(redrawn, width, height);

            if (const auto differs = std::ranges::mismatch(back, glyph); differs.in1 != back.end())
            {
                const auto at = static_cast<int32_t>(differs.in1 - back.begin());
                fail(font, character,
                     std::format("pixel ({}, {}) comes back {:#x}, not {:#x}", at % width, at / width, *differs.in1,
                                 *differs.in2));
            }

            for (const std::vector<uint8_t>& layer : redrawn)
            {
                if (const Farthest farthest = FarthestCover(layer, glyph, width, height);
                    farthest.Distance > StrokeReach)
                {
                    fail(font, character,
                         std::format("stroke at ({:.2f}, {:.2f}) is {:.2f} from the ink", farthest.X, farthest.Y,
                                     farthest.Distance));
                }
            }
        }
    }

    const auto names = [](const std::set<std::string>& set)
    {
        std::string joined;

        for (const std::string& name : set)
        {
            joined += std::format("{}{}", joined.empty() ? "" : " ", name);
        }

        return joined;
    };

    std::cout << std::format("  {} fonts, {} glyphs ({} two-tone), {} wrong{}{}\n  two-tone: {}\n  drop shadows: {}\n",
                             fonts.size(), glyphs, layered, wrong, wrong > 0 ? ", first: " : "", first, names(twoTone),
                             names(shadowed));
    CHECK_EQ(wrong, int64_t{0});
    // The logistics screens' yellow and blue text: a body and its shadow.
    CHECK(shadowed.contains("yelldrp.fnt"));
    CHECK(shadowed.contains("bluedrp.fnt"));
    CHECK(!shadowed.contains("dim.fnt"));
}

/// <summary>
/// Tool: <c>--sharp-fonts &lt;folder&gt;</c> writes every font's glyph set as <c>&lt;font&gt;.png</c>, each glyph's
/// original (scaled up nearest) over its sharp redraw, so every letter can be checked by eye; a two-tone glyph's bytes
/// in greys, its back byte darkest.
/// </summary>
TEST_CASE_ISOLATED("tool: --sharp-fonts pictures every font's glyphs, original over sharp")
{
    const std::optional<std::string_view> out = MCTest::Option("sharp-fonts");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t Scale = MCSharpText::Scale;
    constexpr int32_t PerRow = 16;
    constexpr int32_t Gap = 4;
    constexpr uint8_t Ground = 24;
    /// The backmost byte's grey in a two-tone glyph.
    constexpr int32_t BackShade = 96;
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);

    for (const MCGameFont& font : GameFonts())
    {
        const int32_t height = VfxFontHeight(font.Data);
        const int32_t count = MCVfxRead32(font.Data, 4);
        const MCSharpText::MCFontLayers fontLayers = MCSharpText::LayersOf(font.Data);
        int32_t widest = 1;

        for (int32_t character = 0; character < count; ++character)
        {
            int32_t width = 0;
            GlyphOf(font.Data, character, width);
            widest = std::max(widest, width);
        }

        const int32_t cellWidth = widest * Scale + Gap;
        const int32_t cellHeight = 2 * height * Scale + 3 * Gap;
        const int32_t rows = (count + PerRow - 1) / PerRow;
        const int32_t pictureWidth = PerRow * cellWidth;
        const int32_t pictureHeight = std::max(rows, 1) * cellHeight;
        std::vector<uint8_t> rgba(
            static_cast<size_t>(pictureWidth) * static_cast<size_t>(pictureHeight) * BytesPerPixel, Ground);

        const auto put = [&](int32_t x, int32_t y, uint8_t level)
        {
            uint8_t* pixel = &rgba[(static_cast<size_t>(y) * static_cast<size_t>(pictureWidth) + x) * BytesPerPixel];
            pixel[0] = pixel[1] = pixel[2] = static_cast<uint8_t>(Ground + (255 - Ground) * level / 255);
            pixel[3] = 255;
        };

        for (int32_t character = 0; character < count; ++character)
        {
            int32_t width = 0;
            const std::span<const uint8_t> glyph = GlyphOf(font.Data, character, width);

            if (width <= 0 || height <= 0)
            {
                continue;
            }

            // A two-tone glyph as a sharp screen draws it, a layer per byte, its bytes grey to white back to front.
            std::vector<std::vector<uint8_t>> redrawn;
            std::vector<uint8_t> bytes;

            if (MCSharpText::Layered(glyph, width, width, height))
            {
                const MCSharpText::MCGlyphLayers layers =
                    MCSharpText::LayersOf(glyph, width, width, height, fontLayers);
                redrawn = MCSharpText::RedrawLayers(glyph, width, width, height, layers);
                bytes = layers.Bytes;
            }
            else
            {
                redrawn.push_back(MCSharpText::Redraw(glyph, width, width, height));
            }

            const auto shade = [&](size_t place)
            {
                return bytes.size() < 2 ? 255
                                        : BackShade + (255 - BackShade) * static_cast<int32_t>(place) /
                                                          static_cast<int32_t>(bytes.size() - 1);
            };

            const int32_t left = (character % PerRow) * cellWidth;
            const int32_t top = (character / PerRow) * cellHeight;

            for (int32_t j = 0; j < height * Scale; ++j)
            {
                for (int32_t i = 0; i < width * Scale; ++i)
                {
                    const uint8_t byte = glyph[static_cast<size_t>(j / Scale) * static_cast<size_t>(width) + i / Scale];
                    const auto place = static_cast<size_t>(std::ranges::find(bytes, byte) - bytes.begin());
                    put(left + i, top + Gap + j, byte == MCSharpText::NoInk ? 0 : shade(place));
                    int32_t level = 0;

                    for (size_t layer = 0; layer < redrawn.size(); ++layer)
                    {
                        const int32_t coverage =
                            redrawn[layer][(static_cast<size_t>(j) * static_cast<size_t>(width * Scale) + i) * 4 + 1];
                        level = (level * (255 - coverage) + shade(layer) * coverage) / 255;
                    }

                    put(left + i, top + 2 * Gap + height * Scale + j, level);
                }
            }
        }

        SavePng(folder / std::format("{}.png", font.Name), pictureWidth, pictureHeight, rgba);
    }
}

namespace
{

    /// <summary>
    /// The player's lance selected (its brackets and bars drawn) and ordered to attack <paramref name="target"/>, the
    /// camera on the player's mech.
    /// </summary>
    void SendLanceAt(const std::shared_ptr<MCMover>& target)
    {
        constexpr int32_t LanceSize = 3;
        const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
        REQUIRE(iface != nullptr);
        iface->ClearMechSelection();

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons)
        {
            if (button->Active)
            {
                iface->SelectMech(button->PartId);
            }
        }

        for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + LanceSize; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->IsDisabled() != 0)
            {
                continue;
            }

            MCTacticalOrder order;
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackObject, 0);
            order.Target = target;
            order.AttackParams.Type = 1;
            order.AttackParams.Method = 0;
            order.AttackParams.Range = -1;
            order.AttackParams.Pursue = -1;
            mover->HandleTacticalOrder(order, 1, 0);
        }

        Eye()->ChangeTarget(GetMoverFromPartId(PlayerMechPartId), true);
    }

    /// <summary>
    /// Mission 1's battle, as the overlay tests play it: the player's lance selected (its brackets and bars drawn) and
    /// sent at the Uller (shots, explosions, craters, footprints), the camera following the lance as a player's would
    /// (the Uller starts in the fog). Returns the Uller.
    /// </summary>
    std::shared_ptr<MCMover> SendLanceAtUller()
    {
        constexpr int32_t UllerPartId = 896;
        const std::shared_ptr<MCMover> uller = GetMoverFromPartId(UllerPartId);
        REQUIRE(uller != nullptr);
        SendLanceAt(uller);
        return uller;
    }

    /// <summary>
    /// What drew <paramref name="data"/>, where the audit can tell: a terrain overlay (a fast shape: roads, rivers...), a
    /// crater shape, a cursor, the pack folder an attached shape's frames come from, the one a shape table's hash names,
    /// or none.
    /// </summary>
    std::string AuditOwner(std::span<const uint8_t> data)
    {
        constexpr std::array<uint8_t, 4> FastShapeTag = {'D', 'N', 'A', 'H'};

        if (data.empty())
        {
            return "-";
        }

        if (data.size() >= FastShapeTag.size() && std::ranges::equal(data.first(FastShapeTag.size()), FastShapeTag))
        {
            return "terrain overlay";
        }

        // The menus have no crater manager.
        const std::span<const std::vector<uint8_t>> craters = MCGameContext::Current().HasCraterManager()
                                                                  ? CraterManager().Shapes()
                                                                  : std::span<const std::vector<uint8_t>>();

        for (size_t i = 0; i < craters.size(); ++i)
        {
            if (!craters[i].empty() && craters[i].data() == data.data())
            {
                return std::format("crater shape {}", i);
            }
        }

        constexpr int32_t CursorSlots = 128;

        for (int32_t cursor = 0; cursor < CursorSlots; ++cursor)
        {
            if (const std::span<const uint8_t> shape = GuiSystem().CursorShapeData(cursor);
                !shape.empty() && shape.data() == data.data())
            {
                return std::format("cursor {}", cursor);
            }
        }

        if (const std::optional<std::filesystem::path> folder = MCHdArt::AttachedFolder(data.data());
            folder.has_value())
        {
            return std::filesystem::relative(*folder, MCHdArt::PackFolder()).generic_string();
        }

        // The data is the table to its block's end: the bytes MCVulkanRenderer::HdShape hashes.
        if (VfxIsShapeTable(data))
        {
            return MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(data)).generic_string() + " (by hash)";
        }

        return "?";
    }
}

/// <summary>
/// Tool: <c>--hd-audit &lt;pack&gt; --shots &lt;folder&gt;</c> lists what is still drawn from the original's pixels
/// with HD art on: mission 1 drawn by the GPU as --hd-pack's pictures are, the player's lance selected and sent at the
/// Uller for <c>--hd-audit-frames &lt;n&gt;</c> frames (default 900, a minute) with the camera following it, every draw command
/// tallied by what it drew and
/// whether it was sharp (from the pack, the ground's HD atlas or text redrawn sharp). Prints each command's share of
/// sharp pixels, then the data drawn from the original's pixels, most pixels first, each shape table's first frame
/// saved as <c>&lt;rank&gt;_&lt;command&gt;.png</c> to tell what it is.
/// </summary>
namespace
{
    /// <summary>A renderer audit callback adding each draw to <paramref name="tally"/>.</summary>
    std::function<void(const MCVulkanRenderer::AuditedDraw&)> AuditInto(std::map<AuditKey, AuditTally>& tally)
    {
        return [&tally](const MCVulkanRenderer::AuditedDraw& draw)
        {
            AuditTally& entry =
                tally[{std::string(draw.Command), AuditSharp(draw), draw.Data.data(), draw.From, draw.Window}];
            const bool table = VfxIsShapeTable(draw.Data);

            if (entry.Count == 0)
            {
                entry.WindowWidth = draw.Window->XMax + 1;
                entry.WindowHeight = draw.Window->YMax + 1;
                entry.Owner = AuditOwner(draw.Data);
                entry.Label = AuditLabel;
                MCInput::Display()->GetShownColors(entry.Colors);

                if (const MCWindow* from = draw.From; from != nullptr && from->Pixels != nullptr && from->Size() > 0)
                {
                    DecodedFrame& picture = entry.Picture.emplace();
                    picture.Width = from->XMax + 1;
                    picture.Height = from->YMax + 1;
                    picture.Index.assign(from->Pixels.get(), from->Pixels.get() + from->Size());
                    picture.Drawn.assign(from->Size(), 1);
                }

                if (table)
                {
                    entry.Attached = MCHdArt::AttachedFolder(draw.Data.data());
                    entry.TableHash = MCHdArt::ShapeTableHash(draw.Data);
                }
            }

            if (table && entry.ShapeFrames.size() < AuditFramesKept && !entry.ShapeFrames.contains(draw.Frame))
            {
                if (auto decoded = Decode(draw.Data, draw.Frame); decoded.has_value() && decoded->Width > 0)
                {
                    entry.ShapeFrames.emplace(draw.Frame, std::move(*decoded));
                }
            }

            ++entry.Count;
            entry.Pixels += draw.Pixels;
            entry.Scaled |= draw.Scaled;
            entry.Frames.insert(draw.Frame);
        };
    }

    /// <summary>
    /// Prints an audit's tally of <paramref name="frames"/> frames: each command's share of sharp pixels, then the data
    /// drawn from the original's pixels (with --hd-audit-all, everything), most pixels first, saving each picture or
    /// shape table's first frame into <paramref name="folder"/> to tell what it is.
    /// </summary>
    void ReportAudit(const std::map<AuditKey, AuditTally>& tally, const std::filesystem::path& folder, int32_t frames)
    {
        std::filesystem::create_directories(folder);

        // Each command's draws and pixels, and the share drawn sharp.
        std::map<std::string, std::array<int64_t, 3>> commands;

        for (const auto& [key, entry] : tally)
        {
            std::array<int64_t, 3>& total = commands[std::get<0>(key)];
            total[0] += entry.Count;
            total[1] += entry.Pixels;
            total[2] += std::get<1>(key) ? entry.Pixels : 0;
        }

        std::cout << std::format("  {} frames; per command: draws, window pixels, sharp share\n", frames);

        for (const auto& [command, total] : commands)
        {
            std::cout << std::format(
                "  {:<10} {:>9} {:>13} {:>5.1f}%\n", command, total[0], total[1],
                total[1] > 0 ? 100.0 * static_cast<double>(total[2]) / static_cast<double>(total[1]) : 0.0);
        }

        // What was drawn from the original's pixels (with --hd-audit-all, everything), most pixels first.
        const bool all = MCTest::Option("hd-audit-all").has_value();
        std::vector<std::pair<AuditKey, AuditTally>> original;
        std::ranges::copy_if(tally, std::back_inserter(original),
                             [all](const auto& entry) { return all || !std::get<1>(entry.first); });
        std::ranges::sort(original, std::ranges::greater{}, [](const auto& entry) { return entry.second.Pixels; });
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        constexpr size_t Listed = 120;
        std::cout << "  drawn from the original's pixels (rank, command, data, window, frames, draws, pixels, scaled, "
                     "owner):\n";

        for (size_t rank = 0; rank < std::min(original.size(), Listed); ++rank)
        {
            const auto& [key, entry] = original[rank];
            const auto& [command, sharp, data, from, window] = key;
            // A copy's picture size stands in for its owner (it has no data).
            const std::string owner = entry.Picture.has_value() && entry.Owner == "-"
                                          ? std::format("{}x{}", entry.Picture->Width, entry.Picture->Height)
                                          : entry.Owner;
            std::cout << std::format("  {:>3} {:<10} {} {:>4}x{:<4} {:>4} {:>8} {:>11} {} {}{}{}\n", rank, command,
                                     static_cast<const void*>(data), entry.WindowWidth, entry.WindowHeight,
                                     entry.Frames.size(), entry.Count, entry.Pixels, entry.Scaled ? "scaled" : "1:1",
                                     owner, entry.Label.empty() ? "" : " on " + entry.Label, sharp ? " sharp" : "");

            // A copy's picture as its window held it, and the pack's frame of it when it was drawn sharp.
            if (entry.Picture.has_value())
            {
                const DecodedFrame& picture = *entry.Picture;
                WriteFramePngs(
                    folder / std::format("{:03}_{}", rank, command), picture, entry.Colors, 1,
                    [](SDL_Color color) { return color; }, false);

                if (const std::shared_ptr<const MCHdArt::Frame> hd =
                        sharp ? MCHdArt::FindPicture(picture.Index, picture.Width, picture.Height) : nullptr;
                    hd != nullptr)
                {
                    std::vector<uint8_t> rgba = hd->Format == MCHdArt::ColorFormat::Bc7
                                                    ? MCBc7::Decode(hd->Colors, hd->Width, hd->Height)
                                                    : hd->Colors;
                    SavePng(folder / std::format("{:03}_{}_hd.png", rank, command), hd->Width, hd->Height, rgba);
                }
            }

            // With --hd-audit-all, a sharp shape's pack frames (their stored colours) for its first frames drawn.
            if (sharp && all && command == "Shape" && entry.TableHash.has_value())
            {
                for (const auto& [frame, decoded] : entry.ShapeFrames)
                {
                    const std::shared_ptr<const MCHdArt::Frame> hd =
                        entry.Attached.has_value()
                            ? MCHdArt::FindInFolder(*entry.Attached, frame, decoded.Width, decoded.Height)
                            : MCHdArt::FindShape(*entry.TableHash, frame, decoded.Width, decoded.Height);

                    if (hd != nullptr)
                    {
                        std::vector<uint8_t> rgba = hd->Format == MCHdArt::ColorFormat::Bc7
                                                        ? MCBc7::Decode(hd->Colors, hd->Width, hd->Height)
                                                        : hd->Colors;
                        SavePng(folder / std::format("{:03}_{}_{}_hd.png", rank, command, frame), hd->Width, hd->Height,
                                rgba);
                    }
                }
            }

            if (!sharp && (command == "Shape" || command == "ShapeBlit") && !entry.ShapeFrames.empty())
            {
                Sequence sequence;
                sequence.Folder = std::format("{:03}_{}", rank, command);
                sequence.Frames.push_back(entry.ShapeFrames.begin()->second);
                WritePack(folder, std::span(&sequence, 1), colors, 1, [](SDL_Color color) { return color; });
            }
        }
    }

    /// <summary>The live enemy (another team's mover, not disabled) nearest the first live mech of the player's lance.</summary>
    std::shared_ptr<MCMover> NearestEnemy()
    {
        constexpr int32_t LanceSize = 3;
        std::shared_ptr<MCMover> player;

        for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + LanceSize && player == nullptr; ++partId)
        {
            if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->IsDisabled() == 0)
            {
                player = std::move(mover);
            }
        }

        if (player == nullptr)
        {
            return nullptr;
        }

        const std::shared_ptr<MCTeam> team = player->Team.lock();
        const MCVector3D at = player->GetPosition();
        std::shared_ptr<MCMover> nearest;
        float best = std::numeric_limits<float>::max();

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->Team.lock() == team || mover->IsDisabled() != 0)
            {
                continue;
            }

            const MCVector3D there = mover->GetPosition();
            const float distance = (there.X - at.X) * (there.X - at.X) + (there.Y - at.Y) * (there.Y - at.Y);

            if (distance < best)
            {
                best = distance;
                nearest = mover;
            }
        }

        return nearest;
    }
}

namespace
{
    /// <summary>How many return addresses an audit's call stack keeps.</summary>
    constexpr int32_t AuditStackDepth = 32;
    /// <summary>FNV-1a's 64-bit offset basis and prime (the ids in the audit's tables).</summary>
    constexpr uint64_t AuditFnvBasis = 0xcbf29ce484222325ull;
    constexpr uint64_t AuditFnvPrime = 0x100000001b3ull;
    /// <summary>How many bytes of what a draw drew its id hashes (a fast shape's header and first rows).</summary>
    constexpr size_t AuditDataBytes = 256;
    /// <summary>How many frame numbers (or a font's characters) a table row lists.</summary>
    constexpr size_t AuditFramesListed = 24;
    /// <summary>A picture smaller than this on its longer side is drawn larger in the tables' pictures, up to 4 times.</summary>
    constexpr int32_t AuditPictureSide = 64;
    constexpr int32_t AuditMaxZoom = 4;
    /// <summary>The gap between a font's characters in its picture.</summary>
    constexpr int32_t AuditGlyphGap = 2;
    /// <summary>The first and last printable ASCII characters (a glyph's character is listed as itself, else by number).</summary>
    constexpr int32_t AuditFirstPrintable = 0x21;
    constexpr int32_t AuditLastPrintable = 0x7e;

    uint64_t AuditFnv(std::span<const uint8_t> bytes, uint64_t hash = AuditFnvBasis)
    {
        for (const uint8_t byte : bytes)
        {
            hash = (hash ^ byte) * AuditFnvPrime;
        }

        return hash;
    }

    /// <summary>A picture's id: its size and indices hashed (the extractor's pictures and a copy's alike).</summary>
    uint64_t AuditPictureHash(std::span<const uint8_t> index, int32_t width, int32_t height)
    {
        constexpr int32_t HalfWord = 32;
        return AuditFnv(index, AuditFnvBasis ^ (static_cast<uint64_t>(static_cast<uint32_t>(width)) << HalfWord |
                                                static_cast<uint32_t>(height)));
    }

    /// <summary>The return addresses of the call stack (none where the platform can't walk it).</summary>
    std::vector<uintptr_t> AuditStack()
    {
#if MC_AUDIT_STACKS
        std::array<void*, AuditStackDepth> frames{};
        const int count = backtrace(frames.data(), AuditStackDepth);
        std::vector<uintptr_t> stack;

        for (int i = 0; i < count; ++i)
        {
            stack.push_back(reinterpret_cast<uintptr_t>(frames[static_cast<size_t>(i)]));
        }

        return stack;
#else
        return {};
#endif
    }

    /// <summary>Where this program is loaded, which its call stacks are symbolized against (atos -l), or 0.</summary>
    uintptr_t AuditImageBase()
    {
#if MC_AUDIT_STACKS
        Dl_info info{};
        return dladdr(reinterpret_cast<const void*>(&AuditImageBase), &info) != 0
                   ? reinterpret_cast<uintptr_t>(info.dli_fbase)
                   : 0;
#else
        return 0;
#endif
    }

    /// <summary>The install's fonts, read once (for telling which font a glyph is from).</summary>
    const std::vector<MCGameFont>& AuditFonts()
    {
        static const std::vector<MCGameFont> fonts = GameFonts();
        return fonts;
    }

    /// <summary>The font file <paramref name="data"/> holds (a glyph draw's data starts at its font), or "?".</summary>
    std::string AuditFontName(std::span<const uint8_t> data)
    {
        for (const MCGameFont& font : AuditFonts())
        {
            if (data.size() >= font.Data.size() && std::ranges::equal(data.first(font.Data.size()), font.Data))
            {
                return font.Name;
            }
        }

        return "?";
    }

    std::function<void(const MCVulkanRenderer::AuditedDraw&)> AuditInDetail(std::map<AuditKey, AuditTally>& tally,
                                                                            AuditDetail& detail)
    {
        return [tallied = AuditInto(tally), &detail](const MCVulkanRenderer::AuditedDraw& draw)
        {
            tallied(draw);
            const bool sharp = AuditSharp(draw);
            AuditDetail::Entry& entry =
                detail.Entries[{std::string(draw.Command), sharp, draw.Data.data(), draw.From, draw.Window}];

            if (entry.Screens.empty())
            {
                entry.Stack = AuditStack();
                entry.Font = draw.Command == "Glyph" ? AuditFontName(draw.Data) : std::string();
                MCInput::Display()->GetShownColors(entry.Colors);
            }

            entry.Screens.insert(detail.Screen);

            if (const MCWindow* from = draw.From;
                !sharp && from != nullptr && from->Pixels != nullptr && from->Size() > 0)
            {
                DecodedFrame& latest = entry.Latest.emplace();
                latest.Width = from->XMax + 1;
                latest.Height = from->YMax + 1;
                latest.Index.assign(from->Pixels.get(), from->Pixels.get() + from->Size());
                MCInput::Display()->GetShownColors(entry.LatestColors);
            }

            if (!sharp)
            {
                AuditDetail::Site& site =
                    detail
                        .Sites[{std::string(draw.Command), draw.Window->XMax + 1, draw.Window->YMax + 1, AuditStack()}];
                site.Scaled |= draw.Scaled;
                ++site.Count;
                site.Pixels += draw.Pixels;
                site.Screens.insert(detail.Screen);
            }
        };
    }

    /// <summary>
    /// Saves <paramref name="index"/> (<paramref name="width"/> x <paramref name="height"/>, drawn where
    /// <paramref name="drawn"/> says, every pixel without it) in <paramref name="colors"/> as a PNG, a small picture
    /// drawn larger.
    /// </summary>
    void SaveAuditPicture(const std::filesystem::path& path, int32_t width, int32_t height,
                          std::span<const uint8_t> index, std::span<const uint8_t> drawn,
                          std::span<const SDL_Color, 256> colors)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        const int32_t zoom = std::clamp(AuditPictureSide / std::max(width, height), 1, AuditMaxZoom);
        // A picture that shows nothing in its colours (an effect's alpha colours, a diagram drawn through a table) is
        // pictured by its indices, as greys.
        constexpr int32_t Dark = 24;
        constexpr int32_t GreyFloor = 48;
        bool dark = true;

        for (size_t i = 0; i < index.size() && dark; ++i)
        {
            const SDL_Color color = colors[index[i]];
            dark = (!drawn.empty() && drawn[i] == 0) || color.r + color.g + color.b < Dark;
        }

        std::array<SDL_Color, 256> greys{};

        for (size_t i = 0; i < greys.size(); ++i)
        {
            const auto level = static_cast<uint8_t>(GreyFloor + static_cast<int32_t>(i) * (255 - GreyFloor) / 255);
            greys[i] = SDL_Color{level, level, level, 255};
        }

        const std::span<const SDL_Color, 256> shownIn = dark ? std::span<const SDL_Color, 256>(greys) : colors;
        const int32_t outWidth = width * zoom;
        const int32_t outHeight = height * zoom;
        std::vector<uint8_t> rgba(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * BytesPerPixel, 0);

        for (int32_t y = 0; y < outHeight; ++y)
        {
            for (int32_t x = 0; x < outWidth; ++x)
            {
                const size_t from = static_cast<size_t>(y / zoom) * static_cast<size_t>(width) + x / zoom;

                if (!drawn.empty() && drawn[from] == 0)
                {
                    continue;
                }

                const SDL_Color color = shownIn[index[from]];
                uint8_t* pixel = &rgba[(static_cast<size_t>(y) * static_cast<size_t>(outWidth) + x) * BytesPerPixel];
                pixel[0] = color.r;
                pixel[1] = color.g;
                pixel[2] = color.b;
                pixel[3] = 255;
            }
        }

        SavePng(path, outWidth, outHeight, rgba);
    }

    /// <summary>Saves the characters <paramref name="characters"/> of <paramref name="font"/> in a row, ink white.</summary>
    void SaveAuditGlyphs(const std::filesystem::path& path, std::span<const uint8_t> font,
                         const std::set<int32_t>& characters)
    {
        const int32_t height = VfxFontHeight(font);
        const int32_t count = MCVfxRead32(font, 4);
        std::vector<std::pair<std::span<const uint8_t>, int32_t>> glyphs;
        int32_t width = 0;

        for (const int32_t character : characters)
        {
            int32_t glyphWidth = 0;

            if (character < 0 || character >= count || glyphs.size() >= AuditFramesListed)
            {
                continue;
            }

            const std::span<const uint8_t> glyph = GlyphOf(font, character, glyphWidth);

            if (glyphWidth > 0)
            {
                glyphs.emplace_back(glyph, glyphWidth);
                width += glyphWidth + AuditGlyphGap;
            }
        }

        if (width <= 0 || height <= 0)
        {
            return;
        }

        std::vector<uint8_t> index(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
        int32_t left = 0;

        for (const auto& [glyph, glyphWidth] : glyphs)
        {
            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < glyphWidth; ++x)
                {
                    const bool inked =
                        glyph[static_cast<size_t>(y) * static_cast<size_t>(glyphWidth) + x] != MCSharpText::NoInk;
                    index[static_cast<size_t>(y) * static_cast<size_t>(width) + left + x] = inked ? 255 : 1;
                }
            }

            left += glyphWidth + AuditGlyphGap;
        }

        std::array<SDL_Color, 256> grey{};
        grey[1] = SDL_Color{48, 48, 48, 255};
        grey[255] = SDL_Color{255, 255, 255, 255};
        SaveAuditPicture(path, width, height, index, {}, grey);
    }

    /// <summary>A tally entry's frames (or a glyph entry's characters, as text), the first few.</summary>
    std::string AuditFramesText(const AuditKey& key, const AuditTally& entry)
    {
        const bool glyph = std::get<0>(key) == "Glyph";
        std::string text;

        for (const int32_t frame : entry.Frames | std::views::take(AuditFramesListed))
        {
            if (glyph)
            {
                text += frame >= AuditFirstPrintable && frame <= AuditLastPrintable
                            ? std::string(1, static_cast<char>(frame))
                            : std::format("<{}>", frame);
            }
            else
            {
                text += std::format("{}{}", text.empty() ? "" : ",", frame);
            }
        }

        return text.empty() ? "-" : text;
    }

    /// <summary>The audit's screens or call stack in a table cell.</summary>
    std::string AuditJoined(const std::set<std::string>& screens)
    {
        std::string text;

        for (const std::string& screen : screens)
        {
            text += (text.empty() ? "" : ",") + screen;
        }

        return text;
    }

    std::string AuditStackText(const std::vector<uintptr_t>& stack)
    {
        std::string text;

        for (const uintptr_t address : stack)
        {
            text += std::format("{}{:x}", text.empty() ? "" : " ", address);
        }

        return text;
    }

    void WriteAuditTables(const std::map<AuditKey, AuditTally>& tally, const AuditDetail& detail,
                          const std::filesystem::path& folder, int32_t frames)
    {
        const std::filesystem::path items = folder / "items";
        std::filesystem::create_directories(items);
        std::array<SDL_Color, 256> shown{};
        MCInput::Display()->GetShownColors(shown);

        // The pictures the menu extractor finds (what the pack's menu art was made from), by id, read when a copy
        // drawn from the original's pixels asks.
        std::optional<std::set<uint64_t>> sources;

        const auto isSource = [&](const DecodedFrame& picture) -> std::string
        {
            if (GuiSystem().ArtFile == nullptr)
            {
                return "?";
            }

            if (!sources.has_value())
            {
                sources.emplace();
                int32_t skipped = 0;
                ForEachPicture(shown, skipped,
                               [&](std::string_view, std::span<const uint8_t> pixels, int32_t width, int32_t height,
                                   std::span<const SDL_Color, 256>)
                               { sources->insert(AuditPictureHash(pixels, width, height)); });
            }

            return sources->contains(AuditPictureHash(picture.Index, picture.Width, picture.Height)) ? "source"
                                                                                                     : "made";
        };

        std::vector<std::pair<const AuditKey*, const AuditTally*>> original;

        for (const auto& [key, entry] : tally)
        {
            if (!std::get<1>(key))
            {
                original.emplace_back(&key, &entry);
            }
        }

        std::ranges::sort(original, std::ranges::greater{}, [](const auto& row) { return row.second->Pixels; });
        // The characters drawn from the original's pixels in each font, pictured once the rows are written.
        std::map<std::string, std::set<int32_t>> fontCharacters;
        std::ofstream table(folder / "audit.tsv", std::ios::binary);
        table << std::format("# frames {} load {:x}\n", frames, AuditImageBase());
        // The source column: a copy's picture is the extractor's (source) or made at run time (made); a shape's
        // frames that show nothing (blank).
        table << "id\tcommand\twindow\tscaled\tdraws\tpixels\tper_draw\tper_frame\tframes\towner\tfont\tpack\tsource\t"
                 "screens\tstack\n";

        for (const auto& [key, entry] : original)
        {
            const std::string& command = std::get<0>(*key);
            const auto found = detail.Entries.find(*key);
            const AuditDetail::Entry* extra = found != detail.Entries.end() ? &found->second : nullptr;
            std::string id;
            std::string pack = "-";
            std::string source = "-";

            if (entry->Picture.has_value())
            {
                const DecodedFrame& picture = *entry->Picture;
                id = std::format("picture-{:016x}", AuditPictureHash(picture.Index, picture.Width, picture.Height));
                pack = MCHdArt::FindPicture(picture.Index, picture.Width, picture.Height) != nullptr ? "has" : "lacks";
                source = isSource(picture);

                if (const std::filesystem::path path = items / (id + ".png"); !std::filesystem::exists(path))
                {
                    if (extra != nullptr && extra->Latest.has_value())
                    {
                        const DecodedFrame& latest = *extra->Latest;
                        SaveAuditPicture(path, latest.Width, latest.Height, latest.Index, {}, extra->LatestColors);
                    }
                    else
                    {
                        SaveAuditPicture(path, picture.Width, picture.Height, picture.Index, {},
                                         extra != nullptr ? std::span<const SDL_Color, 256>(extra->Colors) : shown);
                    }
                }
            }
            else if (entry->TableHash.has_value())
            {
                id = std::format("shape-{:016x}", *entry->TableHash);
                int32_t has = 0;

                for (const auto& [frame, decoded] : entry->ShapeFrames)
                {
                    const std::shared_ptr<const MCHdArt::Frame> hd =
                        entry->Attached.has_value()
                            ? MCHdArt::FindInFolder(*entry->Attached, frame, decoded.Width, decoded.Height)
                            : MCHdArt::FindShape(*entry->TableHash, frame, decoded.Width, decoded.Height);
                    has += hd != nullptr ? 1 : 0;
                }

                const auto kept = static_cast<int32_t>(entry->ShapeFrames.size());
                pack = kept == 0     ? "?"
                       : has == kept ? "has"
                       : has == 0    ? "lacks"
                                     : std::format("part {}/{}", has, kept);

                // Frames that show nothing (the audit counts their draws as sharp, so none should be listed here).
                const auto blank = std::ranges::count_if(entry->ShapeFrames, [](const auto& frameKept)
                                                         { return BlankFrame(frameKept.second); });
                source = std::format("blank {}/{}", blank, kept);

                if (const std::filesystem::path path = items / (id + ".png");
                    !entry->ShapeFrames.empty() && !std::filesystem::exists(path))
                {
                    const DecodedFrame& first = entry->ShapeFrames.begin()->second;
                    SaveAuditPicture(path, first.Width, first.Height, first.Index, first.Drawn,
                                     extra != nullptr ? std::span<const SDL_Color, 256>(extra->Colors) : shown);
                }
            }
            else if (extra != nullptr && !extra->Font.empty())
            {
                id = std::format("font-{}", extra->Font);
                pack = "font";
                fontCharacters[extra->Font].insert(entry->Frames.begin(), entry->Frames.end());
            }
            else
            {
                id = std::format("{}-{}x{}", command, entry->WindowWidth, entry->WindowHeight);
            }

            table << std::format("{}\t{}\t{}x{}\t{}\t{}\t{}\t{}\t{:.1f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", id, command,
                                 entry->WindowWidth, entry->WindowHeight, entry->Scaled ? "scaled" : "1:1",
                                 entry->Count, entry->Pixels, entry->Count > 0 ? entry->Pixels / entry->Count : 0,
                                 frames > 0 ? static_cast<double>(entry->Pixels) / frames : 0.0,
                                 AuditFramesText(*key, *entry), entry->Owner.empty() ? "-" : entry->Owner,
                                 extra != nullptr && !extra->Font.empty() ? extra->Font : "-", pack, source,
                                 extra != nullptr ? AuditJoined(extra->Screens) : "-",
                                 extra != nullptr ? AuditStackText(extra->Stack) : "-");
        }

        for (const auto& [name, characters] : fontCharacters)
        {
            if (const auto font = std::ranges::find(AuditFonts(), name, &MCGameFont::Name); font != AuditFonts().end())
            {
                SaveAuditGlyphs(items / std::format("font-{}.png", name), font->Data, characters);
            }
        }

        std::ofstream sites(folder / "sites.tsv", std::ios::binary);
        sites << std::format("# frames {} load {:x}\n", frames, AuditImageBase());
        sites << "command\twindow\tscaled\tdraws\tpixels\tper_frame\tscreens\tstack\n";

        for (const auto& [key, site] : detail.Sites)
        {
            const auto& [command, width, height, stack] = key;
            sites << std::format("{}\t{}x{}\t{}\t{}\t{}\t{:.1f}\t{}\t{}\n", command, width, height,
                                 site.Scaled ? "scaled" : "1:1", site.Count, site.Pixels,
                                 frames > 0 ? static_cast<double>(site.Pixels) / frames : 0.0,
                                 AuditJoined(site.Screens), AuditStackText(stack));
        }

        std::cout << std::format("  audit tables: {} ({} entries from the original's pixels, {} sites)\n",
                                 folder.string(), original.size(), detail.Sites.size());
    }

    /// <summary>Runs <paramref name="count"/> frames.</summary>
    void RunFrames(int32_t count)
    {
        for (int32_t frame = 0; frame < count; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    /// <summary>Runs frames until <paramref name="done"/> (at most <paramref name="limit"/>); whether it came.</summary>
    bool RunUntil(const std::function<bool()>& done, int32_t limit)
    {
        for (int32_t frame = 0; frame < limit; ++frame)
        {
            if (done())
            {
                return true;
            }

            MCTestGame::RunFrame(FrameSeconds);
        }

        return done();
    }

    /// <summary>Closes the logistics message dialog without its callback (a question shown only to be pictured).</summary>
    void DismissMessage()
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.Callback = nullptr;
        dialog.Deactivate(0);
    }

#if MC_AUDIT_CONTROLS
    /// <summary>The text button showing <paramref name="label"/> under <paramref name="parent"/> (at any depth), or null.</summary>
    std::shared_ptr<MCLogTextButton> FindAuditTextButton(const MCGuiObject& parent, std::string_view label)
    {
        for (const std::shared_ptr<MCGuiObject>& child : parent.Children())
        {
            if (auto button = std::dynamic_pointer_cast<MCLogTextButton>(child);
                button != nullptr && button->Label() == label)
            {
                return button;
            }

            if (std::shared_ptr<MCLogTextButton> found = FindAuditTextButton(*child, label); found != nullptr)
            {
                return found;
            }
        }

        return nullptr;
    }
#endif

    /// <summary>The multiplayer screens, hosting a LAN game on the loopback as the session screen tests do.</summary>
    void TourMultiplayer(const std::function<void(const std::string&)>& shoot)
    {
        constexpr std::string_view HostAddress = "127.0.0.1:28877";
        // The logistics states of the LAN and session screens (as MCConnectMenu sets them).
        constexpr int32_t LanState = 0xb;
        constexpr int32_t SessionState = 8;
        ConnectScreen();
        shoot("mp_connect");
        ShowSerialScreen();
        shoot("mp_serial");
        CancelToConnect();
        ShowModemScreen();
        shoot("mp_modem");
        CancelToConnect();
        ShowLanScreen();
        shoot("mp_protocol_question");
        DismissMessage();

        if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
        {
            std::cout << "  no multiplayer: the LAN, ready room and session screens are left out\n";
            return;
        }

        MultiPlayer()->SessionManager->ConnectTcp(HostAddress);
        MCSplashScreen& lan = *GlobalLog()->LanScreen;
        GlobalLog()->MultiplayerScreen->ShowGuiWindow(false);
        lan.ShowGuiWindow(true);
        lan.ShowBlock(0);
        GlobalLog()->SetCurrentScreen(lan);
        GlobalLog()->LogisticsState = LanState;
        lan.Element<MCLogTextObject>(4)->SetStringBuffer("Commander");
        lan.Element<MCLogTextObject>(10)->SetStringBuffer("Audit game");
        lan.Element<MCLogTextObject>(11)->SetStringBuffer("6");
        shoot("mp_lan");
        CreateSession();
        shoot("mp_ready_room");
        // The session screen as GO shows it (its activation sends a lone host back to the main menu, so its art
        // shows without the players).
        GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
        GlobalLog()->SetCurrentScreen(*GlobalLog()->SessionScreen);
        GlobalLog()->ShowLogScreen(true, true);
        GlobalLog()->LogisticsState = SessionState;
        shoot("mp_session");
        DoExit();
        shoot("main_menu_after_mp");
    }

    /// <summary>
    /// Mission 1 of a new campaign launched from its briefing (the first mech deployed, as a player drags it), through
    /// the loading screen to the mission; whether it came up.
    /// </summary>
    bool LaunchFromBriefing(AuditDetail& detail)
    {
        constexpr int32_t SettleFrames = 30;
        constexpr int32_t LoadFrames = 600;
        constexpr int32_t LaunchX = 0x230;
        constexpr int32_t LaunchY = 0x152;
        NewCampaign();
        RunFrames(SettleFrames);
        MCBriefingScreen& briefing = *GlobalLog()->BriefingScreen;
        MCScrollPane& pane = *briefing.DeployPane;

        if (pane.NumberOfChildren() == 0)
        {
            std::cout << "  the briefing has no mech to deploy\n";
            return false;
        }

        const std::shared_ptr<MCLogMech> mech = std::static_pointer_cast<MCMechBriefBlock>(pane.Child(0))->Mech();
        MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(mech->CurTonnage);
        const RECT& slot = briefing.SlotRects[0];
        MCMechBriefBlock& block = *mech->BriefBlock;
        MCScreenInput::Drag(block.GlobalX() + block.Width() / 2, block.GlobalY() + block.Height() / 2,
                            (slot.left + slot.right) / 2, (slot.top + slot.bottom) / 2);
        RunFrames(SettleFrames);
        detail.Screen = "loading";
        MCScreenInput::RealClick(LaunchX, LaunchY);
        return RunUntil([] { return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario; },
                        LoadFrames);
    }

    void TourScreens(const std::function<void(const std::string&)>& shoot, AuditDetail& detail)
    {
        constexpr int32_t BattleFrames = 150;
        constexpr int32_t ResultsFrames = 60;
        constexpr int32_t ResultsSteps = 3000;
        constexpr int32_t ResultsShotStep = 40;
        constexpr uint32_t TicksPerFrame = 5;
        constexpr uint32_t ScenarioWon = 4;
        constexpr int32_t LogisticsFrames = 3000;

        // The inventory tabs the first round leaves out, and the briefing's operation tab.
        GlobalLog()->RepairScreen->SetUpPilotInv(true, true);
        shoot("repair_pilots");
        GlobalLog()->RepairScreen->SetUpVhclInv(true, true);
        shoot("repair_vehicles");
        GlobalLog()->SetUpPurchaseScreen(true);
        GlobalLog()->PurchaseScreen->SetUpPilotInv(true, true);
        shoot("purchase_pilots");
        GlobalLog()->PurchaseScreen->SetUpCompInv(true, true);
        shoot("purchase_components");
        GlobalLog()->PurchaseScreen->SetUpVhclInv(true, true);
        shoot("purchase_vehicles");
        GlobalLog()->SetUpBriefingScreen(true);
        GlobalLog()->BriefingScreen->SetUpOperation();
        shoot("briefing_operation");
        GlobalLog()->BriefingScreen->SetUpMission();

        // The main menu in a campaign, and what it opens.
        Cancel();
        shoot("main_menu_campaign");
        SaveScreen();
        shoot("save");
        Cancel();
        SoloLoadScreen();
        shoot("load_solo");
        Cancel();
        CheckExit();
        shoot("exit_question");
        DismissMessage();
#if MC_AUDIT_CONTROLS
        detail.Screen = "controls";
        ShowPreferences();

        if (const std::shared_ptr<MCLogTextButton> controls = FindAuditTextButton(*GlobalLog()->PrefScreen, "CONTROLS");
            controls != nullptr)
        {
            MCScreenInput::RealClick(controls->GlobalX() + controls->Width() / 2,
                                     controls->GlobalY() + controls->Height() / 2);
            shoot("controls");
        }

        CancelPrefs();
#endif
        TourMultiplayer(shoot);
        NewMcxCampaign();
        shoot("briefing_expansion");

        if (!LaunchFromBriefing(detail))
        {
            std::cout << "  the mission never came up: the mission's screens are left out\n";
            return;
        }

        if (const std::shared_ptr<MCMover> enemy = NearestEnemy(); enemy != nullptr)
        {
            SendLanceAt(enemy);
        }

        detail.Screen = "mission";
        RunFrames(BattleFrames);
        shoot("mission");

        // Escape pauses (the game's only in-mission menu: Enter then ends the mission).
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
        GamePaused = true;
        shoot("paused");
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
        GamePaused = false;

        // The tactical map's pages.
        for (const auto& [page, name] :
             {std::pair{MCTacmapPage::Info, "tacmap_info"}, std::pair{MCTacmapPage::Mission, "tacmap_objectives"},
              std::pair{MCTacmapPage::Salvage, "tacmap_salvage"}, std::pair{MCTacmapPage::Map, "tacmap_map"}})
        {
            TacticalMap()->HideMe(false);
            TacticalMap()->SetDisplayType(page);
            shoot(name);
        }

        // Won: the results screen counts through its steps (its timer counted here, as the results tests do), then
        // the debriefing.
        MouseTimerKill();

        for (int32_t i = 0; i < Scenario()->Objectives.Count(); i++)
        {
            Scenario()->Objectives[i].Status = 1;
        }

        ScenarioResult = ScenarioWon;
        detail.Screen = "results";

        if (!RunUntil([] { return Mission()->State == MCMissionState::Results && Mission()->ResultsScreen != nullptr; },
                      ResultsFrames))
        {
            std::cout << "  the results screen never came up\n";
            return;
        }

        for (int32_t step = 0;
             step < ResultsSteps && Mission()->ResultsScreen != nullptr && !Mission()->ResultsScreen->Finished();
             ++step)
        {
            MouseTicks += TicksPerFrame;
            MCTestGame::RunFrame(FrameSeconds);

            if (step == ResultsShotStep)
            {
                shoot("results");
            }
        }

        shoot("debriefing");
        Mission()->CloseResultsScreen();
        detail.Screen = "after_mission";

        if (!RunUntil(
                []
                {
                    return Mission()->State == MCMissionState::Logistics && GlobalLog() != nullptr &&
                           GlobalLog()->CurrentScreen() != nullptr && GuiSystem().SmackerWindow == nullptr;
                },
                LogisticsFrames))
        {
            std::cout << "  logistics never came back after the mission\n";
            return;
        }

        shoot("logistics_after_mission");
        GlobalLog()->SetUpPurchaseScreen(true);
        shoot("purchase_after_mission");
    }
}

/// <summary>
/// Tool: <c>--hd-audit &lt;pack&gt; --shots &lt;folder&gt;</c> lists what is still drawn from the original's pixels
/// with HD art on: a mission (<c>--mission &lt;n&gt;</c>, default 1) drawn by the GPU as --hd-pack's pictures are, the
/// player's lance selected and sent at the nearest live enemy (again every ten seconds, and when it falls) for
/// <c>--hd-audit-frames &lt;n&gt;</c> frames (default 900, a minute) with the camera following it, every draw command
/// tallied by what it drew and whether it was sharp (ReportAudit, WriteAuditTables). <c>--hd-audit-zoom &lt;lines&gt;</c>
/// zooms the view out first; <c>--hd-audit-settle &lt;n&gt;</c> runs that many frames first (default 15; 0 audits the
/// first frame HD art is on).
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-audit lists what a battle still draws from the original's pixels")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-audit");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    StartPictures(*pack, hdArt);
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);

    // With --hd-audit-zoom <lines>, the view zoomed to show that many game lines (480 closest, 2160 furthest).
    if (const std::optional<std::string_view> zoom = MCTest::Option("hd-audit-zoom"); zoom.has_value())
    {
        // strtof: libc++ has floating-point from_chars only from macOS 26.
        const float lines = std::strtof(std::string(*zoom).c_str(), nullptr);
        MCFixedZoomHeight = 0.0f;
        REQUIRE(MCMainView() != nullptr);
        REQUIRE(MCMainView()->ZoomTo(lines));
    }

    std::map<AuditKey, AuditTally> tally;
    AuditDetail detail;
    // A second for the view to settle once HD art goes on (--hd-audit-settle <n> frames, 0 audits the first frame).
    constexpr int32_t DefaultSettleFrames = 15;
    int32_t settleFrames = DefaultSettleFrames;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-audit-settle"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), settleFrames);
    }

    for (int32_t frame = 0; frame < settleFrames; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    renderer->Audit = AuditInDetail(tally, detail);
    constexpr int32_t DefaultFrames = 900;
    constexpr int32_t RetargetFrames = 150;
    int32_t frames = DefaultFrames;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-audit-frames"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), frames);
    }

    std::shared_ptr<MCMover> target;
    int32_t targets = 0;
    int32_t fallen = 0;

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        const bool down = target != nullptr && target->IsDisabled() != 0;
        fallen += down ? 1 : 0;

        if (target == nullptr || down || frame % RetargetFrames == 0)
        {
            if (std::shared_ptr<MCMover> next = NearestEnemy(); next != nullptr)
            {
                targets += next != target ? 1 : 0;
                target = std::move(next);
                SendLanceAt(target);
            }
        }

        MCTestGame::RunFrame(FrameSeconds);
    }

    std::cout << std::format("  {} enemies targeted, {} fell\n", targets, fallen);
    renderer->Audit = nullptr;
    ReportAudit(tally, folder, frames);
    WriteAuditTables(tally, detail, folder, frames);
}

/// <summary>
/// A terrain overlay the HD art pack has is drawn from it: a real overlay (the tile sets' first with pixels) drawn on
/// the screen by the GPU over mission 1, from a pack holding it with its colours inverted, shows other colours than the
/// original's draw of it, and the HD draws count it.
/// </summary>
TEST_CASE_ISOLATED("game: a terrain overlay is drawn from the HD art pack")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    std::vector<uint8_t> table;
    std::string name;
    DecodedFrame frame;
    ForEachOverlay(
        [&](std::span<const uint8_t> overlayTable, std::span<const uint8_t> shape)
        {
            if (table.empty())
            {
                frame = DecodeOverlay(shape);

                if (std::ranges::find(frame.Drawn, 1) != frame.Drawn.end())
                {
                    table.assign(overlayTable.begin(), overlayTable.end());
                    name = MCHdArt::TerrainTileName(shape);
                }
            }
        });
    REQUIRE(!table.empty());
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_overlay";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    std::filesystem::create_directories(OverlayFolderOf(pack));
    WriteFramePngs(OverlayFolderOf(pack) / name, frame, colors, TestPackScale, Inverted, false);
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const auto drawn = [&]
    {
        MCTestGame::RunFrame(FrameSeconds);
        FastShapeDraw(pane, block.Bytes(), 0, x, y, std::nullopt);
        const auto shown = display->ReadFrame();
        REQUIRE(shown.has_value());
        return *shown;
    };

    GamePaused = true;
    const std::vector<SDL_Color> original = drawn();
    const std::vector<SDL_Color> again = drawn();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    const int64_t drawnBefore = renderer->HdDrawn();
    const std::vector<SDL_Color> hd = drawn();
    CHECK(renderer->HdDrawn() > drawnBefore);
    hdArt.reset();
    GamePaused = false;

    const auto differing = [](const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    {
        return std::ranges::count_if(std::views::iota(size_t{0}, a.size()), [&](size_t i)
                                     { return a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b; });
    };

    const auto changed = differing(original, hd);
    std::cout << std::format("  overlay {}: {}x{}, {} shown pixels changed\n", name, frame.Width, frame.Height,
                             changed);
    CHECK_EQ(differing(original, again), 0);
    CHECK(changed > 0);
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// HD art changes only the colours shown: in mirror mode, mission 1's battle drawn with every terrain overlay from a
/// pack (their colours inverted), the GPU's palette indices are still the software renderer's, frame after frame, and
/// overlays were drawn from the pack.
/// </summary>
TEST_CASE_ISOLATED("game: HD art keeps the terrain overlays' palette indices")
{
    // Frame 105 of this battle differs at the screen's top left with HD art off too (16 pixels, software 151, GPU 43),
    // so the run stops before it; some 900 overlays are drawn from the pack by then.
    constexpr int32_t Frames = 90;

    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::Mirror);

    if (renderer == nullptr)
    {
        return;
    }

    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_overlays";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    REQUIRE(WriteOverlays(OverlayFolderOf(pack), colors, TestPackScale, Inverted).Written > 0);
    WritePaletteRgb(pack, colors);
    SendLanceAtUller();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    renderer->ResetMirror();
    const int64_t drawnBefore = renderer->HdDrawn();

    for (int32_t frame = 0; frame < Frames; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    hdArt.reset();
    const MCVulkanRenderer::MirrorTally& tally = renderer->Mirror();
    std::cout << std::format("  {} overlays drawn from the pack\n", renderer->HdDrawn() - drawnBefore);
    CHECK(renderer->HdDrawn() > drawnBefore);
    CHECK_EQ(tally.Frames, Frames);
    CHECK_EQ(tally.DifferentFrames, 0);

    if (tally.DifferentFrames != 0)
    {
        std::cout << std::format("  first difference, frame {}: {}\n", tally.FirstFrame, tally.First);
    }

    std::filesystem::remove_all(scratch);
}

namespace
{
    /// <summary>
    /// What the pixels of <paramref name="box"/> (the screen pane's) changed by <paramref name="draw"/> show, with HD art
    /// on from <paramref name="pack"/> (none: off).
    /// </summary>
    std::vector<SDL_Color> ChangedPixels(const std::optional<std::filesystem::path>& pack, const MCRect& box,
                                         const std::function<void()>& draw)
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        const MCPane& pane = ScreenPort()->Frame();
        std::optional<MCHdArtOn> on;

        if (pack.has_value())
        {
            on.emplace(*pack);
        }

        MCTestGame::RunFrame(FrameSeconds);
        const auto under = display->ReadFrame();
        draw();
        const auto drawn = display->ReadFrame();
        REQUIRE(under.has_value());
        REQUIRE(drawn.has_value());
        const int32_t width = display->Width();
        std::vector<SDL_Color> changed;

        for (int32_t row = box.Y0; row <= box.Y1; ++row)
        {
            for (int32_t column = box.X0; column <= box.X1; ++column)
            {
                const size_t at = static_cast<size_t>(row + pane.Y0) * width + column + pane.X0;
                const SDL_Color a = (*under)[at];
                const SDL_Color b = (*drawn)[at];

                if (a.r != b.r || a.g != b.g || a.b != b.b)
                {
                    changed.push_back(b);
                }
            }
        }

        return changed;
    }

    /// <summary>
    /// How many pixels of <paramref name="box"/> (the screen pane's) show neither colour <paramref name="color"/> nor what
    /// was under them once <paramref name="draw"/> has drawn, with HD art on from <paramref name="pack"/> (none: off).
    /// </summary>
    int64_t PixelsBetween(const std::optional<std::filesystem::path>& pack, uint8_t color, const MCRect& box,
                          const std::function<void()>& draw)
    {
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        return std::ranges::count_if(ChangedPixels(pack, box, draw),
                                     [&](const SDL_Color& pixel) { return !SameColor(pixel, colors[color]); });
    }
}

/// <summary>
/// With HD art on, lines and ellipses are drawn at the output's resolution: a diagonal line and an ellipse's outline
/// drawn on the screen by the GPU over mission 1 (a window shown larger than the screen) show, with HD art off, only
/// their colour or what was under them; with it on, also colours between (their smooth edges).
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws lines and ellipses at the output's resolution")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_smooth_shapes");
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    constexpr int32_t Length = 60;
    constexpr int32_t Radius = 30;
    constexpr uint8_t Color = 0x30;
    const MCRect box{x - Length - 1, y - Length, x + Length + 1, y + Length + Radius + 1};
    const auto shapes = [&]
    {
        VfxLineDraw(pane, x - Length, y - Length / 2, x + Length, y + Length / 2, Color);
        AGEllipseDraw(pane, x, y + Length, Radius * 2, Radius, Color);
    };

    GamePaused = true;
    const int64_t original = PixelsBetween(std::nullopt, Color, box, shapes);
    const int64_t smooth = PixelsBetween(pack, Color, box, shapes);
    GamePaused = false;
    std::cout << std::format("  pixels between the colours: {} with HD art off, {} on\n", original, smooth);
    CHECK_EQ(original, 0);
    CHECK(smooth > 0);
    std::filesystem::remove_all(pack.parent_path());
}

/// <summary>
/// With HD art on, a health bar is drawn at the output's resolution: its fill ends where the game measured it, inside its
/// last window pixel. Two bars over one colour on mission 1's screen (shown larger), one measured a whole number of
/// pixels long and one half a pixel longer, show alike with HD art off (the original rounds the length down) and differ
/// with it on, only about the first's fill's end; the screen's indices under them are the same either way.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws a health bar's fill as long as measured, at the output's resolution")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_health_bar");
    MCPane& screen = ScreenPort()->Frame();
    const std::shared_ptr<MCWindow> window = screen.Window.lock();
    REQUIRE(window != nullptr);
    // A mech's bar (MCMechActor's): a box 40 pixels wide and 6 high, green, its fill half full; the second bar Gap rows
    // below the first.
    constexpr int32_t BoxWidth = 40;
    constexpr int32_t BoxHeight = 6;
    constexpr int32_t Length = 19;
    constexpr float HalfPixel = 0.5f;
    constexpr int32_t Gap = 20;
    constexpr int32_t Green = 0x101;
    constexpr int32_t Background = 0x30;
    const int32_t x0 = screen.X0 + (screen.X1 - screen.X0) / 2 - BoxWidth / 2;
    const int32_t y0 = screen.Y0 + (screen.Y1 - screen.Y0) / 2;
    constexpr int32_t Margin = 2;
    const MCRect under{x0 - Margin, y0 - Margin, x0 + BoxWidth + Margin, y0 + Gap + BoxHeight + Margin};

    struct Drawn
    {
        Shown Seen;
        std::vector<uint8_t> Indices;
    };

    const auto draw = [&](bool hd)
    {
        std::optional<MCHdArtOn> on;

        if (hd)
        {
            on.emplace(pack);
        }

        MCTestGame::RunFrame(FrameSeconds);
        VfxPaneWipe(MCPane{window, under.X0, under.Y0, under.X1, under.Y1}, Background);
        AGStatusBar(screen, x0, y0, x0 + BoxWidth, y0 + BoxHeight, Green, Length, static_cast<float>(Length));
        AGStatusBar(screen, x0, y0 + Gap, x0 + BoxWidth, y0 + Gap + BoxHeight, Green, Length,
                    static_cast<float>(Length) + HalfPixel);
        Drawn drawn;
        drawn.Seen = ReadShown(pack.parent_path());
        drawn.Indices = MCInput::Display()->ComposeScreen();
        return drawn;
    };

    GamePaused = true;
    const Drawn plain = draw(false);
    const Drawn sharp = draw(true);
    GamePaused = false;

    // The shown pixels of the first bar's box against the second's, Gap window rows (a whole number of shown rows)
    // below; the window column of each that differs.
    const int32_t width = MCInput::Display()->Width();
    REQUIRE(width > 0);
    const float scale = static_cast<float>(sharp.Seen.Width) / static_cast<float>(width);
    const float rows = static_cast<float>(Gap) * scale;
    constexpr float Whole = 0.01f;
    REQUIRE(std::abs(rows - std::round(rows)) < Whole);
    const auto offset = static_cast<int32_t>(std::round(rows));
    const auto differing = [&](const Shown& seen)
    {
        std::set<int32_t> columns;
        int32_t count = 0;

        for (auto y = static_cast<int32_t>(static_cast<float>(y0) * scale);
             y < static_cast<int32_t>(static_cast<float>(y0 + BoxHeight + 1) * scale); ++y)
        {
            for (auto x = static_cast<int32_t>(static_cast<float>(x0) * scale);
                 x < static_cast<int32_t>(static_cast<float>(x0 + BoxWidth + 1) * scale); ++x)
            {
                const size_t a = (static_cast<size_t>(y) * seen.Width + x) * BytesPerPixel;
                const size_t b = (static_cast<size_t>(y + offset) * seen.Width + x) * BytesPerPixel;

                if (std::memcmp(&seen.Pixels[a], &seen.Pixels[b], 3) != 0)
                {
                    ++count;
                    columns.insert(static_cast<int32_t>((static_cast<float>(x) + HalfPixel) / scale));
                }
            }
        }

        std::cout << std::format("  {} shown pixels differ between the bars\n", count);
        return columns;
    };

    CHECK(differing(plain.Seen).empty());
    const std::set<int32_t> columns = differing(sharp.Seen);
    // The first bar, on whole pixels, shows as with HD art off.
    const MCRect first{static_cast<int32_t>(static_cast<float>(under.X0) * scale),
                       static_cast<int32_t>(static_cast<float>(under.Y0) * scale),
                       static_cast<int32_t>(static_cast<float>(x0 + BoxWidth + 1) * scale) - 1,
                       static_cast<int32_t>(static_cast<float>(y0 + BoxHeight + 1) * scale) - 1};
    CHECK_EQ(Compare(plain.Seen, sharp.Seen, first).Count, 0);
    // The rounded fill's last pixel is x0 + Length + 1; the measured one ends half way across the next.
    const int32_t last = x0 + Length + 1;
    CHECK(!columns.empty());
    CHECK(std::ranges::all_of(columns, [&](int32_t column) { return column == last || column == last + 1; }));

    // The indices under the bars are the same with HD art on.
    REQUIRE_EQ(sharp.Indices.size(), plain.Indices.size());
    int32_t kept = 0;
    int32_t pixels = 0;

    for (int32_t y = under.Y0; y <= under.Y1; ++y)
    {
        for (int32_t x = under.X0; x <= under.X1; ++x)
        {
            const size_t at = static_cast<size_t>(y) * width + x;
            kept += plain.Indices[at] == sharp.Indices[at] ? 1 : 0;
            ++pixels;
        }
    }

    CHECK_EQ(kept, pixels);
    std::filesystem::remove_all(pack.parent_path());
}

/// <summary>
/// With HD art on, a flat or Gouraud polygon (a laser's beam) is drawn at the output's resolution: its slanted edges
/// show pixels between its colour and what lies under it, which the original's whole pixels never do.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws flat and Gouraud polygons with smooth edges")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_smooth_polygons");
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    constexpr int32_t Length = 60;
    constexpr int32_t Width = 6;
    constexpr uint8_t Color = 0x30;
    constexpr int32_t Fixed = 1 << 16;
    // A slanted beam, as a laser's: a quad Length long and Width wide, one colour (so the flat and Gouraud fills agree).
    const std::array<MCScreenVertex, 4> beam = {
        MCScreenVertex{x - Length, y - Length / 2, Color * Fixed, 0, 0, 0},
        MCScreenVertex{x + Length, y + Length / 2, Color * Fixed, 0, 0, 0},
        MCScreenVertex{x + Length, y + Length / 2 + Width, Color * Fixed, 0, 0, 0},
        MCScreenVertex{x - Length, y - Length / 2 + Width, Color * Fixed, 0, 0, 0}};
    const MCRect box{x - Length - 1, y - Length / 2 - 1, x + Length + 1, y + Length / 2 + Width + 1};

    GamePaused = true;

    for (const bool gouraud : {false, true})
    {
        const auto polygon = [&]
        {
            if (gouraud)
            {
                VfxGouraudPolygon(pane, beam);
            }
            else
            {
                VfxFlatPolygon(pane, beam);
            }
        };

        MCTest::Scope scope(gouraud ? "Gouraud" : "flat");
        const int64_t original = PixelsBetween(std::nullopt, Color, box, polygon);
        const int64_t smooth = PixelsBetween(pack, Color, box, polygon);
        std::cout << std::format("  {}: pixels between the colours: {} with HD art off, {} on\n",
                                 gouraud ? "Gouraud" : "flat", original, smooth);
        CHECK_EQ(original, 0);
        CHECK(smooth > 0);
    }

    GamePaused = false;
    std::filesystem::remove_all(pack.parent_path());
}

namespace
{
    /// <summary>A point's distance from the segment a-b (window coordinates).</summary>
    float SegmentDistance(std::array<float, 2> p, std::array<float, 2> a, std::array<float, 2> b)
    {
        const float dx = b[0] - a[0];
        const float dy = b[1] - a[1];
        const float length2 = dx * dx + dy * dy;
        const float t =
            length2 > 0.0f ? std::clamp(((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length2, 0.0f, 1.0f) : 0.0f;
        return std::hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy));
    }

    /// <summary>Whether a point lies in the triangle a, b, c (either way round, its edges included).</summary>
    bool InTriangle(std::array<float, 2> p, std::array<float, 2> a, std::array<float, 2> b, std::array<float, 2> c)
    {
        const auto side = [&p](std::array<float, 2> from, std::array<float, 2> to)
        { return (to[0] - from[0]) * (p[1] - from[1]) - (to[1] - from[1]) * (p[0] - from[0]); };
        const float ab = side(a, b);
        const float bc = side(b, c);
        const float ca = side(c, a);
        return (ab >= 0.0f && bc >= 0.0f && ca >= 0.0f) || (ab <= 0.0f && bc <= 0.0f && ca <= 0.0f);
    }
}

/// <summary>
/// With HD art on, the polygons the smooth path left to the original's pixels are drawn smooth too: a quad with one
/// corner turned in (a beam's rounding), as two triangles either side of the diagonal from that corner, a quad whose
/// corners lie in a line, as a line, and one whose sides go out and back (a beam's sides on one point), as its sides.
/// Each, flat and Gouraud, shows pixels between its colour and what lies under it only with HD art on, keeps the
/// screen's indices, and the concave quad shows its colour all over its inside (no seam along the diagonal).
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws concave quads and quads of no area smooth")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_concave");
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    constexpr uint8_t Color = 0x30;
    constexpr int32_t Fixed = 1 << 16;
    // A kite whose left side bends in at its third corner (the walk fills it whole: its sides only go down, then up),
    // a quad whose corners lie on a steep line, and one going out to a bend and on, then back.
    const std::array<std::array<int32_t, 2>, 4> kite = {{{0, 0}, {30, 35}, {0, 60}, {10, 25}}};
    const std::array<std::array<int32_t, 2>, 4> line = {{{0, 0}, {10, 20}, {20, 40}, {10, 20}}};
    const std::array<std::array<int32_t, 2>, 4> path = {{{0, 0}, {12, 14}, {8, 40}, {12, 14}}};
    constexpr int32_t Margin = 1;
    const MCRect box{x - Margin, y - Margin, x + 30 + Margin, y + 60 + Margin};
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);

    for (const auto& [name, corners] : {std::pair{"concave", std::cref(kite)}, std::pair{"in a line", std::cref(line)},
                                        std::pair{"out and back", std::cref(path)}})
    {
        std::array<MCScreenVertex, 4> vertices{};

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            vertices[i] = MCScreenVertex{x + corners.get()[i][0], y + corners.get()[i][1], Color * Fixed, 0, 0, 0};
        }

        for (const bool gouraud : {false, true})
        {
            MCTest::Scope scope(std::format("{}, {}", name, gouraud ? "Gouraud" : "flat"));
            const auto polygon = [&]
            {
                if (gouraud)
                {
                    VfxGouraudPolygon(pane, vertices);
                }
                else
                {
                    VfxFlatPolygon(pane, vertices);
                }
            };

            const int64_t original = PixelsBetween(std::nullopt, Color, box, polygon);
            const int64_t smooth = PixelsBetween(pack, Color, box, polygon);
            std::cout << std::format("  {}, {}: pixels between the colours: {} with HD art off, {} on\n", name,
                                     gouraud ? "Gouraud" : "flat", original, smooth);
            CHECK_EQ(original, 0);
            CHECK(smooth > 0);

            // The screen's indices in the box, with HD art off and on.
            const auto indices = [&](const std::optional<std::filesystem::path>& from)
            {
                std::optional<MCHdArtOn> on;

                if (from.has_value())
                {
                    on.emplace(*from);
                }

                MCTestGame::RunFrame(FrameSeconds);
                polygon();
                const std::vector<uint8_t> screen = MCInput::Display()->ComposeScreen();
                const int32_t width = MCInput::Display()->Width();
                std::vector<uint8_t> inside;

                for (int32_t row = box.Y0; row <= box.Y1; ++row)
                {
                    for (int32_t column = box.X0; column <= box.X1; ++column)
                    {
                        inside.push_back(screen[static_cast<size_t>(row + pane.Y0) * width + column + pane.X0]);
                    }
                }

                return inside;
            };

            GamePaused = true;
            const std::vector<uint8_t> plain = indices(std::nullopt);
            const std::vector<uint8_t> sharp = indices(pack);
            GamePaused = false;
            CHECK(std::ranges::count(plain, Color) > 0);
            CHECK(plain == sharp);
        }
    }

    // The concave quad's inside, more than a window pixel and a half from its sides, shows its colour at every shown
    // pixel: the halves meet along the diagonal without a gap.
    std::array<MCScreenVertex, 4> vertices{};

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i] = MCScreenVertex{x + kite[i][0], y + kite[i][1], Color * Fixed, 0, 0, 0};
    }

    GamePaused = true;
    std::optional<MCHdArtOn> on(std::in_place, pack);
    MCTestGame::RunFrame(FrameSeconds);
    VfxGouraudPolygon(pane, vertices);
    const Shown seen = ReadShown(pack.parent_path());
    on.reset();
    GamePaused = false;

    const int32_t width = MCInput::Display()->Width();
    REQUIRE(width > 0);
    const float scale = static_cast<float>(seen.Width) / static_cast<float>(width);
    constexpr float Deep = 1.5f;
    constexpr float Centre = 0.5f;
    std::array<std::array<float, 2>, 4> at{};

    for (size_t i = 0; i < at.size(); ++i)
    {
        // The corners' pixels' centres, in the screen's window coordinates.
        at[i] = {static_cast<float>(pane.X0 + x + kite[i][0]) + Centre,
                 static_cast<float>(pane.Y0 + y + kite[i][1]) + Centre};
    }

    int32_t deep = 0;
    int32_t colored = 0;

    for (auto row = static_cast<int32_t>(static_cast<float>(pane.Y0 + box.Y0) * scale);
         row < static_cast<int32_t>(static_cast<float>(pane.Y0 + box.Y1 + 1) * scale); ++row)
    {
        for (auto column = static_cast<int32_t>(static_cast<float>(pane.X0 + box.X0) * scale);
             column < static_cast<int32_t>(static_cast<float>(pane.X0 + box.X1 + 1) * scale); ++column)
        {
            const std::array<float, 2> p = {(static_cast<float>(column) + Centre) / scale,
                                            (static_cast<float>(row) + Centre) / scale};
            // The kite is the triangles either side of its diagonal from the bent-in corner (3) to corner 1.
            const bool inside = InTriangle(p, at[3], at[0], at[1]) || InTriangle(p, at[1], at[2], at[3]);
            float nearest = std::numeric_limits<float>::max();

            for (size_t i = 0; i < at.size(); ++i)
            {
                nearest = std::min(nearest, SegmentDistance(p, at[i], at[(i + 1) % at.size()]));
            }

            if (inside && nearest > Deep)
            {
                ++deep;
                const size_t offset = (static_cast<size_t>(row) * seen.Width + column) * BytesPerPixel;
                const SDL_Color& want = colors[Color];
                colored += seen.Pixels[offset] == want.r && seen.Pixels[offset + 1] == want.g &&
                                   seen.Pixels[offset + 2] == want.b
                               ? 1
                               : 0;
            }
        }
    }

    std::cout << std::format("  the concave quad's inside: {} of {} shown pixels its colour\n", colored, deep);
    CHECK(deep > 0);
    CHECK_EQ(colored, deep);
    std::filesystem::remove_all(pack.parent_path());
}

/// <summary>
/// A laser's beam is two Gouraud quads whose corners' colours needn't lie on a plane: with HD art on, a quad whose tail
/// darkens again (head 0x30, sides 0x34, tail 0x31) draws, inside, only colour indices the original's walk draws (no
/// plane through three corners running past the fourth's colour).
/// </summary>
TEST_CASE_ISOLATED("game: HD art steps a Gouraud quad's colours as the original's walk does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_gouraud_quad");
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    constexpr int32_t Length = 60;
    constexpr int32_t Bulge = 10;
    constexpr uint8_t Head = 0x30;
    constexpr uint8_t Side = 0x34;
    constexpr uint8_t Tail = 0x31;
    constexpr int32_t Fixed = 1 << 16;
    // Head, side bulge, tail, and the bulge's centre on the beam's axis, as MCProjectileLaser::Render draws one side.
    const std::array<MCScreenVertex, 4> beam = {MCScreenVertex{x - Length, y - Length / 2, Head * Fixed, 0, 0, 0},
                                                MCScreenVertex{x + Bulge / 2, y - Bulge, Side * Fixed, 0, 0, 0},
                                                MCScreenVertex{x + Length, y + Length / 2, Tail * Fixed, 0, 0, 0},
                                                MCScreenVertex{x, y, Side * Fixed, 0, 0, 0}};
    const MCRect box{x - Length - 1, y - Length / 2 - Bulge, x + Length + 1, y + Length / 2 + 1};
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);

    // The beam's colour indices (Head..Side) its pixels show; the HD edges' blends match none.
    const auto indices = [&](const std::optional<std::filesystem::path>& from)
    {
        std::set<int32_t> shown;

        for (const SDL_Color& pixel : ChangedPixels(from, box, [&] { VfxGouraudPolygon(pane, beam); }))
        {
            for (int32_t index = Head; index <= Side + Side - Head; ++index)
            {
                if (SameColor(pixel, colors[static_cast<size_t>(index)]))
                {
                    shown.insert(index);
                    break;
                }
            }
        }

        return shown;
    };

    GamePaused = true;
    const std::set<int32_t> original = indices(std::nullopt);
    const std::set<int32_t> smooth = indices(pack);
    GamePaused = false;
    const auto list = [](const std::set<int32_t>& set)
    {
        std::string text;

        for (const int32_t index : set)
        {
            text += std::format(" {:#04x}", index);
        }

        return text;
    };

    std::cout << std::format("  indices shown: original{}, HD art{}\n", list(original), list(smooth));
    CHECK(original.contains(Head));
    CHECK(original.contains(Side));
    CHECK(std::ranges::includes(original, smooth));
    std::filesystem::remove_all(pack.parent_path());
}

namespace
{
    /// <summary>The folder of a pack at <paramref name="pack"/> its pictures go in (sprite/MCHdArt.h).</summary>
    std::filesystem::path PictureFolderOf(const std::filesystem::path& pack)
    {
        return pack / "menus";
    }

    /// <summary>
    /// Mission 1 drawn by <paramref name="drawing"/> in a window shown larger than the screen, the tactical map on its
    /// map page, and a pack holding the map's picture (the tests' pack: pixels doubled, colours inverted).
    /// </summary>
    std::optional<HdObjectScene> SetUpHdMapScene(MCGpuDrawing drawing)
    {
        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        HdObjectScene scene;
        scene.Renderer = StartScaledUp(drawing);

        if (scene.Renderer == nullptr)
        {
            return std::nullopt;
        }

        const std::shared_ptr<MCTacticalMap> map = TacticalMap();
        REQUIRE(map != nullptr);
        map->SetDisplayType(MCTacmapPage::Map);
        MCTestGame::RunFrame(FrameSeconds);
        const std::shared_ptr<MCTexture> texture = map->MapPort->Bitmap().Texture.lock();
        REQUIRE(texture != nullptr);
        DecodedFrame picture;
        picture.Width = texture->Width;
        picture.Height = texture->Height;
        picture.Index.assign(texture->Pixels.begin(), texture->Pixels.end());
        picture.Drawn.assign(picture.Index.size(), 1);
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        scene.Scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_map";
        std::filesystem::remove_all(scene.Scratch);
        scene.Pack = scene.Scratch / "pack";
        std::filesystem::create_directories(PictureFolderOf(scene.Pack));
        const std::string name =
            MCHdArt::PictureName(MCHdArt::PictureHash(texture->Pixels, picture.Width, picture.Height));
        WriteFramePngs(PictureFolderOf(scene.Pack) / name, picture, colors, TestPackScale, Inverted, false);
        WritePaletteRgb(scene.Pack, colors);
        return scene;
    }
}

/// <summary>
/// With HD art on and a pack holding the tactical map's picture, the map is drawn from it, turned and scaled as the
/// game maps it: the shown colours change, and only in the map (the window's top left), the game paused.
/// </summary>
TEST_CASE_ISOLATED("game: an HD art pack's picture shows in the tactical map")
{
    const std::optional<HdObjectScene> scene = SetUpHdMapScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    // As the mech bar's test: the frame before is drawn with HD art on from a pack with no frames.
    GamePaused = true;
    std::optional<MCHdArtOn> hdArt(std::in_place, EmptyPack("mc_tests_hd_empty_pack"));
    MCTestGame::RunFrame(FrameSeconds);
    MCTestGame::RunFrame(FrameSeconds);
    const Shown original = ReadShown(scene->Scratch);
    hdArt.reset();
    hdArt.emplace(scene->Pack);
    const int64_t drawnBefore = scene->Renderer->HdDrawn();
    MCTestGame::RunFrame(FrameSeconds);
    const Shown hd = ReadShown(scene->Scratch);
    hdArt.reset();
    GamePaused = false;

    CHECK(scene->Renderer->HdDrawn() > drawnBefore);
    const Changed changed = Compare(original, hd);
    std::cout << std::format("  {} shown pixels changed, about ({}, {})\n", changed.Count, changed.X, changed.Y);
    REQUIRE(changed.Count > 0);
    CHECK(changed.X < hd.Width / 4);
    CHECK(changed.Y < hd.Height / 2);
    std::filesystem::remove_all(scene->Scratch);
}

TEST_CASE_ISOLATED("game: HD art keeps the tactical map's palette indices")
{
    if (const std::optional<HdObjectScene> scene = SetUpHdMapScene(MCGpuDrawing::Mirror); scene.has_value())
    {
        CheckIndicesKept(*scene);
    }
}

/// <summary>
/// A picture the HD art pack has is drawn from it from the first frame the screen is sharp (HD art just turned on, the
/// game's first frame), not one frame on: copied onto mission 1's screen (shown larger) right after a frame shown with
/// HD art off, it is drawn from the pack at once, its shown colours are the pack's, and the screen's indices under it
/// are the picture's, as with HD art off.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws a picture from the pack on the screen's first sharp frame")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    // A picture of its own: upright stripes of four colours.
    constexpr int32_t Side = 32;
    constexpr int32_t Stripe = 8;
    constexpr uint8_t FirstColor = 0x30;
    const std::shared_ptr<MCWindow> picture = MCWindow::Make(Side, Side);

    for (int32_t y = 0; y < Side; ++y)
    {
        for (int32_t x = 0; x < Side; ++x)
        {
            picture->Pixels[static_cast<size_t>(y) * Side + x] = static_cast<uint8_t>(FirstColor + x / Stripe);
        }
    }

    MCRenderer::CreateTexture(*picture, MCTextureUse::Static);
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_first_frame";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    std::filesystem::create_directories(PictureFolderOf(pack));
    DecodedFrame frame;
    frame.Width = Side;
    frame.Height = Side;
    frame.Index.assign(picture->Bytes().begin(), picture->Bytes().end());
    frame.Drawn.assign(frame.Index.size(), 1);
    const std::string name = MCHdArt::PictureName(MCHdArt::PictureHash(picture->Bytes(), Side, Side));
    WriteFramePngs(PictureFolderOf(pack) / name, frame, colors, TestPackScale, Inverted, false);
    WritePaletteRgb(pack, colors);

    MCPane& screen = ScreenPort()->Frame();
    const int32_t left = (screen.X1 - screen.X0) / 2;
    const int32_t top = (screen.Y1 - screen.Y0) / 2;

    struct Copied
    {
        int64_t HdDrawn = 0;
        Shown Seen;
        std::vector<uint8_t> Indices;
    };

    // The copy is the first draw after a frame shown with HD art off; with hd, HD art goes on just before it.
    const auto copy = [&](bool hd)
    {
        MCTestGame::RunFrame(FrameSeconds);
        std::optional<MCHdArtOn> on;

        if (hd)
        {
            on.emplace(pack);
        }

        Copied copied;
        const int64_t before = renderer->HdDrawn();
        VfxPaneCopy(MCPane::Over(picture), 0, 0, screen, left, top, VfxNoColor);
        copied.HdDrawn = renderer->HdDrawn() - before;
        copied.Seen = ReadShown(scratch);
        copied.Indices = MCInput::Display()->ComposeScreen();
        return copied;
    };

    GamePaused = true;
    const Copied plain = copy(false);
    const Copied sharp = copy(true);
    GamePaused = false;

    CHECK_EQ(plain.HdDrawn, 0);
    CHECK_EQ(sharp.HdDrawn, 1);

    // The picture's place in the shown frame (the screen shown larger), a pixel in from its edges.
    const int32_t width = MCInput::Display()->Width();
    REQUIRE(width > 0);
    const float scale = static_cast<float>(sharp.Seen.Width) / static_cast<float>(width);
    const int32_t x0 = static_cast<int32_t>(std::ceil(static_cast<float>(screen.X0 + left) * scale)) + 1;
    const int32_t y0 = static_cast<int32_t>(std::ceil(static_cast<float>(screen.Y0 + top) * scale)) + 1;
    const int32_t x1 = static_cast<int32_t>(static_cast<float>(screen.X0 + left + Side) * scale) - 2;
    const int32_t y1 = static_cast<int32_t>(static_cast<float>(screen.Y0 + top + Side) * scale) - 2;
    const MCRect inside{x0, y0, x1, y1};
    const int32_t area = (x1 - x0 + 1) * (y1 - y0 + 1);
    const Changed changed = Compare(plain.Seen, sharp.Seen, inside);
    std::cout << std::format("  {} of the picture's {} shown pixels from the pack\n", changed.Count, area);
    CHECK(changed.Count > area * 3 / 4);

    // The indices under it are the picture's, both times.
    REQUIRE_EQ(sharp.Indices.size(), plain.Indices.size());
    int32_t kept = 0;

    for (int32_t y = 0; y < Side; ++y)
    {
        for (int32_t x = 0; x < Side; ++x)
        {
            const size_t at = static_cast<size_t>(screen.Y0 + top + y) * width + screen.X0 + left + x;
            const uint8_t index = picture->Pixels[static_cast<size_t>(y) * Side + x];
            kept += plain.Indices[at] == index && sharp.Indices[at] == index ? 1 : 0;
        }
    }

    CHECK_EQ(kept, Side * Side);
    MCRenderer::DestroyTexture(*picture);
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// A frame shown over a texture's pixels (<see cref="MCRenderer::ShowHdPicture"/>: a movie's HD frame over the window
/// it decodes into) is drawn where a copy of the texture onto a sharp screen covers it, in its own colours (the test
/// movie's red), and only there: the copy's other pixels show as they did, the screen's indices are the texture's
/// throughout, and once taken away the copy shows the pixels alone again.
/// </summary>
TEST_CASE_ISOLATED("game: a sharp screen shows a movie's HD frame over the part of its window a copy covers")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    // The window is larger than the movie's frame, which lies in from its corner, as a movie's pane does.
    constexpr int32_t Width = 24;
    constexpr int32_t Height = 16;
    constexpr int32_t FrameX = 4;
    constexpr int32_t FrameY = 2;
    constexpr int32_t FrameWidth = MCHdMovieFixture::Width / 2;
    constexpr int32_t FrameHeight = MCHdMovieFixture::Height / 2;
    constexpr uint8_t Color = 0x30;
    constexpr int RedTolerance = 24;
    const std::shared_ptr<MCWindow> window = MCWindow::Make(Width, Height);
    std::ranges::fill(window->Bytes(), Color);
    MCRenderer::CreateTexture(*window, MCTextureUse::Static);
    const std::shared_ptr<MCTexture> texture = window->Texture.lock();
    REQUIRE(texture != nullptr);

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_movie_copy";
    std::filesystem::remove_all(scratch);
    MCHdMovieFixture::Write(scratch / "MOVIE.ivf", MCHdMovieFixture::Bytes());
    // HD art on (a pack of nothing else) makes the screen sharp, as when the game finds a movie's HD frames.
    std::filesystem::create_directories(scratch / "pack");
    const MCHdArtOn on(scratch / "pack");
    auto movie = MCHdMovie::Open(scratch / "MOVIE.ivf");
    REQUIRE(movie.has_value());
    const auto frame = (*movie)->Next(window->Bytes().subspan(static_cast<size_t>(FrameY) * Width + FrameX), FrameWidth,
                                      FrameHeight, Width);
    REQUIRE(frame.has_value());

    MCPane& screen = ScreenPort()->Frame();
    const int32_t left = (screen.X1 - screen.X0) / 2;
    const int32_t top = (screen.Y1 - screen.Y0) / 2;

    struct Copied
    {
        int64_t HdDrawn = 0;
        Shown Seen;
        std::vector<uint8_t> Indices;
    };

    const auto copy = [&]()
    {
        MCTestGame::RunFrame(FrameSeconds);
        Copied copied;
        const int64_t before = renderer->HdDrawn();
        VfxPaneCopy(MCPane::Over(window), 0, 0, screen, left, top, VfxNoColor);
        copied.HdDrawn = renderer->HdDrawn() - before;
        copied.Seen = ReadShown(scratch);
        copied.Indices = MCInput::Display()->ComposeScreen();
        return copied;
    };

    GamePaused = true;
    const Copied plain = copy();
    MCRenderer::ShowHdPicture(*texture, *frame, FrameX, FrameY);
    const Copied shown = copy();
    MCRenderer::ShowHdPicture(*texture, nullptr, 0, 0);
    const Copied after = copy();
    GamePaused = false;

    CHECK_EQ(plain.HdDrawn, 0);
    CHECK_EQ(shown.HdDrawn, 1);
    CHECK_EQ(after.HdDrawn, 0);

    // Places in the shown frame (the screen shown larger), a pixel in from their edges.
    const int32_t displayWidth = MCInput::Display()->Width();
    REQUIRE(displayWidth > 0);
    const float scale = static_cast<float>(shown.Seen.Width) / static_cast<float>(displayWidth);
    const auto place = [&](int32_t x, int32_t y, int32_t width, int32_t height)
    {
        return MCRect{static_cast<int32_t>(std::ceil(static_cast<float>(screen.X0 + left + x) * scale)) + 1,
                      static_cast<int32_t>(std::ceil(static_cast<float>(screen.Y0 + top + y) * scale)) + 1,
                      static_cast<int32_t>(static_cast<float>(screen.X0 + left + x + width) * scale) - 2,
                      static_cast<int32_t>(static_cast<float>(screen.Y0 + top + y + height) * scale) - 2};
    };

    const MCRect inside = place(FrameX, FrameY, FrameWidth, FrameHeight);
    int32_t red = 0;

    for (int32_t y = inside.Y0; y <= inside.Y1; ++y)
    {
        for (int32_t x = inside.X0; x <= inside.X1; ++x)
        {
            const uint8_t* pixel = &shown.Seen.Pixels[(static_cast<size_t>(y) * shown.Seen.Width + x) * BytesPerPixel];
            const std::array<uint8_t, 3>& wanted = MCHdMovieFixture::Colors[0];
            red += std::abs(pixel[0] - wanted[0]) <= RedTolerance && std::abs(pixel[1] - wanted[1]) <= RedTolerance &&
                           std::abs(pixel[2] - wanted[2]) <= RedTolerance
                       ? 1
                       : 0;
        }
    }

    const int32_t area = (inside.X1 - inside.X0 + 1) * (inside.Y1 - inside.Y0 + 1);
    std::cout << std::format("  {} of the frame's {} shown pixels in its colour\n", red, area);
    CHECK_EQ(red, area);

    // Beside the frame (the window's left columns, and its rows below the frame) the copy shows the pixels as it did.
    CHECK_EQ(Compare(plain.Seen, shown.Seen, place(0, 0, FrameX, Height)).Count, 0);
    CHECK_EQ(
        Compare(plain.Seen, shown.Seen, place(0, FrameY + FrameHeight, Width, Height - FrameY - FrameHeight)).Count, 0);
    CHECK_EQ(Compare(plain.Seen, after.Seen, place(0, 0, Width, Height)).Count, 0);

    // The indices under the window are its pixels, every time.
    int32_t kept = 0;

    for (int32_t y = 0; y < Height; ++y)
    {
        for (int32_t x = 0; x < Width; ++x)
        {
            const size_t at = static_cast<size_t>(screen.Y0 + top + y) * displayWidth + screen.X0 + left + x;
            kept += plain.Indices[at] == Color && shown.Indices[at] == Color && after.Indices[at] == Color ? 1 : 0;
        }
    }

    CHECK_EQ(kept, Width * Height);
    MCRenderer::DestroyTexture(*window);
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// Tool: with <c>--hd-movie-pack &lt;pack folder&gt;</c>, a pilot movie played in a window on a sharp screen (mission
/// 1, shown larger) shows the pack's HD frames: every frame the movie decodes is drawn from its own HD frame, to the
/// movie's end and after it while its window is up; with HD art on and no HD frames, none is. With
/// <c>--shots &lt;folder&gt;</c>, the screen at the movie's frame 20 is written both ways (<c>pilot-original.png</c>,
/// <c>pilot-hd.png</c>).
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-movie-pack plays a pilot movie from the pack's HD frames on a sharp screen")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-movie-pack");

    if (!pack.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    constexpr std::string_view Name = "PILOT00A";
    constexpr uint32_t ShotFrame = 20;
    constexpr int32_t MaxDisplays = 600;
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_movie_play";
    std::filesystem::remove_all(scratch);
    // Packs of the one movie's HD frames and of nothing, so HD draws are the movie's.
    const std::filesystem::path movies = scratch / "movies-pack" / "movies";
    std::filesystem::create_directories(movies);
    std::filesystem::create_directories(scratch / "empty-pack");
    std::filesystem::copy_file(std::filesystem::path(*pack) / "movies" / std::format("{}.ivf", Name),
                               movies / std::format("{}.ivf", Name));
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    struct Played
    {
        uint32_t Frames = 0;
        uint32_t HdFrames = 0;
        bool HdThroughout = true;
        /// <summary>Displays after the movie's end, before its window goes, with its last frame from HD.</summary>
        int32_t HdAfterEnd = 0;
    };

    constexpr int32_t DisplaysAfterEnd = 8;

    const auto play = [&](const std::filesystem::path& folder, std::string_view shot)
    {
        const MCHdArtOn on(folder);
        std::unique_ptr<MCSmackTag> movie = SmackOpen(std::format("data\\movies\\{}.smk", Name), 0xfe000, -1);
        REQUIRE(movie != nullptr);
        // A window of its own on the screen, as the briefing's movie has (remapped to the game's palette), which stays
        // up after the movie's end until its owner takes it down.
        const MCPane& screen = ScreenPort()->Frame();
        const RECT area{(screen.X1 - screen.X0) / 2, (screen.Y1 - screen.Y0) / 2, movie->Player->Width(),
                        movie->Player->Height()};
        const std::shared_ptr<MCGuiSmackerWindow> window = MCMakeGui<MCGuiSmackerWindow>();
        window->Init(area, {});
        ScreenWindow()->AddChild(window);
        REQUIRE_EQ(window->StartSmackerMovie(std::move(movie), false), 0);
        Played played;

        for (int32_t display = 0; display < MaxDisplays && MovieOver == 0; ++display)
        {
            const uint32_t before = window->Movie->Player->FrameNum();
            const int64_t drawn = renderer->HdDrawn();
            MCTestGame::RunFrame(FrameSeconds);

            if (window->Movie->Player->FrameNum() == before)
            {
                continue;
            }

            ++played.Frames;
            played.HdFrames += renderer->HdDrawn() > drawn ? 1 : 0;
            played.HdThroughout = played.HdThroughout && window->HdMovie != nullptr;

            if (played.Frames == ShotFrame && shots.has_value())
            {
                ReadShown(scratch);
                std::filesystem::create_directories(*shots);
                std::filesystem::copy_file(scratch / "shown.png", std::filesystem::path(*shots) / shot,
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }

        // After the end, the window shows the last frame, decoded again as each frame falls due.
        for (int32_t display = 0; display < DisplaysAfterEnd; ++display)
        {
            const int64_t drawn = renderer->HdDrawn();
            MCTestGame::RunFrame(FrameSeconds);
            played.HdAfterEnd += window->HdMovie != nullptr && renderer->HdDrawn() > drawn ? 1 : 0;
        }

        window->EndSmackerMovie();
        return played;
    };

    GamePaused = true;
    const Played original = play(scratch / "empty-pack", "pilot-original.png");
    const Played hd = play(scratch / "movies-pack", "pilot-hd.png");
    GamePaused = false;
    std::cout << std::format("  {}: {} frames, {} from HD frames; without them {} frames, {} from HD frames\n", Name,
                             hd.Frames, hd.HdFrames, original.Frames, original.HdFrames);

    CHECK(hd.Frames > ShotFrame);
    CHECK_EQ(hd.HdFrames, hd.Frames);
    CHECK(hd.HdThroughout);
    CHECK_EQ(hd.HdAfterEnd, DisplaysAfterEnd);
    CHECK(original.Frames > ShotFrame);
    CHECK_EQ(original.HdFrames, 0u);
    std::filesystem::remove_all(scratch);
}

namespace
{
    /// <summary>
    /// How the HD art generator makes a loose shape table's frames (tools/upscale/make_object_pack.py's kinds): stills
    /// (forest edges, waypoint marks, the tactical map's part diagrams), an animation of frames of one size (sensor
    /// blips), or frames holding their blended pixels (smoke, shots, shadows: any table with translucent frames).
    /// </summary>
    enum class LooseKind : uint8_t
    {
        Still,
        Animated,
        Blended,
    };

    constexpr std::array<ObjectKindInfo, 3> LooseKinds = {
        {{"shape", false}, {"shape-animated", true}, {"shape-effect", true, true}}};

    /// <summary>A loose shape table as a pack's sequence (shapes/&lt;hash&gt;), its kind, and where it came from.</summary>
    struct LooseTable
    {
        Sequence Frames;
        LooseKind Kind = LooseKind::Still;
        std::string Source;
    };

    /// <summary>
    /// The game files whose shapes the pack keeps elsewhere (lower case, without extensions): the sprite manager's
    /// (mechs/, objects/), the crater PAKs (craters/), and the mechs' shadows (drawn with smooth edges instead).
    /// </summary>
    std::set<std::string> FilesKeptElsewhere()
    {
        std::set<std::string> names(CraterFiles.begin(), CraterFiles.end());
        names.insert("shadow");

        for (const auto part : std::views::split(std::string_view(MCHdArt::SpriteFiles()), ';'))
        {
            std::string name(std::string_view(part).substr(0, std::string_view(part).find('=')));
            std::ranges::transform(name, name.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (!name.empty())
            {
                names.insert(name);
            }
        }

        return names;
    }

    /// <summary>
    /// <paramref name="table"/> as a loose table: its frames decoded (translucent ones holding their blended pixels),
    /// and its kind; none when it draws nothing.
    /// </summary>
    std::optional<LooseTable> MakeLooseTable(std::span<const uint8_t> table, std::string source)
    {
        LooseTable loose;
        loose.Source = std::move(source);
        loose.Frames.Folder = MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(table));
        std::vector<DecodedFrame> frames;

        for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
        {
            auto decoded = Decode(table, frame);
            frames.push_back(decoded.has_value() ? std::move(*decoded) : DecodedFrame{});
        }

        const bool translucent = std::ranges::any_of(frames, [](const DecodedFrame& f) { return f.Translucent; });
        loose.Frames.Blended = translucent;

        for (DecodedFrame& frame : frames)
        {
            if (frame.Translucent)
            {
                LeaveOutUndrawn(frame, true);
            }

            if (std::ranges::find(frame.Drawn, 1) == frame.Drawn.end())
            {
                loose.Frames.Frames.emplace_back();
                continue;
            }

            loose.Frames.Frames.push_back(std::move(frame));
        }

        if (DrawnFrames(loose.Frames) == 0)
        {
            return std::nullopt;
        }

        // Frames of one size animate (a blip's); frames of several are stills (a forest's edges, full and half size).
        const std::vector<DecodedFrame>& made = loose.Frames.Frames;
        const auto first = std::ranges::find_if(made, [](const DecodedFrame& f) { return f.Width > 0; });
        const bool oneSize =
            std::ranges::all_of(made, [&first](const DecodedFrame& f)
                                { return f.Width == 0 || (f.Width == first->Width && f.Height == first->Height); });
        loose.Kind = translucent                                ? LooseKind::Blended
                     : DrawnFrames(loose.Frames) > 1 && oneSize ? LooseKind::Animated
                                                                : LooseKind::Still;
        return loose;
    }

    /// <summary>
    /// The shape tables the game loads whole outside the sprite, crater and icon files (every <c>.shp</c> file, and
    /// every packet of the other PAKs that is a shape table), in its FastFiles and its folders, each once: what
    /// MCVulkanRenderer::HdShape finds by its bytes' hash.
    /// </summary>
    std::vector<LooseTable> LooseShapeTables()
    {
        const std::set<std::string> elsewhere = FilesKeptElsewhere();
        std::set<uint64_t> seen;
        std::vector<LooseTable> tables;

        const auto add = [&](std::span<const uint8_t> table, std::string source)
        {
            if (VfxIsShapeTable(table) && seen.insert(MCHdArt::ShapeTableHash(table)).second)
            {
                if (std::optional<LooseTable> loose = MakeLooseTable(table, std::move(source)); loose.has_value())
                {
                    tables.push_back(std::move(*loose));
                }
            }
        };

        // A file by its path as the game opens it: a .shp whole, a PAK's packets each.
        const auto take = [&](const std::string& path)
        {
            std::string name(std::string_view(path).substr(path.find_last_of("\\/") + 1));
            std::ranges::transform(name, name.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::string stem = name.substr(0, name.find('.'));

            if (EndsWithIgnoringCase(name, ".shp") && !IsIconShapeFile(name))
            {
                MCFile file;

                if (file.Open(path) == 0)
                {
                    std::vector<uint8_t> bytes(file.FileSize());
                    file.Read(bytes);
                    add(bytes, path);
                }
            }
            else if (EndsWithIgnoringCase(name, ".pak") && !elsewhere.contains(stem))
            {
                MCPacketFile pak;

                if (pak.Open(path) != 0)
                {
                    return;
                }

                for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
                {
                    // The packet's size once it is sought: the bytes its loader reads.
                    if (pak.SeekPacket(packet) != 0)
                    {
                        continue;
                    }

                    std::vector<uint8_t> bytes(static_cast<size_t>(std::max(pak.GetPacketSize(), 0)));

                    if (!bytes.empty() && pak.ReadPacket(packet, bytes) >= 0)
                    {
                        add(bytes, std::format("{} packet {}", path, packet));
                    }
                }
            }
        };

        std::error_code error;

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(MCFileSystem::GameRoot(), error))
        {
            if (EndsWithIgnoringCase(entry.path().filename().string(), ".fst"))
            {
                auto archive = MCFastFile::Create(entry.path().filename().string());
                REQUIRE(archive.has_value());

                for (const MCFileEntry& file : (*archive)->GetEntries())
                {
                    take(std::string(file.GetName()));
                }
            }
        }

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(MCFileSystem::GameRoot(), error))
        {
            if (entry.is_regular_file(error))
            {
                take(std::filesystem::relative(entry.path(), MCFileSystem::GameRoot()).generic_string());
            }
        }

        return tables;
    }
}

/// <summary>
/// Tool: <c>--hd-extract-shapes &lt;folder&gt;</c> writes the shape tables the game loads outside the sprite, crater and
/// icon files (<see cref="LooseShapeTables"/>: trees' shadows, forests' burnt edges, sensor blips, smoke, shots, the
/// waypoint and pause marks, the tactical map's part diagrams...) as an HD art pack at their own size, for
/// tools/upscale/make_object_pack.py: each frame a PNG pair (translucent tables' holding their blended pixels), plus
/// <c>objects.json</c> (an appearance a kind: "shape" stills, "shape-animated", "shape-effect" blended), the shown colours
/// as palette.rgb and <c>sources.tsv</c> (each table's folder, kind, frames and file).
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-extract-shapes writes the shape tables loaded outside the sprite files")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-extract-shapes");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path pack(*out);
    std::filesystem::create_directories(pack);
    WritePaletteRgb(pack, colors);
    const std::vector<LooseTable> tables = LooseShapeTables();
    REQUIRE(!tables.empty());
    std::ofstream sources(pack / "sources.tsv");
    std::string json = "{\"appearances\": [";
    std::array<size_t, LooseKinds.size()> frames{};

    for (size_t kind = 0; kind < LooseKinds.size(); ++kind)
    {
        std::vector<Sequence> sequences;

        for (const LooseTable& table : tables)
        {
            if (static_cast<size_t>(table.Kind) == kind)
            {
                sequences.push_back(table.Frames);
                frames[kind] += DrawnFrames(table.Frames);
                sources << table.Frames.Folder.generic_string() << '\t' << LooseKinds[kind].Name << '\t'
                        << DrawnFrames(table.Frames) << '\t' << table.Source << '\n';
            }
        }

        WritePack(pack, sequences, colors, 1, [](SDL_Color color) { return color; });
        const ObjectKindInfo& info = LooseKinds[kind];
        json += std::format("{}\n {{\"id\": {}, \"kind\": \"{}\", \"animated\": {}, \"blended\": {}, \"delta\": false, "
                            "\"types\": [\"{}\"], \"sequences\": [",
                            kind == 0 ? "" : ",", kind, info.Name, info.Animated, info.Blended, info.Name);

        for (size_t i = 0; i < sequences.size(); ++i)
        {
            json += std::format("{}\n  {{\"folder\": \"{}\", \"frames\": [{}]}}", i == 0 ? "" : ",",
                                sequences[i].Folder.generic_string(), FramesJson(sequences[i]));
        }

        json += "]}";
    }

    std::ofstream(pack / "objects.json") << json << "]}\n";
    std::cout << std::format("  {} shape tables: {} still frames, {} animated, {} blended\n", tables.size(), frames[0],
                             frames[1], frames[2]);
}

/// <summary>
/// A shape table loaded outside the sprite files (here the forests' burnt edges, forest.shp, read whole as
/// MCMiscTerrainObjectType reads it) is drawn from the pack's frame kept under its bytes' hash (shapes/&lt;hash&gt;): the
/// shown colours change where it is drawn.
/// </summary>
TEST_CASE_ISOLATED("game: a shape table loaded outside the sprite files is drawn from the HD art pack")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    MCFile file;
    REQUIRE(file.Open(GamePath(SpritePath, "forest", ".shp")) == 0);
    MCRegisteredBlock block(file.FileSize(), MCDataKind::Shapes);
    file.Read(block.Bytes());
    file.Close();
    const std::optional<LooseTable> table = MakeLooseTable(block.Bytes(), "forest.shp");
    REQUIRE(table.has_value());
    REQUIRE(table->Frames.Frames[0].Width > 0);
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_loose_shape";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    WritePack(pack, std::span(&table->Frames, 1), colors, TestPackScale, Inverted);
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const auto drawn = [&]
    {
        MCTestGame::RunFrame(FrameSeconds);
        AGShapeDraw(pane, block.Bytes(), 0, x, y);
        const auto shown = display->ReadFrame();
        REQUIRE(shown.has_value());
        return *shown;
    };

    GamePaused = true;
    const std::vector<SDL_Color> original = drawn();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    const int64_t drawnBefore = renderer->HdDrawn();
    const std::vector<SDL_Color> hd = drawn();
    CHECK(renderer->HdDrawn() > drawnBefore);
    hdArt.reset();
    GamePaused = false;

    const auto changed = std::ranges::count_if(
        std::views::iota(size_t{0}, original.size()),
        [&](size_t i) { return original[i].r != hd[i].r || original[i].g != hd[i].g || original[i].b != hd[i].b; });
    std::cout << std::format("  forest.shp frame 0: {}x{}, {} shown pixels changed\n", table->Frames.Frames[0].Width,
                             table->Frames.Frames[0].Height, changed);
    CHECK(changed > 0);
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// Line art redrawn as smooth strokes: a staircase run (a line of slope 1/3, its pixels stepping) is straightened, so the
/// straight line's middle is covered where the steps left it bare; a square outline's corners stay sharp (covered at
/// the corner pixel's centre); each redrawn pixel keeps its art pixel's byte; far from the art nothing is drawn.
/// </summary>
TEST_CASE("sharp lines: line art redrawn as strokes is straightened, keeps its corners and its bytes")
{
    constexpr int32_t Columns = 16;
    constexpr int32_t Rows = 12;
    constexpr uint8_t No = MCSharpText::NoInk;
    constexpr uint8_t Step = 5;
    constexpr uint8_t Box = 9;
    std::array<uint8_t, Columns * Rows> art{};
    art.fill(No);
    const auto put = [&](int32_t x, int32_t y, uint8_t value) { art[static_cast<size_t>(y * Columns + x)] = value; };

    // A staircase from (0, 0) to (11, 3): runs of three, each a row lower.
    constexpr int32_t RunLength = 3;
    constexpr int32_t Runs = 4;

    for (int32_t x = 0; x < RunLength * Runs; ++x)
    {
        put(x, x / RunLength, Step);
    }

    // A 5 x 5 square outline from (2, 6) to (6, 10).
    constexpr int32_t BoxLeft = 2;
    constexpr int32_t BoxTop = 6;
    constexpr int32_t BoxSide = 5;

    for (int32_t i = 0; i < BoxSide; ++i)
    {
        put(BoxLeft + i, BoxTop, Box);
        put(BoxLeft + i, BoxTop + BoxSide - 1, Box);
        put(BoxLeft, BoxTop + i, Box);
        put(BoxLeft + BoxSide - 1, BoxTop + i, Box);
    }

    const std::vector<uint8_t> redrawn = MCSharpLines::Redraw(art, Columns, Columns, Rows);
    constexpr int32_t Scale = MCSharpText::Scale;
    REQUIRE_EQ(redrawn.size(), static_cast<size_t>(Columns * Scale * Rows * Scale * BytesPerPixel));

    // The byte and coverage of the redrawn pixel at (x, y) in art pixels.
    const auto at = [&](float x, float y)
    {
        const auto column = static_cast<size_t>(x * Scale);
        const auto row = static_cast<size_t>(y * Scale);
        const size_t offset = (row * Columns * Scale + column) * BytesPerPixel;
        return std::pair{redrawn[offset], redrawn[offset + 1]};
    };

    constexpr uint8_t Full = 255;
    // The straightened line through the run's ends' centres, (0.5, 0.5) to (11.5, 3.5), at x 3.0: y 1.32. The steps
    // left that point between rows 0 and 1 (the original's pixels there are (2, 0) and (3, 1)).
    constexpr float LineX = 3.0f;
    constexpr float LineY = 0.5f + (LineX - 0.5f) * 3.0f / 11.0f;
    CHECK_EQ(at(LineX, LineY).second, Full);
    CHECK_EQ(at(LineX, LineY).first, Step);
    // The box's corner pixel's centre, and the middle of a side.
    CHECK_EQ(at(BoxLeft + 0.5f, BoxTop + 0.5f).second, Full);
    CHECK_EQ(at(BoxLeft + 0.5f, BoxTop + 0.5f).first, Box);
    CHECK_EQ(at(BoxLeft + 2.5f, BoxTop + BoxSide - 0.5f).second, Full);
    // Inside the box, and far from everything.
    CHECK_EQ(at(BoxLeft + 2.5f, BoxTop + 2.5f).second, uint8_t{0});
    CHECK_EQ(at(14.5f, 10.5f).second, uint8_t{0});
}

namespace
{
    /// <summary>The mech store's shapes: the sold-out outlines, the pictures' repair shapes and the body diagrams' icons.</summary>
    enum class MCStoreShape : uint8_t
    {
        Outline,
        Picture,
        Diagram,
        Count
    };

    /// <summary>The draws of the store's shapes an audit saw: of each kind, how many, and how many sharp.</summary>
    struct MCStoreDraws
    {
        std::array<int32_t, static_cast<size_t>(MCStoreShape::Count)> All{};
        std::array<int32_t, static_cast<size_t>(MCStoreShape::Count)> Sharp{};

        int32_t AllOf(MCStoreShape kind) const { return All[static_cast<size_t>(kind)]; }
        int32_t SharpOf(MCStoreShape kind) const { return Sharp[static_cast<size_t>(kind)]; }
    };

    /// <summary>The screen's indices with its top band (the ticker and the clock, which move on) left out.</summary>
    std::vector<uint8_t> LogisticsIndices()
    {
        constexpr size_t ChromeRows = 0x10;
        std::vector<uint8_t> shown = MCInput::Display()->ComposeScreen();
        const size_t width = static_cast<size_t>(MCInput::Display()->Screen().XMax) + 1;
        shown.erase(shown.begin(),
                    shown.begin() + static_cast<std::ptrdiff_t>(std::min(shown.size(), width * ChromeRows)));
        return shown;
    }

    /// <summary>The frames a logistics screen is run for before it is looked at (a screen change's wipe settles).</summary>
    constexpr int32_t LogisticsSettleFrames = 30;

    void SettleLogistics()
    {
        for (int32_t frame = 0; frame < LogisticsSettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    /// <summary>
    /// Boots to logistics with the GPU drawing alone in a 1280x960 window (a screen drawn sharp at twice its size with
    /// HD art on) and starts a new campaign; null when the GPU can't draw alone.
    /// </summary>
    std::shared_ptr<MCVulkanRenderer> StartSharpLogistics()
    {
        constexpr int32_t Width = 1280;
        constexpr int32_t Height = 960;
        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
        REQUIRE(MCTestGame::StartLogistics());
        REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
        REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), Width, Height));
        SDL_SyncWindow(&MCInput::Display()->Window());
        NewCampaign();
        return std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    }

    /// <summary>
    /// Shows the logistics screen as it stands with HD art off, then on with <paramref name="pack"/> (the logistics'
    /// shapes tied to it; from the first frame drawn sharp, every draw given to <paramref name="audit"/>), and checks
    /// it keeps the indices it had (its top band left out).
    /// </summary>
    void CheckSharpKeepsIndices(const std::filesystem::path& pack, MCVulkanRenderer& renderer,
                                std::function<void(const MCVulkanRenderer::AuditedDraw&)> audit)
    {
        SettleLogistics();
        const std::vector<uint8_t> plain = LogisticsIndices();
        GHdArt = 1;
        MCHdArt::SetPackFolder(pack);
        GlobalLog()->AttachHdArt();
        // The screen is drawn at the window's size from the frame after HD art comes on.
        MCTestGame::RunFrame(FrameSeconds);
        renderer.Audit = std::move(audit);
        SettleLogistics();
        const std::vector<uint8_t> sharp = LogisticsIndices();
        renderer.Audit = nullptr;
        GHdArt = 0;
        MCHdArt::SetPackFolder({});
        REQUIRE_EQ(sharp.size(), plain.size());
        CHECK(sharp == plain);

        if (sharp != plain)
        {
            // Where they differ (window pixels below the top band).
            const auto width = static_cast<size_t>(MCInput::Display()->Screen().XMax) + 1;
            MCRect differs{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(), -1, -1};
            size_t count = 0;

            for (size_t i = 0; i < sharp.size(); ++i)
            {
                if (sharp[i] != plain[i])
                {
                    const auto x = static_cast<int32_t>(i % width);
                    const auto y = static_cast<int32_t>(i / width);
                    differs = MCRect{std::min(differs.X0, x), std::min(differs.Y0, y), std::max(differs.X1, x),
                                     std::max(differs.Y1, y)};
                    ++count;
                }
            }

            std::cout << std::format("  {} pixels differ, in ({}, {})..({}, {}) below the top band\n", count,
                                     differs.X0, differs.Y0, differs.X1, differs.Y1);
        }
    }
}

/// <summary>
/// The purchase screen's mech store on a sharp screen with HD art on: its sold-out rows' outlines are redrawn as smooth
/// strokes (line art), and, the store stocked, its rows' pictures (each mech's repair shapes, drawn in place) and body
/// diagrams (the mech icons, tied to the pack's icons) come from the pack; every such draw is sharp, and the screen
/// keeps the indices it has with HD art off.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws the purchase screen's mech pictures and outlines sharp and keeps their indices")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    REQUIRE(renderer != nullptr);
    GlobalLog()->SetUpPurchaseScreen(false);

    // A pack of the mechs' repair shapes (under their bytes' hashes) but their sold-out outlines, which only line art
    // can draw sharp, and the icons.
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    std::vector<Sequence> sequences;

    for (const MCRegisteredBlock& shapes : GlobalLog()->MechRepShapes)
    {
        std::optional<LooseTable> table = MakeLooseTable(shapes.Bytes(), "mechrep");
        REQUIRE(table.has_value());
        REQUIRE(table->Frames.Frames.size() > static_cast<size_t>(MCLogistics::MechSoldOutShape));
        table->Frames.Frames[static_cast<size_t>(MCLogistics::MechSoldOutShape)] = DecodedFrame{};
        sequences.push_back(std::move(table->Frames));
    }

    size_t translucent = 0;
    std::ranges::move(IconSequences(translucent), std::back_inserter(sequences));
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_store";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    WritePack(pack, sequences, colors, TestPackScale, Inverted);
    WritePaletteRgb(pack, colors);

    const auto kindOf = [](const uint8_t* table, int32_t frame) -> std::optional<MCStoreShape>
    {
        const MCLogistics& log = *GlobalLog();

        for (size_t i = 0; i < log.MechRepShapes.size(); ++i)
        {
            if (log.MechRepShapes[i].Bytes().data() == table)
            {
                return frame == MCLogistics::MechSoldOutShape ? MCStoreShape::Outline : MCStoreShape::Picture;
            }

            if (log.MechIconShapes[i].Bytes().data() == table)
            {
                return MCStoreShape::Diagram;
            }
        }

        return std::nullopt;
    };

    // The store with HD art off, then on (its draws audited), and whether they show the same indices.
    const auto compare = [&](std::string_view what)
    {
        MCTest::Scope scope{std::string(what)};
        MCStoreDraws draws;
        CheckSharpKeepsIndices(pack, *renderer,
                               [&](const MCVulkanRenderer::AuditedDraw& draw)
                               {
                                   if (draw.Command != "Shape")
                                   {
                                       return;
                                   }

                                   if (const std::optional<MCStoreShape> kind = kindOf(draw.Data.data(), draw.Frame);
                                       kind.has_value())
                                   {
                                       ++draws.All[static_cast<size_t>(*kind)];
                                       draws.Sharp[static_cast<size_t>(*kind)] += draw.Sharp ? 1 : 0;
                                   }
                               });
        std::cout << std::format("  {}: outlines {}/{}, pictures {}/{}, diagrams {}/{} sharp\n", what,
                                 draws.SharpOf(MCStoreShape::Outline), draws.AllOf(MCStoreShape::Outline),
                                 draws.SharpOf(MCStoreShape::Picture), draws.AllOf(MCStoreShape::Picture),
                                 draws.SharpOf(MCStoreShape::Diagram), draws.AllOf(MCStoreShape::Diagram));
        CHECK(draws.Sharp == draws.All);
        return draws;
    };

    const MCStoreDraws soldOut = compare("sold out");
    CHECK(soldOut.AllOf(MCStoreShape::Outline) > 0);
    constexpr int32_t Stock = 2;
    MCTestGame::StockStore(Stock);
    const MCStoreDraws stocked = compare("in stock");
    CHECK(stocked.AllOf(MCStoreShape::Picture) > 0);
    CHECK(stocked.AllOf(MCStoreShape::Diagram) > 0);
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// The logistics' pictures the game put together once and copied each frame are drawn in place, keeping the indices
/// with HD art on: the mission tab's briefing text (no copy of its 0xb3-wide picture; its letters drawn on the screen),
/// the component rows' icons (their component pictures, lscicc##, copied onto the screen) and the scroll columns
/// (their track tiles, scroll.tga, copied onto the screen).
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws the logistics' composed pictures in place and keeps their indices")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartSharpLogistics();
    REQUIRE(renderer != nullptr);
    const std::filesystem::path pack = EmptyPack("mc_tests_hd_composed");
    constexpr int32_t MissionPictureWidth = 0xb3;
    constexpr int32_t ComponentTab = 2;
    // Whether a draw copies one of the logistics art pictures named.
    const auto fromArt = [](const MCVulkanRenderer::AuditedDraw& draw, const std::set<std::string>& names)
    {
        return draw.Command == "Copy" && draw.From != nullptr &&
               std::ranges::any_of(names,
                                   [&](const std::string& name)
                                   {
                                       const std::shared_ptr<MCTexture> art =
                                           LogScreenArt(name)->Bitmap().Texture.lock();
                                       return art != nullptr && draw.From->Texture.lock() == art;
                                   });
    };

    {
        MCTest::Scope scope("the mission tab");
        // The briefing first starts its operation movie (its tab), then the mission tab is chosen.
        SettleLogistics();
        GlobalLog()->BriefingScreen->SetUpMission();
        int32_t pictureCopies = 0;
        int32_t glyphs = 0;
        CheckSharpKeepsIndices(pack, *renderer,
                               [&](const MCVulkanRenderer::AuditedDraw& draw)
                               {
                                   pictureCopies += draw.Command == "Copy" && draw.From != nullptr &&
                                                            draw.From->XMax + 1 == MissionPictureWidth
                                                        ? 1
                                                        : 0;
                                   glyphs += draw.Command == "Glyph" && draw.Sharp ? 1 : 0;
                               });
        CHECK_EQ(pictureCopies, 0);
        CHECK(glyphs > 0);
    }

    {
        MCTest::Scope scope("the component tab");
        GlobalLog()->CurrentInvTab = ComponentTab;
        GlobalLog()->SetUpPurchaseScreen(false);
        std::set<std::string> componentPictures;

        for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->ComponentInventory->Items)
        {
            componentPictures.insert(item->InventoryBlock->IconArt.Picture);
        }

        REQUIRE(!componentPictures.empty());
        const std::set<std::string> track = {"scroll.tga"};
        int32_t pictures = 0;
        int32_t tracks = 0;
        CheckSharpKeepsIndices(pack, *renderer,
                               [&](const MCVulkanRenderer::AuditedDraw& draw)
                               {
                                   pictures += fromArt(draw, componentPictures) ? 1 : 0;
                                   tracks += fromArt(draw, track) ? 1 : 0;
                               });
        CHECK(pictures > 0);
        CHECK(tracks > 0);
    }
}

/// <summary>
/// With HD art on (a pack with no frames), the tactical map's fog of war (a picture the game draws, one texel a terrain
/// vertex) is drawn with smooth edges at the output's resolution: of the map page's two mapped polygons a frame, the
/// fog's is drawn sharp and the map picture's (not in the pack) isn't.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws the tactical map's fog of war with smooth edges")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCVulkanRenderer> renderer = StartScaledUp(MCGpuDrawing::On);

    if (renderer == nullptr)
    {
        return;
    }

    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    REQUIRE(map != nullptr);
    map->SetDisplayType(MCTacmapPage::Map);
    const std::filesystem::path pack = EmptyPack("mc_tests_hd_fog");
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCTestGame::RunFrame(FrameSeconds);
    int32_t polygons = 0;
    int32_t sharp = 0;
    renderer->Audit = [&](const MCVulkanRenderer::AuditedDraw& draw)
    {
        if (draw.Command == "Polygon")
        {
            ++polygons;
            sharp += draw.Sharp ? 1 : 0;
        }
    };

    MCTestGame::RunFrame(FrameSeconds);
    renderer->Audit = nullptr;
    hdArt.reset();
    CHECK_EQ(polygons, 2);
    CHECK_EQ(sharp, 1);
    std::filesystem::remove_all(pack.parent_path());
}

/// <summary>
/// The system cursor's pictures come from the HD art pack: with HD art on and a pack holding a cursor shape's frame (its
/// PAK packet found by its bytes' hash, as the renderer finds it), the shape's picture has an HD copy, at the pack's
/// scale; without the pack, none.
/// </summary>
TEST_CASE_ISOLATED("game: the system cursor's pictures come from the HD art pack")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::span<const MCRegisteredBlock> shapes = GuiSystem().CursorShapes();
    const auto cursor = std::ranges::find_if(shapes, [](const MCRegisteredBlock& shape) { return !shape.Empty(); });
    REQUIRE(cursor != shapes.end());
    const std::optional<LooseTable> table = MakeLooseTable(cursor->Bytes(), "cursor");
    REQUIRE(table.has_value());
    REQUIRE(table->Frames.Frames[0].Width > 0);
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_cursor";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    WritePack(pack, std::span(&table->Frames, 1), colors, TestPackScale, Inverted);
    WritePaletteRgb(pack, colors);

    CHECK(MCCursorImageFromShape(cursor->Bytes(), 0).Hd == nullptr);
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    const MCCursorImage image = MCCursorImageFromShape(cursor->Bytes(), 0);
    hdArt.reset();
    REQUIRE(image.Hd != nullptr);
    CHECK_EQ(image.Hd->Scale, TestPackScale);
    CHECK_EQ(image.Hd->Rgba.size(),
             static_cast<size_t>(image.Width) * image.Height * TestPackScale * TestPackScale * 4);
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// Tool: <c>--hd-pictures &lt;pack&gt; --shots &lt;folder&gt; --hd-picture-names NAME[,NAME]</c> writes the menus'
/// pictures whose file names hold one of the names (ignoring case) as a keyed copy shows them, over dark grey: the
/// original at the pack's scale (nearest) beside the pack's copy, <c>&lt;file&gt;.png</c>.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-pictures pictures menu pictures beside the pack's copies")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-pictures");
    const std::optional<std::string_view> shots = MCTest::Option("shots");
    const std::optional<std::string_view> named = MCTest::Option("hd-picture-names");

    if (!pack.has_value() || !shots.has_value() || !named.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const auto lower = [](std::string text)
    {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };

    std::vector<std::string> names;

    for (const auto part : std::views::split(*named, ','))
    {
        names.push_back(lower(std::string(std::string_view(part))));
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> shown{};
    MCInput::Display()->GetShownColors(shown);
    MCHdArtOn hdArt(std::filesystem::absolute(std::filesystem::path(*pack)));
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);

    // The key (magenta, as the game's 6-bit palettes give it), and the grey both pictures are shown over.
    const auto keyed = [](SDL_Color color) { return color.r >= 252 && color.g == 0 && color.b >= 252; };
    constexpr uint8_t Under = 48;
    constexpr int32_t Gap = 8;
    int32_t skipped = 0;
    int32_t written = 0;
    ForEachPicture(
        shown, skipped,
        [&](std::string_view source, std::span<const uint8_t> pixels, int32_t width, int32_t height,
            std::span<const SDL_Color, 256> colors)
        {
            const std::string file = lower(std::string(source.substr(source.find_last_of("\\/") + 1)));

            if (std::ranges::none_of(names, [&](const std::string& name) { return file.contains(name); }))
            {
                return;
            }

            const std::shared_ptr<const MCHdArt::Frame> hd = MCHdArt::FindPicture(pixels, width, height);
            const int32_t scale = hd != nullptr ? hd->Scale : TestPackScale;
            const int32_t side = width * scale;
            const int32_t outWidth = side * 2 + Gap;
            const int32_t outHeight = height * scale;
            std::vector<uint8_t> out(static_cast<size_t>(outWidth) * outHeight * BytesPerPixel, Under);
            const std::vector<uint8_t> rgba = hd == nullptr ? std::vector<uint8_t>()
                                              : hd->Format == MCHdArt::ColorFormat::Bc7
                                                  ? MCBc7::Decode(hd->Colors, hd->Width, hd->Height)
                                                  : hd->Colors;

            for (int32_t y = 0; y < outHeight; ++y)
            {
                for (int32_t x = 0; x < side; ++x)
                {
                    const SDL_Color color = colors[pixels[static_cast<size_t>(y / scale) * width + x / scale]];
                    uint8_t* left = &out[(static_cast<size_t>(y) * outWidth + x) * BytesPerPixel];
                    uint8_t* right = left + static_cast<size_t>(side + Gap) * BytesPerPixel;
                    left[3] = right[3] = 255;

                    if (keyed(color))
                    {
                        continue;
                    }

                    left[0] = color.r;
                    left[1] = color.g;
                    left[2] = color.b;

                    if (!rgba.empty() && x < hd->Width && y < hd->Height)
                    {
                        // Premultiplied over the grey.
                        const uint8_t* texel = &rgba[(static_cast<size_t>(y) * hd->Width + x) * BytesPerPixel];

                        for (int32_t c = 0; c < 3; ++c)
                        {
                            right[c] = static_cast<uint8_t>(std::min(255, texel[c] + Under * (255 - texel[3]) / 255));
                        }
                    }
                }
            }

            SavePng(folder / (file.substr(0, file.find('.')) + ".png"), outWidth, outHeight, out);
            std::cout << std::format("  {}: {}x{}, {}\n", source, width, height,
                                     hd != nullptr ? std::format("HD {}x", hd->Scale) : "not in the pack");
            ++written;
        });

    CHECK(written > 0);
}

/// <summary>
/// Tool: <c>--hd-cursors &lt;pack&gt; --shots &lt;folder&gt;</c> pictures every cursor shape as the system cursor
/// shows it on a Retina MacBook (the UI 720 lines tall in 1890 pixels): <c>cursor_&lt;n&gt;.png</c>, the original
/// scaled by the nearest pixel beside the HD art pack's copy.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-cursors pictures the cursors beside the original")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-cursors");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path packFolder = std::filesystem::absolute(std::filesystem::path(*pack));
    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    constexpr float RetinaScale = 1890.0f / 720.0f;
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const std::span<const MCRegisteredBlock> shapes = GuiSystem().CursorShapes();
    MCHdArtOn hdArt(packFolder);
    int32_t pictured = 0;
    int32_t hd = 0;

    for (size_t i = 0; i < shapes.size(); ++i)
    {
        if (shapes[i].Empty())
        {
            continue;
        }

        MCCursorImage image = MCCursorImageFromShape(shapes[i].Bytes(), 0);

        if (image.Width == 0)
        {
            continue;
        }

        const MCCursorBitmap smooth = MCCursor::Rasterize(image, colors, RetinaScale, RetinaScale);
        hd += image.Hd != nullptr ? 1 : 0;
        image.Hd.reset();
        const MCCursorBitmap original = MCCursor::Rasterize(image, colors, RetinaScale, RetinaScale);
        std::vector<uint8_t> both(static_cast<size_t>(original.Width) * 2 * original.Height * 4, 0);

        for (int32_t y = 0; y < original.Height; ++y)
        {
            const size_t row = static_cast<size_t>(original.Width) * 4;
            std::ranges::copy_n(original.Rgba.begin() + static_cast<ptrdiff_t>(y * row), static_cast<ptrdiff_t>(row),
                                both.begin() + static_cast<ptrdiff_t>(y * row * 2));
            std::ranges::copy_n(smooth.Rgba.begin() + static_cast<ptrdiff_t>(y * row), static_cast<ptrdiff_t>(row),
                                both.begin() + static_cast<ptrdiff_t>(y * row * 2 + row));
        }

        SavePng(folder / std::format("cursor_{}.png", i), original.Width * 2, original.Height, both);
        ++pictured;
    }

    std::cout << std::format("  {} cursors pictured, {} from the pack\n", pictured, hd);
}

namespace
{
    /// <summary>
    /// One kind of the game's art: how many frames of it there are, how many the pack holds, a few it lacks, and whether
    /// the game draws it (counted in the total).
    /// </summary>
    struct ArtCoverage
    {
        std::string Name;
        size_t Frames = 0;
        size_t Found = 0;
        std::vector<std::string> Missing;
        bool Drawn = true;
    };

    /// <summary>How many of a kind's missing frames are named.</summary>
    constexpr size_t MissingNamed = 8;

    void NoteFrame(ArtCoverage& coverage, bool found, const std::function<std::string()>& what)
    {
        ++coverage.Frames;

        if (found)
        {
            ++coverage.Found;
        }
        else if (coverage.Missing.size() < MissingNamed)
        {
            coverage.Missing.push_back(what());
        }
    }

    /// <summary>Notes each drawn frame of <paramref name="sequences"/> (their folders' frames named by number).</summary>
    void NoteSequences(ArtCoverage& coverage, std::span<const Sequence> sequences)
    {
        for (const Sequence& sequence : sequences)
        {
            for (size_t f = 0; f < sequence.Frames.size(); ++f)
            {
                const DecodedFrame& frame = sequence.Frames[f];

                if (frame.Width == 0)
                {
                    continue;
                }

                const auto number = static_cast<int32_t>(f);
                NoteFrame(coverage,
                          MCHdArt::FindInFolder(sequence.Folder, number, frame.Width, frame.Height) != nullptr,
                          [&] { return std::format("{}/{}", sequence.Folder.generic_string(), f); });
            }
        }
    }
}

/// <summary>
/// Tool: <c>--hd-coverage &lt;pack&gt;</c> counts, for each kind of the game's art, how many of its frames the HD art
/// pack holds, as the extractors find them in the game's files: the mechs' and objects' sprites, the ground's tiles and
/// overlays, the craters, the mech bar's icons, the shape tables loaded outside the sprite files and the menus' and
/// logistics' pictures (font sheets left out: the GPU redraws the fonts). Names a few missing frames of each kind. Then
/// every other packet of the sprite PAKs, which nothing should draw, as a check on the extractors' walks (the pack holds
/// them when made with --hd-extract-objects --hd-packets undrawn), their frames as their appearance's kind keeps them.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-coverage counts the game's art the HD art pack holds")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-coverage");

    if (!pack.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path packFolder = std::filesystem::absolute(std::filesystem::path(*pack));
    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    MCHdArtOn hdArt(packFolder);
    std::vector<ArtCoverage> report;

    ArtCoverage& mechs = report.emplace_back(ArtCoverage{"mechs"});

    for (const auto& [treeName, packet] : MCTestGame::MechTrees())
    {
        const auto tree = std::static_pointer_cast<MCSpriteTree>(
            AppearanceTypeList().GetAppearance(0x01000000u | static_cast<uint32_t>(packet)));
        REQUIRE(tree != nullptr);
        NoteSequences(mechs, MechSequences(tree, std::nullopt));
    }

    std::map<std::string, ArtCoverage> objects;
    std::set<std::filesystem::path> walked;
    std::set<uint32_t> typed;

    for (const auto& [appearance, type] : PickedObjectAppearances())
    {
        typed.insert(appearance.Id & AppearancePacketMask);
        const ObjectKindInfo& kind = KindInfo(appearance.Kind);
        ArtCoverage& coverage =
            objects.try_emplace(std::string(kind.Name), ArtCoverage{std::string(kind.Name)}).first->second;
        const std::vector<Sequence> sequences = ObjectSequencesOf(type, kind.Blended).Sequences;
        NoteSequences(coverage, sequences);

        for (const Sequence& sequence : sequences)
        {
            walked.insert(sequence.Folder);
        }
    }

    for (auto& [name, coverage] : objects)
    {
        report.push_back(std::move(coverage));
    }

    // The packets of the sprite PAKs no type's walk above draws, as a check on the walks: the game's own lookups never
    // reach them (the original's zoomed-out views, buildings' packets 10 and 11, facings past a type's rotations), and
    // the appearances no type uses are named. A frame counts as --hd-packets undrawn extracts it: with its blended
    // pixels only for an appearance of a kind that keeps them (an appearance no type uses, of the kind its class gives).
    ArtCoverage& packets = report.emplace_back(ArtCoverage{"sprite PAK packets no type draws", 0, 0, {}, false});
    std::map<uint32_t, size_t> untyped;
    std::map<uint32_t, ObjectAppearance> kinds = FindObjectAppearances();
    kinds.merge(FindUntypedAppearances(kinds));
    std::map<uint32_t, bool> keepsBlended;

    for (const auto& [id, appearance] : kinds)
    {
        keepsBlended.emplace(id & AppearancePacketMask, KindInfo(appearance.Kind).Blended);
    }

    for (uint32_t appearance = 0; appearance < SpriteManager()->GetNumAppearances(); ++appearance)
    {
        for (int32_t packet = 0; packet < SpriteManager()->GetNumShapes(appearance); ++packet)
        {
            const std::shared_ptr<MCShape> shape =
                SpriteManager()->GetShapeData(appearance, static_cast<uint32_t>(packet), Turn, {});

            if (shape == nullptr || !VfxIsShapeTable(shape->FrameList()) || walked.contains(shape->HdFolder))
            {
                continue;
            }

            const std::span<const uint8_t> table = shape->FrameList();
            const auto keeps = keepsBlended.find(appearance);
            const bool keepBlended = keeps == keepsBlended.end() || keeps->second;

            for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
            {
                std::expected<DecodedFrame, std::string> decoded = Decode(table, frame);

                if (decoded.has_value() && decoded->Translucent)
                {
                    LeaveOutUndrawn(*decoded, keepBlended);
                }

                if (!decoded.has_value() || std::ranges::find(decoded->Drawn, 1) == decoded->Drawn.end())
                {
                    continue;
                }

                const bool found =
                    MCHdArt::FindInFolder(shape->HdFolder, frame, decoded->Width, decoded->Height) != nullptr;
                untyped[appearance] += typed.contains(appearance) ? 0 : 1;
                NoteFrame(packets, found,
                          [&] { return std::format("{}/{}", shape->HdFolder.generic_string(), frame); });
            }
        }
    }

    for (const auto& [appearance, frames] : untyped)
    {
        if (frames != 0)
        {
            std::cout << std::format("  sprite PAK appearance {} ({}): {} frames, no type uses it\n", appearance,
                                     SpriteManager()->ShapeFileName(appearance), frames);
        }
    }

    ArtCoverage& ground = report.emplace_back(ArtCoverage{"ground tiles"});
    std::set<std::string> tilesSeen;

    for (const std::string_view set : TileSets)
    {
        for (const std::string_view suffix : TileSetSuffixes)
        {
            MCPacketFile file;

            if (file.Open(std::format("data\\tiles\\{}{}.pak", set, suffix)) != 0)
            {
                continue;
            }

            for (int32_t packet = 0; packet < file.GetNumPackets(); ++packet)
            {
                if (file.SeekPacket(packet) != 0 || file.GetPacketSize() <= 0)
                {
                    continue;
                }

                std::vector<uint8_t> data(static_cast<size_t>(file.GetPacketSize()));

                if (file.ReadPacket(packet, data) < 0 || data.size() < 8 || !VfxTileFits(data))
                {
                    continue;
                }

                const std::span<const uint8_t> tile = std::span<const uint8_t>(data).first(TileRow(data, data[2]));
                const DecodedTile decoded = DecodeTile(tile);

                if (decoded.Width == 0 || !tilesSeen.insert(MCHdArt::TerrainTileName(tile)).second)
                {
                    continue;
                }

                NoteFrame(ground, MCHdArt::FindTerrainTile(tile, decoded.Width, decoded.Height) != nullptr,
                          [&] { return std::format("{}{} packet {}", set, suffix, packet); });
            }
        }
    }

    ArtCoverage& overlays = report.emplace_back(ArtCoverage{"terrain overlays"});
    ForEachOverlay(
        [&](std::span<const uint8_t>, std::span<const uint8_t> shape)
        {
            const DecodedFrame frame = DecodeOverlay(shape);

            if (std::ranges::find(frame.Drawn, 1) != frame.Drawn.end())
            {
                NoteFrame(overlays, MCHdArt::FindOverlay(shape, frame.Width, frame.Height) != nullptr,
                          [&] { return MCHdArt::TerrainTileName(shape); });
            }
        });

    size_t translucent = 0;
    NoteSequences(report.emplace_back(ArtCoverage{"craters"}), CraterSequences(translucent));
    NoteSequences(report.emplace_back(ArtCoverage{"mech bar icons"}), IconSequences(translucent));
    ArtCoverage& loose = report.emplace_back(ArtCoverage{"loose shape tables"});

    for (const LooseTable& table : LooseShapeTables())
    {
        NoteSequences(loose, std::span(&table.Frames, 1));
    }

    ArtCoverage& pictures = report.emplace_back(ArtCoverage{"menu pictures"});
    std::set<uint64_t> picturesSeen;
    int32_t skipped = 0;
    ForEachPicture(colors, skipped,
                   [&](std::string_view source, std::span<const uint8_t> pixels, int32_t width, int32_t height,
                       std::span<const SDL_Color, 256>)
                   {
                       std::string name(source);
                       std::ranges::transform(name, name.begin(),
                                              [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                       if (name.find("fnt") != std::string::npos || name.find("font") != std::string::npos ||
                           !picturesSeen.insert(MCHdArt::PictureHash(pixels, width, height)).second)
                       {
                           return;
                       }

                       NoteFrame(pictures, MCHdArt::FindPicture(pixels, width, height) != nullptr,
                                 [&] { return std::string(source); });
                   });

    size_t frames = 0;
    size_t found = 0;

    for (const ArtCoverage& coverage : report)
    {
        std::cout << std::format("  {}: {} of {} frames in the pack{}\n", coverage.Name, coverage.Found,
                                 coverage.Frames, coverage.Drawn ? "" : " (never drawn: not in the total)");

        if (!coverage.Drawn)
        {
            continue;
        }

        frames += coverage.Frames;
        found += coverage.Found;

        for (const std::string& missing : coverage.Missing)
        {
            std::cout << std::format("    missing: {}\n", missing);
        }
    }

    std::cout << std::format("  all: {} of {} frames ({} pictures unreadable)\n", found, frames, skipped);
}

namespace
{
    /// <summary>The grey a packet's pictures are shown over, and the gap between the original and the pack's copy.</summary>
    constexpr uint8_t PacketUnder = 48;
    constexpr int32_t PacketGap = 8;
    /// <summary>The frames of each packet --hd-packet-pictures pictures unless --hd-picture-frames says.</summary>
    constexpr int32_t PacketFramesPictured = 4;

    /// <summary>
    /// <paramref name="original"/>'s drawn pixels in <paramref name="colors"/>, each scaled up to
    /// <paramref name="hd"/>'s scale by the nearest pixel, beside <paramref name="hd"/> (premultiplied), both over grey.
    /// </summary>
    void SavePacketPicture(const std::filesystem::path& path, const DecodedFrame& original, const MCHdArt::Frame& hd,
                           std::span<const SDL_Color, 256> colors)
    {
        const int32_t scale = hd.Scale;
        const int32_t side = original.Width * scale;
        const int32_t width = side * 2 + PacketGap;
        const int32_t height = original.Height * scale;
        std::vector<uint8_t> out(static_cast<size_t>(width) * height * BytesPerPixel, PacketUnder);
        const std::vector<uint8_t> rgba =
            hd.Format == MCHdArt::ColorFormat::Bc7 ? MCBc7::Decode(hd.Colors, hd.Width, hd.Height) : hd.Colors;

        for (int32_t y = 0; y < height; ++y)
        {
            for (int32_t x = 0; x < side; ++x)
            {
                const size_t source = static_cast<size_t>(y / scale) * original.Width + x / scale;
                uint8_t* left = &out[(static_cast<size_t>(y) * width + x) * BytesPerPixel];
                uint8_t* right = left + static_cast<size_t>(side + PacketGap) * BytesPerPixel;
                left[3] = right[3] = std::numeric_limits<uint8_t>::max();

                if (original.Drawn[source] != 0)
                {
                    const SDL_Color color = colors[original.Index[source]];
                    left[0] = color.r;
                    left[1] = color.g;
                    left[2] = color.b;
                }

                if (x < hd.Width && y < hd.Height)
                {
                    const uint8_t* texel = &rgba[(static_cast<size_t>(y) * hd.Width + x) * BytesPerPixel];

                    for (int32_t c = 0; c < 3; ++c)
                    {
                        right[c] = static_cast<uint8_t>(std::min(255, texel[c] + PacketUnder * (255 - texel[3]) / 255));
                    }
                }
            }
        }

        SavePng(path, width, height, out);
    }
}

/// <summary>
/// Tool: <c>--hd-packet-pictures &lt;pack&gt; --shots &lt;folder&gt; --hd-folders &lt;folder,...&gt;</c> pictures sprite PAK
/// packets' frames beside the pack's copies, whether or not a type draws them (the original's zoomed-out views...): each
/// folder as the pack names it (<c>objects/&lt;shape PAK&gt;/&lt;appearance&gt;/&lt;packet&gt;</c>), its first
/// <c>--hd-picture-frames</c> frames (4 unless given) as <c>&lt;shape PAK&gt;_&lt;appearance&gt;_&lt;packet&gt;_&lt;frame&gt;.png</c>:
/// the original scaled up by the nearest pixel beside the pack's frame, over grey.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-packet-pictures pictures sprite packets beside the pack's copies")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-packet-pictures");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    int32_t frames = PacketFramesPictured;

    if (const std::optional<std::string_view> option = MCTest::Option("hd-picture-frames"); option.has_value())
    {
        const auto [end, error] = std::from_chars(option->data(), option->data() + option->size(), frames);
        MCTest::Scope scope(std::format("--hd-picture-frames {}", *option));
        REQUIRE((error == std::errc() && end == option->data() + option->size() && frames > 0));
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    MCHdArtOn hdArt(std::filesystem::absolute(std::filesystem::path(*pack)));
    const std::filesystem::path out(*shots);
    std::filesystem::create_directories(out);
    int32_t written = 0;

    for (const std::string& name : ListOption("hd-folders"))
    {
        // objects/<shape PAK>/<appearance>/<packet>
        const std::filesystem::path folder(name);
        const std::vector<std::filesystem::path> parts(folder.begin(), folder.end());
        MCTest::Scope scope(std::format("--hd-folders {}", name));
        REQUIRE(parts.size() == 4);
        const std::string shapeFile = parts[1].string();
        uint32_t appearance = 0;
        uint32_t packet = 0;

        for (const auto& [part, number] : {std::pair{parts[2].string(), &appearance}, {parts[3].string(), &packet}})
        {
            const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), *number);
            REQUIRE((error == std::errc() && end == part.data() + part.size()));
        }

        REQUIRE(SpriteManager()->ShapeFileName(appearance) == shapeFile);
        const std::shared_ptr<MCShape> shape = SpriteManager()->GetShapeData(appearance, packet, Turn, {});
        REQUIRE((shape != nullptr && VfxIsShapeTable(shape->FrameList())));
        int32_t pictured = 0;

        for (int32_t frame = 0; frame < VfxShapeCount(shape->FrameList()) && pictured < frames; ++frame)
        {
            std::expected<DecodedFrame, std::string> decoded = Decode(shape->FrameList(), frame);

            if (!decoded.has_value() || decoded->Width == 0)
            {
                continue;
            }

            // A translucent frame's colours 0 and 255 draw nothing; a frame with nothing drawn has no pack frame.
            if (decoded->Translucent)
            {
                LeaveOutUndrawn(*decoded, true);
            }

            if (std::ranges::find(decoded->Drawn, 1) == decoded->Drawn.end())
            {
                continue;
            }

            const std::shared_ptr<const MCHdArt::Frame> hd =
                MCHdArt::FindInFolder(shape->HdFolder, frame, decoded->Width, decoded->Height);
            std::cout << std::format("  {}/{}: {}x{}, {}\n", name, frame, decoded->Width, decoded->Height,
                                     hd != nullptr ? std::format("HD {}x", hd->Scale) : "not in the pack");

            if (hd != nullptr)
            {
                SavePacketPicture(out / std::format("{}_{}_{}_{}.png", shapeFile, appearance, packet, frame), *decoded,
                                  *hd, colors);
                ++pictured;
                ++written;
            }
        }
    }

    CHECK(written > 0);
}

/// <summary>
/// Tool: <c>--hd-map-shots &lt;pack&gt; --shots &lt;folder&gt;</c> pictures the ground across a mission's map
/// (<c>--mission</c>, default 1) as --hd-pack does: the camera moved to points of a grid across the map's diamond,
/// <c>ground_&lt;n&gt;_hd.png</c> (or _original.png with <c>--hd-original 1</c>) at each, the map revealed. On a big map
/// the HD ground's tiles lie in two atlases, so the pictures show both.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-map-shots pictures the ground across a map")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-map-shots");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    RevealAll();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    // A 3 x 3 grid inside the map's diamond, around its middle.
    constexpr std::array<float, 3> Fractions = {0.3f, 0.5f, 0.7f};
    const MCVector3D topLeft = MCTerrain::MapTopLeft3d100;
    int32_t shot = 0;

    for (const float down : Fractions)
    {
        for (const float across : Fractions)
        {
            Eye()->SetPosition(
                MCVector3D(topLeft.X + across * WorldUnitsMapSide, topLeft.Y - down * WorldUnitsMapSide, 0.0f));
            MCTestGame::RunFrame(FrameSeconds);
            MCTestGame::RunFrame(FrameSeconds);
            Shoot(folder, std::format("ground_{}", shot++), original);
        }
    }
}

namespace
{
    /// <summary>--hd-water's frames by default: five seconds.</summary>
    constexpr int32_t WaterFrames = 150;
    /// <summary>--hd-water's frames' length: 30 a second, a video's.</summary>
    constexpr float WaterFrameSeconds = 1.0f / 30.0f;
    /// <summary>--hd-water's shore is the tile with the most of both water and land within this many tiles each way.</summary>
    constexpr int32_t ShoreReach = 6;
    /// <summary>The terrain type of the tiles off the map (the corners outside its diamond).</summary>
    constexpr uint32_t OffMapTerrain = 0;
}

/// <summary>
/// Tool: <c>--hd-water &lt;pack&gt; --shots &lt;folder&gt;</c> pictures the water moving (port-only: HD lighting's
/// living water) as --hd-pack does: mission <c>--mission</c>'s map revealed, the camera over the shore with the most of
/// both water and land around it, the view zoomed to <c>--hd-water-zoom &lt;lines&gt;</c> if given, then
/// <c>--hd-water-frames &lt;n&gt;</c> frames (default 150) at 30 a second, <c>water_NNNN_hd.png</c> (or _original.png
/// with <c>--hd-original 1</c>), lit as <c>--hd-lighting</c> asks; <c>--hd-art-modes &lt;frame&gt;:&lt;mode&gt;,...</c>
/// switches the art as it plays (original, hd, lit, dusk: the debug console's art command).
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-water pictures the water moving")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-water");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    RevealAll();
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);

    if (const std::optional<std::string_view> zoom = MCTest::Option("hd-water-zoom"); zoom.has_value())
    {
        // strtof: libc++ has floating-point from_chars only from macOS 26.
        MCFixedZoomHeight = std::strtof(std::string(*zoom).c_str(), nullptr);
    }

    int32_t frames = WaterFrames;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-water-frames"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), frames);
    }

    // The shore: the tile with the most of the fewer of water and land around it, all of it on the map (terrain type 0
    // is off it, the corners outside its diamond).
    const int32_t width = GameMap().Width;
    const int32_t height = GameMap().Height;
    const auto typeAt = [&](int32_t row, int32_t column)
    { return GameMap().Map[static_cast<size_t>(row) * width + column].TerrainType(); };
    int32_t best = -1;
    int32_t bestRow = height / 2;
    int32_t bestColumn = width / 2;

    for (int32_t row = ShoreReach; row < height - ShoreReach; ++row)
    {
        for (int32_t column = ShoreReach; column < width - ShoreReach; ++column)
        {
            int32_t water = 0;
            int32_t land = 0;
            bool onMap = true;

            for (int32_t r = row - ShoreReach; r <= row + ShoreReach; ++r)
            {
                for (int32_t c = column - ShoreReach; c <= column + ShoreReach; ++c)
                {
                    const uint32_t type = typeAt(r, c);
                    onMap = onMap && type != OffMapTerrain;
                    ++(type >= FirstWaterTerrain ? water : land);
                }
            }

            if (onMap && std::min(water, land) > best)
            {
                best = std::min(water, land);
                bestRow = row;
                bestColumn = column;
            }
        }
    }

    std::cout << std::format("  the shore: tile ({}, {}), {} of the fewer of water and land around it\n", bestRow,
                             bestColumn, best);
    Eye()->SetPosition(MCVector3D(TileColToWorld(bestColumn), TileRowToWorld(bestRow), 0.0f));

    const std::map<int32_t, MCArtMode> artModes = ArtModesFromOptions();

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        SwitchArtMode(artModes, frame);
        MCTestGame::RunFrame(WaterFrameSeconds);
        Shoot(folder, std::format("water_{:04}", frame), original);
    }

    MCFixedZoomHeight = 0.0f;
}
