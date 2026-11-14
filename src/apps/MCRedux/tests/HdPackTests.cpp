#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "MCTestShapes.h"
#include "TestGame.h"
#include "fakes/MCFakeMemorySystem.h"
#include "lib/MCFastFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCInflate.h"
#include "lib/MCLz.h"
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

    MCHdArt::Prepare({"legs90", 5, 0});
    CHECK_EQ(MCHdArt::ArchivesOpen(), 1);
    MCHdArt::Prepare({"legs90", 5, 3});
    MCHdArt::Prepare({"legs90", 6, 0});
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

    for (uint32_t n = 0; n < Mechs; ++n)
    {
        MCHdArt::Prepare({"legs90", n, 0});
    }

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

    CHECK(MCHdArt::FindTerrainTile(tiles[2], TileWidth, TileHeight) == nullptr);
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
    /// <paramref name="key"/>'s frame file in a read-ahead pack made with <paramref name="seed"/>: BC7 blocks of bytes
    /// no other key's (or seed's) has, and indices.
    /// </summary>
    std::vector<uint8_t> AheadFile(const AheadKey& key, uint8_t seed)
    {
        constexpr int32_t KeyFrameMix = 31;
        constexpr int32_t KeyStepMix = 3;
        constexpr int32_t ByteMix = 7;
        MCHdArt::Frame frame;
        frame.Scale = AheadScale;
        frame.Width = AheadWidth * AheadScale;
        frame.Height = AheadHeight * AheadScale;
        frame.Format = MCHdArt::ColorFormat::Bc7;
        frame.Colors.resize(static_cast<size_t>(MCBc7::Padded(frame.Width) / MCBc7::BlockSide) *
                            static_cast<size_t>(MCBc7::Padded(frame.Height) / MCBc7::BlockSide) * MCBc7::BlockBytes);
        const int32_t mix = key.first * KeyFrameMix + key.second * KeyStepMix + seed;

        for (size_t at = 0; at < frame.Colors.size(); ++at)
        {
            frame.Colors[at] = static_cast<uint8_t>(static_cast<int32_t>(at) * ByteMix + mix);
        }

        frame.Indices.resize(static_cast<size_t>(AheadWidth) * AheadHeight * MCHdArt::IndexBytes);

        for (size_t at = 0; at < frame.Indices.size(); at += MCHdArt::IndexBytes)
        {
            frame.Indices[at] = static_cast<uint8_t>(mix);
            frame.Indices[at + 1] = MCHdArt::HasIndex;
        }

        return MCHdArt::EncodeHdf(frame);
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
}

/// <summary>
/// Port-only: once a numbered folder's frame is drawn the first time, the reading thread reads the frames drawn after it
/// (the rest of its in-betweens, then the next frame's, looping round; a turn middle's likewise), and they are drawn as
/// the game's thread would have read them: the same frames, refused at a size they weren't made for, one dropped for
/// room and read ahead again keeping its Id. Nothing is read twice; the reading thread opens its own handle on the
/// archive's file once for all its reads of it, and setting the pack closes it.
/// </summary>
TEST_CASE("hd art: the frames drawn after one are read ahead, the same as read when drawn")
{
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
    // The reading thread opened its own handle on the archive once, and keeps it beside the game's.
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
    // both handles, and the reading thread still opened its own once.
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
        file.WriteIdLong("Format", 2);
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
