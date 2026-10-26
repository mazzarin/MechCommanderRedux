#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCViewWindow.h"
#include "color/MCPalette.h"
#include "engine/MCCraterManager.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCHardwareCursor.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBattleMech.h"
#include "object/MCExplosion.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCObjectSystem.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCRegisteredBlock.h"
#include "platform/MCRenderer.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCVulkanRenderer.h"
#include "sprite/MCElementalTree.h"
#include "sprite/MCGVAppearanceType.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCPUAppearanceType.h"
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
#include "logistics/MCLoadSaveMenu.h"
#include "main/MCLogistics.h"
#include "lib/MCFile.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPreferencesMenu.h"
#include "platform/MCSharpText.h"

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

    /// <summary>An appearance the pack can hold: its type id, kind, and the names of the object types that use it.</summary>
    struct ObjectAppearance
    {
        uint32_t Id = 0;
        ObjectKind Kind = ObjectKind::Object;
        std::vector<std::string> Types;
    };

    /// <summary>
    /// The appearances the object PAK's types use, by type id: each with the kind of the first class that uses it
    /// (ClassKinds; an effect's when none does), except mechs' and those of classes without shapes of their own (lines,
    /// weapon effects).
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
                 appearanceClass != MCAppearanceClass::Building &&
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
    /// <paramref name="type"/>'s sequences, their translucent frames holding their blended pixels when
    /// <paramref name="keepBlended"/> (an effect's).
    /// </summary>
    ObjectSequences ObjectSequencesOf(const std::shared_ptr<MCAppearanceType>& type, bool keepBlended)
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

        for (const auto& [packet, shape] : DrawnShapes(type))
        {
            if (!VfxIsShapeTable(shape->FrameList()))
            {
                continue;
            }

            Sequence& sequence = made.Sequences.emplace_back();
            sequence.Folder = MCHdArt::FolderOf(MCHdArt::ObjectShapeKey{shapeFile, appearance, packet});
            sequence.Blended = keepBlended;
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
    /// The appearances <c>--kind &lt;name,...&gt;</c> and <c>--appearance &lt;packet,...&gt;</c> pick (every one when
    /// neither is given), each loaded.
    /// </summary>
    std::vector<std::pair<ObjectAppearance, std::shared_ptr<MCAppearanceType>>> PickedObjectAppearances()
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

        for (const auto& [id, appearance] : FindObjectAppearances())
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
/// once). <c>--kind</c> and <c>--appearance</c> pick as --hd-extract-objects does.
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

    for (const auto& [appearance, type] : PickedObjectAppearances())
    {
        const ObjectSequences made = ObjectSequencesOf(type, KindInfo(appearance.Kind).Blended);
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

        std::string types;

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
/// way) is written only as <c>"same_as": "&lt;that one's folder&gt;"</c>: the generator copies it.
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

    for (const auto& [appearance, type] : PickedObjectAppearances())
    {
        const ObjectSequences made = ObjectSequencesOf(type, KindInfo(appearance.Kind).Blended);

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
            json += std::format("{}\n  {{\"folder\": \"{}\"{}, \"frames\": [{}]}}", i == 0 ? "" : ",",
                                sequence.Folder.generic_string(), sameAs, FramesJson(sequence));
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
                sprites->AttachHdArt();
            }

            if (MCGameContext::Current().HasCraterManager())
            {
                CraterManager().AttachHdArt();
            }

            if (const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
                iface != nullptr && iface->MechBar != nullptr)
            {
                for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons)
                {
                    button->AttachHdArt();
                }
            }
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
    /// <summary>A shape's VFX table, one frame, as the game's encoder lays one out (vfx/MCVfxShapeScan.cpp).</summary>
    std::vector<uint8_t> OneFrameTable(int32_t width, int32_t height, std::span<const uint8_t> rows)
    {
        std::vector<uint8_t> table = {'1', '.', '1', '0'};
        const auto put = [&table](int32_t value)
        {
            for (int32_t byte = 0; byte < 4; ++byte)
            {
                table.push_back(static_cast<uint8_t>(static_cast<uint32_t>(value) >> (8 * byte)));
            }
        };

        constexpr int32_t FirstFrame = 16;
        constexpr int BoundsShift = 16;
        put(1);
        put(FirstFrame);
        put(0);
        put(width << BoundsShift | height);
        put(0);
        put(0);
        put(0);
        put(width - 1);
        put(height - 1);
        table.insert(table.end(), rows.begin(), rows.end());
        return table;
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

    // The frame: nothing, light, black covering what is under it, and the opaque colour at half its brightness, by the
    // original's columns.
    constexpr int32_t Scale = 2;
    constexpr SDL_Color Light = {90, 60, 30, 0};
    constexpr SDL_Color Cover = {0, 0, 0, 255};
    const SDL_Color lit = {static_cast<uint8_t>(colors[opaque].r / 2), static_cast<uint8_t>(colors[opaque].g / 2),
                           static_cast<uint8_t>(colors[opaque].b / 2), 255};
    const auto frameColor = [&](int32_t column)
    {
        return column == 0 ? SDL_Color{0, 0, 0, 0} : column < 3 ? Light : column < 5 ? Cover : lit;
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
                CHECK(sameAs(drawn, std::min(under.r + Light.r, 255), std::min(under.g + Light.g, 255),
                             std::min(under.b + Light.b, 255)));
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
    /// <summary>
    /// The picture tools' start: mission <c>--mission &lt;n&gt;</c> (default 1) drawn by the GPU in a window of
    /// <c>--window &lt;w&gt;x&lt;h&gt;</c> (default 3024x1890, a Retina MacBook's pixels) with the UI 720 lines tall, and
    /// HD art on with the pack at
    /// <paramref name="pack"/> (into <paramref name="hdArt"/>; its ground read up front, as a mission's load reads it)
    /// unless <c>--hd-original 1</c>.
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

        return original;
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
}

/// <summary>
/// Tool: <c>--hd-pack-effects &lt;folder&gt; --shots &lt;folder&gt;</c> pictures a pack's effects: in mission 1, each of
/// <c>--explosion &lt;name,...&gt;</c> (object types; default minedet, mdst and gasexpl) goes off beside the player's mech,
/// shot as it plays (<c>&lt;name&gt;_&lt;frame&gt;_hd.png</c>), or with <c>--hd-original 1</c> without HD art. A run plays
/// out the same every time, so the two runs' pictures differ only in how the effects are drawn. <c>--window</c> as
/// --hd-pack's.
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

    // Beside the mech (world units), the frames shot after it goes off, and the frames it is given to finish.
    constexpr float Beside = 60.0f;
    constexpr std::array<int32_t, 5> ShotFrames = {2, 5, 9, 14, 20};
    constexpr int32_t Frames = 45;

    for (const std::string& name : names)
    {
        const std::optional<int32_t> type = ObjectTypeNamed(name);
        MCTest::Scope scope(std::format("--explosion {}", name));
        REQUIRE(type.has_value());
        MCVector3D at = player->GetPosition();
        at.X += Beside;
        CreateExplosion(*type, at, 0.0f, 0.0f);

        for (int32_t frame = 1; frame <= Frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);

            if (std::ranges::contains(ShotFrames, frame))
            {
                Shoot(folder, std::format("{}_{:02}", name, frame), original);
            }
        }
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
        std::span<const uint8_t> Data;
        std::set<int32_t> Frames;
    };

    std::function<void(const MCVulkanRenderer::AuditedDraw&)> AuditInto(std::map<AuditKey, AuditTally>& tally);
    void ReportAudit(const std::map<AuditKey, AuditTally>& tally, const std::filesystem::path& folder, int32_t frames);
}

/// <summary>
/// Tool: <c>--sharp-menus &lt;folder&gt;</c> shows the main menu and the logistics screens in a window the size of a
/// Retina MacBook's screen (<c>--window WxH</c>, default 3024x1890) with HD art on (a sharp screen: text redrawn sharp,
/// and the pack's menu art with <c>--hd-pack &lt;pack&gt;</c>), and saves each as shown into the folder; with
/// <c>--hd-original 1</c>, as the game shows them without.
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
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    // Two seconds settle a screen change's wipe.
    constexpr int32_t SettleFrames = 30;
    // With --hd-audit-menus 1, every screen's draws are tallied as --hd-audit tallies a battle's.
    const bool audit = MCTest::Option("hd-audit-menus").has_value();
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());
    REQUIRE(renderer != nullptr);
    std::map<AuditKey, AuditTally> tally;
    int32_t audited = 0;

    if (audit)
    {
        renderer->Audit = AuditInto(tally);
    }

    const auto shoot = [&](const std::string& name)
    {
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
    NewCampaign();
    shoot("briefing");
    GlobalLog()->SetUpPurchaseScreen(true);
    shoot("purchase");
    GlobalLog()->SetUpRepairScreen(true);
    shoot("repair");
    GHdArt = 0;
    MCHdArt::SetPackFolder({});

    if (audit)
    {
        renderer->Audit = nullptr;
        ReportAudit(tally, folder / "audit", audited);
    }
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
/// Every glyph of every font in the install, redrawn as strokes (what a sharp screen shows), reads as the original:
/// brought back down to the font's size (each glyph pixel's centre) it is the glyph, ink and bytes, and no stroke
/// reaches farther than half a glyph pixel (and the edge's smoothing) from an ink pixel.
/// </summary>
TEST_CASE_ISOLATED("game: every font's glyphs redrawn sharp read as the originals")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t Scale = MCSharpText::Scale;
    constexpr uint8_t Half = 128;
    // How far a covered redrawn pixel's centre may lie from an ink pixel, in glyph pixels: half a pixel, and the
    // smoothing's half a redrawn pixel.
    constexpr float Reach = 0.5f + 0.5f / static_cast<float>(Scale) + 1e-4f;
    const std::vector<MCGameFont> fonts = GameFonts();
    REQUIRE(!fonts.empty());
    int64_t glyphs = 0;
    int64_t wrong = 0;
    std::string first;

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

        for (int32_t character = 0; character < MCVfxRead32(font.Data, 4); ++character)
        {
            int32_t width = 0;
            const std::span<const uint8_t> glyph = GlyphOf(font.Data, character, width);

            if (width <= 0 || height <= 0)
            {
                continue;
            }

            ++glyphs;
            const std::vector<uint8_t> redrawn = MCSharpText::Redraw(glyph, width, width, height);
            const int32_t redrawnWidth = width * Scale;
            const auto texel = [&](int32_t x, int32_t y)
            { return &redrawn[(static_cast<size_t>(y) * static_cast<size_t>(redrawnWidth) + x) * 4]; };
            const auto ink = [&](int32_t x, int32_t y)
            {
                return x >= 0 && y >= 0 && x < width && y < height &&
                       glyph[static_cast<size_t>(y) * static_cast<size_t>(width) + x] != MCSharpText::NoInk;
            };

            for (int32_t y = 0; y < height; ++y)
            {
                for (int32_t x = 0; x < width; ++x)
                {
                    const uint8_t* centre = texel(x * Scale + Scale / 2, y * Scale + Scale / 2);
                    const uint8_t byte = glyph[static_cast<size_t>(y) * static_cast<size_t>(width) + x];

                    if (ink(x, y) != (centre[1] >= Half) || (ink(x, y) && centre[0] != byte))
                    {
                        fail(font, character,
                             std::format("pixel ({}, {}) comes back {:#x} at {}", x, y, centre[0], centre[1]));
                    }
                }
            }

            for (int32_t j = 0; j < height * Scale; ++j)
            {
                for (int32_t i = 0; i < redrawnWidth; ++i)
                {
                    if (texel(i, j)[1] == 0)
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

                    if (nearest > Reach)
                    {
                        fail(font, character,
                             std::format("stroke at ({:.2f}, {:.2f}) is {:.2f} from the ink", px, py, nearest));
                    }
                }
            }
        }
    }

    std::cout << std::format("  {} fonts, {} glyphs, {} wrong{}{}\n", fonts.size(), glyphs, wrong,
                             wrong > 0 ? ", first: " : "", first);
    CHECK_EQ(wrong, int64_t{0});
}

/// <summary>
/// Tool: <c>--sharp-fonts &lt;folder&gt;</c> writes every font's glyph set as <c>&lt;font&gt;.png</c>, each glyph's
/// original (scaled up nearest) over its sharp redraw, so every letter can be checked by eye.
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
    const std::filesystem::path folder(*out);
    std::filesystem::create_directories(folder);

    for (const MCGameFont& font : GameFonts())
    {
        const int32_t height = VfxFontHeight(font.Data);
        const int32_t count = MCVfxRead32(font.Data, 4);
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

            const std::vector<uint8_t> redrawn = MCSharpText::Redraw(glyph, width, width, height);
            const int32_t left = (character % PerRow) * cellWidth;
            const int32_t top = (character / PerRow) * cellHeight;

            for (int32_t j = 0; j < height * Scale; ++j)
            {
                for (int32_t i = 0; i < width * Scale; ++i)
                {
                    const bool inked = glyph[static_cast<size_t>(j / Scale) * static_cast<size_t>(width) + i / Scale] !=
                                       MCSharpText::NoInk;
                    put(left + i, top + Gap + j, inked ? 255 : 0);
                    put(left + i, top + 2 * Gap + height * Scale + j,
                        redrawn[(static_cast<size_t>(j) * static_cast<size_t>(width * Scale) + i) * 4 + 1]);
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

        const std::span<const std::vector<uint8_t>> craters = CraterManager().Shapes();

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
                tally[{std::string(draw.Command), draw.Sharp, draw.Data.data(), draw.From, draw.Window}];
            ++entry.Count;
            entry.Pixels += draw.Pixels;
            entry.Scaled |= draw.Scaled;
            entry.Data = draw.Data;
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
            std::cout << std::format("  {:>3} {:<10} {} {:>4}x{:<4} {:>4} {:>8} {:>11} {} {}{}\n", rank, command,
                                     static_cast<const void*>(data), window->XMax + 1, window->YMax + 1,
                                     entry.Frames.size(), entry.Count, entry.Pixels, entry.Scaled ? "scaled" : "1:1",
                                     AuditOwner(entry.Data), sharp ? " sharp" : "");

            // A copy's picture as its window holds it, and the pack's frame of it when it was drawn sharp.
            if (from != nullptr && from->Pixels != nullptr && from->Size() > 0)
            {
                DecodedFrame picture;
                picture.Width = from->XMax + 1;
                picture.Height = from->YMax + 1;
                picture.Index.assign(from->Pixels.get(), from->Pixels.get() + from->Size());
                picture.Drawn.assign(from->Size(), 1);
                WriteFramePngs(
                    folder / std::format("{:03}_{}", rank, command), picture, colors, 1,
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

            if (!sharp && (command == "Shape" || command == "ShapeBlit") && VfxIsShapeTable(entry.Data))
            {
                if (const auto decoded = Decode(entry.Data, *entry.Frames.begin());
                    decoded.has_value() && decoded->Width > 0)
                {
                    Sequence sequence;
                    sequence.Folder = std::format("{:03}_{}", rank, command);
                    sequence.Frames.push_back(*decoded);
                    WritePack(folder, std::span(&sequence, 1), colors, 1, [](SDL_Color color) { return color; });
                }
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

/// <summary>
/// Tool: <c>--hd-audit &lt;pack&gt; --shots &lt;folder&gt;</c> lists what is still drawn from the original's pixels
/// with HD art on: a mission (<c>--mission &lt;n&gt;</c>, default 1) drawn by the GPU as --hd-pack's pictures are, the
/// player's lance selected and sent at the nearest live enemy (again every ten seconds, and when it falls) for
/// <c>--hd-audit-frames &lt;n&gt;</c> frames (default 900, a minute) with the camera following it, every draw command
/// tallied by what it drew and whether it was sharp (ReportAudit).
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
    std::map<AuditKey, AuditTally> tally;
    renderer->Audit = AuditInto(tally);
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
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    MCPane& pane = ScreenPort()->Frame();
    const int32_t x = (pane.X1 - pane.X0) / 2;
    const int32_t y = (pane.Y1 - pane.Y0) / 2;
    constexpr int32_t Length = 60;
    constexpr int32_t Radius = 30;
    constexpr uint8_t Color = 0x30;
    std::array<SDL_Color, 256> colors{};
    display->GetShownColors(colors);

    // How many pixels of the box around the shapes show neither the shapes' colour nor what was under them.
    const auto between = [&](bool hdArt)
    {
        std::optional<MCHdArtOn> on;

        if (hdArt)
        {
            on.emplace(pack);
        }

        MCTestGame::RunFrame(FrameSeconds);
        const auto under = display->ReadFrame();
        VfxLineDraw(pane, x - Length, y - Length / 2, x + Length, y + Length / 2, Color);
        AGEllipseDraw(pane, x, y + Length, Radius * 2, Radius, Color);
        const auto drawn = display->ReadFrame();
        REQUIRE(under.has_value());
        REQUIRE(drawn.has_value());
        const int32_t width = display->Width();
        const SDL_Color shape = colors[Color];
        int64_t count = 0;

        for (int32_t row = y - Length; row <= y + Length + Radius + 1; ++row)
        {
            for (int32_t column = x - Length - 1; column <= x + Length + 1; ++column)
            {
                const size_t at = static_cast<size_t>(row + pane.Y0) * width + column + pane.X0;
                const SDL_Color a = (*under)[at];
                const SDL_Color b = (*drawn)[at];
                const bool same = a.r == b.r && a.g == b.g && a.b == b.b;
                const bool shapeColor = b.r == shape.r && b.g == shape.g && b.b == shape.b;
                count += same || shapeColor ? 0 : 1;
            }
        }

        return count;
    };

    GamePaused = true;
    const int64_t original = between(false);
    const int64_t smooth = between(true);
    GamePaused = false;
    std::cout << std::format("  pixels between the colours: {} with HD art off, {} on\n", original, smooth);
    CHECK_EQ(original, 0);
    CHECK(smooth > 0);
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
    /// <summary>One kind of the game's art: how many frames of it there are, how many the pack holds, a few it lacks.</summary>
    struct ArtCoverage
    {
        std::string Name;
        size_t Frames = 0;
        size_t Found = 0;
        std::vector<std::string> Missing;
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
/// logistics' pictures (font sheets left out: the GPU redraws the fonts). Names a few missing frames of each kind.
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

    for (const auto& [appearance, type] : PickedObjectAppearances())
    {
        const ObjectKindInfo& kind = KindInfo(appearance.Kind);
        ArtCoverage& coverage =
            objects.try_emplace(std::string(kind.Name), ArtCoverage{std::string(kind.Name)}).first->second;
        NoteSequences(coverage, ObjectSequencesOf(type, kind.Blended).Sequences);
    }

    for (auto& [name, coverage] : objects)
    {
        report.push_back(std::move(coverage));
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
        std::cout << std::format("  {}: {} of {} frames in the pack\n", coverage.Name, coverage.Found, coverage.Frames);
        frames += coverage.Frames;
        found += coverage.Found;

        for (const std::string& missing : coverage.Missing)
        {
            std::cout << std::format("    missing: {}\n", missing);
        }
    }

    std::cout << std::format("  all: {} of {} frames ({} pictures unreadable)\n", found, frames, skipped);
}
