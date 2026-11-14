#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSmacker.h"

// The Smacker decoder against the game's own movies: what each header says, and every frame and sound sample.

namespace
{
    /// <summary>The fields of a Smacker file's header the decoder reports, read straight from the file's bytes.</summary>
    struct MCSmkHeader
    {
        std::string Signature;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t Frames = 0;
        double FramesPerSecond = 0.0;
        uint32_t Flags = 0;
        std::array<uint32_t, MCSmacker::MaxTracks> AudioRates{};
    };

    /// <summary>The little-endian 32-bit value at <paramref name="at"/> in <paramref name="bytes"/>.</summary>
    uint32_t ReadU32(std::span<const uint8_t> bytes, size_t at)
    {
        return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    /// <summary>
    /// Reads the header as RAD's format lays it out: the signature, width, height and frame count, then the frame
    /// rate (positive: milliseconds per frame; negative: tens of microseconds; zero: 10 fps), the flags, and from
    /// 0x48 a rate word per audio track.
    /// </summary>
    std::optional<MCSmkHeader> ReadHeader(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::array<uint8_t, 0x68> bytes{};

        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        {
            return std::nullopt;
        }

        MCSmkHeader header;
        header.Signature.assign(reinterpret_cast<const char*>(bytes.data()), 4);
        header.Width = ReadU32(bytes, 0x04);
        header.Height = ReadU32(bytes, 0x08);
        header.Frames = ReadU32(bytes, 0x0c);
        const int32_t rate = static_cast<int32_t>(ReadU32(bytes, 0x10));
        const double microseconds = rate > 0 ? rate * 1000.0 : (rate < 0 ? -rate * 10.0 : 100000.0);
        header.FramesPerSecond = 1000000.0 / microseconds;
        header.Flags = ReadU32(bytes, 0x14);

        for (size_t t = 0; t < header.AudioRates.size(); ++t)
        {
            header.AudioRates[t] = ReadU32(bytes, 0x48 + t * 4);
        }

        return header;
    }
}

TEST_CASE("game: every movie decodes to its header's size, length and sound")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::vector<std::string> movies = MCFileSystem::FindFiles("data\\movies\\*.smk");
    REQUIRE(!movies.empty());

    for (const std::string& name : movies)
    {
        MCTest::Scope scope(name);
        const std::filesystem::path path = MCFileSystem::Resolve("data\\movies\\" + name);
        const std::optional<MCSmkHeader> header = ReadHeader(path);
        REQUIRE(header.has_value());

        auto opened = MCSmacker::Open(path);
        REQUIRE(opened.has_value());
        MCSmacker& smacker = **opened;

        CHECK_EQ(std::format("SMK{}", smacker.Version()), header->Signature);
        CHECK_EQ(smacker.Width(), static_cast<int>(header->Width));
        CHECK_EQ(smacker.StoredHeight(), static_cast<int>(header->Height));
        CHECK_EQ(smacker.FrameCount(), header->Frames);
        CHECK_EQ(smacker.Flags(), header->Flags);
        CHECK(std::abs(smacker.FramesPerSecond() - header->FramesPerSecond) < 1e-9);

        // An interlaced or line-doubled movie is shown at twice the rows it stores.
        const bool doubled = (header->Flags & (MCSmacker::FlagYInterlaced | MCSmacker::FlagYDoubled)) != 0;
        CHECK_EQ(smacker.Height(), static_cast<int>(header->Height) * (doubled ? 2 : 1));

        // Each rate word: bit 30 = the track has data, bits 0-23 = samples per second.
        int audioTrack = -1;

        for (int t = 0; t < MCSmacker::MaxTracks; ++t)
        {
            const uint32_t rate = header->AudioRates[static_cast<size_t>(t)];
            const MCSmacker::MCTrackInfo& track = smacker.Track(t);
            CHECK_EQ(track.Present, (rate & 0x40000000) != 0);

            if (track.Present)
            {
                CHECK_EQ(track.Rate, rate & 0x00ffffff);

                if (audioTrack < 0)
                {
                    audioTrack = t;
                }
            }
        }

        size_t audioBytes = 0;

        for (uint32_t frame = 0; frame < smacker.FrameCount(); ++frame)
        {
            const auto decoded = smacker.DecodeNextFrame();

            if (!decoded)
            {
                CHECK(decoded.has_value());
                std::cerr << std::format("  frame {}: {}\n", frame, decoded.error());
                break;
            }

            if (audioTrack >= 0)
            {
                audioBytes += smacker.Audio(audioTrack).size();
            }
        }

        // The game's movies carry their soundtrack for exactly their running time, so the decoded sound must last as
        // long as the pictures, to within one frame.
        if (audioTrack >= 0)
        {
            const MCSmacker::MCTrackInfo& track = smacker.Track(audioTrack);
            const double audioSeconds =
                static_cast<double>(audioBytes) / (track.Rate * static_cast<double>(track.BlockAlign()));
            const double videoSeconds = smacker.FrameCount() / smacker.FramesPerSecond();
            CHECK(std::abs(audioSeconds - videoSeconds) < 1.0 / smacker.FramesPerSecond());
        }
    }
}

/// <summary>
/// Port-only: a movie read from its file (a frame at a time through its disk file's growing buffer) decodes the same,
/// frame for frame and sample for sample, as from its bytes in memory: the game's largest movie.
/// </summary>
TEST_CASE("game: a movie decodes the same read from its file as from memory")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::vector<std::string> movies = MCFileSystem::FindFiles("data\\movies\\*.smk");
    REQUIRE(!movies.empty());
    std::filesystem::path largest;
    uintmax_t largestBytes = 0;

    for (const std::string& name : movies)
    {
        const std::filesystem::path path = MCFileSystem::Resolve("data\\movies\\" + name);
        std::error_code error;

        if (const uintmax_t bytes = std::filesystem::file_size(path, error); !error && bytes > largestBytes)
        {
            largest = path;
            largestBytes = bytes;
        }
    }

    MCTest::Scope scope(MCFileSystem::ToUtf8(largest.filename()));
    std::ifstream in(largest, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE_EQ(bytes.size(), static_cast<size_t>(largestBytes));
    auto fromFile = MCSmacker::Open(largest);
    auto fromMemory = MCSmacker::OpenMemory(std::move(bytes));
    REQUIRE(fromFile.has_value());
    REQUIRE(fromMemory.has_value());
    MCSmacker& file = **fromFile;
    MCSmacker& memory = **fromMemory;
    REQUIRE_EQ(file.FrameCount(), memory.FrameCount());
    size_t differing = 0;

    for (uint32_t frame = 0; frame < file.FrameCount(); ++frame)
    {
        const auto fileFrame = file.DecodeNextFrame();
        const auto memoryFrame = memory.DecodeNextFrame();
        REQUIRE(fileFrame.has_value());
        REQUIRE(memoryFrame.has_value());
        bool same = std::ranges::equal(file.Pixels(), memory.Pixels()) && file.Palette() == memory.Palette();

        for (int track = 0; track < MCSmacker::MaxTracks; ++track)
        {
            same = same && std::ranges::equal(file.Audio(track), memory.Audio(track));
        }

        differing += same ? 0 : 1;
    }

    CHECK_EQ(differing, size_t{0});
}
