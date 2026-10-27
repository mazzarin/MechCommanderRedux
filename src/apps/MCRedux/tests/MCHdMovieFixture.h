#pragma once

// A small HD movie for the tests: SVT-AV1 4.2.0 (preset 8, CRF 20, BT.709 limited range), three 32x24 frames of red,
// green and blue in IVF, as tools/seedvr2 writes the pack's movies.

namespace MCHdMovieFixture
{
    inline constexpr int32_t Width = 32;
    inline constexpr int32_t Height = 24;
    inline constexpr uint32_t Frames = 3;
    /// <summary>Each frame's colour as dav1d and SDL decode it (red, green, blue), within <see cref="Tolerance"/>.</summary>
    inline constexpr std::array<std::array<uint8_t, 3>, Frames> Colors{{{252, 0, 0}, {0, 254, 0}, {0, 0, 251}}};
    inline constexpr int Tolerance = 8;

    inline std::vector<uint8_t> Bytes()
    {
        const std::string_view hex =
            "444b49460000200041563031200018000f00000001000000030000000000000029000000000000000000000012000a0d02000005"
            "513f71abe4408080413216100087000000000100000bef7528c0d6bd6ef8edad803b00000001000000000000001200321b280200"
            "4000003d1c0030c1040000300015ed1f606d66044d37107d321a3002000000497aa000000200030000165f4b547b2f49bf0902a0"
            "05000000020000000000000012001a0198";
        std::vector<uint8_t> bytes;

        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
        }

        return bytes;
    }

    /// <summary>Writes the movie, or <paramref name="bytes"/>, to <paramref name="path"/>.</summary>
    inline void Write(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}
