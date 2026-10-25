#include "stdafx.h"
#include "MCTest.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"

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
