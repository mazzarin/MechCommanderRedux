#include "stdafx.h"
#include "MCHdMovieFixture.h"
#include "MCTest.h"
#include "TestGame.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSmacker.h"
#include "sprite/MCHdMovie.h"

// The HD art pack's movies (sprite/MCHdMovie): AV1 frames in IVF files, decoded by dav1d over the Smacker movie's.

namespace
{
    /// <summary>A folder of its own in the temporary one, empty while it lives and removed after.</summary>
    struct MCScratch
    {
        std::filesystem::path Path;

        explicit MCScratch(std::string_view name) : Path(std::filesystem::temp_directory_path() / name)
        {
            std::filesystem::remove_all(Path);
            std::filesystem::create_directories(Path);
        }

        ~MCScratch() { std::filesystem::remove_all(Path); }
    };

    /// <summary>The fixture's original: half its size, in a buffer wider than a row.</summary>
    constexpr int32_t OriginalWidth = MCHdMovieFixture::Width / 2;
    constexpr int32_t OriginalHeight = MCHdMovieFixture::Height / 2;
    constexpr int32_t OriginalPitch = OriginalWidth + 4;
    constexpr size_t RgbaBytes = 4;
    constexpr uint8_t Opaque = 255;

    std::vector<uint8_t> Original()
    {
        std::vector<uint8_t> pixels(static_cast<size_t>(OriginalPitch) * OriginalHeight);

        for (size_t i = 0; i < pixels.size(); ++i)
        {
            pixels[i] = static_cast<uint8_t>(i * 7 + 1);
        }

        return pixels;
    }

    std::expected<std::unique_ptr<MCHdMovie>, std::string> OpenBytes(const std::filesystem::path& path,
                                                                     std::span<const uint8_t> bytes)
    {
        MCHdMovieFixture::Write(path, bytes);
        return MCHdMovie::Open(path);
    }
}

/// <summary>
/// The fixture's three frames come out in order as frames the GPU takes: their size, the scale over a half-size
/// original, opaque RGBA in the frame's colour (red, green, blue), the original's indices, and a number of their own;
/// then the movie has no more.
/// </summary>
TEST_CASE("hd art: an HD movie's frames decode in order, in their colours, over the original's indices")
{
    const MCScratch scratch("mc_tests_hd_movie");
    auto opened = OpenBytes(scratch.Path / "MOVIE.ivf", MCHdMovieFixture::Bytes());
    REQUIRE(opened.has_value());
    MCHdMovie& movie = **opened;
    CHECK_EQ(movie.Width(), MCHdMovieFixture::Width);
    CHECK_EQ(movie.Height(), MCHdMovieFixture::Height);
    CHECK_EQ(movie.Frames(), MCHdMovieFixture::Frames);

    CHECK_EQ(movie.ScaleOver(OriginalWidth, OriginalHeight), std::optional(2));
    CHECK_EQ(movie.ScaleOver(MCHdMovieFixture::Width, MCHdMovieFixture::Height), std::optional(1));
    CHECK(!movie.ScaleOver(OriginalWidth - 1, OriginalHeight).has_value());
    CHECK(!movie.ScaleOver(OriginalWidth, MCHdMovieFixture::Height / 3).has_value());
    CHECK(!movie.ScaleOver(0, 0).has_value());

    const std::vector<uint8_t> original = Original();
    uint64_t lastId = 0;

    for (uint32_t index = 0; index < MCHdMovieFixture::Frames; ++index)
    {
        MCTest::Scope scope(std::format("frame {}", index));
        const auto next = movie.Next(original, OriginalWidth, OriginalHeight, OriginalPitch);
        REQUIRE(next.has_value());
        const MCHdArt::Frame& frame = **next;
        CHECK_EQ(frame.Width, MCHdMovieFixture::Width);
        CHECK_EQ(frame.Height, MCHdMovieFixture::Height);
        CHECK_EQ(frame.Scale, 2);
        CHECK(frame.Format == MCHdArt::ColorFormat::Rgba8);
        CHECK(!frame.Blended);
        CHECK(frame.Id > lastId);
        lastId = frame.Id;
        REQUIRE_EQ(frame.Colors.size(),
                   static_cast<size_t>(MCHdMovieFixture::Width) * MCHdMovieFixture::Height * RgbaBytes);
        size_t off = 0;

        for (size_t at = 0; at < frame.Colors.size(); at += RgbaBytes)
        {
            bool near = frame.Colors[at + 3] == Opaque;

            for (size_t channel = 0; channel < 3; ++channel)
            {
                near = near && std::abs(frame.Colors[at + channel] - MCHdMovieFixture::Colors[index][channel]) <=
                                   MCHdMovieFixture::Tolerance;
            }

            off += near ? 0 : 1;
        }

        CHECK_EQ(off, size_t{0});
        REQUIRE_EQ(frame.Indices.size(), static_cast<size_t>(OriginalWidth) * OriginalHeight * MCHdArt::IndexBytes);
        size_t wrong = 0;

        for (int32_t y = 0; y < OriginalHeight; ++y)
        {
            for (int32_t x = 0; x < OriginalWidth; ++x)
            {
                const size_t at = (static_cast<size_t>(y) * OriginalWidth + x) * MCHdArt::IndexBytes;
                const uint8_t index = original[static_cast<size_t>(y) * OriginalPitch + x];
                wrong += frame.Indices[at] == index && frame.Indices[at + 1] == MCHdArt::HasIndex ? 0 : 1;
            }
        }

        CHECK_EQ(wrong, size_t{0});
    }

    const auto past = movie.Next(original, OriginalWidth, OriginalHeight, OriginalPitch);
    REQUIRE(!past.has_value());
    CHECK(past.error().contains("no more frames"));
}

/// <summary>
/// The pack check counts a movie's frames to its end; a file that isn't IVF, or holds another codec, isn't opened; one
/// cut short fails where it ends; and a frame isn't made over an original it doesn't scale to, or given short. Each
/// says why, naming the file.
/// </summary>
TEST_CASE("hd art: an HD movie is counted to its end, and a broken one is refused with its reason")
{
    const MCScratch scratch("mc_tests_hd_movie_broken");
    const std::vector<uint8_t> bytes = MCHdMovieFixture::Bytes();

    {
        auto opened = OpenBytes(scratch.Path / "WHOLE.ivf", bytes);
        REQUIRE(opened.has_value());
        const auto counted = (*opened)->Validate();
        REQUIRE(counted.has_value());
        CHECK_EQ(*counted, MCHdMovieFixture::Frames);
    }

    {
        std::vector<uint8_t> riff = bytes;
        std::ranges::copy(std::string_view("RIFF"), riff.begin());
        const auto opened = OpenBytes(scratch.Path / "RIFF.ivf", riff);
        REQUIRE(!opened.has_value());
        CHECK(opened.error().contains("RIFF.ivf isn't an IVF file"));
    }

    {
        std::vector<uint8_t> vp9 = bytes;
        constexpr size_t CodecAt = 8;
        std::ranges::copy(std::string_view("VP90"), vp9.begin() + CodecAt);
        const auto opened = OpenBytes(scratch.Path / "VP9.ivf", vp9);
        REQUIRE(!opened.has_value());
        CHECK(opened.error().contains("doesn't hold AV1"));
    }

    {
        // The header, the first frame's size and timestamp, and a few of its bytes.
        constexpr size_t Kept = 32 + 12 + 10;
        auto opened = OpenBytes(scratch.Path / "SHORT.ivf", std::span(bytes).first(Kept));
        REQUIRE(opened.has_value());
        const auto counted = (*opened)->Validate();
        REQUIRE(!counted.has_value());
        CHECK(counted.error().contains("SHORT.ivf is cut short"));
    }

    {
        auto opened = OpenBytes(scratch.Path / "OVER.ivf", bytes);
        REQUIRE(opened.has_value());
        const std::vector<uint8_t> original = Original();
        const auto unscaled = (*opened)->Next(original, OriginalWidth - 2, OriginalHeight, OriginalPitch);
        REQUIRE(!unscaled.has_value());
        CHECK(unscaled.error().contains("aren't a 14x12 movie's scaled by a whole number"));
        const auto shortGiven =
            (*opened)->Next(std::span(original).first(OriginalPitch), OriginalWidth, OriginalHeight, OriginalPitch);
        REQUIRE(!shortGiven.has_value());
        CHECK(shortGiven.error().contains("is given in"));
    }
}

/// <summary>
/// Tool: <c>--hd-movie-pack &lt;pack folder&gt;</c> checks every HD movie in the pack's <c>movies</c> folder against
/// the game's movie of its name, as the game would show it: its frames the movie's size scaled by a whole number, as
/// many as the movie's, and every one decoding.
/// </summary>
TEST_CASE("tool: --hd-movie-pack verifies every HD movie of a pack against the game's movie")
{
    const std::optional<std::string_view> pack = MCTest::Option("hd-movie-pack");

    if (!pack.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path folder = std::filesystem::path(*pack) / "movies";
    REQUIRE(std::filesystem::is_directory(folder));
    size_t movies = 0;
    uint64_t frames = 0;

    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != ".ivf")
        {
            continue;
        }

        const std::string name = entry.path().stem().string();
        MCTest::Scope scope(name);
        auto smacker = MCSmacker::Open(MCFileSystem::Resolve(std::format("data\\movies\\{}.smk", name)));
        REQUIRE(smacker.has_value());
        auto movie = MCHdMovie::Open(entry.path());
        REQUIRE(movie.has_value());
        const MCSmacker& original = **smacker;
        const std::optional<int32_t> scale = (*movie)->ScaleOver(original.Width(), original.Height());
        CHECK(scale.has_value());
        CHECK_EQ((*movie)->Frames(), original.FrameCount());
        const auto counted = (*movie)->Validate();
        {
            const MCTest::Scope why(counted.error_or(""));
            CHECK(counted.has_value());
        }

        CHECK_EQ(counted.value_or(0), original.FrameCount());
        std::cout << std::format("  {}: {} frames of {}x{} ({}x the movie's)\n", name, counted.value_or(0),
                                 (*movie)->Width(), (*movie)->Height(), scale.value_or(0));
        ++movies;
        frames += counted.value_or(0);
    }

    std::cout << std::format("  verified {} HD movies, {} frames\n", movies, frames);
    CHECK(movies > 0);
}
