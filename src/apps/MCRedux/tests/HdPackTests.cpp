#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "MCTestShapes.h"
#include "TestGame.h"
#include "fakes/MCFakeMemorySystem.h"
#include "gui/MCGuiStartup.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCInflate.h"
#include "lib/MCLz.h"
#include "lib/MCZstd.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteTree.h"
#include "platform/MCFileSystem.h"
#include "platform/MCRegisteredBlock.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCUtf8Path.h"
#include "sound/MCHdAudio.h"

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

    /// <summary>A version 2 archive's stream to write (BuildVersion2Streams): its entries, and its kind.</summary>
    struct Version2Stream
    {
        std::vector<ArchiveEntry> Entries;
        MCFastFile::StreamKind Kind = MCFastFile::StreamKind::Packet;
    };

    /// <summary>
    /// A version 2 HD archive (SPEC-pack-v2.md) of <paramref name="streams"/>, each stored as it is (codec 0), in their
    /// order, and its entries (their stored bytes as they unpack, no sharing) in theirs: each found by its path's key,
    /// each folder its first name is in by its own, with no facts.
    /// </summary>
    std::vector<uint8_t> BuildVersion2Streams(const std::vector<Version2Stream>& streams)
    {
        constexpr size_t HeaderBytes = 64;
        constexpr size_t FolderRowBytes = 16;
        constexpr size_t StreamRowBytes = 24;
        constexpr size_t StreamKindAt = 17;
        constexpr size_t EntryRowBytes = 20;
        constexpr uint16_t Format = 2;
        std::vector<std::pair<uint64_t, std::array<uint32_t, 3>>> rows;
        std::set<uint64_t> folders;
        std::vector<uint8_t> data;
        // Each stream's start in the data.
        std::vector<size_t> starts;

        for (uint32_t number = 0; number < streams.size(); ++number)
        {
            starts.push_back(data.size());

            for (const ArchiveEntry& entry : streams[number].Entries)
            {
                rows.push_back({MCFastFile::PathKey(entry.Name),
                                {number, static_cast<uint32_t>(data.size() - starts.back()),
                                 static_cast<uint32_t>(entry.Stored.size())}});
                data.insert(data.end(), entry.Stored.begin(), entry.Stored.end());

                if (const size_t cut = entry.Name.find('\\'); cut != std::string::npos)
                {
                    folders.insert(MCFastFile::PathKey(std::string_view(entry.Name).substr(0, cut)));
                }
            }
        }

        std::ranges::sort(rows);
        std::vector<uint8_t> table(folders.size() * FolderRowBytes + streams.size() * StreamRowBytes +
                                   rows.size() * EntryRowBytes);
        size_t at = 0;

        for (const uint64_t folder : folders)
        {
            PutAt(table, at, folder);
            at += FolderRowBytes;
        }

        const uint64_t dataAt = HeaderBytes + table.size();

        for (size_t number = 0; number < streams.size(); ++number)
        {
            const size_t end = number + 1 < streams.size() ? starts[number + 1] : data.size();
            const auto bytes = static_cast<uint32_t>(end - starts[number]);
            PutAt(table, at, dataAt + starts[number]);
            PutAt(table, at + 8, bytes);
            PutAt(table, at + 12, bytes);
            PutAt(table, at + StreamKindAt, std::to_underlying(streams[number].Kind));
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
        PutAt(out, 40, dataAt);
        PutAt(out, 48, static_cast<uint64_t>(XXH3_64bits(table.data(), table.size())));
        out.insert(out.end(), table.begin(), table.end());
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    /// <summary>
    /// A version 2 HD archive (SPEC-pack-v2.md) of <paramref name="entries"/> (their stored bytes as they unpack, no
    /// sharing), all in one packet's stream stored as it is (codec 0) in their order (BuildVersion2Streams).
    /// </summary>
    std::vector<uint8_t> BuildVersion2Archive(const std::vector<ArchiveEntry>& entries)
    {
        return BuildVersion2Streams({Version2Stream{entries, MCFastFile::StreamKind::Packet}});
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
/// stays good), while another frame has another.
/// </summary>
TEST_CASE("hd art: a frame read again keeps its Id")
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

    // Room for one frame: each one read drops the other.
    MCHdArt::SetCacheBudget(1);
    const std::shared_ptr<const MCHdArt::Frame> first = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(first != nullptr);
    const uint64_t firstId = first->Id;
    const std::shared_ptr<const MCHdArt::Frame> second = MCHdArt::FindTerrainTile(tiles[1], Width, Height);
    REQUIRE(second != nullptr);
    CHECK(second->Id != firstId);
    const size_t read = MCHdArt::FramesRead();
    const std::shared_ptr<const MCHdArt::Frame> again = MCHdArt::FindTerrainTile(tiles[0], Width, Height);
    REQUIRE(again != nullptr);
    CHECK_EQ(MCHdArt::FramesRead(), read + 1);
    CHECK(again != first);
    CHECK_EQ(again->Id, firstId);

    MCHdArt::SetCacheBudget(0);
    GHdArt = previous;
    MCHdArt::SetPackFolder({});
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

    // Short: the held frames leave the cache, freeing nothing, and 16 only it held go besides them; then 16 a frame, then
    // only those drawn, however long it lasts.
    system->IsShort = true;
    CHECK_EQ(frameDrawn(), (Frames - Held - GoneAFrame) * frameBytes);
    held.clear();
    CHECK_EQ(frameDrawn(), (Frames - Held - 2 * GoneAFrame) * frameBytes);
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

    MCHdArt::DropGround();
    CHECK_EQ(MCHdArt::CachedBytes(), both - ground->Bytes());
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
    /// Writes the read-ahead pack made with <paramref name="seed"/> into <paramref name="pack"/>: the table's folder as
    /// an archive, the even frames' entries packed (DEFLATE), the odd ones' stored.
    /// </summary>
    void WriteAheadPack(const std::filesystem::path& pack, uint8_t seed)
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

        const std::filesystem::path folder = pack / MCHdArt::ShapeTableFolder(AheadTable);
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

    /// <summary>Whether two frames are the same but for their Ids.</summary>
    bool SameFrame(const MCHdArt::Frame& a, const MCHdArt::Frame& b)
    {
        return a.Width == b.Width && a.Height == b.Height && a.Scale == b.Scale && a.Format == b.Format &&
               a.Blended == b.Blended && a.PaintedShadow == b.PaintedShadow && a.Colors == b.Colors &&
               a.Indices == b.Indices;
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

    // Drawn first, with the pack set again, frame 1 is read as drawn: the frame read ahead. Setting the pack closed
    // both handles, and the reader still opened its own once.
    MCHdArt::SetPackFolder(scratch.Path());
    CHECK_EQ(MCHdArt::ArchiveFilesOpen(), size_t{0});
    CHECK_EQ(MCHdArt::ReaderFilesOpened(), opened + 1);
    const size_t fresh = MCHdArt::FramesReadAhead();
    const std::shared_ptr<const MCHdArt::Frame> first = drawn({1, 0});
    CHECK_EQ(MCHdArt::FramesReadAhead(), fresh);
    CHECK(SameFrame(*first, *again));

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
/// skips (an empty frame of the original's), and each is found at its draw without a read.
/// </summary>
TEST_CASE("hd art: a packet read ahead without its count reads every frame its archive holds, past a number skipped")
{
    constexpr int32_t Skipped = 2;
    const ScratchFolder scratch("packet-ahead-count");
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
    WriteBytes(std::filesystem::path(scratch.Path() / folder).concat(".hda"), BuildArchive(entries));
    const int previous = GHdArt;
    GHdArt = 1;
    MCHdArt::SetPackFolder(scratch.Path());

    const size_t ahead = MCHdArt::FramesReadAhead();
    MCHdArt::ReadPacketAhead(folder);
    MCHdArt::WaitForReadAhead();
    MCHdArt::KeepReadAhead();
    CHECK_EQ(MCHdArt::FramesReadAhead(), ahead + static_cast<size_t>((AheadFrames - 1) * AheadSteps));
    const size_t read = MCHdArt::FramesRead();

    for (int32_t frame = 0; frame < AheadFrames; ++frame)
    {
        for (int32_t step = 0; step < AheadSteps && frame != Skipped; ++step)
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
/// Port-only (SPEC-pack-v2.md's Memory): the machine's memory decides the hold (MCHdArt::HoldWanted): off below 16 GB's
/// tier as the system reports it (MCHdArt::HoldLeastMemory), on from it; the command line's <c>-hdhold on|off</c> (the
/// tests' and benchmarks') overrides it, and a value it can't take leaves it as it was.
/// </summary>
TEST_CASE("hd art: the hold is off below 16 GB and with -hdhold off, on from 16 GB and with -hdhold on")
{
    constexpr uint64_t EightGb = uint64_t{8} << 30;
    constexpr uint64_t ThirtyTwoGb = uint64_t{32} << 30;
    constexpr uint64_t Mib = uint64_t{1} << 20;
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
        bool Held = false;
    };

    for (const Case& test : {Case{"8 GB", EightGb, {}, false},
                             Case{"a MB short of 16 GB's tier", MCHdArt::HoldLeastMemory - Mib, {}, false},
                             Case{"16 GB's tier", MCHdArt::HoldLeastMemory, {}, true},
                             Case{"32 GB with -hdhold off", ThirtyTwoGb, "-hdhold off", false},
                             Case{"8 GB with -hdhold on", EightGb, "-hdhold ON", true}})
    {
        MCTest::Scope scope{std::string(test.Name)};
        system->Total = test.Total;
        MCHdArt::SetReaderSettings({.Readers = 1});

        if (!test.Line.empty())
        {
            ParseCommandLine(test.Line);
        }

        CHECK_EQ(MCHdArt::HoldWanted(), test.Held);
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
/// holds nothing more that mission (an archive opened after isn't held) until the next mission's starts it again; at a
/// mission's end (MCHdArt::LetGoHold, MCMission::EndScenario's); and when the pack is set.
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
        MCTest::Scope scope("the pack set");
        MCHdArt::SetPackFolder(pack.Folder());
        CHECK_EQ(MCHdArt::HeldBytes(), 0u);
    }

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
/// Port-only (SPEC-first-use.md): the command line sets how HD art's readers read (<c>-hdreaders</c>,
/// <c>-hdsetreads</c>, <c>-hdreaderpriority</c>), the readers held to 1 to MCHdArt::MostReaders; a value a switch can't
/// take leaves its setting as it was.
/// </summary>
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
    /// Every entry of today's archive <paramref name="todayPath"/> held to the version 2 one made from it.
    /// </summary>
    auto ComparePackArchive(const std::filesystem::path& todayPath, const std::filesystem::path& markedPath)
        -> PackComparison
    {
        PackComparison found;
        auto today = MCFastFile::Create(MCFileSystem::ToUtf8(todayPath));
        auto marked = MCFastFile::Create(MCFileSystem::ToUtf8(markedPath));
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
        const std::span<const MCFileEntry> streams = (*marked)->GetEntries();
        std::ranges::sort(
            pairs, [&](const auto& a, const auto& b)
            { return streams[static_cast<size_t>(a.first)].Offset < streams[static_cast<size_t>(b.first)].Offset; });
        int32_t storedAt = -1;
        std::vector<uint8_t> stored;

        for (const auto& [j, i] : pairs)
        {
            if (streams[static_cast<size_t>(j)].Offset != storedAt)
            {
                storedAt = streams[static_cast<size_t>(j)].Offset;
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
/// differs an archive.
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

        auto marked = MCFastFile::Create(MCFileSystem::ToUtf8(item.path()));

        if (!marked.has_value() || !(*marked)->IsVersion2())
        {
            std::cout << std::format("{}: {}\n", relative.generic_string(),
                                     marked.has_value() ? "not version 2" : marked.error());
            continue;
        }

        const PackComparison found = ComparePackArchive(todayPath, item.path());
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
