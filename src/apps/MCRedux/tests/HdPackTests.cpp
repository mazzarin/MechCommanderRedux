#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "MCTestShapes.h"
#include "TestGame.h"
#include "appear/MCAppearanceTypeList.h"
#include "engine/MCCraterManager.h"
#include "fakes/MCFakeMemorySystem.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiStartup.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCInflate.h"
#include "lib/MCLz.h"
#include "lib/MCPacketFile.h"
#include "lib/MCZstd.h"
#include "logistics/MCLogPort.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCHdSettings.h"
#include "main/MCLogMechList.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "object/MCBattleMech.h"
#include "object/MCMover.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCPackRecords.h"
#include "sprite/MCShape.h"
#include "sprite/MCShapeFile.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCSpriteTree.h"
#include "platform/MCFileSystem.h"
#include "platform/MCMemoryLedger.h"
#include "platform/MCRegisteredBlock.h"
#include "platform/MCRenderer.h"
#include "platform/MCSharpPictures.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCUtf8Path.h"
#include "sound/MCHdAudio.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCGroundNames.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxFunctions.h"

// xxHash, header-only: its functions inline in this file alone (the version 2 archives' table hashes).
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace
{
    constexpr int32_t Channels = 4;
    /// <summary>The peak value of a channel, for PSNR.</summary>
    constexpr double Peak = 255.0;
    /// <summary>The least PSNR (dB) the encoder must reach on the test picture.</summary>
    constexpr double MinimumPsnr = 38.0;
    /// <summary>The PNG frames --hd-compress takes, and the scale they are at unless --hd-scale says.</summary>
    constexpr std::string_view PngExtension = ".png";
    constexpr std::string_view IndexSuffix = "_idx";
    constexpr int32_t DefaultScale = 3;
    /// <summary>
    /// --hd-compress's entropy reduction unless --hd-rdo says. On the Mad Cat (16,032 frames, archived): none 45.3 dB and
    /// 113 MB, 0.5 42.1 dB and 98 MB, 1 39.2 dB and 91 MB, 4 32.8 dB and 70 MB with blocks showing. 0.5 beats the
    /// encoder before bc7enc_rdo (38.6 dB, 105 MB) both ways and keeps room for the closest zoom.
    /// </summary>
    constexpr float DefaultRdoLambda = 0.5f;

    /// <summary>The PSNR (dB) between two RGBA pictures of the same size (infinite for identical ones).</summary>
    double Psnr(std::span<const uint8_t> a, std::span<const uint8_t> b)
    {
        double squared = 0.0;

        for (size_t i = 0; i < a.size(); i++)
        {
            const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
            squared += d * d;
        }

        if (squared == 0.0)
        {
            return std::numeric_limits<double>::infinity();
        }

        return 10.0 * std::log10(Peak * Peak / (squared / static_cast<double>(a.size())));
    }

    /// <summary>
    /// A test picture like an HD sprite: a lit gradient with fine noise inside an ellipse with a soft edge, transparent
    /// outside, premultiplied.
    /// </summary>
    std::vector<uint8_t> SpriteLike(int32_t width, int32_t height)
    {
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * Channels);
        uint32_t noise = 0x12345678u;

        for (int32_t y = 0; y < height; y++)
        {
            for (int32_t x = 0; x < width; x++)
            {
                noise = noise * 1664525u + 1013904223u;
                const double dx = (x - width / 2.0) / (width * 0.4);
                const double dy = (y - height / 2.0) / (height * 0.4);
                const double alpha = std::clamp((1.0 - std::sqrt(dx * dx + dy * dy)) * 8.0, 0.0, 1.0);
                const double grain = static_cast<double>((noise >> 24) % 9) - 4.0;
                const std::array<double, 3> color = {60.0 + 150.0 * x / width + grain, 90.0 + 80.0 * y / height + grain,
                                                     120.0 - 40.0 * x / width + grain};
                uint8_t* out = rgba.data() + (static_cast<size_t>(y) * width + x) * Channels;

                for (int32_t c = 0; c < 3; c++)
                {
                    out[c] = static_cast<uint8_t>(std::lround(std::clamp(color[c], 0.0, Peak) * alpha));
                }

                out[3] = static_cast<uint8_t>(std::lround(alpha * Peak));
            }
        }

        return rgba;
    }

    /// <summary>
    /// An RGBA HD frame of <paramref name="width"/> x <paramref name="height"/> original pixels at
    /// <paramref name="scale"/>, every HD pixel's colour and every index another (from <paramref name="seed"/>).
    /// </summary>
    MCHdArt::Frame PatternedFrame(int32_t width, int32_t height, int32_t scale, uint8_t seed)
    {
        constexpr int32_t ColumnStep = 9;
        constexpr int32_t RowStep = 13;
        MCHdArt::Frame frame;
        frame.Scale = scale;
        frame.Width = width * scale;
        frame.Height = height * scale;
        frame.Colors.resize(static_cast<size_t>(frame.Width) * frame.Height * Channels);

        for (int32_t y = 0; y < frame.Height; y++)
        {
            for (int32_t x = 0; x < frame.Width; x++)
            {
                uint8_t* out = frame.Colors.data() + (static_cast<size_t>(y) * frame.Width + x) * Channels;
                out[0] = static_cast<uint8_t>(x * ColumnStep + seed);
                out[1] = static_cast<uint8_t>(y * RowStep);
                out[2] = seed;
                out[3] = static_cast<uint8_t>(Peak);
            }
        }

        frame.Indices.resize(static_cast<size_t>(width) * height * MCHdArt::IndexBytes);

        for (size_t at = 0; at < frame.Indices.size(); at += MCHdArt::IndexBytes)
        {
            frame.Indices[at] = static_cast<uint8_t>(seed + at / MCHdArt::IndexBytes);
            frame.Indices[at + 1] = MCHdArt::HasIndex;
        }

        return frame;
    }

    /// <summary><paramref name="frame"/> with its colours as BC7 blocks.</summary>
    MCHdArt::Frame AsBc7(MCHdArt::Frame frame)
    {
        frame.Colors = MCBc7::Encode(frame.Colors, frame.Width, frame.Height);
        frame.Format = MCHdArt::ColorFormat::Bc7;
        return frame;
    }

    /// <summary><paramref name="frame"/>'s colours as RGBA.</summary>
    std::vector<uint8_t> RgbaOf(const MCHdArt::Frame& frame)
    {
        return frame.Format == MCHdArt::ColorFormat::Bc7 ? MCBc7::Decode(frame.Colors, frame.Width, frame.Height)
                                                         : frame.Colors;
    }
}

/// <summary>
/// BC7 round trip (bc7enc_rdo): a sprite-like picture comes back above the PSNR floor; a block of transparent black
/// (frame padding) comes back exactly, and one of another colour within a step a channel (bc7enc can miss a
/// translucent colour by one). A frame holding its blended pixels (an effect's) reads back as one.
/// </summary>
TEST_CASE("hd art: BC7 blocks decode close to the pixels encoded, padding exactly")
{
    constexpr int32_t Width = 61;
    constexpr int32_t Height = 47;
    const std::vector<uint8_t> picture = SpriteLike(Width, Height);
    const std::vector<uint8_t> blocks = MCBc7::Encode(picture, Width, Height);
    CHECK_EQ(blocks.size(), static_cast<size_t>(MCBc7::Padded(Width) / 4 * (MCBc7::Padded(Height) / 4) * 16));
    const std::vector<uint8_t> decoded = MCBc7::Decode(blocks, Width, Height);
    const double psnr = Psnr(picture, decoded);
    std::cout << std::format("  BC7 PSNR {:.1f} dB on a {}x{} sprite-like picture\n", psnr, Width, Height);
    CHECK(psnr >= MinimumPsnr);

    for (const std::array<uint8_t, Channels> color :
         {std::array<uint8_t, Channels>{0, 0, 0, 0}, std::array<uint8_t, Channels>{255, 255, 255, 255},
          std::array<uint8_t, Channels>{37, 201, 94, 255}, std::array<uint8_t, Channels>{18, 9, 77, 128}})
    {
        std::array<uint8_t, 64> pixels{};

        for (size_t i = 0; i < pixels.size(); i += Channels)
        {
            std::ranges::copy(color, pixels.begin() + static_cast<ptrdiff_t>(i));
        }

        std::array<uint8_t, MCBc7::BlockBytes> block{};
        MCBc7::EncodeBlock(pixels, block);
        std::array<uint8_t, 64> back{};
        MCBc7::DecodeBlock(block, back);

        if (color == std::array<uint8_t, Channels>{0, 0, 0, 0})
        {
            CHECK(back == pixels);
        }

        for (size_t i = 0; i < pixels.size(); i++)
        {
            CHECK(std::abs(static_cast<int32_t>(back[i]) - static_cast<int32_t>(pixels[i])) <= 1);
        }
    }
}

/// <summary>
/// A frame file (.hdf) reads back as written: its colours as BC7 within the floor, its indices exactly; one made for
/// another shape size, cut short or not a frame file is refused.
/// </summary>
TEST_CASE("hd art: a frame file reads back as written, and a wrong one is refused")
{
    constexpr int32_t Scale = 3;
    constexpr int32_t Width = 23;
    constexpr int32_t Height = 17;
    MCHdArt::Frame frame;
    frame.Width = Width * Scale;
    frame.Height = Height * Scale;
    frame.Scale = Scale;
    frame.Format = MCHdArt::ColorFormat::Rgba8;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.resize(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes);

    for (size_t i = 0; i < frame.Indices.size(); i += MCHdArt::IndexBytes)
    {
        frame.Indices[i] = static_cast<uint8_t>(i * 7);
        frame.Indices[i + 1] = (i / MCHdArt::IndexBytes) % 3 == 0 ? 0 : MCHdArt::HasIndex;
    }

    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);
    const auto read = MCHdArt::DecodeHdf(file, Width, Height);
    REQUIRE(read.has_value());
    CHECK_EQ(read->Width, frame.Width);
    CHECK_EQ(read->Height, frame.Height);
    CHECK_EQ(read->Scale, Scale);
    CHECK(read->Format == MCHdArt::ColorFormat::Bc7);
    CHECK(read->Indices == frame.Indices);
    CHECK(!read->Blended);
    CHECK(Psnr(frame.Colors, MCBc7::Decode(read->Colors, read->Width, read->Height)) >= MinimumPsnr);

    frame.Blended = true;
    const std::vector<uint8_t> blendedFile = MCHdArt::EncodeHdf(frame);
    const auto blended = MCHdArt::DecodeHdf(blendedFile, Width, Height);
    REQUIRE(blended.has_value());
    CHECK(blended->Blended);
    CHECK(blended->Indices == frame.Indices);

    CHECK(!read->Marked);

    // A version 2 frame (SPEC-pack-v2.md's "A frame") reads back marked, and the game's rule leaves its marks alone.
    frame.Blended = false;
    constexpr std::array<uint8_t, 6> Greens = {MCHdArt::HasIndex,     MCHdArt::BakedShadow,
                                               MCHdArt::Spread,       MCHdArt::SpreadShadow,
                                               MCHdArt::DrawnBlended, MCHdArt::DrawnBlendedShadow};

    for (size_t i = 0; i < frame.Indices.size(); i += MCHdArt::IndexBytes)
    {
        frame.Indices[i] = MCHdArt::BakedShadowIndex;
        frame.Indices[i + 1] = Greens[(i / MCHdArt::IndexBytes) % Greens.size()];
    }

    frame.Marked = true;
    auto marked = MCHdArt::DecodeHdf(MCHdArt::EncodeHdf(frame), Width, Height);
    REQUIRE(marked.has_value());
    CHECK(marked->Marked);
    MCHdArt::MarkBakedShadows(*marked);
    CHECK(marked->Indices == frame.Indices);
    CHECK(std::ranges::all_of(Greens, [](uint8_t green) { return MCHdArt::HasIndexMark(green); }));
    CHECK(!MCHdArt::HasIndexMark(0));
    CHECK(MCHdArt::InShadowMark(MCHdArt::BakedShadow));
    CHECK(MCHdArt::InShadowMark(MCHdArt::SpreadShadow));
    CHECK(!MCHdArt::InShadowMark(MCHdArt::HasIndex));
    CHECK(!MCHdArt::InShadowMark(MCHdArt::Spread));
    CHECK(!MCHdArt::InShadowMark(MCHdArt::DrawnBlended));
    CHECK(MCHdArt::InShadowMark(MCHdArt::DrawnBlendedShadow));
    CHECK(!MCHdArt::HasIndexMark(MCHdArt::DrawnBlendedShadow - 1));

    CHECK(MCHdArt::DecodeHdf(file, 0, 0).has_value());
    CHECK(!MCHdArt::DecodeHdf(file, Width + 1, Height).has_value());
    CHECK(!MCHdArt::DecodeHdf(std::span(file).first(file.size() - 1), Width, Height).has_value());
    std::vector<uint8_t> wrong = file;
    wrong[0] = 'X';
    CHECK(!MCHdArt::DecodeHdf(wrong, Width, Height).has_value());
}

/// <summary>
/// A relief map's frame file (BC5) reads back as written, its blocks as they are and without indices; one with indices
/// after its blocks is refused.
/// </summary>
TEST_CASE("hd art: a relief map's frame file reads back as written, without indices")
{
    constexpr int32_t Scale = 2;
    constexpr int32_t Width = 9;
    constexpr int32_t Height = 6;
    MCHdArt::Frame relief;
    relief.Width = Width * Scale;
    relief.Height = Height * Scale;
    relief.Scale = Scale;
    relief.Format = MCHdArt::ColorFormat::Bc5;
    relief.Colors.resize(static_cast<size_t>(MCBc7::Padded(relief.Width) / MCBc7::BlockSide) *
                         static_cast<size_t>(MCBc7::Padded(relief.Height) / MCBc7::BlockSide) * MCBc7::BlockBytes);

    for (size_t i = 0; i < relief.Colors.size(); ++i)
    {
        relief.Colors[i] = static_cast<uint8_t>(i * 13);
    }

    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(relief);
    const auto read = MCHdArt::DecodeHdf(file, Width, Height);
    REQUIRE(read.has_value());
    CHECK(read->Format == MCHdArt::ColorFormat::Bc5);
    CHECK_EQ(read->Scale, Scale);
    CHECK(read->Colors == relief.Colors);
    CHECK(read->Indices.empty());

    std::vector<uint8_t> withIndices = file;
    withIndices.resize(file.size() + static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes);
    CHECK(!MCHdArt::DecodeHdf(withIndices, Width, Height).has_value());
}

namespace
{
    /// <summary>Raw DEFLATE streams from Python's zlib: a stored block, a fixed-Huffman one, a dynamic-Huffman one.</summary>
    constexpr std::array<uint8_t, 33> StoredStream = {0x01, 0x1c, 0x00, 0xe3, 0xff, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x64,
                                                      0x20, 0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2c, 0x20, 0x6e, 0x6f, 0x20,
                                                      0x63, 0x6f, 0x6d, 0x70, 0x72, 0x65, 0x73, 0x73, 0x69, 0x6f, 0x6e};
    constexpr std::array<uint8_t, 42> FixedStream = {0xf3, 0x4d, 0x4d, 0xce, 0x70, 0xce, 0xcf, 0xcd, 0x4d, 0xcc, 0x4b,
                                                     0x49, 0x2d, 0x52, 0x70, 0xcf, 0xcf, 0x49, 0xb1, 0x52, 0x28, 0xc9,
                                                     0x48, 0x55, 0xf0, 0x4d, 0x4c, 0x51, 0x70, 0x4e, 0x2c, 0x51, 0x28,
                                                     0x4f, 0xcc, 0xc9, 0x2e, 0xd6, 0x51, 0x28, 0x01, 0x00};
    constexpr std::array<uint8_t, 944> DynamicStream = {
        0xed, 0x54, 0x4b, 0xb2, 0xac, 0x28, 0x10, 0xdd, 0x4a, 0x2e, 0x80, 0x60, 0x13, 0x3d, 0x7e, 0xb3, 0xb7, 0x81,
        0x54, 0x0f, 0x82, 0x1f, 0x50, 0x48, 0x45, 0x5d, 0x7d, 0x9f, 0xda, 0x44, 0x8f, 0xba, 0xe2, 0x46, 0x58, 0x97,
        0x12, 0x38, 0xdf, 0xcc, 0x56, 0x4f, 0xc0, 0xbd, 0x88, 0x37, 0x82, 0x02, 0xdb, 0x84, 0x82, 0x63, 0x83, 0x1b,
        0xa0, 0xcf, 0x73, 0x63, 0xb8, 0x2b, 0x14, 0x87, 0xa5, 0xc9, 0x5e, 0x0c, 0xf9, 0x70, 0x62, 0xb8, 0x87, 0x47,
        0x23, 0x0e, 0xc1, 0x80, 0x45, 0xbb, 0xbc, 0x9b, 0xcf, 0xd7, 0x7d, 0xc5, 0x5c, 0xbc, 0x89, 0x2f, 0x7c, 0x6b,
        0xee, 0x4d, 0xc6, 0xb4, 0x02, 0x61, 0x32, 0x69, 0xd6, 0x27, 0x7c, 0x90, 0x63, 0xf0, 0x2e, 0x37, 0x69, 0x6b,
        0xbf, 0xdd, 0x05, 0xa4, 0x5d, 0x3e, 0x40, 0x24, 0x9f, 0x83, 0xb8, 0x31, 0x49, 0xfc, 0x1a, 0xae, 0x66, 0x0b,
        0x3a, 0xd4, 0x27, 0x93, 0x52, 0x6b, 0x87, 0x8d, 0xf5, 0x48, 0xe6, 0xb6, 0x23, 0xc9, 0xd4, 0x24, 0xaa, 0xc1,
        0x4c, 0x3f, 0x45, 0xbf, 0xad, 0x24, 0xd8, 0xf4, 0xc6, 0x65, 0xc8, 0x26, 0x61, 0x39, 0x45, 0x70, 0x79, 0x71,
        0x47, 0xe5, 0x07, 0xfb, 0xf3, 0x03, 0xad, 0xa3, 0xa1, 0x11, 0x04, 0x06, 0x42, 0x8f, 0x0b, 0xa0, 0x4b, 0xb4,
        0xa7, 0xb4, 0x35, 0xee, 0x40, 0xbf, 0xf6, 0x3d, 0xc3, 0x20, 0x4d, 0xca, 0x2e, 0x36, 0x43, 0xe7, 0x68, 0xf2,
        0x42, 0xe6, 0x7c, 0x42, 0x0a, 0xff, 0x69, 0x5e, 0xdf, 0xd7, 0x87, 0xf6, 0xe8, 0xe2, 0xbc, 0xa8, 0x2b, 0xae,
        0x8a, 0x53, 0xfd, 0x71, 0x03, 0x7c, 0xda, 0x8b, 0xca, 0xda, 0x46, 0x27, 0x2d, 0xfe, 0xee, 0xda, 0xb5, 0xe9,
        0xbe, 0x2c, 0xd8, 0xcf, 0xe6, 0x4e, 0xd2, 0x8d, 0x5e, 0x44, 0x77, 0xdb, 0xfc, 0xd3, 0xe0, 0xa4, 0x5e, 0x55,
        0x20, 0x66, 0xc4, 0x7f, 0x5f, 0xee, 0xb4, 0xc5, 0xad, 0xfb, 0xd7, 0xec, 0x38, 0x8c, 0xda, 0xcb, 0x89, 0x59,
        0x6c, 0x53, 0xa7, 0x28, 0x9a, 0x42, 0x91, 0x3b, 0x97, 0x77, 0xfa, 0x41, 0xbb, 0xbe, 0xdb, 0xcd, 0x93, 0x0b,
        0xf6, 0xda, 0x1c, 0x30, 0x7a, 0x5b, 0xe1, 0x1d, 0x4f, 0xd9, 0x17, 0xc1, 0x81, 0x2c, 0xf3, 0x7c, 0xec, 0xde,
        0xf3, 0x2b, 0xf7, 0xc1, 0x23, 0x8c, 0x07, 0x04, 0x63, 0x5e, 0xc2, 0x1a, 0x54, 0xd2, 0x18, 0x5e, 0xb7, 0xf3,
        0xb8, 0xb9, 0x1c, 0x94, 0x62, 0x42, 0x1d, 0x33, 0xbe, 0xb2, 0x41, 0xbf, 0xec, 0x41, 0xab, 0xce, 0x2f, 0x5a,
        0xb1, 0x4b, 0x05, 0xb5, 0xdb, 0xa7, 0x26, 0x5b, 0xa8, 0x6d, 0xd2, 0x96, 0x21, 0x11, 0xed, 0xb8, 0x46, 0x41,
        0x6c, 0xf3, 0x92, 0x02, 0xe9, 0x32, 0x19, 0x14, 0xdc, 0x5b, 0x12, 0xba, 0x21, 0xae, 0x54, 0x7b, 0xc3, 0xd0,
        0x25, 0x23, 0x11, 0xbf, 0x6d, 0x7c, 0x3c, 0x70, 0x05, 0x2b, 0x29, 0xe0, 0xe2, 0xe6, 0x29, 0x2d, 0x46, 0x56,
        0x05, 0xc6, 0x3f, 0xcd, 0x4f, 0x9e, 0x45, 0x09, 0x48, 0x2c, 0x35, 0xc7, 0x57, 0x68, 0x7c, 0x38, 0x34, 0xec,
        0x65, 0xc4, 0xe7, 0xce, 0xed, 0xeb, 0x6f, 0x0d, 0x59, 0x94, 0x1a, 0x8a, 0x3c, 0x56, 0x1b, 0x8d, 0xc1, 0x23,
        0x65, 0xe3, 0xe3, 0x8b, 0xe1, 0x4b, 0x6b, 0x91, 0x84, 0xcb, 0x2c, 0x8d, 0x28, 0xb1, 0x1d, 0xb9, 0x20, 0x6b,
        0xa3, 0xbb, 0x11, 0x7b, 0x0a, 0xa7, 0x96, 0xe0, 0x89, 0xcf, 0xc1, 0xea, 0x8d, 0x8a, 0x9c, 0x1f, 0xc5, 0x4d,
        0x49, 0x3f, 0xf1, 0x6b, 0x94, 0x2c, 0x9d, 0xb0, 0x97, 0xd3, 0x88, 0x34, 0x3c, 0x9b, 0x08, 0xf5, 0x9c, 0x25,
        0x0c, 0xce, 0x71, 0xe1, 0x76, 0x22, 0x72, 0x92, 0x1d, 0xec, 0x89, 0x88, 0x14, 0xf0, 0xc1, 0xf3, 0x4a, 0xe9,
        0xa4, 0xe1, 0x08, 0xf2, 0xc5, 0x37, 0x33, 0xa4, 0x3b, 0x0f, 0x2f, 0x85, 0x8e, 0xaa, 0x9c, 0x57, 0xe7, 0xad,
        0x92, 0x3c, 0x96, 0x87, 0x1b, 0x57, 0x6b, 0x85, 0xe1, 0xcb, 0xcb, 0x60, 0xd8, 0xd1, 0x13, 0x6d, 0x56, 0xe1,
        0x63, 0x4e, 0xd7, 0x75, 0xd1, 0x1a, 0xcd, 0x5c, 0x4e, 0x17, 0x5c, 0x18, 0x53, 0x54, 0xe2, 0x82, 0xd1, 0x90,
        0x68, 0xab, 0x7b, 0x42, 0xa2, 0xd0, 0xda, 0x7b, 0x25, 0xdb, 0x2e, 0x63, 0xcd, 0x62, 0x53, 0x8b, 0x19, 0x8d,
        0xce, 0x36, 0x06, 0x98, 0x47, 0xa5, 0x12, 0x51, 0xd9, 0xcc, 0xb8, 0x91, 0x7f, 0xa2, 0x18, 0xe9, 0x61, 0xf0,
        0xb7, 0x90, 0xd5, 0x47, 0xcf, 0x92, 0xca, 0x53, 0x8f, 0x83, 0x2f, 0x42, 0xa6, 0x71, 0x11, 0x3b, 0x53, 0x67,
        0x55, 0x6d, 0x9f, 0x5a, 0xca, 0x44, 0x7b, 0x96, 0x7a, 0x96, 0xbc, 0x1f, 0x1f, 0x6f, 0x20, 0x7d, 0x0d, 0xae,
        0x33, 0x09, 0xac, 0xe6, 0x66, 0x87, 0xd2, 0x97, 0xc6, 0x0c, 0x95, 0x58, 0xdc, 0xa0, 0xdc, 0x1f, 0x27, 0x99,
        0x35, 0x26, 0x86, 0xbb, 0xa7, 0x81, 0x55, 0x08, 0xf3, 0x43, 0xd7, 0x09, 0xa0, 0x9e, 0xe3, 0x96, 0xa9, 0x2d,
        0x7e, 0xd2, 0xed, 0x77, 0x07, 0xaa, 0x9b, 0xaf, 0xb1, 0x78, 0xa8, 0x51, 0x48, 0x82, 0x90, 0x53, 0x2e, 0x1c,
        0xb4, 0xc0, 0x9b, 0x0e, 0x38, 0x25, 0xb3, 0x65, 0xf3, 0x4f, 0x69, 0xca, 0x9a, 0xe7, 0x5b, 0x63, 0x51, 0xcf,
        0x1e, 0xbe, 0xf9, 0x4a, 0xf9, 0x2c, 0x3b, 0xf3, 0x58, 0x30, 0xc9, 0x99, 0xd9, 0xc8, 0x2e, 0x27, 0x07, 0x8e,
        0x7e, 0x1b, 0x85, 0xf8, 0x75, 0x71, 0x8a, 0x52, 0x15, 0x2e, 0x49, 0xe0, 0x6f, 0x9a, 0x3d, 0x0b, 0x77, 0xe9,
        0xcf, 0xfc, 0xa5, 0x2f, 0xa6, 0x7d, 0xfa, 0x28, 0xf8, 0xaf, 0xdb, 0x2b, 0xf3, 0x2c, 0xf4, 0x7b, 0xa4, 0xbd,
        0xf4, 0xae, 0xa9, 0x9d, 0x33, 0x41, 0xe4, 0x4f, 0x62, 0x6c, 0xec, 0x3d, 0xe7, 0x0b, 0xda, 0xc4, 0x19, 0xa5,
        0xb1, 0x71, 0xec, 0x38, 0xb7, 0x51, 0xa3, 0x5d, 0xe3, 0x3b, 0x6a, 0x8e, 0xb2, 0xe3, 0x99, 0x8a, 0x7b, 0x7c,
        0x45, 0x64, 0x5a, 0x69, 0x2d, 0x25, 0x5c, 0x1b, 0xc9, 0x2f, 0xa0, 0x6c, 0x92, 0x13, 0x2b, 0x20, 0x74, 0x88,
        0xe5, 0x54, 0x7a, 0x7d, 0x51, 0xec, 0xb2, 0xb9, 0x6e, 0xfd, 0x97, 0xa1, 0xec, 0x84, 0x45, 0x2c, 0x39, 0x5f,
        0xd1, 0x93, 0x56, 0xd0, 0x2a, 0x56, 0x26, 0x9c, 0xd8, 0x8f, 0x37, 0xb3, 0x0a, 0x28, 0x0b, 0x01, 0x4e, 0x74,
        0xd4, 0xd4, 0xa6, 0x6b, 0x91, 0x22, 0xaf, 0xff, 0x84, 0x23, 0x23, 0xa5, 0x21, 0xc5, 0x22, 0x45, 0xc7, 0x62,
        0xa5, 0x58, 0xba, 0x19, 0x8e, 0x5a, 0x4a, 0x98, 0xf5, 0x74, 0x38, 0x39, 0x13, 0xb1, 0x3b, 0x1d, 0x23, 0x12,
        0xf3, 0xa8, 0xab, 0xbb, 0x15, 0x31, 0xd8, 0xb8, 0x16, 0xef, 0x39, 0x29, 0x9f, 0x55, 0x96, 0xc6, 0x15, 0xc6,
        0x9e, 0xde, 0xad, 0x0c, 0x2d, 0xf3, 0x9b, 0x27, 0x89, 0xb6, 0x64, 0xde, 0xe7, 0xfb, 0xb1, 0x63, 0x82, 0xaf,
        0xdf, 0x61, 0xcf, 0x8b, 0xd3, 0xaa, 0xa4, 0xbf, 0x11, 0xf2, 0x47, 0x27, 0xf9, 0x87, 0xdd, 0xee, 0xba, 0xad,
        0xcd, 0xcb, 0xff, 0x4b, 0xff, 0xd1, 0xd2, 0xbf};
    /// <summary>What the dynamic stream unpacks to: its size, its 64-bit FNV-1a hash and its last words.</summary>
    constexpr size_t DynamicSize = 1880;
    constexpr uint64_t DynamicHash = 0xbd94f97d8dc50624ull;
    constexpr std::string_view DynamicEnd = "The Mad Cat walks. ";

    /// <summary>
    /// Zstandard frames made by the zstd tool (1.5.7, level 19, with its content checksum, as the version 2 pack's streams
    /// are): "MechCommander Gold: the Atlas falls.", and 60 lines of "Frame n: the Mad Cat walks, the Atlas falls, the
    /// Raven runs.", which unpack to ZstdSize bytes of ZstdHash (64-bit FNV-1a) ending ZstdEnd.
    /// </summary>
    constexpr std::array<uint8_t, 49> ZstdSmallStream = {
        0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x68, 0x21, 0x01, 0x00, 0x4d, 0x65, 0x63, 0x68, 0x43, 0x6f, 0x6d, 0x6d,
        0x61, 0x6e, 0x64, 0x65, 0x72, 0x20, 0x47, 0x6f, 0x6c, 0x64, 0x3a, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41,
        0x74, 0x6c, 0x61, 0x73, 0x20, 0x66, 0x61, 0x6c, 0x6c, 0x73, 0x2e, 0xce, 0x85, 0xf8, 0xd7};
    constexpr std::array<uint8_t, 185> ZstdStream = {
        0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x68, 0x65, 0x05, 0x00, 0x32, 0x88, 0x19, 0x18, 0x80, 0x6d, 0x0e, 0x00,
        0xc0, 0xf8, 0xdf, 0x41, 0xa5, 0x02, 0x8e, 0xe2, 0xfa, 0xdd, 0xbd, 0x93, 0x94, 0x62, 0x29, 0xda, 0xe4,
        0x12, 0x3d, 0x01, 0xef, 0xee, 0xed, 0xec, 0x7f, 0x9f, 0xbf, 0xd7, 0xf5, 0xd7, 0xae, 0x5b, 0xb3, 0x7e,
        0x77, 0xf3, 0xea, 0xea, 0xc7, 0x8e, 0x1b, 0x33, 0x3e, 0x37, 0xaf, 0x2e, 0x7e, 0xae, 0xb1, 0x37, 0xaf,
        0xae, 0x7d, 0xae, 0xb1, 0x37, 0xaf, 0xea, 0x73, 0x8d, 0xbd, 0x79, 0x15, 0x10, 0xa2, 0x67, 0x82, 0xf2,
        0x51, 0x28, 0x88, 0xc4, 0x08, 0x97, 0x8c, 0x60, 0x94, 0x88, 0x20, 0x6a, 0x22, 0xac, 0x52, 0x82, 0x45,
        0x26, 0xf0, 0x48, 0x85, 0x91, 0x72, 0x58, 0xa9, 0x38, 0x10, 0x32, 0x02, 0x3e, 0xa8, 0x11, 0x70, 0xf2,
        0x1e, 0xc3, 0x9e, 0x01, 0xe0, 0xa5, 0x61, 0x0d, 0x11, 0x24, 0x08, 0x42, 0xf0, 0xff, 0xff, 0x0a, 0x3f,
        0x9b, 0x22, 0x45, 0x8e, 0x18, 0x48, 0x88, 0x1c, 0x71, 0x44, 0xa9, 0x8c, 0x11, 0x44, 0x86, 0x8a, 0x94,
        0x11, 0x95, 0x28, 0x09, 0xa5, 0x11, 0x95, 0x24, 0x92, 0xa1, 0x40, 0x9e, 0x99, 0xc8, 0x44, 0x99, 0xc8,
        0xbc, 0x0d, 0x01, 0xe4, 0xaa, 0xd2, 0xb6, 0xfe, 0x0d, 0xd8, 0x55, 0x5e, 0x47, 0xb6, 0x72};
    constexpr size_t ZstdSize = 3710;
    constexpr uint64_t ZstdHash = 0xfaa3c4740cbbd29bull;
    constexpr std::string_view ZstdEnd = "the Raven runs.\n";

    /// <summary>
    /// Zstandard dictionaries made by zstd's ZDICT_finalizeDictionary (1.5.7) from lines of mechs' moves, the same
    /// content with ID 1 and with ID 2, and DictionaryText packed against the first (level 19, with its checksum: 36
    /// bytes, 79 without the dictionary).
    /// </summary>
    constexpr std::array<uint8_t, 436> DictionaryOne = {
        0x37, 0xa4, 0x30, 0xec, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x10, 0xf8, 0xec, 0x06, 0xff, 0x5f, 0x2f, 0xf7, 0x88,
        0x48, 0x19, 0x43, 0x40, 0x40, 0x40, 0x20, 0x10, 0x6c, 0xd4, 0x6c, 0x84, 0x88, 0x88, 0xc8, 0x03, 0x54, 0xa0,
        0x40, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0xc1, 0xf3, 0x52, 0x4a, 0x29, 0xa5, 0xaa, 0xaa, 0x07, 0x64, 0xc0, 0x80, 0x56, 0x83, 0xc1, 0x60,
        0x30, 0x18, 0x0c, 0x06, 0x83, 0xc1, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x18, 0x63, 0x8c,
        0x31, 0x66, 0x76, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x54, 0x68, 0x65,
        0x20, 0x52, 0x61, 0x76, 0x65, 0x6e, 0x20, 0x77, 0x61, 0x6c, 0x6b, 0x73, 0x20, 0x61, 0x74, 0x20, 0x64, 0x61,
        0x77, 0x6e, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41, 0x74, 0x6c, 0x61, 0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64,
        0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x72, 0x69, 0x64, 0x67, 0x65, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4d,
        0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69, 0x72, 0x63, 0x6c, 0x65, 0x73, 0x20, 0x65, 0x61, 0x73,
        0x74, 0x2e, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x52, 0x61, 0x76, 0x65, 0x6e, 0x20, 0x77, 0x61, 0x6c, 0x6b, 0x73,
        0x20, 0x61, 0x74, 0x20, 0x64, 0x75, 0x73, 0x6b, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41, 0x74, 0x6c, 0x61,
        0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x70, 0x61, 0x73, 0x73, 0x3b, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x4d, 0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69, 0x72, 0x63, 0x6c, 0x65,
        0x73, 0x20, 0x77, 0x65, 0x73, 0x74, 0x2e, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x52, 0x61, 0x76, 0x65, 0x6e, 0x20,
        0x72, 0x75, 0x6e, 0x73, 0x20, 0x61, 0x74, 0x20, 0x6e, 0x6f, 0x6f, 0x6e, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20,
        0x41, 0x74, 0x6c, 0x61, 0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x67, 0x61,
        0x74, 0x65, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4d, 0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69,
        0x72, 0x63, 0x6c, 0x65, 0x73, 0x20, 0x6e, 0x6f, 0x72, 0x74, 0x68, 0x2e, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x52,
        0x61, 0x76, 0x65, 0x6e, 0x20, 0x77, 0x61, 0x69, 0x74, 0x73, 0x20, 0x61, 0x74, 0x20, 0x6e, 0x69, 0x67, 0x68,
        0x74, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41, 0x74, 0x6c, 0x61, 0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64, 0x73,
        0x20, 0x74, 0x68, 0x65, 0x20, 0x62, 0x72, 0x69, 0x64, 0x67, 0x65, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4d,
        0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69, 0x72, 0x63, 0x6c, 0x65, 0x73, 0x20, 0x73, 0x6f, 0x75,
        0x74, 0x68, 0x2e, 0x0a};
    constexpr std::array<uint8_t, 436> DictionaryTwo = {
        0x37, 0xa4, 0x30, 0xec, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x10, 0xf8, 0xec, 0x06, 0xff, 0x5f, 0x2f, 0xf7, 0x88,
        0x48, 0x19, 0x43, 0x40, 0x40, 0x40, 0x20, 0x10, 0x6c, 0xd4, 0x6c, 0x84, 0x88, 0x88, 0xc8, 0x03, 0x54, 0xa0,
        0x40, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
        0x41, 0x41, 0xc1, 0xf3, 0x52, 0x4a, 0x29, 0xa5, 0xaa, 0xaa, 0x07, 0x64, 0xc0, 0x80, 0x56, 0x83, 0xc1, 0x60,
        0x30, 0x18, 0x0c, 0x06, 0x83, 0xc1, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x18, 0x63, 0x8c,
        0x31, 0x66, 0x76, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x54, 0x68, 0x65,
        0x20, 0x52, 0x61, 0x76, 0x65, 0x6e, 0x20, 0x77, 0x61, 0x6c, 0x6b, 0x73, 0x20, 0x61, 0x74, 0x20, 0x64, 0x61,
        0x77, 0x6e, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41, 0x74, 0x6c, 0x61, 0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64,
        0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x72, 0x69, 0x64, 0x67, 0x65, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4d,
        0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69, 0x72, 0x63, 0x6c, 0x65, 0x73, 0x20, 0x65, 0x61, 0x73,
        0x74, 0x2e, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x52, 0x61, 0x76, 0x65, 0x6e, 0x20, 0x77, 0x61, 0x6c, 0x6b, 0x73,
        0x20, 0x61, 0x74, 0x20, 0x64, 0x75, 0x73, 0x6b, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41, 0x74, 0x6c, 0x61,
        0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x70, 0x61, 0x73, 0x73, 0x3b, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x4d, 0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69, 0x72, 0x63, 0x6c, 0x65,
        0x73, 0x20, 0x77, 0x65, 0x73, 0x74, 0x2e, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x52, 0x61, 0x76, 0x65, 0x6e, 0x20,
        0x72, 0x75, 0x6e, 0x73, 0x20, 0x61, 0x74, 0x20, 0x6e, 0x6f, 0x6f, 0x6e, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20,
        0x41, 0x74, 0x6c, 0x61, 0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x67, 0x61,
        0x74, 0x65, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4d, 0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69,
        0x72, 0x63, 0x6c, 0x65, 0x73, 0x20, 0x6e, 0x6f, 0x72, 0x74, 0x68, 0x2e, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x52,
        0x61, 0x76, 0x65, 0x6e, 0x20, 0x77, 0x61, 0x69, 0x74, 0x73, 0x20, 0x61, 0x74, 0x20, 0x6e, 0x69, 0x67, 0x68,
        0x74, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x41, 0x74, 0x6c, 0x61, 0x73, 0x20, 0x68, 0x6f, 0x6c, 0x64, 0x73,
        0x20, 0x74, 0x68, 0x65, 0x20, 0x62, 0x72, 0x69, 0x64, 0x67, 0x65, 0x3b, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4d,
        0x61, 0x64, 0x20, 0x43, 0x61, 0x74, 0x20, 0x63, 0x69, 0x72, 0x63, 0x6c, 0x65, 0x73, 0x20, 0x73, 0x6f, 0x75,
        0x74, 0x68, 0x2e, 0x0a};
    constexpr std::array<uint8_t, 36> DictionaryFrame = {
        0x28, 0xb5, 0x2f, 0xfd, 0x25, 0x01, 0x5e, 0xb5, 0x00, 0x00, 0x30, 0x54, 0x68, 0x20, 0x54, 0x2e, 0x0a, 0x05,
        0xf0, 0x1e, 0x82, 0x88, 0x1b, 0x46, 0xb1, 0x86, 0xb6, 0x16, 0x32, 0x7a, 0x14, 0x06, 0xb6, 0x9f, 0x5a, 0xba};
    constexpr std::string_view DictionaryText =
        "The Raven runs at dusk; the Atlas holds the ridge; the Mad Cat circles north. The Raven runs.\n";

    uint64_t Fnv(std::span<const uint8_t> bytes)
    {
        uint64_t hash = 0xcbf29ce484222325ull;

        for (const uint8_t byte : bytes)
        {
            hash = (hash ^ byte) * 0x100000001b3ull;
        }

        return hash;
    }

    std::span<const uint8_t> AsBytes(std::string_view text)
    {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }

    /// <summary>
    /// <paramref name="data"/> as stored DEFLATE blocks (a packed entry that packs nothing), each up to a stored block's
    /// most bytes, the last marked final.
    /// </summary>
    std::vector<uint8_t> StoredDeflate(std::span<const uint8_t> data)
    {
        constexpr size_t MostStored = 0xffff;
        std::vector<uint8_t> stream;
        size_t at = 0;

        do
        {
            const auto length = static_cast<uint16_t>(std::min(MostStored, data.size() - at));
            const uint8_t last = at + length == data.size() ? 1 : 0;
            stream.insert(stream.end(), {last, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
                                         static_cast<uint8_t>(~length), static_cast<uint8_t>(~length >> 8)});
            stream.insert(stream.end(), data.begin() + static_cast<ptrdiff_t>(at),
                          data.begin() + static_cast<ptrdiff_t>(at + length));
            at += length;
        } while (at < data.size());

        return stream;
    }

    /// <summary>An archive entry to write: its name, its stored bytes, its unpacked size, and an earlier entry it shares.</summary>
    struct ArchiveEntry
    {
        std::string Name;
        std::vector<uint8_t> Stored;
        size_t RealSize = 0;
        std::optional<size_t> Shares;
    };

    /// <summary>A FastFile of <paramref name="entries"/> (the HD art pack's archive layout).</summary>
    std::vector<uint8_t> BuildArchive(const std::vector<ArchiveEntry>& entries)
    {
        std::vector<uint8_t> out(sizeof(int32_t) + entries.size() * sizeof(MCFileEntry));
        const auto count = static_cast<int32_t>(entries.size());
        std::memcpy(out.data(), &count, sizeof(count));
        std::vector<int32_t> offsets;

        for (size_t i = 0; i < entries.size(); i++)
        {
            const ArchiveEntry& entry = entries[i];
            int32_t offset = 0;

            if (entry.Shares.has_value())
            {
                offset = offsets[*entry.Shares];
            }
            else
            {
                offset = static_cast<int32_t>(out.size());
                out.insert(out.end(), entry.Stored.begin(), entry.Stored.end());
            }

            offsets.push_back(offset);
            MCFileEntry record{};
            record.Offset = offset;
            record.Size = static_cast<int32_t>(entry.Shares.has_value() ? entries[*entry.Shares].Stored.size()
                                                                        : entry.Stored.size());
            record.RealSize = static_cast<int32_t>(entry.RealSize);
            std::ranges::copy(entry.Name, record.Name.begin());
            std::memcpy(out.data() + sizeof(int32_t) + i * sizeof(MCFileEntry), &record, sizeof(record));
        }

        return out;
    }

    /// <summary>A little-endian <paramref name="value"/> written at <paramref name="at"/> of <paramref name="out"/>.</summary>
    template <typename T> void PutAt(std::vector<uint8_t>& out, size_t at, T value)
    {
        std::memcpy(out.data() + at, &value, sizeof(T));
    }

    /// <summary>A facts record (SPEC-pack-v2.md's "The facts"): its type, 2 bytes of 0, its data's size and its data.</summary>
    std::vector<uint8_t> FactsRecord(MCPackRecords::Type type, std::span<const uint8_t> data)
    {
        constexpr size_t HeadBytes = 8;
        std::vector<uint8_t> record(HeadBytes);
        PutAt(record, 0, static_cast<uint16_t>(type));
        PutAt(record, 4, static_cast<uint32_t>(data.size()));
        record.insert(record.end(), data.begin(), data.end());
        return record;
    }

    /// <summary>
    /// A version 2 archive's stream to write (BuildVersion2Streams): its entries, its kind, and what it stores in place
    /// of its entries' bytes, packed by Codec against Dictionary (empty: their bytes, stored as they are).
    /// </summary>
    struct Version2Stream
    {
        std::vector<ArchiveEntry> Entries;
        MCFastFile::StreamKind Kind = MCFastFile::StreamKind::Packet;
        std::vector<uint8_t> Packed;
        uint8_t Codec = 0;
        uint16_t Dictionary = 0;
    };

    /// <summary>
    /// A version 2 HD archive (SPEC-pack-v2.md) of <paramref name="streams"/>, each stored as it is (codec 0) or as
    /// packed (Version2Stream::Packed), in their order, and its entries (their stored bytes as they unpack, no sharing)
    /// in theirs: each found by its path's key, each folder its first name is in by its own, with the facts
    /// <paramref name="facts"/> gives a folder (its records, FactsRecord's); with <paramref name="names"/>, each entry's
    /// path after the data, in the table's order, as pack_v2.py writes them.
    /// </summary>
    std::vector<uint8_t> BuildVersion2Streams(const std::vector<Version2Stream>& streams,
                                              const std::map<std::string, std::vector<uint8_t>>& facts = {},
                                              bool names = false)
    {
        constexpr size_t HeaderBytes = 64;
        constexpr size_t FolderRowBytes = 16;
        constexpr size_t StreamRowBytes = 24;
        constexpr size_t StreamCodecAt = 16;
        constexpr size_t StreamKindAt = 17;
        constexpr size_t StreamDictionaryAt = 18;
        constexpr size_t EntryRowBytes = 20;
        constexpr uint16_t Format = 2;
        std::vector<std::pair<uint64_t, std::array<uint32_t, 3>>> rows;
        std::map<uint64_t, std::string> nameOf;
        std::set<uint64_t> folders;
        std::vector<uint8_t> data;
        // Each stream's start in the data, and its bytes unpacked.
        std::vector<size_t> starts;
        std::vector<uint32_t> unpacked;

        for (uint32_t number = 0; number < streams.size(); ++number)
        {
            const Version2Stream& stream = streams[number];
            starts.push_back(data.size());
            uint32_t at = 0;

            for (const ArchiveEntry& entry : stream.Entries)
            {
                rows.push_back(
                    {MCFastFile::PathKey(entry.Name), {number, at, static_cast<uint32_t>(entry.Stored.size())}});
                nameOf[MCFastFile::PathKey(entry.Name)] = entry.Name;
                at += static_cast<uint32_t>(entry.Stored.size());

                if (stream.Packed.empty())
                {
                    data.insert(data.end(), entry.Stored.begin(), entry.Stored.end());
                }

                if (const size_t cut = entry.Name.find('\\'); cut != std::string::npos)
                {
                    folders.insert(MCFastFile::PathKey(std::string_view(entry.Name).substr(0, cut)));
                }
            }

            data.insert(data.end(), stream.Packed.begin(), stream.Packed.end());
            unpacked.push_back(at);
        }

        std::map<uint64_t, std::span<const uint8_t>> factsOf;
        size_t factsBytes = 0;

        for (const auto& [folder, records] : facts)
        {
            folders.insert(MCFastFile::PathKey(folder));
            factsOf[MCFastFile::PathKey(folder)] = records;
            factsBytes += records.size();
        }

        std::ranges::sort(rows);
        std::vector<uint8_t> table(folders.size() * FolderRowBytes + streams.size() * StreamRowBytes +
                                   rows.size() * EntryRowBytes);
        size_t at = 0;
        std::vector<uint8_t> allFacts;

        for (const uint64_t folder : folders)
        {
            PutAt(table, at, folder);

            if (const auto found = factsOf.find(folder); found != factsOf.end())
            {
                PutAt(table, at + 8, static_cast<uint32_t>(allFacts.size()));
                PutAt(table, at + 12, static_cast<uint32_t>(found->second.size()));
                allFacts.insert(allFacts.end(), found->second.begin(), found->second.end());
            }

            at += FolderRowBytes;
        }

        table.insert(table.end(), allFacts.begin(), allFacts.end());

        const uint64_t dataAt = HeaderBytes + table.size();

        for (size_t number = 0; number < streams.size(); ++number)
        {
            const size_t end = number + 1 < streams.size() ? starts[number + 1] : data.size();
            const auto bytes = static_cast<uint32_t>(end - starts[number]);
            PutAt(table, at, dataAt + starts[number]);
            PutAt(table, at + 8, bytes);
            PutAt(table, at + 12, unpacked[number]);
            PutAt(table, at + StreamCodecAt, streams[number].Codec);
            PutAt(table, at + StreamKindAt, std::to_underlying(streams[number].Kind));
            PutAt(table, at + StreamDictionaryAt, streams[number].Dictionary);
            at += StreamRowBytes;
        }

        for (const auto& [key, place] : rows)
        {
            PutAt(table, at, key);

            for (size_t value = 0; value < place.size(); ++value)
            {
                PutAt(table, at + 8 + value * sizeof(uint32_t), place[value]);
            }

            at += EntryRowBytes;
        }

        std::vector<uint8_t> out(HeaderBytes);
        std::ranges::copy(std::string_view("MCH2"), out.begin());
        PutAt(out, 4, Format);
        PutAt(out, 8, static_cast<uint32_t>(table.size()));
        PutAt(out, 12, static_cast<uint32_t>(table.size()));
        PutAt(out, 20, static_cast<uint32_t>(folders.size()));
        PutAt(out, 24, static_cast<uint32_t>(streams.size()));
        PutAt(out, 28, static_cast<uint32_t>(rows.size()));
        PutAt(out, 32, static_cast<uint32_t>(factsBytes));
        PutAt(out, 40, dataAt);
        PutAt(out, 48, static_cast<uint64_t>(XXH3_64bits(table.data(), table.size())));
        out.insert(out.end(), table.begin(), table.end());
        out.insert(out.end(), data.begin(), data.end());

        if (names)
        {
            constexpr size_t NamesAt = 56;
            PutAt(out, NamesAt, static_cast<uint64_t>(out.size()));

            for (const auto& [key, place] : rows)
            {
                const std::string& name = nameOf.at(key);
                out.insert(out.end(), name.begin(), name.end());
                out.push_back(0);
            }
        }

        return out;
    }

    /// <summary>
    /// A version 2 HD archive (SPEC-pack-v2.md) of <paramref name="entries"/> (their stored bytes as they unpack, no
    /// sharing), all in one packet's stream stored as it is (codec 0) in their order, with the facts
    /// <paramref name="facts"/> gives a folder (BuildVersion2Streams).
    /// </summary>
    std::vector<uint8_t> BuildVersion2Archive(const std::vector<ArchiveEntry>& entries,
                                              const std::map<std::string, std::vector<uint8_t>>& facts = {})
    {
        return BuildVersion2Streams({Version2Stream{.Entries = entries}}, facts);
    }

    /// <summary>The version of archive a test packs its pictures in: 1, or 2 (SPEC-pack-v2.md).</summary>
    enum class PackVersion
    {
        One,
        Two,
    };

    /// <summary>A version 2 stream's codec: Zstandard (SPEC-pack-v2.md's streams).</summary>
    constexpr uint8_t ZstdCodec = 2;

    /// <summary>The pack's zstd dictionaries' file, at its root (SPEC-pack-v2.md's <c>dicts.hdd</c>).</summary>
    constexpr std::string_view PackDictionariesName = "dicts.hdd";

    /// <summary>
    /// A pack's <c>dicts.hdd</c> holding <paramref name="dictionaries"/> by number, in their order, its head giving
    /// <paramref name="count"/> of them (none: how many there are).
    /// </summary>
    std::vector<uint8_t> DictionariesFile(
        const std::vector<std::pair<uint16_t, std::span<const uint8_t>>>& dictionaries,
        std::optional<uint16_t> count = std::nullopt)
    {
        constexpr uint16_t Format = 2;
        constexpr size_t HeadBytes = 8;
        constexpr size_t DictionaryHeadBytes = 6;
        std::vector<uint8_t> file(HeadBytes);
        std::ranges::copy(std::string_view("MCD2"), file.begin());
        PutAt(file, 4, Format);
        PutAt(file, 6, count.value_or(static_cast<uint16_t>(dictionaries.size())));

        for (const auto& [number, bytes] : dictionaries)
        {
            const size_t at = file.size();
            file.resize(at + DictionaryHeadBytes);
            PutAt(file, at, number);
            PutAt(file, at + 2, static_cast<uint32_t>(bytes.size()));
            file.insert(file.end(), bytes.begin(), bytes.end());
        }

        return file;
    }

    /// <summary>
    /// A Zstandard frame of <paramref name="content"/> in raw blocks that names dictionary <paramref name="id"/> in its
    /// header, as one packed against it does, with its checksum: the decoder checks both, and the content needs no
    /// dictionary to come out (the game has no Zstandard packer to make a real one).
    /// </summary>
    std::vector<uint8_t> RawZstdFrame(std::span<const uint8_t> content, uint8_t id)
    {
        // The descriptor: a 4-byte content size (bits 6-7: 2), one segment (bit 5), a checksum (bit 2) and a 1-byte
        // dictionary ID (bits 0-1: 1). Each block's 3-byte head: whether it's the last (bit 0), its type (bits 1-2: 0,
        // raw) and its size (from bit 3), at most 128 KB.
        constexpr std::array<uint8_t, 4> Magic = {0x28, 0xb5, 0x2f, 0xfd};
        constexpr uint8_t Descriptor = 0xa5;
        constexpr size_t MostBlockBytes = 128 * 1024;
        constexpr size_t BlockHeadBytes = 3;
        constexpr uint32_t LastBlock = 1;
        constexpr int BlockSizeShift = 3;
        std::vector<uint8_t> frame(Magic.begin(), Magic.end());
        frame.push_back(Descriptor);
        frame.push_back(id);
        frame.resize(frame.size() + sizeof(uint32_t));
        PutAt(frame, frame.size() - sizeof(uint32_t), static_cast<uint32_t>(content.size()));
        size_t at = 0;

        do
        {
            const size_t bytes = std::min(MostBlockBytes, content.size() - at);
            const uint32_t head =
                static_cast<uint32_t>(bytes) << BlockSizeShift | (at + bytes == content.size() ? LastBlock : 0);
            const std::array<uint8_t, 4> headBytes = std::bit_cast<std::array<uint8_t, 4>>(head);
            frame.insert(frame.end(), headBytes.begin(), headBytes.begin() + BlockHeadBytes);
            frame.insert(frame.end(), content.begin() + static_cast<ptrdiff_t>(at),
                         content.begin() + static_cast<ptrdiff_t>(at + bytes));
            at += bytes;
        } while (at < content.size());

        // The checksum: XXH64's low 32 bits.
        frame.resize(frame.size() + sizeof(uint32_t));
        PutAt(frame, frame.size() - sizeof(uint32_t), static_cast<uint32_t>(XXH64(content.data(), content.size(), 0)));
        return frame;
    }

    /// <summary>
    /// A version 2 archive of <paramref name="entries"/> (their bytes as they unpack) in one stream of
    /// <paramref name="kind"/>, packed as a Zstandard frame naming dictionary <paramref name="id"/> (RawZstdFrame), and
    /// whose row names dictionary <paramref name="number"/> of the pack's.
    /// </summary>
    std::vector<uint8_t> BuildDictionaryArchive(const std::vector<ArchiveEntry>& entries, MCFastFile::StreamKind kind,
                                                uint16_t number, uint8_t id)
    {
        std::vector<uint8_t> content;

        for (const ArchiveEntry& entry : entries)
        {
            content.insert(content.end(), entry.Stored.begin(), entry.Stored.end());
        }

        return BuildVersion2Streams({Version2Stream{.Entries = entries,
                                                    .Kind = kind,
                                                    .Packed = RawZstdFrame(content, id),
                                                    .Codec = ZstdCodec,
                                                    .Dictionary = number}});
    }

    void WriteBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    /// <summary>A bundle entry of <paramref name="name"/> holding <paramref name="bytes"/>, stored raw.</summary>
    ArchiveEntry Raw(std::string name, std::vector<uint8_t> bytes)
    {
        const size_t size = bytes.size();
        return {std::move(name), std::move(bytes), size, std::nullopt};
    }

    /// <summary>A scratch folder of the test's own under the temp folder, deleted with it.</summary>
    class ScratchFolder : public MCScratchFolder
    {
    public:
        explicit ScratchFolder(std::string_view name) : MCScratchFolder("mc_hd_pack_tests", name) {}
    };

    /// <summary>The original's size of TileFrameFile's tile.</summary>
    constexpr int32_t TileWidth = 9;
    constexpr int32_t TileHeight = 7;

    /// <summary>A terrain tile's frame file (TileWidth x TileHeight at 3x).</summary>
    std::vector<uint8_t> TileFrameFile()
    {
        constexpr int32_t Scale = 3;
        MCHdArt::Frame frame;
        frame.Width = TileWidth * Scale;
        frame.Height = TileHeight * Scale;
        frame.Scale = Scale;
        frame.Colors = SpriteLike(frame.Width, frame.Height);
        frame.Indices.assign(static_cast<size_t>(TileWidth) * TileHeight * MCHdArt::IndexBytes, MCHdArt::HasIndex);
        return MCHdArt::EncodeHdf(frame);
    }
}

/// <summary>
/// DEFLATE: a stored, a fixed-Huffman and a dynamic-Huffman stream (Python's zlib) unpack to what was packed; a stream
/// cut short, one with a damaged stored length, or one asked for another size is refused.
/// </summary>
TEST_CASE("hd art: DEFLATE streams inflate, and damaged ones are refused")
{
    const auto stored = MCInflate::Inflate(StoredStream, 28);
    REQUIRE(stored.has_value());
    CHECK(std::ranges::equal(*stored, AsBytes("stored block, no compression")));

    const std::string_view fixedText = "MechCommander Gold: the Mad Cat walks, t";
    const auto fixed = MCInflate::Inflate(FixedStream, fixedText.size());
    REQUIRE(fixed.has_value());
    CHECK(std::ranges::equal(*fixed, AsBytes(fixedText)));

    const auto dynamic = MCInflate::Inflate(DynamicStream, DynamicSize);
    REQUIRE(dynamic.has_value());
    CHECK_EQ(Fnv(*dynamic), DynamicHash);
    CHECK(std::ranges::equal(std::span(*dynamic).last(DynamicEnd.size()), AsBytes(DynamicEnd)));

    CHECK(!MCInflate::Inflate(std::span(DynamicStream).first(DynamicStream.size() / 2), DynamicSize).has_value());
    CHECK(!MCInflate::Inflate(DynamicStream, DynamicSize - 1).has_value());
    CHECK(!MCInflate::Inflate(DynamicStream, DynamicSize + 1).has_value());
    std::array<uint8_t, StoredStream.size()> damaged = StoredStream;
    damaged[3] ^= 0xff;
    CHECK(!MCInflate::Inflate(damaged, 28).has_value());
}

/// <summary>
/// Zstandard: a small frame and one of Huffman-coded sequences (the zstd tool's, level 19, with a checksum) unpack to what
/// was packed; a frame cut short, one with a byte past it, one asked for another size, one whose checksum fails, and one
/// made without a checksum are refused.
/// </summary>
TEST_CASE("hd art: Zstandard streams unpack, and damaged ones are refused")
{
    const std::string_view smallText = "MechCommander Gold: the Atlas falls.";
    const auto small = MCZstd::Unpack(ZstdSmallStream, smallText.size());
    REQUIRE(small.has_value());
    CHECK(std::ranges::equal(*small, AsBytes(smallText)));

    const auto big = MCZstd::Unpack(ZstdStream, ZstdSize);
    REQUIRE(big.has_value());
    CHECK_EQ(Fnv(*big), ZstdHash);
    CHECK(std::ranges::equal(std::span(*big).last(ZstdEnd.size()), AsBytes(ZstdEnd)));

    CHECK(!MCZstd::Unpack(std::span(ZstdStream).first(ZstdStream.size() / 2), ZstdSize).has_value());
    CHECK(!MCZstd::Unpack(ZstdStream, ZstdSize - 1).has_value());
    CHECK(!MCZstd::Unpack(ZstdStream, ZstdSize + 1).has_value());

    std::vector<uint8_t> trailing(ZstdStream.begin(), ZstdStream.end());
    trailing.push_back(0);
    CHECK(!MCZstd::Unpack(trailing, ZstdSize).has_value());

    // The last 4 bytes are the checksum: a changed one fails it, the content unchanged.
    std::array<uint8_t, ZstdSmallStream.size()> damaged = ZstdSmallStream;
    damaged.back() ^= 0xff;
    CHECK(!MCZstd::Unpack(damaged, smallText.size()).has_value());

    // The frame header's descriptor without its checksum flag (bit 2), the checksum cut off: a whole frame, refused.
    std::vector<uint8_t> unchecked(ZstdSmallStream.begin(), ZstdSmallStream.end() - 4);
    unchecked[4] &= static_cast<uint8_t>(~0x04);
    CHECK(!MCZstd::Unpack(unchecked, smallText.size()).has_value());
}

/// <summary>
/// Zstandard dictionaries (SPEC-pack-v2.md's dicts.hdd): a frame packed against one unpacks against it alone, not against
/// its twin of another ID nor without one, and a frame packed without one isn't unpacked against one. Bytes that aren't a
/// trained dictionary (text, an ID of 0, the head without its tables) are refused.
/// </summary>
TEST_CASE("hd art: a Zstandard stream packed against a dictionary unpacks against it alone")
{
    const auto one = MCZstd::Dictionary::Make(DictionaryOne);
    const auto two = MCZstd::Dictionary::Make(DictionaryTwo);
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    CHECK_EQ((*one)->Id(), 1u);
    CHECK_EQ((*two)->Id(), 2u);

    const auto unpacked = MCZstd::Unpack(DictionaryFrame, DictionaryText.size(), std::cref(**one));
    REQUIRE(unpacked.has_value());
    CHECK(std::ranges::equal(*unpacked, AsBytes(DictionaryText)));
    CHECK(!MCZstd::Unpack(DictionaryFrame, DictionaryText.size(), std::cref(**two)).has_value());
    CHECK(!MCZstd::Unpack(DictionaryFrame, DictionaryText.size()).has_value());

    const std::string_view smallText = "MechCommander Gold: the Atlas falls.";
    CHECK(!MCZstd::Unpack(ZstdSmallStream, smallText.size(), std::cref(**one)).has_value());

    // A frame of raw blocks naming dictionary 1 unpacks against it (the archives' tests' frames).
    const std::vector<uint8_t> raw = RawZstdFrame(AsBytes(DictionaryText), 1);
    const auto rawUnpacked = MCZstd::Unpack(raw, DictionaryText.size(), std::cref(**one));
    REQUIRE(rawUnpacked.has_value());
    CHECK(std::ranges::equal(*rawUnpacked, AsBytes(DictionaryText)));
    CHECK(!MCZstd::Unpack(raw, DictionaryText.size(), std::cref(**two)).has_value());

    constexpr size_t IdAt = 4;
    constexpr size_t HeadBytes = 8;
    std::vector<uint8_t> noId(DictionaryOne.begin(), DictionaryOne.end());
    PutAt(noId, IdAt, uint32_t{0});
    CHECK(!MCZstd::Dictionary::Make(AsBytes(DictionaryText)).has_value());
    CHECK(!MCZstd::Dictionary::Make(noId).has_value());
    CHECK(!MCZstd::Dictionary::Make(std::span(DictionaryOne).first(HeadBytes)).has_value());
}

/// <summary>
/// A pack's dicts.hdd (SPEC-pack-v2.md) is read by number, an empty one too; one of another kind or format, a number 0
/// or given twice, a count other than the head's, a dictionary of no bytes, cut short or that isn't one, a head cut
/// short and bytes past the last are refused whole.
/// </summary>
TEST_CASE("hd art: a pack's dicts.hdd is read by number, and a damaged one is refused whole")
{
    const std::vector<uint8_t> good = DictionariesFile({{1, DictionaryOne}, {2, DictionaryTwo}});
    const auto read = MCZstd::Dictionaries::Read(good);
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->Count(), 2u);
    REQUIRE((*read)->Find(1).has_value());
    REQUIRE((*read)->Find(2).has_value());
    CHECK_EQ((*read)->Find(1)->get().Id(), 1u);
    CHECK_EQ((*read)->Find(2)->get().Id(), 2u);
    CHECK(!(*read)->Find(0).has_value());
    CHECK(!(*read)->Find(3).has_value());

    const auto empty = MCZstd::Dictionaries::Read(DictionariesFile({}));
    REQUIRE(empty.has_value());
    CHECK_EQ((*empty)->Count(), 0u);

    constexpr size_t FormatAt = 4;
    std::vector<uint8_t> magic = good;
    magic[3] = '3';
    std::vector<uint8_t> format = good;
    PutAt(format, FormatAt, uint16_t{3});
    std::vector<uint8_t> trailing = good;
    trailing.insert(trailing.end(), {0, 0, 0});
    const std::vector<uint8_t> noBytes = DictionariesFile({{1, {}}});
    const std::vector<uint8_t> numberZero = DictionariesFile({{0, DictionaryOne}});
    const std::vector<uint8_t> twice = DictionariesFile({{1, DictionaryOne}, {1, DictionaryTwo}});
    const std::vector<uint8_t> miscounted = DictionariesFile({{1, DictionaryOne}}, uint16_t{2});
    const std::vector<uint8_t> text = DictionariesFile({{1, AsBytes(DictionaryText)}});
    const std::vector<std::pair<std::string_view, std::span<const uint8_t>>> bad = {
        {"another kind", magic},
        {"a later format", format},
        {"bytes past the last", trailing},
        {"no bytes", noBytes},
        {"number 0", numberZero},
        {"a number twice", twice},
        {"another count", miscounted},
        {"not a dictionary", text},
        {"cut inside a dictionary", std::span(good).first(good.size() - 1)},
        {"cut inside the head", std::span(good).first(5)},
    };

    for (const auto& [what, file] : bad)
    {
        MCTest::Scope scope{std::string(what)};
        CHECK(!MCZstd::Dictionaries::Read(file).has_value());
    }
}

/// <summary>
/// A version 2 archive's stream packed against a dictionary (its row's number) opens with the pack's dictionaries and
/// its entries unpack against it; without them, or with a set lacking its number, the archive is refused whole, as one
/// naming a dictionary for a codec that takes none. With another dictionary under its number, it opens and the stream's
/// unpack is refused.
/// </summary>
TEST_CASE("hd art: a version 2 stream packed against a dictionary opens with the pack's dictionaries alone")
{
    const ScratchFolder scratch("dictionary-stream");
    const std::span<const uint8_t> text = AsBytes(DictionaryText);
    const size_t half = text.size() / 2;
    const std::vector<ArchiveEntry> entries = {
        Raw("a\\0.hdf", {text.begin(), text.begin() + static_cast<ptrdiff_t>(half)}),
        Raw("a\\1.hdf", {text.begin() + static_cast<ptrdiff_t>(half), text.end()})};
    const std::filesystem::path path = scratch.Path() / "a.hda";
    const std::string name = MCFileSystem::ToUtf8(path);
    WriteBytes(path, BuildVersion2Streams({Version2Stream{.Entries = entries,
                                                          .Packed = {DictionaryFrame.begin(), DictionaryFrame.end()},
                                                          .Codec = ZstdCodec,
                                                          .Dictionary = 1}}));

    const auto dictionaries = MCZstd::Dictionaries::Read(DictionariesFile({{1, DictionaryOne}}));
    REQUIRE(dictionaries.has_value());
    const auto archive = MCFastFile::Create(name, *dictionaries);
    REQUIRE(archive.has_value());

    for (const ArchiveEntry& entry : entries)
    {
        MCTest::Scope scope(entry.Name);
        const std::optional<int32_t> index = (*archive)->Find(entry.Name);
        REQUIRE(index.has_value());
        const auto bytes = (*archive)->Unpack(*index, (*archive)->ReadStored(*index));
        REQUIRE(bytes.has_value());
        CHECK(*bytes == entry.Stored);
    }

    const auto none = MCFastFile::Create(name);
    REQUIRE(!none.has_value());
    CHECK(none.error().find("dicts.hdd lacks") != std::string::npos);
    const auto otherNumber = MCZstd::Dictionaries::Read(DictionariesFile({{2, DictionaryOne}}));
    REQUIRE(otherNumber.has_value());
    CHECK(!MCFastFile::Create(name, *otherNumber).has_value());

    const auto twin = MCZstd::Dictionaries::Read(DictionariesFile({{1, DictionaryTwo}}));
    REQUIRE(twin.has_value());
    const auto wrong = MCFastFile::Create(name, *twin);
    REQUIRE(wrong.has_value());
    const std::optional<int32_t> first = (*wrong)->Find(entries[0].Name);
    REQUIRE(first.has_value());
    CHECK(!(*wrong)->Unpack(*first, (*wrong)->ReadStored(*first)).has_value());

    constexpr uint8_t DeflateCodec = 1;
    WriteBytes(path, BuildVersion2Streams({Version2Stream{.Entries = entries,
                                                          .Packed = {DictionaryFrame.begin(), DictionaryFrame.end()},
                                                          .Codec = DeflateCodec,
                                                          .Dictionary = 1}}));
    CHECK(!MCFastFile::Create(name, *dictionaries).has_value());
}

/// <summary>
/// The unpacked version 2 streams kept for their neighbours' reads stay within MCFastFile::KeepStreamsUpTo, the latest
/// kept whatever its size, and the plan keeps from KeptStreamsLeast to KeptStreamsMost of them: the least while memory
/// is short.
/// </summary>
TEST_CASE("hd art: the unpacked streams kept stay within the plan's share")
{
    const ScratchFolder scratch("kept-streams");
    constexpr size_t StreamBytes = 4096;
    const std::vector<ArchiveEntry> entries = {Raw("k\\0.hdf", std::vector<uint8_t>(StreamBytes, 1)),
                                               Raw("k\\1.hdf", std::vector<uint8_t>(StreamBytes, 2))};
    const std::filesystem::path path = scratch.Path() / "k.hda";
    WriteBytes(
        path, BuildVersion2Streams({Version2Stream{.Entries = {entries[0]}}, Version2Stream{.Entries = {entries[1]}}}));
    const auto archive = MCFastFile::Create(MCFileSystem::ToUtf8(path));
    REQUIRE(archive.has_value());
    const size_t before = MCFastFile::KeptStreamsUpTo();
    MCFastFile::KeepStreamsUpTo(MCFastFile::KeptStreamsMost);
    const size_t earlier = MCFastFile::KeptStreamBytes();
    std::vector<int32_t> indices;

    for (const ArchiveEntry& entry : entries)
    {
        MCTest::Scope scope(entry.Name);
        const std::optional<int32_t> index = (*archive)->Find(entry.Name);
        REQUIRE(index.has_value());
        const auto bytes = (*archive)->Unpack(*index, (*archive)->ReadStored(*index));
        REQUIRE(bytes.has_value());
        CHECK(*bytes == entry.Stored);
        indices.push_back(*index);
    }

    CHECK_EQ(MCFastFile::KeptStreamBytes(), earlier + 2 * StreamBytes);
    MCFastFile::KeepStreamsUpTo(1);
    CHECK_EQ(MCFastFile::KeptStreamsUpTo(), size_t{1});
    CHECK_EQ(MCFastFile::KeptStreamBytes(), StreamBytes);
    CHECK((*archive)->KeptStreamOf(indices[0]) == nullptr);
    CHECK((*archive)->KeptStreamOf(indices[1]) != nullptr);

    MCHdArt::NextFrame(true);
    CHECK_EQ(MCFastFile::KeptStreamsUpTo(), MCFastFile::KeptStreamsLeast);
    MCHdArt::NextFrame(false);
    CHECK(MCFastFile::KeptStreamsUpTo() >= MCFastFile::KeptStreamsLeast);
    CHECK(MCFastFile::KeptStreamsUpTo() <= MCFastFile::KeptStreamsMost);
    MCFastFile::KeepStreamsUpTo(before);
}

/// <summary>
/// The pack's dicts.hdd is read with it: a version 2 ground tile packed against its dictionary 1 is drawn; with no
/// dicts.hdd, a damaged one, or another dictionary under that number, it falls back to the original (no HD frame).
/// </summary>
TEST_CASE("hd art: a pack's tiles packed against its dictionary are drawn, and fall back without its dicts.hdd")
{
    const ScratchFolder scratch("dictionary-pack");
    const std::vector<uint8_t> tile = {1, 2, 3};
    const std::vector<uint8_t> file = TileFrameFile();
    WriteBytes(scratch.Path() / "terrain.hda",
               BuildDictionaryArchive({Raw(MCHdArt::TerrainTileName(tile) + ".hdf", file)},
                                      MCFastFile::StreamKind::Tiles, 1, 1));
    const std::filesystem::path dictionaries = scratch.Path() / PackDictionariesName;
    std::vector<uint8_t> damaged = DictionariesFile({{1, DictionaryOne}});
    damaged[0] = 'X';
    const int previous = GHdArt;
    GHdArt = 1;

    const auto drawn = [&]
    {
        MCHdArt::SetPackFolder(scratch.Path());
        return MCHdArt::FindTerrainTile(tile, TileWidth, TileHeight);
    };

    CHECK(drawn() == nullptr);
    WriteBytes(dictionaries, DictionariesFile({{1, DictionaryOne}}));
    const std::shared_ptr<const MCHdArt::Frame> found = drawn();
    REQUIRE(found != nullptr);
    const auto decoded = MCHdArt::DecodeHdf(file, TileWidth, TileHeight);
    REQUIRE(decoded.has_value());
    CHECK_EQ(found->Width, decoded->Width);
    CHECK(std::ranges::equal(MCHdArt::ColorsOf(*found), decoded->Colors));
    CHECK(found->Indices == decoded->Indices);

    WriteBytes(dictionaries, DictionariesFile({{1, DictionaryTwo}}));
    CHECK(drawn() == nullptr);
    WriteBytes(dictionaries, damaged);
    CHECK(drawn() == nullptr);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// An archive (<c>terrain.hda</c>, the HD art pack's FastFile) stands for its folder: a terrain tile's frame loads from
/// its entry, packed (DEFLATE) or not, a duplicate entry sharing another's bytes loads the same, and a tile without an
/// entry has none.
/// </summary>
TEST_CASE("hd art: frames load from an archive, packed or stored, duplicates sharing their bytes")
{
    constexpr int32_t Scale = 3;
    constexpr int32_t Width = 9;
    constexpr int32_t Height = 7;
    MCHdArt::Frame frame;
    frame.Width = Width * Scale;
    frame.Height = Height * Scale;
    frame.Scale = Scale;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);

    // Three tiles: one packed, one stored raw, one sharing the first's bytes.
    const std::array<std::vector<uint8_t>, 4> tiles = {std::vector<uint8_t>{1, 2, 3}, std::vector<uint8_t>{4, 5, 6},
                                                       std::vector<uint8_t>{7, 8, 9}, std::vector<uint8_t>{10}};
    std::vector<std::string> names;

    for (const std::vector<uint8_t>& tile : tiles)
    {
        names.push_back(MCHdArt::TerrainTileName(tile) + ".hdf");
    }

    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_archive_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);
    const std::vector<uint8_t> archive = BuildArchive({{names[0], StoredDeflate(file), file.size(), std::nullopt},
                                                       {names[1], file, file.size(), std::nullopt},
                                                       {names[2], {}, file.size(), size_t{0}}});
    std::ofstream(pack / "terrain.hda", std::ios::binary)
        .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);

    for (size_t i = 0; i < 3; i++)
    {
        const std::shared_ptr<const MCHdArt::Frame> read = MCHdArt::FindTerrainTile(tiles[i], Width, Height);
        REQUIRE(read != nullptr);
        CHECK_EQ(read->Width, frame.Width);
        CHECK(read->Indices == frame.Indices);
    }

    CHECK(MCHdArt::FindTerrainTile(tiles[3], Width, Height) == nullptr);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    std::filesystem::remove_all(pack);
}

/// <summary>
/// A pack in a folder whose name Windows' ANSI code page can't spell (a user name outside it) is read: its pack.cfg,
/// archive, PNG frames and audio.cfg are opened by the name's UTF-8, and a broken archive is logged, not thrown over.
/// Any name passes on the Mac; the Windows build is the one that once read these through the code page.
/// </summary>
TEST_CASE("hd art: a pack in a folder named outside the ANSI code page is read")
{
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    MCHdArt::Frame frame;
    frame.Width = Width;
    frame.Height = Height;
    frame.Scale = 1;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);
    const std::vector<uint8_t> tile = {1, 2, 3};
    const std::vector<uint8_t> overlay = {4, 5, 6};
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_hd_unicode_test";
    const std::filesystem::path pack = scratch / u8"Pilóto 測試";
    const std::filesystem::path rain = pack / "audio" / "ambience" / "rain.flac";
    const std::filesystem::path colorPng = pack / "0.png";
    const std::filesystem::path indexPng = pack / "0_idx.png";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(rain.parent_path());
    const std::vector<uint8_t> archive =
        BuildArchive({{MCHdArt::TerrainTileName(tile) + ".hdf", StoredDeflate(file), file.size(), std::nullopt}});
    std::ofstream(pack / "terrain.hda", std::ios::binary)
        .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    std::ofstream(pack / "overlays.hda", std::ios::binary).put('\0');
    std::ofstream(pack / "audio" / "audio.cfg") << "FITini\n[HDAudioPack]\nl Format=1\nFITend\n";
    std::ofstream(rain).put('\0');

    for (const std::filesystem::path& png : {colorPng, indexPng})
    {
        const MCSdlHandle<SDL_Surface> surface(SDL_CreateSurface(Width, Height, SDL_PIXELFORMAT_RGBA32));
        REQUIRE(surface != nullptr);
        REQUIRE(SDL_FillSurfaceRect(surface.get(), nullptr, 0xff808080));
        REQUIRE(SDL_SavePNG(surface.get(), MCFileSystem::ToUtf8(png).c_str()));
    }

    const int previous = GHdArt;
    GHdArt = 1;
    REQUIRE(MCHdArt::WritePackFile(pack).has_value());
    CHECK(std::filesystem::is_regular_file(pack / "pack.cfg"));
    MCHdArt::SetPackFolder(pack);
    MCHdAudio::Forget();

    CHECK(MCHdArt::Active());
    const std::shared_ptr<const MCHdArt::Frame> read = MCHdArt::FindTerrainTile(tile, Width, Height);
    REQUIRE(read != nullptr);
    CHECK(read->Indices == frame.Indices);
    CHECK_EQ(MCHdArt::ArchivesOpen(), 1);
    CHECK(MCHdArt::FindOverlay(overlay, Width, Height) == nullptr);
    const auto pngs = MCHdArt::FrameFromPngs(colorPng, indexPng, 1);
    REQUIRE(pngs.has_value());
    CHECK_EQ(pngs->Width, Width);
    CHECK_EQ(MCHdAudio::RainLoop(), std::optional(rain));

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    MCHdAudio::Forget();
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// A frame dropped to keep within the budget is read again when next drawn, with the Id it had (the GPU's copy of it
/// stays good), while another frame has another. The ledger counts a frame dropped while something else holds it as let
/// go, until that lets go of it too.
/// </summary>
TEST_CASE("hd art: a frame read again keeps its Id, and one dropped while held counts as let go until it goes")
{
    using MCMemoryLedger::MCHolder;
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    MCHdArt::Frame frame;
    frame.Width = Width;
    frame.Height = Height;
    frame.Scale = 1;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);
    const std::array<std::vector<uint8_t>, 2> tiles = {std::vector<uint8_t>{1, 2}, std::vector<uint8_t>{3, 4}};
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_frame_id_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);
    const std::vector<uint8_t> archive =
        BuildArchive({{MCHdArt::TerrainTileName(tiles[0]) + ".hdf", file, file.size(), std::nullopt},
                      {MCHdArt::TerrainTileName(tiles[1]) + ".hdf", file, file.size(), std::nullopt}});
    std::ofstream(pack / "terrain.hda", std::ios::binary)
        .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    MCHdArt::NextFrame(false);
    const int64_t letGoBefore = MCMemoryLedger::Of(MCHolder::HdFramesLetGo).Payload;

    // Room for one frame: each one read drops the other.
    MCHdArt::SetCacheBudget(1);
    std::shared_ptr<const MCHdArt::Frame> first = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(first != nullptr);
    const uint64_t firstId = first->Id;
    std::shared_ptr<const MCHdArt::Frame> second = MCHdArt::FindTerrainTile(tiles[1], Width, Height);
    REQUIRE(second != nullptr);
    CHECK(second->Id != firstId);
    const size_t read = MCHdArt::FramesRead();
    std::shared_ptr<const MCHdArt::Frame> again = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(again != nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read + 1);
    CHECK(again != first);
    CHECK_EQ(again->Id, firstId);

    // As the next frame starts, the ledger: the frame kept (its record and its colours apart), its blocks as the
    // allocator rounds them up; the two records dropped while this test held them, let go.
    MCHdArt::NextFrame(false);
    const MCMemoryLedger::MCFigures kept = MCMemoryLedger::Of(MCHolder::HdFrames);
    const MCMemoryLedger::MCFigures colors = MCMemoryLedger::Of(MCHolder::HdColors);
    CHECK_EQ(kept.Payload + colors.Payload, static_cast<int64_t>(MCHdArt::CachedBytes()));
    CHECK(kept.Allocated >= kept.Payload);
    CHECK(colors.Allocated >= colors.Payload);
    const MCMemoryLedger::MCFigures letGo = MCMemoryLedger::Of(MCHolder::HdFramesLetGo);
    CHECK_EQ(letGo.Payload - letGoBefore, static_cast<int64_t>(first->Bytes() + second->Bytes()));
    CHECK(letGo.Allocated >= letGo.Payload);

    // Let go of here too: gone from the ledger.
    first.reset();
    second.reset();
    MCHdArt::NextFrame(false);
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdFramesLetGo).Payload, letGoBefore);

    MCHdArt::SetCacheBudget(0);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    again.reset();
    MCHdArt::NextFrame(false);
    std::filesystem::remove_all(pack);
}

/// <summary>
/// Port-only (SPEC-memory.md's frame record and its one copy of a frame's colours): a frame the cache hands out is its
/// record, its colours kept apart until the GPU has its picture (MCHdArt::ColorsUploaded); then the cache lets go of
/// them, and keeps the record however little room it has, or however short the system is, until the GPU lets go of
/// the picture, so a draw of it reads nothing. Asked for again, its colours are read again, the same, its record kept.
/// </summary>
TEST_CASE("hd art: a frame's colours go once the GPU has its picture, and its record stays while the picture does")
{
    using MCMemoryLedger::MCHolder;
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    MCHdArt::Frame frame;
    frame.Width = Width;
    frame.Height = Height;
    frame.Scale = 1;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);
    const auto stored = MCHdArt::DecodeHdf(file, Width, Height);
    REQUIRE(stored.has_value());
    const std::array<std::vector<uint8_t>, 2> tiles = {std::vector<uint8_t>{5, 6}, std::vector<uint8_t>{7, 8}};
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_frame_record_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);
    const std::vector<uint8_t> archive =
        BuildArchive({{MCHdArt::TerrainTileName(tiles[0]) + ".hdf", file, file.size(), std::nullopt},
                      {MCHdArt::TerrainTileName(tiles[1]) + ".hdf", file, file.size(), std::nullopt}});
    std::ofstream(pack / "terrain.hda", std::ios::binary)
        .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    MCHdArt::NextFrame(false);

    // The record holds no colours: the cache keeps them apart, as stored, and counts them apart.
    const std::shared_ptr<const MCHdArt::Frame> first = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(first != nullptr);
    CHECK(first->Colors.empty());
    CHECK(std::ranges::equal(MCHdArt::ColorsOf(*first), stored->Colors));
    MCHdArt::NextFrame(false);
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdColors).Payload, static_cast<int64_t>(stored->Colors.size()));
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdFrames).Payload, static_cast<int64_t>(first->Bytes()));

    // The GPU takes them: the cache lets go of them.
    MCHdArt::ColorsUploaded(*first);
    MCHdArt::NextFrame(false);
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdColors).Payload, 0);
    CHECK_EQ(MCHdArt::CachedBytes(), first->Bytes());

    // Room for one frame, then the system short: the cache lets go of all it can, but not the record of the frame on
    // the GPU, whose draws read nothing.
    MCHdArt::SetCacheBudget(1);
    REQUIRE(MCHdArt::FindTerrainTile(tiles[1], Width, Height) != nullptr);
    MCHdArt::NextFrame(false);
    MCHdArt::NextFrame(false);
    MCHdArt::NextFrame(true);
    const size_t read = MCHdArt::FramesRead();
    // Port-only (SPEC-memory.md's indices): the record kept is the frame's without its indices, which the GPU holds.
    const std::shared_ptr<const MCHdArt::Frame> kept = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(kept != nullptr);
    CHECK_EQ(kept->Id, first->Id);
    CHECK(kept->Indices.empty());
    CHECK_EQ(MCHdArt::FramesRead(), read);

    // Its colours asked for again (the GPU let go of its picture, or never kept it): read again, the same, and its
    // indices with them.
    const size_t readAgain = MCHdArt::ColorsReadAgain();
    CHECK(std::ranges::equal(MCHdArt::ColorsOf(*first), stored->Colors));
    CHECK_EQ(MCHdArt::ColorsReadAgain(), readAgain + 1);
    const std::shared_ptr<const MCHdArt::Frame> withIndices = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(withIndices != nullptr);
    CHECK_EQ(withIndices->Id, first->Id);
    CHECK(withIndices->Indices == stored->Indices);
    CHECK_EQ(MCHdArt::FramesRead(), read);

    // Once the GPU lets go of the picture, so may the cache: the next draw reads the frame again, numbered as before.
    MCHdArt::PictureDropped(first->Id);
    MCHdArt::NextFrame(false);
    MCHdArt::NextFrame(false);
    MCHdArt::NextFrame(true);
    const std::shared_ptr<const MCHdArt::Frame> again = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(again != nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read + 1);
    CHECK(again != first);
    CHECK_EQ(again->Id, first->Id);

    MCHdArt::SetCacheBudget(0);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    MCHdArt::NextFrame(false);
    std::filesystem::remove_all(pack);
}

/// <summary>
/// Port-only (SPEC-memory.md's indices): a frame's record lets go of its indices a couple of frames after the GPU took
/// its picture (which holds them), unless code on the CPU reads its folder's (MCHdArt::IndicesOf); asked for after, they
/// are read again, the same, and from then on the folder's frames keep theirs.
/// </summary>
TEST_CASE("hd art: a frame's indices go once the GPU has its picture, unless code on the CPU reads its folder's")
{
    using MCMemoryLedger::MCHolder;
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    constexpr int KeptFrames = 2;
    MCHdArt::Frame frame;
    frame.Width = Width;
    frame.Height = Height;
    frame.Scale = 1;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);
    const auto stored = MCHdArt::DecodeHdf(file, Width, Height);
    REQUIRE(stored.has_value());
    const std::array<std::vector<uint8_t>, 2> tiles = {std::vector<uint8_t>{5, 6}, std::vector<uint8_t>{7, 8}};
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_frame_indices_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);
    const std::vector<uint8_t> archive =
        BuildArchive({{MCHdArt::TerrainTileName(tiles[0]) + ".hdf", file, file.size(), std::nullopt},
                      {MCHdArt::TerrainTileName(tiles[1]) + ".hdf", file, file.size(), std::nullopt}});
    std::ofstream(pack / "terrain.hda", std::ios::binary)
        .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    MCHdArt::NextFrame(false);

    // The GPU takes the first frame's picture: its record keeps its indices for a couple of frames, then lets them go.
    const std::shared_ptr<const MCHdArt::Frame> first = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(first != nullptr);
    CHECK(MCHdArt::IndicesOf(*first, MCHdArt::MCIndicesUse::Upload).size() == stored->Indices.size());
    MCHdArt::ColorsUploaded(*first);
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::FindTerrainTile(tiles[0], Width, Height)->Indices.empty());

    for (int i = 0; i < KeptFrames; ++i)
    {
        MCHdArt::NextFrame(false);
    }

    const std::shared_ptr<const MCHdArt::Frame> record = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(record != nullptr);
    CHECK_EQ(record->Id, first->Id);
    CHECK(record->Indices.empty());
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdFrames).Payload, 0);

    // Its colours read again while the GPU still has its picture (as a colour reader on the CPU does): the indices come
    // back with them, and go again as after an upload.
    CHECK(!MCHdArt::ColorsOf(*record).empty());
    CHECK(MCHdArt::FindTerrainTile(tiles[0], Width, Height)->Indices == stored->Indices);

    for (int i = 0; i <= KeptFrames; ++i)
    {
        MCHdArt::NextFrame(false);
    }

    CHECK(MCHdArt::FindTerrainTile(tiles[0], Width, Height)->Indices.empty());
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdFrames).Payload, 0);

    // Code on the CPU asks for them: read again, the same, kept.
    const size_t readAgain = MCHdArt::IndicesReadAgain();
    CHECK(std::ranges::equal(MCHdArt::IndicesOf(*record), stored->Indices));
    CHECK_EQ(MCHdArt::IndicesReadAgain(), readAgain + 1);

    // The folder's other frames keep theirs now, once on the GPU too.
    const std::shared_ptr<const MCHdArt::Frame> second = MCHdArt::FindTerrainTile(tiles[1], Width, Height);
    REQUIRE(second != nullptr);
    MCHdArt::ColorsUploaded(*second);

    for (int i = 0; i <= KeptFrames; ++i)
    {
        MCHdArt::NextFrame(false);
    }

    CHECK(MCHdArt::FindTerrainTile(tiles[1], Width, Height)->Indices == stored->Indices);
    CHECK(MCHdArt::FindTerrainTile(tiles[0], Width, Height)->Indices == stored->Indices);
    CHECK_EQ(MCHdArt::IndicesReadAgain(), readAgain + 1);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    MCHdArt::NextFrame(false);
    std::filesystem::remove_all(pack);
}

/// <summary>
/// Port-only: while the system says memory is short, the frames kept in memory go least recently used first,
/// MCMemoryRoom::TrimmedBytesPerFrame a frame, but never one asked for in the frame before (being drawn, it would only be
/// read again), until it stops saying so; meanwhile and after, those asked for are read and kept. A frame something else
/// still holds (a texture's picture) leaves the cache too, but frees nothing, so it isn't counted in a frame's share.
/// </summary>
TEST_CASE(
    "hd art: while memory is short the frames kept go 64 MB a frame, never one the last frame drew, until it isn't")
{
    // Frames of 4 MB of BC7 colours and 128 KB of indices: 16 go in a frame (the 16th takes it past 64 MB).
    constexpr int32_t Scale = 8;
    constexpr int32_t Width = 256;
    constexpr int32_t Height = 256;
    constexpr size_t Frames = 44;
    constexpr size_t Drawn = 4;
    constexpr size_t GoneAFrame = 16;
    // The least recently used, held outside the cache too until the first frame short is over.
    constexpr size_t Held = 4;
    MCHdArt::Frame frame;
    frame.Width = Width * Scale;
    frame.Height = Height * Scale;
    frame.Scale = Scale;
    frame.Format = MCHdArt::ColorFormat::Bc7;
    frame.Colors.assign(static_cast<size_t>(frame.Width) * frame.Height, 0);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const size_t frameBytes = frame.Bytes();
    REQUIRE_EQ(MCMemoryRoom::TrimmedBytesPerFrame / frameBytes + 1, GoneAFrame);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);

    // Every tile's entry shares the first's bytes.
    std::vector<std::vector<uint8_t>> tiles;
    std::vector<ArchiveEntry> entries;

    for (size_t i = 0; i < Frames; ++i)
    {
        tiles.push_back({static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)});
        const std::string name = MCHdArt::TerrainTileName(tiles.back()) + ".hdf";
        entries.push_back(i == 0 ? Raw(name, file) : ArchiveEntry{name, {}, file.size(), size_t{0}});
    }

    ScratchFolder pack("mc_hd_memory_short_test");
    WriteBytes(pack.Path() / "terrain.hda", BuildArchive(entries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack.Path());
    const auto system = std::make_shared<MCFakeMemorySystem>();
    MCFakeMemoryScope memory(system);
    MCMemoryRoom::MCReader reader{"Test GPU"};

    // A frame begins (MCHdArt::NextFrame as the renderer calls it) and draws the last `Drawn` tiles.
    const auto frameDrawn = [&]
    {
        MCHdArt::NextFrame(reader.Frame(0).MemoryShort);

        for (size_t i = Frames - Drawn; i < Frames; ++i)
        {
            REQUIRE(MCHdArt::FindTerrainTile(tiles[i], Width, Height) != nullptr);
        }

        return MCHdArt::CachedBytes();
    };

    // As a texture holds its picture's frame, asked for once.
    std::vector<std::shared_ptr<const MCHdArt::Frame>> held;

    for (size_t i = 0; i < Frames; ++i)
    {
        std::shared_ptr<const MCHdArt::Frame> found = MCHdArt::FindTerrainTile(tiles[i], Width, Height);
        REQUIRE(found != nullptr);

        if (i < Held)
        {
            held.push_back(std::move(found));
        }
    }

    CHECK_EQ(MCHdArt::CachedBytes(), Frames * frameBytes);
    CHECK_EQ(frameDrawn(), Frames * frameBytes);

    // Short: the held frames leave the cache, freeing their colours (the cache's own: their records live on with their
    // holders), and as many only it held go besides them as make 64 MB with those; then 16 a frame, then only those
    // drawn, however long it lasts.
    const size_t heldFreed = Held * frame.Colors.size();
    REQUIRE(heldFreed < MCMemoryRoom::TrimmedBytesPerFrame);
    const size_t goneBesides = (MCMemoryRoom::TrimmedBytesPerFrame - heldFreed + frameBytes - 1) / frameBytes;
    system->IsShort = true;
    CHECK_EQ(frameDrawn(), (Frames - Held - goneBesides) * frameBytes);
    held.clear();
    CHECK_EQ(frameDrawn(), (Frames - Held - goneBesides - GoneAFrame) * frameBytes);
    CHECK_EQ(frameDrawn(), Drawn * frameBytes);
    CHECK_EQ(frameDrawn(), Drawn * frameBytes);
    const size_t read = MCHdArt::FramesRead();
    CHECK_EQ(frameDrawn(), Drawn * frameBytes);
    CHECK_EQ(MCHdArt::FramesRead(), read);

    // No longer short: the frames asked for are read again and kept.
    system->IsShort = false;
    frameDrawn();

    for (const std::vector<uint8_t>& tile : tiles)
    {
        REQUIRE(MCHdArt::FindTerrainTile(tile, Width, Height) != nullptr);
    }

    CHECK_EQ(MCHdArt::FramesRead(), read + Frames - Drawn);
    CHECK_EQ(frameDrawn(), Frames * frameBytes);
    CHECK_EQ(frameDrawn(), Frames * frameBytes);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// A part of an HD frame copies into another (MCHdArt::CopyArea): its HD pixels, as RGBA or as whole BC7 blocks, and
/// its indices, the rest of the target as it was. A part outside either frame, frames of another scale or format, and
/// BC7 blocks holding more than one original pixel (at 2x) are refused, nothing copied. A blank frame is the size its
/// like's scale makes, nothing in it.
/// </summary>
TEST_CASE("hd art: a part of an HD frame copies into another, as RGBA or as BC7 blocks")
{
    constexpr int32_t Scale = 4;
    constexpr int32_t Wide = 5;
    constexpr int32_t High = 3;
    constexpr uint8_t SourceSeed = 1;
    constexpr uint8_t TargetSeed = 100;
    // Two of the source's columns of its last two rows, to the target's top, three columns in.
    constexpr MCRect Part = {2, 1, 3, 2};
    constexpr int32_t ToX = 3;
    constexpr int32_t ToY = 0;
    constexpr int32_t PartWidth = Part.X1 - Part.X0 + 1;
    constexpr int32_t PartHeight = Part.Y1 - Part.Y0 + 1;

    for (const bool bc7 : {false, true})
    {
        MCTest::Scope scope(bc7 ? "BC7" : "RGBA");
        const auto made = [bc7](MCHdArt::Frame frame) { return bc7 ? AsBc7(std::move(frame)) : frame; };
        const MCHdArt::Frame source = made(PatternedFrame(Wide, High, Scale, SourceSeed));
        const MCHdArt::Frame before = made(PatternedFrame(Wide + 2, High + 1, Scale, TargetSeed));
        MCHdArt::Frame target = before;
        REQUIRE(MCHdArt::CopyArea(source, Part.X0, Part.Y0, PartWidth, PartHeight, target, ToX, ToY));
        const std::vector<uint8_t> sourceRgba = RgbaOf(source);
        const std::vector<uint8_t> beforeRgba = RgbaOf(before);
        const std::vector<uint8_t> targetRgba = RgbaOf(target);
        const auto inPart = [](int32_t x, int32_t y)
        { return x >= ToX && x < ToX + PartWidth && y >= ToY && y < ToY + PartHeight; };
        int32_t wrongPixels = 0;

        for (int32_t y = 0; y < target.Height; y++)
        {
            for (int32_t x = 0; x < target.Width; x++)
            {
                const bool copied = inPart(x / Scale, y / Scale);
                const size_t from = copied ? (static_cast<size_t>(y + (Part.Y0 - ToY) * Scale) * source.Width + x +
                                              (Part.X0 - ToX) * Scale) *
                                                 Channels
                                           : (static_cast<size_t>(y) * target.Width + x) * Channels;
                const std::vector<uint8_t>& expected = copied ? sourceRgba : beforeRgba;
                const size_t at = (static_cast<size_t>(y) * target.Width + x) * Channels;
                wrongPixels += std::equal(expected.begin() + static_cast<ptrdiff_t>(from),
                                          expected.begin() + static_cast<ptrdiff_t>(from + Channels),
                                          targetRgba.begin() + static_cast<ptrdiff_t>(at))
                                   ? 0
                                   : 1;
            }
        }

        CHECK_EQ(wrongPixels, 0);
        int32_t wrongIndices = 0;

        for (int32_t y = 0; y < target.OriginalHeight(); y++)
        {
            for (int32_t x = 0; x < target.OriginalWidth(); x++)
            {
                const bool copied = inPart(x, y);
                const MCHdArt::Frame& expected = copied ? source : before;
                const size_t from = copied ? (static_cast<size_t>(y + Part.Y0 - ToY) * Wide + x + Part.X0 - ToX)
                                           : static_cast<size_t>(y) * target.OriginalWidth() + x;
                const size_t at = static_cast<size_t>(y) * target.OriginalWidth() + x;
                wrongIndices +=
                    expected.Indices[from * MCHdArt::IndexBytes] == target.Indices[at * MCHdArt::IndexBytes] ? 0 : 1;
            }
        }

        CHECK_EQ(wrongIndices, 0);

        // Refused, nothing copied: past the source's right edge, past the target's, from a frame at another scale.
        MCHdArt::Frame refused = target;
        CHECK(!MCHdArt::CopyArea(source, Wide - 1, 0, 2, 1, refused, 0, 0));
        CHECK(!MCHdArt::CopyArea(source, 0, 0, 1, 1, refused, Wide + 2, 0));
        CHECK(!MCHdArt::CopyArea(made(PatternedFrame(2, 2, Scale * 2, SourceSeed)), 0, 0, 1, 1, refused, 0, 0));
        CHECK(refused.Colors == target.Colors);
        CHECK(refused.Indices == target.Indices);
    }

    // Another format; BC7 at 2x, a block two original pixels wide (RGBA at 2x copies).
    MCHdArt::Frame rgba = PatternedFrame(Wide, High, Scale, TargetSeed);
    CHECK(!MCHdArt::CopyArea(AsBc7(PatternedFrame(Wide, High, Scale, SourceSeed)), 0, 0, 1, 1, rgba, 0, 0));
    constexpr int32_t HalfBlock = 2;
    MCHdArt::Frame halfBc7 = AsBc7(PatternedFrame(Wide, High, HalfBlock, TargetSeed));
    CHECK(!MCHdArt::CopyArea(AsBc7(PatternedFrame(Wide, High, HalfBlock, SourceSeed)), 0, 0, 2, 2, halfBc7, 0, 0));
    MCHdArt::Frame halfRgba = PatternedFrame(Wide, High, HalfBlock, TargetSeed);
    CHECK(MCHdArt::CopyArea(PatternedFrame(Wide, High, HalfBlock, SourceSeed), 0, 0, 2, 2, halfRgba, 1, 1));

    const MCHdArt::Frame blank = MCHdArt::BlankFrame(AsBc7(PatternedFrame(Wide, High, Scale, SourceSeed)), 3, 2);
    CHECK(blank.Format == MCHdArt::ColorFormat::Bc7);
    CHECK_EQ(blank.Scale, Scale);
    CHECK_EQ(blank.OriginalWidth(), 3);
    CHECK_EQ(blank.OriginalHeight(), 2);
    CHECK_EQ(blank.Colors.size(), static_cast<size_t>(3 * 2) * MCBc7::BlockBytes);
    CHECK(std::ranges::all_of(blank.Colors, [](uint8_t byte) { return byte == 0; }));
    CHECK_EQ(blank.Indices.size(), static_cast<size_t>(3 * 2) * MCHdArt::IndexBytes);
    CHECK(std::ranges::all_of(blank.Indices, [](uint8_t byte) { return byte == 0; }));
}

/// <summary>
/// A picture the game makes of the pack's (MCHdArt::KeepPicture) is found by its pixels while HD art is on: the frame
/// kept, numbered; kept again, the new one under a new number. Nothing while HD art is off, nor once the pack is set
/// again.
/// </summary>
TEST_CASE("hd art: a picture the game makes of the pack's is found by its pixels until the pack changes")
{
    constexpr int32_t Width = 3;
    constexpr int32_t Height = 2;
    constexpr int32_t Scale = 4;
    const std::vector<uint8_t> pixels = {1, 2, 3, 4, 5, 6};
    const std::vector<uint8_t> other = {6, 5, 4, 3, 2, 1};
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_kept_picture_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);

    const MCHdArt::Frame frame = PatternedFrame(Width, Height, Scale, 1);
    MCHdArt::KeepPicture(pixels, Width, Height, MCHdArt::Frame(frame));
    const std::shared_ptr<const MCHdArt::Frame> kept = MCHdArt::FindPicture(pixels, Width, Height);
    REQUIRE(kept != nullptr);
    CHECK(kept->Id != 0);
    CHECK(kept->Colors == frame.Colors);
    CHECK(MCHdArt::FindPicture(other, Width, Height) == nullptr);

    MCHdArt::KeepPicture(pixels, Width, Height, MCHdArt::Frame(frame));
    const std::shared_ptr<const MCHdArt::Frame> again = MCHdArt::FindPicture(pixels, Width, Height);
    REQUIRE(again != nullptr);
    CHECK(again != kept);
    CHECK(again->Id != kept->Id);

    GHdArt = 0;
    CHECK(MCHdArt::FindPicture(pixels, Width, Height) == nullptr);
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    CHECK(MCHdArt::FindPicture(pixels, Width, Height) == nullptr);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    std::filesystem::remove_all(pack);
}

/// <summary>
/// Once the ground is on the GPU its kept frames go (MCHdArt::DropGround), and an overlay's stay; a ground tile asked for
/// again is read again, with its Id.
/// </summary>
TEST_CASE("hd art: the ground's kept frames go once it's up, and come back with their Ids")
{
    constexpr int32_t Width = 8;
    constexpr int32_t Height = 4;
    MCHdArt::Frame frame;
    frame.Width = Width;
    frame.Height = Height;
    frame.Scale = 1;
    frame.Colors = SpriteLike(frame.Width, frame.Height);
    frame.Indices.assign(static_cast<size_t>(Width) * Height * MCHdArt::IndexBytes, MCHdArt::HasIndex);
    const std::vector<uint8_t> file = MCHdArt::EncodeHdf(frame);
    const std::vector<uint8_t> tile = {1, 2};
    const std::vector<uint8_t> overlay = {3, 4};
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_drop_ground_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);

    for (const auto& [name, data] : {std::pair{"terrain.hda", tile}, std::pair{"overlays.hda", overlay}})
    {
        const std::vector<uint8_t> archive =
            BuildArchive({{MCHdArt::TerrainTileName(data) + ".hdf", file, file.size(), std::nullopt}});
        std::ofstream(pack / name, std::ios::binary)
            .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    }

    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    const std::shared_ptr<const MCHdArt::Frame> ground = MCHdArt::FindTerrainTile(tile, Width, Height);
    REQUIRE(ground != nullptr);
    const uint64_t groundId = ground->Id;
    REQUIRE(MCHdArt::FindOverlay(overlay, Width, Height) != nullptr);
    const size_t both = MCHdArt::CachedBytes();
    // The ground's record and its colours, which the cache keeps apart.
    const size_t groundBytes = ground->Bytes() + MCHdArt::ColorsOf(*ground).size();

    MCHdArt::DropGround();
    CHECK_EQ(MCHdArt::CachedBytes(), both - groundBytes);
    const size_t read = MCHdArt::FramesRead();
    REQUIRE(MCHdArt::FindOverlay(overlay, Width, Height) != nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read);
    const std::shared_ptr<const MCHdArt::Frame> again = MCHdArt::FindTerrainTile(tile, Width, Height);
    REQUIRE(again != nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read + 1);
    CHECK_EQ(again->Id, groundId);
    CHECK_EQ(MCHdArt::CachedBytes(), both);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    std::filesystem::remove_all(pack);
}

/// <summary>
/// MCHdArt::Prepare opens a mech part's archive (<c>mechs/legs90/5.hda</c>) before any of its shapes is drawn, once, and
/// nothing for a mech without one or while HD art is off.
/// </summary>
TEST_CASE("hd art: a mech's archive opens before its shapes are drawn")
{
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_prepare_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack / "mechs" / "legs90");
    const std::vector<uint8_t> archive = BuildArchive({{"0\\0.hdf", {1, 2, 3}, 3, std::nullopt}});
    std::ofstream(pack / "mechs" / "legs90" / "5.hda", std::ios::binary)
        .write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    CHECK_EQ(MCHdArt::ArchivesOpen(), 0);

    // Opened by the readers.
    MCHdArt::Prepare({"legs90", 5, 0});
    MCHdArt::WaitForReadAhead();
    CHECK_EQ(MCHdArt::ArchivesOpen(), 1);
    MCHdArt::Prepare({"legs90", 5, 3});
    MCHdArt::Prepare({"legs90", 6, 0});
    MCHdArt::WaitForReadAhead();
    CHECK_EQ(MCHdArt::ArchivesOpen(), 1);

    GHdArt = 0;
    MCHdArt::SetPackFolder(pack);
    MCHdArt::Prepare({"legs90", 5, 0});
    CHECK_EQ(MCHdArt::ArchivesOpen(), 0);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    std::filesystem::remove_all(pack);
}

/// <summary>
/// No more than 128 of the pack's archive files stay open, however many archives it opens (the Mac gives an app started
/// from the Finder 256 open files): the least recently read close, keeping their directories, and one read again opens
/// again, unless another file was written in its place meanwhile (a pack made again while the game runs), whose
/// frames its old directory would misread.
/// </summary>
TEST_CASE("hd art: no more than 128 archive files stay open, a closed one opening again when read")
{
    constexpr uint32_t Mechs = 200;
    constexpr size_t MaxOpen = 128;
    const ScratchFolder scratch("open-files");
    const std::filesystem::path& pack = scratch.Path();
    const std::vector<uint8_t> file = TileFrameFile();
    const std::array<std::vector<uint8_t>, 3> tiles = {std::vector<uint8_t>{1, 2, 3}, std::vector<uint8_t>{4, 5, 6},
                                                       std::vector<uint8_t>{7, 8, 9}};
    const auto terrain = [&](const std::vector<uint8_t>& frame)
    {
        std::vector<ArchiveEntry> entries;

        for (const std::vector<uint8_t>& tile : tiles)
        {
            entries.push_back(Raw(MCHdArt::TerrainTileName(tile) + ".hdf", frame));
        }

        WriteBytes(pack / "terrain.hda", BuildArchive(entries));
    };

    terrain(file);
    std::filesystem::create_directories(pack / "mechs" / "legs90");
    const std::vector<uint8_t> mech = BuildArchive({Raw("0\\0.hdf", {1, 2, 3})});

    for (uint32_t n = 0; n < Mechs; ++n)
    {
        WriteBytes(pack / "mechs" / "legs90" / std::format("{}.hda", n), mech);
    }

    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    CHECK(MCHdArt::FindTerrainTile(tiles[0], TileWidth, TileHeight) != nullptr);
    CHECK_EQ(MCHdArt::ArchiveFilesOpen(), size_t{1});

    // Opened by the readers, their files among those kept open before the game takes them.
    for (uint32_t n = 0; n < Mechs; ++n)
    {
        MCHdArt::Prepare({"legs90", n, 0});
    }

    MCHdArt::WaitForReadAhead();
    CHECK_EQ(MCHdArt::ArchivesOpen(), size_t{Mechs + 1});
    CHECK_EQ(MCHdArt::ArchiveFilesOpen(), MaxOpen);

    // terrain.hda, read longest ago, was closed: its other tile opens it again.
    const std::shared_ptr<const MCHdArt::Frame> again = MCHdArt::FindTerrainTile(tiles[1], TileWidth, TileHeight);
    REQUIRE(again != nullptr);
    CHECK_EQ(again->OriginalWidth(), TileWidth);
    CHECK_EQ(MCHdArt::ArchiveFilesOpen(), MaxOpen);

    // Another terrain.hda in its place, of the same layout (its frames' colours another's), dated later: once closed
    // again, it isn't read by the old directory.
    std::vector<uint8_t> other = file;
    other.back() ^= 0xff;
    terrain(other);
    std::filesystem::last_write_time(pack / "terrain.hda",
                                     std::filesystem::last_write_time(pack / "terrain.hda") + std::chrono::hours(1));

    for (uint32_t n = Mechs; n < Mechs + MaxOpen; ++n)
    {
        WriteBytes(pack / "mechs" / "legs90" / std::format("{}.hda", n), mech);
        MCHdArt::Prepare({"legs90", n, 0});
    }

    MCHdArt::WaitForReadAhead();
    CHECK(MCHdArt::FindTerrainTile(tiles[2], TileWidth, TileHeight) == nullptr);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// The game's thread asks the pack's listing, made once, which folders and archives it holds (MCHdArt's ListPack), not
/// the disk: a packet's frames are found in their part's archive whichever case the pack's names are in, and an archive
/// put in the pack after it was listed is found once the pack is set again.
/// </summary>
TEST_CASE("hd art: the pack is listed once, its names found in any case")
{
    const ScratchFolder scratch("listed");
    const std::filesystem::path& pack = scratch.Path();
    const std::vector<uint8_t> frame = TileFrameFile();
    std::filesystem::create_directories(pack / "Mechs" / "LEGS90");
    WriteBytes(pack / "Mechs" / "LEGS90" / "5.HDA", BuildArchive({Raw("0\\0.hdf", frame)}));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);
    CHECK(MCHdArt::FindInFolder("mechs/legs90/5/0", 0, TileWidth, TileHeight) != nullptr);

    WriteBytes(pack / "Mechs" / "LEGS90" / "6.HDA", BuildArchive({Raw("0\\0.hdf", frame)}));
    CHECK(MCHdArt::FindInFolder("mechs/legs90/6/0", 0, TileWidth, TileHeight) == nullptr);
    MCHdArt::SetPackFolder(pack);
    CHECK(MCHdArt::FindInFolder("mechs/legs90/6/0", 0, TileWidth, TileHeight) != nullptr);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

namespace
{
    /// <summary>
    /// The read-ahead tests' shape table: the hash naming its folder (<c>shapes/&lt;hash&gt;</c>, an archive), its frames,
    /// the steps each frame has in the pack (the frame and its in-betweens; a turn middle and its in-betweens), and the
    /// frames' size and scale: a mech's, at the most scale, so a read takes a while.
    /// </summary>
    constexpr uint64_t AheadTable = 0x0123456789abcdefull;
    constexpr int32_t AheadFrames = 4;
    constexpr int32_t AheadSteps = 3;
    constexpr int32_t AheadTurnSteps = 2;
    constexpr int32_t AheadWidth = 60;
    constexpr int32_t AheadHeight = 50;
    constexpr int32_t AheadScale = 8;

    /// <summary>A frame of the read-ahead pack: its number and step, as MCHdArt::FindShape takes them.</summary>
    using AheadKey = std::pair<int32_t, int32_t>;

    /// <summary>Every frame of the read-ahead pack, frame by frame, each frame's own steps then its turn middle's.</summary>
    std::vector<AheadKey> AheadKeys()
    {
        std::vector<AheadKey> keys;

        for (int32_t frame = 0; frame < AheadFrames; ++frame)
        {
            for (int32_t step = 0; step < AheadSteps; ++step)
            {
                keys.emplace_back(frame, step);
            }

            for (int32_t step = 0; step < AheadTurnSteps; ++step)
            {
                keys.emplace_back(frame, TurnBetweenStep(TurnStep, step));
            }
        }

        return keys;
    }

    /// <summary>The name of <paramref name="key"/>'s frame file in its folder (MCHdArt.h): 3, 3_1, 3_turn, 3_turn_1.</summary>
    std::string AheadName(const AheadKey& key)
    {
        const auto [frame, step] = key;

        if (step < TurnStep)
        {
            return step == 0 ? std::to_string(frame) : std::format("{}_{}", frame, step);
        }

        const int32_t between = (step - TurnStep) >> TurnBetweenShift;
        return between == 0 ? std::format("{}_turn", frame) : std::format("{}_turn_{}", frame, between);
    }

    /// <summary>
    /// The frame file of a <paramref name="width"/> by <paramref name="height"/> frame at the read-ahead pack's scale:
    /// BC7 blocks of bytes made from <paramref name="mix"/>, which no other mix (below 256) makes, and indices.
    /// </summary>
    std::vector<uint8_t> FrameFile(int32_t width, int32_t height, int32_t mix)
    {
        constexpr int32_t ByteMix = 7;
        MCHdArt::Frame frame;
        frame.Scale = AheadScale;
        frame.Width = width * AheadScale;
        frame.Height = height * AheadScale;
        frame.Format = MCHdArt::ColorFormat::Bc7;
        frame.Colors.resize(static_cast<size_t>(MCBc7::Padded(frame.Width) / MCBc7::BlockSide) *
                            static_cast<size_t>(MCBc7::Padded(frame.Height) / MCBc7::BlockSide) * MCBc7::BlockBytes);

        for (size_t at = 0; at < frame.Colors.size(); ++at)
        {
            frame.Colors[at] = static_cast<uint8_t>(static_cast<int32_t>(at) * ByteMix + mix);
        }

        frame.Indices.resize(static_cast<size_t>(width) * height * MCHdArt::IndexBytes);

        for (size_t at = 0; at < frame.Indices.size(); at += MCHdArt::IndexBytes)
        {
            frame.Indices[at] = static_cast<uint8_t>(mix);
            frame.Indices[at + 1] = MCHdArt::HasIndex;
        }

        return MCHdArt::EncodeHdf(frame);
    }

    /// <summary>
    /// <paramref name="key"/>'s frame file in a read-ahead pack made with <paramref name="seed"/>: BC7 blocks of bytes
    /// no other key's (or seed's) has, and indices.
    /// </summary>
    std::vector<uint8_t> AheadFile(const AheadKey& key, uint8_t seed)
    {
        constexpr int32_t KeyFrameMix = 31;
        constexpr int32_t KeyStepMix = 3;
        return FrameFile(AheadWidth, AheadHeight, key.first * KeyFrameMix + key.second * KeyStepMix + seed);
    }

    /// <summary>
    /// Writes the read-ahead pack made with <paramref name="seed"/> into <paramref name="pack"/>: the folder of the table
    /// whose bytes hash to <paramref name="table"/> as an archive, the even frames' entries packed (DEFLATE), the odd
    /// ones' stored.
    /// </summary>
    void WriteAheadPack(const std::filesystem::path& pack, uint8_t seed, uint64_t table = AheadTable)
    {
        std::vector<ArchiveEntry> entries;

        for (const AheadKey& key : AheadKeys())
        {
            std::vector<uint8_t> file = AheadFile(key, seed);
            std::string name = AheadName(key) + ".hdf";
            const size_t size = file.size();
            entries.push_back(key.first % 2 == 0
                                  ? ArchiveEntry{std::move(name), StoredDeflate(file), size, std::nullopt}
                                  : Raw(std::move(name), std::move(file)));
        }

        const std::filesystem::path folder = pack / MCHdArt::ShapeTableFolder(table);
        std::filesystem::create_directories(folder.parent_path());
        WriteBytes(std::filesystem::path(folder).concat(".hda"), BuildArchive(entries));
    }

    /// <summary><paramref name="key"/>'s frame as the read-ahead pack made with <paramref name="seed"/> holds it.</summary>
    MCHdArt::Frame AheadFrame(const AheadKey& key, uint8_t seed)
    {
        auto decoded = MCHdArt::DecodeHdf(AheadFile(key, seed), AheadWidth, AheadHeight);
        REQUIRE(decoded.has_value());
        return std::move(*decoded);
    }

    /// <summary>The read-ahead table's frame <paramref name="key"/> as drawn (the table's frames given).</summary>
    std::shared_ptr<const MCHdArt::Frame> FindAhead(const AheadKey& key)
    {
        return MCHdArt::FindShape(AheadTable, key.first, AheadWidth, AheadHeight, key.second, AheadFrames);
    }

    /// <summary>
    /// Whether two frames are the same but for their Ids: their colours their own or, a frame the cache holds, the
    /// cache's (MCHdArt::ColorsOf).
    /// </summary>
    bool SameFrame(const MCHdArt::Frame& a, const MCHdArt::Frame& b)
    {
        const std::span<const uint8_t> aColors = MCHdArt::ColorsOf(a);
        const std::vector<uint8_t> colors(aColors.begin(), aColors.end());
        return a.Width == b.Width && a.Height == b.Height && a.Scale == b.Scale && a.Format == b.Format &&
               a.Blended == b.Blended && a.PaintedShadow == b.PaintedShadow &&
               std::ranges::equal(colors, MCHdArt::ColorsOf(b)) && a.Indices == b.Indices;
    }

    /// <summary>The HD art's readers read as <c>settings</c> says while this lives, as they read before once it ends.</summary>
    class ReaderScope
    {
    public:
        explicit ReaderScope(const MCHdArt::ReaderSettings& settings) : _Before(MCHdArt::GetReaderSettings())
        {
            MCHdArt::SetReaderSettings(settings);
        }

        ~ReaderScope() { MCHdArt::SetReaderSettings(_Before); }

        ReaderScope(const ReaderScope&) = delete;
        ReaderScope& operator=(const ReaderScope&) = delete;

    private:
        MCHdArt::ReaderSettings _Before;
    };
}

/// <summary>
/// Port-only: once a numbered folder's frame is drawn the first time, the reading thread reads the frames drawn after it
/// (the rest of its in-betweens, then the next frame's, looping round; a turn middle's likewise), and they are drawn as
/// the game's thread would have read them: the same frames, refused at a size they weren't made for, one dropped for
/// room and read ahead again keeping its Id. Nothing is read twice; with one reader, it opens its own handle on the
/// archive's file once for all its reads of it, and setting the pack closes it.
/// </summary>
TEST_CASE("hd art: the frames drawn after one are read ahead, the same as read when drawn")
{
    const ReaderScope oneReader({.Readers = 1});
    const ScratchFolder scratch("read-ahead");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const size_t opened = MCHdArt::ReaderFilesOpened();
    const auto drawn = [](const AheadKey& key)
    {
        MCTest::Scope scope(std::format("frame {}", AheadName(key)));
        std::shared_ptr<const MCHdArt::Frame> frame = FindAhead(key);
        REQUIRE(frame != nullptr);
        CHECK(SameFrame(*frame, AheadFrame(key, 0)));
        return frame;
    };

    // Frame 0, drawn the first time, is read as drawn; the rest of its steps and frame 1's are read ahead meanwhile.
    const size_t read = MCHdArt::FramesRead();
    const size_t ahead = MCHdArt::FramesReadAhead();
    drawn({0, 0});
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead);
    MCHdArt::WaitForReadAhead();
    std::shared_ptr<const MCHdArt::Frame> second;

    for (const AheadKey& key : {AheadKey{0, 1}, AheadKey{0, 2}, AheadKey{1, 0}, AheadKey{1, 1}, AheadKey{1, 2}})
    {
        const std::shared_ptr<const MCHdArt::Frame> frame = drawn(key);
        second = key == AheadKey{1, 0} ? frame : second;
    }

    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 5);

    // On round the loop, each frame's read ahead as the one before it is drawn; frame 0 comes again drawn already.
    for (int32_t frame = 2; frame < AheadFrames; ++frame)
    {
        MCHdArt::WaitForReadAhead();

        for (int32_t step = 0; step < AheadSteps; ++step)
        {
            drawn({frame, step});
        }
    }

    CHECK_EQ(MCHdArt::FramesRead(), read + AheadFrames * AheadSteps);
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + AheadFrames * AheadSteps - 1);
    // The reader opened its own handle on the archive once, and keeps it beside the game's.
    MCHdArt::WaitForReadAhead();
    CHECK_EQ(MCHdArt::ReaderFilesOpened(), opened + 1);
    CHECK_EQ(MCHdArt::ArchiveFilesOpen(), size_t{2});

    // A turn middle: its in-between, then the next frame's turn middle and its in-between.
    drawn({0, TurnStep});
    const size_t turned = MCHdArt::FramesReadAhead();
    MCHdArt::WaitForReadAhead();
    drawn({0, TurnBetweenStep(TurnStep, 1)});
    drawn({1, TurnStep});
    drawn({1, TurnBetweenStep(TurnStep, 1)});
    CHECK_EQ(MCHdArt::FramesReadAhead(), turned + 3);

    // Asked for at a size it wasn't made for, a frame read ahead is refused, as read when drawn it is.
    MCHdArt::WaitForReadAhead();
    CHECK(MCHdArt::FindShape(AheadTable, 2, AheadWidth + 1, AheadHeight, TurnStep, AheadFrames) == nullptr);
    CHECK_EQ(MCHdArt::FramesReadAhead(), turned + 3);

    // With room for one frame, frame 1 goes; drawn again after frame 0 is, it is read ahead again with its Id.
    REQUIRE(second != nullptr);
    const uint64_t id = second->Id;
    MCHdArt::SetCacheBudget(1);
    drawn({0, 0});
    const size_t dropped = MCHdArt::FramesReadAhead();
    MCHdArt::WaitForReadAhead();
    const std::shared_ptr<const MCHdArt::Frame> again = drawn({1, 0});
    CHECK_EQ(MCHdArt::FramesReadAhead(), dropped + 1);
    CHECK(again != second);
    CHECK_EQ(again->Id, id);
    MCHdArt::SetCacheBudget(0);
    // As read, with its colours (the cache's, which it lets go of as the pack is set again).
    MCHdArt::Frame againWhole = *again;
    const std::span<const uint8_t> againColors = MCHdArt::ColorsOf(*again);
    againWhole.Colors.assign(againColors.begin(), againColors.end());

    // Drawn first, with the pack set again, frame 1 is read as drawn: the frame read ahead. Setting the pack closed
    // both handles, and the reader still opened its own once.
    MCHdArt::SetPackFolder(scratch.Path());
    CHECK_EQ(MCHdArt::ArchiveFilesOpen(), size_t{0});
    CHECK_EQ(MCHdArt::ReaderFilesOpened(), opened + 1);
    const size_t fresh = MCHdArt::FramesReadAhead();
    const std::shared_ptr<const MCHdArt::Frame> first = drawn({1, 0});
    CHECK_EQ(MCHdArt::FramesReadAhead(), fresh);
    CHECK(SameFrame(*first, againWhole));

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: the camera's surroundings): a packet read ahead whole (MCHdArt::ReadPacketAhead, a mech
/// part's in or near the view) has every frame and its in-betweens read by the readers and kept, not its turn middles
/// (a follow's as one is drawn), and each is found at its draw without a read, the same frame; asked for again, nothing
/// more is read.
/// </summary>
TEST_CASE("hd art: a packet read ahead whole is found at its draws without a read, its turn middles left")
{
    const ScratchFolder scratch("packet-ahead");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::filesystem::path folder = MCHdArt::ShapeTableFolder(AheadTable);
    const auto readAhead = [&]
    {
        MCHdArt::ReadPacketAhead(folder, AheadFrames);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
    };

    const size_t ahead = MCHdArt::FramesReadAhead();
    readAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + static_cast<size_t>(AheadFrames * AheadSteps));
    const size_t read = MCHdArt::FramesRead();
    const size_t reads = MCHdArt::ReaderReads();

    for (int32_t frame = 0; frame < AheadFrames; ++frame)
    {
        for (int32_t step = 0; step < AheadSteps; ++step)
        {
            MCTest::Scope scope(std::format("frame {}", AheadName({frame, step})));
            const std::shared_ptr<const MCHdArt::Frame> found =
                MCHdArt::FindShape(AheadTable, frame, AheadWidth, AheadHeight, step);
            REQUIRE(found != nullptr);
            CHECK(SameFrame(*found, AheadFrame({frame, step}, 0)));
        }
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    readAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + static_cast<size_t>(AheadFrames * AheadSteps));
    CHECK_EQ(MCHdArt::ReaderReads(), reads);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: a gesture's start, MCSpriteTree::PacketsAhead): a packet read ahead from a frame on
/// (PacketAhead::First, a gesture played backwards from its end) reads those frames alone (the count read ahead), with
/// their in-betweens, each found at its draw without a read.
/// </summary>
TEST_CASE("hd art: a packet's last frames read ahead are found at their draws without a read, the others left")
{
    constexpr int32_t Last = 2;
    const ScratchFolder scratch("packet-ahead-last");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::array<MCHdArt::PacketAhead, 1> packet{
        {{MCHdArt::ShapeTableFolder(AheadTable), Last, false, AheadFrames - Last}}};

    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::ReadPacketsAhead(packet, MCHdArt::ReadAs::Soon);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + static_cast<size_t>(Last * AheadSteps));
    const size_t read = MCHdArt::FramesRead();

    for (int32_t frame = AheadFrames - Last; frame < AheadFrames; ++frame)
    {
        for (int32_t step = 0; step < AheadSteps; ++step)
        {
            MCTest::Scope scope(std::format("frame {}", AheadName({frame, step})));
            const std::shared_ptr<const MCHdArt::Frame> found =
                MCHdArt::FindShape(AheadTable, frame, AheadWidth, AheadHeight, step);
            REQUIRE(found != nullptr);
            CHECK(SameFrame(*found, AheadFrame({frame, step}, 0)));
        }
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: a mission's effects at its load): a shape table the loader read (MCHdArt::NoteShapeLoaded,
/// a laser's or an explosion's) has every frame and its in-betweens read by the readers at the load
/// (MCHdArt::ReadLoadedShapesAhead) and kept, so each is found at its first draw, at any facing, without a read on the
/// game's thread; a second call reads nothing more, the tables read since the first.
/// </summary>
TEST_CASE(
    "hd art: a shape table the loader read has every frame read ahead at the load, found at its draws without a read")
{
    const ScratchFolder scratch("loaded-shapes-ahead");
    // Any bytes stand for a table the loader read: the pack names the table's folder by their hash.
    const std::vector<uint8_t> table{0x45, 0x46, 0x46, 0x45, 0x43, 0x54, 0x01, 0x02};
    const uint64_t hash = MCHdArt::ShapeTableHash(table);
    WriteAheadPack(scratch.Path(), 0, hash);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    MCHdArt::NoteShapeLoaded(table);
    const auto readLoaded = []
    {
        MCHdArt::ReadLoadedShapesAhead();
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
    };

    const size_t ahead = MCHdArt::FramesReadAhead();
    readLoaded();
    const size_t whole = ahead + static_cast<size_t>(AheadFrames * AheadSteps);
    CHECK_EQ(MCHdArt::FramesReadAhead(), whole);
    const size_t read = MCHdArt::FramesRead();

    for (int32_t frame = 0; frame < AheadFrames; ++frame)
    {
        for (int32_t step = 0; step < AheadSteps; ++step)
        {
            MCTest::Scope scope(std::format("frame {}", AheadName({frame, step})));
            const std::shared_ptr<const MCHdArt::Frame> found =
                MCHdArt::FindShape(hash, frame, AheadWidth, AheadHeight, step, AheadFrames);
            REQUIRE(found != nullptr);
            CHECK(SameFrame(*found, AheadFrame({frame, step}, 0)));
        }
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    readLoaded();
    CHECK_EQ(MCHdArt::FramesReadAhead(), whole);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: proving nothing is late): the HD trace (MCHdArt::OpenTrace, -hdtrace) has a line for
/// each read asked of the readers, each of their reads, each archive once, each first use and each note, tab-separated:
/// a packet read ahead is its frames' asks and a read, and its first frame's draw a use of one kept.
/// </summary>
TEST_CASE("hd art: the HD trace has each read asked, each reader's read and each first use")
{
    const ScratchFolder scratch("hd-trace");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::filesystem::path trace = scratch.Path() / "trace.tsv";
    const std::filesystem::path folder = MCHdArt::ShapeTableFolder(AheadTable);
    REQUIRE(MCHdArt::OpenTrace(trace));
    MCHdArt::ReadPacketAhead(folder, AheadFrames);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    REQUIRE(MCHdArt::FindShape(AheadTable, 0, AheadWidth, AheadHeight, 0) != nullptr);
    MCHdArt::TraceNote("done");
    MCHdArt::CloseTrace();

    std::map<std::string, std::vector<std::vector<std::string>>> lines;
    std::ifstream in(trace);

    for (std::string line; std::getline(in, line);)
    {
        std::vector<std::string> fields;

        for (const auto part : std::views::split(line, '\t'))
        {
            fields.emplace_back(part.begin(), part.end());
        }

        lines[fields.front()].push_back(std::move(fields));
    }

    CHECK_EQ(lines["A"].size(), static_cast<size_t>(AheadFrames * AheadSteps));
    CHECK(!lines["R"].empty());
    REQUIRE_EQ(lines["P"].size(), 1u);
    REQUIRE_EQ(lines["U"].size(), 1u);
    CHECK_EQ(lines["U"].front()[2], "kept");
    CHECK_EQ(lines["U"].front()[3], MCFileSystem::ToUtf8(folder));
    CHECK_EQ(lines["U"].front()[4], "0");
    REQUIRE_EQ(lines["N"].size(), 1u);
    CHECK_EQ(lines["N"].front()[2], "done");
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: a load): an archive the readers open ahead (MCHdArt::OpenArchivesAhead, a mission's
/// mechs' and objects' at its load) is taken open by the game's thread at its first look in it, which draws its frames
/// as before; one asked for again isn't opened twice; and with the pack set before it's taken, the readers' is
/// forgotten and the game's thread opens it as it looks.
/// </summary>
TEST_CASE("hd art: an archive the readers open ahead is taken open at the first look in it")
{
    const ScratchFolder scratch("archive-ahead");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::array<std::filesystem::path, 1> folder{MCHdArt::ShapeTableFolder(AheadTable)};

    const size_t opened = MCHdArt::ArchivesOpenedAhead();
    MCHdArt::OpenArchivesAhead(folder);
    MCHdArt::OpenArchivesAhead(folder);
    MCHdArt::WaitForLoadReads();
    const std::shared_ptr<const MCHdArt::Frame> found = FindAhead({0, 0});
    REQUIRE(found != nullptr);
    CHECK(SameFrame(*found, AheadFrame({0, 0}, 0)));
    CHECK_EQ(MCHdArt::ArchivesOpenedAhead(), opened + 1);
    // Open already: asked again, nothing.
    MCHdArt::OpenArchivesAhead(folder);
    MCHdArt::WaitForLoadReads();
    CHECK(FindAhead({1, 0}) != nullptr);
    CHECK_EQ(MCHdArt::ArchivesOpenedAhead(), opened + 1);

    MCHdArt::SetPackFolder(scratch.Path());
    MCHdArt::OpenArchivesAhead(folder);
    MCHdArt::SetPackFolder(scratch.Path());
    CHECK(FindAhead({0, 0}) != nullptr);
    CHECK_EQ(MCHdArt::ArchivesOpenedAhead(), opened + 1);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// An archive the readers open ahead unpacks against the pack's dictionaries, as the game's thread read them: its frames
/// packed against dictionary 1 are drawn from it.
/// </summary>
TEST_CASE("hd art: an archive the readers open ahead unpacks against the pack's dictionaries")
{
    const ScratchFolder scratch("archive-ahead-dictionary");
    std::vector<ArchiveEntry> entries;

    for (const AheadKey& key : AheadKeys())
    {
        entries.push_back(Raw(AheadName(key) + ".hdf", AheadFile(key, 0)));
    }

    const std::filesystem::path table = scratch.Path() / MCHdArt::ShapeTableFolder(AheadTable);
    std::filesystem::create_directories(table.parent_path());
    WriteBytes(std::filesystem::path(table).concat(".hda"),
               BuildDictionaryArchive(entries, MCFastFile::StreamKind::Packet, 1, 1));
    WriteBytes(scratch.Path() / PackDictionariesName, DictionariesFile({{1, DictionaryOne}}));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::array<std::filesystem::path, 1> folder{MCHdArt::ShapeTableFolder(AheadTable)};

    const size_t opened = MCHdArt::ArchivesOpenedAhead();
    MCHdArt::OpenArchivesAhead(folder);
    MCHdArt::WaitForLoadReads();
    const std::shared_ptr<const MCHdArt::Frame> found = FindAhead({0, 0});
    REQUIRE(found != nullptr);
    CHECK(SameFrame(*found, AheadFrame({0, 0}, 0)));
    // Taken open from the readers, not opened again here.
    CHECK_EQ(MCHdArt::ArchivesOpenedAhead(), opened + 1);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: the camera's surroundings): a legs packet read ahead with its turn middles
/// (MCHdArt::PacketAhead::TurnMiddles, read soon) has every frame's steps and turn middles read by the readers, and each
/// is found at its draw without a read.
/// </summary>
TEST_CASE("hd art: a packet read ahead with its turn middles has them found at their draws without a read")
{
    const ScratchFolder scratch("packet-ahead-turns");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::array<MCHdArt::PacketAhead, 1> packet{{{MCHdArt::ShapeTableFolder(AheadTable), AheadFrames, true}}};

    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::ReadPacketsAhead(packet, MCHdArt::ReadAs::Soon);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + AheadKeys().size());
    const size_t read = MCHdArt::FramesRead();

    for (const AheadKey& key : AheadKeys())
    {
        MCTest::Scope scope(std::format("frame {}", AheadName(key)));
        const std::shared_ptr<const MCHdArt::Frame> found =
            MCHdArt::FindShape(AheadTable, key.first, AheadWidth, AheadHeight, key.second);
        REQUIRE(found != nullptr);
        CHECK(SameFrame(*found, AheadFrame(key, 0)));
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: the camera's surroundings): a packet read ahead without its frames' count (an object's,
/// whose shape isn't loaded) has every frame its archive holds read with its in-betweens, past a number the packet
/// skips (an empty frame of the original's), and each is found at its draw without a read; in a version 2 archive (its
/// frames counted by their keys) as in today's, and asked again, counted as before.
/// </summary>
TEST_CASE("hd art: a packet read ahead without its count reads every frame its archive holds, past a number skipped")
{
    constexpr int32_t Skipped = 2;
    const int previous = GHdArt;
    GHdArt = 1;

    for (const bool version2 : {false, true})
    {
        MCTest::Scope scope(version2 ? "version 2" : "version 1");
        const ScratchFolder scratch(version2 ? "packet-ahead-count-v2" : "packet-ahead-count");
        std::vector<ArchiveEntry> entries;

        for (const AheadKey& key : AheadKeys())
        {
            if (key.first != Skipped)
            {
                entries.push_back(Raw(AheadName(key) + ".hdf", AheadFile(key, 0)));
            }
        }

        const std::filesystem::path folder = MCHdArt::ShapeTableFolder(AheadTable);
        std::filesystem::create_directories((scratch.Path() / folder).parent_path());
        WriteBytes(std::filesystem::path(scratch.Path() / folder).concat(".hda"),
                   version2 ? BuildVersion2Archive(entries) : BuildArchive(entries));
        MCHdArt::SetPackFolder(scratch.Path());

        const size_t ahead = MCHdArt::FramesReadAhead();
        MCHdArt::ReadPacketAhead(folder);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        const size_t counted = ahead + static_cast<size_t>((AheadFrames - 1) * AheadSteps);
        CHECK_EQ(MCHdArt::FramesReadAhead(), counted);
        const size_t read = MCHdArt::FramesRead();

        for (int32_t frame = 0; frame < AheadFrames; ++frame)
        {
            for (int32_t step = 0; step < AheadSteps && frame != Skipped; ++step)
            {
                MCTest::Scope frameScope(std::format("frame {}", AheadName({frame, step})));
                const std::shared_ptr<const MCHdArt::Frame> found =
                    MCHdArt::FindShape(AheadTable, frame, AheadWidth, AheadHeight, step);
                REQUIRE(found != nullptr);
                CHECK(SameFrame(*found, AheadFrame({frame, step}, 0)));
            }
        }

        CHECK_EQ(MCHdArt::FramesRead(), read);

        // Asked again (its count kept from the first ask): every frame is kept, so none is read again.
        MCHdArt::ReadPacketAhead(folder);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::FramesReadAhead(), counted);
        MCHdArt::SetPackFolder({});
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (HD art's read-ahead): a mech part's neighbours in a turn are the facings beside its own, nearest first, the
/// one toward straight ahead first: round the turn where the part's facings go all the way round, else stopping at
/// straight ahead and back; a fall's every fourth facing steps four.
/// </summary>
TEST_CASE("sprite tree: a part's neighbours in a turn are the facings beside its own, round only where they go round")
{
    const auto near = [](uint32_t first, int32_t facings, int32_t step, bool mirrored, uint32_t index, int32_t reach)
    {
        MCGesturePacket where;
        where.Packet = first + index;
        where.FirstPacket = first;
        where.Facings = facings;
        where.FacingStep = step;
        where.Mirrored = mirrored;
        return MCSpriteTree::NeighbourPackets(where, reach);
    };

    // The legs' nine mirrored facings (straight ahead 0, straight back 8), from packet 18.
    CHECK((near(18, 9, 1, true, 0, 1) == std::vector<uint32_t>{19}));
    CHECK((near(18, 9, 1, true, 4, 1) == std::vector<uint32_t>{21, 23}));
    CHECK((near(18, 9, 1, true, 8, 1) == std::vector<uint32_t>{25}));
    // An unmirrored torso's 32 go round: straight ahead's are the last two and the next two.
    CHECK((near(64, 32, 1, false, 0, 2) == std::vector<uint32_t>{95, 65, 94, 66}));
    // A mirrored arm's 17, two each side of straight back.
    CHECK((near(17, 17, 1, true, 16, 2) == std::vector<uint32_t>{32, 31}));
    // A fall's every fourth facing, round the turn.
    CHECK((near(0, 32, 4, false, 28, 1) == std::vector<uint32_t>{24, 0}));
    // A packet outside its gesture's facings has none.
    CHECK(near(18, 9, 1, true, 9, 1).empty());
}

/// <summary>
/// Port-only: once a mech part's frame is drawn the first time, the reading thread reads the same frame (and its steps)
/// at the facings its shape has as neighbours in a turn, those its archive holds (none from another mech's archive, none
/// the archive lacks), and they are drawn as read when drawn; another facing's is read as drawn. The shapes keep their
/// neighbours, so with the pack set again they are read ahead again once the shapes are attached again.
/// </summary>
TEST_CASE("hd art: a frame drawn the first time has the same frame at its neighbouring facings read ahead")
{
    constexpr uint32_t Mech = 1;
    constexpr uint32_t OtherMech = 2;
    constexpr uint32_t Packets = 4;
    constexpr uint32_t Missing = 9;
    constexpr int32_t Frames = 2;
    constexpr int32_t Steps = 2;
    const ScratchFolder scratch("read-ahead-neighbours");
    const std::filesystem::path legs = scratch.Path() / "mechs" / "legs90";
    std::filesystem::create_directories(legs);
    // Each packet's frames made with its own seed, so no facing's frame is another's.
    const auto archive = [&](uint32_t mech)
    {
        std::vector<ArchiveEntry> entries;

        for (uint32_t packet = 0; packet < Packets; ++packet)
        {
            for (int32_t frame = 0; frame < Frames; ++frame)
            {
                for (int32_t step = 0; step < Steps; ++step)
                {
                    entries.push_back(Raw(std::format("{}\\{}.hdf", packet, AheadName({frame, step})),
                                          AheadFile({frame, step}, static_cast<uint8_t>(mech * Packets + packet))));
                }
            }
        }

        WriteBytes(legs / std::format("{}.hda", mech), BuildArchive(entries));
    };

    archive(Mech);
    archive(OtherMech);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    const std::vector<uint8_t> table =
        MCTestShapes::FramesTable(std::vector<MCTestShapes::TableFrame>(Frames, {0, 0, AheadWidth, AheadHeight, {}}));
    std::vector<std::shared_ptr<MCShape>> shapes;

    for (uint32_t packet = 0; packet < Packets; ++packet)
    {
        MCRegisteredBlock block(table.size(), MCDataKind::Shapes);
        std::ranges::copy(table, block.Bytes().begin());
        shapes.push_back(std::make_shared<MCShape>(std::move(block), std::weak_ptr<MCAppearanceType>(), 0));
        const MCHdArt::MechShapeKey key{"legs90", Mech, packet};
        shapes.back()->HdFolder = MCHdArt::FolderOf(key);
        MCHdArt::Attach(shapes.back(), key);
    }

    // Packet 1's neighbours: packets 0 and 2, one the archive lacks, and another mech's.
    MCHdArt::SetNeighbours(shapes[1], {MCHdArt::FolderOf(MCHdArt::MechShapeKey{"legs90", Mech, 0}),
                                       MCHdArt::FolderOf(MCHdArt::MechShapeKey{"legs90", Mech, 2}),
                                       MCHdArt::FolderOf(MCHdArt::MechShapeKey{"legs90", Mech, Missing}),
                                       MCHdArt::FolderOf(MCHdArt::MechShapeKey{"legs90", OtherMech, 1})});
    const auto drawn = [&](uint32_t packet, int32_t frame, int32_t step)
    {
        MCTest::Scope scope(std::format("packet {}, frame {}", packet, AheadName({frame, step})));
        const std::shared_ptr<const MCHdArt::Frame> found =
            MCHdArt::Find(shapes[packet]->FrameList(), frame, AheadWidth, AheadHeight, step);
        REQUIRE(found != nullptr);
        CHECK(SameFrame(*found, AheadFrame({frame, step}, static_cast<uint8_t>(Mech * Packets + packet))));
    };

    // Frame 0 of packet 1 drawn: frame 0 and its in-between at packets 0 and 2 are read ahead, and drawn so.
    const size_t neighbours = MCHdArt::NeighboursReadAhead();
    drawn(1, 0, 0);
    MCHdArt::WaitForReadAhead();
    CHECK_EQ(MCHdArt::NeighboursReadAhead(), neighbours + 2 * Steps);
    const size_t ahead = MCHdArt::FramesReadAhead();

    for (const uint32_t packet : {0u, 2u})
    {
        for (int32_t step = 0; step < Steps; ++step)
        {
            drawn(packet, 0, step);
        }
    }

    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 2 * Steps);
    // Packet 3, no neighbour of packet 1's, is read as drawn.
    MCHdArt::WaitForReadAhead();
    const size_t unrelated = MCHdArt::FramesReadAhead();
    drawn(3, 0, 0);
    CHECK_EQ(MCHdArt::FramesReadAhead(), unrelated);

    // The pack set again: the shapes attached again keep their neighbours, so frame 1 of packet 1 has packet 0's and
    // packet 2's read ahead.
    MCHdArt::SetPackFolder(scratch.Path());

    for (const std::shared_ptr<MCShape>& shape : shapes)
    {
        MCHdArt::AttachFolder(shape);
    }

    const size_t again = MCHdArt::NeighboursReadAhead();
    drawn(1, 1, 0);
    MCHdArt::WaitForReadAhead();
    CHECK_EQ(MCHdArt::NeighboursReadAhead(), again + 2 * Steps);
    const size_t takenAgain = MCHdArt::FramesReadAhead();
    drawn(0, 1, 0);
    drawn(2, 1, 1);
    CHECK_EQ(MCHdArt::FramesReadAhead(), takenAgain + 2);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

namespace
{
    /// <summary>HD art on with a pack while it lives, and as it was after it, however the test ends.</summary>
    class PackOn
    {
    public:
        explicit PackOn(const std::filesystem::path& pack) : _Previous(GHdArt)
        {
            GHdArt = 1;
            MCHdArt::SetPackFolder(pack);
        }

        ~PackOn()
        {
            GHdArt = _Previous;
            MCHdArt::SetPackFolder({});
        }

        PackOn(const PackOn&) = delete;
        PackOn& operator=(const PackOn&) = delete;

    private:
        int _Previous;
    };
} // namespace

/// <summary>
/// Port-only: the frames the reading thread read ahead are kept in the cache at a frame's start (MCHdArt::KeepReadAhead),
/// so that one drawn long after its read is still there: its draw finds it without a read, the same frame, and has the
/// frames drawn after it read ahead, as a first draw that reads one does; asked for at a size it wasn't made for, it is
/// refused at that draw, as one read then is.
/// </summary>
TEST_CASE("hd art: frames read ahead are kept at a frame's start and found by their draw, checked against its size")
{
    const ScratchFolder scratch("read-ahead-kept");
    WriteAheadPack(scratch.Path(), 0);
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    REQUIRE(FindAhead({0, 0}) != nullptr);
    MCHdArt::WaitForReadAhead();
    // The rest of frame 0's steps and frame 1's, kept.
    const size_t read = MCHdArt::FramesRead();
    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesRead(), read + 2 * AheadSteps - 1);
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 2 * AheadSteps - 1);

    for (const AheadKey& key : {AheadKey{0, 1}, AheadKey{0, 2}, AheadKey{1, 0}})
    {
        MCTest::Scope scope(std::format("frame {}", AheadName(key)));
        const std::shared_ptr<const MCHdArt::Frame> frame = FindAhead(key);
        REQUIRE(frame != nullptr);
        CHECK(SameFrame(*frame, AheadFrame(key, 0)));
        CHECK(frame->Id != 0);
    }

    // Frame 1's first draw had frame 2's steps read ahead.
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    const size_t kept = read + 3 * AheadSteps - 1;
    CHECK_EQ(MCHdArt::FramesRead(), kept);
    CHECK(FindAhead({2, 0}) != nullptr);

    // Asked for at another size, frame 1's in-between is refused, and stays so, as a frame read when drawn would.
    CHECK(MCHdArt::FindShape(AheadTable, 1, AheadWidth + 1, AheadHeight, 1, AheadFrames) == nullptr);
    CHECK(FindAhead({1, 1}) == nullptr);
    // None of them was read by its draw.
    CHECK_EQ(MCHdArt::FramesRead(), kept);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only: the HD copies of pictures read ahead (the logistics screens' art: MCLogistics::MakeAhead) are kept as a
/// first draw keeps a frame once the game's thread takes them in (a frame's start): found then without a read, the same
/// frames as read when drawn. One the pack lacks isn't read; one read already isn't asked for again; and with the frames
/// kept taking half the cache's budget, none is read ahead.
/// </summary>
TEST_CASE("hd art: pictures read ahead are found as drawn ones are, without a read")
{
    constexpr int32_t Width = AheadWidth;
    constexpr int32_t Height = AheadHeight;
    constexpr size_t InPack = 3;
    const ScratchFolder scratch("pictures-ahead");
    std::vector<std::vector<uint8_t>> pixels;
    std::vector<std::vector<uint8_t>> files;
    std::vector<ArchiveEntry> entries;

    for (uint8_t picture = 0; picture <= InPack; ++picture)
    {
        std::vector<uint8_t>& shown = pixels.emplace_back(static_cast<size_t>(Width) * Height);
        std::ranges::generate(shown, [&, at = 0]() mutable { return static_cast<uint8_t>(picture * Width + at++); });

        if (picture < InPack)
        {
            // A BC7 frame with bytes no other picture's has (as the read-ahead pack's frames).
            files.push_back(AheadFile({picture, 0}, static_cast<uint8_t>(picture + 1)));
            entries.push_back(
                Raw(MCHdArt::PictureName(MCHdArt::PictureHash(shown, Width, Height)) + ".hdf", files.back()));
        }
    }

    WriteBytes(scratch.Path() / "menus.hda", BuildArchive(entries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    std::vector<MCHdArt::PictureAhead> asked;

    for (const std::vector<uint8_t>& shown : pixels)
    {
        asked.push_back({shown, Width, Height});
    }

    const auto readAhead = [&]
    {
        MCHdArt::ReadPicturesAhead(asked);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
    };

    const size_t ahead = MCHdArt::FramesReadAhead();
    readAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + InPack);
    const size_t read = MCHdArt::FramesRead();

    for (size_t picture = 0; picture < InPack; ++picture)
    {
        MCTest::Scope scope(std::format("picture {}", picture));
        const std::shared_ptr<const MCHdArt::Frame> found = MCHdArt::FindPicture(pixels[picture], Width, Height);
        REQUIRE(found != nullptr);
        const auto decoded = MCHdArt::DecodeHdf(files[picture], Width, Height);
        REQUIRE(decoded.has_value());
        CHECK(SameFrame(*found, *decoded));
        CHECK(found->Id != 0);
    }

    CHECK(MCHdArt::FindPicture(pixels[InPack], Width, Height) == nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read);

    // Asked again: kept already, none read.
    readAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + InPack);

    // A new pack (the same files), its cache's budget half taken: none read ahead, each read as drawn.
    MCHdArt::SetPackFolder(scratch.Path());
    MCHdArt::SetCacheBudget(1);
    readAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + InPack);
    CHECK(MCHdArt::FindPicture(pixels[0], Width, Height) != nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read + 1);
    MCHdArt::SetCacheBudget(0);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only: the briefing maps' pictures lie in an archive of their own (<c>maps.hda</c>), so the menus' holds only what
/// the screens show: a picture is found in either, as drawn and as read ahead, and in a pack's loose <c>maps</c> folder.
/// </summary>
TEST_CASE("hd art: a briefing map's picture is found in maps.hda, a screen's in menus.hda, drawn or read ahead")
{
    constexpr int32_t Width = AheadWidth;
    constexpr int32_t Height = AheadHeight;
    // A screen's picture, a map's, and one the pack lacks.
    constexpr size_t Screen = 0;
    constexpr size_t Map = 1;
    constexpr size_t Lacked = 2;
    const ScratchFolder scratch("pictures-maps");
    std::vector<std::vector<uint8_t>> pixels;
    std::vector<std::vector<uint8_t>> files;
    std::vector<std::string> names;

    for (uint8_t picture = 0; picture <= Lacked; ++picture)
    {
        std::vector<uint8_t>& shown = pixels.emplace_back(static_cast<size_t>(Width) * Height);
        std::ranges::generate(shown, [&, at = 0]() mutable { return static_cast<uint8_t>(picture * Width + at++); });
        files.push_back(AheadFile({picture, 0}, static_cast<uint8_t>(picture + 1)));
        names.push_back(MCHdArt::PictureName(MCHdArt::PictureHash(shown, Width, Height)) + ".hdf");
    }

    const std::filesystem::path archived = scratch.Path() / "archived";
    std::filesystem::create_directories(archived);
    WriteBytes(archived / "menus.hda", BuildArchive({Raw(names[Screen], files[Screen])}));
    WriteBytes(archived / "maps.hda", BuildArchive({Raw(names[Map], files[Map])}));
    const int previous = GHdArt;
    GHdArt = 1;

    const auto found = [&](size_t picture)
    {
        MCTest::Scope scope(std::format("picture {}", picture));
        const std::shared_ptr<const MCHdArt::Frame> frame = MCHdArt::FindPicture(pixels[picture], Width, Height);
        REQUIRE(frame != nullptr);
        const auto decoded = MCHdArt::DecodeHdf(files[picture], Width, Height);
        REQUIRE(decoded.has_value());
        CHECK(SameFrame(*frame, *decoded));
    };

    // Drawn: each read from its archive.
    MCHdArt::SetPackFolder(archived);
    size_t read = MCHdArt::FramesRead();
    found(Screen);
    found(Map);
    CHECK(MCHdArt::FindPicture(pixels[Lacked], Width, Height) == nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read + 2);

    // Read ahead, from both archives: found without a read.
    MCHdArt::SetPackFolder(archived);
    const std::vector<MCHdArt::PictureAhead> asked = {
        {pixels[Screen], Width, Height}, {pixels[Map], Width, Height}, {pixels[Lacked], Width, Height}};
    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::ReadPicturesAhead(asked);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 2);
    read = MCHdArt::FramesRead();
    found(Screen);
    found(Map);
    CHECK_EQ(MCHdArt::FramesRead(), read);

    // A generator's pack: the map's frame file loose in maps/.
    const std::filesystem::path loose = scratch.Path() / "loose";
    std::filesystem::create_directories(loose / "maps");
    WriteBytes(loose / "maps" / names[Map], files[Map]);
    MCHdArt::SetPackFolder(loose);
    found(Map);
    CHECK(MCHdArt::FindPicture(pixels[Screen], Width, Height) == nullptr);

    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

namespace
{
    /// <summary>The screens' set read ahead (the tests below), its archives <paramref name="version"/>'s.</summary>
    void CheckMenuSetAhead(PackVersion version)
    {
        constexpr int32_t Width = AheadWidth;
        constexpr int32_t Height = AheadHeight;
        // The screens' pictures, then one whose name is a picture of another size's (its file made for this size), then a
        // map.
        constexpr uint8_t Screens = 4;
        constexpr uint8_t Misfit = Screens;
        constexpr uint8_t Map = Screens + 1;
        constexpr int32_t MisfitWidth = Width + 2;
        // The menus' and the maps' archives as <paramref name="version"/>'s: a version 2 one each picture its own stream, as
        // the pack's are, its names kept (or not).
        const auto archive = [&](const std::vector<ArchiveEntry>& entries, bool keepNames = true)
        {
            if (version == PackVersion::One)
            {
                return BuildArchive(entries);
            }

            std::vector<Version2Stream> streams;

            for (const ArchiveEntry& entry : entries)
            {
                streams.push_back(Version2Stream{.Entries = {entry}, .Kind = MCFastFile::StreamKind::Pictures});
            }

            return BuildVersion2Streams(streams, {}, keepNames);
        };

        const ScratchFolder scratch(version == PackVersion::One ? "menu-set" : "menu-set-2");
        std::vector<std::vector<uint8_t>> pixels;
        std::vector<std::vector<uint8_t>> files;
        std::vector<std::string> names;

        for (uint8_t picture = 0; picture <= Map; ++picture)
        {
            const int32_t width = picture == Misfit ? MisfitWidth : Width;
            std::vector<uint8_t>& shown = pixels.emplace_back(static_cast<size_t>(width) * Height);
            std::ranges::generate(shown,
                                  [&, at = 0]() mutable { return static_cast<uint8_t>(picture * Width + at++); });
            files.push_back(AheadFile({picture, 0}, static_cast<uint8_t>(picture + 1)));
            names.push_back(MCHdArt::PictureName(MCHdArt::PictureHash(shown, width, Height)) + ".hdf");
        }

        std::vector<ArchiveEntry> set;

        for (uint8_t picture = 0; picture < Map; ++picture)
        {
            set.push_back(Raw(names[picture], files[picture]));
        }

        const std::filesystem::path split = scratch.Path() / "split";
        std::filesystem::create_directories(split);
        WriteBytes(split / "menus.hda", archive(set));
        WriteBytes(split / "maps.hda", archive({Raw(names[Map], files[Map])}));
        const std::filesystem::path before = scratch.Path() / "before";
        std::filesystem::create_directories(before);
        set.push_back(Raw(names[Map], files[Map]));
        WriteBytes(before / "menus.hda", archive(set));
        const int previous = GHdArt;
        GHdArt = 1;

        // The split pack: the set read ahead, the map not.
        MCHdArt::SetPackFolder(split);
        size_t ahead = MCHdArt::FramesReadAhead();
        CHECK(MCHdArt::ReadMenuSetAhead() > 0);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + Screens + 1);
        CHECK_EQ(MCHdArt::ReadMenuSetAhead(), 0u);
        size_t read = MCHdArt::FramesRead();

        for (uint8_t picture = 0; picture < Screens; ++picture)
        {
            MCTest::Scope scope(std::format("picture {}", picture));
            const std::shared_ptr<const MCHdArt::Frame> frame = MCHdArt::FindPicture(pixels[picture], Width, Height);
            REQUIRE(frame != nullptr);
            const auto decoded = MCHdArt::DecodeHdf(files[picture], Width, Height);
            REQUIRE(decoded.has_value());
            CHECK(SameFrame(*frame, *decoded));
        }

        CHECK_EQ(MCHdArt::FramesRead(), read);
        CHECK(MCHdArt::FindPicture(pixels[Misfit], MisfitWidth, Height) == nullptr);
        read = MCHdArt::FramesRead();
        CHECK(MCHdArt::FindPicture(pixels[Map], Width, Height) != nullptr);
        CHECK_EQ(MCHdArt::FramesRead(), read + 1);

        // The pack made before: nothing read ahead.
        MCHdArt::SetPackFolder(before);
        ahead = MCHdArt::FramesReadAhead();
        CHECK_EQ(MCHdArt::ReadMenuSetAhead(), 0u);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead);

        // Version 2 without its names: nothing read ahead (logged), the pictures left to their draws.
        if (version == PackVersion::Two)
        {
            const std::filesystem::path nameless = scratch.Path() / "nameless";
            std::filesystem::create_directories(nameless);
            set.pop_back();
            WriteBytes(nameless / "menus.hda", archive(set, false));
            WriteBytes(nameless / "maps.hda", archive({Raw(names[Map], files[Map])}));
            MCHdArt::SetPackFolder(nameless);
            CHECK_EQ(MCHdArt::ReadMenuSetAhead(), 0u);
        }

        GHdArt = previous;
        MCHdArt::SetPackFolder({});
    }
}

/// <summary>
/// Port-only (SPEC-first-use.md): with the briefing maps in an archive of their own, the screens' whole set is read
/// ahead (MCHdArt::ReadMenuSetAhead), every picture in the menus' archive whatever shows it, and each is found as drawn
/// without a read; one drawn at a size it wasn't made for is refused, as when read at its draw. The maps are left to
/// their draw, and a pack made before (its maps in the menus' archive) to the screens' own read-ahead.
/// </summary>
TEST_CASE("hd art: the screens' whole set is read ahead where the maps lie apart, each found without a read")
{
    CheckMenuSetAhead(PackVersion::One);
}

/// <summary>
/// Port-only: a version 2 pack's screens' set is read ahead as version 1's is, found by its menus' archive's names
/// (SPEC-pack-v2.md: it has no directory), which the game reads for it alone; one keeping none reads nothing ahead.
/// It read nothing ahead before (19 Nov: pack v2's final art), finding the set in the directory version 2 lacks.
/// </summary>
TEST_CASE("hd art: a version 2 pack's screens' set is read ahead by its menus' archive's names")
{
    CheckMenuSetAhead(PackVersion::Two);
}

/// <summary>
/// Port-only (SPEC-first-use.md): several readers read a set of pictures ahead in its archive's order, the archive's
/// neighbouring entries in one read, split where a gap between two passes MCHdArt::ReaderRunGap or the read would take
/// more than MCHdArt::ReaderRunBytes. Each picture is then found as drawn ones are, the same frame, without a read, and
/// the readers open no more handles on the archive than there were reads at once.
/// </summary>
TEST_CASE("hd art: several readers read a set ahead, an archive's neighbouring entries in one read")
{
    constexpr size_t Readers = 4;
    constexpr size_t Pictures = 24;
    constexpr int32_t SmallSide = 8;
    constexpr int32_t PixelMix = 3;
    constexpr int32_t FileMix = 5;

    struct Layout
    {
        std::string_view Name;
        int32_t Width = 0;
        int32_t Height = 0;
        bool Gap = false;
    };

    const ReaderScope readers({.Readers = Readers});
    const int previous = GHdArt;
    GHdArt = 1;

    for (const Layout& layout : {Layout{"small, back to back", SmallSide, SmallSide, false},
                                 Layout{"small, a gap past the widest read across", SmallSide, SmallSide, true},
                                 Layout{"large, past the most bytes a read takes", AheadWidth, AheadHeight, false}})
    {
        MCTest::Scope scope{std::string(layout.Name)};
        const ScratchFolder scratch("set-ahead");
        std::vector<std::vector<uint8_t>> pixels;
        std::vector<std::vector<uint8_t>> files;
        std::vector<ArchiveEntry> entries;

        for (size_t picture = 0; picture < Pictures; ++picture)
        {
            // Bytes no picture of the set holds, between its halves.
            if (layout.Gap && picture == Pictures / 2)
            {
                entries.push_back(Raw("between.bin", std::vector<uint8_t>(MCHdArt::ReaderRunGap + 1)));
            }

            std::vector<uint8_t>& shown = pixels.emplace_back(static_cast<size_t>(layout.Width) * layout.Height);
            std::ranges::generate(shown, [at = 0]() mutable { return static_cast<uint8_t>(at++ * PixelMix); });
            shown.front() = static_cast<uint8_t>(picture);
            files.push_back(FrameFile(layout.Width, layout.Height, static_cast<int32_t>(picture) * FileMix + 1));
            const uint64_t hash = MCHdArt::PictureHash(shown, layout.Width, layout.Height);
            entries.push_back(Raw(MCHdArt::PictureName(hash) + ".hdf", files.back()));
        }

        WriteBytes(scratch.Path() / "menus.hda", BuildArchive(entries));
        MCHdArt::SetPackFolder(scratch.Path());
        std::vector<MCHdArt::PictureAhead> asked;

        for (const std::vector<uint8_t>& shown : pixels)
        {
            asked.push_back({shown, layout.Width, layout.Height});
        }

        // The reads the set takes: its halves apart, each in as many as its bytes need at the most a read takes.
        const size_t halves = layout.Gap ? 2 : 1;
        const size_t inRead = std::max<size_t>(1, static_cast<size_t>(MCHdArt::ReaderRunBytes / files.front().size()));
        const size_t reads = halves * ((Pictures / halves + inRead - 1) / inRead);
        const size_t readsBefore = MCHdArt::ReaderReads();
        const size_t opened = MCHdArt::ReaderFilesOpened();
        const size_t ahead = MCHdArt::FramesReadAhead();
        MCHdArt::ReadPicturesAhead(asked);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + Pictures);
        CHECK_EQ(MCHdArt::ReaderReads(), readsBefore + reads);
        CHECK(MCHdArt::ReaderFilesOpened() > opened);
        CHECK(MCHdArt::ReaderFilesOpened() <= opened + std::min(reads, Readers));
        const size_t read = MCHdArt::FramesRead();

        for (size_t picture = 0; picture < Pictures; ++picture)
        {
            MCTest::Scope pictureScope(std::format("picture {}", picture));
            const std::shared_ptr<const MCHdArt::Frame> found =
                MCHdArt::FindPicture(pixels[picture], layout.Width, layout.Height);
            REQUIRE(found != nullptr);
            const auto decoded = MCHdArt::DecodeHdf(files[picture], layout.Width, layout.Height);
            REQUIRE(decoded.has_value());
            CHECK(SameFrame(*found, *decoded));
        }

        CHECK_EQ(MCHdArt::FramesRead(), read);
        // Closed before the scratch folder goes (a file open can't be deleted on Windows).
        MCHdArt::SetPackFolder({});
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-first-use.md): a load's ground read ahead (MCHdArt::ReadGroundAhead) is read by the readers, its tiles
/// and their relief maps each archive's in one read, a tile asked for twice read once and one the pack lacks left out;
/// once waited for and kept, each is found as a drawn one is, the same frame, without a read.
/// </summary>
TEST_CASE("hd art: a load's ground is read ahead by the readers, tiles and relief maps, found without a read")
{
    constexpr size_t InPack = 12;
    constexpr int32_t Side = 8;
    constexpr int32_t ReliefMix = 101;
    constexpr uint8_t TileByte = 0x5a;
    const ScratchFolder scratch("ground-ahead");
    std::vector<std::vector<uint8_t>> tiles;
    std::vector<std::vector<uint8_t>> tileFiles;
    std::vector<std::vector<uint8_t>> reliefFiles;
    std::vector<ArchiveEntry> tileEntries;
    std::vector<ArchiveEntry> reliefEntries;

    // The last tile isn't in the pack.
    for (size_t tile = 0; tile <= InPack; ++tile)
    {
        tiles.push_back({static_cast<uint8_t>(tile), TileByte});

        if (tile < InPack)
        {
            const auto mix = static_cast<int32_t>(tile);
            tileFiles.push_back(FrameFile(Side, Side, mix + 1));
            reliefFiles.push_back(FrameFile(Side, Side, mix + ReliefMix));
            const std::string name = MCHdArt::TerrainTileName(tiles.back()) + ".hdf";
            tileEntries.push_back(Raw(name, tileFiles.back()));
            reliefEntries.push_back(Raw(name, reliefFiles.back()));
        }
    }

    WriteBytes(scratch.Path() / "terrain.hda", BuildArchive(tileEntries));
    WriteBytes(scratch.Path() / "relief.hda", BuildArchive(reliefEntries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    std::vector<MCHdArt::GroundAhead> asked;

    for (const std::vector<uint8_t>& tile : tiles)
    {
        asked.push_back({tile, Side, Side});
    }

    asked.push_back(asked.front());
    const size_t ahead = MCHdArt::FramesReadAhead();
    const size_t reads = MCHdArt::ReaderReads();
    MCHdArt::ReadGroundAhead(asked, true);
    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 2 * InPack);
    // Each archive's entries back to back: a read each.
    CHECK_EQ(MCHdArt::ReaderReads(), reads + 2);
    const size_t read = MCHdArt::FramesRead();

    for (size_t tile = 0; tile < InPack; ++tile)
    {
        MCTest::Scope scope(std::format("tile {}", tile));
        const std::shared_ptr<const MCHdArt::Frame> found = MCHdArt::FindTerrainTile(tiles[tile], Side, Side);
        const std::shared_ptr<const MCHdArt::Frame> relief = MCHdArt::FindTerrainRelief(tiles[tile], Side, Side);
        REQUIRE(found != nullptr);
        REQUIRE(relief != nullptr);
        const auto decoded = MCHdArt::DecodeHdf(tileFiles[tile], Side, Side);
        const auto decodedRelief = MCHdArt::DecodeHdf(reliefFiles[tile], Side, Side);
        REQUIRE(decoded.has_value());
        REQUIRE(decodedRelief.has_value());
        CHECK(SameFrame(*found, *decoded));
        CHECK(SameFrame(*relief, *decodedRelief));
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    CHECK(MCHdArt::FindTerrainTile(tiles.back(), Side, Side) == nullptr);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md: a load): a load's overlays read ahead (MCHdArt::ReadOverlaysAhead, the map's roads,
/// banks and rubble) are read by the readers as a load's reads, and once waited for and kept, each is found as a drawn
/// one is (MCHdArt::FindOverlay), the same frame, without a read.
/// </summary>
TEST_CASE("hd art: a load's overlays are read ahead by the readers, found without a read")
{
    constexpr size_t Overlays = 6;
    constexpr int32_t Side = 8;
    constexpr uint8_t ShapeByte = 0x3c;
    const ScratchFolder scratch("overlays-ahead");
    std::vector<std::vector<uint8_t>> shapes;
    std::vector<std::vector<uint8_t>> files;
    std::vector<ArchiveEntry> entries;

    for (size_t overlay = 0; overlay < Overlays; ++overlay)
    {
        shapes.push_back({static_cast<uint8_t>(overlay), ShapeByte, ShapeByte});
        files.push_back(FrameFile(Side, Side, static_cast<int32_t>(overlay) + 1));
        entries.push_back(Raw(MCHdArt::TerrainTileName(shapes.back()) + ".hdf", files.back()));
    }

    WriteBytes(scratch.Path() / "overlays.hda", BuildArchive(entries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    std::vector<MCHdArt::GroundAhead> asked;

    for (const std::vector<uint8_t>& shape : shapes)
    {
        asked.push_back({shape, Side, Side});
    }

    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::ReadOverlaysAhead(asked);
    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + Overlays);
    const size_t read = MCHdArt::FramesRead();

    for (size_t overlay = 0; overlay < Overlays; ++overlay)
    {
        MCTest::Scope scope(std::format("overlay {}", overlay));
        const std::shared_ptr<const MCHdArt::Frame> found = MCHdArt::FindOverlay(shapes[overlay], Side, Side);
        REQUIRE(found != nullptr);
        const auto decoded = MCHdArt::DecodeHdf(files[overlay], Side, Side);
        REQUIRE(decoded.has_value());
        CHECK(SameFrame(*found, *decoded));
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md): shapes' frames read ahead (MCHdArt::ReadShapesAhead, the cursors' at the game's start)
/// are read by the readers as a load's, several tables' folders in one archive in one read, and found once kept as a
/// drawn one is, the same frames, without a read.
/// </summary>
TEST_CASE("hd art: shapes' frames are read ahead by the readers, several tables' in one read, found without a read")
{
    constexpr size_t Tables = 6;
    constexpr int32_t Side = 8;
    constexpr uint8_t TableByte = 0xc3;
    const ScratchFolder scratch("shapes-ahead");
    std::vector<std::vector<uint8_t>> tables;
    std::vector<std::vector<uint8_t>> files;
    std::vector<ArchiveEntry> entries;

    for (size_t table = 0; table < Tables; ++table)
    {
        tables.push_back({static_cast<uint8_t>(table), TableByte});
        files.push_back(FrameFile(Side, Side, static_cast<int32_t>(table) + 1));
        const std::filesystem::path folder = MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(tables.back()));
        entries.push_back(Raw(MCFileSystem::ToUtf8(folder.filename()) + "\\0.hdf", files.back()));
    }

    WriteBytes(scratch.Path() / std::filesystem::path(MCHdArt::ShapeTableFolder(0)).parent_path().concat(".hda"),
               BuildArchive(entries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    std::vector<MCHdArt::ShapeAhead> asked;

    for (const std::vector<uint8_t>& table : tables)
    {
        asked.push_back({table, 0, Side, Side});
    }

    const size_t ahead = MCHdArt::FramesReadAhead();
    const size_t reads = MCHdArt::ReaderReads();
    MCHdArt::ReadShapesAhead(asked, MCHdArt::ReadAs::Load);
    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + Tables);
    CHECK_EQ(MCHdArt::ReaderReads(), reads + 1);
    const size_t read = MCHdArt::FramesRead();

    for (size_t table = 0; table < Tables; ++table)
    {
        MCTest::Scope scope(std::format("table {}", table));
        const std::shared_ptr<const MCHdArt::Frame> found =
            MCHdArt::FindShape(MCHdArt::ShapeTableHash(tables[table]), 0, Side, Side);
        REQUIRE(found != nullptr);
        const auto decoded = MCHdArt::DecodeHdf(files[table], Side, Side);
        REQUIRE(decoded.has_value());
        CHECK(SameFrame(*found, *decoded));
    }

    CHECK_EQ(MCHdArt::FramesRead(), read);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

namespace
{
    /// <summary>
    /// Port-only: an archive of shapes' frames for the readers' runs (TakeRun): each piece a frame (a table's frame 0, of
    /// <see cref="Side"/>'s or its own size) or bytes no frame holds, in the archive's order as given.
    /// </summary>
    struct RunPiece
    {
        int32_t Width = 0;
        int32_t Height = 0;
        size_t FillerBytes = 0;
    };

    class RunArchive
    {
    public:
        static constexpr int32_t Side = 8;

        /// <summary>
        /// <paramref name="pieces"/> in an archive of version 1, or (<paramref name="version2"/>) of version 2 in one stream,
        /// the bytes no frame holds an entry of their own there too.
        /// </summary>
        RunArchive(std::string_view name, std::span<const RunPiece> pieces, bool version2 = false) : _Scratch(name)
        {
            constexpr uint8_t TableByte = 0x5e;
            std::vector<ArchiveEntry> entries;

            for (const RunPiece& piece : pieces)
            {
                if (piece.FillerBytes > 0)
                {
                    entries.push_back(
                        Raw(std::format("filler{}.bin", entries.size()), std::vector<uint8_t>(piece.FillerBytes)));
                    continue;
                }

                const auto number = static_cast<uint8_t>(_Tables.size());
                _Tables.push_back({number, TableByte, static_cast<uint8_t>(entries.size())});
                _Sizes.emplace_back(piece.Width, piece.Height);
                _Files.push_back(FrameFile(piece.Width, piece.Height, static_cast<int32_t>(number) + 1));
                const std::filesystem::path folder = MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(_Tables.back()));
                entries.push_back(Raw(MCFileSystem::ToUtf8(folder.filename()) + "\\0.hdf", _Files.back()));
            }

            WriteBytes(_Scratch.Path() /
                           std::filesystem::path(MCHdArt::ShapeTableFolder(0)).parent_path().concat(".hda"),
                       version2 ? BuildVersion2Archive(entries) : BuildArchive(entries));
            MCHdArt::SetPackFolder(_Scratch.Path());
        }

        ~RunArchive() { MCHdArt::SetPackFolder({}); }
        RunArchive(const RunArchive&) = delete;
        RunArchive& operator=(const RunArchive&) = delete;

        /// <summary>Has the readers read frame <paramref name="frame"/> (the archive's frames counted) as <paramref name="as"/>.</summary>
        void Ask(size_t frame, MCHdArt::ReadAs as) const
        {
            const MCHdArt::ShapeAhead shape{_Tables[frame], 0, _Sizes[frame].first, _Sizes[frame].second};
            MCHdArt::ReadShapesAhead(std::span(&shape, 1), as);
        }

        /// <summary>Frame <paramref name="frame"/> as drawn.</summary>
        std::shared_ptr<const MCHdArt::Frame> Find(size_t frame) const
        {
            return MCHdArt::FindShape(MCHdArt::ShapeTableHash(_Tables[frame]), 0, _Sizes[frame].first,
                                      _Sizes[frame].second);
        }

        /// <summary>Checks each frame is found without a read, as its own file decodes.</summary>
        void CheckFound() const
        {
            const size_t read = MCHdArt::FramesRead();

            for (size_t frame = 0; frame < _Tables.size(); ++frame)
            {
                MCTest::Scope scope(std::format("frame {}", frame));
                const std::shared_ptr<const MCHdArt::Frame> found = Find(frame);
                REQUIRE(found != nullptr);
                const auto decoded = MCHdArt::DecodeHdf(_Files[frame], _Sizes[frame].first, _Sizes[frame].second);
                REQUIRE(decoded.has_value());
                CHECK(SameFrame(*found, *decoded));
            }

            CHECK_EQ(MCHdArt::FramesRead(), read);
        }

        size_t Frames() const { return _Tables.size(); }
        size_t FileBytes(size_t frame) const { return _Files[frame].size(); }

    private:
        ScratchFolder _Scratch;
        std::vector<std::vector<uint8_t>> _Tables;
        std::vector<std::pair<int32_t, int32_t>> _Sizes;
        std::vector<std::vector<uint8_t>> _Files;
    };

    /// <summary>A small frame, of RunArchive::Side.</summary>
    constexpr RunPiece Small{RunArchive::Side, RunArchive::Side, 0};
} // namespace

/// <summary>
/// Port-only (SPEC-first-use.md): a reader takes the first read waiting and with it every read of the same archive
/// waiting whose entry lies within MCHdArt::ReaderRunGap of the read's span, before it or after it, asked for in any
/// order, in one read of at most MCHdArt::ReaderRunBytes: a gap wider than that splits them, the read's limit too, and an
/// entry larger than it is read alone. Each frame is then found as drawn ones are, the same frame, without a read.
/// </summary>
TEST_CASE("hd art: the readers read an archive's waiting entries near each other in one read, in any order asked")
{
    // A frame of a third of the most a read takes (64 bytes of BC7 a shape pixel at the scale), one larger than it all.
    constexpr RunPiece Third{80, 60, 0};
    constexpr RunPiece Larger{130, 130, 0};
    constexpr RunPiece Gap{0, 0, MCHdArt::ReaderRunGap + 1};
    const ReaderScope readers({.Readers = 1});
    const int previous = GHdArt;
    GHdArt = 1;

    struct Layout
    {
        std::string_view Name;
        std::vector<RunPiece> Pieces;
        std::vector<size_t> Asked;
        size_t Reads = 0;
    };

    for (const Layout& layout :
         {Layout{"back to back", std::vector<RunPiece>(8, Small), {5, 2, 7, 0, 3, 6, 1, 4}, 1},
          Layout{"a gap past the widest read across",
                 {Small, Small, Small, Small, Gap, Small, Small, Small, Small},
                 {6, 1, 4, 3, 0, 7, 2, 5},
                 2},
          Layout{"three to the most a read takes", std::vector<RunPiece>(9, Third), {0, 1, 2, 3, 4, 5, 6, 7, 8}, 3},
          Layout{"one larger than a read takes", {Small, Small, Larger, Small, Small}, {3, 2, 0, 4, 1}, 3}})
    {
        MCTest::Scope scope{std::string(layout.Name)};
        const RunArchive archive("runs", layout.Pieces);
        REQUIRE(archive.FileBytes(0) * 3 <= MCHdArt::ReaderRunBytes);
        const size_t reads = MCHdArt::ReaderReads();
        const size_t ahead = MCHdArt::FramesReadAhead();
        MCHdArt::HoldReads(true);

        for (const size_t frame : layout.Asked)
        {
            archive.Ask(frame, MCHdArt::ReadAs::Set);
        }

        MCHdArt::HoldReads(false);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + archive.Frames());
        CHECK_EQ(MCHdArt::ReaderReads(), reads + layout.Reads);
        archive.CheckFound();
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-first-use.md): the read a reader takes first is the most urgent waiting (a load's before the sets'),
/// and the less urgent reads waiting near it come in the same read; a read whose frame the game's thread read itself
/// meanwhile isn't read.
/// </summary>
TEST_CASE("hd art: the readers' first read takes the less urgent reads near it, and none the game's thread read")
{
    const ReaderScope readers({.Readers = 1});
    const int previous = GHdArt;
    GHdArt = 1;

    {
        MCTest::Scope scope("a load's frame amid a set's");
        const RunArchive archive("runs-urgent", std::vector<RunPiece>(8, Small));
        const size_t reads = MCHdArt::ReaderReads();
        const size_t ahead = MCHdArt::FramesReadAhead();
        MCHdArt::HoldReads(true);

        for (const size_t frame : {0, 1, 2, 3, 5, 6, 7})
        {
            archive.Ask(frame, MCHdArt::ReadAs::Set);
        }

        archive.Ask(4, MCHdArt::ReadAs::Load);
        MCHdArt::HoldReads(false);
        MCHdArt::WaitForLoadReads();
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::ReaderReads(), reads + 1);
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + archive.Frames());
        archive.CheckFound();
    }

    {
        MCTest::Scope scope("a frame the game's thread read while it waited");
        const RunArchive archive("runs-read", std::vector<RunPiece>(8, Small));
        constexpr size_t ReadByTheGame = 2;
        const size_t reads = MCHdArt::ReaderReads();
        const size_t ahead = MCHdArt::FramesReadAhead();
        MCHdArt::HoldReads(true);

        for (size_t frame = 0; frame < archive.Frames(); ++frame)
        {
            archive.Ask(frame, MCHdArt::ReadAs::Set);
        }

        const size_t read = MCHdArt::FramesRead();
        REQUIRE(archive.Find(ReadByTheGame) != nullptr);
        CHECK_EQ(MCHdArt::FramesRead(), read + 1);
        MCHdArt::HoldReads(false);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::ReaderReads(), reads + 1);
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + archive.Frames() - 1);
        archive.CheckFound();
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-pack-v2.md): a version 2 archive's stream is read once for all its entries waiting, asked for in any
/// order, however much larger it is than a read of neighbouring entries takes (MCHdArt::ReaderRunBytes): one reader takes
/// them all, the others none (each holding the run it took until all have taken theirs); and unpacked once. Kept
/// unpacked after, its other entries asked for later need no read. Each frame is found as drawn ones are, the same frame,
/// without a read.
/// </summary>
TEST_CASE("hd art: the readers read a version 2 stream once for all its entries, however large it is")
{
    constexpr size_t Asked = 6;
    constexpr RunPiece Filler{0, 0, MCHdArt::ReaderRunBytes};
    const ReaderScope readers({.Readers = 4});
    const int previous = GHdArt;
    GHdArt = 1;
    std::vector<RunPiece> pieces(Asked + 2, Small);
    pieces.push_back(Filler);
    const RunArchive archive("runs-stream", pieces, true);
    const size_t reads = MCHdArt::ReaderReads();
    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::HoldReads(true);

    for (const size_t frame : {4, 1, 5, 0, 3, 2})
    {
        archive.Ask(frame, MCHdArt::ReadAs::Set);
    }

    MCHdArt::HoldTakenRuns(true);
    MCHdArt::HoldReads(false);
    MCHdArt::WaitForHeldRun();
    MCHdArt::HoldTakenRuns(false);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::ReaderReads(), reads + 1);
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + Asked);

    {
        MCTest::Scope scope("the stream's other frames asked for after, kept unpacked");

        for (size_t frame = Asked; frame < archive.Frames(); ++frame)
        {
            archive.Ask(frame, MCHdArt::ReadAs::Set);
        }

        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::ReaderReads(), reads + 1);
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + archive.Frames());
    }

    archive.CheckFound();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-pack-v2.md): an entry asked for while a reader reads its stream (another reader free to read it) waits
/// on that read, and is served from it: two entries of one stream asked for one after the other are one read.
/// </summary>
TEST_CASE("hd art: an entry asked for while its version 2 stream is read is served by that read")
{
    constexpr RunPiece Filler{0, 0, MCHdArt::ReaderRunBytes};
    const ReaderScope readers({.Readers = 2});
    const int previous = GHdArt;
    GHdArt = 1;
    const RunArchive archive("runs-joined", std::vector<RunPiece>{Small, Small, Filler}, true);
    const size_t reads = MCHdArt::ReaderReads();
    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::HoldTakenRuns(true);
    archive.Ask(0, MCHdArt::ReadAs::Set);
    MCHdArt::WaitForHeldRun();
    archive.Ask(1, MCHdArt::ReadAs::Set);
    MCHdArt::WaitForHeldRun();
    MCHdArt::HoldTakenRuns(false);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::ReaderReads(), reads + 1);
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + archive.Frames());
    archive.CheckFound();
    GHdArt = previous;
}

namespace
{
    /// <summary>
    /// Port-only (MCHdArt::StartHold): a stream of a HoldPack's archives: its kind, and the bytes no frame holds after its
    /// frame (an entry of their own) to make it larger.
    /// </summary>
    struct HoldPiece
    {
        MCFastFile::StreamKind Kind = MCFastFile::StreamKind::Packet;
        size_t FillerBytes = 0;
    };

    /// <summary>
    /// Port-only (MCHdArt::StartHold): a pack of two version 2 archives of the same streams (HoldPiece's, each a shape
    /// table's frame 0 of RunArchive::Side and its filler): the shapes' (shapes.hda), whose frames FindShape draws, and
    /// another (MoreFolder's), opened apart.
    /// </summary>
    class HoldPack
    {
    public:
        static constexpr std::string_view MoreFolder = "more";

        HoldPack(std::string_view name, std::span<const HoldPiece> pieces) : _Scratch(name)
        {
            constexpr uint8_t TableByte = 0x6b;
            std::vector<Version2Stream> streams;

            for (const HoldPiece& piece : pieces)
            {
                const auto number = static_cast<uint8_t>(_Tables.size());
                _Tables.push_back({number, TableByte});
                _Files.push_back(FrameFile(RunArchive::Side, RunArchive::Side, number + 1));
                const std::filesystem::path folder = MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(_Tables.back()));
                Version2Stream& stream = streams.emplace_back(Version2Stream{{}, piece.Kind});
                stream.Entries.push_back(Raw(MCFileSystem::ToUtf8(folder.filename()) + "\\0.hdf", _Files.back()));

                if (piece.FillerBytes > 0)
                {
                    stream.Entries.push_back(
                        Raw(std::format("filler{}.bin", number), std::vector<uint8_t>(piece.FillerBytes)));
                }

                _Kinds.push_back(piece.Kind);
                _Stored.push_back(_Files.back().size() + piece.FillerBytes);
            }

            const std::vector<uint8_t> archive = BuildVersion2Streams(streams);
            _Shapes = _Scratch.Path() / std::filesystem::path(ShapesFolder()).concat(".hda");
            WriteBytes(_Shapes, archive);
            WriteBytes(_Scratch.Path() / std::filesystem::path(MoreFolder).concat(".hda"), archive);
            MCHdArt::SetPackFolder(_Scratch.Path());
        }

        ~HoldPack() { MCHdArt::SetPackFolder({}); }
        HoldPack(const HoldPack&) = delete;
        HoldPack& operator=(const HoldPack&) = delete;

        /// <summary>The pack's folder of shapes' frames, which shapes.hda stands for.</summary>
        static std::filesystem::path ShapesFolder() { return MCHdArt::ShapeTableFolder(0).parent_path(); }

        /// <summary>Opens the shapes' archive (or the other), as a mission's load does, reading none of its streams.</summary>
        void Open(bool more = false) const
        {
            MCHdArt::ArchiveFacts(more ? std::filesystem::path(MoreFolder) : ShapesFolder(), 0);
        }

        /// <summary>Whether the hold keeps stream <paramref name="stream"/>: a packet's or the heads'.</summary>
        bool Held(size_t stream) const
        {
            return _Kinds[stream] == MCFastFile::StreamKind::Packet || _Kinds[stream] == MCFastFile::StreamKind::Heads;
        }

        /// <summary>The stored bytes the hold keeps of one of the archives: its packets' and heads' streams'.</summary>
        size_t HeldBytes() const
        {
            size_t bytes = 0;

            for (size_t stream = 0; stream < _Stored.size(); ++stream)
            {
                bytes += Held(stream) ? _Stored[stream] : 0;
            }

            return bytes;
        }

        /// <summary>Has the readers read stream <paramref name="stream"/>'s frame as <paramref name="as"/>.</summary>
        void Ask(size_t stream, MCHdArt::ReadAs as) const
        {
            const MCHdArt::ShapeAhead shape{_Tables[stream], 0, RunArchive::Side, RunArchive::Side};
            MCHdArt::ReadShapesAhead(std::span(&shape, 1), as);
        }

        /// <summary>
        /// Checks each stream's frame is drawn, as its own file decodes, where the hold keeps it (or the readers read it
        /// ahead), and none where it doesn't: its file cut (CutShapes), they can't be read.
        /// </summary>
        void CheckDrawnFromTheHold() const
        {
            for (size_t stream = 0; stream < _Tables.size(); ++stream)
            {
                MCTest::Scope scope(std::format("stream {}", stream));
                const std::shared_ptr<const MCHdArt::Frame> found =
                    MCHdArt::FindShape(MCHdArt::ShapeTableHash(_Tables[stream]), 0, RunArchive::Side, RunArchive::Side);

                if (!Held(stream))
                {
                    CHECK(found == nullptr);
                    continue;
                }

                REQUIRE(found != nullptr);
                const auto decoded = MCHdArt::DecodeHdf(_Files[stream], RunArchive::Side, RunArchive::Side);
                REQUIRE(decoded.has_value());
                CHECK(SameFrame(*found, *decoded));
            }
        }

        /// <summary>Cuts the shapes' archive's file to nothing (open, it stays open), so no stream of it can be read.</summary>
        void CutShapes() const { std::filesystem::resize_file(_Shapes, 0); }

        /// <summary>Stream <paramref name="stream"/>'s frame asked for ahead, as <paramref name="as"/>.</summary>
        void AskStream(size_t stream, MCHdArt::ReadAs as) const
        {
            const MCHdArt::ShapeAhead shape{_Tables[stream], 0, RunArchive::Side, RunArchive::Side};
            MCHdArt::ReadShapesAhead(std::span(&shape, 1), as);
        }

        /// <summary>Stream <paramref name="stream"/>'s frame as drawn.</summary>
        std::shared_ptr<const MCHdArt::Frame> FindStream(size_t stream) const
        {
            return MCHdArt::FindShape(MCHdArt::ShapeTableHash(_Tables[stream]), 0, RunArchive::Side, RunArchive::Side);
        }

        /// <summary>Port-only (MCHdArt::FillGpu): each stream's folder as a packet, every frame its archive holds.</summary>
        std::vector<MCHdArt::PacketAhead> Packets() const
        {
            std::vector<MCHdArt::PacketAhead> packets;

            for (const std::vector<uint8_t>& table : _Tables)
            {
                packets.push_back({MCHdArt::ShapeTableFolder(MCHdArt::ShapeTableHash(table)), std::nullopt, false, 0});
            }

            return packets;
        }

        /// <summary>
        /// Port-only (MCHdArt::FillGpu): checks each stream's frame went up through the slots (<paramref name="up"/>):
        /// its colours there as its file decodes them, its indices beside them, and the cache's record, which a draw
        /// finds, keeping no colours.
        /// </summary>
        void CheckStaged(std::span<const MCHdArt::StagedPicture> up) const
        {
            REQUIRE_EQ(up.size(), _Tables.size());

            for (size_t stream = 0; stream < _Tables.size(); ++stream)
            {
                MCTest::Scope scope(std::format("stream {}", stream));
                const std::shared_ptr<const MCHdArt::Frame> found = FindStream(stream);
                REQUIRE(found != nullptr);
                CHECK(found->Colors.empty());
                const auto picture = std::ranges::find_if(up, [&found](const MCHdArt::StagedPicture& staged)
                                                          { return staged.Record->Id == found->Id; });
                REQUIRE(picture != up.end());
                const auto decoded = MCHdArt::DecodeHdf(_Files[stream], RunArchive::Side, RunArchive::Side);
                REQUIRE(decoded.has_value());
                const std::span<const uint8_t> colors = picture->Lease->At(picture->ColorsAt, picture->ColorBytes);
                CHECK(std::ranges::equal(colors, decoded->Colors));
                CHECK_EQ(picture->IndexBytes, decoded->Indices.size());
            }
        }

        const std::filesystem::path& Folder() const { return _Scratch.Path(); }

    private:
        ScratchFolder _Scratch;
        std::filesystem::path _Shapes;
        std::vector<std::vector<uint8_t>> _Tables;
        std::vector<std::vector<uint8_t>> _Files;
        std::vector<MCFastFile::StreamKind> _Kinds;
        std::vector<size_t> _Stored;
    };

    /// <summary>The hold started (MCHdArt::StartHold) and done reading.</summary>
    void HoldAndWait()
    {
        MCHdArt::StartHold();
        MCHdArt::WaitForHold();
    }

    using StreamKind = MCFastFile::StreamKind;
} // namespace

/// <summary>
/// Port-only (SPEC-pack-v2.md's Memory: the hold): once a mission's load is done (MCHdArt::StartHold), the readers read
/// every version 2 archive open, and one opened later, and keep its packets' and heads' streams as stored, the tiles' and
/// pictures' not: two reads here, the tiles and pictures between the streams cutting the archive's run. Its file cut to
/// nothing after, the frames of the streams held are drawn as they were made, and one the readers read ahead is read from
/// memory (no read of theirs); those of the streams not held can't be read.
/// </summary>
TEST_CASE("hd art: the hold keeps a mission's packet and heads streams as stored, and their frames need no read")
{
    constexpr size_t AheadStream = 1;
    constexpr size_t HoldRuns = 2;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet},   {StreamKind::Packet, 1000},
                                        {StreamKind::Tiles}, {StreamKind::Pictures}, {StreamKind::Packet}};

    for (const bool openedLater : {false, true})
    {
        MCTest::Scope scope(openedLater ? "an archive opened after the load" : "an archive open as the load ends");
        const HoldPack pack("hold", pieces);
        const size_t holdReads = MCHdArt::HeldReads();
        const size_t reads = MCHdArt::ReaderReads();
        const size_t ahead = MCHdArt::FramesReadAhead();

        if (!openedLater)
        {
            pack.Open();
        }

        MCHdArt::StartHold();

        if (openedLater)
        {
            pack.Open();
        }

        MCHdArt::WaitForHold();
        CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
        CHECK_EQ(MCHdArt::HeldReads(), holdReads + HoldRuns);
        pack.CutShapes();
        pack.Ask(AheadStream, MCHdArt::ReadAs::Set);
        MCHdArt::WaitForReadAhead();
        MCHdArt::KeepReadAhead();
        CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 1);
        CHECK_EQ(MCHdArt::ReaderReads(), reads);
        pack.CheckDrawnFromTheHold();
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's fill): the plan decides how much the hold keeps (MCHdArt::HoldRoom): every byte that fits
/// the HD art's room less the working set's, so an 8 GB machine holds a mission whole and one whose room the game itself
/// takes holds none (whatever card was read last); the command line's <c>-hdhold off</c> holds nothing and
/// <c>-hdhold on</c> (the tests' and benchmarks') every byte whatever the room, and a value it can't take leaves it as
/// it was.
/// </summary>
TEST_CASE("hd art: the hold keeps what fits the plan's room, nothing with -hdhold off, everything with -hdhold on")
{
    constexpr uint64_t EightGb = uint64_t{8} << 30;
    constexpr uint64_t ThirtyTwoGb = uint64_t{32} << 30;
    // Half of it is the game itself, so the room left for the hold is none, on a card of its own or sharing the memory.
    constexpr uint64_t NoRoom = 2 * MCMemoryRoom::GameItselfBytes;
    const ReaderScope readers({.Readers = 1});
    const auto system = std::make_shared<MCFakeMemorySystem>();
    const MCFakeMemoryScope memory(system);
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Packet}};
    const HoldPack pack("hold-memory", pieces);
    pack.Open();

    struct Case
    {
        std::string_view Name;
        uint64_t Total = 0;
        std::string_view Line;
        bool Wanted = false;
        bool Held = false;
    };

    for (const Case& test :
         {Case{"8 GB", EightGb, {}, true, true}, Case{"a room the game itself takes", NoRoom, {}, true, false},
          Case{"32 GB with -hdhold off", ThirtyTwoGb, "-hdhold off", false, false},
          Case{"no room with -hdhold on", NoRoom, "-hdhold ON", true, true}})
    {
        MCTest::Scope scope{std::string(test.Name)};
        system->Total = test.Total;
        MCHdArt::SetReaderSettings({.Readers = 1});

        if (!test.Line.empty())
        {
            ParseCommandLine(test.Line);
        }

        CHECK_EQ(MCHdArt::HoldWanted(), test.Wanted);
        CHECK_EQ(MCHdArt::HoldRoom().has_value(), test.Line != "-hdhold ON");
        HoldAndWait();
        CHECK_EQ(MCHdArt::HeldBytes(), test.Held ? pack.HeldBytes() : 0);
        MCHdArt::LetGoHold();
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
    }

    ParseCommandLine("-hdhold off -hdhold maybe");
    CHECK(MCHdArt::GetReaderSettings().Hold == std::optional<bool>(false));
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-pack-v2.md's Memory): the hold is let go of whole when memory goes short (MCHdArt::NextFrame), and
/// holds nothing more that mission (an archive opened after isn't held) until the next mission's starts it again, its
/// fill stopped first (a run being read keeps nothing); at a mission's end (MCHdArt::LetGoHold, MCMission::EndScenario's);
/// and when the pack is set.
/// </summary>
TEST_CASE("hd art: the hold is let go of when memory goes short, at a mission's end and when the pack is set")
{
    constexpr size_t Archives = 2;
    const ReaderScope readers({.Readers = 1, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("hold-let-go", pieces);
    pack.Open();
    HoldAndWait();
    REQUIRE_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());

    {
        MCTest::Scope scope("memory short");
        MCHdArt::NextFrame(false);
        CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
        MCHdArt::NextFrame(true);
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
        pack.Open(true);
        MCHdArt::WaitForHold();
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
        HoldAndWait();
        CHECK_EQ(MCHdArt::HeldBytes(), Archives * pack.HeldBytes());
    }

    {
        MCTest::Scope scope("a mission's end");
        MCHdArt::LetGoHold();
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
        HoldAndWait();
        CHECK_EQ(MCHdArt::HeldBytes(), Archives * pack.HeldBytes());
    }

    {
        // SPEC-memory.md's first step when memory is short: the fill stops, even with nothing held yet to give back,
        // and the run a reader is reading keeps nothing.
        MCTest::Scope scope("memory short while the fill reads");
        MCHdArt::LetGoHold();
        MCHdArt::HoldTakenRuns(true);
        MCHdArt::StartHold();
        MCHdArt::WaitForHeldRun();
        MCHdArt::NextFrame(true);
        MCHdArt::HoldTakenRuns(false);
        MCHdArt::WaitForHold();
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
    }

    {
        MCTest::Scope scope("the pack set");
        MCHdArt::SetPackFolder(pack.Folder());
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's fill): the hold starts with a mission's load (MCHdArt::StartLoadFill), holding every
/// archive the load opens, and the load ends once it has read them (MCHdArt::FinishLoadFill, the bar shown as it
/// waits and once more, whole): the bar's last LoadFillBarPercent is the hold's share read, none before the load hands
/// it anything, and the bar never goes back as the load hands it more; the bar as the load says once it's done.
/// </summary>
TEST_CASE("hd art: the load's fill holds what the load opens before the load ends, the bar's last quarter the hold's")
{
    constexpr int32_t Part = 40;
    constexpr int32_t Whole = 100;
    constexpr int32_t Steps = Whole - MCHdArt::LoadFillBarPercent;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet, 1000}, {StreamKind::Tiles}};
    const HoldPack pack("load-fill", pieces);
    CHECK_EQ(MCHdArt::LoadBarPercent(Part), Part);

    MCHdArt::StartLoadFill();
    CHECK_EQ(MCHdArt::LoadFillShare(), 0.0);
    CHECK_EQ(MCHdArt::LoadBarPercent(0), 0);
    CHECK_EQ(MCHdArt::LoadBarPercent(Part), Part * Steps / Whole);
    CHECK_EQ(MCHdArt::LoadBarPercent(Whole), Steps);
    pack.Open();
    // The load's steps done and its archives handed, their share not all read: the bar stays where it was.
    CHECK(MCHdArt::LoadBarPercent(Part) >= Steps);
    int32_t shown = 0;
    MCHdArt::FinishLoadFill([&shown] { ++shown; });
    CHECK(shown >= 1);
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    CHECK_EQ(MCHdArt::LoadFillShare(), 1.0);
    CHECK_EQ(MCHdArt::LoadBarPercent(Part), Part);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

namespace
{
    /// <summary>Port-only (MCHdArt::FillGpu): upload slots over memory of the test's, whose copies are done at once.</summary>
    class FillSlotsBacking final : public MCUploadSlots::MCBacking
    {
    public:
        explicit FillSlotsBacking(size_t slots) : _Memory(slots * MCUploadSlots::SlotBytes) {}

        std::span<uint8_t> Memory() override { return _Memory; }
        SDL_GPUTransferBuffer* Buffer() override { return nullptr; }
        bool Done(SDL_GPUFence*) override { return true; }
        bool Wait(SDL_GPUFence*) override { return true; }
        void Release(SDL_GPUFence*) override {}
        void Flush() override {}

    private:
        std::vector<uint8_t> _Memory;
    };

    /// <summary>Port-only: the fill's slots for a scope, as the renderer sets its own (MCHdArt::SetUploadSlots).</summary>
    class FillSlotsScope
    {
    public:
        static constexpr size_t Slots = 64;

        FillSlotsScope() : _Slots(MCUploadSlots::MakeWith(std::make_unique<FillSlotsBacking>(Slots)))
        {
            MCHdArt::SetUploadSlots(_Slots, true);
        }

        ~FillSlotsScope() { MCHdArt::SetUploadSlots(nullptr, false); }
        FillSlotsScope(const FillSlotsScope&) = delete;
        FillSlotsScope& operator=(const FillSlotsScope&) = delete;

        MCUploadSlots& Pool() const { return *_Slots; }

    private:
        std::shared_ptr<MCUploadSlots> _Slots;
    };

    /// <summary>
    /// Port-only: the renderer's part of a frame for the fill: the pictures the readers put in the slots taken, all of
    /// them, their copies recorded, added to <paramref name="up"/>.
    /// </summary>
    void TakeEveryPicture(std::vector<MCHdArt::StagedPicture>& up)
    {
        for (MCHdArt::StagedPicture& picture :
             MCHdArt::TakeStagedPictures(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()))
        {
            MCHdArt::ColorsUploaded(*picture.Record);
            up.push_back(std::move(picture));
        }
    }

    /// <summary>
    /// Port-only: a load's end (MCHdArt::FinishLoadFill), the renderer's part done as each frame of the bar is shown
    /// (TakeEveryPicture): the pictures taken.
    /// </summary>
    std::vector<MCHdArt::StagedPicture> FinishTakingEveryPicture()
    {
        std::vector<MCHdArt::StagedPicture> up;
        MCHdArt::FinishLoadFill([&up] { TakeEveryPicture(up); });
        return up;
    }

    /// <summary>
    /// Port-only: play's frames (or a briefing's) for the fill, at most <paramref name="most"/>, until
    /// <paramref name="until"/> pictures are up: each after the readers' work, the fill's too (MCHdArt::WaitForGpuReads),
    /// its pictures taken (TakeEveryPicture).
    /// </summary>
    void FramesUntilUp(uint64_t until, size_t most, std::vector<MCHdArt::StagedPicture>& up)
    {
        for (size_t frame = 0; frame < most && MCHdArt::GpuFillNow().Up < until; ++frame)
        {
            MCHdArt::WaitForReadAhead();
            MCHdArt::WaitForHold();
            MCHdArt::NextFrame(false);
            MCHdArt::WaitForReadAhead();
            MCHdArt::WaitForGpuReads();
            TakeEveryPicture(up);
        }
    }

    /// <summary>Port-only: the folders of <paramref name="pack"/>'s packets, as a mission's list names them.</summary>
    std::vector<std::filesystem::path> PacketFolders(const HoldPack& pack)
    {
        std::vector<std::filesystem::path> folders;

        for (const MCHdArt::PacketAhead& packet : pack.Packets())
        {
            folders.push_back(packet.Folder);
        }

        return folders;
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "The GPU from the briefing"): a type's archive in <paramref name="pack"/> at
    /// <paramref name="type"/> (<c>objects/shapes90/7</c>), a packet's frame 0 in each of its streams, and a row for the
    /// archive's own folder, as the pack's archives have.
    /// </summary>
    void WriteTypeArchive(const ScratchFolder& pack, std::string_view type, std::span<const int32_t> packets)
    {
        std::vector<Version2Stream> streams;

        for (const int32_t packet : packets)
        {
            Version2Stream& stream = streams.emplace_back();
            stream.Entries.push_back(
                Raw(std::format("{}\\0.hdf", packet), FrameFile(RunArchive::Side, RunArchive::Side, packet)));
        }

        const std::filesystem::path archive = pack.Path() / std::filesystem::path(type).concat(".hda");
        std::filesystem::create_directories(archive.parent_path());
        WriteBytes(archive, BuildVersion2Streams(streams, {{std::string(), {}}}));
    }

    /// <summary>Port-only: a load's fill of <paramref name="pack"/>, to its end (FinishTakingEveryPicture).</summary>
    std::vector<MCHdArt::StagedPicture> FillUnderTheBar(const HoldPack& pack)
    {
        MCHdArt::StartLoadFill();
        pack.Open();
        MCHdArt::FillGpu(pack.Packets());
        return FinishTakingEveryPicture();
    }
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2: the whole mission on the GPU, if it fits): under the loading bar the readers
/// unpack the mission's frames from the hold and put their pictures in the upload slots, which the renderer copies up as
/// the bar is shown; the bar ends once they're all up. Each picture's bytes are its file's, and the cache's record, which
/// a draw finds with the archive's file gone, keeps no colours: nothing is read again for them.
/// </summary>
TEST_CASE("hd art: a mission that fits goes on the GPU under the loading bar, through the upload slots")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}, {StreamKind::Packet, 1000}};
    const HoldPack pack("gpu-fill", pieces);
    const FillSlotsScope slots;
    const size_t again = MCHdArt::ColorsReadAgain();
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    const std::vector<MCHdArt::StagedPicture> up = FillUnderTheBar(pack);
    const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
    CHECK(fill.Fits);
    CHECK(fill.MissionBytes > 0);
    CHECK_EQ(fill.Up, before + pieces.size());
    CHECK(!fill.Reading);
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    pack.CutShapes();
    pack.CheckStaged(up);
    CHECK_EQ(MCHdArt::ColorsReadAgain(), again);
    MCHdArt::LetGoHold();
    CHECK(!MCHdArt::GpuFillNow().Fits);
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2): a picture the fill put up and the GPU let go of (past its room) before its
/// frame was drawn held the frame's only colours: the frame goes from the cache whole, so asked for ahead it's read again
/// by the readers, and its first draw finds its colours, reading nothing on the game's thread (19 Nov: mission 28, its
/// mission past a 4 GiB room, read 379 mechs' frames' colours again so).
/// </summary>
TEST_CASE("hd art: a frame whose fill picture the GPU lets go before it's drawn is read ahead again")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-dropped", pieces);
    const FillSlotsScope slots;
    const std::vector<MCHdArt::StagedPicture> up = FillUnderTheBar(pack);
    REQUIRE_EQ(up.size(), pieces.size());
    const size_t again = MCHdArt::ColorsReadAgain();

    // The GPU lets every picture go, no frame drawn yet; then each frame is asked for ahead, and drawn.
    for (const MCHdArt::StagedPicture& picture : up)
    {
        MCHdArt::PictureDropped(picture.Record->Id);
    }

    for (size_t stream = 0; stream < pieces.size(); ++stream)
    {
        pack.AskStream(stream, MCHdArt::ReadAs::Load);
    }

    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();

    for (size_t stream = 0; stream < pieces.size(); ++stream)
    {
        MCTest::Scope scope(std::format("stream {}", stream));
        const std::shared_ptr<const MCHdArt::Frame> found = pack.FindStream(stream);
        REQUIRE(found != nullptr);
        CHECK(!MCHdArt::ColorsOf(*found).empty());
    }

    CHECK_EQ(MCHdArt::ColorsReadAgain(), again);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's one copy of a frame's colours): a frame drawn, its colours let go once the GPU took its
/// picture, then the picture let go of (past the GPU's room: a 4 GB card panning away) goes from the cache whole, so asked
/// for ahead again (its appearance near the view) it's read by the readers, and its next draw reads nothing on the game's
/// thread. Kept as a record without colours, that draw read them there.
/// </summary>
TEST_CASE("hd art: a drawn frame whose picture the GPU lets go is read ahead again")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("drawn-dropped", pieces);
    pack.Open();

    // Each frame asked for ahead, then drawn, its colours there.
    const auto askAndDraw = [&pack, &pieces]
    {
        for (size_t stream = 0; stream < pieces.size(); ++stream)
        {
            pack.AskStream(stream, MCHdArt::ReadAs::Load);
        }

        MCHdArt::WaitForLoadReads();
        MCHdArt::KeepReadAhead();
        std::vector<std::shared_ptr<const MCHdArt::Frame>> drawn;

        for (size_t stream = 0; stream < pieces.size(); ++stream)
        {
            MCTest::Scope scope(std::format("stream {}", stream));
            std::shared_ptr<const MCHdArt::Frame> found = pack.FindStream(stream);
            REQUIRE(found != nullptr);
            CHECK(!MCHdArt::ColorsOf(*found).empty());
            drawn.push_back(std::move(found));
        }

        return drawn;
    };

    const size_t again = MCHdArt::ColorsReadAgain();
    const size_t ahead = MCHdArt::FramesReadAhead();

    const std::vector<std::shared_ptr<const MCHdArt::Frame>> drawn = askAndDraw();

    for (const std::shared_ptr<const MCHdArt::Frame>& frame : drawn)
    {
        MCHdArt::ColorsUploaded(*frame);
        MCHdArt::PictureDropped(frame->Id);
    }

    const std::vector<std::shared_ptr<const MCHdArt::Frame>> drawnAgain = askAndDraw();
    REQUIRE_EQ(drawnAgain.size(), drawn.size());

    for (size_t stream = 0; stream < drawn.size(); ++stream)
    {
        CHECK(drawnAgain[stream] != drawn[stream]);
        CHECK_EQ(drawnAgain[stream]->Id, drawn[stream]->Id);
    }

    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 2 * pieces.size());
    CHECK_EQ(MCHdArt::ColorsReadAgain(), again);
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, items 2 and 3): a mission that doesn't fit on the GPU (a room the game itself takes)
/// isn't put there: the bar ends once the hold has it, and its frames are drawn from the hold's memory as they were.
/// </summary>
TEST_CASE("hd art: a mission that doesn't fit on the GPU stays in the hold, its frames drawn from memory")
{
    constexpr uint64_t NoRoom = 2 * MCMemoryRoom::GameItselfBytes;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const auto system = std::make_shared<MCFakeMemorySystem>();
    const MCFakeMemoryScope memory(system);
    system->Total = NoRoom;
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-short", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    const std::vector<MCHdArt::StagedPicture> up = FillUnderTheBar(pack);
    CHECK(up.empty());
    CHECK(!MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before);
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    pack.CutShapes();
    pack.CheckDrawnFromTheHold();
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2): whether the whole mission fits on the GPU is decided once the archives
/// being opened for the hold are open, on all of them: a mission whose first archive fits on the card and whose whole
/// doesn't puts none of it up (19 Nov: mission 28 entered the fill on its first archive's 0.8 MB, then left it past a
/// 4 GiB room, the GPU letting go of what it had put up).
/// </summary>
TEST_CASE("hd art: a mission that doesn't fit whole puts none of it on the GPU, though its first archive would fit")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-whole", pieces);
    const FillSlotsScope slots;
    const std::array<std::filesystem::path, 1> more{std::filesystem::path(HoldPack::MoreFolder)};
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());

    // The other archive, as big, waits to be opened: the room fits the first with more than a tenth to spare.
    MCHdArt::HoldOpens(true);
    MCHdArt::OpenArchivesAhead(more);
    MCHdArt::NextFrame(false);
    const uint64_t first = MCHdArt::GpuFillNow().MissionBytes;
    REQUIRE(first > 0);
    MCHdArt::NoteGpuRoom(first + first / 2);
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    MCHdArt::HoldOpens(false);
    MCHdArt::WaitForReadAhead();
    std::vector<MCHdArt::StagedPicture> up;
    MCHdArt::FinishLoadFill(
        [&up]
        {
            for (MCHdArt::StagedPicture& picture :
                 MCHdArt::TakeStagedPictures(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()))
            {
                up.push_back(std::move(picture));
            }
        });
    const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
    CHECK(up.empty());
    CHECK(!fill.Fits);
    CHECK_EQ(fill.MissionBytes, 2 * first);
    CHECK_EQ(fill.Up, before);
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing": the opening): a mission that doesn't fit on the GPU whole puts
/// its opening up first, in its order, as far as the opening's room (a quarter of the pictures' room) takes it; given room
/// for all of it, the rest goes up after; left again, the opening is walked again within its room, nothing up read again
/// (21 Nov: on a card the mission didn't fit, none of it went up, and its first draws were read in play).
/// </summary>
TEST_CASE("hd art: a mission that doesn't fit whole puts its opening on the GPU first, as far as its room takes it")
{
    constexpr size_t Pieces = 16;
    constexpr size_t Frames = 16;
    constexpr std::string_view Mission = "mis0101";
    constexpr uint64_t NoRoom = 1;
    // The rooms, in tenths of the mission's bytes at the pages' share: one it doesn't fit (to enter or to stay), whose
    // quarter takes a few of its folders; one it fits whole.
    constexpr double ShortTenths = 9;
    constexpr double WholeTenths = 20;
    constexpr double Tenths = 10;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces(Pieces, HoldPiece{StreamKind::Packet});
    const HoldPack pack("gpu-opening", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    // The pack's folders backwards: the opening's order goes up, not the list's.
    std::vector<MCHdArt::OpeningPacket> opening;

    for (const std::filesystem::path& folder : PacketFolders(pack) | std::views::reverse)
    {
        opening.push_back({folder, std::nullopt});
    }

    std::vector<MCHdArt::StagedPicture> up;
    MCHdArt::NoteGpuRoom(NoRoom);
    MCHdArt::StartBriefingHold(Mission, PacketFolders(pack), false);
    MCHdArt::SetOpening(opening);
    FramesUntilUp(before + 1, Frames, up);
    const MCHdArt::GpuFill known = MCHdArt::GpuFillNow();
    REQUIRE(known.MissionBytes > 0);
    CHECK(!known.Fits);
    CHECK(up.empty());
    CHECK_EQ(known.OpeningAsked, 0);
    const auto room = [&known](double tenths)
    { return static_cast<uint64_t>(static_cast<double>(known.MissionBytes) * known.PagesShare * tenths / Tenths); };
    // Within the opening's room, the first of its folders up, in its order: the pack's last.
    const auto checkOpening = [&](uint64_t pictures)
    {
        const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
        CHECK(!fill.Fits);
        CHECK(fill.OpeningDone);
        REQUIRE(fill.OpeningAsked > 0);
        CHECK(fill.OpeningAsked < Pieces);
        CHECK(static_cast<uint64_t>(static_cast<double>(fill.OpeningBytes) * fill.PagesShare) <=
              std::min(MCHdArt::OpeningMostBytes, room(ShortTenths) / MCHdArt::OpeningRoomShare));
        CHECK_EQ(fill.Up, before + pictures);

        for (size_t at = 0; at < fill.OpeningAsked; ++at)
        {
            MCTest::Scope scope(std::format("its folder {}", at));
            const std::shared_ptr<const MCHdArt::Frame> found = pack.FindStream(Pieces - 1 - at);
            REQUIRE(found != nullptr);
            CHECK(std::ranges::any_of(up, [&found](const MCHdArt::StagedPicture& picture)
                                      { return picture.Record->Id == found->Id; }));
        }

        return fill.OpeningAsked;
    };

    {
        MCTest::Scope scope("a room it doesn't fit");
        MCHdArt::NoteGpuRoom(room(ShortTenths));
        FramesUntilUp(before + Pieces, Frames, up);
        CHECK_EQ(up.size(), checkOpening(up.size()));
    }

    {
        MCTest::Scope scope("a room it fits whole");
        MCHdArt::NoteGpuRoom(room(WholeTenths));
        FramesUntilUp(before + Pieces, Frames, up);
        const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
        CHECK(fill.Fits);
        CHECK(fill.OpeningDone);
        CHECK_EQ(fill.OpeningAsked, Pieces);
        CHECK_EQ(fill.Up, before + Pieces);
    }

    {
        MCTest::Scope scope("left again");
        MCHdArt::NoteGpuRoom(room(ShortTenths));
        FramesUntilUp(before + Pieces + 1, Frames, up);
        checkOpening(Pieces);
        CHECK_EQ(up.size(), Pieces);
    }

    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2): a load no briefing held for (a saved game's, a staged battle's) decides
/// whether the mission fits on the GPU only once it has made its units (MCHdArt::LoadUnitsMade): a force in no list
/// counts (20 Nov: the canyon benchmark's ten mechs, 6.2 GB of its 7.9, came after the fill had decided on 1.7).
/// </summary>
TEST_CASE("hd art: a load no briefing held for decides the fill on the GPU once its units are made")
{
    constexpr std::string_view Unlisted = "no-list";
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-units", pieces);
    const FillSlotsScope slots;
    REQUIRE(MCHdArt::BriefingFolders(Unlisted).empty());
    MCHdArt::StartLoadFill(Unlisted);
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::WaitForReadAhead();
    // The ground already up and in the room the card gives (MCHdArt::GroundOnGpu): the units are what's left.
    MCHdArt::GroundOnGpu();
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::NextFrame(false);
    CHECK(MCHdArt::GpuFillNow().MissionBytes > 0);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    MCHdArt::LoadUnitsMade();
    MCHdArt::NextFrame(false);
    CHECK(MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(FinishTakingEveryPicture().size(), pieces.size());
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2: "less only the plan's own error, which the ledger measures"): the fill
/// decides whether the mission fits once the load's ground is on the GPU and the card has been read again after it, and
/// counts the mission's frames at what the pages they share take (MCHdArt::GpuFill::PagesShare), not at their own bytes
/// (20 Nov, the A10's 4 GB card: pictures took 1.25-1.48 times their bytes; the fill decided on the bytes before a 2x
/// mission's 456 MB of ground went up, and 14 missions went up and came down again).
/// </summary>
TEST_CASE("hd art: the fill decides beside the load's ground, the mission's frames counted at what their pages take")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-ground", pieces);
    const FillSlotsScope slots;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::WaitForReadAhead();
    MCHdArt::NextFrame(false);
    const MCHdArt::GpuFill known = MCHdArt::GpuFillNow();
    REQUIRE(known.MissionBytes > 0);
    REQUIRE(known.PagesShare > 1.0);
    const auto pages = static_cast<uint64_t>(static_cast<double>(known.MissionBytes) * known.PagesShare);
    constexpr uint64_t PlentyTimes = 4;

    // Before the ground goes up, whatever the room.
    MCHdArt::NoteGpuRoom(PlentyTimes * pages);
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);

    // The ground up, the room noted before it: not yet.
    MCHdArt::GroundOnGpu();
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);

    // A room just what its pages take, short of their tenth to spare (its bytes alone fitted it with the tenth).
    MCHdArt::NoteGpuRoom(pages);
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);

    // A room its pages fit with a tenth to spare.
    MCHdArt::NoteGpuRoom(pages + pages / 5);
    MCHdArt::NextFrame(false);
    CHECK(MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(FinishTakingEveryPicture().size(), pieces.size());
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2): what the HD pictures' pages take is measured once the fill has put the
/// mission up whole, never part way, and kept for the next mission's (20 Nov, the A10's 4 GB card: measured part way it
/// moved with the part going up, 2x 19's 1.58 near its load's end against 1.29 whole, and three missions that fit left
/// the fill).
/// </summary>
TEST_CASE("hd art: the fill measures what its pictures' pages take once the mission is up whole, not part way")
{
    constexpr int64_t PagesBytes = int64_t{512} << 20;
    constexpr uint64_t PlentyTimes = 4;
    constexpr double ShareTolerance = 1e-9;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-share", pieces);
    const FillSlotsScope slots;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::WaitForReadAhead();
    MCHdArt::GroundOnGpu();
    MCHdArt::NextFrame(false);
    const MCHdArt::GpuFill known = MCHdArt::GpuFillNow();
    REQUIRE(known.MissionBytes > 0);

    // Part way, the pages part full: twice their pictures' bytes, past the bytes the share is measured from.
    MCMemoryLedger::MCShare pages{MCMemoryLedger::MCHolder::GpuHdPictures};
    pages.Set({.Payload = PagesBytes, .Allocated = 2 * PagesBytes});
    MCHdArt::NoteGpuRoom(PlentyTimes * known.MissionBytes);
    MCHdArt::NextFrame(false);
    REQUIRE(MCHdArt::GpuFillNow().Fits);
    MCHdArt::NextFrame(false);
    CHECK_EQ(MCHdArt::GpuFillNow().PagesShare, known.PagesShare);

    // Up whole: measured.
    CHECK_EQ(FinishTakingEveryPicture().size(), pieces.size());
    MCHdArt::NextFrame(false);
    const MCMemoryLedger::MCFigures whole = MCMemoryLedger::Of(MCMemoryLedger::MCHolder::GpuHdPictures);
    const double measured = static_cast<double>(whole.Allocated) / static_cast<double>(whole.Payload);
    CHECK(std::abs(MCHdArt::GpuFillNow().PagesShare - measured) < ShareTolerance);
    pages.Set({});
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2): a fill left once the mission no longer fits puts nothing more up, the
/// frames a reader had taken to read included (20 Nov, the A10's 4 GB card: 2x missions 22 and 24, left at the card's
/// reading after they entered, went up whole in play, 64 pictures a frame, the game's thread waiting for slots).
/// </summary>
TEST_CASE("hd art: a fill left puts nothing more up, the frames a reader had taken included")
{
    constexpr uint64_t PlentyTimes = 4;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-left", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::WaitForReadAhead();
    MCHdArt::GroundOnGpu();
    MCHdArt::NextFrame(false);
    const uint64_t mission = MCHdArt::GpuFillNow().MissionBytes;
    REQUIRE(mission > 0);

    // In, the readers holding the runs they take.
    MCHdArt::HoldTakenRuns(true);
    MCHdArt::NoteGpuRoom(PlentyTimes * mission);
    MCHdArt::NextFrame(false);
    REQUIRE(MCHdArt::GpuFillNow().Fits);
    MCHdArt::WaitForHeldRun();

    // Out before they're read.
    MCHdArt::NoteGpuRoom(0);
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    MCHdArt::HoldTakenRuns(false);
    MCHdArt::WaitForReadAhead();
    MCHdArt::NextFrame(false);
    CHECK(MCHdArt::TakeStagedPictures(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()).empty());
    CHECK_EQ(MCHdArt::GpuFillNow().Waiting, 0);
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before);
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's plan, item 2): a mission up whole stays in once the load's fill is done, whatever its
/// estimate (the bytes of its archives, some of which the GPU never takes): what's up is all of it (20 Nov, the A10's
/// 4 GB card: whole missions ended 66-175 MB of the estimate over what went up, and 2x 21 and full 1 were counted out in
/// play by 2-3 MB as the card filled).
/// </summary>
TEST_CASE("hd art: a mission up whole stays in once the load's fill is done, whatever its estimate")
{
    constexpr uint64_t PlentyTimes = 4;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-done", pieces);
    const FillSlotsScope slots;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::WaitForReadAhead();
    MCHdArt::GroundOnGpu();
    MCHdArt::NextFrame(false);
    const uint64_t mission = MCHdArt::GpuFillNow().MissionBytes;
    REQUIRE(mission > 0);
    MCHdArt::NoteGpuRoom(PlentyTimes * mission);
    MCHdArt::NextFrame(false);
    REQUIRE(MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(FinishTakingEveryPicture().size(), pieces.size());

    // Up whole, its pictures half the estimate: a room under the estimate but over them keeps it in.
    const auto half = static_cast<int64_t>(mission / 2);
    MCMemoryLedger::MCShare up{MCMemoryLedger::MCHolder::GpuHdPictures};
    up.Set({.Payload = half, .Allocated = half});
    MCHdArt::NoteGpuRoom(mission * 3 / 4);
    MCHdArt::NextFrame(false);
    CHECK(MCHdArt::GpuFillNow().Fits);

    // A room under what's up: out.
    MCHdArt::NoteGpuRoom(mission / 4);
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    up.Set({});
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's upload slots: a fill reader waits for its room before it takes any read): with the fill's
/// share of the slots held (as pictures staged and not yet recorded hold it), its reader waits and the bar is shown
/// meanwhile, the game's thread never waiting; once the share is given back the mission goes up whole.
/// </summary>
TEST_CASE("hd art: the fill waits for room in the upload slots, the bar shown meanwhile, then goes up whole")
{
    constexpr size_t ShowsHeld = 3;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-room", pieces);
    const FillSlotsScope slots;
    MCUploadSlots& pool = slots.Pool();
    const int64_t waits = pool.Tally().Of(MCUploadSlots::MCKind::Fill).Waits;
    std::optional<MCUploadSlots::MCLease> share =
        pool.TryTake(MCUploadSlots::MCKind::Fill, pool.MostFor(MCUploadSlots::MCKind::Fill) * MCUploadSlots::SlotBytes);
    REQUIRE(share.has_value());
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    size_t shows = 0;
    uint64_t upWhileHeld = 0;
    std::vector<MCHdArt::StagedPicture> up;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::FinishLoadFill(
        [&]
        {
            if (++shows == ShowsHeld)
            {
                upWhileHeld = MCHdArt::GpuFillNow().Up - before;
                share.reset();
            }

            for (MCHdArt::StagedPicture& picture :
                 MCHdArt::TakeStagedPictures(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()))
            {
                MCHdArt::ColorsUploaded(*picture.Record);
                up.push_back(std::move(picture));
            }
        });
    CHECK(shows > ShowsHeld);
    CHECK_EQ(upWhileHeld, 0);
    CHECK(pool.Tally().Of(MCUploadSlots::MCKind::Fill).Waits > waits);
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before + pieces.size());
    pack.CutShapes();
    pack.CheckStaged(up);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's upload slots: who waits): with the fill's readers waiting for room in the slots (its share
/// held, as pictures staged and not yet recorded hold it, which only the game's thread frees), an archive the load asks
/// to open is opened all the same: other work ends a fill reader's wait, so the game's thread never waits on the readers
/// for ever (19 Nov: the canyon's load hung so, every reader waiting for room and the game's thread for an open).
/// </summary>
TEST_CASE("hd art: an archive asked for while the fill's readers wait for room is opened")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("gpu-fill-open", pieces);
    const FillSlotsScope slots;
    MCUploadSlots& pool = slots.Pool();
    const int64_t waits = pool.Tally().Of(MCUploadSlots::MCKind::Fill).Waits;
    std::optional<MCUploadSlots::MCLease> share =
        pool.TryTake(MCUploadSlots::MCKind::Fill, pool.MostFor(MCUploadSlots::MCKind::Fill) * MCUploadSlots::SlotBytes);
    REQUIRE(share.has_value());
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    const std::array<std::filesystem::path, 1> more{std::filesystem::path(HoldPack::MoreFolder) / "0"};
    bool openedWhileHeld = false;
    uint64_t upWhileHeld = 0;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::FinishLoadFill(
        [&]
        {
            // Once a fill reader has waited for room: the open asked for, and waited for as the load waits for its own.
            if (share.has_value() && pool.Tally().Of(MCUploadSlots::MCKind::Fill).Waits > waits)
            {
                MCHdArt::OpenArchivesAhead(more);
                MCHdArt::WaitForLoadReads();
                openedWhileHeld = true;
                upWhileHeld = MCHdArt::GpuFillNow().Up - before;
                share.reset();
            }

            for (MCHdArt::StagedPicture& picture :
                 MCHdArt::TakeStagedPictures(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()))
            {
                MCHdArt::ColorsUploaded(*picture.Record);
            }
        });
    CHECK(openedWhileHeld);
    CHECK_EQ(upWhileHeld, 0);
    CHECK(MCHdArt::GpuFillNow().Up >= before + pieces.size());
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "From the briefing"): the hold starts at a mission's briefing
/// (MCHdArt::StartBriefingHold), reading the archives of the folders it's given in runs of MCHdArt::BriefingRunBytes
/// (here two, where the load reads the same archive in one); the mission's load keeps it (MCHdArt::HoldsFor, as
/// MCMission::StartScenario asks), its bar starting from what the briefing read, and notes the folders it hands the hold
/// (MCHdArt::LoadFolders); another mission's briefing lets go of it first.
/// </summary>
TEST_CASE("hd art: a briefing holds its mission's art in short runs, and the mission's load keeps it")
{
    constexpr size_t Filler = 600'000;
    constexpr size_t BriefingRuns = 2;
    constexpr size_t LoadRuns = 1;
    constexpr std::string_view Mission = "mis0101";
    constexpr std::string_view Other = "mis0102";
    static_assert(Filler < MCHdArt::BriefingRunBytes && 2 * Filler > MCHdArt::BriefingRunBytes &&
                  2 * Filler < MCHdArt::HoldRunBytes);
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{
        {StreamKind::Packet, Filler}, {StreamKind::Packet, Filler}, {StreamKind::Heads}};
    const HoldPack pack("briefing-hold", pieces);
    const std::array<std::filesystem::path, 1> shapes{HoldPack::ShapesFolder()};
    const std::array<std::filesystem::path, 1> more{std::filesystem::path(HoldPack::MoreFolder)};
    CHECK(!MCHdArt::HoldsFor(Mission));
    const size_t reads = MCHdArt::HeldReads();

    {
        MCTest::Scope scope("the briefing");
        MCHdArt::StartBriefingHold(Mission, shapes, false);
        // Its archive opened by the readers, then held.
        MCHdArt::WaitForReadAhead();
        MCHdArt::WaitForHold();
        CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
        CHECK_EQ(MCHdArt::HeldReads(), reads + BriefingRuns);
        CHECK(MCHdArt::HoldsFor(Mission));
        CHECK(MCHdArt::HoldsFor("MIS0101"));
        CHECK(!MCHdArt::HoldsFor(Other));
    }

    {
        MCTest::Scope scope("its load");
        MCHdArt::StartLoadFill();
        CHECK(MCHdArt::HoldsFor(Mission));
        CHECK_EQ(MCHdArt::LoadFillShare(), 1.0);
        MCHdArt::OpenArchivesAhead(more);
        // As the load waits for its reads ahead (the opens among them) before its fill.
        MCHdArt::WaitForReadAhead();
        MCHdArt::FinishLoadFill([] {});
        CHECK_EQ(MCHdArt::HeldBytes(), 2 * pack.HeldBytes());
        CHECK_EQ(MCHdArt::HeldReads(), reads + BriefingRuns + LoadRuns);
        const std::vector<std::filesystem::path> folders = MCHdArt::LoadFolders();
        REQUIRE_EQ(folders.size(), 1u);
        CHECK_EQ(folders[0], more[0]);
    }

    {
        MCTest::Scope scope("another mission's briefing");
        MCHdArt::StartBriefingHold(Other, {}, false);
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
        CHECK(MCHdArt::HoldsFor(Other));
        CHECK(!MCHdArt::HoldsFor(Mission));
        MCHdArt::LetGoHold();
        CHECK(!MCHdArt::HoldsFor(Other));
    }

    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's ground by name): a load notes the ground's frames it asks for among the folders it hands
/// the hold (MCHdArt::LoadFolders: <c>terrain/&lt;hash&gt;</c>, <c>overlays/&lt;hash&gt;</c>), and a briefing given them
/// holds those frames' streams of the ground's archives alone (MCHdArt::StartBriefingHold), each tile's relief map beside
/// it with relief maps: never the rest of an archive that keeps every map's ground, nor a stream twice.
/// </summary>
TEST_CASE("hd art: a briefing holds the ground its list names, only those frames of the ground's archives")
{
    constexpr std::string_view Mission = "mis0101";
    constexpr std::string_view ReliefFolder = "relief";
    constexpr int32_t Side = RunArchive::Side;
    // Each stream a frame and a filler past the gap a run reads across, so no run takes a stream not asked for.
    constexpr size_t Filler = MCHdArt::ReaderRunGap + 1;
    constexpr std::array<uint64_t, 4> Keys{0x1111, 0x2222, 0x3333, 0x4444};
    constexpr size_t TerrainRuns = 2;
    constexpr size_t ReliefRuns = 2;
    constexpr size_t OverlayRuns = 1;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const ScratchFolder pack("ground-hold");
    const auto name = [](uint64_t key) { return std::format("{:016x}", key); };
    std::vector<size_t> stored;
    std::vector<Version2Stream> streams;

    for (size_t at = 0; at < Keys.size(); ++at)
    {
        Version2Stream& stream = streams.emplace_back(Version2Stream{{}, MCFastFile::StreamKind::Tiles});
        const std::vector<uint8_t> frame = FrameFile(Side, Side, static_cast<int32_t>(at) + 1);
        stream.Entries.push_back(Raw(name(Keys[at]) + ".hdf", frame));
        stream.Entries.push_back(Raw(std::format("filler{}.bin", at), std::vector<uint8_t>(Filler)));
        stored.push_back(frame.size() + Filler);
    }

    const std::vector<uint8_t> archive = BuildVersion2Streams(streams);

    for (const std::string_view folder : {MCHdArt::TerrainFolder, ReliefFolder, MCHdArt::OverlayFolder})
    {
        WriteBytes(pack.Path() / std::filesystem::path(folder).concat(".hda"), archive);
    }

    MCHdArt::SetPackFolder(pack.Path());

    {
        MCTest::Scope scope("a load's names");
        const std::array<MCHdArt::GroundAhead, 1> tile{{{.Width = Side, .Height = Side, .Key = Keys[0]}}};
        const std::array<MCHdArt::GroundAhead, 1> overlay{{{.Width = Side, .Height = Side, .Key = Keys[3]}}};
        MCHdArt::StartLoadFill();
        MCHdArt::ReadGroundAhead(tile, true);
        MCHdArt::ReadOverlaysAhead(overlay);
        MCHdArt::WaitForReadAhead();
        MCHdArt::FinishLoadFill([] {});
        const std::vector<std::filesystem::path> folders = MCHdArt::LoadFolders();
        CHECK(std::ranges::contains(folders, std::filesystem::path(MCHdArt::TerrainFolder) / name(Keys[0])));
        CHECK(std::ranges::contains(folders, std::filesystem::path(MCHdArt::OverlayFolder) / name(Keys[3])));
        // Read for the load, not held: the ground's archives keep every map's.
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
        MCHdArt::LetGoHold();
    }

    const std::vector<std::filesystem::path> list{std::filesystem::path(MCHdArt::TerrainFolder) / name(Keys[0]),
                                                  std::filesystem::path(MCHdArt::TerrainFolder) / name(Keys[2]),
                                                  std::filesystem::path(MCHdArt::OverlayFolder) / name(Keys[3])};
    const size_t reads = MCHdArt::HeldReads();

    {
        MCTest::Scope scope("the briefing");
        MCHdArt::StartBriefingHold(Mission, list, true);
        MCHdArt::WaitForReadAhead();
        MCHdArt::WaitForHold();
        // The two tiles, their relief maps and the overlay: never the streams between.
        CHECK_EQ(MCHdArt::HeldBytes(), 2 * (stored[0] + stored[2]) + stored[3]);
        CHECK_EQ(MCHdArt::HeldReads(), reads + TerrainRuns + ReliefRuns + OverlayRuns);

        // Its manifest (MCHdArt::Manifest): each ground archive once, with the frames its list names; not complete
        // before the load, when the players' mechs may still change.
        const MCHdArt::MissionManifest manifest = MCHdArt::Manifest();
        CHECK(manifest.Mission == Mission);
        CHECK(manifest.Players.empty());
        CHECK(!manifest.Complete);
        REQUIRE_EQ(manifest.Entries.size(), 3u);
        uint64_t bytes = 0;
        uint32_t frames = 0;

        for (const MCHdArt::ManifestEntry& entry : manifest.Entries)
        {
            bytes += entry.Bytes;
            frames += entry.Frames;
            CHECK(entry.GpuBytes > 0);
        }

        CHECK_EQ(bytes, 2 * (stored[0] + stored[2]) + stored[3]);
        CHECK_EQ(frames, 5u);
    }

    {
        MCTest::Scope scope("the same list again");
        MCHdArt::StartBriefingHold(Mission, list, true);
        MCHdArt::WaitForReadAhead();
        MCHdArt::WaitForHold();
        CHECK_EQ(MCHdArt::HeldBytes(), 2 * (stored[0] + stored[2]) + stored[3]);
        CHECK_EQ(MCHdArt::HeldReads(), reads + TerrainRuns + ReliefRuns + OverlayRuns);
    }

    {
        // Complete once the load begins, a change of its own.
        MCTest::Scope scope("the load");
        const uint64_t generation = MCHdArt::Manifest().Generation;
        MCHdArt::StartLoadFill();
        const MCHdArt::MissionManifest loading = MCHdArt::Manifest();
        CHECK(loading.Complete);
        CHECK(loading.Generation > generation);
        CHECK_EQ(loading.Entries.size(), 3u);
        MCHdArt::FinishLoadFill([] {});
    }

    MCHdArt::LetGoHold();
    CHECK_EQ(MCHdArt::HeldBytes(), 0u);
    CHECK(MCHdArt::Manifest().Entries.empty());
    MCHdArt::SetPackFolder({});
    GHdArt = previous;
}

/// <summary>
/// Port-only (MCHdArt::StartBriefingHold): a briefing left before the readers opened its archives hands none of them to
/// the next mission's hold, as they open or at its start (MCHdArt::StartHold): each open is the hold's it was asked
/// in, let go of with it (MCHdArt::LetGoHold), the archive the game's thread's to take. A later mission asking for
/// them again holds them, past a folder found to have no archive of its own.
/// </summary>
/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing"): a mission that fits goes on the GPU from its briefing, at
/// play's pace (the readers' work and the renderer's take each frame), before Launch; its load then asks for nothing more
/// and its bar waits for nothing on the GPU.
/// </summary>
TEST_CASE("hd art: a mission that fits goes on the GPU from its briefing, and its load adds nothing")
{
    constexpr size_t MostFrames = 64;
    constexpr std::string_view Mission = "mis0101";
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}, {StreamKind::Packet, 1000}};
    const HoldPack pack("briefing-fill", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    std::vector<MCHdArt::StagedPicture> up;

    {
        MCTest::Scope scope("the briefing");
        MCHdArt::StartBriefingHold(Mission, PacketFolders(pack), false);
        FramesUntilUp(before + pieces.size(), MostFrames, up);
        const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
        CHECK(fill.Fits);
        CHECK(fill.MissionBytes > 0);
        CHECK_EQ(fill.Up, before + pieces.size());
        CHECK(!MCHdArt::LoadFilling());
    }

    {
        MCTest::Scope scope("its load");
        const uint64_t asked = MCHdArt::GpuFillNow().Asked;
        MCHdArt::StartLoadFill(Mission);
        MCHdArt::WaitForReadAhead();
        CHECK(FinishTakingEveryPicture().empty());
        CHECK_EQ(MCHdArt::GpuFillNow().Asked, asked);
        CHECK_EQ(MCHdArt::GpuFillNow().Up, before + pieces.size());
        CHECK(MCHdArt::GpuFillNow().Fits);
    }

    pack.CutShapes();
    pack.CheckStaged(up);
    MCHdArt::LetGoHold();
    CHECK(!MCHdArt::GpuFillNow().Fits);
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing"): a mission's list names a type by its packet 0's folder, which
/// opens the type's archive; the briefing puts up every packet the archive holds, though the type has no packet 0 and its
/// numbers skip more than the pack's types do.
/// </summary>
TEST_CASE("hd art: the briefing puts up every packet of a type its list names by one folder")
{
    constexpr size_t MostFrames = 64;
    constexpr std::string_view Mission = "mis0101";
    constexpr std::string_view Type = "objects/shapes90/7";
    constexpr std::array<int32_t, 3> Packets{3, 4, 140};
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const ScratchFolder pack("briefing-type-packets");
    WriteTypeArchive(pack, Type, Packets);
    MCHdArt::SetPackFolder(pack.Path());
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    std::vector<MCHdArt::StagedPicture> up;
    const std::array<std::filesystem::path, 1> list{std::filesystem::path(Type) / "0"};
    MCHdArt::StartBriefingHold(Mission, list, false);
    FramesUntilUp(before + Packets.size(), MostFrames, up);
    CHECK(MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before + Packets.size());
    CHECK_EQ(up.size(), Packets.size());
    MCHdArt::LetGoHold();
    MCHdArt::SetPackFolder({});
    GHdArt = previous;
}

namespace
{
    /// <summary>
    /// Port-only (the opening): a load no briefing held for, of a mission that doesn't fit on the GPU whole (a room of
    /// 0.9 of it at its pages' share), whose opening is the pack's folders backwards: the bar ends with it up
    /// (<paramref name="waitForGpu"/>), or in a match (not) with it asked for before play, up in play's frames after.
    /// </summary>
    void CheckLoadOpening(std::string_view name, bool waitForGpu)
    {
        constexpr size_t Pieces = 8;
        constexpr size_t Frames = 16;
        constexpr double ShortShare = 0.9;
        const ReaderScope readers({.Readers = 2, .Hold = true});
        const int previous = GHdArt;
        GHdArt = 1;
        const std::vector<HoldPiece> pieces(Pieces, HoldPiece{StreamKind::Packet});
        const HoldPack pack(name, pieces);
        const FillSlotsScope slots;
        const MCHdArt::GpuFill start = MCHdArt::GpuFillNow();
        std::vector<MCHdArt::OpeningPacket> opening;

        for (const std::filesystem::path& folder : PacketFolders(pack) | std::views::reverse)
        {
            opening.push_back({folder, std::nullopt});
        }

        MCHdArt::NoteGpuRoom(
            static_cast<uint64_t>(static_cast<double>(pack.HeldBytes()) * start.PagesShare * ShortShare));
        MCHdArt::StartLoadFill();
        pack.Open();
        MCHdArt::FillGpu(pack.Packets());
        MCHdArt::SetOpening(opening);
        std::vector<MCHdArt::StagedPicture> up;
        MCHdArt::FinishLoadFill([&up] { TakeEveryPicture(up); }, waitForGpu);
        const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
        CHECK(!fill.Fits);
        CHECK(fill.OpeningDone);
        REQUIRE(fill.OpeningAsked > 0);
        CHECK(fill.OpeningAsked < Pieces);

        if (!waitForGpu)
        {
            FramesUntilUp(start.Up + fill.OpeningAsked, Frames, up);
        }

        CHECK_EQ(up.size(), fill.OpeningAsked);
        CHECK_EQ(MCHdArt::GpuFillNow().Up, start.Up + fill.OpeningAsked);
        const std::shared_ptr<const MCHdArt::Frame> first = pack.FindStream(Pieces - 1);
        REQUIRE(first != nullptr);
        CHECK(std::ranges::any_of(up, [&first](const MCHdArt::StagedPicture& picture)
                                  { return picture.Record->Id == first->Id; }));
        MCHdArt::NoteGpuRoom(std::nullopt);
        MCHdArt::LetGoHold();
        GHdArt = previous;
    }
}

/// <summary>
/// Port-only (SPEC-memory.md's "Under the loading bar"): a load no briefing came before (a saved game) of a mission that
/// doesn't fit on the GPU whole ends its bar once the mission's opening is up.
/// </summary>
TEST_CASE("hd art: a load no briefing held for ends its bar once the opening of a mission that doesn't fit is up")
{
    CheckLoadOpening("load-opening", true);
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing", a match): a match's load, whose bar doesn't wait for the
/// GPU, asks for the opening of a mission that doesn't fit while the load's archives are its to look up, so it goes up in
/// play's first frames (21 Nov: only a fitting mission's rest was asked for then; in play none is opened for it).
/// </summary>
TEST_CASE("hd art: a match's load asks for the opening of a mission that doesn't fit before play")
{
    CheckLoadOpening("match-opening", false);
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing": the opening): a packet of the player's mechs in the opening (a
/// PAK and a packet number, which names one gesture at one facing on any mech) goes up as that packet of each mech placed
/// whose part is that PAK's, and not of a mech of the mission's own, though the player places one of its type too.
/// </summary>
TEST_CASE("hd art: the opening's packets of the player's mechs go up as those of the mechs placed")
{
    constexpr size_t MostFrames = 64;
    constexpr std::string_view Mission = "mis0101";
    constexpr std::string_view Part = "mechs/legs90";
    constexpr uint16_t Packet = 51;
    // The player's mech's archive and the mission's own mech's, each with the opening's packet among others.
    constexpr std::string_view Players = "mechs/legs90/7";
    constexpr std::string_view Missions = "mechs/legs90/8";
    constexpr std::array<int32_t, 3> Packets{0, Packet, Packet + 1};
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const ScratchFolder pack("opening-lance");
    WriteTypeArchive(pack, Players, Packets);
    WriteTypeArchive(pack, Missions, Packets);
    MCHdArt::SetPackFolder(pack.Path());
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    std::vector<MCHdArt::StagedPicture> up;
    const std::array<std::filesystem::path, 1> list{std::filesystem::path(Missions) / "0"};
    const std::array<std::filesystem::path, 1> placed{std::filesystem::path(Players) / "0"};
    const std::array<MCHdArt::OpeningPacket, 1> opening{MCHdArt::OpeningPacket{std::filesystem::path(Part), Packet}};
    MCHdArt::StartBriefingHold(Mission, list, false);
    MCHdArt::SetOpening(opening);
    MCHdArt::HoldMechAhead(placed);
    MCHdArt::HoldMechAhead(list);
    FramesUntilUp(before + 2 * Packets.size(), MostFrames, up);
    const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
    CHECK(fill.Fits);
    CHECK_EQ(fill.Up, before + 2 * Packets.size());
    CHECK(fill.OpeningDone);
    CHECK_EQ(fill.OpeningAsked, 1);
    CHECK(fill.OpeningBytes > 0);
    MCHdArt::LetGoHold();
    MCHdArt::SetPackFolder({});
    GHdArt = previous;
}

/// <summary>
/// Port-only (the opening): a packet of the player's mechs at a place in the lance goes up as that packet of the mech
/// placed there among those whose part is that PAK's (MCHdArt::OpeningLance's order), not of every one; a mech past the
/// list's places takes theirs in turn again (21 Nov: every lance packet went up for every mech of its part, 3.2 times
/// what the play drew, filling the opening's room).
/// </summary>
TEST_CASE("hd art: the opening's packets of the player's mechs at a place go up as those of the mech placed there")
{
    constexpr size_t MostFrames = 64;
    constexpr std::string_view Mission = "mis0101";
    constexpr std::string_view Part = "mechs/legs90";
    constexpr uint16_t First = 51;
    constexpr uint16_t Second = 52;
    // Three of the player's mechs placed in turn: the first has both packets, the second the second's, the third the
    // first's; the list's play had two places.
    constexpr std::array<std::string_view, 3> Mechs{"mechs/legs90/7", "mechs/legs90/9", "mechs/legs90/11"};
    constexpr std::array<int32_t, 3> FirstPackets{0, First, Second};
    constexpr std::array<int32_t, 2> SecondPackets{0, Second};
    constexpr std::array<int32_t, 2> ThirdPackets{0, First};
    // Placed so, 7/51, 9/52 and 11/51 go up (11 at place 2, the first again); for every mech, 7/52 too.
    constexpr size_t OpeningFolders = 3;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const ScratchFolder pack("opening-places");
    WriteTypeArchive(pack, Mechs[0], FirstPackets);
    WriteTypeArchive(pack, Mechs[1], SecondPackets);
    WriteTypeArchive(pack, Mechs[2], ThirdPackets);
    MCHdArt::SetPackFolder(pack.Path());
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    const uint64_t frames = FirstPackets.size() + SecondPackets.size() + ThirdPackets.size();
    std::vector<MCHdArt::StagedPicture> up;
    const std::array<MCHdArt::OpeningPacket, 2> opening{MCHdArt::OpeningPacket{std::filesystem::path(Part), First, 0},
                                                        MCHdArt::OpeningPacket{std::filesystem::path(Part), Second, 1}};
    MCHdArt::StartBriefingHold(Mission, {}, false);
    MCHdArt::SetOpening(opening);

    for (const std::string_view mech : Mechs)
    {
        const std::array<std::filesystem::path, 1> placed{std::filesystem::path(mech) / "0"};
        MCHdArt::HoldMechAhead(placed);
    }

    CHECK(std::ranges::equal(MCHdArt::OpeningLance(), Mechs));
    FramesUntilUp(before + frames, MostFrames, up);
    const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
    CHECK(fill.Fits);
    CHECK_EQ(fill.Up, before + frames);
    CHECK(fill.OpeningDone);
    CHECK_EQ(fill.OpeningAsked, OpeningFolders);
    MCHdArt::LetGoHold();
    MCHdArt::SetPackFolder({});
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing": counted again at each change): a mech taken off in the mech
/// bay comes off the mission's size on the GPU, as it went on it when placed, and goes back on when placed again (21 Nov:
/// a mech taken off stayed counted, so swapping mechs could make a mission that fits seem not to).
/// </summary>
TEST_CASE("hd art: a mech taken off at the briefing comes off the mission's size on the GPU")
{
    constexpr std::string_view Mission = "mis0101";
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("briefing-mech-off", pieces);
    const FillSlotsScope slots;
    // The other archive stands for a mech's folder of its own, as big as the mission's.
    const std::array<std::filesystem::path, 1> mech{std::filesystem::path(HoldPack::MoreFolder)};
    const auto missionBytes = []
    {
        MCHdArt::WaitForReadAhead();
        MCHdArt::WaitForHold();
        MCHdArt::NextFrame(false);
        return MCHdArt::GpuFillNow().MissionBytes;
    };

    MCHdArt::StartBriefingHold(Mission, PacketFolders(pack), false);
    const uint64_t own = missionBytes();
    REQUIRE(own > 0);
    MCHdArt::HoldMechAhead(mech);
    CHECK_EQ(missionBytes(), 2 * own);
    MCHdArt::MechTakenOff(mech);
    CHECK_EQ(missionBytes(), own);
    MCHdArt::HoldMechAhead(mech);
    CHECK_EQ(missionBytes(), 2 * own);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "When the system says memory is short", its first step): memory short, the fill on the GPU
/// stops with the hold's, the rest of the mission asked for no more (21 Nov: it went on asking, the hold keeping no
/// more), and the load's bar waits for nothing on the GPU.
/// </summary>
TEST_CASE("hd art: memory short stops the fill on the GPU with the hold's")
{
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}, {StreamKind::Packet, 1000}};
    const HoldPack pack("gpu-fill-memory-short", pieces);
    const FillSlotsScope slots;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    MCHdArt::WaitForHold();
    MCHdArt::NextFrame(true);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    const uint64_t asked = MCHdArt::GpuFillNow().Asked;
    FinishTakingEveryPicture();
    MCHdArt::NextFrame(false);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(MCHdArt::GpuFillNow().Asked, asked);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "When the system says memory is short", and the opening): the same where the mission
/// doesn't fit and only its opening goes up from the briefing, at the briefing's pace: memory short, what the opening
/// asked for goes (its pictures in the slots with it) and nothing more goes up (22 Nov: it ran on, up to
/// OpeningMostBytes, on the 8 GB machines the stop is for).
/// </summary>
TEST_CASE("hd art: memory short stops the opening's fill on the GPU where the mission doesn't fit")
{
    constexpr size_t Pieces = 16;
    constexpr size_t Frames = 16;
    constexpr std::string_view Mission = "mis0101";
    constexpr uint64_t NoRoom = 1;
    // A room it doesn't fit, in tenths of the mission's bytes at the pages' share, whose quarter takes a few folders.
    constexpr double ShortTenths = 9;
    constexpr double Tenths = 10;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces(Pieces, HoldPiece{StreamKind::Packet});
    const HoldPack pack("gpu-opening-memory-short", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    std::vector<MCHdArt::OpeningPacket> opening;

    for (const std::filesystem::path& folder : PacketFolders(pack))
    {
        opening.push_back({folder, std::nullopt});
    }

    std::vector<MCHdArt::StagedPicture> up;
    MCHdArt::NoteGpuRoom(NoRoom);
    MCHdArt::StartBriefingHold(Mission, PacketFolders(pack), false);
    MCHdArt::SetOpening(opening);
    FramesUntilUp(before + 1, Frames, up);
    const MCHdArt::GpuFill known = MCHdArt::GpuFillNow();
    REQUIRE(known.MissionBytes > 0);
    REQUIRE(up.empty());

    // Its opening asked for within its room, and put in the slots, no frame taking them up yet: at the briefing's pace.
    MCHdArt::NoteGpuRoom(
        static_cast<uint64_t>(static_cast<double>(known.MissionBytes) * known.PagesShare * ShortTenths / Tenths));
    MCHdArt::WaitForHold();
    MCHdArt::NextFrame(false);
    MCHdArt::WaitForReadAhead();
    MCHdArt::WaitForGpuReads();
    const MCHdArt::GpuFill asked = MCHdArt::GpuFillNow();
    CHECK(!asked.Fits);
    REQUIRE(asked.OpeningAsked > 0);
    REQUIRE(asked.Waiting > 0);
    CHECK(MCHdArt::BriefingFilling());

    // Memory short: what it asked for goes at once (a read under way then is dropped as it ends), and nothing goes up
    // after.
    MCHdArt::NextFrame(true);
    CHECK_EQ(MCHdArt::GpuFillNow().Waiting, 0);
    CHECK(!MCHdArt::BriefingFilling());
    MCHdArt::WaitForGpuReads();
    const MCHdArt::GpuFill stopped = MCHdArt::GpuFillNow();
    CHECK_EQ(stopped.Waiting, 0);
    CHECK(!stopped.Reading);
    FramesUntilUp(before + 1, Frames, up);
    CHECK(up.empty());
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before);
    CHECK_EQ(MCHdArt::GpuFillNow().Asked, stopped.Asked);
    MCHdArt::NoteGpuRoom(std::nullopt);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing", for the timing runs' comparison): with -hdbriefingfill off the
/// briefing holds its mission's art and puts none of it on the GPU, as before it.
/// </summary>
TEST_CASE("hd art: with the briefing's fill off, a briefing puts nothing on the GPU")
{
    constexpr size_t Frames = 8;
    constexpr std::string_view Mission = "mis0101";
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("briefing-fill-off", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    std::vector<MCHdArt::StagedPicture> up;
    MCHdArt::SetBriefingFill(false);
    CHECK(!MCHdArt::BriefingFillWanted());
    MCHdArt::StartBriefingHold(Mission, PacketFolders(pack), false);
    FramesUntilUp(before + pieces.size(), Frames, up);
    CHECK(up.empty());
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before);
    CHECK(!MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    MCHdArt::SetBriefingFill(true);
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-memory.md's "The GPU from the briefing", a match): a match's load ends once its art is in memory (the
/// player checks in then), not once it's all on the GPU; what's left goes up in the frames after, so no player's fill
/// holds up the room.
/// </summary>
TEST_CASE("hd art: a match's load ends with its art in memory, what's left going up after it")
{
    constexpr size_t MostFrames = 64;
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("match-fill", pieces);
    const FillSlotsScope slots;
    const uint64_t before = MCHdArt::GpuFillNow().Up;
    MCHdArt::StartLoadFill();
    pack.Open();
    MCHdArt::FillGpu(pack.Packets());
    // The renderer takes nothing meanwhile: a load that waited for the GPU would wait out its stall.
    const auto began = std::chrono::steady_clock::now();
    MCHdArt::FinishLoadFill([] {}, false);
    CHECK(std::chrono::steady_clock::now() - began < MCHdArt::GpuFillStall);
    CHECK(!MCHdArt::LoadFilling());
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    CHECK(MCHdArt::GpuFillNow().Fits);
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before);
    std::vector<MCHdArt::StagedPicture> up;
    FramesUntilUp(before + pieces.size(), MostFrames, up);
    CHECK_EQ(MCHdArt::GpuFillNow().Up, before + pieces.size());
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

TEST_CASE("hd art: a briefing left before its archives open hands them to no other mission's hold")
{
    constexpr std::string_view Mission = "mis0101";
    constexpr std::string_view Other = "mis0102";
    constexpr std::string_view Third = "mis0103";
    const ReaderScope readers({.Readers = 2, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Packet, 1000}, {StreamKind::Heads}};
    const HoldPack pack("briefing-left", pieces);
    // A folder in the shapes' without an archive of its own: its frames are the shapes' archive's.
    const std::array<std::filesystem::path, 1> shapes{HoldPack::ShapesFolder() / "sub"};
    const std::array<std::filesystem::path, 1> more{std::filesystem::path(HoldPack::MoreFolder)};

    // The first briefing's archive waits to be opened while the player goes on to another mission's briefing.
    MCHdArt::HoldOpens(true);
    MCHdArt::StartBriefingHold(Mission, shapes, false);
    MCHdArt::StartBriefingHold(Other, more, false);
    MCHdArt::HoldOpens(false);
    MCHdArt::WaitForReadAhead();
    MCHdArt::WaitForHold();
    CHECK(MCHdArt::HoldsFor(Other));
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    // The hold started again with the archives opened: still the other mission's alone.
    MCHdArt::StartHold();
    MCHdArt::WaitForHold();
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    // A third mission's briefing asks for the first's folder: the shapes' archive opened for it is held now.
    MCHdArt::StartBriefingHold(Third, shapes, false);
    MCHdArt::WaitForReadAhead();
    MCHdArt::WaitForHold();
    CHECK(MCHdArt::HoldsFor(Third));
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only, for test runs: -hdbriefing (TestBriefingSeconds) waits before a game segment's load with the mission's
/// hold started as its briefing would start it, and the load keeps that hold (MCHdArt::HoldsFor); the wait is spent.
/// </summary>
TEST_CASE_ISOLATED("hd art: -hdbriefing holds a game segment's mission before its load, and the load keeps it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t WaitSeconds = 1;
    const ScratchFolder pack("hdbriefing");
    MCTestGame::BootOptions.Started = [folder = pack.Path()]
    {
        GHdArt = 1;
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.Hold = true;
        MCHdArt::SetReaderSettings(settings);
        MCHdArt::SetPackFolder(folder);
        TestBriefingSeconds = WaitSeconds;
    };

    REQUIRE(MCTestGame::StartMission(1));
    const std::string mission = Mission()->Scenarios.at(static_cast<size_t>(Mission()->CurrentScenario));
    CHECK(MCHdArt::HoldsFor(mission));
    CHECK_EQ(TestBriefingSeconds, 0);
}

/// <summary>
/// Port-only (SPEC-memory.md's "The mission's manifest"): a mission's load keeps what it handed the hold as the
/// mission's list in the user folder (MCHdArt::KeepHoldList), the list the load works out (MCScenario::HoldList: the
/// player's mechs' folders left out), and its next briefing holds from it (MCHdArt::BriefingFolders) before the
/// program's table: a mod's mission, a multiplayer map or one made in the editor is held from its second play on. Beside
/// it, the pieces of the game folder's files the load read (MCHdArt::KeepFilePieces), which the mission's next pick reads
/// ahead (StartMissionHold).
/// </summary>
TEST_CASE_ISOLATED("hd art: a mission's load keeps its hold list, and its next briefing holds from it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const ScratchFolder pack("learned-hold-list");
    MCTestGame::BootOptions.Started = [folder = pack.Path()]
    {
        GHdArt = 1;
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.Hold = true;
        MCHdArt::SetReaderSettings(settings);
        MCHdArt::SetPackFolder(folder);
    };

    REQUIRE(MCTestGame::StartMission(1));
    const std::string mission = Mission()->Scenarios.at(static_cast<size_t>(Mission()->CurrentScenario));
    const MCScenario::MCHoldList list = Scenario()->HoldList(mission);
    REQUIRE(!list.Folders.empty());
    CHECK(list.PlayersLeft > 0);
    const std::vector<std::filesystem::path> briefing = MCHdArt::BriefingFolders(mission);
    REQUIRE_EQ(briefing.size(), list.Folders.size());

    for (size_t index = 0; index < briefing.size(); ++index)
    {
        CHECK_EQ(briefing[index].generic_string(), list.Folders[index]);
    }

    // The original's packet files and archives among the files' pieces, all read ahead at the mission's next pick.
    const std::vector<MCFilesAhead::MCFilePiece> pieces = MCHdArt::MissionFilePieces(mission);
    REQUIRE(!pieces.empty());
    CHECK(pieces == MCFilesAhead::NotedPieces());
    const auto ofKind = [&pieces](std::string_view extension)
    {
        return std::ranges::any_of(
            pieces, [extension](const MCFilesAhead::MCFilePiece& piece)
            { return MCIEquals(std::filesystem::path(piece.File).extension().string(), extension); });
    };

    CHECK(ofKind(".pak"));
    CHECK(ofKind(".fst"));
    const uint64_t listed = std::ranges::fold_left(pieces | std::views::transform(&MCFilesAhead::MCFilePiece::Bytes),
                                                   uint64_t{0}, std::plus<>());
    const uint64_t from = MCFilesAhead::PiecesReadBytes();
    StartMissionHold(mission);
    constexpr int32_t WaitMs = 20000;

    for (int32_t waited = 0; waited < WaitMs && MCFilesAhead::PiecesReadBytes() - from < listed; ++waited)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK_EQ(MCFilesAhead::PiecesReadBytes() - from, listed);
    MCFilesAhead::ForgetPieces();
}

/// <summary>
/// Port-only (SPEC-memory.md's fill): today's pack (version 1) is held too: an archive of a shape's numbered frames is
/// kept as stored, its entries in the order they lie, and with its file cut to nothing every frame is drawn as made, read
/// from the hold (none of the frames kept, MCHdArt::SetCacheBudget).
/// </summary>
TEST_CASE("hd art: the hold keeps a version 1 archive's numbered frames as stored, and they're drawn without its file")
{
    const ReaderScope readers({.Readers = 1, .Hold = true});
    const ScratchFolder scratch("hold-version-1");
    std::vector<ArchiveEntry> entries;
    size_t stored = 0;

    for (const AheadKey& key : AheadKeys())
    {
        entries.push_back(Raw(AheadName(key) + ".hdf", AheadFile(key, 0)));
        stored += entries.back().Stored.size();
    }

    const std::filesystem::path folder = MCHdArt::ShapeTableFolder(AheadTable);
    const std::filesystem::path file = std::filesystem::path(scratch.Path() / folder).concat(".hda");
    std::filesystem::create_directories(file.parent_path());
    WriteBytes(file, BuildArchive(entries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    MCHdArt::StartHold();
    REQUIRE(MCHdArt::FindShape(AheadTable, 0, AheadWidth, AheadHeight) != nullptr);
    MCHdArt::WaitForHold();
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::HeldBytes(), stored);

    std::filesystem::resize_file(file, 0);
    MCHdArt::SetCacheBudget(1);
    MCHdArt::NextFrame(false);
    MCHdArt::NextFrame(false);
    const size_t reads = MCHdArt::ReaderReads();

    for (int32_t frame = 0; frame < AheadFrames; ++frame)
    {
        for (int32_t step = 0; step < AheadSteps; ++step)
        {
            MCTest::Scope scope(std::format("frame {}", AheadName({frame, step})));
            const std::shared_ptr<const MCHdArt::Frame> found =
                MCHdArt::FindShape(AheadTable, frame, AheadWidth, AheadHeight, step);
            REQUIRE(found != nullptr);
            CHECK(SameFrame(*found, AheadFrame({frame, step}, 0)));
        }
    }

    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::ReaderReads(), reads);
    MCHdArt::SetCacheBudget(0);
    MCHdArt::LetGoHold();
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-memory.md's plan): the hold gives back what's past its share of the art's room as the room shrinks (a
/// frame's MCHdArt::NextFrame, MCMemoryRoom::TrimmedBytesPerFrame at most), and holds nothing more that mission (an
/// archive opened after isn't held) until the next mission's starts it again.
/// </summary>
TEST_CASE("hd art: the hold gives back what's past its share as the room shrinks, then holds no more that mission")
{
    constexpr uint64_t EightGb = uint64_t{8} << 30;
    // Half of it is the game itself, so the art's room is none.
    constexpr uint64_t NoRoom = 2 * MCMemoryRoom::GameItselfBytes;
    constexpr size_t Archives = 2;
    const ReaderScope readers({.Readers = 1});
    const auto system = std::make_shared<MCFakeMemorySystem>();
    const MCFakeMemoryScope memory(system);
    system->Total = EightGb;
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Heads}, {StreamKind::Packet}};
    const HoldPack pack("hold-give-back", pieces);
    pack.Open();
    HoldAndWait();
    REQUIRE_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());
    MCHdArt::NextFrame(false);
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());

    system->Total = NoRoom;
    MCHdArt::NextFrame(false);
    CHECK_EQ(MCHdArt::HeldBytes(), 0u);
    system->Total = EightGb;
    pack.Open(true);
    MCHdArt::WaitForHold();
    CHECK_EQ(MCHdArt::HeldBytes(), 0u);

    HoldAndWait();
    CHECK_EQ(MCHdArt::HeldBytes(), Archives * pack.HeldBytes());
    MCHdArt::LetGoHold();
    GHdArt = previous;
}

/// <summary>
/// Port-only (SPEC-pack-v2.md's Memory): the hold reads below everything else, a run of at most MCHdArt::HoldRunBytes at
/// a time: a load's read asked for while the one reader reads the hold's first run (each run here a stream as large) is
/// read as that run ends, before the hold's next two (the HD trace's H lines and the load's R line, in order).
/// </summary>
TEST_CASE("hd art: a read asked for while the hold reads waits for one of the hold's runs at most")
{
    constexpr size_t AskedStream = 3;
    constexpr size_t HoldRuns = 3;
    // The kind's place in a reader's read's trace line (R begun read unpacked kind ...).
    constexpr size_t ReadKindField = 4;
    const ReaderScope readers({.Readers = 1, .Hold = true});
    const int previous = GHdArt;
    GHdArt = 1;
    const std::vector<HoldPiece> pieces{{StreamKind::Packet, MCHdArt::HoldRunBytes},
                                        {StreamKind::Packet, MCHdArt::HoldRunBytes},
                                        {StreamKind::Packet, MCHdArt::HoldRunBytes},
                                        {StreamKind::Tiles}};
    const HoldPack pack("hold-urgent", pieces);
    const ScratchFolder traces("hold-urgent-trace");
    const std::filesystem::path trace = traces.Path() / "trace.tsv";
    const size_t ahead = MCHdArt::FramesReadAhead();
    REQUIRE(MCHdArt::OpenTrace(trace));
    pack.Open();
    MCHdArt::HoldTakenRuns(true);
    MCHdArt::StartHold();
    MCHdArt::WaitForHeldRun();
    pack.Ask(AskedStream, MCHdArt::ReadAs::Load);
    MCHdArt::HoldTakenRuns(false);
    MCHdArt::WaitForLoadReads();
    MCHdArt::WaitForHold();
    MCHdArt::CloseTrace();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + 1);
    CHECK_EQ(MCHdArt::HeldBytes(), pack.HeldBytes());

    size_t holds = 0;
    std::optional<size_t> holdsBeforeLoad;
    std::ifstream in(trace);

    for (std::string line; std::getline(in, line);)
    {
        std::vector<std::string> fields;

        for (const auto part : std::views::split(line, '\t'))
        {
            fields.emplace_back(part.begin(), part.end());
        }

        if (fields.front() == "H")
        {
            ++holds;
        }
        else if (fields.front() == "R" && fields.size() > ReadKindField && fields[ReadKindField] == "load")
        {
            holdsBeforeLoad = holdsBeforeLoad.value_or(holds);
        }
    }

    CHECK_EQ(holds, HoldRuns);
    REQUIRE(holdsBeforeLoad.has_value());
    CHECK_EQ(*holdsBeforeLoad, 1u);
    GHdArt = previous;
}

/// <summary>
/// Port-only: a sprite packet stripped for a version 2 pack's record 1 (MCPackRecords::ShapeTable) keeps its gesture
/// header, its frames' count, their headers and translucency and their directory's second words, and a frame it lists but
/// doesn't hold, without the rows: the table ends after its last frame's header. A packet with no table, or one whose
/// directory runs past it, is refused.
/// </summary>
TEST_CASE("hd art: a shape table stripped for a version 2 pack keeps its frames' headers, not their rows")
{
    constexpr std::array<uint8_t, 6> GestureHeader = {1, 2, 3, 4, 5, 6};
    constexpr size_t HeaderBytes = sizeof(MCVfxShapeHeader);
    constexpr size_t TranslucentBytes = 2;
    constexpr int32_t NotHeld = 1;
    constexpr int32_t Translucent = 2;
    constexpr uint32_t SecondWord = 0x1234;
    constexpr size_t SecondWordAt = 12;
    // A run of 3 of colour 7, then the row's end; the translucent frame's starts 03 00 (a lone pixel of colour 0).
    constexpr std::array<uint8_t, 3> OpaqueRows = {6, 7, 0};
    constexpr std::array<uint8_t, 5> TranslucentRows = {3, 0, 4, 9, 0};
    const std::array<MCTestShapes::TableFrame, 3> frames = {MCTestShapes::TableFrame{0, 0, 3, 1, OpaqueRows},
                                                            MCTestShapes::TableFrame{1, 0, 3, 1, OpaqueRows},
                                                            MCTestShapes::TableFrame{-1, -2, 3, 1, TranslucentRows}};
    std::vector<uint8_t> table = MCTestShapes::FramesTable(frames);
    // Frame 1 listed but not held (its directory entry at the table's end, as OB-190's are).
    PutAt(table, static_cast<size_t>(8 + 8 * NotHeld), static_cast<uint32_t>(table.size()));
    PutAt(table, SecondWordAt, SecondWord);
    std::vector<uint8_t> packet(GestureHeader.begin(), GestureHeader.end());
    packet.insert(packet.end(), table.begin(), table.end());

    for (const bool gesture : {true, false})
    {
        MCTest::Scope scope(gesture ? "a gesture's packet" : "a bare table");
        const std::expected<std::vector<uint8_t>, std::string> stripped =
            MCPackRecords::ShapeTable(gesture ? std::span<const uint8_t>(packet) : std::span<const uint8_t>(table));
        REQUIRE(stripped.has_value());
        const size_t start = gesture ? GestureHeader.size() : 0;
        CHECK(std::ranges::equal(std::span(*stripped).first(start), std::span(GestureHeader).first(start)));
        const std::span<const uint8_t> kept = std::span(*stripped).subspan(start);
        CHECK_EQ(kept.size(), 8 + 8 * frames.size() + 2 * HeaderBytes + TranslucentBytes);
        CHECK_EQ(VfxShapeCount(kept), VfxShapeCount(table));
        CHECK_EQ(MCVfxRead32(kept, SecondWordAt), static_cast<int32_t>(SecondWord));

        for (int32_t frame = 0; frame < static_cast<int32_t>(frames.size()); ++frame)
        {
            MCTest::Scope frameScope(std::format("frame {}", frame));
            REQUIRE_EQ(MCVfxHasShape(kept, frame), frame != NotHeld);
            CHECK_EQ(MCAgShapeIsAlpha(kept, frame), frame == Translucent);

            if (frame != NotHeld)
            {
                CHECK(std::ranges::equal(MCVfxShape(kept, frame).first(HeaderBytes),
                                         MCVfxShape(std::span<const uint8_t>(table), frame).first(HeaderBytes)));
            }
        }
    }

    CHECK(!MCPackRecords::ShapeTable(GestureHeader).has_value());
    std::vector<uint8_t> overlong = table;
    PutAt(overlong, 4, static_cast<int32_t>(table.size()));
    CHECK(!MCPackRecords::ShapeTable(overlong).has_value());
}

namespace
{
    /// <summary>
    /// A version 2 frame of <paramref name="frame"/> of <paramref name="table"/> at <paramref name="scale"/> times its
    /// size: the tests' colours, and its index picture the pixels the original's rows draw (MCHdArt::HasIndex).
    /// </summary>
    std::vector<uint8_t> MarkedFrameFile(std::span<const uint8_t> table, int32_t frame, int32_t scale)
    {
        const MCFrameBounds bounds = MCShapeFrameBounds(table, frame);
        const auto width = static_cast<int32_t>(bounds.Width);
        const auto height = static_cast<int32_t>(bounds.Height);
        MCHdArt::Frame hd;
        hd.Width = width * scale;
        hd.Height = height * scale;
        hd.Scale = scale;
        hd.Colors = SpriteLike(hd.Width, hd.Height);
        hd.Indices.assign(static_cast<size_t>(width) * height * MCHdArt::IndexBytes, 0);
        hd.Marked = true;
        VfxShapeVisit(table, frame,
                      [&](int32_t x, int32_t y, uint8_t color)
                      {
                          const int32_t column = x - static_cast<int32_t>(bounds.MinX);
                          const int32_t row = y - static_cast<int32_t>(bounds.MinY);
                          REQUIRE((column >= 0 && column < width && row >= 0 && row < height));
                          const size_t at = (static_cast<size_t>(row) * width + column) * MCHdArt::IndexBytes;
                          hd.Indices[at] = color;
                          hd.Indices[at + 1] = MCHdArt::HasIndex;
                      });
        return MCHdArt::EncodeHdf(hd);
    }

    /// <summary>The pixels <paramref name="visit"/> visits of a frame, in order of their place.</summary>
    std::vector<std::tuple<int32_t, int32_t, uint8_t>> Visited(
        const std::function<void(const std::function<void(int32_t, int32_t, uint8_t)>&)>& visit)
    {
        std::vector<std::tuple<int32_t, int32_t, uint8_t>> pixels;
        visit([&pixels](int32_t x, int32_t y, uint8_t color) { pixels.emplace_back(y, x, color); });
        std::ranges::sort(pixels);
        return pixels;
    }
}

/// <summary>
/// Port-only: with HD art drawing on the GPU alone and a version 2 pack whose mech folder holds its packet's stripped
/// table (record 1) and a frame for each frame the table holds with a box, the mech's shape is loaded from the pack in
/// place of its packet (no rows: MCHdArt::Stripped), its pixels visited from its frames' index pictures as the
/// original's rows visit them (MCHdArt::VisitShape), and with HD art off it takes its packet again
/// (MCSpriteManager::RestoreOriginalShapes). A folder lacking one of its frames is loaded from the packet, which the
/// guard of test runs notes (MCHdArt::GuardOriginalReads); given the packet whole as its record (the builder's for a
/// folder the pack lacks frames of), from the pack with its rows, nothing read.
/// </summary>
TEST_CASE_ISOLATED("game: a version 2 pack's stripped shape tables load a mech's shapes, their pixels from the frames")
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

    constexpr int32_t Scale = 2;
    const std::shared_ptr<MCSpriteTree> tree = MCTestGame::FindMechTree("madcat");
    REQUIRE(tree != nullptr);
    const std::shared_ptr<MCSpriteManager> sprites = SpriteManager();
    REQUIRE(sprites != nullptr);

    // Two packets of one part's PAK, so one archive holds both: the first whole, the second lacking a frame.
    const std::vector<MCGesturePacket> preloaded = tree->PreloadPackets();
    REQUIRE(!preloaded.empty());
    std::vector<MCGesturePacket> packets = {preloaded.front()};

    for (const MCGesturePacket& packet : preloaded)
    {
        if (packet.FilePart == packets.front().FilePart && packet.Packet != packets.front().Packet)
        {
            packets.push_back(packet);
            break;
        }
    }

    REQUIRE_EQ(packets.size(), 2);
    const MCMechPart part = packets.front().FilePart;
    const uint32_t mechNum = tree->FileNumbers[part];
    const auto load = [&](size_t which)
    {
        const std::shared_ptr<MCShape> shape =
            sprites->GetMechShapeData(mechNum, packets[which].Packet, part, Turn, tree, true);
        REQUIRE(shape != nullptr);
        return shape;
    };

    const auto folderOf = [&](size_t which)
    {
        return MCHdArt::FolderOf(
            MCHdArt::MechShapeKey{sprites->PartFileName(part, true), mechNum, packets[which].Packet});
    };

    std::vector<std::vector<uint8_t>> originals;
    std::vector<ArchiveEntry> entries;
    std::map<std::string, std::vector<uint8_t>> facts;

    for (size_t which = 0; which < packets.size(); ++which)
    {
        const std::span<const uint8_t> table = load(which)->FrameList();
        originals.emplace_back(table.begin(), table.end());
        const std::string folder = std::to_string(packets[which].Packet);
        std::vector<int32_t> boxed;

        for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
        {
            const MCFrameBounds bounds = MCShapeFrameBounds(table, frame);

            if (MCVfxHasShape(table, frame) && bounds.Width > 0.0f && bounds.Height > 0.0f)
            {
                boxed.push_back(frame);
            }
        }

        REQUIRE(!boxed.empty());

        // The second packet's folder lacks its last frame.
        for (const int32_t frame : which == 0 ? boxed : std::vector(boxed.begin(), boxed.end() - 1))
        {
            entries.push_back(Raw(std::format("{}\\{}.hdf", folder, frame), MarkedFrameFile(table, frame, Scale)));
        }

        const std::expected<std::vector<uint8_t>, std::string> stripped = MCPackRecords::ShapeTable(table);
        REQUIRE(stripped.has_value());
        facts[folder] = FactsRecord(MCPackRecords::Type::ShapeTable, *stripped);
    }

    const ScratchFolder scratch("stripped-tables");
    const std::filesystem::path archive =
        scratch.Path() / std::filesystem::path(folderOf(0)).parent_path().concat(".hda");
    std::filesystem::create_directories(archive.parent_path());
    WriteBytes(archive, BuildVersion2Archive(entries, facts));
    REQUIRE(MCHdArt::WritePackFile(scratch.Path()).has_value());
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    sprites->DumpAll();
    REQUIRE(MCHdArt::DrawsStrippedTables());
    const size_t fromPack = sprites->ShapeTablesFromPack();
    // The guard of test runs (-hdguard) on, to note the packet read from its PAK.
    MCHdArt::GuardOriginalReads(true);
    REQUIRE(MCHdArt::GuardingOriginalReads());
    const std::shared_ptr<MCShape> stripped = load(0);
    const std::shared_ptr<MCShape> lacking = load(1);
    const std::optional<std::vector<MCHdArt::GuardedRead>> guarded = MCHdArt::GuardedReads();
    REQUIRE(guarded.has_value());
    REQUIRE_EQ(guarded->size(), size_t{1});
    CHECK_EQ(guarded->front().Count, size_t{1});
    CHECK_EQ(guarded->front().Examples.front(), MCFileSystem::ToUtf8(std::filesystem::path(folderOf(1))));
    MCHdArt::GuardOriginalReads(false);
    CHECK(stripped->Stripped);
    CHECK(MCHdArt::Stripped(stripped->FrameList()));
    CHECK(stripped->FrameList().size() < originals[0].size());
    CHECK(!lacking->Stripped);
    CHECK(std::ranges::equal(lacking->FrameList(), originals[1]));
    CHECK_EQ(sprites->ShapeTablesFromPack(), fromPack + 1);
    const std::span<const uint8_t> original = originals[0];
    REQUIRE_EQ(VfxShapeCount(stripped->FrameList()), VfxShapeCount(original));
    size_t visited = 0;

    for (int32_t frame = 0; frame < VfxShapeCount(original); ++frame)
    {
        MCTest::Scope scope(std::format("frame {}", frame));
        CHECK_EQ(MCVfxHasShape(stripped->FrameList(), frame), MCVfxHasShape(original, frame));
        using Pixel = std::function<void(int32_t, int32_t, uint8_t)>;
        const auto fromRows = Visited([&](const Pixel& pixel) { VfxShapeVisit(original, frame, pixel); });
        const auto fromFrame =
            Visited([&](const Pixel& pixel) { MCHdArt::VisitShape(stripped->FrameList(), frame, pixel); });
        CHECK(fromFrame == fromRows);
        visited += fromRows.size();
    }

    CHECK(visited > 0);
    CHECK_EQ(sprites->RestoreOriginalShapes(), 1);
    CHECK(!stripped->Stripped);
    CHECK(!MCHdArt::Stripped(stripped->FrameList()));
    CHECK(std::ranges::equal(stripped->FrameList(), originals[0]));

    // The folder lacking a frame given its packet whole as its record (the builder's): loaded from the pack with its
    // rows, nothing read of the original's.
    facts[std::to_string(packets[1].Packet)] = FactsRecord(MCPackRecords::Type::ShapeTable, originals[1]);
    WriteBytes(archive, BuildVersion2Archive(entries, facts));
    MCHdArt::SetPackFolder({});
    MCHdArt::SetPackFolder(scratch.Path());
    sprites->DumpAll();
    MCHdArt::GuardOriginalReads(true);
    const size_t beforeWhole = sprites->ShapeTablesFromPack();
    const std::shared_ptr<MCShape> whole = load(1);
    CHECK(!whole->Stripped);
    CHECK(std::ranges::equal(whole->FrameList(), originals[1]));
    CHECK_EQ(sprites->ShapeTablesFromPack(), beforeWhole + 1);
    const std::optional<std::vector<MCHdArt::GuardedRead>> wholeReads = MCHdArt::GuardedReads();
    REQUIRE(wholeReads.has_value());
    CHECK(wholeReads->empty());
    MCHdArt::GuardOriginalReads(false);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (SPEC-first-use.md): the command line sets how HD art's readers read (<c>-hdreaders</c>,
/// <c>-hdsetreads</c>, <c>-hdreaderpriority</c>), the readers held to 1 to MCHdArt::MostReaders; a value a switch can't
/// take leaves its setting as it was.
/// </summary>
/// <summary>
/// Port-only (PLAN.md's first-use item 10): with a version 2 pack, whose overlays' frames mark the pixels the original
/// draws apart from those their indices spread to, the terrain's load leaves the map's overlays, the .pre's and its
/// mines' tiles unread (MCTerrainLeaveRebuiltTiles, MCTerrainReadMinesAhead), and the .pre's ground tiles no vertex
/// names, never drawn; with those its bridges, forests and walls take as they're destroyed
/// (MCTerrainDestroyedOverlaySlots), the load's HD ground
/// (MCTerrainReadHdTiles) puts each overlay in the cache rebuilt from its HD frame, byte for byte the original's packet;
/// one whose frame doesn't hold its pixels is read, which the guard of test runs notes (MCHdArt::GuardOriginalReads),
/// and no other. Mission 8, which starts with mined cells, its tile set opened again with none read.
/// </summary>
TEST_CASE_ISOLATED("game: a version 2 pack's overlays rebuild the map's overlays, its .pre's and its mines' tiles, "
                   "none read, the guard noting the one read")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t MinedMission = 8;
    constexpr int32_t Scale = 2;
    constexpr float FrameSeconds = 1.0f / 15.0f;
    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(MinedMission));

    if (MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        return;
    }

    const std::shared_ptr<MCTerrain> terrain = Terrain();
    REQUIRE(terrain != nullptr);
    // The mission's tile set opened again twice: one with none read, one to hold the rebuilt ones to.
    const auto open = [&terrain]() -> std::unique_ptr<MCTerrainTiles>
    {
        for (const std::string_view set : {"tiles", "gtiles"})
        {
            std::expected<std::unique_ptr<MCTerrainTiles>, std::string> tiles = MCTerrainTiles::Create(set);

            if (tiles.has_value() && (*tiles)->FilesHash() == terrain->Tiles->FilesHash())
            {
                return std::move(*tiles);
            }
        }

        return nullptr;
    };

    std::unique_ptr<MCTerrainTiles> fresh = open();
    const std::unique_ptr<MCTerrainTiles> reference = open();
    REQUIRE(fresh != nullptr);
    REQUIRE(reference != nullptr);
    const MCOptionalRef<const MCKnownGroundNames> known = MCKnownGroundNamesOf(*fresh);
    REQUIRE(known.has_value());
    const auto isOverlay = [&known](int32_t slot) { return known->get().At(slot).Overlay.has_value(); };

    // The map's overlays, the .pre's besides, its destroyed bridges', forests' and walls', then its mines' tiles.
    const std::vector<int32_t> mapUsed = terrain->MapBlocks->TilesUsed();
    std::vector<int32_t> slots;
    std::ranges::copy_if(mapUsed, std::back_inserter(slots), isOverlay);
    const size_t mapOverlays = slots.size();
    const auto addOverlays = [&slots, &isOverlay](std::span<const int32_t> more)
    {
        for (const int32_t slot : more)
        {
            if (isOverlay(slot) && !std::ranges::contains(slots, slot))
            {
                slots.push_back(slot);
            }
        }
    };

    addOverlays(terrain->PreloadedSlots);
    const size_t preOverlays = slots.size() - mapOverlays;
    addOverlays(MCTerrainDestroyedOverlaySlots());
    const size_t destroyedOverlays = slots.size() - mapOverlays - preOverlays;
    addOverlays(MCTerrainMineSlots(*terrain));
    REQUIRE(mapOverlays > 0);
    REQUIRE(preOverlays > 0);
    REQUIRE(slots.size() > mapOverlays + preOverlays + destroyedOverlays);

    // Their HD frames at version 2: the original's index where it draws, spread everywhere else; the first overlay's
    // first drawn pixel one index off, a pack that doesn't match the game's.
    std::vector<ArchiveEntry> entries;

    for (const int32_t slot : slots)
    {
        const MCGroundName name = *known->get().At(slot).Overlay;
        const std::optional<MCHdArt::GroundAhead> shape = MCGroundOverlayOf(*reference, slot);
        REQUIRE(shape.has_value());
        std::vector<uint8_t> indices = MCGroundOverlayIndices(reference->Lookup(slot), name);
        REQUIRE(!indices.empty());
        bool misnamed = slot == slots.front();

        for (size_t at = 0; at < indices.size(); at += MCHdArt::IndexBytes)
        {
            if (indices[at + 1] == 0)
            {
                indices[at + 1] = MCHdArt::Spread;
            }
            else if (misnamed)
            {
                ++indices[at];
                misnamed = false;
            }
        }

        MCHdArt::Frame hd;
        hd.Width = name.Width * Scale;
        hd.Height = name.Height * Scale;
        hd.Scale = Scale;
        hd.Colors = SpriteLike(hd.Width, hd.Height);
        hd.Indices = std::move(indices);
        hd.Marked = true;
        entries.push_back(Raw(MCHdArt::TerrainTileName(shape->Tile) + ".hdf", MCHdArt::EncodeHdf(hd)));
    }

    const ScratchFolder scratch("overlays-rebuilt");
    WriteBytes(scratch.Path() / (std::string(MCHdArt::OverlayFolder) + ".hda"), BuildVersion2Archive(entries));
    REQUIRE(MCHdArt::WritePackFile(scratch.Path()).has_value());
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());
    REQUIRE(MCHdArt::MarksOriginalPixels(MCHdArt::OverlayFolder));
    terrain->Tiles = std::move(fresh);
    // The guard of test runs (-hdguard) on, to note the original's tiles read.
    MCHdArt::GuardOriginalReads(true);
    REQUIRE(MCHdArt::GuardingOriginalReads());

    // The terrain's load (the .pre's slots, then the map's) leaves out the overlays and the .pre's ground tiles no
    // vertex names; the mines' and destroyed objects' reads (as the mission's load) leave theirs.
    std::vector<int32_t> used = terrain->PreloadedSlots;
    used.insert(used.end(), mapUsed.begin(), mapUsed.end());
    MCTerrainLeaveRebuiltTiles(*terrain, used);
    CHECK(std::ranges::none_of(used, isOverlay));
    CHECK(std::ranges::none_of(
        used, [&known, &mapUsed](int32_t slot)
        { return known->get().At(slot).Tile.has_value() && !std::ranges::contains(mapUsed, slot); }));
    MCTerrainReadMinesAhead();
    MCTerrainReadDestroyedAhead();
    const auto cached = [&terrain](int32_t slot)
    {
        const MCOptionalRef<const MCTerrainTile> tile = terrain->Tiles->Slot(slot);
        return tile.has_value() && !tile->get().TileData().empty();
    };

    CHECK(std::ranges::none_of(slots, cached));

    // The load's HD ground rebuilds all but the misnamed one, which it reads.
    const MCGroundRebuilt done = MCTerrainReadHdTiles();
    CHECK_EQ(done.Overlays, slots.size() - 1);
    size_t differ = 0;

    for (const int32_t slot : slots)
    {
        REQUIRE(cached(slot));
        differ += std::ranges::equal(terrain->Tiles->Slot(slot)->get().TileData(), reference->Lookup(slot)) ? 0 : 1;
    }

    std::cout << std::format(
        "  mission {}: {} overlays, {} the .pre's, {} destroyed objects' and {} mines' tiles, {} rebuilt from HD\n",
        MinedMission, mapOverlays, preOverlays, destroyedOverlays,
        slots.size() - mapOverlays - preOverlays - destroyedOverlays, done.Overlays);
    CHECK_EQ(differ, size_t{0});
    // The guard noted the misnamed overlay read from the tile file, and no other overlay.
    const std::optional<std::vector<MCHdArt::GuardedRead>> guarded = MCHdArt::GuardedReads();
    REQUIRE(guarded.has_value());
    const auto overlaysRead = std::ranges::find(*guarded, MCOverlayReadFromFile, &MCHdArt::GuardedRead::Kind);
    REQUIRE(overlaysRead != guarded->end());
    CHECK_EQ(overlaysRead->Count, size_t{1});
    CHECK(overlaysRead->Examples.front().starts_with(std::format("slot {} ", slots.front())));
    MCHdArt::GuardOriginalReads(false);
    CHECK(!MCHdArt::GuardedReads().has_value());
    // The mission draws on from them.
    MCTestGame::RunFrame(FrameSeconds);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

TEST_CASE("hd art: the command line sets the readers, the sets' reads at once and the readers' priority")
{
    constexpr size_t Readers = 8;
    constexpr size_t SetReads = 2;
    const ReaderScope defaults({});
    const auto set = [](std::string_view line, const MCHdArt::ReaderSettings& expected)
    {
        MCTest::Scope scope{std::string(line)};
        ParseCommandLine(line);
        const MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        CHECK_EQ(settings.Readers, expected.Readers);
        CHECK_EQ(settings.SetReads, expected.SetReads);
        CHECK_EQ(settings.Below, expected.Below);
    };

    set(std::format("-hdreaders {} -hdsetreads {} -hdreaderpriority level", Readers, SetReads),
        {.Readers = Readers, .SetReads = SetReads, .Below = false});
    set(std::format("-hdreaders {} -hdsetreads two -hdreaderpriority lowest", MCHdArt::MostReaders + 1),
        {.Readers = MCHdArt::MostReaders, .SetReads = SetReads, .Below = false});
    set("-HDREADERS 0 -hdsetreads 3x -hdreaderpriority below", {.Readers = 1, .SetReads = SetReads, .Below = true});
}

/// <summary>
/// Port-only: when another pack is set while the reading thread reads ahead from one, nothing it read is drawn for the
/// new pack's frames of the same names: every frame drawn is the new pack's, wherever the thread was in its reads (a
/// little further each round).
/// </summary>
TEST_CASE("hd art: frames read ahead from one pack are never drawn from the next")
{
    constexpr int32_t Rounds = 32;
    constexpr auto RoundDelay = std::chrono::microseconds(25);
    constexpr uint8_t NextSeed = 101;
    const ScratchFolder scratch("read-ahead-packs");
    const std::filesystem::path first = scratch.Path() / "first";
    const std::filesystem::path next = scratch.Path() / "next";
    WriteAheadPack(first, 0);
    WriteAheadPack(next, NextSeed);
    std::map<AheadKey, MCHdArt::Frame> expected;

    for (const AheadKey& key : AheadKeys())
    {
        expected.emplace(key, AheadFrame(key, NextSeed));
    }

    const int previous = GHdArt;
    GHdArt = 1;
    const size_t ahead = MCHdArt::FramesReadAhead();
    int32_t wrong = 0;

    for (int32_t round = 0; round < Rounds; ++round)
    {
        MCHdArt::SetPackFolder(first);
        REQUIRE(FindAhead({0, 0}) != nullptr);
        const auto until = std::chrono::steady_clock::now() + RoundDelay * round;

        while (std::chrono::steady_clock::now() < until)
        {
        }

        MCHdArt::SetPackFolder(next);

        for (const AheadKey& key : AheadKeys())
        {
            const std::shared_ptr<const MCHdArt::Frame> frame = FindAhead(key);
            wrong += frame == nullptr || !SameFrame(*frame, expected.at(key)) ? 1 : 0;
        }
    }

    CHECK_EQ(wrong, 0);
    CHECK(MCHdArt::FramesReadAhead() > ahead);
    MCHdArt::WaitForReadAhead();
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// With no pack folder set, the pack is the user folder's <c>HD</c> when there is one, else the game folder's (a pack
/// unzipped into the game's folder, as mods are).
/// </summary>
TEST_CASE_ISOLATED("hd art: the pack is found in the user folder, else in the game folder")
{
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_hd_pack_folder_test";
    const std::filesystem::path game = scratch / "game";
    const std::filesystem::path user = scratch / "user";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(game);
    std::filesystem::create_directories(user);
    MCFileSystem::SetGameRoot(game);
    MCFileSystem::SetUserRoot(user);

    // Neither: the user folder's, where the game makes things.
    MCHdArt::SetPackFolder({});
    CHECK_EQ(MCHdArt::PackFolder(), user / "HD");

    // The game folder's alone.
    std::filesystem::create_directories(game / "HD");
    MCHdArt::SetPackFolder({});
    CHECK_EQ(MCHdArt::PackFolder(), game / "HD");

    // Both: the user folder's.
    std::filesystem::create_directories(user / "HD");
    MCHdArt::SetPackFolder({});
    CHECK_EQ(MCHdArt::PackFolder(), user / "HD");
    std::filesystem::remove_all(scratch);
}

/// <summary>
/// pack.cfg: a pack of a later format is left out; one made from other sprite files keeps only what is named by its
/// pixels (the ground, the menus), not its mech and object folders. Only the files both the pack and the game opened
/// count (the 90-pixel parts are opened only when the player's preference asks); a pack without pack.cfg is used whole.
/// </summary>
TEST_CASE_ISOLATED(
    "hd art: pack.cfg leaves out a later format, and a pack made from other sprite files keeps its ground")
{
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_pack_file_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack);
    GHdArt = 1;
    const std::array<std::pair<std::string, uint32_t>, 2> made = {{{"shapes90", 100}, {"legs90", 200}}};

    const auto use = [&pack](std::span<const std::pair<std::string, uint32_t>> here)
    {
        MCHdArt::SetSpriteFiles(here);
        MCHdArt::SetPackFolder(pack);
    };

    // No pack.cfg: whole.
    use(made);
    CHECK(MCHdArt::Active());
    CHECK(MCHdArt::NumberedFolders());

    // Made from these files.
    REQUIRE(MCHdArt::WritePackFile(pack).has_value());
    use(made);
    CHECK(MCHdArt::Active());
    CHECK(MCHdArt::NumberedFolders());

    // A file of another size.
    const std::array<std::pair<std::string, uint32_t>, 2> other = {{{"shapes90", 100}, {"legs90", 201}}};
    use(other);
    CHECK(MCHdArt::Active());
    CHECK(!MCHdArt::NumberedFolders());

    // A file the game didn't open isn't compared.
    const std::array<std::pair<std::string, uint32_t>, 1> fewer = {{{"shapes90", 100}}};
    use(fewer);
    CHECK(MCHdArt::NumberedFolders());

    // A later format: none of it.
    {
        MCFitIniFile file;
        REQUIRE_EQ(file.Create((pack / "pack.cfg").string()), 0);
        file.WriteBlock("HDArtPack");
        file.WriteIdLong("Format", 3);
        file.Close();
    }

    use(made);
    CHECK(!MCHdArt::Active());
    CHECK(!MCHdArt::NumberedFolders());
    std::filesystem::remove_all(pack);
}

/// <summary>A movie's HD frames are found by the Smacker file's name, whatever its folders, extension or case.</summary>
TEST_CASE("hd art: a movie's HD frames are found by its name")
{
    const std::filesystem::path pack = std::filesystem::temp_directory_path() / "mc_hd_movie_test";
    std::filesystem::remove_all(pack);
    std::filesystem::create_directories(pack / "movies");
    std::ofstream(pack / "movies" / "OPENING.ivf").put('\0');
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(pack);

    CHECK_EQ(MCHdArt::FindMovie("data\\movies\\opening.smk"), std::optional(pack / "movies" / "OPENING.ivf"));
    CHECK_EQ(MCHdArt::FindMovie("data/movies/OPENING.SMK"), std::optional(pack / "movies" / "OPENING.ivf"));
    CHECK(!MCHdArt::FindMovie("CLOSING.SMK").has_value());
    CHECK(!MCHdArt::FindMovie("").has_value());
    GHdArt = 0;
    CHECK(!MCHdArt::FindMovie("OPENING.SMK").has_value());
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
    std::filesystem::remove_all(pack);
}

/// <summary>
/// Tool: <c>--hd-pack-file &lt;pack folder&gt;</c> writes the pack's pack.cfg for this game: the pack format and the
/// sprite files (mission 1's, as the game opens them) its mech and object folders are numbered by.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-pack-file writes a pack's pack.cfg for this game")
{
    const std::optional<std::string_view> folder = MCTest::Option("hd-pack-file");

    if (!folder.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path pack = std::filesystem::absolute(std::filesystem::path(*folder));
    REQUIRE(MCTestGame::StartMission(1));
    REQUIRE(!MCHdArt::SpriteFiles().empty());
    REQUIRE(MCHdArt::WritePackFile(pack).has_value());
    std::cout << std::format("  {}: {}\n", (pack / "pack.cfg").string(), MCHdArt::SpriteFiles());
}

/// <summary>
/// Tool: <c>--hd-compress &lt;folder&gt;</c> turns every PNG frame pair under a pack folder (<c>n.png</c> and
/// <c>n_idx.png</c>, at <c>--hd-scale</c> times the original, 3 unless given) into a frame file <c>n.hdf</c>, using every
/// core; the PNGs go unless <c>--hd-keep-png 1</c>. <c>--hd-effort</c> (0 to 4, 4 unless given) and <c>--hd-rdo</c> (the
/// entropy reduction's lambda, DefaultRdoLambda unless given; 0 = none) set bc7enc_rdo's work. It prints the frames, the bytes before and
/// after, and the colours' mean PSNR. A pair that can't be converted is reported and kept.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-compress turns a pack's PNG frames into BC7 frame files")
{
    const std::optional<std::string_view> folder = MCTest::Option("hd-compress");

    if (!folder.has_value())
    {
        return;
    }

    int32_t scale = DefaultScale;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-scale"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), scale);
    }

    MCBc7::MCBc7Options options;
    options.Effort = MCBc7::MaxEffort;
    options.RdoLambda = DefaultRdoLambda;

    if (const std::optional<std::string_view> given = MCTest::Option("hd-effort"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), options.Effort);
    }

    // strtof: libc++ has from_chars for floats only from macOS 26.
    if (const std::optional<std::string_view> given = MCTest::Option("hd-rdo"); given.has_value())
    {
        options.RdoLambda = std::strtof(std::string(*given).c_str(), nullptr);
    }

    const bool keepPngs = MCTest::Option("hd-keep-png").has_value();
    std::vector<std::filesystem::path> frames;
    std::error_code error;

    for (std::filesystem::recursive_directory_iterator it(std::filesystem::path(*folder), error), end;
         !error && it != end; it.increment(error))
    {
        const std::filesystem::path& path = it->path();
        const std::string stem = path.stem().string();

        if (path.extension() == PngExtension && !stem.ends_with(IndexSuffix))
        {
            frames.push_back(path);
        }
    }

    REQUIRE(!error);
    std::atomic<size_t> next = 0;
    std::atomic<uint64_t> before = 0;
    std::atomic<uint64_t> after = 0;
    std::atomic<size_t> done = 0;
    std::mutex lock;
    std::vector<std::string> failures;
    double psnrTotal = 0.0;
    size_t psnrFrames = 0;
    const auto started = std::chrono::steady_clock::now();

    const auto work = [&]
    {
        for (size_t i = next++; i < frames.size(); i = next++)
        {
            const std::filesystem::path& colorPath = frames[i];
            const std::filesystem::path indexPath =
                colorPath.parent_path() /
                (colorPath.stem().string() + std::string(IndexSuffix) + std::string(PngExtension));
            const std::filesystem::path hdfPath = std::filesystem::path(colorPath).replace_extension(".hdf");
            auto frame = MCHdArt::FrameFromPngs(colorPath, indexPath, scale);

            if (!frame)
            {
                const std::scoped_lock guard(lock);
                failures.push_back(std::format("{}: {}", colorPath.string(), frame.error()));
                continue;
            }

            const std::vector<uint8_t> file = MCHdArt::EncodeHdf(*frame, options);
            const auto read = MCHdArt::DecodeHdf(file, frame->OriginalWidth(), frame->OriginalHeight());
            const double psnr =
                read.has_value() ? Psnr(frame->Colors, MCBc7::Decode(read->Colors, read->Width, read->Height)) : 0.0;
            std::ofstream out(hdfPath, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));

            if (!out.flush() || !read.has_value())
            {
                const std::scoped_lock guard(lock);
                failures.push_back(std::format("{}: can't write {}", colorPath.string(), hdfPath.string()));
                continue;
            }

            std::error_code sizeError;
            before +=
                std::filesystem::file_size(colorPath, sizeError) + std::filesystem::file_size(indexPath, sizeError);
            after += file.size();

            if (!keepPngs)
            {
                std::filesystem::remove(colorPath, sizeError);
                std::filesystem::remove(indexPath, sizeError);
            }

            {
                const std::scoped_lock guard(lock);

                if (std::isfinite(psnr))
                {
                    psnrTotal += psnr;
                    ++psnrFrames;
                }
            }

            if (const size_t count = ++done; count % 10000 == 0)
            {
                std::cout << std::format("  {} of {} frames\n", count, frames.size()) << std::flush;
            }
        }
    };

    std::vector<std::thread> threads;

    for (uint32_t i = 0; i < std::max(std::thread::hardware_concurrency(), 1u); i++)
    {
        threads.emplace_back(work);
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << std::format(
        "  {} frames at {}x (effort {}, RDO {}): {:.1f} MB of PNG became {:.1f} MB, mean PSNR {:.1f} "
        "dB, {:.0f} s\n",
        done.load(), scale, options.Effort, options.RdoLambda, static_cast<double>(before.load()) / 1e6,
        static_cast<double>(after.load()) / 1e6, psnrFrames > 0 ? psnrTotal / psnrFrames : 0.0, seconds);

    for (const std::string& failure : failures)
    {
        std::cout << "  not converted: " << failure << "\n";
    }

    CHECK(failures.empty());
}

/// <summary>Tool (measuring): <c>--hd-measure-lz &lt;folder&gt;</c> packs every frame file under it with the game's LZ.</summary>
TEST_CASE_ISOLATED("tool: --hd-measure-lz packs frame files with the game's LZ")
{
    const std::optional<std::string_view> folder = MCTest::Option("hd-measure-lz");

    if (!folder.has_value())
    {
        return;
    }

    uint64_t raw = 0;
    uint64_t packed = 0;
    size_t files = 0;
    const auto started = std::chrono::steady_clock::now();

    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(*folder)))
    {
        if (entry.path().extension() != ".hdf")
        {
            continue;
        }

        std::ifstream in(entry.path(), std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        raw += bytes.size();
        packed += LZCompress(bytes).size();
        ++files;
    }

    std::cout << std::format("  {} files: {:.1f} MB raw, {:.1f} MB with the game's LZ ({:.1f}%), {:.1f} s\n", files,
                             static_cast<double>(raw) / 1e6, static_cast<double>(packed) / 1e6,
                             100.0 * static_cast<double>(packed) / static_cast<double>(std::max<uint64_t>(raw, 1)),
                             std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
}

namespace
{
    /// <summary>
    /// The sharp pictures' test (the two below): the pictures it packs, as <paramref name="version"/>'s archive, and what
    /// the game takes of them.
    /// </summary>
    void CheckSharpPack(PackVersion version)
    {
        if (!MCTestGame::Available())
        {
            return;
        }

        REQUIRE(MCTestGame::StartLogistics());
        GHdArt = 1;
        GlobalLog()->AttachHdArt();
        REQUIRE(WhiteFont != nullptr);
        const std::span<const uint8_t> font = WhiteFont->FontData.Bytes();
        const MCSharpText::MCFontLayers layers = MCSharpText::LayersOf(font);

        // A picture's red and green bytes, and the pack's of it: each byte changed, so that one taken from the pack is
        // told from one redrawn.
        const auto redGreen = [](std::vector<uint8_t> rgba)
        {
            const MCSharpPictures::MCMadePicture picture(std::move(rgba));
            REQUIRE(picture.RedGreen().has_value());
            return std::vector<uint8_t>(picture.RedGreen()->begin(), picture.RedGreen()->end());
        };

        const auto marked = [](std::vector<uint8_t> bytes)
        {
            std::ranges::transform(bytes, bytes.begin(),
                                   [](uint8_t byte) { return static_cast<uint8_t>(byte ^ 0x5a); });
            return bytes;
        };

        const auto name = [](uint64_t key) { return std::format("{:016x}.rg8", key); };

        // The printable glyphs' pictures.
        struct Glyph
        {
            uint64_t Key = 0;
            int32_t Columns = 0;
            int32_t Rows = 0;
            std::vector<uint8_t> Redrawn;
        };

        std::vector<Glyph> glyphs;
        std::set<uint64_t> seen;

        for (int32_t character = 0x20; character <= 0x7e; ++character)
        {
            const std::optional<MCSharpPictures::MCFontGlyph> glyph = MCSharpPictures::FontGlyph(font, character);

            if (!glyph.has_value() || glyph->Pitch <= 0)
            {
                continue;
            }

            const MCSharpPictures::MCGlyphPictures how =
                MCSharpPictures::GlyphPictures(glyph->Pixels, glyph->Pitch, glyph->Height, layers);
            std::vector<std::vector<uint8_t>> redrawn =
                MCSharpPictures::RedrawGlyph(glyph->Pixels, glyph->Pitch, glyph->Height, how);

            for (size_t picture = 0; picture < how.Count(); ++picture)
            {
                if (seen.insert(how.KeyOf(picture)).second)
                {
                    glyphs.push_back({how.KeyOf(picture), glyph->Pitch, glyph->Height, redGreen(redrawn[picture])});
                }
            }
        }

        REQUIRE(glyphs.size() > 10);

        // What the pack holds of a picture: its bytes as they unpack, and whether they're its own picture's (taken from
        // the pack), of another size (left out) or don't unpack (logged, and redrawn).
        enum class Held
        {
            Taken,
            OtherSize,
            Broken,
        };

        struct Packing
        {
            std::string Name;
            std::vector<uint8_t> Bytes;
            Held How = Held::Taken;
        };

        // Every other glyph's picture in the pack: the second of those of another size, the third broken.
        constexpr size_t OtherSizeGlyph = 2;
        constexpr size_t BrokenGlyph = 4;
        std::vector<Packing> pictures;
        std::set<uint64_t> packed;

        for (size_t at = 0; at < glyphs.size(); at += 2)
        {
            const Glyph& glyph = glyphs[at];
            const Held how = at == OtherSizeGlyph ? Held::OtherSize : at == BrokenGlyph ? Held::Broken : Held::Taken;
            pictures.push_back(
                {name(glyph.Key),
                 how == Held::OtherSize ? std::vector<uint8_t>(glyph.Redrawn.size() + 2) : marked(glyph.Redrawn), how});

            if (how == Held::Taken)
            {
                packed.insert(glyph.Key);
            }
        }

        // A mech's diagram's first frame, never made ahead: read from the pack as it's first drawn.
        const std::span<const uint8_t> atlas = GlobalLog()->MechRepShapes[0].Bytes();
        const int32_t frame = MCLogistics::MechArmorShapes;
        REQUIRE(MCHdArt::LineArt(atlas, frame));
        const std::optional<MCSharpPictures::MCLineArtCut> cut =
            MCSharpPictures::CutLineArt(atlas, frame, MCHdArt::LineArtDrawn(atlas, frame));
        REQUIRE(cut.has_value());
        const std::vector<uint8_t> lines = marked(redGreen(MCSharpLines::Redraw(
            cut->Art, cut->Columns, cut->Columns, cut->Rows, MCHdArt::LineArtDrawn(atlas, frame))));
        pictures.push_back({name(cut->Key), lines, Held::Taken});

        // Version 1: the first stored raw, the rest packed (DEFLATE), the broken one's stored bytes a block of a type
        // DEFLATE lacks. Version 2: the first and fourth cut from one stream stored as it is, the rest each its own
        // stream (a Zstandard frame), the broken one's checksum spoiled.
        std::vector<uint8_t> archive;

        if (version == PackVersion::One)
        {
            constexpr size_t BrokenBytes = 8;
            constexpr uint8_t ReservedBlocks = 0xff;
            std::vector<ArchiveEntry> entries;

            for (size_t at = 0; at < pictures.size(); ++at)
            {
                const Packing& picture = pictures[at];
                entries.push_back(at == 0 ? Raw(picture.Name, picture.Bytes)
                                          : ArchiveEntry{picture.Name,
                                                         picture.How == Held::Broken
                                                             ? std::vector<uint8_t>(BrokenBytes, ReservedBlocks)
                                                             : StoredDeflate(picture.Bytes),
                                                         picture.Bytes.size(), std::nullopt});
            }

            archive = BuildArchive(entries);
        }
        else
        {
            constexpr size_t SharesWithFirst = 3;
            constexpr uint8_t NoDictionary = 0;
            std::vector<Version2Stream> streams(1, Version2Stream{.Kind = MCFastFile::StreamKind::Pictures});

            for (size_t at = 0; at < pictures.size(); ++at)
            {
                const Packing& picture = pictures[at];

                if (at == 0 || at == SharesWithFirst)
                {
                    streams.front().Entries.push_back(Raw(picture.Name, picture.Bytes));
                    continue;
                }

                std::vector<uint8_t> frame = RawZstdFrame(picture.Bytes, NoDictionary);

                if (picture.How == Held::Broken)
                {
                    frame.back() ^= 0xff;
                }

                streams.push_back(Version2Stream{.Entries = {Raw(picture.Name, picture.Bytes)},
                                                 .Kind = MCFastFile::StreamKind::Pictures,
                                                 .Packed = std::move(frame),
                                                 .Codec = ZstdCodec});
            }

            archive = BuildVersion2Streams(streams);
        }

        const ScratchFolder scratch("sharp-pictures");
        WriteBytes(scratch.Path() / "sharp.hda", archive);
        MCHdArt::SetPackFolder(scratch.Path());
        REQUIRE(MCHdArt::SharpArchive() == scratch.Path() / "sharp.hda");
        MCSharpPictures::MakeFont(font);
        MCSharpPictures::WaitForMade();
        CHECK_EQ(MCSharpPictures::Tally().PackPictures, pictures.size());
        CHECK_EQ(MCSharpPictures::Tally().FromPack, packed.size());

        for (const Glyph& glyph : glyphs)
        {
            const MCTest::Scope scope(std::format("picture {}", name(glyph.Key)));
            const std::optional<MCSharpPictures::MCMadePicture> taken =
                MCSharpPictures::Take(glyph.Key, glyph.Columns, glyph.Rows);
            REQUIRE(taken.has_value());
            REQUIRE(taken->RedGreen().has_value());
            CHECK(std::ranges::equal(*taken->RedGreen(),
                                     packed.contains(glyph.Key) ? marked(glyph.Redrawn) : glyph.Redrawn));
        }

        // Drawn first: the pack's, the size the key's picture is.
        const size_t fromPack = MCSharpPictures::Tally().TakenFromPack;
        CHECK(!MCSharpPictures::Take(cut->Key, cut->Columns + 1, cut->Rows).has_value());
        const std::optional<MCSharpPictures::MCMadePicture> drawn =
            MCSharpPictures::Take(cut->Key, cut->Columns, cut->Rows);
        REQUIRE(drawn.has_value());
        REQUIRE(drawn->RedGreen().has_value());
        CHECK(std::ranges::equal(*drawn->RedGreen(), lines));
        CHECK_EQ(MCSharpPictures::Tally().TakenFromPack, fromPack + 1);

        // A pack without one: none.
        const ScratchFolder bare("sharp-pictures-none");
        MCHdArt::SetPackFolder(bare.Path());
        CHECK(!MCHdArt::SharpArchive().has_value());
        GHdArt = 0;
    }
}

/// <summary>
/// Port-only: the pack's sharp.hda (MCHdArt::SharpArchive) holds the sharp screens' pictures, each named by its key
/// (<c>&lt;key&gt;.rg8</c>, its red and green bytes): a font's glyphs and line art made ahead are taken from it where
/// it has them, of the size the key's picture is; the rest are redrawn, as is one that doesn't unpack; and a picture
/// never made ahead is read from it as it's first drawn.
/// </summary>
TEST_CASE_ISOLATED("hd art: the sharp screens' pictures are the pack's sharp.hda's where it has them, the rest redrawn")
{
    CheckSharpPack(PackVersion::One);
}

/// <summary>
/// Port-only: a version 2 sharp.hda (SPEC-pack-v2.md: no names, each entry cut from its stream) gives the sharp
/// screens' pictures as a version 1 one does, one that doesn't unpack redrawn. It crashed the menus' making thread
/// before (19 Nov: pack v2's final art), which took a version 2 entry's stream for its own stored bytes.
/// </summary>
TEST_CASE_ISOLATED("hd art: a version 2 sharp.hda gives the sharp screens' pictures as version 1's does")
{
    CheckSharpPack(PackVersion::Two);
}

namespace
{
    /// <summary>What --pack-compare found in one archive (or all of them).</summary>
    struct PackComparison
    {
        size_t Entries = 0;
        size_t Matched = 0;
        size_t Extra = 0;
        size_t SameBytes = 0;
        size_t Frames = 0;
        size_t Failed = 0;
        size_t ColoursDiffer = 0;
        size_t IndexChanged = 0;
        size_t IndexPixels = 0;
        size_t ShadowFrames = 0;
        size_t ShadowDiffers = 0;
        size_t OtherDiffer = 0;
        std::map<std::string, size_t> Missing;
        /// <summary>The shadow's pixels off the game's rule, by which way and the version 2 green.</summary>
        std::map<std::string, size_t> ShadowOffBy;

        auto Add(const PackComparison& other) -> void
        {
            Entries += other.Entries;
            Matched += other.Matched;
            Extra += other.Extra;
            SameBytes += other.SameBytes;
            Frames += other.Frames;
            Failed += other.Failed;
            ColoursDiffer += other.ColoursDiffer;
            IndexChanged += other.IndexChanged;
            IndexPixels += other.IndexPixels;
            ShadowFrames += other.ShadowFrames;
            ShadowDiffers += other.ShadowDiffers;
            OtherDiffer += other.OtherDiffer;

            for (const auto& [kind, count] : other.Missing)
            {
                Missing[kind] += count;
            }

            for (const auto& [kind, count] : other.ShadowOffBy)
            {
                ShadowOffBy[kind] += count;
            }
        }

        [[nodiscard]] auto Text() const -> std::string
        {
            std::string missing;

            for (const auto& [kind, count] : Missing)
            {
                missing += std::format(" {}={}", kind, count);
            }

            for (const auto& [kind, count] : ShadowOffBy)
            {
                missing += std::format(" (shadow {}: {})", kind, count);
            }

            return std::format("{} entries, {} found ({} the same bytes), {} only in version 2; {} frames compared, {} "
                               "unreadable, {} with other colours, {} with other indices ({} pixels), {} with shadow "
                               "marks ({} pixels off the game's rule), {} other files differing; missing:{}",
                               Entries, Matched, SameBytes, Extra, Frames, Failed, ColoursDiffer, IndexChanged,
                               IndexPixels, ShadowFrames, ShadowDiffers, OtherDiffer,
                               missing.empty() ? " none" : missing);
        }
    };

    /// <summary>
    /// A version 1 frame and the version 2 one made from it, held to SPEC-pack-v2.md's rule: the same size, scale,
    /// format and colours; each pixel with an index in one has one in the other, at the same index (only a translucent
    /// frame's and an icon's may change); the shadow marks those the game's own rule marks.
    /// </summary>
    auto CompareFrames(const MCHdArt::Frame& today, const MCHdArt::Frame& marked, PackComparison& found) -> void
    {
        ++found.Frames;

        if (today.Width != marked.Width || today.Height != marked.Height || today.Scale != marked.Scale ||
            today.Format != marked.Format || today.Blended != marked.Blended || today.Colors != marked.Colors ||
            today.Indices.size() != marked.Indices.size() || !marked.Marked || today.Marked)
        {
            ++found.ColoursDiffer;
            return;
        }

        // The shadow's reference: the pack's own marks where today's frame has them (a forest's, which the game
        // doesn't mark), else the game's rule.
        MCHdArt::Frame ruled = today;
        bool painted = false;

        for (size_t at = 1; at < today.Indices.size() && !painted; at += MCHdArt::IndexBytes)
        {
            painted = today.Indices[at] == MCHdArt::BakedShadow;
        }

        if (!painted)
        {
            MCHdArt::MarkBakedShadows(ruled);
        }

        size_t changed = 0;
        size_t shadow = 0;
        size_t shadowOff = 0;

        for (size_t at = 0; at < marked.Indices.size(); at += MCHdArt::IndexBytes)
        {
            const bool had = today.Indices[at + 1] != 0;
            const bool has = MCHdArt::HasIndexMark(marked.Indices[at + 1]);

            if (had != has || (has && today.Indices[at] != marked.Indices[at]))
            {
                ++changed;
            }

            const bool inShadow = MCHdArt::InShadowMark(marked.Indices[at + 1]);
            const bool ruledShadow = MCHdArt::InShadowMark(ruled.Indices[at + 1]);
            shadow += inShadow ? 1 : 0;
            shadowOff += inShadow != ruledShadow ? 1 : 0;

            if (inShadow != ruledShadow)
            {
                ++found.ShadowOffBy[std::format("{}{}", inShadow ? "added " : "dropped ", marked.Indices[at + 1])];
            }
        }

        found.IndexChanged += changed != 0 ? 1 : 0;
        found.IndexPixels += changed;
        found.ShadowFrames += shadow != 0 ? 1 : 0;
        found.ShadowDiffers += shadow != 0 ? shadowOff : 0;
    }

    /// <summary>
    /// The version 2 pack at <paramref name="root"/>'s dictionaries, as the game reads them (its <c>dicts.hdd</c>);
    /// none when it has no such file.
    /// </summary>
    auto ReadPackDictionaries(const std::filesystem::path& root) -> std::shared_ptr<const MCZstd::Dictionaries>
    {
        const std::filesystem::path path = root / PackDictionariesName;

        if (!std::filesystem::exists(path))
        {
            return nullptr;
        }

        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        auto read = MCZstd::Dictionaries::Read(bytes);
        const MCTest::Scope scope{
            std::format("{}: {}", MCFileSystem::ToUtf8(path), read.has_value() ? "read" : read.error())};
        REQUIRE(read.has_value());
        return *read;
    }

    /// <summary>
    /// Every entry of today's archive <paramref name="todayPath"/> held to the version 2 one made from it, whose
    /// streams unpack against <paramref name="dictionaries"/> (its pack's).
    /// </summary>
    auto ComparePackArchive(const std::filesystem::path& todayPath, const std::filesystem::path& markedPath,
                            const std::shared_ptr<const MCZstd::Dictionaries>& dictionaries) -> PackComparison
    {
        PackComparison found;
        auto today = MCFastFile::Create(MCFileSystem::ToUtf8(todayPath));
        auto marked = MCFastFile::Create(MCFileSystem::ToUtf8(markedPath), dictionaries);
        const MCTest::Scope opened{
            std::format("{}: {}", MCFileSystem::ToUtf8(markedPath), marked.has_value() ? "opened" : marked.error())};
        REQUIRE(today.has_value());
        REQUIRE(marked.has_value());
        CHECK(!(*today)->IsVersion2());
        REQUIRE((*marked)->IsVersion2());

        // Read in the version 2 archive's order, so each stream is read and unpacked once.
        std::vector<std::pair<int32_t, int32_t>> pairs;
        const std::span<const MCFileEntry> entries = (*today)->GetEntries();
        found.Entries = entries.size();

        for (int32_t i = 0; i < static_cast<int32_t>(entries.size()); ++i)
        {
            const std::string_view name = entries[static_cast<size_t>(i)].GetName();
            const std::optional<int32_t> j = (*marked)->Find(name);

            if (!j.has_value())
            {
                const size_t dot = name.rfind('.');
                ++found.Missing[dot == std::string_view::npos ? std::string("(none)") : std::string(name.substr(dot))];
                continue;
            }

            pairs.emplace_back(*j, i);
        }

        found.Matched = pairs.size();
        found.Extra = static_cast<size_t>((*marked)->GetNumFiles()) - pairs.size();
        const auto streamAt = [&marked](int32_t j) { return (*marked)->EntryAt(j).Offset; };
        std::ranges::sort(pairs, [&](const auto& a, const auto& b) { return streamAt(a.first) < streamAt(b.first); });
        int32_t storedAt = -1;
        std::vector<uint8_t> stored;

        for (const auto& [j, i] : pairs)
        {
            if (streamAt(j) != storedAt)
            {
                storedAt = streamAt(j);
                stored = (*marked)->ReadStored(j);
            }

            const std::string_view name = entries[static_cast<size_t>(i)].GetName();
            MCTest::Scope scope{std::string(name)};
            const auto before = (*today)->Unpack(i, (*today)->ReadStored(i));
            const auto after = (*marked)->Unpack(j, stored);

            if (!before.has_value() || !after.has_value())
            {
                ++found.Failed;
                continue;
            }

            if (*before == *after)
            {
                ++found.SameBytes;
                continue;
            }

            if (!name.ends_with(".hdf"))
            {
                ++found.OtherDiffer;
                continue;
            }

            const auto beforeFrame = MCHdArt::DecodeHdf(*before, 0, 0);
            const auto afterFrame = MCHdArt::DecodeHdf(*after, 0, 0);

            if (!beforeFrame.has_value() || !afterFrame.has_value())
            {
                ++found.Failed;
                continue;
            }

            CompareFrames(*beforeFrame, *afterFrame, found);
        }

        return found;
    }
} // namespace

/// <summary>
/// Tool: <c>--pack-compare &lt;a version 2 pack&gt; --pack-compare-with &lt;the pack it was made from&gt;</c> holds
/// every version 2 archive (<c>.hda</c>) to the version 1 archive at its path in the other: the same entries found by
/// their paths, the same bytes unpacked but the frames' version and marks (SPEC-pack-v2.md's "A frame"), printing what
/// differs an archive. The version 2 archives open against its <c>dicts.hdd</c>, when it has one, as the game's do; an
/// archive the game would refuse fails.
/// </summary>
TEST_CASE("tool: --pack-compare holds a version 2 pack to the pack it was made from")
{
    const std::optional<std::string_view> v2 = MCTest::Option("pack-compare");
    const std::optional<std::string_view> v1 = MCTest::Option("pack-compare-with");

    if (!v2.has_value() || !v1.has_value())
    {
        return;
    }

    const std::filesystem::path markedRoot = MCFileSystem::FromUtf8(*v2);
    const std::filesystem::path todayRoot = MCFileSystem::FromUtf8(*v1);
    const std::shared_ptr<const MCZstd::Dictionaries> dictionaries = ReadPackDictionaries(markedRoot);
    std::cout << std::format("{}: {} {}\n", *v2, dictionaries != nullptr ? "read its" : "no", PackDictionariesName);
    PackComparison all;
    size_t archives = 0;
    const auto started = std::chrono::steady_clock::now();

    for (const auto& item : std::filesystem::recursive_directory_iterator(markedRoot))
    {
        if (!item.is_regular_file() || item.path().extension() != ".hda")
        {
            continue;
        }

        const std::filesystem::path relative = item.path().lexically_relative(markedRoot);
        const std::filesystem::path todayPath = todayRoot / relative;
        MCTest::Scope scope(relative.generic_string());

        if (!std::filesystem::exists(todayPath))
        {
            std::cout << std::format("{}: not in {}\n", relative.generic_string(), *v1);
            continue;
        }

        auto marked = MCFastFile::Create(MCFileSystem::ToUtf8(item.path()), dictionaries);

        if (!marked.has_value() || !(*marked)->IsVersion2())
        {
            std::cout << std::format("{}: {}\n", relative.generic_string(),
                                     marked.has_value() ? "not version 2" : marked.error());
            all.Failed += marked.has_value() ? 0 : 1;
            continue;
        }

        const PackComparison found = ComparePackArchive(todayPath, item.path(), dictionaries);
        std::string own;

        for (const auto& [type, data] : (*marked)->Facts({}))
        {
            own += std::format(" {} ({} bytes)", type, data.size());
        }

        std::cout << std::format("{}: {}; its own folder's facts:{}\n", relative.generic_string(), found.Text(),
                                 own.empty() ? " none" : own);
        all.Add(found);
        ++archives;
    }

    std::cout << std::format("{} archives, {:.1f} s: {}\n", archives,
                             std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
                             all.Text());
    CHECK(archives > 0);
    CHECK(all.Failed == 0);
    CHECK(all.ColoursDiffer == 0);
    CHECK(all.OtherDiffer == 0);
}

namespace
{
    /// <summary>A PAK (MCPacketFile's) holding <paramref name="packets"/> raw, in their order.</summary>
    std::vector<uint8_t> PacketFileBytes(const std::vector<std::vector<uint8_t>>& packets)
    {
        constexpr size_t HeadBytes = 8;
        constexpr size_t FirstPacketAt = 4;
        constexpr size_t EntryBytes = 4;
        const auto first = static_cast<uint32_t>(HeadBytes + EntryBytes * packets.size());
        std::vector<uint8_t> file(first);
        PutAt(file, 0, MCPacketFile::PacketFileMagic);
        PutAt(file, FirstPacketAt, first);

        for (size_t packet = 0; packet < packets.size(); ++packet)
        {
            // Each entry its packet's offset, stored raw (MCPacketStorage::Raw: 0 in its top bits).
            PutAt(file, HeadBytes + EntryBytes * packet, static_cast<uint32_t>(file.size()));
            file.insert(file.end(), packets[packet].begin(), packets[packet].end());
        }

        return file;
    }

    /// <summary>The files a test put (MCFileSystem::PutImage) and the guard taken away, however the test ends.</summary>
    class PutFiles
    {
    public:
        PutFiles() = default;
        PutFiles(const PutFiles&) = delete;
        PutFiles& operator=(const PutFiles&) = delete;

        ~PutFiles()
        {
            MCFileSystem::ClearPutImages();
            MCHdArt::GuardOriginalReads(false);
        }
    };

    /// <summary>How many reads of <paramref name="kind"/> the guard has noted.</summary>
    size_t GuardedCount(std::string_view kind)
    {
        const std::optional<std::vector<MCHdArt::GuardedRead>> reads = MCHdArt::GuardedReads();
        REQUIRE(reads.has_value());
        const auto found = std::ranges::find(*reads, kind, &MCHdArt::GuardedRead::Kind);
        return found == reads->end() ? 0 : found->Count;
    }

    /// <summary>The folder in <c>shapes.hda</c> of a shape file or PAK packet (MCHdArt::ShapeFileFolder's).</summary>
    std::string ShapesArchiveFolder(std::string_view gamePath, std::optional<int32_t> packet)
    {
        const std::filesystem::path folder = MCHdArt::ShapeFileFolder(gamePath, packet);
        std::string inArchive;

        for (auto part = std::next(folder.begin()); part != folder.end(); ++part)
        {
            inArchive += (inArchive.empty() ? "" : "\\") + part->generic_string();
        }

        return inArchive;
    }

    /// <summary>Shape file <paramref name="gamePath"/> read whole through MCShapeFile.</summary>
    std::vector<uint8_t> ReadThroughShapeFile(std::string_view gamePath)
    {
        MCShapeFile file;
        REQUIRE_EQ(file.Open(gamePath), NO_ERR);
        std::vector<uint8_t> bytes(file.FileSize());
        CHECK_EQ(file.Read(bytes), static_cast<int32_t>(bytes.size()));
        return bytes;
    }
} // namespace

/// <summary>
/// Port-only: with HD art's numbered folders drawn, a shape file's loader (MCShapeFile: the effects', markers', shadows'
/// and damage diagrams' files, the cursors' and sensor contacts' PAK packets) takes the pack's copy of the original's
/// bytes (shapes.hda's ShapeFileFolder, its shape table record whole), reading nothing of the game's; a file of another
/// size than the pack's (a mod's), a packet the pack lacks, or HD art off is read from the game's files, which the guard
/// notes. A table the loader read is found by its hash without a note; another table with one.
/// </summary>
TEST_CASE("hd art: a shape file's loader takes the pack's copy of the original, a mod's file from the game's files")
{
    constexpr std::string_view ShapePath = "data\\sprites\\MapsTest.shp";
    constexpr std::string_view ModPath = "data\\sprites\\mapsmod.shp";
    constexpr std::string_view PakPath = "data\\sprites\\MapsTest.pak";
    constexpr int32_t PackedPacket = 1;
    const std::vector<uint8_t> game = {1, 2, 3, 4, 5, 6};
    // The pack's copy, of the game's file's size, told apart by its bytes.
    const std::vector<uint8_t> pack = {9, 8, 7, 6, 5, 4};
    const std::vector<uint8_t> modded = {1, 2, 3, 4, 5, 6, 7};
    const std::vector<std::vector<uint8_t>> packets = {{10, 11, 12}, {20, 21, 22, 23}};
    const std::vector<uint8_t> packPacket = {30, 31, 32, 33};
    const std::vector<uint8_t> unloaded = {42, 43};
    const PutFiles put;
    MCFileSystem::PutImage(ShapePath, [&game] { return std::optional(game); });
    MCFileSystem::PutImage(ModPath, [&modded] { return std::optional(modded); });
    MCFileSystem::PutImage(PakPath, [&packets] { return std::optional(PacketFileBytes(packets)); });
    std::map<std::string, std::vector<uint8_t>> facts;
    facts[ShapesArchiveFolder(ShapePath, std::nullopt)] = FactsRecord(MCPackRecords::Type::ShapeTable, pack);
    facts[ShapesArchiveFolder(ModPath, std::nullopt)] = FactsRecord(MCPackRecords::Type::ShapeTable, pack);
    facts[ShapesArchiveFolder(PakPath, PackedPacket)] = FactsRecord(MCPackRecords::Type::ShapeTable, packPacket);
    const ScratchFolder scratch("shape-files");
    WriteBytes(scratch.Path() / "shapes.hda", BuildVersion2Archive({}, facts));
    REQUIRE(MCHdArt::WritePackFile(scratch.Path()).has_value());
    const PackOn on(scratch.Path());
    REQUIRE(MCHdArt::NumberedFolders());
    MCHdArt::GuardOriginalReads(true);
    REQUIRE(MCHdArt::GuardingOriginalReads());

    CHECK(ReadThroughShapeFile(ShapePath) == pack);
    CHECK_EQ(GuardedCount(MCShapeFile::FileRead), 0);
    CHECK(ReadThroughShapeFile(ModPath) == modded);
    CHECK_EQ(GuardedCount(MCShapeFile::FileRead), 1);

    MCPacketFile pak;
    REQUIRE_EQ(pak.Open(PakPath), NO_ERR);
    REQUIRE_EQ(pak.GetNumPackets(), static_cast<int32_t>(packets.size()));

    for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
    {
        MCTest::Scope scope(std::format("packet {}", packet));
        REQUIRE_EQ(pak.SeekPacket(packet), NO_ERR);
        std::vector<uint8_t> bytes(static_cast<size_t>(pak.GetPacketSize()));
        CHECK_EQ(MCShapeFile::ReadPacket(pak, packet, bytes), static_cast<int32_t>(bytes.size()));
        CHECK(bytes == (packet == PackedPacket ? packPacket : packets[static_cast<size_t>(packet)]));
    }

    CHECK_EQ(GuardedCount(MCShapeFile::PacketRead), 1);

    // Found by their hashes, as the renderer finds a table no folder is attached to.
    for (const std::vector<uint8_t>& loaded : {pack, modded, packPacket})
    {
        MCHdArt::FindShape(MCHdArt::ShapeTableHash(loaded), 0, 1, 1, 0, 1);
    }

    CHECK_EQ(GuardedCount(MCHdArt::ShapeHashRead), 0);
    MCHdArt::FindShape(MCHdArt::ShapeTableHash(unloaded), 0, 1, 1, 0, 1);
    CHECK_EQ(GuardedCount(MCHdArt::ShapeHashRead), 1);

    GHdArt = 0;
    CHECK(ReadThroughShapeFile(ShapePath) == game);
}

namespace
{
    /// <summary>
    /// An 8-bit TGA as the game's pictures are and their loaders read them: <paramref name="width"/> by
    /// <paramref name="height"/> at +0x0c and +0x0e, a palette of zeroes, then <paramref name="pixels"/> at 0x312.
    /// </summary>
    std::vector<uint8_t> PictureTga(int16_t width, int16_t height, std::span<const uint8_t> pixels)
    {
        constexpr size_t WidthAt = 0x0c;
        constexpr size_t HeightAt = 0x0e;
        constexpr size_t PixelsAt = 0x312;
        std::vector<uint8_t> tga(PixelsAt);
        std::memcpy(tga.data() + WidthAt, &width, sizeof(width));
        std::memcpy(tga.data() + HeightAt, &height, sizeof(height));
        tga.insert(tga.end(), pixels.begin(), pixels.end());
        return tga;
    }

    /// <summary>A port's pixels, copied.</summary>
    std::vector<uint8_t> PortPixels(const MCGuiPort& port)
    {
        const std::span<const uint8_t> pixels = port.Pixels();
        return {pixels.begin(), pixels.end()};
    }
} // namespace

/// <summary>
/// Port-only: with HD art's numbered folders drawn, a menu's picture (MCGuiPort::Init by name) and a logistics screen's
/// or briefing map's (MCLogPort::Load) take the pack's copy of the original's TGA (shapes.hda's ShapeFileFolder record,
/// its bytes whole), reading nothing of the game's; a picture the pack lacks is read from the game's files, which the
/// guard notes.
/// </summary>
TEST_CASE("hd art: a menu's and a logistics screen's pictures take the pack's copy of the original's TGA")
{
    constexpr std::string_view MenuName = "mapsmenu.tga";
    constexpr int16_t Width = 4;
    constexpr int16_t Height = 1;
    const std::vector<uint8_t> gamePixels = {1, 2, 3, 4};
    // The pack's copies, of the game's files' size, told apart by their pixels.
    const std::vector<uint8_t> packPixels = {5, 6, 7, 8};
    const std::string menuPath = std::format("{}{}", std::string_view(ArtPath), MenuName);
    const std::string logPath = std::format("{}logart\\mapslog.tga", std::string_view(ArtPath));
    const std::string unpackedPath = std::format("{}logart\\mapsnone.tga", std::string_view(ArtPath));
    const std::vector<uint8_t> game = PictureTga(Width, Height, gamePixels);
    const std::vector<uint8_t> pack = PictureTga(Width, Height, packPixels);
    const PutFiles put;

    for (const std::string& path : {menuPath, logPath, unpackedPath})
    {
        MCFileSystem::PutImage(path, [&game] { return std::optional(game); });
    }

    std::map<std::string, std::vector<uint8_t>> facts;
    facts[ShapesArchiveFolder(menuPath, std::nullopt)] = FactsRecord(MCPackRecords::Type::ShapeTable, pack);
    facts[ShapesArchiveFolder(logPath, std::nullopt)] = FactsRecord(MCPackRecords::Type::ShapeTable, pack);
    const ScratchFolder scratch("picture-files");
    WriteBytes(scratch.Path() / "shapes.hda", BuildVersion2Archive({}, facts));
    REQUIRE(MCHdArt::WritePackFile(scratch.Path()).has_value());
    const PackOn on(scratch.Path());
    REQUIRE(MCHdArt::NumberedFolders());
    MCHdArt::GuardOriginalReads(true);
    REQUIRE(MCHdArt::GuardingOriginalReads());

    MCGuiPort menu;
    REQUIRE_EQ(menu.Init(MenuName), 0);
    CHECK(PortPixels(menu) == packPixels);
    MCLogPort log;
    log.Load(logPath);
    CHECK(PortPixels(log) == packPixels);
    CHECK_EQ(GuardedCount(MCShapeFile::FileRead), 0);

    MCLogPort unpacked;
    unpacked.Load(unpackedPath);
    CHECK(PortPixels(unpacked) == gamePixels);
    CHECK_EQ(GuardedCount(MCShapeFile::FileRead), 1);
}

/// <summary>
/// Port-only: with HD art's numbered folders drawn, the craters' and footprints' PAK (a mission's CraterFile) is read
/// through MCShapeFile: each packet the pack's copy (shapes.hda's ShapeFileFolder record), reading nothing of the game's;
/// a packet the pack lacks is read from the PAK, which the guard notes; HD art off reads the PAK as before.
/// </summary>
TEST_CASE("hd art: the craters' and footprints' packets come from the pack's copy of their PAK")
{
    constexpr std::string_view CraterName = "MapsCrater";
    const std::string pakPath = GamePath(SpritePath, CraterName, ".pak");
    const std::vector<std::vector<uint8_t>> packets = {{10, 11, 12}, {20, 21, 22, 23}, {40, 41}};
    // The pack's copies of packets 0 and 1, of their sizes, told apart by their bytes; packet 2 the pack lacks.
    const std::vector<std::vector<uint8_t>> packCopies = {{50, 51, 52}, {60, 61, 62, 63}};
    constexpr int32_t NumCraters = 5;
    const PutFiles put;
    MCFileSystem::PutImage(pakPath, [&packets] { return std::optional(PacketFileBytes(packets)); });
    std::map<std::string, std::vector<uint8_t>> facts;

    for (size_t packet = 0; packet < packCopies.size(); ++packet)
    {
        facts[ShapesArchiveFolder(pakPath, static_cast<int32_t>(packet))] =
            FactsRecord(MCPackRecords::Type::ShapeTable, packCopies[packet]);
    }

    const ScratchFolder scratch("crater-files");
    WriteBytes(scratch.Path() / "shapes.hda", BuildVersion2Archive({}, facts));
    REQUIRE(MCHdArt::WritePackFile(scratch.Path()).has_value());
    const PackOn on(scratch.Path());
    REQUIRE(MCHdArt::NumberedFolders());
    MCHdArt::GuardOriginalReads(true);
    REQUIRE(MCHdArt::GuardingOriginalReads());

    {
        const std::expected<std::unique_ptr<MCCraterManager>, std::string> craters =
            MCCraterManager::Create(NumCraters, CraterName);
        REQUIRE(craters.has_value());
        REQUIRE_EQ((*craters)->ShapeCount(), static_cast<int32_t>(packets.size()));
        CHECK((*craters)->Shapes()[0] == packCopies[0]);
        CHECK((*craters)->Shapes()[1] == packCopies[1]);
        CHECK((*craters)->Shapes()[2] == packets[2]);
        CHECK_EQ(GuardedCount(MCShapeFile::PacketRead), 1);
    }

    GHdArt = 0;
    const std::expected<std::unique_ptr<MCCraterManager>, std::string> original =
        MCCraterManager::Create(NumCraters, CraterName);
    REQUIRE(original.has_value());

    for (size_t packet = 0; packet < packets.size(); ++packet)
    {
        MCTest::Scope scope(std::format("packet {}", packet));
        CHECK(original.value()->Shapes()[packet] == packets[packet]);
    }
}

/// <summary>
/// Tool: <c>--hd-guard-mission &lt;n&gt; --hd-guard-pack &lt;pack&gt;</c> starts mission <c>n</c> with HD art on and the
/// version 2 pack at <c>pack</c> from the boot (as a player's preferences have it), and the guard of test runs on
/// (MCHdArt::GuardOriginalReads, the app's <c>-hdguard</c>), plays <c>--hd-guard-frames &lt;n&gt;</c> frames (default
/// GuardMissionFrames) and lists each kind of original art the load and the play read, with its first examples; it
/// fails on any. One mission a run (the boot's rule): a survey of every mission runs it once a mission.
/// </summary>
TEST_CASE_ISOLATED("tool: --hd-guard-mission lists the original art a mission's load and play read with a version 2 "
                   "pack")
{
    const std::optional<std::string_view> mission = MCTest::Option("hd-guard-mission");
    const std::optional<std::string_view> pack = MCTest::Option("hd-guard-pack");

    if (!mission.has_value() || !pack.has_value() || !MCTestGame::Available())
    {
        return;
    }

    // Ten seconds at the game's 15 frames a second: the load, the first moves and the first shots' effects.
    constexpr int32_t GuardMissionFrames = 150;
    constexpr float GuardFrameSeconds = 1.0f / 15.0f;
    int32_t segment = 0;
    int32_t frames = GuardMissionFrames;
    REQUIRE(std::from_chars(mission->data(), mission->data() + mission->size(), segment).ec == std::errc());

    if (const std::optional<std::string_view> given = MCTest::Option("hd-guard-frames"); given.has_value())
    {
        REQUIRE(std::from_chars(given->data(), given->data() + given->size(), frames).ec == std::errc());
    }

    // The folder made absolute before the game starts: the game works in the install's folder. HD art on from the
    // boot, as the player's app has it (its personal defaults, the pack set before the menus' first loads), so nothing
    // the start loads is read before the pack is.
    const std::filesystem::path packFolder = std::filesystem::absolute(std::filesystem::path(*pack));
    UseHdByDefault();
    MCHdArt::SetPackFolder(packFolder);
    MCHdArt::GuardOriginalReads(true);
    MCTestGame::BootOptions.Started = [] { GHdArt = 1; };

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(segment));
    REQUIRE(MCHdArt::GuardingOriginalReads());
    const std::optional<std::vector<MCHdArt::GuardedRead>> loadReads = MCHdArt::GuardedReads();
    REQUIRE(loadReads.has_value());
    std::cout << std::format("  mission {}: {} reads of the original's art by the end of the load\n", segment,
                             std::ranges::fold_left(*loadReads | std::views::transform(&MCHdArt::GuardedRead::Count),
                                                    size_t{0}, std::plus<>()));

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        MCTestGame::RunFrame(GuardFrameSeconds);
    }

    REQUIRE(MCHdArt::GuardedReads().has_value());
    const size_t total = MCTestGame::PrintGuardedReads(std::format("mission {}", segment));
    std::cout << std::format("  mission {}: {} reads of the original's art in {} frames\n", segment, total, frames);
    MCHdArt::GuardOriginalReads(false);
    CHECK_EQ(total, size_t{0});
}

namespace
{
    /// <summary>The stem's end of a list of the pieces of the game folder's files a load read, beside its list.</summary>
    constexpr std::string_view PiecesListStem = ".files";

    /// <summary>
    /// Writes the pieces of the game folder's files the last load read (MCFilesAhead::NotedPieces) to
    /// <paramref name="out"/>/&lt;mission&gt;.files.txt, beside its list.
    /// </summary>
    /// <returns>How many.</returns>
    size_t WriteLoadPieces(const std::filesystem::path& out, std::string_view mission)
    {
        const std::vector<MCFilesAhead::MCFilePiece> pieces = MCFilesAhead::NotedPieces();
        std::ofstream(out / MCFileSystem::FromUtf8(std::format("{}{}.txt", mission, PiecesListStem)), std::ios::binary)
            << MCFilesAhead::PiecesText(pieces);
        return pieces.size();
    }
}

/// <summary>
/// Tool (SPEC-memory.md's "From the briefing": a mission's list): <c>--hold-list &lt;pack&gt;</c> loads mission
/// <c>--hold-list-mission &lt;n&gt;</c> (1 by default) with the pack and its hold on, as a player's load is, and writes the
/// pack's folders the load handed the hold (MCHdArt::LoadFolders), one a line in the order handed, to
/// <c>&lt;--hold-list-out&gt;/&lt;mission&gt;.txt</c> (the working folder by default): all but those of the player's mechs
/// that no other unit's tree shares, which a briefing reads as each is placed in the lance. A type's packets' folders
/// (<c>objects/shapes90/&lt;n&gt;/&lt;packet&gt;</c>) are listed once, by its packet 0's, as the game hands a type's art
/// ahead (MCAppearanceType's MCHdArt::OpenArchivesAhead): any of them opens the same archive. Beside it,
/// <c>&lt;mission&gt;.files.txt</c>: the pieces of the game folder's files the load read (MCFilesAhead::NotedPieces).
/// </summary>
TEST_CASE_ISOLATED(
    "tool: --hold-list writes the pack's folders a mission's load hands the hold, the player's mechs' out")
{
    const std::optional<std::string_view> pack = MCTest::Option("hold-list");

    if (!pack.has_value() || !MCTestGame::Available())
    {
        return;
    }

    int32_t segment = 1;

    if (const std::optional<std::string_view> asked = MCTest::Option("hold-list-mission"))
    {
        REQUIRE(std::from_chars(asked->data(), asked->data() + asked->size(), segment).ec == std::errc());
    }

    const std::filesystem::path out = MCFileSystem::FromUtf8(MCTest::Option("hold-list-out").value_or("."));
    MCTestGame::BootOptions.Started = [folder = MCFileSystem::FromUtf8(*pack)]
    {
        GHdArt = 1;
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.Hold = true;
        MCHdArt::SetReaderSettings(settings);
        MCHdArt::SetPackFolder(folder);
    };

    REQUIRE(MCTestGame::StartMission(segment));
    REQUIRE(MCHdArt::Active());
    const std::string mission = Mission()->Scenarios.at(static_cast<size_t>(Mission()->CurrentScenario));
    const std::shared_ptr<MCSpriteManager> sprites = SpriteManager();
    REQUIRE(sprites != nullptr);

    // The load's list as a player's game keeps it (MCScenario::HoldList): the player's mechs' folders left out.
    const MCScenario::MCHoldList list = Scenario()->HoldList(mission);
    std::string lines;

    for (const std::string& folder : list.Folders)
    {
        lines += folder + '\n';
    }

    std::filesystem::create_directories(out);
    const std::filesystem::path written = out / MCFileSystem::FromUtf8(mission + ".txt");
    std::ofstream(written, std::ios::binary) << lines;
    const size_t pieces = WriteLoadPieces(out, mission);
    std::cout << std::format(
        "  mission {} ({}): {} folders handed, {} of the player's mechs' left out, {} types' listed, {} files' "
        "pieces, to {}\n",
        segment, mission, MCHdArt::LoadFolders().size(), list.PlayersLeft, list.Folders.size(), pieces,
        MCFileSystem::ToUtf8(written));
    CHECK(!lines.empty());
    CHECK(pieces > 0);

    // With --hold-list-chassis, every mech chassis in the object file and its tree's folders (MechTreeFolders, as a
    // briefing works a lance's mech out as it's placed), a line each to <out>/chassis.txt, to check against.
    if (!MCTest::Option("hold-list-chassis").has_value())
    {
        return;
    }

    MCPacketFile objects;
    REQUIRE_EQ(objects.Open(GamePath(ObjectPath, ObjectPakName)), 0);
    std::string chassisLines;
    size_t chassisCount = 0;

    for (int32_t number = 1; number < objects.GetNumPackets(); ++number)
    {
        const std::vector<std::filesystem::path> folders = MechTreeFolders(static_cast<uint32_t>(number));

        if (folders.empty())
        {
            continue;
        }

        std::string row = std::to_string(number);

        for (const std::filesystem::path& folder : folders)
        {
            row += ' ' + folder.generic_string();
        }

        chassisLines += row + '\n';
        ++chassisCount;
    }

    std::ofstream(out / "chassis.txt", std::ios::binary) << chassisLines;
    std::cout << std::format("  {} mech chassis\n", chassisCount);
    CHECK(chassisCount > 0);
}

/// <summary>
/// Tool (SPEC-memory.md's manifest: multiplayer maps): <c>--hold-list-map &lt;pack&gt;</c> loads the mission of match
/// map <c>--hold-list-map-name &lt;map&gt;</c> (its <c>.mpk</c>'s name) with the pack and its hold on, as single player
/// loads a campaign's, and writes the pack's folders the load handed the hold as <c>--hold-list</c> does, to
/// <c>&lt;--hold-list-out&gt;/&lt;mission&gt;.txt</c> (the mission its file names), and its files' pieces beside it. A
/// match's players' mechs are no part of a list (the players' part is worked out as each is picked), so a load with none
/// leaves out nothing of the map's.
/// </summary>
TEST_CASE_ISOLATED("tool: --hold-list-map writes the pack's folders a match map's load hands the hold")
{
    const std::optional<std::string_view> pack = MCTest::Option("hold-list-map");
    const std::optional<std::string_view> map = MCTest::Option("hold-list-map-name");

    if (!pack.has_value() || !map.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path out = MCFileSystem::FromUtf8(MCTest::Option("hold-list-out").value_or("."));
    MCTestGame::BootOptions.Started = [folder = MCFileSystem::FromUtf8(*pack)]
    {
        GHdArt = 1;
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.Hold = true;
        MCHdArt::SetReaderSettings(settings);
        MCHdArt::SetPackFolder(folder);
    };

    // A match's file has none of the campaign's lists (its warriors, parts and place in the master mission file): their
    // reads assert, at its load and wherever the logistics' screens list a save of it.
    const MCTest::ExpectAssert campaignOnly("", MCTest::ExpectAssert::AnyCount);
    REQUIRE(MCTestGame::StartLogistics());

    // The map's mission: its file's first packet's.
    MCPacketFile packets;
    MCFitIniFile file;
    REQUIRE_EQ(packets.Open(GamePath(SavePath, *map, ".mpk")), 0);
    REQUIRE_EQ(packets.SeekPacket(0), 0);
    REQUIRE_EQ(file.Open(packets, static_cast<uint32_t>(packets.GetPacketSize())), 0);
    REQUIRE_EQ(file.SeekBlock("Multiplayer"), 0);
    const MCFitResult<std::string> named = file.Read<std::string>("MissionName");
    REQUIRE(named.has_value());
    const std::string mission = *named;
    file.Close();

    // Its load as a campaign mission's.
    Mission()->StartScenario(mission);
    REQUIRE(MCTestGuest::SettleUntil([] { return Scenario() != nullptr && Scenario()->StartingUp; }));

    REQUIRE(MCHdArt::Active());

    const MCScenario::MCHoldList list = Scenario()->HoldList(mission);
    std::string lines;

    for (const std::string& folder : list.Folders)
    {
        lines += folder + '\n';
    }

    std::filesystem::create_directories(out);
    const std::filesystem::path written = out / MCFileSystem::FromUtf8(mission + ".txt");
    std::ofstream(written, std::ios::binary) << lines;
    const size_t pieces = WriteLoadPieces(out, mission);
    std::cout << std::format("  map {} ({}): {} folders handed, {} listed, {} files' pieces, to {}\n", *map, mission,
                             MCHdArt::LoadFolders().size(), list.Folders.size(), pieces, MCFileSystem::ToUtf8(written));
    CHECK(!lines.empty());
    CHECK(pieces > 0);
}

/// <summary>
/// Tool (SPEC-memory.md's "From the briefing"): <c>--hold-lists-table &lt;folder&gt;</c> reads the lists
/// <c>--hold-list</c> wrote there (<c>&lt;mission&gt;.txt</c> each, and <c>chassis.txt</c>) and writes
/// sprite/MCHoldLists.h, the lists the program carries, to <c>--hold-lists-out &lt;file&gt;</c>
/// (<c>&lt;folder&gt;/MCHoldLists.h</c> by default): each folder once, each mission's as indices into them, the missions
/// by name; each mission's ground by name (<c>terrain/&lt;hash&gt;</c>, <c>overlays/&lt;hash&gt;</c>) as its tile set
/// and the slots of it named, a bit each (MCKnownGroundNames: the program's table); and the pieces of the game
/// folder's files its load read (<c>&lt;mission&gt;.files.txt</c>), each file named once.
/// </summary>
TEST_CASE("tool: --hold-lists-table writes the missions' hold lists as the table the program carries")
{
    const std::optional<std::string_view> in = MCTest::Option("hold-lists-table");

    if (!in.has_value())
    {
        return;
    }

    // --hold-list-chassis's check file, which the table leaves out: a mech's are worked out as it's picked.
    constexpr std::string_view ChassisFile = "chassis";
    constexpr size_t RowLength = 16;
    const std::filesystem::path folder = MCFileSystem::FromUtf8(*in);
    const std::filesystem::path out = MCTest::Option("hold-lists-out").has_value()
                                          ? MCFileSystem::FromUtf8(*MCTest::Option("hold-lists-out"))
                                          : folder / "MCHoldLists.h";
    std::map<std::string, std::vector<std::string>> missions;
    std::map<std::string, std::vector<MCFilesAhead::MCFilePiece>> missionPieces;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != ".txt")
        {
            continue;
        }

        std::string name = MCFileSystem::ToUtf8(entry.path().stem());
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (name == ChassisFile)
        {
            continue;
        }

        if (name.ends_with(PiecesListStem))
        {
            std::ifstream file(entry.path(), std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            missionPieces[name.substr(0, name.size() - PiecesListStem.size())] = MCFilesAhead::PiecesOfText(text);
            continue;
        }

        std::ifstream file(entry.path());

        for (std::string line; std::getline(file, line);)
        {
            if (!line.empty())
            {
                missions[name].push_back(line);
            }
        }
    }

    REQUIRE(!missions.empty());
    std::vector<std::string> folders;
    std::unordered_map<std::string, size_t> indices;
    std::string lists;

    // A mission's ground by name, as the tile set whose slots name it all and a bitset of those slots, each kind's.
    constexpr size_t BitsAWord = 64;
    constexpr size_t WordsARow = 4;
    const std::span<const MCKnownGroundNames> sets = MCGroundNamesInUse();
    const auto groundOf = [&](const std::string& name, std::vector<std::string>& lines)
    {
        std::map<std::string, std::vector<uint64_t>> asked;
        std::erase_if(lines,
                      [&asked](const std::string& line)
                      {
                          const size_t slash = line.find('/');
                          const std::string kind = line.substr(0, slash);

                          if (slash == std::string::npos ||
                              (kind != MCHdArt::TerrainFolder && kind != MCHdArt::OverlayFolder))
                          {
                              return false;
                          }

                          asked[kind].push_back(std::stoull(line.substr(slash + 1), nullptr, 16));
                          return true;
                      });

        if (asked.empty())
        {
            return std::string("0, {}, {}");
        }

        const auto slotsIn = [](std::span<const MCGroundNameRow> rows, std::span<const uint64_t> keys)
        {
            std::vector<int32_t> slots;

            for (const uint64_t key : keys)
            {
                if (const auto row = std::ranges::find(rows, key, &MCGroundNameRow::Key); row != rows.end())
                {
                    slots.push_back(row->Slot);
                }
            }

            return slots;
        };

        for (const MCKnownGroundNames& set : sets)
        {
            const std::vector<int32_t> tiles = slotsIn(set.Tiles, asked[std::string(MCHdArt::TerrainFolder)]);
            const std::vector<int32_t> overlays = slotsIn(set.Overlays, asked[std::string(MCHdArt::OverlayFolder)]);

            if (tiles.size() != asked[std::string(MCHdArt::TerrainFolder)].size() ||
                overlays.size() != asked[std::string(MCHdArt::OverlayFolder)].size())
            {
                continue;
            }

            const auto bits = [&](std::string identifier, const std::vector<int32_t>& slots)
            {
                std::ranges::replace_if(identifier, [](unsigned char c) { return std::isalnum(c) == 0; }, '_');
                std::vector<uint64_t> words((static_cast<size_t>(set.Slots) + BitsAWord - 1) / BitsAWord);

                for (const int32_t slot : slots)
                {
                    words[static_cast<size_t>(slot) / BitsAWord] |= uint64_t{1}
                                                                    << (static_cast<size_t>(slot) % BitsAWord);
                }

                std::string numbers;

                for (size_t at = 0; at < words.size(); ++at)
                {
                    numbers += std::format("{}0x{:016x},", at % WordsARow == 0 ? "\n    " : " ", words[at]);
                }

                lists += std::format("inline constexpr std::array<uint64_t, {}> {}{{{{{}\n}}}};\n", words.size(),
                                     identifier, numbers);
                return identifier;
            };

            return std::format("0x{:016x}, {}, {}", set.FilesHash, bits("Tiles_" + name, tiles),
                               bits("Overlays_" + name, overlays));
        }

        FAIL_CHECK(std::format("{}: no tile set the program knows names all its ground", name));
        return std::string("0, {}, {}");
    };

    // An array of indices into Folders named <identifier>, of <lines>' folders (each added to Folders the first time).
    const auto listOf = [&](std::string identifier, const std::vector<std::string>& lines)
    {
        std::ranges::replace_if(identifier, [](unsigned char c) { return std::isalnum(c) == 0; }, '_');
        std::string numbers;

        for (size_t at = 0; at < lines.size(); ++at)
        {
            const auto [found, fresh] = indices.try_emplace(lines[at], folders.size());

            if (fresh)
            {
                folders.push_back(lines[at]);
            }

            numbers += std::format("{}{},", at % RowLength == 0 ? "\n    " : " ", found->second);
        }

        lists += std::format("inline constexpr std::array<uint16_t, {}> {}{{{{{}\n}}}};\n", lines.size(), identifier,
                             numbers);
        return identifier;
    };

    // An array of the mission's pieces named Pieces_<name>, each file an index into Files (added the first time).
    std::vector<std::string> files;
    std::unordered_map<std::string, size_t> fileIndices;
    size_t pieceCount = 0;
    const auto piecesOf = [&](const std::string& name)
    {
        const auto found = missionPieces.find(name);

        if (found == missionPieces.end() || found->second.empty())
        {
            return std::string("{}");
        }

        std::string identifier = "Pieces_" + name;
        std::ranges::replace_if(identifier, [](unsigned char c) { return std::isalnum(c) == 0; }, '_');
        std::string rows;

        for (const MCFilesAhead::MCFilePiece& piece : found->second)
        {
            const auto [file, fresh] = fileIndices.try_emplace(piece.File, files.size());

            if (fresh)
            {
                files.push_back(piece.File);
            }

            REQUIRE(piece.Offset + piece.Bytes <= std::numeric_limits<uint32_t>::max());
            ++pieceCount;
            rows += std::format("\n    {{{}, {}, {}}},", file->second, piece.Offset, piece.Bytes);
        }

        lists += std::format("inline constexpr std::array<Piece, {}> {}{{{{{}\n}}}};\n", found->second.size(),
                             identifier, rows);
        return identifier;
    };

    std::string missionRows;

    for (auto& [name, lines] : missions)
    {
        const std::string ground = groundOf(name, lines);
        const std::string folderList = listOf("Mission_" + name, lines);
        missionRows += std::format("    {{\"{}\", {}, {}, {}}},\n", name, folderList, ground, piecesOf(name));
    }

    REQUIRE(files.size() <= std::numeric_limits<uint16_t>::max());
    std::string fileNames;

    for (const std::string& name : files)
    {
        fileNames += std::format("    \"{}\",\n", name);
    }

    REQUIRE(folders.size() <= std::numeric_limits<uint16_t>::max());
    std::string names;

    for (const std::string& name : folders)
    {
        names += std::format("    \"{}\",\n", name);
    }

    std::ofstream(out, std::ios::binary) << std::format(
        "#pragma once\n\n"
        "// Port-only (SPEC-memory.md's \"From the briefing\"): the HD pack's folders each mission's load hands the "
        "hold,\n"
        "// and the ground's frames it asks for, which its briefing reads ahead (MCHdArt::BriefingFolders), and the "
        "pieces of\n"
        "// the game folder's files it reads (MCHdArt::MissionFilePieces); a lance's mechs' are worked out as each is "
        "picked\n"
        "// (MechTreeFolders). Worked out by loading each mission with mc_tests --hold-list (a match map's with\n"
        "// --hold-list-map) and written by mc_tests --hold-lists-table (tests/HdPackTests.cpp). Don't edit it.\n\n"
        "#include <array>\n#include <cstdint>\n#include <span>\n#include <string_view>\n\n"
        "// clang-format off\n"
        "namespace MCHoldLists\n{{\n"
        "/// <summary>A piece of a file of the game folder's a load reads: the file, as an index into Files, and "
        "where its\n"
        "/// bytes lie.</summary>\n"
        "struct Piece\n{{\n    uint16_t File = 0;\n    uint32_t Offset = 0;\n    uint32_t Bytes = 0;\n}};\n\n"
        "/// <summary>\n"
        "/// A mission (its file's name, lower case), its folders, as indices into Folders, the ground its load "
        "asks for:\n"
        "/// its tile set (MCKnownGroundNames::FilesHash; 0: none) and the slots of it asked for as ground tiles and "
        "as\n"
        "/// overlays, a bit each, from slot 0's lowest; and the pieces of the game folder's files it reads.\n"
        "/// </summary>\n"
        "struct Mission\n{{\n    std::string_view Name;\n    std::span<const uint16_t> Folders;\n"
        "    uint64_t GroundSet = 0;\n    std::span<const uint64_t> Tiles;\n    std::span<const uint64_t> "
        "Overlays;\n    std::span<const Piece> Pieces;\n}};\n\n"
        "inline constexpr std::array<std::string_view, {}> Folders{{{{\n{}}}}};\n\n"
        "inline constexpr std::array<std::string_view, {}> Files{{{{\n{}}}}};\n\n{}\n"
        "inline constexpr std::array<Mission, {}> Missions{{{{\n{}}}}};\n}}\n\n// clang-format on\n",
        folders.size(), names, files.size(), fileNames, lists, missions.size(), missionRows);
    std::cout << std::format("  {} missions, {} folders, {} files' pieces of {} files, to {}\n", missions.size(),
                             folders.size(), pieceCount, files.size(), MCFileSystem::ToUtf8(out));
}
