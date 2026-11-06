#include "stdafx.h"
#include "MCHdMovieFixture.h"
#include "fakes/MCNullAudioDevice.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCMoveSystem.h"
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
#include "lib/MCDice.h"
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
#include "sound/MCHdAudio.h"
#include "sound/MCSoundSystem.h"
#include "platform/MCAudio.h"
#include "gameos/MCSoundRenderer.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "main/MCMissionGlobals.h"
#include "object/MCArtillery.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCBullet.h"
#include "object/MCCommander.h"
#include "object/MCDebris.h"
#include "object/MCExplosion.h"
#include "object/MCForces.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCLaser.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverMath.h"
#include "object/MCObjectDrawing.h"
#include "object/MCPotentialContact.h"
#include "object/MCObjectSystem.h"
#include "object/MCProjectileLaser.h"
#include "object/MCSensorSystem.h"
#include "object/MCMiscTerrainObject.h"
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
#include "sprite/MCMechStops.h"
#include "sprite/MCPUAppearanceType.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "sprite/MCVfxAppearance.h"
#include "sprite/MCVfxAppearanceType.h"
#include "sprite/MCVfxBuildingAppearanceType.h"
#include "sprite/MCWeather.h"
#include "sprite/MCWeatherPace.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"
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
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMechBriefBlock.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCMatchOptionsTab.h"
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
#include "logistics/MCUpdatePage.h"
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
        // A window no taller than the UI (640x480 for 720 lines) shows the screen as it is.
        REQUIRE((height <= lines || MCInput::Display()->UnderlayScale() > 1.0f));
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
    /// The object nearest the player's mech in the mission that <paramref name="wanted"/> picks (it has an appearance):
    /// from the movers' part table (those that have entered the mission) and from the object lists.
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

        // The part table also holds the movers the script has yet to bring in (mission 5's nearest vehicle): they have
        // no tile in the object map, so the camera can't follow them (MCMover::IsRevealed reads its tile).
        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->GetExists() != 0)
            {
                consider(mover);
            }
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

    /// <summary>The object of <paramref name="objectClass"/> nearest the player's mech in the mission.</summary>
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

/// <summary>
/// The object tests' and tools' nearest vehicle is one in the mission, which the camera can follow: in mission 5 the
/// part table's nearest is one the script has yet to bring in (--hd-pack-objects crashed on it).
/// </summary>
TEST_CASE_ISOLATED("game: the object tests' nearest vehicle is one the camera can follow")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t WaitingVehicleMission = 5;
    REQUIRE(MCTestGame::StartMission(WaitingVehicleMission));
    const std::shared_ptr<MCGameObject> vehicle = NearestSubject(HdSubject::Vehicle);
    REQUIRE(vehicle != nullptr);
    REQUIRE(vehicle->GetExists() != 0);
    CHECK(vehicle->GetObjPosition() != nullptr);
    RevealAll();
    Eye()->ChangeTarget(vehicle, true);
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(LockObject(Eye()->TargetObject) == vehicle);
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

/// <summary>
/// A frame read from a PNG pair keeps the baked shadow its generator marked in the index picture's green (a forest's,
/// which the game doesn't mark): green 255 and the mark stay as they are, any other green has no index.
/// </summary>
TEST_CASE("hd art: a frame read from PNGs keeps the baked shadow its generator marked")
{
    constexpr int32_t Scale = 2;
    constexpr std::array<uint8_t, 4> Greens = {HasIndex, MCHdArt::BakedShadow, 0, 200};
    constexpr int32_t Width = static_cast<int32_t>(Greens.size());
    std::vector<uint8_t> color(static_cast<size_t>(Width * Scale) * Scale * BytesPerPixel, HasIndex);
    std::vector<uint8_t> index(color.size());

    for (int32_t y = 0; y < Scale; ++y)
    {
        for (int32_t x = 0; x < Width * Scale; ++x)
        {
            const size_t at = (static_cast<size_t>(y) * Width * Scale + x) * BytesPerPixel;
            std::ranges::copy(std::array<uint8_t, 4>{MCHdArt::BakedShadowIndex, Greens[x / Scale], 0, HasIndex},
                              index.begin() + static_cast<ptrdiff_t>(at));
        }
    }

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_marked_shadow";
    std::filesystem::create_directories(scratch);
    SavePng(scratch / "0.png", Width * Scale, Scale, color);
    SavePng(scratch / "0_idx.png", Width * Scale, Scale, index);
    const auto frame = MCHdArt::FrameFromPngs(scratch / "0.png", scratch / "0_idx.png", Scale);
    std::filesystem::remove_all(scratch);
    REQUIRE(frame.has_value());
    REQUIRE_EQ(frame->Indices.size(), Greens.size() * MCHdArt::IndexBytes);

    for (size_t x = 0; x < Greens.size(); ++x)
    {
        MCTest::Scope scope(std::format("pixel {}", x));
        CHECK_EQ(frame->Indices[x * MCHdArt::IndexBytes], MCHdArt::BakedShadowIndex);
        CHECK_EQ(frame->Indices[x * MCHdArt::IndexBytes + 1], x < 2 ? Greens[x] : uint8_t{0});
    }
}

namespace
{
    /// <summary>
    /// Writes an object's frame (not holding its blended pixels) as a PNG pair, <paramref name="stem"/>.png and _idx.png:
    /// <paramref name="width"/> x <paramref name="height"/> original pixels, <paramref name="scale"/> HD pixels each,
    /// opaque where <paramref name="indexAt"/> gives an index, in that index's colour of <paramref name="colors"/>; the
    /// pixels <paramref name="marked"/> gives (if any) marked as its baked shadow, as the pack's generator marks a forest's.
    /// </summary>
    void WriteObjectFrame(const std::filesystem::path& stem, int32_t width, int32_t height, int32_t scale,
                          std::span<const SDL_Color, 256> colors,
                          const std::function<std::optional<uint8_t>(int32_t, int32_t)>& indexAt,
                          const std::function<bool(int32_t, int32_t)>& marked = {})
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
                const uint8_t green =
                    original.has_value() && marked && marked(x / scale, y / scale) ? MCHdArt::BakedShadow : alpha;
                std::ranges::copy(std::array{c.r, c.g, c.b, alpha}, pixels.begin() + static_cast<ptrdiff_t>(at));
                std::ranges::copy(std::array<uint8_t, 4>{original.value_or(0), green, 0, HasIndex},
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

/// <summary>
/// Sun shadows over mission 1 drawn by the GPU, paused: a shadow the pack marked in art that casts none of its own (a
/// shape table's frame, as a forest's edges; MCHdArt::Frame::PaintedShadow) darkens the ground under it by day, and at
/// dusk too (where an object's is hidden), lengthened as far as the low sun's shadows are: the ground beyond it along
/// the sun's way darkens, the ground past that reach or below it doesn't. At night it is hidden, as an object's.
/// </summary>
TEST_CASE_ISOLATED("game: sun shadows keep a forest's painted shadow at dusk, lengthened as the low sun's")
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

    // 40 x 24 above its foot: the body in columns 0-7, its painted shadow in the rest.
    constexpr int32_t Width = 40;
    constexpr int32_t Height = 24;
    constexpr int32_t BodyColumns = 8;
    constexpr int32_t Top = -Height;
    constexpr uint8_t RowPixels = 2 * Width + 1;
    constexpr uint8_t RowEnd = 0;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Height; ++row)
    {
        rows.push_back(RowPixels);

        for (int32_t column = 0; column < Width; ++column)
        {
            rows.push_back(column < BodyColumns ? body : MCHdArt::BakedShadowIndex);
        }

        rows.push_back(RowEnd);
    }

    const std::vector<uint8_t> made = FramesTable(std::array{TableFrame{0, Top, Width, Height, rows}});
    MCRegisteredBlock table(made.size(), MCDataKind::Shapes);
    std::ranges::copy(made, table.Bytes().begin());

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_painted_shadow";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const std::filesystem::path folder = pack / MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(table.Bytes()));
    std::filesystem::create_directories(folder);
    constexpr int32_t Scale = 2;
    WriteObjectFrame(
        folder / "0", Width, Height, Scale, colors,
        [&](int32_t column, int32_t) { return column < BodyColumns ? body : MCHdArt::BakedShadowIndex; },
        [](int32_t column, int32_t) { return column >= BodyColumns; });
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const int32_t width = display->Width();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    constexpr float Darkness = 0.5f;
    lighting.SunShadow = Darkness;
    lighting.SunShadowRight = 1.0f;
    lighting.SunShadowUp = 1.0f;
    GHdSunShadows = 1;
    GamePaused = true;
    const int lightingWas = GHdLighting;

    // The ground, then the shape drawn on it.
    const auto drawn = [&]
    {
        MCTestGame::RunFrame(FrameSeconds);
        auto under = display->ReadFrame();
        REQUIRE(under.has_value());
        AGShapeDraw(pane, table.Bytes(), 0, x, y);
        auto over = display->ReadFrame();
        REQUIRE(over.has_value());
        return std::pair{std::move(*under), std::move(*over)};
    };

    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t column, int32_t row)
    { return frame[static_cast<size_t>(y + row + pane.Y0) * width + x + column + pane.X0]; };
    const auto luma = [](SDL_Color color) { return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b; };
    const auto same = [](SDL_Color a, SDL_Color b)
    { return std::abs(a.r - b.r) <= 3 && std::abs(a.g - b.g) <= 3 && std::abs(a.b - b.b) <= 3; };

    // Inside the shadow; beyond its top right corner along the sun's way (right and up), within the reach and past it;
    // and below it.
    constexpr std::pair<int32_t, int32_t> Inside = {Width - Width / 4, Top + Height / 2};
    constexpr int32_t Near = 8;
    constexpr int32_t Far = 80;
    constexpr std::pair<int32_t, int32_t> Along = {Width - 1 + Near, Top - Near};
    constexpr std::pair<int32_t, int32_t> Past = {Width - 1 + Far, Top - Far};
    constexpr std::pair<int32_t, int32_t> Below = {Width - Width / 4, Near};

    for (const int shown : {lightingWas, static_cast<int>(HdLightingDusk), static_cast<int>(HdLightingNight)})
    {
        GHdLighting = shown;
        const bool dusk = shown == HdLightingDusk;
        const bool night = shown == HdLightingNight;
        MCTest::Scope timeScope(dusk ? "dusk" : night ? "night" : "day");
        const std::array<float, 2> reach = MCSceneLights::PaintedShadowReach();
        CHECK_EQ(reach[0] > static_cast<float>(Near) && reach[1] > static_cast<float>(Near), dusk);
        CHECK(reach[0] < static_cast<float>(Far));
        const auto [under, over] = drawn();
        const auto darkened = [&](std::pair<int32_t, int32_t> place, float atMost)
        {
            const SDL_Color ground = at(under, place.first, place.second);
            return luma(at(over, place.first, place.second)) < luma(ground) * atMost;
        };

        const auto untouched = [&](std::pair<int32_t, int32_t> place)
        { return same(at(over, place.first, place.second), at(under, place.first, place.second)); };

        if (night)
        {
            CHECK(untouched(Inside));
        }
        else
        {
            CHECK(darkened(Inside, 1.0f - Darkness * 0.8f));
        }

        CHECK_EQ(darkened(Along, 1.0f - Darkness * 0.2f), dusk);
        CHECK(untouched(Past));
        CHECK(untouched(Below));
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

namespace
{
    /// <summary>
    /// A forest's frame for the forest tests, as a shape table's (<see cref="Table"/>, frame 0) and in an HD pack
    /// (<see cref="Pack"/>): Width x Height above its foot, its canopy in its left CanopyColumns in squares of two free
    /// colours far apart, either by a hash of their place (so no shift of it looks like it), its painted shadow in the
    /// rest (MCHdArt::Frame::PaintedShadow).
    /// </summary>
    struct TestForest
    {
        static constexpr int32_t Width = 96;
        static constexpr int32_t Height = 48;
        static constexpr int32_t CanopyColumns = 64;
        std::vector<uint8_t> Table;
        std::filesystem::path Pack;
    };

    /// <summary>Makes a <see cref="TestForest"/> in the palette's <paramref name="colors"/>, its pack in
    /// <paramref name="scratch"/> (emptied first).</summary>
    TestForest MakeTestForest(const std::filesystem::path& scratch, std::span<const SDL_Color, 256> colors)
    {
        // The canopy's two colours: free ones (as FullColorIndex's) with room to brighten, far apart.
        constexpr int32_t FirstFree = 10;
        constexpr int32_t EndFree = 246;
        constexpr uint8_t Least = 40;
        constexpr uint8_t Most = 160;
        constexpr float Apart = 40.0f;
        const auto canopyColor = [&](std::optional<uint8_t> other)
        {
            const auto free = std::views::iota(FirstFree, EndFree);
            const auto found = std::ranges::find_if(
                free,
                [&](int32_t i)
                {
                    const SDL_Color& c = colors[static_cast<size_t>(i)];
                    return SpecialColor[static_cast<size_t>(i)] == 0 && std::min({c.r, c.g, c.b}) >= Least &&
                           std::max({c.r, c.g, c.b}) <= Most &&
                           (!other.has_value() || std::abs(LumaOf(c) - LumaOf(colors[*other])) >= Apart);
                });
            REQUIRE(found != free.end());
            return static_cast<uint8_t>(*found);
        };

        const uint8_t firstShade = canopyColor(std::nullopt);
        const std::array<uint8_t, 2> shades = {firstShade, canopyColor(firstShade)};

        constexpr int32_t Width = TestForest::Width;
        constexpr int32_t Height = TestForest::Height;
        constexpr int32_t CanopyColumns = TestForest::CanopyColumns;
        constexpr int32_t Cell = 4;
        constexpr int32_t Top = -Height;
        constexpr uint32_t HashAcross = 374761393u;
        constexpr uint32_t HashDown = 668265263u;
        constexpr uint32_t HashMix = 1274126177u;
        constexpr int HashFold = 13;
        constexpr int HashBit = 16;
        const auto indexAt = [&](int32_t column, int32_t row) -> uint8_t
        {
            if (column >= CanopyColumns)
            {
                return MCHdArt::BakedShadowIndex;
            }

            uint32_t hash =
                static_cast<uint32_t>(column / Cell) * HashAcross + static_cast<uint32_t>(row / Cell) * HashDown;
            hash = (hash ^ (hash >> HashFold)) * HashMix;
            return shades[(hash >> HashBit) & 1u];
        };

        constexpr uint8_t RowPixels = 2 * Width + 1;
        constexpr uint8_t RowEnd = 0;
        std::vector<uint8_t> rows;

        for (int32_t row = 0; row < Height; ++row)
        {
            rows.push_back(RowPixels);

            for (int32_t column = 0; column < Width; ++column)
            {
                rows.push_back(indexAt(column, row));
            }

            rows.push_back(RowEnd);
        }

        TestForest forest{.Table = FramesTable(std::array{TableFrame{0, Top, Width, Height, rows}}),
                          .Pack = scratch / "pack"};
        std::filesystem::remove_all(scratch);
        const std::filesystem::path& pack = forest.Pack;
        const std::filesystem::path folder = pack / MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(forest.Table));
        std::filesystem::create_directories(folder);
        constexpr int32_t Scale = 2;
        WriteObjectFrame(folder / "0", Width, Height, Scale, colors, indexAt,
                         [](int32_t column, int32_t) { return column >= CanopyColumns; });
        WritePaletteRgb(pack, colors);
        return forest;
    }
}

/// <summary>
/// Forests swaying (GHdSway) over mission 1 drawn by the GPU, paused: a forest's frame from the pack drawn in the world
/// (a shape table's, its canopy in squares of two colours at random, its painted shadow beside it;
/// MCHdArt::Frame::PaintedShadow) shows as drawn with them off whatever the game's time, and the same at the same time
/// with them on. With them on, as the game's time goes on the canopy is drawn moved: its middle as a whole and in the
/// canopy's own colours, never twice its sway away, rocking to and fro and downwind on the average (the gusts lean it
/// before the wind); the ground round it and under its painted shadow stays put. Leaning downwind, it shows brighter,
/// by up to its light.
/// </summary>
TEST_CASE_ISOLATED("game: with HDSway a forest's canopy sways downwind as the game's time goes, the ground staying put")
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

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_sway";
    const TestForest forest = MakeTestForest(scratch, colors);
    constexpr int32_t Width = TestForest::Width;
    constexpr int32_t CanopyColumns = TestForest::CanopyColumns;
    MCRegisteredBlock table(forest.Table.size(), MCDataKind::Shapes);
    std::ranges::copy(forest.Table, table.Bytes().begin());
    const std::filesystem::path& pack = forest.Pack;

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    REQUIRE(camera->View() != nullptr);

    // The screen's pixels a meter of the ground takes (the most, east or north), and the wind's way on the screen.
    constexpr float Span = 100.0f;
    const MCVector2D origin = ProjectToScreen(MCVector3D(0.0f, 0.0f, 0.0f), *camera);
    const auto onScreen = [&](float east, float north)
    {
        const MCVector2D to = ProjectToScreen(MCVector3D(east, north, 0.0f), *camera);
        return std::array{to.X - origin.X, to.Y - origin.Y};
    };

    const std::array<float, 2> east = onScreen(Span, 0.0f);
    const std::array<float, 2> north = onScreen(0.0f, Span);
    const std::array<float, 2> wind = onScreen(MCSceneLights::WindEast * Span, MCSceneLights::WindNorth * Span);
    const float perMeter = std::max(std::hypot(east[0], east[1]), std::hypot(north[0], north[1])) / Span;
    const float windLength = std::hypot(wind[0], wind[1]);
    REQUIRE(perMeter > 0.0f);
    REQUIRE(windLength > 0.0f);
    const float alongWind = windLength / Span;

    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    // The sway, at most SwayPixels on the screen; the light it shows leaning downwind.
    constexpr float SwayPixels = 6.0f;
    constexpr float Light = 0.4f;
    lighting.Sway = SwayPixels / perMeter;
    const float now = ScenarioTime;
    GamePaused = true;

    // A frame of the world, with the forest drawn in it (a little right of the middle and up, clear of the units) or not.
    constexpr int32_t Aside = 30;
    const auto drawn = [&](bool forest, int32_t sways, float time, float light)
    {
        GHdSway = sways;
        ScenarioTime = time;
        lighting.SwayLight = light;
        MCTestGame::RunFrame(FrameSeconds);

        if (forest)
        {
            MCPane& world = camera->View()->WorldFrame();
            AGShapeDraw(world, table.Bytes(), 0, (world.X1 - world.X0) / 2 + Aside, (world.Y1 - world.Y0) / 2 - Aside);
        }

        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t x, int32_t y)
    { return frame[static_cast<size_t>(y) * width + x]; };

    // The frame on the screen, where it differs from the bare ground (near the middle: the minimap's blips blink), and
    // its canopy, the left CanopyColumns of its Width (each past its last pixel).
    const std::vector<SDL_Color> bare = drawn(false, 0, now, 0.0f);
    const std::vector<SDL_Color> still = drawn(true, 0, now, 0.0f);
    SaveShownFrame(still, width, height, "sway-still");
    int32_t frameLeft = width;
    int32_t frameRight = 0;
    int32_t frameTop = height;
    int32_t frameBottom = 0;

    for (int32_t y = height / 4; y < height * 3 / 4; ++y)
    {
        for (int32_t x = width / 4; x < width * 3 / 4; ++x)
        {
            if (!SameColor(at(bare, x, y), at(still, x, y)))
            {
                frameLeft = std::min(frameLeft, x);
                frameRight = std::max(frameRight, x + 1);
                frameTop = std::min(frameTop, y);
                frameBottom = std::max(frameBottom, y + 1);
            }
        }
    }

    REQUIRE(frameRight > frameLeft);
    REQUIRE(frameBottom > frameTop);
    const int32_t canopyLeft = frameLeft;
    const int32_t canopyRight = frameLeft + (frameRight - frameLeft) * CanopyColumns / Width;
    const int32_t canopyTop = frameTop;
    const int32_t canopyBottom = frameBottom;

    // The frame and Margin pixels round it but for its canopy and Fringe pixels round that (its edge, drawn smooth):
    // the pixels that differ from the still frame's.
    constexpr int32_t Margin = 8;
    constexpr int32_t Fringe = 2;
    const auto movedOutside = [&](const std::vector<SDL_Color>& frame)
    {
        size_t moved = 0;

        for (int32_t y = frameTop - Margin; y < frameBottom + Margin; ++y)
        {
            for (int32_t x = frameLeft - Margin; x < frameRight + Margin; ++x)
            {
                const bool canopy = x >= canopyLeft - Fringe && x < canopyRight + Fringe && y >= canopyTop - Fringe &&
                                    y < canopyBottom + Fringe;
                moved += canopy || SameColor(at(frame, x, y), at(still, x, y)) ? 0 : 1;
            }
        }

        return moved;
    };

    // The pixels of the frame and Margin round it that differ between two frames.
    const auto differing = [&](const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    {
        size_t count = 0;

        for (int32_t y = frameTop - Margin; y < frameBottom + Margin; ++y)
        {
            for (int32_t x = frameLeft - Margin; x < frameRight + Margin; ++x)
            {
                count += SameColor(at(a, x, y), at(b, x, y)) ? 0 : 1;
            }
        }

        return count;
    };

    // The canopy's own colours: each component's least and most, as drawn inside its edge.
    std::array<int32_t, 3> least = {255, 255, 255};
    std::array<int32_t, 3> most = {0, 0, 0};

    for (int32_t y = canopyTop + Fringe; y < canopyBottom - Fringe; ++y)
    {
        for (int32_t x = canopyLeft + Fringe; x < canopyRight - Fringe; ++x)
        {
            const SDL_Color c = at(still, x, y);
            const std::array<int32_t, 3> parts = {c.r, c.g, c.b};

            for (size_t part = 0; part < parts.size(); ++part)
            {
                least[part] = std::min(least[part], parts[part]);
                most[part] = std::max(most[part], parts[part]);
            }
        }
    }

    const int32_t contrast = most[0] - least[0] + most[1] - least[1] + most[2] - least[2];
    REQUIRE(contrast > 0);

    // The canopy's middle, Patch pixels square: the shift of the still canopy it best shows (within Reach), and how far
    // from that it is (each pixel's components, on the average).
    constexpr int32_t Patch = 16;
    constexpr auto Reach = static_cast<int32_t>(2.0f * SwayPixels) + 1;
    const int32_t patchLeft = (canopyLeft + canopyRight - Patch) / 2;
    const int32_t patchTop = (canopyTop + canopyBottom - Patch) / 2;
    REQUIRE(patchLeft - Reach >= canopyLeft);
    REQUIRE(patchLeft + Patch + Reach <= canopyRight);
    REQUIRE(patchTop - Reach >= canopyTop);
    REQUIRE(patchTop + Patch + Reach <= canopyBottom);
    const auto mismatch = [&](const std::vector<SDL_Color>& frame, int32_t dx, int32_t dy)
    {
        int64_t sum = 0;

        for (int32_t y = patchTop; y < patchTop + Patch; ++y)
        {
            for (int32_t x = patchLeft; x < patchLeft + Patch; ++x)
            {
                const SDL_Color a = at(frame, x, y);
                const SDL_Color b = at(still, x - dx, y - dy);
                sum += std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
            }
        }

        return static_cast<double>(sum) / (Patch * Patch);
    };

    struct Shift
    {
        int32_t Dx = 0;
        int32_t Dy = 0;
        double Mismatch = 0.0;
    };

    const auto shown = [&](const std::vector<SDL_Color>& frame)
    {
        Shift best{0, 0, std::numeric_limits<double>::max()};

        for (int32_t dy = -Reach; dy <= Reach; ++dy)
        {
            for (int32_t dx = -Reach; dx <= Reach; ++dx)
            {
                if (const double off = mismatch(frame, dx, dy); off < best.Mismatch)
                {
                    best = {dx, dy, off};
                }
            }
        }

        return best;
    };

    const auto downwind = [&](const Shift& shift) { return (shift.Dx * wind[0] + shift.Dy * wind[1]) / windLength; };

    // Every Step seconds for Steps.
    constexpr float Step = 0.5f;
    constexpr int32_t Steps = 60;
    constexpr int32_t Rounding = 2;
    size_t moved = 0;
    size_t foreign = 0;
    double worst = 0.0;
    double farthest = 0.0;
    double leaned = 0.0;
    float leanedAt = now;
    double downwindSum = 0.0;
    std::set<std::pair<int32_t, int32_t>> shifts;

    for (int32_t step = 0; step < Steps; ++step)
    {
        const float time = now + static_cast<float>(step) * Step;
        const std::vector<SDL_Color> frame = drawn(true, 1, time, 0.0f);
        moved += movedOutside(frame);

        // Inside its edge farther than the sway reaches (the edge, drawn smooth over the ground, sways with it).
        for (int32_t y = canopyTop + Fringe + Reach; y < canopyBottom - Fringe - Reach; ++y)
        {
            for (int32_t x = canopyLeft + Fringe + Reach; x < canopyRight - Fringe - Reach; ++x)
            {
                const SDL_Color c = at(frame, x, y);
                const std::array<int32_t, 3> parts = {c.r, c.g, c.b};
                const bool own = std::ranges::all_of(
                    std::views::iota(size_t{0}, parts.size()), [&](size_t part)
                    { return parts[part] >= least[part] - Rounding && parts[part] <= most[part] + Rounding; });
                foreign += own ? 0 : 1;
            }
        }

        const Shift shift = shown(frame);
        shifts.insert({shift.Dx, shift.Dy});
        worst = std::max(worst, shift.Mismatch);
        farthest = std::max(farthest, std::hypot(static_cast<double>(shift.Dx), static_cast<double>(shift.Dy)));
        downwindSum += downwind(shift);

        if (downwind(shift) > leaned)
        {
            leaned = downwind(shift);
            leanedAt = time;
        }
    }

    const double downwindMean = downwindSum / Steps;
    const std::vector<SDL_Color> leaning = drawn(true, 1, leanedAt, 0.0f);
    const std::vector<SDL_Color> again = drawn(true, 1, leanedAt, 0.0f);
    const std::vector<SDL_Color> lit = drawn(true, 1, leanedAt, Light);
    const std::vector<SDL_Color> stillLater = drawn(true, 0, leanedAt, 0.0f);
    SaveShownFrame(leaning, width, height, "sway-leaning");
    SaveShownFrame(lit, width, height, "sway-leaning-lit");

    // The light leaning downwind, and the lean's share of its most (its sway).
    double lumaLit = 0.0;
    double lumaUnlit = 0.0;

    for (int32_t y = patchTop; y < patchTop + Patch; ++y)
    {
        for (int32_t x = patchLeft; x < patchLeft + Patch; ++x)
        {
            lumaLit += LumaOf(at(lit, x, y));
            lumaUnlit += LumaOf(at(leaning, x, y));
        }
    }

    const double brighter = lumaLit / lumaUnlit;
    const double leanShare = std::clamp(leaned / alongWind / lighting.Sway, 0.0, 1.0);
    GamePaused = false;
    GHdSway = 0;
    ScenarioTime = now;
    lighting = kept;
    hdArt.reset();
    std::filesystem::remove_all(scratch);

    std::cout << std::format("  swayed up to {:.1f} pixels of {:.0f}, {:.1f} downwind at most and {:.1f} on the "
                             "average, through {} places; mismatch {:.1f} of {} at worst; {:.2f} as bright leaning "
                             "{:.0f}% of its sway\n",
                             farthest, SwayPixels, leaned, downwindMean, shifts.size(), worst, contrast, brighter,
                             100.0 * leanShare);
    CHECK(differing(stillLater, still) == 0);
    CHECK(differing(again, leaning) == 0);
    CHECK(moved == 0);
    CHECK(foreign == 0);
    CHECK(farthest >= 2.0);
    CHECK(farthest <= 2.0 * SwayPixels);
    CHECK(shifts.size() >= 3);
    CHECK(downwindMean > 0.0);
    CHECK(leanShare > 0.5);
    CHECK(worst < 0.25 * contrast);
    CHECK(brighter > 1.0 + 0.5 * Light * leanShare);
    CHECK(brighter < 1.0 + Light + 0.02);
}

/// <summary>
/// The sun's relief (GHdRelief) with HD lighting by day, over mission 1 drawn by the GPU, paused, forests swaying
/// (GHdSway, held still so the frames compare): a forest's frame from the pack drawn in the world shows its canopy with
/// relief on as with it off, as drawn, unless MCLighting::CanopyRelief gives it a share of the sun's relief; the ground
/// round it takes the relief whatever that share. The canopy's tiny bumps catching the sun glittered as it swayed.
/// </summary>
TEST_CASE_ISOLATED("game: the sun's relief spares a swaying forest's canopy, the ground round it lit by it")
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
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_canopy_relief";
    const TestForest forest = MakeTestForest(scratch, colors);
    MCRegisteredBlock table(forest.Table.size(), MCDataKind::Shapes);
    std::ranges::copy(forest.Table, table.Bytes().begin());
    std::optional<MCHdArtOn> hdArt(std::in_place, forest.Pack);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    REQUIRE(camera->View() != nullptr);
    const int lightingWas = GHdLighting;
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    GHdLighting = HdLightingOn;
    GHdSway = 1;
    lighting.Sway = 0.0f;
    lighting.SunRelief = 1.0f;
    GamePaused = true;

    // A frame of the world with relief on or off and the canopy's share of it, the forest drawn in it (a little right
    // of the middle and up, clear of the units) or not.
    constexpr int32_t Aside = 30;
    const auto shown = [&](bool drawForest, int relief, float canopy)
    {
        GHdRelief = relief;
        lighting.CanopyRelief = canopy;
        MCTestGame::RunFrame(FrameSeconds);

        if (drawForest)
        {
            MCPane& world = camera->View()->WorldFrame();
            AGShapeDraw(world, table.Bytes(), 0, (world.X1 - world.X0) / 2 + Aside, (world.Y1 - world.Y0) / 2 - Aside);
        }

        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    const std::vector<SDL_Color> bare = shown(false, 0, 0.0f);
    const std::vector<SDL_Color> flat = shown(true, 0, 0.0f);
    const std::vector<SDL_Color> spared = shown(true, 1, 0.0f);
    const std::vector<SDL_Color> lit = shown(true, 1, 1.0f);
    SaveShownFrame(lit, width, height, "canopy-relief-lit");
    SaveShownFrame(spared, width, height, "canopy-relief-spared");
    GHdRelief = 0;
    GHdSway = 0;
    GamePaused = false;
    GHdLighting = lightingWas;
    lighting = kept;
    hdArt.reset();
    std::filesystem::remove_all(scratch);

    // The frame on the screen, where it differs from the bare ground (near the middle: the minimap's blips blink), and
    // its canopy, the left CanopyColumns of its Width (each past its last pixel).
    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t x, int32_t y)
    { return frame[static_cast<size_t>(y) * width + x]; };
    int32_t frameLeft = width;
    int32_t frameRight = 0;
    int32_t frameTop = height;
    int32_t frameBottom = 0;

    for (int32_t y = height / 4; y < height * 3 / 4; ++y)
    {
        for (int32_t x = width / 4; x < width * 3 / 4; ++x)
        {
            if (!SameColor(at(bare, x, y), at(flat, x, y)))
            {
                frameLeft = std::min(frameLeft, x);
                frameRight = std::max(frameRight, x + 1);
                frameTop = std::min(frameTop, y);
                frameBottom = std::max(frameBottom, y + 1);
            }
        }
    }

    REQUIRE(frameRight > frameLeft);
    REQUIRE(frameBottom > frameTop);
    const int32_t canopyRight = frameLeft + (frameRight - frameLeft) * TestForest::CanopyColumns / TestForest::Width;

    // Inside the canopy's edge (Fringe pixels, drawn smooth): the pixels the relief changed, spared and with its share.
    // Round the frame, Margin pixels wide: the ground's, and those the canopy's share changed.
    constexpr int32_t Fringe = 2;
    constexpr int32_t Margin = 24;
    size_t canopyPixels = 0;
    size_t canopySpared = 0;
    size_t canopyLit = 0;
    size_t groundPixels = 0;
    size_t groundLit = 0;
    size_t groundByShare = 0;

    for (int32_t y = frameTop - Margin; y < frameBottom + Margin; ++y)
    {
        for (int32_t x = frameLeft - Margin; x < frameRight + Margin; ++x)
        {
            const bool canopy = x >= frameLeft + Fringe && x < canopyRight - Fringe && y >= frameTop + Fringe &&
                                y < frameBottom - Fringe;
            const bool ground = x < frameLeft || x >= frameRight || y < frameTop || y >= frameBottom;
            const SDL_Color under = at(flat, x, y);

            if (canopy)
            {
                ++canopyPixels;
                canopySpared += SameColor(at(spared, x, y), under) ? 0 : 1;
                canopyLit += SameColor(at(lit, x, y), under) ? 0 : 1;
            }
            else if (ground)
            {
                ++groundPixels;
                groundLit += SameColor(at(spared, x, y), under) ? 0 : 1;
                groundByShare += SameColor(at(spared, x, y), at(lit, x, y)) ? 0 : 1;
            }
        }
    }

    std::cout << std::format(
        "  the canopy: {} of {} pixels changed by the relief spared, {} with all of it; the ground "
        "round it: {} of {} lit by it, {} changed by the canopy's share\n",
        canopySpared, canopyPixels, canopyLit, groundLit, groundPixels, groundByShare);
    REQUIRE(canopyPixels > 0);
    CHECK_EQ(canopySpared, 0u);
    CHECK(canopyLit * 10 > canopyPixels);
    CHECK(groundLit * 10 > groundPixels);
    CHECK_EQ(groundByShare, 0u);
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
/// HD weather's drops catching the light (port-only, MCSceneLights::LightInAir and MCWeather::Glint): a drop by a light
/// that shines every way (a fire's) catches its colour, most at its height and less below or beside it, none past its
/// reach; one by a spotlight (a headlight) catches it only inside its beam, ahead of its lamp, not behind it nor beside
/// its beam. What a drop shows of it is the light's hue (as far from grey or further) in a few steps, none for a little
/// light, more for more, the drops whose facets turn less of it to the eye showing less. They catch none with the
/// setting at 0, more at dusk than by day and more at night.
/// </summary>
TEST_CASE("hd weather: a drop catches a light by it in the light's colour, a headlight's only in its beam, in steps")
{
    // The view's tilt (a screen height of the ground per its width), and a fire's light: its ground, height and reach.
    constexpr float Aspect = 0.5f;
    constexpr float FireX = 100.0f;
    constexpr float FireY = 100.0f;
    constexpr float FireHeight = 20.0f;
    constexpr float FireReach = 60.0f;
    const MCSceneLight fire{.X0 = FireX,
                            .Y0 = FireY,
                            .X1 = FireX,
                            .Y1 = FireY,
                            .Height = FireHeight,
                            .Reach = FireReach,
                            .Red = 1.0f,
                            .Green = 0.5f,
                            .Blue = 0.2f};
    const std::array lights{fire};
    const auto at = [&](std::span<const MCSceneLight> shining, float x, float y, float height)
    { return MCSceneLights::LightInAir(shining, x, y, height, Aspect); };

    const std::array<float, 3> inFire = at(lights, FireX, FireY, FireHeight);
    const std::array<float, 3> under = at(lights, FireX, FireY, 0.0f);
    const std::array<float, 3> beside = at(lights, FireX + 0.5f * FireReach, FireY, FireHeight);
    const std::array<float, 3> past = at(lights, FireX + 1.1f * FireReach, FireY, FireHeight);
    CHECK(std::abs(inFire[0] - fire.Red) < 1e-4f);
    CHECK(std::abs(inFire[1] / inFire[0] - fire.Green / fire.Red) < 1e-4f);
    CHECK(under[0] < inFire[0]);
    CHECK(under[0] > 0.0f);
    CHECK(beside[0] < inFire[0]);
    CHECK(beside[0] > 0.0f);
    CHECK(past[0] == 0.0f);

    // A headlight: its lamp over the ground at (LampX, LampY), its beam aimed across the screen at the ground Ahead on.
    constexpr float LampX = 300.0f;
    constexpr float LampY = 100.0f;
    constexpr float LampHeight = 20.0f;
    constexpr float Ahead = 70.0f;
    constexpr float BeamDegrees = 22.0f;
    const MCSceneLight headlight{.X0 = LampX,
                                 .Y0 = LampY,
                                 .X1 = LampX,
                                 .Y1 = LampY,
                                 .Height = LampHeight,
                                 .Reach = 180.0f,
                                 .Red = 0.8f,
                                 .Green = 0.75f,
                                 .Blue = 0.65f,
                                 .SpotX = Ahead,
                                 .SpotCos = std::cos(BeamDegrees * std::numbers::pi_v<float> / 180.0f)};
    const std::array spot{headlight};
    // Halfway along its beam's middle; as far behind its lamp; and as far ahead but well off to the side of the beam.
    const std::array<float, 3> inBeam = at(spot, LampX + 0.5f * Ahead, LampY, 0.5f * LampHeight);
    const std::array<float, 3> behind = at(spot, LampX - 0.5f * Ahead, LampY, 0.5f * LampHeight);
    const std::array<float, 3> aside = at(spot, LampX + 0.5f * Ahead, LampY + 0.5f * Ahead * Aspect, LampHeight);
    std::cout << std::format("  fire: {:.3f} in it, {:.3f} under it, {:.3f} beside it; headlight: {:.3f} in its beam, "
                             "{:.3f} behind, {:.3f} aside\n",
                             inFire[0], under[0], beside[0], inBeam[0], behind[0], aside[0]);
    CHECK(inBeam[0] > headlight.Red);
    CHECK(behind[0] == 0.0f);
    CHECK(aside[0] == 0.0f);

    // What a drop shows: steps of one hue (the light's, its red ahead of its green as far or further), none for a
    // little, never brighter for less (but for rounding).
    constexpr int32_t Samples = 200;
    constexpr float Rounding = 1e-5f;
    std::set<int32_t> steps;
    float lastShown = 0.0f;
    std::optional<float> hue;
    bool hueKept = true;
    bool neverLess = true;

    for (int32_t i = 0; i <= Samples; ++i)
    {
        const float light = 2.0f * static_cast<float>(i) / static_cast<float>(Samples);
        const std::array<float, 3> glint =
            MCWeather::Glint({light * fire.Red, light * fire.Green, light * fire.Blue}, 1.0f, 1.0f);
        steps.insert(static_cast<int32_t>(std::lround(glint[0] * 1000.0f)));
        neverLess = neverLess && glint[0] >= lastShown - Rounding;
        lastShown = glint[0];

        if (glint[0] > 0.0f)
        {
            hue = hue.value_or(glint[1] / glint[0]);
            hueKept = hueKept && std::abs(glint[1] / glint[0] - *hue) < Rounding;
        }
    }

    const std::array<float, 3> faint = MCWeather::Glint({0.05f, 0.025f, 0.01f}, 1.0f, 1.0f);
    const std::array<float, 3> fullFacet = MCWeather::Glint(inFire, 1.0f, 1.0f);
    const std::array<float, 3> leastFacet = MCWeather::Glint(inFire, 1.0f, 0.35f);
    CHECK(faint[0] == 0.0f);
    CHECK(steps.contains(0));
    CHECK(steps.size() >= 3);
    CHECK(steps.size() <= 5);
    CHECK(neverLess);
    REQUIRE(hue.has_value());
    CHECK(hueKept);
    CHECK(*hue > 0.0f);
    CHECK(*hue <= fire.Green / fire.Red);
    CHECK(lastShown <= 1.0f);
    CHECK(leastFacet[0] < fullFacet[0]);

    // How much they catch: none at 0, a little by day, more at dusk, more at night.
    const int lightingWas = GHdLighting;
    const MCTimeOfDay missionWas = MCSceneLights::MissionTime();
    const float settingWas = MCWeather::Settings().Light;
    const auto gainAt = [](MCTimeOfDay time, float setting)
    {
        GHdLighting = HdLightingOn;
        MCSceneLights::SetMissionTime(time);
        MCWeather::Settings().Light = setting;
        return MCWeather::CatchGain();
    };

    const float day = gainAt(MCTimeOfDay::Day, 1.0f);
    const float dusk = gainAt(MCTimeOfDay::Dusk, 1.0f);
    const float night = gainAt(MCTimeOfDay::Night, 1.0f);
    const float none = gainAt(MCTimeOfDay::Night, 0.0f);
    const float twice = gainAt(MCTimeOfDay::Dusk, 2.0f);
    GHdLighting = lightingWas;
    MCSceneLights::SetMissionTime(missionWas);
    MCWeather::Settings().Light = settingWas;
    CHECK(none == 0.0f);
    CHECK(day > 0.0f);
    CHECK(dusk > day);
    CHECK(night > dusk);
    CHECK(std::abs(twice - 2.0f * dusk) < 1e-5f);
}

/// <summary>
/// HD weather's drops and flakes catching the light (port-only, MCSceneLights::MCAirLights): the frame's lights sorted
/// once into the window's tiles light every point of the air exactly as all of them do (LightInAir), each sum to the
/// bit: lights that shine every way, beams and headlights, from a reach under a pixel to most of the window, inside
/// it and beyond its edges, at points inside it and beyond, near the ground and high over it.
/// </summary>
TEST_CASE("hd weather: the lights sorted into the window's tiles light the air exactly as all of them do")
{
    constexpr float Aspect = 0.6f;
    constexpr float Width = 700.0f;
    constexpr float Height = 420.0f;
    // Lights every LightStep pixels each way, from Beyond outside the window to as far past it, their reach, kind and
    // colour turning with their place; points every PointStep pixels, at each of the Heights.
    constexpr float Beyond = 150.0f;
    constexpr float LightStep = 90.0f;
    constexpr float PointStep = 7.0f;
    constexpr std::array Reaches{0.5f, 12.0f, 30.0f, 50.0f, 80.0f};
    // And one past the window's corner, reaching most of it.
    constexpr float FarReach = 600.0f;
    constexpr std::array Heights{0.0f, 9.0f, 30.0f};
    constexpr float BeamLength = 50.0f;
    constexpr float SpotAhead = 45.0f;
    constexpr float SpotCos = 0.92f;
    constexpr float LampStep = 10.0f;
    std::vector<MCSceneLight> lights;

    for (float y = -Beyond; y <= Height + Beyond; y += LightStep)
    {
        for (float x = -Beyond; x <= Width + Beyond; x += LightStep)
        {
            const size_t n = lights.size();
            MCSceneLight light{.X0 = x,
                               .Y0 = y,
                               .X1 = x,
                               .Y1 = y,
                               .Height = static_cast<float>(n % 3) * LampStep,
                               .Reach = Reaches[n % Reaches.size()],
                               .Red = 0.2f + 0.1f * static_cast<float>(n % 7),
                               .Green = 0.5f,
                               .Blue = 0.9f - 0.1f * static_cast<float>(n % 5)};

            if (n % 4 == 1)
            {
                light.X1 = x + BeamLength;
                light.Y1 = y + 0.5f * BeamLength;
            }
            else if (n % 4 == 2)
            {
                light.SpotX = SpotAhead;
                light.SpotY = -0.5f * SpotAhead;
                light.SpotCos = SpotCos;
            }

            lights.push_back(light);
        }
    }

    lights.push_back({.X0 = -Beyond,
                      .Y0 = -Beyond,
                      .X1 = -Beyond,
                      .Y1 = -Beyond,
                      .Reach = FarReach,
                      .Red = 0.3f,
                      .Green = 0.3f,
                      .Blue = 0.3f});
    const MCSceneLights::MCAirLights binned(lights, Width, Height, Aspect);
    int64_t points = 0;
    int64_t lit = 0;
    int64_t differ = 0;

    for (const float height : Heights)
    {
        for (float y = -Beyond; y <= Height + Beyond; y += PointStep)
        {
            for (float x = -Beyond; x <= Width + Beyond; x += PointStep)
            {
                const std::array<float, 3> all = MCSceneLights::LightInAir(lights, x, y, height, Aspect);
                ++points;
                lit += all[0] > 0.0f ? 1 : 0;
                differ += binned.At(x, y, height) != all ? 1 : 0;
            }
        }
    }

    std::cout << std::format("  {} lights, {} points: {} lit, {} differ\n", lights.size(), points, lit, differ);
    CHECK(lit > 0);
    CHECK(lit < points);
    CHECK(differ == 0);
    // None at all without lights or with the view on its edge, as LightInAir.
    CHECK(MCSceneLights::MCAirLights().Empty());
    const std::array<float, 3> flat =
        MCSceneLights::MCAirLights(lights, Width, Height, 0.0f).At(0.5f * Width, 0.5f * Height, 0.0f);
    CHECK(flat[0] == 0.0f);
    CHECK(flat[1] == 0.0f);
    CHECK(flat[2] == 0.0f);
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
/// HD weather's drops catching the light (MCWeatherSettings::Light), on the screen drawn by the GPU over mission 1 at
/// night in a downpour, paused, where the world shows clear of the interface, the rain falling as the game's time goes:
/// with the units' headlights on, the drops in their beams show brighter than without it, in the lamps' warm light, a
/// few of the world's pixels; with every unit shut down (no light at all) none shows brighter for it. The rain falls
/// the same with it or without.
/// </summary>
TEST_CASE_ISOLATED(
    "game: at night in a downpour the drops in a headlight's beam catch its light, and none away from it")
{
    // A mover's status shut down (MCMover::ShutDown).
    constexpr int32_t MechShutDown = 4;
    // A frame's step of the game's time, and the frames the rain falls for before it's looked at.
    constexpr float Step = 1.0f / 30.0f;
    constexpr int32_t Frames = 30;
    // A pixel shows brighter for it when a component rises this much; at least this many do in the headlights, at most
    // this share of the world.
    constexpr int32_t Brighter = 12;
    constexpr size_t LeastCaught = 20;
    constexpr double MostShare = 0.05;

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
    const float now = ScenarioTime;
    const float settingWas = MCWeather::Settings().Light;
    GamePaused = true;
    GHdLighting = HdLightingOn;
    MCSceneLights::SetMissionTime(MCTimeOfDay::Night);
    GHdWeather = HdWeatherDownpour;

    // The rain's last frame with the drops catching `light` as made, and how many drops and splashes there were.
    size_t count = 0;
    const auto rain = [&](float light)
    {
        MCWeather::Settings().Light = light;
        MCWeather::Clear();
        std::vector<SDL_Color> last;

        for (int32_t frame = 0; frame <= Frames; ++frame)
        {
            ScenarioTime = now + Step * static_cast<float>(frame);
            MCTestGame::RunFrame(FrameSeconds);
            auto shown = display->ReadFrame();
            REQUIRE(shown.has_value());
            REQUIRE(shown->size() == static_cast<size_t>(width) * height);
            last = std::move(*shown);
        }

        count = MCWeather::Count();
        return last;
    };

    const std::vector<SDL_Color> caught = rain(1.0f);
    const size_t caughtCount = count;
    const std::vector<SDL_Color> uncaught = rain(0.0f);
    const size_t uncaughtCount = count;

    // Every mech shut down (no headlights), then as it was.
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

    const std::vector<SDL_Color> darkCaught = rain(1.0f);
    const std::vector<SDL_Color> darkUncaught = rain(0.0f);

    for (const auto& [object, status] : statuses)
    {
        static_cast<MCBattleMech&>(*object).Status = status;
    }

    ScenarioTime = now;
    GamePaused = false;
    GHdWeather = HdWeatherOff;
    GHdLighting = HdLightingOff;
    MCSceneLights::SetMissionTime(MCTimeOfDay::Day);
    MCWeather::Settings().Light = settingWas;
    SaveShownFrame(caught, width, height, "rain-light-caught");
    SaveShownFrame(uncaught, width, height, "rain-light-uncaught");

    // The pixels of the world clear of the interface (as the rain's test) that show brighter in `a` than in `b`, and
    // how much their red and blue rose.
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    struct Rise
    {
        size_t Pixels = 0;
        int64_t Red = 0;
        int64_t Blue = 0;
    };

    const auto brighter = [&](const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    {
        Rise rise;

        for (int32_t y = top; y < height; ++y)
        {
            for (int32_t x = left; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                const int32_t red = a[i].r - b[i].r;
                const int32_t green = a[i].g - b[i].g;
                const int32_t blue = a[i].b - b[i].b;

                if (std::max({red, green, blue}) >= Brighter)
                {
                    ++rise.Pixels;
                    rise.Red += red;
                    rise.Blue += blue;
                }
            }
        }

        return rise;
    };

    const Rise lit = brighter(caught, uncaught);
    const Rise dark = brighter(darkCaught, darkUncaught);
    const size_t world = static_cast<size_t>(width - left) * static_cast<size_t>(height - top);
    std::cout << std::format(
        "  drops catching the headlights: {} pixels brighter of {} (red up {}, blue up {}); {} with "
        "no light; {} drops and splashes with it, {} without\n",
        lit.Pixels, world, lit.Red, lit.Blue, dark.Pixels, caughtCount, uncaughtCount);
    CHECK(caughtCount > 0);
    CHECK_EQ(caughtCount, uncaughtCount);
    CHECK(lit.Pixels >= LeastCaught);
    CHECK(static_cast<double>(lit.Pixels) <= MostShare * static_cast<double>(world));
    CHECK(lit.Red > lit.Blue);
    CHECK_EQ(dark.Pixels, size_t{0});
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
/// HD weather (GHdWeather), on the screen drawn by the GPU over mission 1, paused, where the world shows clear of the
/// interface: off, nothing falls. In rain, as the game's time goes on drops fall over the view and splash, under an
/// overcast that darkens the world; while the game's time holds, they hold; the same times show the same rain, its dice
/// starting again (MCWeather::Clear), and none of it throws the game's dice. Off again, the world shows as drawn.
/// </summary>
TEST_CASE_ISOLATED("game: rain falls as the game's time goes, under an overcast, and never touches the game")
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
    const float now = ScenarioTime;
    GamePaused = true;
    // A frame's step of the game's time, and the frames the rain falls for before it's looked at.
    constexpr float Step = 1.0f / 30.0f;
    constexpr int32_t Frames = 30;
    // Under the rain's overcast the world's middle brightness is at most this share of the clear sky's.
    constexpr float Overcast = 0.85f;

    const auto shown = [&](float time)
    {
        ScenarioTime = time;
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    // The rain's last frame, the same time drawn again, and how many drops and splashes there were.
    struct Run
    {
        std::vector<SDL_Color> Last;
        std::vector<SDL_Color> Held;
        size_t Count = 0;
    };

    const auto run = [&](int weather)
    {
        GHdWeather = weather;
        MCWeather::Clear();
        Run made;

        for (int32_t frame = 0; frame <= Frames; ++frame)
        {
            made.Last = shown(now + Step * static_cast<float>(frame));
        }

        made.Count = MCWeather::Count();
        made.Held = shown(now + Step * static_cast<float>(Frames));
        return made;
    };

    // Clear, then rain twice, then clear again; the game paused, nothing else throws its dice.
    const Run clear = run(HdWeatherOff);
    const uint32_t dice = MCGameContext::Current().Random().State();
    const Run first = run(HdWeatherRain);
    const Run second = run(HdWeatherRain);
    CHECK(MCGameContext::Current().Random().State() == dice);
    const Run after = run(HdWeatherOff);
    ScenarioTime = now;
    SaveShownFrame(clear.Last, width, height, "weather_clear");
    SaveShownFrame(first.Last, width, height, "weather_rain");

    // The world clear of the interface (as the particles' test).
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    size_t changed = 0;
    size_t moved = 0;
    size_t unlike = 0;
    size_t leftOver = 0;
    std::vector<float> clearLumas;
    std::vector<float> rainLumas;

    for (int32_t y = top; y < height; ++y)
    {
        for (int32_t x = left; x < width; ++x)
        {
            const size_t i = static_cast<size_t>(y) * width + x;
            changed += SameColor(first.Last[i], clear.Last[i]) ? 0 : 1;
            moved += SameColor(first.Held[i], first.Last[i]) ? 0 : 1;
            unlike += SameColor(first.Last[i], second.Last[i]) ? 0 : 1;
            leftOver += SameColor(after.Last[i], clear.Last[i]) ? 0 : 1;
            clearLumas.push_back(LumaOf(clear.Last[i]));
            rainLumas.push_back(LumaOf(first.Last[i]));
        }
    }

    const auto median = [](std::vector<float>& lumas)
    {
        std::ranges::nth_element(lumas, lumas.begin() + static_cast<std::ptrdiff_t>(lumas.size() / 2));
        return lumas[lumas.size() / 2];
    };

    const float clearMedian = median(clearLumas);
    const float rainMedian = median(rainLumas);
    std::cout << std::format(
        "  rain: {} drops and splashes, {} pixels changed, middle brightness {:.1f} (clear {:.1f}); "
        "{} moved while the time held, {} unlike the second time, {} left once clear\n",
        first.Count, changed, rainMedian, clearMedian, moved, unlike, leftOver);
    CHECK(clear.Count == 0);
    CHECK(first.Count > 0);
    CHECK(changed > 0);
    CHECK(rainMedian < Overcast * clearMedian);
    CHECK(moved == 0);
    CHECK(second.Count == first.Count);
    CHECK(unlike == 0);
    CHECK(after.Count == 0);
    CHECK(leftOver == 0);
}

/// <summary>
/// HD weather's lightning, on the screen drawn by the GPU over mission 1, paused, where the world shows clear of the
/// interface: in a downpour (its strikes made StrikesTimes as often) as the game's time goes the world flashes brighter
/// than the overcast now and then and dims again; each strike's thunder comes after its flash; the same times show the
/// same lightning, its dice starting again (MCWeather::Clear), and none of it throws the game's dice. Without its flash
/// the downpour holds as bright.
/// </summary>
TEST_CASE_ISOLATED(
    "game: in a downpour lightning lights the world now and then, its thunder after, never touching the game")
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
    const float now = ScenarioTime;
    GamePaused = true;
    // The sound takes no thunder here: the test does.
    const int32_t usedSound = UseSound;
    UseSound = 0;
    GHdWeather = HdWeatherDownpour;
    // Strikes about once a second (a downpour's two a minute, 30 times), over 4 seconds of frames.
    constexpr float StrikesTimes = 30.0f;
    constexpr float Step = 1.0f / 30.0f;
    constexpr int32_t Frames = 120;
    // A flash lights the world's mean brightness at least this much (of 255) over the dimmest frame's.
    constexpr float Flashed = 8.0f;
    // Without the flash, the frames' mean brightness strays no more than this (the drops falling).
    constexpr float Steady = 3.0f;
    MCWeather::Settings().Strikes = StrikesTimes;

    // Each frame's mean brightness of the world clear of the interface, and the frames a strike's thunder came on.
    struct Run
    {
        std::vector<float> Brightness;
        std::vector<int32_t> Thunder;
        int32_t FirstFlash = -1;
    };

    const auto run = [&](float flash)
    {
        MCWeather::Clear();
        MCWeather::Settings().Flash = flash;
        Run made;

        for (int32_t frame = 0; frame <= Frames; ++frame)
        {
            ScenarioTime = now + Step * static_cast<float>(frame);
            MCTestGame::RunFrame(FrameSeconds);
            const auto shown = display->ReadFrame();
            REQUIRE(shown.has_value());
            double sum = 0.0;
            size_t count = 0;

            for (int32_t y = height / 4; y < height; ++y)
            {
                for (int32_t x = width / 3; x < width; ++x)
                {
                    sum += LumaOf((*shown)[static_cast<size_t>(y) * width + x]);
                    ++count;
                }
            }

            made.Brightness.push_back(static_cast<float>(sum / static_cast<double>(count)));

            if (made.FirstFlash < 0 && MCWeather::ShownFlash().Light > 0.0f)
            {
                made.FirstFlash = frame;
            }

            if (MCWeather::TakeThunder().has_value())
            {
                made.Thunder.push_back(frame);
            }
        }

        return made;
    };

    const uint32_t dice = MCGameContext::Current().Random().State();
    const Run first = run(1.0f);
    const Run second = run(1.0f);
    const Run unlit = run(0.0f);
    CHECK(MCGameContext::Current().Random().State() == dice);
    MCWeather::Settings() = {};
    GHdWeather = HdWeatherOff;
    UseSound = usedSound;
    ScenarioTime = now;

    const auto [dimmest, brightest] = std::ranges::minmax(first.Brightness);
    const auto [unlitDimmest, unlitBrightest] = std::ranges::minmax(unlit.Brightness);
    std::cout << std::format(
        "  lightning: brightness {:.1f} to {:.1f} (no flash {:.1f} to {:.1f}), first flash frame {}, "
        "{} thunder, the first on frame {}\n",
        dimmest, brightest, unlitDimmest, unlitBrightest, first.FirstFlash, first.Thunder.size(),
        first.Thunder.empty() ? -1 : first.Thunder.front());
    CHECK(brightest > dimmest + Flashed);
    CHECK(unlitBrightest < unlitDimmest + Steady);
    CHECK(first.FirstFlash >= 0);
    REQUIRE(!first.Thunder.empty());
    CHECK(first.Thunder.front() > first.FirstFlash);
    CHECK(second.Brightness == first.Brightness);
    CHECK(second.Thunder == first.Thunder);
}

/// <summary>
/// HD weather's showers (MCWeather::ShowersAt, LookAt), over two hours of the game's time: in rain and in a downpour the
/// rain builds and eases, from lulls to bursts as far as their showers go (a downpour's lull still a downpour), smoothly,
/// over minutes, the same each time; the look follows it (fewer drops in a lull, fewer near the eye, a thinner veil, a
/// lighter overcast, quieter), the drops' sizes' shares still whole. The puddles start at none and spread as the rain
/// falls: they fill while it falls harder than they hold, and dry, slowly, only while it falls less. The pace speeds it
/// all up alike; without showers, or under a clear sky, the look is as made at any time; the look shown is the look at
/// the game's time.
/// </summary>
TEST_CASE("HD weather: showers come and go over minutes, the same each time, and the puddles spread as the rain falls")
{
    struct Restore
    {
        MCWeather::MCWeatherSettings Settings = MCWeather::Settings();
        int Weather = GHdWeather;
        float Time = ScenarioTime;

        ~Restore()
        {
            MCWeather::Settings() = Settings;
            GHdWeather = Weather;
            ScenarioTime = Time;
        }
    } restore;

    MCWeather::Settings() = {};
    constexpr int32_t Seconds = 2 * 60 * 60;
    // A lull and a burst reach this share of the showers' reach; the rain changes at most this share of it a second;
    // two hours see this many swings from a lull to a burst or back (a lull below half the reach, a burst above).
    constexpr float Reached = 0.9f;
    constexpr float Steepest = 0.06f;
    constexpr int32_t FewestSwings = 8;
    constexpr int32_t MostSwings = 40;
    // The puddles' spread after a minute and after ten, and the most they dry in a second.
    constexpr float FirstMinuteLeast = 0.1f;
    constexpr float FirstMinuteMost = 0.5f;
    constexpr float TenMinutesLeast = 0.8f;
    constexpr float MostDrying = 0.002f;
    constexpr int32_t Pace = 8;
    constexpr float Slack = 1e-5f;

    const auto showers = [](MCWeatherKind kind)
    {
        std::vector<MCWeather::MCShowers> at;

        for (int32_t second = 0; second <= Seconds; ++second)
        {
            at.push_back(MCWeather::ShowersAt(kind, static_cast<float>(second)));
        }

        return at;
    };

    const auto same = [](const MCWeather::MCShowers& a, const MCWeather::MCShowers& b)
    { return a.Rain == b.Rain && a.Puddles == b.Puddles; };
    const auto sameLook = [](const MCWeather::MCWeatherLook& a, const MCWeather::MCWeatherLook& b)
    {
        return std::tie(a.Dark, a.Grey, a.Wet, a.Veil, a.Drops, a.Sizes, a.Puddles, a.Sheen, a.Gloss, a.Drips, a.Heard,
                        a.Strikes, a.Flash, a.ThunderHeard, a.Showers) ==
               std::tie(b.Dark, b.Grey, b.Wet, b.Veil, b.Drops, b.Sizes, b.Puddles, b.Sheen, b.Gloss, b.Drips, b.Heard,
                        b.Strikes, b.Flash, b.ThunderHeard, b.Showers);
    };

    for (const MCWeatherKind kind : {MCWeatherKind::Rain, MCWeatherKind::Downpour})
    {
        const MCWeather::MCWeatherLook made = MCWeather::LookOf(kind);
        const float reach = made.Showers;
        REQUIRE(reach > 0.0f);
        const std::vector<MCWeather::MCShowers> first = showers(kind);
        MCWeather::Clear();
        const std::vector<MCWeather::MCShowers> second = showers(kind);
        CHECK(std::ranges::equal(first, second, same));

        // The rain: within its reach, reaching both ends, smoothly, over minutes.
        float least = std::numeric_limits<float>::max();
        float most = 0.0f;
        float steepest = 0.0f;
        size_t lull = 0;
        size_t burst = 0;
        int32_t swings = 0;
        std::optional<bool> inBurst;

        for (size_t t = 0; t < first.size(); ++t)
        {
            const float rain = first[t].Rain;
            lull = rain < first[lull].Rain ? t : lull;
            burst = rain > first[burst].Rain ? t : burst;
            least = std::min(least, rain);
            most = std::max(most, rain);
            steepest = t > 0 ? std::max(steepest, std::abs(rain - first[t - 1].Rain)) : steepest;

            if (const bool high = rain > 1.0f; std::abs(rain - 1.0f) > 0.5f * reach && inBurst != high)
            {
                swings += inBurst.has_value() ? 1 : 0;
                inBurst = high;
            }
        }

        std::cout << std::format(
            "  {}: rain {:.3f} to {:.3f} as made, at most {:.4f} a second, {} swings in two hours\n",
            kind == MCWeatherKind::Rain ? "rain" : "downpour", least, most, steepest, swings);
        CHECK(least >= 1.0f - reach - Slack);
        CHECK(most <= 1.0f + reach + Slack);
        CHECK(least <= 1.0f - Reached * reach);
        CHECK(most >= 1.0f + Reached * reach);
        CHECK(steepest <= Steepest * reach);
        CHECK(swings >= FewestSwings);
        CHECK(swings <= MostSwings);

        // The puddles: none at the start, spreading as the rain falls, drying slowly only in a lull.
        float driest = 0.0f;
        bool followed = true;

        for (size_t t = 0; t + 1 < first.size(); ++t)
        {
            const float change = first[t + 1].Puddles - first[t].Puddles;
            const float rain = std::min(first[t].Rain, first[t + 1].Rain);
            const float lightest = std::max(first[t].Rain, first[t + 1].Rain);
            followed = followed && (rain <= first[t + 1].Puddles || change >= 0.0f) &&
                       (lightest >= first[t].Puddles || change <= 0.0f);
            driest = std::max(driest, -change);
        }

        std::cout << std::format("  puddles {:.3f} after a minute, {:.3f} after ten, drying at most {:.5f} a second\n",
                                 first[60].Puddles, first[600].Puddles, driest);
        CHECK(first.front().Puddles == 0.0f);
        CHECK(first[60].Puddles >= FirstMinuteLeast);
        CHECK(first[60].Puddles <= FirstMinuteMost);
        CHECK(first[600].Puddles >= TenMinutesLeast);
        CHECK(followed);
        CHECK(driest > 0.0f);
        CHECK(driest <= MostDrying);
        CHECK(std::ranges::all_of(first, [&](const MCWeather::MCShowers& at)
                                  { return at.Puddles >= 0.0f && at.Puddles <= 1.0f + reach + Slack; }));

        // The look follows: in the deepest lull fewer drops, fewer near, a thinner veil, a lighter overcast, quieter;
        // the puddles as far as they've spread, the sizes' shares whole.
        const MCWeather::MCWeatherLook atLull = MCWeather::LookAt(kind, static_cast<float>(lull));
        const MCWeather::MCWeatherLook atBurst = MCWeather::LookAt(kind, static_cast<float>(burst));
        CHECK(atLull.Drops < made.Drops);
        CHECK(atBurst.Drops > made.Drops);
        CHECK(atLull.Sizes[2] < atBurst.Sizes[2]);
        CHECK(atLull.Veil < atBurst.Veil);
        CHECK(atLull.Dark < atBurst.Dark);
        CHECK(atLull.Grey < atBurst.Grey);
        CHECK(atLull.Heard < atBurst.Heard);
        CHECK(atLull.Drips < atBurst.Drips);
        CHECK(atLull.Puddles == made.Puddles * first[lull].Puddles);
        CHECK(MCWeather::LookAt(kind, 0.0f).Puddles == 0.0f);
        CHECK(MCWeather::LookAt(kind, 0.0f).Sheen > 0.0f);
        CHECK(MCWeather::LookAt(kind, 0.0f).Sheen < made.Sheen);

        for (const MCWeather::MCWeatherLook& look : {atLull, atBurst})
        {
            CHECK(std::ranges::all_of(look.Sizes, [](float share) { return share >= 0.0f; }));
            CHECK(std::abs(look.Sizes[0] + look.Sizes[1] + look.Sizes[2] - 1.0f) <= Slack);
        }

        // Faster alike: at the pace, a second shows what Pace seconds did.
        MCWeather::Settings().ShowerPace = static_cast<float>(Pace);

        for (int32_t at = 0; at * Pace <= Seconds; at += 7)
        {
            CHECK(same(MCWeather::ShowersAt(kind, static_cast<float>(at)), first[static_cast<size_t>(at * Pace)]));
        }

        MCWeather::Settings().ShowerPace = 1.0f;
    }

    // Without showers, or under a clear sky, the look is as made at any time.
    MCWeather::Settings().Showers = 0.0f;

    for (const float time : {0.0f, 61.0f, 600.5f, 3600.0f})
    {
        CHECK(sameLook(MCWeather::LookAt(MCWeatherKind::Downpour, time), MCWeather::LookOf(MCWeatherKind::Downpour)));
        CHECK(same(MCWeather::ShowersAt(MCWeatherKind::Downpour, time), {}));
    }

    MCWeather::Settings().Showers = 1.0f;
    CHECK(sameLook(MCWeather::LookAt(MCWeatherKind::Clear, 600.0f), MCWeather::LookOf(MCWeatherKind::Clear)));

    // The look shown is the look at the game's time.
    GHdWeather = HdWeatherDownpour;
    ScenarioTime = 345.0f;
    CHECK(sameLook(MCWeather::ShownLook(), MCWeather::LookAt(MCWeatherKind::Downpour, ScenarioTime)));
}

/// <summary>
/// HD weather's snows: light snow, snow and a blizzard fall as flakes, ever more of them, silently and without lightning;
/// their snow settles from bare ground at a mission's start toward its share of the ground, the heavier the faster
/// (MCWeather::CoverAt), all at once when it had snowed long before the start (MCWeatherSettings::Settled), never in
/// rain; their names read as a scenario file and the tools give them, and each HD weather setting shows its own.
/// </summary>
TEST_CASE("HD weather: snow falls as flakes and settles through a mission from bare ground, the heavier the faster")
{
    struct Restore
    {
        MCWeather::MCWeatherSettings Settings = MCWeather::Settings();
        int Weather = GHdWeather;

        ~Restore()
        {
            MCWeather::Settings() = Settings;
            GHdWeather = Weather;
        }
    } restore;

    MCWeather::Settings() = {};
    constexpr std::array Snows = {MCWeatherKind::LightSnow, MCWeatherKind::Snow, MCWeatherKind::Blizzard};
    // A minute in, and long after (the snow as settled as it gets).
    constexpr float Minute = 60.0f;
    constexpr float LongAfter = 100000.0f;
    constexpr float Settled = 0.999f;
    float drops = 0.0f;
    float cover = 0.0f;
    float minuteCover = 0.0f;

    for (const MCWeatherKind kind : Snows)
    {
        const MCWeather::MCWeatherLook look = MCWeather::LookOf(kind);
        MCTest::Scope scope(std::format("weather {}", static_cast<int>(kind)));
        CHECK(look.Snows);
        CHECK(look.Drops > drops);
        CHECK(look.Cover > cover);
        CHECK_EQ(look.Heard, 0.0f);
        CHECK_EQ(look.Strikes, 0.0f);
        CHECK_EQ(look.Puddles, 0.0f);
        CHECK_EQ(MCWeather::CoverAt(kind, 0.0f), 0.0f);
        CHECK(MCWeather::CoverAt(kind, Minute) > minuteCover);
        CHECK(MCWeather::CoverAt(kind, 2.0f * Minute) > MCWeather::CoverAt(kind, Minute));
        CHECK(MCWeather::CoverAt(kind, LongAfter) > Settled * look.Cover);
        CHECK(MCWeather::CoverAt(kind, LongAfter) <= look.Cover);
        drops = look.Drops;
        cover = look.Cover;
        minuteCover = MCWeather::CoverAt(kind, Minute);

        // Snowed long before the start: settled from the first.
        MCWeather::Settings().Settled = LongAfter;
        CHECK(MCWeather::CoverAt(kind, 0.0f) > Settled * look.Cover);
        MCWeather::Settings().Settled = 0.0f;

        GHdWeather = MCWeather::SettingOf(kind);
        CHECK(MCWeather::Shown() == kind);
    }

    CHECK(!MCWeather::LookOf(MCWeatherKind::Downpour).Snows);
    CHECK_EQ(MCWeather::CoverAt(MCWeatherKind::Downpour, LongAfter), 0.0f);
    CHECK_EQ(MCWeather::CoverAt(MCWeatherKind::Clear, LongAfter), 0.0f);
    CHECK(MCWeather::Named("LightSnow") == MCWeatherKind::LightSnow);
    CHECK(MCWeather::Named("snow") == MCWeatherKind::Snow);
    CHECK(MCWeather::Named("BLIZZARD") == MCWeatherKind::Blizzard);
    CHECK(MCWeather::SettingOf(MCWeatherKind::Clear) == HdWeatherOff);

    // The more of the ground covered, the higher the level under which it lies.
    CHECK(MCWeather::SnowLevel(0.2f) < MCWeather::SnowLevel(0.5f));
    CHECK(MCWeather::SnowLevel(0.5f) < MCWeather::SnowLevel(0.9f));
    CHECK_EQ(MCWeather::SnowLevel(0.0f), 0.0f);
}

/// <summary>
/// HD weather's splashes fitting the ground (MCWeather::MCWeatherSettings::Ground), on mission 1 drawn by the GPU in a
/// downpour letting no rain of its own fall, paused, as the game's time goes: a near drop landed (MCWeather::AddDrop)
/// on hard ground throws droplets up; into soil it soaks, a dark dot, and in grass it puffs, neither throwing any; on
/// the water drawn it rings; on the player's mech's top it glints, throwing droplets. At a strength of 0 every land and
/// the mech throw droplets, as the rain first fell. Each shows where it lands (the soak darker, the puff and the glint
/// lighter), the same drop splashes the same again, and none of it throws the game's dice.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a raindrop splashes as the ground takes it: off rock droplets, into soil a dark dot, in grass a "
    "puff, on water rings, on a unit a glint")
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

    using Ground = MCWeather::MCSplashGround;
    const std::shared_ptr<MCMover> mech = GetMoverFromPartId(PlayerMechPartId);
    REQUIRE(mech != nullptr);
    GHdWeather = HdWeatherDownpour;
    MCWeather::Settings().Amount = 0.0f;
    GamePaused = true;
    const float now = ScenarioTime;
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();

    // The nearest point of each ground to the camera's middle, as the camera shows it, in the world clear of the
    // interface (as the rain's test: the panels take the screen's left third, the paused message its top quarter's
    // middle), Margin pixels in from its edges, and clear of the mech (Clear pixels to its sides, Tall above its feet
    // and Margin below them).
    constexpr float Reach = 400.0f;
    constexpr float Apart = 4.0f;
    constexpr float Margin = 24.0f;
    constexpr float Clear = 40.0f;
    constexpr int32_t Tall = 60;
    const auto findGrounds = [&](const MCVector3D& middle)
    {
        const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
        REQUIRE(camera != nullptr);
        std::map<Ground, std::pair<float, MCVector3D>> nearest;
        const MCVector2D feet = MCParticles::ViewPoint(mech->Position.X, mech->Position.Y, mech->Position.Z, *camera);

        for (float dy = -Reach; dy <= Reach; dy += Apart)
        {
            for (float dx = -Reach; dx <= Reach; dx += Apart)
            {
                MCVector3D point(middle.X + dx, middle.Y + dy, 0.0f);
                point.Z = MCParticles::GroundAt(point.X, point.Y);
                const MCVector2D at = MCParticles::ViewPoint(point.X, point.Y, point.Z, *camera);

                const bool byMech = std::abs(at.X - feet.X) < Clear && at.Y > feet.Y - static_cast<float>(Tall) &&
                                    at.Y < feet.Y + Margin;

                if (byMech || at.X < static_cast<float>(width / 3) + Margin ||
                    at.X > static_cast<float>(width) - Margin || at.Y < static_cast<float>(height / 4) + Margin ||
                    at.Y > static_cast<float>(height) - Margin ||
                    !MCParticles::GroundDrawnAt(point.X, point.Y, point.Z, *camera).has_value())
                {
                    continue;
                }

                const float off = std::hypot(dx, dy);
                const auto [found, isNew] = nearest.try_emplace(MCWeather::GroundAt(point, *camera), off, point);

                if (!isNew && off < found->second.first)
                {
                    found->second = {off, point};
                }
            }
        }

        std::map<Ground, MCVector3D> found;

        for (const auto& [ground, point] : nearest)
        {
            found.emplace(ground, point.second);
        }

        return found;
    };

    // What a near drop landed at a point splashes into: its pieces at their most over the frames it lasts, and the
    // frame shown as it lands; the weather's dice started again.
    constexpr float Step = 1.0f / 30.0f;
    constexpr int32_t Frames = 20;
    constexpr uint8_t NearDrop = 2;
    struct Splashed
    {
        MCWeather::MCSplashPieces Most;
        std::vector<SDL_Color> Landing;
        MCVector2D At;
        size_t Left = 0;
    };

    const auto splashed = [&](const std::optional<MCVector3D>& point)
    {
        MCWeather::Clear();
        ScenarioTime = now;
        MCTestGame::RunFrame(FrameSeconds);

        Splashed made;

        if (point.has_value())
        {
            MCWeather::AddDrop(*point, NearDrop);
            made.At = MCParticles::ViewPoint(point->X, point->Y, point->Z, *ActiveMainCamera());
        }

        for (int32_t frame = 1; frame <= Frames; ++frame)
        {
            ScenarioTime = now + Step * static_cast<float>(frame);
            MCTestGame::RunFrame(FrameSeconds);
            const MCWeather::MCSplashPieces pieces = MCWeather::Pieces();
            MCWeather::MCSplashPieces& most = made.Most;
            most.Droplets = std::max(most.Droplets, pieces.Droplets);
            most.Dots = std::max(most.Dots, pieces.Dots);
            most.Rings = std::max(most.Rings, pieces.Rings);
            most.Soaks = std::max(most.Soaks, pieces.Soaks);
            most.Puffs = std::max(most.Puffs, pieces.Puffs);
            most.Glints = std::max(most.Glints, pieces.Glints);

            if (frame == 1)
            {
                auto shown = display->ReadFrame();
                REQUIRE(shown.has_value());
                REQUIRE(shown->size() == static_cast<size_t>(width) * height);
                made.Landing = std::move(*shown);
            }
        }

        made.Left = MCWeather::Count();
        return made;
    };

    // How the frame a drop lands in differs from one without round where it lands (Near pixels to each side and below
    // it, Tall above it, a unit's top's height): how many pixels, and how much brighter on average.
    constexpr int32_t Near = 12;
    const auto changeOf = [&](const Splashed& made, const Splashed& none)
    {
        size_t changed = 0;
        float brighter = 0.0f;
        const auto x0 = static_cast<int32_t>(made.At.X);
        const auto y0 = static_cast<int32_t>(made.At.Y);

        for (int32_t y = std::max(y0 - Tall, 0); y <= std::min(y0 + Near, height - 1); ++y)
        {
            for (int32_t x = std::max(x0 - Near, 0); x <= std::min(x0 + Near, width - 1); ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;

                if (!SameColor(made.Landing[i], none.Landing[i]))
                {
                    ++changed;
                    brighter += LumaOf(made.Landing[i]) - LumaOf(none.Landing[i]);
                }
            }
        }

        return std::pair(changed, changed > 0 ? brighter / static_cast<float>(changed) : 0.0f);
    };

    const auto print = [](std::string_view name, const Splashed& made)
    {
        const MCWeather::MCSplashPieces& most = made.Most;
        std::cout << std::format("  {}: {} droplets, {} dots, {} rings, {} soaks, {} puffs, {} glints\n", name,
                                 most.Droplets, most.Dots, most.Rings, most.Soaks, most.Puffs, most.Glints);
    };

    const uint32_t dice = MCGameContext::Current().Random().State();

    // The water: the camera over the map's (the tile with the most water round it).
    const MCScenarioMap& map = GameMap();
    constexpr int32_t Round = 2;
    int32_t most = 0;
    MCVector3D waterMiddle;

    for (int32_t row = Round; row < map.Height - Round; ++row)
    {
        for (int32_t column = Round; column < map.Width - Round; ++column)
        {
            int32_t water = 0;

            for (int32_t r = row - Round; r <= row + Round; ++r)
            {
                for (int32_t c = column - Round; c <= column + Round; ++c)
                {
                    water += map.Map[static_cast<size_t>(r) * map.Width + c].TerrainType() >= FirstWaterTerrain ? 1 : 0;
                }
            }

            if (water > most)
            {
                most = water;
                waterMiddle = MCVector3D(TileColToWorld(column), TileRowToWorld(row), 0.0f);
            }
        }
    }

    REQUIRE(most > 0);
    Eye()->SetPosition(waterMiddle);
    MCTestGame::RunFrame(FrameSeconds);
    const std::map<Ground, MCVector3D> byWater = findGrounds(waterMiddle);
    REQUIRE(byWater.contains(Ground::Water));
    const Splashed water = splashed(byWater.at(Ground::Water));

    // The land and the mech: the camera on the mech; a point of its footprint just behind its feet, up the view.
    Eye()->ChangeTarget(mech, true);
    MCTestGame::RunFrame(FrameSeconds);
    const std::map<Ground, MCVector3D> byMech = findGrounds(mech->Position);

    for (const auto& [ground, point] : byMech)
    {
        std::cout << std::format("  ground {} at ({:.0f}, {:.0f})\n", static_cast<int>(ground), point.X, point.Y);
    }

    REQUIRE(byMech.contains(Ground::Hard));
    REQUIRE(byMech.contains(Ground::Soil));
    REQUIRE(byMech.contains(Ground::Grass));
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    constexpr float Behind = 4.0f;
    constexpr float WorldStep = 0.5f;
    constexpr float FarthestBack = 60.0f;
    const float feetY = MCParticles::ViewPoint(mech->Position.X, mech->Position.Y, mech->Position.Z, *camera).Y;
    std::optional<MCVector3D> onMech;

    for (float back = WorldStep; back <= FarthestBack && !onMech.has_value(); back += WorldStep)
    {
        MCVector3D point(mech->Position.X - back, mech->Position.Y + back, 0.0f);
        point.Z = MCParticles::GroundAt(point.X, point.Y);

        if (MCParticles::ViewPoint(point.X, point.Y, point.Z, *camera).Y <= feetY - Behind)
        {
            onMech = point;
        }
    }

    REQUIRE(onMech.has_value());
    const Splashed none = splashed(std::nullopt);
    const Splashed hard = splashed(byMech.at(Ground::Hard));
    const Splashed hardAgain = splashed(byMech.at(Ground::Hard));
    const Splashed soil = splashed(byMech.at(Ground::Soil));
    const Splashed grass = splashed(byMech.at(Ground::Grass));
    const Splashed metal = splashed(onMech);
    MCWeather::Settings().Ground = 0.0f;
    const Splashed soilAsFirst = splashed(byMech.at(Ground::Soil));
    const Splashed grassAsFirst = splashed(byMech.at(Ground::Grass));
    const Splashed metalAsFirst = splashed(onMech);
    MCWeather::Settings().Ground = 1.0f;
    MCWeather::Settings().Amount = 1.0f;
    CHECK(MCGameContext::Current().Random().State() == dice);
    ScenarioTime = now;
    GHdWeather = HdWeatherOff;
    GamePaused = false;

    print("hard", hard);
    print("soil", soil);
    print("grass", grass);
    print("water", water);
    print("metal", metal);
    print("soil at 0", soilAsFirst);
    print("grass at 0", grassAsFirst);
    print("metal at 0", metalAsFirst);
    const auto [soilChanged, soilBrighter] = changeOf(soil, none);
    const auto [grassChanged, grassBrighter] = changeOf(grass, none);
    const auto [metalChanged, metalBrighter] = changeOf(metal, none);
    std::cout << std::format("  as they land: soil {} pixels {:+.1f} brighter, grass {} {:+.1f}, metal {} {:+.1f}\n",
                             soilChanged, soilBrighter, grassChanged, grassBrighter, metalChanged, metalBrighter);

    CHECK(none.Most.Droplets + none.Most.Dots + none.Most.Soaks + none.Most.Puffs + none.Most.Glints == 0);
    CHECK(hard.Most.Droplets > 0);
    CHECK(hard.Most.Dots > 0);
    CHECK(hard.Most.Soaks + hard.Most.Puffs + hard.Most.Glints + hard.Most.Rings == 0);
    CHECK(hardAgain.Most.Droplets == hard.Most.Droplets);
    CHECK(changeOf(hardAgain, hard).first == 0);
    CHECK(soil.Most.Soaks > 0);
    CHECK(soil.Most.Droplets + soil.Most.Dots + soil.Most.Puffs + soil.Most.Glints + soil.Most.Rings == 0);
    CHECK(grass.Most.Puffs > 0);
    CHECK(grass.Most.Droplets + grass.Most.Dots + grass.Most.Soaks + grass.Most.Glints + grass.Most.Rings == 0);
    CHECK(water.Most.Rings > 0);
    CHECK(water.Most.Soaks + water.Most.Puffs + water.Most.Glints == 0);
    CHECK(metal.Most.Glints > 0);
    CHECK(metal.Most.Droplets > 0);
    CHECK(metal.Most.Soaks + metal.Most.Puffs + metal.Most.Rings == 0);
    CHECK(soilAsFirst.Most.Droplets > 0);
    CHECK(grassAsFirst.Most.Droplets > 0);
    CHECK(soilAsFirst.Most.Soaks + grassAsFirst.Most.Puffs + metalAsFirst.Most.Glints == 0);
    CHECK(metalAsFirst.Most.Droplets > 0);
    CHECK(soilChanged > 0);
    CHECK(soilBrighter < 0.0f);
    CHECK(grassChanged > 0);
    CHECK(grassBrighter > 0.0f);
    CHECK(metalChanged > 0);
    CHECK(metalBrighter > 0.0f);
    CHECK(hard.Left == 0);
}

/// <summary>
/// HD particles' steps (MCParticles::AddStep: a mech's feet, where they print or would), on the screen drawn by the GPU
/// over mission 1's water, paused: as the game's time goes a foot coming down in the water drawn splashes, droplets and a
/// ring; one on the land drawn throws nothing; none splash at a strength of 0 (MCParticleLook::Steps) or with the
/// particles off; and none of it throws the game's dice.
/// </summary>
TEST_CASE_ISOLATED("game: with HDParticles a foot coming down in the water drawn splashes, and on land it doesn't")
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

    // The camera over the map's water: the tile with the most water round it.
    constexpr int32_t Round = 2;
    const MCScenarioMap& map = GameMap();
    int32_t most = 0;
    int32_t waterRow = 0;
    int32_t waterColumn = 0;

    for (int32_t row = Round; row < map.Height - Round; ++row)
    {
        for (int32_t column = Round; column < map.Width - Round; ++column)
        {
            int32_t water = 0;

            for (int32_t r = row - Round; r <= row + Round; ++r)
            {
                for (int32_t c = column - Round; c <= column + Round; ++c)
                {
                    water += map.Map[static_cast<size_t>(r) * map.Width + c].TerrainType() >= FirstWaterTerrain ? 1 : 0;
                }
            }

            if (water > most)
            {
                most = water;
                waterRow = row;
                waterColumn = column;
            }
        }
    }

    REQUIRE(most > 0);
    const MCVector3D middle(TileColToWorld(waterColumn), TileRowToWorld(waterRow), 0.0f);
    Eye()->SetPosition(middle);
    MCTestGame::RunFrame(FrameSeconds);
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);

    // A point of the water drawn and one of the land drawn, round it.
    constexpr float Reach = 300.0f;
    constexpr float Apart = 10.0f;
    std::optional<MCVector3D> water;
    std::optional<MCVector3D> land;

    for (float dy = -Reach; dy <= Reach && (!water.has_value() || !land.has_value()); dy += Apart)
    {
        for (float dx = -Reach; dx <= Reach; dx += Apart)
        {
            MCVector3D point(middle.X + dx, middle.Y + dy, 0.0f);
            point.Z = MCParticles::GroundAt(point.X, point.Y);

            if (const std::optional<uint8_t> drawn = MCParticles::GroundDrawnAt(point.X, point.Y, point.Z, *camera);
                drawn.has_value())
            {
                (MCParticles::IsWaterColor(*drawn) ? water : land) = point;
            }
        }
    }

    REQUIRE(water.has_value());
    REQUIRE(land.has_value());

    // The most particles over a few frames after a foot comes down (or none), the dice started again.
    const float now = ScenarioTime;
    GamePaused = true;
    constexpr float Step = 1.0f / 30.0f;
    constexpr int32_t Frames = 10;
    constexpr float HalfWidth = 20.0f;
    const auto splashed = [&](const std::optional<MCVector3D>& foot)
    {
        MCParticles::Clear();
        ScenarioTime = now;
        MCTestGame::RunFrame(FrameSeconds);

        if (foot.has_value())
        {
            MCParticles::AddStep(*foot, HalfWidth);
        }

        size_t made = 0;

        for (int32_t frame = 1; frame <= Frames; ++frame)
        {
            ScenarioTime = now + Step * static_cast<float>(frame);
            MCTestGame::RunFrame(FrameSeconds);
            made = std::max(made, MCParticles::Count());
        }

        return made;
    };

    const uint32_t dice = MCGameContext::Current().Random().State();
    GHdParticles = 1;
    const size_t none = splashed(std::nullopt);
    const size_t inWater = splashed(water);
    const size_t onLand = splashed(land);
    MCParticles::Look().Steps = 0.0f;
    const size_t still = splashed(water);
    MCParticles::Look().Steps = 1.0f;
    GHdParticles = 0;
    const size_t off = splashed(water);
    CHECK(MCGameContext::Current().Random().State() == dice);
    ScenarioTime = now;
    std::cout << std::format("  particles: {} with no foot, {} after a foot in the water, {} on land, {} at a strength "
                             "of 0, {} with the particles off\n",
                             none, inWater, onLand, still, off);
    CHECK(inWater > none);
    CHECK(onLand == none);
    CHECK(still == none);
    CHECK(off == 0);
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
    /// With <c>--hd-sun-turn &lt;seconds&gt;[,&lt;length&gt;]</c>, turns the sun once round the world in that many
    /// seconds of the mission's time, its shadows that many times as long as the day's (MCLighting::SunTurnSeconds and
    /// SunTurnLength), every object casting its own: shows the sun's way with sun shadows on.
    /// </summary>
    void SunTurnFromOptions()
    {
        const std::optional<std::string_view> turn = MCTest::Option("hd-sun-turn");

        if (!turn.has_value())
        {
            return;
        }

        MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
        const std::string values(*turn);
        char* rest = nullptr;
        lighting.SunTurnSeconds = std::strtof(values.c_str(), &rest);

        if (*rest == ',')
        {
            lighting.SunTurnLength = std::strtof(rest + 1, nullptr);
        }

        std::cout << std::format("  sun: once round in {:.0f} s, shadows {:.2f} times the day's\n",
                                 lighting.SunTurnSeconds, lighting.SunTurnLength);
    }

    /// <summary>
    /// With <c>--hd-arms original|hit|dropped|thrown|shattered</c>, a blown-off mech arm shows so with HD art (GHdArms, an
    /// MCArmStyle): as the original, as the hit that took it off says (the game's), or always dropped, thrown or
    /// shattered.
    /// </summary>
    void ArmsFromOptions()
    {
        const std::optional<std::string_view> style = MCTest::Option("hd-arms");

        if (!style.has_value())
        {
            return;
        }

        constexpr std::array<std::string_view, 5> Styles = {"original", "hit", "dropped", "thrown", "shattered"};
        const auto found = std::ranges::find(Styles, *style);
        REQUIRE(found != Styles.end());
        GHdArms = static_cast<MCArmStyle>(found - Styles.begin());
        std::cout << std::format("  arms: {}\n", *style);
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
    /// With <c>--hd-sway &lt;meters&gt;[,&lt;light&gt;]</c>, lets a forest's canopy sway (GHdSway) that far at the
    /// height of a gust, that much brighter there (MCLighting::Sway, SwayLight; each left out keeps its default).
    /// </summary>
    void SwayFromOptions()
    {
        const std::optional<std::string_view> sway = MCTest::Option("hd-sway");

        if (!sway.has_value())
        {
            return;
        }

        MCSceneLights::MCLighting& settings = MCSceneLights::Lighting();
        const std::string given(*sway);
        REQUIRE(std::sscanf(given.c_str(), "%f,%f", &settings.Sway, &settings.SwayLight) >= 1);
        GHdSway = 1;
        std::cout << std::format("  forests sway: {:.2f} m, {:.2f} brighter\n", settings.Sway, settings.SwayLight);
    }

    /// <summary>
    /// With <c>--hd-footprints-on 1</c>, turns HDFootprints on (GHdFootprints): footprints under the feet as drawn. The
    /// ring of footprints and craters keeps the mission's length (three times it needs PREFS, read as the mission starts,
    /// before its ring is made).
    /// </summary>
    void FootprintsFromOptions()
    {
        if (MCTest::Option("hd-footprints-on").has_value())
        {
            GHdFootprints = 1;
            std::cout << "  footprints: under the feet\n";
        }
    }

    /// <summary>
    /// With <c>--hd-turns 1</c>, turns HDTurns on (GHdTurns): mechs drawn turning eased, their legs between facings where
    /// the pack has the middles.
    /// </summary>
    void TurnsFromOptions()
    {
        if (MCTest::Option("hd-turns").has_value())
        {
            GHdTurns = 1;
            std::cout << "  turns: eased, legs between facings\n";
        }
    }

    /// <summary>
    /// With <c>--hd-stops 1</c>, turns HDStops on (GHdStops): a mech stopped mid-stride with its move held up holds its
    /// stride and walks on from it; with <c>--mech-braking 1</c>, MechBraking (GMechBraking): mechs brake to a halt
    /// through their stopping art where their moves end.
    /// </summary>
    void StopsFromOptions()
    {
        if (MCTest::Option("hd-stops").has_value())
        {
            GHdStops = 1;
            std::cout << "  stops: strides held while held up\n";
        }

        if (MCTest::Option("mech-braking").has_value())
        {
            GMechBraking = 1;
            std::cout << "  mech braking: halts through the stopping art\n";
        }
    }

    /// <summary>
    /// With <c>--hd-particles &lt;amount&gt;</c>, turns the particles on (GHdParticles), that many as made
    /// (MCParticles::MCParticleLook::Amount); with <c>--hd-shake &lt;strength&gt;</c>, the shake (GHdShake), that strong;
    /// <c>--hd-flecks edge|falling</c> picks how explosions' flecks fly (GHdFlecks); <c>--hd-steps &lt;strength&gt;</c>
    /// how big feet coming down in water and puddles splash (MCParticleLook::Steps, MCWeatherSettings::Steps).
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

        if (const std::optional<std::string_view> steps = MCTest::Option("hd-steps"); steps.has_value())
        {
            look.Steps = std::strtof(std::string(*steps).c_str(), nullptr);
            MCWeather::Settings().Steps = look.Steps;
            std::cout << std::format("  steps: feet splash {:.2f} as big as made\n", look.Steps);
        }
    }

    /// <summary>
    /// With <c>--hd-weather &lt;name&gt;[,&lt;amount&gt;]</c>, shows every mission in that weather (GHdWeather: rain,
    /// downpour, or clear), that many drops falling as made (MCWeather::MCWeatherSettings::Amount); with
    /// <c>--hd-thunder &lt;loudness&gt;</c> too, its thunder that loud, 1 as made (0: none; MCWeatherSettings::Thunder);
    /// with <c>--hd-lightning &lt;flash&gt;[,&lt;strikes&gt;]</c>, its lightning that bright and that many times as
    /// often, 1 as made (MCWeatherSettings::Flash and Strikes); with <c>--hd-showers &lt;strength&gt;[,&lt;pace&gt;]</c>,
    /// its showers coming and going that far, 1 as made (0: steady rain, its puddles as made from the start;
    /// MCWeatherSettings::Showers), that many times as fast (MCWeatherSettings::ShowerPace; 8 shows a burst and a lull
    /// in a 70-second take); with <c>--hd-rain-light &lt;strength&gt;</c>, the drops catch the lights they fall past
    /// that much, 1 as made (0: none, as before; MCWeatherSettings::Light); with <c>--hd-splashes &lt;strength&gt;</c>,
    /// its drops splashing as the ground takes them that strongly, 1 as made (0: every land alike, as the rain first
    /// fell; MCWeatherSettings::Ground). The snows (lightsnow, snow, blizzard): with <c>--hd-snow
    /// &lt;seconds&gt;[,&lt;pace&gt;]</c>, it had snowed that long before the mission's start (MCWeatherSettings::Settled)
    /// and settles and fills the prints in that many times as fast (MCWeatherSettings::SettlePace); with
    /// <c>--hd-snow-share &lt;share&gt;</c>, that share of its flakes are thrown real and the rest drawn as specks, as
    /// on a machine whose frames keep up with only that many (MCWeatherPacing::Hold).
    /// </summary>
    void WeatherFromOptions()
    {
        const std::optional<std::string_view> given = MCTest::Option("hd-weather");

        if (!given.has_value())
        {
            return;
        }

        const size_t comma = given->find(',');
        const std::optional<MCWeatherKind> weather = MCWeather::Named(given->substr(0, comma));
        REQUIRE(weather.has_value());
        GHdWeather = MCWeather::SettingOf(*weather);

        // strtof: libc++ has floating-point from_chars only from macOS 26.
        if (comma != std::string_view::npos)
        {
            MCWeather::Settings().Amount = std::strtof(std::string(given->substr(comma + 1)).c_str(), nullptr);
        }

        if (const std::optional<std::string_view> thunder = MCTest::Option("hd-thunder"); thunder.has_value())
        {
            MCWeather::Settings().Thunder = std::strtof(std::string(*thunder).c_str(), nullptr);
        }

        if (const std::optional<std::string_view> lightning = MCTest::Option("hd-lightning"); lightning.has_value())
        {
            const size_t split = lightning->find(',');
            MCWeather::Settings().Flash = std::strtof(std::string(lightning->substr(0, split)).c_str(), nullptr);

            if (split != std::string_view::npos)
            {
                MCWeather::Settings().Strikes = std::strtof(std::string(lightning->substr(split + 1)).c_str(), nullptr);
            }
        }

        if (const std::optional<std::string_view> showers = MCTest::Option("hd-showers"); showers.has_value())
        {
            const size_t pace = showers->find(',');
            MCWeather::Settings().Showers = std::strtof(std::string(showers->substr(0, pace)).c_str(), nullptr);

            if (pace != std::string_view::npos)
            {
                MCWeather::Settings().ShowerPace = std::strtof(std::string(showers->substr(pace + 1)).c_str(), nullptr);
            }
        }

        if (const std::optional<std::string_view> light = MCTest::Option("hd-rain-light"); light.has_value())
        {
            MCWeather::Settings().Light = std::strtof(std::string(*light).c_str(), nullptr);
        }

        if (const std::optional<std::string_view> splashes = MCTest::Option("hd-splashes"); splashes.has_value())
        {
            MCWeather::Settings().Ground = std::strtof(std::string(*splashes).c_str(), nullptr);
        }

        if (const std::optional<std::string_view> snow = MCTest::Option("hd-snow"); snow.has_value())
        {
            const size_t pace = snow->find(',');
            MCWeather::Settings().Settled = std::strtof(std::string(snow->substr(0, pace)).c_str(), nullptr);

            if (pace != std::string_view::npos)
            {
                MCWeather::Settings().SettlePace = std::strtof(std::string(snow->substr(pace + 1)).c_str(), nullptr);
            }

            std::cout << std::format("  snow: fallen {:.0f} s before the start, settling {:.2f} times as fast\n",
                                     MCWeather::Settings().Settled, MCWeather::Settings().SettlePace);
        }

        if (const std::optional<std::string_view> share = MCTest::Option("hd-snow-share"); share.has_value())
        {
            MCWeatherPacing::Hold(std::strtof(std::string(*share).c_str(), nullptr));
            std::cout << std::format("  snow: {:.2f} of the flakes thrown real, the rest drawn as specks\n",
                                     MCWeatherPacing::Share());
        }

        const MCWeather::MCWeatherSettings& settings = MCWeather::Settings();
        std::cout << std::format(
            "  weather: {}, {:.2f} as many drops as made, thunder {:.2f} as loud, lightning {:.2f} "
            "as bright, {:.2f} as often, showers {:.2f} as far as made, {:.2f} times as fast, drops catching {:.2f} of "
            "the light as made, splashes fitting the ground {:.2f} as strongly\n",
            given->substr(0, comma), settings.Amount, settings.Thunder, settings.Flash, settings.Strikes,
            settings.Showers, settings.ShowerPace, settings.Light, settings.Ground);
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
    /// With <c>--hd-sun-relief &lt;strength&gt;[,&lt;canopy&gt;]</c>, turns relief on (GHdRelief), the sun showing the
    /// world's relief that much (MCLighting::SunRelief, 0 to 1), and a swaying forest's canopy that share of it
    /// (MCLighting::CanopyRelief; left out, its default); the relief maps keep their steepness unless --hd-relief-maps
    /// says.
    /// </summary>
    void SunReliefFromOptions()
    {
        const std::optional<std::string_view> strength = MCTest::Option("hd-sun-relief");

        if (!strength.has_value())
        {
            return;
        }

        MCSceneLights::MCLighting& settings = MCSceneLights::Lighting();
        const std::string given(*strength);
        REQUIRE(std::sscanf(given.c_str(), "%f,%f", &settings.SunRelief, &settings.CanopyRelief) >= 1);
        GHdRelief = 1;
        std::cout << std::format("  sun relief: {:.2f}, the canopy {:.2f} of it\n", settings.SunRelief,
                                 settings.CanopyRelief);
    }

    /// <summary>
    /// With <c>--hd-relief-maps &lt;steepness&gt;</c>, turns relief on (GHdRelief), the HD ground taking the pack's
    /// relief maps that steep (MCLighting::ReliefMaps; 3 about matches the brightness relief, 0 leaves them out); the
    /// sun's relief keeps its strength unless --hd-sun-relief says.
    /// </summary>
    void ReliefMapsFromOptions()
    {
        const std::optional<std::string_view> steepness = MCTest::Option("hd-relief-maps");

        if (!steepness.has_value())
        {
            return;
        }

        MCSceneLights::Lighting().ReliefMaps = std::strtof(std::string(*steepness).c_str(), nullptr);
        GHdRelief = 1;
        std::cout << std::format("  relief maps: {:.2f}\n", MCSceneLights::Lighting().ReliefMaps);
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
    /// <paramref name="pack"/> (into <paramref name="hdArt"/>; its ground read and uploaded up front, as a mission's load does)
    /// unless <c>--hd-original 1</c>, and HD lighting as <c>--hd-lighting</c> asks (<see cref="LightingFromOptions"/>),
    /// at the time of day <c>--hd-time-of-day</c> sets (<see cref="TimeOfDayFromOptions"/>), the other HD options as
    /// their flags ask (<see cref="FootprintsFromOptions"/>, <see cref="TurnsFromOptions"/>...).
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
        }

        LightingFromOptions();
        TimeOfDayFromOptions();
        AirBeamsFromOptions();
        SunReliefFromOptions();
        ReliefMapsFromOptions();
        SunShadowsFromOptions();
        SunTurnFromOptions();
        ArmsFromOptions();
        CloudsFromOptions();
        SwayFromOptions();
        ParticlesFromOptions();
        WeatherFromOptions();
        FootprintsFromOptions();
        TurnsFromOptions();
        StopsFromOptions();

        // The ground read and put on the GPU as a mission's load does, with the options (the relief maps') set.
        MCTerrainReadHdTiles();

        if (const std::expected<void, std::string> uploaded = MCTerrainUploadGround(); !uploaded)
        {
            FAIL(uploaded.error());
        }

        return original;
    }

    /// <summary>
    /// The art modes <c>--hd-art-modes &lt;frame&gt;:&lt;mode&gt;,...</c> asks a picture tool to switch to as it plays,
    /// each from that frame on (the tool's frames, as its shots number them), by frame; modes as the debug console's art
    /// command names them (main/MCArtModes.h: original, hd, day, dusk, night). Without <c>--hd-original 1</c>, so the
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

namespace
{
    /// <summary>A footprint as it was made, and what the mech that made it was doing.</summary>
    struct MadePrint
    {
        int32_t Leg = 0;
        MCVector3D Print;
        MCVector3D Mech;
        float Facing = 0.0f;
        int32_t Gesture = 0;
        int32_t LegsFrame = 0;
        bool Mirrored = false;
    };

    /// <summary>The player's mech as the mission gave it, with its own tree (its hot spots are its type's).</summary>
    PlayerMech OwnPlayerMech()
    {
        PlayerMech player;
        player.Mech = std::static_pointer_cast<MCBattleMech>(GetMoverFromPartId(PlayerMechPartId));
        REQUIRE(player.Mech != nullptr);
        player.Actor = std::static_pointer_cast<MCMechActor>(player.Mech->Appearance);
        REQUIRE(player.Actor != nullptr);
        player.Tree = player.Actor->MechTree;
        Eye()->ChangeTarget(player.Mech, true);
        return player;
    }

    /// <summary>
    /// Walks <paramref name="player"/>'s mech a leg in each of <paramref name="legs"/> ways (evenly round, from 10
    /// degrees), the camera on it, and hands each of a leg's first <paramref name="perLeg"/> prints made once it walks
    /// to <paramref name="made"/>, as it is made. <paramref name="step"/> runs a frame (made may run more).
    /// </summary>
    void WalkPrinting(const PlayerMech& player, int32_t legs, int32_t perLeg, const std::function<void()>& step,
                      const std::function<void(const MadePrint&)>& made)
    {
        const std::shared_ptr<MCMechWarrior> pilot = player.Mech->GetPilot();
        REQUIRE(pilot != nullptr);
        MCCraterManager& craters = CraterManager();
        const auto slots = static_cast<int32_t>(craters.Craters().size());
        constexpr float FirstWay = 10.0f;
        constexpr float LegDistance = 250.0f;
        constexpr int32_t LegFrames = 150;
        // Prints while the mech still turns onto its way are left out.
        constexpr int32_t SettleFrames = 30;

        for (int32_t leg = 0; leg < legs; ++leg)
        {
            const double way = (FirstWay + 360.0f * static_cast<float>(leg) / static_cast<float>(legs)) *
                               MCMoverMath::DegreesToRadians;
            const MCVector3D from = player.Mech->GetPosition();
            pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player,
                                    MCVector3D(from.X + static_cast<float>(std::cos(way)) * LegDistance,
                                               from.Y + static_cast<float>(std::sin(way)) * LegDistance, from.Z),
                                    -1, 0);
            int32_t slot = craters.NextSlot();
            int32_t printed = 0;

            for (int32_t frame = 0; frame < LegFrames && printed < perLeg; ++frame)
            {
                step();

                if (craters.NextSlot() == slot)
                {
                    continue;
                }

                slot = craters.NextSlot();

                if (frame < SettleFrames)
                {
                    continue;
                }

                const MCFrameOfRef facing = player.Mech->GetFrame();
                made(MadePrint{leg, craters.Craters()[static_cast<size_t>((slot + slots - 1) % slots)].Position,
                               player.Mech->GetPosition(),
                               static_cast<float>(std::atan2(facing.I.Y, facing.I.X) / MCMoverMath::DegreesToRadians),
                               player.Actor->CurrentGesture, player.Actor->CurrentFrame[0],
                               player.Actor->Reverse[0] != 0});
                slot = craters.NextSlot();
                ++printed;
            }
        }
    }

    /// <summary>A mech type's hot spot packet for <paramref name="gesture"/> (32 bytes: the feet's frames and offsets).</summary>
    std::span<const uint8_t> FootPacket(const MCBattleMechType& type, int32_t gesture)
    {
        constexpr size_t PacketBytes = 32;
        const uint32_t packet = MCMechActor::GetHotSpotIndex(static_cast<uint32_t>(gesture));
        REQUIRE(packet < type.NumHotSpotPackets);
        return std::span<const uint8_t>(type.HotSpotData).subspan(packet * PacketBytes, PacketBytes);
    }

    /// <summary>The 4-byte value in slot <paramref name="slot"/> of a foot hot spot packet.</summary>
    template <typename T> T FootValue(std::span<const uint8_t> packet, size_t slot)
    {
        T value{};
        std::memcpy(&value, packet.data() + slot * sizeof(T), sizeof(T));
        return value;
    }
}

/// <summary>
/// Tool: <c>--hd-footprints &lt;pack&gt; --shots &lt;folder&gt;</c> pictures where a walking mech's footprints land:
/// mission 1's mech (its own tree, or <c>--mech</c>'s) walks a leg in each of 16 ways, the camera on it, zoomed in; at
/// each of a leg's first prints once it walks, <c>print_&lt;leg&gt;_&lt;n&gt;_&lt;step&gt;.png</c> (the view's middle as
/// the print is made, then every other frame), and a line in the log: the mech's facing, the print's offset from it,
/// the legs' frame and the foot frames of their hot spot packet, and where the mech and the print are on screen.
/// <c>--hd-original 1</c>, <c>--window</c> and the lighting flags as --hd-pack's; <c>--hd-footprints-on 1</c> turns
/// HDFootprints on.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-footprints pictures where a walking mech's footprints land")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-footprints");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    std::optional<MCHdArtOn> hdArt;
    const bool original = StartPictures(*pack, hdArt);
    const std::optional<std::string_view> tree = MCTest::Option("mech");
    const PlayerMech player = tree.has_value() ? GivePlayerMech(*tree) : OwnPlayerMech();
    std::cout << std::format("  the mech: {}\n", player.Mech->TypeAs<MCBattleMechType>().Name);
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    constexpr float ZoomedLines = 150.0f;
    MCFixedZoomHeight = ZoomedLines;

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    constexpr int32_t CropWidth = 640;
    constexpr int32_t CropHeight = 520;
    const auto shoot = [&](const std::string& name)
    {
        const auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        std::vector<uint8_t> crop;
        crop.reserve(static_cast<size_t>(CropWidth) * CropHeight * BytesPerPixel);

        for (int32_t y = (height - CropHeight) / 2; y < (height + CropHeight) / 2; ++y)
        {
            for (int32_t x = (width - CropWidth) / 2; x < (width + CropWidth) / 2; ++x)
            {
                const SDL_Color c = (*frame)[static_cast<size_t>(y) * width + x];
                crop.insert(crop.end(), {c.r, c.g, c.b, 255});
            }
        }

        SavePng(folder / std::format("{}_{}.png", name, original ? "original" : "hd"), CropWidth, CropHeight, crop);
    };

    const auto step = [&]()
    {
        Eye()->SetPosition(player.Mech->GetPosition());
        MCTestGame::RunFrame(FrameSeconds);
    };

    const auto& type = player.Mech->TypeAs<MCBattleMechType>();
    constexpr int32_t Legs = 16;
    constexpr int32_t PrintsShot = 2;
    constexpr int32_t ShotSteps = 4;
    constexpr int32_t FramesBetweenShots = 2;
    std::map<int32_t, int32_t> printsOfLeg;

    WalkPrinting(player, Legs, PrintsShot, step,
                 [&](const MadePrint& made)
                 {
                     const std::span<const uint8_t> packet = FootPacket(type, made.Gesture);
                     const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
                     REQUIRE(camera != nullptr);
                     const MCVector2D mechOnScreen = ProjectToScreen(made.Mech, *camera);
                     const MCVector2D printOnScreen = ProjectToScreen(made.Print, *camera);
                     const int32_t number = printsOfLeg[made.Leg]++;
                     std::cout << std::format(
                         "  leg {:2} print {}: facing {:+7.1f}, print {:+6.1f} {:+6.1f} m from the mech, legs' frame "
                         "{} (gesture {}, mirrored {}), feet at frames {} and {}; on screen mech {:.1f} {:.1f}, print "
                         "{:.1f} {:.1f}, view {:.1f} x {:.1f}, window {} x {}\n",
                         made.Leg, number, made.Facing, made.Print.X - made.Mech.X, made.Print.Y - made.Mech.Y,
                         made.LegsFrame, made.Gesture, made.Mirrored ? 1 : 0, FootValue<int32_t>(packet, 0),
                         FootValue<int32_t>(packet, 4), mechOnScreen.X, mechOnScreen.Y, printOnScreen.X,
                         printOnScreen.Y, camera->HalfWidth * 2.0f, camera->HalfHeight * 2.0f, width, height);

                     for (int32_t shot = 0; shot < ShotSteps; ++shot)
                     {
                         shoot(std::format("print_{:02}_{}_{}", made.Leg, number, shot));

                         for (int32_t i = 0; i < FramesBetweenShots; ++i)
                         {
                             step();
                         }
                     }
                 });
}

/// <summary>
/// Footprints, mission 1's mech walking eight ways: as the original made them, each foot's print two frames before its
/// frame (its hot spot packet's), its offset turned by the facing floored to 40 degrees; with HDFootprints (OB-185), at
/// its frame, turned as the legs are drawn (MCSpriteTree::DrawnFacing, within one of their 17 facings of the mech's
/// way). A mirrored mech's feet swap offsets: the original took the legs as mirrored by the frame's mirroring
/// (SetGesture's), which mirrors left of straight ahead and back though they're drawn unmirrored; HDFootprints as
/// drawn.
/// </summary>
TEST_CASE_ISOLATED("game: with HDFootprints a walking mech's footprints land under its feet as its legs are drawn")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const PlayerMech player = OwnPlayerMech();
    const auto& type = player.Mech->TypeAs<MCBattleMechType>();
    constexpr int32_t Legs = 8;
    constexpr int32_t PerLeg = 2;
    constexpr int32_t EarlyFrames = 2;
    constexpr float FootScale = 20.0f;
    constexpr float OriginalStep = 40.0f;
    constexpr float LegFacing = 360.0f / 17.0f;
    constexpr float Tolerance = 0.05f;
    // A foot packet's slots: the first foot's frame, x, y, then (from slot 4) the second's.
    constexpr size_t FirstFoot = 0;
    constexpr size_t SecondFoot = 4;

    const auto step = [&]()
    {
        Eye()->SetPosition(player.Mech->GetPosition());
        MCTestGame::RunFrame(FrameSeconds);
    };

    for (const int hd : {0, 1})
    {
        MCTest::Scope scope(std::format("HDFootprints {}", hd));
        GHdFootprints = hd;
        size_t checked = 0;
        // Prints where the frame's mirroring isn't the drawn one, so the original swapped the feet.
        size_t swapped = 0;

        WalkPrinting(player, Legs, PerLeg, step,
                     [&](const MadePrint& made)
                     {
                         MCTest::Scope printScope(std::format("leg {}, facing {:.1f}", made.Leg, made.Facing));
                         const std::span<const uint8_t> packet = FootPacket(type, made.Gesture);
                         const int32_t early = hd != 0 ? 0 : EarlyFrames;
                         const bool first = made.LegsFrame == FootValue<int32_t>(packet, FirstFoot) - early;
                         const bool second = made.LegsFrame == FootValue<int32_t>(packet, SecondFoot) - early;
                         REQUIRE(first != second);
                         const std::optional<MCDrawnFacing> drawn =
                             player.Tree->DrawnFacing(made.Gesture, 0, made.Facing);
                         REQUIRE(drawn.has_value());
                         // The actor's mirroring once the frame is drawn is the drawn one.
                         CHECK_EQ(drawn->Mirrored, made.Mirrored);
                         int32_t reverse = 0;
                         float frameRate = 0.0f;
                         player.Tree->SetGesture(made.Gesture, 0, made.Facing, reverse, frameRate);
                         swapped += (reverse != 0) != drawn->Mirrored ? 1 : 0;
                         const bool mirrored = hd != 0 ? drawn->Mirrored : reverse != 0;
                         // A foot's offset: its own, or (mirrored) the other foot's.
                         const size_t offset = (first != mirrored ? FirstFoot : SecondFoot) + 1;
                         const float x = FootValue<float>(packet, offset);
                         const float y = FootValue<float>(packet, offset + 1);
                         float degrees = std::floor(made.Facing / OriginalStep) * OriginalStep;

                         if (hd != 0)
                         {
                             const float off = std::remainder(made.Facing - drawn->Degrees, 360.0f);
                             CHECK(std::fabs(off) < LegFacing);
                             degrees = drawn->Degrees;
                         }

                         const double turn = degrees * MCMoverMath::DegreesToRadians;
                         const auto c = static_cast<float>(std::cos(turn));
                         const auto s = static_cast<float>(std::sin(turn));
                         CHECK(std::fabs(made.Print.X - made.Mech.X - (c * x - s * y) * FootScale) < Tolerance);
                         CHECK(std::fabs(made.Print.Y - made.Mech.Y - (s * x + c * y) * FootScale) < Tolerance);
                         ++checked;
                     });

        CHECK(checked >= static_cast<size_t>(Legs));
        CHECK(swapped > 0);
    }

    GHdFootprints = 0;
}

/// <summary>
/// With HDTurns a mech's drawn facing (MCMechActor::TurnedFacing) follows its own, eased: mission 1's mech, walking a
/// leg then turned 120 degrees onto another, is drawn turning no faster than TurnEaseRate (or kept within TurnLagMost),
/// lags its facing by more than a facing's width (~21 degrees) as it turns, and catches up once it walks straight; with
/// HDTurns off it is drawn at its own facing every frame.
/// </summary>
TEST_CASE_ISOLATED("game: with HDTurns a mech is drawn turning eased, catching up with its facing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const PlayerMech player = OwnPlayerMech();
    const std::shared_ptr<MCMechWarrior> pilot = player.Mech->GetPilot();
    REQUIRE(pilot != nullptr);
    constexpr float FirstWay = 10.0f;
    constexpr float Turn = 120.0f;
    constexpr float LegDistance = 250.0f;
    constexpr int32_t LegFrames = 90;
    constexpr float LegWidth = 360.0f / 17.0f;
    constexpr float Caught = 0.5f;
    constexpr float Slack = 0.01f;
    const auto wrap = [](float degrees) { return std::remainder(degrees, 360.0f); };
    const auto walk = [&](float way)
    {
        const double radians = way * MCMoverMath::DegreesToRadians;
        const MCVector3D from = player.Mech->GetPosition();
        pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player,
                                MCVector3D(from.X + static_cast<float>(std::cos(radians)) * LegDistance,
                                           from.Y + static_cast<float>(std::sin(radians)) * LegDistance, from.Z),
                                -1, 0);
    };

    for (const int turns : {0, 1})
    {
        MCTest::Scope scope(std::format("HDTurns {}", turns));
        GHdTurns = turns;
        walk(FirstWay);

        for (int32_t frame = 0; frame < LegFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        walk(FirstWay + Turn);
        float most = 0.0f;
        float last = 0.0f;
        std::optional<float> before;

        for (int32_t frame = 0; frame < LegFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
            REQUIRE(player.Actor->TurnedFacing.has_value());
            const float drawn = *player.Actor->TurnedFacing;
            const auto own = static_cast<float>(MCActorFacing(*player.Mech));
            const float lag = std::fabs(wrap(own - drawn));
            MCTest::Scope frameScope(std::format("frame {}: facing {:.1f}, drawn {:.1f}", frame, own, drawn));
            CHECK(lag <= TurnLagMost + Slack);

            // Each frame it turns at the ease rate, or is pulled along within the lag.
            if (turns != 0 && before.has_value() && lag < TurnLagMost - Slack)
            {
                CHECK(std::fabs(wrap(drawn - *before)) <= TurnEaseRate * FrameSeconds + Slack);
            }

            if (turns == 0)
            {
                CHECK(lag == 0.0f);
            }

            most = std::max(most, lag);
            last = lag;
            before = drawn;
        }

        if (turns != 0)
        {
            CHECK(most > LegWidth);
        }

        CHECK(last < Caught);
    }

    GHdTurns = 0;
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
/// <c>--hd-effects-arms 1</c>, the player's mech throws off both its arms instead (<c>arms_...</c>; the debris named): with
/// HD art torn off and dropped (no hit takes them off), or as <c>--hd-arms</c> says; with <c>--hd-original 1</c> each
/// tumbling at 7 frames a second as it flies. <c>--hd-effects-zoom &lt;lines&gt;</c> zooms
/// the view to that many lines. <c>--hd-art-modes &lt;frame&gt;:&lt;mode&gt;,...</c> switches the art as
/// each plays (original, hd, day, dusk: the debug console's art command).
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

    if (const std::optional<std::string_view> zoom = MCTest::Option("hd-effects-zoom"); zoom.has_value())
    {
        // strtof: libc++ has floating-point from_chars only from macOS 26.
        MCFixedZoomHeight = std::strtof(std::string(*zoom).c_str(), nullptr);
    }

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
        MCFixedZoomHeight = 0.0f;
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

    MCFixedZoomHeight = 0.0f;
}

namespace
{
    /// <summary>
    /// What the HD art audit tallies a draw by: the command, whether it was sharp, what it drew (data, or the window it
    /// copied from), and where.
    /// </summary>
    using AuditKey = std::tuple<std::string, bool, MCAddressKey, MCAddressKey, MCAddressKey>;

    /// <summary><paramref name="draw"/>'s <see cref="AuditKey"/>, drawn sharp or not (<paramref name="sharp"/>).</summary>
    AuditKey AuditKeyOf(const MCVulkanRenderer::AuditedDraw& draw, bool sharp)
    {
        return {std::string(draw.Command), sharp, MCAddressKey::OfData(draw.Data),
                draw.From ? MCAddressKey::Of(draw.From->get()) : MCAddressKey(), MCAddressKey::Of(draw.Window.get())};
    }

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
    /// colour as <paramref name="paint"/> makes it (inverted), at <see cref="TerrainScale"/> (nearest), opaque, with the
    /// index picture beside it.
    /// </summary>
    size_t WriteTerrainPack(const std::filesystem::path& pack, const MCTerrainMesh& mesh,
                            std::span<const SDL_Color, 256> colors,
                            const std::function<SDL_Color(SDL_Color)>& paint = Inverted)
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
                    const SDL_Color painted = paint(colors[decoded.Index[from]]);
                    color[to] = painted.r;
                    color[to + 1] = painted.g;
                    color[to + 2] = painted.b;
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
        const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
        REQUIRE(inUse.has_value());
        const MCTerrainMesh& mesh = *inUse;
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        HdGround ground;
        ground.Tiles = WriteTerrainPack(scene->Pack, mesh, colors);
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
    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    // A pack of the ground alone: the scene's pack has the mech's frames, which a frame reads when it draws the mech.
    const std::filesystem::path pack = scene->Scratch / "ground";
    const size_t tiles = WriteTerrainPack(pack, mesh, colors);
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

/// <summary>
/// Port-only: the ground goes up to the GPU while the mission loads (MCTerrainUploadGround), as the first frame would
/// have put it up (the off-map ground raised as on the first turn), so the frames find it there; HD art's ground too,
/// and its upload keeps no memory. A mesh built again of the same tiles sends its cells alone.
/// </summary>
TEST_CASE_ISOLATED("game: the ground goes up to the GPU as the mission loads, and its upload memory goes back")
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

    // The load put it up; the frames since, the mission's first among them, found the mesh they'd build there.
    CHECK_EQ(renderer->TerrainUploads(), uint64_t{1});
    CHECK_EQ(renderer->TerrainTileUploads(), 1);
    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_ground_upload";
    std::filesystem::remove_all(scratch);
    REQUIRE(WriteTerrainPack(scratch / "pack", mesh, colors) > 0);

    {
        const MCHdArtOn hdArt(scratch / "pack");
        MCTerrainReadHdTiles();

        if (const std::expected<void, std::string> uploaded = MCTerrainUploadGround(); !uploaded)
        {
            FAIL(uploaded.error());
        }

        CHECK(renderer->HdTerrain());
        CHECK_EQ(renderer->TerrainTileUploads(), 2);
        MCTestGame::RunFrame(FrameSeconds);
        CHECK_EQ(renderer->TerrainUploads(), uint64_t{2});
        CHECK_EQ(renderer->TerrainTileUploads(), 2);
        CHECK(renderer->UploadBytesKept() <= 2 * MCVulkanRenderer::KeptUploadBytes);

        MCTerrainForgetMesh();
        MCTestGame::RunFrame(FrameSeconds);
        CHECK_EQ(renderer->TerrainUploads(), uint64_t{3});
        CHECK_EQ(renderer->TerrainTileUploads(), 2);
        CHECK(renderer->HdTerrain());
    }

    std::filesystem::remove_all(scratch);
}

/// <summary>
/// Port-only (HD weather's puddles and snow, kept off cliffs): the ground mesh carries how many of each cell's movement
/// cells can't be entered, as the map has them (none off the map), and its eight neighbours' (draw.pshader's BlockedAt
/// blends the share between them, so the tiles' seams don't show); the map has ground none of which can be entered,
/// beside ground that can.
/// </summary>
TEST_CASE_ISOLATED("game: the ground mesh carries the cells mechs can't enter and its neighbours', so puddles and snow "
                   "keep off cliffs without showing the tiles")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    MCTestGame::RunFrame(FrameSeconds);
    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    REQUIRE_EQ(mesh.CellBlocked.size(), mesh.CellTiles.size());
    REQUIRE_EQ(mesh.CellBlockedRound.size(), mesh.CellTiles.size());
    const MCScenarioMap& map = GameMap();
    constexpr uint32_t AllCells = MapCellDim * MapCellDim;
    constexpr uint32_t CountMask = (1u << MCTerrainMesh::BlockedRoundBits) - 1u;
    const auto blockedAt = [&map](int32_t row, int32_t col)
    { return map.OnMap(row, col) ? map.TileAt(row, col).BlockedCells() : 0u; };
    size_t matching = 0;
    size_t roundMatching = 0;
    size_t walledOff = 0;
    size_t besideOpen = 0;

    for (int32_t r = 0; r < mesh.Rows; ++r)
    {
        for (int32_t c = 0; c < mesh.Cols; ++c)
        {
            const int32_t row = mesh.FirstRow + r;
            const int32_t col = mesh.FirstCol + c;
            const size_t cell = static_cast<size_t>(r) * mesh.Cols + c;
            const uint8_t carried = mesh.CellBlocked[cell];
            const uint32_t round = mesh.CellBlockedRound[cell];
            matching += carried == blockedAt(row, col) ? 1 : 0;
            walledOff += carried == AllCells ? 1 : 0;
            bool roundMatches = true;
            bool open = false;

            for (int32_t dr = -1; dr <= 1; ++dr)
            {
                for (int32_t dc = -1; dc <= 1; ++dc)
                {
                    if (dr == 0 && dc == 0)
                    {
                        continue;
                    }

                    const uint32_t neighbour = round >> MCTerrainMesh::BlockedRoundShift(dr, dc) & CountMask;
                    roundMatches = roundMatches && neighbour == blockedAt(row + dr, col + dc);
                    open = open || (neighbour == 0 && map.OnMap(row + dr, col + dc));
                }
            }

            roundMatching += roundMatches ? 1 : 0;
            besideOpen += carried == AllCells && open ? 1 : 0;
        }
    }

    CHECK_EQ(matching, mesh.CellBlocked.size());
    CHECK_EQ(roundMatching, mesh.CellBlockedRound.size());
    CHECK(walledOff > 0);
    CHECK(besideOpen > 0);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>
/// Port-only (HD weather at the fog of war's edge): the ground frame measures how each haze table darkens the palette's
/// pale greys, so the snow and the wet ground's sky there are darkened as the ground is: each channel 0 to 1, and the
/// edge with the fewest corners seen darker than none.
/// </summary>
TEST_CASE_ISOLATED("game: the fog of war's edge darkens the snow and the wet ground as its haze darkens the ground")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    MCTestGame::RunFrame(FrameSeconds);
    const std::array<std::array<float, 3>, MCTerrainFogEdgeLevels> shades = MCTerrainFogEdgeShades();
    constexpr float Undarkened = 1.0f;

    for (const std::array<float, 3>& shade : shades)
    {
        for (const float channel : shade)
        {
            CHECK(channel >= 0.0f);
            CHECK(channel <= Undarkened);
        }
    }

    const std::array<float, 3>& fewest = shades.front();
    CHECK(fewest[0] + fewest[1] + fewest[2] < 3.0f * Undarkened);
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
    /// A colour pressed into the middle (MutedFloor and MutedShare of it): ground bright enough to measure, and dim
    /// enough that none of it counts as a light's own pixels (composite.pshader's SelfLit, above 55%).
    /// </summary>
    constexpr int32_t MutedFloor = 60;
    constexpr int32_t MutedShare = 3;
    constexpr int32_t MutedOf = 10;

    SDL_Color Muted(SDL_Color color)
    {
        const auto mute = [](uint8_t value) { return static_cast<uint8_t>(MutedFloor + value * MutedShare / MutedOf); };
        return SDL_Color{mute(color.r), mute(color.g), mute(color.b), 255};
    }

    /// <summary>
    /// Writes a relief map of each of the ground mesh's tiles into <paramref name="pack"/>'s relief folder: BC5 at
    /// <see cref="TerrainScale"/>, the ground rising <paramref name="rise"/> steps (MCHdArt::ReliefFlat) a pixel right
    /// everywhere, and flat down the picture.
    /// </summary>
    size_t WriteReliefPack(const std::filesystem::path& pack, const MCTerrainMesh& mesh, int32_t rise)
    {
        const std::filesystem::path folder = pack / "relief";
        std::filesystem::create_directories(folder);
        // A block of two BC4 blocks (R, then G), each with both ends its value: every pixel decodes to it.
        constexpr size_t Bc4Bytes = MCBc7::BlockBytes / 2;
        std::array<uint8_t, MCBc7::BlockBytes> block{};
        block[0] = static_cast<uint8_t>(MCHdArt::ReliefFlat + rise);
        block[1] = block[0];
        block[Bc4Bytes] = static_cast<uint8_t>(MCHdArt::ReliefFlat);
        block[Bc4Bytes + 1] = block[Bc4Bytes];
        size_t written = 0;

        for (const std::vector<uint8_t>& tile : mesh.Tiles)
        {
            const DecodedTile decoded = DecodeTile(tile);

            if (decoded.Width == 0)
            {
                continue;
            }

            MCHdArt::Frame relief;
            relief.Scale = TerrainScale;
            relief.Width = decoded.Width * TerrainScale;
            relief.Height = decoded.Height * TerrainScale;
            relief.Format = MCHdArt::ColorFormat::Bc5;
            const size_t blocks = static_cast<size_t>(MCBc7::Padded(relief.Width) / MCBc7::BlockSide) *
                                  static_cast<size_t>(MCBc7::Padded(relief.Height) / MCBc7::BlockSide);

            for (size_t i = 0; i < blocks; ++i)
            {
                relief.Colors.insert(relief.Colors.end(), block.begin(), block.end());
            }

            const std::vector<uint8_t> file = MCHdArt::EncodeHdf(relief);
            std::ofstream(folder / (MCHdArt::TerrainTileName(tile) + ".hdf"), std::ios::binary)
                .write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
            ++written;
        }

        return written;
    }

    /// <summary>composite.pshader's ReliefSteps (a relief map's steps for a rise of a texel) and SunFacingMost.</summary>
    constexpr float ShaderReliefSteps = 127.0f;
    constexpr float ShaderSunFacingMost = 0.3f;
}

/// <summary>
/// Relief (GHdRelief) with HD lighting by day, on the screen drawn by the GPU over mission 1, paused, with a pack of the
/// map's tiles (muted) whose relief maps rise evenly to the right: on, the HD ground facing the sun takes more of the
/// world's own light, as much as its maps' slope, their steepness and the sun's way make it (composite.pshader's
/// SunFacing), and with the sun on its other side as much less; off, the sun's way changes nothing.
/// </summary>
TEST_CASE_ISOLATED("game: relief lights the HD ground by the pack's relief maps, brighter facing the sun")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    // A pack of the ground alone.
    const std::filesystem::path pack = scene->Scratch / "ground";
    constexpr int32_t Rise = 20;
    REQUIRE(WriteTerrainPack(pack, mesh, colors, Muted) > 0);
    REQUIRE(WriteReliefPack(pack, mesh, Rise) > 0);
    const MCHdArtOn hdArt(pack);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const int lightingWas = GHdLighting;
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    GHdLighting = HdLightingOn;
    GamePaused = true;

    // Two frames each: the first may build the ground again with its relief maps or without them.
    const auto shown = [&](int relief, float sunRight)
    {
        GHdRelief = relief;
        lighting.SunShadowRight = sunRight;
        MCTestGame::RunFrame(FrameSeconds);
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        return std::move(*frame);
    };

    const float right = kept.SunShadowRight;
    const std::vector<SDL_Color> flat = shown(0, right);
    const std::vector<SDL_Color> flatOtherSun = shown(0, -right);
    const std::vector<SDL_Color> facing = shown(1, right);
    const std::vector<SDL_Color> away = shown(1, -right);
    GHdRelief = 0;
    GamePaused = false;
    GHdLighting = lightingWas;
    lighting = kept;

    // The maps' slope a pixel along the ground against the sun's way (SlopeAt's and the presenter's; their scale by the
    // brightness relief's span cancels).
    const float toward = static_cast<float>(Rise) / ShaderReliefSteps * kept.ReliefMaps * kept.SunRelief * right;
    const float more = ShaderSunFacingMost * std::tanh(toward / ShaderSunFacingMost);

    // The world clear of the interface (the panels at the left, whose minimap blinks, the paused message at the top,
    // the mech bar at the bottom), where it isn't too dark to measure.
    constexpr float MinLuma = 40.0f;
    constexpr float Tolerance = 0.03f;
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    const int32_t bottom = height - height / 8;
    size_t measured = 0;
    size_t brighter = 0;
    size_t darker = 0;
    size_t sunChanged = 0;
    std::vector<float> facingShares;
    std::vector<float> awayShares;

    for (int32_t y = top; y < bottom; ++y)
    {
        for (int32_t x = left; x < width; ++x)
        {
            const size_t i = static_cast<size_t>(y) * width + x;
            const float under = LumaOf(flat[i]);
            sunChanged += SameColor(flatOtherSun[i], flat[i]) ? 0 : 1;

            if (under < MinLuma)
            {
                continue;
            }

            ++measured;
            facingShares.push_back(LumaOf(facing[i]) / under);
            awayShares.push_back(LumaOf(away[i]) / under);
            brighter += std::fabs(facingShares.back() - (1.0f + more)) <= Tolerance ? 1 : 0;
            darker += std::fabs(awayShares.back() - (1.0f - more)) <= Tolerance ? 1 : 0;
        }
    }

    REQUIRE(measured > static_cast<size_t>(width - left) * (bottom - top) / 2);
    const auto median = [](std::vector<float>& shares)
    {
        std::ranges::nth_element(shares, shares.begin() + static_cast<ptrdiff_t>(shares.size() / 2));
        return shares[shares.size() / 2];
    };

    const float facingMedian = median(facingShares);
    const float awayMedian = median(awayShares);
    std::cout << std::format("  expected x{:.3f} facing the sun, x{:.3f} away; median x{:.3f} and x{:.3f}; "
                             "{:.0f}% and {:.0f}% of {} pixels within {:.2f}\n",
                             1.0f + more, 1.0f - more, facingMedian, awayMedian, 100.0 * brighter / measured,
                             100.0 * darker / measured, measured, Tolerance);
    CHECK_EQ(sunChanged, 0u);
    CHECK(std::fabs(facingMedian - (1.0f + more)) <= Tolerance / 3.0f);
    CHECK(std::fabs(awayMedian - (1.0f - more)) <= Tolerance / 3.0f);
    CHECK(brighter * 2 > measured);
    CHECK(darker * 2 > measured);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>
/// A mech stands on the HD ground, not in its relief, whichever way it faces: the relief maps light none of its pixels,
/// its mirrored parts' (drawn by a transform, MCVulkanRenderer::ShapeBlit) as its others', drawn from the original or
/// from the pack, nor its frames' smooth edges past the original's pixels. Their indices tell the composite what stands
/// on the ground (the relief's light, the water, the rain's wet look), and a frame's mark pass where it shows past them;
/// a mirrored part once wrote none, and showed the ground's relief and puddles through it, and a frame's edge read as
/// the ground.
/// </summary>
TEST_CASE_ISOLATED("game: the relief maps light none of a mech's pixels, its mirrored parts' too")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    // The ground with its relief maps alone, and with the mech's standing frames (colours inverted) too.
    constexpr int32_t Rise = 20;
    const std::filesystem::path ground = scene->Scratch / "ground";

    for (const std::filesystem::path& pack : {ground, scene->Pack})
    {
        REQUIRE(WriteTerrainPack(pack, mesh, colors, Muted) > 0);
        REQUIRE(WriteReliefPack(pack, mesh, Rise) > 0);
    }

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const int lightingWas = GHdLighting;
    MCSceneLights::MCLighting& lighting = MCSceneLights::Lighting();
    const MCSceneLights::MCLighting kept = lighting;
    REQUIRE(kept.ReliefMaps > 0.0f);
    GHdLighting = HdLightingOn;
    GHdRelief = 1;
    GamePaused = true;

    // The view's middle, where the mech stands (clear of the interface).
    const int32_t left = width / 4;
    const int32_t top = height / 4;
    const int32_t cropWidth = width / 2;
    const int32_t cropHeight = height / 2;
    // Two frames each: the first may build the ground again with its relief maps. The relief maps as steep as
    // `steeper` times as made (the sun's relief, which lights everything, as it is).
    const auto shown = [&](float steeper)
    {
        lighting.ReliefMaps = kept.ReliefMaps * steeper;
        MCTestGame::RunFrame(FrameSeconds);
        MCTestGame::RunFrame(FrameSeconds);
        const auto frame = display->ReadFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->size() == static_cast<size_t>(width) * height);
        std::vector<SDL_Color> middle;
        middle.reserve(static_cast<size_t>(cropWidth) * cropHeight);

        for (int32_t y = top; y < top + cropHeight; ++y)
        {
            const auto row = frame->begin() + static_cast<ptrdiff_t>(y) * width + left;
            middle.insert(middle.end(), row, row + cropWidth);
        }

        return middle;
    };

    // The mech facing four ways, a quarter turn apart (half of them mirroring parts): its frames from the original and
    // from the pack, on the relief maps as made and twice as steep.
    constexpr int32_t Facings = 4;
    constexpr float FirstFacing = 45.0f;
    constexpr float FacingStep = 360.0f / Facings;
    constexpr float Steeper = 2.0f;
    MCBattleMech& mech = *scene->Player.Mech;
    struct Facing
    {
        std::vector<SDL_Color> Relief;
        std::vector<SDL_Color> Steep;
        std::vector<SDL_Color> HdRelief;
        std::vector<SDL_Color> HdSteep;
        bool Mirrored = false;
    };

    std::array<Facing, Facings> facings;

    for (const bool hd : {false, true})
    {
        const MCHdArtOn hdArt(hd ? scene->Pack : ground);

        for (int32_t i = 0; i < Facings; ++i)
        {
            MCTestGame::FaceMech(mech, FirstFacing + static_cast<float>(i) * FacingStep);
            Facing& facing = facings[static_cast<size_t>(i)];
            (hd ? facing.HdRelief : facing.Relief) = shown(1.0f);
            (hd ? facing.HdSteep : facing.Steep) = shown(Steeper);

            for (int32_t part = 0; part < MechPartCount; ++part)
            {
                const std::optional<MCGesturePacket> packet =
                    scene->Player.Tree->GesturePacket(StandGesture, part, static_cast<float>(MCActorFacing(mech)));
                facing.Mirrored = facing.Mirrored || (packet.has_value() && packet->Reverse);
            }
        }
    }

    GHdRelief = 0;
    GamePaused = false;
    GHdLighting = lightingWas;
    lighting = kept;

    // The mech's pixels: where its frames from the pack (inverted) show far other colours than the original's, more
    // than MechApart in a channel (the ground about it differs a little too: the sun's relief shades it by the mech's
    // brightness); of those, the ones inside it (the frame's smooth edges blend the ground's light in), and the ones at
    // its edge, where the frame from the pack shows over the ground, past the original's pixels too.
    constexpr int MechApart = 64;
    const auto at = [&](int32_t x, int32_t y) { return static_cast<size_t>(y) * cropWidth + x; };
    size_t groundPixels = 0;
    size_t groundLit = 0;
    size_t edgePixels = 0;
    int32_t mirrored = 0;

    for (int32_t i = 0; i < Facings; ++i)
    {
        const Facing& facing = facings[static_cast<size_t>(i)];
        const auto isMech = [&](int32_t x, int32_t y)
        {
            const SDL_Color original = facing.Relief[at(x, y)];
            const SDL_Color hd = facing.HdRelief[at(x, y)];
            return std::max({std::abs(original.r - hd.r), std::abs(original.g - hd.g), std::abs(original.b - hd.b)}) >
                   MechApart;
        };

        size_t inside = 0;
        size_t lit = 0;
        size_t hdLit = 0;
        size_t edge = 0;
        size_t edgeLit = 0;

        for (int32_t y = 1; y < cropHeight - 1; ++y)
        {
            for (int32_t x = 1; x < cropWidth - 1; ++x)
            {
                if (!isMech(x, y))
                {
                    ++groundPixels;
                    groundLit += SameColor(facing.Relief[at(x, y)], facing.Steep[at(x, y)]) ? 0 : 1;
                    continue;
                }

                const bool hdLitHere = !SameColor(facing.HdRelief[at(x, y)], facing.HdSteep[at(x, y)]);

                if (!(isMech(x - 1, y - 1) && isMech(x, y - 1) && isMech(x + 1, y - 1) && isMech(x - 1, y) &&
                      isMech(x + 1, y) && isMech(x - 1, y + 1) && isMech(x, y + 1) && isMech(x + 1, y + 1)))
                {
                    ++edge;
                    edgeLit += hdLitHere ? 1 : 0;
                    continue;
                }

                ++inside;
                lit += SameColor(facing.Relief[at(x, y)], facing.Steep[at(x, y)]) ? 0 : 1;
                hdLit += hdLitHere ? 1 : 0;
            }
        }

        mirrored += facing.Mirrored ? 1 : 0;
        edgePixels += edge;
        std::cout << std::format("  facing {:.0f}{}: {} of the mech's pixels lit by the relief from the original, {} "
                                 "from the pack, of {} inside it; from the pack {} of {} at its edge\n",
                                 FirstFacing + static_cast<float>(i) * FacingStep,
                                 facing.Mirrored ? " (mirrored parts)" : "", lit, hdLit, inside, edgeLit, edge);
        CHECK(inside > 0);
        CHECK_EQ(lit, 0u);
        CHECK_EQ(hdLit, 0u);
        CHECK_EQ(edgeLit, 0u);
    }

    std::cout << std::format("  {} of {} ground pixels lit by the relief\n", groundLit, groundPixels);
    CHECK(mirrored > 0);
    CHECK(edgePixels > 0);
    CHECK(groundLit * 2 > groundPixels);
    std::filesystem::remove_all(scene->Scratch);
}

/// <summary>
/// HD weather's feet in puddles (MCWeather::AddStep), on mission 1's HD ground with its relief maps (the wet look) in a
/// downpour, paused, as the game's time goes: a foot coming down in a puddle sprays and the puddle reacts round it, from
/// its footprint's outline (the print taken as circles apart), changing only what lies near it, for as long as it lasts;
/// one on dry ground, or at a strength of 0, changes nothing shown; and none of it throws the game's dice.
/// </summary>
TEST_CASE_ISOLATED("game: in a downpour a foot coming down in a puddle sprays and the puddle reacts round its print")
{
    std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

    if (!scene.has_value())
    {
        return;
    }

    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    constexpr int32_t Rise = 20;
    const std::filesystem::path ground = scene->Scratch / "ground";
    REQUIRE(WriteTerrainPack(ground, mesh, colors, Muted) > 0);
    REQUIRE(WriteReliefPack(ground, mesh, Rise) > 0);
    const MCHdArtOn hdArt(ground);
    const int lightingWas = GHdLighting;
    GHdLighting = HdLightingOn;
    GHdRelief = 1;
    GHdWeather = HdWeatherDownpour;
    // The puddles as made from the start (with showers, at a mission's start they have yet to spread).
    MCWeather::Settings().Showers = 0.0f;
    GamePaused = true;
    MCTestGame::RunFrame(FrameSeconds);
    MCTestGame::RunFrame(FrameSeconds);
    REQUIRE(MCSceneLights::ReliefMapsOn());

    // A puddle a foot splashes in round the mech, the camera over it, and dry ground near it.
    constexpr float Reach = 200.0f;
    constexpr float Apart = 4.0f;
    const MCVector3D from = scene->Player.Mech->GetPosition();
    const auto find = [&](bool wet) -> std::optional<MCVector3D>
    {
        const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
        REQUIRE(camera != nullptr);

        for (float dy = -Reach; dy <= Reach; dy += Apart)
        {
            for (float dx = -Reach; dx <= Reach; dx += Apart)
            {
                MCVector3D point(from.X + dx, from.Y + dy, 0.0f);
                point.Z = MCParticles::GroundAt(point.X, point.Y);
                const std::optional<uint8_t> drawn = MCParticles::GroundDrawnAt(point.X, point.Y, point.Z, *camera);

                if (drawn.has_value() && !MCParticles::IsWaterColor(*drawn) &&
                    MCWeather::PuddleAt(point, *camera) == wet)
                {
                    return point;
                }
            }
        }

        return std::nullopt;
    };

    const std::optional<MCVector3D> puddle = find(true);
    REQUIRE(puddle.has_value());
    Eye()->SetPosition(*puddle);
    MCTestGame::RunFrame(FrameSeconds);
    const std::optional<MCVector3D> dry = find(false);
    REQUIRE(dry.has_value());
    REQUIRE(MCWeather::PuddleAt(*puddle, *ActiveMainCamera()));

    // The mech's footprint, facing the first way.
    const std::span<const std::vector<uint8_t>> prints = CraterManager().Shapes();
    const auto printType = static_cast<size_t>(scene->Player.Mech->TypeAs<MCBattleMechType>().FootprintType);
    REQUIRE(printType < prints.size());
    const std::span<const uint8_t> print = prints[printType];

    // The frame shown Shown frames after a foot comes down (or none), the most puddles reacting then, the first's print,
    // and how many still react once its time is up; the weather's dice started again.
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const float now = ScenarioTime;
    constexpr float Step = 1.0f / 30.0f;
    constexpr int32_t Shown = 6;
    // How long a puddle reacts (MCWeather.cpp's StepLife).
    constexpr float Life = 0.7f;
    const auto lastFrame = static_cast<int32_t>(std::ceil(Life / Step)) + 2;
    struct Stepped
    {
        std::vector<SDL_Color> Frame;
        size_t Reacting = 0;
        std::array<MCWeather::MCGroundCircle, MCWeather::PrintCircles> Print{};
        size_t Left = 0;
    };

    const auto stepped = [&](const std::optional<MCVector3D>& foot)
    {
        MCWeather::Clear();
        ScenarioTime = now;
        MCTestGame::RunFrame(FrameSeconds);

        if (foot.has_value())
        {
            MCWeather::AddStep(*foot, print, 0);
        }

        Stepped made;

        for (int32_t frame = 1; frame <= lastFrame; ++frame)
        {
            ScenarioTime = now + Step * static_cast<float>(frame);
            MCTestGame::RunFrame(FrameSeconds);
            const std::span<const MCWeather::MCPuddleStep> reacting = MCWeather::ShownPuddleSteps();
            made.Reacting = std::max(made.Reacting, reacting.size());

            if (frame == 1 && !reacting.empty())
            {
                made.Print = reacting.front().Print;
            }

            if (frame == Shown)
            {
                auto shown = display->ReadFrame();
                REQUIRE(shown.has_value());
                REQUIRE(shown->size() == static_cast<size_t>(width) * height);
                made.Frame = std::move(*shown);
            }
        }

        made.Left = MCWeather::ShownPuddleSteps().size();
        return made;
    };

    const uint32_t dice = MCGameContext::Current().Random().State();
    const Stepped none = stepped(std::nullopt);
    const Stepped inPuddle = stepped(puddle);
    const Stepped onDry = stepped(dry);
    MCWeather::Settings().Steps = 0.0f;
    const Stepped still = stepped(puddle);
    MCWeather::Settings().Steps = 1.0f;
    CHECK(MCGameContext::Current().Random().State() == dice);
    ScenarioTime = now;
    GHdWeather = HdWeatherOff;
    MCWeather::Settings().Showers = 1.0f;
    GHdRelief = 0;
    GHdLighting = lightingWas;
    GamePaused = false;

    // The pixels a foot changed, and how far apart they lie.
    struct Touched
    {
        size_t Count = 0;
        int32_t Left = std::numeric_limits<int32_t>::max();
        int32_t Right = std::numeric_limits<int32_t>::min();
        int32_t Top = std::numeric_limits<int32_t>::max();
        int32_t Bottom = std::numeric_limits<int32_t>::min();
    };

    const auto changedBy = [&](const Stepped& made)
    {
        Touched changed;

        for (int32_t y = 0; y < height; ++y)
        {
            for (int32_t x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;

                if (!SameColor(made.Frame[i], none.Frame[i]))
                {
                    ++changed.Count;
                    changed.Left = std::min(changed.Left, x);
                    changed.Right = std::max(changed.Right, x);
                    changed.Top = std::min(changed.Top, y);
                    changed.Bottom = std::max(changed.Bottom, y);
                }
            }
        }

        return changed;
    };

    const Touched splashed = changedBy(inPuddle);
    const Touched dryChanged = changedBy(onDry);
    const Touched stillChanged = changedBy(still);
    const auto apart = std::ranges::count_if(
        inPuddle.Print, [&](const MCWeather::MCGroundCircle& circle)
        { return std::hypot(circle.East - inPuddle.Print[0].East, circle.North - inPuddle.Print[0].North) > 0.5f; });
    std::cout << std::format(
        "  a foot in a puddle: {} pixels changed over {}x{}, {} of its print's circles apart from the "
        "first; on dry ground {}, at a strength of 0 {}\n",
        splashed.Count, splashed.Right - splashed.Left + 1, splashed.Bottom - splashed.Top + 1, apart, dryChanged.Count,
        stillChanged.Count);
    CHECK(none.Reacting == 0);
    CHECK(inPuddle.Reacting == 1);
    CHECK(inPuddle.Left == 0);
    CHECK(splashed.Count > 0);
    CHECK(splashed.Right - splashed.Left < width / 3);
    CHECK(splashed.Bottom - splashed.Top < height / 3);
    CHECK(apart > 0);
    CHECK(inPuddle.Print[0].Radius > 0.0f);
    CHECK(onDry.Reacting == 0);
    CHECK(dryChanged.Count == 0);
    CHECK(still.Reacting == 0);
    CHECK(stillChanged.Count == 0);
    std::filesystem::remove_all(scene->Scratch);
}

namespace
{
    /// <summary>
    /// Mission 1's HD ground with its relief maps (which tell the ground from what stands on it) in <paramref
    /// name="weather"/>, paused, the scene's madcat standing on it; HD art on while the result lives. None (noted) without
    /// the game or a GPU.
    /// </summary>
    struct SnowScene
    {
        HdScene Scene;
        std::unique_ptr<MCHdArtOn> HdArt;
        int LightingWas = HdLightingOff;

        ~SnowScene()
        {
            GHdWeather = HdWeatherOff;
            MCWeather::Settings() = {};
            GHdRelief = 0;
            GHdLighting = LightingWas;
            GamePaused = false;
            HdArt.reset();
            std::filesystem::remove_all(Scene.Scratch);
        }
    };

    std::unique_ptr<SnowScene> SetUpSnowScene(int weather)
    {
        std::optional<HdScene> scene = SetUpHdScene(MCGpuDrawing::On);

        if (!scene.has_value())
        {
            return nullptr;
        }

        const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
        REQUIRE(inUse.has_value());
        const MCTerrainMesh& mesh = *inUse;
        std::array<SDL_Color, 256> colors{};
        MCInput::Display()->GetShownColors(colors);
        constexpr int32_t Rise = 20;
        const std::filesystem::path ground = scene->Scratch / "ground";
        REQUIRE(WriteTerrainPack(ground, mesh, colors, Muted) > 0);
        REQUIRE(WriteReliefPack(ground, mesh, Rise) > 0);
        auto made = std::make_unique<SnowScene>();
        made->Scene = std::move(*scene);
        made->HdArt = std::make_unique<MCHdArtOn>(ground);
        made->LightingWas = GHdLighting;
        GHdLighting = HdLightingOn;
        GHdRelief = 1;
        GHdWeather = weather;
        MCWeather::Settings() = {};
        GamePaused = true;
        MCTestGame::RunFrame(FrameSeconds);
        MCTestGame::RunFrame(FrameSeconds);
        REQUIRE(MCSceneLights::ReliefMapsOn());
        return made;
    }

    /// <summary>The frame shown at the game's time <paramref name="time"/>, the game paused.</summary>
    std::vector<SDL_Color> ShownAt(float time)
    {
        ScenarioTime = time;
        MCTestGame::RunFrame(FrameSeconds);
        auto frame = MCInput::Display()->ReadFrame();
        REQUIRE(frame.has_value());
        return std::move(*frame);
    }
}

/// <summary>
/// HD weather's snow on mission 1's HD ground with its relief maps, drawn by the GPU, paused, where the world shows clear
/// of the interface: as the game's time goes flakes fall over the view and rest a moment where they land (no drops,
/// rings or droplets); while the time holds they hold. With none settled yet the ground shows bare; settled (it had
/// snowed long before the start), much of it shows white, the same each time, while the snow lets no flakes fall. None
/// of it throws the game's dice, and with HD weather off the world shows as drawn.
/// </summary>
TEST_CASE_ISOLATED("game: snow falls as flakes that rest where they land, and settles white over the ground")
{
    const std::unique_ptr<SnowScene> scene = SetUpSnowScene(HdWeatherSnow);

    if (scene == nullptr)
    {
        return;
    }

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const float now = ScenarioTime;
    constexpr float Step = 1.0f / 30.0f;
    // Two seconds of frames: the first flakes land, nearer ones still falling.
    constexpr int32_t Frames = 60;
    // Settled, at least Whitened of the world shows Whiter (luma) than bare.
    constexpr float Whitened = 0.15f;
    constexpr float Whiter = 20.0f;
    constexpr float LongBefore = 100000.0f;

    struct Run
    {
        std::vector<SDL_Color> Last;
        std::vector<SDL_Color> Held;
        size_t Count = 0;
        MCWeather::MCSplashPieces Pieces;
        float Share = 0.0f;
    };

    const auto run = [&](int weather, float amount, float settled, float pace)
    {
        GHdWeather = weather;
        MCWeather::Settings().Amount = amount;
        MCWeather::Settings().Settled = settled;
        MCWeather::Settings().SettlePace = pace;
        MCWeather::Clear();
        Run made;

        for (int32_t frame = 0; frame <= Frames; ++frame)
        {
            made.Last = ShownAt(now + Step * static_cast<float>(frame));
        }

        made.Count = MCWeather::Count();
        made.Pieces = MCWeather::Pieces();
        made.Share = MCWeather::ShownCover().Share;
        made.Held = ShownAt(now + Step * static_cast<float>(Frames));
        return made;
    };

    const Run clear = run(HdWeatherOff, 1.0f, 0.0f, 1.0f);
    const uint32_t dice = MCGameContext::Current().Random().State();
    // Bare: nothing settles (the pace 0); once without flakes.
    const Run bare = run(HdWeatherSnow, 1.0f, 0.0f, 0.0f);
    const Run bareStill = run(HdWeatherSnow, 0.0f, 0.0f, 0.0f);
    const Run settled = run(HdWeatherSnow, 0.0f, LongBefore, 1.0f);
    const Run again = run(HdWeatherSnow, 0.0f, LongBefore, 1.0f);
    CHECK(MCGameContext::Current().Random().State() == dice);
    const Run after = run(HdWeatherOff, 1.0f, 0.0f, 1.0f);
    ScenarioTime = now;
    SaveShownFrame(bare.Last, width, height, "weather_snow_bare");
    SaveShownFrame(settled.Last, width, height, "weather_snow_settled");

    // The world clear of the interface (as the rain's test).
    const int32_t left = width / 3;
    const int32_t top = height / 4;
    size_t pixels = 0;
    size_t moved = 0;
    size_t unlike = 0;
    size_t leftOver = 0;
    size_t whiter = 0;

    for (int32_t y = top; y < height; ++y)
    {
        for (int32_t x = left; x < width; ++x)
        {
            const size_t i = static_cast<size_t>(y) * width + x;
            ++pixels;
            moved += SameColor(bare.Held[i], bare.Last[i]) ? 0 : 1;
            unlike += SameColor(settled.Last[i], again.Last[i]) ? 0 : 1;
            leftOver += SameColor(after.Last[i], clear.Last[i]) ? 0 : 1;
            whiter += LumaOf(settled.Last[i]) > LumaOf(bareStill.Last[i]) + Whiter ? 1 : 0;
        }
    }

    const auto share = [&](size_t count) { return static_cast<float>(count) / static_cast<float>(pixels); };
    std::cout << std::format(
        "  snow: {} flakes falling and resting ({} resting), cover {:.2f} bare and {:.2f} settled; {:.3f} of the world "
        "whiter settled than bare; {} moved while the time held, {} unlike the second time, {} left once clear\n",
        bare.Count, bare.Pieces.Flakes, bare.Share, settled.Share, share(whiter), moved, unlike, leftOver);
    CHECK(clear.Count == 0);
    CHECK(bare.Count > 0);
    CHECK(bare.Pieces.Flakes > 0);
    CHECK(bare.Pieces.Droplets == 0);
    CHECK(bare.Pieces.Rings == 0);
    CHECK(bare.Pieces.Dots == 0);
    CHECK(bare.Share == 0.0f);
    CHECK(moved == 0);
    CHECK(settled.Count == 0);
    CHECK(settled.Share > 0.0f);
    CHECK(share(whiter) > Whitened);
    CHECK(unlike == 0);
    CHECK(after.Count == 0);
    CHECK(leftOver == 0);
}

/// <summary>
/// HD weather's snow filling in the footprints and craters (MCWeather::FilledIn, MCCraterManager's), on mission 1's HD
/// ground with its relief maps under a settled blizzard letting no flakes fall, drawn by the GPU, paused: a print made
/// just now shows as made, one made long ago is all but gone under the snow (nothing of it shows more than a phone
/// shows); with HD weather off both show as made. The newer the print, the less it has filled in.
/// </summary>
TEST_CASE_ISOLATED("game: under the snow a footprint fills in by its age, and without snow shows as made")
{
    const std::unique_ptr<SnowScene> scene = SetUpSnowScene(HdWeatherBlizzard);

    if (scene == nullptr)
    {
        return;
    }

    constexpr float LongBefore = 100000.0f;
    MCWeather::Settings().Amount = 0.0f;
    MCWeather::Settings().Settled = LongBefore;
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const float now = ScenarioTime;
    REQUIRE(MCWeather::ShownCover().Share > 0.0f);

    // How far the snow has filled a print in, by its age (a blizzard's prints fill in about two thirds of the way in
    // half a minute): none just made, all but full long after.
    constexpr float Old = 600.0f;
    constexpr float AllButFull = 0.99f;
    CHECK_EQ(MCWeather::FilledIn(0.0f), 0.0f);
    CHECK(MCWeather::FilledIn(10.0f) > 0.0f);
    CHECK(MCWeather::FilledIn(20.0f) > MCWeather::FilledIn(10.0f));
    CHECK(MCWeather::FilledIn(Old) > AllButFull);

    // The madcat's footprints on a grid over the view's right half below its top quarter (clear of the interface),
    // Spaced view pixels apart, none by the mech, the first of each pair made long ago and the second just now (a few
    // pairs, as some lie where the snow has left the ground bare).
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    const auto printType = scene->Scene.Player.Mech->TypeAs<MCBattleMechType>().FootprintType;
    const MCVector3D from = scene->Scene.Player.Mech->GetPosition();
    const MCVector2D feet = MCParticles::ViewPoint(from.X, from.Y, from.Z, *camera);
    constexpr float Reach = 400.0f;
    constexpr float Apart = 6.0f;
    // A print's box (view pixels round its point), and the prints' least distance from each other and the mech.
    constexpr int32_t Box = 12;
    constexpr float Spaced = 40.0f;
    constexpr size_t Pairs = 6;
    std::vector<std::pair<MCVector3D, MCVector2D>> places;

    for (float dy = -Reach; dy <= Reach && places.size() < 2 * Pairs; dy += Apart)
    {
        for (float dx = -Reach; dx <= Reach && places.size() < 2 * Pairs; dx += Apart)
        {
            MCVector3D point(from.X + dx, from.Y + dy, 0.0f);
            point.Z = MCParticles::GroundAt(point.X, point.Y);
            const MCVector2D at = MCParticles::ViewPoint(point.X, point.Y, point.Z, *camera);
            const std::optional<uint8_t> drawn = MCParticles::GroundDrawnAt(point.X, point.Y, point.Z, *camera);
            const bool inView = at.X > static_cast<float>(width / 2 + Box) && at.X < static_cast<float>(width - Box) &&
                                at.Y > static_cast<float>(height / 4 + Box) && at.Y < static_cast<float>(height - Box);
            const bool spaced =
                std::hypot(at.X - feet.X, at.Y - feet.Y) >= Spaced &&
                std::ranges::all_of(places, [&](const auto& place)
                                    { return std::hypot(at.X - place.second.X, at.Y - place.second.Y) >= Spaced; });

            if (inView && spaced && drawn.has_value() && !MCParticles::IsWaterColor(*drawn))
            {
                places.emplace_back(point, at);
            }
        }
    }

    REQUIRE(places.size() == 2 * Pairs);

    // The frames before the prints are made, then with them, under the snow and without.
    const std::vector<SDL_Color> snowNone = ShownAt(now);
    GHdWeather = HdWeatherOff;
    const std::vector<SDL_Color> clearNone = ShownAt(now);

    for (size_t i = 0; i < places.size(); ++i)
    {
        ScenarioTime = i % 2 == 0 ? now - Old : now;
        CraterManager().Place(printType, places[i].first, 0);
    }

    ScenarioTime = now;
    const std::vector<SDL_Color> clearPrints = ShownAt(now);
    GHdWeather = HdWeatherBlizzard;
    const std::vector<SDL_Color> snowPrints = ShownAt(now);
    SaveShownFrame(snowPrints, width, height, "weather_snow_prints");

    // The pixels the old prints, and the new, changed visibly within their boxes: a component by more than Visible (a
    // phone shows little under it; a filled print's ground differs from the ground round it by about that).
    constexpr int32_t Visible = 8;
    const auto changedRound =
        [&](const std::vector<SDL_Color>& with, const std::vector<SDL_Color>& without, size_t first)
    {
        size_t changed = 0;

        for (size_t k = first; k < places.size(); k += 2)
        {
            const MCVector2D& at = places[k].second;

            for (int32_t y = static_cast<int32_t>(at.Y) - Box; y <= static_cast<int32_t>(at.Y) + Box; ++y)
            {
                for (int32_t x = static_cast<int32_t>(at.X) - Box; x <= static_cast<int32_t>(at.X) + Box; ++x)
                {
                    const size_t i = static_cast<size_t>(y) * width + x;
                    const int32_t apart =
                        std::max({std::abs(with[i].r - without[i].r), std::abs(with[i].g - without[i].g),
                                  std::abs(with[i].b - without[i].b)});
                    changed += apart > Visible ? 1 : 0;
                }
            }
        }

        return changed;
    };

    const size_t oldInSnow = changedRound(snowPrints, snowNone, 0);
    const size_t newInSnow = changedRound(snowPrints, snowNone, 1);
    const size_t oldClear = changedRound(clearPrints, clearNone, 0);
    const size_t newClear = changedRound(clearPrints, clearNone, 1);
    std::cout << std::format("  pixels {} prints changed: under the snow {} made long ago and {} just now; without "
                             "snow {} and {}\n",
                             Pairs, oldInSnow, newInSnow, oldClear, newClear);
    // The prints' least pixels, and a filled-in print's most share of a new one's.
    constexpr size_t LeastPrints = 20 * Pairs;
    constexpr size_t FilledShare = 4;
    CHECK(newClear > LeastPrints);
    CHECK(oldClear > LeastPrints);
    CHECK(newInSnow > LeastPrints);
    CHECK(oldInSnow * FilledShare < newInSnow);
}

/// <summary>
/// HD weather's snow zoomed out (MCWeather::FullFlakeLines and ShownSpeckle) on mission 1's HD ground with its relief
/// maps, a light blizzard (its flakes seldom on each other) drawn by the GPU, paused: a view showing twice
/// FullFlakeLines' lines throws no more real flakes than one showing that many, and with the composite's specks laying
/// the rest shows as many flakes as the real ones did in its middle the moment it zoomed out (none laid at
/// FullFlakeLines); zoomed back in, the real flakes and the specks show as many as ever while the specks drain, all gone
/// once the flakes left out would have fallen and rested. Zoomed out a little more with the game's time held, the specks
/// keep their places.
/// </summary>
TEST_CASE_ISOLATED("game: zoomed out, the snow throws no more flakes than its full view, and specks lay the rest")
{
    const std::unique_ptr<SnowScene> scene = SetUpSnowScene(HdWeatherBlizzard);

    if (scene == nullptr)
    {
        return;
    }

    // None settles: the flakes are told by what they change of the bare ground (a cover takes on the specks' look).
    MCWeather::Settings().SettlePace = 0.0f;
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const int32_t width = display->Width();
    const int32_t height = display->Height();
    const float now = ScenarioTime;
    constexpr float Step = 1.0f / 30.0f;
    // Three seconds of frames: as many flakes falling and resting as there will be. The flakes left out are gone after
    // the longest fall and rest (MCWeather.cpp's LongestFlake), a little longer.
    constexpr int32_t Frames = 90;
    constexpr int32_t DrainFrames = 105;
    constexpr float Light = 0.2f;
    constexpr float Out = 2.0f * MCWeather::FullFlakeLines;
    // Farther out, where the specks are most of the flakes, and a little farther.
    constexpr float FarOut = 4.0f * MCWeather::FullFlakeLines;
    constexpr float LittleFarther = 1.01f;
    const auto timeAt = [&](int32_t frame) { return now + Step * static_cast<float>(frame); };

    const auto specksShown = []
    {
        float specks = 0.0f;

        for (const MCWeather::MCSpeckLayer& layer : MCWeather::ShownSpeckle().Layers)
        {
            specks += layer.Density;
        }

        return specks;
    };

    // The view `lines` lines high at frame `frame`, snowing from frame `from` (the weather started afresh there).
    const auto snowFor = [&](float lines, int32_t from, int32_t frame)
    {
        MCFixedZoomHeight = lines;
        MCWeather::Settings().Amount = Light;

        if (from == 0)
        {
            MCWeather::Clear();
        }

        std::vector<SDL_Color> shown;

        for (int32_t at = from; at <= frame; ++at)
        {
            shown = ShownAt(timeAt(at));
        }

        return shown;
    };

    // The same view without a flake at frame `frame`.
    const auto bareAt = [&](float lines, int32_t frame)
    {
        MCFixedZoomHeight = lines;
        MCWeather::Settings().Amount = 0.0f;
        MCWeather::Clear();
        return ShownAt(timeAt(frame));
    };

    // The world's pixels the flakes changed as the eye tells (not the bloom's faint glow round the real ones, which the
    // specks, drawn in the composite, don't cast), clear of the interface (as the rain's test) and, `middle`, in the
    // middle quarter of the view (what a view of half its lines shows).
    constexpr int32_t SeenChange = 12;
    const auto flaked = [&](const std::vector<SDL_Color>& shown, const std::vector<SDL_Color>& bare, bool middle)
    {
        std::vector<bool> changed;

        for (int32_t y = height / 4; y < (middle ? 3 * height / 4 : height); ++y)
        {
            for (int32_t x = width / 3; x < (middle ? 3 * width / 4 : width); ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                const int32_t change = std::max({std::abs(shown[i].r - bare[i].r), std::abs(shown[i].g - bare[i].g),
                                                 std::abs(shown[i].b - bare[i].b)});
                changed.push_back(change >= SeenChange);
            }
        }

        return changed;
    };

    const auto covered = [](const std::vector<bool>& changed)
    { return static_cast<double>(std::ranges::count(changed, true)) / static_cast<double>(changed.size()); };

    const uint32_t dice = MCGameContext::Current().Random().State();
    // Snowing at FullFlakeLines; zoomed out with the time held (its real flakes drawn farther out, none laid as
    // specks yet), then snowing on there.
    const std::vector<SDL_Color> full = snowFor(MCWeather::FullFlakeLines, 0, Frames);
    const size_t fullCount = MCWeather::Count();
    const float fullSpecks = specksShown();
    const std::vector<SDL_Color> justOut = snowFor(Out, Frames, Frames);
    const std::vector<SDL_Color> out = snowFor(Out, Frames + 1, 2 * Frames);
    const size_t outCount = MCWeather::Count();
    const float outSpecks = specksShown();
    SaveShownFrame(out, width, height, "weather_snow_specks");
    // Back in: a frame a second while they drain, then after.
    constexpr int32_t Second = 30;
    std::vector<std::pair<int32_t, std::vector<SDL_Color>>> draining;

    for (int32_t frame = 2 * Frames + Second; frame < 2 * Frames + DrainFrames; frame += Second)
    {
        draining.emplace_back(frame, snowFor(MCWeather::FullFlakeLines, frame - Second + 1, frame));
    }

    snowFor(MCWeather::FullFlakeLines, draining.back().first + 1, 2 * Frames + DrainFrames);
    const float drained = specksShown();

    const double fullShare = covered(flaked(full, bareAt(MCWeather::FullFlakeLines, Frames), false));
    const double justOutShare = covered(flaked(justOut, bareAt(Out, Frames), true));
    const double outShare = covered(flaked(out, bareAt(Out, 2 * Frames), true));
    std::cout << std::format("  real flakes {} at {} lines, {} at {} with specks {:.4f} a view pixel squared; the "
                             "middle {:.3f} flaked as it zoomed out, {:.3f} snowing on\n",
                             fullCount, MCWeather::FullFlakeLines, outCount, Out, outSpecks, justOutShare, outShare);
    // As many as the real ones a view throwing them all shows, give or take the sheets' (where they're thick, more).
    constexpr double Most = 1.25;
    CHECK(fullCount > 0);
    CHECK(fullSpecks == 0.0f);
    CHECK(outCount <= fullCount);
    CHECK(outSpecks > 0.0f);
    CHECK(outShare < justOutShare * Most);
    CHECK(justOutShare < outShare * Most);

    for (const auto& [frame, shown] : draining)
    {
        const double share = covered(flaked(shown, bareAt(MCWeather::FullFlakeLines, frame), false));
        std::cout << std::format("  back in, {:.1f} s on: {:.3f} flaked (snowing there throughout {:.3f})\n",
                                 Step * static_cast<float>(frame - 2 * Frames), share, fullShare);
        CHECK(share < fullShare * Most);
        CHECK(fullShare < share * Most);
    }

    CHECK(drained == 0.0f);

    // Farther out with the time held, zoomed out a little more: the specks stay where they were (most flaked texels
    // the same), as they wouldn't laid afresh (as many as both cover, about the share each covers).
    const std::vector<SDL_Color> far = snowFor(FarOut, 0, Frames);
    const std::vector<SDL_Color> farther = snowFor(FarOut * LittleFarther, Frames, Frames);
    const std::vector<bool> farFlakes = flaked(far, bareAt(FarOut, Frames), false);
    const std::vector<bool> fartherFlakes = flaked(farther, bareAt(FarOut * LittleFarther, Frames), false);
    size_t both = 0;
    size_t either = 0;

    for (size_t i = 0; i < farFlakes.size(); ++i)
    {
        both += farFlakes[i] && fartherFlakes[i] ? 1 : 0;
        either += farFlakes[i] || fartherFlakes[i] ? 1 : 0;
    }

    const double kept = static_cast<double>(both) / static_cast<double>(std::max<size_t>(either, 1));
    std::cout << std::format("  zoomed out from {} to {} lines, {:.2f} of the flaked texels the same ({:.3f} flaked)\n",
                             FarOut, FarOut * LittleFarther, kept, covered(farFlakes));
    constexpr double KeptLeast = 0.6;
    CHECK(kept > KeptLeast);
    CHECK(MCGameContext::Current().Random().State() == dice);
    MCFixedZoomHeight = 0.0f;
    ScenarioTime = now;
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
    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    std::array<SDL_Color, 256> colors{};
    MCInput::Display()->GetShownColors(colors);
    const std::filesystem::path pack = scene->Scratch / "ground";
    REQUIRE(WriteTwoColourTerrain(pack, mesh) > 0);
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
    MCVehiclePurchaseBlock::OnBuyConfirmed(*GlobalLog()->PurVehicleList->Vehicles.front(), 1, 1);
    shoot("purchase_vehicles_stocked");
    GlobalLog()->CurrentInvTab = PilotTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    shoot("purchase_pilots");
    GlobalLog()->CurrentInvTab = ComponentTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    shoot("purchase_components");
    GlobalLog()->CurrentInvTab = MechTab;
    GlobalLog()->SetUpPurchaseScreen(false);
    MCMechPurchaseBlock::OnBuyConfirmed(*GlobalLog()->PurMechList->Mechs.front(), 1, 1);
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

#if MC_AUDIT_CONTROLS
namespace
{
    /// <summary>Calls <paramref name="each"/> with every text button under <paramref name="parent"/> (at any depth).</summary>
    void ForEachTextButton(const MCGuiObject& parent, const std::function<void(const MCLogTextButton&)>& each)
    {
        for (const std::shared_ptr<MCGuiObject>& child : parent.Children())
        {
            if (const auto button = std::dynamic_pointer_cast<MCLogTextButton>(child); button != nullptr)
            {
                each(*button);
            }

            ForEachTextButton(*child, each);
        }
    }
}
#endif

/// <summary>
/// Tool: <c>--hd-extract-menus &lt;folder&gt;</c> writes every picture of the menus' and logistics' art for the HD art
/// generator (tools/upscale/make_menu_pack.py): the TGAs in the install's FastFiles and the GIF packets of the GUI's art
/// file, in the logistics palette, each named by its pixels' hash, with a manifest of where each came from; the
/// pictures of the port's text buttons (MCLogTextButton: the preferences' CONTROLS and the CONTROLS panel's), named as
/// the art's buttons are (<c>textbutton\b&lt;state&gt;_text_&lt;word&gt;.tga</c>, the state's letter h up, g pressed, n
/// grayed); the main menu's UPDATE button (logistics/MCUpdatePage.h) as the menu's own are named
/// (<c>updatebutton\mm_xb&lt;state&gt;_update.tga</c>); and the session screen's MATCH OPTIONS tabs
/// (logistics/MCMatchOptionsTab.h: <c>matchtabs\lsbdw02_match.tga</c> behind MULTIPLAYER CHAT,
/// <c>matchtabs\lsbdw03_match.tga</c> in front).
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
    // UPDATE is drawn in ORIGINAL CINEMA's colours, which its picture holds; MATCH OPTIONS' tabs in the tabs'.
    constexpr std::string_view cinemaPicture = "mm_xbh08.tga";
    std::optional<std::array<SDL_Color, 256>> cinemaColors;
    constexpr std::string_view tabsPicture = "lsbdw02.tga";
    std::optional<std::array<SDL_Color, 256>> tabsColors;
    ForEachPicture(colors, skipped,
                   [&](std::string_view source, std::span<const uint8_t> pixels, int32_t width, int32_t height,
                       std::span<const SDL_Color, 256> pictureColors)
                   {
                       WritePicture(folder, manifest, source, pixels, width, height, pictureColors, written);
                       ++pictures;
                       std::string file(source.substr(source.find_last_of("\\/") + 1));
                       std::ranges::transform(file, file.begin(),
                                              [](char c) { return static_cast<char>(std::tolower(c)); });

                       if (file == cinemaPicture)
                       {
                           cinemaColors.emplace();
                           std::ranges::copy(pictureColors, cinemaColors->begin());
                       }

                       if (file == tabsPicture)
                       {
                           tabsColors.emplace();
                           std::ranges::copy(pictureColors, tabsColors->begin());
                       }
                   });

    // The main menu's UPDATE button.
    REQUIRE(cinemaColors.has_value());
    const MCMenuButtonPictures update = UpdateButtonPictures(GlobalLog()->MainScreen->Palette);

    for (const auto& [state, picture] :
         {std::pair{'h', update.Up}, std::pair{'g', update.Down}, std::pair{'n', update.Gray}})
    {
        WritePicture(folder, manifest, std::format("updatebutton\\mm_xb{}_update.tga", state), picture->Pixels(),
                     picture->Width(), picture->Height(), *cinemaColors, written);
        ++pictures;
    }

    // The session screen's MATCH OPTIONS tabs (logistics/MCMatchOptionsTab.h), named as the tabs they're made from.
    REQUIRE(tabsColors.has_value());

    for (const auto& [front, name] : {std::pair{MCSessionTab::Chat, "matchtabs\\lsbdw02_match.tga"},
                                      std::pair{MCSessionTab::Options, "matchtabs\\lsbdw03_match.tga"}})
    {
        const std::shared_ptr<MCLogPort> tabs = MakeMatchTabs(front);
        REQUIRE(tabs != nullptr);
        WritePicture(folder, manifest, name, tabs->Pixels(), tabs->Width(), tabs->Height(), *tabsColors, written);
        ++pictures;
    }

#if MC_AUDIT_CONTROLS
    ShowPreferences();
    MCTestGame::RunFrame(FrameSeconds);
    MCInput::Display()->GetShownColors(colors);
    ForEachTextButton(*GlobalLog()->PrefScreen,
                      [&](const MCLogTextButton& button)
                      {
                          std::string word = button.Label();
                          std::ranges::transform(word, word.begin(),
                                                 [](char c) { return static_cast<char>(std::tolower(c)); });

                          for (const auto& [state, picture] :
                               {std::pair{'h', button.GetUpPicture()}, std::pair{'g', button.GetDownPicture()},
                                std::pair{'n', button.GetGrayPicture()}})
                          {
                              WritePicture(folder, manifest, std::format("textbutton\\b{}_text_{}.tga", state, word),
                                           picture->Pixels(), picture->Width(), picture->Height(), colors, written);
                              ++pictures;
                          }
                      });
    CancelPrefs();
#endif

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

    /// <summary><paramref name="mover"/> ordered (by the player, unless <paramref name="origin"/> says otherwise) to
    /// attack <paramref name="target"/> from <paramref name="range"/> (a Mover::getFireRange selector: -1 its optimal
    /// range, 0 the short band's).</summary>
    void OrderAttack(MCMover& mover, const std::shared_ptr<MCMover>& target,
                     MCOrderOrigin origin = MCOrderOrigin::Player, int32_t range = -1)
    {
        MCTacticalOrder order;
        order.Reset(origin, MCTacticalOrderCode::AttackObject, 0);
        order.Target = target;
        order.AttackParams.Type = 1;
        order.AttackParams.Method = 0;
        order.AttackParams.Range = range;
        order.AttackParams.Pursue = -1;
        mover.HandleTacticalOrder(order, 1, 0);
    }

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

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons())
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

            OrderAttack(*mover, target);
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

        if (const std::optional<std::filesystem::path> folder = MCHdArt::AttachedFolder(data); folder.has_value())
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
            AuditTally& entry = tally[AuditKeyOf(draw, AuditSharp(draw))];
            const bool table = VfxIsShapeTable(draw.Data);

            if (entry.Count == 0)
            {
                entry.WindowWidth = draw.Window.get().XMax + 1;
                entry.WindowHeight = draw.Window.get().YMax + 1;
                entry.Owner = AuditOwner(draw.Data);
                entry.Label = AuditLabel;
                MCInput::Display()->GetShownColors(entry.Colors);

                if (draw.From && draw.From->get().Pixels != nullptr && draw.From->get().Size() > 0)
                {
                    const MCWindow& from = *draw.From;
                    DecodedFrame& picture = entry.Picture.emplace();
                    picture.Width = from.XMax + 1;
                    picture.Height = from.YMax + 1;
                    picture.Index.assign(from.Pixels.get(), from.Pixels.get() + from.Size());
                    picture.Drawn.assign(from.Size(), 1);
                }

                if (table)
                {
                    entry.Attached = MCHdArt::AttachedFolder(draw.Data);
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
                                     std::format("{:#x}", data - MCAddressKey()), entry.WindowWidth, entry.WindowHeight,
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
            AuditDetail::Entry& entry = detail.Entries[AuditKeyOf(draw, sharp)];

            if (entry.Screens.empty())
            {
                entry.Stack = AuditStack();
                entry.Font = draw.Command == "Glyph" ? AuditFontName(draw.Data) : std::string();
                MCInput::Display()->GetShownColors(entry.Colors);
            }

            entry.Screens.insert(detail.Screen);

            if (!sharp && draw.From && draw.From->get().Pixels != nullptr && draw.From->get().Size() > 0)
            {
                const MCWindow& from = *draw.From;
                DecodedFrame& latest = entry.Latest.emplace();
                latest.Width = from.XMax + 1;
                latest.Height = from.YMax + 1;
                latest.Index.assign(from.Pixels.get(), from.Pixels.get() + from.Size());
                MCInput::Display()->GetShownColors(entry.LatestColors);
            }

            if (!sharp)
            {
                AuditDetail::Site& site = detail.Sites[{std::string(draw.Command), draw.Window.get().XMax + 1,
                                                        draw.Window.get().YMax + 1, AuditStack()}];
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

        std::vector<std::pair<std::reference_wrapper<const AuditKey>, std::reference_wrapper<const AuditTally>>>
            original;

        for (const auto& [key, entry] : tally)
        {
            if (!std::get<1>(key))
            {
                original.emplace_back(key, entry);
            }
        }

        std::ranges::sort(original, std::ranges::greater{}, [](const auto& row) { return row.second.get().Pixels; });
        // The characters drawn from the original's pixels in each font, pictured once the rows are written.
        std::map<std::string, std::set<int32_t>> fontCharacters;
        std::ofstream table(folder / "audit.tsv", std::ios::binary);
        table << std::format("# frames {} load {:x}\n", frames, AuditImageBase());
        // The source column: a copy's picture is the extractor's (source) or made at run time (made); a shape's
        // frames that show nothing (blank).
        table << "id\tcommand\twindow\tscaled\tdraws\tpixels\tper_draw\tper_frame\tframes\towner\tfont\tpack\tsource\t"
                 "screens\tstack\n";

        for (const auto& [keyKept, entryKept] : original)
        {
            const AuditKey& key = keyKept;
            const AuditTally& entry = entryKept;
            const std::string& command = std::get<0>(key);
            const auto found = detail.Entries.find(key);
            const MCOptionalRef<const AuditDetail::Entry> extra =
                found != detail.Entries.end() ? MCOptionalRef<const AuditDetail::Entry>(found->second) : std::nullopt;
            std::string id;
            std::string pack = "-";
            std::string source = "-";

            if (entry.Picture.has_value())
            {
                const DecodedFrame& picture = *entry.Picture;
                id = std::format("picture-{:016x}", AuditPictureHash(picture.Index, picture.Width, picture.Height));
                pack = MCHdArt::FindPicture(picture.Index, picture.Width, picture.Height) != nullptr ? "has" : "lacks";
                source = isSource(picture);

                if (const std::filesystem::path path = items / (id + ".png"); !std::filesystem::exists(path))
                {
                    if (extra.has_value() && extra->get().Latest.has_value())
                    {
                        const DecodedFrame& latest = *extra->get().Latest;
                        SaveAuditPicture(path, latest.Width, latest.Height, latest.Index, {},
                                         extra->get().LatestColors);
                    }
                    else
                    {
                        SaveAuditPicture(path, picture.Width, picture.Height, picture.Index, {},
                                         extra.has_value() ? std::span<const SDL_Color, 256>(extra->get().Colors)
                                                           : shown);
                    }
                }
            }
            else if (entry.TableHash.has_value())
            {
                id = std::format("shape-{:016x}", *entry.TableHash);
                int32_t has = 0;

                for (const auto& [frame, decoded] : entry.ShapeFrames)
                {
                    const std::shared_ptr<const MCHdArt::Frame> hd =
                        entry.Attached.has_value()
                            ? MCHdArt::FindInFolder(*entry.Attached, frame, decoded.Width, decoded.Height)
                            : MCHdArt::FindShape(*entry.TableHash, frame, decoded.Width, decoded.Height);
                    has += hd != nullptr ? 1 : 0;
                }

                const auto kept = static_cast<int32_t>(entry.ShapeFrames.size());
                pack = kept == 0     ? "?"
                       : has == kept ? "has"
                       : has == 0    ? "lacks"
                                     : std::format("part {}/{}", has, kept);

                // Frames that show nothing (the audit counts their draws as sharp, so none should be listed here).
                const auto blank = std::ranges::count_if(entry.ShapeFrames, [](const auto& frameKept)
                                                         { return BlankFrame(frameKept.second); });
                source = std::format("blank {}/{}", blank, kept);

                if (const std::filesystem::path path = items / (id + ".png");
                    !entry.ShapeFrames.empty() && !std::filesystem::exists(path))
                {
                    const DecodedFrame& first = entry.ShapeFrames.begin()->second;
                    SaveAuditPicture(path, first.Width, first.Height, first.Index, first.Drawn,
                                     extra.has_value() ? std::span<const SDL_Color, 256>(extra->get().Colors) : shown);
                }
            }
            else if (extra.has_value() && !extra->get().Font.empty())
            {
                id = std::format("font-{}", extra->get().Font);
                pack = "font";
                fontCharacters[extra->get().Font].insert(entry.Frames.begin(), entry.Frames.end());
            }
            else
            {
                id = std::format("{}-{}x{}", command, entry.WindowWidth, entry.WindowHeight);
            }

            table << std::format("{}\t{}\t{}x{}\t{}\t{}\t{}\t{}\t{:.1f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", id, command,
                                 entry.WindowWidth, entry.WindowHeight, entry.Scaled ? "scaled" : "1:1", entry.Count,
                                 entry.Pixels, entry.Count > 0 ? entry.Pixels / entry.Count : 0,
                                 frames > 0 ? static_cast<double>(entry.Pixels) / frames : 0.0,
                                 AuditFramesText(key, entry), entry.Owner.empty() ? "-" : entry.Owner,
                                 extra.has_value() && !extra->get().Font.empty() ? extra->get().Font : "-", pack,
                                 source, extra.has_value() ? AuditJoined(extra->get().Screens) : "-",
                                 extra.has_value() ? AuditStackText(extra->get().Stack) : "-");
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

        // The deploy pane's first unit, a mech.
        if (briefing.DeployUnits.empty() || briefing.DeployUnits.front()->PartType != 1)
        {
            std::cout << "  the briefing has no mech to deploy\n";
            return false;
        }

        const auto mech = std::static_pointer_cast<MCLogMech>(briefing.DeployUnits.front());
        MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(mech->CurTonnage);
        const RECT& slot = briefing.SlotRects[0];
        MCMechBriefBlock& block = *briefing.BlockOf(*mech);
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

namespace
{
    /// <summary>A showcase option's whole number (<c>--showcase-&lt;name&gt; &lt;n&gt;</c>), else <paramref name="fallback"/>.</summary>
    int32_t ShowcaseOption(std::string_view name, int32_t fallback)
    {
        int32_t value = fallback;

        if (const std::optional<std::string_view> given = MCTest::Option(std::format("showcase-{}", name));
            given.has_value())
        {
            std::from_chars(given->data(), given->data() + given->size(), value);
        }

        return value;
    }

    /// <summary>A showcase option's frames (<c>--showcase-&lt;name&gt; &lt;n&gt;,&lt;n&gt;...</c>), else none.</summary>
    std::vector<int32_t> ShowcaseFrames(std::string_view name)
    {
        std::vector<int32_t> frames;

        if (const std::optional<std::string_view> given = MCTest::Option(std::format("showcase-{}", name));
            given.has_value())
        {
            for (const auto part : std::views::split(*given, ','))
            {
                int32_t frame = 0;
                std::from_chars(part.data(), part.data() + part.size(), frame);
                frames.push_back(frame);
            }
        }

        return frames;
    }

    /// <summary>A showcase's staged moment: from a frame, one of the player's mechs (by place) and what befalls it.</summary>
    struct MCShowcaseEvent
    {
        int32_t Frame = 0;
        /// <summary>The mech's place in the player's force (0 the first), or -1 for none named.</summary>
        int32_t Mech = -1;
        std::string What;
    };

    /// <summary>
    /// A showcase option's moments (<c>--showcase-&lt;name&gt; frame:mech[:what],...</c>; a bare frame names no
    /// mech), else none.
    /// </summary>
    std::vector<MCShowcaseEvent> ShowcaseEvents(std::string_view name)
    {
        std::vector<MCShowcaseEvent> events;

        if (const std::optional<std::string_view> given = MCTest::Option(std::format("showcase-{}", name));
            given.has_value())
        {
            for (const auto item : std::views::split(*given, ','))
            {
                MCShowcaseEvent event;
                size_t field = 0;

                for (const auto part : std::views::split(std::string_view(item.begin(), item.end()), ':'))
                {
                    const std::string_view text(part.begin(), part.end());

                    if (field == 0)
                    {
                        std::from_chars(text.data(), text.data() + text.size(), event.Frame);
                    }
                    else if (field == 1)
                    {
                        std::from_chars(text.data(), text.data() + text.size(), event.Mech);
                    }
                    else
                    {
                        event.What = text;
                    }

                    ++field;
                }

                events.push_back(std::move(event));
            }
        }

        return events;
    }

    /// <summary>The most mechs a player fields (part ids from <see cref="PlayerMechPartId"/>).</summary>
    constexpr int32_t PlayerMechs = 12;

    /// <summary>The player's mech in place <paramref name="place"/> of the force (fallen or not), or none.</summary>
    std::shared_ptr<MCMover> ForceMech(int32_t place)
    {
        return place >= 0 && place < PlayerMechs ? GetMoverFromPartId(PlayerMechPartId + place) : nullptr;
    }

    /// <summary>The player's mechs still fighting, in part order.</summary>
    std::vector<std::shared_ptr<MCMover>> LiveLance()
    {
        std::vector<std::shared_ptr<MCMover>> lance;

        for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + PlayerMechs; ++partId)
        {
            if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->IsDisabled() == 0)
            {
                lance.push_back(std::move(mover));
            }
        }

        return lance;
    }

    /// <summary><paramref name="mover"/>'s armor gone and its internal structure cut to <paramref name="share"/> of
    /// full, so the next hits take it apart.</summary>
    void Weaken(MCMover& mover, float share)
    {
        for (MCArmorLocation& armor : mover.Armor)
        {
            armor.CurArmor = 0.0f;
        }

        for (MCBodyLocation& body : mover.Body)
        {
            body.CurInternalStructure = std::min(body.CurInternalStructure, body.MaxInternalStructure * share);
        }
    }

    /// <summary>Whether <paramref name="mover"/>'s name holds one of <paramref name="names"/> (comma-separated parts of
    /// names).</summary>
    bool Named(MCMover& mover, std::string_view names)
    {
        const std::string_view name = mover.GetIfaceName();
        bool named = false;

        for (const auto part : std::views::split(names, ','))
        {
            named = named || name.find(std::string_view(part.begin(), part.end())) != std::string_view::npos;
        }

        return named;
    }

    /// <summary>The live enemy mechs (or, with <paramref name="anyKind"/>, units), nearest <paramref name="from"/>
    /// first; with <paramref name="names"/>, those so named (<see cref="Named"/>).</summary>
    std::vector<std::shared_ptr<MCMover>> EnemyMechsByDistance(MCMover& from, bool anyKind = false,
                                                               std::string_view names = {})
    {
        const std::shared_ptr<MCTeam> team = from.Team.lock();
        const MCVector3D at = from.GetPosition();
        std::vector<std::pair<float, std::shared_ptr<MCMover>>> found;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->Team.lock() == team || mover->IsDisabled() != 0 ||
                (!anyKind && mover->ObjectClass != MCObjectClass::BattleMech) ||
                (!names.empty() && !Named(*mover, names)))
            {
                continue;
            }

            const MCVector3D to = mover->GetPosition();
            found.emplace_back((to.X - at.X) * (to.X - at.X) + (to.Y - at.Y) * (to.Y - at.Y), mover);
        }

        std::ranges::stable_sort(found, {}, &std::pair<float, std::shared_ptr<MCMover>>::first);
        std::vector<std::shared_ptr<MCMover>> nearestFirst;

        for (auto& [distance, mover] : found)
        {
            nearestFirst.push_back(std::move(mover));
        }

        return nearestFirst;
    }

    /// <summary>The live enemy mech (or unit) nearest <paramref name="from"/>, or none (see
    /// <see cref="EnemyMechsByDistance"/>).</summary>
    std::shared_ptr<MCMover> NearestEnemyMech(MCMover& from, bool anyKind = false, std::string_view names = {})
    {
        const std::vector<std::shared_ptr<MCMover>> nearestFirst = EnemyMechsByDistance(from, anyKind, names);
        return nearestFirst.empty() ? nullptr : nearestFirst.front();
    }

    /// <summary>Whether <paramref name="mover"/> is a mech in the air.</summary>
    bool Jumping(const MCMover& mover)
    {
        return mover.ObjectClass == MCObjectClass::BattleMech && static_cast<const MCBattleMech&>(mover).InJump;
    }

    /// <summary>
    /// <paramref name="mover"/> ordered to jump at <paramref name="target"/> when it can and the target is not too near:
    /// landing <see cref="JumpLanding"/> of the way there, or as far as its jets take it. Returns whether it jumps.
    /// </summary>
    bool JumpAt(MCMover& mover, MCMover& target)
    {
        // A jump lands three quarters of the way to the target; one shorter than a third of the range isn't worth it.
        constexpr double JumpLanding = 0.75;
        constexpr float ShortestJump = 1.0f / 3.0f;
        const float range = mover.GetJumpRange();

        if (mover.CanJump() == 0 || Jumping(mover) || range <= 0.0f)
        {
            return false;
        }

        const MCVector3D from = mover.GetPosition();
        const MCVector3D to = target.GetPosition();
        const double dx = static_cast<double>(to.X) - from.X;
        const double dy = static_cast<double>(to.Y) - from.Y;
        const double distance = std::sqrt(dx * dx + dy * dy);

        if (distance < range * ShortestJump)
        {
            return false;
        }

        const double share = std::min(JumpLanding, range / distance);
        const MCWayPathPoint point{
            MCVector3D(static_cast<float>(from.X + dx * share), static_cast<float>(from.Y + dy * share), to.Z), false};
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::JumpToPoint, 0);
        order.InitWayPath(std::span(&point, 1));
        order.MoveParams.Wait = 0;
        mover.HandleTacticalOrder(order, 1, 0);
        return true;
    }

    /// <summary>How far apart the two sides' lines stand (meters): inside most weapons' reach, in one frame.</summary>
    constexpr float LineMeters = 45.0f;
    /// <summary>How near those closing in on a mech come (meters): the noose tightened, not a pile.</summary>
    constexpr float NooseMeters = 30.0f;
    /// <summary>How far behind a mech a second line is set down to walk in on it (meters), unless a close says.</summary>
    constexpr float BehindMeters = 110.0f;

    /// <summary><paramref name="mover"/> ordered by <paramref name="origin"/> to walk (or <paramref name="run"/>) to
    /// <paramref name="point"/>.</summary>
    void WalkTo(MCMover& mover, const MCVector3D& point, MCOrderOrigin origin = MCOrderOrigin::Player, bool run = false)
    {
        const MCWayPathPoint way{point, run};
        MCTacticalOrder order;
        order.Reset(origin, MCTacticalOrderCode::MoveToPoint, 0);
        order.InitWayPath(std::span(&way, 1));
        order.MoveParams.Wait = 0;
        mover.HandleTacticalOrder(order, 1, 0);
    }

    /// <summary>
    /// <paramref name="mover"/> walked to <paramref name="meters"/> from <paramref name="target"/> along the line between
    /// them (where it is, if nearer: backing off would turn it away), its pilot's target set to it, so it fires as it goes
    /// and once there (a pilot fires on his last target under a move order while it is within his attack radius). An
    /// attack order keeps only coarse distances (its range bands, 60, 140 and 200 m, or the pilot's optimal range, which
    /// for a mech that outranges its target is its longest weapon's), so the two sides would stand far off or pile up.
    /// </summary>
    void Engage(MCMover& mover, const std::shared_ptr<MCMover>& target, float meters, MCOrderOrigin origin)
    {
        const MCVector3D from = target->GetPosition();
        const MCVector3D at = mover.GetPosition();
        const float length = std::max(std::hypot(at.X - from.X, at.Y - from.Y), 1.0f);
        const float away = std::min(meters * WorldUnitsPerMeter, length);
        WalkTo(mover,
               MCVector3D(from.X + (at.X - from.X) / length * away, from.Y + (at.Y - from.Y) / length * away, at.Z),
               origin);

        if (const std::shared_ptr<MCMechWarrior> pilot = mover.GetPilot(); pilot != nullptr)
        {
            pilot->SetLastTarget(target);
        }
    }

    /// <summary>
    /// A target of <paramref name="targets"/> for each of <paramref name="attackers"/> (in their order; none if there
    /// are no targets): the nearest, each attacker already given one counting SpreadPenalty further (world units), the
    /// attackers nearest a target choosing first, so fire spreads along the other side's line.
    /// </summary>
    std::vector<std::shared_ptr<MCMover>> SpreadTargets(const std::vector<std::shared_ptr<MCMover>>& attackers,
                                                        const std::vector<std::shared_ptr<MCMover>>& targets)
    {
        constexpr float SpreadPenalty = 150.0f;
        std::vector<std::shared_ptr<MCMover>> chosen(attackers.size());

        if (targets.empty())
        {
            return chosen;
        }

        const auto Distance = [](MCMover& one, MCMover& other)
        {
            const MCVector3D a = one.GetPosition();
            const MCVector3D b = other.GetPosition();
            return std::hypot(a.X - b.X, a.Y - b.Y);
        };

        std::vector<std::pair<float, size_t>> order;

        for (size_t i = 0; i < attackers.size(); ++i)
        {
            float nearest = std::numeric_limits<float>::max();

            for (const std::shared_ptr<MCMover>& target : targets)
            {
                nearest = std::min(nearest, Distance(*attackers[i], *target));
            }

            order.emplace_back(nearest, i);
        }

        std::ranges::stable_sort(order, {}, &std::pair<float, size_t>::first);
        std::vector<int32_t> load(targets.size(), 0);

        for (const auto& [nearest, i] : order)
        {
            size_t best = 0;
            float bestCost = std::numeric_limits<float>::max();

            for (size_t t = 0; t < targets.size(); ++t)
            {
                if (const float cost =
                        Distance(*attackers[i], *targets[t]) + SpreadPenalty * static_cast<float>(load[t]);
                    cost < bestCost)
                {
                    best = t;
                    bestCost = cost;
                }
            }

            ++load[best];
            chosen[i] = targets[best];
        }

        return chosen;
    }

    /// <summary>
    /// The other sides' units whose names hold one of <paramref name="names"/> (comma-separated) ordered by their
    /// commander at a mech of <paramref name="lance"/> to <see cref="LineMeters"/> from it (<see cref="Engage"/>), spread
    /// along it (<see cref="SpreadTargets"/>), but those of <paramref name="busy"/> (part ids). Returns how many.
    /// </summary>
    int32_t Converge(std::string_view names, const std::vector<std::shared_ptr<MCMover>>& lance,
                     const std::set<int32_t>& busy = {})
    {
        if (lance.empty())
        {
            return 0;
        }

        const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
        std::vector<std::shared_ptr<MCMover>> movers;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->Team.lock() == team || mover->IsDisabled() != 0 || !Named(*mover, names) ||
                busy.contains(partId))
            {
                continue;
            }

            movers.push_back(mover);
        }

        const std::vector<std::shared_ptr<MCMover>> targets = SpreadTargets(movers, lance);

        for (size_t i = 0; i < movers.size(); ++i)
        {
            Engage(*movers[i], targets[i], LineMeters, MCOrderOrigin::Commander);
        }

        return static_cast<int32_t>(movers.size());
    }

    /// <summary>
    /// <paramref name="mover"/> ordered to run <paramref name="distance"/> away from the live enemy mech nearest it.
    /// Returns whether there was one to run from.
    /// </summary>
    bool RetreatFrom(MCMover& mover, float distance)
    {
        const std::shared_ptr<MCMover> enemy = NearestEnemyMech(mover);

        if (enemy == nullptr)
        {
            return false;
        }

        const MCVector3D at = mover.GetPosition();
        const MCVector3D from = enemy->GetPosition();
        const float dx = at.X - from.X;
        const float dy = at.Y - from.Y;
        const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
        const MCWayPathPoint point{MCVector3D(at.X + dx / length * distance, at.Y + dy / length * distance, at.Z),
                                   true};
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 0);
        order.InitWayPath(std::span(&point, 1));
        order.MoveParams.Wait = 0;
        mover.HandleTacticalOrder(order, 1, 0);
        return true;
    }

    /// <summary>
    /// Whether <paramref name="point"/> shows in the main camera's view, or within a sprite's reach of its edge (a mech
    /// standing just outside it still shows its top), as MCBigGameObject::OnScreen projects.
    /// </summary>
    bool PointOnScreen(const MCVector3D& point)
    {
        constexpr float SpriteReach = 120.0f;
        const std::shared_ptr<MCCamera> camera = CameraList()->FindCameraFromIDNumber(1);

        if (camera == nullptr || camera->Active == 0)
        {
            return false;
        }

        const double dx = point.X - camera->Position.X;
        const double dy = point.Y - camera->Position.Y;
        const double dz = point.Z - camera->Position.Z;
        const double x = dy * camera->CosAngle + dx * camera->CosAngle + camera->HalfWidth;
        const double y = dx * camera->SinAngle + camera->HalfHeight - dy * camera->SinAngle - dz;
        return x >= -SpriteReach && y >= -SpriteReach && x <= camera->ViewWidth + SpriteReach &&
               y <= camera->ViewHeight + SpriteReach;
    }

    /// <summary>Whether a mech could stand at <paramref name="spot"/>: open ground, within LevelMeters of the elevation
    /// <paramref name="level"/> (a terrace's, not the slope below it).</summary>
    bool OpenAndLevel(const MCVector3D& spot, float level)
    {
        constexpr float LevelMeters = 5.0f;
        return GameMap().CellPassable(spot) &&
               std::abs(GameMap().GetTerrainElevation(spot) - level) <= LevelMeters * WorldUnitsPerMeter;
    }

    /// <summary>
    /// The spot nearest <paramref name="point"/> (itself first, then rings RingMeters apart out to
    /// <paramref name="meters"/>) that is open ground level with <paramref name="level"/>; the point itself if none.
    /// </summary>
    MCVector3D NearestOpenAndLevel(const MCVector3D& point, float level, float meters)
    {
        constexpr float RingMeters = 5.0f;
        constexpr int32_t RingSpots = 24;

        if (OpenAndLevel(point, level))
        {
            return point;
        }

        for (float ring = RingMeters; ring <= meters; ring += RingMeters)
        {
            for (int32_t i = 0; i < RingSpots; ++i)
            {
                const float turn = static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / RingSpots;
                const MCVector3D spot(point.X + std::cos(turn) * ring * WorldUnitsPerMeter,
                                      point.Y + std::sin(turn) * ring * WorldUnitsPerMeter, point.Z);

                if (OpenAndLevel(spot, level))
                {
                    return spot;
                }
            }
        }

        return point;
    }

    /// <summary>
    /// Up to <paramref name="count"/> spots <paramref name="meters"/> behind <paramref name="at"/> (along the unit
    /// vector (<paramref name="backX"/>, <paramref name="backY"/>)), abreast AbreastMeters apart, each where a mech can
    /// stand and walk in from: open ground, level with <paramref name="at"/> (a terrace, not the slope below it), off
    /// screen. Where an abreast spot is not, the nearest that is of those fanned out behind (FanDegrees to either side,
    /// up to FanDepthMeters further back), AbreastMeters from those already chosen. In abreast order. On a camera
    /// <paramref name="cut"/> what the camera showed doesn't matter: that view ends.
    /// </summary>
    std::vector<MCVector3D> SpotsBehind(const MCVector3D& at, float backX, float backY, float meters, int32_t count,
                                        bool cut)
    {
        constexpr float AbreastMeters = 20.0f;
        constexpr float FanDegrees = 75.0f;
        constexpr float FanStepDegrees = 5.0f;
        constexpr float FanDepthMeters = 60.0f;
        constexpr float FanStepMeters = 10.0f;
        const float level = GameMap().GetTerrainElevation(at);
        const float apart = AbreastMeters * WorldUnitsPerMeter;
        const auto open = [&](const MCVector3D& spot)
        { return OpenAndLevel(spot, level) && (cut || !PointOnScreen(spot)); };

        std::vector<MCVector3D> fan;

        for (float depth = meters; depth <= meters + FanDepthMeters; depth += FanStepMeters)
        {
            for (float degrees = -FanDegrees; degrees <= FanDegrees; degrees += FanStepDegrees)
            {
                const float turn = degrees * std::numbers::pi_v<float> / 180.0f;
                const float x = backX * std::cos(turn) - backY * std::sin(turn);
                const float y = backX * std::sin(turn) + backY * std::cos(turn);
                const MCVector3D spot(at.X + x * depth * WorldUnitsPerMeter, at.Y + y * depth * WorldUnitsPerMeter,
                                      at.Z);

                if (open(spot))
                {
                    fan.push_back(spot);
                }
            }
        }

        std::vector<MCVector3D> spots;

        for (int32_t place = 0; place < count; ++place)
        {
            const float side = (static_cast<float>(place) - static_cast<float>(count - 1) * 0.5f) * apart;
            const float back = meters * WorldUnitsPerMeter;
            MCVector3D ideal(at.X + backX * back - backY * side, at.Y + backY * back + backX * side, at.Z);
            const auto clear = [&](const MCVector3D& spot)
            {
                return std::ranges::all_of(spots, [&](const MCVector3D& other)
                                           { return std::hypot(other.X - spot.X, other.Y - spot.Y) >= apart; });
            };

            if (open(ideal) && clear(ideal))
            {
                spots.push_back(ideal);
                continue;
            }

            MCOptionalRef<const MCVector3D> nearest;

            for (const MCVector3D& spot : fan)
            {
                if (clear(spot) && (!nearest || std::hypot(spot.X - ideal.X, spot.Y - ideal.Y) <
                                                    std::hypot(nearest->get().X - ideal.X, nearest->get().Y - ideal.Y)))
                {
                    nearest = spot;
                }
            }

            if (nearest)
            {
                spots.push_back(nearest->get());
            }
        }

        return spots;
    }

    /// <summary>
    /// The other sides' units named in <paramref name="names"/> (comma-separated parts of names) closing in on
    /// <paramref name="mover"/>, each sent to <see cref="NooseMeters"/> from it (Engage: the noose tightens); the <paramref name="bring"/>
    /// of them furthest from it first set down <paramref name="behind"/> meters behind it (on the side away from the
    /// rest of them, abreast, SpotsBehind), each where neither it nor its spot is on screen (on a camera
    /// <paramref name="cut"/>, anywhere: the view they were in ends, and the next finds them there), facing it, and
    /// only those that can walk: a second line that walks in on it from behind, the force caught between, added to
    /// <paramref name="brought"/>. Those of
    /// <paramref name="busy"/> (part ids) are left be. Returns how many close in.
    /// </summary>
    int32_t CloseIn(std::string_view names, const std::shared_ptr<MCMover>& mover, int32_t bring = 0,
                    const std::set<int32_t>& busy = {}, float behind = BehindMeters,
                    MCOptionalRef<std::vector<std::shared_ptr<MCMover>>> brought = std::nullopt, bool cut = false)
    {
        // The second line walks in on a player's orders: under fire a pilot keeps to his alarm's over his
        // commander's, and would stand where he was set down.
        constexpr MCOrderOrigin SecondOrders = MCOrderOrigin::Player;
        const auto Second = [&](const std::shared_ptr<MCMover>& enemy)
        { return brought && std::ranges::find(brought->get(), enemy) != brought->get().end(); };
        const std::shared_ptr<MCTeam> team = mover->Team.lock();
        const MCVector3D at = mover->GetPosition();
        std::vector<std::pair<float, std::shared_ptr<MCMover>>> named;
        double sumX = 0.0;
        double sumY = 0.0;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

            if (enemy == nullptr || enemy->Team.lock() == team || enemy->IsDisabled() != 0 || !Named(*enemy, names) ||
                busy.contains(partId))
            {
                continue;
            }

            const MCVector3D from = enemy->GetPosition();
            named.emplace_back(std::hypot(from.X - at.X, from.Y - at.Y), enemy);
            sumX += from.X;
            sumY += from.Y;
            Engage(*enemy, mover, NooseMeters, Second(enemy) ? SecondOrders : MCOrderOrigin::Commander);
        }

        if (named.empty() || bring <= 0)
        {
            return static_cast<int32_t>(named.size());
        }

        // Behind: away from where the rest stand.
        const auto count = static_cast<double>(named.size());
        const auto awayX = static_cast<float>(at.X - sumX / count);
        const auto awayY = static_cast<float>(at.Y - sumY / count);
        const float length = std::max(std::hypot(awayX, awayY), 1.0f);
        const float backX = awayX / length;
        const float backY = awayY / length;
        std::ranges::stable_sort(named, std::ranges::greater{}, &std::pair<float, std::shared_ptr<MCMover>>::first);
        std::vector<MCVector3D> spots = SpotsBehind(at, backX, backY, behind, bring, cut);
        size_t placed = 0;

        for (const auto& [distance, enemy] : named)
        {
            if (placed == spots.size())
            {
                break;
            }

            if (!cut && PointOnScreen(enemy->GetPosition()))
            {
                continue;
            }

            // A part the mission's script hasn't brought into play yet (its Exists 0), asleep or powered down stands
            // where it is set down, unseen or still.
            constexpr int32_t PoweredUp = 0;

            if (enemy->GetExistsAndAwake() == 0 || enemy->Status != PoweredUp)
            {
                std::cout << std::format("  {} (part {:#x}) left be: it can't walk (exists {}, awake {}, status {})\n",
                                         enemy->GetIfaceName(), enemy->PartId, enemy->GetExists(), enemy->GetAwake(),
                                         enemy->Status);
                continue;
            }

            MCVector3D& place = spots[placed++];
            enemy->SetPosition(place);

            // Facing it, torso and arms with the legs: a line walking in on it, not one with its back to it.
            if (enemy->ObjectClass == MCObjectClass::BattleMech)
            {
                auto& mech = static_cast<MCBattleMech&>(*enemy);
                FaceMechTo(mech, at);
                mech.TorsoRotation = 0.0f;
                mech.LeftArmRotation = 0.0f;
                mech.RightArmRotation = 0.0f;
            }

            Engage(*enemy, mover, NooseMeters, SecondOrders);

            if (brought)
            {
                brought->get().push_back(enemy);
            }

            std::cout << std::format(
                "  {} (part {:#x}) set down {:.0f} m behind {} at {:.0f} {:.0f} ({:+.1f} m up)\n",
                enemy->GetIfaceName(), enemy->PartId, std::hypot(place.X - at.X, place.Y - at.Y) / WorldUnitsPerMeter,
                mover->GetIfaceName(), place.X, place.Y,
                (GameMap().GetTerrainElevation(place) - GameMap().GetTerrainElevation(at)) / WorldUnitsPerMeter);
        }

        return static_cast<int32_t>(named.size());
    }

    /// <summary><paramref name="mover"/>'s armor and internal structure left, every location's together: less after a
    /// frame than before it, it was hit.</summary>
    float Toughness(const MCMover& mover)
    {
        float left = 0.0f;

        for (const MCArmorLocation& armor : mover.Armor)
        {
            left += armor.CurArmor;
        }

        for (const MCBodyLocation& body : mover.Body)
        {
            left += body.CurInternalStructure;
        }

        return left;
    }

    /// <summary>Whether either is on screen (or near enough its edge to show), where a unit set down would be seen to
    /// jump.</summary>
    bool EitherOnScreen(MCMover& one, MCMover& other)
    {
        return PointOnScreen(one.GetPosition()) || PointOnScreen(other.GetPosition());
    }

    /// <summary>When units' weapons were last readied out of turn (mission seconds, by part id).</summary>
    using MCReadiedAt = std::map<int32_t, float>;

    /// <summary>
    /// A weapon readied out of turn is one this share of the way through its recycle since it last fired: so none ever
    /// fires again much sooner than it would.
    /// </summary>
    constexpr float RecycledShare = 0.65f;

    /// <summary>Whether the weapon has done RecycledShare of its recycle since it last fired (or never has).</summary>
    bool Recycled(const MCInventoryItem& item)
    {
        return item.ReadyTime - ScenarioTime <= MasterComponentList[item.MasterID].RecycleTime * (1.0f - RecycledShare);
    }

    /// <summary>
    /// Whether the player sees <paramref name="unit"/>: one of the home team's, or in its sight. The game draws another
    /// side's unit only then (on the sensors alone, or unseen, it isn't drawn).
    /// </summary>
    bool PlayerSees(MCMover& unit)
    {
        const std::shared_ptr<MCTeam> home = HomeTeam();
        return home == nullptr || unit.Team.lock() == home ||
               static_cast<MCContactStatus>(unit.GetContactType(home->Id)) == MCContactStatus::Visual;
    }

    /// <summary>
    /// <paramref name="shooter"/>'s weapon <paramref name="weaponIndex"/> fired at <paramref name="target"/> as the AI
    /// fires it (FireWeapon's answer: 0 fired), but staged to land: the dice are rolled, each dropped, until the next
    /// d100 (FireWeapon's first roll, its hit roll) comes up 0, so it hits at any chance; and nothing on the tiles under
    /// its line (units, trees, buildings: their cells' blocks-fire bits) blocks it. The dice are as they were if it
    /// didn't fire, the tiles as they were after. Not fired (-1) where the shooter or the target is a unit the player
    /// doesn't see, on screen.
    /// </summary>
    int32_t StagedShot(MCMover& shooter, const std::shared_ptr<MCMover>& target, int32_t weaponIndex)
    {
        // Never out of, or into, empty ground the camera shows: a unit the game doesn't draw (PlayerSees), on screen.
        constexpr int32_t NotStaged = -1;

        if (const auto hidden = [](MCMover& unit) { return !PlayerSees(unit) && PointOnScreen(unit.GetPosition()); };
            hidden(shooter) || hidden(*target))
        {
            return NotStaged;
        }

        constexpr int32_t HitRollRange = 100;
        const uint32_t unloaded = MCPort::RandState();
        uint32_t loaded = unloaded;

        while (RandomNumber(HitRollRange) != 0)
        {
            loaded = MCPort::RandState();
        }

        MCPort::SeedRand(loaded);
        // The tiles LineOfFire walks (a third of a cell at a time), each with its nine cells' bits (15 + 2c) set clear.
        constexpr int32_t CellsPerTile = 9;
        constexpr uint32_t ClearBit = 0x8000u;
        uint32_t clear = 0;

        for (int32_t cell = 0; cell < CellsPerTile; ++cell)
        {
            clear |= ClearBit << (cell * 2);
        }

        std::map<std::pair<int32_t, int32_t>, uint32_t> opened;
        const MCVector3D from = shooter.GetPosition();
        const MCVector3D to = target->GetPosition();
        const float length = std::hypot(to.X - from.X, to.Y - from.Y);
        constexpr float StepShare = 0.33f;
        const float step = MCTerrain::MetersPerVertexDivMapcellDim * StepShare;

        for (float along = 0.0f; along <= length + step; along += step)
        {
            const float share = length > 0.0f ? std::min(along / length, 1.0f) : 1.0f;
            int32_t tileR = 0;
            int32_t tileC = 0;
            int32_t cellR = 0;
            int32_t cellC = 0;
            MCScenarioMap::WorldToMapPos(
                MCVector3D(from.X + (to.X - from.X) * share, from.Y + (to.Y - from.Y) * share, 0.0f), tileR, tileC,
                cellR, cellC);

            if (GameMap().OnMap(tileR, tileC) && !opened.contains({tileR, tileC}))
            {
                MCMapTile& tile = GameMap().TileAt(tileR, tileC);
                opened[{tileR, tileC}] = tile.Cells;
                tile.Cells |= clear;
            }
        }

        const int32_t answer = shooter.FireWeapon(target, 0.0f, weaponIndex, 0, -1, std::nullopt);

        for (const auto& [at, cells] : opened)
        {
            MCMapTile& tile = GameMap().TileAt(at.first, at.second);
            tile.Cells = (tile.Cells & ~clear) | (cells & clear);
        }

        if (MCPort::RandState() == loaded)
        {
            MCPort::SeedRand(unloaded);
        }

        return answer;
    }

    /// <summary>
    /// Every live unit not on <paramref name="victim"/>'s side firing each weapon that is ready and reaches it. First the
    /// <paramref name="readyNearest"/> units nearest it that have not been readied for <paramref name="rest"/> seconds
    /// (<paramref name="readiedAt"/>) have the weapons that reach it and have nearly recycled readied (those of
    /// <paramref name="form"/> only, if given): out of turn, so only a few, never all at once. Those in
    /// <paramref name="skip"/> (part ids) don't fire. Returns how many fired (their names added to
    /// <paramref name="names"/>, if given).
    /// </summary>
    int32_t FireAt(const std::shared_ptr<MCMover>& victim, int32_t readyNearest, MCReadiedAt& readiedAt, float rest,
                   const std::set<int32_t>& skip = {}, std::optional<MCComponentForm> form = std::nullopt,
                   MCOptionalRef<std::vector<std::string>> names = std::nullopt)
    {
        const std::shared_ptr<MCTeam> team = victim->Team.lock();
        const MCVector3D at = victim->GetPosition();
        std::vector<std::pair<float, std::shared_ptr<MCMover>>> shooters;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> shooter = GetMoverFromPartId(partId);
                shooter != nullptr && shooter->Team.lock() != team && shooter->IsDisabled() == 0 &&
                !skip.contains(partId))
            {
                const MCVector3D from = shooter->GetPosition();
                shooters.emplace_back(std::hypot(at.X - from.X, at.Y - from.Y) / WorldUnitsPerMeter, shooter);
            }
        }

        std::ranges::sort(shooters, {}, &std::pair<float, std::shared_ptr<MCMover>>::first);
        int32_t readied = 0;
        int32_t fired = 0;

        for (const auto& [meters, shooter] : shooters)
        {
            const auto last = readiedAt.find(shooter->PartId);
            const bool rested = last == readiedAt.end() || ScenarioTime - last->second >= rest;
            bool ready = false;

            for (int32_t i = 0; i < shooter->NumWeapons && readied < readyNearest && rested; ++i)
            {
                MCInventoryItem& item = shooter->Inventory[shooter->NumOther + i];
                const MCMasterComponent& weapon = MasterComponentList[item.MasterID];

                if (meters >= weapon.WeaponRange[0] && meters <= weapon.WeaponRange[3] && Recycled(item) &&
                    (!form.has_value() || weapon.Form == *form))
                {
                    item.ReadyTime = 0.0f;
                    ready = true;
                }
            }

            if (ready)
            {
                readiedAt[shooter->PartId] = ScenarioTime;
                ++readied;
            }

            for (int32_t i = 0; i < shooter->NumWeapons; ++i)
            {
                if (StagedShot(*shooter, victim, shooter->NumOther + i) == 0)
                {
                    ++fired;

                    if (names)
                    {
                        names->get().push_back(
                            MasterComponentList[shooter->Inventory[shooter->NumOther + i].MasterID].Name);
                    }
                }
            }
        }

        return fired;
    }

    /// <summary>
    /// Between which distances every one of <paramref name="shooter"/>'s weapons reaches (meters): their longest
    /// minimum range and their shortest maximum (these can cross).
    /// </summary>
    std::pair<float, float> WeaponsBand(MCMover& shooter)
    {
        float shortest = 0.0f;
        float longest = std::numeric_limits<float>::max();

        for (int32_t i = 0; i < shooter.NumWeapons; ++i)
        {
            const MCMasterComponent& weapon = MasterComponentList[shooter.Inventory[shooter.NumOther + i].MasterID];
            shortest = std::max(shortest, weapon.WeaponRange[0]);
            longest = std::min(longest, weapon.WeaponRange[3]);
        }

        return {shortest, longest};
    }

    /// <summary>
    /// Where every one of <paramref name="shooter"/>'s weapons reaches (meters): halfway between their longest minimum
    /// range and their shortest maximum.
    /// </summary>
    float WeaponsReach(MCMover& shooter)
    {
        const auto [shortest, longest] = WeaponsBand(shooter);
        return shortest < longest ? (shortest + longest) * 0.5f : longest;
    }

    /// <summary>
    /// <paramref name="target"/> set down on its line from <paramref name="shooter"/> (or, with
    /// <paramref name="moveShooter"/>, the shooter on its line from the target) where every one of the shooter's weapons
    /// reaches it (<see cref="WeaponsReach"/>). Returns that distance (meters).
    /// </summary>
    float BringWithinReach(MCMover& shooter, MCMover& target, bool moveShooter = false)
    {
        const float meters = WeaponsReach(shooter);
        const float reach = meters * WorldUnitsPerMeter;
        MCMover& moved = moveShooter ? shooter : target;
        MCMover& stays = moveShooter ? target : shooter;
        const MCVector3D from = stays.GetPosition();
        const MCVector3D to = moved.GetPosition();
        const float dx = to.X - from.X;
        const float dy = to.Y - from.Y;
        const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
        MCVector3D place(from.X + dx / length * reach, from.Y + dy / length * reach, from.Z);
        moved.SetPosition(place);
        return meters;
    }

    /// <summary>
    /// <paramref name="enemy"/> run in along its line to where every weapon of <paramref name="mech"/> reaches it (if
    /// further), on a player's order: a pilot under fire keeps to its alarm orders over its commander's. Returns how far
    /// it is (meters).
    /// </summary>
    float RunInto(MCMover& enemy, MCMover& mech)
    {
        const MCVector3D from = mech.GetPosition();
        const MCVector3D to = enemy.GetPosition();
        const float length = std::max(std::hypot(to.X - from.X, to.Y - from.Y), 1.0f);
        const float reach = std::min(WeaponsReach(mech) * WorldUnitsPerMeter, length);
        WalkTo(enemy,
               MCVector3D(from.X + (to.X - from.X) / length * reach, from.Y + (to.Y - from.Y) / length * reach, to.Z),
               MCOrderOrigin::Player, true);
        return length / WorldUnitsPerMeter;
    }

    /// <summary>ffmpeg reading raw RGBA frames on its input, writing an H.264 video.</summary>
    class MCVideoPipe
    {
    public:
        MCVideoPipe(const std::filesystem::path& path, int width, int height, int framesPerSecond)
        {
            // CRF 10: near lossless, so the edit's own encode is the only loss that shows.
            const std::string command = std::format(
                "ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - -c:v libx264 -preset fast "
                "-crf 10 -pix_fmt yuv420p \"{}\"",
                width, height, framesPerSecond, path.string());
#ifdef _WIN32
            _Pipe = _popen(command.c_str(), "wb");
#else
            _Pipe = popen(command.c_str(), "w");
#endif
        }

        ~MCVideoPipe()
        {
            if (_Pipe != nullptr)
            {
#ifdef _WIN32
                _pclose(_Pipe);
#else
                pclose(_Pipe);
#endif
            }
        }

        MCVideoPipe(const MCVideoPipe&) = delete;
        MCVideoPipe& operator=(const MCVideoPipe&) = delete;

        /// <summary>Whether ffmpeg started.</summary>
        bool Open() const { return _Pipe != nullptr; }

        /// <summary>Writes a frame; false when ffmpeg took less.</summary>
        bool Write(std::span<const SDL_Color> pixels)
        {
            return std::fwrite(pixels.data(), sizeof(SDL_Color), pixels.size(), _Pipe) == pixels.size();
        }

    private:
        FILE* _Pipe = nullptr;
    };
}

/// <summary>
/// Tool: <c>--showcase &lt;pack&gt; --shots &lt;folder&gt;</c> records a mission's battle (<c>--mission &lt;n&gt;</c>,
/// default 1) for a before/after video, picture and sound: the game booted with its original sound on (so pilots'
/// videos play with their radio calls), into a mixer without a device whose mix is taken frame by frame, drawn as --hd-pack's
/// pictures are (<c>--window</c>), the player's mechs (up to 12) sent at the nearest enemy as --hd-audit plays it. Each of
/// <c>--showcase-frames &lt;n&gt;</c> frames (default 900: 30 seconds at 30 a second) from frame
/// <c>--showcase-from &lt;n&gt;</c> on (default 0; the earlier ones are played, their sound mixed and dropped) is saved
/// as <c>frames/NNNNN.png</c>, or with <c>--showcase-video 1</c> into <c>video.mp4</c>, and its share of the mix
/// appended to <c>sound.f32</c> (48 kHz stereo, 32-bit float), with HD art on from the pack, or with
/// <c>--hd-original 1</c> off; <c>--showcase-frames 0</c> scouts two minutes, saving only a look every
/// <c>--showcase-peek &lt;n&gt;</c> frames (<c>peek/NNNNN.png</c>). Staging, all optional, a mech named by its place
/// in the player's force (0 the first; <c>frame:mech</c>, or <c>frame:mech:what</c>, comma-separated):
/// <c>--showcase-zoom &lt;lines&gt;</c> zooms the view to that many game lines (480 the closest a player gets, fewer
/// fixed as a taller screen's closest); <c>--showcase-hud &lt;percent&gt;</c> sets the interface size (200 the
/// biggest) on a 1080-line UI; <c>--showcase-camera</c> follows that mech from that frame (its wreck once it falls;
/// the first mech still fighting before any);
/// <c>--showcase-walk &lt;frame&gt;</c> has the force walk at the target until then, no one firing, and
/// <c>--showcase-contact &lt;frame&gt;</c> sets the nearest converging enemy mech down ahead of the force (halted a
/// second before), on its sensors and out of its sight (the rest kept off its sensors): it walks in as a blip, steps
/// into sight just before the walk's end (<c>--showcase-contact-step &lt;frames&gt;</c> before, default 100) and is the
/// force's first target;
/// <c>--showcase-watch &lt;from&gt;,&lt;to&gt;</c> puts
/// the camera between the first mech and the contact between those frames, the fog's edge, zoomed out to
/// <c>--showcase-watch-zoom &lt;lines&gt;</c> if given (eased out and back in over
/// <c>--showcase-zoom-ease &lt;frames&gt;</c>, else cut); <c>--showcase-converge &lt;names&gt;</c> sends the other
/// sides' units named so (comma-separated parts of names) at the force once it attacks, the two sides standing
/// LineMeters apart, each unit on its own mech of the other side (fire spread along the lines);
/// <c>--showcase-answer &lt;frame&gt;</c> has the force answer from then: its recycled weapons readied each second and
/// fired at the most worn enemy mech in the fight, staged to land, and the other side's mechs held as the force's are
/// (their bars fall with the hits, none falls but on its cue);
/// <c>--showcase-jump &lt;n&gt;</c> and <c>--showcase-jump-at &lt;frame&gt;,...</c> have the first three mechs jump at
/// the target every n frames or at those, <c>--showcase-dfa</c> one mech at the enemy mech nearest it;
/// <c>--showcase-strike &lt;frame&gt;,...</c> drops a large artillery strike on the target, and
/// <c>--showcase-incoming &lt;frame&gt;[:mech],...</c> the other side's on that mech (the first) and its nearest mate,
/// marked; <c>--showcase-mines</c> sets a mine off under a mech as it steps out toward its enemy onto ground the force
/// hasn't crossed (the other side's minefield), <c>--showcase-smoke</c> sets the forests nearest it alight
/// (their smoke all around it); <c>--showcase-snow-least &lt;from&gt;,&lt;to&gt;</c> throws the snow's least share
/// from one frame and all of it again from the other (MCWeatherPacing::Hold), the steepest a slow machine's pace cuts
/// it and brings it back;
/// <c>--showcase-alpha</c> sends the enemy mech nearest a mech running in to its reach five seconds before (held
/// standing until then; set down within reach the frame before if still further) and has the mech fire every weapon at
/// it, the camera between them if on it; <c>--showcase-destroy
/// &lt;frame&gt;[:mech],...</c> blows apart the alpha strike's target (that mech's), or the enemy mech nearest that
/// mech (the camera's) the force reaches;
/// <c>--showcase-kill &lt;frame&gt;,...</c> weakens the enemy mech nearest the force and sends the force at it.
/// The force's end: <c>--showcase-run</c> has a mech run from the nearest enemy mech for three seconds (or until it
/// loses a leg); <c>--showcase-close &lt;frame&gt;:mech[:n[@meters]]</c> has those named by
/// <c>--showcase-close-names</c> close in to NooseMeters of a mech (the camera's once it falls), the n furthest
/// (default four) first set down behind it (BehindMeters, or meters) on open ground level with it, where neither they
/// nor their spot are on screen (on a camera cut, anywhere: the new view finds them there), to walk in on it (the
/// camera on it framing it with them for a while);
/// <c>--showcase-volley</c> readies every
/// enemy weapon and has those in range (missiles and all) fire at a mech for a second; <c>--showcase-loss</c> takes a
/// mech's leg, an arm (the left, then the right), its life (<c>die</c>: blown apart) or its pilot (<c>eject</c>), each
/// on a hit: whatever reaches the mech fires at it just before, the nearest three readied (<c>hit</c>: only that, for a
/// call of enemy fire); once the force is gone, no one fires. Staged fire (for losses, kills, alpha strikes and the
/// volley) hits, and no unit's cells block its line, so what it is staged for lands on cue. With losses staged, the
/// force holds (structure, vital parts, pilots and the last of its armor) until then, so none falls before its time;
/// the game's hits wear the rest of its armor, so its bars fall as it is hit (<c>--showcase-wear</c> sends the enemy
/// mechs nearest a mech at it from a frame on, to wear it down). The player's mechs are printed first, and
/// each second's mix level, state and the force's health bars.
/// A run plays out the same every time, so the two runs differ only in how they look and sound.
/// </summary>
TEST_CASE_ISOLATED("tool: --showcase records a mission's battle, picture and sound, for a before/after video")
{
    const std::optional<std::string_view> pack = MCTest::Option("showcase");
    const std::optional<std::string_view> shots = MCTest::Option("shots");

    if (!pack.has_value() || !shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const bool original = MCTest::Option("hd-original").has_value();
    const std::filesystem::path packFolder = std::filesystem::absolute(std::filesystem::path(*pack));
    MCTestContextScope scope;
    scope.Context().SetAudio(std::make_unique<MCNullAudioDevice>());
    MCTestGame::BootOptions.Sound = true;
    // The original's sound in both runs: the radio's lines last as long in each, so when the next one starts, and the
    // dice it takes, match (HD audio's lines differ by a few samples, enough to part the runs within minutes).
    MCTestGame::BootOptions.Started = [&]
    {
        GHdAudio = HdAudioOff;
        MCHdArt::SetPackFolder(original ? std::filesystem::path() : packFolder);
    };

    std::optional<MCHdArtOn> hdArt;
    StartPictures(*pack, hdArt);
    const std::shared_ptr<MCSoundRenderer> sound = MCGameContext::Current().SoundRenderer();
    REQUIRE(sound != nullptr);
    REQUIRE(!sound->Mixer().HasDevice());
    // The mouse timer's thread counts its ticks and services the sound's streams in real time, which a slower run (one
    // taking pictures, or without HD art) would see later: when a radio message ends decides when the next starts and
    // the dice it takes. Its ticks are counted here instead, frame by frame, so every run plays out the same.
    // (The timer ticks ResultsStepTicks times a second.)
    MouseTimerKill();

    constexpr int32_t FramesPerSecond = 30;
    constexpr float ShowcaseFrameSeconds = 1.0f / FramesPerSecond;
    constexpr int32_t DefaultFrames = 900;
    constexpr int32_t RetargetFrames = 150;
    constexpr size_t SoundChannels = 2;
    constexpr int32_t ScoutFrames = 3600;
    constexpr int32_t LargeStrike = 1;
    constexpr int32_t StrikeDelaySeconds = 1;
    // A kill keeps 30% of its internal structure and goes in a volley or two.
    constexpr float KillStructure = 0.3f;
    // A mech running goes this far from the nearest enemy mech, ordered again every second, for three.
    constexpr float RetreatDistance = 300.0f;
    constexpr int32_t RetreatFrames = 30;
    constexpr int32_t RunFrames = 90;
    // Staged damage lands with a hit: whatever reaches its mech fires at it from FireLeadFrames before it is due, in a
    // volley every LossVolleyFrames until it lands (a shot's damage comes when its effect gets there: a beam's when it
    // ends, half a second on, a slug's or missile's when it arrives), and the first hit from HitWindowFrames before
    // takes the part (with none by HitGraceFrames after, it goes anyway).
    constexpr int32_t FireLeadFrames = 24;
    // A death's fire starts DieLeadFrames before it is due instead (from those not held for it until FireLeadFrames),
    // so its hits wear the mech's bar down first.
    constexpr int32_t DieLeadFrames = 90;
    constexpr int32_t LossVolleyFrames = 8;
    constexpr int32_t HitWindowFrames = 8;
    constexpr int32_t HitGraceFrames = 30;
    constexpr int32_t LossGapFrames = 10;
    // A held mech of the force has its full armor raised to the most a location takes, so no one frame's fire (a
    // volley's missiles bursting all around it) goes through to its structure and breaks its engine, ammunition or head
    // before its staged end. What the game's own hits take off it comes off the armor the mech really has, and the
    // raised armor is held at the share of that left, so its bar (the share of its armor its locations have) falls as
    // it is hit; no location's last WornFloor share goes before its staged end.
    constexpr uint8_t ArmorHeld = 255;
    constexpr float WornFloor = 0.2f;
    // A mech worn down (--showcase-wear frame:mech[:frames]) has WearShooters enemy mechs (the nearest, none sent at one
    // worn sooner) kept at it for that many frames or until it falls, and while its bar is above one falling from full
    // to WearEndBar over them: one of those further than all its weapons reach (by WearSlack) set down where they do
    // once neither is on screen, and from WearLeadFrames on, what reaches it fired at it as for a staged loss (the
    // WearReadied units nearest readied), a volley every LossVolleyFrames. The game's own shots wear its armor.
    constexpr size_t WearShooters = 3;
    constexpr int32_t WearFrames = 30;
    constexpr int32_t WearLeadFrames = 30;
    constexpr float WearSlack = 1.25f;
    constexpr float WearEndBar = 0.3f;
    constexpr int32_t WearReadied = 8;
    constexpr int32_t ApproachFrames = 90;
    constexpr size_t ApproachingMechs = 3;
    // Out of turn, only the weapons of this many units nearest a target are readied, a unit no more often than this.
    constexpr int32_t ReadiedShooters = 4;
    constexpr float ShooterRestSeconds = 2.0f;
    // The force sees as far as its tiles' heights let it (the sight its mechs give is not used), its sensors further.
    // The fog's edge is the first spot ahead of the force it does not see between frames (spots tried this much
    // further along the line from the first mech at a time, world units); a unit there is seen about a fifth further
    // out. The contact is set down this many times the edge's distance from the force's nearest mech, walks in to stand
    // this many (a blip on the sensors, in the fog), and steps into sight, this many, ContactStepFrames before the
    // walk's end (it walks about 8 m a second; --showcase-contact-step gives a ground whose sight reaches further more).
    constexpr float ContactTry = 10.0f;
    constexpr float ContactFrom = 1.55f;
    constexpr float ContactOutside = 1.4f;
    constexpr float ContactStop = 0.75f;
    constexpr int32_t ContactStepFrames = 100;
    // The force halts this long before the contact, so what it sees has settled.
    constexpr int32_t HaltLeadFrames = 30;
    // Until the force attacks, the rest of the other side is kept this many of the sensors' reaches from it, so the
    // contact is the first; then those converging and further off are set down this many times the edge's distance
    // from it, unseen, to come in.
    constexpr float SensorClear = 1.1f;
    constexpr float ConvergeFrom = 1.2f;
    // The watch's camera holds this share of the way from the force's mech nearest where the contact stands to there:
    // the fog's edge, the force's front on one side, the blip coming in on the other.
    constexpr float WatchShare = 0.45f;
    // Once the fight starts it looks this share of the way from the force's nearest mech to the contact.
    constexpr float FightShare = 0.7f;
    // The other side's strikes are called this far from their mech, toward its nearest enemy, to land this long after
    // (their marks show from 4 seconds out; under 3 the strike's own time is used).
    constexpr float IncomingOffset = 120.0f;
    constexpr int32_t IncomingSeconds = 3;
    // It is then moved onto the force, on open ground level with the mech, looked for this far around (meters).
    constexpr float IncomingSearchMeters = 40.0f;
    // A volley: the weapons of this many enemy units nearest the mech readied at once, and fired as they reach it, for a
    // second.
    constexpr int32_t VolleyFrames = 30;
    constexpr int32_t VolleyShooters = 6;
    // An alpha strike: the mech fires every weapon as it comes to bear, for a second and a half, its target brought
    // within reach the frame before (staged on a cut to the mech, so the camera is elsewhere then). The camera, on the
    // mech since before, looks this share of the way from it to the target.
    constexpr int32_t AlphaFrames = 45;
    constexpr int32_t AlphaBringFrames = 1;
    // Before it, the mech holds the fire of the weapons that come ready this long, so they go together, and the enemy
    // mech nearest it (not sent in for another) runs to within its reach this long before (of its own accord, where the
    // camera may see), ordered on again this often.
    constexpr int32_t AlphaHoldFrames = 240;
    constexpr int32_t AlphaApproachFrames = 150;
    constexpr int32_t AlphaRunFrames = 15;
    constexpr float AlphaShare = 0.5f;
    // An enemy being finished near the mech followed is framed with it (AlphaShare between them) this long from the
    // fire on it.
    constexpr int32_t KillFrames = 90;
    // A mech in smoke has the SmokeTrees forests nearest it within SmokeTreeMeters burning this long.
    constexpr size_t SmokeTrees = 12;
    constexpr float SmokeTreeMeters = 90.0f;
    constexpr float SmokeSeconds = 15.0f;
    // Those closing in are ordered again every two seconds; at the close's frame its what (default Brought) of them are
    // set down behind the mech first (CloseIn). The camera on the mech they close on frames it with them (AlphaShare
    // between it and their middle) this long from then, so they are seen coming.
    constexpr int32_t CloseFrames = 60;
    constexpr int32_t Brought = 4;
    constexpr int32_t BroughtFrames = 75;
    // A strike's or a kill's target is the most worn of this many nearest.
    constexpr size_t WornChoices = 3;
    // The enemy mechs in the fight: those this near one of the force (the medium band).
    constexpr float FightMeters = 140.0f;
    constexpr int HudUiLines = 1080;
    const int32_t frames = ShowcaseOption("frames", DefaultFrames);
    const int32_t from = ShowcaseOption("from", 0);
    const int32_t peek = ShowcaseOption("peek", 0);
    const int32_t zoom = ShowcaseOption("zoom", 0);
    const int32_t hud = ShowcaseOption("hud", 0);
    const int32_t jumpEvery = ShowcaseOption("jump", 0);
    const std::vector<int32_t> strikes = ShowcaseFrames("strike");
    const std::vector<int32_t> snowLeast = ShowcaseFrames("snow-least");
    const std::vector<MCShowcaseEvent> incoming = ShowcaseEvents("incoming");
    const int32_t walkFrame = ShowcaseOption("walk", 0);
    const int32_t contactFrame = ShowcaseOption("contact", -1);
    const int32_t contactStep = ShowcaseOption("contact-step", ContactStepFrames);
    const int32_t answerFrame = ShowcaseOption("answer", -1);
    const std::vector<int32_t> kills = ShowcaseFrames("kill");
    const std::vector<int32_t> watch = ShowcaseFrames("watch");
    const int32_t watchZoom = ShowcaseOption("watch-zoom", 0);
    const int32_t zoomEase = std::max(ShowcaseOption("zoom-ease", 0), 1);
    const std::vector<int32_t> jumpFrames = ShowcaseFrames("jump-at");
    const std::vector<MCShowcaseEvent> cameras = ShowcaseEvents("camera");
    const std::vector<MCShowcaseEvent> dfas = ShowcaseEvents("dfa");
    const std::vector<MCShowcaseEvent> mines = ShowcaseEvents("mines");
    const std::vector<MCShowcaseEvent> alphas = ShowcaseEvents("alpha");
    const std::vector<MCShowcaseEvent> destroys = ShowcaseEvents("destroy");
    const std::vector<MCShowcaseEvent> runs = ShowcaseEvents("run");
    const std::vector<MCShowcaseEvent> closes = ShowcaseEvents("close");
    const std::vector<MCShowcaseEvent> volleys = ShowcaseEvents("volley");
    const std::vector<MCShowcaseEvent> losses = ShowcaseEvents("loss");
    const std::vector<MCShowcaseEvent> smokes = ShowcaseEvents("smoke");
    const std::vector<MCShowcaseEvent> wears = ShowcaseEvents("wear");
    const bool video = ShowcaseOption("video", 0) != 0;
    const std::optional<std::string_view> converge = MCTest::Option("showcase-converge");
    const std::optional<std::string_view> closeNames = MCTest::Option("showcase-close-names");

    if (hud > 0)
    {
        GUiResolution = HudUiLines;
        GInterfaceSize = hud;
        MCTestGame::RunFrame(ShowcaseFrameSeconds);
    }

    // Closer than a player's closest zoom (480 lines, 2.25 window pixels a game pixel at 1080) stands for a taller
    // screen's closest: 360 lines is a 1440-line screen's.
    if (zoom > 0)
    {
        constexpr int32_t ClosestPlayerZoom = 480;
        MCFixedZoomHeight = zoom < ClosestPlayerZoom ? static_cast<float>(zoom) : 0.0f;
        REQUIRE(MCMainView() != nullptr);
        MCMainView()->ZoomTo(static_cast<float>(zoom));
        std::cout << std::format("  zoom: {:.0f} lines\n", MCMainView()->ZoomTarget);
    }

    // Every mech of the player's, with its pilot and the pilot's voice.
    for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + PlayerMechs; ++partId)
    {
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover == nullptr || mover->IsDisabled() != 0)
        {
            continue;
        }

        const std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
        std::cout << std::format(
            "  player: {} (part {:#x}), pilot {} voice {} video {}, jump range {:.0f}, fire ranges "
            "{:.0f}/{:.0f}/{:.0f}/{:.0f} m\n",
            mover->GetIfaceName(), partId, pilot != nullptr ? pilot->Callsign : "-",
            pilot != nullptr ? pilot->AudioStr : "-", pilot != nullptr ? pilot->VideoStr : "-",
            mover->CanJump() != 0 ? mover->GetJumpRange() : 0.0f, mover->GetFireRange(0), mover->GetFireRange(1),
            mover->GetFireRange(2), mover->GetFireRange(-2));
    }

    // The other sides' units by name, to choose a mission.
    if (const std::vector<std::shared_ptr<MCMover>> lance = LiveLance(); !lance.empty())
    {
        const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
        std::map<std::string, int32_t> enemies;
        std::map<std::string, std::string> ranges;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->Team.lock() != team && mover->IsDisabled() == 0)
            {
                ++enemies[std::string(mover->GetIfaceName())];
                ranges[std::string(mover->GetIfaceName())] =
                    std::format("{:.0f}/{:.0f}/{:.0f}/{:.0f} m", mover->GetFireRange(0), mover->GetFireRange(1),
                                mover->GetFireRange(2), mover->GetFireRange(-2));
            }
        }

        for (const auto& [name, count] : enemies)
        {
            std::cout << std::format("  enemy: {} x{}, fire ranges {}\n", name, count, ranges[name]);
        }
    }

    // A scout plays two minutes and saves nothing but its looks (--showcase-peek).
    const bool scout = frames == 0;
    const int32_t last = scout ? ScoutFrames : from + frames;

    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder / "frames");
    std::ofstream soundFile(folder / "sound.f32", std::ios::binary);
    REQUIRE(soundFile.good());
    std::optional<MCVideoPipe> pipe;
    std::vector<float> mix;
    uint64_t rendered = 0;
    std::shared_ptr<MCMover> target;
    // When each of the player's mechs lost its leg (it fights on where it stands), and how many arms.
    std::map<int32_t, int32_t> crippledAt;
    std::map<int32_t, int32_t> armsLost;
    // The alpha strike's mech and target, the contact coming in and the enemy mech being finished.
    std::shared_ptr<MCMover> alphaShooter;
    std::shared_ptr<MCMover> alphaTarget;
    int32_t alphaFrame = -1;
    // The enemy mech sent in for each alpha strike (by its frame), held standing until then.
    std::map<int32_t, std::shared_ptr<MCMover>> alphaRunners;
    std::shared_ptr<MCMover> contactMech;
    MCVector3D contactInSight;
    float contactEdge = 0.0f;
    std::optional<MCVector3D> watchAt;
    int32_t contactType = 0;
    std::shared_ptr<MCMover> killing;
    // When each of the force last had its weapons readied to answer (--showcase-answer), by part id.
    std::map<int32_t, float> answeredAt;
    // The mech the enemy being finished is near, and when the fire on it started.
    std::shared_ptr<MCMover> killNear;
    int32_t killFrom = -1;
    // The second line the last close brought in behind its mech (CloseIn), and when.
    std::vector<std::shared_ptr<MCMover>> brought;
    int32_t broughtFrame = -1;
    // Every unit's toughness as the frame starts running, those it hit, and the staged damage landed (due, mech, kind).
    std::map<int32_t, float> toughness;
    std::set<int32_t> hit;
    std::set<std::tuple<int32_t, int32_t, char>> landed;
    // When each mech (by part id) last took a staged loss.
    std::map<int32_t, int32_t> lastLanded;
    std::map<int32_t, bool> wasJumping;
    std::set<int32_t> fell;
    MCReadiedAt readiedAt;
    // The health the force's vital parts started with (by part id and inventory index), held with its armor.
    std::map<std::pair<int32_t, size_t>, uint8_t> vitals;
    // The armor the force's mechs really have (by part id, each location's points left and full), worn by the game's
    // hits.
    std::map<int32_t, std::vector<std::pair<float, float>>> worn;
    // The enemy units last sent to wear a mech down (part ids; by wear, its index): staged losses leave their fire be.
    std::set<int32_t> wearing;
    std::map<size_t, std::vector<std::shared_ptr<MCMover>>> wearSent;
    // The units holding their fire for each staged loss (due, mech, kind).
    std::map<std::tuple<int32_t, int32_t, char>, std::vector<std::shared_ptr<MCMover>>> holding;
    // The enemy mechs sent in at each staged loss's mech (FireFor), by loss: the stand-off's orders leave them be.
    std::map<std::tuple<int32_t, int32_t, char>, std::vector<std::shared_ptr<MCMover>>> approaching;
    int32_t fallen = 0;
    int32_t jumps = 0;
    double secondEnergy = 0.0;
    // The fight's balance, each second: shots fired by each side, armor and structure the enemy mechs lost.
    std::map<int32_t, std::vector<float>> readyTimes;
    std::map<int32_t, float> enemyToughness;
    int32_t forceShots = 0;
    int32_t answerShots = 0;
    int32_t enemyShots = 0;
    float enemyLost = 0.0f;

    // The mechs running now.
    const auto Running = [&](int32_t frame)
    {
        std::set<int32_t> running;

        for (const MCShowcaseEvent& run : runs)
        {
            const std::shared_ptr<MCMover> mech = ForceMech(run.Mech);

            if (mech != nullptr && mech->IsDisabled() == 0 && frame >= run.Frame && frame < run.Frame + RunFrames &&
                !crippledAt.contains(mech->PartId))
            {
                running.insert(mech->PartId);
            }
        }

        return running;
    };

    // The enemy mechs in the fight: within FightMeters of one of the force.
    const auto InFight = [](const std::vector<std::shared_ptr<MCMover>>& lance)
    {
        std::vector<std::shared_ptr<MCMover>> fighting;

        if (lance.empty())
        {
            return fighting;
        }

        for (const std::shared_ptr<MCMover>& enemy : EnemyMechsByDistance(*lance.front()))
        {
            const MCVector3D to = enemy->GetPosition();

            if (std::ranges::any_of(lance,
                                    [&](const std::shared_ptr<MCMover>& mech)
                                    {
                                        const MCVector3D at = mech->GetPosition();
                                        return std::hypot(to.X - at.X, to.Y - at.Y) < FightMeters * WorldUnitsPerMeter;
                                    }))
            {
                fighting.push_back(enemy);
            }
        }

        return fighting;
    };

    // A minefield is the other side's, laid before its line: before its first mine (MineWalkFrames, or its what's
    // frames) its mech steps out toward its enemy as far as it walks by then at MineWalkPace and MineBeyondMeters more
    // (its own way round what's between; the first of MineWalkShares of that with such ground) to where the way from
    // MineSlackMeters short of the first mine's place (at most MineSlackShare of the way there) on is open ground level with it that the force has not crossed
    // (none of it within FreshMeters; the force's places are kept every TrailFrames), turned as little from the line to
    // its enemy as there is such ground (up to MineTurnDegrees either side, MineTurnStepDegrees apart). Each mine goes
    // off MineAheadMeters ahead of it, MineSideMeters left and right in turn; MineSettleFrames after the last it backs
    // out the way it came, and keeps out of the force's orders until it is back.
    constexpr int32_t MineWalkFrames = 60;
    constexpr float MineWalkPace = 10.0f;
    constexpr float MineBeyondMeters = 15.0f;
    constexpr float MineSlackMeters = 15.0f;
    constexpr float MineSlackShare = 0.5f;
    constexpr std::array MineWalkShares = {1.0f, 0.85f, 1.15f, 0.7f};
    constexpr float FreshMeters = 10.0f;
    constexpr float WayStep = 0.05f;
    constexpr int32_t TrailFrames = 15;
    constexpr float MineTurnDegrees = 90.0f;
    constexpr float MineTurnStepDegrees = 10.0f;
    constexpr float MineAheadMeters = 4.0f;
    constexpr float MineSideMeters = 3.0f;
    constexpr int32_t MineSettleFrames = 45;
    // Where the force has been, and each mech walking onto its minefield (part id): where from, where to, the way's
    // heading, when it backs out and when it is back.
    std::vector<MCVector3D> trail;
    struct MCMineWalk
    {
        MCVector3D From;
        MCVector3D To;
        float Heading = 0.0f;
        int32_t Back = 0;
        int32_t Until = 0;
    };

    std::map<int32_t, MCMineWalk> mineWalks;
    // Each minefield's first mine (its place in mines) and its last mine's frame: a mech's mines each within
    // MineWalkFrames of the one before.
    std::map<size_t, int32_t> minefields;

    for (size_t index = 0; index < mines.size(); ++index)
    {
        const auto field = std::ranges::find_if(minefields,
                                                [&](const std::pair<const size_t, int32_t>& known)
                                                {
                                                    return mines[known.first].Mech == mines[index].Mech &&
                                                           mines[index].Frame - known.second <= MineWalkFrames;
                                                });

        if (field != minefields.end())
        {
            field->second = mines[index].Frame;
        }
        else
        {
            minefields[index] = mines[index].Frame;
        }
    }

    // The mechs walking onto their minefields now.
    const auto MineWalking = [&](int32_t frame)
    {
        std::set<int32_t> walking;

        for (const auto& [partId, walk] : mineWalks)
        {
            if (frame <= walk.Until)
            {
                walking.insert(partId);
            }
        }

        return walking;
    };

    // Where <mech> walks for its minefield, <lead> frames before the first mine (and how far it turns from its enemy,
    // degrees): the way open, level and fresh from MineSlackMeters short of the first mine on, at the first walk with
    // any such way, turned least. None if there is no such way.
    const auto FreshGround = [&](MCMover& mech, int32_t lead) -> std::optional<std::pair<MCVector3D, float>>
    {
        const float first = MineWalkPace * static_cast<float>(lead) / FramesPerSecond;
        const MCVector3D at = mech.GetPosition();
        const std::shared_ptr<MCMover> enemy = NearestEnemyMech(mech);
        const float toward =
            enemy != nullptr ? std::atan2(enemy->GetPosition().Y - at.Y, enemy->GetPosition().X - at.X) : 0.0f;
        const float level = GameMap().GetTerrainElevation(at);
        const float fresh = FreshMeters * WorldUnitsPerMeter;
        const auto Fresh = [&](const MCVector3D& point)
        {
            return std::ranges::none_of(trail, [&](const MCVector3D& been)
                                        { return std::hypot(been.X - point.X, been.Y - point.Y) < fresh; });
        };

        std::optional<std::pair<MCVector3D, float>> best;

        for (const float share : MineWalkShares)
        {
            const float meters = (first + MineBeyondMeters) * share;
            const float freshFrom = std::max(first - MineSlackMeters, first * MineSlackShare) / meters;

            for (float degrees = -MineTurnDegrees; degrees <= MineTurnDegrees; degrees += MineTurnStepDegrees)
            {
                const float turn = toward + degrees * std::numbers::pi_v<float> / 180.0f;
                const MCVector3D to(at.X + std::cos(turn) * meters * WorldUnitsPerMeter,
                                    at.Y + std::sin(turn) * meters * WorldUnitsPerMeter, at.Z);
                bool clear = true;

                for (float along = freshFrom; along <= 1.0f + WayStep * 0.5f && clear; along += WayStep)
                {
                    const MCVector3D point(at.X + (to.X - at.X) * along, at.Y + (to.Y - at.Y) * along, at.Z);
                    clear = OpenAndLevel(point, level) && Fresh(point);
                }

                if (clear && (!best.has_value() || std::abs(degrees) < std::abs(best->second)))
                {
                    best = std::pair(to, degrees);
                }
            }

            if (best.has_value())
            {
                break;
            }
        }

        return best;
    };

    // The lance (the first three) as SendLanceAt sends it (selected, as a player would), then each of the player's mechs
    // at its own enemy mech of those in the fight, to LineMeters from it (Engage; SpreadTargets: the force's fire spread
    // along their line); at the fight's start (spread false), or with none in the fight, all at the one given. Those
    // running run on; those walking onto a minefield walk on.
    const auto SendAllAt = [&](const std::shared_ptr<MCMover>& at, int32_t frame, bool spread)
    {
        SendLanceAt(at);
        const std::set<int32_t> running = Running(frame);
        const std::set<int32_t> mining = MineWalking(frame);
        std::vector<std::shared_ptr<MCMover>> attackers;

        for (const std::shared_ptr<MCMover>& mover : LiveLance())
        {
            if (running.contains(mover->PartId))
            {
                RetreatFrom(*mover, RetreatDistance);
            }
            else if (!mining.contains(mover->PartId))
            {
                attackers.push_back(mover);
            }
        }

        const std::vector<std::shared_ptr<MCMover>> targets =
            SpreadTargets(attackers, spread ? InFight(attackers) : std::vector<std::shared_ptr<MCMover>>());

        for (size_t i = 0; i < attackers.size(); ++i)
        {
            Engage(*attackers[i], targets[i] != nullptr ? targets[i] : at, LineMeters, MCOrderOrigin::Player);
        }
    };

    // Before --showcase-walk's frame the force walks at the target instead: nothing is in sight yet.
    const auto WalkAllAt = [&](const std::shared_ptr<MCMover>& at)
    {
        for (const std::shared_ptr<MCMover>& mover : LiveLance())
        {
            WalkTo(*mover, at->GetPosition());
        }
    };

    // The force's mech nearest a point, and how far it is (world units).
    const auto NearestOfForce = [](const std::vector<std::shared_ptr<MCMover>>& lance, const MCVector3D& point)
    {
        std::pair<std::shared_ptr<MCMover>, float> nearest{nullptr, std::numeric_limits<float>::max()};

        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            const MCVector3D mech = mover->GetPosition();

            if (const float away = std::hypot(mech.X - point.X, mech.Y - point.Y); away < nearest.second)
            {
                nearest = {mover, away};
            }
        }

        return nearest;
    };

    // From --showcase-contact's frame until the walk's end the force stands.
    const auto HaltAll = [&]()
    {
        for (const std::shared_ptr<MCMover>& mover : LiveLance())
        {
            MCTacticalOrder order;
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Stop, 0);
            mover->HandleTacticalOrder(order, 1, 0);
        }
    };

    // The farthest the force's sensors reach standing (meters; moving cuts it).
    const auto Sensors = [](const std::vector<std::shared_ptr<MCMover>>& lance)
    {
        float sensors = 0.0f;

        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            if (const std::shared_ptr<MCSensorSystem> sensor = mover->SensorSystem.lock(); sensor != nullptr)
            {
                sensors = std::max(sensors, sensor->Range * sensor->Multiplier);
            }
        }

        return sensors;
    };

    // A mech held standing this frame: its armor (at <paramref name="share"/> of its full, raised to
    // <paramref name="full"/> first if given), the structure of what it still has, its vital parts (engine, gyro,
    // cockpit, life support: in its torso and head) and its pilot.
    const auto Hold = [&](MCMover& mover, std::optional<uint8_t> full = std::nullopt, float share = 1.0f)
    {
        for (MCArmorLocation& armor : mover.Armor)
        {
            armor.MaxArmor = full.value_or(armor.MaxArmor);
            armor.CurArmor = armor.MaxArmor * share;
        }

        for (MCBodyLocation& body : mover.Body)
        {
            body.CurInternalStructure =
                body.CurInternalStructure > 0.0f ? body.MaxInternalStructure : body.CurInternalStructure;
        }

        for (size_t i = 0; i < mover.Inventory.size(); ++i)
        {
            MCInventoryItem& item = mover.Inventory[i];

            if (const MCComponentForm form = MasterComponentList[item.MasterID].Form;
                form == MCComponentForm::Engine || form == MCComponentForm::Gyroscope ||
                form == MCComponentForm::Cockpit || form == MCComponentForm::LifeSupport)
            {
                item.Health = vitals.try_emplace({mover.PartId, i}, item.Health).first->second;
            }
        }

        if (const std::shared_ptr<MCMechWarrior> pilot = mover.GetPilot(); pilot != nullptr)
        {
            pilot->Wounds = 0.0f;
        }
    };

    // A unit's fire held this frame: a weapon ready is put off a little (more than the frame the mission's clock moves
    // on before the unit fires), so it fires with the rest when let go.
    const auto HoldFire = [&](MCMover& mover)
    {
        constexpr float HeldSeconds = 0.3f;

        for (int32_t i = 0; i < mover.NumWeapons; ++i)
        {
            float& ready = mover.Inventory[mover.NumOther + i].ReadyTime;
            ready = std::max(ready, ScenarioTime + HeldSeconds);
        }
    };

    // The enemy mechs running in for an alpha strike still to come (part ids): others' orders leave them be.
    const auto Runners = [&](int32_t frame)
    {
        std::set<int32_t> runners;

        for (const auto& [strike, runner] : alphaRunners)
        {
            if (strike > frame && runner != nullptr)
            {
                runners.insert(runner->PartId);
            }
        }

        return runners;
    };

    // When a strike's mech starts holding its fire: AlphaHoldFrames before, from the end of its last strike at the
    // earliest.
    const auto AlphaHoldFrom = [&](const MCShowcaseEvent& alpha)
    {
        int32_t holdFrom = alpha.Frame - AlphaHoldFrames;

        for (const MCShowcaseEvent& before : alphas)
        {
            holdFrom = before.Mech == alpha.Mech && before.Frame < alpha.Frame
                           ? std::max(holdFrom, before.Frame + AlphaFrames)
                           : holdFrom;
        }

        return holdFrom;
    };

    // The mechs holding their fire for a strike this frame (part ids): other staged fire leaves their weapons be, so
    // they have them all for it.
    const auto Holding = [&](int32_t frame)
    {
        std::set<int32_t> holding;

        for (const MCShowcaseEvent& alpha : alphas)
        {
            if (const std::shared_ptr<MCMover> mech = ForceMech(alpha.Mech);
                mech != nullptr && frame >= AlphaHoldFrom(alpha) && frame < alpha.Frame)
            {
                holding.insert(mech->PartId);
            }
        }

        return holding;
    };

    // When a wear (--showcase-wear) stops: its frames on, if given.
    const auto WearUntil = [](const MCShowcaseEvent& wear)
    {
        int32_t frames = std::numeric_limits<int32_t>::max() - wear.Frame;
        std::from_chars(wear.What.data(), wear.What.data() + wear.What.size(), frames);
        return wear.Frame + frames;
    };

    // The bar a worn mech is to be down to by a frame (see WearShooters): full at the wear's start, WearEndBar at its
    // end (an endless wear's stays full).
    const auto WearBar = [&](const MCShowcaseEvent& wear, int32_t frame)
    {
        const int64_t span = std::max<int64_t>(static_cast<int64_t>(WearUntil(wear)) - wear.Frame, 1);
        const float progress =
            std::clamp(static_cast<float>(frame - wear.Frame) / static_cast<float>(span), 0.0f, 1.0f);
        return 1.0f - (1.0f - WearEndBar) * progress;
    };

    // A mech of the force held this frame (see ArmorHeld): what the game's hits took off its raised armor since the
    // last frame comes off the armor it really has, down to WornFloor of a location's (all of it once the location is
    // gone), and its raised armor is held at the share left.
    const auto HoldWorn = [&](MCMover& mover)
    {
        const auto [entry, first] = worn.try_emplace(mover.PartId);
        std::vector<std::pair<float, float>>& armor = entry->second;

        for (size_t i = 0; first && i < mover.Armor.size(); ++i)
        {
            armor.emplace_back(mover.Armor[i].CurArmor, static_cast<float>(mover.Armor[i].MaxArmor));
        }

        const auto Share = [](const std::pair<float, float>& location)
        { return location.second > 0.0f ? location.first / location.second : 1.0f; };

        for (size_t i = 0; !first && i < armor.size(); ++i)
        {
            const float held = static_cast<float>(ArmorHeld) * Share(armor[i]);
            const float now = mover.Armor[i].CurArmor;
            armor[i].first = now <= 0.0f ? 0.0f
                                         : std::max(armor[i].first - std::max(held - now, 0.0f),
                                                    std::min(armor[i].first, armor[i].second * WornFloor));
        }

        Hold(mover, ArmorHeld);

        for (size_t i = 0; i < armor.size(); ++i)
        {
            mover.Armor[i].CurArmor = static_cast<float>(ArmorHeld) * Share(armor[i]);
        }
    };

    // A mech HoldWorn held let go: its armor back to what it really has and its full, so the hits after take that.
    const auto Release = [&](MCMover& mover)
    {
        const auto held = worn.find(mover.PartId);

        if (held == worn.end())
        {
            return;
        }

        for (size_t i = 0; i < held->second.size() && i < mover.Armor.size(); ++i)
        {
            mover.Armor[i].MaxArmor = static_cast<uint8_t>(held->second[i].second);
            mover.Armor[i].CurArmor = held->second[i].first;
        }

        worn.erase(held);
    };

    // Whether staged damage of a kind due at a frame lands on its mech this frame: on a hit from HitWindowFrames
    // before (but not within LossGapFrames of the mech's last loss: two in one blast read as one), else HitGraceFrames
    // after regardless; once.
    const auto Lands = [&](MCMover& victim, int32_t frame, int32_t due, int32_t mech, char kind, std::string_view what)
    {
        if (due < 0 || frame < due - HitWindowFrames || frame > due + HitGraceFrames ||
            landed.contains({due, mech, kind}))
        {
            return false;
        }

        const auto last = lastLanded.find(victim.PartId);
        const bool apart = last == lastLanded.end() || frame >= last->second + LossGapFrames;
        const bool struck = hit.contains(victim.PartId);

        if ((!struck || !apart) && frame < due + HitGraceFrames)
        {
            return false;
        }

        landed.insert({due, mech, kind});
        lastLanded[victim.PartId] = kind == 'h' ? lastLanded[victim.PartId] : frame;
        std::cout << std::format("  frame {}: {} (part {:#x}) {} at bar {:.2f} ({})\n", frame, victim.GetIfaceName(),
                                 victim.PartId, what, victim.GetTotalEffectiveness(),
                                 struck ? "on a hit" : "no hit came");
        return true;
    };

    // How long before staged damage of a kind is due its fire starts, and its shooters are sent at its mech (FireFor).
    const auto LeadFor = [](char kind) { return kind == 'd' ? DieLeadFrames : FireLeadFrames; };
    const auto ApproachFor = [&](char kind) { return LeadFor(kind) + ApproachFrames - FireLeadFrames; };

    // The units held for staged damage still to land (part ids; see FireFor): wearing fire leaves them be, so they have
    // their weapons for it.
    const auto HeldForLosses = [&](int32_t frame)
    {
        std::set<int32_t> held;

        for (const auto& [loss, shooters] : holding)
        {
            if (const int32_t due = std::get<0>(loss);
                frame > due - ApproachFor(std::get<2>(loss)) && frame <= due + HitGraceFrames && !landed.contains(loss))
            {
                for (const std::shared_ptr<MCMover>& shooter : shooters)
                {
                    held.insert(shooter->PartId);
                }
            }
        }

        return held;
    };

    // The enemy mechs sent in to finish a staged loss's mech still to land (part ids): the stand-off's re-orders
    // (Converge, CloseIn) leave them closing in.
    const auto Finishing = [&](int32_t frame)
    {
        std::set<int32_t> finishing;

        for (const auto& [loss, enemies] : approaching)
        {
            if (const int32_t due = std::get<0>(loss);
                frame > due - ApproachFor(std::get<2>(loss)) && frame <= due + HitGraceFrames && !landed.contains(loss))
            {
                for (const std::shared_ptr<MCMover>& enemy : enemies)
                {
                    finishing.insert(enemy->PartId);
                }
            }
        }

        return finishing;
    };

    // Its mech fired at for staged damage, from its lead (LeadFor) before it is due until it lands, the nearly recycled
    // weapons that reach it of the units nearest it readied every LossVolleyFrames: the ApproachingMechs enemy mechs
    // nearest it are sent at it before that (ApproachFor; the nearest set down within its reach if it is further and
    // neither is on screen), and the ReadiedShooters units nearest it hold their fire from then until the fire starts,
    // so they have weapons recycled for it (but those wearing a mech down: their fire lands hits anyway).
    const auto FireFor =
        [&](const std::shared_ptr<MCMover>& victim, int32_t frame, int32_t due, int32_t mech, char kind)
    {
        if (due < 0 || landed.contains({due, mech, kind}))
        {
            return;
        }

        const int32_t lead = LeadFor(kind);
        const int32_t approach = ApproachFor(kind);

        if (frame == due - approach)
        {
            const std::vector<std::shared_ptr<MCMover>> nearest = EnemyMechsByDistance(*victim);

            // The nearest not where all its weapons reach: set down there, unless either is on screen.
            if (!nearest.empty())
            {
                const MCVector3D at = victim->GetPosition();
                const MCVector3D from = nearest.front()->GetPosition();

                if (std::hypot(at.X - from.X, at.Y - from.Y) > WeaponsReach(*nearest.front()) * WorldUnitsPerMeter &&
                    !EitherOnScreen(*nearest.front(), *victim))
                {
                    std::cout << std::format("  frame {}: {} brought {:.0f} m from {}\n", frame,
                                             nearest.front()->GetIfaceName(),
                                             BringWithinReach(*nearest.front(), *victim, true), victim->GetIfaceName());
                }
            }

            for (const std::shared_ptr<MCMover>& enemy : nearest | std::views::take(ApproachingMechs))
            {
                OrderAttack(*enemy, victim, MCOrderOrigin::Commander);
                approaching[{due, mech, kind}].push_back(enemy);
            }

            std::ranges::copy(EnemyMechsByDistance(*victim, true) | std::views::take(ReadiedShooters),
                              std::back_inserter(holding[{due, mech, kind}]));
        }

        // Those held hold their fire until the last FireLeadFrames (a death's earlier fire is the others').
        for (const std::shared_ptr<MCMover>& shooter : holding[{due, mech, kind}])
        {
            if (frame > due - approach && frame < due - FireLeadFrames && shooter->IsDisabled() == 0 &&
                !wearing.contains(shooter->PartId))
            {
                HoldFire(*shooter);
            }
        }

        if (frame < due - lead || frame > due + HitGraceFrames)
        {
            return;
        }

        const bool volley = (frame - (due - lead)) % LossVolleyFrames == 0;
        std::set<int32_t> skip = Holding(frame);

        if (frame < due - FireLeadFrames)
        {
            for (const std::shared_ptr<MCMover>& shooter : holding[{due, mech, kind}])
            {
                skip.insert(shooter->PartId);
            }
        }

        std::vector<std::string> names;
        int32_t fired =
            FireAt(victim, volley ? ReadiedShooters : 0, readiedAt, ShooterRestSeconds, skip, std::nullopt, names);

        // None that has rested reaches it: those that do, readied again sooner (their weapons still nearly recycled).
        if (volley && fired == 0)
        {
            fired = FireAt(victim, ReadiedShooters, readiedAt, 0.0f, skip, std::nullopt, names);
        }

        if (volley)
        {
            const std::vector<std::shared_ptr<MCMover>> nearest = EnemyMechsByDistance(*victim);
            const MCVector3D at = victim->GetPosition();
            const MCVector3D from = nearest.empty() ? at : nearest.front()->GetPosition();
            std::string fire;

            for (const std::string& name : names)
            {
                fire += std::format("{}{}", fire.empty() ? "" : ", ", name);
            }

            std::cout << std::format(
                "  frame {}: {} weapons fire at {} (part {:#x}), the nearest enemy mech {:.0f} m off ({})\n", frame,
                fired, victim->GetIfaceName(), victim->PartId,
                std::hypot(at.X - from.X, at.Y - from.Y) / WorldUnitsPerMeter, fire);
        }
    };

    // When the camera came to the mech it follows: the last --showcase-camera's frame (0 before any).
    const auto CameraSince = [&](int32_t frame)
    {
        int32_t since = 0;

        for (const MCShowcaseEvent& camera : cameras)
        {
            since = camera.Frame <= frame && ForceMech(camera.Mech) != nullptr ? camera.Frame : since;
        }

        return since;
    };

    // The mech the camera follows: the last --showcase-camera's from before this frame (fallen or not), else the
    // first still fighting.
    const auto CameraMech = [&](int32_t frame, const std::vector<std::shared_ptr<MCMover>>& lance)
    {
        std::shared_ptr<MCMover> followed = lance.empty() ? nullptr : lance.front();

        for (const MCShowcaseEvent& camera : cameras)
        {
            if (camera.Frame <= frame && ForceMech(camera.Mech) != nullptr)
            {
                followed = ForceMech(camera.Mech);
            }
        }

        return followed;
    };

    for (int32_t frame = 0; frame < last; ++frame)
    {
        if (!snowLeast.empty() && frame == snowLeast.front())
        {
            MCWeatherPacing::Hold(MCWeatherPace::LeastShare);
        }
        else if (snowLeast.size() > 1 && frame == snowLeast[1])
        {
            MCWeatherPacing::Hold(1.0f);
        }

        const bool down = target != nullptr && target->IsDisabled() != 0;
        fallen += down ? 1 : 0;
        const std::vector<std::shared_ptr<MCMover>> lance = LiveLance();
        const bool walking = frame < walkFrame;
        const std::shared_ptr<MCMover> followed = CameraMech(frame, lance);
        const std::set<int32_t> running = Running(frame);

        // Those the last frame hit.
        hit.clear();

        for (const auto& [partId, before] : toughness)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && Toughness(*mover) < before)
            {
                hit.insert(partId);
            }
        }

        // The staged losses, each on a hit (each hit on a mech with one to come printed, once a frame).
        std::set<int32_t> hitShown;

        for (const MCShowcaseEvent& loss : losses)
        {
            const std::shared_ptr<MCMover> mover = ForceMech(loss.Mech);

            if (mover == nullptr || mover->IsDisabled() != 0 || mover->ObjectClass != MCObjectClass::BattleMech)
            {
                continue;
            }

            auto& mech = static_cast<MCBattleMech&>(*mover);
            const char kind = loss.What.empty() ? 'd' : loss.What.front();

            if (hit.contains(mech.PartId) && frame >= loss.Frame - LeadFor(kind) &&
                frame <= loss.Frame + HitGraceFrames && !landed.contains({loss.Frame, loss.Mech, kind}) &&
                hitShown.insert(mech.PartId).second)
            {
                std::cout << std::format("  frame {}: {} (part {:#x}) hit\n", frame, mech.GetIfaceName(), mech.PartId);
            }

            FireFor(mover, frame, loss.Frame, loss.Mech, kind);

            if (loss.What == "leg" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "loses a leg"))
            {
                mech.DestroyBodyLocation(MechLeftLeg);
                crippledAt[mech.PartId] = frame;
            }
            else if (loss.What == "arm" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "loses an arm"))
            {
                mech.DestroyBodyLocation(armsLost[mech.PartId]++ == 0 ? MechLeftArm : MechRightArm);
            }
            else if (loss.What == "eject" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "ejects"))
            {
                mech.HandleEjection();
            }
            else if (loss.What == "die" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "blown apart"))
            {
                mech.DestroyBodyLocation(MechCenterTorso);
            }
            else if (loss.What == "hit")
            {
                Lands(mech, frame, loss.Frame, loss.Mech, kind, "under fire");
            }
        }

        // Those being worn down: each one's enemy mechs kept on it, the nearest not on another filling its empty places
        // (the one to stop first first), every WearFrames.
        if (frame % WearFrames == 0)
        {
            std::set<int32_t> sent = Runners(frame);
            std::vector<std::pair<int32_t, size_t>> worns;

            for (size_t i = 0; i < wears.size(); ++i)
            {
                const std::shared_ptr<MCMover> mech = ForceMech(wears[i].Mech);
                std::vector<std::shared_ptr<MCMover>>& shooters = wearSent[i];

                if (mech == nullptr || mech->IsDisabled() != 0 || frame < wears[i].Frame ||
                    frame >= WearUntil(wears[i]))
                {
                    shooters.clear();
                    continue;
                }

                std::erase_if(shooters, [](const std::shared_ptr<MCMover>& enemy) { return enemy->IsDisabled() != 0; });
                worns.emplace_back(WearUntil(wears[i]), i);

                for (const std::shared_ptr<MCMover>& enemy : shooters)
                {
                    sent.insert(enemy->PartId);
                }
            }

            std::ranges::sort(worns);
            wearing.clear();

            for (const auto& [until, i] : worns)
            {
                const std::shared_ptr<MCMover> mech = ForceMech(wears[i].Mech);
                std::vector<std::shared_ptr<MCMover>>& shooters = wearSent[i];

                for (const std::shared_ptr<MCMover>& enemy : EnemyMechsByDistance(*mech))
                {
                    if (shooters.size() < WearShooters && sent.insert(enemy->PartId).second)
                    {
                        shooters.push_back(enemy);
                    }
                }

                for (const std::shared_ptr<MCMover>& enemy : shooters)
                {
                    OrderAttack(*enemy, mech, MCOrderOrigin::Commander);
                    wearing.insert(enemy->PartId);
                }

                if (frame < wears[i].Frame + WearFrames)
                {
                    std::cout << std::format("  frame {}: {} worn down by {} enemy mechs\n", frame,
                                             mech->GetIfaceName(), shooters.size());
                }
            }
        }

        // While a worn mech's bar is above its wear's (WearBar): its enemy mechs set down within reach once neither is
        // on screen, and what reaches it firing at it; its own shooters hold their fire between (their own shots,
        // blocked or missing, would leave them nothing recycled for it).
        for (size_t i = 0; i < wears.size(); ++i)
        {
            const MCShowcaseEvent& wear = wears[i];
            const std::shared_ptr<MCMover> mech = ForceMech(wear.Mech);

            if (mech == nullptr || mech->IsDisabled() != 0 || frame < wear.Frame || frame >= WearUntil(wear) ||
                mech->GetTotalEffectiveness() <= WearBar(wear, frame))
            {
                continue;
            }

            const MCVector3D at = mech->GetPosition();

            for (const std::shared_ptr<MCMover>& enemy : wearSent[i])
            {
                HoldFire(*enemy);
                const MCVector3D from = enemy->GetPosition();

                if (std::hypot(at.X - from.X, at.Y - from.Y) > WeaponsReach(*enemy) * WorldUnitsPerMeter * WearSlack &&
                    !EitherOnScreen(*enemy, *mech))
                {
                    std::cout << std::format("  frame {}: {} brought {:.0f} m from {}\n", frame, enemy->GetIfaceName(),
                                             BringWithinReach(*enemy, *mech, true), mech->GetIfaceName());
                }
            }

            if (frame >= wear.Frame + WearLeadFrames && (frame - wear.Frame) % LossVolleyFrames == 0)
            {
                if (const int32_t fired =
                        FireAt(mech, WearReadied, readiedAt, ShooterRestSeconds, HeldForLosses(frame));
                    fired > 0)
                {
                    std::cout << std::format("  frame {}: {} weapons wear {} down\n", frame, fired,
                                             mech->GetIfaceName());
                }
            }
        }

        // A mech without its leg turns on the nearest enemy mech.
        for (const auto& [partId, at] : crippledAt)
        {
            const std::shared_ptr<MCMover> mech = GetMoverFromPartId(partId);

            if (frame == at + 1 && mech != nullptr && mech->IsDisabled() == 0)
            {
                if (const std::shared_ptr<MCMover> enemy = NearestEnemyMech(*mech); enemy != nullptr)
                {
                    OrderAttack(*mech, enemy);
                }
            }
        }

        // Those running, ordered on every second.
        for (const MCShowcaseEvent& run : runs)
        {
            if (const std::shared_ptr<MCMover> mech = ForceMech(run.Mech);
                mech != nullptr && running.contains(mech->PartId) && (frame - run.Frame) % RetreatFrames == 0)
            {
                RetreatFrom(*mech, RetreatDistance);

                if (frame == run.Frame)
                {
                    std::cout << std::format("  frame {}: {} runs\n", frame, mech->GetIfaceName());
                }
            }
        }

        // The last --showcase-close's mech, those named closing in on it (on the camera's once it falls).
        MCOptionalRef<const MCShowcaseEvent> close;

        for (const MCShowcaseEvent& event : closes)
        {
            if (event.Frame <= frame)
            {
                close = event;
            }
        }

        std::shared_ptr<MCMover> closeOn = close.has_value() ? ForceMech(close->get().Mech) : nullptr;
        closeOn = closeOn != nullptr && closeOn->IsDisabled() == 0 ? closeOn : followed;
        const bool closing =
            close.has_value() && closeNames.has_value() && closeOn != nullptr && closeOn->IsDisabled() == 0;

        if (closing && (frame - close->get().Frame) % CloseFrames == 0)
        {
            // Those wearing a mech down stay on it.
            std::set<int32_t> busy = Runners(frame);
            busy.merge(Finishing(frame));
            busy.insert(wearing.begin(), wearing.end());
            // Its what: how many to bring and, after an @, how far behind (meters): n or n@meters.
            const size_t split = close->get().What.find('@');
            const int32_t bring = close->get().What.empty() ? Brought : std::stoi(close->get().What.substr(0, split));
            const float behind =
                split == std::string::npos ? BehindMeters : std::strtof(close->get().What.c_str() + split + 1, nullptr);

            if (frame == close->get().Frame && bring > 0)
            {
                brought.clear();
                broughtFrame = frame;
            }

            const int32_t closed = CloseIn(*closeNames, closeOn, frame == close->get().Frame ? bring : 0, busy, behind,
                                           brought, CameraSince(frame) == frame);

            if (frame == close->get().Frame)
            {
                std::cout << std::format("  frame {}: {} enemies close in on {}, {} of the player's mechs left\n",
                                         frame, closed, closeOn->GetIfaceName(), lance.size());
            }
        }

        for (const MCShowcaseEvent& volley : volleys)
        {
            // The weapons of those near it readied at once when it starts, so they fire together (missiles can't
            // fire inside their minimum range, so not only those).
            if (const std::shared_ptr<MCMover> mech = ForceMech(volley.Mech);
                mech != nullptr && mech->IsDisabled() == 0 && frame >= volley.Frame &&
                frame < volley.Frame + VolleyFrames)
            {
                if (const int32_t fired = FireAt(mech, frame == volley.Frame ? VolleyShooters : 0, readiedAt, 0.0f);
                    fired > 0)
                {
                    std::cout << std::format("  frame {}: {} enemy weapons fire at {}\n", frame, fired,
                                             mech->GetIfaceName());
                }
            }
        }

        // With losses staged, the force holds until its staged end: the hits show, none falls before.
        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            if (losses.empty())
            {
                break;
            }

            HoldWorn(*mover);
        }

        // With the force answering, the other side's mechs hold the same way: the force's hits show on their bars, but
        // none falls before a strike or a kill stages its end.
        if (answerFrame >= 0 && frame >= answerFrame && !lance.empty())
        {
            for (const std::shared_ptr<MCMover>& enemy : EnemyMechsByDistance(*lance.front()))
            {
                HoldWorn(*enemy);
            }
        }

        const bool retarget = frame % RetargetFrames == 0 || (walkFrame > 0 && frame == walkFrame);

        if (target == nullptr || down || retarget)
        {
            if (std::shared_ptr<MCMover> next = NearestEnemy(); next != nullptr)
            {
                // The fight starts on the contact.
                const bool contactFirst =
                    frame == walkFrame && contactMech != nullptr && contactMech->IsDisabled() == 0;
                target = contactFirst ? contactMech : std::move(next);

                // Halted from just before the contact until the fight.
                const bool halted = contactFrame >= 0 && frame >= contactFrame - HaltLeadFrames;

                if (!walking)
                {
                    SendAllAt(target, frame, !contactFirst);

                    // A mech holding its fire for a strike stands, then keeps its weapons' reach of the strike's target
                    // once that one runs in (every weapon fires there: none inside its minimum range).
                    for (const MCShowcaseEvent& alpha : alphas)
                    {
                        const std::shared_ptr<MCMover> striker = ForceMech(alpha.Mech);

                        if (striker == nullptr || striker->IsDisabled() != 0 || frame < AlphaHoldFrom(alpha) ||
                            frame >= alpha.Frame)
                        {
                            continue;
                        }

                        if (const auto runner = alphaRunners.find(alpha.Frame); runner != alphaRunners.end() &&
                                                                                runner->second != nullptr &&
                                                                                runner->second->IsDisabled() == 0)
                        {
                            Engage(*striker, runner->second, WeaponsReach(*striker), MCOrderOrigin::Player);
                        }
                        else
                        {
                            WalkTo(*striker, striker->GetPosition());
                        }
                    }
                }
                else if (!halted)
                {
                    WalkAllAt(target);
                }
            }
        }

        if (contactFrame >= 0 && frame == contactFrame - HaltLeadFrames)
        {
            HaltAll();
        }

        // The contact: the nearest enemy mech of those converging set down ahead of the first mech, on the way to the
        // target, on its sensors and out of the force's sight, walking in to stand off; the force halts.
        if (frame == contactFrame && target != nullptr && !lance.empty())
        {
            MCMover& lead = *lance.front();
            contactMech = NearestEnemyMech(lead, false, converge.value_or(std::string_view()));

            if (contactMech != nullptr)
            {
                const MCVector3D at = lead.GetPosition();
                const MCVector3D to = target->GetPosition();
                const float dx = to.X - at.X;
                const float dy = to.Y - at.Y;
                const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
                // Sensor ranges are meters, positions world units.
                const float sensors = Sensors(lance) * WorldUnitsPerMeter;
                const auto Ahead = [&](float reach)
                { return MCVector3D(at.X + dx / length * reach, at.Y + dy / length * reach, at.Z); };
                // The fog's edge, and the nearest spot along the line that far from the force.
                const std::shared_ptr<MCTeam> team = lead.Team.lock();
                float edge = 0.0f;

                while (team->LineOfSight(Ahead(edge)) != 0 && edge < sensors)
                {
                    edge += ContactTry;
                }

                contactEdge = NearestOfForce(lance, Ahead(edge)).second;
                const auto Where = [&](float away)
                {
                    float reach = 0.0f;

                    while (NearestOfForce(lance, Ahead(reach)).second < away && reach < sensors * 2.0f)
                    {
                        reach += ContactTry;
                    }

                    return Ahead(reach);
                };

                const MCVector3D stand = Where(contactEdge * ContactOutside);
                MCVector3D place = Where(std::min(contactEdge * ContactFrom, sensors));
                contactMech->SetPosition(place);
                WalkTo(*contactMech, stand, MCOrderOrigin::Commander);
                contactInSight = Where(contactEdge * ContactStop);
                const MCVector3D front = NearestOfForce(lance, stand).first->GetPosition();
                watchAt = MCVector3D(front.X + (stand.X - front.X) * WatchShare,
                                     front.Y + (stand.Y - front.Y) * WatchShare, front.Z);
                std::cout << std::format(
                    "  frame {}: contact {} (part {:#x}) {:.0f} m from the force, to stand {:.0f} m "
                    "from it (it sees {:.0f} m, its sensors reach {:.0f} m)\n",
                    frame, contactMech->GetIfaceName(), contactMech->PartId,
                    NearestOfForce(lance, place).second / WorldUnitsPerMeter,
                    NearestOfForce(lance, stand).second / WorldUnitsPerMeter, contactEdge / WorldUnitsPerMeter,
                    sensors / WorldUnitsPerMeter);
            }
        }

        // It steps into sight before the fight.
        if (contactMech != nullptr && contactMech->IsDisabled() == 0 && frame == walkFrame - contactStep)
        {
            WalkTo(*contactMech, contactInSight, MCOrderOrigin::Commander);
        }

        // How the force has the contact, when that changes: on its sensors (a blip), in sight.
        if (contactMech != nullptr && !lance.empty())
        {
            if (const int32_t type = lance.front()->Team.lock()->GetContactType(*contactMech); type != contactType)
            {
                contactType = type;
                std::cout << std::format("  frame {}: the contact is {}, {:.0f} m from the force\n", frame,
                                         type == 1   ? "in sight"
                                         : type == 2 ? "on the sensors"
                                                     : "unseen",
                                         NearestOfForce(lance, contactMech->GetPosition()).second / WorldUnitsPerMeter);
            }
        }

        // Until then the rest of the other side's units are set back off the force's sensors.
        if (walking && contactFrame >= 0 && !lance.empty())
        {
            const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
            const float clear = Sensors(lance) * WorldUnitsPerMeter * SensorClear;

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
            {
                const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

                if (enemy == nullptr || enemy == contactMech || enemy->Team.lock() == team || enemy->IsDisabled() != 0)
                {
                    continue;
                }

                const MCVector3D to = enemy->GetPosition();

                if (const auto [mech, away] = NearestOfForce(lance, to); away < clear)
                {
                    const MCVector3D at = mech->GetPosition();
                    const float scale = clear / std::max(away, 1.0f);
                    MCVector3D place(at.X + (to.X - at.X) * scale, at.Y + (to.Y - at.Y) * scale, to.Z);
                    enemy->SetPosition(place);
                }
            }
        }

        // While the force walks, and once it is gone, no unit fires (its weapons come ready a moment after).
        const bool ceasefire = walking || (frame > walkFrame && lance.empty());

        for (int32_t partId = MCMover::FirstPartId; ceasefire && partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId); mover != nullptr)
            {
                HoldFire(*mover);
            }
        }

        // (Those closing in get their own orders.)
        if (converge.has_value() && retarget && !walking && !closing)
        {
            // At the fight's start those further off are set down nearer, unseen, to come in.
            if (frame == walkFrame && contactEdge > 0.0f && !lance.empty())
            {
                const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
                const float from = contactEdge * ConvergeFrom;

                for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
                {
                    const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

                    if (enemy == nullptr || enemy == contactMech || enemy->Team.lock() == team ||
                        enemy->IsDisabled() != 0 || !Named(*enemy, *converge))
                    {
                        continue;
                    }

                    const MCVector3D to = enemy->GetPosition();

                    if (const auto [mech, away] = NearestOfForce(lance, to); away > from)
                    {
                        const MCVector3D at = mech->GetPosition();
                        const float scale = from / away;
                        MCVector3D place(at.X + (to.X - at.X) * scale, at.Y + (to.Y - at.Y) * scale, to.Z);
                        enemy->SetPosition(place);
                    }
                }
            }

            std::set<int32_t> busy = Runners(frame);
            busy.merge(Finishing(frame));
            busy.insert(wearing.begin(), wearing.end());
            const int32_t ordered = Converge(*converge, lance, busy);

            if (frame == walkFrame)
            {
                std::cout << std::format("  {} enemies sent at the force\n", ordered);
            }
        }

        // A mech that has landed goes back to the attack.
        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            const bool jumping = Jumping(*mover);

            if (wasJumping[mover->PartId] && !jumping && target != nullptr && !running.contains(mover->PartId) &&
                !MineWalking(frame).contains(mover->PartId))
            {
                Engage(*mover, target, LineMeters, MCOrderOrigin::Player);
            }

            wasJumping[mover->PartId] = jumping;
        }

        // A death from above: the mech jumps at the enemy mech nearest it that is far enough to jump at.
        for (const MCShowcaseEvent& dfa : dfas)
        {
            const std::shared_ptr<MCMover> mech = ForceMech(dfa.Mech);

            if (frame != dfa.Frame || mech == nullptr || mech->IsDisabled() != 0)
            {
                continue;
            }

            const std::vector<std::shared_ptr<MCMover>> enemies = EnemyMechsByDistance(*mech);
            const auto enemy =
                std::ranges::find_if(enemies, [&](const std::shared_ptr<MCMover>& at) { return JumpAt(*mech, *at); });

            if (enemy != enemies.end())
            {
                ++jumps;
                std::cout << std::format("  frame {}: {} jumps at {}\n", frame, mech->GetIfaceName(),
                                         (*enemy)->GetIfaceName());
            }
            else
            {
                std::cout << std::format("  frame {}: {} has nothing to jump at\n", frame, mech->GetIfaceName());
            }
        }

        const bool lanceJump = (jumpEvery > 0 && frame % jumpEvery == 0) || std::ranges::contains(jumpFrames, frame);

        if (target != nullptr && lanceJump)
        {
            for (const std::shared_ptr<MCMover>& mover : lance)
            {
                if (JumpAt(*mover, *target))
                {
                    ++jumps;
                    std::cout << std::format("  frame {}: {} jumps\n", frame, mover->GetIfaceName());
                }
            }
        }

        if (target != nullptr && std::ranges::contains(strikes, frame))
        {
            const std::shared_ptr<MCCommander> commander = HomeCommander();
            commander->SetNumLargeStrikes(commander->NumLargeStrikes + 1);
            CallArtillery(commander->Id, LargeStrike, target->GetPosition(), StrikeDelaySeconds, 0);
            std::cout << std::format("  frame {}: strike on {}\n", frame, target->GetIfaceName());
        }

        // The other side's strike, called by the commander of the nearest enemy of the mech named (else the first),
        // landing on it and the mech nearest it.
        std::shared_ptr<MCMover> under;

        if (const auto called = std::ranges::find(incoming, frame, &MCShowcaseEvent::Frame);
            called != incoming.end() && !lance.empty())
        {
            const std::shared_ptr<MCMover> named = called->Mech >= 0 ? ForceMech(called->Mech) : nullptr;
            under = named != nullptr && named->IsDisabled() == 0 ? named : lance.front();
        }

        if (under != nullptr)
        {
            MCMover& beside = *under;

            if (const std::shared_ptr<MCMover> enemy = NearestEnemyMech(beside, true); enemy != nullptr)
            {
                const MCVector3D at = beside.GetPosition();
                const MCVector3D to = enemy->GetPosition();
                const float dx = to.X - at.X;
                const float dy = to.Y - at.Y;
                const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
                const std::shared_ptr<MCCommander> commander = CommanderById(enemy->GetCommanderId());
                commander->SetNumLargeStrikes(commander->NumLargeStrikes + 1);
                CallArtillery(
                    commander->Id, LargeStrike,
                    MCVector3D(at.X + dx / length * IncomingOffset, at.Y + dy / length * IncomingOffset, at.Z),
                    IncomingSeconds, 0);

                // A strike's one frame of collisions as it lands is its damage: the strike just called (the newest
                // in the world) is moved onto the force, between the mech and its nearest mate (aimed at both), on
                // open ground level with them (else the nearest that is). The force is held, so it wears their bars.
                std::shared_ptr<MCGameObject> strike;

                for (const std::shared_ptr<MCBaseObject>& object : ObjectList()->DefaultList())
                {
                    strike = object->ObjectClass == MCObjectClass::Artillery
                                 ? std::static_pointer_cast<MCGameObject>(object)
                                 : strike;
                }

                std::shared_ptr<MCMover> mate;

                for (const std::shared_ptr<MCMover>& mover : lance)
                {
                    const MCVector3D other = mover->GetPosition();
                    mate = mover != under && (mate == nullptr || std::hypot(other.X - at.X, other.Y - at.Y) <
                                                                     std::hypot(mate->GetPosition().X - at.X,
                                                                                mate->GetPosition().Y - at.Y))
                               ? mover
                               : mate;
                }

                const MCVector3D with = mate != nullptr ? mate->GetPosition() : at;
                MCVector3D place = NearestOpenAndLevel(MCVector3D((at.X + with.X) * 0.5f, (at.Y + with.Y) * 0.5f, at.Z),
                                                       GameMap().GetTerrainElevation(at), IncomingSearchMeters);

                if (strike != nullptr)
                {
                    strike->SetPosition(place);
                }

                std::cout << std::format("  frame {}: {}'s side strikes {:.0f} m from {}, by {}\n", frame,
                                         enemy->GetIfaceName(),
                                         std::hypot(place.X - at.X, place.Y - at.Y) / WorldUnitsPerMeter,
                                         beside.GetIfaceName(), mate != nullptr ? mate->GetIfaceName() : "itself");
            }
        }

        // The forests nearest the mech set alight: the game's own fire, and its smoke all around the mech.
        for (const MCShowcaseEvent& smoke : smokes)
        {
            const std::shared_ptr<MCMover> mover = ForceMech(smoke.Mech);

            if (frame != smoke.Frame || mover == nullptr || mover->IsDisabled() != 0)
            {
                continue;
            }

            const MCVector3D at = mover->GetPosition();
            std::vector<std::pair<float, std::shared_ptr<MCMiscTerrainObject>>> forests;
            // The nearest forest at any distance, for the log (a mission's ground may have none in reach).
            float nearest = std::numeric_limits<float>::max();

            for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
            {
                for (const std::shared_ptr<MCBaseObject> object : *list)
                {
                    if (object->ObjectClass != MCObjectClass::MiscTerrainObject)
                    {
                        continue;
                    }

                    const auto forest = std::static_pointer_cast<MCMiscTerrainObject>(object);
                    const MCVector3D where = forest->GetPosition();

                    const float meters = std::hypot(where.X - at.X, where.Y - at.Y) / WorldUnitsPerMeter;

                    if (forest->Kind != MCMiscTerrainKind::Forest)
                    {
                        continue;
                    }

                    nearest = std::min(nearest, meters);

                    if (meters <= SmokeTreeMeters)
                    {
                        forests.emplace_back(meters, forest);
                    }
                }
            }

            std::ranges::sort(forests, {}, &std::pair<float, std::shared_ptr<MCMiscTerrainObject>>::first);
            size_t alight = 0;

            for (const auto& [meters, forest] : forests | std::views::take(SmokeTrees))
            {
                forest->LightOnFire(SmokeSeconds);
                alight += forest->FireObject != nullptr ? 1 : 0;
            }

            std::cout << std::format("  frame {}: {} smokes ({} forests alight, the nearest forest {:.0f} m off)\n",
                                     frame, mover->GetIfaceName(), alight, nearest);
        }

        // Where the force is, kept.
        if (frame % TrailFrames == 0)
        {
            for (const std::shared_ptr<MCMover>& mover : lance)
            {
                trail.push_back(mover->GetPosition());
            }
        }

        // A minefield's mech walks onto fresh ground, and later backs out of it.
        for (const auto& [first, last] : minefields)
        {
            const int32_t lead = mines[first].What.empty() ? MineWalkFrames : std::stoi(mines[first].What);

            if (const std::shared_ptr<MCMover> mech = ForceMech(mines[first].Mech);
                frame == mines[first].Frame - lead && mech != nullptr && mech->IsDisabled() == 0)
            {
                MCVector3D at = mech->GetPosition();
                const std::optional<std::pair<MCVector3D, float>> ground = FreshGround(*mech, lead);
                MCVector3D to = ground.has_value() ? ground->first : at;
                WalkTo(*mech, to);
                const float meters = std::hypot(to.X - at.X, to.Y - at.Y) / WorldUnitsPerMeter;
                const int32_t back = last + MineSettleFrames;
                mineWalks[mech->PartId] = {at, to, std::atan2(to.Y - at.Y, to.X - at.X), back,
                                           back + static_cast<int32_t>(meters / MineWalkPace * FramesPerSecond)};
                std::cout << std::format(
                    "  frame {}: {} walks {:.0f} m onto {}, turned {:.0f} degrees from its enemy\n", frame,
                    mech->GetIfaceName(), std::hypot(to.X - at.X, to.Y - at.Y) / WorldUnitsPerMeter,
                    ground.has_value() ? "fresh ground" : "no fresh ground (none near)",
                    ground.has_value() ? ground->second : 0.0f);
            }
        }

        for (auto& [partId, walk] : mineWalks)
        {
            if (const std::shared_ptr<MCMover> mech = GetMoverFromPartId(partId);
                frame == walk.Back && mech != nullptr && mech->IsDisabled() == 0)
            {
                WalkTo(*mech, walk.From);
                std::cout << std::format("  frame {}: {} backs out of the minefield\n", frame, mech->GetIfaceName());
            }
        }

        // A mine under the mech as it steps onto that ground: a step ahead of it, left and right in turn.
        for (size_t index = 0; index < mines.size(); ++index)
        {
            if (const std::shared_ptr<MCMover> mech = ForceMech(mines[index].Mech);
                frame == mines[index].Frame && mech != nullptr && mech->IsDisabled() == 0)
            {
                const auto walk = mineWalks.find(mech->PartId);
                const float heading = walk != mineWalks.end() ? walk->second.Heading : 0.0f;
                const float side = (index % 2 == 0 ? 1.0f : -1.0f) * MineSideMeters * WorldUnitsPerMeter;
                const float ahead = MineAheadMeters * WorldUnitsPerMeter;
                MCVector3D at = mech->GetPosition();
                at.X += std::cos(heading) * ahead - std::sin(heading) * side;
                at.Y += std::sin(heading) * ahead + std::cos(heading) * side;
                CreateExplosion(MineExplosion, at, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
                std::cout << std::format("  frame {}: a mine under {}\n", frame, mech->GetIfaceName());
            }
        }

        // An alpha strike: the enemy mech nearest the mech runs in to its reach AlphaApproachFrames before (held
        // standing until the strike), the mech holds its fire for AlphaHoldFrames, the enemy is brought in along its
        // line to within reach AlphaBringFrames before if still further (the camera elsewhere yet), and the mech turns
        // on it with every weapon that has (nearly) recycled, each fired as it comes to bear, the rest as they come
        // ready (the pilot fires each one as it comes ready, so they never go together otherwise).
        for (const MCShowcaseEvent& alpha : alphas)
        {
            const std::shared_ptr<MCMover> mech = ForceMech(alpha.Mech);

            if (mech == nullptr || mech->IsDisabled() != 0)
            {
                continue;
            }

            // The nearest enemy mech not sent in for another strike still to come, nor being finished.
            const auto Taken = [&](const std::shared_ptr<MCMover>& enemy)
            {
                return (enemy == killing && killing->IsDisabled() == 0) ||
                       std::ranges::any_of(
                           alphaRunners, [&](const auto& runner)
                           { return runner.first > frame && runner.first != alpha.Frame && runner.second == enemy; });
            };

            // Of the nearest few free no nearer than its weapons' minimum ranges (inside them they can't fire), the most
            // worn (an earned kill); else the nearest free.
            const std::vector<std::shared_ptr<MCMover>> enemies = EnemyMechsByDistance(*mech);
            const float tooNear = WeaponsBand(*mech).first * WorldUnitsPerMeter;
            const MCVector3D strikerAt = mech->GetPosition();
            std::vector<std::shared_ptr<MCMover>> free;

            for (const std::shared_ptr<MCMover>& enemy : enemies)
            {
                const MCVector3D to = enemy->GetPosition();

                if (free.size() < WornChoices && !Taken(enemy) &&
                    std::hypot(to.X - strikerAt.X, to.Y - strikerAt.Y) >= tooNear)
                {
                    free.push_back(enemy);
                }
            }

            if (free.empty())
            {
                if (const auto first = std::ranges::find_if(enemies, std::not_fn(Taken)); first != enemies.end())
                {
                    free.push_back(*first);
                }
            }

            std::ranges::stable_sort(free, {}, [](const std::shared_ptr<MCMover>& enemy)
                                     { return enemy->GetTotalEffectiveness(); });
            const std::shared_ptr<MCMover> nearest = free.empty() ? nullptr : free.front();

            if (frame == alpha.Frame - AlphaApproachFrames)
            {
                const std::shared_ptr<MCMover> enemy = nearest;

                if (enemy == nullptr)
                {
                    continue;
                }

                alphaRunners[alpha.Frame] = enemy;
                std::cout << std::format("  frame {}: {} (part {:#x}) runs in from {:.0f} m to {:.0f} m of {}\n", frame,
                                         enemy->GetIfaceName(), enemy->PartId, RunInto(*enemy, *mech),
                                         WeaponsReach(*mech), mech->GetIfaceName());
            }

            // Ordered on again every AlphaRunFrames (its own orders would turn it away), to the mech's reach as it is.
            if (const std::shared_ptr<MCMover> sent = alphaRunners[alpha.Frame];
                sent != nullptr && sent->IsDisabled() == 0 && frame > alpha.Frame - AlphaApproachFrames &&
                frame < alpha.Frame && (alpha.Frame - frame) % AlphaRunFrames == 0)
            {
                RunInto(*sent, *mech);
            }

            // The strike's target: the runner where every weapon of the mech reaches it on a clear line of fire (one
            // of its own in the way holds direct fire), else the nearest such enemy mech not sent in for another
            // strike; else those two where the weapons reach, clear or not; else the runner while it stands; else the
            // nearest.
            const auto Target = [&]
            {
                const auto Within = [&](const std::shared_ptr<MCMover>& enemy, bool clear)
                {
                    const auto [shortest, longest] = WeaponsBand(*mech);
                    const MCVector3D from = mech->GetPosition();
                    const MCVector3D to = enemy->GetPosition();
                    const float meters = std::hypot(to.X - from.X, to.Y - from.Y) / WorldUnitsPerMeter;
                    return meters <= longest && (meters >= shortest || shortest >= longest) &&
                           (!clear || mech->LineOfFire(*enemy) != 0);
                };

                const std::shared_ptr<MCMover> sent = alphaRunners[alpha.Frame];
                const bool standing = sent != nullptr && sent->IsDisabled() == 0;

                for (const bool clear : {true, false})
                {
                    if (standing && Within(sent, clear))
                    {
                        return sent;
                    }

                    if (const auto reached = std::ranges::find_if(enemies, [&](const std::shared_ptr<MCMover>& enemy)
                                                                  { return !Taken(enemy) && Within(enemy, clear); });
                        reached != enemies.end())
                    {
                        return *reached;
                    }
                }

                return standing ? sent : nearest;
            };

            // Held standing as the force's mechs are (HoldWorn: the hits it takes show), let go at the strike.
            if (const std::shared_ptr<MCMover> sent = alphaRunners[alpha.Frame];
                sent != nullptr && sent->IsDisabled() == 0 && frame < alpha.Frame)
            {
                HoldWorn(*sent);
            }
            else if (sent != nullptr && frame == alpha.Frame)
            {
                Release(*sent);
            }

            // Its fire held (AlphaHoldFrom).
            if (frame >= AlphaHoldFrom(alpha) && frame < alpha.Frame)
            {
                HoldFire(*mech);
            }

            // The target set down within reach when it is out of it (further than every weapon reaches, or, where the
            // weapons share a band, inside some's minimum range): the frame before the strike if neither shows, else on
            // the strike's own cut to the mech, which hides it.
            const auto BringIn = [&](MCMover& runner)
            {
                const MCVector3D from = mech->GetPosition();
                const MCVector3D to = runner.GetPosition();
                const float meters = std::hypot(to.X - from.X, to.Y - from.Y) / WorldUnitsPerMeter;
                const auto [shortest, longest] = WeaponsBand(*mech);

                if (meters > WeaponsReach(*mech) || (shortest < longest && meters < shortest))
                {
                    std::cout << std::format("  frame {}: {} brought {:.0f} m from {}\n", frame, runner.GetIfaceName(),
                                             BringWithinReach(*mech, runner), mech->GetIfaceName());
                }
            };

            const bool cutToMech = std::ranges::any_of(cameras, [&](const MCShowcaseEvent& camera)
                                                       { return camera.Frame == frame && camera.Mech == alpha.Mech; });

            if (const std::shared_ptr<MCMover> runner = frame == alpha.Frame - AlphaBringFrames ? Target() : nullptr;
                runner != nullptr && !EitherOnScreen(*runner, *mech))
            {
                BringIn(*runner);
            }
            else if (const std::shared_ptr<MCMover> late = frame == alpha.Frame && cutToMech ? Target() : nullptr;
                     late != nullptr)
            {
                BringIn(*late);
            }

            if (frame != alpha.Frame)
            {
                continue;
            }

            alphaShooter = mech;
            alphaTarget = Target();
            alphaFrame = frame;

            if (alphaTarget != nullptr)
            {
                const MCVector3D from = mech->GetPosition();
                const MCVector3D to = alphaTarget->GetPosition();
                std::cout << std::format("  frame {}: {} fires on {} {:.0f} m away\n", frame, mech->GetIfaceName(),
                                         alphaTarget->GetIfaceName(),
                                         std::hypot(to.X - from.X, to.Y - from.Y) / WorldUnitsPerMeter);

                for (int32_t i = 0; i < mech->NumWeapons; ++i)
                {
                    if (MCInventoryItem& item = mech->Inventory[mech->NumOther + i]; Recycled(item))
                    {
                        item.ReadyTime = 0.0f;
                    }
                }

                OrderAttack(*mech, alphaTarget);
                OrderAttack(*alphaTarget, mech, MCOrderOrigin::Commander);
            }
        }

        if (alphaTarget != nullptr && alphaShooter != nullptr && alphaShooter->IsDisabled() == 0 &&
            frame >= alphaFrame && frame < alphaFrame + AlphaFrames)
        {
            int32_t fired = 0;
            // Each weapon's answer (FireWeapon's: 0 fired; 3 not ready: disabled, or the seconds left of its recycle; 4
            // out of range, sight or ammunition: its shots left and range).
            std::string answers;

            for (int32_t i = 0; i < alphaShooter->NumWeapons; ++i)
            {
                const MCInventoryItem& item = alphaShooter->Inventory[alphaShooter->NumOther + i];
                const int32_t answer = StagedShot(*alphaShooter, alphaTarget, alphaShooter->NumOther + i);
                fired += answer == 0 ? 1 : 0;
                const MCMasterComponent& weapon = MasterComponentList[item.MasterID];
                answers += answer == 4   ? std::format(" 4({} shots, {:.0f}-{:.0f} m)",
                                                       alphaShooter->GetWeaponShots(alphaShooter->NumOther + i),
                                                       weapon.WeaponRange[0], weapon.WeaponRange[3])
                           : answer != 3 ? std::format(" {}", answer)
                           : item.Disabled != 0
                               ? std::string(" 3(disabled)")
                               : std::format(" 3({:.1f}/{:.1f}s)", item.ReadyTime - ScenarioTime, weapon.RecycleTime);
            }

            if (fired > 0 || frame == alphaFrame)
            {
                std::cout << std::format("  frame {}: {} fires {} weapons at {} (answers{})\n", frame,
                                         alphaShooter->GetIfaceName(), fired, alphaTarget->GetIfaceName(), answers);
            }
        }

        // An enemy mech blown apart on a hit: the alpha strike's target while it stands, else the one nearest the mech
        // named (the camera's) that the player's mechs reach, fired at first.
        for (const MCShowcaseEvent& destroy : destroys)
        {
            const std::shared_ptr<MCMover> named = destroy.Mech >= 0 ? ForceMech(destroy.Mech) : nullptr;
            const std::shared_ptr<MCMover> near = named != nullptr && named->IsDisabled() == 0 ? named : followed;

            if (frame == destroy.Frame - FireLeadFrames && near != nullptr)
            {
                killNear = near;
                killFrom = frame;
                killing = alphaTarget != nullptr && alphaTarget->IsDisabled() == 0 &&
                                  (named == nullptr || named == alphaShooter)
                              ? alphaTarget
                              : nullptr;

                // Of the nearest few, the most worn first.
                std::vector<std::shared_ptr<MCMover>> candidates = EnemyMechsByDistance(*near);
                candidates.resize(std::min(candidates.size(), WornChoices));
                std::ranges::stable_sort(candidates, {}, [](const std::shared_ptr<MCMover>& enemy)
                                         { return enemy->GetTotalEffectiveness(); });

                for (const std::shared_ptr<MCMover>& enemy : candidates)
                {
                    if (killing != nullptr)
                    {
                        break;
                    }

                    killing = FireAt(enemy, ReadiedShooters, readiedAt, ShooterRestSeconds, Holding(frame)) > 0
                                  ? enemy
                                  : nullptr;
                }

                // None in reach: the nearest brought within the mech's, unless either is on screen.
                if (killing == nullptr)
                {
                    killing = NearestEnemyMech(*near);

                    if (killing != nullptr && !EitherOnScreen(*killing, *near))
                    {
                        std::cout << std::format("  frame {}: {} brought {:.0f} m from {}\n", frame,
                                                 killing->GetIfaceName(), BringWithinReach(*near, *killing),
                                                 near->GetIfaceName());
                    }
                }
            }

            if (killing != nullptr && killing->IsDisabled() == 0 && killing->ObjectClass == MCObjectClass::BattleMech)
            {
                FireFor(killing, frame, destroy.Frame, -1, 'k');

                if (Lands(*killing, frame, destroy.Frame, -1, 'k', "blown apart"))
                {
                    static_cast<MCBattleMech&>(*killing).DestroyBodyLocation(MechCenterTorso);
                }
            }
        }

        // A kill: the enemy mech nearest the force weakened and made its target.
        if (!lance.empty() && std::ranges::contains(kills, frame))
        {
            if (std::shared_ptr<MCMover> mech = NearestEnemyMech(*lance.front()); mech != nullptr)
            {
                Weaken(*mech, KillStructure);
                target = std::move(mech);
                SendAllAt(target, frame, false);
                std::cout << std::format("  frame {}: {} (part {:#x}) weakened\n", frame, target->GetIfaceName(),
                                         target->PartId);
            }
        }

        // Who falls, and when.
        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover != nullptr && mover->IsDisabled() != 0 && !fell.contains(partId))
            {
                fell.insert(partId);

                if (frame > 0)
                {
                    std::cout << std::format("  frame {}: {} (part {:#x}) down\n", frame, mover->GetIfaceName(),
                                             partId);
                }

                // One of the force not by a staged loss: why.
                if (const std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
                    partId >= PlayerMechPartId && partId < PlayerMechPartId + PlayerMechs && pilot != nullptr &&
                    !std::ranges::any_of(
                        landed,
                        [&](const auto& staged)
                        {
                            const char kind = std::get<2>(staged);
                            return (kind == 'd' || kind == 'e') && ForceMech(std::get<1>(staged)) == mover;
                        }))
                {
                    std::string broken;

                    for (const MCInventoryItem& item : mover->Inventory)
                    {
                        broken += item.Health == 0 ? std::format(" {}", MasterComponentList[item.MasterID].Name) : "";
                    }

                    std::cout << std::format("    unstaged: status {}, pilot status {} wounds {:.1f}, broken:{}\n",
                                             mover->Status, pilot->Status, pilot->Wounds, broken);
                }
            }
        }

        // The camera on the contact being watched, else on the mech followed (or its wreck).
        const bool watching = watch.size() == 2 && frame >= watch[0] && frame < watch[1] && !lance.empty();
        // The second line brought in, those still standing, while they are framed coming.
        std::vector<MCVector3D> coming;

        if (broughtFrame >= 0 && frame < broughtFrame + BroughtFrames)
        {
            for (const std::shared_ptr<MCMover>& enemy : brought)
            {
                if (enemy->IsDisabled() == 0)
                {
                    coming.push_back(enemy->GetPosition());
                }
            }
        }

        // The watch's zoom, then the recording's again, each eased in over zoomEase frames from the last (one: a cut).
        if (watchZoom > 0 && watch.size() == 2)
        {
            const bool out = frame >= watch[0] && frame < watch[0] + zoomEase;
            const bool back = frame >= watch[1] && frame < watch[1] + zoomEase;

            if (out || back)
            {
                const float eased =
                    static_cast<float>(frame - (out ? watch[0] : watch[1]) + 1) / static_cast<float>(zoomEase);
                const float from = static_cast<float>(out ? zoom : watchZoom);
                const float to = static_cast<float>(out ? watchZoom : zoom);
                MCFixedZoomHeight = std::lerp(from, to, eased * eased * (3.0f - 2.0f * eased));
            }
        }

        const std::shared_ptr<MCMover> contact = !watching ? nullptr
                                                 : contactMech != nullptr && contactMech->IsDisabled() == 0
                                                     ? contactMech
                                                     : NearestEnemyMech(*lance.front());

        // The fog's edge where the watch holds while the force waits, then between the contact and the force's mech
        // nearest it, the fight's first shots.
        if (contact != nullptr)
        {
            const MCVector3D to = contact->GetPosition();
            const MCVector3D at = NearestOfForce(lance, to).first->GetPosition();
            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(walking && watchAt.has_value() ? *watchAt
                                                              : MCVector3D(at.X + (to.X - at.X) * FightShare,
                                                                           at.Y + (to.Y - at.Y) * FightShare, at.Z));
        }

        // A mech firing an alpha strike since the camera came to it: between it and its target (or the wreck).
        else if (followed != nullptr && followed == alphaShooter && alphaTarget != nullptr &&
                 alphaFrame >= CameraSince(frame))
        {
            const MCVector3D from = followed->GetPosition();
            const MCVector3D to = alphaTarget->GetPosition();
            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(
                MCVector3D(from.X + (to.X - from.X) * AlphaShare, from.Y + (to.Y - from.Y) * AlphaShare, from.Z));
        }

        // An enemy being finished near the mech followed: between them (or the wreck).
        else if (followed != nullptr && followed == killNear && killing != nullptr && frame >= killFrom &&
                 frame < killFrom + KillFrames)
        {
            const MCVector3D from = followed->GetPosition();
            const MCVector3D to = killing->GetPosition();
            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(
                MCVector3D(from.X + (to.X - from.X) * AlphaShare, from.Y + (to.Y - from.Y) * AlphaShare, from.Z));
        }

        // A second line brought in on the mech followed (or its wreck), walking in: between it and their middle.
        else if (followed != nullptr && followed == closeOn && !coming.empty())
        {
            const MCVector3D from = followed->GetPosition();
            MCVector3D to(0.0f, 0.0f, from.Z);

            for (const MCVector3D& at : coming)
            {
                to.X += at.X / static_cast<float>(coming.size());
                to.Y += at.Y / static_cast<float>(coming.size());
            }

            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(
                MCVector3D(from.X + (to.X - from.X) * AlphaShare, from.Y + (to.Y - from.Y) * AlphaShare, from.Z));
        }
        else if (followed != nullptr)
        {
            Eye()->ChangeTarget(followed, true);
        }

        // Every unit's toughness as the frame starts, to tell what it hits.
        toughness.clear();

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->IsDisabled() == 0)
            {
                toughness[partId] = Toughness(*mover);
            }
        }

        for (int64_t tick = static_cast<int64_t>(frame) * ResultsStepTicks / FramesPerSecond;
             tick < static_cast<int64_t>(frame + 1) * ResultsStepTicks / FramesPerSecond; ++tick)
        {
            MouseTimerTick();
        }

        MCTestGame::RunFrame(ShowcaseFrameSeconds);
        // The frame's share of the mix, its sample count rounded as the frames add up (1600 at 30 a second).
        const uint64_t until = static_cast<uint64_t>(frame + 1) * MCAudio::OutputRate / FramesPerSecond;
        mix.resize(static_cast<size_t>(until - rendered) * SoundChannels);
        sound->Mixer().Render(mix);
        rendered = until;

        for (const float sample : mix)
        {
            secondEnergy += static_cast<double>(sample) * sample;
        }

        // The force answers: from --showcase-answer's frame each of its mechs fires each weapon that has recycled (its
        // recycled weapons readied out of turn every AnswerRestSeconds, as the other side's staged fire has) at the
        // most worn enemy mech in the fight, focusing fire as a lance does, staged to land as the other side's staged
        // fire is (the game deals the damage); an enemy mech is left once its bar is down to AnswerFloor, so none falls
        // but on its cue (a strike's or a kill's), and a mech keeps its weapons for a strike of its own to come.
        if (answerFrame >= 0 && frame >= answerFrame && !walking && !lance.empty())
        {
            constexpr float AnswerFloor = 0.3f;
            constexpr float AnswerRestSeconds = 1.0f;
            // A strike's mech keeps its weapons for it this long before (they would not recycle in time).
            constexpr int32_t StrikeKeepFrames = 450;
            std::set<int32_t> holding = Holding(frame);

            for (const MCShowcaseEvent& alpha : alphas)
            {
                if (const std::shared_ptr<MCMover> striker = ForceMech(alpha.Mech);
                    striker != nullptr && frame >= alpha.Frame - StrikeKeepFrames && frame < alpha.Frame)
                {
                    holding.insert(striker->PartId);
                }
            }

            std::vector<std::shared_ptr<MCMover>> open = InFight(lance);
            std::erase_if(open, [](const std::shared_ptr<MCMover>& enemy)
                          { return enemy->GetTotalEffectiveness() <= AnswerFloor; });
            std::ranges::stable_sort(open, {}, [](const std::shared_ptr<MCMover>& enemy)
                                     { return enemy->GetTotalEffectiveness(); });

            for (const std::shared_ptr<MCMover>& mover : lance)
            {
                const MCVector3D at = mover->GetPosition();
                // In reach of its longest weapon (each shot is fired only where its own weapon reaches).
                const float reach = mover->GetFireRange(-2) * WorldUnitsPerMeter;
                const auto victim = std::ranges::find_if(open,
                                                         [&](const std::shared_ptr<MCMover>& enemy)
                                                         {
                                                             const MCVector3D to = enemy->GetPosition();
                                                             return std::hypot(to.X - at.X, to.Y - at.Y) <= reach;
                                                         });

                if (mover->IsDisabled() != 0 || victim == open.end() || holding.contains(mover->PartId))
                {
                    continue;
                }

                const auto last = answeredAt.find(mover->PartId);
                const bool rested = last == answeredAt.end() || ScenarioTime - last->second >= AnswerRestSeconds;

                for (int32_t i = 0; i < mover->NumWeapons; ++i)
                {
                    MCInventoryItem& item = mover->Inventory[mover->NumOther + i];

                    if (rested && item.Disabled == 0 && Recycled(item) && item.ReadyTime > ScenarioTime)
                    {
                        item.ReadyTime = ScenarioTime;
                        answeredAt[mover->PartId] = ScenarioTime;
                    }

                    if (item.Disabled == 0 && item.ReadyTime <= ScenarioTime &&
                        StagedShot(*mover, *victim, mover->NumOther + i) == 0)
                    {
                        ++answerShots;
                    }
                }
            }
        }

        // Each side's shots (a weapon whose recycle starts over: held fire only pushes it a moment on) and the armor and
        // structure the other side's mechs lose, counted each second for the fight's balance.
        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            constexpr float FreshRecycle = 1.0f;
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->ObjectClass != MCObjectClass::BattleMech || mover->IsDisabled() != 0)
            {
                continue;
            }

            const bool ours = partId >= PlayerMechPartId && partId < PlayerMechPartId + PlayerMechs;
            std::vector<float>& ready = readyTimes[partId];
            ready.resize(static_cast<size_t>(mover->NumWeapons), 0.0f);

            for (int32_t i = 0; i < mover->NumWeapons; ++i)
            {
                const float now = mover->Inventory[mover->NumOther + i].ReadyTime;
                const bool fired = now > ready[static_cast<size_t>(i)] && now - ScenarioTime > FreshRecycle;
                (ours ? forceShots : enemyShots) += fired ? 1 : 0;
                ready[static_cast<size_t>(i)] = now;
            }

            if (!ours)
            {
                const float left = Toughness(*mover);

                if (const auto last = enemyToughness.find(partId); last != enemyToughness.end() && left < last->second)
                {
                    enemyLost += last->second - left;
                }

                enemyToughness[partId] = left;
            }
        }

        if ((frame + 1) % FramesPerSecond == 0)
        {
            std::cout << std::format(
                "  second {} fire: force {} shots ({} answering), enemy {} shots; enemy mechs lost {:.0f}\n",
                (frame + 1) / FramesPerSecond, forceShots, answerShots, enemyShots, enemyLost);
            forceShots = 0;
            answerShots = 0;
            enemyShots = 0;
            enemyLost = 0.0f;
        }

        if ((frame + 1) % FramesPerSecond == 0)
        {
            constexpr double SilenceFloor = 1e-12;
            const double meanSquare = secondEnergy / (static_cast<double>(MCAudio::OutputRate) * SoundChannels);
            std::cout << std::format("  second {}: mix {:.1f} dB, state {:08x}{}\n", (frame + 1) / FramesPerSecond,
                                     10.0 * std::log10(meanSquare + SilenceFloor), MCTestGame::StateHash(),
                                     down ? ", target down" : "");

            // HD weather's showers: how hard the rain falls and how far its puddles have spread (MCWeather::ShowersAt).
            if (MCWeather::Shown() != MCWeatherKind::Clear)
            {
                const MCWeather::MCShowers showers = MCWeather::ShowersAt(MCWeather::Shown(), ScenarioTime);
                std::cout << std::format("  second {} weather: rain {:.3f} as made, puddles {:.3f} as made\n",
                                         (frame + 1) / FramesPerSecond, showers.Rain, showers.Puddles);
            }

            secondEnergy = 0.0;
            // The force's health bars, as the game measures them, and where each stands (world units).
            std::string bars;
            std::string places;

            for (const std::shared_ptr<MCMover>& mover : LiveLance())
            {
                bars += std::format("{}{} {:.2f}", bars.empty() ? "" : ", ", mover->GetIfaceName(),
                                    mover->GetTotalEffectiveness());
                const MCVector3D at = mover->GetPosition();
                places +=
                    std::format("{}{} {:.0f} {:.0f}", places.empty() ? "" : ", ", mover->GetIfaceName(), at.X, at.Y);
            }

            std::cout << std::format("  second {} bars: {}\n", (frame + 1) / FramesPerSecond, bars);
            std::cout << std::format("  second {} at: {}\n", (frame + 1) / FramesPerSecond, places);
            // The other side's mechs the same way, by part id: bar and place.
            std::string enemies;

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
            {
                const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

                if (enemy == nullptr || enemy->ObjectClass != MCObjectClass::BattleMech || enemy->IsDisabled() != 0 ||
                    (partId >= PlayerMechPartId && partId < PlayerMechPartId + PlayerMechs))
                {
                    continue;
                }

                const MCVector3D at = enemy->GetPosition();
                enemies += std::format("{}{:#x} {} {:.2f} {:.0f} {:.0f}", enemies.empty() ? "" : ", ", partId,
                                       enemy->GetIfaceName(), enemy->GetTotalEffectiveness(), at.X, at.Y);
            }

            std::cout << std::format("  second {} enemies: {}\n", (frame + 1) / FramesPerSecond, enemies);
        }

        // A scout's look every --showcase-peek <n> frames, to choose where the recording starts.
        if (scout && peek > 0 && frame % peek == 0)
        {
            std::filesystem::create_directories(folder / "peek");
            const std::filesystem::path picture = folder / "peek" / std::format("{:05}.png", frame);
            REQUIRE(MCInput::Display()->SaveShownScreenshot(picture).has_value());
        }

        if (scout || frame < from)
        {
            continue;
        }

        soundFile.write(reinterpret_cast<const char*>(mix.data()),
                        static_cast<std::streamsize>(mix.size() * sizeof(float)));

        if (!video)
        {
            const std::filesystem::path picture = folder / "frames" / std::format("{:05}.png", frame - from);
            REQUIRE(MCInput::Display()->SaveShownScreenshot(picture).has_value());
            continue;
        }

        auto shown = MCInput::Display()->ReadShownScreen();
        REQUIRE(shown.has_value());

        if (!pipe.has_value())
        {
            pipe.emplace(folder / "video.mp4", shown->Width, shown->Height, FramesPerSecond);
            REQUIRE(pipe->Open());
        }

        REQUIRE(pipe->Write(shown->Pixels));
    }

    std::cout << std::format("  frames {} to {} saved ({} HD), {} frames with the target down, {} jumps\n", from, last,
                             original ? "without" : "with", fallen, jumps);
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

    // As int64_t: MSVC counts an iota of size_t in a 128-bit type std::format doesn't take.
    const auto differing = [](const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    {
        return static_cast<int64_t>(
            std::ranges::count_if(std::views::iota(size_t{0}, a.size()),
                                  [&](size_t i) { return a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b; }));
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
    // Ten seconds: some 1,300 overlays are drawn from the pack by then.
    constexpr int32_t Frames = 150;

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
/// Port-only: a line drawn wide (VfxLineDrawWide) is that wide where HD art draws lines at the output's resolution, and
/// is the original's line with HD art off. A level line on mission 1's screen (shown larger), drawn the original's width
/// and three times it: with HD art off both change the same pixels to the same colours; with it on, the wide one changes
/// about three times as many.
/// </summary>
TEST_CASE_ISOLATED("game: HD art draws a wide line as wide as asked, and with HD art off the original's line")
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

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_wide_line");
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    constexpr int32_t Length = 60;
    constexpr int32_t Margin = 4;
    constexpr float Wide = 3.0f;
    constexpr uint8_t Color = 0x30;
    const MCRect box{x - Length - Margin, y - Margin, x + Length + Margin, y + Margin};
    const auto line = [&](float width)
    { return [&pane, x, y, width] { VfxLineDrawWide(pane, x - Length, y, x + Length, y, Color, width); }; };
    const auto same = [](const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    { return std::ranges::equal(a, b, SameColor); };

    GamePaused = true;
    const std::vector<SDL_Color> original = ChangedPixels(std::nullopt, box, line(1.0f));
    const std::vector<SDL_Color> originalWide = ChangedPixels(std::nullopt, box, line(Wide));
    const size_t smooth = ChangedPixels(pack, box, line(1.0f)).size();
    const size_t smoothWide = ChangedPixels(pack, box, line(Wide)).size();
    GamePaused = false;
    std::cout << std::format("  pixels changed: {} and {} wide with HD art off, {} and {} wide on\n", original.size(),
                             originalWide.size(), smooth, smoothWide);
    CHECK(!original.empty());
    CHECK(same(original, originalWide));
    REQUIRE(smooth > 0);
    CHECK(smoothWide >= smooth * 5 / 2);
    CHECK(smoothWide <= smooth * 7 / 2);
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
/// With HD art on, the tactical map's objective numbers are drawn bolder over a dark outline, and the units' dots are
/// still drawn over the objectives' dots, as the original's (a unit at an objective stays in sight). Mission 1's first
/// objective moved under a home mech, its markers lit, the map revealed: beside its dot, the outline's black and more of the number's
/// yellow show only with HD art on; at its middle, the mech's dot shows, not the objective's, either way.
/// </summary>
TEST_CASE_ISOLATED("game: HD art outlines the tactical map's objective numbers, under the units' dots")
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
    // The map's ground shown (the fog of war is black, as the outline).
    RevealAll();
    const std::shared_ptr<MCTeam> home = HomeTeam();
    REQUIRE(home->NumObjectives > 0);
    MCScenarioObjective& objective = Scenario()->Objectives[home->FirstObjective];
    REQUIRE(objective.Status == MCScenarioObjective::Pending);
    const std::shared_ptr<MCObjectList> mechs = home == InnerSphereTeam() ? InnerSphereMechList() : ClanMechList();
    const auto mech = std::static_pointer_cast<MCGameObject>(mechs->First());
    REQUIRE(mech != nullptr);
    REQUIRE(mech->GetAwake() != 0);
    const MCVector3D at = mech->GetPosition();
    objective.Position = {at.X, at.Y, at.Z};
    MCVector3D dot(at.X, at.Y, 0.0f);
    map->WorldToTacMap(dot, true);
    const auto dotX = static_cast<int32_t>(std::floor(dot.X));
    const auto dotY = static_cast<int32_t>(std::floor(dot.Y));

    const std::filesystem::path pack = EmptyPack("mc_tests_hd_objective_numbers");
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    std::array<SDL_Color, 256> colors{};
    display->GetShownColors(colors);
    constexpr uint8_t DotColor = 0x1f;
    constexpr uint8_t MechColor = 0xf;
    constexpr uint8_t SelectedColor = 0xb;
    constexpr uint8_t NumberColor = 0xf2;
    constexpr uint8_t OutlineColor = 0x10;
    // The number "1" right of and below the dot's middle (the line font at its scale), in screen pixels.
    const MCRect number{dotX + 1, dotY - 2, dotX + 16, dotY + 20};

    struct Seen
    {
        int64_t Outline = 0;
        int64_t Number = 0;
        SDL_Color Middle{};
    };

    const auto seen = [&](bool hd)
    {
        std::optional<MCHdArtOn> on;

        if (hd)
        {
            on.emplace(pack);
        }

        // Two frames, the second sharp from the start; the markers lit throughout (they blink five times a second).
        for (int32_t frame = 0; frame < 2; ++frame)
        {
            MCTacticalMap::MarkersLit = true;
            MCTacticalMap::MarkerBlinkTime = 0.0f;
            MCTestGame::RunFrame(FrameSeconds);
        }

        const Shown shown = ReadShown(pack.parent_path());
        const float scale = static_cast<float>(shown.Width) / static_cast<float>(display->Width());
        const auto pixel = [&](int32_t x, int32_t y)
        {
            const size_t offset = (static_cast<size_t>(y) * shown.Width + x) * BytesPerPixel;
            return SDL_Color{shown.Pixels[offset], shown.Pixels[offset + 1], shown.Pixels[offset + 2], 255};
        };

        const auto window = [scale](int32_t screen)
        { return static_cast<int32_t>(static_cast<float>(screen) * scale); };
        Seen result;

        for (int32_t y = window(number.Y0); y < window(number.Y1 + 1); ++y)
        {
            for (int32_t x = window(number.X0); x < window(number.X1 + 1); ++x)
            {
                result.Outline += SameColor(pixel(x, y), colors[OutlineColor]) ? 1 : 0;
                result.Number += SameColor(pixel(x, y), colors[NumberColor]) ? 1 : 0;
            }
        }

        result.Middle = pixel(window(dotX) + window(1) / 2, window(dotY) + window(1) / 2);
        return result;
    };

    GamePaused = true;
    const Seen original = seen(false);
    const Seen hd = seen(true);
    GamePaused = false;
    std::cout << std::format("  beside the dot: {} outline and {} number pixels with HD art off, {} and {} on\n",
                             original.Outline, original.Number, hd.Outline, hd.Number);
    CHECK(original.Number > 0);
    CHECK(hd.Number > original.Number);
    CHECK(hd.Outline > original.Outline + 50);

    for (const Seen& each : {original, hd})
    {
        CHECK(!SameColor(each.Middle, colors[DotColor]));
        CHECK((SameColor(each.Middle, colors[MechColor]) || SameColor(each.Middle, colors[SelectedColor])));
    }

    std::filesystem::remove_all(pack.parent_path());
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
        const MCGuiHandle<MCGuiSmackerWindow> made = ScreenWindow()->CreateChild<MCGuiSmackerWindow>(
            [&area](MCGuiSmackerWindow& smacker) { smacker.Init(area, {}); });
        const std::shared_ptr<MCGuiSmackerWindow> window = made.Lock();
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
        CloseGuiObject(made);
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

    // As int64_t: MSVC counts an iota of size_t in a 128-bit type std::format doesn't take.
    const auto changed = static_cast<int64_t>(std::ranges::count_if(
        std::views::iota(size_t{0}, original.size()),
        [&](size_t i) { return original[i].r != hd[i].r || original[i].g != hd[i].g || original[i].b != hd[i].b; }));
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
        return draw.Command == "Copy" && draw.From.has_value() &&
               std::ranges::any_of(names,
                                   [&](const std::string& name)
                                   {
                                       const std::shared_ptr<MCTexture> art =
                                           LogScreenArt(name)->Bitmap().Texture.lock();
                                       return art != nullptr && draw.From->get().Texture.lock() == art;
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
                                   pictureCopies += draw.Command == "Copy" && draw.From.has_value() &&
                                                            draw.From->get().XMax + 1 == MissionPictureWidth
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

        const MCListView& inventory = *GlobalLog()->PurchaseScreen->InventoryPane;

        for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->ComponentInventory->Items)
        {
            if (const std::shared_ptr<MCCompInventoryBlock> row = inventory.RowOf<MCCompInventoryBlock>(*item);
                row != nullptr)
            {
                componentPictures.insert(row->IconArt.Picture);
            }
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

/// <summary>
/// Tool: <c>--hd-map-mosaic &lt;pack&gt; --shots &lt;folder&gt;</c> pictures a whole map (<c>--mission</c>, default 1)
/// for a mosaic, the map revealed and the view zoomed out as far as it goes: the camera stepped along the screen's own
/// axes by half a view, so each shot's middle half meets its neighbours', <c>tile_&lt;row&gt;_&lt;column&gt;_hd.png</c>
/// (or _original.png with <c>--hd-original 1</c>) over the map's diamond, each tile's world point printed.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-map-mosaic pictures a whole map in tiles")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-map-mosaic");
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
    const std::shared_ptr<MCViewWindow> view = MCMainView();
    REQUIRE(view != nullptr);
    view->ZoomTo(std::numeric_limits<float>::max());

    // The zoom eases in over real time.
    constexpr int32_t MaxZoomFrames = 600;

    for (int32_t frame = 0; frame < MaxZoomFrames && view->ZoomHeight != view->ZoomTarget; ++frame)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    // How a world step projects on screen, from the map's middle and two points a probe's length from it.
    constexpr float Probe = 1000.0f;
    const MCVector3D topLeft = MCTerrain::MapTopLeft3d100;
    const MCVector3D middle(topLeft.X + 0.5f * WorldUnitsMapSide, topLeft.Y - 0.5f * WorldUnitsMapSide, 0.0f);
    Eye()->SetPosition(middle);
    MCTestGame::RunFrame(FrameSeconds);
    const MCVector2D at = Eye()->Project(middle);
    const MCVector2D alongX = Eye()->Project(MCVector3D(middle.X + Probe, middle.Y, 0.0f)) - at;
    const MCVector2D alongY = Eye()->Project(MCVector3D(middle.X, middle.Y + Probe, 0.0f)) - at;
    const float determinant = alongX.X * alongY.Y - alongY.X * alongX.Y;
    REQUIRE(determinant != 0.0f);

    // The world point a screen offset from the middle shows.
    const auto worldAt = [&](float screenX, float screenY)
    {
        const float x = (alongY.Y * screenX - alongY.X * screenY) / determinant * Probe;
        const float y = (alongX.X * screenY - alongX.Y * screenX) / determinant * Probe;
        return MCVector3D(middle.X + x, middle.Y + y, 0.0f);
    };

    // The map's diamond on screen, in half views.
    const float stepX = static_cast<float>(view->WorldWidth()) * 0.5f;
    const float stepY = static_cast<float>(view->WorldHeight()) * 0.5f;
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;

    for (const float down : {0.0f, 1.0f})
    {
        for (const float across : {0.0f, 1.0f})
        {
            const MCVector2D corner = Eye()->Project(MCVector3D(topLeft.X + across * WorldUnitsMapSide,
                                                                topLeft.Y - down * WorldUnitsMapSide, 0.0f)) -
                                      at;
            left = std::min(left, corner.X);
            right = std::max(right, corner.X);
            top = std::min(top, corner.Y);
            bottom = std::max(bottom, corner.Y);
        }
    }

    const auto firstRow = static_cast<int32_t>(std::floor(top / stepY));
    const auto lastRow = static_cast<int32_t>(std::ceil(bottom / stepY));
    const auto firstColumn = static_cast<int32_t>(std::floor(left / stepX));
    const auto lastColumn = static_cast<int32_t>(std::ceil(right / stepX));
    std::cout << std::format(
        "  mosaic: rows {}..{}, columns {}..{}, a step {:.0f} x {:.0f} window pixels, zoom {:.0f}\n", firstRow, lastRow,
        firstColumn, lastColumn, stepX * view->WorldScaleX(), stepY * view->WorldScaleY(), view->ZoomHeight);

    for (int32_t row = firstRow; row <= lastRow; ++row)
    {
        for (int32_t column = firstColumn; column <= lastColumn; ++column)
        {
            const MCVector3D point = worldAt(static_cast<float>(column) * stepX, static_cast<float>(row) * stepY);
            Eye()->SetPosition(point);
            MCTestGame::RunFrame(FrameSeconds);
            MCTestGame::RunFrame(FrameSeconds);
            const std::string name = std::format("tile_{}_{}", row - firstRow, column - firstColumn);
            std::cout << std::format("  {}: world {:.0f}, {:.0f}\n", name, point.X, point.Y);
            Shoot(folder, name, original);
        }
    }
}

/// <summary>
/// Tool: <c>--hd-terrain-layout &lt;folder&gt;</c> writes how mission <c>--mission</c>'s map lays its ground tiles, for
/// the HD art generator (tools/upscale/make_terrain_pack.py's neighbours): <c>mission_&lt;n&gt;.tsv</c>, a first line
/// with the ground mesh's steps (across, down and up an elevation level, at the default zoom), then a line a cell drawn
/// (in the order the GPU draws them, row by row): its top-left vertex (row, column), its tile's pack name
/// (MCHdArt::TerrainTileName), its hot spot and its corners' elevation levels (top left, top right, bottom right, bottom
/// left). A cell's tile lies at its vertex's point less the hot spot.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-terrain-layout writes how a map lays its ground tiles")
{
    const std::optional<std::string_view> out = MCTest::Option("hd-terrain-layout");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    int32_t mission = 1;

    if (const std::optional<std::string_view> given = MCTest::Option("mission"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), mission);
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(mission));
    REQUIRE(MCRenderer::GpuDrawing() == MCGpuDrawing::On);
    RevealAll();
    MCTestGame::RunFrame(FrameSeconds);
    const MCOptionalRef<const MCTerrainMesh> inUse = MCTerrainMeshInUse();
    const std::optional<MCTerrainMeshSteps> steps = MCTerrainMeshStepsInUse();
    REQUIRE(inUse.has_value());
    const MCTerrainMesh& mesh = *inUse;
    REQUIRE(steps.has_value());
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);
    std::vector<std::string> names(mesh.Tiles.size());
    std::ranges::transform(mesh.Tiles, names.begin(),
                           [](const std::vector<uint8_t>& tile) { return MCHdArt::TerrainTileName(tile); });
    std::ofstream layout(folder / std::format("mission_{}.tsv", mission));
    layout << std::format("steps\t{}\t{}\t{}\n", steps->Across, steps->Down, steps->Up);
    constexpr int32_t ElevationBits = 8;
    constexpr uint32_t ElevationMask = 0xff;
    size_t cells = 0;

    for (int32_t r = 0; r < mesh.Rows; ++r)
    {
        for (int32_t c = 0; c < mesh.Cols; ++c)
        {
            const size_t cell = static_cast<size_t>(r) * mesh.Cols + c;
            const uint32_t tile = mesh.CellTiles[cell];

            if (tile == MCTerrainMesh::NoTile)
            {
                continue;
            }

            const std::vector<uint8_t>& data = mesh.Tiles[tile];
            const uint32_t e = mesh.CellElevations[cell];
            layout << std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", mesh.FirstRow + r, mesh.FirstCol + c,
                                  names[tile], data[0], data[1], e & ElevationMask,
                                  (e >> ElevationBits) & ElevationMask, (e >> (2 * ElevationBits)) & ElevationMask,
                                  e >> (3 * ElevationBits));
            ++cells;
        }
    }

    std::cout << std::format("  mission {}: {} cells, {} tiles, steps {} {} {}\n", mission, cells, names.size(),
                             steps->Across, steps->Down, steps->Up);
    CHECK(cells > 0);
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
/// switches the art as it plays (original, hd, day, dusk: the debug console's art command). <c>--hd-water-at
/// &lt;row&gt;,&lt;column&gt;</c> puts the camera over that tile instead of the shore.
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

    // Port-only, for the HD art audits: --hd-water-at <row>,<column> parks the camera over that tile instead.
    if (const std::optional<std::string_view> at = MCTest::Option("hd-water-at"); at.has_value())
    {
        const size_t comma = at->find(',');
        REQUIRE(comma != std::string_view::npos);
        std::from_chars(at->data(), at->data() + comma, bestRow);
        std::from_chars(at->data() + comma + 1, at->data() + at->size(), bestColumn);
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

namespace
{
    /// <summary>--hd-walk's frames unless --hd-walk-frames says, at 30 a second (WaterFrameSeconds).</summary>
    constexpr int32_t WalkFrames = 240;
    /// <summary>
    /// --hd-walk's mech walks this many tiles on unless --hd-walk-to says where, the way with the most puddles, looked
    /// for every PuddleLook world units.
    /// </summary>
    constexpr int32_t WalkTiles = 4;
    constexpr float PuddleLook = 8.0f;

    /// <summary>
    /// The map tile a tool's option <paramref name="name"/> names, as <c>row,column</c>; <paramref name="otherwise"/>
    /// without it.
    /// </summary>
    std::pair<int32_t, int32_t> TileOption(std::string_view name, std::pair<int32_t, int32_t> otherwise)
    {
        const std::optional<std::string_view> given = MCTest::Option(name);

        if (!given.has_value())
        {
            return otherwise;
        }

        int32_t row = 0;
        int32_t column = 0;
        REQUIRE(std::sscanf(std::string(*given).c_str(), "%d,%d", &row, &column) == 2);
        REQUIRE(GameMap().OnMap(row, column));
        return {row, column};
    }
}

/// <summary>
/// Tool: <c>--hd-walk &lt;pack&gt; --shots &lt;folder&gt;</c> pictures a mech walking (port-only: its feet splashing in
/// puddles in HD weather, at the water's edge with HD particles): mission <c>--mission</c>'s map revealed, its player's
/// first mech put on map tile <c>--hd-walk-from &lt;row,column&gt;</c> (or where it stands), walking to
/// <c>--hd-walk-to &lt;row,column&gt;</c> (or 4 tiles on along the map's rows, columns or diagonals, the way with the
/// most puddles in the rain), the camera following it (held between with <c>--hd-walk-held 1</c>); the view zoomed to <c>--hd-walk-zoom &lt;lines&gt;</c> if given,
/// then <c>--hd-walk-frames &lt;n&gt;</c> frames (default 240) at 30 a second, <c>walk_NNNN_hd.png</c> (or
/// _original.png with <c>--hd-original 1</c>), lit as <c>--hd-lighting</c> asks; <c>--hd-steps</c> sizes the splashes.
/// (The original's mechs never walk into the water: a water tile they can enter has a bridge over it, or is the shore.)
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-walk pictures a mech walking")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-walk");
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

    int32_t frames = WalkFrames;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-walk-frames"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), frames);
    }

    const PlayerMech player = OwnPlayerMech();
    int32_t standingRow = 0;
    int32_t standingColumn = 0;
    MCScenarioMap::WorldToMapTilePos(player.Mech->GetPosition(), standingRow, standingColumn);
    const auto [fromRow, fromColumn] = TileOption("hd-walk-from", {standingRow, standingColumn});
    MCVector3D from(TileColToWorld(fromColumn), TileRowToWorld(fromRow), 0.0f);
    from.Z = MCTerrain::GetTerrainElevation(from);

    // The way with the most puddles, the ground drawn round the mech (at the game's own zoom, the most of it).
    Eye()->SetPosition(from);
    MCTestGame::RunFrame(WaterFrameSeconds);
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();
    REQUIRE(camera != nullptr);
    std::pair<int32_t, int32_t> wettest{fromRow, fromColumn + WalkTiles};
    int32_t mostPuddles = 0;

    for (int32_t wayRow = -1; wayRow <= 1; ++wayRow)
    {
        for (int32_t wayColumn = -1; wayColumn <= 1; ++wayColumn)
        {
            const int32_t row = fromRow + wayRow * WalkTiles;
            const int32_t column = fromColumn + wayColumn * WalkTiles;

            if ((wayRow == 0 && wayColumn == 0) || !GameMap().OnMap(row, column))
            {
                continue;
            }

            const float x = TileColToWorld(column);
            const float y = TileRowToWorld(row);
            const auto looks = static_cast<int32_t>(std::hypot(x - from.X, y - from.Y) / PuddleLook);
            int32_t puddles = 0;

            for (int32_t at = 0; at <= looks; ++at)
            {
                const float share = static_cast<float>(at) / static_cast<float>(looks);
                MCVector3D point(std::lerp(from.X, x, share), std::lerp(from.Y, y, share), 0.0f);
                point.Z = MCTerrain::GetTerrainElevation(point);
                puddles += MCWeather::PuddleAt(point, *camera) ? 1 : 0;
            }

            if (puddles > mostPuddles)
            {
                mostPuddles = puddles;
                wettest = {row, column};
            }
        }
    }

    const auto [toRow, toColumn] = TileOption("hd-walk-to", wettest);

    if (const std::optional<std::string_view> zoom = MCTest::Option("hd-walk-zoom"); zoom.has_value())
    {
        // strtof: libc++ has floating-point from_chars only from macOS 26.
        MCFixedZoomHeight = std::strtof(std::string(*zoom).c_str(), nullptr);
    }

    const MCVector3D to(TileColToWorld(toColumn), TileRowToWorld(toRow), 0.0f);
    player.Mech->SetPosition(from);
    const std::shared_ptr<MCMechWarrior> pilot = player.Mech->GetPilot();
    REQUIRE(pilot != nullptr);
    pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, to, -1, 0);
    const MCVector3D between((from.X + to.X) * 0.5f, (from.Y + to.Y) * 0.5f, 0.0f);
    std::cout << std::format("  the mech ({}) walks from tile ({}, {}) to ({}, {}); the wettest way had {} puddle "
                             "points {:.0f} apart\n",
                             player.Mech->TypeAs<MCBattleMechType>().Name, fromRow, fromColumn, toRow, toColumn,
                             mostPuddles, PuddleLook);
    const std::map<int32_t, MCArtMode> artModes = ArtModesFromOptions();

    // The camera follows the mech (its target), or with --hd-walk-held is held between.
    const bool held = MCTest::Option("hd-walk-held").has_value();

    if (held)
    {
        Eye()->ChangeTarget(nullptr, true);
    }

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        SwitchArtMode(artModes, frame);

        if (held)
        {
            Eye()->SetPosition(between);
        }

        MCTestGame::RunFrame(WaterFrameSeconds);
        Shoot(folder, std::format("walk_{:04}", frame), original);
    }

    const MCVector3D reached = player.Mech->GetPosition();
    std::cout << std::format("  the mech ended {:.0f} from where it was sent\n",
                             std::hypot(reached.X - to.X, reached.Y - to.Y));
    MCFixedZoomHeight = 0.0f;
}

/// <summary>
/// A mech's legs turning (HDTurns: MCMechActor::LegsTurnMiddle's in-between, which says where the middle lies) show the
/// pack's turn middle round their frame, over the frame's own indices: on the screen, drawn by the GPU over mission 1, a
/// 4 x 4 frame shows red alone; within the scope of its middle (6 x 4, the frame 2 columns in), the middle's colour
/// from 2 columns before the frame as it is, and mirrored about the hot spot (as the frame is) to 2 columns past the
/// mirrored frame: blue for the middle toward the next facing, green for the one toward the next facing mirrored.
/// </summary>
TEST_CASE_ISOLATED("game: a mech's legs turning show the pack's turn middle round their frame")
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

    // One frame of 4 x 4 opaque pixels; its middles 6 x 4, the frame 2 columns in.
    constexpr int32_t Side = 4;
    constexpr int32_t Shift = 2;
    constexpr int32_t Together = Side + Shift;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Side; ++row)
    {
        rows.insert(rows.end(), {RunOf(Side), opaque, opaque, opaque, opaque, RowEnd});
    }

    const std::vector<uint8_t> table = FramesTable(std::array{TableFrame{0, 0, Side, Side, rows}});
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    const auto shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);

    constexpr int32_t Scale = 2;
    constexpr SDL_Color Red = {200, 0, 0, 255};
    constexpr SDL_Color Blue = {0, 0, 200, 255};
    constexpr SDL_Color Green = {0, 200, 0, 255};
    const auto everywhere = [&](int32_t, int32_t) { return std::optional<uint8_t>(opaque); };
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_turn_middle";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const MCHdArt::MechShapeKey key{"legs90", 1, 0};
    const std::filesystem::path folder = pack / MCHdArt::FolderOf(key);
    std::filesystem::create_directories(folder);
    WriteSteppedFrame(folder / "0", Side, Side, Scale, Red, everywhere);
    WriteSteppedFrame(folder / "0_turn", Together, Side, Scale, Blue, everywhere);
    WriteSteppedFrame(folder / "0_turnm", Together, Side, Scale, Green, everywhere);
    WritePaletteRgb(pack, colors);

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    const int32_t width = display->Width();
    std::optional<MCHdArtOn> hdArt(std::in_place, pack);
    MCHdArt::Attach(shape, key);

    // The screen's pixels after drawing the frame with its hot spot at (x, y), as it is or mirrored, in the scope of an
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
    const MCInBetweenBounds bounds{Shift, 0, Together, Side};

    // Within the edges (smooth, so their pixels are part covered): column c of the middle at x + c - 2 as it is, at
    // x - (c - 2) mirrored; column c of the frame at x + c, or x - c.
    for (const bool mirrored : {false, true})
    {
        for (const int32_t step : {0, TurnStep, TurnMirroredStep})
        {
            const std::optional<MCInBetween> inBetween =
                step == 0 ? std::nullopt : std::optional(MCInBetween{0, 0, step, bounds});
            const auto [before, after] = drawn(mirrored, inBetween);

            for (int32_t row = 1; row < Side - 1; ++row)
            {
                for (int32_t column = 1; column < Together - 1; ++column)
                {
                    const int32_t placed = column - Shift;
                    const int32_t shown = mirrored ? -placed : placed;
                    const SDL_Color pixel = at(after, shown, row);
                    MCTest::Scope scope(std::format("{}, step {}, column {}, row {}: {} {} {}",
                                                    mirrored ? "mirrored" : "as is", step, column, row, pixel.r,
                                                    pixel.g, pixel.b));

                    if (step != 0)
                    {
                        CHECK(same(pixel, step == TurnStep ? Blue : Green));
                    }
                    else if (placed >= 1 && placed < Side - 1)
                    {
                        CHECK(same(pixel, Red));
                    }
                    else if (placed < -1)
                    {
                        CHECK(same(pixel, at(before, shown, row)));
                    }
                }
            }
        }
    }

    hdArt.reset();
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// A mech's legs turning cast their turn middle's sun shadow, where the middle lies: on the screen, drawn by the GPU
/// over mission 1, paused, a 4 x 4 frame standing 12 to 9 rows above its foot, cast a pixel right for every pixel above
/// it, darkens the ground where its middle's column 1 (2 columns before the frame's, mirrored after) lands only in the
/// scope of the middle; without it that ground stays as it was.
/// </summary>
TEST_CASE_ISOLATED("game: a mech's legs turning cast their turn middle's sun shadow")
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
    const uint8_t opaque = FullColorIndex(colors);

    // One frame of 4 x 4 opaque pixels standing above its foot; its middle 7 x 4, the frame 3 columns in.
    constexpr int32_t Side = 4;
    constexpr int32_t Shift = 3;
    constexpr int32_t Together = Side + Shift;
    constexpr int32_t Top = -12;
    std::vector<uint8_t> rows;

    for (int32_t row = 0; row < Side; ++row)
    {
        rows.insert(rows.end(), {RunOf(Side), opaque, opaque, opaque, opaque, RowEnd});
    }

    const std::vector<uint8_t> table = FramesTable(std::array{TableFrame{0, Top, Side, Side, rows}});
    MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, block.Bytes().begin());
    const auto shape = std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0);

    constexpr int32_t Scale = 2;
    constexpr SDL_Color Red = {200, 0, 0, 255};
    constexpr SDL_Color Blue = {0, 0, 200, 255};
    const auto everywhere = [&](int32_t, int32_t) { return std::optional<uint8_t>(opaque); };
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_hd_turn_shadow";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    const MCHdArt::MechShapeKey key{"legs90", 1, 0};
    const std::filesystem::path folder = pack / MCHdArt::FolderOf(key);
    std::filesystem::create_directories(folder);
    WriteSteppedFrame(folder / "0", Side, Side, Scale, Red, everywhere);
    WriteSteppedFrame(folder / "0_turn", Together, Side, Scale, Blue, everywhere);
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
    GHdSunShadows = 1;
    GamePaused = true;

    // The ground, then the frame drawn on it as its sun shadow, as it is or mirrored, in the scope of an in-between.
    const auto drawn = [&](bool mirrored, const std::optional<MCInBetween>& inBetween)
    {
        MCTestGame::RunFrame(FrameSeconds);
        auto under = display->ReadFrame();
        REQUIRE(under.has_value());
        {
            const MCRenderer::SunShadowScope cast(true);
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

        auto over = display->ReadFrame();
        REQUIRE(over.has_value());
        return std::pair{std::move(*under), std::move(*over)};
    };

    const auto at = [&](const std::vector<SDL_Color>& frame, int32_t column, int32_t row)
    { return frame[static_cast<size_t>(y + row + pane.Y0) * width + x + column + pane.X0]; };
    const auto near = [](int32_t actual, int32_t expected) { return std::abs(actual - expected) <= 3; };
    const auto darkened = [&](SDL_Color actual, SDL_Color under, float kept)
    {
        return near(actual.r, static_cast<int32_t>(std::lround(under.r * kept))) &&
               near(actual.g, static_cast<int32_t>(std::lround(under.g * kept))) &&
               near(actual.b, static_cast<int32_t>(std::lround(under.b * kept)));
    };

    // The middle's column 1 stands at -2 (mirrored, at 2); a row h above the foot lands h pixels right of it, where no
    // column of the frame lands, nor its smooth edge.
    const MCInBetweenBounds bounds{Shift, 0, Together, Side};
    constexpr int32_t MiddleColumn = 1;

    for (const bool mirrored : {false, true})
    {
        for (const bool middle : {false, true})
        {
            const std::optional<MCInBetween> inBetween =
                middle ? std::optional(MCInBetween{0, 0, TurnStep, bounds}) : std::nullopt;
            const auto [under, over] = drawn(mirrored, inBetween);

            for (int32_t row = Top + 1; row < Top + Side - 1; ++row)
            {
                const int32_t stands = mirrored ? Shift - MiddleColumn : MiddleColumn - Shift;
                const int32_t lands = stands - row;
                MCTest::Scope scope(std::format("{}, {}, row {}: column {}", mirrored ? "mirrored" : "as is",
                                                middle ? "its middle" : "no middle", row, lands));
                CHECK(darkened(at(over, lands, row), at(under, lands, row), middle ? 1.0f - Darkness : 1.0f));
            }
        }
    }

    lighting = kept;
    GHdSunShadows = 0;
    GamePaused = false;
    hdArt.reset();
    std::filesystem::remove_all(scratch);
}
