#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCViewWindow.h"
#include "color/MCPalette.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBattleMech.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCVulkanRenderer.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"

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

    /// <summary>A pack's sequence: one shape table (a gesture's part at one facing), as the tree uses it.</summary>
    struct Sequence
    {
        MCHdArt::MechShapeKey Key;
        int32_t Gesture = 0;
        /// <summary>The tree's part numbering (0 legs, 1 torso, 2 and 3 the arms' sprites).</summary>
        int32_t Part = 0;
        float Facing = 0.0f;
        bool Reverse = false;
        std::vector<DecodedFrame> Frames;
    };

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

    /// <summary>A sequence's folder in a pack (sprite/MCHdArt.h).</summary>
    std::filesystem::path SequenceFolder(const std::filesystem::path& pack, const MCHdArt::MechShapeKey& key)
    {
        return pack / "mechs" / key.PartFile / std::to_string(key.MechNum) / std::to_string(key.Packet);
    }

    /// <summary>
    /// Writes <paramref name="sequences"/> into a pack at <paramref name="pack"/>: each frame at
    /// <paramref name="scale"/> times its size (each pixel repeated), its colours through <paramref name="paint"/>,
    /// with its index picture.
    /// </summary>
    void WritePack(const std::filesystem::path& pack, std::span<const Sequence> sequences,
                   std::span<const SDL_Color, 256> colors, int32_t scale,
                   const std::function<SDL_Color(SDL_Color)>& paint)
    {
        for (const Sequence& sequence : sequences)
        {
            const std::filesystem::path folder = SequenceFolder(pack, sequence.Key);
            std::filesystem::create_directories(folder);

            for (size_t frameNum = 0; frameNum < sequence.Frames.size(); ++frameNum)
            {
                const DecodedFrame& frame = sequence.Frames[frameNum];

                if (frame.Width == 0)
                {
                    continue;
                }

                const int32_t width = frame.Width * scale;
                const int32_t height = frame.Height * scale;
                std::vector<uint8_t> color(static_cast<size_t>(width) * height * BytesPerPixel, 0);
                std::vector<uint8_t> index(color.size(), 0);

                for (int32_t y = 0; y < height; ++y)
                {
                    for (int32_t x = 0; x < width; ++x)
                    {
                        const size_t from =
                            static_cast<size_t>(y / scale) * frame.Width + static_cast<size_t>(x / scale);
                        uint8_t* outColor = &color[(static_cast<size_t>(y) * width + x) * BytesPerPixel];
                        uint8_t* outIndex = &index[(static_cast<size_t>(y) * width + x) * BytesPerPixel];
                        outIndex[3] = HasIndex;

                        if (frame.Drawn[from] == 0)
                        {
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

                SavePng(folder / std::format("{}.png", frameNum), width, height, color);
                SavePng(folder / std::format("{}_idx.png", frameNum), width, height, index);
            }
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
            if (folders.insert(SequenceFolder({}, sequence.Key).generic_string()).second)
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
        json += std::format("{}\n  {{\"folder\": \"mechs/{}/{}/{}\", \"gesture\": {}, \"part\": {}, \"facing\": {}, "
                            "\"reverse\": {}, \"frames\": [",
                            i == 0 ? "" : ",", sequence.Key.PartFile, sequence.Key.MechNum, sequence.Key.Packet,
                            sequence.Gesture, sequence.Part, sequence.Facing, sequence.Reverse ? "true" : "false");

        for (size_t f = 0; f < sequence.Frames.size(); ++f)
        {
            const DecodedFrame& frame = sequence.Frames[f];
            json += std::format("{}{{\"width\": {}, \"height\": {}, \"xmin\": {}, \"ymin\": {}, \"translucent\": {}}}",
                                f == 0 ? "" : ", ", frame.Width, frame.Height, frame.XMin, frame.YMin,
                                frame.Translucent ? "true" : "false");
            ++frameCount;
        }

        json += "]}";
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

    std::optional<HdScene> SetUpHdScene(MCGpuDrawing drawing)
    {
        if (!MCTestGame::Available())
        {
            return std::nullopt;
        }

        MCRenderer::RequestGpuDrawing(drawing);
        REQUIRE(MCTestGame::StartMission(1));
        HdScene scene;
        scene.Renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());

        if (scene.Renderer == nullptr || MCRenderer::GpuDrawing() != drawing)
        {
            std::cout << "  (skipped: no GPU renderer on this machine)\n";
            return std::nullopt;
        }

        constexpr int32_t WindowWidth = 1600;
        constexpr int32_t WindowHeight = 1200;
        constexpr int UiLines = 480;
        constexpr int32_t Scale = 2;
        ScaleUp(WindowWidth, WindowHeight, UiLines);
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
        WritePack(scene.Pack, standing, colors, Scale,
                  [](SDL_Color color)
                  {
                      return SDL_Color{static_cast<uint8_t>(255 - color.r), static_cast<uint8_t>(255 - color.g),
                                       static_cast<uint8_t>(255 - color.b), 255};
                  });
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
            SpriteManager()->DumpAll();
        }

        ~MCHdArtOn()
        {
            GHdArt = 0;
            MCHdArt::SetPackFolder({});
        }

        MCHdArtOn(const MCHdArtOn&) = delete;
        MCHdArtOn& operator=(const MCHdArtOn&) = delete;
    };

    /// <summary>The pixels whose shown colour differs between <paramref name="a"/> and <paramref name="b"/>, and their
    /// mean position.</summary>
    struct Changed
    {
        int32_t Count = 0;
        int64_t X = 0;
        int64_t Y = 0;
    };

    Changed Compare(const Shown& a, const Shown& b)
    {
        REQUIRE_EQ(a.Width, b.Width);
        REQUIRE_EQ(a.Height, b.Height);
        Changed changed;

        for (int32_t y = 0; y < a.Height; ++y)
        {
            for (int32_t x = 0; x < a.Width; ++x)
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

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(1));
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
    std::optional<MCHdArtOn> hdArt;

    if (!original)
    {
        hdArt.emplace(std::filesystem::path(*pack));
    }

    const PlayerMech player = GivePlayerMech(MCTest::Option("mech").value_or("madcat"));
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);

    const auto shoot = [&](const std::string& name)
    {
        const std::filesystem::path path = folder / std::format("{}_{}.png", name, original ? "original" : "hd");
        REQUIRE(MCInput::Display()->SaveShownScreenshot(path).has_value());
        const size_t read = MCHdArt::FramesRead();
        const auto readTime = std::chrono::duration<double, std::milli>(MCHdArt::ReadTime()).count();
        std::cout << std::format("  {}: {} HD frames read so far ({:.2f} ms each), {} MB kept\n", name, read,
                                 read > 0 ? readTime / static_cast<double>(read) : 0.0, MCHdArt::CachedBytes() >> 20);
    };

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
