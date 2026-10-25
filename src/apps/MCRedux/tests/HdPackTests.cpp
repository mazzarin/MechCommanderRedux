#include "stdafx.h"
#include "MCTest.h"
#include "lib/MCFastFile.h"
#include "lib/MCInflate.h"
#include "lib/MCLz.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "platform/MCFileSystem.h"

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
}

/// <summary>
/// BC7 (mode 6) round trip: a sprite-like picture comes back above the PSNR floor, and a block of one colour, or of
/// transparent black (frame padding), comes back exactly.
/// </summary>
TEST_CASE("hd art: BC7 blocks decode close to the pixels encoded, one colour exactly")
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
        CHECK(back == pixels);
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
    CHECK(Psnr(frame.Colors, MCBc7::Decode(read->Colors, read->Width, read->Height)) >= MinimumPsnr);

    CHECK(MCHdArt::DecodeHdf(file, 0, 0).has_value());
    CHECK(!MCHdArt::DecodeHdf(file, Width + 1, Height).has_value());
    CHECK(!MCHdArt::DecodeHdf(std::span(file).first(file.size() - 1), Width, Height).has_value());
    std::vector<uint8_t> wrong = file;
    wrong[0] = 'X';
    CHECK(!MCHdArt::DecodeHdf(wrong, Width, Height).has_value());
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

    /// <summary><paramref name="data"/> as one stored DEFLATE block (a packed entry that packs nothing).</summary>
    std::vector<uint8_t> StoredDeflate(std::span<const uint8_t> data)
    {
        const auto length = static_cast<uint16_t>(data.size());
        std::vector<uint8_t> stream = {1, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
                                       static_cast<uint8_t>(~length), static_cast<uint8_t>(~length >> 8)};
        stream.insert(stream.end(), data.begin(), data.end());
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
/// Tool: <c>--hd-compress &lt;folder&gt;</c> turns every PNG frame pair under a pack folder (<c>n.png</c> and
/// <c>n_idx.png</c>, at <c>--hd-scale</c> times the original, 3 unless given) into a frame file <c>n.hdf</c>, using every
/// core; the PNGs go unless <c>--hd-keep-png 1</c>. It prints the frames, the bytes before and after, and the colours' mean
/// PSNR. A pair that can't be converted is reported and kept.
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

            const std::vector<uint8_t> file = MCHdArt::EncodeHdf(*frame);
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
    std::cout << std::format("  {} frames at {}x: {:.1f} MB of PNG became {:.1f} MB, mean PSNR {:.1f} dB, {:.0f} s\n",
                             done.load(), scale, static_cast<double>(before.load()) / 1e6,
                             static_cast<double>(after.load()) / 1e6, psnrFrames > 0 ? psnrTotal / psnrFrames : 0.0,
                             seconds);

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
